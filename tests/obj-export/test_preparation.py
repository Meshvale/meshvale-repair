# SPDX-License-Identifier: Apache-2.0
"""Actual native conversion and deliberately labeled consistency controls."""
from array import array
from dataclasses import FrozenInstanceError, replace
from pathlib import Path
import unittest

import meshvale_geometry as geometry
import meshvale_interchange as interchange
from meshvale_repair._obj_export import (
    ExportPreparationError, ObjCancellation, geometry_cancellation, io_cancellation,
    prepare_triangle_asset, resolve_export)


SOURCE = """mtllib panel.mtl
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
vt .25 0
vt 1 0
vt 1 1
vt .25 1
vt 0 0
vt 1 0
vt 1 1
vt .5 .5
vt 0 1
vn 0 0 1
o panel
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
MTL = b"newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\nnewmtl green\nKd 0 1 0\n"
FACE_MAP = [0, [1, 2], [3, 4, 5]]
CORNER_MAP = [0, 1, 2, 4, [5, 6], 7, [3, 8], [12, 15], [9, 13], 10, [11, 14, 16], 17]
CORNER_SOURCES = [0, 1, 2, 6, 3, 4, 4, 5, 6, 8, 9, 10, 7, 8, 10, 7, 10, 11]
TRIANGLE_VERTICES = [0, 1, 2, 2, 1, 3, 3, 4, 2, 6, 7, 8, 5, 6, 8, 5, 8, 9]


def asset(text=SOURCE):
    imported = interchange.read_obj(text, MTL)
    if imported.document is None:
        raise AssertionError(imported.diagnostics)
    return interchange.ObjFileAsset(imported.document, Path("model.obj"), (Path("panel.mtl"),),
                                    (interchange.ObjResource(Path("panel.mtl"), MTL),))


def native_request(workers=1, payload=8 * 1024 * 1024, corner_limit=4096):
    return resolve_export("triangles", geometry.TriangulationOptions(
        max_corners_per_face=corner_limit, minimum_parallel_faces=0),
        geometry.ExecutionContext(worker_budget=workers, tracked_payload_budget_bytes=payload), None)


def words(values):
    return memoryview(array("Q", values).tobytes()).cast("Q")


class PreparationTests(unittest.TestCase):
    def accepted(self, source=None, workers=1):
        source = asset() if source is None else source
        request = native_request(workers)
        result = request.convert(source.document.mesh, None)
        self.assertEqual(result.status, "accepted")
        self.assertEqual(request.execution.active_workers, 0)
        self.assertEqual(request.execution.active_tracked_payload_bytes, 0)
        return source, result

    def test_preserve_and_strict_admission(self):
        legacy = interchange.Cancellation()
        coupled = ObjCancellation()
        for token in (None, legacy, coupled):
            self.assertIsNone(resolve_export("preserve", None, None, token))
        for topology in (None, True, 1, b"triangles"):
            with self.subTest(topology=topology), self.assertRaises(TypeError):
                resolve_export(topology, None, None, None)
        with self.assertRaises(ValueError):
            resolve_export("fan", None, None, None)
        with self.assertRaises(ValueError):
            resolve_export("preserve", geometry.TriangulationOptions(), None, None)
        with self.assertRaises(ValueError):
            resolve_export("preserve", None, geometry.ExecutionContext(), None)
        for token in (legacy, geometry.Cancellation(), object()):
            with self.subTest(token=type(token)), self.assertRaises(TypeError):
                resolve_export("triangles", None, None, token)
        for options in ({}, True, object()):
            with self.subTest(options=options), self.assertRaises(TypeError):
                resolve_export("triangles", options, None, None)
        with self.assertRaises(TypeError):
            resolve_export("triangles", None, 2, None)
        poisoned = geometry.TriangulationOptions()
        object.__setattr__(poisoned, "max_corners_per_face", True)
        with self.assertRaises(TypeError):
            resolve_export("triangles", poisoned, None, None)

    def test_resolved_options_use_supplied_context(self):
        context = geometry.ExecutionContext(worker_budget=2, tracked_payload_budget_bytes=1234567)
        request = resolve_export("triangles", geometry.TriangulationOptions(64, 7), context, None)
        self.assertIs(request.execution, context)
        self.assertEqual(request.resolved_options(), {"max_corners_per_face": 64,
            "minimum_parallel_faces": 7, "worker_budget": 2, "tracked_payload_budget_bytes": 1234567})
        request.resolved_options()["worker_budget"] = 999
        self.assertEqual(request.resolved_options()["worker_budget"], 2)
        automatic = resolve_export("triangles", None, None, None)
        self.assertGreaterEqual(automatic.resolved_options()["worker_budget"], 1)
        with self.assertRaises(FrozenInstanceError):
            request.options = geometry.TriangulationOptions()

    def test_coupled_stop_reaches_both_real_sources(self):
        token = ObjCancellation()
        self.assertIs(type(io_cancellation(token)), interchange.Cancellation)
        self.assertIs(type(geometry_cancellation(token)), geometry.Cancellation)
        self.assertFalse(token.stop_requested)
        self.assertTrue(token.request_stop())
        self.assertFalse(token.request_stop())
        self.assertTrue(io_cancellation(token).stop_requested)
        self.assertTrue(geometry_cancellation(token).stop_requested)
        with self.assertRaises(AttributeError):
            token.stop_requested = False
        source = asset()
        result = native_request().convert(source.document.mesh, token)
        self.assertEqual(result.status, "canceled")
        self.assertIsNone(result.candidate)
        self.assertEqual(list(result.corner_sources), [])
        with self.assertRaises(ExportPreparationError):
            prepare_triangle_asset(source, result)

    def test_literal_maps_assets_and_real_two_worker_agreement(self):
        source = asset()
        before = source.document.mesh.to_record()
        plans = []
        for workers in (1, 2):
            original, result = self.accepted(source, workers)
            plan = prepare_triangle_asset(original, result)
            self.assertEqual(result.workers_used, 0 if workers == 1 else 2)
            self.assertEqual(list(result.corner_sources), CORNER_SOURCES)
            self.assertEqual(plan.face_map, FACE_MAP)
            self.assertEqual(plan.corner_map, CORNER_MAP)
            self.assertTrue(plan.topology_changed)
            self.assertEqual(list(plan.asset.document.mesh.to_record()["corner_vertices"]), TRIANGLE_VERTICES)
            self.assertEqual(plan.asset.document.parts, source.document.parts)
            self.assertEqual(plan.asset.document.material_names, ("red", "blue", "green"))
            self.assertEqual(plan.asset.resources, source.resources)
            plans.append(plan)
        self.assertEqual(plans[0].asset.document.mesh.to_record(), plans[1].asset.document.mesh.to_record())
        self.assertEqual(source.document.mesh.to_record(), before)

    def test_triangle_and_empty_actual_identity(self):
        for source in (asset(SOURCE.split("g quad")[0]),
                       interchange.ObjFileAsset(interchange.ObjDocument(geometry.Mesh()), Path("empty.obj"))):
            source, result = self.accepted(source)
            plan = prepare_triangle_asset(source, result)
            self.assertIsNone(plan.face_map)
            self.assertIsNone(plan.corner_map)
            self.assertFalse(plan.topology_changed)
            self.assertEqual(plan.asset.document.mesh.to_record(), source.document.mesh.to_record())
        self.assertEqual(list(result.face_output_offsets), [0])

    def test_actual_native_profile_and_budget_refusals(self):
        cases = [(asset(SOURCE.replace("v 2 1 0\n", "v 2 1 .25\n")), native_request(), "conversion.nonplanar_face"),
                 (asset(), native_request(corner_limit=4), "conversion.face_corner_limit"),
                 (asset(), native_request(payload=0), "conversion.payload_budget")]
        for source, request, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                result = request.convert(source.document.mesh, None)
                self.assertEqual(result.status, "blocked")
                self.assertIn(diagnostic, [row.code for row in result.diagnostics])
                self.assertIsNone(result.candidate)
                self.assertEqual(list(result.face_sources), [])
                with self.assertRaises(ExportPreparationError):
                    prepare_triangle_asset(source, result)
                self.assertEqual(request.execution.active_workers, 0)
                self.assertEqual(request.execution.active_tracked_payload_bytes, 0)

    def test_injected_correspondence_copy_inconsistencies(self):
        source, result = self.accepted()
        controls = [replace(result, face_sources=words([0, 2, 1, 2, 2, 2])),
                    replace(result, face_output_offsets=words([0, 1, 2, 6])),
                    replace(result, corner_sources=words([99] + CORNER_SOURCES[1:])),
                    replace(result, corner_sources=words([3] + CORNER_SOURCES[1:])),
                    replace(result, corner_sources=words(CORNER_SOURCES[:-1])),
                    replace(result, face_sources=[0, 1, 1, 2, 2, 2])]
        for injected in controls:
            with self.subTest(injected=injected.face_output_offsets), self.assertRaises(ExportPreparationError):
                prepare_triangle_asset(source, injected)
        self.assertEqual(prepare_triangle_asset(source, result).corner_map, CORNER_MAP)

    def test_injected_vertex_bits_channel_and_presence_copies(self):
        source, result = self.accepted()
        controls = []
        record = result.candidate.to_record()
        record["positions"] = array("d", record["positions"])
        record["positions"][0] = -0.0
        controls.append(replace(result, candidate=geometry.Mesh.from_record(record)))
        record = result.candidate.to_record()
        record["attributes"][0]["metadata"] = {"control": "changed"}
        controls.append(replace(result, candidate=geometry.Mesh.from_record(record)))
        record = result.candidate.to_record()
        record["attributes"][0]["present"] = None
        controls.append(replace(result, candidate=geometry.Mesh.from_record(record)))
        record = result.candidate.to_record()
        record["attributes"][0]["values"] = array("d", record["attributes"][0]["values"])
        record["attributes"][0]["values"][0] = .5
        controls.append(replace(result, candidate=geometry.Mesh.from_record(record)))
        for injected in controls:
            with self.assertRaises(ExportPreparationError):
                prepare_triangle_asset(source, injected)

    def test_typed_ragged_missing_backing_and_unused_signed_zero(self):
        source = asset()
        record = source.document.mesh.to_record()
        record["positions"] = array("d", record["positions"])
        record["positions"].extend([-0.0, 0.0, 7.0])
        kinds = [("f", "float32"), ("d", "float64"), ("i", "int32"), ("B", "uint8"),
                 ("H", "uint16"), ("I", "uint32"), ("Q", "uint64")]
        for code, name in kinds:
            final_value = (1 << 63) + 17 if code == "Q" else 7
            record["attributes"].append({"domain": "vertex", "name": "literal." + name,
                "semantic": "label", "set_index": None, "components": 1, "scalar_type": name,
                "values": array(code, [0] * 10 + [final_value]), "offsets": None, "present": None,
                "metadata": {"fixture": "typed-native-transfer"}})
        offsets, values = [0], []
        for row in range(12):
            values.extend([row, row + 20] * (row % 3))
            offsets.append(len(values))
        record["attributes"].append({"domain": "corner", "name": "literal.ragged",
            "semantic": "label", "set_index": None, "components": 2, "scalar_type": "uint16",
            "values": array("H", values), "offsets": array("Q", offsets),
            "present": array("B", [row % 2 for row in range(12)]), "metadata": {}})
        source = source.with_mesh(geometry.Mesh.from_record(record))
        source, result = self.accepted(source)
        plan = prepare_triangle_asset(source, result)
        self.assertEqual(plan.asset.document.mesh.to_record()["positions"].tobytes(), record["positions"].tobytes())
        actual = plan.asset.document.mesh.to_record()["attributes"][-1]
        expected = [value for row in CORNER_SOURCES for value in values[offsets[row]:offsets[row + 1]]]
        self.assertEqual(list(actual["values"]), expected)
        self.assertEqual(list(actual["present"]), [row % 2 for row in CORNER_SOURCES])
        # Inject inconsistent copies of an actual accepted result: missing-row
        # backing and uint64 low bits must not disappear in the verifier.
        changed = result.candidate.to_record()
        ragged = changed["attributes"][-1]
        self.assertEqual(ragged["present"][2], 0)
        ragged["values"] = array("H", ragged["values"])
        ragged["values"][ragged["offsets"][2]] += 1
        with self.assertRaises(ExportPreparationError):
            prepare_triangle_asset(source, replace(result, candidate=geometry.Mesh.from_record(changed)))
        changed = result.candidate.to_record()
        wide = changed["attributes"][-2]
        self.assertEqual(wide["scalar_type"], "uint64")
        wide["values"] = array("Q", wide["values"])
        wide["values"][-1] += 1
        with self.assertRaises(ExportPreparationError):
            prepare_triangle_asset(source, replace(result, candidate=geometry.Mesh.from_record(changed)))
        # These generic stored channels exercise native transfer only. They do
        # not claim that the OBJ writer can export unsupported channels.
        self.assertEqual(plan.asset.document.mesh.vertex_count, 11)

    def test_plan_is_frozen_and_maps_are_fresh(self):
        source, result = self.accepted()
        plan = prepare_triangle_asset(source, result)
        with self.assertRaises(FrozenInstanceError):
            plan.asset = source
        plan.face_map[1].append(999)
        plan.corner_map[4].clear()
        self.assertEqual(plan.face_map, FACE_MAP)
        self.assertEqual(plan.corner_map, CORNER_MAP)


if __name__ == "__main__":
    unittest.main()
