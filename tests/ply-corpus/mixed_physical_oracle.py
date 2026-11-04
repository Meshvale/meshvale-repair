# SPDX-License-Identifier: Apache-2.0
"""Independent physical oracle for the small double/region/index fixture only."""
import math
import re
import struct


ENCODINGS = ("ascii", "binary_little_endian", "binary_big_endian")


def require(condition, message):
    if not condition:
        raise AssertionError("mixed PLY: " + message)


def verify_mixed_ply(raw, case, encoding, *, source=False):
    require(type(raw) is bytes and len(raw) <= 4096, "bounded physical entry")
    require(encoding in ENCODINGS, "explicit encoding")
    mesh = case["source_literal"] if source else case["expected"]
    positions = mesh["positions_f64_hex"] if source else mesh["candidate_positions_f64_hex"]
    offsets, corners = mesh["face_offsets"], mesh["corner_vertices"]
    regions = mesh["region_code_values"]
    vertices, faces = len(positions) // 3, len(offsets) - 1
    require(len(positions) == vertices * 3 and vertices <= 14 and faces <= 5,
            "literal fixture counts")
    boundary = raw.find(b"end_header\n", 0, 1024)
    require(boundary >= 0, "complete bounded header")
    boundary += len(b"end_header\n")
    expected_header = ["ply", "format " + encoding + " 1.0", "element vertex " + str(vertices),
                       "property double x", "property double y", "property double z",
                       "element face " + str(faces), "property uint region_code",
                       "property list uchar uint vertex_indices", "end_header"]
    require(raw[:boundary].decode("ascii").splitlines() == expected_header,
            "literal ordered double/region/index schema")
    body, position = raw[boundary:], 0
    tokens = body.split() if encoding == "ascii" else None
    if tokens is not None:
        require(len(tokens) <= 72, "fixture token bound")

    def take(kind):
        nonlocal position
        if tokens is not None:
            require(position < len(tokens), "truncated scalar")
            token = tokens[position]
            position += 1
            require(len(token) <= 128, "bounded scalar token")
            if kind == "d":
                value = float(token)
                require(math.isfinite(value), "finite double")
                return struct.pack(">d", value).hex()
            require(re.fullmatch(rb"[+]?[0-9]+", token) is not None, "unsigned spelling")
            value = int(token)
            require(0 <= value <= (255 if kind == "B" else 2**32 - 1), "unsigned width")
            return value
        size = 8 if kind == "d" else 1 if kind == "B" else 4
        require(position + size <= len(body), "truncated scalar")
        data = body[position:position + size]
        position += size
        little = encoding == "binary_little_endian"
        if kind == "d":
            return (data[::-1] if little else data).hex()
        return struct.unpack(("<" if little else ">") + kind, data)[0]

    for ordinal, expected in enumerate(positions):
        require(take("d") == expected, "position bits " + str(ordinal))
    for face in range(faces):
        require(take("I") == regions[face], "region value " + str(face))
        start, end = offsets[face:face + 2]
        require(0 < end - start <= 5 and take("B") == end - start,
                "polygon length " + str(face))
        for corner in range(start, end):
            require(take("I") == corners[corner], "directed index " + str(corner))
    require(position == (len(tokens) if tokens is not None else len(body)), "trailing physical body")
    return {"vertices": vertices, "faces": faces, "corners": len(corners),
            "encoding": encoding, "physical_bytes": len(raw)}
