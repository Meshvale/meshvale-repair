# SPDX-License-Identifier: Apache-2.0
"""Explicit own-process command qualification with the actual compiled test variant.

This script is not part of unittest discovery or a production import path. Load
only the real isolated observer extension, bind that module at the package's
native import slot before importing installed pure Python commands, and prove
that no production _repair extension was loaded. No native result is fabricated.
The subprocess alone owns this temporary module mapping and allocator observer.
"""
import argparse
from copy import deepcopy
import gc
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gltf_command_fixture import (BIN_KIND, COMMAND_EXTENSION, ENTRY, IMAGE,
    JSON_SOURCE, NON_JSON, assemble_glb, canonical, job, request)
sys.path.pop(0)

TRIANGLES = 2048


def make_source(root, glb):
    root.mkdir()
    binary = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0) + struct.pack("<3H", 0, 1, 2) * TRIANGLES
    document = json.loads(JSON_SOURCE)
    document["buffers"][0]["byteLength"] = len(binary)
    document["bufferViews"][1]["byteLength"] = 6 * TRIANGLES
    document["accessors"][1]["count"] = 3 * TRIANGLES
    if glb:
        document["buffers"] = [{"byteLength": len(binary) + 1}, {"uri": "mesh.bin", "byteLength": len(binary)}]
        padded = binary + b"\xd3"
        padded += b"\0" * (-len(padded) % 4)
        chunks = ((BIN_KIND, padded), *NON_JSON[1:])
        entry = assemble_glb(canonical(document), chunks)
    else:
        entry = canonical(document)
    (root / ENTRY).write_bytes(entry)
    (root / "mesh.bin").write_bytes(binary + b"\xde\xad\xbe\xef")
    (root / "texture.bin").write_bytes(IMAGE)
    return root / ENTRY


def checked(report, reports, schema):
    reports.validate(report)
    schema.validate(report)
    assert report["candidate"]["outcome"] in ("accepted", "unchanged")
    assert report["execution"]["outcome"] == "cancelled" and reports.exit_code(report) == 130
    assert report["profile"]["outcome"] == "incomplete"
    assert report["profile"]["required_checks"] == ["native-receipt-validation"]
    coverage, = report["coverage"]
    assert coverage["status"] == "skipped" and coverage["finding"] is None and coverage["reason"]
    assert report["input"]["sha256"] is None
    assert report["preservation"] == [] and report["publication"]["artifacts"] == []
    assert report["publication"]["outcome"] == "not-attempted"
    extension = report["extensions"][COMMAND_EXTENSION]
    assert extension["origin"] == "command-workflow-fallback" and extension["native_report_available"] is False
    assert extension["workflow_started"] is True and extension["publication"] is None
    assert extension["capture"]["status"] == {"domain": "GltfFileStatus", "member": "kSuccess"}
    assert extension["workflow"]["status"] == {"domain": "GltfWorkflowStatus", "member": "kCanceled"}
    assert extension["workflow"]["diagnostic"]["memory"]["stage"] == {"domain": "GltfWorkflowStage", "member": "kReport"}
    assert extension["memory_outcomes"]["profile"] == {"domain": "GltfWorkflowProfileOutcome", "member": "kPassed"}
    snapshots = {value["role"]: value for value in report["snapshots"]}
    changed = report["candidate"]["outcome"] == "accepted"
    for role, triangles in (("input", TRIANGLES), ("candidate", TRIANGLES - int(changed))):
        mesh, = snapshots[role]["meshes"]
        assert tuple(mesh[name] for name in ("vertex_count", "face_count", "corner_count")) == (3, triangles, 3 * triangles)
    maps = {value["domain"]: value for value in report["maps"]}
    assert maps["vertex"]["kind"] == "identity" and maps["vertex"]["entries"] is None
    if changed:
        assert maps["face"]["entries"] == [0 if i < 2 else i - 1 for i in range(TRIANGLES)]
        assert maps["corner"]["entries"] == [i if i < 3 else i % 3 if i < 6 else i - 3 for i in range(3 * TRIANGLES)]
    else:
        assert all(m["kind"] == "identity" and m["entries"] is None for m in maps.values())
    return extension


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("output", type=Path)
    options = parser.parse_args()
    assert "meshvale_repair" not in sys.modules and "meshvale_repair._repair" not in sys.modules
    module_path = options.module.resolve(strict=True)
    output = options.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("_repair_gltf_return_test", module_path)
    observer = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = observer
    spec.loader.exec_module(observer)
    sys.modules["meshvale_repair._repair"] = observer
    import meshvale_repair
    import meshvale_reports as reports
    import jsonschema
    from meshvale_repair import gltf_commands as commands, gltf_workflow as workflow
    assert meshvale_repair._repair is observer and workflow.GltfExecution is observer.GltfExecution
    assert Path(observer.__file__).resolve() == module_path
    schema = jsonschema.Draft202012Validator(reports.schema_document())
    original_run = workflow._run_gltf_request
    setup, finish, restore = observer._observer_setup, observer._observer_finish, observer._observer_restore
    rows, statistics = [], []

    def canceled_native(prepared):
        setup("report-canceled")
        try:
            result = original_run(prepared)
            stats = finish(result)
        finally:
            restore()
        assert result.report_json is None and result.publication is None
        assert result.input_snapshot_counts == (3, TRIANGLES, 3 * TRIANGLES)
        assert stats["restored"] and stats["report_stop_requested"] and stats["report_workers_launched"] >= 2
        assert stats["report_workers_entered"] == stats["report_workers_joined"] == stats["report_workers_launched"]
        assert stats["cpp_requests"] == 0 and tuple(stats["python_requests"]) == (0, 0, 0)
        statistics.append(stats)
        return result

    for glb in (False, True):
        for changed in (False, True):
            name = ("glb" if glb else "json") + ("-changed" if changed else "-noop")
            entry = make_source(output / (name + "-source"), glb)
            for batch in (False, True):
                session = observer.GltfExecution()
                identity = name + ("-batch" if batch else "-single")
                row = job(identity, source=str(entry), destination=str(output / (identity + "-unpublished")), changed=changed)
                with patch.object(workflow, "_run_gltf_request", new=canceled_native):
                    result = (commands.run_gltf_jobs(request(row), execution=session) if batch else
                              commands._run_gltf_single(row, execution=session))
                wire = result.report_json
                whole = reports.load(wire)
                report = whole["jobs"][0]["report"] if batch else whole
                extension = checked(report, reports, schema)
                assert result.exit_code == 130 and not Path(row["destination"]).exists()
                if batch:
                    assert whole["jobs"][0]["outcome"] == "attempted" and whole["execution"]["outcome"] == "cancelled"
                assert session.active_tracked_payload == 0 and session.active_workers == 0
                gc.collect()
                assert observer._observer_live_blocks() == 0
                # Independent semantic corruption controls on actual detached
                # fallback documents; valid JSON shape alone is insufficient.
                for corruption in ("missing-map", "wrong-count", "invented-profile", "invented-publication"):
                    bad = deepcopy(report)
                    if corruption == "missing-map":
                        bad["maps"] = [m for m in bad["maps"] if m["domain"] != "corner"]
                    elif corruption == "wrong-count":
                        next(s for s in bad["snapshots"] if s["role"] == "candidate")["meshes"][0]["face_count"] = 0
                    elif corruption == "invented-profile":
                        bad["profile"]["outcome"] = "passed"
                    else:
                        bad["publication"]["outcome"] = "published"
                    try:
                        reports.validate(bad)
                    except reports.ReportError:
                        pass
                    else:
                        raise AssertionError("Actual fallback corruption accepted: " + corruption)
                rows.append({"glb": glb, "changed": changed, "batch": batch,
                             "sha256": hashlib.sha256(wire).hexdigest(), "wire_bytes": len(wire),
                             "input_counts": [3, TRIANGLES, 3 * TRIANGLES],
                             "candidate_counts": [3, TRIANGLES - int(changed), 3 * (TRIANGLES - int(changed))],
                             "exit": result.exit_code, "native_profile": extension["memory_outcomes"]["profile"],
                             "active_after_detach": session.active_tracked_payload})
                (output / (identity + ".json")).write_bytes(wire)
    assert len(rows) == len(statistics) == 8
    loaded_native = {name: str(getattr(module, "__file__", "")) for name, module in sys.modules.items()
                     if name == "meshvale_repair._repair" or "_repair_gltf_return_test" in name}
    assert all(Path(path).resolve() == module_path for path in loaded_native.values())
    assert not any(Path(str(getattr(module, "__file__", ""))).name.startswith("_repair.")
                   for module in sys.modules.values() if getattr(module, "__file__", None))
    (output / "receipt.json").write_text(json.dumps({
        "scope": "Explicit own-process actual compiled testvariant + installed pure Python command projection/scheduling only; separate production wheel tests remain required. Only this child module import slot is redirected; no fake native facts or production-extension execution.",
        "observer_module": str(module_path), "observer_sha256": hashlib.sha256(module_path.read_bytes()).hexdigest(),
        "native_import_mapping": loaded_native, "production_extension_loaded": False,
        "rows": rows, "native_statistics": statistics, "semantic_corruptions_rejected": 32,
        "complete": True}, indent=2) + "\n", encoding="utf-8")
    print("Actual compiled command fallbacks: 8 single/batch JSON/GLB cases and 32 semantic corruption controls passed")


if __name__ == "__main__":
    main()
