# SPDX-License-Identifier: Apache-2.0
"""Run the installed path API on small JSON glTF and embedded-BIN GLB assets.

The targets and primitive policies are explicit. No mesh discovery, image
decoding, skin/animation processing or format conversion is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile

from meshvale_reports import load
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unsupported-profile", action="store_true",
                        help="prove the configured native numerical refusal explicitly")
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="meshvale-installed-gltf-") as scratch:
        root = Path(scratch).resolve() / "\u8d44\u4ea7-\U0001f9ca"
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
