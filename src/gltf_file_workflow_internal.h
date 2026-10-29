// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_GLTF_FILE_WORKFLOW_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_GLTF_FILE_WORKFLOW_INTERNAL_H_

#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>

#include "gltf_file_sha256_internal.h"
#include "gltf_workflow_internal.h"
#include "gltf_workflow_report_internal.h"
#include "materialized_duplicate_allocation.h"

namespace meshvale::repair::gltf_file_workflow_detail {
namespace allocation = materialized_duplicates_detail;
template <class T>
using Vector = gltf_workflow_detail::Vector<T>;
struct FileWorkflowTestOptions;
struct Artifact {
  // artifact- + every uint64 decimal digit, without an allocating formatter.
  static constexpr std::size_t kIdCapacity =
      sizeof("artifact-") - 1 + std::numeric_limits<std::uint64_t>::digits10 +
      1;
  std::array<char, kIdCapacity> id{};
  std::size_t id_size = 0;
  gltf_file_detail::Sha256Hex sha256{};
  Vector<char> path;
  explicit Artifact(const allocation::AllocationContext& allocation);
};
struct State {
  allocation::AllocationContext allocation;
  interchange::GltfFileAsset source;
  std::optional<GltfDuplicateWorkflowRecord> memory;
  // Exclusive with memory: retains completed facts, never partial report wire.
  std::optional<MaterializedDuplicateRepair> retained_repair;
  // Inline reached facts survive memory report refusal even without its owner.
  std::optional<GltfWorkflowOutcomes> memory_outcomes;
  GltfWorkflowTranslationFacts translation;
  std::optional<GltfWorkflowDeformationFacts> deformation;
  std::array<std::optional<interchange::GltfDeformationPolicy>,
             gltf_workflow_detail::kDeformationPhaseCount>
      deformation_policies;
  std::optional<interchange::GltfPublishedBundle> published;
  GltfFileWorkflowOutcomes outcomes;
  GltfFileWorkflowDiagnostic diagnostic;
  GltfWorkflowStatus status = GltfWorkflowStatus::kInvalidConfiguration;
  GltfFileWorkflowByteExecution execution;
  Vector<char> report_path;
  Vector<std::byte> report;
  Vector<Artifact> artifacts;
  Vector<gltf_workflow_detail::FileReportArtifact> artifact_views;
  std::array<interchange::GltfFileView, 1> provided_files{};
  gltf_file_detail::Sha256Hex input_hash{};
  bool input_hash_available = false;
  bool report_available = false;
  bool conditional_report = false;
  // Internal delivery witness; secondary delivery never replaces primary.
  bool failure_report_attempted = false;
  bool failure_report_delivered = false;
  GltfWorkflowCoverage staged_primitive;
  GltfWorkflowCoverage physical_members;
  State(const allocation::AllocationContext& allocation,
        const interchange::GltfFileAsset& source);
  [[nodiscard]] GltfFileWorkflowRequestedBytes Bytes() const noexcept;
};
struct Access {
  static GltfDuplicateFileWorkflowRecord Make(
      std::shared_ptr<const State> state) noexcept {
    return GltfDuplicateFileWorkflowRecord(std::move(state));
  }
};
void PrepareFileReport(State& state,
                       const GltfDuplicateWorkflowRequest& memory_request,
                       gltf_workflow_detail::FileReportProjection projection,
                       const GltfDuplicateFileWorkflowLimits& limits,
                       const GltfDuplicateFileWorkflowWorkPolicy& work,
                       const geometry::ExecutionContext& context,
                       std::stop_token stop,
                       const FileWorkflowTestOptions& options);
}  // namespace meshvale::repair::gltf_file_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_GLTF_FILE_WORKFLOW_INTERNAL_H_
