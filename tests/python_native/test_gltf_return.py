# SPDX-License-Identifier: Apache-2.0
"""Run the opt-in observer module alone in a controlled CPython subprocess."""
from pathlib import Path
import argparse
import gc
import hashlib
import importlib.util
import json
import struct


def fixture(root, glb, triangle_count=2):
    root.mkdir()
    binary = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0) + struct.pack("<3H", 0, 1, 2) * triangle_count
    document = {"asset": {"version": "2.0"}, "buffers": [{"uri": "mesh.bin", "byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 6 * triangle_count}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
                      {"bufferView": 1, "componentType": 5123, "count": 3 * triangle_count, "type": "SCALAR"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 4}]}]}
    entry = root / ("scene.glb" if glb else "scene.gltf")
    if glb:
        document["buffers"] = [{"byteLength": len(binary) + 1}]
        text = json.dumps(document, separators=(",", ":")).encode() + b" \t\n"
        text += b" " * (-len(text) % 4)
        chunks = [(0x4E4F534A, text), (0x004E4942, binary + b"\xd3\0\0\0"),
                  (0x12345678, b"\x7f\0\xde\xad"), (0xDECAFBAD, b"\1\2\3\4\x80\xff\0\xaa"),
                  (0x12345678, b"\0\1\2\xff")]
        payload = b"".join(struct.pack("<II", len(data), kind) + data for kind, data in chunks)
        entry.write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(payload)) + payload)
    else:
        entry.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
        (root / "mesh.bin").write_bytes(binary + b"\xde\xad\xbe\xef")
    return entry


def retained_facts(module, result, changed, triangles):
    assert result.input_snapshot_counts == (3, triangles, 3 * triangles)
    assert result.candidate_snapshot_counts == (3, triangles - int(changed), 3 * (triangles - int(changed)))
    assert all(type(value) is int for value in result.input_snapshot_counts + result.candidate_snapshot_counts)
    expected_faces = tuple(0 if i < 2 else i - 1 for i in range(triangles)) if changed else tuple(range(triangles))
    expected_corners = tuple(i if i < 3 else i % 3 if i < 6 else i - 3 for i in range(3 * triangles)) if changed else tuple(range(3 * triangles))
    expected_source_faces = (0,) + tuple(range(2, triangles)) if changed else tuple(range(triangles))
    expected_source_corners = (0, 1, 2) + tuple(range(6, 3 * triangles)) if changed else tuple(range(3 * triangles))
    actual_maps = (result.face_map, result.corner_map, result.source_faces, result.source_corners)
    assert actual_maps == (expected_faces, expected_corners, expected_source_faces, expected_source_corners)
    assert all(type(value) is tuple for value in actual_maps)
    subset = result.requested_bytes.retained_repair
    assert type(subset).__name__ == "GltfRetainedRepairRequestedBytes"
    assert type(subset.repair).__name__ == "MaterializedDuplicateRequestedBytes"
    assert subset.source_geometry > 0 and subset.repair.total > 0
    assert subset.total == subset.source_geometry + subset.repair.total
    repair = subset.repair
    assert repair.total == repair.repair + repair.source_inspection + repair.selection + repair.candidate_inspection
    assert result.requested_bytes.memory.total == 0
    return {"input_counts": result.input_snapshot_counts, "candidate_counts": result.candidate_snapshot_counts,
            "source_geometry": subset.source_geometry, "repair_total": subset.repair.total, "subset_total": subset.total,
            "map_sha256": [hashlib.sha256(json.dumps(value).encode()).hexdigest() for value in actual_maps]}


def retained_controls(module, output):
    # The actual native report serializes original-corner provenance even for
    # unchanged selection; this fixture reaches real report-copy workers.
    triangles = 2048
    prepare, run = module._prepare_gltf_file, module._run_gltf_file
    setup, finish, restore = module._observer_setup, module._observer_finish, module._observer_restore
    rows = []
    for glb in (False, True):
        for changed in (False, True):
            label = ("glb" if glb else "json") + ("-changed" if changed else "-noop")
            entry = fixture(output / (label + "-failure-source"), glb, triangles)
            destination = output / (label + "-never-published")
            released_payload = None
            for retain in (False, True):
                session = module.GltfExecution()
                cancellation = module.GltfCancellation()
                options = {"primitive_ordinal": 0, "triangles": "require-triangles", "values": "raw",
                           "execution": session, "cancellation": cancellation, "retain_failure_facts": retain}
                prepared = prepare(entry, destination, [(0, 1)] if changed else [], **options)
                del options, cancellation
                setup("report-canceled")
                try:
                    returned = run(prepared)
                    stats = finish(returned)
                finally:
                    restore()
                assert returned is prepared
                assert returned.status == module.GltfWorkflowStatus.kCanceled
                assert returned.outcomes.execution == module.GltfWorkflowExecutionOutcome.kCanceled
                assert returned.outcomes.candidate == (module.CandidateOutcome.accepted if changed else module.CandidateOutcome.unchanged)
                assert returned.outcomes.publication == module.GltfFileWorkflowPublicationOutcome.kNotAttempted
                assert returned.outcomes.profile == module.GltfWorkflowProfileOutcome.kIncomplete
                assert returned.diagnostic.stage == module.GltfFileWorkflowStage.kMemory
                assert returned.diagnostic.memory.stage == module.GltfWorkflowStage.kReport
                assert returned.diagnostic.memory.code == module.GltfWorkflowDiagnosticCode.kStopRequested
                assert returned.memory_outcomes.stage == module.GltfWorkflowStage.kReport
                assert returned.memory_outcomes.execution == module.GltfWorkflowExecutionOutcome.kCanceled
                assert returned.memory_outcomes.candidate == returned.outcomes.candidate
                # Real native preservation checks completed before report copy;
                # their passed profile is not a delivered receipt/publication.
                assert returned.memory_outcomes.profile == module.GltfWorkflowProfileOutcome.kPassed
                assert returned.publication is None and returned.report_json is None
                assert not destination.exists()
                assert stats["return_armed"] and stats["restored"] and stats["prepared_reports"] == 0
                assert stats["cpp_requests"] == 0 and sum(stats["python_requests"]) == 0, stats
                assert stats["wrapper_after_freeze"] == stats["memory_after_freeze"] == 0
                assert stats["report_stop_requested"] and stats["report_stop_accepted"]
                assert stats["report_workers_launched"] >= 2
                assert stats["report_workers_entered"] == stats["report_workers_joined"] == stats["report_workers_launched"]
                assert session.active_workers == 0
                live_payload = session.active_tracked_payload
                if retain:
                    facts = retained_facts(module, returned, changed, triangles)
                    assert live_payload == released_payload + facts["subset_total"]
                    setup("report-refused")
                    try:
                        try:
                            module._observer_refuse_property(returned, "candidate_snapshot_counts")
                        except MemoryError:
                            pass
                        else:
                            raise AssertionError("real lazy retained-count property allocation was not refused")
                        lazy = finish(returned)
                    finally:
                        restore()
                    assert lazy["restored"] and lazy["refusal_attempts"] == 1
                    # Destroy caller session/options and physical source before
                    # reading the native owner again, without cached map tuples.
                    del session
                    entry.unlink()
                    if not glb:
                        (entry.parent / "mesh.bin").unlink()
                    entry.parent.rmdir()
                    assert retained_facts(module, returned, changed, triangles) == facts
                    assert returned.report_json is None and returned.publication is None
                else:
                    assert returned.input_snapshot_counts is returned.candidate_snapshot_counts is None
                    assert returned.face_map is returned.corner_map is returned.source_faces is returned.source_corners is None
                    assert returned.requested_bytes.memory.total == 0 and returned.requested_bytes.retained_repair is None
                    released_payload = live_payload
                    facts, lazy = None, None
                rows.append({"glb": glb, "changed": changed, "retain": retain, "return": stats,
                             "live_payload": live_payload, "released_payload": released_payload, "facts": facts, "lazy": lazy})
                del returned, prepared
                gc.collect()
                assert module._observer_live_blocks() == 0
                if not retain:
                    assert session.active_tracked_payload == 0
                    del session
    assert len(rows) == 8 and sum(row["lazy"] is not None for row in rows) == 4
    return rows


def strict_bool_controls(module, output, session):
    class PathProbe:
        calls = 0

        def __fspath__(self):
            self.calls += 1
            raise AssertionError("invalid bool reached path conversion")

    class TruthinessProbe:
        calls = 0

        def __bool__(self):
            self.calls += 1
            raise AssertionError("invalid bool was coerced")

    truthiness = TruthinessProbe()
    rows = []
    for value in (None, 0, 1, truthiness):
        source = PathProbe()
        module._observer_setup("instance-refused")
        try:
            try:
                module._prepare_gltf_file(source, output / "bool-never-captured", [], primitive_ordinal=0,
                    triangles="require-triangles", values="raw", execution=session, retain_failure_facts=value)
            except TypeError:
                pass
            else:
                raise AssertionError("retain_failure_facts accepted a non-bool")
            stats = module._observer_finish(False)
        finally:
            module._observer_restore()
        assert source.calls == truthiness.calls == 0
        assert stats["restored"] and stats["refusal_attempts"] == 0
        assert stats["wrapper_requests"] == stats["memory_requests"] == 0
        assert session.active_tracked_payload == 0
        rows.append({"type": type(value).__name__, "stats": stats})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("_repair_gltf_return_test", args.module)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    session = module.GltfExecution()
    prepare, run = module._prepare_gltf_file, module._run_gltf_file
    setup, finish, restore = module._observer_setup, module._observer_finish, module._observer_restore
    rows = []
    for glb in (False, True):
        entry = fixture(args.output / ("glb-source" if glb else "json-source"), glb)
        for changed in (False, True):
            for mode in ("normal", "positive-wrapper", "positive-wrapper-refused"):
                destination = args.output / (("glb" if glb else "json") + ("-changed-" if changed else "-noop-") + mode)
                prepared = prepare(entry, destination, [(0, 1)] if changed else [], primitive_ordinal=0,
                    triangles="require-triangles", values="raw", execution=session)
                setup(mode)
                try:
                    returned = run(prepared)
                    stats = finish(returned)
                finally:
                    restore()
                assert returned is prepared
                assert returned.status == module.GltfWorkflowStatus.kSuccess
                assert returned.outcomes.publication == module.GltfFileWorkflowPublicationOutcome.kPublished
                assert returned.face_map == ((0, 0) if changed else (0, 1))
                receipt = returned.report_json
                assert receipt == (destination / returned.published_report_path).read_bytes()
                report = json.loads(receipt)
                assert report["publication"]["outcome"] == "published"
                if not changed:
                    assert entry.read_bytes() == (destination / returned.published_entry_path).read_bytes()
                assert stats["restored"] and stats["return_armed"] and stats["prepared_reports"] == 1
                assert stats["wrapper_after_freeze"] == stats["memory_after_freeze"] == 0
                assert stats["wrapper_requests"] > 0 and stats["memory_requests"] > 0
                if mode == "normal":
                    assert stats["cpp_requests"] == 0 and sum(stats["python_requests"]) == 0, stats
                    assert stats["probe"] == module._probe_not_attempted
                else:
                    assert stats["pre_probe_cpp_requests"] == stats["pre_probe_python_requests"] == 0, stats
                    assert sum(stats["python_requests"]) > 0, stats
                    if mode == "positive-wrapper":
                        assert stats["probe"] == module._probe_succeeded and stats["refusal_attempts"] == 0
                    else:
                        assert stats["probe"] == module._probe_python_failure and stats["refusal_attempts"] == 1
                # Real lazy bytes refusal is auto-restored; retry retains actual facts.
                setup("report-refused")
                try:
                    try:
                        returned.report_json
                    except MemoryError:
                        pass
                    else:
                        raise AssertionError("actual report bytes allocation was not refused")
                    lazy = finish(returned)
                finally:
                    restore()
                assert lazy["restored"] and lazy["refusal_attempts"] == 1
                assert returned.report_json == receipt
                rows.append({"glb": glb, "changed": changed, "mode": mode, "return": stats, "lazy": lazy,
                             "report_sha256": hashlib.sha256(receipt).hexdigest()})
                del returned, prepared
                gc.collect()
                assert module._observer_live_blocks() == 0
                assert session.active_tracked_payload == 0
        destination = args.output / (("glb" if glb else "json") + "-instance-refused")
        setup("instance-refused")
        try:
            try:
                prepare(entry, destination, [(0, 1)], primitive_ordinal=0, triangles="require-triangles", values="raw", execution=session)
            except MemoryError:
                pass
            else:
                raise AssertionError("actual pre-capture result instance allocation was not refused")
            instance = finish(False)
        finally:
            restore()
        assert instance["restored"] and instance["refusal_attempts"] == 1
        assert instance["wrapper_requests"] == instance["memory_requests"] == 0
        assert not destination.exists() and session.active_tracked_payload == 0
    retained_rows = retained_controls(module, args.output)
    bool_rows = strict_bool_controls(module, args.output, session)
    (args.output / "receipt.json").write_text(json.dumps({"scope": "Own-process opt-in compiled binding return observation on selected CPython13 Release; no universal CPython/process allocation claim",
        "module": str(args.module), "module_sha256": hashlib.sha256(args.module.read_bytes()).hexdigest(),
        "cases": rows, "pre_capture_instance_refusals": 2,
        "retained_failure_returns": retained_rows, "retained_lazy_refusals": 4,
        "strict_bool_refusals": bool_rows}, indent=2) + "\n", encoding="utf-8")
    print("Compiled return: 12 real JSON/GLB publication controls, 12 lazy refusals, 2 pre-capture refusals passed")
    print("Retained facts: 8 genuine report-cancel returns, 4 lazy property refusals, 4 strict-bool refusals passed")


if __name__ == "__main__":
    main()
