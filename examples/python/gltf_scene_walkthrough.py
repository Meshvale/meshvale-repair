# SPDX-License-Identifier: Apache-2.0
"""Repair one shared primitive while retaining its static scene and resources.

Use --output NEW_DIRECTORY to keep original assets, requests and actual reports.
Optional --scene-inspector runs the separately built installed C++ consumer.
Transforms are retained as authored; this example never evaluates them.
"""
import argparse
import base64
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from meshvale_reports import exit_code, load
from meshvale_repair.gltf_commands import run_gltf_jobs
from meshvale_repair.gltf_workflow import repair_gltf_file


ENTRY = "scene.gltf"  # Both formats deliberately share a suffix.
RECEIPT = "receipts/report.json"
REQUEST_SCHEMA = "meshvale.repair.gltf-jobs/1"
RED_TEXTURE = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
    "0000000d4944415478da63f8cfc0f01f00050001ff56c72f0d0000000049454e44ae426082")
BLUE_TEXTURE = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
    "0000000d4944415478da636060f8ff1f00030201ff392919be0000000049454e44ae426082")


def make_scene(root, glb=False):
    """Create the original contracted scene, refusing an existing source root."""
    root = Path(root)
    root.mkdir(parents=True, exist_ok=False)
    positions = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    uv = struct.pack("<6f", 0, 0, 1, 0, 0, 1)
    other_positions = struct.pack("<9f", 10, 0, 0, 12, 0, 0, 10, 2, 0)
    binary = positions + uv + struct.pack("<6H", 0, 1, 2, 0, 1, 2)
    binary += other_positions + uv + struct.pack("<3H", 0, 1, 2)
    assert len(binary) == 138
    views = [{"buffer": 0, "byteOffset": offset, "byteLength": length}
             for offset, length in ((0, 36), (36, 24), (60, 12), (72, 36), (108, 24), (132, 6))]
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"},
        {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"},
        {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3", "min": [10, 0, 0], "max": [12, 2, 0]},
        {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC2"},
        {"bufferView": 5, "componentType": 5123, "count": 3, "type": "SCALAR"},
    ]
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": 139} if glb else {"uri": "geometry.bin", "byteLength": 138},
                    {"uri": "unused.bin", "byteLength": 4}],
        "bufferViews": views, "accessors": accessors,
        "images": [{"uri": "texture-red.png", "mimeType": "image/png"},
                   {"uri": "texture-blue.png", "mimeType": "image/png"}],
        "textures": [{"source": 0}, {"source": 1}],
        "materials": [
            {"name": name, "pbrMetallicRoughness": {"baseColorTexture": {"index": index},
                                                     "metallicFactor": 0, "roughnessFactor": 1}}
            for index, name in enumerate(("red", "blue"))],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": pos, "TEXCOORD_0": tex},
                      "indices": indices, "mode": 4, "material": material}]} for name, pos, tex, indices, material
                   in (("shared", 0, 1, 2, 0), ("other", 3, 4, 5, 1))],
        "nodes": [
            {"name": "scaled-shared", "mesh": 0, "translation": [2, 0, 0],
             "rotation": [0, 0, 0, 1], "scale": [2, 1, 1]},
            {"name": "matrix-shared", "mesh": 0, "matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -3, 4, 0, 1]},
            {"name": "unrelated", "mesh": 1, "translation": [0, 0, 5]},
            {"name": "unused-node"}],
        "scenes": [{"name": "shared-scene", "nodes": [0, 1, 2]}], "scene": 0,
        "extras": {"authored": "original static scene"},
    }
    text = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b" \t\n"
    if glb:
        text += b" " * (-len(text) % 4)
        chunks = ((0x4E4F534A, text), (0x004E4942, binary + b"\xd3\0"), (0x12345678, b"\x7f\0\xde\xad"))
        body = b"".join(struct.pack("<II", len(data), kind) + data for kind, data in chunks)
        text = struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body
    members = {ENTRY: text, "unused.bin": bytes.fromhex("01020304 deadbeefcafebabe"),
               "texture-red.png": RED_TEXTURE, "texture-blue.png": BLUE_TEXTURE}
    if not glb:
        members["geometry.bin"] = binary + bytes.fromhex("deadbeefcafebabe")
    for name, data in members.items():
        (root / name).write_bytes(data)
    return members


def entry_document(wire):
    if wire[:4] != b"glTF":
        return json.loads(wire), []
    assert struct.unpack_from("<III", wire) == (0x46546C67, 2, len(wire))
    cursor, chunks = 12, []
    while cursor < len(wire):
        length, kind = struct.unpack_from("<II", wire, cursor)
        cursor += 8
        assert length % 4 == 0 and cursor + length <= len(wire)
        chunks.append((kind, wire[cursor:cursor + length]))
        cursor += length
    assert cursor == len(wire) and chunks[0][0] == 0x4E4F534A
    return json.loads(chunks[0][1]), chunks[1:]


def check_scene_fragments(source_wire, output_wire):
    """Compare known generated source spans, without reserializing the output."""
    before, _ = entry_document(source_wire)
    source_json = source_wire[20:20 + struct.unpack_from("<I", source_wire, 12)[0]] if source_wire[:4] == b"glTF" else source_wire
    output_json = output_wire[20:20 + struct.unpack_from("<I", output_wire, 12)[0]] if output_wire[:4] == b"glTF" else output_wire
    fragments = [json.dumps({key: before[key]}, ensure_ascii=False, separators=(",", ":")).encode("utf-8")[1:-1]
                 for key in ("nodes", "scenes", "materials", "textures", "images")]
    fragments.append(json.dumps(before["meshes"][1], ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
    for expected in fragments:
        # The original generator has exactly these compact table spellings.
        # Read the actual original span, then require the same bytes in output.
        assert source_json.count(expected) == 1
        start = source_json.index(expected)
        fragment = source_json[start:start + len(expected)]
        assert output_json.count(fragment) == 1


def check_output(source, destination, original, changed, raw, inspector=None):
    """Verify actual receipt, literal correspondence and retained scene subtrees."""
    report = load(raw)
    assert exit_code(report) == 0 and report["publication"]["verified"]
    assert report["candidate"]["outcome"] == ("accepted" if changed else "unchanged")
    assert (destination / RECEIPT).read_bytes() == raw
    paths = report["extensions"]["meshvale.repair.gltf.file"]["artifact_paths"]
    assert report["publication"]["artifacts"] == [
        {"id": row["id"], "sha256": hashlib.sha256((destination / row["path"]).read_bytes()).hexdigest()}
        for row in paths]
    assert {p.relative_to(destination).as_posix() for p in destination.rglob("*") if p.is_file()} == set(original) | {RECEIPT}
    for name in original.keys() - {ENTRY}:
        assert (destination / name).read_bytes() == original[name]
    output_wire = (destination / ENTRY).read_bytes()
    before, before_chunks = entry_document(original[ENTRY])
    after, after_chunks = entry_document(output_wire)
    check_scene_fragments(original[ENTRY], output_wire)
    assert before_chunks == after_chunks
    if not changed:
        assert output_wire == original[ENTRY]
    else:
        expected = deepcopy(before)
        expected["meshes"][0]["primitives"][0]["indices"] = len(before["accessors"])
        for table in ("buffers", "bufferViews", "accessors"):
            assert len(after[table]) == len(before[table]) + 1
            assert after[table][:-1] == before[table]
            expected[table] = after[table]
        assert after == expected
        encoded = after["buffers"][-1]["uri"]
        assert encoded.startswith("data:application/octet-stream;base64,")
        assert base64.b64decode(encoded.split(",", 1)[1], validate=True) == struct.pack("<3I", 0, 1, 2)
    for key in ("nodes", "scenes", "scene", "materials", "textures", "images"):
        assert after[key] == before[key]
    snapshots = {row["role"]: row for row in report["snapshots"]}
    for role, faces in (("input", 2), ("candidate", 1 if changed else 2)):
        mesh, = snapshots[role]["meshes"]
        assert tuple(mesh[key] for key in ("vertex_count", "face_count", "corner_count")) == (3, faces, 3 * faces)
    maps = {row["domain"]: row for row in report["maps"]
            if row["from"]["snapshot"] == snapshots["input"]["id"] and row["to"]["snapshot"] == snapshots["candidate"]["id"]}
    assert set(maps) == {"vertex", "face", "corner"}
    assert (maps["vertex"]["kind"], maps["vertex"]["entries"]) == ("identity", None)
    if changed:
        assert (maps["face"]["kind"], maps["face"]["entries"]) == ("explicit", [0, 0])
        assert (maps["corner"]["kind"], maps["corner"]["entries"]) == ("explicit", [0, 1, 2, 0, 1, 2])
    else:
        assert all(row["kind"] == "identity" and row["entries"] is None for row in maps.values())
    if inspector is not None:
        subprocess.run([str(inspector), str(source / ENTRY), str(destination / ENTRY),
                        "changed" if changed else "unchanged"], check=True, timeout=90)
    return report


def job(identity, source, changed):
    # Raw UV rows contain values, not references to face/corner identities.
    # Their glTF spelling is an open semantic label, so declare it explicitly.
    return {"id": identity, "input": "sources/" + source + "/" + ENTRY, "destination": "published/" + identity,
            "targets": [[0, 1]] if changed else [], "primitive_ordinal": 0,
            "triangles": "require-triangles", "values": "raw",
            "row_local_attributes": [["vertex", "TEXCOORD_0"]]}


def run_walkthrough(root, unsupported=False, inspector=None):
    root = Path(root)
    sources = {name: make_scene(root / "sources" / name, glb) for name, glb in (("json", False), ("glb", True))}
    missing = make_scene(root / "sources/missing")
    (root / "sources/missing/texture-blue.png").unlink()
    del missing["texture-blue.png"]
    (root / "published").mkdir()
    (root / "reports").mkdir()
    module = [sys.executable, "-I", "-m", "meshvale_repair"]
    console = [str(Path(sys.executable).parent / ("meshvale-repair.exe" if sys.platform == "win32" else "meshvale-repair"))]
    observations = []

    def observe(row, raw, kind):
        report = load(raw)
        destination = root / row["destination"]
        if unsupported:
            assert exit_code(report) == 2 and report["candidate"]["outcome"] == "not-attempted"
            assert report["publication"]["outcome"] == "not-attempted" and not destination.exists()
            assert "meshvale.repair.gltf.commands" not in report["extensions"]
        else:
            name = row["input"].split("/")[1]
            check_output(root / "sources" / name, destination, sources[name], bool(row["targets"]), raw, inspector)
        observations.append({"id": row["id"], "entry": kind, "exit": exit_code(report),
                             "report_sha256": hashlib.sha256(raw).hexdigest(), "destination": row["destination"]})

    for name in sources:
        for changed in (True, False):
            suffix = name + ("-changed" if changed else "-unchanged")
            row = job("api-" + suffix, name, changed)
            result = repair_gltf_file(root / row["input"], root / row["destination"], row["targets"],
                primitive_ordinal=0, triangles="require-triangles", values="raw", input_id=row["id"],
                row_local_attributes=row["row_local_attributes"])
            assert result.capture_status.name == "kSuccess" and result.workflow_started
            if unsupported:
                assert result.status.name == "kDelegatedFailure"
                assert result.diagnostic.memory.delegated.geometry_status.name == "kUnsupportedAllocationProfile"
            else:
                assert result.status.name == "kSuccess" and result.face_map == ((0, 0) if changed else (0, 1))
            observe(row, result.report_json, "api")
            row = job("single-" + suffix, name, changed)
            arguments = ["gltf", row["input"], row["destination"], "--input-id", row["id"],
                         "--primitive-ordinal", "0", "--triangles", "require-triangles", "--values", "raw",
                         "--row-local", "vertex:TEXCOORD_0"]
            arguments += ["--target", "0:1"] if changed else ["--unchanged"]
            process = subprocess.run((console if changed else module) + arguments, cwd=root,
                                     capture_output=True, timeout=90)
            assert process.returncode == (2 if unsupported else 0), process.stderr.decode("utf-8", errors="replace")
            assert process.stdout.endswith(b"\n")
            observe(row, process.stdout[:-1], "console" if changed else "module")
            (root / "reports" / (row["id"] + ".json")).write_bytes(process.stdout)

    def batch(rows, identity, frontend, fail_fast=False):
        request = {"schema": REQUEST_SCHEMA, "jobs": rows}
        if frontend == "api":
            result = run_gltf_jobs(request, base=root, fail_fast=fail_fast)
            raw, code = result.report_json, result.exit_code
        else:
            path = root / (identity + ".json")
            path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
            process = subprocess.run(module + ["gltf-batch", str(path)] + (["--fail-fast"] if fail_fast else []),
                                     cwd=root.parent, capture_output=True, timeout=90)
            assert process.stdout.endswith(b"\n"), process.stderr.decode("utf-8", errors="replace")
            raw, code = process.stdout[:-1], process.returncode
        value = load(raw)
        assert code == exit_code(value)
        assert [row["id"] for row in value["jobs"]] == [row["id"] for row in rows]
        (root / "reports" / (identity + ".json")).write_bytes(raw)
        return raw, code, value

    for frontend in ("api", "cli"):
        rows = [job(f"batch-{frontend}-{name}-{'changed' if changed else 'unchanged'}", name, changed)
                for name in sources for changed in (True, False)]
        raw, code, value = batch(rows, "batch-" + frontend, frontend)
        assert code == (2 if unsupported else 0)
        for row, completed in zip(rows, value["jobs"]):
            assert completed["outcome"] == "attempted"
            if not unsupported:
                receipt = (root / row["destination"] / RECEIPT).read_bytes()
                assert receipt in raw and load(receipt) == completed["report"]
            else:
                receipt = json.dumps(completed["report"]).encode("utf-8")
            observe(row, receipt, "batch-" + frontend)
        for fail_fast in (False, True):
            identity = f"mixed-{frontend}-{'fast' if fail_fast else 'continue'}"
            rows = [job(identity + "-first", "json", True), job(identity + "-missing", "missing", True),
                    job(identity + "-last", "glb", False)]
            raw, code, value = batch(rows, identity, frontend, fail_fast)
            expected = (["attempted", "not-attempted", "not-attempted"] if unsupported and fail_fast else
                        ["attempted", "attempted", "not-attempted"] if fail_fast else ["attempted"] * 3)
            assert code == 2 and [row["outcome"] for row in value["jobs"]] == expected
            for row, completed in zip(rows, value["jobs"]):
                if completed["outcome"] == "not-attempted":
                    assert completed["report"] is None and not (root / row["destination"]).exists()
                elif "-missing" in row["id"]:
                    failure = completed["report"]
                    assert exit_code(failure) == 2 and failure["candidate"]["outcome"] == "not-attempted"
                    fallback = failure["extensions"]["meshvale.repair.gltf.commands"]
                    assert fallback["origin"] == "command-capture-fallback" and fallback["workflow_started"] is False
                    assert not (root / row["destination"]).exists()
                else:
                    receipt = ((root / row["destination"] / RECEIPT).read_bytes() if not unsupported else
                               json.dumps(completed["report"]).encode("utf-8"))
                    if not unsupported:
                        assert receipt in raw and load(receipt) == completed["report"]
                    observe(row, receipt, identity)
            observations.append({"id": identity, "entry": "mixed-batch", "exit": code, "outcomes": expected})
    for name, original in sources.items():
        assert {p.name: p.read_bytes() for p in (root / "sources" / name).iterdir()} == original
    assert {p.name: p.read_bytes() for p in (root / "sources/missing").iterdir()} == missing
    summary = {"example": "static-shared-scene", "unsupported_profile": unsupported,
               "native_inspector_executed": inspector is not None and not unsupported, "cases": observations}
    (root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"Static shared scene: {len(observations)} entry-point observations verified; "
          + ("actual unsupported outcomes, no published bundles" if unsupported else "three instances retained, selected shared mesh repaired"))
    if inspector is None:
        print("Native scene inspector was not requested; lexical and physical checks completed.")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="retain assets and reports in a new directory")
    parser.add_argument("--scene-inspector", type=Path, help="installed C++ scene consumer executable")
    parser.add_argument("--unsupported-profile", action="store_true", help="require the actual native numerical refusal")
    options = parser.parse_args()
    inspector = options.scene_inspector.resolve(strict=True) if options.scene_inspector else None
    if options.output is not None:
        root = options.output.resolve()
        root.mkdir(exist_ok=False)
        run_walkthrough(root, options.unsupported_profile, inspector)
        print("Retained scene assets and reports:", ascii(str(root)))
    else:
        with tempfile.TemporaryDirectory(prefix="meshvale-static-scene-") as scratch:
            run_walkthrough(Path(scratch).resolve(), options.unsupported_profile, inspector)


if __name__ == "__main__":
    main()
