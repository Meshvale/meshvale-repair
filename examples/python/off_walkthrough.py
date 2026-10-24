# SPDX-License-Identifier: Apache-2.0
"""Run installed reported OFF repair on self-contained indexed polygon assets.

The default demonstrates the Python API. Add --commands for installed console,
module and ordered API/CLI batches. All files live in a unique temporary folder.
Text is explicitly rewritten as canonical ASCII with unknown edge count zero;
unchanged repair preserves geometry, rather than original lexical bytes.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from meshvale_reports import exit_code, load
from meshvale_repair.off_workflow import OffExecution, repair_off_file

ENTRY = "mesh-\u7f51\u683c-\U0001f30d.off"
RECEIPT = "meshvale-report.json"
POLICIES = {"text": "canonical-ascii", "edge_count": "unknown-zero"}
SOURCE = (
    b"# Original indexed polygons, including unused signed-zero rows.\n"
    b"OFF\n8 3 17\n-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    b"4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n"
)
EMPTY = b"# A genuine empty input, not an unattempted operation.\nOFF\n0 0 17\n"
POSITION_BITS = (
    0x8000000000000000, 0, 0, 0x3FF0000000000000, 0, 0,
    0x3FF0000000000000, 0x3FF0000000000000, 0, 0, 0x3FF0000000000000, 0,
    0x4000000000000000, 0, 0, 0x4008000000000000, 0, 0,
    0x4000000000000000, 0x3FF0000000000000, 0, 0x4058C00000000000,
    0x8000000000000000, 0x4014000000000000,
)
FACE_MAP = (0, 0, 1)
CORNER_MAP = (0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6)
SOURCE_FACES = (0, 2)
SOURCE_CORNERS = (0, 1, 2, 3, 8, 9, 10)
CHECKS = (
    "source-storage", "target-policy", "candidate-storage",
    "repair-correspondence", "canonical-preservation", "reload-candidate",
    "source-unchanged", "staged-candidate", "staged-physical-members",
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def make_source(root, data):
    root.mkdir(parents=True)
    path = root / ENTRY
    path.write_bytes(data)
    return path


def expected_maps(changed, empty):
    if empty:
        return (), (), (), ()
    if changed:
        return FACE_MAP, CORNER_MAP, SOURCE_FACES, SOURCE_CORNERS
    return tuple(range(3)), tuple(range(11)), tuple(range(3)), tuple(range(11))


def expected_counts(changed, empty):
    return (0, 0, 0) if empty else (8, 2, 7) if changed else (8, 3, 11)


def check_output(raw, changed, empty):
    # Only this literal fixture's canonical records are decoded here. This is
    # an independent byte/bit oracle, not a general OFF reader or repair path.
    lines = raw.decode("ascii").splitlines()
    assert lines[0] == "OFF"
    vertices, faces, edge = map(int, lines[1].split())
    assert (vertices, faces, edge) == ((0, 0, 0) if empty else (8, 2 if changed else 3, 0))
    assert len(lines) == 2 + vertices + faces
    rows = [line.split() for line in lines[2:2 + vertices]]
    assert all(len(row) == 3 for row in rows)
    bits = tuple(struct.unpack("<Q", struct.pack("<d", float(word)))[0]
                 for row in rows for word in row)
    assert bits == (() if empty else POSITION_BITS)
    polygons = [list(map(int, line.split())) for line in lines[2 + vertices:]]
    assert all(row[0] == len(row) - 1 for row in polygons)
    assert [row[1:] for row in polygons] == ([] if empty else
        [[0, 1, 2, 3], [4, 5, 6]] if changed else
        [[0, 1, 2, 3], [2, 3, 0, 1], [4, 5, 6]])


def physical(directory):
    members = list(directory.iterdir())
    assert all(member.is_file() and not member.is_symlink() for member in members)
    return {member.name: member.read_bytes() for member in members}


def check_report(raw, original, changed, empty, destination=None):
    report = load(raw)
    assert exit_code(report) == 0
    assert report["execution"] == {"outcome": "completed", "stage": "complete", "reason": None}
    assert report["candidate"]["outcome"] == ("accepted" if changed else "unchanged")
    assert report["input"]["sha256"] == digest(original)
    assert report["profile"]["name"] == ("off-indexed-duplicate-publication" if destination else
                                         "off-indexed-duplicate-preservation")
    assert report["profile"]["outcome"] == "passed"
    assert report["profile"]["required_checks"] == list(CHECKS[:9 if destination else 7])
    extension = report["extensions"]["meshvale.repair.off"]
    assert extension["source"] == {"portable_name": ENTRY, "profile": "ascii-records", "declared_edge_count": 17}
    assert extension["policies"] == POLICIES | {"units": "unknown"}
    assert extension["memory_output_verified"]
    maps = expected_maps(changed, empty)
    assert extension["source_faces"] == list(maps[2])
    assert extension["source_corners"] == list(maps[3])
    snapshots = {row["role"]: row["meshes"][0] for row in report["snapshots"]}
    assert set(snapshots) == ({"input", "candidate", "output"} if destination else {"input", "candidate"})
    for role, counts in (("input", (0, 0, 0) if empty else (8, 3, 11)),
                         ("candidate", expected_counts(changed, empty))):
        assert snapshots[role]["id"] == "surface"
        assert tuple(snapshots[role][name] for name in ("vertex_count", "face_count", "corner_count")) == counts
    wire_maps = {row["id"]: row for row in report["maps"]}
    assert set(wire_maps) == {"repair-vertices", "repair-faces", "repair-corners"} | (
        {"export-vertices", "export-faces", "export-corners"} if destination else set())
    for name, row in wire_maps.items():
        assert row["from"]["mesh"] == row["to"]["mesh"] == "surface"
        assert (row["from"]["snapshot"], row["to"]["snapshot"]) == (
            ("input", "candidate") if name.startswith("repair-") else ("candidate", "output"))
        if changed and name in ("repair-faces", "repair-corners"):
            assert row["kind"] == "explicit" and row["entries"] == list(maps[0 if name == "repair-faces" else 1])
        else:
            assert row["kind"] == "identity" and row["entries"] is None
    if destination:
        files = physical(destination)
        assert set(files) == {ENTRY, RECEIPT} and files[RECEIPT] == raw
        assert extension["artifact_paths"] == {"entry": ENTRY}
        assert report["publication"]["verified"] and report["publication"]["outcome"] == "published"
        assert report["publication"]["artifacts"] == [{"id": "entry", "sha256": digest(files[ENTRY])}]
        check_output(files[ENTRY], changed, empty)
        assert files[ENTRY] != original  # Canonical text and edge zero are explicit conversions.
    else:
        assert report["publication"]["outcome"] == "not-requested"
        assert report["publication"]["artifacts"] == []
    return report


def check_maps(result, changed, empty):
    assert (result.face_map, result.corner_map, result.source_faces, result.source_corners) == expected_maps(changed, empty)
    assert result.input_snapshot_counts == ((0, 0, 0) if empty else (8, 3, 11))
    assert result.candidate_snapshot_counts == expected_counts(changed, empty)


def check_native_refusal(result, destination):
    assert result.capture_status.name == "kOffFailure" and not result.workflow_started
    diagnostic = result.capture_diagnostic
    assert diagnostic.off_status.name == "kGeometryFailure"
    assert diagnostic.off_diagnostic.geometry_status.name == "kUnsupportedAllocationProfile"
    assert result.status is None and result.report_json is None
    assert result.face_map is None and result.candidate_snapshot_counts is None
    assert not destination.exists()


def api_walkthrough(root, unsupported):
    cases = [("empty", EMPTY, False, True)] if unsupported else [
        ("changed", SOURCE, True, False), ("unchanged", SOURCE, False, False),
        ("empty", EMPTY, False, True)]
    session = OffExecution()
    for name, original, changed, empty in cases:
        for publish in (False, True):
            case = root / "api" / (name + ("-published" if publish else "-memory"))
            source = make_source(case / "source", original)
            destination = case / "published" if publish else None
            result = repair_off_file(source, destination, [(0, 1)] if changed else [],
                                     input_id="api-" + name, execution=session, **POLICIES)
            assert result.capture_status.name == "kSuccess" and result.status.name == "kSuccess"
            check_maps(result, changed, empty)
            raw = result.report_json
            check_report(raw, original, changed, empty, destination)
            check_output(result.output_bytes, changed, empty)
            assert result.write_facts.emitted_edge_count == 0
            assert result.write_facts.source_declared_edge_count == 17
            assert source.read_bytes() == original
            if publish:
                before = physical(destination)
                refused = repair_off_file(source, destination, [(0, 1)] if changed else [],
                                          execution=session, **POLICIES)
                assert refused.publication.diagnostic.status.name == "kDestinationExists"
                assert exit_code(load(refused.report_json)) == 2 and physical(destination) == before
                del refused
            source.unlink()
            # Maps and reports belong to retained native owners, not this path.
            assert result.report_json == raw
            check_maps(result, changed, empty)
            if publish:
                reopened = repair_off_file(destination / ENTRY, None, [], execution=session, **POLICIES)
                assert reopened.status.name == "kSuccess"
                assert reopened.candidate_snapshot_counts == expected_counts(changed, empty)
                assert reopened.output_bytes == (destination / ENTRY).read_bytes()
                assert reopened.input_sha256 == digest((destination / ENTRY).read_bytes())
                del reopened
            del result
            gc.collect()
            assert session.active_tracked_payload == session.active_workers == 0
            print(f"API {name}: {'published/reopened/no-overwrite' if publish else 'memory'} verified")
    if unsupported:
        source = make_source(root / "api" / "nonempty-refusal", SOURCE)
        destination = source.parent / "published"
        result = repair_off_file(source, destination, [(0, 1)], execution=session, **POLICIES)
        check_native_refusal(result, destination)
        assert source.read_bytes() == SOURCE
        del result
        gc.collect()
        assert session.active_tracked_payload == session.active_workers == 0
        print("API nonempty: actual unsupported numerical producer refusal verified")


def check_command_refusal(raw, destination):
    report = load(raw)
    assert exit_code(report) == 2 and report["candidate"]["outcome"] == "not-attempted"
    assert report["publication"]["outcome"] == "not-attempted" and not destination.exists()
    assert report["snapshots"] == report["maps"] == []
    extension = report["extensions"]["meshvale.repair.off.commands"]
    assert extension["origin"] == "command-capture-fallback" and not extension["native_report_available"]
    assert extension["capture"]["status"] == {"domain": "OffFileStatus", "member": "kOffFailure"}
    assert extension["capture"]["diagnostic"]["off_diagnostic"]["geometry_status"] == {
        "domain": "MaterializationStatus", "member": "kUnsupportedAllocationProfile"}
    return report


def command_walkthrough(root, unsupported):
    from meshvale_repair.off_commands import run_off_jobs

    cases = [("empty", EMPTY, False, True, False), ("nonempty-refusal", SOURCE, True, False, True)] if unsupported else [
        ("changed", SOURCE, True, False, False), ("unchanged", SOURCE, False, False, False),
        ("empty", EMPTY, False, True, False)]
    sources = {name: make_source(root / "sources" / name, original)
               for name, original, _, _, _ in cases}
    (root / "published").mkdir()
    frontends = {"module": [sys.executable, "-I", "-m", "meshvale_repair"],
                 "console": [str(Path(sys.executable).parent / ("meshvale-repair.exe" if sys.platform == "win32" else "meshvale-repair"))]}
    for frontend, prefix in frontends.items():
        for name, original, changed, empty, refused in cases:
            identity = frontend + "-" + name
            destination = root / "published" / identity
            arguments = ["off", str(sources[name].relative_to(root)), str(destination.relative_to(root)),
                         "--text", "canonical-ascii", "--edge-count", "unknown-zero", "--input-id", identity]
            arguments += ["--target", "0:1"] if changed else ["--unchanged"]
            process = subprocess.run(prefix + arguments, cwd=root, capture_output=True, timeout=90)
            assert process.returncode == (2 if refused else 0), process.stderr.decode("utf-8", errors="replace")
            assert process.stdout.endswith(b"\n")
            raw = process.stdout[:-1]
            if refused:
                check_command_refusal(raw, destination)
            else:
                check_report(raw, original, changed, empty, destination)
                before = physical(destination)
                again = subprocess.run(prefix + arguments, cwd=root, capture_output=True, timeout=90)
                assert again.returncode == 2 and exit_code(load(again.stdout)) == 2
                assert physical(destination) == before
            print(f"{frontend} {name}: {'actual refusal' if refused else 'exact receipt/no-overwrite'} verified")
    rows = [{"id": name, "input": str(sources[name].relative_to(root)),
             "destination": "published/batch-" + name,
             "targets": [[0, 1]] if changed else [], **POLICIES}
            for name, _, changed, _, _ in cases]
    session = OffExecution()
    for frontend in ("api", "module"):
        selected = [row | {"destination": row["destination"] + "-" + frontend} for row in rows]
        request = {"schema": "meshvale.repair.off-jobs/1", "jobs": selected}
        if frontend == "api":
            result = run_off_jobs(request, base=root, execution=session)
            raw, code = result.report_json, result.exit_code
            assert session.active_tracked_payload == session.active_workers == 0
        else:
            path = root / "jobs.json"
            path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
            process = subprocess.run(frontends["module"] + ["off-batch", str(path)],
                                     cwd=root.parent, capture_output=True, timeout=90)
            assert process.stdout.endswith(b"\n"), process.stderr.decode("utf-8", errors="replace")
            raw, code = process.stdout[:-1], process.returncode
        batch = load(raw)
        assert code == exit_code(batch) == (2 if unsupported else 0)
        assert [row["id"] for row in batch["jobs"]] == [row["id"] for row in selected]
        for requested, completed, case in zip(selected, batch["jobs"], cases):
            _, original, changed, empty, refused = case
            assert completed["outcome"] == "attempted"
            destination = root / requested["destination"]
            if refused:
                check_command_refusal(json.dumps(completed["report"]).encode("utf-8"), destination)
            else:
                receipt = (destination / RECEIPT).read_bytes()
                assert receipt in raw and load(receipt) == completed["report"]
                check_report(receipt, original, changed, empty, destination)
        print(f"batch {frontend}: {len(rows)} ordered actual outcomes and verbatim native receipts verified")
    assert all(path.read_bytes() == next(case[1] for case in cases if case[0] == name)
               for name, path in sources.items())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commands", action="store_true",
                        help="run installed console/module singles and ordered API/CLI batches")
    parser.add_argument("--unsupported-profile", action="store_true",
                        help="verify actual empty success and nonempty native numerical refusal")
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="meshvale-installed-off-") as scratch:
        root = Path(scratch).resolve() / "\u8d44\u4ea7-\U0001f30d"
        root.mkdir()
        if options.commands:
            command_walkthrough(root, options.unsupported_profile)
        else:
            api_walkthrough(root, options.unsupported_profile)


if __name__ == "__main__":
    main()
