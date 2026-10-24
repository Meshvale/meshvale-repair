# SPDX-License-Identifier: Apache-2.0
"""Installed glTF command tests; qualified/unsupported profiles execute explicitly."""
from copy import deepcopy
import gc
import io
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import jsonschema
import meshvale_reports as reports
from meshvale_repair import commands, gltf_commands as gltf, gltf_workflow as workflow
from gltf_command_fixture import (BINARY, COMMAND_EXTENSION, ENTRY, FILE_EXTENSION,
    NON_JSON, RECEIPT, REQUEST_SCHEMA, actual_files, canonical, duplicate_triangle_fixture, fixture, job,
    parse_glb, physical_source_digest, request)

PROFILE = os.environ.get("MESHVALE_GLTF_PYTHON_PROFILE", "qualified")
if PROFILE not in {"qualified", "unsupported"}:
    raise ValueError("MESHVALE_GLTF_PYTHON_PROFILE must be qualified or unsupported")
REQUEST_BYTES = 8 * 1024**2
REPORT_BYTES = 16 * 1024**2


class BrokenOutput:
    def __init__(self, phase, callback=None):
        self.phase, self.callback, self.payload, self.writes, self.flushes = phase, callback, "", 0, 0
    def write(self, value):
        self.writes += 1
        if self.callback:
            self.callback()
        if self.phase == "write":
            raise BrokenPipeError("private-delivery-marker")
        self.payload += value
        if self.phase == "short":
            return max(0, len(value) - 1)
        if self.phase == "none":
            return None
        return len(value)
    def flush(self):
        self.flushes += 1
        if self.phase == "flush":
            raise OSError("private-delivery-marker")


class Base(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="meshvale-gltf-cli-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve()
        (self.root / "outputs").mkdir()
        self.members = fixture(self.root / "sources/a")
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
        other = result.report
        other["tool"]["name"] = "changed detached observation"
        self.assertEqual(result.report, value)
        with self.assertRaises((AttributeError, TypeError)):
            result.report_json = b"mutated"
        return value
    def invoke(self, *arguments, console=False, cwd=None):
        binary = Path(sys.executable).parent / ("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        executable = [str(binary)] if console else [sys.executable, "-I", "-m", "meshvale_repair"]
        return subprocess.run(executable + list(arguments), cwd=cwd or self.root,
                              capture_output=True, timeout=90)
    def single_arguments(self, row):
        args = ["gltf", str(self.root / row["input"]), str(self.root / row["destination"]),
                "--input-id", row["id"], "--primitive-ordinal", str(row["primitive_ordinal"]),
                "--triangles", row["triangles"], "--values", row["values"]]
        if row["targets"]:
            for keep, remove in row["targets"]:
                args += ["--target", f"{keep}:{remove}"]
        else:
            args += ["--unchanged"]
        return args
    def refuse(self, value, **kwargs):
        with patch.object(workflow, "_run_gltf_request") as run:
            with self.assertRaises(commands.InvocationError):
                gltf.run_gltf_jobs(value, base=self.root, **kwargs)
            run.assert_not_called()
        self.assertEqual(actual_files(self.root / "sources/a"), self.members)
        self.assertEqual(list((self.root / "outputs").iterdir()), [])
    def counts_and_maps(self, report, changed, triangles=2):
        snapshots = {row["role"]: row for row in report["snapshots"]}
        expected = {"input": (3, triangles, 3 * triangles),
                    "candidate": (3, triangles - int(changed), 3 * (triangles - int(changed)))}
        for role, counts in expected.items():
            meshes = snapshots[role]["meshes"]
            self.assertEqual(len(meshes), 1)
            self.assertEqual(tuple(meshes[0][name] for name in ("vertex_count", "face_count", "corner_count")), counts)
        maps = {row["domain"]: row for row in report["maps"]
                if row["from"]["snapshot"] == snapshots["input"]["id"] and
                   row["to"]["snapshot"] == snapshots["candidate"]["id"]}
        self.assertEqual(set(maps), {"vertex", "face", "corner"})
        self.assertEqual((maps["vertex"]["kind"], maps["vertex"]["entries"]), ("identity", None))
        if changed:
            faces = [0 if i < 2 else i - 1 for i in range(triangles)]
            corners = [i if i < 3 else i % 3 if i < 6 else i - 3 for i in range(3 * triangles)]
            self.assertEqual((maps["face"]["kind"], maps["face"]["entries"]), ("explicit", faces))
            self.assertEqual((maps["corner"]["kind"], maps["corner"]["entries"]), ("explicit", corners))
        else:
            for domain in ("face", "corner"):
                self.assertEqual((maps[domain]["kind"], maps[domain]["entries"]), ("identity", None))
    def fallback(self, report, *, workflow_started):
        extension = report["extensions"][COMMAND_EXTENSION]
        self.assertEqual(extension["version"], "1")
        self.assertEqual(extension["origin"], "command-workflow-fallback" if workflow_started else "command-capture-fallback")
        self.assertIs(extension["native_report_available"], False)
        self.assertIs(extension["workflow_started"], workflow_started)
        self.assertEqual(report["profile"]["required_checks"], ["native-receipt-validation"])
        self.assertEqual(report["profile"]["outcome"], "incomplete")
        coverage, = report["coverage"]
        self.assertEqual((coverage["id"], coverage["status"], coverage["finding"]),
                         ("native-receipt-validation", "skipped", None))
        self.assertTrue(coverage["reason"])
        self.assertEqual(report["preservation"], [])
        self.assertEqual(report["publication"]["artifacts"], [])
        self.assertIsNone(report["input"]["sha256"])
        return extension


class AdmissionTests(Base):
    def test_empty_immutable_result_and_actual_final_token_boundary(self):
        result = gltf.run_gltf_jobs(request(), base=self.root)
        report = self.parsed(result)
        self.assertEqual((report["schema"], report["jobs"], result.exit_code), ("meshvale.batch/1", [], 0))
        token = workflow.GltfCancellation()
        token.cancel()
        canceled = gltf.run_gltf_jobs(request(), base=self.root, cancellation=token)
        self.assertEqual((self.parsed(canceled)["execution"]["outcome"], canceled.exit_code), ("cancelled", 130))
    def test_whole_invocation_invalid_last_row_never_captures(self):
        valid = job("first")
        malformed = []
        for field, value in (("primitive_ordinal", True), ("primitive_ordinal", -1),
                             ("primitive_ordinal", 1 << 64), ("triangles", "automatic"),
                             ("values", "normalized-only"), ("targets", [[False, 1]]),
                             ("targets", [[0, 1 << 64]]), ("targets", [[0]]),
                             ("targets", ((0, 1),)), ("id", "bad space"),
                             ("id", "\ud800"), ("input", b"asset"), ("destination", ""),
                             ("row_local_attributes", [["edge", "name"]]),
                             ("row_local_attributes", [["vertex", "a" * 257]]),
                             ("report_path", "../receipt.json")):
            bad = job("last")
            bad[field] = value
            malformed.append(bad)
        malformed += [dict(job("first")), job("last") | {"unknown": 1}]
        missing = job("last"); del missing["primitive_ordinal"]; malformed.append(missing)
        for bad in malformed:
            with self.subTest(fields=tuple(bad)):
                self.refuse(request(valid, bad))
    def test_closed_plain_container_shape_and_invocation_types(self):
        class DictSubclass(dict):
            pass
        class ListSubclass(list):
            pass
        for value in ({}, request() | {"extra": 1}, {"schema": "other", "jobs": []},
                      {"schema": REQUEST_SCHEMA, "jobs": ()}, DictSubclass(request()),
                      {"schema": REQUEST_SCHEMA, "jobs": ListSubclass()},
                      request(DictSubclass(job("a")))):
            with self.subTest(shape=type(value)):
                self.refuse(value)
        for options in ({"fail_fast": 1}, {"fail_fast": None}, {"execution": object()},
                        {"cancellation": object()}, {"base": object()}):
            if "base" in options:
                with patch.object(workflow, "_run_gltf_request") as run:
                    with self.assertRaises(commands.InvocationError):
                        gltf.run_gltf_jobs(request(job("a")), **options)
                    run.assert_not_called()
            else:
                self.refuse(request(job("a")), **options)
    def test_large_declarations_text_and_poison_subclasses_refuse_before_preparation(self):
        class PoisonList(list):
            def __iter__(self):
                raise AssertionError("Non-plain list must not be iterated")
            def __len__(self):
                raise AssertionError("Non-plain list must not be sized")
        class PoisonText(str):
            def encode(self, *args, **kwargs):
                raise AssertionError("Non-plain text must not be encoded")
            def __iter__(self):
                raise AssertionError("Non-plain text must not be iterated")
            def __len__(self):
                raise AssertionError("Non-plain text must not be sized")
        variants = [
            {"row_local_attributes": [["vertex", "x"]] * 129},
            {"row_local_attributes": [["vertex", "x"]] * 1_000_000},
            {"row_local_attributes": [["vertex", "a" * 128], ["face", "b" * 129]]},
            {"row_local_attributes": [["vertex", "\u00e9" * 64], ["face", "b" * 129]]},
            {"row_local_attributes": [["vertex", "a" * 257]]},
            {"row_local_attributes": [["vertex", "\u00e9" * 129]]},
            {"row_local_attributes": [["vertex", "a" * 1_000_000]]},
            {"row_local_attributes": PoisonList()},
            {"row_local_attributes": [["vertex", PoisonText("x")]]},
            {"targets": PoisonList()},
            {"id": "a" * 129}, {"input": "x" * 4096},
            {"destination": "x" * 4096}, {"resource_root": "x" * 4096},
            {"report_path": "a" * 1025}, {"input": "x" * 1_000_000},
            {"id": PoisonText("name")}, {"input": PoisonText("source")},
            {"report_path": PoisonText("receipt.json")},
        ]
        for options in variants:
            value = request(job("first"), job("last") | options)
            with self.subTest(fields=tuple(options)):
                with patch.object(workflow, "_prepare_gltf_request", side_effect=AssertionError("Preparation reached before closed admission")) as prepare, patch.object(workflow, "_run_gltf_request") as run:
                    with self.assertRaises(commands.InvocationError):
                        gltf.run_gltf_jobs(value, base=self.root)
                    prepare.assert_not_called()
                    run.assert_not_called()
        for base in ("x" * 4096, "x" * 1_000_000):
            with patch.object(workflow, "_prepare_gltf_request", side_effect=AssertionError("Preparation reached")) as prepare, patch.object(workflow, "_run_gltf_request") as run:
                with self.assertRaises(commands.InvocationError):
                    gltf.run_gltf_jobs(request(job("a")), base=base)
                prepare.assert_not_called()
                run.assert_not_called()
        self.assertEqual(actual_files(self.root / "sources/a"), self.members)
    def test_missing_report_dependencies_fail_before_prepare_or_capture(self):
        script = r"""import builtins,sys
original = builtins.__import__
blocked = sys.argv[1]
def guarded(name,*args,**kwargs):
 if name == blocked or name.startswith(blocked + '.'):
  raise ImportError('Unavailable report dependency')
 return original(name,*args,**kwargs)
builtins.__import__ = guarded
from meshvale_repair import commands,gltf_commands,gltf_workflow
from unittest.mock import patch
from contextlib import redirect_stdout
from io import StringIO
with redirect_stdout(StringIO()):
 assert commands.main(['--help']) == 0
 assert commands.main(['--version']) == 0
value = {'schema':'meshvale.repair.gltf-jobs/1','jobs':[{'id':'source','input':'sources/missing.gltf','destination':'outputs/new','targets':[],'primitive_ordinal':0,'triangles':'require-triangles','values':'raw'}]}
with patch.object(gltf_workflow,'_prepare_gltf_request',side_effect=AssertionError('Preparation reached')) as prepare, patch.object(gltf_workflow,'_run_gltf_request') as run:
 try:
  gltf_commands.run_gltf_jobs(value)
 except ImportError:
  pass
 else:
  raise AssertionError('Missing report dependency did not fail before capture')
 prepare.assert_not_called()
 run.assert_not_called()
print('Missing report dependency refused before preparation/capture')
"""
        for dependency in ("meshvale_reports", "jsonschema"):
            process = subprocess.run([sys.executable, "-I", "-c", script, dependency], cwd=self.root, capture_output=True, timeout=60)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertIn(b"before preparation/capture", process.stdout)
    def test_global_job_target_and_canonical_object_byte_limits(self):
        self.refuse(request(*(job("j" + str(i)) for i in range(17))))
        many = job("many"); many["targets"] = [[0, 1]] * 1_000_001
        self.refuse(request(many))
        wide = job("wide"); wide["targets"] = [[(1 << 64) - 1, (1 << 64) - 1]] * 200_000
        self.assertLess(len(wide["targets"]), 1_000_000)
        self.assertGreater(len(canonical(request(wide))), REQUEST_BYTES)
        self.refuse(request(wide))
        # Exact canonical-byte admission without producing a huge attempted
        # report: an observing run seam raises only after full preparation.
        class AdmissionReached(Exception):
            pass
        boundary = job("boundary"); boundary["report_path"] = "r"
        pair = [(1 << 64) - 1, (1 << 64) - 1]
        overhead = len(canonical(request(boundary)))
        boundary["targets"] = [pair] * ((REQUEST_BYTES - overhead) // 44)
        while len(canonical(request(boundary))) > REQUEST_BYTES:
            boundary["targets"].pop()
        padding = REQUEST_BYTES - len(canonical(request(boundary)))
        boundary["report_path"] += "a" * padding
        self.assertEqual(len(canonical(request(boundary))), REQUEST_BYTES)
        with patch.object(workflow, "_run_gltf_request", side_effect=AdmissionReached) as run:
            with self.assertRaises(AdmissionReached):
                gltf.run_gltf_jobs(request(boundary), base=self.root)
            self.assertEqual(run.call_count, 1)
        boundary["report_path"] += "a"
        self.refuse(request(boundary))
    def test_reader_duplicate_nonfinite_invalid_utf8_and_inclusive_raw_bytes(self):
        path = self.root / "jobs.json"
        payloads = [b'{"schema":"meshvale.repair.gltf-jobs/1","schema":"other","jobs":[]}',
                    b'{"schema":"meshvale.repair.gltf-jobs/1","jobs":[{"id":"a","id":"b"}]}',
                    b'\xff', b'{"schema":"meshvale.repair.gltf-jobs/1","jobs":[],"x":NaN}',
                    b'{"schema":"meshvale.repair.gltf-jobs/1","jobs":[],"x":Infinity}',
                    b'{"schema":"meshvale.repair.gltf-jobs/1","jobs":[],"x":-Infinity}']
        for payload in payloads:
            path.write_bytes(payload)
            with self.assertRaises(commands.InvocationError):
                gltf._read_gltf_request(path)
        payload = canonical(request())
        path.write_bytes(payload + b" " * (REQUEST_BYTES - len(payload)))
        self.assertEqual(gltf._read_gltf_request(path), request())
        with path.open("ab") as stream:
            stream.write(b" ")
        with self.assertRaises(commands.InvocationError):
            gltf._read_gltf_request(path)
    def test_source_output_and_all_job_domain_isolation(self):
        cases = [request(job("a", destination="sources/a/output")),
                 request(job("a", destination="sources")),
                 request(job("a", destination="sources/a/scene.gltf")),
                 request(job("a", destination="outputs/same"), job("b", destination="outputs/same/nested")),
                 request(job("a", destination="outputs/same"), job("b", destination="outputs/same")),
                 request(job("a", destination="outputs/a"), job("b", source="outputs/a/future.gltf"))]
        for value in cases:
            self.refuse(value)
        outside = job("rooted"); outside["resource_root"] = "."
        self.refuse(request(outside))
    def test_logical_aliased_base_reaches_native_alias_refusal(self):
        real = self.root / "physical"
        original_members = fixture(real / "sources/a")
        (real / "outputs").mkdir()
        alias = self.root / "owned-alias"
        if os.name == "nt":
            powershell = shutil.which("powershell.exe")
            self.assertIsNotNone(powershell, "Windows junction fixture requires ordinary PowerShell")
            environment = os.environ.copy()
            environment["MESHVALE_TEST_ALIAS_PATH"] = str(alias)
            environment["MESHVALE_TEST_TARGET_PATH"] = str(real)
            script = "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:MESHVALE_TEST_ALIAS_PATH -Target $env:MESHVALE_TEST_TARGET_PATH | Out-Null"
            created = subprocess.run([powershell, "-NoProfile", "-NonInteractive", "-Command", script], env=environment, capture_output=True, timeout=30)
            self.assertEqual(created.returncode, 0, created.stderr)
        else:
            alias.symlink_to(real, target_is_directory=True)
        def remove_alias():
            if os.name == "nt":
                alias.rmdir()  # Remove only the owned junction, never recurse.
            else:
                alias.unlink()
        self.addCleanup(remove_alias)
        self.assertEqual(alias.resolve(), real.resolve())
        actual = workflow._run_gltf_request
        captures = []
        def observe(prepared):
            result = actual(prepared)
            captures.append((result.capture_status.name, result.capture_diagnostic.code.name))
            return result
        with patch.object(workflow, "_run_gltf_request", new=observe):
            refused = gltf._run_gltf_single(job("aliased"), base=alias)
            control = gltf._run_gltf_single(job("canonical"), base=real)
        self.assertEqual(captures[0], ("kUnsupportedProfile", "kPathUnsupported"))
        self.assertEqual(captures[1][0], "kSuccess")
        value = self.parsed(refused)
        self.fallback(value, workflow_started=False)
        self.assertEqual(refused.exit_code, 2)
        self.assertFalse((real / "outputs/aliased").exists())
        self.assertEqual(control.exit_code, 0 if PROFILE == "qualified" else 2)
        self.parsed(control)
        self.assertEqual(actual_files(real / "sources/a"), original_members)
    def test_capture_failure_real_typed_fallback_and_corruption_oracles(self):
        row = job("missing", source="sources/missing/scene.gltf")
        result = gltf._run_gltf_single(row, base=self.root)
        report = self.parsed(result)
        extension = self.fallback(report, workflow_started=False)
        self.assertEqual((report["candidate"]["outcome"], report["snapshots"], report["maps"], result.exit_code),
                         ("not-attempted", [], [], 2))
        self.assertEqual(extension["capture"]["status"], {"domain": "GltfFileStatus", "member": "kIoFailure"})
        self.assertIsNone(extension["workflow"])
        self.assertIsNone(extension["memory_outcomes"])
        self.assertIsNone(extension["publication"])
        self.assertFalse((self.root / row["destination"]).exists())
        changed = deepcopy(report); changed["candidate"]["outcome"] = "accepted"
        with self.assertRaises(reports.ReportError):
            reports.validate(changed)
        changed = deepcopy(report); changed["publication"]["outcome"] = "published"
        with self.assertRaises(reports.ReportError):
            reports.validate(changed)
    def test_malformed_json_glb_capture_fallback_preserves_real_diagnostics(self):
        for glb in (False, True):
            name = "malformed-" + str(glb)
            members = fixture(self.root / "sources" / name, glb)
            wire = bytearray(members[ENTRY])
            if glb:
                import struct
                struct.pack_into("<I", wire, 8, len(wire) + 4)
            else:
                wire = b'{"asset":'
            (self.root / "sources" / name / ENTRY).write_bytes(wire)
            result = gltf._run_gltf_single(job(name, source=f"sources/{name}/{ENTRY}"), base=self.root)
            report = self.parsed(result)
            extension = self.fallback(report, workflow_started=False)
            self.assertEqual(result.exit_code, 2)
            self.assertEqual(extension["capture"]["status"], {"domain": "GltfFileStatus", "member": "kMalformed"})
            self.assertEqual(extension["capture"]["diagnostic"]["type"], "GltfFileDiagnostic")
            if glb:
                self.assertEqual(extension["capture"]["diagnostic"]["container_status"],
                                 {"domain": "GlbStatus", "member": "kMalformed"})
            self.assertEqual(report["snapshots"], [])
            self.assertEqual(report["maps"], [])
    def test_precanceled_batch_and_single_actual_cancellation(self):
        token = workflow.GltfCancellation(); token.cancel()
        value = self.parsed(gltf.run_gltf_jobs(request(job("a"), job("b")), base=self.root, cancellation=token))
        self.assertEqual(value["exit_code"], 130)
        self.assertTrue(all(row["outcome"] == "not-attempted" and row["report"] is None and row["reason"] for row in value["jobs"]))
        self.assertTrue(all(row["input"]["sha256"] is None for row in value["jobs"]))
        result = gltf._run_gltf_single(job("single"), base=self.root, cancellation=token)
        report = self.parsed(result)
        self.fallback(report, workflow_started=False)
        self.assertEqual((result.exit_code, report["execution"]["outcome"]), (130, "cancelled"))
    def test_help_version_and_gltf_capture_do_not_import_interchange_python(self):
        script = """import builtins,sys
original=builtins.__import__
def guarded(name,*a,**k):
 if name.split('.')[0] in ('meshvale_interchange',): raise AssertionError('optional interchange imported')
 return original(name,*a,**k)
builtins.__import__=guarded
from meshvale_repair.commands import main
raise SystemExit(main(sys.argv[1:]))
"""
        for args in (("--help",), ("--version",), ("gltf", "sources/missing.gltf", "outputs/x", "--primitive-ordinal", "0", "--triangles", "require-triangles", "--values", "raw", "--unchanged")):
            process = subprocess.run([sys.executable, "-I", "-c", script, *args], cwd=self.root, capture_output=True, timeout=60)
            self.assertEqual(process.returncode, 2 if args[0] == "gltf" else 0, process.stderr)
            if args[0] == "gltf":
                self.checked(reports.load(process.stdout))
    def test_wrong_frontend_policies_never_capture(self):
        base = ["gltf", str(self.root / "sources/a" / ENTRY), str(self.root / "outputs/a")]
        valid = ["--primitive-ordinal", "0", "--triangles", "require-triangles", "--values", "raw"]
        for extra in ([], ["--target", "0:1", "--unchanged"], ["--target", "-1:1"],
                      ["--target", "0:18446744073709551616"], ["--unchanged", "--row-local", "edge:x"]):
            out, error = io.StringIO(), io.StringIO()
            with patch.object(workflow, "_run_gltf_request") as run:
                code = commands.main(base + valid + extra, stdout=out, stderr=error)
                run.assert_not_called()
            self.assertEqual((code, out.getvalue()), (2, ""))
            self.assertNotIn(str(self.root), error.getvalue())


class UnsupportedTests(Base):
    def test_actual_nonempty_unsupported_materialization_receipt(self):
        for glb in (False, True):
            name = "glb" if glb else "json"
            fixture(self.root / "sources" / name, glb)
            row = job(name, source=f"sources/{name}/{ENTRY}")
            result = gltf._run_gltf_single(row, base=self.root)
            report = self.parsed(result)
            self.assertEqual((result.exit_code, report["candidate"]["outcome"]), (2, "not-attempted"))
            self.assertNotIn(COMMAND_EXTENSION, report["extensions"])
            self.assertEqual(report["publication"]["outcome"], "not-attempted")
            self.assertFalse((self.root / row["destination"]).exists())
            process = self.invoke(*self.single_arguments(row))
            self.assertEqual(process.returncode, 2, process.stderr)
            self.checked(reports.load(process.stdout))


class QualifiedTests(Base):
    def test_completed_repair_report_extent_refuses_delivery_and_later_jobs(self):
        # The real correspondence exceeds the fixed wire limit; no caps are changed.
        faces, removed = 400_000, 200_000
        original_members = duplicate_triangle_fixture(self.root / "sources/large", faces)
        row = job("large-maps", source="sources/large/scene.gltf")
        row["targets"] = [[0, index] for index in range(1, removed + 1)]
        value = request(row, job("later", source="sources/missing/later.gltf"))
        self.assertLess(len(canonical(value)), REQUEST_BYTES)
        session, observations = workflow.GltfExecution(), []
        original_run = workflow._run_gltf_request
        def observe(prepared):
            result = original_run(prepared)
            observations.append((result.capture_status.name, result.workflow_started,
                result.report_json is None, result.status.name, result.memory_outcomes.stage.name,
                result.input_snapshot_counts, result.candidate_snapshot_counts))
            return result
        with patch.object(workflow, "_run_gltf_request", new=observe):
            with self.assertRaises(gltf.GltfCommandDeliveryError) as error:
                gltf.run_gltf_jobs(value, base=self.root, execution=session)
        self.assertEqual(error.exception.exit_code, 2)
        self.assertEqual(observations, [("kSuccess", True, True, "kLimitExceeded", "kReport",
            (3, faces, 3 * faces), (3, faces - removed, 3 * (faces - removed)))])
        self.assertEqual((session.active_tracked_payload, session.active_workers), (0, 0))
        self.assertEqual(list((self.root / "outputs").iterdir()), [])
        self.assertEqual(actual_files(self.root / "sources/large"), original_members)
    def test_module_console_api_changed_noop_json_glb_exact_receipt(self):
        for glb in (False, True):
            name = "glb" if glb else "json"
            original = fixture(self.root / "sources" / name, glb)
            for changed in (False, True):
                identity = name + ("-changed" if changed else "-noop")
                row = job(identity, source=f"sources/{name}/{ENTRY}", changed=changed)
                process = self.invoke(*self.single_arguments(row), console=changed)
                self.assertEqual(process.returncode, 0, process.stderr)
                receipt = (self.root / row["destination"] / RECEIPT).read_bytes()
                self.assertEqual(process.stdout, receipt + b"\n")
                report = self.checked(reports.load(receipt))
                self.counts_and_maps(report, changed)
                self.assertEqual(report["input"]["sha256"], physical_source_digest(original, glb))
                self.assertNotIn(COMMAND_EXTENSION, report["extensions"])
                self.assertEqual(report["candidate"]["outcome"], "accepted" if changed else "unchanged")
                files = actual_files(self.root / row["destination"])
                self.assertEqual(files["mesh.bin"], BINARY)
                if glb:
                    self.assertEqual(parse_glb(files[ENTRY])[1:], list(NON_JSON))
                if not changed:
                    self.assertEqual(files[ENTRY], original[ENTRY])
                self.assertEqual(actual_files(self.root / "sources" / name), original)
                api_row = row | {"id": identity + "-api", "destination": "outputs/" + identity + "-api"}
                captured = []
                original_run = workflow._run_gltf_request
                def observe(prepared):
                    result = original_run(prepared)
                    captured.append(result.report_json)
                    return result
                with patch.object(workflow, "_run_gltf_request", new=observe):
                    result = gltf._run_gltf_single(api_row, base=self.root)
                self.assertEqual(result.report_json, captured[0])
                self.assertEqual(result.report_json, (self.root / api_row["destination"] / RECEIPT).read_bytes())
                self.parsed(result)
    def test_ordered_batch_literal_slices_and_owner_release_before_next_job(self):
        fixture(self.root / "sources/b", True)
        rows = [job("a"), job("b", source="sources/b/scene.gltf", changed=False)]
        session = workflow.GltfExecution()
        captured, charges = [], []
        original = workflow._run_gltf_request
        def observe(prepared):
            charges.append(session.active_tracked_payload)
            result = original(prepared)
            captured.append(result.report_json)
            return result
        with patch.object(workflow, "_run_gltf_request", new=observe):
            result = gltf.run_gltf_jobs(request(*rows), base=self.root, execution=session)
        batch = self.parsed(result)
        self.assertEqual(charges, [0, 0])
        self.assertEqual(session.active_tracked_payload, 0)
        self.assertEqual([row["id"] for row in batch["jobs"]], ["a", "b"])
        self.assertTrue(all(row["outcome"] == "attempted" for row in batch["jobs"]))
        for index, raw in enumerate(captured):
            self.assertIn(raw, result.report_json)
            self.assertEqual(raw, (self.root / rows[index]["destination"] / RECEIPT).read_bytes())
            self.assertEqual(reports.load(raw), batch["jobs"][index]["report"])
    def test_request_file_base_unicode_and_continue_fail_fast(self):
        unicode_root = self.root / "\u8d44\u4ea7-\U0001f9ca"
        fixture(unicode_root / "source")
        rows = [job("first", source="source/scene.gltf", destination="published/a"),
                job("missing", source="missing/scene.gltf", destination="published/b"),
                job("last", source="source/scene.gltf", destination="published/c", changed=False)]
        (unicode_root / "published").mkdir()
        path = unicode_root / "jobs.json"; path.write_bytes(canonical(request(*rows)))
        other = self.root / "elsewhere"; other.mkdir()
        process = self.invoke("gltf-batch", str(path), cwd=other)
        self.assertEqual(process.returncode, 2, process.stderr)
        batch = self.checked(reports.load(process.stdout))
        self.assertEqual([r["outcome"] for r in batch["jobs"]], ["attempted"] * 3)
        fast_rows = [r | {"destination": r["destination"] + "-fast"} for r in rows]
        fast = gltf.run_gltf_jobs(request(*fast_rows), base=unicode_root, fail_fast=True)
        value = self.parsed(fast)
        self.assertEqual([r["outcome"] for r in value["jobs"]], ["attempted", "attempted", "not-attempted"])
        self.assertIsNone(value["jobs"][2]["report"])
        self.assertIn("missing", value["jobs"][2]["reason"])
    def test_actual_commit_then_cancel_batch_and_single_do_not_erase_receipt(self):
        original = workflow._run_gltf_request
        token = workflow.GltfCancellation()
        def commit_then_cancel(prepared):
            result = original(prepared)
            token.cancel()
            return result
        with patch.object(workflow, "_run_gltf_request", new=commit_then_cancel):
            result = gltf.run_gltf_jobs(request(job("first"), job("later")), base=self.root, cancellation=token)
        value = self.parsed(result)
        self.assertEqual(result.exit_code, 130)
        self.assertEqual(value["jobs"][0]["report"]["publication"]["outcome"], "published")
        self.assertEqual(value["jobs"][1]["outcome"], "not-attempted")
        self.assertFalse((self.root / "outputs/later").exists())
        token = workflow.GltfCancellation()
        with patch.object(workflow, "_run_gltf_request", new=commit_then_cancel):
            single = gltf._run_gltf_single(job("single"), base=self.root, cancellation=token)
        self.assertEqual((single.exit_code, self.parsed(single)["publication"]["outcome"]), (0, "published"))
    def test_stdout_is_native_bytes_and_late_delivery_interrupt_keeps_completed_exit(self):
        previous = signal.getsignal(signal.SIGINT)
        def interrupt():
            signal.raise_signal(signal.SIGINT)
        out = BrokenOutput("ok", interrupt); error = io.StringIO()
        row = job("late")
        code = commands.main(self.single_arguments(row), stdout=out, stderr=error)
        self.assertEqual(code, 0)
        self.assertEqual(out.payload.encode("utf-8"), (self.root / row["destination"] / RECEIPT).read_bytes() + b"\n")
        self.assertEqual(signal.getsignal(signal.SIGINT), previous)
        self.assertEqual(error.getvalue(), "")
    def test_real_sigint_cancels_shared_token_and_restores_handler(self):
        previous = signal.getsignal(signal.SIGINT)
        original = workflow._run_gltf_request
        def interrupt(prepared):
            signal.raise_signal(signal.SIGINT)
            return original(prepared)
        out, error = io.StringIO(), io.StringIO()
        with patch.object(workflow, "_run_gltf_request", new=interrupt):
            code = commands.main(self.single_arguments(job("sigint")), stdout=out, stderr=error)
        self.assertEqual(code, 130)
        self.assertEqual(self.checked(reports.load(out.getvalue()))["execution"]["outcome"], "cancelled")
        self.assertEqual(signal.getsignal(signal.SIGINT), previous)
        self.assertFalse((self.root / "outputs/sigint").exists())
    def test_broken_write_short_none_flush_preserve_native_publication(self):
        for phase in ("write", "short", "none", "flush"):
            row = job("broken-" + phase)
            out, error = BrokenOutput(phase), io.StringIO()
            code = commands.main(self.single_arguments(row), stdout=out, stderr=error)
            self.assertEqual(code, 2)
            receipt = (self.root / row["destination"] / RECEIPT).read_bytes()
            self.assertEqual(reports.exit_code(reports.load(receipt)), 0)
            self.assertEqual(out.writes, 1)
            self.assertNotIn(str(self.root), error.getvalue())
            self.assertNotIn("private-delivery-marker", error.getvalue())
            self.assertNotIn(b"report_delivery", receipt)
    def test_geometry_semantics_reject_corrupt_actual_correspondence(self):
        result = gltf._run_gltf_single(job("actual"), base=self.root)
        value = self.parsed(result)
        for corrupt in ("map", "counts", "profile", "publication"):
            bad = deepcopy(value)
            if corrupt == "map":
                next(m for m in bad["maps"] if m["domain"] == "face")["entries"] = [0]
            elif corrupt == "counts":
                next(s for s in bad["snapshots"] if s["role"] == "candidate")["meshes"][0]["corner_count"] = 4
            elif corrupt == "profile":
                bad["profile"]["outcome"] = "failed"
            else:
                bad["publication"]["snapshot"] = None
            with self.subTest(corrupt=corrupt), self.assertRaises(reports.ReportError):
                reports.validate(bad)


def load_tests(loader, tests, pattern):
    suite = unittest.TestSuite(loader.loadTestsFromTestCase(AdmissionTests))
    suite.addTests(loader.loadTestsFromTestCase(QualifiedTests if PROFILE == "qualified" else UnsupportedTests))
    return suite


if __name__ == "__main__":
    unittest.main(verbosity=2)
