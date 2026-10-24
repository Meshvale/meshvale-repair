# SPDX-License-Identifier: Apache-2.0
"""Owned OFF capture and reported native duplicate-face repair."""
from . import _repair

try:
    OffExecution = _repair.OffExecution
    OffCancellation = _repair.OffCancellation
    OffFileWorkflowResult = _repair.OffFileWorkflowResult
except AttributeError as error:
    raise ImportError("This Repair build does not include the OFF file workflow") from error

# Returned native owners retain the session ledger after this wrapper dies.
_DEFAULT_EXECUTION = OffExecution()


def _prepare_off_request(
    source, destination, targets, *, text, edge_count, input_id="source",
    report_path="meshvale-report.json", execution=None, cancellation=None,
):
    """Own conversions and the final result before reading asset contents."""
    return _repair._prepare_off_file(
        source, destination, targets, text=text, edge_count=edge_count,
        input_id=input_id, report_path=report_path,
        execution=_DEFAULT_EXECUTION if execution is None else execution,
        cancellation=cancellation,
    )


_run_off_request = _repair._run_off_file


def repair_off_file(
    source, destination, targets, *, text, edge_count, input_id="source",
    report_path="meshvale-report.json", execution=None, cancellation=None,
):
    """Verify an explicit indexed OFF repair and optionally publish a new bundle.

    Required policies acknowledge canonical ASCII text and unknown edge zero.
    Pass destination=None for genuine memory-only writing/reload and an empty
    target iterable for the existing engine's unchanged operation. Results retain
    actual native facts; report bytes and correspondence observations are lazy.
    """
    prepared = _prepare_off_request(
        source, destination, targets, text=text, edge_count=edge_count,
        input_id=input_id, report_path=report_path,
        execution=execution, cancellation=cancellation,
    )
    return _run_off_request(prepared)


__all__ = [
    "OffExecution", "OffCancellation", "OffFileWorkflowResult", "repair_off_file",
]
