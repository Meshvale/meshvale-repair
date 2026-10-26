# SPDX-License-Identifier: Apache-2.0
"""Original wire fixture; no product generator or output supplies its oracle."""
import json
from pathlib import Path
import struct
import zlib


ENTRY = "scene.gltf"
REPORT = "receipts/report.json"
JSON_KIND = 0x4E4F534A
BIN_KIND = 0x004E4942
OPAQUE_KIND = 0x13572468
OPAQUE = bytes.fromhex("31 00 fe 72")
TRANSLATION_BYTES = bytes.fromhex(
    "00000000 00000000 00000000 "
    "0000803e 00000000 0000003f "
    "0000803f 00000000 0000803f"
)
# Positions (including negative zero), duplicate UInt16 faces, three dyadic
# times, translations, then the unrelated triangle. Four physical tail bytes
# are outside JSON's logical buffer, but inside GLB's declared logical buffer.
BINARY = bytes.fromhex(
    "00000080 00000000 00000000 0000003f 00000000 00000000 "
    "00000000 0000803e 00000000 "
    "000001000200000001000200 00000000 0000803e 0000803f"
) + TRANSLATION_BYTES + bytes.fromhex(
    "00000040 00000000 00000000 00004040 00000000 00000000 "
    "00000040 0000803f 00000000 d1e2f304"
)


def _png():
    def chunk(kind, content):
        return (struct.pack(">I", len(content)) + kind + content +
                struct.pack(">I", zlib.crc32(kind + content)))
    return (bytes.fromhex("89504e470d0a1a0a") +
            chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes((0, 0, 128, 255, 255)))) +
            chunk(b"IEND", b""))


def parse_entry(payload):
    if not payload.startswith(b"glTF"):
        return json.loads(payload), []
    magic, version, total = struct.unpack_from("<4sII", payload)
    if (magic, version, total) != (b"glTF", 2, len(payload)):
        raise ValueError("Invalid GLB header")
    offset, chunks = 12, []
    while offset < total:
        size, kind = struct.unpack_from("<II", payload, offset)
        offset += 8
        if size % 4 or offset + size > total:
            raise ValueError("Invalid GLB chunk")
        chunks.append((kind, payload[offset:offset + size]))
        offset += size
    if offset != total or not chunks or chunks[0][0] != JSON_KIND:
        raise ValueError("Invalid GLB framing")
    return json.loads(chunks[0][1]), chunks[1:]


def fixture(root, *, glb=False, animated=True, interpolation="LINEAR", malformed=False):
    root = Path(root)
    root.mkdir(parents=True)
    binary = bytearray(BINARY)
    if malformed:
        binary[52:56] = bytes(4)  # Second time equals the first; not increasing.
    buffer = {"byteLength": 136 if glb else 132}
    if not glb:
        buffer["uri"] = "mesh.bin"
    document = {
        "asset": {"version": "2.0"},
        "buffers": [buffer, {"uri": "metadata.bin", "byteLength": 7}],
        "bufferViews": [{"buffer": 0, "byteOffset": offset, "byteLength": size}
                        for offset, size in ((0, 36), (36, 12), (48, 12), (60, 36), (96, 36))],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [0.5, 0.25, 0]},
            {"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0], "max": [1]},
            {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3", "min": [2, 0, 0], "max": [3, 1, 0]},
        ],
        "images": [{"uri": "preview.png", "mimeType": "image/png"}],
        "materials": [{"name": "selected"}, {"name": "unrelated", "pbrMetallicRoughness": {"baseColorFactor": [0, 0.5, 1, 1]}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]},
                   {"primitives": [{"attributes": {"POSITION": 4}, "material": 1}]}],
        "nodes": [{"children": [1, 2]}, {"children": [3], "translation": [1, 0, 0]},
                  {"mesh": 0, "translation": [-0.0, 0.25, 0]},
                  {"mesh": 0, "translation": [0, 0, 0.5]}, {"mesh": 1}],
        "scenes": [{"nodes": [0, 4]}], "scene": 0,
        "extras": {"exact_integer": 9007199254740993},
    }
    if animated:
        document["animations"] = [{
            "samplers": [{"input": 2, "output": 3, "interpolation": interpolation}],
            "channels": [{"sampler": 0, "target": {"node": node, "path": "translation"}} for node in (2, 3)],
        }]
    encoded = json.dumps(document, separators=(",", ":")).encode()
    members = {"metadata.bin": bytes.fromhex("a1b2c3d4e5f6078899"), "preview.png": _png()}
    if glb:
        encoded += b" " * (-len(encoded) % 4)
        chunks = b"".join(struct.pack("<II", len(content), kind) + content
                          for kind, content in ((JSON_KIND, encoded), (BIN_KIND, bytes(binary)), (OPAQUE_KIND, OPAQUE)))
        members[ENTRY] = struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks
    else:
        members[ENTRY], members["mesh.bin"] = encoded, bytes(binary)
    for name, content in members.items():
        (root / name).write_bytes(content)
    return root / ENTRY, document, members
