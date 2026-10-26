// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_GLTF_PRESETS_H_
#define MESHVALE_REPAIR_PYTHON_GLTF_PRESETS_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/repair/gltf_file_workflow.h>

#include <string_view>

namespace meshvale::repair::python {
inline constexpr std::string_view kBalancedGltfPreset = "balanced-v1";
inline constexpr std::string_view kSerialGltfPreset = "serial-reference-v1";
inline constexpr std::string_view kBalancedTranslationGltfPreset =
    "balanced-direct-translation-v1";
inline constexpr std::string_view kSerialTranslationGltfPreset =
    "serial-direct-translation-v1";
inline constexpr std::string_view kStaticGltfProcessingProfile =
    "static-local-triangles-v1";
inline constexpr std::string_view kDirectTranslationGltfProcessingProfile =
    "direct-translation-v1";
inline constexpr std::string_view kRequireTrianglesPolicy = "require-triangles";
inline constexpr std::string_view kConvertStripOrFanPolicy =
    "convert-strip-or-fan";
inline constexpr std::string_view kRawValuesPolicy = "raw";
inline constexpr std::string_view kRawAndNormalizedValuesPolicy =
    "raw-and-normalized";
inline constexpr std::string_view kDefaultGltfInputId = "source";
inline constexpr std::string_view kDefaultGltfReportPath =
    "receipts/report.json";
// Separate operation caps share execution's ledger; they are not a total.
struct GltfPreset {
  geometry::ExecutionOptions execution;
  interchange::GltfFileReadLimits capture_limits;
  interchange::GltfFileReadWorkPolicy capture_work;
  GltfDuplicateFileWorkflowLimits workflow_limits;
  GltfDuplicateFileWorkflowWorkPolicy workflow_work;
};
[[nodiscard]] GltfPreset MakeGltfPreset(std::string_view name);
}  // namespace meshvale::repair::python
#endif  // MESHVALE_REPAIR_PYTHON_GLTF_PRESETS_H_
