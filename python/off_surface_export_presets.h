// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_OFF_SURFACE_EXPORT_PRESETS_H_
#define MESHVALE_REPAIR_PYTHON_OFF_SURFACE_EXPORT_PRESETS_H_
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/triangulation.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/off_surface_export.h>

#include <string_view>
namespace meshvale::repair::python {
inline constexpr std::string_view kBalancedOffGlbPreset = "balanced-v1";
inline constexpr std::string_view kSerialOffGlbPreset = "serial-reference-v1";
struct OffGlbPreset {
  geometry::ExecutionOptions execution;
  interchange::OffFileReadLimits capture_limits;
  interchange::OffFileReadWorkPolicy capture_work;
  geometry::TriangulationOptions triangulation_options;
  OffGlbSurfaceWorkflowLimits limits;
  OffGlbSurfaceWorkflowWorkPolicy work;
};
[[nodiscard]] OffGlbPreset MakeOffGlbPreset(std::string_view name);
}  // namespace meshvale::repair::python
#endif  // MESHVALE_REPAIR_PYTHON_OFF_SURFACE_EXPORT_PRESETS_H_
