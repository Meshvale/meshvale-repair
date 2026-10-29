# SPDX-License-Identifier: Apache-2.0
"""Actual installed missing TRS cells and one sparse CUBIC rotation pipeline."""
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


_FIXTURES = Path(__file__).resolve().parents[1] / "fixtures" / "gltf-deformation-animation-sparse"
_LITERAL = json.loads((_FIXTURES / "literal-expectations.json").read_bytes())
_STEMS = ("trs-step-linear", "translation-scale-cubic", "14-sparse-implicit-zero-derivatives")
_PHASES = (
    "capture_read", "source_read", "source", "materialization", "writing",
    "reload_read", "reload", "verification", "publication_read",
    "publication_reload", "publication_verification",
)
_REQUIRED = (
    "source-storage", "target-policy", "candidate-storage", "repair-correspondence",
    "canonical-preservation", "reload-selected-primitive", "retained-asset-preservation",
    "source-unchanged", "gltf.deformation.source-admission", "gltf.deformation.influences",
    "gltf.deformation.bindings", "gltf.deformation.morphs",
    "gltf.deformation.affecting-animation", "gltf.deformation.canonical-association",
    "gltf.deformation.output-reload", "gltf.deformation.staged-reload",
    "staged-selected-primitive", "staged-physical-members",
)
_CUBIC_ROWS = {
    "translation": [[0, 0, 0], [0, 0, 0], [0, 0, 0],
                    [0, 0, 0], [1065353216, 0, 0], [0, 0, 0]],
    "scale": [[0, 0, 0], [1065353216, 1065353216, 1065353216], [0, 0, 0],
              [0, 0, 0], [1073741824, 1073741824, 1073741824], [0, 0, 0]],
}


def document(wire, binary):
    # Float token spelling, including signed zero, is source data too.
    decode = lambda raw: json.loads(raw, parse_float=lambda token: ("decimal", token))
    if not binary:
        return decode(wire), ()
    if struct.unpack_from("<III", wire) != (0x46546C67, 2, len(wire)):
        raise AssertionError("actual GLB header/extent")
    offset = 12
    parts = []
    while offset < len(wire):
        size, kind = struct.unpack_from("<II", wire, offset)
        end = offset + 8 + size
        if size % 4 or end > len(wire):
            raise AssertionError("actual GLB chunk bounds")
        parts.append((kind, wire[offset + 8:end]))
        offset = end
    if offset != len(wire) or parts[0][0] != 0x4E4F534A:
        raise AssertionError("actual ordered JSON chunk")
    return decode(parts[0][1]), tuple(parts[1:])


def resources(asset, parts):
    result = []
    for index, buffer in enumerate(asset["buffers"]):
        if "uri" in buffer:
            prefix, payload = buffer["uri"].split(",", 1)
            if not prefix.endswith(";base64"):
                raise AssertionError("literal embedded buffer")
            raw = base64.b64decode(payload, validate=True)
        else:
            if index != 0 or parts[0][0] != 0x004E4942:
                raise AssertionError("literal first BIN binding")
            raw = parts[0][1]
        if len(raw) < buffer["byteLength"]:
            raise AssertionError("literal buffer extent")
        result.append(raw)
    return result


def float_rows(asset, backing, accessor_index):
    """Independent raw-word oracle for these FLOAT scalar/vector fixtures only."""
    accessor = asset["accessors"][accessor_index]
    width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    if accessor["componentType"] != 5126 or accessor.get("normalized", False):
        raise AssertionError("literal nonnormalized FLOAT accessor")
    rows = [[0] * width for _ in range(accessor["count"])]
    if "bufferView" in accessor:
        view = asset["bufferViews"][accessor["bufferView"]]
        raw = backing[view["buffer"]]
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        stride = view.get("byteStride", width * 4)
        if stride < width * 4 or (
            rows and start + (len(rows) - 1) * stride + width * 4
            > view.get("byteOffset", 0) + view["byteLength"]
        ):
            raise AssertionError("literal dense view extent")
        for row in range(len(rows)):
            begin = start + row * stride
            if begin + width * 4 > len(raw):
                raise AssertionError("literal dense row extent")
            rows[row] = list(struct.unpack_from("<" + "I" * width, raw, begin))
    selectors = []
    if "sparse" in accessor:
        sparse = accessor["sparse"]
        if sparse["indices"]["componentType"] != 5121:
            raise AssertionError("literal U8 sparse selectors")
        selector_view = asset["bufferViews"][sparse["indices"]["bufferView"]]
        value_view = asset["bufferViews"][sparse["values"]["bufferView"]]
        selector_start = selector_view.get("byteOffset", 0) + sparse["indices"].get("byteOffset", 0)
        value_start = value_view.get("byteOffset", 0) + sparse["values"].get("byteOffset", 0)
        selector_raw = backing[selector_view["buffer"]]
        value_raw = backing[value_view["buffer"]]
        if (
            selector_start + sparse["count"]
            > selector_view.get("byteOffset", 0) + selector_view["byteLength"]
            or value_start + sparse["count"] * width * 4
            > value_view.get("byteOffset", 0) + value_view["byteLength"]
        ):
            raise AssertionError("literal sparse view extent")
        for i in range(sparse["count"]):
            if selector_start + i >= len(selector_raw) or value_start + (i + 1) * width * 4 > len(value_raw):
                raise AssertionError("literal sparse extent")
            destination = selector_raw[selector_start + i]
            if destination >= len(rows) or (selectors and destination <= selectors[-1]):
                raise AssertionError("literal sorted sparse destination")
            selectors.append(destination)
            rows[destination] = list(struct.unpack_from(
                "<" + "I" * width, value_raw, value_start + i * width * 4,
            ))
    return rows, selectors


def animation_cells(asset):
    result = {}
    for animation in asset["animations"]:
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            result[channel["target"]["path"]] = sampler
    return result


class DeformationAnimationSparseEndToEndTests(unittest.TestCase):
    def _case(self, stem, binary, edit):
        suffix = ".glb.hex" if binary else ".gltf"
        path = _FIXTURES / (stem + suffix)
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), _LITERAL["assets"][path.name]["sha256"])
        wire = bytes.fromhex(raw.decode()) if binary else raw
        original, original_parts = document(wire, binary)
        backing = resources(original, original_parts)
        cells = animation_cells(original)
        sparse = stem.startswith("14-")
        if stem == "trs-step-linear":
            for role, mode in (("translation", "STEP"), ("rotation", "STEP"), ("scale", "LINEAR")):
                self.assertEqual(cells[role].get("interpolation", "LINEAR"), mode)
        if stem == "translation-scale-cubic":
            for role, expected in _CUBIC_ROWS.items():
                self.assertEqual(cells[role]["interpolation"], "CUBICSPLINE")
                self.assertEqual(float_rows(original, backing, cells[role]["output"]), (expected, []))
                self.assertEqual(float_rows(original, backing, cells[role]["input"])[0], [[0], [1065353216]])
        if sparse:
            self.assertEqual(cells["rotation"]["interpolation"], "CUBICSPLINE")
            self.assertNotIn("bufferView", original["accessors"][cells["rotation"]["output"]])
            self.assertEqual(float_rows(original, backing, cells["rotation"]["output"]), (
                _LITERAL["sparse_expected"]["row_words"], [1, 4],
            ))
        handles = (workflow._DEFAULT_EXECUTION, workflow._DEFAULT_TRANSLATION_EXECUTION,
                   workflow._DEFAULT_DEFORMATION_EXECUTION)
        baseline = tuple(h.active_tracked_payload for h in handles)
        self.assertEqual(baseline, (baseline[0],) * 3)
        result = None
        try:
            with tempfile.TemporaryDirectory(prefix="meshvale-animation-sparse-") as directory:
                source = Path(directory).resolve() / "source.gltf"
                destination = source.parent / "bundle"
                source.write_bytes(wire)
                result = workflow.repair_gltf_file(
                    source, destination, [(0, 1)] if edit else [], primitive_ordinal=0,
                    triangles="require-triangles", values="raw-and-normalized",
                    processing_profile="deformation-retained-v1",
                    row_local_attributes=[] if sparse else _LITERAL["row_local_attributes"],
                )
                self.assertTrue(result.workflow_started)
                self.assertEqual(result.status.name, "kSuccess")
                self.assertEqual(source.read_bytes(), wire)
                facts = result.deformation_facts
                options = result.deformation_options
                for phase in _PHASES:
                    if phase == "source_read":
                        self.assertIsNone(facts[phase])
                        self.assertIsNone(options["deformation_limits"][phase])
                    else:
                        self.assertIsNotNone(facts[phase])
                        self.assertTrue(facts[phase]["complete"])
                        self.assertEqual(facts[phase]["status"], 0)
                        self.assertIsNotNone(options["deformation_limits"][phase])
                for phase in ("source", "writing", "reload", "verification", "publication_reload", "publication_verification"):
                    for predicate in ("influences", "bindings", "morphs", "affecting_animation"):
                        self.assertIs(facts[phase]["predicates"][predicate], True)
                for phase in ("materialization", "writing", "verification", "publication_verification"):
                    self.assertIs(facts[phase]["predicates"]["canonical_association"], True)
                for phase in ("capture_read", "reload_read", "reload", "publication_read", "publication_reload"):
                    self.assertIs(facts[phase]["predicates"]["source_admission"], True)
                self.assertIs(facts["verification"]["predicates"]["output_reload"], True)
                self.assertIsNone(facts["verification"]["predicates"]["staged_reload"])
                self.assertIs(facts["publication_verification"]["predicates"]["staged_reload"], True)
                if sparse:
                    for phase in ("source", "reload", "publication_reload"):
                        self.assertEqual(facts[phase]["counts"]["sparse_writes"], 2)
                        quaternion = facts[phase]["quaternions"]
                        self.assertTrue(quaternion["complete"])
                        self.assertEqual(quaternion["animation_values_checked"], 2)
                        self.assertEqual(quaternion["tangent_rows_checked"], 4)
                        self.assertIsNone(quaternion["first_refusal"])
                for name, values in _LITERAL["edited_maps" if edit else "identity_maps"].items():
                    self.assertEqual(getattr(result, name), tuple(values))
                report_bytes = result.report_json
                report = load(report_bytes)
                self.assertEqual(exit_code(report), 0)
                self.assertEqual(report["profile"]["outcome"], "passed")
                self.assertEqual(report["profile"]["required_checks"], list(_REQUIRED))
                coverage = {entry["id"]: entry for entry in report["coverage"]}
                for check in _REQUIRED:
                    self.assertEqual((coverage[check]["status"], coverage[check]["finding"]), ("performed", "passed"))
                self.assertEqual(report["extensions"]["meshvale.repair.gltf"]["processing"], facts)
                physical = {p.relative_to(destination).as_posix(): p.read_bytes()
                            for p in destination.rglob("*") if p.is_file()}
                self.assertEqual(physical["receipts/report.json"], report_bytes)
                members = report["extensions"]["meshvale.repair.gltf.file"]["artifact_paths"]
                self.assertEqual(set(physical), {row["path"] for row in members} | {"receipts/report.json"})
                self.assertEqual(report["publication"]["artifacts"], [
                    {"id": row["id"], "sha256": hashlib.sha256(physical[row["path"]]).hexdigest()}
                    for row in members
                ])
                actual, actual_parts = document(physical["source.gltf"], binary)
                self.assertEqual(actual_parts, original_parts)
                self.assertEqual(resources(actual, actual_parts)[:len(backing)], backing)
                if edit:
                    for table in ("buffers", "bufferViews", "accessors"):
                        self.assertEqual(len(actual[table]), len(original[table]) + 1)
                        actual[table].pop()
                    primitive = actual["meshes"][0]["primitives"][0]
                    primitive["indices"] = original["meshes"][0]["primitives"][0]["indices"]
                    if "mode" not in original["meshes"][0]["primitives"][0]:
                        self.assertEqual(primitive.pop("mode", 4), 4)
                else:
                    self.assertEqual(physical["source.gltf"], wire)
                self.assertEqual(actual, original)
                if sparse:
                    self.assertEqual(float_rows(actual, backing, cells["rotation"]["output"]), (
                        _LITERAL["sparse_expected"]["row_words"], [1, 4],
                    ))
                live = tuple(h.active_tracked_payload for h in handles)
                self.assertEqual(live, (live[0],) * 3)
                self.assertGreater(live[0], baseline[0])
        finally:
            result = None
            gc.collect()
            self.assertEqual(tuple(h.active_tracked_payload for h in handles), baseline)
            self.assertEqual(tuple(h.active_workers for h in handles), (0, 0, 0))

    def test_missing_trs_cells_identity_and_edit(self):
        for stem in _STEMS[:2]:
            for binary in (False, True):
                for edit in (False, True):
                    with self.subTest(stem=stem, binary=binary, edit=edit):
                        self._case(stem, binary, edit)

    def test_actual_sparse_rotation_identity_and_edit(self):
        for binary in (False, True):
            for edit in (False, True):
                with self.subTest(binary=binary, edit=edit):
                    self._case(_STEMS[2], binary, edit)


if __name__ == "__main__":
    unittest.main()
