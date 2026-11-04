# SPDX-License-Identifier: Apache-2.0
"""Run a small installed PLY API/single/batch example; keep every result on disk."""
import argparse
import gc
import hashlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import sysconfig

HEADER = """ply
format ascii 1.0
element vertex 3
property double x
property double y
property double z
element face {faces}
property list uchar uint vertex_indices
end_header
"""
POSITIONS = "0 0 0\n1 0 0\n0 1 0\n"
# Original public fixture from the PLY guide, including the cyclic second loop.
DUPLICATES = (HEADER.format(faces=2) + POSITIONS + "3 0 1 2\n3 1 2 0\n").encode("ascii")
CLEAN = (HEADER.format(faces=1) + POSITIONS + "3 0 1 2\n").encode("ascii")
REVERSE = (HEADER.format(faces=2) + POSITIONS + "3 0 1 2\n3 0 2 1\n").encode("ascii")
SOURCES = {"duplicates.ply": DUPLICATES, "clean.ply": CLEAN, "reverse.ply": REVERSE}
# name, source, caller targets, expected candidate, forward/inverse edit maps.
CASES = (
    ("changed", "duplicates.ply", [(0, 1)], "accepted",
     ((0, 0), (0, 1, 2, 1, 2, 0), (0,), (0, 1, 2))),
    ("unchanged", "duplicates.ply", [], "unchanged",
     ((0, 1), tuple(range(6)), (0, 1), tuple(range(6)))),
    ("clean", "clean.ply", [], "unchanged", ((0,), (0, 1, 2), (0,), (0, 1, 2))),
    ("reverse", "reverse.ply", [(0, 1)], "rejected", None),
    ("invalid-target", "duplicates.ply", [(0, 99)], "rejected", None),
)
RECEIPT = "meshvale-report.json"
CHECKS = ("source-storage", "target-policy", "candidate-storage", "repair-correspondence",
          "canonical-preservation", "reload-candidate", "source-unchanged", "typed-properties",
          "wire-schema", "staged-candidate", "staged-physical-members")
POSITION_BITS = (0, 0, 0, 0x3FF0000000000000, 0, 0, 0, 0x3FF0000000000000, 0)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def save_json(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def check_ascii(raw, loops):
    """Decode only this fixture's fixed double-XYZ/list-uchar-uint ASCII shape."""
    lines = raw.decode("ascii").splitlines()
    expected_header = HEADER.format(faces=len(loops)).splitlines()
    require(lines[:len(expected_header)] == expected_header, "exact ordered output wire schema")
    payload = lines[len(expected_header):]
    require(len(payload) == 3 + len(loops), "complete vertex and face records")
    rows = [line.split() for line in payload[:3]]
    require(all(len(row) == 3 for row in rows), "three XYZ components per vertex")
    words = tuple(struct.unpack("<Q", struct.pack("<d", float(value)))[0]
                  for row in rows for value in row)
    require(words == POSITION_BITS, "independent exact position bits")
    polygons = [tuple(map(int, line.split())) for line in payload[3:]]
    require(polygons == [(3, *loop) for loop in loops], "exact directed output polygon loops")


def check_report(report, case, destination):
    name, entry, targets, outcome, maps = case
    require(report["execution"]["outcome"] == "completed", "actual completed operation")
    require(report["candidate"]["outcome"] == outcome, "literal candidate outcome: " + name)
    require(report["input"]["id"] == name, "caller input ID preserved")
    require(report["operation"]["options"] == {
        "encoding": "ascii", "rewrite": "canonical",
        "targets": [{"keep": keep, "remove": remove} for keep, remove in targets],
        "row_local_attributes": []}, "explicit caller options; no custom-property inference")
    ext = report["extensions"]["meshvale.repair.ply"]
    require(ext["native_status"] == (6 if outcome == "rejected" else 0), "actual native success/rejection")
    if maps is None:
        require(not destination.exists(), "refused destination absent")
        require(report["publication"]["outcome"] != "published" and
                report["publication"]["artifacts"] == [], "refusal has no published artifacts")
        require(report["profile"]["outcome"] != "passed", "refusal cannot claim passed preservation")
        require(ext["source_faces"] is None and ext["source_corners"] is None,
                "refusal invents no completed edit maps")
        return (outcome, ext["native_status"], report["publication"]["outcome"])
    require(report["input"]["sha256"] == digest(SOURCES[entry]), "captured original byte hash")
    require(report["profile"]["outcome"] == "passed" and ext["memory_output_verified"],
            "actual preservation and memory verification passed")
    require(report["profile"]["required_checks"] == list(CHECKS), "complete ordered publication checks")
    require(report["publication"]["outcome"] == "published" and report["publication"]["verified"],
            "true verified publication")
    require(ext["source_faces"] == list(maps[2]) and ext["source_corners"] == list(maps[3]),
            "literal inverse edit maps")
    snapshots = {row["role"]: row["meshes"][0] for row in report["snapshots"]}
    require(set(snapshots) == {"input", "candidate", "output"}, "exact reached snapshots")
    input_faces = 1 if name == "clean" else 2
    for role, faces in (("input", input_faces), ("candidate", len(maps[2])), ("output", len(maps[2]))):
        mesh = snapshots[role]
        require(mesh["id"] == "surface" and
                (mesh["vertex_count"], mesh["face_count"], mesh["corner_count"]) == (3, faces, 3 * faces),
                "literal physical counts: " + role)
    wire = {row["id"]: row for row in report["maps"]}
    require(len(report["maps"]) == 6 and set(wire) == {
        "repair-vertices", "repair-faces", "repair-corners",
        "export-vertices", "export-faces", "export-corners"}, "exact six forward map records")
    for phase in ("repair", "export"):
        for domain, expected in (("vertices", (0, 1, 2)), ("faces", maps[0]), ("corners", maps[1])):
            row = wire[phase + "-" + domain]
            require(row["from"] == {"snapshot": "input" if phase == "repair" else "candidate", "mesh": "surface"}
                    and row["to"] == {"snapshot": "candidate" if phase == "repair" else "output", "mesh": "surface"},
                    "exact map scopes")
            if phase == "repair" and name == "changed" and domain != "vertices":
                require(row["kind"] == "explicit" and row["entries"] == list(expected), "literal edit map")
            else:
                require(row["kind"] == "identity" and row["entries"] is None, "real identity map")
    members = list(destination.iterdir())
    require(all(path.is_file() and not path.is_symlink() for path in members) and
            {path.name for path in members} == {entry, RECEIPT}, "complete flat two-member bundle")
    raw = (destination / entry).read_bytes()
    loops = [(0, 1, 2), (1, 2, 0)] if name == "unchanged" else [(0, 1, 2)]
    check_ascii(raw, loops)
    require(report["publication"]["artifacts"] == [{"id": "entry", "sha256": digest(raw)}],
            "independently hashed physical entry")
    return (outcome, report["input"], report["operation"], report["snapshots"], report["maps"],
            ext["source_faces"], ext["source_corners"], ext["source"], report["publication"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new directory with an existing parent; retains all inputs/results")
    parser.add_argument("--console", type=Path, help="meshvale-repair entrypoint in this interpreter's scripts directory")
    args = parser.parse_args()
    require(sys.flags.isolated, "run with python -I to use the installed package")
    require(not sys.flags.optimize, "run without Python optimization")
    root = args.output.resolve()
    require(root.parent.is_dir() and not root.exists(), "new output directory and existing parent required")
    console = args.console or Path(sysconfig.get_path("scripts")) / (
        "meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
    require(console.is_file() and console.parent.resolve() == Path(sysconfig.get_path("scripts")).resolve(),
            "console belongs to this interpreter")
    from meshvale_reports import exit_code, load
    from meshvale_repair.ply_commands import run_ply_jobs
    from meshvale_repair.ply_workflow import PlyExecution, repair_ply_file
    import meshvale_repair
    import meshvale_repair._repair
    import meshvale_reports
    origins = {}
    for module in (meshvale_repair, meshvale_repair._repair, meshvale_reports):
        path = Path(module.__file__).resolve()
        require(any(path.is_relative_to(Path(sysconfig.get_path(key)).resolve())
                    for key in ("purelib", "platlib")), "installed module origin")
        origins[module.__name__] = str(path)
    root.mkdir()
    inputs = root / "inputs"
    inputs.mkdir()
    for entry, raw in SOURCES.items():
        (inputs / entry).write_bytes(raw)
    save_json(root / "runtime.json", {"python": sys.version, "executable": sys.executable,
              "repair_version": version("meshvale-repair"), "geometry_version": version("meshvale-geometry"),
              "origins": origins, "console": str(console.resolve())})
    references, observations = {}, []
    session = PlyExecution()

    def observe(raw, case, destination, route):
        report = load(raw)
        expected_exit = 1 if case[3] == "rejected" else 0
        require(exit_code(report) == expected_exit, "shared report exit semantics")
        projection = check_report(report, case, destination)
        if case[0] in references:
            require(projection == references[case[0]], "API/commands/batches agree on preserved semantic facts")
        else:
            references[case[0]] = projection
        if case[4] is not None:
            require((destination / RECEIPT).read_bytes() == raw, "delivered receipt equals physical native receipt")
        observations.append({"route": route, "case": case[0], "exit_code": expected_exit})

    for case in CASES:
        name, entry, targets, outcome, maps = case
        destination = root / ("api-" + name)
        result = repair_ply_file(inputs / entry, destination, targets, encoding="ascii", input_id=name, execution=session)
        require(result.exit_code == (1 if maps is None else 0) and result.report_json is not None,
                "actual API receipt; unsupported/missing receipt is not success")
        observe(result.report_json, case, destination, "api")
        (root / ("api-" + name + ".json")).write_bytes(result.report_json)
        if maps is not None:
            require((result.face_map, result.corner_map, result.source_faces, result.source_corners) == maps,
                    "literal owned API maps")
            check_ascii(result.output_bytes, [(0, 1, 2), (1, 2, 0)] if name == "unchanged" else [(0, 1, 2)])
        del result
        gc.collect()
        require(session.active_workers == 0 and session.active_tracked_payload == 0, "API owners released; workers joined")

    def command(route, arguments, label, expected_exit):
        argv = [sys.executable, "-I", "-m", "meshvale_repair"] if route == "module" else [str(console)]
        process = subprocess.run(argv + arguments, cwd=root, capture_output=True, timeout=120, check=False)
        (root / (label + ".stdout")).write_bytes(process.stdout)
        (root / (label + ".stderr")).write_bytes(process.stderr)
        save_json(root / (label + ".process.json"), {"argv": argv + arguments, "returncode": process.returncode})
        require(process.returncode == expected_exit and process.stdout.endswith(b"\n"), "actual command exit and report framing")
        return process.stdout[:-1]

    for route in ("module", "console"):
        for case in CASES:
            name, entry, targets, _, maps = case
            destination = root / (route + "-single-" + name)
            arguments = ["ply", str(inputs / entry), str(destination), "--encoding", "ascii", "--input-id", name]
            arguments += ["--target", str(targets[0][0]) + ":" + str(targets[0][1])] if targets else ["--unchanged"]
            observe(command(route, arguments, route + "-single-" + name, 1 if maps is None else 0), case, destination, route + "-single")
    for route in ("api", "module", "console"):
        jobs = [{"id": case[0], "input": str(inputs / case[1]), "destination": str(root / (route + "-batch-" + case[0])),
                 "targets": [list(pair) for pair in case[2]], "encoding": "ascii"} for case in CASES]
        request = {"schema": "meshvale.repair.ply-jobs/1", "jobs": jobs}
        request_path = root / (route + "-jobs.json")
        save_json(request_path, request)
        if route == "api":
            batch_result = run_ply_jobs(request, execution=session)
            raw = batch_result.report_json
            require(batch_result.exit_code == 1, "actual continue-batch aggregate rejection")
            (root / "api-batch.json").write_bytes(raw)
            del batch_result
        else:
            raw = command(route, ["ply-batch", str(request_path)], route + "-batch", 1)
        batch = load(raw)
        require(exit_code(batch) == batch["exit_code"] == 1 and batch["mode"] == "continue", "ordered continue mode")
        require(len(batch["jobs"]) == len(CASES), "every batch row delivered")
        for row, case, job in zip(batch["jobs"], CASES, jobs):
            require(row["id"] == case[0] and row["outcome"] == "attempted", "all five actual jobs attempted in order")
            destination = Path(job["destination"])
            # Successful native receipts are retained byte-for-byte in the aggregate.
            receipt = (destination / RECEIPT).read_bytes() if case[4] is not None else None
            if receipt is not None:
                require(receipt in raw and load(receipt) == row["report"], "batch embeds exact published native bytes")
                observe(receipt, case, destination, route + "-batch")
            else:
                derived = json.dumps(row["report"], allow_nan=False).encode("utf-8")
                observe(derived, case, destination, route + "-batch")
        del batch
        gc.collect()
        require(session.active_workers == 0 and session.active_tracked_payload == 0, "batch owners released; workers joined")
    for entry, original in SOURCES.items():
        require((inputs / entry).read_bytes() == original, "physical source bytes unchanged")
    save_json(root / "results.json", {"observations": observations, "source_sha256": {
        entry: digest(raw) for entry, raw in SOURCES.items()}, "active_workers": session.active_workers,
        "active_tracked_payload": session.active_tracked_payload})
    print("PLY walkthrough passed: 30 attempted jobs; retained inputs, bundles, receipts and command streams in", root)


if __name__ == "__main__":
    main()
