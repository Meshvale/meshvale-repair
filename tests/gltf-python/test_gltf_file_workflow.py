# SPDX-License-Identifier: Apache-2.0
"""Installed path workflow checks with independently authored wire expectations.

Run after installing the matched wheels and Geometry's report dependencies:
python -I -m unittest discover -s tests/gltf-python -v
MESHVALE_GLTF_PYTHON_PROFILE selects qualified or unsupported explicitly.
"""
import base64
from concurrent.futures import ThreadPoolExecutor
import gc
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import threading
import time
import unittest

import jsonschema
import meshvale_reports
from meshvale_repair.gltf_workflow import (
    GltfCancellation, GltfExecution, GltfFileWorkflowResult, repair_gltf_file,
)


ENTRY = "scene.gltf"  # Both contents use this name: suffix does not choose format.
REPORT = "receipts/report.json"
DOMAIN = b"meshvale.gltf-file-snapshot/1\0"
JSON_KIND = 0x4E4F534A
BIN_KIND = 0x004E4942
FILE_EXTENSION = "meshvale.repair.gltf.file"
MEMORY_EXTENSION = "meshvale.repair.gltf"
JSON_PREFIX = (
    br'{"asset":{"version":"2.0"},"buffers":'
)
JSON_SUFFIX = (
    br',"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],'
    br'"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},'
    br'{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],'
    br'"images":[{"uri":"texture.bin","mimeType":"image/png"}],'
    br'"materials":[{"name":"authored-material"}],'
    br'"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],'
    br'"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],'
    br'"extras":{"number":1.2300e+04,"string":"keep\u0020lexeme"}}' + b" \t\n"
)
JSON_SOURCE = JSON_PREFIX + br'[{"uri":"mesh.bin","byteLength":48}]' + JSON_SUFFIX
GLB_JSON = JSON_PREFIX + br'[{"byteLength":49},{"uri":"mesh.bin","byteLength":48}]' + JSON_SUFFIX
# Float32 negative zero/positions, literal duplicate UInt16 indices, physical tail.
BINARY = bytes.fromhex(
    "00000080 00000000 00000000 0000803f 00000000 00000000 "
    "00000000 0000803f 00000000 000001000200000001000200 deadbeefcafebabe"
)
BIN_BYTES = BINARY[:48] + bytes.fromhex("d3 000000")
NON_JSON = (
    (BIN_KIND, BIN_BYTES),
    (0x12345678, bytes.fromhex("7f00dead")),
    (0xDECAFBAD, bytes.fromhex("0102030480ff00aa")),
    (0x12345678, bytes.fromhex("000102ff")),
)
IMAGE = bytes.fromhex("89504e470d0a1a0a") + b"\x5a" * (4096 - 8)
INDEX_BYTES = bytes.fromhex("00000000 01000000 02000000")
INDEX_URI = "data:application/octet-stream;base64,AAAAAAEAAAACAAAA"
POLICY = {"primitive_ordinal": 0, "triangles": "require-triangles", "values": "raw"}
PROFILE = os.environ.get("MESHVALE_GLTF_PYTHON_PROFILE", "qualified")
if PROFILE not in {"qualified", "unsupported"}:
    raise ValueError("MESHVALE_GLTF_PYTHON_PROFILE must be qualified or unsupported")


def assemble_glb(json_bytes, chunks=NON_JSON):
    """Literal little-endian container construction; no production assembler."""
    padded = json_bytes + b" " * (-len(json_bytes) % 4)
    body = b"".join(struct.pack("<II", len(data), kind) + data
                    for kind, data in ((JSON_KIND, padded), *chunks))
    return struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body


def parse_glb(wire):
    if len(wire) < 12 or struct.unpack_from("<III", wire) != (0x46546C67, 2, len(wire)):
        raise AssertionError("literal GLB magic/version/full physical extent")
    cursor, chunks = 12, []
    while cursor < len(wire):
        if len(wire) - cursor < 8:
            raise AssertionError("complete GLB chunk header")
        size, kind = struct.unpack_from("<II", wire, cursor)
        cursor += 8
        if size % 4 or size > len(wire) - cursor:
            raise AssertionError("aligned GLB chunk within exact extent")
        chunks.append((kind, wire[cursor:cursor + size]))
        cursor += size
    if not chunks or chunks[0][0] != JSON_KIND or any(k == JSON_KIND for k, _ in chunks[1:]):
        raise AssertionError("one leading JSON chunk")
    return chunks


def fixture(root, glb=False, image=IMAGE):
    root.mkdir(parents=True)
    members = {ENTRY: assemble_glb(GLB_JSON) if glb else JSON_SOURCE,
               "mesh.bin": BINARY, "texture.bin": image}
    for name, data in members.items():
        (root / name).write_bytes(data)
    return members


def source_digest(members, glb):
    def frame(data):
        return struct.pack("<Q", len(data)) + data
    names = sorted(members, key=lambda name: name.encode("utf-8"))
    digest = hashlib.sha256()
    digest.update(DOMAIN)
    digest.update(frame(b"glb" if glb else b"gltf-json"))
    digest.update(struct.pack("<Q", len(names)))
    for name in names:
        digest.update(frame(name.encode("utf-8")))
        digest.update(frame(members[name]))
    return digest.hexdigest()


def actual_files(root):
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob("*") if path.is_file()}


def call(source, destination, targets=((0, 1),), **options):
    return repair_gltf_file(source, destination, targets, **(POLICY | options))


class CommonChecks(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="meshvale-gltf-python-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve()

    def enum(self, value, member, domain=None):
        if domain is not None:
            self.assertEqual(type(value).__name__, domain)
        self.assertEqual(value, getattr(type(value), member))

    def joined(self, stats):
        self.assertEqual(stats.launched_workers, stats.joined_workers)

    def validated_report(self, result):
        raw = result.report_json
        self.assertIs(type(raw), bytes)
        report = meshvale_reports.load(raw)
        jsonschema.Draft202012Validator(meshvale_reports.schema_document()).validate(report)
        self.assertEqual(result.report_json, raw)
        return report

    def check_document(self, before, after, changed, glb):
        if not changed:
            self.assertEqual(after, before)
            return
        old_text, new_text = before.decode("ascii"), after.decode("ascii")
        original, old_end = json.JSONDecoder().raw_decode(old_text)
        document, new_end = json.JSONDecoder().raw_decode(new_text)
        suffix = before[old_end:]
        expected_suffix = suffix + (b" " * (-(new_end + len(suffix)) % 4) if glb else b"")
        self.assertEqual(after[new_end:], expected_suffix)
        self.assertIn(br'"number":1.2300e+04', after)
        self.assertIn(br'"string":"keep\u0020lexeme"', after)
        self.assertEqual(len(document["buffers"]), len(original["buffers"]) + 1)
        new_buffer = document["buffers"].pop()
        self.assertEqual(new_buffer, {"byteLength": 12, "uri": INDEX_URI})
        self.assertEqual(base64.b64decode(new_buffer["uri"].split(",", 1)[1], validate=True), INDEX_BYTES)
        view = document["bufferViews"].pop()
        self.assertEqual(view["buffer"], len(original["buffers"]))
        self.assertEqual(view["byteLength"], 12)
        self.assertEqual(view.get("byteOffset", 0), 0)
        self.assertLessEqual(set(view), {"buffer", "byteLength", "byteOffset", "target"})
        self.assertEqual(view.get("target", 34963), 34963)
        accessor = document["accessors"].pop()
        self.assertEqual((accessor["bufferView"], accessor["componentType"], accessor["count"], accessor["type"]),
                         (2, 5125, 3, "SCALAR"))
        self.assertEqual(accessor.get("byteOffset", 0), 0)
        self.assertIs(accessor.get("normalized", False), False)
        self.assertLessEqual(set(accessor), {"bufferView", "componentType", "count", "type", "byteOffset", "normalized"})
        primitive = document["meshes"][0]["primitives"][0]
        self.assertEqual(primitive["indices"], 2)
        self.assertEqual(primitive.get("mode", 4), 4)
        primitive["indices"] = 1
        primitive.pop("mode", None)
        self.assertEqual(document, original)

    def check_success(self, result, members, destination, changed, glb):
        self.assertIsInstance(result, GltfFileWorkflowResult)
        self.assertTrue(result.workflow_started)
        self.enum(result.capture_status, "kSuccess", "GltfFileStatus")
        self.enum(result.status, "kSuccess", "GltfWorkflowStatus")
        self.enum(result.outcomes.publication, "kPublished")
        self.enum(result.publication.outcome, "kPublished")
        self.enum(result.source_format, "kGlb" if glb else "kGltfJson")
        self.assertEqual((result.source_entry_path, result.published_entry_path, result.published_report_path),
                         (ENTRY, ENTRY, REPORT))
        expected_faces = (0, 0) if changed else (0, 1)
        expected_corners = (0, 1, 2, 0, 1, 2) if changed else tuple(range(6))
        self.assertEqual(result.face_map, expected_faces)
        self.assertEqual(result.corner_map, expected_corners)
        self.assertEqual(result.source_faces, (0,) if changed else (0, 1))
        self.assertEqual(result.source_corners, (0, 1, 2) if changed else tuple(range(6)))
        self.assertIs(type(result.face_map), tuple)
        report = self.validated_report(result)
        self.assertEqual(report["input"]["sha256"], source_digest(members, glb))
        self.assertEqual(report["execution"]["outcome"], "completed")
        self.assertEqual(report["candidate"]["outcome"], "accepted" if changed else "unchanged")
        self.assertEqual(meshvale_reports.exit_code(report), 0)
        self.assertEqual(report["profile"]["outcome"], "passed")
        physical = actual_files(destination)
        self.assertEqual(set(physical), {*members, REPORT})
        self.assertEqual(physical[REPORT], result.report_json)
        for name in ("mesh.bin", "texture.bin"):
            self.assertEqual(physical[name], members[name])
        if glb:
            original, published = parse_glb(members[ENTRY]), parse_glb(physical[ENTRY])
            self.assertEqual(original[1:], list(NON_JSON))
            self.assertEqual(published[1:], list(NON_JSON))
            self.assertEqual(published[1][1][48:], bytes.fromhex("d3 000000"))
            self.check_document(original[0][1], published[0][1], changed, True)
            if not changed:
                self.assertEqual(physical[ENTRY], members[ENTRY])
        else:
            self.check_document(members[ENTRY], physical[ENTRY], changed, False)
        extension = report["extensions"][FILE_EXTENSION]
        self.assertEqual(extension["input_hash_convention"], "meshvale.gltf-file-snapshot/1")
        self.assertEqual(extension["source_format"], "glb" if glb else "gltf-json")
        self.assertEqual(extension["report_path"], REPORT)
        paths = extension["artifact_paths"]
        self.assertEqual([row["path"] for row in paths], sorted(members))
        self.assertEqual(report["publication"]["artifacts"], [
            {"id": row["id"], "sha256": hashlib.sha256(physical[row["path"]]).hexdigest()}
            for row in paths])
        snapshots = {row["role"]: row["meshes"][0] for row in report["snapshots"]}
        self.assertEqual(tuple(snapshots["input"][key] for key in ("vertex_count", "face_count", "corner_count")), (3, 2, 6))
        self.assertEqual(tuple(snapshots["output"][key] for key in ("vertex_count", "face_count", "corner_count")),
                         (3, 1, 3) if changed else (3, 2, 6))
        maps = {row["id"]: row for row in report["maps"]}
        self.assertEqual(maps["repair-faces"]["entries"], list(expected_faces) if changed else None)
        self.assertEqual(maps["repair-corners"]["entries"], list(expected_corners) if changed else None)
        memory = report["extensions"][MEMORY_EXTENSION]
        self.assertEqual(memory["source_faces"], list(result.source_faces))
        self.assertEqual(memory["source_corners"], list(result.source_corners))
        counts = result.requested_bytes
        self.assertGreater(counts.retained_source.files, 0)
        self.assertGreater(counts.retained_source.capped_reader, 0)
        self.enum(counts.retained_source.container, "kUnavailable" if glb else "kNotApplicable")
        self.assertEqual(counts.memory.read, 0)
        self.assertGreater(counts.publication_files, 0)
        self.assertFalse(hasattr(counts, "total"))
        self.joined(result.capture_execution.capture)
        self.joined(result.capture_execution.byte_verification)
        self.joined(result.execution.hashing)
        self.joined(result.execution.report_copy)
        self.joined(result.publication.execution.capture)
        self.joined(result.publication.execution.byte_verification)
        reopened = call(destination / ENTRY, None, (), execution=GltfExecution())
        self.enum(reopened.status, "kSuccess")
        self.assertEqual(reopened.face_map, (0,) if changed else (0, 1))
        self.assertEqual(reopened.corner_map, tuple(range(3 if changed else 6)))
        return report


class QualifiedWorkflowTests(CommonChecks):
    def test_changed_and_noop_literal_json_and_glb(self):
        for glb in (False, True):
            for changed in (False, True):
                with self.subTest(glb=glb, changed=changed):
                    root = self.root / f"{glb}-{changed}"
                    members = fixture(root / "source", glb)
                    session = GltfExecution()
                    result = call(root / "source" / ENTRY, root / "bundle",
                                  ((0, 1),) if changed else (), execution=session)
                    self.check_success(result, members, root / "bundle", changed, glb)
                    self.assertEqual(actual_files(root / "source"), members)

    def test_source_session_result_and_detached_observation_lifetimes(self):
        source = self.root / "source"
        members = fixture(source, True)
        session = GltfExecution()
        baseline = session.active_tracked_payload
        result = call(source / ENTRY, self.root / "bundle", execution=session)
        self.assertGreater(session.active_tracked_payload, baseline)
        shutil.rmtree(source)
        del session
        gc.collect()
        self.check_success(result, members, self.root / "bundle", True, True)
        diagnostic = result.capture_diagnostic
        retained_map, retained_report = result.face_map, result.report_json
        with self.assertRaises((AttributeError, TypeError)):
            result.status = None
        with self.assertRaises((AttributeError, TypeError)):
            diagnostic.code = None
        del result
        gc.collect()
        self.assertEqual(retained_map, (0, 0))
        self.assertEqual(meshvale_reports.load(retained_report)["publication"]["outcome"], "published")
        self.enum(diagnostic.status, "kSuccess")

    def test_no_export_shared_session_charge_release_and_serial_agreement(self):
        members = fixture(self.root / "source", image=IMAGE + b"\x5a" * (1024**2 - len(IMAGE)))
        session = GltfExecution()
        baseline = session.active_tracked_payload
        one = call(self.root / "source" / ENTRY, None, execution=session)
        first = session.active_tracked_payload
        two = call(self.root / "source" / ENTRY, None, execution=session)
        self.assertGreater(first, baseline)
        self.assertGreater(session.active_tracked_payload, first)
        for result in (one, two):
            self.enum(result.status, "kSuccess")
            self.enum(result.outcomes.publication, "kNotRequested")
            self.assertIsNone(result.publication)
            self.assertIsNone(result.published_entry_path)
            self.assertIsNone(result.requested_bytes.publication_files)
            self.assertEqual(self.validated_report(result)["publication"]["outcome"], "not-requested")
        del result, two
        gc.collect()
        self.assertEqual(session.active_tracked_payload, first)
        serial = GltfExecution(preset="serial-reference-v1")
        self.assertEqual(serial.worker_budget, 1)
        self.assertEqual(serial.preset, "serial-reference-v1")
        detached = serial.resolved_preset
        self.assertEqual(detached["execution"]["worker_budget"], 1)
        self.assertEqual(detached["execution"]["tracked_payload_budget_bytes"], 512 * 1024**2)
        detached["execution"]["worker_budget"] = 99
        self.assertEqual(serial.resolved_preset["execution"]["worker_budget"], 1)
        reference = call(self.root / "source" / ENTRY, None, execution=serial)
        self.enum(reference.status, "kSuccess")
        self.assertEqual((reference.face_map, reference.corner_map, reference.source_faces, reference.source_corners),
                         (one.face_map, one.corner_map, one.source_faces, one.source_corners))
        def serial_stats(value):
            if isinstance(value, dict):
                if "launched_workers" in value:
                    self.assertEqual(value["launched_workers"], 0)
                    self.assertEqual(value["joined_workers"], 0)
                for child in value.values():
                    serial_stats(child)
        serial_stats(self.validated_report(reference)["extensions"][MEMORY_EXTENSION]["phase_execution"])
        self.assertEqual(actual_files(self.root / "source"), members)
        del one
        gc.collect()
        self.assertEqual(session.active_tracked_payload, baseline)
        self.assertEqual(session.active_workers, 0)

    def test_concurrent_calls_retain_one_shared_session(self):
        session = GltfExecution()
        baseline = session.active_tracked_payload
        for name in ("first", "second"):
            fixture(self.root / name, image=IMAGE + b"\x5a" * (1024**2 - len(IMAGE)))
        barrier = threading.Barrier(3)
        def operate(name):
            barrier.wait(timeout=10)
            return call(self.root / name / ENTRY, None, execution=session)
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(operate, name) for name in ("first", "second")]
            barrier.wait(timeout=10)
            results = [future.result(timeout=30) for future in futures]
        self.assertGreater(session.active_tracked_payload, baseline)
        self.assertEqual(session.worker_budget, 4)
        self.assertEqual(session.active_workers, 0)
        for result in results:
            self.enum(result.status, "kSuccess")
            self.assertEqual(result.face_map, (0, 0))
            self.joined(result.capture_execution.capture)
            self.joined(result.capture_execution.byte_verification)
        del result, results, futures
        gc.collect()
        self.assertEqual(session.active_tracked_payload, baseline)

    def test_unicode_paths_resource_root_and_reopen(self):
        parent = self.root / "\u6e90-\U0001f9ca"
        members = fixture(parent / "\u8d44\u4ea7", True)
        class UnicodePath:
            def __init__(self, path):
                self.path = path
            def __fspath__(self):
                return str(self.path)
        destination = parent / "\u6210\u679c-\U0001f680"
        result = call(UnicodePath(parent / "\u8d44\u4ea7" / ENTRY), UnicodePath(destination),
                      resource_root=UnicodePath(parent / "\u8d44\u4ea7"))
        self.check_success(result, members, destination, True, True)

    def test_rejection_and_existing_destinations_preserve_actual_facts(self):
        for glb in (False, True):
            root = self.root / str(glb)
            members = fixture(root / "source", glb)
            rejected = call(root / "source" / ENTRY, root / "rejected", ((0, 999),))
            self.enum(rejected.status, "kRejected")
            self.assertIsNone(rejected.publication)
            self.assertFalse((root / "rejected").exists())
            report = self.validated_report(rejected)
            self.assertEqual((report["candidate"]["outcome"], meshvale_reports.exit_code(report)), ("rejected", 1))
            for directory in (False, True):
                destination = root / f"occupied-{directory}"
                marker = b"existing destination survives byte for byte\x00\xff"
                if directory:
                    destination.mkdir()
                    (destination / "marker").write_bytes(marker)
                else:
                    destination.write_bytes(marker)
                result = call(root / "source" / ENTRY, destination)
                self.enum(result.outcomes.publication, "kFailed")
                self.enum(result.publication.diagnostic.code, "kDestinationExists")
                self.assertIsNone(result.published_entry_path)
                failed = self.validated_report(result)
                self.assertEqual((failed["publication"]["outcome"], meshvale_reports.exit_code(failed)), ("failed", 2))
                self.assertEqual((destination / "marker" if directory else destination).read_bytes(), marker)
            self.assertEqual(actual_files(root / "source"), members)

    def test_gil_available_and_midcapture_cancellation_joins(self):
        # Large opaque image bytes create real native IO/copy work without a
        # giant numerical mesh. No sleeps/time estimate is used as stop evidence.
        fixture(self.root / "source", image=IMAGE + b"\x5a" * (24 * 1024**2))
        session, cancellation = GltfExecution(), GltfCancellation()
        baseline = session.active_tracked_payload
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(call, self.root / "source" / ENTRY, self.root / "bundle",
                                 execution=session, cancellation=cancellation)
            deadline = time.monotonic() + 20
            observed_live_native_work = False
            while not future.done() and time.monotonic() < deadline:
                if session.active_tracked_payload > baseline:
                    observed_live_native_work = True
                    self.assertTrue(cancellation.cancel())
                    break
                time.sleep(0)
            self.assertTrue(observed_live_native_work, "Python ran while actual native owners were charged")
            result = future.result(timeout=30)
        self.assertTrue(cancellation.cancelled)
        self.assertFalse((self.root / "bundle").exists())
        if result.workflow_started:
            self.enum(result.status, "kCanceled")
            self.enum(result.outcomes.execution, "kCanceled")
            self.joined(result.execution.hashing)
            self.joined(result.execution.report_copy)
        else:
            self.enum(result.capture_status, "kCanceled")
            self.assertIsNone(result.status)
            self.assertIsNone(result.report_json)
        self.joined(result.capture_execution.capture)
        self.joined(result.capture_execution.byte_verification)
        del result, future
        gc.collect()
        self.assertEqual(session.active_tracked_payload, baseline)
        self.assertEqual(session.active_workers, 0)


class AdmissionTests(CommonChecks):
    def assert_before_io(self, options=None, targets=((0, 1),), source=None, destination=None):
        with self.assertRaises((TypeError, ValueError, OverflowError)):
            call(self.root / "does-not-exist" if source is None else source,
                 self.root / "bundle" if destination is None else destination,
                 targets, **(options or {}))
        self.assertEqual(list(self.root.iterdir()), [])

    def test_positive_complete_preset_observations_and_independent_sessions(self):
        balanced = GltfExecution()
        serial = GltfExecution(preset="serial-reference-v1")
        self.assertEqual(balanced.preset, "balanced-v1")
        tree, reference = balanced.resolved_preset, serial.resolved_preset
        self.assertEqual(set(tree), {"execution", "capture_limits", "capture_work", "workflow_limits", "workflow_work"})
        def compare(left, right):
            self.assertEqual(left.keys(), right.keys())
            for key, value in left.items():
                if isinstance(value, dict):
                    compare(value, right[key])
                else:
                    self.assertIs(type(value), int)
                    self.assertGreater(value, 0)
                    if key.startswith("parallel_min_") or key == "minimum_parallel_vertices":
                        self.assertEqual(right[key], (1 << 63) - 1)
                    elif key in {"worker_budget", "max_jobs"}:
                        self.assertEqual(right[key], 1)
                    else:
                        self.assertEqual(right[key], value)
        compare(tree, reference)
        materialization = tree["workflow_limits"]["memory"]["materialization"]
        self.assertEqual(materialization["max_bridge_new_requested_bytes"] +
                         materialization["materialization"]["max_new_requested_bytes"] +
                         materialization["inspection"]["max_new_requested_bytes"], 48 * 1024**2)
        self.assertEqual(tree["workflow_limits"]["memory"]["repair"]["max_targets"], 1_000_000)
        self.assertEqual(tree["workflow_limits"]["memory"]["repair"]["max_declaration_name_bytes"], 256)
        self.assertEqual(tree["capture_limits"]["files"]["max_file_bytes"], 64 * 1024**2)
        with self.assertRaises((TypeError, ValueError)):
            GltfExecution(preset="automatic")

    def test_argument_conversion_precedes_io(self):
        for missing in POLICY:
            with self.subTest(missing=missing), self.assertRaises(TypeError):
                repair_gltf_file(self.root / "does-not-exist", None, (),
                                 **{key: value for key, value in POLICY.items() if key != missing})
        with self.assertRaises(TypeError):
            repair_gltf_file(self.root / "does-not-exist", **POLICY)
        for index in (True, False, None, -1, 1 << 64, 1.0, "0"):
            with self.subTest(index=index):
                self.assert_before_io({"primitive_ordinal": index})
        for targets in (((True, 1),), ((0, -1),), ((0, 1 << 64),), ((0.0, 1),), ((0,),), ((0, 1, 2),), ("01",)):
            with self.subTest(targets=targets):
                self.assert_before_io(targets=targets)
        for options in ({"triangles": "automatic"}, {"values": "normalized-only"},
                        {"input_id": ""}, {"input_id": "bad space"}, {"input_id": "\u6e90"},
                        {"input_id": "a" * 129}, {"input_id": "a\0b"},
                        {"row_local_attributes": (("edge", "id"),)},
                        {"row_local_attributes": (("vertex", ""),)},
                        {"row_local_attributes": (("vertex", "\u00e9" * 129),)},
                        {"row_local_attributes": (("vertex", "a" * 128), ("face", "b" * 129))},
                        {"execution": object()}, {"cancellation": object()},
                        {"report_path": b"report.json"}, {"report_path": "bad\0name"}):
            with self.subTest(options=options):
                self.assert_before_io(options)
        for path in (b"source.gltf", "bad\0path"):
            self.assert_before_io(source=path)
            self.assert_before_io(destination=path)
            self.assert_before_io({"resource_root": path})
        class BytesPath:
            def __fspath__(self):
                return b"source.gltf"
        self.assert_before_io(source=BytesPath())

    def test_bounded_iteration_never_trusts_custom_length(self):
        class Endless:
            def __init__(self, item):
                self.item, self.calls = item, 0
            def __iter__(self):
                return self
            def __next__(self):
                self.calls += 1
                return self.item
            def __len__(self):
                raise AssertionError("custom length must not authorize allocation")
        targets = Endless((0, 1))
        self.assert_before_io(targets=targets)
        self.assertEqual(targets.calls, 1_000_001)
        declarations = Endless(("vertex", "x"))
        self.assert_before_io({"row_local_attributes": declarations})
        self.assertEqual(declarations.calls, 129)
        pair = Endless(0)
        self.assert_before_io(targets=(pair,))
        self.assertLessEqual(pair.calls, 3)

    def test_capture_failures_precancel_and_file_ceiling(self):
        missing = call(self.root / "missing.gltf", None)
        self.assertFalse(missing.workflow_started)
        self.assertIsNone(missing.status)
        self.assertIsNone(missing.outcomes)
        self.assertIsNone(missing.publication)
        self.assertIsNone(missing.report_json)
        self.assertIsNone(missing.source_format)
        source = self.root / "source"
        fixture(source)
        cancellation = GltfCancellation()
        self.assertFalse(cancellation.cancelled)
        self.assertTrue(cancellation.cancel())
        self.assertFalse(cancellation.cancel())
        result = call(source / ENTRY, self.root / "canceled", cancellation=cancellation)
        self.enum(result.capture_status, "kCanceled")
        self.assertFalse(result.workflow_started)
        self.assertIsNone(result.report_json)
        self.assertFalse((self.root / "canceled").exists())
        # Sparse temporary physical file exceeds the actual 64 MiB member cap.
        with (source / "texture.bin").open("wb") as stream:
            stream.seek(64 * 1024**2)
            stream.write(b"\x5a")
        limited = call(source / ENTRY, self.root / "limited")
        self.enum(limited.capture_status, "kLimitExceeded")
        self.assertFalse(limited.workflow_started)
        self.assertIsNone(limited.report_json)
        self.assertFalse((self.root / "limited").exists())

    def test_malformed_json_and_container_keep_capture_diagnostics(self):
        for glb in (False, True):
            source = self.root / str(glb)
            members = fixture(source, glb)
            if glb:
                wire = bytearray(members[ENTRY])
                struct.pack_into("<I", wire, 8, len(wire) + 4)
                (source / ENTRY).write_bytes(wire)
            else:
                (source / ENTRY).write_bytes(b'{"asset":')
            result = call(source / ENTRY, self.root / f"out-{glb}")
            self.enum(result.capture_status, "kMalformed")
            self.assertFalse(result.workflow_started)
            self.assertIsNone(result.status)
            self.assertIsNone(result.outcomes)
            self.assertIsNone(result.report_json)
            self.assertFalse((self.root / f"out-{glb}").exists())
            if glb:
                self.enum(result.capture_diagnostic.container_status, "kMalformed", "GlbStatus")
                self.assertIsInstance(result.capture_diagnostic.container_byte_offset, int)
            else:
                self.assertIsNotNone(result.capture_diagnostic.gltf_diagnostic)
        source = self.root / "missing-position-bounds"
        fixture(source)
        (source / ENTRY).write_bytes(JSON_SOURCE.replace(b',"min":[0,0,0],"max":[1,1,0]', b""))
        result = call(source / ENTRY, self.root / "bounds-output")
        self.enum(result.capture_status, "kSuccess")
        self.assertTrue(result.workflow_started)
        self.enum(result.status, "kDelegatedFailure")
        self.enum(result.diagnostic.memory.stage, "kExtract")
        self.enum(result.diagnostic.memory.delegated_status, "kMalformed")
        self.enum(result.diagnostic.memory.delegated.code, "kCoreFieldRequired")
        self.assertEqual(result.diagnostic.memory.delegated.rule_id, 312)
        self.assertIsNone(result.publication)
        self.assertIsNone(result.face_map)
        failed = self.validated_report(result)
        self.assertEqual((failed["execution"]["stage"], failed["candidate"]["outcome"]),
                         ("import", "not-attempted"))
        self.assertIsNone(failed["input"]["sha256"])
        self.assertFalse((self.root / "bounds-output").exists())


class UnsupportedProfileTests(CommonChecks):
    def test_actual_nonempty_native_profile_refusal_both_formats(self):
        for glb in (False, True):
            root = self.root / str(glb)
            members = fixture(root / "source", glb)
            result = call(root / "source" / ENTRY, root / "bundle")
            self.enum(result.capture_status, "kSuccess")
            self.assertTrue(result.workflow_started)
            self.enum(result.status, "kDelegatedFailure")
            self.enum(result.diagnostic.memory.stage, "kMaterialize")
            self.enum(result.diagnostic.memory.delegated_status, "kGeometryFailure")
            self.enum(result.diagnostic.memory.delegated.geometry_status, "kUnsupportedAllocationProfile")
            self.assertIsNone(result.publication)
            self.assertFalse((root / "bundle").exists())
            self.assertEqual(actual_files(root / "source"), members)
            report = self.validated_report(result)
            self.assertEqual(report["publication"]["outcome"], "not-attempted")
            self.assertEqual(meshvale_reports.exit_code(report), 2)


def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromTestCase(AdmissionTests))
    suite.addTests(loader.loadTestsFromTestCase(
        QualifiedWorkflowTests if PROFILE == "qualified" else UnsupportedProfileTests))
    return suite


if __name__ == "__main__":
    unittest.main(verbosity=2)
