# SPDX-License-Identifier: Apache-2.0
from copy import deepcopy
import gc
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import meshvale_interchange as io
from meshvale_geometry import Mesh
from meshvale_reports import load, validate, exit_code
from meshvale_repair.obj_workflow import repair_obj_file


OBJ = b"""mtllib materials/paint.mtl
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
vt 1 0
vt 1 1
vn 0 0 1
o mixed
g shared
s 1
usemtl painted
f 1/1/1 2/2/1 3/3/1
f 2/2/1 4/4/1 5/5/1 3/3/1
o pentagon
g concave
f 6 7 8 9 10
o mixed
g shared
f 5/5/1 3/3/1 2/2/1 4/4/1
"""


def fixture(root):
    source = root / "source"
    (source / "materials").mkdir(parents=True)
    (source / "textures").mkdir()
    (source / "model.obj").write_bytes(OBJ)
    (source / "materials/paint.mtl").write_bytes(b"newmtl painted\nKd .1 .2 .3\nmap_Kd ../textures/paint.bin\n")
    (source / "textures/paint.bin").write_bytes(b"\0\xffopaque texture")
    return source / "model.obj"


def source_bytes(root):
    return {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()}


class AssetWorkflowTests(unittest.TestCase):
    def run_file(self, root, source, targets, **options):
        before = source_bytes(source.parent)
        result = repair_obj_file(source, root / "bundle", targets, **options)
        self.assertEqual(source_bytes(source.parent), before)
        validate(result.report)
        self.assertEqual(load(json.dumps(result.report)), result.report)
        self.assertEqual(exit_code(result.report), result.exit_code)
        self.assertFalse(list(root.glob(".meshvale-stage-*")))
        return result

    def test_actual_bundle_report_resources_maps_and_snapshot_lifetimes(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            result = self.run_file(root, source, iter([(1, 3)]), input_id="original-asset")
            self.assertEqual(result.exit_code, 0, result.report)
            self.assertEqual(result.source.document.mesh.face_count, 4)
            self.assertEqual(result.candidate.document.mesh.face_count, 3)
            self.assertEqual(result.entry, Path("model.obj"))
            receipt = load((root / "bundle/meshvale-report.json").read_text(encoding="ascii"))
            self.assertEqual(receipt, result.report)
            self.assertEqual(receipt["publication"]["outcome"], "published")
            inventory = receipt["extensions"]["meshvale.repair.obj"]["artifacts"]
            hashes = {r["id"]: r["sha256"] for r in receipt["publication"]["artifacts"]}
            self.assertEqual({r["path"] for r in inventory}, {"model.obj", "materials/paint.mtl", "textures/paint.bin"})
            for file in inventory:
                self.assertEqual(hashlib.sha256((root / "bundle" / file["path"]).read_bytes()).hexdigest(), hashes[file["id"]])
            reloaded = io.read_obj_file(root / "bundle" / result.entry).asset
            self.assertEqual(reloaded.resources, result.source.resources)
            self.assertEqual(list(reloaded.document.mesh.to_record()["face_offsets"]), [0, 3, 7, 12])
            self.assertEqual(len(result.report["input"]["sha256"]), 64)
            snapshots = {r["role"]: r for r in receipt["snapshots"]}
            self.assertEqual(snapshots["output"]["meshes"][0]["face_count"], reloaded.document.mesh.face_count)
            candidate = result.candidate
            for path in source.parent.rglob("*"):
                if path.is_file(): path.unlink()
            del result
            gc.collect()
            self.assertEqual(candidate.document.mesh.face_count, 3)

    def test_noop_and_snapshot_hash_excludes_formatting_but_includes_resources(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            first = self.run_file(root, source, [])
            self.assertEqual(first.exit_code, 0)
            self.assertEqual(first.report["candidate"]["outcome"], "unchanged")
            source.write_bytes(b"# ignored formatting\n" + OBJ)
            second = repair_obj_file(source, root / "second", [])
            self.assertEqual(second.report["input"]["sha256"], first.report["input"]["sha256"])
            (source.parent / "textures/paint.bin").write_bytes(b"changed resource")
            third = repair_obj_file(source, root / "third", [])
            self.assertNotEqual(third.report["input"]["sha256"], first.report["input"]["sha256"])

    def test_bytes_receipt_path_and_invalid_paths_before_processing(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            result = self.run_file(root, source, [], report_path=b"receipts/result.json")
            self.assertEqual(result.exit_code, 0)
            self.assertEqual(load((root / "bundle/receipts/result.json").read_text()), result.report)
            for path in ("../receipt.json", "/receipt.json", "drive:receipt.json", ""):
                with patch("meshvale_repair.obj_workflow.io.read_obj_file") as reader:
                    with self.assertRaises(ValueError):
                        repair_obj_file(source, root / "other", [], report_path=path)
                    reader.assert_not_called()

    def test_rejected_malformed_missing_and_unsupported_imports_publish_nothing(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            rejected = self.run_file(root, source, [(0, 2)])
            self.assertEqual(rejected.exit_code, 1)
            self.assertIsNone(rejected.candidate)
            self.assertEqual(rejected.report["candidate"]["outcome"], "rejected")
            self.assertFalse((root / "bundle").exists())
            source.write_bytes(OBJ.replace(b"f 1/1/1 2/2/1 3/3/1", b"f 99/1/1 2/2/1 3/3/1"))
            malformed = self.run_file(root, source, [(1, 3)])
            self.assertEqual(malformed.exit_code, 2)
            self.assertEqual(malformed.report["execution"]["stage"], "inspect")
            source.write_bytes(OBJ + b"vp 0 0 0\n")
            unsupported = self.run_file(root, source, [])
            self.assertEqual(unsupported.exit_code, 2)
            self.assertIsNone(unsupported.source)
            source.write_bytes(OBJ)
            (source.parent / "textures/paint.bin").unlink()
            missing = self.run_file(root, source, [])
            self.assertEqual(missing.exit_code, 2)
            self.assertEqual(missing.report["execution"]["stage"], "import")
            self.assertFalse((root / "bundle").exists())

    def test_export_conflict_verification_failure_and_publication_race(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            (root / "bundle").mkdir(); (root / "bundle/retained").write_bytes(b"existing")
            result = self.run_file(root, source, [(1, 3)])
            self.assertEqual(result.exit_code, 2)
            self.assertEqual(result.report["candidate"]["outcome"], "accepted")
            self.assertEqual((root / "bundle/retained").read_bytes(), b"existing")
            (root / "bundle/retained").unlink(); (root / "bundle").rmdir()
            def corrupt(phase):
                if phase == "verification":
                    stage = next(root.glob(".meshvale-stage-*"))
                    (stage / "textures/paint.bin").write_bytes(b"corrupt")
            failed = self.run_file(root, source, [(1, 3)], on_phase=corrupt)
            self.assertEqual(failed.exit_code, 2)
            self.assertFalse((root / "bundle").exists())
            def race(phase):
                if phase == "publication": (root / "bundle").mkdir()
            failed = self.run_file(root, source, [(1, 3)], on_phase=race)
            self.assertEqual(failed.exit_code, 2)
            self.assertEqual(failed.report["publication"]["outcome"], "failed")
            self.assertFalse(any(s["role"] == "output" for s in failed.report["snapshots"]))
            self.assertEqual(list((root / "bundle").iterdir()), [])

    def test_receipt_failures_and_post_commit_sink_failure(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            collision = self.run_file(root, source, [(1, 3)], report_path="model.obj")
            self.assertEqual(collision.exit_code, 2)
            self.assertEqual(collision.report["execution"]["stage"], "report")
            self.assertFalse((root / "bundle").exists())
            with patch("meshvale_repair.obj_workflow._output_report", side_effect=OSError("receipt")):
                failure = self.run_file(root, source, [(1, 3)])
                self.assertEqual(failure.exit_code, 2)
                self.assertEqual(failure.report["execution"]["stage"], "report")
                self.assertFalse((root / "bundle").exists())
            delivered = []
            def sink(report):
                delivered.append(deepcopy(report)); report.clear(); raise OSError("delivery")
            committed = self.run_file(root, source, [(1, 3)], report_sink=sink)
            self.assertEqual(committed.exit_code, 2)
            self.assertEqual(committed.report["execution"]["stage"], "report")
            self.assertEqual(committed.report["publication"]["outcome"], "published")
            self.assertEqual(committed.report["profile"]["outcome"], "passed")
            self.assertEqual(len(delivered), 1)
            self.assertEqual(delivered[0]["execution"]["outcome"], "completed")
            self.assertEqual(load((root / "bundle/meshvale-report.json").read_text())["execution"]["outcome"], "completed")

    def test_cancellation_before_import_and_at_all_publication_phases(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            token = io.Cancellation(); token.request_stop()
            before = self.run_file(root, source, [], cancellation=token)
            self.assertEqual(before.exit_code, 130)
            self.assertIsNone(before.source)
            for phase in ("preflight", "staging", "verification", "publication"):
                token = io.Cancellation()
                def cancel(current):
                    if current == phase: token.request_stop()
                result = self.run_file(root, source, [(1, 3)], cancellation=token, on_phase=cancel)
                self.assertEqual(result.exit_code, 130)
                self.assertEqual(result.report["candidate"]["outcome"], "accepted")
                self.assertEqual(result.report["publication"]["outcome"], "cancelled")
                self.assertFalse((root / "bundle").exists())
                token = io.Cancellation()
                def failed_sink(report):
                    raise OSError("delivery")
                delivered = self.run_file(root, source, [(1, 3)], cancellation=token, on_phase=cancel, report_sink=failed_sink)
                self.assertEqual(delivered.exit_code, 130)
                self.assertEqual(delivered.report["execution"]["outcome"], "cancelled")
                self.assertEqual(delivered.report["publication"]["outcome"], "cancelled")
                self.assertEqual(delivered.report["extensions"]["meshvale.repair.obj"]["report_delivery"]["outcome"], "failed")

    def test_unsupported_export_channel_and_internal_errors_are_explicit(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            asset = io.read_obj_file(source).asset
            record = asset.document.mesh.to_record()
            original = next(r for r in record["attributes"] if r["semantic"] == "texcoord")
            uv = {**original, "name": "secondary-uv", "set_index": 1}
            record["attributes"].append(uv)
            asset = asset.with_mesh(Mesh.from_record(record))
            with patch("meshvale_repair.obj_workflow.io.read_obj_file", return_value=io.ObjFileResult(asset, ())):
                result = self.run_file(root, source, [(1, 3)])
                self.assertEqual(result.exit_code, 2)
                self.assertEqual(result.report["candidate"]["outcome"], "accepted")
                self.assertFalse((root / "bundle").exists())
            with patch("meshvale_repair.obj_workflow._output_report", side_effect=AssertionError("bug")), self.assertRaises(AssertionError):
                repair_obj_file(source, root / "bundle", [(1, 3)])
            self.assertFalse(list(root.glob(".meshvale-stage-*")))
            for options in ({"input_id": "path/to/input"}, {"cancellation": False}, {"report_sink": False}, {"on_phase": False}):
                with self.assertRaises((TypeError, ValueError)): repair_obj_file(source, root / "bundle", [], **options)


if __name__ == "__main__":
    unittest.main()
