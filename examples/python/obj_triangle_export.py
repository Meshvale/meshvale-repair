# SPDX-License-Identifier: Apache-2.0
"""Keep an original mixed-polygon repair and verified triangle bundles for inspection."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

import meshvale_geometry as geometry
import meshvale_interchange as io
from meshvale_reports import load, validate
from meshvale_repair.obj_workflow import ObjCancellation, repair_obj_file


OBJ = """# Original triangle, quad, concave pentagon and cyclic quad copy.
mtllib panel.mtl
v 0 0 0
v 1 0 0
v 0 1 0
v 2 0 0
v 2 1 0
v 4 0 0
v 6 0 0
v 6 2 0
v 5 1 0
v 4 2 0
vt 0 0
vt 1 0
vt 0 1
vt .25 0
vt 1 0
vt 1 1
vt .25 1
vt 0 0
vt 1 0
vt 1 1
vt .5 .5
vt 0 1
vn 0 0 1
o panel
s off
g triangle
usemtl red
f 1/1/1 2/2/1 3/3/1
g quad
usemtl blue
f 2/4/1 4/5/1 5/6/1 3/7/1
g pentagon
usemtl green
f 6/8/1 7/9/1 8/10/1 9/11/1 10/12/1
g quad
usemtl blue
f 5/6/1 3/7/1 2/4/1 4/5/1
"""
MTL = (b"# Original material definitions\nnewmtl red\nKd 1 0 0\nmap_Kd checker.ppm\n"
       b"newmtl blue\nKd 0 0 1\nmap_Kd checker.ppm\n"
       b"newmtl green\nKd 0 1 0\nmap_Kd checker.ppm\n")
TEXTURE = b"P3\n# Original checker\n2 2\n255\n255 255 255 0 0 0\n0 0 0 255 255 255\n"
FACE_MAP = [0, [1, 2], [3, 4, 5]]
CORNER_MAP = [0, 1, 2, 4, [5, 6], 7, [3, 8], [12, 15], [9, 13], 10, [11, 14, 16], 17]
TRIANGLE_VERTICES = [0, 1, 2, 2, 1, 3, 3, 4, 2, 6, 7, 8, 5, 6, 8, 5, 8, 9]
TRIANGLE_UV = [0, 0, 1, 0, 0, 1, .25, 1, .25, 0, 1, 0, 1, 0, 1, 1, .25, 1,
               1, 0, 1, 1, .5, .5, 0, 0, 1, 0, .5, .5, 0, 0, .5, .5, 0, 1]


def require(value, reason):
    if not value:
        raise RuntimeError(reason)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def inventory(directory):
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in directory.rglob("*") if p.is_file()}


def check_bundle(directory, report, source):
    validate(report)
    require(load((directory / "meshvale-report.json").read_text(encoding="ascii")) == report, "Receipt differs from returned report")
    require(report["candidate"]["outcome"] == "accepted" and report["profile"]["outcome"] == "passed", "Candidate/profile did not pass")
    require(report["publication"]["outcome"] == "published" and report["publication"]["verified"], "Publication was not verified")
    counts = {row["role"]: row["meshes"][0]["face_count"] for row in report["snapshots"]}
    require(counts == {"input": 4, "candidate": 3, "output": 6}, "Repair and export states were conflated")
    maps = {row["id"]: row for row in report["maps"]}
    require(maps["repair-face"]["entries"] == [0, 1, 2, 1] and
            maps["repair-corner"]["entries"] == list(range(12)) + [5, 6, 3, 4], "Repair provenance changed")
    require(maps["export-face"]["entries"] == FACE_MAP and maps["export-corner"]["entries"] == CORNER_MAP, "Export provenance changed")
    loaded = io.read_obj_file(directory / "model.obj")
    require(loaded.asset is not None, "Published OBJ did not reload")
    record = loaded.asset.document.mesh.to_record()
    require(record["positions"].tobytes() == source.document.mesh.to_record()["positions"].tobytes(), "Vertex storage changed")
    require(list(record["face_offsets"]) == [0, 3, 6, 9, 12, 15, 18] and
            list(record["corner_vertices"]) == TRIANGLE_VERTICES, "Actual triangle loops differ")
    channels = record["attributes"]
    require([row["name"] for row in channels] == ["obj.uv0", "obj.normal", "obj.material", "obj.part", "obj.smoothing_group"], "Channel inventory changed")
    require([list(row["values"]) for row in channels] ==
            [TRIANGLE_UV, [0, 0, 1] * 18, [0, 1, 1, 2, 2, 2], [0, 1, 1, 2, 2, 2], [0] * 6], "Actual transferred rows differ")
    require(all(list(row["present"]) == [1] * 18 for row in channels[:2]) and
            all(row["present"] is None for row in channels[2:]), "Authored presence changed")
    require(loaded.asset.document.parts == source.document.parts and
            loaded.asset.document.material_names == ("red", "blue", "green"), "Material/group bindings changed")
    require(loaded.asset.resources == source.resources, "Referenced bytes changed")
    files = inventory(directory)
    require(set(files) == {"model.obj", "panel.mtl", "checker.ppm", "meshvale-report.json"}, "Bundle inventory differs")
    paths = {row["id"]: row["path"] for row in report["extensions"]["meshvale.repair.obj"]["artifacts"]}
    for artifact in report["publication"]["artifacts"]:
        require(hashlib.sha256(files[paths[artifact["id"]]]).hexdigest() == artifact["sha256"], "Physical artifact digest differs")
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="New directory with an existing parent; retained for inspection")
    parser.add_argument("--commands", action="store_true", help="Also run installed console and module batch commands")
    parser.add_argument("--workers", type=int, default=2, help="Native conversion worker budget; zero selects available concurrency")
    options = parser.parse_args()
    execution = geometry.ExecutionContext(worker_budget=options.workers)
    root = options.directory.absolute()
    root.mkdir()
    source_root, outputs, reports = root / "sources", root / "outputs", root / "reports"
    for directory in (source_root, outputs, reports): directory.mkdir()
    (source_root / "model.obj").write_text(OBJ, encoding="ascii")
    (source_root / "nonplanar.obj").write_text(OBJ.replace("v 2 1 0\n", "v 2 1 .25\n"), encoding="ascii")
    (source_root / "panel.mtl").write_bytes(MTL)
    (source_root / "checker.ppm").write_bytes(TEXTURE)
    before = inventory(source_root)
    source = io.read_obj_file(source_root / "model.obj").asset
    require(source is not None, "Original source did not import")
    observations = []
    api = repair_obj_file(source_root / "model.obj", outputs / "api", [(1, 3)], input_id="mixed-panel",
                          output_topology="triangles", triangulation_execution=execution)
    require(api.exit_code == 0 and api.candidate.document.mesh.face_count == 3, "Reusable file interface failed")
    bundles = [check_bundle(outputs / "api", api.report, source)]
    write_json(reports / "api.json", api.report)
    observations.append({"id": "api", "exit": api.exit_code, "published": True})
    if options.commands:
        console = Path(sys.executable).parent / ("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        completed = subprocess.run([str(console), "obj", "sources/model.obj", "outputs/single", "--target", "1:3",
            "--input-id", "mixed-panel", "--output-topology", "triangles", "--triangulation-workers", str(options.workers)],
            cwd=root, capture_output=True, text=True, timeout=60, check=False)
        require(completed.returncode == 0, "Console command failed: " + completed.stderr)
        single = load(completed.stdout)
        write_json(reports / "single.json", single)
        request = {"schema": "meshvale.repair.obj-jobs/2", "jobs": [{"id": "mixed-panel", "input": "sources/model.obj",
            "destination": "outputs/batch", "targets": [[1, 3]], "output_topology": "triangles",
            "triangulation": {"worker_budget": options.workers}}]}
        write_json(root / "jobs.json", request)
        completed = subprocess.run([sys.executable, "-I", "-m", "meshvale_repair", "obj-batch", "jobs.json"],
                                   cwd=root, capture_output=True, text=True, timeout=60, check=False)
        require(completed.returncode == 0, "Module batch failed: " + completed.stderr)
        batch = load(completed.stdout); validate(batch)
        write_json(reports / "batch.json", batch)
        require(batch["jobs"][0]["outcome"] == "attempted", "Batch job was not attempted")
        require(api.report == single == batch["jobs"][0]["report"], "Entry points disagree")
        bundles += [check_bundle(outputs / name, report, source)
                    for name, report in (("single", single), ("batch", batch["jobs"][0]["report"]))]
        require(bundles[0] == bundles[1] == bundles[2], "Entry-point bundle bytes differ")
        observations += [{"id": "single", "exit": 0, "published": True}, {"id": "batch", "exit": 0, "published": True}]
    stopped = ObjCancellation(); stopped.request_stop()
    for name, path, extra, code in (
            ("nonplanar", source_root / "nonplanar.obj", {}, 1),
            ("budget", source_root / "model.obj", {"triangulation_execution": geometry.ExecutionContext(tracked_payload_budget_bytes=0)}, 2),
            ("prestop", source_root / "model.obj", {"cancellation": stopped}, 130)):
        result = repair_obj_file(path, outputs / name, [(1, 3)], output_topology="triangles", **extra)
        validate(result.report)
        require(result.exit_code == code and not (outputs / name).exists(), name + " did not retain the actual refusal/cancellation")
        require(result.report["publication"]["outcome"] == "not-attempted", name + " fabricated a publication attempt")
        write_json(reports / (name + ".json"), result.report)
        observations.append({"id": name, "exit": code, "published": False})
    require(inventory(source_root) == before, "Source/material/resource bytes changed")
    require(not list(outputs.glob(".meshvale-stage-*")), "Unexpected staging directory remains")
    write_json(root / "summary.json", {"observations": observations, "published": len(bundles),
        "source_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in before.items()},
        "repair_faces": [4, 3], "export_faces": [3, 6]})
    print("Verified Repair4→3 polygons and explicit export3→6 triangles; original resources and separate maps retained.")
    print("Inspect the source, bundles, reports and refusal/cancellation outcomes in:", root)


if __name__ == "__main__":
    main()
