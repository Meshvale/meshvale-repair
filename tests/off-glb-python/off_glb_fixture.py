# SPDX-License-Identifier: Apache-2.0
"""Literal OFF data and independent physical GLB/capsule/map oracles.

No Repair/Interchange parser, encoder, observer or expected native output is used.
The same small corpus serves installed API and actual command subprocess tests.
"""
from collections import Counter
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct


POLICIES = dict(text="canonical-ascii", edge_count="unknown-zero",
    coordinates="meters-right-handed-y-up", precision="exact32",
    max_position_error=0.0, acknowledge_triangle_conversion=True,
    acknowledge_corner_expansion=True, acknowledge_unused_vertices=True)
FACE_MAP = (0, 0, 1, 2)
CORNER_MAP = (0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 11)
SOURCE_FACES = (0, 2, 3)
SOURCE_CORNERS = (0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15)
R_OFFSETS = (0, 4, 7, 12)
R_CORNERS = tuple(range(12))
O_OFFSETS = (0, 4, 8, 11, 16)
O_CORNERS = (0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 11)
MAPS = ("repair_face_map", "repair_corner_map", "repair_source_faces",
        "repair_source_corners")
OUTPUT_MAPS = ("wire_triangle_sources", "wire_triangle_faces", "wire_corner_sources",
    "wire_corner_corners", "wire_vertex_sources", "source_vertex_offsets",
    "source_vertex_wire_members", "source_face_offsets", "source_face_wire_triangles",
    "source_corner_offsets", "source_corner_wire_corners")
CAPSULE_MAPS = ("triangle_sources", "triangle_faces", "corner_sources", "corner_corners",
    "vertex_sources", "source_vertex_offsets", "source_vertex_members",
    "source_face_offsets", "source_face_members", "source_corner_offsets", "source_corner_members")


def source_bytes(name="mixed"):
    return (Path(__file__).with_name("fixtures") / (name + ".off")).read_bytes()


def fixture(root, name="mixed"):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    source = root / "source.off"
    source.write_bytes(source_bytes(name))
    return source


def physical(root):
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in Path(root).rglob("*") if path.is_file()}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False,
                      separators=(",", ":")).encode("utf-8")


def job(identity, *, source="source.off", destination=None, targets=((0, 1),), **options):
    return dict(id=identity, input=str(source), destination=str(destination or ("out/" + identity)),
                targets=[list(pair) for pair in targets], **(POLICIES | options))


def request(*jobs):
    return {"schema": "meshvale.repair.off-glb-jobs/1", "jobs": list(jobs)}


def enum(test, value, name):
    test.assertEqual(value, getattr(type(value), name))


def u64(test, value):
    test.assertIs(type(value), str)
    test.assertRegex(value, r"^(?:0|[1-9][0-9]*)$")
    integer = int(value)
    test.assertLessEqual(integer, (1 << 64) - 1)
    return integer


def readonly(test, view, code):
    test.assertIsInstance(view, memoryview)
    test.assertTrue(view.readonly)
    test.assertEqual(view.format, code)
    if len(view):
        with test.assertRaises(TypeError):
            view[0] = view[0]
    return tuple(view)


def mesh(test, value, positions, offsets, corners):
    test.assertEqual((value.vertex_count, value.face_count, value.corner_count,
                      value.attribute_count), (len(positions) // 3, len(offsets) - 1, len(corners), 0))
    actual = readonly(test, value.positions, "d")
    test.assertEqual(struct.pack("<" + "d" * len(actual), *actual), positions_bits(positions))
    test.assertEqual(readonly(test, value.face_offsets, "Q"), tuple(offsets))
    test.assertEqual(readonly(test, value.corner_vertices, "Q"), tuple(corners))


def positions(name="mixed"):
    lines = source_bytes(name).decode("ascii").splitlines()
    count = int(lines[1].split()[0])
    return tuple(float(word) for row in lines[2:2 + count] for word in row.split())


def topology(name="mixed"):
    """Read only this literal fixture's deliberately tiny, comment-free grammar."""
    lines = source_bytes(name).decode("ascii").splitlines()
    vertices, faces, _ = map(int, lines[1].split())
    offsets, corners = [0], []
    for line in lines[2 + vertices:2 + vertices + faces]:
        count, *row = map(int, line.split())
        if count != len(row):
            raise AssertionError("Invalid literal test data")
        corners.extend(row)
        offsets.append(len(corners))
    return tuple(offsets), tuple(corners)


def positions_bits(values):
    return struct.pack("<" + "d" * len(values), *values)


def repair_maps(test, result, changed=True):
    expected = ((FACE_MAP, CORNER_MAP, SOURCE_FACES, SOURCE_CORNERS) if changed else
                (tuple(range(4)), tuple(range(16)), tuple(range(4)), tuple(range(16))))
    for name, values in zip(MAPS, expected):
        test.assertEqual(readonly(test, getattr(result, name), "Q"), values)
    test.assertEqual(result.repair_vertex_identity, range(14))


def triangulation(test, result, changed=True, name="mixed"):
    """Exact directed boundaries/positive area admit every legal ear order."""
    offsets, corners = (R_OFFSETS, R_CORNERS) if changed else (O_OFFSETS, O_CORNERS)
    fs = readonly(test, result.triangulation_face_sources, "Q")
    cs = readonly(test, result.triangulation_corner_sources, "Q")
    fos = readonly(test, result.triangulation_face_output_offsets, "Q")
    test.assertEqual(fs, (0, 0, 1, 2, 2, 2) if changed else (0, 0, 1, 1, 2, 3, 3, 3))
    test.assertEqual(fos, (0, 2, 3, 6) if changed else (0, 2, 4, 5, 8))
    test.assertEqual(len(cs), 3 * len(fs))
    test.assertEqual(result.triangulation_vertex_identity, range(14))
    tc = tuple(corners[corner] for corner in cs)
    mesh(test, result.triangulated_mesh, positions(name), range(0, len(cs) + 1, 3), tc)
    triangle_coverage(test, positions(name), offsets, corners, fs, cs, fos, tc)
    return fs, cs, tc


def triangle_coverage(test, position_values, offsets, corners, fs, cs, fos, tc):
    """Positive oriented chains with exactly the polygon boundary cover it once.

    Exact directed cancellation also rules out interior overlap/exterior area:
    every triangle has positive winding and their sum has the polygon's boundary.
    This checks the concave face without selecting its ears or a diagonal.
    """
    test.assertEqual(len(cs), 3 * len(fs))
    test.assertEqual(len(tc), len(cs))
    test.assertEqual(len(fos), len(offsets))
    test.assertEqual((fos[0], fos[-1]), (0, len(fs)))
    test.assertEqual(tuple(tc), tuple(corners[c] for c in cs))
    xy = [tuple(Fraction(v) for v in position_values[3 * i:3 * i + 2])
          for i in range(len(position_values) // 3)]
    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    for face in range(len(offsets) - 1):
        ids = corners[offsets[face]:offsets[face + 1]]
        boundary = Counter(zip(ids, ids[1:] + ids[:1]))
        edges, area = Counter(), Fraction(0)
        for triangle in range(fos[face], fos[face + 1]):
            test.assertEqual(fs[triangle], face)
            authored = cs[3 * triangle:3 * triangle + 3]
            test.assertTrue(all(offsets[face] <= c < offsets[face + 1] for c in authored))
            abc = tc[3 * triangle:3 * triangle + 3]
            a = cross(*(xy[v] for v in abc))
            test.assertGreater(a, 0)
            area += a
            edges.update(zip(abc, abc[1:] + abc[:1]))
        for edge, count in list(edges.items()):
            reverse = (edge[1], edge[0])
            cancel = min(edges[edge], edges[reverse])
            edges[edge] -= cancel
            edges[reverse] -= cancel
        test.assertEqual(+edges, boundary)
        polygon_area = sum(xy[a][0] * xy[b][1] - xy[a][1] * xy[b][0]
                           for a, b in zip(ids, ids[1:] + ids[:1]))
        test.assertEqual(area, polygon_area)


def csr(test, offsets, members, forward, count):
    test.assertEqual(len(offsets), count + 1)
    test.assertEqual((offsets[0], offsets[-1]), (0, len(members)))
    for source in range(count):
        test.assertEqual(tuple(members[offsets[source]:offsets[source + 1]]),
                         tuple(i for i, value in enumerate(forward) if value == source))


def payload(test, files, *, result=None, changed=True, name="mixed", vertices=14):
    """Read the physical format independently, then relate it to retained owners."""
    test.assertEqual(set(files), {"surface.glb", "surface.bin", "surface.json", "publication.json"})
    report = json.loads(files["publication.json"])
    test.assertEqual(set(report), {"schema", "phase", "encoding", "representation",
                                  "entry_path", "payload_members", "observations", "caller_report"})
    test.assertEqual(report["schema"], "meshvale.surface-publication/1")
    test.assertEqual((report["phase"], report["encoding"], report["representation"], report["entry_path"]),
                     ("staged-payload-verified", "glb", "annotated-bundle-v1", "surface.glb"))
    test.assertEqual([m["path"] for m in report["payload_members"]],
                     ["surface.bin", "surface.glb", "surface.json"])
    for member in report["payload_members"]:
        test.assertEqual(set(member), {"kind", "path", "byte_length", "sha256"})
        raw = files[member["path"]]
        test.assertEqual(u64(test, member["byte_length"]), len(raw))
        test.assertEqual(member["sha256"], sha(raw))
    caller = report["caller_report"]
    test.assertEqual(set(caller), {"encoding", "byte_length", "sha256", "text"})
    caller_raw = caller["text"].encode("utf-8")
    test.assertEqual(caller["encoding"], "utf-8")
    test.assertEqual(u64(test, caller["byte_length"]), len(caller_raw))
    test.assertEqual(caller["sha256"], sha(caller_raw))
    if result is not None:
        test.assertEqual(files["publication.json"], result.publication_receipt_bytes)
        test.assertEqual(caller_raw, result.off_report_bytes)
    capsule = json.loads(files["surface.json"])
    test.assertEqual((capsule["schema"], capsule["encoding"], capsule["representation"]),
                     ("meshvale.surface-bundle/1", "glb", "annotated-bundle-v1"))
    test.assertEqual({m["path"] for m in capsule["members"]}, {"surface.glb", "surface.bin"})
    for member in capsule["members"]:
        test.assertEqual(u64(test, member["bytes"]), len(files[member["path"]]))
        test.assertEqual(member["sha256"], sha(files[member["path"]]))
    binary, cursor = files["surface.bin"], 0
    def sliced(spec, scalar="u64"):
        nonlocal cursor
        test.assertEqual(set(spec), {"offset", "count", "bytes", "scalar"})
        start, count, length = (u64(test, spec[k]) for k in ("offset", "count", "bytes"))
        test.assertEqual(spec["scalar"], scalar)
        test.assertEqual(length, 8 * count)
        aligned = (cursor + 7) // 8 * 8
        test.assertEqual(start, aligned)
        test.assertEqual(binary[cursor:aligned], b"\0" * (aligned - cursor))
        test.assertLessEqual(start + length, len(binary))
        cursor = start + length
        return binary[start:cursor]
    offsets, corners = ((R_OFFSETS, R_CORNERS) if changed and name in ("mixed", "mixed-shift")
                        else topology(name))
    polygons = len(offsets) - 1
    triangles = sum(offsets[i + 1] - offsets[i] - 2 for i in range(polygons))
    triangle_corners = ()
    for domain, faces, ncorner in (("original", polygons, len(corners) if polygons else 0),
                                    ("triangulated", triangles, triangles * 3)):
        snap = capsule[domain]
        test.assertEqual(set(snap), {"counts", "positions", "face_offsets", "corner_vertices", "attributes"})
        expected_counts = {"vertices": vertices, "faces": faces, "corners": ncorner,
            "face_offsets": faces + 1, "attributes": 0, "attribute_scalars": 0,
            "attribute_bytes": 0, "attribute_offsets": 0, "presence_bytes": 0,
            "metadata_entries": 0, "descriptor_bytes": 0}
        test.assertEqual({k: u64(test, v) for k, v in snap["counts"].items()}, expected_counts)
        test.assertEqual(snap["attributes"], [])
        test.assertEqual(sliced(snap["positions"], "f64"), positions_bits(positions(name)))
        off = struct.unpack("<" + "Q" * (faces + 1), sliced(snap["face_offsets"]))
        test.assertEqual(off, (offsets if domain == "original" else tuple(range(0, ncorner + 1, 3)))
                         if polygons else (0,))
        raw_corners = sliced(snap["corner_vertices"])
        if domain == "original":
            test.assertEqual(raw_corners, struct.pack("<" + "Q" * ncorner, *(corners if polygons else ())))
        elif result is not None:
            test.assertEqual(raw_corners, struct.pack("<" + "Q" * ncorner, *result.triangulated_mesh.corner_vertices))
        if domain == "triangulated":
            triangle_corners = struct.unpack("<" + "Q" * ncorner, raw_corners)
    charged_maps = {}
    for key, getter in (("face_sources", "triangulation_face_sources"),
                        ("corner_sources", "triangulation_corner_sources"),
                        ("face_output_offsets", "triangulation_face_output_offsets")):
        raw = sliced(capsule["triangulation"][key])
        charged_maps[key] = struct.unpack("<" + "Q" * (len(raw) // 8), raw)
        if result is not None:
            expected = tuple(getattr(result, getter))
            test.assertEqual(raw, struct.pack("<" + "Q" * len(expected), *expected))
    triangle_coverage(test, positions(name), offsets, corners, charged_maps["face_sources"],
        charged_maps["corner_sources"], charged_maps["face_output_offsets"], triangle_corners)
    wire_maps = {}
    for key, getter in zip(CAPSULE_MAPS, OUTPUT_MAPS):
        raw = sliced(capsule["wire"]["maps"][key])
        values = struct.unpack("<" + "Q" * (len(raw) // 8), raw)
        wire_maps[key] = values
        if result is not None:
            test.assertEqual(values, readonly(test, getattr(result.output, getter), "Q"))
    test.assertEqual(cursor, len(binary))
    test.assertEqual(wire_maps["triangle_sources"], tuple(range(triangles)))
    test.assertEqual(wire_maps["corner_sources"], tuple(range(triangles * 3)))
    test.assertEqual(wire_maps["triangle_faces"], tuple(charged_maps["face_sources"][i]
        for i in wire_maps["triangle_sources"]))
    test.assertEqual(wire_maps["corner_corners"], tuple(charged_maps["corner_sources"][i]
        for i in wire_maps["corner_sources"]))
    test.assertEqual(wire_maps["vertex_sources"], tuple(corners[i] for i in wire_maps["corner_corners"]))
    for label, count, forward in (("vertex", vertices, wire_maps["vertex_sources"]),
        ("face", polygons, wire_maps["triangle_faces"]),
        ("corner", len(corners) if polygons else 0, wire_maps["corner_corners"])):
        csr(test, wire_maps["source_" + label + "_offsets"],
            wire_maps["source_" + label + "_members"], forward, count)
    raw = files["surface.glb"]
    test.assertEqual(struct.unpack_from("<III", raw), (0x46546C67, 2, len(raw)))
    chunks, pos = [], 12
    while pos < len(raw):
        length, kind = struct.unpack_from("<II", raw, pos)
        test.assertEqual(length % 4, 0)
        test.assertLessEqual(pos + 8 + length, len(raw))
        chunks.append((kind, raw[pos + 8:pos + 8 + length]))
        pos += length + 8
    test.assertEqual(pos, len(raw))
    test.assertEqual([k for k, _ in chunks], [0x4E4F534A, 0x004E4942] if triangles else [0x4E4F534A])
    gltf = json.loads(chunks[0][1])
    test.assertEqual(gltf["asset"]["version"], "2.0")
    for absent in ("materials", "textures", "images", "skins", "animations"):
        test.assertNotIn(absent, gltf)
    if not triangles:
        test.assertEqual((gltf["scene"], gltf["scenes"]), (0, [{}]))
        for absent in ("nodes", "meshes", "accessors", "buffers", "bufferViews"):
            test.assertNotIn(absent, gltf)
        return report
    test.assertIs(type(gltf["scene"]), int)
    test.assertEqual(gltf["scene"], 0)
    test.assertEqual(gltf["scenes"], [{"nodes": [0]}])
    test.assertEqual(gltf["nodes"], [{"mesh": 0}])
    primitive, = gltf["meshes"][0]["primitives"]
    test.assertIs(type(primitive.get("mode", 4)), int)
    test.assertEqual(primitive.get("mode", 4), 4)
    test.assertEqual(len(gltf["meshes"]), 1)
    test.assertEqual(len(gltf["buffers"]), 1)
    test.assertEqual(len(gltf["accessors"]), 2)
    test.assertEqual(len(gltf["bufferViews"]), 2)
    test.assertNotIn("uri", gltf["buffers"][0])
    buffer_length = gltf["buffers"][0]["byteLength"]
    test.assertLessEqual(buffer_length, len(chunks[1][1]))
    test.assertLess(len(chunks[1][1]) - buffer_length, 4)
    test.assertEqual(chunks[1][1][buffer_length:], b"\0" * (len(chunks[1][1]) - buffer_length))
    test.assertEqual(set(primitive["attributes"]), {"POSITION"})
    test.assertNotIn("material", primitive)
    def gltf_integer(value):
        test.assertIs(type(value), int)
        test.assertGreaterEqual(value, 0)
        return value
    buffer_length = gltf_integer(buffer_length)
    gltf_integer(gltf["scenes"][0]["nodes"][0])
    gltf_integer(gltf["nodes"][0]["mesh"])
    ranges = []
    def accessor(index, component, shape, width, target):
        index = gltf_integer(index)
        test.assertLess(index, len(gltf["accessors"]))
        a = gltf["accessors"][index]
        test.assertNotIn("sparse", a)
        test.assertNotIn("normalized", a)
        test.assertTrue(set(a) <= {"bufferView", "byteOffset", "componentType", "count", "type", "min", "max"})
        gltf_integer(a["componentType"])
        gltf_integer(a["count"])
        test.assertEqual((a["componentType"], a["type"], a["count"]), (component, shape, triangles * 3))
        test.assertGreater(a["count"], 0)
        view_index = gltf_integer(a["bufferView"])
        test.assertLess(view_index, len(gltf["bufferViews"]))
        b = gltf["bufferViews"][view_index]
        test.assertNotIn("byteStride", b)
        test.assertTrue(set(b) <= {"buffer", "byteOffset", "byteLength", "target"})
        gltf_integer(b["buffer"])
        test.assertEqual(b["buffer"], 0)
        if "target" in b:
            test.assertEqual(gltf_integer(b["target"]), target)
        view_start = gltf_integer(b.get("byteOffset", 0))
        view_length = gltf_integer(b["byteLength"])
        accessor_start = gltf_integer(a.get("byteOffset", 0))
        # Both selected component types are four-byte scalars. These are tightly
        # packed blocks, with all ranges checked against the logical buffer.
        test.assertEqual(view_start % 4, 0)
        test.assertEqual(accessor_start % 4, 0)
        test.assertGreater(view_length, 0)
        test.assertLessEqual(view_start + view_length, buffer_length)
        start = view_start + accessor_start
        test.assertEqual(start % 4, 0)
        length = width * triangles * 3
        test.assertLessEqual(start + length, buffer_length)
        test.assertLessEqual(accessor_start + length, view_length)
        test.assertEqual((view_start, view_length), (start, length))
        ranges.append((start, start + length, view_index))
        return a, chunks[1][1][start:start + length]
    position_accessor, p = accessor(primitive["attributes"]["POSITION"], 5126, "VEC3", 12, 34962)
    values = positions(name)
    # Every accepted value is F32-exact except the independently specified shift:
    # its known RNE32 word is 0x40a00000 (5), with error exactly 2^-30.
    expected = tuple(5.0 if name == "mixed-shift" and 3 * index + component == 15 else v
        for index in wire_maps["vertex_sources"]
        for component, v in enumerate(values[3 * index:3 * index + 3]))
    test.assertEqual(p, struct.pack("<" + "f" * len(expected), *expected))
    decoded_positions = struct.unpack("<" + "f" * (3 * triangles * 3), p)
    for field, reduction in (("min", min), ("max", max)):
        actual = position_accessor[field]
        test.assertIs(type(actual), list)
        test.assertEqual(len(actual), 3)
        test.assertTrue(all(type(value) in (int, float) for value in actual))
        test.assertEqual(actual, [reduction(decoded_positions[axis::3]) for axis in range(3)])
    _, indices = accessor(primitive["indices"], 5125, "SCALAR", 4, 34963)
    test.assertEqual(indices, struct.pack("<" + "I" * (triangles * 3), *range(triangles * 3)))
    test.assertNotEqual(ranges[0][2], ranges[1][2])
    test.assertEqual((ranges[0][0], ranges[0][1], ranges[1][1]),
                     (0, ranges[1][0], buffer_length))
    return report
