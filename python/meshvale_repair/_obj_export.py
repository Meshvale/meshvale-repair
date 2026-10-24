# SPDX-License-Identifier: Apache-2.0
"""Private native-conversion admission and checked OBJ publication preparation.

Geometry owns triangulation and its native reservation budget. Record inspection,
inverse-map projection and JSON-facing copies here are serial Python marshalling,
outside that budget. Nothing in this module publishes or constructs a receipt.
"""
from dataclasses import dataclass
import sys

import meshvale_geometry as geometry
import meshvale_interchange as interchange


class ExportPreparationError(ValueError):
    """A returned conversion is inconsistent with its supplied source asset."""


class ObjCancellation:
    """One stop request reaches both existing native cancellation sources."""

    __slots__ = ("_io", "_geometry")

    def __init__(self):
        self._io = interchange.Cancellation()
        self._geometry = geometry.Cancellation()

    def request_stop(self):
        first = self._io.request_stop()
        self._geometry.request_stop()
        return first

    @property
    def stop_requested(self):
        return self._io.stop_requested or self._geometry.stop_requested


def io_cancellation(token):
    if token is None or isinstance(token, interchange.Cancellation):
        return token
    if type(token) is ObjCancellation:
        return token._io
    raise TypeError("cancellation must be Interchange Cancellation or ObjCancellation")


def geometry_cancellation(token):
    if token is None:
        return None
    if type(token) is ObjCancellation:
        return token._geometry
    raise TypeError("triangle cancellation must be ObjCancellation or None")


@dataclass(frozen=True)
class TriangleRequest:
    options: geometry.TriangulationOptions
    execution: geometry.ExecutionContext

    def resolved_options(self):
        return {"max_corners_per_face": self.options.max_corners_per_face,
                "minimum_parallel_faces": self.options.minimum_parallel_faces,
                "worker_budget": self.execution.worker_budget,
                "tracked_payload_budget_bytes": self.execution.tracked_payload_budget_bytes}

    def convert(self, mesh, cancellation):
        # Return the genuine result directly: the caller retains its facts before
        # attempting any fallible projection or publication preparation.
        return geometry.triangulate(mesh, options=self.options, execution=self.execution,
                                    cancellation=geometry_cancellation(cancellation))


def resolve_export(output_topology, triangulation_options, triangulation_execution, cancellation):
    if type(output_topology) is not str:
        raise TypeError("output_topology must be a string")
    if output_topology not in ("preserve", "triangles"):
        raise ValueError("output_topology must be preserve or triangles")
    if output_topology == "preserve":
        io_cancellation(cancellation)
        if triangulation_options is not None or triangulation_execution is not None:
            raise ValueError("triangulation arguments require triangle output")
        return None
    geometry_cancellation(cancellation)
    if triangulation_options is None:
        options = geometry.TriangulationOptions()
    elif type(triangulation_options) is geometry.TriangulationOptions:
        # Revalidate the fields as well as the genuine frozen value's type.
        options = geometry.TriangulationOptions(
            max_corners_per_face=triangulation_options.max_corners_per_face,
            minimum_parallel_faces=triangulation_options.minimum_parallel_faces)
    else:
        raise TypeError("triangulation_options must be TriangulationOptions or None")
    if triangulation_execution is None:
        execution = geometry.ExecutionContext()
    elif type(triangulation_execution) is geometry.ExecutionContext:
        execution = triangulation_execution
    else:
        raise TypeError("triangulation_execution must be ExecutionContext or None")
    return TriangleRequest(options, execution)


def _require(condition, message):
    if not condition:
        raise ExportPreparationError(message)


def _extent(count, width=1):
    if count > sys.maxsize // width:
        raise OverflowError("conversion projection exceeds native extent")
    return count * width


def _view(value, label):
    _require(type(value) is memoryview and value.ndim == 1 and value.c_contiguous,
             label + " must be a contiguous native buffer")
    _require(value.readonly, label + " must be owned readonly storage")
    return value


def _unsigned(value, label):
    view = _view(value, label)
    _require(view.itemsize == 8 and view.format.lstrip("@=") in ("Q", "L"),
             label + " must contain native uint64 values")
    return view


def _mesh_layout(mesh, label):
    _require(isinstance(mesh, geometry.Mesh), label + " must be a genuine Mesh")
    record = mesh.to_record()
    positions = _view(record["positions"], label + " positions")
    offsets = _unsigned(record["face_offsets"], label + " offsets")
    corners = _unsigned(record["corner_vertices"], label + " corners")
    _require(positions.itemsize == 8 and positions.format.lstrip("@=") == "d",
             label + " positions must contain binary64 values")
    _require(len(positions) == _extent(mesh.vertex_count, 3), label + " position extent differs")
    _require(len(offsets) == mesh.face_count + 1 and len(corners) == mesh.corner_count,
             label + " topology extent differs")
    _require(offsets[0] == 0 and offsets[-1] == len(corners), label + " offsets are incomplete")
    _require(all(offsets[i + 1] >= offsets[i] + 3 for i in range(mesh.face_count)),
             label + " contains an invalid face range")
    _require(all(vertex < mesh.vertex_count for vertex in corners), label + " vertex index is out of range")
    return record, offsets, corners


def _channel_layout(channel, rows):
    width = channel["components"]
    _require(type(width) is int and width > 0, "invalid channel width")
    values = _view(channel["values"], "channel values")
    offsets = channel["offsets"]
    if offsets is None:
        _require(len(values) == _extent(rows, width), "dense channel extent differs")
    else:
        offsets = _unsigned(offsets, "channel offsets")
        _require(len(offsets) == rows + 1 and offsets[0] == 0 and offsets[-1] == len(values),
                 "ragged channel extent differs")
        _require(all(value % width == 0 for value in offsets) and
                 all(offsets[i] <= offsets[i + 1] for i in range(rows)), "invalid ragged channel ranges")
    present = channel["present"]
    if present is not None:
        present = _view(present, "channel presence")
        _require(present.itemsize == 1 and present.format.lstrip("@=") == "B" and
                 len(present) == rows and all(value in (0, 1) for value in present), "invalid channel presence")
    return values, offsets, present


def _verify_channels(source, output, vertex_count, face_sources, corner_sources, source_faces, source_corners):
    _require(len(source) == len(output), "conversion changed the channel inventory")
    domains = {"vertex": (vertex_count, range(vertex_count)),
               "face": (source_faces, face_sources), "corner": (source_corners, corner_sources)}
    for before, after in zip(source, output):
        metadata = lambda row: {key: value for key, value in row.items() if key not in ("values", "offsets", "present")}
        _require(metadata(before) == metadata(after), "conversion changed channel metadata or ordering")
        _require(before["domain"] in domains, "unsupported channel domain")
        row_count, mapping = domains[before["domain"]]
        original, original_offsets, original_present = _channel_layout(before, row_count)
        converted, converted_offsets, converted_present = _channel_layout(after, len(mapping))
        _require(original.format == converted.format and original.itemsize == converted.itemsize,
                 "conversion changed channel scalar encoding")
        _require((original_offsets is None) == (converted_offsets is None), "conversion changed channel row layout")
        _require((original_present is None) == (converted_present is None), "conversion changed presence layout")
        original_bytes, converted_bytes = original.cast("B"), converted.cast("B")
        width, scalar_bytes = before["components"], original.itemsize
        for destination, source_row in enumerate(mapping):
            if original_offsets is None:
                start, end = source_row * width, (source_row + 1) * width
                target_start, target_end = destination * width, (destination + 1) * width
            else:
                start, end = original_offsets[source_row], original_offsets[source_row + 1]
                target_start, target_end = converted_offsets[destination], converted_offsets[destination + 1]
            _require(original_bytes[start * scalar_bytes:end * scalar_bytes] ==
                     converted_bytes[target_start * scalar_bytes:target_end * scalar_bytes],
                     "conversion changed a channel row or missing backing bytes")
            if original_present is not None:
                _require(original_present[source_row] == converted_present[destination], "conversion changed authored presence")


def _inverse_rows(rows, output_count):
    _require(all(rows), "conversion omitted a source row")
    if len(rows) == output_count and all(row == [index] for index, row in enumerate(rows)):
        return None
    return tuple(row[0] if len(row) == 1 else tuple(row) for row in rows)


def _json_rows(rows):
    return None if rows is None else [list(row) if type(row) is tuple else row for row in rows]


@dataclass(frozen=True)
class TriangleExportPlan:
    asset: interchange.ObjFileAsset
    _face_rows: tuple | None
    _corner_rows: tuple | None
    topology_changed: bool

    @property
    def face_map(self):
        return _json_rows(self._face_rows)

    @property
    def corner_map(self):
        return _json_rows(self._corner_rows)


def prepare_triangle_asset(candidate, native_result):
    _require(isinstance(candidate, interchange.ObjFileAsset), "candidate must be a genuine ObjFileAsset")
    _require(type(native_result) is geometry.TriangulationResult and native_result.status == "accepted" and
             native_result.candidate is not None, "preparation requires an accepted genuine native result")
    before, source_offsets, source_corners = _mesh_layout(candidate.document.mesh, "source")
    after, output_offsets, output_corners = _mesh_layout(native_result.candidate, "output")
    source = candidate.document.mesh
    output = native_result.candidate
    _extent(source.face_count, 8)
    _extent(source.corner_count, 8)
    _require(output.vertex_count == source.vertex_count and
             before["positions"].cast("B") == after["positions"].cast("B"), "conversion changed vertex storage")
    _require(output.corner_count == _extent(output.face_count, 3) and
             all(output_offsets[index + 1] - output_offsets[index] == 3 for index in range(output.face_count)),
             "conversion output is not complete triangles")
    faces = _unsigned(native_result.face_sources, "native face sources")
    corners = _unsigned(native_result.corner_sources, "native corner sources")
    ranges = _unsigned(native_result.face_output_offsets, "native face ranges")
    _require(len(faces) == output.face_count and len(corners) == output.corner_count and
             len(ranges) == source.face_count + 1 and ranges[0] == 0 and ranges[-1] == output.face_count,
             "native correspondence extents differ")
    face_rows = [[] for _ in range(source.face_count)]
    corner_rows = [[] for _ in range(source.corner_count)]
    for face in range(source.face_count):
        _require(ranges[face] < ranges[face + 1] <= output.face_count, "native face range is invalid")
        _require(ranges[face + 1] - ranges[face] == source_offsets[face + 1] - source_offsets[face] - 2,
                 "native triangle count disagrees with the complete simple loop")
        for destination in range(ranges[face], ranges[face + 1]):
            _require(faces[destination] == face, "native face ranges disagree with source identities")
            face_rows[face].append(destination)
            triangle = corners[destination * 3:destination * 3 + 3]
            _require(len(set(triangle)) == 3, "triangle repeats a source corner")
            for target in range(destination * 3, destination * 3 + 3):
                original = corners[target]
                _require(source_offsets[face] <= original < source_offsets[face + 1],
                         "triangle corner belongs to another source face")
                _require(output_corners[target] == source_corners[original], "triangle vertex disagrees with source corner")
                corner_rows[original].append(target)
    _verify_channels(before["attributes"], after["attributes"], source.vertex_count, faces, corners,
                     source.face_count, source.corner_count)
    face_map = _inverse_rows(face_rows, output.face_count)
    corner_map = _inverse_rows(corner_rows, output.corner_count)
    changed = (before["face_offsets"].cast("B") != after["face_offsets"].cast("B") or
               before["corner_vertices"].cast("B") != after["corner_vertices"].cast("B"))
    return TriangleExportPlan(candidate.with_mesh(output), face_map, corner_map, changed)
