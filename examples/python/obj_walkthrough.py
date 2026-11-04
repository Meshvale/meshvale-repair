# SPDX-License-Identifier: Apache-2.0
"""Keep an inspectable OBJ preservation walkthrough in a new output directory."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import meshvale_interchange as io
from meshvale_reports import exit_code, load, validate
from meshvale_repair.obj_workflow import repair_obj_file


BASE = """# Original mixed polygon fixture; UV seam at shared vertices.
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
"""
CYCLIC = """g quad
f 5/6/1 3/7/1 2/4/1 4/5/1
"""
UV_DIFFERENT = """g quad
f 5/13/1 3/7/1 2/4/1 4/5/1
"""
OBJ = BASE + CYCLIC
# The added UV is used only by the fourth face; its geometry stays identical.
PROTECTED_UV_OBJ = BASE.replace("vn 0 0 1\n", "vt 0.5 0.25\nvn 0 0 1\n") + UV_DIFFERENT
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


def check_bundle(root, name, report, source, face_map, corner_map, *, unchanged):
    directory = root / "outputs" / name
    receipt = load((directory / "meshvale-report.json").read_text(encoding="ascii"))
    require(receipt == report, name + ": returned report differs from bundled receipt")
    validate(report)
    outcome = "unchanged" if unchanged else "accepted"
    require(exit_code(report) == 0 and report["candidate"]["outcome"] == outcome and
            report["profile"]["outcome"] == "passed",
            name + ": repair was not independently accepted")
    require(report["publication"]["outcome"] == "published" and report["publication"]["verified"],
            name + ": publication was not verified")
    maps = {row["id"]: row for row in report["maps"]}
    expected_maps = {"repair-face": face_map, "repair-corner": corner_map,
                     "repair-vertex": None, "export-vertex": None, "export-face": None, "export-corner": None}
    require(len(report["maps"]) == len(expected_maps) and set(maps) == set(expected_maps),
            name + ": correspondence inventory changed")
    for identifier, entries in expected_maps.items():
        require(maps[identifier]["entries"] == entries and
                maps[identifier]["kind"] == ("identity" if entries is None else "explicit"),
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
        require((before["present"] is None) == (after["present"] is None), name + ": channel missingness shape changed")
        if before["present"] is not None:
            require(after["present"].tobytes() == before["present"][:rows].tobytes(), name + ": missingness changed")
    uv = record["attributes"][0]["values"]
    require(uv[2] == 1 and uv[6] == 0.25, name + ": shared-vertex corner UV seam lost")
    require(loaded.asset.document.parts == source.document.parts and
            loaded.asset.document.material_names == source.document.material_names,
            name + ": object/group or material definitions changed")
    require(loaded.asset.resources == source.resources, name + ": material/texture bytes changed")
    require(mesh.inspect_topology()["topology"]["boundaries"], name + ": intentional open boundaries lost")
    payloads = {"model.obj", "panel.mtl", "checker.ppm"}
    files = inventory(directory)
    require(set(files) == payloads | {"meshvale-report.json"}, name + ": bundle inventory changed")
    require(files["panel.mtl"] == MTL and files["checker.ppm"] == TEXTURE,
            name + ": original material/texture bytes changed")
    artifacts = report["extensions"]["meshvale.repair.obj"]["artifacts"]
    paths = {row["id"]: row["path"] for row in artifacts}
    require(len(artifacts) == len(paths) == len(payloads) and set(paths.values()) == payloads,
            name + ": payload inventory changed")
    require(len(report["publication"]["artifacts"]) == len(paths) and
            {row["id"] for row in report["publication"]["artifacts"]} == set(paths),
            name + ": published hash inventory changed")
    for artifact in report["publication"]["artifacts"]:
        require(hashlib.sha256((directory / paths[artifact["id"]]).read_bytes()).hexdigest() == artifact["sha256"],
                name + ": receipt hash does not match published bytes")
    return files


def write_source(directory, text):
    directory.mkdir()
    (directory / "model.obj").write_text(text, encoding="ascii")
    (directory / "panel.mtl").write_bytes(MTL)
    (directory / "checker.ppm").write_bytes(TEXTURE)


def exercise(root, name, input_path, input_id, targets, expected):
    prefix = name + "-" if name else ""
    imported = io.read_obj_file(root / input_path)
    require(imported.asset is not None and imported.asset.document.mesh.face_count == (4 if targets else 3),
            prefix + "fixture import failed")
    api = repair_obj_file(root / input_path, root / "outputs" / (prefix + "api"), targets, input_id=input_id)
    require(api.exit_code == expected, prefix + "reusable API returned an unexpected outcome")
    api_report = api.report
    write_json(root / (prefix + "api-report.json"), api_report)
    arguments = ["obj", input_path, "outputs/" + prefix + "single", "--input-id", input_id]
    for keep, remove in targets:
        arguments += ["--target", f"{keep}:{remove}"]
    if not targets:
        arguments += ["--unchanged"]
    single = command(root, *arguments, expected=expected)
    write_json(root / (prefix + "single-report.json"), single)
    jobs = {"schema": "meshvale.repair.obj-jobs/1", "jobs": [
        {"id": input_id, "input": input_path, "destination": "outputs/" + prefix + "batch",
         "targets": [list(p) for p in targets]}]}
    write_json(root / (prefix + "jobs.json"), jobs)
    batch = command(root, "obj-batch", prefix + "jobs.json", expected=expected)
    write_json(root / (prefix + "batch-report.json"), batch)
    require(len(batch["jobs"]) == 1 and batch["jobs"][0]["outcome"] == "attempted",
            prefix + "batch job was not attempted")
    batch_report = batch["jobs"][0]["report"]
    require(api_report == single == batch_report, prefix + "API/single/batch evidence differs for the same request")
    reports = (("api", api_report), ("single", single), ("batch", batch_report))
    if expected == 0:
        require(api.candidate is not None, prefix + "successful API returned no candidate")
        bundles = [check_bundle(root, prefix + method, report, imported.asset,
                                None if not targets else FACE_MAP, None if not targets else CORNER_MAP,
                                unchanged=not targets) for method, report in reports]
        require(bundles[0] == bundles[1] == bundles[2], prefix + "API/single/batch bundle bytes differ")
    else:
        require(api.candidate is None, prefix + "rejected API returned a candidate")
        for method, report in reports:
            validate(report)
            require(exit_code(report) == 1 and report["candidate"]["outcome"] == "rejected" and
                    report["publication"]["outcome"] == "not-attempted" and report["maps"] == [],
                    prefix + method + ": protected-UV request was not rejected")
            require(not (root / "outputs" / (prefix + method)).exists(),
                    prefix + method + ": rejected pair published a bundle")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="New directory with an existing parent; kept for inspection")
    root = parser.parse_args().directory.absolute()
    root.mkdir()  # Exclusive creation: existing paths are never reused or overwritten.
    (root / "outputs").mkdir()
    write_source(root / "sources", OBJ)
    write_source(root / "sources/clean", BASE)
    write_source(root / "sources/protected-uv", PROTECTED_UV_OBJ)
    original_files = inventory(root / "sources")
    exercise(root, "", "sources/model.obj", "mixed-panel", TARGETS, 0)
    exercise(root, "clean", "sources/clean/model.obj", "clean-panel", [], 0)
    exercise(root, "protected-uv", "sources/protected-uv/model.obj", "protected-uv-panel", TARGETS, 1)

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
    print("Verified clean API, single and batch: 3 faces unchanged with identity correspondence.")
    print("Verified protected-UV refusal through all three entry points; no rejected destination exists.")
    print("Inspect the preserved source, bundles and reports in:", root)


if __name__ == "__main__":
    main()
