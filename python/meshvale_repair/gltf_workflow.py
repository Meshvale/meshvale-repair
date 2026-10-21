# SPDX-License-Identifier: Apache-2.0
"""Owned path capture and the native reported glTF/GLB repair operation."""
from . import _repair


try:
    GltfExecution = _repair.GltfExecution
    GltfCancellation = _repair.GltfCancellation
    GltfFileWorkflowResult = _repair.GltfFileWorkflowResult
except AttributeError as error:
    raise ImportError("This Repair build does not include the glTF file workflow") from error


# One module-owned session, with native ownership retained by returned records.
_DEFAULT_EXECUTION = GltfExecution()


def repair_gltf_file(
    source, destination, targets, *,
    primitive_ordinal, triangles, values,
    input_id="source", resource_root=None, row_local_attributes=(),
    report_path="receipts/report.json", execution=None, cancellation=None,
    retain_failure_facts=False,
):
    """Run one explicit native primitive repair, optionally publishing its bundle.

    Pass destination=None explicitly for no export and empty targets for unchanged
    processing. Required policies choose the canonical triangle/value view.
    Results retain actual native observations; report and map access is lazy.
    """
    prepared = _repair._prepare_gltf_file(
        source, destination, targets,
        primitive_ordinal=primitive_ordinal, triangles=triangles, values=values,
        input_id=input_id, resource_root=resource_root,
        row_local_attributes=row_local_attributes, report_path=report_path,
        execution=_DEFAULT_EXECUTION if execution is None else execution,
        cancellation=cancellation,
        retain_failure_facts=retain_failure_facts,
    )
    return _repair._run_gltf_file(prepared)


__all__ = [
    "GltfExecution", "GltfCancellation", "GltfFileWorkflowResult", "repair_gltf_file",
]
