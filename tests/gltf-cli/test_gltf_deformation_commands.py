# SPDX-License-Identifier: Apache-2.0
"""Actual Python command component tests over explicitly literal value seams.

No native Repair extension is imported or asset producer invoked. Execution and
returned-value controls isolate admission, scheduling and wire projection; they
do not establish a new-profile native/installed pipeline or worker/ledger result.
The ordinary report reader/validator is real and required (the workflow extra).
"""
from copy import deepcopy
from contextlib import redirect_stdout
from enum import Enum
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
import weakref
from unittest.mock import patch

import meshvale_reports as reports


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "fixtures"
ORACLES = json.loads((FIXTURES / "gltf-deformation-report-oracles.json").read_text())
MIXED = json.loads((FIXTURES / "gltf-deformation-jobs3.json").read_text())
PHASES = ("capture_read", "source_read", "source", "materialization", "writing",
          "reload_read", "reload", "verification", "publication_read",
          "publication_reload", "publication_verification")
OPTION_KEYS = ("processing_profile", "read_admission_profile", "normative_revision",
               "numerical_profile", "deformation_limits", "deformation_work", "quaternion_policy")
OLD = ("balanced-v1", "serial-reference-v1")
TRANSLATION = ("balanced-direct-translation-v1", "serial-direct-translation-v1")
DEFORMATION = ("balanced-deformation-v1", "serial-deformation-v1")
PROFILES = ("static-local-triangles-v1", "direct-translation-v1", "deformation-retained-v1")


def literal_value(value):
    """Restore only named test values from unexecuted public projection oracles."""
    if type(value) is not dict:
        return value
    if set(value) == {"domain", "member"}:
        domain = Enum(value["domain"], {value["member"]: 0})
        return getattr(domain, value["member"])
    if "type" in value:
        result = type(value["type"], (), {})()
        for key, child in value.items():
            if key != "type":
                setattr(result, key, literal_value(child))
        return result
    raise AssertionError("Unexpected literal control shape")


class LiteralResult:
    """Controlled detached properties, no asset/proof/context/native ownership."""
    def __init__(self, report, profile=PROFILES[2]):
        self.expected = deepcopy(report)
        extension = report["extensions"]["meshvale.repair.gltf.commands"]
        self.processing_profile = profile
        self.workflow_started = extension["workflow_started"]
        self.report_json = None
        for field in ("status", "diagnostic", "execution"):
            setattr(self, "capture_" + field, literal_value(extension["capture"][field]))
        if self.workflow_started:
            for field, value in extension["workflow"].items():
                setattr(self, field, literal_value(value))
        self.memory_outcomes = literal_value(extension["memory_outcomes"])
        self.publication = literal_value(extension["publication"])
        self.requested_bytes = literal_value(extension["requested_bytes"])
        if profile == PROFILES[1]:
            self.translation_facts = {"profile": PROFILES[1], **{name: None for name in (
                "source", "writing", "reload", "verification", "publication_reload", "publication_verification")}}

    @property
    def deformation_facts(self):
        return deepcopy(self.expected["extensions"]["meshvale.repair.gltf.commands"]["processing"])

    @property
    def deformation_options(self):
        options = self.expected["operation"]["options"]
        return {key: deepcopy(options[key]) for key in OPTION_KEYS}


def job_from_report(report):
    options = report["operation"]["options"]
    return {"id": report["input"]["id"], "input": "inputs/source/scene.gltf",
            "destination": "outputs/source", "targets": options["target_pairs"],
            "primitive_ordinal": options["primitive_ordinal"], "triangles": options["triangles"],
            "values": options["values"], "row_local_attributes": options["row_local_attributes"],
            "processing_profile": PROFILES[2]}


class CommandControl:
    """Import the real pure command code with isolated declared boundary controls."""
    def __init__(self):
        self.created, self.trace, self.results = [], [], []
        control = self

        class Execution:
            def __init__(self, *, preset="balanced-v1"):
                self._preset = preset
                control.created.append(self)

            @property
            def preset(self):
                return self._preset

            @property
            def resolved_preset(self):
                if self._preset in DEFORMATION:
                    return json.loads((ROOT / "docs" / "presets" / f"{self._preset}.json").read_text())["values"]
                name = TRANSLATION[1] if self._preset in (OLD[1], TRANSLATION[1]) else TRANSLATION[0]
                # Only unchanged admission descriptor fields are used here.
                return json.loads((ROOT / "docs" / "gltf-translation-presets.json").read_text())[name]

        class Cancellation:
            cancelled = False
            def cancel(self):
                self.cancelled = True

        workflow = types.ModuleType("_gltf_command_control.gltf_workflow")
        workflow.GltfExecution, workflow.GltfCancellation = Execution, Cancellation

        def prepare(**arguments):
            control.trace.append(("prepare", arguments["input_id"], arguments["execution"], arguments["cancellation"]))
            return arguments

        def run(prepared):
            control.trace.append(("run", prepared["input_id"]))
            if hasattr(control, "after_run"):
                control.after_run(prepared)
            return control.results.pop(0)

        workflow._prepare_gltf_request, workflow._run_gltf_request = prepare, run
        package = types.ModuleType("_gltf_command_control")
        package.__path__, package.__version__ = [], "0.0.0"  # Literal oracle tool version.
        modules = {package.__name__: package, workflow.__name__: workflow}
        with patch.dict(sys.modules, modules):
            self.commands = self.load("commands", "commands.py")
            with patch.dict(sys.modules, {self.commands.__name__: self.commands}):
                self.module = self.load("gltf_commands", "gltf_commands.py")
        self.workflow = workflow

    @staticmethod
    def load(name, filename):
        spec = importlib.util.spec_from_file_location("_gltf_command_control." + name,
            ROOT / "python" / "meshvale_repair" / filename)
        module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {spec.name: module}):
            spec.loader.exec_module(module)
        return module


class DeformationCommandTests(unittest.TestCase):
    def setUp(self):
        self.control = CommandControl()
        self.gltf = self.control.module
        self.scratch = tempfile.TemporaryDirectory(prefix="meshvale-command-control-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name).resolve()

    def request(self, *profiles, schema=3):
        rows = []
        for index, profile in enumerate(profiles):
            row = deepcopy(MIXED["jobs"][index % 3])
            row.update(id=f"job{index}", input=f"inputs/job{index}/scene.gltf",
                       destination=f"outputs/job{index}", processing_profile=profile)
            if schema == 1:
                row.pop("processing_profile")
            rows.append(row)
        return {"schema": f"meshvale.repair.gltf-jobs/{schema}", "jobs": rows}

    def refuse(self, request, **kwargs):
        with self.assertRaises(self.control.commands.InvocationError):
            self.gltf.run_gltf_jobs(request, base=self.base, **kwargs)
        self.assertEqual(self.control.trace, [])

    def test_schema3_mixed_prepare_all_before_any_run_and_one_context(self):
        jobs, _ = self.gltf._admit(deepcopy(MIXED), self.base, None, None)
        self.assertEqual([entry[:2] for entry in self.control.trace],
                         [("prepare", name) for name in ("static", "translation", "deformation")])
        self.assertEqual(len(self.control.created), 1)
        self.assertEqual(self.control.created[0].preset, DEFORMATION[0])
        self.assertEqual({id(j.arguments["execution"]) for j in jobs}, {id(self.control.created[0])})
        self.assertEqual(len({id(j.arguments["cancellation"]) for j in jobs}), 1)
        self.assertEqual([j.arguments["processing_profile"] for j in jobs], list(PROFILES))

    def test_schema_defaults_and_empty_cancelled_boundary_are_fresh(self):
        for schema, preset in enumerate((OLD[0], TRANSLATION[0], DEFORMATION[0]), 1):
            with self.subTest(schema=schema):
                result = self.gltf.run_gltf_jobs(self.request(schema=schema), base=self.base)
                self.assertEqual((reports.load(result.report_json)["jobs"], result.exit_code), ([], 0))
                self.assertEqual(self.control.created[-1].preset, preset)
        self.assertEqual(len({id(x) for x in self.control.created}), 3)
        token = self.control.workflow.GltfCancellation(); token.cancel()
        result = self.gltf.run_gltf_jobs(self.request(schema=3), base=self.base, cancellation=token)
        self.assertEqual((reports.load(result.report_json)["execution"]["outcome"], result.exit_code), ("cancelled", 130))

    def test_all_six_preset_profile_matrix_and_empty_vacuity(self):
        for name in OLD + TRANSLATION + DEFORMATION:
            for index, profile in enumerate(PROFILES):
                with self.subTest(preset=name, profile=profile):
                    control = CommandControl(); session = control.workflow.GltfExecution(preset=name)
                    expected = index == 0 or index == 1 and name in TRANSLATION + DEFORMATION or name in DEFORMATION
                    if expected:
                        jobs, _ = control.module._admit(self.request(profile), self.base, session, None)
                        self.assertIs(jobs[0].arguments["execution"], session)
                    else:
                        with self.assertRaises(control.commands.InvocationError):
                            control.module._admit(self.request(profile), self.base, session, None)
                        self.assertEqual(control.trace, [])
            session = self.control.workflow.GltfExecution(preset=name)
            self.gltf._admit(self.request(schema=3), self.base, session, None)

    def test_closed_old_schemas_and_invalid_last_row_refuse_before_prepare(self):
        self.refuse(self.request(PROFILES[2], schema=2))
        bad = self.request(PROFILES[0], schema=1); bad["jobs"][0]["processing_profile"] = PROFILES[0]
        self.refuse(bad)
        for change in ({"processing_profile": "unknown"}, {"primitive_ordinal": True},
                       {"targets": [[0, False]]}, {"session": "balanced-deformation-v1"},
                       {"report_path": "../report.json"}):
            with self.subTest(change=change):
                bad = self.request(*PROFILES); bad["jobs"][-1].update(change); self.refuse(bad)
        bad = self.request(*PROFILES); del bad["jobs"][-1]["processing_profile"]; self.refuse(bad)

    def test_schema3_path_isolation_precedes_prepare(self):
        bad = self.request(*PROFILES); bad["jobs"][-1]["destination"] = "inputs/job0/child"
        self.refuse(bad)

    def test_last_preparation_refusal_releases_earlier_control_before_any_run(self):
        class PreparedControl:
            pass
        reached = []
        references = []
        def prepare(**arguments):
            reached.append(arguments["input_id"])
            if arguments["input_id"] == "job2":
                raise ValueError("literal conversion refusal, not native capture")
            value = PreparedControl()
            references.append(weakref.ref(value))
            return value
        with patch.object(self.control.workflow, "_prepare_gltf_request", prepare):
            with self.assertRaises(self.control.commands.InvocationError):
                self.gltf.run_gltf_jobs(self.request(*PROFILES), base=self.base)
        self.assertEqual(reached, ["job0", "job1", "job2"])
        self.assertTrue(all(reference() is None for reference in references))
        self.assertFalse(any(row[0] == "run" for row in self.control.trace))

    def test_single_deformation_uses_schema3_fresh_command_context(self):
        report = deepcopy(ORACLES["missing_native_receipt_full_report"])
        self.control.results.append(LiteralResult(report))
        job = job_from_report(report); job.pop("processing_profile")
        result = self.gltf._run_gltf_single(job, base=self.base, processing_profile=PROFILES[2])
        self.assertEqual(self.control.created[0].preset, DEFORMATION[0])
        self.assertEqual(reports.load(result.report_json)["extensions"][self.gltf._EXTENSION]["version"], "3")

    def test_three_literal_fallbacks_match_all_data_and_existing_freeform_reason(self):
        for key in ("missing_native_receipt_full_report", "unmasked_inexact_full_fallback",
                    "quaternion_source_read_full_fallback"):
            with self.subTest(oracle=key):
                expected = deepcopy(ORACLES[key]); result = LiteralResult(expected)
                actual = self.gltf._project_gltf_fallback(result, job_from_report(expected))
                # The report schema owns this as freeform explanation, not a
                # closed diagnostic code. These planned oracles use specific
                # prose; the existing command uses its truthful generic prose.
                # Normalize exactly this one documented leaf in a local copy;
                # no phase/number/mask/predicate/options/metadata is normalized.
                self.assertIsInstance(expected["execution"]["reason"], str)
                self.assertEqual(actual["execution"]["reason"],
                    "Native execution did not complete; exact reached observations are in the command extension.")
                expected["execution"]["reason"] = actual["execution"]["reason"]
                self.assertEqual(actual, expected)
                reports.validate(actual)
                processing = actual["extensions"][self.gltf._EXTENSION]["processing"]
                self.assertEqual(len(processing), 15)
                self.assertTrue(set(PHASES) <= set(processing))
                self.assertEqual(actual["profile"]["required_checks"], ["native-receipt-validation"])
                self.assertFalse(actual["publication"]["verified"])

    def test_nonempty_receipt_bytes_take_precedence_over_all_fallback_properties(self):
        class BytesOnly(LiteralResult):
            @property
            def deformation_facts(self):
                raise AssertionError("Fallback must not read facts when bytes exist")
            @property
            def deformation_options(self):
                raise AssertionError("Fallback must not read options when bytes exist")
        expected = ORACLES["missing_native_receipt_full_report"]
        result = BytesOnly(expected)
        payload = json.dumps(expected, indent=1).encode()
        result.report_json = payload
        actual, _, code = self.gltf._detach(result, job_from_report(expected))
        self.assertIs(actual, payload)
        self.assertEqual(code, reports.exit_code(expected))

    def test_missing_facts_and_published_without_receipt_are_delivery_errors(self):
        class NoFacts(LiteralResult):
            @property
            def deformation_facts(self):
                return None
        expected = ORACLES["missing_native_receipt_full_report"]
        with self.assertRaises(self.gltf.GltfCommandDeliveryError):
            self.gltf._detach(NoFacts(expected), job_from_report(expected))
        published = deepcopy(expected)
        published["extensions"][self.gltf._EXTENSION]["publication"] = {
            "type": "GltfFilePublicationObservation",
            "outcome": {"domain": "GltfFilePublicationOutcome", "member": "kPublished"},
            "phase": None, "diagnostic": None, "cleanup": None, "cleanup_error": None, "execution": None}
        with self.assertRaises(self.gltf.GltfCommandDeliveryError):
            self.gltf._detach(LiteralResult(published), job_from_report(expected))

    def test_ordered_fail_fast_preserves_attempted_rows_and_clears_remaining(self):
        request = self.request(*PROFILES)
        expected = deepcopy(ORACLES["missing_native_receipt_full_report"])
        expected["input"]["id"] = "job0"
        self.control.results.append(LiteralResult(expected, profile=PROFILES[0]))
        result = self.gltf.run_gltf_jobs(request, base=self.base, fail_fast=True)
        value = reports.load(result.report_json)
        self.assertEqual([row["id"] for row in value["jobs"]], ["job0", "job1", "job2"])
        self.assertEqual([row["outcome"] for row in value["jobs"]], ["attempted", "not-attempted", "not-attempted"])
        self.assertEqual([row[:2] for row in self.control.trace],
                         [("prepare", "job0"), ("prepare", "job1"), ("prepare", "job2"), ("run", "job0")])

    def test_cli_parser_accepts_new_profile_and_presets_lazily(self):
        for preset in DEFORMATION:
            options = self.control.commands._parser().parse_args([
                "gltf", "input.gltf", "output", "--primitive-ordinal", "0",
                "--triangles", "require-triangles", "--values", "raw", "--unchanged",
                "--processing-profile", PROFILES[2], "--preset", preset])
            self.assertEqual((options.processing_profile, options.preset), (PROFILES[2], preset))
        stdout = io.StringIO()
        with patch.dict(sys.modules, {"_gltf_command_control.gltf_commands": None}), redirect_stdout(stdout):
            self.assertEqual(self.control.commands.main(["--version"], stdout=stdout), 0)
        self.assertTrue(stdout.getvalue())

    def test_continue_orders_three_profiles_and_cancellation_skips_remaining(self):
        request = self.request(*PROFILES)
        for index, profile in enumerate(PROFILES):
            expected = deepcopy(ORACLES["missing_native_receipt_full_report"])
            expected["input"]["id"] = f"job{index}"
            self.control.results.append(LiteralResult(expected, profile=profile))
        result = self.gltf.run_gltf_jobs(request, base=self.base)
        value = reports.load(result.report_json)
        self.assertEqual([row["outcome"] for row in value["jobs"]], ["attempted"] * 3)
        self.assertEqual([row["report"]["extensions"][self.gltf._EXTENSION]["version"]
                          for row in value["jobs"]], ["1", "2", "3"])
        self.assertEqual([row[:2] for row in self.control.trace],
                         [("prepare", f"job{i}") for i in range(3)] + [("run", f"job{i}") for i in range(3)])
        control = CommandControl(); token = control.workflow.GltfCancellation()
        expected = deepcopy(ORACLES["missing_native_receipt_full_report"])
        expected["input"]["id"] = "job0"
        control.results = [LiteralResult(expected, profile=PROFILES[0])]
        control.after_run = lambda _: token.cancel()
        result = control.module.run_gltf_jobs(request, base=self.base, cancellation=token)
        self.assertEqual(result.exit_code, 130)
        self.assertEqual([row["outcome"] for row in reports.load(result.report_json)["jobs"]],
                         ["attempted", "not-attempted", "not-attempted"])

    def test_new_fallback_byte_getter_fault_keeps_cancellation_precedence(self):
        class ByteFault(LiteralResult):
            @property
            def report_json(self):
                raise MemoryError("literal byte-getter control")
            @report_json.setter
            def report_json(self, _):
                pass
        expected = ORACLES["missing_native_receipt_full_report"]
        for canceled, code in ((False, 2), (True, 130)):
            result = ByteFault(expected)
            if canceled:
                result.capture_status = Enum("GltfFileStatus", {"kCanceled": 0}).kCanceled
            with self.subTest(canceled=canceled), self.assertRaises(self.gltf.GltfCommandDeliveryError) as caught:
                self.gltf._detach(result, job_from_report(expected))
            self.assertEqual(caught.exception.exit_code, code)

    def test_schema3_inclusive_job_and_raw_request_limits_and_strict_json(self):
        self.assertEqual((self.gltf._MAX_JOBS, self.gltf._MAX_REQUEST_BYTES,
                          self.gltf._MAX_TARGET_PAIRS, self.gltf._MAX_REPORT_BYTES,
                          self.gltf._MAX_FRAMING_BYTES), (16, 8 * 1024**2, 1_000_000, 16 * 1024**2, 8 * 1024**2))
        jobs, _ = self.gltf._admit(self.request(*([PROFILES[2]] * 16)), self.base, None, None)
        self.assertEqual(len(jobs), 16)
        control = CommandControl()
        with self.assertRaises(control.commands.InvocationError):
            control.module._admit(self.request(*([PROFILES[2]] * 17)), self.base, None, None)
        self.assertEqual(control.trace, [])
        path = self.base / "jobs.json"
        raw = json.dumps(self.request(schema=3)).encode()
        path.write_bytes(raw + b" " * (self.gltf._MAX_REQUEST_BYTES - len(raw)))
        self.assertEqual(self.gltf._read_gltf_request(path), self.request(schema=3))
        with path.open("ab") as stream:
            stream.write(b" ")
        with self.assertRaises(self.control.commands.InvocationError):
            self.gltf._read_gltf_request(path)
        for raw in (b'{"schema":"meshvale.repair.gltf-jobs/3","schema":"other","jobs":[]}',
                    b'{"schema":"meshvale.repair.gltf-jobs/3","jobs":[NaN]}'):
            path.write_bytes(raw)
            with self.assertRaises(self.control.commands.InvocationError):
                self.gltf._read_gltf_request(path)

    def test_all_uncalled_slots_stay_null_and_incoherent_views_refuse_delivery(self):
        class Uncalled(LiteralResult):
            @property
            def deformation_facts(self):
                return deepcopy(ORACLES["pre_admission_native_result_projection"]["processing"])
        expected = ORACLES["missing_native_receipt_full_report"]
        actual = self.gltf._project_gltf_fallback(Uncalled(expected), job_from_report(expected))
        self.assertTrue(all(actual["extensions"][self.gltf._EXTENSION]["processing"][slot] is None for slot in PHASES))
        for remove, extra, mismatch in (("reload", False, False), (None, True, False), (None, False, True)):
            class Incoherent(LiteralResult):
                @property
                def deformation_facts(self):
                    value = super().deformation_facts
                    if remove:
                        del value[remove]
                    if extra:
                        value["private_field"] = 1
                    if mismatch:
                        value["normative_revision"]["blob"] = "different"
                    return value
            with self.subTest(remove=remove, extra=extra, mismatch=mismatch), self.assertRaises(self.gltf.GltfCommandDeliveryError):
                self.gltf._detach(Incoherent(expected), job_from_report(expected))

    def test_new_profile_postcandidate_memory_failure_stage_is_verify(self):
        execution = Enum("GltfWorkflowExecutionOutcome", {"kFailed": 0})
        candidate = Enum("CandidateOutcome", {"accepted": 0})
        publication = Enum("GltfFileWorkflowPublicationOutcome", {"kNotAttempted": 0})
        outer = Enum("GltfFileWorkflowStage", {"kMemory": 0})
        inner = Enum("GltfWorkflowStage", {"kWrite": 0, "kRepair": 1})
        result = types.SimpleNamespace(workflow_started=True,
            outcomes=types.SimpleNamespace(execution=execution.kFailed, candidate=candidate.accepted,
                publication=publication.kNotAttempted, stage=outer.kMemory),
            memory_outcomes=types.SimpleNamespace(stage=inner.kWrite))
        self.assertEqual(self.gltf._core_outcomes(result, PROFILES[2])[1], "verify")
        self.assertEqual(self.gltf._core_outcomes(result, PROFILES[0])[1], "export")
        result.memory_outcomes.stage = inner.kRepair
        self.assertEqual(self.gltf._core_outcomes(result, PROFILES[2])[1], "verify")
        self.assertEqual(self.gltf._core_outcomes(result, PROFILES[0])[1], "repair")


if __name__ == "__main__":
    unittest.main(verbosity=2)
