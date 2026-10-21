# SPDX-License-Identifier: Apache-2.0
"""Run the installed path API on small JSON glTF and embedded-BIN GLB assets.

Add --commands to use the installed module, console command and batch interface.

The targets and primitive policies are explicit. No mesh discovery, image
decoding, skin/animation processing or format conversion is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from meshvale_reports import exit_code, load
from meshvale_repair.gltf_workflow import GltfExecution, repair_gltf_file


def make_source(root, glb):
    root.mkdir(parents=True)
    # Three float32 vertices, two identical UInt16 triangles, external tail.
    geometry = bytes.fromhex(
        "00000000 00000000 00000000 0000803f 00000000 00000000 "
        "00000000 0000803f 00000000 000001000200000001000200"
    )
    texture = bytes.fromhex("89504e470d0a1a0a") + b"opaque resource bytes\0\xff"
    buffers = ('[{"byteLength":49},{"uri":"mesh.bin","byteLength":48}]' if glb else
               '[{"uri":"mesh.bin","byteLength":48}]')
    text = ('{"asset":{"version":"2.0"},"buffers":' + buffers +
            ',"bufferViews":[{"buffer":0,"byteLength":36},'
            '{"buffer":0,"byteOffset":36,"byteLength":12}],'
            '"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},'
            '{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],'
            '"images":[{"uri":"texture.bin","mimeType":"image/png"}],'
            '"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],'
            '"extras":{"authored":1.2300e+04}} \t\n').encode("ascii")
    entry = text
    opaque = bytes.fromhex("7f00dead")
    embedded = geometry + bytes.fromhex("d3 000000")
    if glb:
        json_chunk = text + b" " * (-len(text) % 4)
        body = b"".join(struct.pack("<II", len(data), kind) + data for kind, data in (
            (0x4E4F534A, json_chunk), (0x004E4942, embedded), (0x12345678, opaque)))
        entry = struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body
    members = {"scene.gltf": entry, "mesh.bin": geometry + bytes.fromhex("deadbeefcafebabe"),
               "texture.bin": texture}
    for name, data in members.items():
        (root / name).write_bytes(data)
    return members, embedded, opaque


def check_command_report(raw, destination, source, changed, unsupported):
    report = load(raw)
    if unsupported:
        assert exit_code(report) == 2
        assert report["candidate"]["outcome"] == "not-attempted"
        assert report["publication"]["outcome"] == "not-attempted"
        assert "meshvale.repair.gltf.commands" not in report["extensions"]
        assert not destination.exists()
        return report
    assert exit_code(report) == 0
    assert (destination / "receipts/report.json").read_bytes() == raw
    assert report["candidate"]["outcome"] == ("accepted" if changed else "unchanged")
    assert report["publication"]["verified"]
    paths = report["extensions"]["meshvale.repair.gltf.file"]["artifact_paths"]
    assert report["publication"]["artifacts"] == [
        {"id": row["id"], "sha256": hashlib.sha256((destination / row["path"]).read_bytes()).hexdigest()}
        for row in paths]
    snapshots = {row["role"]: row for row in report["snapshots"]}
    for role, faces in (("input", 2), ("candidate", 1 if changed else 2)):
        mesh, = snapshots[role]["meshes"]
        assert tuple(mesh[key] for key in ("vertex_count", "face_count", "corner_count")) == (3, faces, 3 * faces)
    maps = {row["domain"]: row for row in report["maps"]
            if row["from"]["snapshot"] == snapshots["input"]["id"]
            and row["to"]["snapshot"] == snapshots["candidate"]["id"]}
    assert set(maps) == {"vertex", "face", "corner"}
    assert (maps["vertex"]["kind"], maps["vertex"]["entries"]) == ("identity", None)
    if changed:
        assert (maps["face"]["kind"], maps["face"]["entries"]) == ("explicit", [0, 0])
        assert (maps["corner"]["kind"], maps["corner"]["entries"]) == ("explicit", [0, 1, 2, 0, 1, 2])
    else:
        assert all(row["kind"] == "identity" and row["entries"] is None for row in maps.values())
    original, embedded, opaque = source
    for name in ("mesh.bin", "texture.bin"):
        assert (destination / name).read_bytes() == original[name]
    output = (destination / "scene.gltf").read_bytes()
    if not changed:
        assert output == original["scene.gltf"]
    if original["scene.gltf"][:4] == b"glTF":
        assert struct.unpack_from("<III", output) == (0x46546C67, 2, len(output))
        position = 20 + struct.unpack_from("<I", output, 12)[0]
        assert output[position:] == (struct.pack("<II", 52, 0x004E4942) + embedded
                                    + struct.pack("<II", 4, 0x12345678) + opaque)
    return report


def command_walkthrough(root, unsupported):
    from meshvale_repair.gltf_commands import run_gltf_jobs

    # Content chooses JSON or GLB even though both examples use scene.gltf.
    sources = {name: make_source(root / "sources" / name, glb)
               for name, glb in (("json", False), ("glb", True))}
    (root / "published").mkdir()
    module = [sys.executable, "-I", "-m", "meshvale_repair"]
    console = [str(Path(sys.executable).parent / ("meshvale-repair.exe" if sys.platform == "win32"
                                                else "meshvale-repair"))]
    rows = []
    expected_exit = 2 if unsupported else 0
    for name, source in sources.items():
        for changed in (True, False):
            identity = name + ("-changed" if changed else "-unchanged")
            row = {"id": identity, "input": f"sources/{name}/scene.gltf",
                   "destination": f"published/{identity}", "targets": [[0, 1]] if changed else [],
                   "primitive_ordinal": 0, "triangles": "require-triangles", "values": "raw"}
            rows.append(row)
            # The same policies are explicit in the CLI and batch request.
            arguments = ["gltf", row["input"], row["destination"], "--input-id", identity,
                         "--primitive-ordinal", "0", "--triangles", "require-triangles", "--values", "raw"]
            arguments += ["--target", "0:1"] if changed else ["--unchanged"]
            frontend = console if changed else module
            process = subprocess.run(frontend + arguments, cwd=root, capture_output=True, timeout=90)
            assert process.returncode == expected_exit, process.stderr.decode("utf-8", errors="replace")
            assert process.stdout.endswith(b"\n")
            report = check_command_report(process.stdout[:-1], root / row["destination"],
                                          source, changed, unsupported)
            if not unsupported:
                assert process.stdout == (root / row["destination"] / "receipts/report.json").read_bytes() + b"\n"
            print(f"{identity}: {'console' if changed else 'module'} report verified, exit {exit_code(report)}")
    for frontend in ("api", "cli"):
        batch_rows = [row | {"destination": row["destination"] + "-" + frontend} for row in rows]
        request = {"schema": "meshvale.repair.gltf-jobs/1", "jobs": batch_rows}
        if frontend == "api":
            result = run_gltf_jobs(request, base=root)
            raw, code = result.report_json, result.exit_code
            assert result.report == load(raw)
        else:
            path = root / "jobs.json"
            path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
            # Batch paths use the request's parent, even from another working directory.
            process = subprocess.run(module + ["gltf-batch", str(path)], cwd=root.parent,
                                     capture_output=True, timeout=90)
            assert process.stdout.endswith(b"\n"), process.stderr.decode("utf-8", errors="replace")
            raw, code = process.stdout[:-1], process.returncode
        batch = load(raw)
        assert code == exit_code(batch) == expected_exit
        assert [row["id"] for row in batch["jobs"]] == [row["id"] for row in batch_rows]
        for requested, completed in zip(batch_rows, batch["jobs"]):
            assert completed["outcome"] == "attempted"
            destination = root / requested["destination"]
            name = requested["id"].split("-", 1)[0]
            if not unsupported:
                receipt = (destination / "receipts/report.json").read_bytes()
                assert receipt in raw  # Batch framing embeds the original native bytes.
                assert load(receipt) == completed["report"]
                check_command_report(receipt, destination, sources[name], bool(requested["targets"]), False)
            else:
                assert completed["report"]["candidate"]["outcome"] == "not-attempted"
                assert completed["report"]["publication"]["outcome"] == "not-attempted"
                assert not destination.exists()
        print(f"batch {frontend}: four ordered actual reports verified, exit {code}")
    for name, (original, _, _) in sources.items():
        assert {path.name: path.read_bytes() for path in (root / "sources" / name).iterdir()} == original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unsupported-profile", action="store_true",
                        help="prove the configured native numerical refusal explicitly")
    parser.add_argument("--commands", action="store_true",
                        help="demonstrate module/console single commands and API/CLI batches")
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="meshvale-installed-gltf-") as scratch:
        root = Path(scratch).resolve() / "\u8d44\u4ea7-\U0001f9ca"
        if options.commands:
            command_walkthrough(root, options.unsupported_profile)
            return
        for glb in (False, True):
            for changed in (True, False):
                case = root / f"{'glb' if glb else 'json'}-{'changed' if changed else 'unchanged'}"
                source, destination = case / "source", case / "published"
                original, embedded, opaque = make_source(source, glb)
                session = GltfExecution()
                result = repair_gltf_file(
                    source / "scene.gltf", destination, [(0, 1)] if changed else [],
                    primitive_ordinal=0, triangles="require-triangles", values="raw",
                    input_id="installed-example", execution=session,
                )
                assert result.capture_status == type(result.capture_status).kSuccess
                assert result.workflow_started
                if options.unsupported_profile:
                    assert result.status == type(result.status).kDelegatedFailure
                    assert result.diagnostic.memory.delegated.geometry_status == type(
                        result.diagnostic.memory.delegated.geometry_status).kUnsupportedAllocationProfile
                    assert result.publication is None and not destination.exists()
                    print(f"{'GLB' if glb else 'JSON'}: actual native allocation-profile refusal retained")
                    continue
                assert result.status == type(result.status).kSuccess
                assert result.face_map == ((0, 0) if changed else (0, 1))
                raw = result.report_json
                assert (destination / "receipts/report.json").read_bytes() == raw
                report = load(raw)
                assert report["publication"]["verified"]
                assert report["candidate"]["outcome"] == ("accepted" if changed else "unchanged")
                paths = report["extensions"]["meshvale.repair.gltf.file"]["artifact_paths"]
                assert report["publication"]["artifacts"] == [
                    {"id": row["id"], "sha256": hashlib.sha256((destination / row["path"]).read_bytes()).hexdigest()}
                    for row in paths]
                for name in ("mesh.bin", "texture.bin"):
                    assert (destination / name).read_bytes() == original[name]
                output = (destination / "scene.gltf").read_bytes()
                if not changed:
                    assert output == original["scene.gltf"]
                if glb:
                    assert struct.unpack_from("<III", output) == (0x46546C67, 2, len(output))
                    json_size = struct.unpack_from("<I", output, 12)[0]
                    position = 20 + json_size
                    assert struct.unpack_from("<II", output, position) == (52, 0x004E4942)
                    assert output[position + 8:position + 60] == embedded
                    assert output[position + 60:] == struct.pack("<II", 4, 0x12345678) + opaque
                shutil.rmtree(source)
                del session
                assert result.report_json == raw and json.loads(raw)["publication"]["outcome"] == "published"
                reopened = repair_gltf_file(
                    destination / "scene.gltf", None, [], primitive_ordinal=0,
                    triangles="require-triangles", values="raw")
                assert reopened.status == type(reopened.status).kSuccess
                assert reopened.face_map == ((0,) if changed else (0, 1))
                print(f"{'GLB' if glb else 'JSON'} {'changed' if changed else 'unchanged'}: "
                      f"published, reopened, resource bytes and receipt verified; "
                      f"report SHA256 {hashlib.sha256(raw).hexdigest()}")


if __name__ == "__main__":
    main()
