# SPDX-License-Identifier: Apache-2.0
"""Installed binding admission/projection checks; no deformation success claim."""
import json
from pathlib import Path
import tempfile
import unittest

from meshvale_repair import gltf_workflow as workflow


_PROFILE = "deformation-retained-v1"
_PHASES = (
    "capture_read", "source_read", "source", "materialization", "writing",
    "reload_read", "reload", "verification", "publication_read",
    "publication_reload", "publication_verification",
)
_PRESETS = Path(__file__).resolve().parents[2] / "docs" / "presets"


class DeformationBindingTests(unittest.TestCase):
    def test_resolved_new_presets_match_both_literal_tables(self):
        for name in (
            "balanced-deformation-v1", "serial-deformation-v1",
        ):
            with self.subTest(preset=name):
                expected = json.loads((_PRESETS / f"{name}.json").read_text())
                self.assertEqual(
                    workflow.GltfExecution(preset=name).resolved_preset,
                    expected["values"],
                )

    def test_new_presets_admit_each_profile_without_source_upgrade(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "absent.gltf"
            for preset in ("balanced-deformation-v1", "serial-deformation-v1"):
                for profile in ("static-local-triangles-v1",
                                "direct-translation-v1", _PROFILE):
                    with self.subTest(preset=preset, profile=profile):
                        result = workflow.repair_gltf_file(
                            missing, None, (), primitive_ordinal=0,
                            triangles="require-triangles", values="raw",
                            processing_profile=profile,
                            execution=workflow.GltfExecution(preset=preset),
                        )
                        self.assertFalse(result.workflow_started)
                        self.assertEqual(result.processing_profile, profile)
                        if profile != _PROFILE:
                            self.assertIsNone(result.deformation_facts)
                            self.assertIsNone(result.deformation_options)
                        if profile != "direct-translation-v1":
                            self.assertIsNone(result.translation_facts)

    def test_old_presets_reject_new_profile_before_path_conversion(self):
        class UnreadPath:
            def __fspath__(self):
                raise AssertionError("preset rejection must precede source access")

        for name in (
            "balanced-v1", "serial-reference-v1",
            "balanced-direct-translation-v1", "serial-direct-translation-v1",
        ):
            with self.subTest(preset=name), self.assertRaises(ValueError):
                workflow.repair_gltf_file(
                    UnreadPath(), None, (),
                    primitive_ordinal=0, triangles="require-triangles",
                    values="raw", processing_profile=_PROFILE,
                    execution=workflow.GltfExecution(preset=name),
                )

    def test_configured_options_survive_capture_failure_with_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = workflow.repair_gltf_file(
                root / "absent.gltf", root / "bundle", (), primitive_ordinal=0,
                triangles="require-triangles", values="raw",
                processing_profile=_PROFILE)
            options = result.deformation_options
            self.assertEqual(len(options), 7)
            self.assertEqual(options["processing_profile"], _PROFILE)
            for key in ("deformation_limits", "deformation_work"):
                self.assertEqual(set(options[key]), set(_PHASES))
                self.assertIsNone(options[key]["source_read"])
                self.assertTrue(all(options[key][p] is not None
                                    for p in _PHASES if p != "source_read"))
            self.assertEqual(
                options["deformation_limits"]["capture_read"]["max_new_requested_bytes"],
                4194304)
            options["deformation_limits"]["capture_read"]["max_new_requested_bytes"] = 0
            self.assertEqual(result.deformation_options["deformation_limits"]
                             ["capture_read"]["max_new_requested_bytes"], 4194304)
            with self.assertRaises(AttributeError):
                result.deformation_options = {}
            self.assertFalse((root / "bundle").exists())

    def test_capture_failure_preserves_uncalled_slots_and_detaches_property(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "absent.gltf"
            for preset in (None, "balanced-deformation-v1", "serial-deformation-v1"):
                with self.subTest(preset=preset):
                    execution = (None if preset is None else
                                 workflow.GltfExecution(preset=preset))
                    result = workflow.repair_gltf_file(
                        missing, None, (), primitive_ordinal=0,
                        triangles="require-triangles", values="raw",
                        processing_profile=_PROFILE, execution=execution,
                    )
                    self.assertFalse(result.workflow_started)
                    self.assertEqual(result.processing_profile, _PROFILE)
                    self.assertIsNone(result.translation_facts)
                    options = result.deformation_options
                    self.assertEqual(len(options), 7)
                    for key in ("deformation_limits", "deformation_work"):
                        self.assertIsNone(options[key]["source_read"])
                        for phase in _PHASES[-3:]:
                            self.assertIsNone(options[key][phase])
                    facts = result.deformation_facts
                    self.assertEqual(len(facts), 15)
                    self.assertEqual(facts["profile"], _PROFILE)
                    self.assertTrue(all(facts[p] is None for p in _PHASES))
                    facts["normative_revision"]["commit"] = "caller mutation"
                    self.assertNotEqual(
                        result.deformation_facts["normative_revision"]["commit"],
                        "caller mutation",
                    )
                    with self.assertRaises(AttributeError):
                        result.deformation_facts = {}
                    self.assertFalse(missing.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)