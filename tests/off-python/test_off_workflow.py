# SPDX-License-Identifier: Apache-2.0
"""Real installed OFF API checks; select the actual numerical producer profile.

python -I -m unittest discover -s tests/off-python -v
MESHVALE_OFF_PYTHON_PROFILE is qualified or unsupported. Empty geometry executes
on both; unsupported never fabricates a nonempty numerical owner.
"""
from concurrent.futures import ThreadPoolExecutor
import gc
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest

import jsonschema
import meshvale_reports as reports
from meshvale_repair import off_workflow as workflow
from off_test_fixture import (CHECKS, CORNER_MAP, DISTINCT_IDS, EMPTY, ENTRY,
    EXTENSION, FACE_MAP, POINT, POLICIES, RECEIPT, REVERSED, SOURCE,
    check_output, digest, directory_alias, fixture, maps, physical)

PROFILE = os.environ.get("MESHVALE_OFF_PYTHON_PROFILE", "qualified")
if PROFILE not in {"qualified", "unsupported"}:
    raise ValueError("MESHVALE_OFF_PYTHON_PROFILE must be qualified or unsupported")


def call(source, destination=None, targets=((0, 1),), **options):
    return workflow.repair_off_file(source, destination, targets, **(POLICIES | options))


class Base(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="meshvale-off-python-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve()
    def enum(self, value, member, domain=None):
        if domain:
            self.assertEqual(type(value).__name__, domain)
        self.assertEqual(value, getattr(type(value), member))
    def report(self, result):
        raw = result.report_json
        self.assertIs(type(raw), bytes)
        value = reports.load(raw)
        jsonschema.Draft202012Validator(reports.schema_document()).validate(value)
        self.assertEqual(reports.profile_outcome(value), value["profile"]["outcome"])
        return value
    def joined(self, stats):
        self.assertEqual(stats.launched_workers, stats.joined_workers)
    def all_joined(self, value):
        if isinstance(value, dict):
            if "launched_workers" in value:
                self.assertEqual(value["launched_workers"], value["joined_workers"])
            for child in value.values():
                self.all_joined(child)
        elif isinstance(value, list):
            for child in value:
                self.all_joined(child)
    def success(self, result, raw, changed, destination=None, empty=False, entry=ENTRY):
        self.assertIsInstance(result, workflow.OffFileWorkflowResult)
        self.assertTrue(result.workflow_started)
        self.enum(result.capture_status, "kSuccess", "OffFileStatus")
        self.enum(result.status, "kSuccess", "OffWorkflowStatus")
        self.enum(result.source_profile, "kAsciiRecords", "OffProfile")
        self.enum(result.outcomes.execution, "kCompleted")
        self.enum(result.outcomes.candidate, "accepted" if changed else "unchanged", "CandidateOutcome")
        self.enum(result.outcomes.profile, "kPassed")
        self.enum(result.outcomes.exit, "kSuccess")
        self.assertEqual((result.face_map, result.corner_map, result.source_faces, result.source_corners), maps(changed, empty))
        self.assertTrue(all(type(value) is tuple for value in
                            (result.face_map, result.corner_map, result.source_faces, result.source_corners)))
        self.assertEqual(result.source_entry_path, entry)
        self.assertEqual(result.input_sha256, digest(raw))
        self.assertEqual(result.input_snapshot_counts, (0, 0, 0) if empty else (8, 3, 11))
        self.assertEqual(result.candidate_snapshot_counts, (0, 0, 0) if empty else (8, 2, 7) if changed else (8, 3, 11))
        check_output(self, result.output_bytes, changed, empty)
        self.assertNotEqual(result.output_bytes, raw)
        facts = result.write_facts
        self.assertEqual((facts.vertex_count, facts.face_count, facts.corner_count), result.candidate_snapshot_counts)
        self.assertEqual((facts.source_declared_edge_count, facts.emitted_edge_count), (17, 0))
        self.enum(facts.correspondence, "kIdentity", "OffCorrespondence")
        value = self.report(result)
        self.assertEqual(value["input"]["sha256"], digest(raw))
        self.assertEqual(reports.exit_code(value), 0)
        self.assertEqual(value["profile"]["required_checks"], list(CHECKS[:9 if destination else 7]))
        coverage = {row["check"]: row for row in value["coverage"]}
        self.assertEqual(set(coverage), {*CHECKS, "solid-validity"})
        for name in CHECKS[:9 if destination else 7]:
            self.assertEqual((coverage[name]["status"], coverage[name]["finding"]), ("performed", "passed"))
        self.assertEqual((coverage["solid-validity"]["status"], coverage["solid-validity"]["finding"]), ("unsupported", None))
        extension = value["extensions"][EXTENSION]
        self.assertEqual((extension["source_faces"], extension["source_corners"]), (list(result.source_faces), list(result.source_corners)))
        self.assertEqual(extension["source"], {"portable_name": entry, "profile": "ascii-records", "declared_edge_count": 17})
        self.assertEqual(extension["policies"], {"text": "canonical-ascii", "edge_count": "unknown-zero", "units": "unknown"})
        self.all_joined(extension["phase_execution"])
        self.joined(result.capture_execution.capture)
        self.assertGreater(result.capture_requested_bytes.files, 0)
        self.assertGreater(result.requested_bytes.wrapper, 0)
        self.assertFalse(hasattr(result.requested_bytes, "total"))
        if destination:
            self.enum(result.outcomes.publication, "kPublished")
            self.enum(result.publication.outcome, "kPublished", "OffFilePublicationOutcome")
            self.assertEqual((result.published_entry_path, result.published_report_path), (entry, RECEIPT))
            files = physical(destination)
            self.assertEqual(files, {entry: result.output_bytes, RECEIPT: result.report_json})
            self.assertEqual(value["publication"]["artifacts"], [{"id": "entry", "sha256": digest(files[entry])}])
            self.assertEqual(extension["hash_bytes_processed"], len(raw) + len(result.output_bytes))
            self.assertGreater(result.requested_bytes.publication_files, 0)
            self.assertGreater(result.requested_bytes.publication_off.total, 0)
        else:
            self.enum(result.outcomes.publication, "kNotRequested")
            self.assertIsNone(result.publication)
            self.assertIsNone(result.published_entry_path)
            self.assertIsNone(result.published_report_path)
            self.assertEqual(value["publication"]["artifacts"], [])
            self.assertNotIn("output", {row["role"] for row in value["snapshots"]})
            self.assertEqual(extension["hash_bytes_processed"], len(raw))
        return value


class CommonTests(Base):
    def test_real_empty_memory_and_publication_both_presets(self):
        for preset in ("balanced-v1", "serial-reference-v1"):
            for publish in (False, True):
                with self.subTest(preset=preset, publish=publish):
                    root = self.root / f"{preset}-{publish}"
                    source = fixture(root / "source", EMPTY)
                    destination = root / "bundle" if publish else None
                    session = workflow.OffExecution(preset=preset)
                    result = call(source, destination, (), execution=session)
                    self.success(result, EMPTY, False, destination, True)
                    del result
                    gc.collect()
                    self.assertEqual((session.active_tracked_payload, session.active_workers), (0, 0))
    def test_prepare_is_once_owned_conversion_without_capture(self):
        source = fixture(self.root / "source", EMPTY)
        prepared = workflow._prepare_off_request(source, None, (), **POLICIES)
        with self.assertRaises(ValueError):
            _ = prepared.capture_status
        source.write_bytes(b"OFF\nnot counts\n")
        result = workflow._run_off_request(prepared)
        self.assertIs(result, prepared)
        self.enum(result.capture_status, "kOffFailure")
        self.enum(result.capture_diagnostic.off_status, "kMalformed")
        self.assertFalse(result.workflow_started)
        self.assertIsNone(result.status)
        self.assertIsNone(result.report_json)
        with self.assertRaises(ValueError):
            workflow._run_off_request(prepared)
    def test_actual_missing_malformed_and_unsupported_variant_capture(self):
        for name, raw, status in (("missing", None, "kIoFailure"), ("malformed", b"OFF\n1 x 0\n", "kOffFailure"),
                                  ("variant", b"COFF\n0 0 0\n", "kOffFailure")):
            source = self.root / (name + ".off")
            if raw is not None:
                source.write_bytes(raw)
            result = call(source, self.root / (name + "-out"), ())
            self.enum(result.capture_status, status)
            self.assertFalse(result.workflow_started)
            for value in (result.status, result.outcomes, result.publication, result.report_json, result.face_map,
                          result.corner_map, result.source_faces, result.source_corners, result.input_sha256):
                self.assertIsNone(value)
            self.assertFalse((self.root / (name + "-out")).exists())
            if name == "variant":
                self.enum(result.capture_diagnostic.off_status, "kUnsupportedVariant")
    def test_precanceled_capture_is_actual_and_joined(self):
        source = fixture(self.root / "source", EMPTY)
        token = workflow.OffCancellation()
        self.assertFalse(token.cancelled)
        self.assertTrue(token.cancel())
        self.assertFalse(token.cancel())
        result = call(source, self.root / "out", (), cancellation=token)
        self.enum(result.capture_status, "kCanceled")
        self.assertFalse(result.workflow_started)
        self.assertIsNone(result.report_json)
        self.joined(result.capture_execution.capture)
        self.assertFalse((self.root / "out").exists())
    def test_logical_directory_alias_refuses_and_resolved_empty_control_runs(self):
        source = fixture(self.root / "actual", EMPTY)
        alias = directory_alias(self, self.root / "alias", source.parent)
        result = call(alias / ENTRY, self.root / "alias-output", ())
        self.enum(result.capture_status, "kUnsupportedProfile")
        self.enum(result.capture_diagnostic.code, "kPathUnsupported")
        self.assertFalse(result.workflow_started)
        self.assertIsNone(result.report_json)
        self.assertFalse((self.root / "alias-output").exists())
        self.success(call(source, None, ()), EMPTY, False, empty=True)
        self.assertEqual(source.read_bytes(), EMPTY)
    def test_complete_presets_positive_independent_detached_observations(self):
        balanced, serial = workflow.OffExecution(), workflow.OffExecution(preset="serial-reference-v1")
        self.assertEqual((balanced.worker_budget, serial.worker_budget), (4, 1))
        left, right = balanced.resolved_preset, serial.resolved_preset
        leaves, changes = [], []
        def visit(a, b, path=()):
            self.assertEqual(a.keys(), b.keys())
            for key, value in a.items():
                location = (*path, key)
                if isinstance(value, dict):
                    visit(value, b[key], location)
                else:
                    self.assertIs(type(value), int)
                    self.assertGreater(value, 0)
                    leaves.append(location)
                    if value != b[key]:
                        changes.append(location)
                        self.assertEqual(b[key], 1 if key in {"worker_budget", "max_jobs"} else (1 << 63) - 1)
        visit(left, right)
        self.assertEqual((len(leaves), len(changes)), (183, 24))
        left["execution"]["worker_budget"] = 99
        self.assertEqual(balanced.resolved_preset["execution"]["worker_budget"], 4)
        self.assertEqual((balanced.active_tracked_payload, serial.active_tracked_payload), (0, 0))
        with self.assertRaises(ValueError):
            workflow.OffExecution(preset="unselected")
    def test_real_gil_visible_capture_cancellation_on_large_empty_source(self):
        source = fixture(self.root / "source", (b"#" + b"x" * 63 + b"\n") * (384 * 1024) + EMPTY)
        session, token = workflow.OffExecution(), workflow.OffCancellation()
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(call, source, self.root / "out", (), execution=session, cancellation=token)
            observed = False
            deadline = time.monotonic() + 20
            while not future.done() and time.monotonic() < deadline:
                if session.active_tracked_payload:
                    observed = True
                    token.cancel()
                    break
                time.sleep(0)
            self.assertTrue(observed, "Python ran while actual native owners were charged")
            result = future.result(timeout=30)
        self.assertFalse((self.root / "out").exists())
        if result.workflow_started:
            self.enum(result.status, "kCanceled")
            self.enum(result.outcomes.execution, "kCanceled")
        else:
            self.enum(result.capture_status, "kCanceled")
        self.joined(result.capture_execution.capture)
        del result, future
        gc.collect()
        self.assertEqual((session.active_tracked_payload, session.active_workers), (0, 0))


class AdmissionTests(Base):
    def invalid(self, *, source=None, destination=None, targets=(), options=None):
        with self.assertRaises((TypeError, ValueError, OverflowError, UnicodeError)):
            call(self.root / "missing.off" if source is None else source,
                 self.root / "out" if destination is None else destination, targets, **(options or {}))
        self.assertEqual(list(self.root.iterdir()), [])
    def test_policy_identity_session_token_and_boolean_indices_before_io(self):
        for options in ({"text": "automatic"}, {"edge_count": "preserve"}, {"text": None},
                        {"input_id": "bad space"}, {"input_id": "_bad"}, {"input_id": ""},
                        {"input_id": "a" * 129}, {"input_id": "\u00e9"}, {"input_id": "\ud800"},
                        {"execution": object()}, {"cancellation": object()}):
            with self.subTest(options=options):
                self.invalid(options=options)
        for targets in (((False, 1),), ((0, True),), ((-1, 1),), ((0, 1 << 64),), ((0,),), (None,), None):
            with self.subTest(targets=targets):
                self.invalid(targets=targets)
    def test_native_utf8_path_receipt_and_target_extents_before_io(self):
        for source in (b"missing.off", object(), "nul\0path", "x" * 4096, "\U0001f600" * 1025):
            with self.subTest(source_type=type(source)):
                self.invalid(source=source)
        for name in ("", ".", "..", "../report", "a/b", "a\\b", "CON", "nul.txt", "trailing.",
                     "trailing ", "bad:name", "control\nname", "missing.off", "MISSING.OFF",
                     "a" * 1025, "\u00e9" * 513, "\ud800"):
            with self.subTest(name=name[:20]):
                self.invalid(options={"report_path": name})
        self.invalid(targets=[(0, 1)] * 1_000_001)
    def test_single_row_entry_output_overlap_refuses_before_capture(self):
        source = self.root / "missing.off"
        for destination in (source, source / "nested", self.root):
            with self.subTest(destination=destination):
                self.invalid(source=source, destination=destination)
        with self.assertRaises(TypeError):
            call(None, None, ())


class QualifiedTests(Base):
    def test_changed_unchanged_memory_and_publication_literal_corpus(self):
        for changed in (False, True):
            for publish in (False, True):
                root = self.root / f"{changed}-{publish}"
                source = fixture(root / "source")
                destination = root / "bundle" if publish else None
                result = call(source, destination, ((0, 1),) if changed else ())
                self.success(result, SOURCE, changed, destination)
                self.assertEqual(source.read_bytes(), SOURCE)
                if publish:
                    reopened = call(destination / ENTRY, None, ())
                    self.enum(reopened.status, "kSuccess")
                    self.assertEqual(reopened.input_snapshot_counts, (8, 2, 7) if changed else (8, 3, 11))
                    self.assertEqual(reopened.face_map, tuple(range(2 if changed else 3)))
    def test_prepared_owned_targets_pathlike_and_final_result_single_use(self):
        source = fixture(self.root / "source")
        class OwnedPath:
            def __init__(self, value):
                self.value, self.calls = value, 0
            def __fspath__(self):
                self.calls += 1
                return str(self.value)
        path, targets = OwnedPath(source), [[0, 1]]
        prepared = workflow._prepare_off_request(path, None, targets, **POLICIES)
        path.value = self.root / "missing.off"
        targets[0][:] = [2, 1]
        self.assertEqual(path.calls, 1)
        result = workflow._run_off_request(prepared)
        self.assertIs(result, prepared)
        self.assertEqual(path.calls, 1)
        self.success(result, SOURCE, True)
        with self.assertRaises(ValueError):
            workflow._run_off_request(prepared)
        self.assertEqual(result.face_map, FACE_MAP)
    def test_same_default_or_explicit_session_retention_and_full_release(self):
        source = fixture(self.root / "source")
        for session in (workflow.OffExecution(), workflow._DEFAULT_EXECUTION):
            gc.collect()
            baseline = session.active_tracked_payload
            one = call(source, None, execution=session)
            first = session.active_tracked_payload
            two = call(source, None, execution=session)
            self.assertGreater(first, baseline)
            self.assertGreater(session.active_tracked_payload, first)
            self.assertEqual(one.face_map, two.face_map)
            del two
            gc.collect()
            self.assertEqual(session.active_tracked_payload, first)
            del one
            gc.collect()
            self.assertEqual((session.active_tracked_payload, session.active_workers), (baseline, 0))
    def test_unicode_source_removal_session_result_and_detached_lifetimes(self):
        entry = "\u6e90-\U0001f600.off"
        source = fixture(self.root / "\u8d44\u4ea7", entry=entry)
        destination = self.root / "\u6210\u679c-\U0001f680"
        session = workflow.OffExecution()
        result = call(source, destination, execution=session)
        source.unlink()
        del session
        gc.collect()
        self.success(result, SOURCE, True, destination, entry=entry)
        raw, face_map, diagnostic = result.report_json, result.face_map, result.capture_diagnostic
        with self.assertRaises((AttributeError, TypeError)):
            result.status = None
        with self.assertRaises((AttributeError, TypeError)):
            diagnostic.code = None
        del result
        gc.collect()
        self.assertEqual(face_map, FACE_MAP)
        self.assertEqual(reports.load(raw)["candidate"]["outcome"], "accepted")
        self.enum(diagnostic.status, "kSuccess")
    def test_reversed_distinct_ids_range_rejections_and_existing_directory(self):
        for name, raw, targets, code in (("reversed", REVERSED, ((0, 1),), "kNotEquivalent"),
                                         ("distinct", DISTINCT_IDS, ((0, 1),), "kNotEquivalent"),
                                         ("range", SOURCE, ((0, 99),), "kTargetRange")):
            source = fixture(self.root / name, raw)
            result = call(source, self.root / (name + "-out"), targets)
            self.enum(result.status, "kRejected")
            self.enum(result.diagnostic.delegated.code, code)
            self.assertIsNone(result.face_map)
            self.assertIsNone(result.input_sha256)
            report = self.report(result)
            self.assertEqual((report["execution"]["outcome"], report["execution"]["stage"], reports.exit_code(report)), ("completed", "complete", 1))
            self.assertFalse((self.root / (name + "-out")).exists())
        destination = self.root / "occupied"
        destination.mkdir()
        (destination / "marker").write_bytes(b"preserve\0\xff")
        source = fixture(self.root / "occupied-source")
        result = call(source, destination)
        self.enum(result.publication.diagnostic.code, "kDestinationExists")
        self.enum(result.outcomes.execution, "kFailed")
        self.enum(result.outcomes.candidate, "accepted")
        self.assertEqual(result.face_map, FACE_MAP)
        self.assertEqual(result.input_sha256, digest(SOURCE))
        self.assertEqual(physical(destination), {"marker": b"preserve\0\xff"})
        self.assertEqual(reports.exit_code(self.report(result)), 2)
    def test_point_only_and_concurrent_shared_session_serial_agreement(self):
        point = fixture(self.root / "point", POINT)
        result = call(point, None, ())
        self.enum(result.status, "kSuccess")
        self.assertEqual(result.candidate_snapshot_counts, (1, 0, 0))
        self.assertEqual((result.face_map, result.corner_map, result.source_faces, result.source_corners), ((), (), (), ()))
        source = fixture(self.root / "source")
        session = workflow.OffExecution()
        gate = threading.Barrier(3)
        def run():
            gate.wait(timeout=10)
            return call(source, None, execution=session)
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(run), pool.submit(run)]
            gate.wait(timeout=10)
            results = [future.result(timeout=30) for future in futures]
        self.assertGreater(session.active_tracked_payload, 0)
        serial = call(source, None, execution=workflow.OffExecution(preset="serial-reference-v1"))
        for item in results:
            self.assertEqual((item.face_map, item.corner_map), (serial.face_map, serial.corner_map))
        del item, results, futures
        gc.collect()
        self.assertEqual((session.active_tracked_payload, session.active_workers), (0, 0))


class UnsupportedTests(Base):
    def test_actual_nonempty_profile_refusal_is_capture_failure_without_fake_report(self):
        source = fixture(self.root / "source")
        result = call(source, self.root / "out")
        self.enum(result.capture_status, "kOffFailure")
        self.enum(result.capture_diagnostic.off_status, "kGeometryFailure")
        self.enum(result.capture_diagnostic.off_diagnostic.geometry_status, "kUnsupportedAllocationProfile")
        self.assertFalse(result.workflow_started)
        self.assertIsNone(result.status)
        self.assertIsNone(result.report_json)
        self.assertIsNone(result.face_map)
        self.assertFalse((self.root / "out").exists())


def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite()
    for group in (CommonTests, AdmissionTests, QualifiedTests if PROFILE == "qualified" else UnsupportedTests):
        suite.addTests(loader.loadTestsFromTestCase(group))
    return suite


if __name__ == "__main__":
    unittest.main(verbosity=2)
