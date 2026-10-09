# SPDX-License-Identifier: Apache-2.0
"""Owned OBJ import, verified repair and atomic bundle/receipt publication."""
from array import array
from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import sys
import meshvale_interchange as io
from meshvale_geometry import Mesh
from meshvale_reports import profile_outcome, exit_code, validate
from . import __version__
from .workflow import repair_duplicate_mesh, _request, _snapshot, _scope, _check, _diagnostics


@dataclass(frozen=True)
class ObjWorkflowResult:
    source: io.ObjFileAsset | None
    candidate: io.ObjFileAsset | None
    report: dict
    exit_code: int
    entry: Path | None


def _json(value):
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("ascii")


def _metadata(asset):
    doc = asset.document
    return {"obj_path": asset.obj_path.as_posix(), "libraries": [p.as_posix() for p in asset.material_libraries],
            "parts": [{"object": p.object, "groups": list(p.groups)} for p in doc.parts],
            "material_names": list(doc.material_names)}


def _content_hash(asset):
    digest = hashlib.sha256(b"meshvale.obj-snapshot/1\0")
    def frame(value):
        digest.update(len(value).to_bytes(8, "little")); digest.update(value)
    def buffer(value):
        if value is None:
            frame(b"\0"); return
        payload = value.tobytes()
        if sys.byteorder != "little" and value.itemsize > 1:
            scalars = array(value.format); scalars.frombytes(payload); scalars.byteswap(); payload = scalars.tobytes()
        frame(b"\1" + payload)
    frame(_json(_metadata(asset)))
    frame(asset.document.material_library)
    record = asset.document.mesh.to_record()
    for key in ("positions", "face_offsets", "corner_vertices"):
        buffer(record[key])
    for channel in record["attributes"]:
        frame(_json({k: v for k, v in channel.items() if k not in ("values", "offsets", "present")}))
        for key in ("values", "offsets", "present"):
            buffer(channel[key])
    for resource in sorted(asset.resources, key=lambda r: r.path.as_posix().encode("utf-8", "surrogateescape")):
        frame(_json(resource.path.as_posix())); frame(resource.bytes)
    return digest.hexdigest()


def _empty_report(input_id, targets, declarations, report_path):
    required = ["input.storage", "input.unchanged", "candidate.storage", "repair.correspondence", "repair.preservation",
                "asset.metadata-resources", "export.verified-reload"]
    report = {"schema": "meshvale.report/1", "tool": {"name": "meshvale-repair", "version": __version__},
        "input": {"id": input_id, "sha256": None},
        "operation": {"kind": "repair", "name": "remove-duplicate-faces", "version": "1",
                      "options": {"row_local_attributes": [list(pair) for pair in declarations]}, "targets": []},
        "execution": {"outcome": "completed", "stage": "complete", "reason": None},
        "candidate": {"outcome": "not-attempted", "snapshot": None},
        "profile": {"name": "obj-exact-duplicate-publication", "version": "1", "required_checks": required, "outcome": "incomplete"},
        "snapshots": [], "diagnostics": [], "coverage": [], "maps": [], "preservation": [],
        "publication": {"requested": True, "outcome": "not-attempted", "snapshot": None, "artifacts": [],
                        "verified": False, "reason": "Publication not reached."},
        "extensions": {"meshvale.repair": {"version": "1", "target_pairs": [list(pair) for pair in targets]},
                       "meshvale.repair.obj": {"version": "1", "input_hash_convention": "meshvale.obj-snapshot/1",
                           "adapter_version": io.__version__, "report_path": report_path, "artifacts": []}}}
    for name in required:
        _check(report, name, None, status="skipped", reason="Stage not reached.")
    return report


def _failure(report, stage, reason, *, cancelled=False):
    report["execution"] = {"outcome": "cancelled" if cancelled else "failed", "stage": stage, "reason": reason}
    if stage in ("export", "report"):
        report["publication"] = {"requested": True, "outcome": "cancelled" if cancelled else "failed",
                                 "snapshot": None, "artifacts": [], "verified": False, "reason": reason}


def _finish(source, candidate, report, entry, sink):
    report["profile"]["outcome"] = profile_outcome(report)
    if sink is not None:
        try:
            sink(deepcopy(report))
        except Exception as error:
            # Caller-owned report delivery has an explicit failure contract.
            reason = "External report sink failed: " + type(error).__name__
            report["extensions"]["meshvale.repair.obj"]["report_delivery"] = {"outcome": "failed", "reason": reason}
            if report["execution"]["outcome"] != "cancelled":
                report["execution"] = {"outcome": "failed", "stage": "report", "reason": reason}
    return ObjWorkflowResult(source, candidate, report, exit_code(report), entry)


def _output_report(report, candidate, reloaded, files):
    receipt = deepcopy(report)
    receipt["snapshots"].append(_snapshot(reloaded.document.mesh, "output"))
    before, after = candidate.document.mesh, reloaded.document.mesh
    if (before.vertex_count, before.face_count, before.corner_count) != (after.vertex_count, after.face_count, after.corner_count):
        raise ValueError("verified adapter changed mesh row counts")
    for domain in ("vertex", "face", "corner"):
        receipt["maps"].append({"id": "export-" + domain, "domain": domain, "kind": "identity", "entries": None,
                               "from": {"snapshot": "candidate", "mesh": "surface"}, "to": {"snapshot": "output", "mesh": "surface"}})
    _check(receipt, "export.verified-reload", "output", finding="passed")
    artifacts = [{"id": "artifact-" + str(i), "sha256": hashlib.sha256(file.bytes).hexdigest()} for i, file in enumerate(files)]
    receipt["extensions"]["meshvale.repair.obj"]["artifacts"] = [
        {"id": artifact["id"], "path": file.path.as_posix()} for artifact, file in zip(artifacts, files)]
    receipt["publication"] = {"requested": True, "outcome": "published", "snapshot": "output",
                              "artifacts": artifacts, "verified": True, "reason": None}
    # The adapter owns checked semantic/numerical conversion; claims must name it.
    source_record, output_record = before.to_record(), after.to_record()
    exact = all(source_record[k].tobytes() == output_record[k].tobytes() for k in ("positions", "face_offsets", "corner_vertices"))
    receipt["preservation"].append({"feature": {"kind": "geometry", "name": "OBJ polygon serialization", "domain": None, "set_index": None},
        "from": _scope("candidate", "OBJ semantics"), "to": _scope("output", "OBJ semantics"), "outcome": "preserved" if exact else "converted",
        "reason": None if exact else "Verified ordered loops and adapter numerical round-trip bound; serialized storage differs.",
        "maps": ["export-vertex", "export-face", "export-corner"]})
    output_channels = {(r["domain"], r["name"]): r for r in output_record["attributes"]}
    for row in source_record["attributes"]:
        channel = output_channels.get((row["domain"], row["name"]))
        exact = channel is not None and all(row[k] == channel[k] for k in row if k not in ("values", "offsets", "present"))
        if exact:
            exact = all((row[k] is None and channel[k] is None) or (row[k] is not None and channel[k] is not None
                         and row[k].tobytes() == channel[k].tobytes()) for k in ("values", "offsets", "present"))
        receipt["preservation"].append({"feature": {"kind": "attribute", "name": row["name"], "domain": row["domain"], "set_index": row["set_index"]},
            "from": _scope("candidate", "OBJ channel semantics"), "to": _scope("output", "OBJ channel semantics"),
            "outcome": "preserved" if exact else "converted", "reason": None if exact else "Verified OBJ authored values/missingness and binding semantics; pool/part indices or float storage differ.",
            "maps": ["export-" + row["domain"]]})
    for kind, name in (("resource", "referenced resources"), ("scene", "OBJ material and object/group bindings")):
        receipt["preservation"].append({"feature": {"kind": kind, "name": name, "domain": None, "set_index": None},
            "from": {"snapshot": "candidate", "mesh": None, "subject": name}, "to": {"snapshot": "output", "mesh": None, "subject": name},
            "outcome": "preserved", "reason": None, "maps": []})
    receipt["profile"]["outcome"] = profile_outcome(receipt)
    validate(receipt)
    return receipt


def repair_obj_file(input, destination, targets, *, input_id="source", resource_root=None, row_local_attributes=(),
                    cancellation=None, on_phase=None, report_path="meshvale-report.json", report_sink=None):
    """Publish a verified accepted duplicate repair and its receipt in one new bundle."""
    targets, declarations = _request(Mesh(), targets, row_local_attributes, input_id, None)
    for path in (input, destination, report_path) + (() if resource_root is None else (resource_root,)):
        if "\0" in os.fsdecode(os.fspath(path)):
            raise ValueError("path contains NUL")
    receipt_path = Path(os.fsdecode(os.fspath(report_path)))
    receipt_name = receipt_path.as_posix()
    if (not receipt_name or receipt_name == "." or receipt_name.startswith("/") or receipt_path.is_absolute() or
            ":" in receipt_name or "\\" in receipt_name or ".." in receipt_path.parts):
        raise ValueError("report_path must be bundle-relative without parent traversal")
    if cancellation is not None and not isinstance(cancellation, io.Cancellation):
        raise TypeError("cancellation must be an Interchange Cancellation token")
    if on_phase is not None and not callable(on_phase) or report_sink is not None and not callable(report_sink):
        raise TypeError("callbacks must be callable or None")
    report = _empty_report(input_id, targets, declarations, receipt_name)
    source = candidate = None
    try:
        imported = io.read_obj_file(input, resource_root=resource_root, cancellation=cancellation)
        source = imported.asset
    except (MemoryError, OSError) as error:
        _failure(report, "import", "Import failed: " + type(error).__name__)
        return _finish(source, candidate, report, None, report_sink)
    if source is None:
        _diagnostics(report, imported.diagnostics, None, "error")
        cancelled = any(d["code"] == "obj.cancelled" for d in imported.diagnostics)
        _failure(report, "import", "OBJ import cancelled." if cancelled else "OBJ import failed.", cancelled=cancelled)
        return _finish(source, candidate, report, None, report_sink)
    try:
        workflow = repair_duplicate_mesh(source.document.mesh, targets, input_id=input_id, row_local_attributes=declarations,
                                         cancelled=None if cancellation is None else lambda: cancellation.stop_requested)
        report = workflow.report
        report["input"]["sha256"] = _content_hash(source)
        report["extensions"]["meshvale.repair.obj"] = {"version": "1", "input_hash_convention": "meshvale.obj-snapshot/1",
            "adapter_version": io.__version__, "report_path": receipt_name, "artifacts": []}
        report["profile"]["name"] = "obj-exact-duplicate-publication"
        report["profile"]["required_checks"] += ["asset.metadata-resources", "export.verified-reload"]
        report["publication"] = {"requested": True, "outcome": "not-attempted", "snapshot": None, "artifacts": [],
                                 "verified": False, "reason": "Publication not reached."}
        for name in ("asset.metadata-resources", "export.verified-reload"):
            _check(report, name, None, status="skipped", reason="Stage not reached.")
        _diagnostics(report, imported.diagnostics, "input", "warning")
        if workflow.candidate is None:
            return _finish(source, candidate, report, None, report_sink)
        candidate = source.with_mesh(workflow.candidate)
        equal = (_metadata(candidate) == _metadata(source) and candidate.document.material_library == source.document.material_library
                 and candidate.resources == source.resources)
        _check(report, "asset.metadata-resources", "candidate", finding="passed" if equal else "failed")
        if not equal:
            report["candidate"]["outcome"] = "rejected"
            return _finish(source, None, report, None, report_sink)
    except MemoryError:
        _failure(report, "verify", "Allocation failed during asset verification.")
        return _finish(source, candidate, report, None, report_sink)
    prepared = success_result = None
    preparation_error = None
    def receipt(reloaded, files):
        nonlocal prepared, success_result, preparation_error
        try:
            prepared = _output_report(report, candidate, reloaded, files)
            payload = _json(prepared) + b"\n"
            success_result = ObjWorkflowResult(source, candidate, prepared, exit_code(prepared), candidate.obj_path)
            return [io.ObjResource(receipt_path, payload)]
        except Exception as error:
            preparation_error = error
            raise
    try:
        published = io.publish_obj_bundle(candidate, destination, cancellation=cancellation, on_phase=on_phase, on_verified=receipt)
    except (MemoryError, OSError) as error:
        _failure(report, "export", "Publication failed: " + type(error).__name__)
        return _finish(source, candidate, report, None, report_sink)
    if published.outcome == "published":
        # Receipt, wrapper result and native return storage were allocated before commit.
        return success_result if report_sink is None else _finish(source, candidate, prepared, published.entry, report_sink)
    if preparation_error is not None and not isinstance(preparation_error, (MemoryError, OSError)):
        raise preparation_error
    _diagnostics(report, published.diagnostics, "candidate", "error")
    receipt_failed = (prepared is not None and published.phase == "verification" and
                      any(d["code"] in ("obj.resource_collision", "obj.resource_path") or
                          d["subject"] == receipt_name for d in published.diagnostics))
    stage = "report" if preparation_error is not None or receipt_failed else "export"
    _failure(report, stage, "Bundle receipt failed." if stage == "report" else "OBJ publication did not commit.",
             cancelled=published.outcome == "cancelled")
    return _finish(source, candidate, report, None, report_sink)
