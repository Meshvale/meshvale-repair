# SPDX-License-Identifier: Apache-2.0
"""Prepared, owned OFF repair to annotated GLB export.

Native feature discovery is lazy, so importing this module captures nothing.
"""
import os
from pathlib import Path


_NATIVE_TYPES = ("OffGlbExecution", "PreparedOffGlbExport", "OffGlbExportResult",
                 "OffCancellation")
_MAX_PATH_BYTES = 4096


def _native():
    from . import _repair
    if not hasattr(_repair, "_prepare_off_glb_export"):
        raise ImportError("This Repair build does not include OFF to GLB export")
    return _repair


def __getattr__(name):
    if name in _NATIVE_TYPES:
        return getattr(_native(), name)
    raise AttributeError(name)


def _logical_path(value, base):
    value = os.fspath(value)
    if (type(value) is not str or not value or "\0" in value or
            len(value) > _MAX_PATH_BYTES or len(value.encode("utf-8")) > _MAX_PATH_BYTES):
        raise ValueError("Invalid OFF to GLB path representation")
    # Preserve spelling for native IO, including aliases rejected by capture.
    result = os.path.join(base, value)
    if not os.path.isabs(result):
        # A Windows drive-relative spelling can name another drive's cwd.
        # Snapshot that drive's cwd alone, retaining the input components.
        drive, _ = os.path.splitdrive(result)
        result = os.path.join(os.path.abspath(drive + "."), result)
    if len(result.encode("utf-8")) > _MAX_PATH_BYTES:
        raise ValueError("OFF to GLB path exceeds its conversion limit")
    return result


def _comparison_path(value):
    try:
        return Path(value).resolve()
    except (OSError, RuntimeError):
        # Resolution is only a comparison aid, never a replacement IO path or
        # an invented native diagnosis when an ancestor cannot be resolved.
        return None


def _overlap(first, second):
    return (first is not None and second is not None and
            (first == second or first in second.parents or second in first.parents))


def prepare_off_glb_export(
    source, destination, targets, *, text, edge_count, coordinates, precision,
    max_position_error, acknowledge_triangle_conversion,
    acknowledge_corner_expansion, acknowledge_unused_vertices,
    input_id="source", receipt_path="publication.json", preset="balanced-v1",
):
    """Own and validate declarations without reading the source or publishing."""
    base = os.getcwd()
    source = _logical_path(source, base)
    destination = _logical_path(destination, base)
    if _overlap(_comparison_path(source), _comparison_path(destination)):
        raise ValueError("OFF source and destination domains overlap")
    return _native()._prepare_off_glb_export(
        source, destination, targets, text=text, edge_count=edge_count,
        coordinates=coordinates, precision=precision,
        max_position_error=max_position_error,
        acknowledge_triangle_conversion=acknowledge_triangle_conversion,
        acknowledge_corner_expansion=acknowledge_corner_expansion,
        acknowledge_unused_vertices=acknowledge_unused_vertices,
        input_id=input_id, receipt_path=receipt_path, preset=preset,
    )


def run_off_glb_export(prepared, *, execution=None, cancellation=None):
    """Capture once and execute the native workflow once with retained owners."""
    return _native()._run_off_glb_export(
        prepared, execution=execution, cancellation=cancellation)


def repair_off_to_glb(
    source, destination, targets, *, text, edge_count, coordinates, precision,
    max_position_error, acknowledge_triangle_conversion,
    acknowledge_corner_expansion, acknowledge_unused_vertices,
    input_id="source", receipt_path="publication.json", preset="balanced-v1",
    execution=None, cancellation=None,
):
    """Prepare the explicit OFF declaration, then run its fresh operation."""
    prepared = prepare_off_glb_export(
        source, destination, targets, text=text, edge_count=edge_count,
        coordinates=coordinates, precision=precision,
        max_position_error=max_position_error,
        acknowledge_triangle_conversion=acknowledge_triangle_conversion,
        acknowledge_corner_expansion=acknowledge_corner_expansion,
        acknowledge_unused_vertices=acknowledge_unused_vertices,
        input_id=input_id, receipt_path=receipt_path, preset=preset,
    )
    return run_off_glb_export(prepared, execution=execution, cancellation=cancellation)


__all__ = [*_NATIVE_TYPES, "prepare_off_glb_export", "run_off_glb_export",
           "repair_off_to_glb"]
