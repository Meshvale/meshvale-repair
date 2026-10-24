# SPDX-License-Identifier: Apache-2.0
"""Literal glTF command fixtures and physical-byte oracles, independent of Repair."""
import hashlib
import json
from pathlib import Path
import struct

ENTRY = "scene.gltf"  # Deliberately shared suffix; actual content selects the format.
RECEIPT = "receipts/report.json"
REQUEST_SCHEMA = "meshvale.repair.gltf-jobs/1"
COMMAND_EXTENSION = "meshvale.repair.gltf.commands"
FILE_EXTENSION = "meshvale.repair.gltf.file"
JSON_KIND, BIN_KIND = 0x4E4F534A, 0x004E4942
JSON_SOURCE = (
    br'{"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":48}],'
    br'"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],'
    br'"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},'
    br'{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],'
    br'"images":[{"uri":"texture.bin","mimeType":"image/png"}],'
    br'"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],'
    br'"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],'
    br'"extras":{"number":1.2300e+04,"string":"keep\u0020lexeme"}}' + b" \t\n"
)
BINARY = bytes.fromhex(
    "00000080 00000000 00000000 0000803f 00000000 00000000 "
    "00000000 0000803f 00000000 000001000200000001000200 deadbeefcafebabe"
)
IMAGE = bytes.fromhex("89504e470d0a1a0a") + b"\x5a" * 4088
NON_JSON = ((BIN_KIND, BINARY[:48] + b"\xd3\0\0\0"),
            (0x12345678, bytes.fromhex("7f00dead")),
            (0xDECAFBAD, bytes.fromhex("0102030480ff00aa")),
            (0x12345678, bytes.fromhex("000102ff")))


def assemble_glb(text, chunks=NON_JSON):
    text += b" " * (-len(text) % 4)
    body = b"".join(struct.pack("<II", len(data), kind) + data
                    for kind, data in ((JSON_KIND, text), *chunks))
    return struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body


def parse_glb(wire):
    assert len(wire) >= 12 and struct.unpack_from("<III", wire) == (0x46546C67, 2, len(wire))
    cursor, chunks = 12, []
    while cursor < len(wire):
        assert cursor + 8 <= len(wire)
        size, kind = struct.unpack_from("<II", wire, cursor)
        cursor += 8
        assert size % 4 == 0 and size <= len(wire) - cursor
        chunks.append((kind, wire[cursor:cursor + size]))
        cursor += size
    assert cursor == len(wire) and chunks[0][0] == JSON_KIND
    return chunks


def fixture(root, glb=False):
    root = Path(root)
    root.mkdir(parents=True)
    text = JSON_SOURCE.replace(br'[{"uri":"mesh.bin","byteLength":48}]',
                               br'[{"byteLength":49},{"uri":"mesh.bin","byteLength":48}]')
    members = {ENTRY: assemble_glb(text) if glb else JSON_SOURCE,
               "mesh.bin": BINARY, "texture.bin": IMAGE}
    for name, content in members.items():
        (root / name).write_bytes(content)
    assert len(BINARY) == 56 and struct.unpack_from("<6H", BINARY, 36) == (0, 1, 2, 0, 1, 2)
    return members


def job(identity, *, source="sources/a/scene.gltf", destination=None, changed=True):
    return {"id": identity, "input": source, "destination": destination or "outputs/" + identity,
            "targets": [[0, 1]] if changed else [], "primitive_ordinal": 0,
            "triangles": "require-triangles", "values": "raw"}


def request(*jobs):
    return {"schema": REQUEST_SCHEMA, "jobs": list(jobs)}


def duplicate_triangle_fixture(root, face_count):
    """Literal triangles for a genuine completed-Repair report extent witness."""
    root = Path(root)
    root.mkdir(parents=True)
    binary = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0) + struct.pack("<3H", 0, 1, 2) * face_count
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"uri": "mesh.bin", "byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteLength": 36},
                        {"buffer": 0, "byteOffset": 36, "byteLength": 6 * face_count}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3,
                       "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
                      {"bufferView": 1, "componentType": 5123, "count": 3 * face_count, "type": "SCALAR"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
    }
    members = {ENTRY: canonical(document), "mesh.bin": binary}
    for name, content in members.items():
        (root / name).write_bytes(content)
    return members


def actual_files(root):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in Path(root).rglob("*") if p.is_file()}


def physical_source_digest(members, glb):
    frame = lambda b: struct.pack("<Q", len(b)) + b
    digest = hashlib.sha256(b"meshvale.gltf-file-snapshot/1\0")
    digest.update(frame(b"glb" if glb else b"gltf-json"))
    digest.update(struct.pack("<Q", len(members)))
    for name in sorted(members, key=lambda n: n.encode("utf-8")):
        digest.update(frame(name.encode("utf-8")))
        digest.update(frame(members[name]))
    return digest.hexdigest()


def canonical(value):
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")
