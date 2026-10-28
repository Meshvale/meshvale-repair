# SPDX-License-Identifier: Apache-2.0
"""Installed SUR-01 API integration; genuine native owners and physical output.

Run from any cwd with the installed opt-in extension, for example:
python -I -m unittest discover -s /absolute/tests/off-glb-python -v
The qualified numerical producer is required; refusals never skip to green.
"""
import gc
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import meshvale_reports as reports
from meshvale_repair import off_surface_export as workflow
from off_glb_fixture import (MAPS, O_CORNERS, O_OFFSETS, OUTPUT_MAPS, POLICIES,
    R_CORNERS, R_OFFSETS, enum, fixture, mesh, payload, physical, positions,
    readonly, repair_maps, sha, source_bytes, topology, triangulation)


class OffGlbApi(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="meshvale-off-glb-api-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve()

    def test_isolated_installed_import_has_no_shared_record_registration_warnings(self):
        # These exports may alias existing immutable record types. Their Python
        # class names need not change; the declared readonly fields must remain.
        script = """
import inspect
from meshvale_repair import _repair
from meshvale_repair.off_surface_export import OffGlbExecution, OffCancellation

fields = {
    "OffGlbGeometryMaterializationDiagnostic": ("code", "attribute", "element", "storage_issue"),
    "OffGlbGeometryStorageIssue": ("code", "attribute", "element"),
    "OffGlbMaterializedDuplicateDiagnostic": ("stage", "code", "target", "attribute", "element",
        "geometry_status", "geometry_diagnostic", "storage_issue"),
    "OffGlbGltfDiagnostic": ("status", "code", "json_span", "resource_kind", "resource_index",
        "resource_span", "rule_id"),
    "OffGlbGltfSourceSpan": ("offset", "length"),
    "OffGlbSystemErrorCode": ("category", "value"),
}
for exported, names in fields.items():
    record = getattr(_repair, exported)
    for name in names:
        descriptor = inspect.getattr_static(record, name)
        assert isinstance(descriptor, property), (exported, name)
        assert descriptor.fget is not None and descriptor.fset is None, (exported, name)
"""
        process = subprocess.run([sys.executable, "-I", "-W", "error", "-c", script],
            cwd=self.root, capture_output=True, timeout=30, check=False)
        self.assertEqual(process.returncode, 0, process.stderr.decode("utf-8", errors="replace"))
        self.assertEqual(process.stdout, b"")
        self.assertEqual(process.stderr, b"")

    def call(self, name="mixed", targets=((0, 1),), **options):
        source = fixture(self.root / name, name)
        destination = self.root / (name + "-output")
        result = workflow.repair_off_to_glb(source, destination, targets,
                                           **(POLICIES | options))
        self.assertEqual(source.read_bytes(), source_bytes(name))
        return result, destination

    def off_facts(self, result, changed=True):
        enum(self, result.capture.status, "kSuccess")
        enum(self, result.off.status, "kSuccess")
        enum(self, result.off.outcomes.candidate, "accepted" if changed else "unchanged")
        enum(self, result.off.outcomes.publication, "kNotRequested")
        self.assertIs(type(result.off_report_bytes), bytes)
        report = reports.load(result.off_report_bytes)
        self.assertEqual(report["input"]["sha256"], sha(source_bytes("mixed")))
        self.assertEqual(report["publication"]["artifacts"], [])
        self.assertEqual(reports.exit_code(report), 0)
        repair_maps(self, result, changed)

    def success(self, result, destination, *, name="mixed", changed=True, vertices=14):
        self.assertEqual(result.exit, 0)
        enum(self, result.native.status, "kSuccess")
        enum(self, result.native.diagnostic.stage, "kComplete")
        enum(self, result.native.diagnostic.code, "kNone")
        self.assertIsNotNone(result.proof)
        self.assertIsNotNone(result.published)
        enum(self, result.publication.outcome, "kPublished")
        enum(self, result.publication.facts.commit.outcome, "kCommitted")
        enum(self, result.publication.facts.inspection.status, "kSuccess")
        self.assertIsNot(result.proof, result.published.inspection)
        self.assertTrue(result.candidate_inspection.storage_valid)
        self.assertTrue(result.triangulated_inspection.storage_valid)
        self.assertEqual(result.candidate_inspection.issues, ())
        self.assertEqual(result.triangulated_inspection.issues, ())
        files = physical(destination)
        payload(self, files, result=result, changed=changed, name=name, vertices=vertices)
        for inspected in (result.proof, result.published.inspection):
            self.assertEqual({m.path: bytes(m.bytes) for m in inspected.captured_members},
                             {k: v for k, v in files.items() if k != "publication.json"})
        self.assertEqual({m.path: bytes(m.bytes) for m in result.output.members},
                         {k: v for k, v in files.items() if k != "publication.json"})
        for member in result.output.members:
            readonly(self, member.bytes, "B")
            self.assertEqual(member.sha256.hex(), sha(files[member.path]))
        receipt = result.published.receipt
        self.assertEqual((receipt.path, bytes(receipt.bytes)), ("publication.json", files["publication.json"]))
        self.assertEqual(receipt.sha256.hex(), sha(files["publication.json"]))
        offsets, corners = ((R_OFFSETS, R_CORNERS) if changed and name in ("mixed", "mixed-shift")
                            else topology(name))
        faces = len(offsets) - 1
        triangles = sum(offsets[i + 1] - offsets[i] - 2 for i in range(faces))
        for facts in (result.encoding.facts, result.inspection.facts,
                      result.published.inspection.facts, result.publication.facts.inspection.facts):
            self.assertEqual((facts.original_counts.vertices, facts.original_counts.faces,
                facts.original_counts.corners), (vertices, faces, len(corners)))
            self.assertEqual((facts.triangle_counts.vertices, facts.triangle_counts.faces,
                facts.triangle_counts.corners), (vertices, triangles, 3 * triangles))
            self.assertEqual((facts.wire_counts.vertices, facts.wire_counts.triangles,
                facts.wire_counts.corners), (3 * triangles, triangles, 3 * triangles))
            summary = facts.position_numeric
            enum(self, summary.kind, "kDecodedWire")
            self.assertTrue(summary.complete)
            self.assertEqual(summary.examined_values, 3 * len(set(corners)))
            self.assertEqual(summary.changed_values, 1 if name == "mixed-shift" else 0)
            self.assertEqual(facts.uv_numeric, ())
            error = summary.max_absolute_error
            self.assertEqual((error.used_words, error.exponent2), (1, -30) if name == "mixed-shift" else (0, 0))
            self.assertEqual(tuple(error.magnitude), (1 if name == "mixed-shift" else 0,) +
                             (0,) * (len(error.magnitude) - 1))

    def test_literal_changed_and_empty_targets_both_presets(self):
        for preset in ("balanced-v1", "serial-reference-v1"):
            for changed in (True, False):
                with self.subTest(preset=preset, changed=changed):
                    root = self.root / preset / str(changed)
                    source = fixture(root)
                    destination = root / "out"
                    result = workflow.repair_off_to_glb(source, destination,
                        ((0, 1),) if changed else (), **POLICIES, preset=preset)
                    self.off_facts(result, changed)
                    mesh(self, result.source_mesh, positions(), O_OFFSETS, O_CORNERS)
                    mesh(self, result.candidate, positions(), R_OFFSETS if changed else O_OFFSETS,
                         R_CORNERS if changed else O_CORNERS)
                    triangulation(self, result, changed)
                    self.success(result, destination, changed=changed)
                    self.assertEqual(source.read_bytes(), source_bytes())

    def test_empty_and_isolated_signed_zero_are_real_distinct_domains(self):
        for name, count in (("empty", 0), ("points", 2)):
            with self.subTest(name=name):
                result, destination = self.call(name, targets=())
                self.success(result, destination, name=name, vertices=count)
                mesh(self, result.source_mesh, positions(name), (0,), ())
                mesh(self, result.candidate, positions(name), (0,), ())
                mesh(self, result.triangulated_mesh, positions(name), (0,), ())
                for getter in (*MAPS, "triangulation_face_sources", "triangulation_corner_sources"):
                    self.assertEqual(readonly(self, getattr(result, getter), "Q"), ())
                self.assertEqual(tuple(result.triangulation_face_output_offsets), (0,))
                self.assertEqual(result.repair_vertex_identity, range(count))
                self.assertEqual(result.triangulation_vertex_identity, range(count))
                self.assertEqual(tuple(result.output.source_vertex_offsets), (0,) * (count + 1))

    def test_numeric_refusals_retain_real_repair_report_and_candidate(self):
        cases = (("mixed-shift", "exact32", 0, "kExactness", ((0, 1),)),
            ("mixed-shift", "bounded-rne32", math.nextafter(2.0 ** -30, 0.0), "kErrorBound", ((0, 1),)),
            ("new-coincidence", "bounded-rne32", 2.0 ** -30, "kNewCoincidence", ()),
            ("collapse", "bounded-rne32", 2.0 ** -30, "kCollapsedTriangle", ()),
            ("orientation", "bounded-rne32", float.fromhex("0x1.8p-24"), "kOrientation", ()),
            ("range", "bounded-rne32", 2.0 ** 128, "kNumericRange", ()),
            ("underflow", "bounded-rne32", 2.0 ** -150, "kNonzeroUnderflow", ()))
        for index, (name, precision, bound, code, targets) in enumerate(cases):
            with self.subTest(name=name, code=code):
                source = fixture(self.root / str(index), name)
                destination = self.root / ("refused-" + str(index))
                result = workflow.repair_off_to_glb(source, destination, targets,
                    **(POLICIES | dict(precision=precision, max_position_error=bound)))
                self.assertEqual(result.exit, 1)
                enum(self, result.native.status, "kUnsupported")
                enum(self, result.native.diagnostic.stage, "kEncoding")
                enum(self, result.encoding.status, "kUnsupported")
                enum(self, result.encoding.diagnostic.code, code)
                self.assertIsNotNone(result.encoding.facts)
                enum(self, result.off.status, "kSuccess")
                self.assertIsNotNone(result.candidate)
                self.assertIsNotNone(result.triangulated_mesh)
                retained_report = reports.load(result.off_report_bytes)
                self.assertEqual(reports.exit_code(retained_report), 0)
                self.assertEqual(retained_report["input"]["sha256"], sha(source_bytes(name)))
                self.assertEqual(retained_report["publication"]["artifacts"], [])
                enum(self, result.off.outcomes.publication, "kNotRequested")
                expected_offsets, expected_corners = ((R_OFFSETS, R_CORNERS)
                    if name == "mixed-shift" else topology(name))
                mesh(self, result.candidate, positions(name), expected_offsets, expected_corners)
                if name == "mixed-shift":
                    repair_maps(self, result)
                for getter in MAPS:
                    readonly(self, getattr(result, getter), "Q")
                for getter in ("output", "proof", "inspection", "published", "publication",
                               "publication_receipt_bytes"):
                    self.assertIsNone(getattr(result, getter))
                self.assertFalse(destination.exists())
                self.assertEqual(source.read_bytes(), source_bytes(name))

    def test_bounded_rounding_and_exact_subnormal_have_actual_proofs(self):
        result, destination = self.call("mixed-shift", precision="bounded-rne32",
                                        max_position_error=2.0 ** -30)
        repair_maps(self, result)
        triangulation(self, result, name="mixed-shift")
        self.success(result, destination, name="mixed-shift")
        # The capsule retains the F64 value; the independent wire reader narrows it.
        self.assertEqual(result.candidate.positions[15], 5.0 + 2.0 ** -30)
        result, destination = self.call("subnormal", targets=())
        self.success(result, destination, name="subnormal", changed=False, vertices=3)

    def test_distinct_coincident_vertex_ids_survive_unchanged_export(self):
        result, destination = self.call("different-ids", targets=())
        self.success(result, destination, name="different-ids", changed=False, vertices=8)
        self.assertEqual(tuple(result.repair_face_map), (0, 1))
        self.assertEqual(result.repair_vertex_identity, range(8))
        self.assertEqual(set(result.output.wire_vertex_sources), set(range(8)))
        offsets = tuple(result.output.source_vertex_offsets)
        # Separate IDs keep separate nonempty CSR rows even when XYZ coincide.
        self.assertGreater(offsets[1], offsets[0])
        self.assertGreater(offsets[5], offsets[4])

    def test_geometric_rejections_are_not_repaired_partially(self):
        for name, targets, code, target in (("reversed", ((0, 1),), "kNotEquivalent", 0),
            ("different-ids", ((0, 1),), "kNotEquivalent", 0),
            ("mixed", ((0, 1), (2, 4)), "kTargetRange", 1)):
            with self.subTest(name=name):
                result, destination = self.call(name, targets=targets)
                self.assertEqual(result.exit, 1)
                enum(self, result.native.status, "kRejected")
                enum(self, result.native.diagnostic.stage, "kOffWorkflow")
                enum(self, result.off.status, "kRejected")
                enum(self, result.off.diagnostic.stage, "kRepair")
                enum(self, result.off.diagnostic.delegated.code, code)
                self.assertEqual(result.off.diagnostic.delegated.target, target)
                self.assertIsNotNone(result.source_mesh)
                for getter in ("candidate", "triangulation", "output", "proof", "published", *MAPS):
                    self.assertIsNone(getattr(result, getter))
                self.assertFalse(destination.exists())

    def test_nonplanar_conversion_blocks_after_accepted_repair(self):
        result, destination = self.call("nonplanar")
        self.assertEqual(result.exit, 1)
        enum(self, result.native.diagnostic.stage, "kTriangulation")
        enum(self, result.triangulation.status, "kBlocked")
        enum(self, result.triangulation.diagnostic.code, "kNonplanarFace")
        self.assertEqual(result.triangulation.diagnostic.face, 0)
        self.assertIsNotNone(result.candidate)
        repair_maps(self, result)
        self.assertIsNotNone(result.off_report_bytes)
        self.assertIsNone(result.triangulated_mesh)
        self.assertIsNone(result.encoding)
        self.assertFalse(destination.exists())

    def test_existing_destination_retains_memory_proof_and_all_off_facts(self):
        source = fixture(self.root / "input")
        destination = self.root / "out"
        destination.mkdir()
        (destination / "sentinel").write_bytes(b"keep")
        result = workflow.repair_off_to_glb(source, destination, ((0, 1),), **POLICIES)
        self.assertEqual(result.exit, 2)
        enum(self, result.native.diagnostic.stage, "kPublication")
        self.off_facts(result)
        self.assertIsNotNone(result.proof)
        self.assertIsNotNone(result.publication)
        self.assertIsNone(result.published)
        self.assertIsNone(result.publication_receipt_bytes)
        self.assertEqual(physical(destination), {"sentinel": b"keep"})

    def test_prepare_owns_targets_freezes_cwd_and_runs_fresh_capture(self):
        source = fixture(self.root / "first")
        elsewhere = self.root / "elsewhere"
        elsewhere.mkdir()
        previous = Path.cwd()
        self.addCleanup(os.chdir, previous)
        os.chdir(source.parent)
        targets = [[0, 1]]
        prepared = workflow.prepare_off_glb_export("source.off", "out", targets, **POLICIES)
        targets[0][1] = 99
        self.assertFalse((source.parent / "out").exists())
        os.chdir(elsewhere)
        first = workflow.run_off_glb_export(prepared)
        self.success(first, source.parent / "out")
        again = workflow.run_off_glb_export(prepared)
        self.assertEqual(again.exit, 2)
        self.assertIsNotNone(again.proof)
        self.assertIsNone(again.published)
        source.unlink()
        second = workflow.run_off_glb_export(prepared)
        self.assertEqual(second.exit, 2)
        self.assertIsNone(second.native)
        self.assertIsNone(second.source_mesh)
        self.assertEqual(first.exit, 0)

    @unittest.skipUnless(os.name == "nt", "Windows drive-relative path contract")
    def test_windows_drive_relative_path_freezes_preparation_cwd(self):
        source = fixture(self.root / "anchor")
        elsewhere = self.root / "later"
        elsewhere.mkdir()
        previous = Path.cwd()
        self.addCleanup(os.chdir, previous)
        os.chdir(source.parent)
        drive = source.drive
        prepared = workflow.prepare_off_glb_export(drive + "source.off", "out", ((0, 1),), **POLICIES)
        os.chdir(elsewhere)
        result = workflow.run_off_glb_export(prepared)
        self.success(result, source.parent / "out")

    def other_drive_directory(self):
        """Own a temporary directory on a different actual writable drive."""
        for drive in os.listdrives():
            if os.path.splitdrive(drive)[0].casefold() == self.root.drive.casefold():
                continue
            base = Path(os.path.abspath(os.path.splitdrive(drive)[0] + "."))
            try:
                alternate = tempfile.TemporaryDirectory(prefix="meshvale-off-glb-drive-", dir=base)
            except OSError:
                continue
            self.addCleanup(alternate.cleanup)
            return Path(alternate.name).resolve()
        self.skipTest("No second writable real drive is available")

    @unittest.skipUnless(os.name == "nt", "Windows root-relative current-drive contract")
    def test_windows_root_relative_paths_freeze_drive_before_actual_drive_switch(self):
        previous = Path.cwd()
        other = self.other_drive_directory()
        self.addCleanup(os.chdir, previous)
        source = fixture(self.root / "root-anchor")
        destination = source.parent / "out"
        source_spelling = str(source)[len(source.drive):]
        destination_spelling = str(destination)[len(destination.drive):]
        self.assertFalse(os.path.splitdrive(source_spelling)[0])
        self.assertFalse(os.path.splitdrive(destination_spelling)[0])
        os.chdir(source.parent)
        prepared = workflow.prepare_off_glb_export(source_spelling, destination_spelling,
                                                   ((0, 1),), **POLICIES)
        os.chdir(other)
        self.assertNotEqual(Path.cwd().drive.casefold(), source.drive.casefold())
        result = workflow.run_off_glb_export(prepared)
        self.success(result, destination)
        self.assertEqual(source.read_bytes(), source_bytes())
        self.assertFalse(Path(other.drive + destination_spelling).exists())

    @unittest.skipUnless(os.name == "nt", "Windows per-drive cwd contract")
    def test_windows_other_drive_relative_path_freezes_its_actual_drive_cwd(self):
        previous = Path.cwd()
        other = self.other_drive_directory()
        self.addCleanup(os.chdir, previous)
        source = fixture(other / "input")
        later = other / "later"
        later.mkdir()
        os.chdir(self.root)
        # Snapshot the actual other-drive cwd just as a Windows caller sees it.
        # The owned directory is underneath that cwd, so no '..' or alias is added.
        drive_base = Path(os.path.abspath(other.drive + "."))
        relative_source = source.relative_to(drive_base)
        relative_output = (other / "out").relative_to(drive_base)
        prepared = workflow.prepare_off_glb_export(other.drive + str(relative_source),
            other.drive + str(relative_output), ((0, 1),), **POLICIES)
        os.chdir(later)
        os.chdir(self.root)
        result = workflow.run_off_glb_export(prepared)
        self.success(result, other / "out")

    def test_admission_precedes_stop_and_missing_capture(self):
        token = workflow.OffCancellation()
        token.cancel()
        for options in (dict(precision="exact64"), dict(acknowledge_unused_vertices=False),
                        dict(max_position_error=True), dict(preset="unknown")):
            with self.subTest(options=options), self.assertRaises((TypeError, ValueError)):
                workflow.repair_off_to_glb(self.root / "absent", self.root / "out", (),
                    **(POLICIES | options), cancellation=token)
        with self.assertRaises((TypeError, ValueError)):
            workflow.prepare_off_glb_export(self.root / "absent", self.root / "out",
                                           ((0, 1), (True, 2)), **POLICIES)
        prepared = workflow.prepare_off_glb_export(self.root / "absent", self.root / "out", (), **POLICIES)
        with self.assertRaises((TypeError, ValueError)):
            workflow.run_off_glb_export(prepared, execution=workflow.OffGlbExecution(preset="serial-reference-v1"))
        stopped = workflow.run_off_glb_export(prepared, cancellation=token)
        self.assertEqual(stopped.exit, 130)
        enum(self, stopped.capture.status, "kCanceled")
        self.assertIsNone(stopped.native)
        self.assertFalse((self.root / "out").exists())

    def test_shared_stop_is_not_sticky_in_execution_and_views_own_native_lifetime(self):
        source = fixture(self.root / "input")
        execution = workflow.OffGlbExecution()
        token = workflow.OffCancellation()
        token.cancel()
        stopped = workflow.repair_off_to_glb(source, self.root / "stopped", ((0, 1),),
                                            **POLICIES, execution=execution, cancellation=token)
        self.assertEqual(stopped.exit, 130)
        completed_token = workflow.OffCancellation()
        result = workflow.repair_off_to_glb(source, self.root / "out", ((0, 1),),
                                           **POLICIES, execution=execution, cancellation=completed_token)
        self.assertEqual(result.exit, 0)
        completed_token.cancel()
        self.assertEqual(result.exit, 0)
        enum(self, result.native.status, "kSuccess")
        self.assertEqual(execution.active_workers, 0)
        self.assertGreater(execution.active_tracked_payload, 0)
        retained = [getattr(owner, field) for owner in (result.source_mesh, result.candidate,
                    result.triangulated_mesh) for field in ("positions", "face_offsets", "corner_vertices")]
        retained += [getattr(result, field) for field in (*MAPS, "triangulation_face_sources",
                     "triangulation_corner_sources", "triangulation_face_output_offsets")]
        retained += [getattr(result.output, field) for field in OUTPUT_MAPS]
        retained += [member.bytes for member in result.output.members]
        retained += [result.published.receipt.bytes]
        expected = [bytes(view) for view in retained]
        strong = (result.candidate, result.output, result.proof, result.published)
        for owner, field in ((result, "exit"), (result.capture, "status"),
                             (result.candidate, "vertex_count"), (execution, "worker_budget")):
            with self.assertRaises((AttributeError, TypeError)):
                setattr(owner, field, getattr(owner, field))
        source.unlink()
        del result, execution, stopped, token, completed_token, owner
        gc.collect()
        self.assertEqual([bytes(view) for view in retained], expected)
        self.assertEqual(strong[0].vertex_count, 14)
        self.assertEqual(bytes(strong[3].receipt.bytes), expected[-1])
        del strong
        gc.collect()
        self.assertEqual([bytes(view) for view in retained], expected)


if __name__ == "__main__":
    unittest.main()
