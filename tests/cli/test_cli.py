# SPDX-License-Identifier: Apache-2.0
"""Installed commands and sequential scheduling over real OBJ bundles."""
from copy import deepcopy
import io as streams
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import meshvale_interchange as interchange
from meshvale_reports import load, validate, exit_code
from meshvale_repair import commands, obj_workflow


OBJ = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nf 1 2 3\nf 2 3 1\nf 2 4 3\n"


def fixture(root):
    (root / "outputs").mkdir()
    for name in ("a", "b", "c"):
        source = root / "sources" / name
        source.mkdir(parents=True)
        (source / "model.obj").write_text(OBJ, encoding="ascii")


def job(name, *, targets=None, source=None):
    return {"id": name, "input": source or "sources/a/model.obj",
            "destination": "outputs/" + name, "targets": [[0, 1]] if targets is None else targets}


def request(*jobs):
    return {"schema": "meshvale.repair.obj-jobs/1", "jobs": list(jobs)}


class BrokenOutput:
    def __init__(self, phase):
        self.phase = phase
        self.text = ""

    def write(self, value):
        if self.phase == "write":
            raise BrokenPipeError("private-stream-marker")
        self.text += value
        if self.phase == "short":
            return len(value) - 1
        return len(value)

    def flush(self):
        if self.phase == "flush":
            raise OSError("private-stream-marker")


class CommandTests(unittest.TestCase):
    def invoke(self, root, *arguments, console=False):
        executable = Path(sys.executable).parent / ("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        command = [str(executable)] if console else [sys.executable, "-I", "-m", "meshvale_repair"]
        return subprocess.run(command + list(arguments), cwd=root, capture_output=True,
                              text=True, encoding="utf-8", check=False, timeout=60)

    def checked(self, root, batch):
        validate(batch)
        self.assertEqual(load(json.dumps(batch)), batch)
        self.assertEqual(exit_code(batch), batch["exit_code"])
        self.assertNotIn(str(root), json.dumps(batch))
        self.assertFalse(list((root / "outputs").glob(".meshvale-stage-*")))
        return batch

    def receipt(self, root, name):
        return load((root / "outputs" / name / "meshvale-report.json").read_text(encoding="ascii"))

    def test_help_and_version_do_not_import_optional_asset_runtime(self):
        script = """import builtins, sys
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in ('meshvale_interchange', 'meshvale_reports', 'jsonschema'):
        raise AssertionError('optional runtime imported')
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
from meshvale_repair.commands import main
raise SystemExit(main([sys.argv[1]]))
"""
        for option in ("--help", "--version"):
            process = subprocess.run([sys.executable, "-I", "-c", script, option], capture_output=True, text=True, timeout=60)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertTrue(process.stdout.strip())

    def test_module_and_console_actual_single_outcomes_and_private_paths(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            original = (root / "sources/a/model.obj").read_bytes()
            cases = (("accepted", ["--target", "0:1"], 0, "accepted", False),
                     ("unchanged", ["--unchanged"], 0, "unchanged", True),
                     ("rejected", ["--target", "0:2"], 1, "rejected", False))
            for name, options, code, outcome, console in cases:
                process = self.invoke(root, "obj", "sources/a/model.obj", "outputs/" + name,
                                      "--input-id", name, *options, console=console)
                self.assertEqual(process.returncode, code, process.stderr)
                report = load(process.stdout)
                self.assertEqual(exit_code(report), code)
                self.assertEqual(report["candidate"]["outcome"], outcome)
                self.assertEqual(report["input"]["id"], name)
                self.assertNotIn(str(root), process.stdout + process.stderr)
                self.assertEqual(process.stdout.count("\n"), 1)
                if code == 0:
                    self.assertEqual(self.receipt(root, name), report)
                    imported = interchange.read_obj_file(root / "outputs" / name / "model.obj").asset
                    self.assertEqual(imported.document.mesh.face_count, 2 if outcome == "accepted" else 3)
                else:
                    self.assertFalse((root / "outputs" / name).exists())
            missing = self.invoke(root, "obj", "sources/missing/model.obj", "outputs/missing", "--unchanged")
            self.assertEqual(missing.returncode, 2, missing.stderr)
            self.assertEqual(load(missing.stdout)["execution"]["stage"], "import")
            self.assertNotIn(str(root), missing.stdout + missing.stderr)
            self.assertEqual((root / "sources/a/model.obj").read_bytes(), original)

    def test_wrong_single_invocations_and_root_overlap_never_process(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            invalid = ([], ["--target", "0:1", "--unchanged"], ["--target", "-1:1"],
                       ["--target", "0:18446744073709551616"], ["--target", "a:1"],
                       ["--unchanged", "--row-local", "unknown:name"],
                       ["--unchanged", "--report-path", "../receipt.json"])
            for arguments in invalid:
                stdout, stderr = streams.StringIO(), streams.StringIO()
                with patch.object(obj_workflow, "repair_obj_file") as operation:
                    code = commands.main(["obj", str(root / "sources/a/model.obj"),
                                          str(root / "outputs/a"), *arguments], stdout=stdout, stderr=stderr)
                self.assertEqual(code, 2)
                operation.assert_not_called()
                self.assertEqual(stdout.getvalue(), "")
                self.assertNotIn(str(root), stderr.getvalue())
            process = self.invoke(root, "obj", "sources/a/model.obj", "sources/a/published", "--unchanged")
            self.assertEqual(process.returncode, 2)
            self.assertEqual(process.stdout, "")
            self.assertFalse((root / "sources/a/published").exists())

    def test_request_relative_paths_and_continue_or_fail_fast(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            jobs = request(job("first"), job("rejected", targets=[[0, 2]]),
                           job("missing", source="sources/missing/model.obj"), job("last", targets=[]))
            (root / "jobs.json").write_text(json.dumps(jobs), encoding="utf-8")
            outside = root / "elsewhere"; outside.mkdir()
            process = self.invoke(outside, "obj-batch", str(root / "jobs.json"))
            self.assertEqual(process.returncode, 2, process.stderr)
            batch = self.checked(root, load(process.stdout))
            self.assertEqual([exit_code(row["report"]) for row in batch["jobs"]], [0, 1, 2, 0])
            for index, name in ((0, "first"), (3, "last")):
                receipt = self.receipt(root, name)
                self.assertEqual(batch["jobs"][index]["report"], receipt)
                self.assertEqual(batch["jobs"][index]["input"], receipt["input"])
                self.assertEqual(len(receipt["input"]["sha256"]), 64)
            fast = deepcopy(jobs)
            for row in fast["jobs"]: row["destination"] += "-fast"
            batch = self.checked(root, commands.run_obj_jobs(fast, base=root, fail_fast=True))
            self.assertEqual(batch["exit_code"], 1)
            self.assertEqual([row["outcome"] for row in batch["jobs"]], ["attempted", "attempted", "not-attempted", "not-attempted"])
            for row in batch["jobs"][2:]:
                self.assertEqual(row["input"], {"id": row["id"], "sha256": None})
                self.assertIsNone(row["report"])
                self.assertIn("rejected", row["reason"])
            empty = self.checked(root, commands.run_obj_jobs(request(), base=root))
            self.assertEqual(empty["exit_code"], 0)

    def test_entire_request_prevalidated_and_json_ambiguities_rejected(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            valid = request(job("first"), job("second"))
            malformed = []
            for mutation in (lambda r: r.update(extra=True), lambda r: r["jobs"][1].update(extra=True),
                             lambda r: r["jobs"][1].pop("targets"), lambda r: r["jobs"][1].update(id="first"),
                             lambda r: r["jobs"][1].update(targets=[[False, 1]]),
                             lambda r: r["jobs"][1].update(row_local_attributes=[["unknown", "channel"]]),
                             lambda r: r["jobs"][1].update(report_path="/private/report.json")):
                value = deepcopy(valid); mutation(value); malformed.append(value)
            for value in malformed:
                with patch.object(obj_workflow, "repair_obj_file") as operation:
                    with self.assertRaises(commands.InvocationError): commands.run_obj_jobs(value, base=root)
                operation.assert_not_called()
            for text in ('{"schema":"meshvale.repair.obj-jobs/1","jobs":[],"jobs":[]}',
                         '{"schema":"meshvale.repair.obj-jobs/1","jobs":[],"extra":NaN}'):
                (root / "jobs.json").write_text(text, encoding="utf-8")
                process = self.invoke(root, "obj-batch", "jobs.json")
                self.assertEqual(process.returncode, 2)
                self.assertEqual(process.stdout, "")
                self.assertNotIn(str(root), process.stderr)
            self.assertEqual(list((root / "outputs").iterdir()), [])

    def test_equal_nested_resource_and_resolved_alias_conflicts(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            conflicts = [("outputs/a", "outputs/a"), ("outputs/a", "outputs/a/child"),
                         ("outputs/a", "outputs/a/../a"), ("outputs/a", "sources/b/published")]
            for first, second in conflicts:
                value = request(job("one"), job("two", source="sources/b/model.obj"))
                value["jobs"][0]["destination"], value["jobs"][1]["destination"] = first, second
                with patch.object(obj_workflow, "repair_obj_file") as operation:
                    with self.assertRaises(commands.InvocationError): commands.run_obj_jobs(value, base=root)
                operation.assert_not_called()
            value = request(job("one")); value["jobs"][0]["resource_root"] = "."
            with self.assertRaises(commands.InvocationError): commands.run_obj_jobs(value, base=root)
            try: (root / "alias").symlink_to(root / "sources/a", target_is_directory=True)
            except OSError: return  # Symlinks may require OS privileges; other conflicts were still checked.
            value = request(job("alias")); value["jobs"][0]["destination"] = "alias/published"
            with self.assertRaises(commands.InvocationError): commands.run_obj_jobs(value, base=root)

    def test_later_job_unpaired_surrogates_refuse_entire_request_before_processing(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            for field, value in (("row_local_attributes", [["face", "\ud800"]]),
                                 ("report_path", "receipts/\ud800.json")):
                jobs = request(job("first"), job("second"))
                jobs["jobs"][1][field] = value
                with patch.object(obj_workflow, "repair_obj_file") as operation:
                    with self.assertRaises(commands.InvocationError): commands.run_obj_jobs(jobs, base=root)
                operation.assert_not_called()
                self.assertEqual(list((root / "outputs").iterdir()), [])

    def test_real_closed_stdout_pipe_preserves_exit_two_without_shutdown_retry(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "jobs.json").write_text(json.dumps(request()), encoding="utf-8")
            reader, writer = os.pipe()
            os.close(reader)
            try:
                process = subprocess.run([sys.executable, "-I", "-m", "meshvale_repair", "obj-batch", "jobs.json"],
                                         cwd=root, stdout=writer, stderr=subprocess.PIPE, text=True,
                                         encoding="utf-8", timeout=60, check=False)
            finally:
                os.close(writer)
            self.assertEqual(process.returncode, 2, process.stderr)
            self.assertIn("Report delivery failed", process.stderr)
            self.assertNotIn("Exception ignored", process.stderr)
            self.assertNotIn(str(root), process.stderr)

    def test_linked_obj_inside_root_retains_logical_resource_base(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            shared = root / "sources/shared"; shared.mkdir()
            (shared / "model.obj").write_text("mtllib material.mtl\nusemtl painted\n" + OBJ, encoding="ascii")
            logical = root / "sources/a/model.obj"; logical.unlink()
            try:
                logical.symlink_to(Path("..") / "shared" / "model.obj")
            except OSError as error:
                self.skipTest("File symlink unavailable: " + type(error).__name__)
            material = root / "sources/a/material.mtl"
            material.write_bytes(b"newmtl painted\nKd .2 .3 .4\n")
            self.assertEqual(logical.read_bytes(), (shared / "model.obj").read_bytes())
            source_bytes = logical.read_bytes(), (shared / "model.obj").read_bytes(), material.read_bytes()
            direct = obj_workflow.repair_obj_file(logical, root / "outputs/direct", [[0, 1]],
                                                  input_id="linked", resource_root=root / "sources")
            self.assertEqual(direct.exit_code, 0, direct.report)
            jobs = request(job("linked")); jobs["jobs"][0].update(resource_root="sources", destination="outputs/command")
            batch = self.checked(root, commands.run_obj_jobs(jobs, base=root))
            self.assertEqual(batch["exit_code"], 0, batch)
            self.assertEqual(batch["jobs"][0]["report"], direct.report)
            self.assertEqual(self.receipt(root, "command"), direct.report)
            imported = interchange.read_obj_file(root / "outputs/command/a/model.obj", resource_root=root / "outputs/command").asset
            self.assertEqual(imported.document.mesh.face_count, 2)
            self.assertEqual((root / "outputs/command/a/material.mtl").read_bytes(), material.read_bytes())
            self.assertEqual((logical.read_bytes(), (shared / "model.obj").read_bytes(), material.read_bytes()), source_bytes)
            self.assertTrue(logical.is_symlink())

    def test_cancellation_before_jobs_and_all_actual_publication_phases(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            token = interchange.Cancellation(); token.request_stop()
            with patch.object(obj_workflow, "repair_obj_file") as operation:
                batch = self.checked(root, commands.run_obj_jobs(request(job("a"), job("b")), base=root, cancellation=token))
            operation.assert_not_called()
            self.assertEqual(batch["exit_code"], 130)
            self.assertTrue(all(row["reason"] and row["report"] is None for row in batch["jobs"]))
            for phase in ("preflight", "staging", "verification", "publication"):
                token = interchange.Cancellation(); observed = []
                def cancel(identity, current):
                    observed.append((identity, current))
                    if current == phase: token.request_stop()
                batch = self.checked(root, commands.run_obj_jobs(request(job("a"), job("b")), base=root,
                                     cancellation=token, on_job_phase=cancel))
                self.assertEqual(batch["exit_code"], 130)
                self.assertIn(("a", phase), observed)
                self.assertEqual(batch["jobs"][0]["report"]["publication"]["outcome"], "cancelled")
                self.assertEqual(batch["jobs"][1]["outcome"], "not-attempted")
                self.assertFalse((root / "outputs/a").exists())

    def test_cancellation_after_real_commit_retains_receipt_and_stops_later_jobs(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            token = interchange.Cancellation(); real_operation = obj_workflow.repair_obj_file
            def commit_then_cancel(*args, **kwargs):
                result = real_operation(*args, **kwargs)
                self.assertEqual(result.exit_code, 0)
                token.request_stop()
                return result
            with patch.object(obj_workflow, "repair_obj_file", side_effect=commit_then_cancel):
                batch = self.checked(root, commands.run_obj_jobs(request(job("a"), job("b")), base=root, cancellation=token))
            self.assertEqual(batch["exit_code"], 130)
            self.assertEqual(batch["jobs"][0]["report"], self.receipt(root, "a"))
            self.assertEqual(batch["jobs"][0]["report"]["publication"]["outcome"], "published")
            self.assertEqual(batch["jobs"][1]["outcome"], "not-attempted")
            self.assertFalse((root / "outputs/b").exists())

    def test_delivery_write_and_flush_failures_preserve_real_receipts_and_handler(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            handler = signal.getsignal(signal.SIGINT)
            for phase in ("write", "flush", "short"):
                name = phase; stderr = streams.StringIO()
                code = commands.main(["obj", str(root / "sources/a/model.obj"), str(root / "outputs" / name),
                                      "--target", "0:1"], stdout=BrokenOutput(phase), stderr=stderr)
                self.assertEqual(code, 2)
                self.assertEqual(self.receipt(root, name)["execution"]["outcome"], "completed")
                self.assertNotIn("private-stream-marker", stderr.getvalue())
                self.assertNotIn(str(root), stderr.getvalue())
                self.assertIs(signal.getsignal(signal.SIGINT), handler)

    def test_sigint_requests_cooperative_token_and_restores_caller_handler(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            handler = signal.getsignal(signal.SIGINT); real_operation = obj_workflow.repair_obj_file
            def interrupt_during_publication(*args, **kwargs):
                kwargs["on_phase"] = lambda phase: signal.raise_signal(signal.SIGINT) if phase == "publication" else None
                return real_operation(*args, **kwargs)
            stdout = streams.StringIO()
            with patch.object(obj_workflow, "repair_obj_file", side_effect=interrupt_during_publication):
                code = commands.main(["obj", str(root / "sources/a/model.obj"), str(root / "outputs/a"),
                                      "--target", "0:1"], stdout=stdout, stderr=streams.StringIO())
            self.assertEqual(code, 130)
            self.assertEqual(load(stdout.getvalue())["publication"]["outcome"], "cancelled")
            self.assertFalse((root / "outputs/a").exists())
            self.assertIs(signal.getsignal(signal.SIGINT), handler)

    def test_late_sigint_during_report_delivery_retains_completed_exit(self):
        class InterruptingOutput(streams.StringIO):
            def write(self, value):
                signal.raise_signal(signal.SIGINT)
                return super().write(value)
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            handler = signal.getsignal(signal.SIGINT)
            for batch_mode in (False, True):
                name = "batch" if batch_mode else "single"
                if batch_mode:
                    (root / "jobs.json").write_text(json.dumps(request(job(name))), encoding="utf-8")
                    arguments = ["obj-batch", str(root / "jobs.json")]
                else:
                    arguments = ["obj", str(root / "sources/a/model.obj"), str(root / "outputs" / name), "--target", "0:1"]
                stdout = InterruptingOutput()
                self.assertEqual(commands.main(arguments, stdout=stdout, stderr=streams.StringIO()), 0)
                report = load(stdout.getvalue())
                self.assertEqual(report["execution"]["outcome"], "completed")
                single = report["jobs"][0]["report"] if batch_mode else report
                self.assertEqual(single, self.receipt(root, name))
                self.assertIs(signal.getsignal(signal.SIGINT), handler)

    def test_batch_delivery_failure_and_cancellation_precedence(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            real_schedule = commands.run_obj_jobs; captured = []
            def capture(*args, **kwargs):
                batch = real_schedule(*args, **kwargs); captured.append(batch); return batch
            (root / "jobs.json").write_text(json.dumps(request(job("a"))), encoding="utf-8")
            with patch.object(commands, "run_obj_jobs", side_effect=capture):
                code = commands.main(["obj-batch", str(root / "jobs.json")], stdout=BrokenOutput("flush"), stderr=streams.StringIO())
            self.assertEqual(code, 2)
            self.checked(root, captured[0])
            self.assertEqual(captured[0]["execution"]["stage"], "report")
            self.assertEqual(captured[0]["jobs"][0]["report"], self.receipt(root, "a"))
            (root / "jobs.json").write_text(json.dumps(request(job("b"), job("c"))), encoding="utf-8")
            real_operation = obj_workflow.repair_obj_file
            def commit_then_cancel(*args, **kwargs):
                result = real_operation(*args, **kwargs)
                kwargs["cancellation"].request_stop()
                return result
            with patch.object(obj_workflow, "repair_obj_file", side_effect=commit_then_cancel):
                code = commands.main(["obj-batch", str(root / "jobs.json")], stdout=BrokenOutput("write"), stderr=streams.StringIO())
            self.assertEqual(code, 130)
            self.assertEqual(self.receipt(root, "b")["publication"]["outcome"], "published")
            self.assertFalse((root / "outputs/c").exists())

    def test_internal_errors_propagate_from_scheduler_and_cli_fabricates_no_report(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); fixture(root)
            with patch.object(obj_workflow, "repair_obj_file", side_effect=AssertionError("private-internal-marker")):
                with self.assertRaises(AssertionError): commands.run_obj_jobs(request(job("a")), base=root)
                stdout, stderr = streams.StringIO(), streams.StringIO()
                code = commands.main(["obj", str(root / "sources/a/model.obj"), str(root / "outputs/a"),
                                      "--unchanged"], stdout=stdout, stderr=stderr)
            self.assertEqual(code, 2)
            self.assertEqual(stdout.getvalue(), "")
            self.assertNotIn("private-internal-marker", stderr.getvalue())
            self.assertFalse((root / "outputs/a").exists())


if __name__ == "__main__":
    unittest.main()
