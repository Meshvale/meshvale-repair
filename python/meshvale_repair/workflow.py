# SPDX-License-Identifier: Apache-2.0
"""Actual duplicate operations with independent preservation predicates and reports."""
from dataclasses import dataclass
import math
import re
from meshvale_geometry import Mesh
from meshvale_reports import exit_code, profile_outcome
from . import __version__, remove_duplicate_faces


@dataclass(frozen=True)
class WorkflowResult:
    candidate: Mesh | None
    report: dict
    exit_code: int


def _pairs(values):
    result = []
    for value in values:
        if not isinstance(value, (tuple, list)) or len(value) != 2:
            raise ValueError("request items must be pairs")
        result.append(tuple(value))
    return result


def _request(mesh, targets, declarations, input_id, cancelled):
    if not isinstance(mesh, Mesh):
        raise TypeError("mesh must be a meshvale_geometry.Mesh snapshot")
    if not isinstance(input_id, str) or len(input_id) > 256 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.:-]*", input_id):
        raise ValueError("input_id must be an opaque report identifier")
    if cancelled is not None and not callable(cancelled):
        raise TypeError("cancelled must be a callable or None")
    targets, declarations = _pairs(targets), _pairs(declarations)
    for pair in targets:
        for index in pair:
            if not isinstance(index, int) or isinstance(index, bool):
                raise TypeError("face indices must be integers, not booleans")
            if not 0 <= index < 2**64:
                raise OverflowError("face index outside uint64 range")
    for domain, name in declarations:
        if not isinstance(domain, str) or not isinstance(name, str):
            raise TypeError("row-local declarations require strings")
        if domain not in ("vertex", "face", "corner"):
            raise ValueError("row-local domain must be vertex, face or corner")
    return [(int(a), int(b)) for a, b in targets], declarations


def _fingerprint(record):
    def buffer(value):
        return None if value is None else value.tobytes()
    return (record["schema"], *(buffer(record[k]) for k in ("positions", "face_offsets", "corner_vertices")),
            tuple((tuple((k, row[k]) for k in ("domain", "name", "semantic", "set_index", "components", "scalar_type")),
                   tuple(sorted(row["metadata"].items())), *(buffer(row[k]) for k in ("values", "offsets", "present")))
                  for row in record["attributes"]))


def _scope(snapshot, subject):
    return {"snapshot": snapshot, "mesh": "surface" if snapshot else None, "subject": subject}


def _snapshot(mesh, role):
    return {"id": role, "role": role, "meshes": [{"id": "surface", "vertex_count": mesh.vertex_count,
            "face_count": mesh.face_count, "corner_count": mesh.corner_count}]}


def _check(report, name, snapshot, *, finding=None, status="performed", reason=None, diagnostics=()):
    row = {"id": name, "check": name.split(".", 1)[-1], "version": "1", "scope": _scope(snapshot, name),
           "status": status, "finding": finding, "reason": reason, "diagnostics": list(diagnostics)}
    report["coverage"] = [r for r in report["coverage"] if r["id"] != name] + [row]


def _diagnostics(report, rows, snapshot, severity):
    ids = []
    for row in rows:
        identity = "diagnostic-" + str(len(report["diagnostics"]))
        report["diagnostics"].append({"id": identity, "code": row["code"], "severity": severity,
            "message": row["code"], "scope": _scope(snapshot, row["subject"]), "element": None,
            "data": {"native_subject": row["subject"], "native_element": row["element"]}})
        ids.append(identity)
    return ids


def _finite_attributes(record):
    diagnostics = []
    for channel in record["attributes"]:
        if channel["scalar_type"] not in ("float32", "float64"):
            continue
        values, offsets, present = (channel[key] for key in ("values", "offsets", "present"))
        rows = len(offsets) - 1 if offsets is not None else len(values) // channel["components"]
        for row in range(rows):
            if present is not None and not present[row]:
                continue
            start, end = (row * channel["components"], (row + 1) * channel["components"]) if offsets is None else offsets[row:row+2]
            if any(not math.isfinite(value) for value in values[start:end]):
                diagnostics.append({"code": "repair.nonfinite_attribute", "subject": channel["name"], "element": row})
    return diagnostics


def _inspect(report, mesh, role):
    storage = mesh.inspect_storage()
    if not storage:
        # Row access is safe only after structural inspection. Missing backing
        # payload remains raw data; only authored floating rows must be finite.
        storage.extend(_finite_attributes(mesh.to_record()))
    ids = _diagnostics(report, storage, role, "error")
    _check(report, role + ".storage", role, finding="failed" if storage else "passed", diagnostics=ids)
    topology = mesh.inspect_topology()
    report["extensions"]["meshvale.repair"]["topology"][role] = topology
    ids = _diagnostics(report, [r for r in topology["diagnostics"] if r not in storage], role, "warning")
    observed = topology["topology"] is not None
    _check(report, role + ".topology.observed", role, finding="passed" if observed else None,
           status="performed" if observed else "skipped", reason=None if observed else "Unsafe topology storage.", diagnostics=ids)
    for check in topology["checks"]:
        if check["status"] == "unsupported":
            _check(report, role + "." + check["name"], role, status="unsupported", reason=check["reason"])
    return not storage


def _correspondence(source, destination, face_map, corner_map, targets):
    old_offsets, new_offsets = source["face_offsets"], destination["face_offsets"]
    old_count, new_count = len(old_offsets) - 1, len(new_offsets) - 1
    if len(face_map) != old_count or len(corner_map) != len(source["corner_vertices"]):
        return False
    if any(type(i) is not int or not 0 <= i < new_count for i in face_map):
        return False
    if any(type(i) is not int or not 0 <= i < len(destination["corner_vertices"]) for i in corner_map):
        return False
    removed = dict((remove, keep) for keep, remove in targets)
    if (len(removed) != len(targets) or any(not 0 <= i < old_count for pair in targets for i in pair)
            or any(keep in removed for keep in removed.values())):
        return False
    retained = [i for i in range(old_count) if i not in removed]
    if new_count != len(retained):
        return False
    rows = dict((old, new) for new, old in enumerate(retained))
    for face in range(old_count):
        expected = rows[removed.get(face, face)]
        if face_map[face] != expected:
            return False
        start, end = old_offsets[face:face+2]
        first, last = new_offsets[expected:expected+2]
        size = end - start
        if size != last - first or not size:
            return False
        rotation = corner_map[start] - first
        if not 0 <= rotation < size or (face not in removed and rotation != 0):
            return False
        for offset in range(size):
            mapped = first + (rotation + offset) % size
            if (corner_map[start+offset] != mapped or
                    source["corner_vertices"][start+offset] != destination["corner_vertices"][mapped]):
                return False
    return True


def _row(channel, index):
    offsets = channel["offsets"]
    start, end = (index * channel["components"], (index+1) * channel["components"]) if offsets is None else offsets[index:index+2]
    return channel["values"][start:end]


def _preserved(source, destination, face_map, corner_map, targets):
    if source["positions"].tobytes() != destination["positions"].tobytes():
        return False
    if len(source["attributes"]) != len(destination["attributes"]):
        return False
    removed = {remove for _, remove in targets}
    retained_faces = [i for i in range(len(face_map)) if i not in removed]
    retained_corners = {i for face in retained_faces for i in range(source["face_offsets"][face], source["face_offsets"][face+1])}
    mappings = {"vertex": range(len(source["positions"]) // 3), "face": face_map, "corner": corner_map}
    retained = {"vertex": set(mappings["vertex"]), "face": set(retained_faces), "corner": retained_corners}
    for old, new in zip(source["attributes"], destination["attributes"]):
        fields = ("domain", "name", "semantic", "set_index", "components", "scalar_type", "metadata")
        if any(old[k] != new[k] for k in fields):
            return False
        if any((old[k] is None) != (new[k] is None) for k in ("offsets", "present")):
            return False
        for index, mapped in enumerate(mappings[old["domain"]]):
            authored = old["present"] is None or bool(old["present"][index])
            if old["present"] is not None and old["present"][index] != new["present"][mapped]:
                return False
            before, after = _row(old, index), _row(new, mapped)
            if index in retained[old["domain"]]:
                if before.tobytes() != after.tobytes():
                    return False
            elif authored and list(before) != list(after):
                return False
    return True


def _maps_and_claims(report, source, result):
    for domain, entries in (("vertex", None), ("face", list(result.face_map)), ("corner", list(result.corner_map))):
        identity = entries is None or entries == list(range(len(entries)))
        report["maps"].append({"id": "repair-" + domain, "domain": domain,
            "from": {"snapshot": "input", "mesh": "surface"}, "to": {"snapshot": "candidate", "mesh": "surface"},
            "kind": "identity" if identity else "explicit", "entries": None if identity else entries})
    features = [{"kind": "geometry", "name": "positions-and-directed-polygons", "domain": None, "set_index": None}]
    features += [{"kind": "attribute", "name": row["name"], "domain": row["domain"], "set_index": row["set_index"]}
                 for row in source["attributes"]]
    for feature in features:
        ids = ["repair-" + domain for domain in ("vertex", "face", "corner") if feature["domain"] in (None, domain)]
        report["preservation"].append({"feature": feature, "from": _scope("input", "correspondence"),
            "to": _scope("candidate", "correspondence"), "outcome": "preserved", "reason": None, "maps": ids})


def repair_duplicate_mesh(mesh, targets, *, input_id="source", row_local_attributes=(), cancelled=None):
    """Inspect, perform native duplicate removal, verify independently, and report."""
    targets, declarations = _request(mesh, targets, row_local_attributes, input_id, cancelled)
    required = ["input.storage", "input.unchanged", "candidate.storage", "repair.correspondence", "repair.preservation"]
    report = {"schema": "meshvale.report/1", "tool": {"name": "meshvale-repair", "version": __version__},
        "input": {"id": input_id, "sha256": None},
        "operation": {"kind": "repair", "name": "remove-duplicate-faces", "version": "1",
            "options": {"row_local_attributes": [list(pair) for pair in declarations]},
            "targets": [{"snapshot": "input", "mesh": "surface", "domain": "face", "index": index} for pair in targets for index in pair]},
        "execution": {"outcome": "completed", "stage": "complete", "reason": None},
        "candidate": {"outcome": "not-attempted", "snapshot": None},
        "profile": {"name": "exact-duplicate-preservation", "version": "1", "required_checks": required, "outcome": "incomplete"},
        "snapshots": [_snapshot(mesh, "input")], "diagnostics": [], "coverage": [], "maps": [], "preservation": [],
        "publication": {"requested": False, "outcome": "not-requested", "snapshot": None, "artifacts": [], "verified": False, "reason": None},
        "extensions": {"meshvale.repair": {"version": "1", "target_pairs": [list(pair) for pair in targets], "native_outcome": None, "topology": {}}}}
    for name in required:
        _check(report, name, None, status="skipped", reason="Stage not reached.")

    def finish(candidate=None):
        report["profile"]["outcome"] = profile_outcome(report)
        return WorkflowResult(candidate, report, exit_code(report))

    def interrupted(stage):
        if cancelled is None:
            return False
        value = cancelled()
        if type(value) is not bool:
            raise TypeError("cancelled must return a bool")
        if value:
            report["execution"] = {"outcome": "cancelled", "stage": stage, "reason": "Cancellation requested."}
        return value

    stage = "inspect"
    if interrupted(stage):
        return finish()
    try:
        source = mesh.to_record()
        before = _fingerprint(source)
        safe = _inspect(report, mesh, "input")
    except MemoryError:
        report["execution"] = {"outcome": "failed", "stage": stage, "reason": "Allocation failed."}
        return finish()
    if not safe:
        report["execution"] = {"outcome": "failed", "stage": stage, "reason": "Input storage failed required inspection."}
        return finish()
    stage = "repair"
    if interrupted(stage):
        return finish()
    try:
        result = remove_duplicate_faces(mesh, targets, row_local_attributes=declarations)
        report["extensions"]["meshvale.repair"]["native_outcome"] = result.outcome
        ids = _diagnostics(report, result.diagnostics, "input", "error")
        _check(report, "input.unchanged", "input", finding="passed" if before == _fingerprint(mesh.to_record()) else "failed", diagnostics=ids)
    except MemoryError:
        report["execution"] = {"outcome": "failed", "stage": stage, "reason": "Allocation failed."}
        return finish()
    if interrupted("verify"):
        return finish()
    if result.outcome == "rejected":
        report["candidate"]["outcome"] = "rejected"
        return finish()
    stage = "verify"
    try:
        report["snapshots"].append(_snapshot(result.candidate, "candidate"))
        report["candidate"] = {"outcome": "rejected", "snapshot": "candidate"}
        safe = _inspect(report, result.candidate, "candidate")
        destination = result.candidate.to_record()
        relation = (safe and result.outcome == ("accepted" if targets else "unchanged") and
                    _correspondence(source, destination, list(result.face_map), list(result.corner_map), targets))
        preservation = relation and _preserved(source, destination, list(result.face_map), list(result.corner_map), targets)
        for name, finding in (("repair.correspondence", relation), ("repair.preservation", preservation)):
            ids = [] if finding else _diagnostics(report, [{"code": name + ".failed", "subject": "verification", "element": None}], "candidate", "error")
            _check(report, name, "candidate", finding="passed" if finding else "failed", diagnostics=ids)
    except MemoryError:
        report["execution"] = {"outcome": "failed", "stage": stage, "reason": "Allocation failed."}
        return finish()
    if interrupted(stage):
        return finish()
    if all(row["finding"] == "passed" for row in report["coverage"] if row["id"] in required):
        report["candidate"]["outcome"] = result.outcome
        _maps_and_claims(report, source, result)
        return finish(result.candidate)
    return finish()
