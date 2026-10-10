# SPDX-License-Identifier: Apache-2.0
"""Keep an inspectable OBJ preservation walkthrough in a new output directory."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import meshvale_interchange as io
from meshvale_reports import load, validate
from meshvale_repair.obj_workflow import repair_obj_file


OBJ = """# Original mixed polygon fixture; UV seam at shared vertices.
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
vt 0.25 0
vt 1 0
vt 1 1
vt 0.25 1
vt 0 0
vt 1 0
vt 1 1
vt 0.5 0.5
vt 0 1
vn 0 0 1
o panel
g triangle
s off
usemtl checker
f 1/1/1 2/2/1 3/3/1
g quad
f 2/4/1 4/5/1 5/6/1 3/7/1
g pentagon
f 6/8/1 7/9/1 8/10/1 9/11/1 10/12/1
g quad
f 5/6/1 3/7/1 2/4/1 4/5/1
"""
MTL = b"# Original checker material\nnewmtl checker\nKd 1 1 1\nmap_Kd checker.ppm\n"
TEXTURE = b"P3\n# Original two-by-two checker\n2 2\n255\n255 255 255 0 0 0\n0 0 0 255 255 255\n"
TARGETS = [(1, 3)]
FACE_MAP = [0, 1, 2, 1]
CORNER_MAP = list(range(12)) + [5, 6, 3, 4]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def inventory(directory):
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in directory.rglob("*") if p.is_file()}


def command(root, *arguments, expected=0):
    completed = subprocess.run([sys.executable, "-I", "-m", "meshvale_repair", *arguments],
                               cwd=root, capture_output=True, text=True, timeout=60, check=False)
    require(completed.returncode == expected, "Command failed: " + completed.stderr)
    return load(completed.stdout)


def check_bundle(root, name, report, source):
    directory = root / "outputs" / name
    receipt = load((directory / "meshvale-report.json").read_text(encoding="ascii"))
    require(receipt == report, name + ": returned report differs from bundled receipt")
    validate(report)
    require(report["candidate"]["outcome"] == "accepted" and report["profile"]["outcome"] == "passed",
            name + ": repair was not independently accepted")
    require(report["publication"]["outcome"] == "published" and report["publication"]["verified"],
            name + ": publication was not verified")
    maps = {row["id"]: row for row in report["maps"]}
    require(maps["repair-face"]["entries"] == FACE_MAP and maps["repair-corner"]["entries"] == CORNER_MAP,
            name + ": source correspondence changed")
    loaded = io.read_obj_file(directory / "model.obj")
    require(loaded.asset is not None, name + ": independent output reload failed")
    mesh = loaded.asset.document.mesh
    record, original = mesh.to_record(), source.document.mesh.to_record()
    require(list(record["face_offsets"]) == [0, 3, 7, 12], name + ": triangle/quad/n-gon loops changed")
    require(list(record["corner_vertices"]) == list(original["corner_vertices"])[:12], name + ": vertex loops changed")
    require(record["positions"].tobytes() == original["positions"].tobytes(), name + ": positions changed")
    require(len(record["attributes"]) == len(original["attributes"]), name + ": channel count changed")
    for before, after in zip(original["attributes"], record["attributes"]):
        require(all(before[k] == after[k] for k in before if k not in ("values", "offsets", "present")),
                name + ": channel description changed")
        rows = 12 if before["domain"] == "corner" else 3
        require(after["values"].tobytes() == before["values"][:rows * before["components"]].tobytes(),
                name + ": retained channel values changed")
        require(before["offsets"] is None and after["offsets"] is None, name + ": fixture channel shape changed")
        if before["present"] is not None:
            require(after["present"].tobytes() == before["present"][:rows].tobytes(), name + ": missingness changed")
    uv = record["attributes"][0]["values"]
    require(uv[2] == 1 and uv[6] == 0.25, name + ": shared-vertex corner UV seam lost")
    require(loaded.asset.document.parts == source.document.parts and
            loaded.asset.document.material_names == source.document.material_names,
            name + ": object/group or material definitions changed")
    require(loaded.asset.resources == source.resources, name + ": material/texture bytes changed")
    require(mesh.inspect_topology()["topology"]["boundaries"], name + ": intentional open boundaries lost")
    paths = {row["id"]: row["path"] for row in report["extensions"]["meshvale.repair.obj"]["artifacts"]}
    require(set(paths.values()) == {"model.obj", "panel.mtl", "checker.ppm"}, name + ": payload inventory changed")
    for artifact in report["publication"]["artifacts"]:
        require(hashlib.sha256((directory / paths[artifact["id"]]).read_bytes()).hexdigest() == artifact["sha256"],
                name + ": receipt hash does not match published bytes")
    return inventory(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="New directory with an existing parent; kept for inspection")
    root = parser.parse_args().directory.absolute()
    root.mkdir()  # Exclusive creation: existing paths are never reused or overwritten.
    (root / "sources").mkdir()
    (root / "outputs").mkdir()
    (root / "sources/model.obj").write_text(OBJ, encoding="ascii")
    (root / "sources/panel.mtl").write_bytes(MTL)
    (root / "sources/checker.ppm").write_bytes(TEXTURE)
    original_files = inventory(root / "sources")
    imported = io.read_obj_file(root / "sources/model.obj")
    require(imported.asset is not None and imported.asset.document.mesh.face_count == 4, "Fixture import failed")

    api = repair_obj_file(root / "sources/model.obj", root / "outputs/api", TARGETS, input_id="mixed-panel")
    require(api.exit_code == 0, "Reusable API failed")
    write_json(root / "api-report.json", api.report)
    single = command(root, "obj", "sources/model.obj", "outputs/single", "--target", "1:3", "--input-id", "mixed-panel")
    write_json(root / "single-report.json", single)
    jobs = {"schema": "meshvale.repair.obj-jobs/1", "jobs": [
        {"id": "mixed-panel", "input": "sources/model.obj", "destination": "outputs/batch", "targets": [list(p) for p in TARGETS]}]}
    write_json(root / "jobs.json", jobs)
    batch = command(root, "obj-batch", "jobs.json")
    write_json(root / "batch-report.json", batch)
    require(batch["jobs"][0]["outcome"] == "attempted", "Batch job was not attempted")
    batch_report = batch["jobs"][0]["report"]
    require(api.report == single == batch_report, "API/single/batch evidence differs for the same request")
    bundles = [check_bundle(root, name, report, imported.asset)
               for name, report in (("api", api.report), ("single", single), ("batch", batch_report))]
    require(bundles[0] == bundles[1] == bundles[2], "API/single/batch bundle bytes differ")

    rejected = command(root, "obj", "sources/model.obj", "outputs/rejected", "--target", "0:1",
                       "--input-id", "mixed-panel", expected=1)
    write_json(root / "rejected-report.json", rejected)
    require(rejected["candidate"]["outcome"] == "rejected" and rejected["publication"]["outcome"] == "not-attempted",
            "Triangle/quad mismatch was not rejected")
    require(not (root / "outputs/rejected").exists(), "Rejected pair published a bundle")
    require(inventory(root / "sources") == original_files, "Source OBJ/material/texture changed")
    require(not list((root / "outputs").glob(".meshvale-stage-*")), "Unexpected staging directory remains")
    print("Verified API, single and batch: 4 faces -> 3; triangle/quad/n-gon loops and UV seam retained.")
    print("Original material/texture bytes, correspondence, receipts and open boundaries checked; rejection published nothing.")
    print("Inspect the preserved source, bundles and reports in:", root)


if __name__ == "__main__":
    main()
