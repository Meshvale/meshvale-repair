# SPDX-License-Identifier: Apache-2.0
"""Installed static-scene entry points and independent literal fixture controls."""
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from meshvale_reports import load
from meshvale_repair.gltf_workflow import repair_gltf_file

EXAMPLE = Path(__file__).resolve().parents[2] / "examples/python/gltf_scene_walkthrough.py"
SCENE = runpy.run_path(str(EXAMPLE))
PROFILE = os.environ.get("MESHVALE_GLTF_PYTHON_PROFILE", "qualified")
if PROFILE not in {"qualified", "unsupported"}:
    raise ValueError("MESHVALE_GLTF_PYTHON_PROFILE must be qualified or unsupported")


class Base(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="meshvale-scene-test-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve()

    def invoke(self, *arguments):
        return subprocess.run([sys.executable, "-I", str(EXAMPLE), *map(str, arguments)],
                              cwd=self.root, capture_output=True, timeout=180)


class SceneFixtureTests(Base):
    def test_original_literal_scene_binary_and_resource_extents(self):
        for glb in (False, True):
            members = SCENE["make_scene"](self.root / ("glb" if glb else "json"), glb)
            document, chunks = SCENE["entry_document"](members["scene.gltf"])
            self.assertEqual(document["scenes"], [{"name": "shared-scene", "nodes": [0, 1, 2]}])
            self.assertEqual(document["scene"], 0)
            self.assertEqual(document["nodes"], [
                {"name": "scaled-shared", "mesh": 0, "translation": [2, 0, 0],
                 "rotation": [0, 0, 0, 1], "scale": [2, 1, 1]},
                {"name": "matrix-shared", "mesh": 0, "matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -3, 4, 0, 1]},
                {"name": "unrelated", "mesh": 1, "translation": [0, 0, 5]},
                {"name": "unused-node"}])
            self.assertEqual([mesh["primitives"][0] for mesh in document["meshes"]], [
                {"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "indices": 2, "mode": 4, "material": 0},
                {"attributes": {"POSITION": 3, "TEXCOORD_0": 4}, "indices": 5, "mode": 4, "material": 1}])
            self.assertEqual(document["materials"], [
                {"name": "red", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 1}},
                {"name": "blue", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}, "metallicFactor": 0, "roughnessFactor": 1}}])
            self.assertEqual(document["textures"], [{"source": 0}, {"source": 1}])
            self.assertEqual(document["images"], [{"uri": "texture-red.png", "mimeType": "image/png"},
                                                   {"uri": "texture-blue.png", "mimeType": "image/png"}])
            binary = chunks[0][1] if glb else members["geometry.bin"]
            self.assertEqual(struct.unpack_from("<9f", binary), (0, 0, 0, 1, 0, 0, 0, 1, 0))
            self.assertEqual(struct.unpack_from("<6f", binary, 36), (0, 0, 1, 0, 0, 1))
            self.assertEqual(struct.unpack_from("<6H", binary, 60), (0, 1, 2, 0, 1, 2))
            self.assertEqual(struct.unpack_from("<9f", binary, 72), (10, 0, 0, 12, 0, 0, 10, 2, 0))
            self.assertEqual(struct.unpack_from("<6f", binary, 108), (0, 0, 1, 0, 0, 1))
            self.assertEqual(struct.unpack_from("<3H", binary, 132), (0, 1, 2))
            self.assertEqual(binary[138:], b"\xd3\0" if glb else bytes.fromhex("deadbeefcafebabe"))
            self.assertEqual(members["unused.bin"], bytes.fromhex("01020304deadbeefcafebabe"))
            self.assertEqual(document["buffers"][1], {"uri": "unused.bin", "byteLength": 4})
            self.assertEqual(document["buffers"][0]["byteLength"], 139 if glb else 138)
            for name, rgba, expected_hash in (
                    ("red", bytes((255, 0, 0, 255)), "49e1dad481e94dfab7c9573a9a81d56aa2ca629fe15a3f7a910aa4f47601c00d"),
                    ("blue", bytes((0, 0, 255, 255)), "e4de9adb10f197cd553b011440c25c4cc0989ad642a0011c0006cd955d32be0c")):
                image = members["texture-" + name + ".png"]
                self.assertEqual(hashlib.sha256(image).hexdigest(), expected_hash)
                self.assertEqual(image[:8], b"\x89PNG\r\n\x1a\n")
                cursor, image_chunks = 8, []
                while cursor < len(image):
                    length, kind = struct.unpack_from(">I4s", image, cursor)
                    payload = image[cursor + 8:cursor + 8 + length]
                    crc, = struct.unpack_from(">I", image, cursor + 8 + length)
                    self.assertEqual(crc, zlib.crc32(kind + payload))
                    image_chunks.append((kind, payload))
                    cursor += 12 + length
                self.assertEqual(cursor, len(image))
                self.assertEqual([kind for kind, _ in image_chunks], [b"IHDR", b"IDAT", b"IEND"])
                self.assertEqual(struct.unpack(">IIBBBBB", image_chunks[0][1]), (1, 1, 8, 6, 0, 0, 0))
                self.assertEqual(zlib.decompress(image_chunks[1][1]), b"\0" + rgba)
                self.assertEqual(image_chunks[2][1], b"")
            if glb:
                self.assertEqual(chunks[0][0], 0x004E4942)
                self.assertEqual(chunks[1], (0x12345678, b"\x7f\0\xde\xad"))

    def test_lexical_oracle_rejects_semantically_equal_reformat_and_graph_changes(self):
        original = SCENE["make_scene"](self.root / "source")["scene.gltf"]
        SCENE["check_scene_fragments"](original, original)
        reformatted = original.replace(b'"nodes":[', b'"nodes": [', 1)
        self.assertEqual(json.loads(original), json.loads(reformatted))
        with self.assertRaises(AssertionError):
            SCENE["check_scene_fragments"](original, reformatted)
        for key in ("nodes", "scenes", "materials", "textures", "images", "meshes"):
            document = deepcopy(json.loads(original))
            document[key][1 if key == "meshes" else 0]["name"] = "altered-contract-record"
            wire = json.dumps(document, separators=(",", ":")).encode()
            with self.subTest(table=key), self.assertRaises(AssertionError):
                SCENE["check_scene_fragments"](original, wire)

    def test_existing_output_directory_is_never_overwritten(self):
        destination = self.root / "existing"
        destination.mkdir()
        marker = destination / "authored.txt"
        marker.write_bytes(b"existing user bytes\0\xff")
        result = self.invoke("--output", destination)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual({p.name: p.read_bytes() for p in destination.iterdir()}, {"authored.txt": b"existing user bytes\0\xff"})

    def test_actual_installed_api_commands_batches_and_source_protection(self):
        destination = self.root / "retained-\u8d44\u4ea7-\U0001f9ca"
        arguments = ["--output", destination]
        if PROFILE == "unsupported":
            arguments.append("--unsupported-profile")
        inspector = os.environ.get("MESHVALE_GLTF_SCENE_INSPECTOR")
        if inspector:
            arguments += ["--scene-inspector", inspector]
        result = self.invoke(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
        summary = json.loads((destination / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["unsupported_profile"], PROFILE == "unsupported")
        mixed = [row for row in summary["cases"] if row["entry"] == "mixed-batch"]
        self.assertEqual(len(mixed), 4)
        self.assertTrue(all(row["exit"] == 2 for row in mixed))
        singles = [row for row in summary["cases"] if row["entry"] in {"api", "module", "console"}]
        self.assertEqual(len(singles), 8)
        self.assertEqual(len(list((destination / "published").iterdir())), 0 if PROFILE == "unsupported" else 22)
        self.assertEqual(summary["native_inspector_executed"], bool(inspector) and PROFILE == "qualified")
        self.assertEqual(len(summary["cases"]), 26)


class QualifiedSceneTests(Base):
    def test_undeclared_raw_uv_is_a_real_policy_refusal(self):
        for glb in (False, True):
            source = self.root / ("glb" if glb else "json")
            original = SCENE["make_scene"](source, glb)
            destination = self.root / (source.name + "-refused")
            result = repair_gltf_file(source / "scene.gltf", destination, [(0, 1)],
                primitive_ordinal=0, triangles="require-triangles", values="raw")
            self.assertEqual(result.capture_status.name, "kSuccess")
            self.assertEqual(result.status.name, "kRejected")
            self.assertEqual(result.diagnostic.memory.delegated.code.name, "kUnsupportedAttribute")
            self.assertEqual(result.diagnostic.memory.delegated.attribute, 0)
            self.assertFalse(destination.exists())
            self.assertIsNone(result.face_map)
            self.assertEqual(load(result.report_json)["candidate"]["outcome"], "rejected")
            self.assertEqual({p.name: p.read_bytes() for p in source.iterdir()}, original)

    def test_primitive_ordinal_one_selects_unrelated_mesh_not_second_instance(self):
        source = self.root / "source"
        original = SCENE["make_scene"](source)
        destination = self.root / "other-primitive"
        result = repair_gltf_file(source / "scene.gltf", destination, [], primitive_ordinal=1,
            triangles="require-triangles", values="raw", row_local_attributes=[("vertex", "TEXCOORD_0")])
        self.assertEqual(result.status.name, "kSuccess")
        self.assertEqual((result.input_snapshot_counts, result.candidate_snapshot_counts), ((3, 1, 3), (3, 1, 3)))
        self.assertEqual(result.face_map, (0,))
        report = load(result.report_json)
        extension = report["extensions"]["meshvale.repair.gltf"]
        self.assertEqual((extension["mesh_index"], extension["primitive_index"]), (1, 0))
        self.assertEqual((destination / "scene.gltf").read_bytes(), original["scene.gltf"])
        self.assertEqual((destination / "receipts/report.json").read_bytes(), result.report_json)


class UnsupportedSceneTests(Base):
    def test_scene_capture_preserves_real_numerical_refusal(self):
        source = self.root / "source"
        original = SCENE["make_scene"](source, True)
        result = repair_gltf_file(source / "scene.gltf", self.root / "unpublished", [(0, 1)],
            primitive_ordinal=0, triangles="require-triangles", values="raw",
            row_local_attributes=[("vertex", "TEXCOORD_0")])
        self.assertEqual(result.capture_status.name, "kSuccess")
        self.assertTrue(result.workflow_started)
        self.assertEqual(result.status.name, "kDelegatedFailure")
        self.assertEqual(result.diagnostic.memory.delegated.geometry_status.name, "kUnsupportedAllocationProfile")
        self.assertIsNone(result.publication)
        self.assertFalse((self.root / "unpublished").exists())
        self.assertEqual({p.name: p.read_bytes() for p in source.iterdir()}, original)


def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite(loader.loadTestsFromTestCase(SceneFixtureTests))
    suite.addTests(loader.loadTestsFromTestCase(QualifiedSceneTests if PROFILE == "qualified" else UnsupportedSceneTests))
    return suite


if __name__ == "__main__":
    unittest.main(verbosity=2)
