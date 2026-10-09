# SPDX-License-Identifier: Apache-2.0
from array import array
from dataclasses import replace, FrozenInstanceError
import gc
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch
from meshvale_geometry import Mesh
from meshvale_reports import load, validate, exit_code
from meshvale_repair import remove_duplicate_faces
from meshvale_repair.workflow import repair_duplicate_mesh

# Share original product fixtures, while importing only installed product code.
spec = importlib.util.spec_from_file_location("duplicate_fixtures", Path(__file__).parents[1] / "python/test_duplicates.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class WorkflowTests(unittest.TestCase):
    def run_workflow(self, mesh, targets, **options):
        before = fixtures.fingerprint(mesh)
        result = repair_duplicate_mesh(mesh, targets, **options)
        self.assertEqual(fixtures.fingerprint(mesh), before)
        validate(result.report)
        self.assertEqual(load(json.dumps(result.report)), result.report)
        self.assertEqual(exit_code(result.report), result.exit_code)
        return result

    def test_real_mixed_polygons_maps_and_every_channel(self):
        mesh = Mesh.from_record(fixtures.fixture())
        result = self.run_workflow(mesh, iter([(1, 3)]), input_id="example-source")
        native = remove_duplicate_faces(mesh, [(1, 3)])
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.report["candidate"]["outcome"], "accepted")
        self.assertEqual(fixtures.fingerprint(result.candidate), fixtures.fingerprint(native.candidate))
        maps = {m["domain"]: m for m in result.report["maps"]}
        self.assertEqual(maps["face"]["entries"], list(native.face_map))
        self.assertEqual(maps["corner"]["entries"], list(native.corner_map))
        self.assertEqual(maps["vertex"]["kind"], "identity")
        claims = [p["feature"] for p in result.report["preservation"] if p["feature"]["kind"] == "attribute"]
        self.assertEqual(len(claims), len(mesh.to_record()["attributes"]))
        self.assertEqual({p["set_index"] for p in claims if p["name"].startswith("uv")}, {0, 1})
        self.assertEqual(result.report["publication"]["outcome"], "not-requested")
        self.assertIsNone(result.report["input"]["sha256"])
        extension = result.report["extensions"]["meshvale.repair"]
        self.assertEqual(extension["target_pairs"], [[1, 3]])
        self.assertIn("topology.edge_nonmanifold", {d["code"] for d in result.report["diagnostics"]})
        unsupported = [c for c in result.report["coverage"] if c["status"] == "unsupported"]
        self.assertTrue(any(c["check"] == "self_intersection" for c in unsupported))
        self.assertTrue(all(c["finding"] is None for c in unsupported))

    def test_noop_identity_and_detached_lifetimes(self):
        mesh = Mesh.from_record(fixtures.fixture())
        expected = fixtures.fingerprint(mesh)
        result = self.run_workflow(mesh, [])
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.report["candidate"]["outcome"], "unchanged")
        self.assertTrue(all(m["kind"] == "identity" for m in result.report["maps"]))
        self.assertIsNot(result.candidate, mesh)
        with self.assertRaises(FrozenInstanceError):
            result.exit_code = 9
        candidate = result.candidate
        result.report["extensions"]["meshvale.repair"]["topology"].clear()
        del mesh, result
        gc.collect()
        self.assertEqual(fixtures.fingerprint(candidate), expected)
        empty = self.run_workflow(Mesh(), [])
        self.assertEqual(empty.exit_code, 0)

    def test_native_rejection_unknown_semantics_and_out_of_range(self):
        for targets in [[(0, 2)], [(1, 3), (0, 2)], [(1, 2**64-1)]]:
            with self.subTest(targets=targets):
                result = self.run_workflow(Mesh.from_record(fixtures.fixture()), targets)
                self.assertEqual(result.exit_code, 1)
                self.assertIsNone(result.candidate)
                self.assertEqual(result.report["candidate"]["outcome"], "rejected")
                self.assertEqual(result.report["maps"], [])
        source = fixtures.fixture()
        source["attributes"].append(fixtures.attribute("face", "custom", "vendor_tag", "H", [0, 1, 2, 1]))
        mesh = Mesh.from_record(source)
        self.assertEqual(self.run_workflow(mesh, [(1, 3)]).exit_code, 1)
        self.assertEqual(self.run_workflow(mesh, [(1, 3)], row_local_attributes=iter([("face", "custom")])).exit_code, 0)

    def test_malformed_and_nonfinite_input_never_calls_kernel(self):
        for field, index, value in [("corner_vertices", 0, 99), ("positions", 0, float("nan"))]:
            source = fixtures.fixture()
            source[field][index] = value
            with self.subTest(field=field), patch("meshvale_repair.workflow.remove_duplicate_faces") as kernel:
                result = self.run_workflow(Mesh.from_record(source), [(1, 3)])
                kernel.assert_not_called()
                self.assertEqual(result.exit_code, 2)
                self.assertEqual(result.report["execution"]["stage"], "inspect")
                self.assertEqual(result.report["profile"]["outcome"], "failed")

    def test_missing_backing_ragged_rows_and_signed_zero(self):
        source = fixtures.fixture()
        uv = source["attributes"][1]
        uv["offsets"] = array("Q", range(0, 33, 2))
        uv["present"] = array("B", [1]*3 + [0]*4 + [1]*5 + [0]*4)
        uv["values"][24:] = array("f", [123]*8)
        source["attributes"][0]["values"][29] = -0.0
        result = self.run_workflow(Mesh.from_record(source), [(1, 3)])
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(list(result.candidate.to_record()["attributes"][1]["offsets"]), list(range(0, 25, 2)))

    def test_faulty_candidate_maps_geometry_metadata_and_channels_rejected(self):
        mesh = Mesh.from_record(fixtures.fixture())
        original = remove_duplicate_faces(mesh, [(1, 3)])
        faulty = [replace(original, outcome="unchanged"),
                  replace(original, face_map=memoryview(array("Q", [0, 0, 2, 0]))),
                  replace(original, corner_map=memoryview(array("Q", [0]*16)))]
        for change in ("position", "weight", "metadata", "channel", "loop", "missingness"):
            record = original.candidate.to_record()
            if change == "position":
                record["positions"] = array("d", record["positions"]); record["positions"][0] = .25
            elif change == "weight":
                row = record["attributes"][6]; row["values"] = array("f", row["values"]); row["values"][0] = .5
            elif change == "metadata":
                record["attributes"][-1]["metadata"]["origin"] = "changed"
            elif change == "channel":
                record["attributes"].pop()
            elif change == "loop":
                record["corner_vertices"] = array("Q", record["corner_vertices"]); record["corner_vertices"][0] = 1
            else:
                record["attributes"][0]["present"] = array("B", [1]*12)
            faulty.append(replace(original, candidate=Mesh.from_record(record)))
        for candidate in faulty:
            with self.subTest(candidate=candidate), patch("meshvale_repair.workflow.remove_duplicate_faces", return_value=candidate):
                result = self.run_workflow(mesh, [(1, 3)])
                self.assertEqual(result.exit_code, 1)
                self.assertIsNone(result.candidate)
                self.assertEqual(result.report["candidate"], {"outcome": "rejected", "snapshot": "candidate"})
                self.assertEqual(result.report["profile"]["outcome"], "failed")
                self.assertEqual(result.report["preservation"], [])

    def test_cancellation_at_each_checkpoint(self):
        for point in range(4):
            with self.subTest(point=point):
                sequence = iter([False]*point + [True])
                result = self.run_workflow(Mesh.from_record(fixtures.fixture()), [(1, 3)], cancelled=lambda: next(sequence))
                self.assertEqual(result.exit_code, 130)
                self.assertIsNone(result.candidate)
                self.assertEqual(result.report["execution"]["outcome"], "cancelled")
                self.assertNotIn(result.report["candidate"]["outcome"], ("accepted", "unchanged"))

    def test_errors_and_allocation_failure_are_not_success(self):
        mesh = Mesh.from_record(fixtures.fixture())
        for targets in [[(True, 3)], [(1., 3)], [(1, -1)], [(1, 2**64)], [None]]:
            with self.assertRaises((TypeError, ValueError, OverflowError)):
                repair_duplicate_mesh(mesh, targets)
        for options in [{"input_id": "path/to/source"}, {"cancelled": False}, {"cancelled": lambda: 1},
                        {"row_local_attributes": [("edge", "custom")]}]:
            with self.assertRaises((TypeError, ValueError)):
                repair_duplicate_mesh(mesh, [], **options)
        with patch("meshvale_repair.workflow.remove_duplicate_faces", side_effect=MemoryError):
            result = self.run_workflow(mesh, [(1, 3)])
            self.assertEqual(result.exit_code, 2)
            self.assertEqual(result.report["execution"]["stage"], "repair")
        for function, stage in [("_inspect", "inspect"), ("_correspondence", "verify")]:
            with patch("meshvale_repair.workflow." + function, side_effect=MemoryError):
                result = self.run_workflow(mesh, [(1, 3)])
                self.assertEqual(result.exit_code, 2)
                self.assertEqual(result.report["execution"]["stage"], stage)
        for kind in (RuntimeError, AssertionError):
            with patch("meshvale_repair.workflow.remove_duplicate_faces", side_effect=kind), self.assertRaises(kind):
                repair_duplicate_mesh(mesh, [(1, 3)])
        with self.assertRaises(RuntimeError):
            repair_duplicate_mesh(mesh, [], cancelled=lambda: (_ for _ in ()).throw(RuntimeError()))


if __name__ == "__main__":
    unittest.main()
