// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_PLY_PRESETS_H_
#define MESHVALE_REPAIR_PYTHON_PLY_PRESETS_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/interchange/ply_files.h>
#include <meshvale/repair/ply_workflow.h>

#include <cstddef>
#include <string_view>

namespace meshvale::repair::python {
// Front-end owned UTF-8 paths; native code-unit limits remain independent.
inline constexpr std::size_t kPlyFrontendPathUtf8Bytes = 4096;
inline constexpr std::string_view kBalancedPlyPreset = "balanced-v1";
inline constexpr std::string_view kSerialPlyPreset = "serial-reference-v1";
inline constexpr std::string_view kCanonicalPlyRewritePolicy = "canonical";
inline constexpr std::string_view kDefaultPlyInputId = "source";
inline constexpr std::string_view kDefaultPlyReportPath =
    "meshvale-report.json";

struct PlyPreset {
  geometry::ExecutionOptions execution;
  interchange::PlyFileReadLimits capture_limits;
  interchange::PlyFileReadWorkPolicy capture_work;
  PlyDuplicateFileWorkflowLimits workflow_limits;
  PlyDuplicateFileWorkflowWorkPolicy workflow_work;
};

[[nodiscard]] PlyPreset MakePlyPreset(std::string_view name);
}  // namespace meshvale::repair::python
#endif  // MESHVALE_REPAIR_PYTHON_PLY_PRESETS_H_
