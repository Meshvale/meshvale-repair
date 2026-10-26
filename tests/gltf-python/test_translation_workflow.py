# SPDX-License-Identifier: Apache-2.0
"""Installed API and command checks with an independent moving wire fixture."""
import base64
from copy import deepcopy
import gc
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import meshvale_reports
from meshvale_repair import gltf_commands as commands, gltf_workflow as workflow
from meshvale_repair.commands import InvocationError
from gltf_translation_fixture import (
    BIN_KIND, ENTRY, REPORT, TRANSLATION_BYTES, fixture, parse_entry,
)


STATIC = "static-local-triangles-v1"
DIRECT = "direct-translation-v1"
BALANCED = "balanced-direct-translation-v1"
SERIAL = "serial-direct-translation-v1"
SLOTS = ("source", "writing", "reload", "verification",
         "publication_reload", "publication_verification")
OBSERVATION_KEYS = frozenset(("status", "diagnostic", "complete", "node_rows", "mesh_users",
    "scanned_channels", "affected_channels", "distinct_samplers", "keys_checked",
    "decoded_bytes_checked", "relationship_steps", "new_requested_bytes",
    "effective_request_cap", "work", "execution"))
DIAGNOSTIC_KEYS = frozenset(("status", "code", "json_span", "resource_kind",
                           "resource_index", "resource_span", "rule_id"))
EXTENSION = "meshvale.repair.gltf"
PROFILE = os.environ.get("MESHVALE_GLTF_PYTHON_PROFILE", "qualified")
if PROFILE not in ("qualified", "unsupported"):
    raise ValueError("MESHVALE_GLTF_PYTHON_PROFILE must be qualified or unsupported")
QUALIFIED = PROFILE == "qualified"


def leaves(value):
    return sum(leaves(item) for item in value.values()) if type(value) is dict else 1


def indices(document, chunks, directory):
    accessor = document["accessors"][document["meshes"][0]["primitives"][0]["indices"]]
    view = document["bufferViews"][accessor["bufferView"]]
    buffer = document["buffers"][view["buffer"]]
    uri = buffer.get("uri")
    if uri is None:
        data = next(content for kind, content in chunks if kind == BIN_KIND)
    elif uri.startswith("data:"):
        data = base64.b64decode(uri.split(",", 1)[1], validate=True)
    else:
        data = (directory / uri).read_bytes()
    code, width = {5123: ("H", 2), 5125: ("I", 4)}[accessor["componentType"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", width)
    return tuple(struct.unpack_from("<" + code, data, start + i * stride)[0]
                 for i in range(accessor["count"]))


class TranslationWorkflow(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="meshvale-moving-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def api(self, source, destination=None, targets=(), **options):
        return workflow.repair_gltf_file(
            source, destination, targets, primitive_ordinal=0,
            triangles="require-triangles", values="raw", **options)

    def row(self, identity, profile):
        return {"id": identity, "input": "sources/" + identity + "/" + ENTRY,
                "destination": "outputs/" + identity, "targets": [[0, 1]],
                "primitive_ordinal": 0, "triangles": "require-triangles",
                "values": "raw", "processing_profile": profile}

    def test_exact_selected_presets_and_detached_values(self):
        path = Path(__file__).resolve().parents[2] / "docs/gltf-translation-presets.json"
        expected = json.loads(path.read_text(encoding="utf-8"))
        for name in (BALANCED, SERIAL):
            session = workflow.GltfExecution(preset=name)
            actual = session.resolved_preset
            self.assertEqual(actual, expected[name])
            self.assertEqual(leaves(actual), 303)
            actual["execution"]["worker_budget"] = 999
            self.assertEqual(session.resolved_preset, expected[name])
        for name in ("balanced-v1", "serial-reference-v1"):
            self.assertEqual(leaves(workflow.GltfExecution(preset=name).resolved_preset), 219)

    def test_batch_profile_admission_precedes_all_filesystem_work(self):
        request = {"schema": "meshvale.repair.gltf-jobs/2",
                   "jobs": [self.row("static", STATIC), self.row("moving", DIRECT)]}
        for name in ("balanced-v1", "serial-reference-v1"):
            with self.subTest(preset=name), patch.object(commands, "_preflight_reports") as reader, \
                    patch.object(Path, "resolve", side_effect=AssertionError("premature filesystem work")), \
                    patch.object(workflow, "_prepare_gltf_request") as prepare:
                with self.assertRaises(InvocationError):
                    commands.run_gltf_jobs(request, base=self.root,
                                           execution=workflow.GltfExecution(preset=name))
                reader.assert_not_called()
                prepare.assert_not_called()
        malformed = deepcopy(request)
        malformed["jobs"][1]["processing_profile"] = "unknown"
        with patch.object(commands, "_preflight_reports") as reader, \
                patch.object(workflow, "_prepare_gltf_request") as prepare:
            with self.assertRaises(InvocationError):
                commands.run_gltf_jobs(malformed, base=self.root)
            reader.assert_not_called()
            prepare.assert_not_called()
        old = {"schema": "meshvale.repair.gltf-jobs/1", "jobs": [self.row("static", STATIC)]}
        with self.assertRaises(InvocationError):
            commands.run_gltf_jobs(old, base=self.root)

    def test_capture_failure_keeps_selected_plain_facts(self):
        result = self.api(self.root / "absent.gltf", processing_profile=DIRECT)
        self.assertFalse(result.workflow_started)
        self.assertEqual(result.processing_profile, DIRECT)
        self.assertEqual(result.translation_facts, {"profile": DIRECT, **dict.fromkeys(SLOTS)})
        detached = result.translation_facts
        detached["profile"] = "mutated"
        self.assertEqual(result.translation_facts["profile"], DIRECT)
        with self.assertRaises(AttributeError):
            result.processing_profile = STATIC
        fallback = commands._run_gltf_single(
            {key: value for key, value in self.row("absent", DIRECT).items() if key != "processing_profile"},
            base=self.root, processing_profile=DIRECT)
        report = meshvale_reports.load(fallback.report_json)
        extension = report["extensions"]["meshvale.repair.gltf.commands"]
        self.assertEqual((extension["version"], extension["processing_profile"]), ("2", DIRECT))
        self.assertEqual(extension["processing"], result.translation_facts)
        self.assertEqual(fallback.exit_code, 2)

    def test_file_api_profile_refusal_precedes_source_conversion(self):
        class UnreadPath:
            def __fspath__(self):
                raise AssertionError("profile refusal converted the source path")
        with patch.object(workflow, "_run_gltf_request") as run:
            for name in ("balanced-v1", "serial-reference-v1"):
                with self.assertRaises(ValueError):
                    self.api(UnreadPath(), processing_profile=DIRECT,
                             execution=workflow.GltfExecution(preset=name))
            for value in (None, True, 3, "unknown-profile"):
                with self.assertRaises((TypeError, ValueError)):
                    self.api(UnreadPath(), processing_profile=value)
            run.assert_not_called()

    @unittest.skipUnless(QUALIFIED, "requires the selected qualified Geometry producer")
    def test_actual_moving_publications_and_retained_owners(self):
        for glb in (False, True):
            for interpolation in ("LINEAR", "STEP"):
                for changed in (False, True):
                    outputs = []
                    for preset in (BALANCED, SERIAL):
                        case = f"{glb}-{interpolation}-{changed}-{preset}"
                        with self.subTest(case=case):
                            source, original, members = fixture(
                                self.root / (case + "-source"), glb=glb, interpolation=interpolation)
                            destination = self.root / (case + "-output")
                            session = workflow.GltfExecution(preset=preset)
                            result = self.api(source, destination, [(0, 1)] if changed else (),
                                              processing_profile=DIRECT, execution=session)
                            del session
                            # This exercises retention after the completed file call.
                            # Native capture-before-work destruction has its own native test.
                            for name in members:
                                (source.parent / name).unlink()
                            gc.collect()
                            self.assertEqual(result.processing_profile, DIRECT)
                            self.assertEqual(result.face_map, (0, 0) if changed else (0, 1))
                            self.assertEqual(result.corner_map, (0, 1, 2, 0, 1, 2) if changed else tuple(range(6)))
                            self.assertEqual(result.source_faces, (0,) if changed else (0, 1))
                            self.assertEqual(result.source_corners, (0, 1, 2) if changed else tuple(range(6)))
                            with patch.object(meshvale_reports, "load", side_effect=AssertionError("facts parsed a report")):
                                facts = result.translation_facts
                            self.assertEqual(set(facts), {"profile", *SLOTS})
                            for slot in SLOTS:
                                self.assertIs(type(facts[slot]), dict)
                                self.assertEqual(set(facts[slot]), OBSERVATION_KEYS)
                                self.assertEqual(set(facts[slot]["diagnostic"]), DIAGNOSTIC_KEYS)
                                self.assertIs(facts[slot]["complete"], True)
                                self.assertIs(type(facts[slot]["status"]), int)
                                self.assertEqual(facts[slot]["status"], 0)
                                self.assertEqual(facts[slot]["keys_checked"], 3)
                                self.assertEqual(facts[slot]["distinct_samplers"], 1)
                                self.assertEqual(facts[slot]["affected_channels"], 2)
                                expected_bytes = 96 if slot in ("writing", "verification", "publication_verification") else 48
                                self.assertEqual(facts[slot]["decoded_bytes_checked"], expected_bytes)
                            facts["source"]["work"]["max_jobs"] = 0
                            self.assertGreater(result.translation_facts["source"]["work"]["max_jobs"], 0)
                            report = meshvale_reports.load(result.report_json)
                            self.assertEqual(report["profile"]["outcome"], "passed")
                            self.assertEqual(report["extensions"][EXTENSION]["version"], "2")
                            self.assertEqual(report["extensions"][EXTENSION]["processing"], result.translation_facts)
                            self.assertEqual((destination / REPORT).read_bytes(), result.report_json)
                            entry = (destination / ENTRY).read_bytes()
                            document, chunks = parse_entry(entry)
                            for field in ("nodes", "animations", "scenes", "scene", "images", "materials", "extras"):
                                self.assertEqual(document[field], original[field])
                            self.assertEqual(struct.pack("<f", document["nodes"][2]["translation"][0]), bytes.fromhex("00000080"))
                            self.assertEqual(document["meshes"][1], original["meshes"][1])
                            self.assertEqual(document["accessors"][:5], original["accessors"])
                            self.assertEqual(document["bufferViews"][:5], original["bufferViews"])
                            self.assertEqual(document["buffers"][:2], original["buffers"])
                            self.assertEqual(indices(document, chunks, destination), (0, 1, 2) if changed else (0, 1, 2, 0, 1, 2))
                            self.assertEqual(chunks, parse_entry(members[ENTRY])[1])
                            binary = chunks[0][1] if glb else (destination / "mesh.bin").read_bytes()
                            self.assertEqual(binary[60:96], TRANSLATION_BYTES)
                            for name, content in members.items():
                                if name != ENTRY:
                                    self.assertEqual((destination / name).read_bytes(), content)
                            if not changed:
                                self.assertEqual(entry, members[ENTRY])
                            outputs.append(entry)
                    self.assertEqual(outputs[0], outputs[1])

    @unittest.skipUnless(QUALIFIED, "requires the selected qualified Geometry producer")
    def test_default_modes_share_one_live_ledger_and_release(self):
        static, _, _ = fixture(self.root / "static", animated=False)
        moving, _, _ = fixture(self.root / "moving")
        original = workflow._DEFAULT_EXECUTION
        policy = workflow._DEFAULT_TRANSLATION_EXECUTION
        before = original.active_tracked_payload
        first = self.api(static)
        second = self.api(moving, processing_profile=DIRECT)
        self.assertIsNone(first.translation_facts)
        self.assertEqual(first.processing_profile, STATIC)
        self.assertEqual(original.active_tracked_payload, policy.active_tracked_payload)
        self.assertGreater(original.active_tracked_payload, before)
        del first, second
        gc.collect()
        self.assertEqual(original.active_tracked_payload, before)

    @unittest.skipUnless(QUALIFIED, "requires the selected qualified Geometry producer")
    def test_malformed_time_retains_actual_failed_source_call(self):
        source, _, _ = fixture(self.root / "bad-time", malformed=True)
        result = self.api(source, processing_profile=DIRECT)
        self.assertTrue(result.workflow_started)
        facts = result.translation_facts
        self.assertIsNotNone(facts["source"])
        self.assertFalse(facts["source"]["complete"])
        self.assertEqual(facts["source"]["status"], 3)
        self.assertTrue(all(facts[slot] is None for slot in SLOTS[1:]))

    @unittest.skipUnless(QUALIFIED, "requires the selected qualified Geometry producer")
    def test_mixed_batch_and_actual_module_console_entrypoints(self):
        fixture(self.root / "sources/moving", glb=True, interpolation="STEP")
        fixture(self.root / "sources/static", animated=False)
        (self.root / "outputs").mkdir()
        request = {"schema": "meshvale.repair.gltf-jobs/2",
                   "jobs": [self.row("moving", DIRECT), self.row("static", STATIC)]}
        result = commands.run_gltf_jobs(request, base=self.root)
        self.assertEqual(result.exit_code, 0)
        batch = meshvale_reports.load(result.report_json)
        for row, profile in zip(batch["jobs"], (DIRECT, STATIC)):
            report = row["report"]
            self.assertEqual(report["extensions"][EXTENSION]["version"], "2" if profile == DIRECT else "1")
        executable = Path(sys.executable).with_name("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        for name, prefix in (("module", [sys.executable, "-I", "-m", "meshvale_repair"]),
                             ("console", [str(executable)])):
            source, _, _ = fixture(self.root / (name + "-source"), glb=True)
            destination = self.root / (name + "-output")
            completed = subprocess.run(prefix + ["gltf", str(source), str(destination),
                "--primitive-ordinal", "0", "--triangles", "require-triangles", "--values", "raw",
                "--target", "0:1", "--processing-profile", DIRECT],
                cwd=self.root, capture_output=True, timeout=60)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode(errors="replace"))
            self.assertEqual(completed.stdout, (destination / REPORT).read_bytes() + b"\n")


if __name__ == "__main__":
    unittest.main()
