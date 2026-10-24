# SPDX-License-Identifier: Apache-2.0
"""Installed OFF command checks; native work is genuine, fault boundaries explicit."""
from copy import deepcopy
import gc
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import jsonschema
import meshvale_reports as reports
from meshvale_repair import commands, off_commands as off, off_workflow as workflow
from off_command_fixture import (CHECKS, COMMAND_EXTENSION, CORNER_MAP, DISTINCT_IDS,
    EMPTY, ENTRY, EXTENSION, FACE_MAP, POLICIES, RECEIPT, REQUEST_SCHEMA, REVERSED,
    SOURCE, SOURCE_CORNERS, SOURCE_FACES, canonical, check_output, digest, directory_alias, fixture,
    job, physical, request)

PROFILE = os.environ.get("MESHVALE_OFF_PYTHON_PROFILE", "qualified")
if PROFILE not in {"qualified", "unsupported"}:
    raise ValueError("MESHVALE_OFF_PYTHON_PROFILE must be qualified or unsupported")
REQUEST_BYTES = 8 * 1024**2
REPORT_BYTES = 16 * 1024**2


class BrokenOutput:
    def __init__(self, phase, callback=None):
        self.phase, self.callback = phase, callback
        self.payload, self.writes = "", 0
    def write(self, value):
        self.writes += 1
        if self.callback:
            self.callback()
        if self.phase == "write":
            raise BrokenPipeError("private-output-marker")
        self.payload += value
        if self.phase == "short":
            return len(value) - 1
        if self.phase == "none":
            return None
        return len(value)
    def flush(self):
        if self.phase == "flush":
            raise OSError("private-output-marker")


class Base(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="meshvale-off-cli-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve()
        (self.root / "outputs").mkdir()
        self.source = fixture(self.root / "sources/a")
    def checked(self, value):
        reports.validate(value)
        jsonschema.Draft202012Validator(reports.schema_document()).validate(value)
        self.assertNotIn(str(self.root), json.dumps(value))
        return value
    def parsed(self, result):
        self.assertIs(type(result.report_json), bytes)
        self.assertIs(type(result.exit_code), int)
        value = self.checked(reports.load(result.report_json))
        self.assertEqual(reports.exit_code(value), result.exit_code)
        self.assertEqual(result.report, value)
        detached = result.report
        detached["tool"]["name"] = "detached only"
        self.assertEqual(result.report, value)
        with self.assertRaises((AttributeError, TypeError)):
            result.report_json = b"replacement"
        return value
    def invoke(self, *arguments, console=False, cwd=None):
        binary = Path(sys.executable).parent / ("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        prefix = [str(binary)] if console else [sys.executable, "-I", "-m", "meshvale_repair"]
        return subprocess.run(prefix + list(arguments), cwd=cwd or self.root,
                              capture_output=True, timeout=90)
    def single(self, row):
        arguments = ["off", str(self.root / row["input"]), str(self.root / row["destination"]),
                     "--input-id", row["id"], "--text", row["text"], "--edge-count", row["edge_count"]]
        if row["targets"]:
            for keep, remove in row["targets"]:
                arguments += ["--target", f"{keep}:{remove}"]
        else:
            arguments += ["--unchanged"]
        if "report_path" in row:
            arguments += ["--report-path", row["report_path"]]
        return arguments
    def refuse(self, value, **options):
        with patch.object(workflow, "_prepare_off_request") as prepare, patch.object(workflow, "_run_off_request") as run:
            with self.assertRaises(commands.InvocationError):
                off.run_off_jobs(value, base=self.root, **options)
            prepare.assert_not_called()
            run.assert_not_called()
        self.assertEqual(self.source.read_bytes(), SOURCE)
        self.assertEqual(list((self.root / "outputs").iterdir()), [])
    def path_native_facts(self, result):
        publication = result.publication
        return {"capture": result.capture_status.name,
                "capture_code": result.capture_diagnostic.code.name,
                "started": result.workflow_started,
                "status": None if result.status is None else result.status.name,
                "publication": None if publication is None else publication.outcome.name,
                "publication_code": None if publication is None else publication.diagnostic.code.name,
                "counts": result.candidate_snapshot_counts,
                "face_map": result.face_map, "corner_map": result.corner_map,
                "source_faces": result.source_faces, "source_corners": result.source_corners}
    def path_attempt(self, row, frontend, session, base=None):
        observed, original = [], workflow._run_off_request
        def observe(prepared):
            actual = original(prepared)
            observed.append(self.path_native_facts(actual))
            return actual
        if frontend == "api":
            result = workflow.repair_off_file(row["input"], row["destination"], row["targets"],
                input_id=row["id"], execution=session, **POLICIES)
            try:
                observed.append(self.path_native_facts(result))
                raw = result.report_json
                value = None if raw is None else self.checked(reports.load(raw))
            finally:
                result = None
        elif frontend in ("single", "batch"):
            with patch.object(workflow, "_run_off_request", new=observe):
                result = (off._run_off_single(row, base=self.root if base is None else base, execution=session)
                          if frontend == "single" else
                          off.run_off_jobs(request(row), base=self.root if base is None else base, execution=session))
            value = self.parsed(result)
            if frontend == "batch":
                self.assertEqual(value["jobs"][0]["outcome"], "attempted")
                value = value["jobs"][0]["report"]
        else:
            # Preserve exact raw operands: Base.single intentionally makes its
            # ordinary fixtures absolute and would mask this regression.
            arguments = ["off", row["input"], row["destination"], "--input-id", row["id"],
                         "--text", "canonical-ascii", "--edge-count", "unknown-zero"]
            if row["targets"]:
                for keep, remove in row["targets"]:
                    arguments += ["--target", f"{keep}:{remove}"]
            else:
                arguments += ["--unchanged"]
            process = self.invoke(*arguments, console=frontend == "console", cwd=self.root)
            self.assertTrue(process.stdout, process.stderr)
            value = self.checked(reports.load(process.stdout))
            self.assertEqual(process.returncode, reports.exit_code(value))
        gc.collect()
        if frontend in ("api", "single", "batch"):
            self.assertEqual((session.active_tracked_payload, session.active_workers), (0, 0))
        # Child frontends expose actual joined phase counters; a separate
        # parent session cannot witness a child process's retained ledger.
        def joined(value):
            if isinstance(value, dict):
                if "launched_workers" in value:
                    self.assertEqual(value["launched_workers"], value["joined_workers"])
                for child in value.values():
                    joined(child)
            elif isinstance(value, list):
                for child in value:
                    joined(child)
        if value is not None:
            joined(value["extensions"])
        self.assertEqual(self.source.read_bytes(), SOURCE)
        return value, observed
    def maps(self, report, changed, empty=False):
        snapshots = {row["role"]: row["meshes"][0] for row in report["snapshots"]}
        for role, counts in (("input", (0, 0, 0) if empty else (8, 3, 11)),
                             ("candidate", (0, 0, 0) if empty else (8, 2, 7) if changed else (8, 3, 11))):
            self.assertEqual(tuple(snapshots[role][key] for key in ("vertex_count", "face_count", "corner_count")), counts)
        maps = {row["domain"]: row for row in report["maps"] if row["from"]["snapshot"] == "input"}
        self.assertEqual(set(maps), {"vertex", "face", "corner"})
        for domain in maps:
            explicit = changed and domain in {"face", "corner"}
            self.assertEqual(maps[domain]["kind"], "explicit" if explicit else "identity")
            self.assertEqual(maps[domain]["entries"], list(FACE_MAP if domain == "face" else CORNER_MAP) if explicit else None)
    def fallback(self, value, started):
        extension = value["extensions"][COMMAND_EXTENSION]
        self.assertIs(extension["native_report_available"], False)
        self.assertIs(extension["workflow_started"], started)
        self.assertEqual(value["profile"]["name"], "off-command-receipt")
        self.assertEqual(value["profile"]["version"], "1")
        self.assertEqual(value["profile"]["required_checks"], ["native-receipt-validation"])
        self.assertEqual(value["profile"]["outcome"], "incomplete")
        coverage, = value["coverage"]
        self.assertEqual((coverage["status"], coverage["finding"]), ("skipped", None))
        self.assertTrue(coverage["reason"])
        self.assertEqual(value["preservation"], [])
        self.assertEqual(value["publication"]["artifacts"], [])
        return extension


class AdmissionTests(Base):
    def test_invalid_last_row_never_prepares_or_captures_any_source(self):
        bad_rows = []
        for key, value in (("id", "bad space"), ("id", "a" * 129), ("id", "\ud800"),
                           ("input", b"source"), ("input", ""), ("destination", ""),
                           ("text", "automatic"), ("edge_count", "preserve"), ("targets", [[False, 1]]),
                           ("targets", [[0, True]]), ("targets", [[0, -1]]), ("targets", [[0, 1 << 64]]),
                           ("targets", [[0]]), ("targets", ((0, 1),)), ("report_path", "../receipt.json"),
                           ("report_path", "source.off"), ("report_path", "SOURCE.OFF")):
            bad_rows.append(job("last") | {key: value})
        for key in ("text", "edge_count", "targets", "destination"):
            row = job("last")
            del row[key]
            bad_rows.append(row)
        bad_rows += [job("first"), job("last") | {"primitive_ordinal": 0},
                     job("last") | {"row_local_attributes": []}, job("last") | {"resource_root": "sources/a"}]
        for bad in bad_rows:
            with self.subTest(keys=tuple(bad)):
                self.refuse(request(job("first"), bad))
    def test_plain_closed_containers_types_and_poison_subclasses(self):
        class PoisonList(list):
            def __iter__(self):
                raise AssertionError("non-plain list was iterated")
            def __len__(self):
                raise AssertionError("non-plain list was sized")
        class PoisonText(str):
            def encode(self, *args, **kwargs):
                raise AssertionError("non-plain text was encoded")
            def __len__(self):
                raise AssertionError("non-plain text was sized")
        class PoisonMap(dict):
            def __iter__(self):
                raise AssertionError("non-plain dictionary was iterated")
        values = [{}, request() | {"extra": 1}, {"schema": "other", "jobs": []},
                  {"schema": REQUEST_SCHEMA, "jobs": ()}, PoisonMap(request()),
                  {"schema": REQUEST_SCHEMA, "jobs": PoisonList()}, request(PoisonMap(job("last"))),
                  request(job("last") | {"targets": PoisonList()}),
                  request(job("last") | {"id": PoisonText("name")}),
                  request(job("last") | {"input": PoisonText("source")})]
        for value in values:
            self.refuse(value)
        for options in ({"fail_fast": 1}, {"fail_fast": None}, {"execution": object()}, {"cancellation": object()}):
            self.refuse(request(job("valid")), **options)
    def test_job_target_text_path_and_canonical_request_caps_before_preparation(self):
        self.refuse(request(*(job(f"job{i}") for i in range(17))))
        self.refuse(request(job("count") | {"targets": [[0, 1]] * 1_000_001}))
        large_pairs = [[(1 << 64) - 2, (1 << 64) - 1]] * 200_000
        value = request(job("encoded") | {"targets": large_pairs})
        self.assertGreater(len(canonical(value)), REQUEST_BYTES)
        self.refuse(value)
        for values in ({"input": "x" * 4096}, {"input": "\U0001f600" * 1025},
                       {"destination": "x" * 4096}, {"report_path": "a" * 1025},
                       {"report_path": "\u00e9" * 513}, {"id": "x" * 1_000_000}):
            self.refuse(request(job("first"), job("last") | values))
    def test_bounded_utf8_json_file_duplicate_nonfinite_and_exact_byte_ceiling(self):
        path = self.root / "jobs.json"
        for raw in (b'{"schema":"meshvale.repair.off-jobs/1","jobs":[],"jobs":[]}',
                    b'{"schema":"meshvale.repair.off-jobs/1","jobs":[NaN]}', b'{"jobs":[Infinity]}', b'\xff'):
            path.write_bytes(raw)
            with self.assertRaises(commands.InvocationError):
                off._read_off_request(path)
        valid = canonical(request(job("valid")))
        path.write_bytes(valid + b" " * (REQUEST_BYTES - len(valid)))
        self.assertEqual(off._read_off_request(path), request(job("valid")))
        with path.open("ab") as stream:
            stream.write(b" ")
        with self.assertRaises(commands.InvocationError):
            off._read_off_request(path)
    def test_source_entry_output_and_cross_row_output_isolation(self):
        variants = [request(job("a", destination="sources/a/source.off")),
                    request(job("a", destination="sources/a/source.off/nested")),
                    request(job("a", destination="sources/a")),
                    request(job("a"), job("b", destination="outputs/a/nested")),
                    request(job("a"), job("b", destination="outputs/a")),
                    request(job("a", destination="sources/b/source.off"), job("b", source="sources/b/source.off"))]
        for value in variants:
            self.refuse(value)
    def test_private_encoded_and_framing_boundaries_are_separate_wire_controls(self):
        # These are helper-only byte-accounting controls, not claims that a
        # native operation generated a maximal report or maximal batch framing.
        for ceiling in (REQUEST_BYTES, REPORT_BYTES):
            text = "\u00e9" * ((ceiling - 2) // 2)
            wire = off._encoded(text, ceiling, commands.InvocationError)
            self.assertEqual(len(wire), ceiling)
            with self.assertRaises(commands.InvocationError):
                off._encoded(text + "x", ceiling, commands.InvocationError)
        actual = off.run_off_jobs(request(), base=self.root)
        size = len(actual.report_json)
        with patch.object(off, "_MAX_FRAMING_BYTES", size):
            self.assertEqual(off._frame_batch([], "continue", False, []).report_json,
                             actual.report_json)
        with patch.object(off, "_MAX_FRAMING_BYTES", size - 1):
            with self.assertRaises(off.OffCommandDeliveryError) as caught:
                off._frame_batch([], "continue", False, [])
        self.assertEqual(caught.exception.exit_code, 2)
    def test_missing_declared_report_dependency_refuses_before_native_prepare(self):
        child = """import builtins,sys
original=builtins.__import__
def guarded(name,*args,**kwargs):
 if name.split('.')[0]=='jsonschema': raise ImportError('missing declared report dependency')
 return original(name,*args,**kwargs)
builtins.__import__=guarded
from meshvale_repair import off_workflow as w, off_commands as c
def forbidden(*args,**kwargs): raise AssertionError('native preparation reached')
w._prepare_off_request=forbidden
try:
 c.run_off_jobs({'schema':'meshvale.repair.off-jobs/1','jobs':[]})
except ImportError:
 raise SystemExit(0)
raise SystemExit('missing dependency was not preflighted')
"""
        process = subprocess.run([sys.executable, "-I", "-c", child], cwd=self.root, capture_output=True, timeout=60)
        self.assertEqual(process.returncode, 0, process.stderr)


class CommonTests(Base):
    def test_source_and_destination_suffixes_remain_native_attempts(self):
        source = fixture(self.root / "sources/suffix", EMPTY)
        previous = Path.cwd()
        try:
            os.chdir(self.root)
            for field in ("source", "destination"):
                for suffix, label in (("/.", "dot"), ("/", "separator")):
                    for frontend in ("api", "single", "batch", "module", "console"):
                        with self.subTest(field=field, suffix=label, frontend=frontend):
                            identity = "suffix-" + field + "-" + label + "-" + frontend
                            absolute = self.root / "outputs" / identity
                            row = job(identity, source=str(source) + (suffix if field == "source" else ""),
                                      destination=str(absolute) + (suffix if field == "destination" else ""),
                                      changed=False)
                            value, observed = self.path_attempt(row, frontend, workflow.OffExecution())
                            if field == "source":
                                if observed:
                                    self.assertEqual((observed[0]["capture"], observed[0]["capture_code"],
                                                      observed[0]["started"]),
                                                     ("kUnsupportedProfile", "kNameUnsupported", False))
                                if value is not None:
                                    self.assertEqual((value["publication"]["outcome"], reports.exit_code(value)),
                                                     ("not-attempted", 2))
                                    extension = self.fallback(value, False)
                                    self.assertEqual(extension["capture"]["diagnostic"]["code"],
                                                     {"domain": "OffFileDiagnosticCode", "member": "kNameUnsupported"})
                            else:
                                code = "kOpen" if label == "dot" else "kPathUnsupported"
                                if observed:
                                    fact = observed[0]
                                    self.assertEqual((fact["capture"], fact["started"], fact["publication"],
                                                      fact["publication_code"]),
                                                     ("kSuccess", True, "kFailed", code))
                                    self.assertEqual((fact["counts"], fact["face_map"], fact["corner_map"],
                                                      fact["source_faces"], fact["source_corners"]),
                                                     ((0, 0, 0), (), (), (), ()))
                                self.assertEqual((value["candidate"]["outcome"], value["publication"]["outcome"],
                                                  reports.exit_code(value)), ("unchanged", "failed", 2))
                                self.maps(value, False, True)
                                extension = value["extensions"][EXTENSION]
                                self.assertEqual(extension["publisher"]["diagnostic"]["code"],
                                                 getattr(workflow._repair.OffFileDiagnosticCode, code).value)
                                self.assertEqual((extension["source_faces"], extension["source_corners"]), ([], []))
                                self.assertNotIn("output", {snapshot["role"] for snapshot in value["snapshots"]})
                            self.assertFalse(absolute.exists())
                            self.assertEqual(source.read_bytes(), EMPTY)
            request_path = self.root / "suffix-jobs.json"
            for suffix, label in (("/.", "dot"), ("/", "separator")):
                reader_value = request(job("reader-" + label,
                    source=str(source.relative_to(self.root)), changed=False))
                request_path.write_bytes(canonical(reader_value))
                # Request files use bounded Python IO, not the native asset
                # profile. Windows accepts file/., whose raw logical parent
                # must then remain a regular file during native capture.
                reader_accepts = os.name == "nt" and label == "dot"
                with self.subTest(reader_suffix=label):
                    if reader_accepts:
                        self.assertEqual(off._read_off_request(str(request_path) + suffix), reader_value)
                    else:
                        with self.assertRaises(commands.InvocationError):
                            off._read_off_request(str(request_path) + suffix)
                for frontend in ("inprocess", "module", "console"):
                    with self.subTest(request_suffix=label, frontend=frontend):
                        row = job("request-suffix-" + label + "-" + frontend,
                                  source=str(source.relative_to(self.root)), changed=False)
                        request_path.write_bytes(canonical(request(row)))
                        arguments = ["off-batch", str(request_path) + suffix]
                        if frontend == "inprocess":
                            calls, observed = [], []
                            original_prepare, original_run = workflow._prepare_off_request, workflow._run_off_request
                            def prepare(*args, **kwargs):
                                calls.append("prepare")
                                return original_prepare(*args, **kwargs)
                            def run(prepared):
                                calls.append("run")
                                actual = original_run(prepared)
                                observed.append(self.path_native_facts(actual))
                                return actual
                            output, error = io.StringIO(), io.StringIO()
                            with patch.object(workflow, "_prepare_off_request", new=prepare), \
                                 patch.object(workflow, "_run_off_request", new=run):
                                code = commands.main(arguments, stdout=output, stderr=error)
                            self.assertEqual(code, 2)
                            raw = output.getvalue()
                            self.assertEqual(calls, ["prepare", "run"] if reader_accepts else [])
                            if reader_accepts:
                                self.assertEqual((observed[0]["capture"], observed[0]["capture_code"],
                                                  observed[0]["started"], observed[0]["counts"],
                                                  observed[0]["face_map"], observed[0]["publication"]),
                                                 ("kUnsupportedProfile", "kNotRegular", False, None, None, None))
                        else:
                            process = self.invoke(*arguments, console=frontend == "console", cwd=self.root)
                            self.assertEqual(process.returncode, 2)
                            raw = process.stdout
                        if reader_accepts:
                            batch = self.checked(reports.load(raw))
                            self.assertEqual(batch["jobs"][0]["outcome"], "attempted")
                            value = batch["jobs"][0]["report"]
                            self.assertEqual((value["candidate"]["outcome"], value["publication"]["outcome"],
                                              value["snapshots"], value["maps"]),
                                             ("not-attempted", "not-attempted", [], []))
                            extension = self.fallback(value, False)
                            self.assertEqual(extension["capture"]["diagnostic"]["code"],
                                             {"domain": "OffFileDiagnosticCode", "member": "kNotRegular"})
                        else:
                            self.assertFalse(raw)
                        self.assertFalse((self.root / row["destination"]).exists())
                        self.assertEqual(source.read_bytes(), EMPTY)
            relative = str(source.relative_to(self.root))
            controls = [os.path.join(".", relative),
                        os.path.join(os.path.dirname(relative), ".", source.name),
                        os.path.dirname(relative) + os.sep + os.sep + source.name]
            for index, spelling in enumerate(controls):
                for frontend in ("api", "single", "batch", "module", "console"):
                    with self.subTest(ordinary=index, frontend=frontend):
                        row = job(f"ordinary-suffix-{index}-{frontend}", source=spelling, changed=False)
                        value, _ = self.path_attempt(row, frontend, workflow.OffExecution())
                        self.assertEqual((value["candidate"]["outcome"], value["publication"]["outcome"],
                                          reports.exit_code(value)), ("unchanged", "published", 0))
                        self.maps(value, False, True)
                        check_output(self, (self.root / row["destination"] / ENTRY).read_bytes(), False, True)
                        self.assertEqual(source.read_bytes(), EMPTY)
        finally:
            os.chdir(previous)
    def test_empty_batch_final_token_boundary_and_sixteen_real_missing_attempts(self):
        result = off.run_off_jobs(request(), base=self.root)
        self.assertEqual((self.parsed(result)["jobs"], result.exit_code), ([], 0))
        token = workflow.OffCancellation()
        token.cancel()
        canceled = off.run_off_jobs(request(), base=self.root, cancellation=token)
        self.assertEqual((self.parsed(canceled)["execution"]["outcome"], canceled.exit_code), ("cancelled", 130))
        rows = [job(f"missing{i}", source=f"missing/{i}.off") for i in range(16)]
        value = self.parsed(off.run_off_jobs(request(*rows), base=self.root))
        self.assertEqual(len(value["jobs"]), 16)
        self.assertTrue(all(row["outcome"] == "attempted" for row in value["jobs"]))
        self.assertEqual(value["exit_code"], 2)
    def test_empty_module_console_api_batch_native_receipts(self):
        source = fixture(self.root / "sources/empty", EMPTY)
        for console in (False, True):
            row = job("empty-" + str(console), source=str(source.relative_to(self.root)), changed=False)
            process = self.invoke(*self.single(row), console=console)
            self.assertEqual(process.returncode, 0, process.stderr)
            raw = (self.root / row["destination"] / RECEIPT).read_bytes()
            self.assertEqual(process.stdout, raw + b"\n")
            value = self.checked(reports.load(raw))
            self.maps(value, False, True)
            check_output(self, (self.root / row["destination"] / ENTRY).read_bytes(), False, True)
        row = job("empty-api", source=str(source.relative_to(self.root)), changed=False)
        observed, original = [], workflow._run_off_request
        def observe(prepared):
            result = original(prepared)
            observed.append(result.report_json)
            return result
        with patch.object(workflow, "_run_off_request", new=observe):
            result = off._run_off_single(row, base=self.root)
        self.assertEqual(result.report_json, observed[0])
        self.parsed(result)
        batch_row = row | {"id": "empty-batch", "destination": "outputs/empty-batch"}
        batch = self.parsed(off.run_off_jobs(request(batch_row), base=self.root))
        self.assertEqual(batch["jobs"][0]["report"]["candidate"]["outcome"], "unchanged")
    def test_actual_capture_failures_and_precanceled_fallback_are_truthful(self):
        for name, raw, status in (("missing", None, "kIoFailure"), ("malformed", b"OFF\n1 x 0\n", "kOffFailure"),
                                  ("variant", b"COFF\n0 0 0\n", "kOffFailure")):
            if raw is not None:
                fixture(self.root / "sources" / name, raw)
            row = job(name, source=f"sources/{name}/source.off", changed=False)
            result = off._run_off_single(row, base=self.root)
            value = self.parsed(result)
            extension = self.fallback(value, False)
            self.assertEqual(extension["capture"]["status"], {"domain": "OffFileStatus", "member": status})
            self.assertEqual((value["snapshots"], value["maps"], value["candidate"]["outcome"], result.exit_code), ([], [], "not-attempted", 2))
            self.assertFalse((self.root / row["destination"]).exists())
        token = workflow.OffCancellation()
        token.cancel()
        row = job("canceled")
        result = off._run_off_single(row, base=self.root, cancellation=token)
        self.assertEqual((self.parsed(result)["execution"]["outcome"], result.exit_code), ("cancelled", 130))
    def test_alias_comparison_isolation_and_logical_base_native_refusal(self):
        source = fixture(self.root / "actual", EMPTY)
        alias = directory_alias(self, self.root / "alias", source.parent)
        # Canonical copies refuse actual source-entry/output overlap before any
        # preparation; the logical source spelling is not rewritten for capture.
        self.refuse(request(job("overlap", source=str(source.relative_to(self.root)),
                                destination="alias/source.off")))
        row = job("logical", source=ENTRY, destination="../outputs/logical", changed=False)
        result = off._run_off_single(row, base=alias)
        value = self.parsed(result)
        extension = self.fallback(value, False)
        self.assertEqual(extension["capture"]["status"],
                         {"domain": "OffFileStatus", "member": "kUnsupportedProfile"})
        self.assertEqual(extension["capture"]["diagnostic"]["code"],
                         {"domain": "OffFileDiagnosticCode", "member": "kPathUnsupported"})
        self.assertEqual(result.exit_code, 2)
        self.assertFalse((self.root / "outputs/logical").exists())
        row = row | {"id": "resolved", "destination": "../outputs/resolved"}
        self.assertEqual(off._run_off_single(row, base=source.parent).exit_code, 0)
        self.assertEqual(source.read_bytes(), EMPTY)
    def test_help_version_and_missing_capture_do_not_import_interchange_python(self):
        child = """import builtins,sys
original=builtins.__import__
def guarded(name,*args,**kwargs):
 if name.split('.')[0]=='meshvale_interchange': raise AssertionError('optional Interchange Python imported')
 return original(name,*args,**kwargs)
builtins.__import__=guarded
from meshvale_repair.commands import main
raise SystemExit(main(sys.argv[1:]))
"""
        for arguments in (("--help",), ("--version",), ("off", "missing/source.off", "outputs/missing", "--text", "canonical-ascii", "--edge-count", "unknown-zero", "--unchanged")):
            process = subprocess.run([sys.executable, "-I", "-c", child, *arguments], cwd=self.root, capture_output=True, timeout=60)
            self.assertEqual(process.returncode, 2 if arguments[0] == "off" else 0, process.stderr)
            if arguments[0] == "off":
                self.checked(reports.load(process.stdout))
    def test_invalid_parser_shape_never_calls_native_and_diagnostics_are_bounded(self):
        prefix = ["off", str(self.source), str(self.root / "outputs/invalid"), "--text", "canonical-ascii", "--edge-count", "unknown-zero"]
        for tail in ([], ["--target", "0:1", "--unchanged"], ["--target", "-1:1"], ["--target", "0:18446744073709551616"],
                     ["--unchanged", "--row-local", "vertex:unsafe"], ["--unchanged", "--primitive-ordinal", "0"]):
            out, error = io.StringIO(), io.StringIO()
            with patch.object(workflow, "_run_off_request") as run:
                code = commands.main(prefix + tail, stdout=out, stderr=error)
                run.assert_not_called()
            self.assertEqual((code, out.getvalue()), (2, ""))
            self.assertNotIn(str(self.root), error.getvalue())
    def test_real_sigint_restores_handler_and_canceled_delivery_exit_dominates(self):
        source = fixture(self.root / "sources/empty", EMPTY)
        row = job("sigint", source=str(source.relative_to(self.root)), changed=False)
        previous, original = signal.getsignal(signal.SIGINT), workflow._run_off_request
        def interrupt(prepared):
            signal.raise_signal(signal.SIGINT)
            return original(prepared)
        out, error = io.StringIO(), io.StringIO()
        with patch.object(workflow, "_run_off_request", new=interrupt):
            code = commands.main(self.single(row), stdout=out, stderr=error)
        self.assertEqual((code, self.checked(reports.load(out.getvalue()))["execution"]["outcome"]), (130, "cancelled"))
        self.assertEqual(signal.getsignal(signal.SIGINT), previous)
        with patch.object(workflow, "_run_off_request", new=interrupt):
            code = commands.main(self.single(row | {"id": "broken", "destination": "outputs/broken"}), stdout=BrokenOutput("write"), stderr=io.StringIO())
        self.assertEqual(code, 130)
        self.assertEqual(signal.getsignal(signal.SIGINT), previous)
    def test_late_stdout_failure_or_interrupt_never_relabels_true_empty_commit(self):
        source = fixture(self.root / "sources/empty", EMPTY)
        for phase in ("write", "short", "none", "flush", "late-interrupt"):
            row = job("delivery-" + phase, source=str(source.relative_to(self.root)), changed=False)
            callback = (lambda: signal.raise_signal(signal.SIGINT)) if phase == "late-interrupt" else None
            out = BrokenOutput("ok" if callback else phase, callback)
            error, previous = io.StringIO(), signal.getsignal(signal.SIGINT)
            code = commands.main(self.single(row), stdout=out, stderr=error)
            self.assertEqual(code, 0 if callback else 2)
            self.assertEqual(signal.getsignal(signal.SIGINT), previous)
            raw = (self.root / row["destination"] / RECEIPT).read_bytes()
            self.assertEqual(reports.exit_code(self.checked(reports.load(raw))), 0)
            if callback:
                self.assertEqual(out.payload.encode("utf-8"), raw + b"\n")
            self.assertNotIn(str(self.root), error.getvalue())
            self.assertNotIn("private-output-marker", error.getvalue())
    def test_injected_receipt_getter_fault_after_actual_publication_never_returns_aggregate(self):
        # Only wire access is injected; every native result and committed bundle
        # is real, and every actual typed publication/map remains untouched.
        source = fixture(self.root / "sources/empty", EMPTY)
        original_getter = off._native_receipt
        for fault in ("none", "malformed", "oversize", "memory"):
            row = job("wire-" + fault, source=str(source.relative_to(self.root)), changed=False)
            seen = []
            def faulted(actual):
                seen.append((actual.publication.outcome.name, actual.report_json))
                if fault == "memory":
                    raise MemoryError("injected receipt observation failure")
                if fault == "none":
                    return None
                if fault == "malformed":
                    return b"{not json"
                raw = original_getter(actual)
                return raw + b" " * (REPORT_BYTES + 1 - len(raw))
            with patch.object(off, "_native_receipt", new=faulted):
                with self.assertRaises(off.OffCommandDeliveryError) as caught:
                    off.run_off_jobs(request(row, job("never-" + fault, source="missing/source.off")), base=self.root)
            self.assertEqual(caught.exception.exit_code, 2)
            self.assertEqual(len(seen), 1)
            self.assertEqual(seen[0][0], "kPublished")
            self.assertEqual((self.root / row["destination"] / RECEIPT).read_bytes(), seen[0][1])
            self.assertEqual(reports.exit_code(reports.load(seen[0][1])), 0)
            self.assertFalse((self.root / ("outputs/never-" + fault)).exists())

    def test_injected_wire_failure_after_real_commit_and_cancel_keeps_130(self):
        source = fixture(self.root / "sources/empty", EMPTY)
        row = job("committed-wire-cancel", source=str(source.relative_to(self.root)), changed=False)
        token = workflow.OffCancellation()
        seen = []
        def missing(actual):
            seen.append((actual.publication.outcome.name, actual.report_json))
            token.cancel()
            return None
        with patch.object(off, "_native_receipt", new=missing):
            with self.assertRaises(off.OffCommandDeliveryError) as caught:
                off.run_off_jobs(request(row), base=self.root, cancellation=token)
        self.assertEqual(caught.exception.exit_code, 130)
        self.assertEqual(seen[0][0], "kPublished")
        self.assertEqual((self.root / row["destination"] / RECEIPT).read_bytes(), seen[0][1])
        self.assertEqual(reports.exit_code(reports.load(seen[0][1])), 0)


class QualifiedTests(Base):
    def test_module_console_single_and_batch_exact_native_slices_and_literal_output(self):
        for changed in (False, True):
            row = job("changed" if changed else "unchanged", changed=changed)
            process = self.invoke(*self.single(row), console=changed)
            self.assertEqual(process.returncode, 0, process.stderr)
            files = physical(self.root / row["destination"])
            self.assertEqual(set(files), {ENTRY, RECEIPT})
            self.assertEqual(process.stdout, files[RECEIPT] + b"\n")
            value = self.checked(reports.load(files[RECEIPT]))
            self.maps(value, changed)
            check_output(self, files[ENTRY], changed)
            self.assertEqual(value["input"]["sha256"], digest(SOURCE))
            extension = value["extensions"][EXTENSION]
            self.assertEqual((extension["source_faces"], extension["source_corners"]),
                             (list(SOURCE_FACES), list(SOURCE_CORNERS)) if changed else (list(range(3)), list(range(11))))
        rows = [job("batch-a"), job("batch-b", changed=False)]
        session, captured, charges = workflow.OffExecution(), [], []
        original = workflow._run_off_request
        def observe(prepared):
            charges.append(session.active_tracked_payload)
            native = original(prepared)
            captured.append(native.report_json)
            return native
        with patch.object(workflow, "_run_off_request", new=observe):
            result = off.run_off_jobs(request(*rows), base=self.root, execution=session)
        value = self.parsed(result)
        self.assertEqual(charges, [0, 0])
        self.assertEqual((session.active_tracked_payload, session.active_workers), (0, 0))
        for index, raw in enumerate(captured):
            self.assertIn(raw, result.report_json)
            self.assertEqual(raw, (self.root / rows[index]["destination"] / RECEIPT).read_bytes())
            self.assertEqual(value["jobs"][index]["report"], reports.load(raw))
    def test_same_cwd_source_entry_allowed_and_unicode_request_file_continue_failfast(self):
        root_source = fixture(self.root / "shared", entry="\u6e90-\U0001f600.off")
        (root_source.parent / "out").mkdir()
        row = job("samecwd", source=root_source.name, destination="out/new")
        self.assertEqual(off._run_off_single(row, base=root_source.parent).exit_code, 0)
        rows = [job("first"), job("missing", source="missing/source.off"), job("last", changed=False)]
        path = self.root / "jobs-\U0001f600.json"
        path.write_bytes(canonical(request(*rows)))
        elsewhere = self.root / "elsewhere"
        elsewhere.mkdir()
        process = self.invoke("off-batch", str(path), cwd=elsewhere)
        self.assertEqual(process.returncode, 2, process.stderr)
        batch = self.checked(reports.load(process.stdout))
        self.assertEqual([row["outcome"] for row in batch["jobs"]], ["attempted"] * 3)
        fast_rows = [row | {"destination": row["destination"] + "-fast"} for row in rows]
        fast = self.parsed(off.run_off_jobs(request(*fast_rows), base=self.root, fail_fast=True))
        self.assertEqual([row["outcome"] for row in fast["jobs"]], ["attempted", "attempted", "not-attempted"])
        self.assertTrue(fast["jobs"][2]["reason"])
    def test_actual_rejections_no_implicit_discovery_and_schema_corruption_controls(self):
        for name, wire in (("reverse", REVERSED), ("distinct", DISTINCT_IDS)):
            fixture(self.root / "sources" / name, wire)
            result = off._run_off_single(job(name, source=f"sources/{name}/source.off"), base=self.root)
            value = self.parsed(result)
            self.assertEqual((value["candidate"]["outcome"], result.exit_code), ("rejected", 1))
            self.assertEqual(value["maps"], [])
        result = off._run_off_single(job("golden"), base=self.root)
        value = self.parsed(result)
        for fault in ("maps", "counts", "profile", "publication"):
            bad = deepcopy(value)
            if fault == "maps":
                next(row for row in bad["maps"] if row["id"] == "repair-faces")["entries"][0] = 99
            elif fault == "counts":
                next(row for row in bad["snapshots"] if row["role"] == "candidate")["meshes"][0]["corner_count"] = 1
            elif fault == "profile":
                bad["profile"]["outcome"] = "failed"
            else:
                bad["publication"]["snapshot"] = None
            with self.assertRaises(reports.ReportError):
                reports.validate(bad)
    def test_injected_missing_unpublished_receipt_projects_genuine_completed_maps(self):
        destination = self.root / "outputs/occupied"
        destination.mkdir()
        (destination / "marker").write_bytes(b"preserve")
        row = job("occupied", destination="outputs/occupied")
        seen, actual = [], workflow._run_off_request
        def observe(prepared):
            native = actual(prepared)
            seen.append((native.status.name, native.face_map, native.source_faces, native.publication.outcome.name))
            return native
        with patch.object(workflow, "_run_off_request", new=observe), patch.object(off, "_native_receipt", return_value=None):
            result = off._run_off_single(row, base=self.root)
        value = self.parsed(result)
        self.fallback(value, True)
        self.assertEqual(seen, [("kDelegatedFailure", FACE_MAP, SOURCE_FACES, "kFailed")])
        self.maps(value, True)
        self.assertNotIn("output", {row["role"] for row in value["snapshots"]})
        self.assertEqual(physical(destination), {"marker": b"preserve"})
    def test_actual_commit_then_cancel_preserves_receipt_and_final_batch_cancellation(self):
        token, actual = workflow.OffCancellation(), workflow._run_off_request
        def committed_then_cancel(prepared):
            result = actual(prepared)
            token.cancel()
            return result
        with patch.object(workflow, "_run_off_request", new=committed_then_cancel):
            result = off.run_off_jobs(request(job("committed"), job("later")), base=self.root, cancellation=token)
        value = self.parsed(result)
        self.assertEqual(result.exit_code, 130)
        self.assertEqual(value["jobs"][0]["report"]["publication"]["outcome"], "published")
        self.assertEqual(value["jobs"][1]["outcome"], "not-attempted")
        token = workflow.OffCancellation()
        with patch.object(workflow, "_run_off_request", new=committed_then_cancel):
            result = off._run_off_single(job("single"), base=self.root, cancellation=token)
        self.assertEqual((result.exit_code, self.parsed(result)["publication"]["outcome"]), (0, "published"))


class WindowsAnchoredTests(Base):
    """Ordinary Windows anchored spellings remain real native attempts."""
    def anchored(self, absolute, drive_relative):
        return (self.root.drive + str(absolute.relative_to(self.root)) if drive_relative
                else str(absolute)[len(self.root.drive):])
    def ordinary(self, frontend, label):
        row = job(label + "-ordinary-" + frontend,
                  source=str(self.source.relative_to(self.root)))
        value, observed = self.path_attempt(row, frontend, workflow.OffExecution())
        self.assertEqual((value["candidate"]["outcome"], value["publication"]["outcome"],
                          reports.exit_code(value)), ("accepted", "published", 0))
        self.maps(value, True)
        check_output(self, (self.root / row["destination"] / ENTRY).read_bytes(), True)
        if observed:
            self.assertEqual((observed[0]["capture"], observed[0]["status"]), ("kSuccess", "kSuccess"))
    def test_drive_and_root_relative_sources_remain_native_refusals(self):
        previous = Path.cwd()
        try:
            os.chdir(self.root)
            for drive_relative in (True, False):
                form = "drive" if drive_relative else "root"
                for frontend in ("api", "single", "batch", "module", "console"):
                    with self.subTest(form=form, frontend=frontend):
                        row = job("source-" + form + "-" + frontend,
                                  source=self.anchored(self.source, drive_relative))
                        value, observed = self.path_attempt(row, frontend, workflow.OffExecution())
                        if observed:
                            self.assertEqual((observed[0]["capture"], observed[0]["capture_code"],
                                              observed[0]["started"]),
                                             ("kUnsupportedProfile", "kPathUnsupported", False))
                        if value is not None:
                            self.assertEqual((value["execution"]["outcome"], value["publication"]["outcome"],
                                              reports.exit_code(value)), ("failed", "not-attempted", 2))
                            extension = self.fallback(value, False)
                            self.assertEqual(extension["capture"]["diagnostic"]["code"],
                                             {"domain": "OffFileDiagnosticCode", "member": "kPathUnsupported"})
                            self.assertEqual((value["candidate"]["outcome"], reports.exit_code(value)),
                                             ("not-attempted", 2))
                        self.assertFalse((self.root / row["destination"]).exists())
            for form, base in (("drive", self.root.drive),
                               ("root", str(self.root)[len(self.root.drive):])):
                for frontend in ("single", "batch"):
                    with self.subTest(anchored_base=form, frontend=frontend):
                        row = job("base-" + form + "-" + frontend,
                                  source=str(self.source.relative_to(self.root)))
                        value, observed = self.path_attempt(row, frontend, workflow.OffExecution(), base=base)
                        self.assertEqual((observed[0]["capture"], observed[0]["capture_code"],
                                          observed[0]["started"]),
                                         ("kUnsupportedProfile", "kPathUnsupported", False))
                        self.assertEqual((value["publication"]["outcome"], reports.exit_code(value)),
                                         ("not-attempted", 2))
                        self.fallback(value, False)
                        self.assertFalse((self.root / row["destination"]).exists())
            for drive_relative in (True, False):
                form = "drive" if drive_relative else "root"
                for frontend in ("module", "console"):
                    with self.subTest(request_anchor=form, frontend=frontend):
                        row = job("request-anchor-" + form + "-" + frontend,
                                  source=str(self.source.relative_to(self.root)))
                        path = self.root / (row["id"] + ".json")
                        path.write_bytes(canonical(request(row)))
                        spelling = self.root.drive + path.name if drive_relative else self.anchored(path, False)
                        process = self.invoke("off-batch", spelling, console=frontend == "console", cwd=self.root)
                        value = self.checked(reports.load(process.stdout))
                        self.assertEqual((process.returncode, value["jobs"][0]["outcome"]), (2, "attempted"))
                        report = value["jobs"][0]["report"]
                        extension = self.fallback(report, False)
                        self.assertEqual(extension["capture"]["diagnostic"]["code"],
                                         {"domain": "OffFileDiagnosticCode", "member": "kPathUnsupported"})
                        self.assertFalse((self.root / row["destination"]).exists())
                        self.assertEqual(self.source.read_bytes(), SOURCE)
            for frontend in ("module", "console"):
                with self.subTest(ordinary_request=frontend):
                    row = job("ordinary-request-" + frontend,
                              source=str(self.source.relative_to(self.root)))
                    path = self.root / (row["id"] + ".json")
                    path.write_bytes(canonical(request(row)))
                    process = self.invoke("off-batch", os.path.join(".", path.name),
                                          console=frontend == "console", cwd=self.root)
                    value = self.checked(reports.load(process.stdout))
                    report = value["jobs"][0]["report"]
                    self.assertEqual((process.returncode, report["publication"]["outcome"]), (0, "published"))
                    self.maps(report, True)
                    self.assertEqual(self.source.read_bytes(), SOURCE)
            for frontend in ("api", "single", "batch", "module", "console"):
                with self.subTest(ordinary=frontend):
                    self.ordinary(frontend, "source")
        finally:
            os.chdir(previous)
    def test_drive_and_root_relative_destinations_retain_repair_and_refuse_publication(self):
        previous = Path.cwd()
        try:
            os.chdir(self.root)
            for drive_relative in (True, False):
                form = "drive" if drive_relative else "root"
                for frontend in ("api", "single", "batch", "module", "console"):
                    with self.subTest(form=form, frontend=frontend):
                        absolute = self.root / "outputs" / ("destination-" + form + "-" + frontend)
                        row = job("destination-" + form + "-" + frontend,
                                  source=str(self.source), destination=self.anchored(absolute, drive_relative))
                        value, observed = self.path_attempt(row, frontend, workflow.OffExecution())
                        if observed:
                            fact = observed[0]
                            self.assertEqual((fact["capture"], fact["started"], fact["publication"],
                                              fact["publication_code"]),
                                             ("kSuccess", True, "kFailed", "kPathUnsupported"))
                            self.assertEqual((fact["counts"], fact["face_map"], fact["corner_map"],
                                              fact["source_faces"], fact["source_corners"]),
                                             ((8, 2, 7), FACE_MAP, CORNER_MAP, SOURCE_FACES, SOURCE_CORNERS))
                        self.assertEqual((value["candidate"]["outcome"], value["publication"]["outcome"],
                                          reports.exit_code(value)), ("accepted", "failed", 2))
                        self.maps(value, True)
                        extension = value["extensions"][EXTENSION]
                        self.assertEqual(extension["publisher"]["diagnostic"]["code"],
                                         workflow._repair.OffFileDiagnosticCode.kPathUnsupported.value)
                        self.assertEqual((extension["source_faces"], extension["source_corners"]),
                                         (list(SOURCE_FACES), list(SOURCE_CORNERS)))
                        self.assertEqual(value["input"]["sha256"], digest(SOURCE))
                        self.assertNotIn("output", {row["role"] for row in value["snapshots"]})
                        self.assertFalse(absolute.exists())
            for frontend in ("api", "single", "batch", "module", "console"):
                with self.subTest(ordinary=frontend):
                    self.ordinary(frontend, "destination")
        finally:
            os.chdir(previous)


class UnsupportedTests(Base):
    def test_genuine_nonempty_capture_profile_refusal_api_module_console(self):
        for console in (False, True):
            row = job("unsupported-" + str(console))
            result = off._run_off_single(row, base=self.root)
            value = self.parsed(result)
            extension = self.fallback(value, False)
            self.assertEqual(extension["capture"]["status"], {"domain": "OffFileStatus", "member": "kOffFailure"})
            self.assertEqual(value["candidate"]["outcome"], "not-attempted")
            self.assertEqual(result.exit_code, 2)
            process = self.invoke(*self.single(row), console=console)
            self.assertEqual(process.returncode, 2, process.stderr)
            self.checked(reports.load(process.stdout))


def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite()
    for group in (AdmissionTests, CommonTests, QualifiedTests if PROFILE == "qualified" else UnsupportedTests):
        suite.addTests(loader.loadTestsFromTestCase(group))
    if os.name == "nt" and PROFILE == "qualified":
        suite.addTests(loader.loadTestsFromTestCase(WindowsAnchoredTests))
    return suite


if __name__ == "__main__":
    unittest.main(verbosity=2)
