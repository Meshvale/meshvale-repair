# SPDX-License-Identifier: Apache-2.0
"""Installed observations through the public prepared path interface."""
import gc
from pathlib import Path
import shutil
import tempfile
import unittest

from meshvale_repair.gltf_workflow import GltfExecution, repair_gltf_file
from test_gltf_file_workflow import POLICY, PROFILE, fixture


class Admission(unittest.TestCase):
    def test_retention_requires_bool_before_path_conversion(self):
        class UnreachedPath:
            calls = 0

            def __fspath__(self):
                self.calls += 1
                raise AssertionError("invalid retention must precede path conversion")

        source = UnreachedPath()
        for value in (None, 0, 1, "true", object()):
            with self.subTest(value=type(value).__name__):
                with self.assertRaises(TypeError):
                    repair_gltf_file(source, None, (), **POLICY,
                                     retain_failure_facts=value)
        self.assertEqual(source.calls, 0)

    def test_capture_failure_has_no_completed_facts(self):
        with tempfile.TemporaryDirectory(prefix="meshvale-facts-admission-") as scratch:
            source = Path(scratch).resolve() / "missing.gltf"
            for retain in (False, True):
                result = repair_gltf_file(source, None, (), **POLICY,
                                          retain_failure_facts=retain)
                self.assertFalse(result.workflow_started)
                for name in ("memory_outcomes", "input_snapshot_counts",
                             "candidate_snapshot_counts", "face_map", "corner_map",
                             "source_faces", "source_corners", "report_json",
                             "requested_bytes"):
                    self.assertIsNone(getattr(result, name), name)


@unittest.skipUnless(PROFILE == "qualified", "completed Repair requires qualified profile")
class CompletedFacts(unittest.TestCase):
    def test_ordinary_owner_counts_and_outcomes_survive_source_and_session(self):
        for glb in (False, True):
            for changed in (False, True):
                for retain in (False, True):
                    with self.subTest(glb=glb, changed=changed, retain=retain):
                        with tempfile.TemporaryDirectory(prefix="meshvale-facts-") as scratch:
                            root = Path(scratch).resolve()
                            source = root / "source"
                            fixture(source, glb)
                            execution = GltfExecution()
                            result = repair_gltf_file(
                                source / "scene.gltf", root / "output",
                                ((0, 1),) if changed else (), **POLICY,
                                execution=execution, retain_failure_facts=retain,
                            )
                            self.assertEqual(result.outcomes.publication,
                                             type(result.outcomes.publication).kPublished)
                            original_report = result.report_json
                            shutil.rmtree(source)
                            del execution
                            gc.collect()
                            self.assertEqual(result.input_snapshot_counts, (3, 2, 6))
                            self.assertEqual(result.candidate_snapshot_counts,
                                             (3, 1, 3) if changed else (3, 2, 6))
                            self.assertEqual(result.face_map, (0, 0) if changed else (0, 1))
                            self.assertEqual(result.corner_map,
                                             (0, 1, 2, 0, 1, 2) if changed else tuple(range(6)))
                            self.assertEqual(result.source_faces, (0,) if changed else (0, 1))
                            self.assertEqual(result.source_corners,
                                             (0, 1, 2) if changed else tuple(range(6)))
                            self.assertEqual(result.memory_outcomes.candidate,
                                             result.outcomes.candidate)
                            self.assertEqual(result.memory_outcomes.execution,
                                             result.outcomes.execution)
                            self.assertIsNone(result.requested_bytes.retained_repair)
                            self.assertGreater(result.requested_bytes.memory.total, 0)
                            self.assertEqual(result.report_json, original_report)


if __name__ == "__main__":
    unittest.main()
