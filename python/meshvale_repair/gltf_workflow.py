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
_STATIC_PROFILE = "static-local-triangles-v1"
_TRANSLATION_PROFILE = "direct-translation-v1"
_DEFORMATION_PROFILE = "deformation-retained-v1"
# An immutable policy handle copies the same native context; no second pool.
_DEFAULT_TRANSLATION_EXECUTION = _DEFAULT_EXECUTION._direct_translation_policy()
_DEFAULT_DEFORMATION_EXECUTION = _DEFAULT_EXECUTION._deformation_policy()


def _prepare_gltf_request(
    source, destination, targets, *,
    primitive_ordinal, triangles, values,
    processing_profile=_STATIC_PROFILE,
    input_id="source", resource_root=None, row_local_attributes=(),
    report_path="receipts/report.json", execution=None, cancellation=None,
    retain_failure_facts=False,
):
    """Own all conversions and the final result without reading an asset."""
    if execution is None:
        if processing_profile == _DEFORMATION_PROFILE:
            execution = _DEFAULT_DEFORMATION_EXECUTION
        elif processing_profile == _TRANSLATION_PROFILE:
            execution = _DEFAULT_TRANSLATION_EXECUTION
        else:
            execution = _DEFAULT_EXECUTION
    return _repair._prepare_gltf_file(
        source, destination, targets,
        primitive_ordinal=primitive_ordinal, triangles=triangles, values=values,
        processing_profile=processing_profile,
        input_id=input_id, resource_root=resource_root,
        row_local_attributes=row_local_attributes, report_path=report_path,
        execution=execution,
        cancellation=cancellation,
        retain_failure_facts=retain_failure_facts,
    )


_run_gltf_request = _repair._run_gltf_file


def repair_gltf_file(
    source, destination, targets, *,
    primitive_ordinal, triangles, values,
    processing_profile=_STATIC_PROFILE,
    input_id="source", resource_root=None, row_local_attributes=(),
    report_path="receipts/report.json", execution=None, cancellation=None,
    retain_failure_facts=False,
):
    """Run one explicit native primitive repair, optionally publishing its bundle.

    Pass destination=None explicitly for no export and empty targets for unchanged
    processing. Required policies choose the canonical triangle/value view.
    Results retain actual native observations; report and map access is lazy.
    """
    prepared = _prepare_gltf_request(
        source, destination, targets,
        primitive_ordinal=primitive_ordinal, triangles=triangles, values=values,
        processing_profile=processing_profile,
        input_id=input_id, resource_root=resource_root,
        row_local_attributes=row_local_attributes, report_path=report_path,
        execution=execution, cancellation=cancellation,
        retain_failure_facts=retain_failure_facts,
    )
    return _run_gltf_request(prepared)


__all__ = [
    "GltfExecution", "GltfCancellation", "GltfFileWorkflowResult", "repair_gltf_file",
]
