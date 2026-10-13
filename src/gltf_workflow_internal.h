// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_INTERNAL_H_

#include <meshvale/repair/gltf_workflow.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include "materialized_duplicate_allocation.h"

namespace meshvale::repair::gltf_workflow_detail {
namespace allocation = materialized_duplicates_detail;
template <class T>
using Vector = std::vector<T, allocation::Allocator<T>>;
inline constexpr std::size_t kRequiredCheckCount = 8;
inline constexpr std::size_t kCoverageCount = kRequiredCheckCount + 1;
inline constexpr std::size_t kReportIdentityBytes = 256;
struct ReportExtentFailure {};
struct WorkflowTestOptions;
struct WorkflowState {
  allocation::AllocationContext allocation;
  GltfWorkflowOutcomes outcomes;
  GltfWorkflowDiagnostic diagnostic;
  GltfWorkflowExecutionStats execution;
  std::array<GltfWorkflowCoverage, kCoverageCount> coverage;
  std::optional<interchange::GltfAsset> read;
  std::optional<interchange::GltfPrimitiveHandle> primitive;
  std::optional<interchange::GltfPrimitiveExtraction> extraction;
  std::optional<interchange::GltfMaterializedPrimitive> source;
  std::optional<MaterializedDuplicateRepair> repair;
  // The unchecked bundle is retained for accounting but never exposed.
  std::optional<interchange::GltfTopologyWriteBundle> written;
  std::optional<interchange::GltfTopologyWriteBundle> output;
  std::optional<interchange::GltfAsset> reload;
  std::optional<interchange::GltfPrimitiveExtraction> reload_extraction;
  std::optional<interchange::GltfExtractionRequest> reload_policy;
  std::optional<interchange::GltfTopologyOutputInspection> inspection;
  Vector<std::byte> report;
  explicit WorkflowState(const allocation::AllocationContext& allocator);
  GltfWorkflowRequestedBytes Bytes() const noexcept;
  void UpdateDelegates();
};
struct Access {
  static GltfDuplicateWorkflowRecord Make(
      std::shared_ptr<const WorkflowState> state) noexcept {
    return GltfDuplicateWorkflowRecord(std::move(state));
  }
};
// Ordered token planning is serial; byte copying uses the shared worker ledger.
void SerializeReport(WorkflowState& state,
                     const GltfDuplicateWorkflowRequest& request,
                     const GltfDuplicateWorkflowLimits& limits,
                     const GltfDuplicateWorkflowWorkPolicy& work,
                     const geometry::ExecutionContext& context,
                     std::stop_token stop, const WorkflowTestOptions& options);
}  // namespace meshvale::repair::gltf_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_INTERNAL_H_
