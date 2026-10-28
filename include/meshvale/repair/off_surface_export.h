// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_OFF_SURFACE_EXPORT_H_
#define MESHVALE_REPAIR_OFF_SURFACE_EXPORT_H_

#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/geometry/triangulation.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/off_workflow.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

namespace meshvale::repair {
namespace off_surface_export_detail {
struct State;
struct Access;
}  // namespace off_surface_export_detail

// All borrowed handles, paths, spans and options stay readable and unchanged
// through synchronous return and every delegate join. Coordinates declare the
// existing values; this operation performs no basis or unit transformation.
struct OffGlbSurfaceWorkflowRequest {
  const interchange::OffFileAsset& source;
  std::string_view input_id;
  std::span<const DuplicateTarget> targets;
  const std::filesystem::path& destination;
  interchange::OffTextPolicy text;
  interchange::OffEdgeCountPolicy edge_count;
  interchange::SurfaceCoordinates coordinates;
  interchange::SurfaceNumericPolicy numeric;
  bool acknowledge_triangle_conversion;
  bool acknowledge_corner_expansion;
  bool acknowledge_unused_vertices;
  geometry::TriangulationOptions triangulation_options;
  std::string_view receipt_path =
      interchange::kDefaultSurfacePublicationReceiptPath;
};
struct OffGlbSurfaceWorkflowLimits {
  std::uint64_t max_wrapper_new_requested_bytes = 0;
  OffDuplicateFileWorkflowLimits off;
  geometry::ChargedTriangulationLimits triangulation;
  interchange::SurfaceEncodeLimits encoding;
  interchange::SurfaceInspectLimits inspection;
  interchange::SurfaceFilePublicationLimits publication;
};
struct OffGlbSurfaceWorkflowWorkPolicy {
  OffDuplicateFileWorkflowWorkPolicy off;
  geometry::MeshMaterializationWorkPolicy triangulation;
  interchange::SurfaceWorkPolicy encoding;
  interchange::SurfaceWorkPolicy inspection;
  interchange::SurfaceFilePublicationWorkPolicy publication;
};
enum class OffGlbSurfaceStage {
  kConfiguration,
  kOffWorkflow,
  kTriangulation,
  kEncoding,
  kInspection,
  kPublication,
  kComplete
};
enum class OffGlbSurfaceStatus {
  kSuccess,
  kInvalidConfiguration,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled,
  kRejected,
  kUnsupported,
  kDelegatedFailure
};
enum class OffGlbSurfaceCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kPolicy,
  kCoordinates,
  kNumericPolicy,
  kProjectionAcknowledgment,
  kInputIdentity,
  kReceiptPath,
  kRequestBudget,
  kStopRequested,
  kOffWorkflow,
  kTriangulation,
  kEncoding,
  kInspection,
  kPublication
};
struct OffGlbSurfaceDiagnostic {
  OffGlbSurfaceStage stage = OffGlbSurfaceStage::kConfiguration;
  OffGlbSurfaceCode code = OffGlbSurfaceCode::kNone;
};
struct OffGlbSurfaceFacts {
  std::optional<OffDuplicateFileWorkflowResult> off;
  std::optional<geometry::ChargedTriangulationResult> triangulation;
  std::optional<interchange::SurfaceEncodeResult> encoding;
  std::optional<interchange::SurfaceInspectionResult> inspection;
  std::optional<interchange::SurfaceFilePublicationResult> publication;
};
// Copy-only immutable owner. Copies allocate nothing and retain the original
// ledger/backend until its last actual free. Views borrow a live unreassigned
// handle. Distinct copies permit concurrent reads; assignment/destruction of a
// handle cannot race access to that handle or its borrowed views.
class OffGlbSurfaceWorkflowRecord {
 public:
  OffGlbSurfaceWorkflowRecord(const OffGlbSurfaceWorkflowRecord&) noexcept;
  OffGlbSurfaceWorkflowRecord& operator=(
      const OffGlbSurfaceWorkflowRecord&) noexcept;
  ~OffGlbSurfaceWorkflowRecord() noexcept;
  [[nodiscard]] const interchange::OffFileAsset& Source() const noexcept;
  [[nodiscard]] const OffGlbSurfaceFacts& Facts() const noexcept;
  // Wrapper-only live requested scope; delegate owners have separate scopes.
  [[nodiscard]] std::uint64_t NewRequestedBytes() const noexcept;

 private:
  friend struct off_surface_export_detail::Access;
  explicit OffGlbSurfaceWorkflowRecord(
      std::shared_ptr<const off_surface_export_detail::State>) noexcept;
  std::shared_ptr<const off_surface_export_detail::State> state_;
};
struct OffGlbSurfaceWorkflowResult {
  OffGlbSurfaceStatus status = OffGlbSurfaceStatus::kInvalidConfiguration;
  OffGlbSurfaceDiagnostic diagnostic;
  std::optional<OffGlbSurfaceWorkflowRecord> record;
};
// Runs one memory-only OFF workflow, retained triangulation, encoding, actual
// byte inspection and new-directory publication on the source's own ledger.
// Expected failures retain every reached result after wrapper construction.
// Unexpected exceptions propagate after delegate joins and staged cleanup.
// Successful publication requires no further fallible native work or callback.
[[nodiscard]] OffGlbSurfaceWorkflowResult RunOffGlbSurfaceWorkflow(
    const OffGlbSurfaceWorkflowRequest&, const OffGlbSurfaceWorkflowLimits&,
    const OffGlbSurfaceWorkflowWorkPolicy&, std::stop_token stop = {});
}  // namespace meshvale::repair
#endif  // MESHVALE_REPAIR_OFF_SURFACE_EXPORT_H_
