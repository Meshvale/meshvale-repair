# SPDX-License-Identifier: Apache-2.0
"""Owned typed PLY capture and reported native duplicate-face repair."""
from . import _repair

try:
    PlyExecution = _repair.PlyExecution
    PlyCancellation = _repair.PlyCancellation
    PlyFileWorkflowResult = _repair.PlyFileWorkflowResult
except AttributeError as error:
    raise ImportError("This Repair build does not include the PLY file workflow") from error

# Returned native owners keep the interpreter's shared session ledger alive.
_DEFAULT_EXECUTION = PlyExecution()


def _prepare_ply_request(
    source, destination, targets, *, encoding, row_local_attributes=(),
    input_id="source", report_path="meshvale-report.json", execution=None,
    cancellation=None,
):
    """Own paths, targets, declarations and result slots before asset capture."""
    return _repair._prepare_ply_file(
        source, destination, targets, encoding=encoding,
        row_local_attributes=row_local_attributes, input_id=input_id,
        report_path=report_path,
        execution=_DEFAULT_EXECUTION if execution is None else execution,
        cancellation=cancellation,
    )


_run_ply_request = _repair._run_ply_file


def repair_ply_file(
    source, destination, targets, *, encoding, row_local_attributes=(),
    input_id="source", report_path="meshvale-report.json", execution=None,
    cancellation=None,
):
    """Verify an explicit typed PLY repair and optionally publish a new bundle.

    Encoding is explicit; canonical serialization preserves the admitted ordered
    schema and typed property values. Caller declarations assert that named
    properties are safe across changed face/corner identities. Their names are
    structurally admitted even for empty targets; unused declarations are valid.
    Empty targets select the actual unchanged engine path without attribute
    semantic eligibility evaluation. Pass destination=None for memory-only
    writing and reload. Actual native facts, maps and bytes are retained lazily.
    """
    prepared = _prepare_ply_request(
        source, destination, targets, encoding=encoding,
        row_local_attributes=row_local_attributes, input_id=input_id,
        report_path=report_path, execution=execution,
        cancellation=cancellation,
    )
    return _run_ply_request(prepared)


__all__ = [
    "PlyExecution", "PlyCancellation", "PlyFileWorkflowResult", "repair_ply_file",
]
