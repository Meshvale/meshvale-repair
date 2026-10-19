# SPDX-License-Identifier: Apache-2.0
"""Run the opt-in observer module alone in a controlled CPython subprocess."""
from pathlib import Path
import argparse
import gc
import hashlib
import importlib.util
import json
import struct


def fixture(root, glb):
    root.mkdir()
    binary = struct.pack("<9f6H", 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 2, 0, 1, 2)
    document = {"asset": {"version": "2.0"}, "buffers": [{"uri": "mesh.bin", "byteLength": 48}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 12}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
                      {"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 4}]}]}
    entry = root / ("scene.glb" if glb else "scene.gltf")
    if glb:
        document["buffers"] = [{"byteLength": 49}]
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
    (args.output / "receipt.json").write_text(json.dumps({"scope": "Own-process opt-in compiled binding return observation on selected CPython13 Release; no universal CPython/process allocation claim",
        "module": str(args.module), "module_sha256": hashlib.sha256(args.module.read_bytes()).hexdigest(),
        "cases": rows, "pre_capture_instance_refusals": 2}, indent=2) + "\n", encoding="utf-8")
    print("Compiled return: 12 real JSON/GLB publication controls, 12 lazy refusals, 2 pre-capture refusals passed")


if __name__ == "__main__":
    main()
