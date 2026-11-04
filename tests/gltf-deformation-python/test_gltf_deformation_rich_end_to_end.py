# SPDX-License-Identifier: Apache-2.0
"""Real installed rich catalog/write/memory and staged reload regressions.

Requires matching qualified native producers. Literal input data is independent
of product results; no observations, proofs or execution contexts are replaced.
"""
import gc
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from meshvale_repair import gltf_workflow as workflow
from meshvale_reports import exit_code, load


_FIXTURES = Path(__file__).resolve().parents[1] / "fixtures" / "gltf-deformation-rich-end-to-end"
_LITERALS = json.loads((_FIXTURES / "literal-expectations.json").read_text())
_PHASES = (
    "capture_read", "source_read", "source", "materialization", "writing",
    "reload_read", "reload", "verification", "publication_read",
    "publication_reload", "publication_verification",
)
_FAMILIES = ("influences", "bindings", "morphs", "affecting_animation")
_MEMORY_CHECKS = (
    "source-storage", "target-policy", "candidate-storage", "repair-correspondence",
    "canonical-preservation", "reload-selected-primitive", "retained-asset-preservation",
    "source-unchanged", "gltf.deformation.source-admission", "gltf.deformation.influences",
    "gltf.deformation.bindings", "gltf.deformation.morphs",
    "gltf.deformation.affecting-animation", "gltf.deformation.canonical-association",
    "gltf.deformation.output-reload",
)
_FILE_CHECKS = (
    "gltf.deformation.staged-reload", "staged-selected-primitive", "staged-physical-members",
)


def asset_bytes(stem, binary):
    path = _FIXTURES / (stem + (".glb.hex" if binary else ".gltf"))
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != _LITERALS["asset_file_sha256"][path.name]:
        raise AssertionError("literal input identity changed")
    return bytes.fromhex(raw.decode()) if binary else raw


def document_and_chunks(wire, binary):
    # Preserve every original floating token spelling in this independent oracle.
    decode = lambda raw: json.loads(raw, parse_float=lambda token: ("decimal", token))
    if not binary:
        return decode(wire), ()
    if struct.unpack_from("<III", wire) != (0x46546C67, 2, len(wire)):
        raise AssertionError("literal complete GLB extent")
    offset = 12
    chunks = []
    while offset < len(wire):
        size, kind = struct.unpack_from("<II", wire, offset)
        end = offset + 8 + size
        if size % 4 or end > len(wire):
            raise AssertionError("literal GLB chunk extent")
        chunks.append((kind, wire[offset + 8:end]))
        offset = end
    if offset != len(wire) or not chunks or chunks[0][0] != 0x4E4F534A:
        raise AssertionError("literal ordered GLB chunks")
    return decode(chunks[0][1]), tuple(chunks[1:])


class DeformationRichEndToEndTests(unittest.TestCase):
    def _case(self, stem, *, binary, edit=False, publish=True, prepare_mutation=False):
        handles = (
            workflow._DEFAULT_EXECUTION, workflow._DEFAULT_TRANSLATION_EXECUTION,
            workflow._DEFAULT_DEFORMATION_EXECUTION,
        )
        baseline = tuple(handle.active_tracked_payload for handle in handles)
        self.assertEqual(baseline, (baseline[0],) * 3)
        wire = asset_bytes(stem, binary)
        result = None
        try:
            with tempfile.TemporaryDirectory(prefix="meshvale-rich-deformation-") as directory:
                root = Path(directory).resolve()
                source = root / "source.gltf"
                source.write_bytes(wire)
                destination = root / "bundle" if publish else None
                declarations = _LITERALS["row_local_attributes"] if stem.startswith("catalog-") else []
                # DuplicateTarget is (keep, remove); source successor has two
                # identical directed loops on the same three original vertices.
                arguments = dict(
                    primitive_ordinal=0, triangles="require-triangles",
                    values="raw-and-normalized", processing_profile="deformation-retained-v1",
                    row_local_attributes=declarations,
                )
                if prepare_mutation:
                    caller_rows = [
                        [domain, name.encode("utf-8").decode("utf-8")]
                        for domain, name in declarations
                    ]
                    self.assertTrue(all(
                        row[1] is not expected[1]
                        for row, expected in zip(caller_rows, declarations)
                    ))
                    arguments["row_local_attributes"] = caller_rows
                    result = workflow._prepare_gltf_request(
                        source, destination, [(0, 1)] if edit else [], **arguments,
                    )
                    for row in caller_rows:
                        row[1] = "changed-after-prepare"
                    caller_rows.clear()
                    del row, arguments, caller_rows
                    gc.collect()
                    result = workflow._run_gltf_request(result)
                else:
                    result = workflow.repair_gltf_file(
                        source, destination, [(0, 1)] if edit else [], **arguments,
                    )
                self.assertTrue(result.workflow_started)
                self.assertEqual(result.status.name, "kSuccess")
                self.assertEqual(source.read_bytes(), wire)
                facts = result.deformation_facts
                options = result.deformation_options
                for phase in _PHASES:
                    applicable = phase != "source_read" and (
                        publish or not phase.startswith("publication_")
                    )
                    with self.subTest(phase=phase):
                        if applicable:
                            self.assertIsNotNone(facts[phase])
                            self.assertIs(facts[phase]["complete"], True)
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
                for phase in ("source", "reload") + (("publication_reload",) if publish else ()):
                    self.assertGreater(facts[phase]["counts"]["morph_targets"], 0)
                    self.assertGreater(facts[phase]["counts"]["animation_channels"], 0)
                    if stem.startswith("catalog-"):
                        self.assertGreater(facts[phase]["counts"]["skin_rows"], 0)
                        self.assertGreater(facts[phase]["counts"]["mesh_users"], 1)
                prefix = "duplicate" if edit else "identity"
                for name in ("face_map", "corner_map"):
                    self.assertEqual(getattr(result, name), tuple(_LITERALS[prefix + "_" + name]))
                self.assertEqual(result.source_faces, (0,))
                self.assertEqual(result.source_corners, (0, 1, 2))
                payload = result.report_json
                report = load(payload)
                self.assertEqual(exit_code(report), 0)
                self.assertEqual(report["profile"]["outcome"], "passed")
                required = _MEMORY_CHECKS + (_FILE_CHECKS if publish else ())
                self.assertEqual(report["profile"]["required_checks"], list(required))
                coverage = {entry["id"]: entry for entry in report["coverage"]}
                for check in required:
                    self.assertEqual(
                        (coverage[check]["status"], coverage[check]["finding"]),
                        ("performed", "passed"),
                    )
                self.assertEqual(report["extensions"]["meshvale.repair.gltf"]["processing"], facts)
                if publish:
                    physical = {
                        path.relative_to(destination).as_posix(): path.read_bytes()
                        for path in destination.rglob("*") if path.is_file()
                    }
                    self.assertEqual(physical["receipts/report.json"], payload)
                    members = report["extensions"]["meshvale.repair.gltf.file"]["artifact_paths"]
                    self.assertEqual({row["path"] for row in members} | {"receipts/report.json"}, set(physical))
                    self.assertEqual(report["publication"]["artifacts"], [
                        {"id": row["id"], "sha256": hashlib.sha256(physical[row["path"]]).hexdigest()}
                        for row in members
                    ])
                    published_wire = physical["source.gltf"]
                    expected, original_chunks = document_and_chunks(wire, binary)
                    actual, actual_chunks = document_and_chunks(published_wire, binary)
                    self.assertEqual(actual_chunks, original_chunks)
                    if edit:
                        for table in ("buffers", "bufferViews", "accessors"):
                            self.assertEqual(len(actual[table]), len(expected[table]) + 1)
                            actual[table].pop()
                        primitive = actual["meshes"][0]["primitives"][0]
                        primitive["indices"] = expected["meshes"][0]["primitives"][0]["indices"]
                        if "mode" not in expected["meshes"][0]["primitives"][0]:
                            self.assertEqual(primitive.pop("mode", 4), 4)
                    else:
                        self.assertEqual(published_wire, wire)
                    # This covers every original raw resource and source
                    # catalog/user/IBM, morph/default, TRS and animation token.
                    self.assertEqual(actual, expected)
                else:
                    self.assertEqual([path.name for path in root.iterdir()], ["source.gltf"])
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

    def test_catalog_identity_without_publication(self):
        for binary in (False, True):
            with self.subTest(binary=binary):
                self._case("catalog-complete-stride", binary=binary, publish=False)

    def test_catalog_identity_and_literal_duplicate_publication(self):
        for binary in (False, True):
            for edit in (False, True):
                with self.subTest(binary=binary, edit=edit):
                    self._case("catalog-duplicate-stride" if edit else "catalog-complete-stride", binary=binary, edit=edit)

    def test_matrix_step_and_cubic_weights_publication(self):
        for stem in ("weights-step", "weights-cubic"):
            for binary in (False, True):
                with self.subTest(stem=stem, binary=binary):
                    self._case(stem, binary=binary)

    def test_prepared_declarations_own_their_names(self):
        self._case("catalog-complete-stride", binary=False, publish=False, prepare_mutation=True)

    def test_face_and_corner_assertions_never_become_vertex_assertions(self):
        handles = (
            workflow._DEFAULT_EXECUTION, workflow._DEFAULT_TRANSLATION_EXECUTION,
            workflow._DEFAULT_DEFORMATION_EXECUTION,
        )
        baseline = tuple(handle.active_tracked_payload for handle in handles)
        for domain in ("face", "corner"):
            result = None
            try:
                with self.subTest(domain=domain), tempfile.TemporaryDirectory() as directory:
                    source = Path(directory).resolve() / "source.gltf"
                    wire = asset_bytes("catalog-complete-stride", False)
                    source.write_bytes(wire)
                    result = workflow.repair_gltf_file(
                        source, None, [], primitive_ordinal=0,
                        triangles="require-triangles", values="raw-and-normalized",
                        processing_profile="deformation-retained-v1",
                        row_local_attributes=[
                            [domain, name] for _, name in _LITERALS["row_local_attributes"]
                        ],
                    )
                    self.assertTrue(result.workflow_started)
                    self.assertEqual(result.status.name, "kUnsupported")
                    self.assertEqual(source.read_bytes(), wire)
                    facts = result.deformation_facts
                    self.assertIsNotNone(facts["source"])
                    self.assertEqual(facts["source"]["diagnostic"]["rule_id"], 311)
                    for phase in _PHASES[3:]:
                        self.assertIsNone(facts[phase])
            finally:
                result = None
                gc.collect()
                self.assertEqual(tuple(handle.active_tracked_payload for handle in handles), baseline)
                self.assertEqual(tuple(handle.active_workers for handle in handles), (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
