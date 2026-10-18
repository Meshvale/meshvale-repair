// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_INTERNAL_H_

#include <meshvale/repair/gltf_workflow.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>
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
struct RetainedSourceInitialization {
  const interchange::GltfAsset& source;
  std::uint64_t max_protection_bytes;
  std::uint64_t max_binding_records;
};
[[nodiscard]] bool IsWorkflowReportUtf8(std::string_view value) noexcept;
[[nodiscard]] std::optional<GltfDuplicateWorkflowResult>
ValidateRetainedWorkflow(const RetainedSourceInitialization& retained,
                         const GltfDuplicateWorkflowRequest& request,
                         const GltfDuplicateWorkflowLimits& limits,
                         const GltfDuplicateWorkflowWorkPolicy& work);
struct SourceBaselineBinding {
  interchange::GltfResourceKind kind;
  std::uint64_t index;
  Vector<std::byte> bytes;
  SourceBaselineBinding(interchange::GltfResourceKind kind, std::uint64_t index,
                        const allocation::AllocationContext& allocation);
};
struct WorkflowState {
  allocation::AllocationContext allocation;
  GltfWorkflowOutcomes outcomes;
  GltfWorkflowDiagnostic diagnostic;
  GltfWorkflowExecutionStats execution;
  std::array<GltfWorkflowCoverage, kCoverageCount> coverage;
  std::optional<interchange::GltfAsset> read;
  // A retained read owner is authoritative input, never fresh read traffic.
  bool retained_read = false;
  Vector<std::byte> baseline_json;
  Vector<SourceBaselineBinding> baseline_bindings;
  Vector<interchange::SuppliedGltfResource> baseline_views;
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
  static const WorkflowState& StateOf(
      const GltfDuplicateWorkflowRecord& record) noexcept {
    return *record.state_;
  }
};
// Exact retained-owner initialization shares the sole stage engine. The file
// caller admits source/caller ledger equality before this fresh operation.
// Baselines are independent charged copies, not a second reader or oracle.
[[nodiscard]] GltfDuplicateWorkflowResult RunRetainedGltfDuplicateWorkflow(
    const RetainedSourceInitialization& retained,
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const WorkflowTestOptions& options);
// Ordered token planning is serial; byte copying uses the shared worker ledger.
void SerializeReport(WorkflowState& state,
                     const GltfDuplicateWorkflowRequest& request,
                     const GltfDuplicateWorkflowLimits& limits,
                     const GltfDuplicateWorkflowWorkPolicy& work,
                     const geometry::ExecutionContext& context,
                     std::stop_token stop, const WorkflowTestOptions& options);
}  // namespace meshvale::repair::gltf_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_INTERNAL_H_
