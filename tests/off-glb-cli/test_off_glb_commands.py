# SPDX-License-Identifier: Apache-2.0
"""Installed SUR-01 commands: real native work, files and subprocess exits.

python -I -m unittest discover -s /absolute/tests/off-glb-cli -v
Only post-native framing limits and an actual failing stdout stream are injected.
"""
from concurrent.futures import ThreadPoolExecutor
from dataclasses import FrozenInstanceError
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import sysconfig
import tempfile
import unittest
from unittest.mock import patch

# Share data/oracles, never a source package or an alternate native implementation.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "off-glb-python"))
from off_glb_fixture import (canonical, fixture, job, payload, physical, request,
                             source_bytes)
from meshvale_repair import off_glb_commands as commands
from meshvale_repair import off_surface_export as workflow
from meshvale_repair.commands import InvocationError


FIELDS = {"schema", "id", "attempted", "exit", "status", "stage", "code", "capture",
          "off", "triangulation", "encoding", "inspection", "publication", "delivery",
          "off_report_utf8", "publication_receipt_utf8", "reason"}
LATER = ("off", "triangulation", "encoding", "inspection", "publication",
         "off_report_utf8", "publication_receipt_utf8")


def single_args(source, destination, identity="one", targets="[[0,1]]"):
    return ["single", "--input", str(source), "--destination", str(destination),
        "--targets", targets, "--text", "canonical-ascii", "--edge-count", "unknown-zero",
        "--coordinates", "meters-right-handed-y-up", "--precision", "exact32",
        "--max-position-error", "0", "--ack-triangle-conversion", "--ack-corner-expansion",
        "--ack-unused-vertices", "--id", identity]


class FailingStdout:
    """An actual output sink that refuses delivery after genuine publication."""
    @property
    def buffer(self):
        return self
    def write(self, _):
        raise OSError("deliberate output failure")
    def flush(self):
        raise OSError("deliberate output failure")


class OffGlbCommands(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="meshvale-off-glb-cli-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve()
        self.source = fixture(self.root / "input")

    def subprocess(self, args, *, console=False, cwd=None):
        if console:
            script = Path(sysconfig.get_path("scripts")) / ("meshvale-repair-off-glb.exe" if os.name == "nt" else "meshvale-repair-off-glb")
            self.assertTrue(script.is_file(), "The current interpreter's installed console script must be present")
            prefix = [str(script)]
        else:
            prefix = [sys.executable, "-I", "-m", "meshvale_repair.off_glb_commands"]
        return subprocess.run(prefix + args, cwd=cwd or self.root, capture_output=True,
                              timeout=120, check=False)

    def row(self, row, result=None):
        self.assertEqual(set(row), FIELDS)
        self.assertEqual(row["schema"], "meshvale.repair.off-glb-result/1")
        self.assertEqual(row["delivery"], {"status": "not-attempted", "code": "none"})
        if result is not None:
            self.assertEqual(row["exit"], result.exit)
            for field, getter in (("off_report_utf8", "off_report_bytes"),
                                  ("publication_receipt_utf8", "publication_receipt_bytes")):
                raw = getattr(result, getter)
                self.assertEqual(None if row[field] is None else row[field].encode("utf-8"), raw)

    def success_row(self, row, destination, *, changed=True):
        self.row(row)
        self.assertTrue(row["attempted"])
        self.assertEqual((row["exit"], row["status"], row["stage"], row["code"]),
                         (0, "native:kSuccess", "native:kComplete", "native:kNone"))
        self.assertIsNone(row["reason"])
        files = physical(destination)
        payload(self, files, changed=changed)
        self.assertEqual(row["publication_receipt_utf8"].encode("utf-8"), files["publication.json"])
        self.assertEqual(json.loads(files["publication.json"])["caller_report"]["text"], row["off_report_utf8"])

    def test_module_console_and_reusable_api_keep_actual_reports(self):
        prior = signal.getsignal(signal.SIGINT)
        holder = commands.run_off_glb_jobs(request(job("api", source=self.source, destination="api")), base=self.root)
        self.assertIs(signal.getsignal(signal.SIGINT), prior)
        self.assertEqual((holder.exit, holder.delivery.status, holder.delivery.code), (0, "prepared", "none"))
        self.assertIs(type(holder.stdout_bytes), bytes)
        self.assertIs(holder.stdout_bytes, holder.stdout_bytes)
        self.assertIsInstance(holder.jobs, tuple)
        native = holder.jobs[0].result
        detached = json.loads(holder.stdout_bytes)
        self.assertEqual(set(detached), {"schema", "mode", "exit", "jobs"})
        self.assertEqual((detached["schema"], detached["mode"], detached["exit"]),
                         ("meshvale.repair.off-glb-batch/1", "continue", 0))
        self.row(detached["jobs"][0], native)
        self.success_row(detached["jobs"][0], self.root / "api")
        for console in (False, True):
            with self.subTest(console=console):
                destination = self.root / ("console" if console else "module")
                process = self.subprocess(single_args(self.source, destination), console=console)
                self.assertEqual(process.returncode, 0, process.stderr)
                self.success_row(json.loads(process.stdout), destination)
                self.assertEqual(self.source.read_bytes(), source_bytes())
        for owner, field in ((holder, "exit"), (holder.delivery, "status"), (holder.jobs[0], "attempted")):
            with self.assertRaises((AttributeError, FrozenInstanceError)):
                setattr(owner, field, getattr(owner, field))

    def test_batch_default_base_is_request_parent_not_process_cwd(self):
        folder = self.root / "request-parent"
        source = fixture(folder / "input")
        document = request(job("edit", source="input/source.off", destination="edit"),
                           job("noop", source="input/source.off", destination="noop", targets=()))
        path = folder / "jobs.json"
        path.write_bytes(canonical(document))
        elsewhere = self.root / "cwd"
        elsewhere.mkdir()
        process = self.subprocess(["batch", "--request", str(path)], cwd=elsewhere)
        self.assertEqual(process.returncode, 0, process.stderr)
        detached = json.loads(process.stdout)
        self.assertEqual([r["id"] for r in detached["jobs"]], ["edit", "noop"])
        self.success_row(detached["jobs"][0], folder / "edit")
        self.success_row(detached["jobs"][1], folder / "noop", changed=False)
        self.assertEqual(source.read_bytes(), source_bytes())
        self.assertEqual(list(elsewhere.iterdir()), [])

    def test_capture_only_failure_continues_and_fail_fast_is_unattempted(self):
        for fail_fast in (False, True):
            with self.subTest(fail_fast=fail_fast):
                output = "after-" + str(fail_fast)
                holder = commands.run_off_glb_jobs(request(
                    job("missing", source="absent.off", destination="missing-" + str(fail_fast)),
                    job("after", source=self.source, destination=output)), base=self.root, fail_fast=fail_fast)
                self.assertEqual(holder.exit, 2)
                first, second = holder.jobs
                self.assertTrue(first.attempted)
                self.assertIsNone(first.result.native)
                self.assertIsNotNone(first.result.capture)
                rows = json.loads(holder.stdout_bytes)["jobs"]
                self.row(rows[0], first.result)
                self.assertTrue(rows[0]["status"].startswith("capture:"))
                for key in LATER:
                    self.assertIsNone(rows[0][key])
                if fail_fast:
                    self.assertFalse(second.attempted)
                    self.assertIsNone(second.result)
                    self.assertEqual(dict(second.reason), {"kind": "fail-fast", "id": "missing"})
                    self.assertEqual((rows[1]["status"], rows[1]["stage"], rows[1]["code"], rows[1]["exit"]),
                                     ("not-attempted", None, None, None))
                    for key in ("capture", *LATER):
                        self.assertIsNone(rows[1][key])
                    self.assertFalse((self.root / output).exists())
                else:
                    self.assertTrue(second.attempted)
                    self.assertEqual(second.result.exit, 0)
                    self.success_row(rows[1], self.root / output)

    def test_canceled_invocation_has_no_invented_attempted_results(self):
        token = workflow.OffCancellation()
        token.cancel()
        holder = commands.run_off_glb_jobs(request(job("first", source=self.source),
            job("second", source=self.source)), base=self.root, cancellation=token)
        self.assertEqual(holder.exit, 130)
        self.assertEqual(holder.delivery.status, "prepared")
        for slot, row in zip(holder.jobs, json.loads(holder.stdout_bytes)["jobs"]):
            self.assertFalse(slot.attempted)
            self.assertIsNone(slot.result)
            self.assertEqual(dict(slot.reason), {"kind": "canceled", "id": None})
            self.row(row)
            self.assertIsNone(row["exit"])
            for field in ("capture", *LATER):
                self.assertIsNone(row[field])
        self.assertFalse((self.root / "out").exists())

    def test_actual_batch_process_continue_and_fail_fast_after_rejection(self):
        refused = fixture(self.root / "refusal-input", "reversed")
        for fail_fast in (False, True):
            with self.subTest(fail_fast=fail_fast):
                folder = self.root / str(fail_fast)
                folder.mkdir()
                document = request(job("bad", source=refused, destination="bad"),
                    job("good", source=self.source, destination="good"))
                path = folder / "jobs.json"
                path.write_bytes(canonical(document))
                args = ["batch", "--request", str(path)] + (["--fail-fast"] if fail_fast else [])
                process = self.subprocess(args)
                self.assertEqual(process.returncode, 1, process.stderr)
                detached = json.loads(process.stdout)
                self.assertEqual((detached["exit"], detached["mode"]),
                                 (1, "fail-fast" if fail_fast else "continue"))
                first, second = detached["jobs"]
                self.row(first)
                self.assertEqual((first["exit"], first["status"]), (1, "native:kRejected"))
                self.assertTrue(first["attempted"])
                self.assertFalse((folder / "bad").exists())
                if fail_fast:
                    self.assertFalse(second["attempted"])
                    self.assertEqual(second["reason"], {"kind": "fail-fast", "id": "bad"})
                    self.assertFalse((folder / "good").exists())
                else:
                    self.success_row(second, folder / "good")
                self.assertEqual(refused.read_bytes(), source_bytes("reversed"))

    def test_invalid_last_row_and_path_domains_fail_whole_admission(self):
        valid = job("first", source=self.source, destination="would-publish")
        bad_rows = [job("last", precision="exact64"), job("last", max_position_error=True),
                    job("last", acknowledge_corner_expansion=False),
                    job("last", targets=((0, 1), (True, 2))),
                    job("last", destination="would-publish/nested"),
                    job("last", destination=self.source.parent),
                    dict(job("last"), unknown="forbidden")]
        for bad in bad_rows:
            with self.subTest(bad=bad), self.assertRaises(InvocationError):
                commands.run_off_glb_jobs(request(valid, bad), base=self.root)
            self.assertFalse((self.root / "would-publish").exists())
            self.assertEqual(self.source.read_bytes(), source_bytes())
        path = self.root / "invalid.json"
        path.write_bytes(canonical(request(valid, bad_rows[0])))
        process = self.subprocess(["batch", "--request", str(path)])
        self.assertEqual((process.returncode, process.stdout), (2, b""))
        self.assertFalse((self.root / "would-publish").exists())

    def test_resolved_directory_alias_collision_is_admission_error(self):
        target = self.root / "target"
        target.mkdir()
        alias = self.root / "alias"
        if os.name == "nt":
            environment = os.environ.copy()
            environment["MESHVALE_TEST_ALIAS"] = str(alias)
            environment["MESHVALE_TEST_TARGET"] = str(target)
            created = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command",
                "New-Item -ItemType Junction -Path $env:MESHVALE_TEST_ALIAS "
                "-Target $env:MESHVALE_TEST_TARGET -ErrorAction Stop | Out-Null"],
                env=environment, capture_output=True, timeout=30)
            self.assertEqual(created.returncode, 0, created.stderr)
            self.addCleanup(alias.rmdir)
        else:
            alias.symlink_to(target, target_is_directory=True)
            self.addCleanup(alias.unlink)
        with self.assertRaises(InvocationError):
            commands.run_off_glb_jobs(request(job("one", source=self.source, destination=target / "new"),
                job("two", source=self.source, destination=alias / "new")), base=self.root)
        self.assertEqual(list(target.iterdir()), [])

    def test_framing_refusal_retains_every_genuine_attempted_owner(self):
        # Reduce a delivery-only cap; native inputs/results/owners are untouched.
        with patch.object(commands, "_MAX_REPORT_BYTES", 1):
            holder = commands.run_off_glb_jobs(request(job("one", source=self.source, destination="framed")), base=self.root)
        self.assertEqual((holder.delivery.status, holder.delivery.code, holder.exit),
                         ("framing-failed", "report-limit", 2))
        self.assertIsNone(holder.stdout_bytes)
        self.assertTrue(holder.jobs[0].attempted)
        result = holder.jobs[0].result
        self.assertEqual(result.exit, 0)
        self.assertIsNotNone(result.published)
        payload(self, physical(self.root / "framed"), result=result)

    def test_actual_failed_stdout_does_not_change_publication_or_native_exit(self):
        holder = commands.run_off_glb_jobs(request(job("one", source=self.source, destination="delivered")), base=self.root)
        original = holder.stdout_bytes
        result = holder.jobs[0].result
        before = physical(self.root / "delivered")
        with patch.object(commands.sys, "stdout", FailingStdout()):
            commands._write(holder)
        self.assertEqual((holder.delivery.status, holder.delivery.code, holder.exit),
                         ("write-failed", "stdout-io", 2))
        self.assertEqual(result.exit, 0)
        self.assertIs(holder.jobs[0].result, result)
        self.assertIs(holder.stdout_bytes, original)
        self.assertEqual(json.loads(original)["jobs"][0]["delivery"]["status"], "not-attempted")
        self.assertEqual(physical(self.root / "delivered"), before)
        payload(self, before, result=result)
        prior = signal.getsignal(signal.SIGINT)
        with patch.object(commands.sys, "stdout", FailingStdout()):
            exit_code = commands.main(single_args(self.source, self.root / "main-delivery"))
        self.assertEqual(exit_code, 2)
        self.assertIs(signal.getsignal(signal.SIGINT), prior)
        payload(self, physical(self.root / "main-delivery"))

    def test_main_thread_policy_and_help_restore_signal_handler(self):
        prior = signal.getsignal(signal.SIGINT)
        with ThreadPoolExecutor(max_workers=1) as pool:
            code = pool.submit(commands.main, single_args(self.source, self.root / "thread")).result()
        self.assertEqual(code, 2)
        self.assertFalse((self.root / "thread").exists())
        self.assertIs(signal.getsignal(signal.SIGINT), prior)
        with patch.object(commands.sys, "stdout", io.StringIO()):
            self.assertEqual(commands.main(["--help"]), 0)
        self.assertIs(signal.getsignal(signal.SIGINT), prior)


if __name__ == "__main__":
    unittest.main()
