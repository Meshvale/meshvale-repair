# SPDX-License-Identifier: Apache-2.0
"""Actual installed deformation pipeline regressions for a qualified producer.

These tests require matching native producers and the report-reader dependency.
They exercise real assets and never substitute observations or execution contexts.
"""
import base64
import gc
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from meshvale_repair import gltf_workflow as workflow
from meshvale_reports import exit_code, load


_FIXTURES = Path(__file__).resolve().parents[1] / "fixtures" / "gltf-deformation-end-to-end"
_PROFILE = "deformation-retained-v1"
_PHASES = (
    "capture_read", "source_read", "source", "materialization", "writing",
    "reload_read", "reload", "verification", "publication_read",
    "publication_reload", "publication_verification",
)
_FAMILIES = ("influences", "bindings", "morphs", "affecting_animation")
_REQUIRED_MEMORY = (
    "source-storage", "target-policy", "candidate-storage",
    "repair-correspondence", "canonical-preservation", "reload-selected-primitive",
    "retained-asset-preservation", "source-unchanged",
    "gltf.deformation.source-admission", "gltf.deformation.influences",
    "gltf.deformation.bindings", "gltf.deformation.morphs",
    "gltf.deformation.affecting-animation", "gltf.deformation.canonical-association",
    "gltf.deformation.output-reload",
)
_REQUIRED_FILE = (
    "gltf.deformation.staged-reload", "staged-selected-primitive",
    "staged-physical-members",
)


def fixture_bytes(binary):
    suffix = "glb.hex" if binary else "gltf"
    path = _FIXTURES / f"d04-dense-float-0-2-5-stride.{suffix}"
    return bytes.fromhex(path.read_text()) if binary else path.read_bytes()


def document_and_first_buffer(wire, binary):
    """Small independent fixture decoder, not a product reader/proof substitute."""
    if binary:
        magic, version, size = struct.unpack_from("<III", wire)
        if (magic, version, size) != (0x46546C67, 2, len(wire)):
            raise AssertionError("literal GLB header")
        json_size, kind = struct.unpack_from("<II", wire, 12)
        if kind != 0x4E4F534A:
            raise AssertionError("literal JSON chunk")
        document = json.loads(wire[20:20 + json_size])
        bin_offset = 20 + json_size
        bin_size, kind = struct.unpack_from("<II", wire, bin_offset)
        if kind != 0x004E4942 or bin_offset + 8 + bin_size > len(wire):
            raise AssertionError("literal BIN extent")
        return document, wire[bin_offset + 8:bin_offset + 8 + bin_size]
    document = json.loads(wire)
    return document, base64.b64decode(document["buffers"][0]["uri"].split(",", 1)[1], validate=True)


class DeformationEndToEndTests(unittest.TestCase):
    def _case(self, *, binary, edit, publish, values="raw-and-normalized"):
        handles = (
            workflow._DEFAULT_EXECUTION,
            workflow._DEFAULT_TRANSLATION_EXECUTION,
            workflow._DEFAULT_DEFORMATION_EXECUTION,
        )
        baseline = tuple(handle.active_tracked_payload for handle in handles)
        self.assertEqual(baseline, (baseline[0],) * 3)
        result = None
        try:
            with tempfile.TemporaryDirectory(prefix="meshvale-deformation-") as directory:
                root = Path(directory).resolve()
                source = root / "source.gltf"
                destination = root / "bundle" if publish else None
                wire = fixture_bytes(binary)
                source.write_bytes(wire)
                result = workflow.repair_gltf_file(
                    source, destination, [(0, 1)] if edit else [],
                    primitive_ordinal=0, triangles="require-triangles", values=values,
                    processing_profile=_PROFILE,
                )
                self.assertTrue(result.workflow_started)
                self.assertEqual(result.status.name, "kSuccess")
                self.assertEqual(source.read_bytes(), wire)
                facts = result.deformation_facts
                options = result.deformation_options
                self.assertEqual(facts["profile"], _PROFILE)
                self.assertIsNone(facts["source_read"])
                self.assertIsNone(options["deformation_limits"]["source_read"])
                for phase in _PHASES:
                    applicable = phase != "source_read" and (
                        publish or not phase.startswith("publication_")
                    )
                    with self.subTest(phase=phase):
                        if applicable:
                            self.assertIsNotNone(facts[phase])
                            self.assertTrue(facts[phase]["complete"])
                            self.assertEqual(facts[phase]["status"], 0)
                            self.assertIsNotNone(options["deformation_limits"][phase])
                        else:
                            self.assertIsNone(facts[phase])
                            self.assertIsNone(options["deformation_limits"][phase])
                for phase in ("source", "writing", "reload", "verification") + (
                    ("publication_reload", "publication_verification") if publish else ()
                ):
                    for predicate in _FAMILIES:
                        self.assertIs(facts[phase]["predicates"][predicate], True)
                for phase in ("materialization", "writing", "verification") + (
                    ("publication_verification",) if publish else ()
                ):
                    self.assertIs(facts[phase]["predicates"]["canonical_association"], True)
                for phase in ("capture_read", "reload_read", "reload") + (
                    ("publication_read", "publication_reload") if publish else ()
                ):
                    self.assertIs(facts[phase]["predicates"]["source_admission"], True)
                self.assertIs(facts["verification"]["predicates"]["output_reload"], True)
                self.assertIsNone(facts["verification"]["predicates"]["staged_reload"])
                if publish:
                    self.assertIs(
                        facts["publication_verification"]["predicates"]["staged_reload"], True,
                    )
                # Dense VEC4 zero lanes are retained, not converted into absent rows.
                for phase in ("source", "reload") + (("publication_reload",) if publish else ()):
                    sums = facts[phase]["weight_sums"]
                    self.assertTrue(sums["complete"])
                    self.assertEqual(sums["floating_rows_expected"], 3)
                    self.assertEqual(sums["rows_evaluated"], 3)
                    self.assertEqual(sums["advisory_rows"], 2)
                    self.assertEqual(sums["all_zero_rows"], 1)
                self.assertEqual(result.face_map, (0, 0, 1) if edit else (0, 1, 2))
                self.assertEqual(
                    result.corner_map,
                    (0, 1, 2, 1, 2, 0, 3, 4, 5) if edit else tuple(range(9)),
                )
                self.assertEqual(result.source_faces, (0, 2) if edit else (0, 1, 2))
                self.assertEqual(
                    result.source_corners, (0, 1, 2, 6, 7, 8) if edit else tuple(range(9)),
                )
                payload = result.report_json
                report = load(payload)
                self.assertEqual(exit_code(report), 0)
                self.assertEqual(report["profile"]["outcome"], "passed")
                coverage = {entry["id"]: entry for entry in report["coverage"]}
                required = _REQUIRED_MEMORY + (_REQUIRED_FILE if publish else ())
                self.assertEqual(report["profile"]["required_checks"], list(required))
                for check in required:
                    self.assertEqual(
                        (coverage[check]["status"], coverage[check]["finding"]),
                        ("performed", "passed"),
                    )
                self.assertEqual(report["extensions"]["meshvale.repair.gltf"]["processing"], facts)
                if publish:
                    self.assertTrue(destination.is_dir())
                    self.assertEqual((destination / "receipts/report.json").read_bytes(), payload)
                    self.assertTrue((destination / "source.gltf").is_file())
                    published_wire = (destination / "source.gltf").read_bytes()
                    original, original_buffer = document_and_first_buffer(wire, binary)
                    published, published_buffer = document_and_first_buffer(published_wire, binary)
                    self.assertEqual(published_buffer, original_buffer)
                    self.assertEqual(published["nodes"], original["nodes"])
                    self.assertEqual(published["skins"], original["skins"])
                    for table in ("accessors", "bufferViews"):
                        self.assertEqual(published[table][:len(original[table])], original[table])
                    self.assertEqual(
                        published["meshes"][0]["primitives"][0]["attributes"],
                        original["meshes"][0]["primitives"][0]["attributes"],
                    )
                    physical = {
                        path.relative_to(destination).as_posix(): path.read_bytes()
                        for path in destination.rglob("*") if path.is_file()
                    }
                    file_facts = report["extensions"]["meshvale.repair.gltf.file"]
                    artifact_paths = file_facts["artifact_paths"]
                    self.assertEqual(
                        {row["path"] for row in artifact_paths} | {"receipts/report.json"},
                        set(physical),
                    )
                    self.assertEqual(report["publication"]["artifacts"], [
                        {"id": row["id"],
                         "sha256": hashlib.sha256(physical[row["path"]]).hexdigest()}
                        for row in artifact_paths
                    ])
                    if not edit:
                        self.assertEqual((destination / "source.gltf").read_bytes(), wire)
                else:
                    self.assertEqual(sorted(path.name for path in root.iterdir()), ["source.gltf"])
                live = tuple(handle.active_tracked_payload for handle in handles)
                self.assertEqual(live, (live[0],) * 3)
                self.assertGreater(live[0], baseline[0])
                independent = workflow.GltfExecution(preset="balanced-deformation-v1")
                self.assertEqual(independent.active_tracked_payload, 0)
        finally:
            result = None
            gc.collect()
            self.assertEqual(tuple(handle.active_tracked_payload for handle in handles), baseline)
            self.assertEqual(tuple(handle.active_workers for handle in handles), (0, 0, 0))

    def test_minimal_identity_without_publication(self):
        self._case(binary=False, edit=False, publish=False)

    def test_actual_json_glb_publication_identity_and_edit(self):
        for binary in (False, True):
            for edit in (False, True):
                with self.subTest(binary=binary, edit=edit):
                    self._case(binary=binary, edit=edit, publish=True)

    def test_raw_value_policy_has_same_required_predicates(self):
        self._case(binary=False, edit=False, publish=False, values="raw")


if __name__ == "__main__":
    unittest.main()
