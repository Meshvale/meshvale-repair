# SPDX-License-Identifier: Apache-2.0
"""Literal OFF corpus and byte oracles, independent of the binding/codec."""
import hashlib
import os
import subprocess
from pathlib import Path
import struct

ENTRY = "source.off"
RECEIPT = "meshvale-report.json"
POLICIES = {"text": "canonical-ascii", "edge_count": "unknown-zero"}
EXTENSION = "meshvale.repair.off"
SOURCE = (
    b"# Original indexed polygons; retain these exact bytes.\nOFF\n8 3 17\n"
    b"-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    b"4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n"
)
EMPTY = b"# real empty source\nOFF\n0 0 17\n"
POINT = b"OFF\n1 0 17\n-0 2 3\n"
REVERSED = SOURCE.replace(b"4 2 3 0 1\n", b"4 0 3 2 1\n")
DISTINCT_IDS = SOURCE.replace(b"8 3 17\n", b"9 3 17\n").replace(
    b"99 -0 5\n", b"99 -0 5\n-0 0 0\n"
).replace(b"4 2 3 0 1\n", b"4 8 1 2 3\n")
POSITION_BITS = (
    0x8000000000000000, 0, 0, 0x3FF0000000000000, 0, 0,
    0x3FF0000000000000, 0x3FF0000000000000, 0, 0, 0x3FF0000000000000, 0,
    0x4000000000000000, 0, 0, 0x4008000000000000, 0, 0,
    0x4000000000000000, 0x3FF0000000000000, 0, 0x4058C00000000000,
    0x8000000000000000, 0x4014000000000000,
)
FACE_MAP = (0, 0, 1)
CORNER_MAP = (0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6)
SOURCE_FACES = (0, 2)
SOURCE_CORNERS = (0, 1, 2, 3, 8, 9, 10)
CHECKS = (
    "source-storage", "target-policy", "candidate-storage", "repair-correspondence",
    "canonical-preservation", "reload-candidate", "source-unchanged",
    "staged-candidate", "staged-physical-members",
)


def fixture(root, content=SOURCE, entry=ENTRY):
    root = Path(root)
    root.mkdir(parents=True)
    path = root / entry
    path.write_bytes(content)
    return path


def physical(root):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in Path(root).rglob("*") if p.is_file()}


def digest(content):
    return hashlib.sha256(content).hexdigest()


def check_output(test, content, changed, empty=False):
    """Check only the known canonical fixture grammar, never a product parser."""
    lines = content.decode("ascii").splitlines()
    test.assertEqual(lines[0], "OFF")
    vertices, faces, edge = map(int, lines[1].split())
    test.assertEqual((vertices, faces, edge), (0, 0, 0) if empty else (8, 2 if changed else 3, 0))
    positions = tuple(struct.unpack("<Q", struct.pack("<d", float(word)))[0]
                      for line in lines[2:2 + vertices] for word in line.split())
    test.assertEqual(positions, () if empty else POSITION_BITS)
    rows = [list(map(int, line.split())) for line in lines[2 + vertices:]]
    test.assertTrue(all(row[0] == len(row) - 1 for row in rows))
    test.assertEqual([row[1:] for row in rows], [] if empty else
                     [[0, 1, 2, 3], [4, 5, 6]] if changed else
                     [[0, 1, 2, 3], [2, 3, 0, 1], [4, 5, 6]])


def maps(changed, empty=False):
    if empty:
        return (), (), (), ()
    return ((FACE_MAP, CORNER_MAP, SOURCE_FACES, SOURCE_CORNERS) if changed else
            (tuple(range(3)), tuple(range(11)), tuple(range(3)), tuple(range(11))))


def directory_alias(test, alias, target):
    """Create only an ordinary owned folder alias; remove only that alias."""
    if os.name == "nt":
        environment = os.environ.copy()
        environment["MESHVALE_TEST_ALIAS"] = str(alias)
        environment["MESHVALE_TEST_TARGET"] = str(target)
        process = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command",
            "New-Item -ItemType Junction -Path $env:MESHVALE_TEST_ALIAS "
            "-Target $env:MESHVALE_TEST_TARGET -ErrorAction Stop | Out-Null"],
            env=environment, capture_output=True, timeout=30)
        test.assertEqual(process.returncode, 0, process.stderr)
        test.addCleanup(alias.rmdir)
    else:
        alias.symlink_to(target, target_is_directory=True)
        test.addCleanup(alias.unlink)
    return alias
