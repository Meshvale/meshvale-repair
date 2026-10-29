// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_GLTF_DEFORMATION_OBSERVATIONS_H_
#define MESHVALE_REPAIR_PYTHON_GLTF_DEFORMATION_OBSERVATIONS_H_

#include <meshvale/repair/gltf_workflow.h>
#include <nanobind/nanobind.h>

#include "gltf_presets.h"

namespace meshvale::repair::python {
[[nodiscard]] nanobind::dict ObserveGltfDeformation(
    const GltfWorkflowDeformationFacts& facts);
[[nodiscard]] nanobind::dict ObserveGltfFileDeformationOptions(
    const GltfWorkflowDeformationFacts& metadata, const GltfPreset& preset,
    bool publication_requested);
// Append configured policies to a fresh legacy-shaped preset dictionary.
void AppendGltfDeformationPreset(nanobind::dict& result,
                                 const GltfPreset& preset);
}  // namespace meshvale::repair::python
#endif  // MESHVALE_REPAIR_PYTHON_GLTF_DEFORMATION_OBSERVATIONS_H_
