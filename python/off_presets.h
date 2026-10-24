// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_OFF_PRESETS_H_
#define MESHVALE_REPAIR_PYTHON_OFF_PRESETS_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/off_workflow.h>

#include <cstddef>
#include <string_view>

namespace meshvale::repair::python {
// Front-end owned UTF-8 paths; native code-unit limits remain independent.
inline constexpr std::size_t kOffFrontendPathUtf8Bytes = 4096;
inline constexpr std::string_view kBalancedOffPreset = "balanced-v1";
inline constexpr std::string_view kSerialOffPreset = "serial-reference-v1";
inline constexpr std::string_view kCanonicalOffTextPolicy = "canonical-ascii";
inline constexpr std::string_view kUnknownZeroOffEdgePolicy = "unknown-zero";
inline constexpr std::string_view kDefaultOffInputId = "source";
inline constexpr std::string_view kDefaultOffReportPath =
    "meshvale-report.json";
struct OffPreset {
  geometry::ExecutionOptions execution;
  interchange::OffFileReadLimits capture_limits;
  interchange::OffFileReadWorkPolicy capture_work;
  OffDuplicateFileWorkflowLimits workflow_limits;
  OffDuplicateFileWorkflowWorkPolicy workflow_work;
};
[[nodiscard]] OffPreset MakeOffPreset(std::string_view name);
}  // namespace meshvale::repair::python
#endif  // MESHVALE_REPAIR_PYTHON_OFF_PRESETS_H_
