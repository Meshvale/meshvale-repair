# SPDX-License-Identifier: Apache-2.0
"""Conservative targeted repair of owned polygon snapshots."""
from . import _repair
from ._version import __version__
from dataclasses import dataclass
from typing import Iterable, Literal
from meshvale_geometry import Mesh


@dataclass(frozen=True)
class DuplicateResult:
    outcome: Literal["accepted", "unchanged", "rejected"]
    candidate: Mesh | None
    diagnostics: tuple[dict, ...]
    face_map: memoryview
    corner_map: memoryview


def remove_duplicate_faces(mesh: Mesh, targets: Iterable[tuple[int, int]], *,
                           row_local_attributes: Iterable[tuple[str, str]] = ()) -> DuplicateResult:
    """Remove explicit equivalent face copies; a bad target rejects the entire request."""
    if not isinstance(mesh, Mesh):
        raise TypeError("mesh must be a meshvale_geometry.Mesh snapshot")
    result = _repair.remove_duplicates(mesh.to_record(), list(targets), list(row_local_attributes))
    candidate = None if result["candidate_record"] is None else Mesh.from_record(result["candidate_record"])
    return DuplicateResult(result["outcome"], candidate, tuple(result["diagnostics"]),
                           result["face_map"], result["corner_map"])


__all__ = ["DuplicateResult", "remove_duplicate_faces", "__version__"]
