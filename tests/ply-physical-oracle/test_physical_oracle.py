# SPDX-License-Identifier: Apache-2.0
"""Check real retained fixture output and independent physical corruption controls."""
import argparse
import json
from pathlib import Path
import re
import runpy
import unittest


verify_physical_ply = runpy.run_path(str(Path(__file__).with_name("physical_oracle.py")))["verify_physical_ply"]
ENCODINGS = ("ascii", "binary_little_endian", "binary_big_endian")


class PhysicalOracleTests(unittest.TestCase):
    def entry(self, encoding, changed=True):
        name = "ascii-" + ("changed" if changed else "unchanged") + "-" + encoding
        return (self.fixture / name / "bundle/source.ply").read_bytes()

    def verify(self, raw, encoding, kind="changed"):
        return verify_physical_ply(raw, self.literal, kind, encoding)

    def mutation(self, encoding, control):
        raw = self.entry(encoding)
        boundary = raw.index(b"end_header\n") + len(b"end_header\n")
        body = bytearray(raw[boundary:])
        if encoding == "ascii":
            spans = list(re.finditer(rb"\S+", body))
            if control == "winding":
                # The original literal schema has 98 vertex tokens, then face
                # weight/count/two bins/count and four directed vertex indices.
                first, second = spans[103], spans[104]
                body[first.start():first.end()] = b"1"
                body[second.start():second.end()] = b"0"
            else:
                index, value = {"scalar": (0, b"-127"), "list-count": (10, b"1"),
                                "signed-zero": (7, b"0")}[control]
                span = spans[index]
                body[span.start():span.end()] = value
        elif control == "scalar":
            body[0] ^= 1
        elif control == "list-count":
            body[34] = 1
        elif control == "signed-zero":
            body[21 if encoding == "binary_little_endian" else 18] ^= 0x80
        else:
            # Eight 35-byte scalar/count rows plus ten two-byte sample values;
            # first face has weight4/count2/bins8/count1 before its first index.
            body[315:319], body[319:323] = body[319:323], body[315:319]
        return raw[:boundary] + body

    def test_actual_full_matrix_and_empty_outputs(self):
        for source in ENCODINGS:
            for kind in ("changed", "unchanged", "empty"):
                for destination in ENCODINGS:
                    directory = ("empty-" + source + "-" + destination if kind == "empty" else
                                 source + "-" + kind + "-" + destination)
                    with self.subTest(cell=directory):
                        raw = (self.fixture / directory / "bundle/source.ply").read_bytes()
                        result = self.verify(raw, destination, kind)
                        self.assertEqual(result["scalars_checked"] == 0, kind == "empty")

    def test_independent_integer_scalar_corruption(self):
        for encoding in ENCODINGS:
            with self.subTest(encoding=encoding), self.assertRaisesRegex(AssertionError, "sample_i8/0 value/bits"):
                self.verify(self.mutation(encoding, "scalar"), encoding)

    def test_list_count_corruption(self):
        for encoding in ENCODINGS:
            with self.subTest(encoding=encoding), self.assertRaisesRegex(AssertionError, "samples list count"):
                self.verify(self.mutation(encoding, "list-count"), encoding)

    def test_directed_polygon_winding_corruption(self):
        for encoding in ENCODINGS:
            with self.subTest(encoding=encoding), self.assertRaisesRegex(AssertionError, "vertex_indices/0 value/bits"):
                self.verify(self.mutation(encoding, "winding"), encoding)

    def test_signed_zero_is_not_numeric_equality(self):
        for encoding in ENCODINGS:
            with self.subTest(encoding=encoding), self.assertRaisesRegex(AssertionError, "vertex/0/x/0 value/bits"):
                self.verify(self.mutation(encoding, "signed-zero"), encoding)

    def test_truncation_and_extra_body_are_rejected(self):
        for encoding in ENCODINGS:
            raw = self.entry(encoding)
            with self.subTest(encoding=encoding, control="truncate"), self.assertRaises(AssertionError):
                self.verify(raw[:-2] if encoding == "ascii" else raw[:-1], encoding)
            with self.subTest(encoding=encoding, control="extra"), self.assertRaisesRegex(AssertionError, "trailing body"):
                self.verify(raw + b" 0" if encoding == "ascii" else raw + b"\x00", encoding)

    def test_schema_and_byte_bounds_are_not_inferred(self):
        raw = self.entry("ascii")
        with self.assertRaisesRegex(AssertionError, "literal ordered schema/header"):
            self.verify(raw.replace(b"property char sample_i8", b"property uchar sample_i8"), "ascii")
        with self.assertRaisesRegex(AssertionError, "bounded complete header"):
            self.verify(b"ply\n" + b" " * 4096 + raw, "ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-root", required=True, type=Path)
    parser.add_argument("--literal-oracles", required=True, type=Path)
    args = parser.parse_args()
    PhysicalOracleTests.fixture = args.fixture_root.resolve(strict=True)
    PhysicalOracleTests.literal = json.loads(args.literal_oracles.read_text(encoding="utf-8-sig"))
    unittest.main(argv=[__file__], verbosity=2)
