# SPDX-License-Identifier: Apache-2.0
"""Original mixed-polygon assets through installed API, console and batch."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import meshvale_interchange as io
from meshvale_reports import exit_code, load, validate
from meshvale_repair.obj_workflow import repair_obj_file


BASE = """# Original mixed polygons with three material and group bindings.
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
vt 0.5 0.25
vn 0 0 1
vn 0 1 0
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
"""
EXACT = "g quad\nusemtl blue\nf 2/4/1 4/5/1 5/6/1 3/7/1\n"
CYCLIC = "g quad\nusemtl blue\nf 5/6/1 3/7/1 2/4/1 4/5/1\n"
UV_DIFFERENT = "g quad\nusemtl blue\nf 5/13/1 3/7/1 2/4/1 4/5/1\n"
MTL = (b"# Original red, blue and green materials\n"
       b"newmtl red\nKd 1 0 0\nmap_Kd checker.ppm\n"
       b"newmtl blue\nKd 0 0 1\nmap_Kd checker.ppm\n"
       b"newmtl green\nKd 0 1 0\nmap_Kd checker.ppm\n")
TEXTURE = b"P3\n# Original checker\n2 2\n255\n255 255 255 0 0 0\n0 0 0 255 255 255\n"
POSITIONS = [0, 0, 0, 1, 0, 0, 0, 1, 0, 2, 0, 0, 2, 1, 0,
             4, 0, 0, 6, 0, 0, 6, 2, 0, 5, 1, 0, 4, 2, 0]
CORNERS = [0, 1, 2, 1, 3, 4, 2, 5, 6, 7, 8, 9]
UV = [0, 0, 1, 0, 0, 1, .25, 0, 1, 0, 1, 1, .25, 1,
      0, 0, 1, 0, 1, 1, .5, .5, 0, 1]
CHANNELS = [("corner", "obj.uv0", "texcoord", 0, 2, "float64"),
            ("corner", "obj.normal", "normal", None, 3, "float64"),
            ("face", "obj.material", "material_index", None, 1, "int32"),
            ("face", "obj.part", "label", None, 1, "uint32"),
            ("face", "obj.smoothing_group", "label", None, 1, "uint32")]


def inventory(root):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in root.rglob("*") if p.is_file()}


class ObjCorpusTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="meshvale-obj-corpus-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        (self.root / "outputs").mkdir()

    def source(self, name, text):
        directory = self.root / "sources" / name
        directory.mkdir(parents=True)
        (directory / "model.obj").write_text(text, encoding="ascii")
        (directory / "panel.mtl").write_bytes(MTL)
        (directory / "checker.ppm").write_bytes(TEXTURE)
        return directory / "model.obj"

    def invoke(self, *arguments, expected=0, console=False):
        executable = Path(sys.executable).parent / ("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        prefix = [str(executable)] if console else [sys.executable, "-I", "-m", "meshvale_repair"]
        process = subprocess.run(prefix + list(arguments), cwd=self.root, capture_output=True,
                                 text=True, encoding="utf-8", timeout=60, check=False)
        self.assertEqual(process.returncode, expected, process.stderr)
        report = load(process.stdout)
        validate(report)
        self.assertEqual(exit_code(report), expected)
        return report

    def batch(self, jobs, *, fail_fast=False, expected=0):
        path = self.root / "jobs.json"
        path.write_text(json.dumps({"schema": "meshvale.repair.obj-jobs/1", "jobs": jobs}), encoding="utf-8")
        return self.invoke("obj-batch", "jobs.json", *( ["--fail-fast"] if fail_fast else []), expected=expected)

    def job(self, name, source, destination, targets):
        return {"id": name, "input": source.relative_to(self.root).as_posix(),
                "destination": "outputs/" + destination, "targets": [list(pair) for pair in targets]}

    def check_base(self, asset, *, expected_uv=UV):
        record = asset.document.mesh.to_record()
        self.assertEqual(list(record["positions"])[:30], POSITIONS)
        self.assertEqual(list(record["face_offsets"])[:4], [0, 3, 7, 12])
        self.assertEqual(list(record["corner_vertices"])[:12], CORNERS)
        channels = record["attributes"]
        self.assertEqual([tuple(c[k] for k in ("domain", "name", "semantic", "set_index", "components", "scalar_type"))
                          for c in channels], CHANNELS)
        self.assertTrue(all(c["offsets"] is None and c["metadata"] == {} for c in channels))
        self.assertEqual(list(channels[0]["values"])[:24], expected_uv)
        self.assertEqual(list(channels[0]["present"])[:12], [1] * 12)
        self.assertEqual(list(channels[1]["values"])[:36], [0, 0, 1] * 12)
        self.assertEqual(list(channels[1]["present"])[:12], [1] * 12)
        self.assertEqual([list(c["values"])[:3] for c in channels[2:]], [[0, 1, 2], [0, 1, 2], [0, 0, 0]])
        self.assertTrue(all(c["present"] is None for c in channels[2:]))
        self.assertEqual(asset.document.material_names, ("red", "blue", "green"))
        self.assertEqual([(part.object, part.groups) for part in asset.document.parts][:3],
                         [("panel", ("triangle",)), ("panel", ("quad",)), ("panel", ("pentagon",))])
        return record

    def check_report(self, report, code, *, unchanged=False):
        validate(report)
        self.assertEqual(exit_code(report), code)
        if code == 0:
            self.assertEqual(report["candidate"]["outcome"], "unchanged" if unchanged else "accepted")
            self.assertEqual(report["profile"]["outcome"], "passed")
            self.assertEqual(report["publication"]["outcome"], "published")
            self.assertTrue(report["publication"]["verified"])
        else:
            self.assertEqual(report["publication"]["outcome"], "not-attempted")
            self.assertEqual(report["maps"], [])
            if code == 1:
                self.assertEqual(report["candidate"]["outcome"], "rejected")
            else:
                self.assertEqual(report["execution"]["stage"], "inspect")
                self.assertEqual(report["candidate"]["outcome"], "not-attempted")

    def check_bundle(self, name, report, face_map, corner_map):
        directory = self.root / "outputs" / name
        self.assertEqual(load((directory / "meshvale-report.json").read_text(encoding="ascii")), report)
        loaded = io.read_obj_file(directory / "model.obj")
        self.assertIsNotNone(loaded.asset)
        record = self.check_base(loaded.asset)
        self.assertEqual(list(record["positions"]), POSITIONS)
        self.assertEqual(list(record["face_offsets"]), [0, 3, 7, 12])
        self.assertEqual(list(record["corner_vertices"]), CORNERS)
        self.assertEqual([list(c["values"]) for c in record["attributes"]],
                         [UV, [0, 0, 1] * 12, [0, 1, 2], [0, 1, 2], [0, 0, 0]])
        self.assertEqual([None if c["present"] is None else list(c["present"]) for c in record["attributes"]],
                         [[1] * 12, [1] * 12, None, None, None])
        self.assertEqual(len(loaded.asset.document.parts), 3)
        self.assertTrue(loaded.asset.document.mesh.inspect_topology()["topology"]["boundaries"])
        maps = {row["id"]: row for row in report["maps"]}
        for domain, entries in (("face", face_map), ("corner", corner_map)):
            self.assertEqual(maps["repair-" + domain]["entries"], entries)
            self.assertEqual(maps["repair-" + domain]["kind"], "identity" if entries is None else "explicit")
        for identifier in ("repair-vertex", "export-vertex", "export-face", "export-corner"):
            self.assertEqual((maps[identifier]["kind"], maps[identifier]["entries"]), ("identity", None))
        files = inventory(directory)
        self.assertEqual(set(files), {"model.obj", "panel.mtl", "checker.ppm", "meshvale-report.json"})
        self.assertEqual((files["panel.mtl"], files["checker.ppm"]), (MTL, TEXTURE))
        paths = {row["id"]: row["path"] for row in report["extensions"]["meshvale.repair.obj"]["artifacts"]}
        self.assertEqual(set(paths.values()), {"model.obj", "panel.mtl", "checker.ppm"})
        for artifact in report["publication"]["artifacts"]:
            self.assertEqual(hashlib.sha256(files[paths[artifact["id"]]]).hexdigest(), artifact["sha256"])
        return files

    def exercise(self, name, text, *, targets=((1, 3),), code=1, face_map=None, corner_map=None):
        source = self.source(name, text)
        before = inventory(self.root / "sources")
        imported = io.read_obj_file(source)
        self.assertIsNotNone(imported.asset)
        if code != 2:
            record = self.check_base(imported.asset, expected_uv=UV[:6] + [0, 0] + UV[8:] if name == "missing-uv" else UV)
            uv, normal, material, part, _ = record["attributes"]
            if name == "missing-uv":
                self.assertEqual(list(uv["present"]), [1] * 14 + [0, 1])
                self.assertEqual(list(uv["values"])[6:8], [0, 0])
                self.assertEqual(list(uv["values"])[28:30], [0, 0])
            elif name in ("uv", "whole-request"):
                self.assertEqual(list(uv["values"])[-8:-6], [.5, .25])
            elif name == "normal":
                self.assertEqual(list(normal["values"])[36:39], [0, 1, 0])
            elif name == "material":
                self.assertEqual(list(material["values"]), [0, 1, 2, 0])
            elif name == "group":
                self.assertEqual(list(part["values"]), [0, 1, 2, 3])
                self.assertEqual(imported.asset.document.parts[3].groups, ("other",))
            elif name == "distinct-ids":
                self.assertEqual(list(record["positions"]), POSITIONS + [1, 0, 0, 2, 0, 0, 2, 1, 0, 0, 1, 0])
                self.assertEqual(list(record["corner_vertices"])[12:], [12, 13, 10, 11])
            elif name == "reverse":
                self.assertEqual(list(record["corner_vertices"])[12:], [1, 2, 4, 3])
        result = repair_obj_file(source, self.root / "outputs/api", targets, input_id=name)
        self.assertEqual(result.exit_code, code, result.report)
        arguments = ["obj", source.relative_to(self.root).as_posix(), "outputs/single", "--input-id", name]
        for representative, duplicate in targets:
            arguments += ["--target", f"{representative}:{duplicate}"]
        if not targets:
            arguments += ["--unchanged"]
        single = self.invoke(*arguments, console=True, expected=code)
        batch = self.batch([self.job(name, source, "batch", targets)], expected=code)
        self.assertEqual(batch["jobs"][0]["outcome"], "attempted")
        reports = [result.report, single, batch["jobs"][0]["report"]]
        self.assertEqual(reports[0], reports[1])
        self.assertEqual(reports[1], reports[2])
        bundles = []
        for destination, report in zip(("api", "single", "batch"), reports):
            self.check_report(report, code, unchanged=not targets)
            if code == 0:
                bundles.append(self.check_bundle(destination, report, face_map, corner_map))
            else:
                self.assertFalse((self.root / "outputs" / destination).exists())
        if code == 0:
            self.assertEqual(bundles[0], bundles[1])
            self.assertEqual(bundles[1], bundles[2])
        else:
            self.assertIsNone(result.candidate)
        self.assertEqual(inventory(self.root / "sources"), before)
        self.assertFalse(list((self.root / "outputs").glob(".meshvale-stage-*")))

    def test_clean_three_face_copy(self):
        self.exercise("clean", BASE, targets=(), code=0)

    def test_exact_quad(self):
        self.exercise("exact", BASE + EXACT, code=0, face_map=[0, 1, 2, 1],
                      corner_map=list(range(12)) + [3, 4, 5, 6])

    def test_cyclic_quad(self):
        self.exercise("cyclic", BASE + CYCLIC, code=0, face_map=[0, 1, 2, 1],
                      corner_map=list(range(12)) + [5, 6, 3, 4])

    def test_reverse_winding(self):
        self.exercise("reverse", BASE + "g quad\nusemtl blue\nf 2/4/1 3/7/1 5/6/1 4/5/1\n")

    def test_uv_value(self):
        self.exercise("uv", BASE + UV_DIFFERENT)

    def test_material_binding(self):
        self.exercise("material", BASE + CYCLIC.replace("usemtl blue", "usemtl red"))

    def test_normal_value(self):
        self.exercise("normal", BASE + CYCLIC.replace("5/6/1", "5/6/2"))

    def test_coincident_positions_with_distinct_ids(self):
        text = BASE.replace("vt 0 0\n", "v 1 0 0\nv 2 0 0\nv 2 1 0\nv 0 1 0\nvt 0 0\n", 1)
        self.exercise("distinct-ids", text + "g quad\nusemtl blue\nf 13/6/1 14/7/1 11/4/1 12/5/1\n")

    def test_missing_uv_is_not_authored_zero(self):
        text = BASE.replace("f 2/4/1", "f 2/1/1")
        self.exercise("missing-uv", text + "g quad\nusemtl blue\nf 5/6/1 3/7/1 2//1 4/5/1\n")

    def test_group_binding(self):
        self.exercise("group", BASE + CYCLIC.replace("g quad", "g other"))

    def test_whole_request_after_eligible_target(self):
        self.exercise("whole-request", BASE + EXACT + UV_DIFFERENT, targets=((1, 3), (1, 4)))

    def test_malformed_index(self):
        self.exercise("malformed", (BASE + EXACT).replace("f 1/1/1", "f 99/1/1"), code=2)

    def test_mixed_batch_continuation_and_fail_fast(self):
        clean = self.source("clean", BASE)
        exact = self.source("exact", BASE + EXACT)
        malformed = self.source("malformed", (BASE + EXACT).replace("f 1/1/1", "f 99/1/1"))
        rows = [("clean", clean, (), 0), ("exact", exact, ((1, 3),), 0),
                ("malformed", malformed, ((1, 3),), 2), ("clean-again", clean, (), 0)]
        before = inventory(self.root / "sources")
        references = []
        for name, source, targets, code in rows:
            result = repair_obj_file(source, self.root / "outputs" / ("api-" + name), targets, input_id=name)
            self.assertEqual(result.exit_code, code)
            self.check_report(result.report, code, unchanged=not targets)
            references.append(result.report)
        for fail_fast, prefix in ((False, "continue-"), (True, "stop-")):
            batch = self.batch([self.job(name, source, prefix + name, targets)
                                for name, source, targets, _ in rows], fail_fast=fail_fast, expected=2)
            self.assertEqual([row["outcome"] for row in batch["jobs"]],
                             ["attempted"] * 3 + ["not-attempted" if fail_fast else "attempted"])
            for index, (name, _, _, code) in enumerate(rows):
                destination = prefix + name
                if fail_fast and index == 3:
                    self.assertFalse((self.root / "outputs" / destination).exists())
                    continue
                report = batch["jobs"][index]["report"]
                self.assertEqual(report, references[index])
                if code == 0:
                    face_map = [0, 1, 2, 1] if name == "exact" else None
                    corner_map = list(range(12)) + [3, 4, 5, 6] if name == "exact" else None
                    files = self.check_bundle(destination, report, face_map, corner_map)
                    self.assertEqual(files, inventory(self.root / "outputs" / ("api-" + name)))
                else:
                    self.assertFalse((self.root / "outputs" / destination).exists())
        self.assertEqual(inventory(self.root / "sources"), before)
        self.assertFalse(list((self.root / "outputs").glob(".meshvale-stage-*")))


if __name__ == "__main__":
    unittest.main()
