# SPDX-License-Identifier: Apache-2.0
"""Independently check the bounded workflow fixture's physical PLY values.

This is a test oracle for the literal schema, not a general PLY reader. It uses
no Meshvale reader, native observations, or reference serialized output.
"""
import math
import re
import struct


TYPES = {
    "char": ("b", -(2**7), 2**7 - 1), "uchar": ("B", 0, 2**8 - 1),
    "short": ("h", -(2**15), 2**15 - 1), "ushort": ("H", 0, 2**16 - 1),
    "int": ("i", -(2**31), 2**31 - 1), "uint": ("I", 0, 2**32 - 1),
    "float": ("f", None, None), "double": ("d", None, None),
}
ENCODINGS = ("ascii", "binary_little_endian", "binary_big_endian")
MAX_BYTES = 64 * 1024 * 1024
MAX_HEADER_BYTES = 4096
MAX_ASCII_TOKENS = 4096


def require(condition, message):
    if not condition:
        raise AssertionError("physical PLY: " + message)


def expected_rows(literal, kind):
    require(kind in ("changed", "unchanged", "empty"), "fixture kind")
    mesh = literal["changed" if kind == "changed" else "unchanged"]
    result = []
    for element in literal["original_schema"]:
        rows = []
        count = 0 if kind == "empty" else mesh["counts"]["vertices" if element["kind"] == "vertex" else "faces"]
        require(0 <= count <= 8, "fixture row bound")
        for row in range(count):
            properties = []
            for prop in element["properties"]:
                binding = prop["binding"]
                if binding in ("x", "y", "z"):
                    word = literal["positions_f64_hex"][row * 3 + ("x", "y", "z").index(binding)]
                    value = struct.unpack(">d", bytes.fromhex(word))[0]
                    values = [struct.pack(">f", value).hex()]
                elif binding == "vertex-indices":
                    start, end = mesh["face_offsets"][row:row + 2]
                    values = mesh["corner_vertices"][start:end]
                else:
                    attribute = mesh["attributes"][prop["attribute"]]
                    if prop["kind"] == "list":
                        start, end = attribute["offsets"][row:row + 2]
                        values = attribute["values"][start:end]
                    else:
                        values = [attribute["values"][row]]
                require(len(values) <= 8, "fixture list bound")
                properties.append((prop, values))
            rows.append(properties)
        result.append((element, rows))
    return result


def verify_physical_ply(raw, literal, kind, encoding):
    require(type(raw) is bytes and len(raw) <= MAX_BYTES, "bounded bytes")
    require(encoding in ENCODINGS, "encoding")
    end = raw.find(b"end_header\n", 0, MAX_HEADER_BYTES)
    require(end >= 0, "bounded complete header")
    boundary = end + len(b"end_header\n")
    try:
        lines = [line.split() for line in raw[:boundary].decode("ascii").splitlines()]
    except UnicodeError as exc:
        raise AssertionError("physical PLY: ASCII header") from exc
    rows = expected_rows(literal, kind)
    expected_header = [["ply"], ["format", encoding, "1.0"]]
    for element, records in rows:
        expected_header.append(["element", element["kind"], str(len(records))])
        for prop in element["properties"]:
            expected_header.append(["property", prop["value_type"], prop["name"]] if prop["kind"] == "scalar" else
                                   ["property", "list", prop["count_type"], prop["value_type"], prop["name"]])
    expected_header.append(["end_header"])
    require(lines == expected_header, "literal ordered schema/header")
    body = raw[boundary:]
    tokens = None
    if encoding == "ascii":
        require(len(body) <= 65536, "fixture ASCII body bound")
        tokens = body.split()
        require(len(tokens) <= MAX_ASCII_TOKENS, "ASCII token bound")
    position = 0
    scalar_count = 0

    def take(type_word):
        nonlocal position
        code, low, high = TYPES[type_word]
        if tokens is not None:
            require(position < len(tokens), "truncated scalar")
            token = tokens[position]
            position += 1
            require(len(token) <= 128, "scalar token bound")
            try:
                if low is not None:
                    require(re.fullmatch(rb"[+-]?[0-9]+", token) is not None, "integer spelling")
                    value = int(token)
                    require(low <= value <= high, "integer width")
                    return value
                value = float(token)
                require(math.isfinite(value), "finite floating value")
                return struct.pack(">" + code, value).hex()
            except (ValueError, OverflowError, struct.error) as exc:
                raise AssertionError("physical PLY: scalar representation") from exc
        size = struct.calcsize(code)
        require(position + size <= len(body), "truncated scalar")
        data = body[position:position + size]
        position += size
        order = "<" if encoding == "binary_little_endian" else ">"
        if low is not None:
            return struct.unpack(order + code, data)[0]
        # Compare authored wire bits directly, including signed zero/subnormals.
        return (data[::-1] if order == "<" else data).hex()

    for element, records in rows:
        for row, properties in enumerate(records):
            for prop, values in properties:
                where = element["kind"] + "/" + str(row) + "/" + prop["name"]
                if prop["kind"] == "list":
                    count = take(prop["count_type"])
                    require(count == len(values), where + " list count")
                for index, expected in enumerate(values):
                    actual = take(prop["value_type"])
                    require(type(actual) is type(expected) and actual == expected,
                            where + "/" + str(index) + " value/bits")
                    scalar_count += 1
    require(position == (len(tokens) if tokens is not None else len(body)), "trailing body")
    return {"encoding": encoding, "kind": kind, "scalars_checked": scalar_count,
            "physical_bytes": len(raw)}
