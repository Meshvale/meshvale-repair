# SPDX-License-Identifier: Apache-2.0
"""Real file/command composition and publication facts for triangle export."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import meshvale_geometry as geometry
import meshvale_interchange as io
from meshvale_reports import exit_code, load, validate
from meshvale_repair import commands, obj_workflow
from meshvale_repair import _obj_export as export_module
from test_preparation import FACE_MAP, CORNER_MAP


FIXTURE = runpy.run_path(str(Path(__file__).resolve().parents[1] / "obj-corpus/test_obj_corpus.py"))


def inventory(directory):
    return {p.relative_to(directory).as_posix(): p.read_bytes()
            for p in directory.rglob("*") if p.is_file()}


class TriangleWorkflowTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="meshvale-obj-triangles-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "outputs").mkdir()
        self.source = self.fixture("panel", FIXTURE["BASE"] + FIXTURE["CYCLIC"])

    def fixture(self, name, text):
        root = self.root / "sources" / name
        root.mkdir(parents=True)
        (root / "model.obj").write_text(text, encoding="ascii")
        (root / "panel.mtl").write_bytes(FIXTURE["MTL"])
        (root / "checker.ppm").write_bytes(FIXTURE["TEXTURE"])
        return root / "model.obj"

    def run_file(self, name, targets=((1, 3),), *, source=None, expected=0, **options):
        before = inventory(self.root / "sources")
        result = obj_workflow.repair_obj_file(source or self.source, self.root / "outputs" / name,
                                              targets, input_id="panel", output_topology="triangles", **options)
        validate(result.report)
        self.assertEqual(result.exit_code, expected, result.report)
        self.assertEqual(exit_code(result.report), expected)
        self.assertEqual(inventory(self.root / "sources"), before)
        self.assertFalse(list((self.root / "outputs").glob(".meshvale-stage-*")))
        if result.report["publication"]["outcome"] == "published":
            receipt = self.root / "outputs" / name / "meshvale-report.json"
            bundled = load(receipt.read_text(encoding="ascii"))
            validate(bundled)
            if result.report["execution"]["outcome"] == "completed":
                self.assertEqual(bundled, result.report)
            else:
                self.assertEqual(bundled["publication"], result.report["publication"])
            extension = result.report["extensions"]["meshvale.repair.obj"]
            paths = {row["id"]: row["path"] for row in extension["artifacts"]}
            for artifact in result.report["publication"]["artifacts"]:
                self.assertEqual(hashlib.sha256((receipt.parent / paths[artifact["id"]]).read_bytes()).hexdigest(), artifact["sha256"])
            for resource in result.source.resources:
                self.assertEqual((receipt.parent / resource.path).read_bytes(), resource.bytes)
        return result

    def native(self, report):
        return report["extensions"]["meshvale.repair.obj"]["triangulation"]["native"]

    def test_actual_candidate_maps_reload_and_geometric_coverage(self):
        for name, suffix, repair_corners in (("cyclic", FIXTURE["CYCLIC"], [5, 6, 3, 4]),
                                              ("exact", FIXTURE["EXACT"], [3, 4, 5, 6])):
            with self.subTest(name=name):
                source = self.fixture(name, FIXTURE["BASE"] + suffix)
                result = self.run_file(name, source=source)
                self.assertEqual(result.candidate.document.mesh.face_count, 3)
                self.assertEqual(result.report["candidate"]["outcome"], "accepted")
                maps = {row["id"]: row for row in result.report["maps"]}
                self.assertEqual(maps["repair-face"]["entries"], [0, 1, 2, 1])
                self.assertEqual(maps["repair-corner"]["entries"], list(range(12)) + repair_corners)
                self.assertEqual(maps["export-face"]["entries"], FACE_MAP)
                self.assertEqual(maps["export-corner"]["entries"], CORNER_MAP)
                output = io.read_obj_file(self.root / "outputs" / name / "model.obj").asset
                record = output.document.mesh.to_record()
                self.assertEqual(list(record["face_offsets"]), [0, 3, 6, 9, 12, 15, 18])
                self.assertEqual(list(record["positions"]), FIXTURE["POSITIONS"])
                self.assertEqual(output.document.material_names, ("red", "blue", "green"))
                self.assertEqual([list(row["values"]) for row in record["attributes"] if row["domain"] == "face"],
                                 [[0, 1, 1, 2, 2, 2], [0, 1, 1, 2, 2, 2], [0] * 6])
                original = result.candidate.document.mesh.to_record()
                points = [tuple(int(v) for v in record["positions"][3*i:3*i+2]) for i in range(10)]
                def area2(loop):
                    return sum(points[a][0]*points[b][1] - points[b][0]*points[a][1]
                               for a, b in zip(loop, loop[1:] + loop[:1]))
                for source_face, destinations in enumerate(FACE_MAP):
                    destinations = [destinations] if type(destinations) is int else destinations
                    start, end = original["face_offsets"][source_face:source_face + 2]
                    polygon = list(original["corner_vertices"])[start:end]
                    triangles = [list(record["corner_vertices"])[3*i:3*i+3] for i in destinations]
                    self.assertTrue(all(area2(loop) > 0 for loop in triangles))
                    self.assertEqual(sum(area2(loop) for loop in triangles), area2(polygon))
                    edges = {}
                    for loop in triangles:
                        for a, b in zip(loop, loop[1:] + loop[:1]):
                            reverse = (b, a)
                            if edges.get(reverse):
                                edges[reverse] -= 1
                                if not edges[reverse]: del edges[reverse]
                            else: edges[(a, b)] = edges.get((a, b), 0) + 1
                    self.assertEqual(edges, {(a, b): 1 for a, b in zip(polygon, polygon[1:] + polygon[:1])})

    def test_empty_targets_clean_source_are_unchanged_repair_with_converted_output(self):
        source = self.fixture("clean", FIXTURE["BASE"])
        result = self.run_file("clean", (), source=source)
        self.assertEqual(result.report["candidate"]["outcome"], "unchanged")
        self.assertEqual(result.candidate.document.mesh.face_count, 3)
        self.assertEqual(result.report["snapshots"][-1]["meshes"][0]["face_count"], 6)
        self.assertTrue(all(row["kind"] == "identity" for row in result.report["maps"] if row["id"].startswith("repair-")))

    def test_triangle_and_empty_identity(self):
        for name, text in (("triangle", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), ("empty", "# Original empty mesh\n")):
            with self.subTest(name=name):
                result = self.run_file(name, (), source=self.fixture(name, text))
                self.assertTrue(all(row["kind"] == "identity" for row in result.report["maps"]))
                self.assertEqual(result.report["candidate"]["outcome"], "unchanged")

    def test_refusal_diagnostics_scope_repaired_candidate(self):
        text = FIXTURE["BASE"].replace("g quad\n", "g triangle\nusemtl red\nf 1/1/1 2/2/1 3/3/1\ng quad\n", 1)
        source = self.fixture("nonplanar", text.replace("v 2 1 0\n", "v 2 1 0.25\n"))
        result = self.run_file("nonplanar", ((0, 1),), source=source, expected=1)
        self.assertEqual(result.candidate.document.mesh.face_count, 3)
        self.assertEqual(result.report["execution"], {"outcome": "completed", "stage": "complete", "reason": None})
        diagnostic, = [d for d in result.report["diagnostics"] if d["code"] == "conversion.nonplanar_face"]
        self.assertEqual(diagnostic["scope"]["snapshot"], "candidate")
        self.assertEqual(diagnostic["data"]["native_element"], 1)
        self.assertEqual(result.report["publication"]["outcome"], "not-attempted")
        self.assertFalse((self.root / "outputs/nonplanar").exists())

    def test_corner_and_native_payload_limits(self):
        corner = self.run_file("corner", expected=1, triangulation_options=geometry.TriangulationOptions(max_corners_per_face=3))
        self.assertEqual(corner.report["candidate"]["outcome"], "accepted")
        budget = self.run_file("budget", expected=2, triangulation_execution=geometry.ExecutionContext(tracked_payload_budget_bytes=0))
        self.assertEqual(budget.report["execution"]["stage"], "export")
        self.assertEqual(budget.report["publication"]["outcome"], "not-attempted")
        self.assertEqual(self.native(budget.report)["status"], "blocked")

    def test_crossing_and_degenerate_native_refusals_retain_repair_facts(self):
        cases = (("crossing", "v 0 0 0\nv 2 2 0\nv 0 2 0\nv 2 0 0\nf 1 2 3 4\n", "conversion.self_intersection"),
                 ("degenerate", "v 0 0 0\nv 1 1 0\nv 2 2 0\nf 1 2 3\n", "conversion.degenerate_face"))
        for name, text, diagnostic in cases:
            with self.subTest(name=name):
                result = self.run_file(name, (), source=self.fixture(name, text), expected=1)
                self.assertIsNotNone(result.candidate)
                self.assertEqual(result.report["candidate"]["outcome"], "unchanged")
                self.assertEqual(result.report["execution"], {"outcome": "completed", "stage": "complete", "reason": None})
                self.assertIn(diagnostic, [row["code"] for row in result.report["diagnostics"]])
                self.assertEqual(result.report["publication"]["outcome"], "not-attempted")
                self.assertTrue(all(row["id"].startswith("repair-") for row in result.report["maps"]))
                self.assertFalse(any(row["role"] == "output" for row in result.report["snapshots"]))
                self.assertFalse((self.root / "outputs" / name).exists())

    def test_native_cancellation_survives_diagnostic_allocation_failure(self):
        # Deliberately inject a stop at the native-call seam and an allocation
        # fault after that genuine canceled result; no native status is invented.
        def actual_canceled(request, mesh, cancellation):
            token = geometry.Cancellation(); token.request_stop()
            return geometry.triangulate(mesh, options=request.options, execution=request.execution, cancellation=token)
        original = obj_workflow._diagnostics
        def allocation(report, rows, snapshot, severity):
            if any(row["code"] == "conversion.cancelled" for row in rows):
                raise MemoryError("diagnostic allocation control")
            return original(report, rows, snapshot, severity)
        with patch.object(export_module.TriangleRequest, "convert", new=actual_canceled), \
                patch.object(obj_workflow, "_diagnostics", side_effect=allocation):
            result = self.run_file("cancel-allocation", expected=130)
        self.assertIsNotNone(result.candidate)
        self.assertEqual(self.native(result.report)["status"], "canceled")
        self.assertEqual(result.report["execution"]["outcome"], "cancelled")
        self.assertEqual(result.report["execution"]["stage"], "export")
        self.assertEqual(result.report["publication"]["outcome"], "not-attempted")
        self.assertFalse((self.root / "outputs/cancel-allocation").exists())

    def test_missing_malformed_and_rejected_have_no_native_result(self):
        for name, source, targets, code in (("missing", self.root / "absent.obj", (), 2),
                ("malformed", self.fixture("malformed", FIXTURE["BASE"].replace("f 1/1/1", "f 99/1/1")), (), 2),
                ("rejected", self.source, ((0, 1),), 1)):
            with self.subTest(name=name):
                result = self.run_file(name, targets, source=source, expected=code)
                self.assertIsNone(self.native(result.report))

    def test_coupled_prestop_and_io_phase_stop(self):
        for phase in (None, "preflight", "staging", "verification", "publication"):
            with self.subTest(phase=phase):
                token = obj_workflow.ObjCancellation()
                if phase is None: token.request_stop()
                def stop(current):
                    if current == phase: token.request_stop()
                result = self.run_file(str(phase), expected=130, cancellation=token, on_phase=stop)
                self.assertFalse((self.root / "outputs" / str(phase)).exists())
                if phase is None: self.assertIsNone(self.native(result.report))
                else: self.assertEqual(self.native(result.report)["status"], "accepted")

    def test_stop_after_native_conversion_preserves_native_facts(self):
        token = obj_workflow.ObjCancellation()
        original = obj_workflow.prepare_triangle_asset
        def stop(candidate, native):
            plan = original(candidate, native)
            token.request_stop()
            return plan
        with patch.object(obj_workflow, "prepare_triangle_asset", side_effect=stop):
            result = self.run_file("between", expected=130, cancellation=token)
        self.assertEqual(self.native(result.report)["status"], "accepted")
        self.assertIsNotNone(result.candidate)
        self.assertEqual(result.report["publication"]["outcome"], "not-attempted")

    def test_real_mid_conversion_stop_and_gil_release(self):
        source = self.fixture("large", "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n" + "f 1 2 3 4\n" * 40000)
        execution = geometry.ExecutionContext(worker_budget=2)
        token = obj_workflow.ObjCancellation()
        done, observed = threading.Event(), threading.Event()
        def stop():
            deadline = time.monotonic() + 30
            while not done.is_set() and time.monotonic() < deadline:
                if execution.active_workers:
                    observed.set(); token.request_stop(); return
                time.sleep(.001)
        thread = threading.Thread(target=stop)
        thread.start()
        try:
            result = self.run_file("midcall", (), source=source, expected=130, cancellation=token,
                                   triangulation_execution=execution,
                                   triangulation_options=geometry.TriangulationOptions(minimum_parallel_faces=0))
        finally:
            done.set(); thread.join(5)
        self.assertFalse(thread.is_alive())
        self.assertTrue(observed.is_set(), "Another Python thread never observed actual native workers")
        self.assertEqual(self.native(result.report)["status"], "canceled")
        self.assertIsNotNone(result.candidate)
        self.assertEqual((execution.active_workers, execution.active_tracked_payload_bytes), (0, 0))

    def test_real_worker_choices_and_shared_context_reuse(self):
        records = []
        for workers in (1, 2):
            execution = geometry.ExecutionContext(worker_budget=workers)
            for repetition in range(2):
                name = f"workers-{workers}-{repetition}"
                result = self.run_file(name, triangulation_execution=execution,
                                       triangulation_options=geometry.TriangulationOptions(minimum_parallel_faces=0))
                self.assertEqual(self.native(result.report)["workers_used"], 0 if workers == 1 else 2)
                self.assertEqual((execution.active_workers, execution.active_tracked_payload_bytes), (0, 0))
                records.append(io.read_obj_file(self.root / "outputs" / name / "model.obj").asset.document.mesh.to_record())
        self.assertTrue(all(record == records[0] for record in records))

    def test_closed_invocation_before_capture_and_legacy_tokens(self):
        token = io.Cancellation()
        with patch.object(io, "read_obj_file", side_effect=AssertionError("capture before admission")) as reader:
            for options in ({"output_topology": "unknown"}, {"output_topology": "triangles", "cancellation": token},
                            {"triangulation_options": geometry.TriangulationOptions()},
                            {"output_topology": "triangles", "triangulation_execution": False}):
                with self.subTest(options=options), self.assertRaises((TypeError, ValueError)):
                    obj_workflow.repair_obj_file(self.source, self.root / "outputs/invalid", [], **options)
            bad = {"schema": "meshvale.repair.obj-jobs/2", "jobs": [
                {"id": "first", "input": str(self.source), "destination": "outputs/first", "targets": [], "output_topology": "triangles"},
                {"id": "last", "input": str(self.source), "destination": "outputs/last", "targets": [], "unknown": True}]}
            with self.assertRaises(commands.InvocationError): commands.run_obj_jobs(bad, base=self.root)
            bad["jobs"] = bad["jobs"][:1]; bad["schema"] = "meshvale.repair.obj-jobs/1"
            with self.assertRaises(commands.InvocationError): commands.run_obj_jobs(bad, base=self.root)
            reader.assert_not_called()
        result = obj_workflow.repair_obj_file(self.source, self.root / "outputs/legacy", [], cancellation=token)
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.report["extensions"]["meshvale.repair.obj"]["version"], "1")

    def test_existing_destination_and_staged_failure_publish_no_triangle_bundle(self):
        directory = self.root / "outputs/existing"; directory.mkdir(); (directory / "marker").write_bytes(b"retained")
        result = self.run_file("existing", expected=2)
        self.assertEqual(inventory(directory), {"marker": b"retained"})
        self.assertEqual(result.report["candidate"]["outcome"], "accepted")
        def corrupt(phase):
            if phase == "verification":
                stage, = (self.root / "outputs").glob(".meshvale-stage-*")
                (stage / "checker.ppm").write_bytes(b"deliberate staged-readback fault control")
        result = self.run_file("corrupt", expected=2, on_phase=corrupt)
        self.assertFalse((self.root / "outputs/corrupt").exists())
        self.assertFalse(any(row["role"] == "output" for row in result.report["snapshots"]))

    def test_mixed_v2_batch_continue_and_fail_fast(self):
        source = self.fixture("nonplanar-batch", FIXTURE["BASE"].replace("v 2 1 0\n", "v 2 1 0.25\n"))
        before = inventory(self.root / "sources")
        for fail_fast, prefix in ((False, "continue-"), (True, "stop-")):
            jobs = [{"id": name, "input": str(path), "destination": "outputs/" + prefix + name,
                     "targets": targets, "output_topology": "triangles"}
                    for name, path, targets in (("first", self.source, [[1, 3]]),
                                               ("refused", source, []), ("last", self.source, [[1, 3]]))]
            batch = commands.run_obj_jobs({"schema": "meshvale.repair.obj-jobs/2", "jobs": jobs},
                                          base=self.root, fail_fast=fail_fast)
            validate(batch)
            self.assertEqual(batch["exit_code"], 1)
            self.assertEqual([row["outcome"] for row in batch["jobs"]],
                             ["attempted", "attempted", "not-attempted" if fail_fast else "attempted"])
            self.assertEqual([exit_code(row["report"]) for row in batch["jobs"] if row["outcome"] == "attempted"],
                             [0, 1] if fail_fast else [0, 1, 0])
            self.assertFalse((self.root / "outputs" / (prefix + "refused")).exists())
            self.assertEqual((self.root / "outputs" / (prefix + "last")).exists(), not fail_fast)
        self.assertEqual(inventory(self.root / "sources"), before)

    def test_sink_failure_after_commit_keeps_receipt_and_candidate(self):
        def sink(report):
            raise OSError("delivery control")
        result = self.run_file("delivery", expected=2, report_sink=sink)
        self.assertEqual(result.report["publication"]["outcome"], "published")
        self.assertEqual(result.report["execution"]["stage"], "report")
        self.assertEqual(result.candidate.document.mesh.face_count, 3)
        receipt = load((self.root / "outputs/delivery/meshvale-report.json").read_text(encoding="ascii"))
        self.assertEqual(receipt["execution"]["outcome"], "completed")
        self.assertNotEqual(result.report, receipt)

    def test_actual_console_and_module_batch_match_api(self):
        execution = geometry.ExecutionContext(worker_budget=2)
        api = self.run_file("api", triangulation_execution=execution)
        console = Path(sys.executable).parent / ("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        single = subprocess.run([str(console), "obj", str(self.source), str(self.root / "outputs/single"),
            "--target", "1:3", "--input-id", "panel", "--output-topology", "triangles", "--triangulation-workers", "2"],
            cwd=self.root, capture_output=True, text=True, timeout=60, check=False)
        self.assertEqual(single.returncode, 0, single.stderr)
        self.assertEqual(load(single.stdout), api.report)
        request = {"schema": "meshvale.repair.obj-jobs/2", "jobs": [{"id": "panel", "input": str(self.source),
            "destination": "outputs/batch", "targets": [[1, 3]], "output_topology": "triangles", "triangulation": {"worker_budget": 2}}]}
        (self.root / "jobs.json").write_text(json.dumps(request), encoding="utf-8")
        batch = subprocess.run([sys.executable, "-I", "-m", "meshvale_repair", "obj-batch", "jobs.json"],
                               cwd=self.root, capture_output=True, text=True, timeout=60, check=False)
        self.assertEqual(batch.returncode, 0, batch.stderr)
        self.assertEqual(load(batch.stdout)["jobs"][0]["report"], api.report)
        self.assertEqual(inventory(self.root / "outputs/api"), inventory(self.root / "outputs/single"))
        self.assertEqual(inventory(self.root / "outputs/api"), inventory(self.root / "outputs/batch"))


if __name__ == "__main__":
    unittest.main()
