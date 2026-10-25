// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_PLY_WORKFLOW_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_PLY_WORKFLOW_INTERNAL_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/ply_workflow.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

#include "gltf_file_sha256_internal.h"
#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"
#include "ply_workflow_test_internal.h"

namespace meshvale::repair::ply_workflow_detail {
namespace allocation = materialized_duplicates_detail;
template <class T>
using Vector = std::vector<T, allocation::Allocator<T>>;
inline constexpr std::size_t kMemoryChecks = 9;
inline constexpr std::size_t kPublicationChecks = 11;
inline constexpr std::size_t kCoverageCount = 12;
inline constexpr std::size_t kPositionWidth = 3;
inline constexpr std::size_t kSchemaIdBytes = 256;
struct Failure {
  PlyWorkflowStatus status;
  PlyWorkflowDiagnostic diagnostic;
};
[[noreturn]] void Fail(PlyWorkflowStatus status, PlyWorkflowStage stage,
                       PlyWorkflowDiagnosticCode code,
                       std::optional<geometry::index_t> element = {});
struct Buffer {
  std::byte* data = nullptr;
  std::size_t size = 0;
  Buffer() noexcept = default;
  Buffer(const allocation::AllocationContext& allocation, std::size_t count)
      : data(count ? static_cast<std::byte*>(
                         allocation.Allocate(count, alignof(std::byte)))
                   : nullptr),
        size(count) {}
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&& other) noexcept
      : data(std::exchange(other.data, nullptr)),
        size(std::exchange(other.size, 0)) {}
  ~Buffer() noexcept {
    if (data) allocation::AllocationContext::Deallocate(data);
  }
  [[nodiscard]] std::span<std::byte> Bytes() noexcept { return {data, size}; }
  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept {
    return {data, size};
  }
};
struct State {
  allocation::AllocationContext allocation;
  interchange::PlyFileAsset source;
  interchange::PlyWriteOptions requested_writing;
  Vector<char> input_id;
  Vector<char> receipt_name;
  std::optional<Buffer> baseline;
  std::optional<MaterializedDuplicateRepair> repair;
  std::optional<interchange::PlyOutput> written;
  std::optional<interchange::PlyDocument> reloaded;
  // These inline absences prevent unchecked delegate owners being exposed.
  std::optional<interchange::PlyOutput> absent_output;
  std::optional<interchange::PlyDocument> absent_reload;
  std::optional<interchange::PlyPublishedBundle> published;
  std::optional<interchange::PlyFilePublicationResult> publication;
  std::optional<MaterializedDuplicateStatus> repair_status;
  MaterializedDuplicateDiagnostic repair_diagnostic;
  std::optional<interchange::PlyStatus> write_status, reload_status;
  interchange::PlyDiagnostic write_diagnostic, reload_diagnostic;
  PlyWorkflowStatus status = PlyWorkflowStatus::kSuccess;
  PlyWorkflowOutcomes outcomes;
  PlyWorkflowDiagnostic diagnostic;
  std::optional<PlyWorkflowDiagnostic> report_diagnostic;
  PlyWorkflowExecution execution;
  std::array<PlyWorkflowCoverage, kCoverageCount> coverage;
  std::optional<Buffer> report;
  gltf_file_detail::Sha256Hex input_hash{}, output_hash{};
  bool input_hash_available = false;
  bool output_hash_available = false;
  bool memory_verified = false;
  bool report_available = false;
  std::uint64_t hash_bytes = 0;
  State(const allocation::AllocationContext&, const interchange::PlyFileAsset&);
  [[nodiscard]] PlyWorkflowRequestedBytes Bytes() const noexcept;
};
struct Access {
  static PlyDuplicateFileWorkflowRecord Make(
      std::shared_ptr<const State> state) noexcept {
    return PlyDuplicateFileWorkflowRecord(std::move(state));
  }
};
struct Session {
  State& state;
  const geometry::ExecutionContext& context;
  const PlyDuplicateFileWorkflowWorkPolicy& work;
  std::stop_token stop;
  const WorkflowTestOptions& testing;
  void Check(PlyWorkflowStage stage, std::uint64_t ordinal = 0) const;
};
void Compute(State& state) noexcept;
void Passed(State& state, PlyWorkflowCheck check) noexcept;
void Failed(State& state, PlyWorkflowCheck check,
            PlyWorkflowDiagnosticCode reason) noexcept;
void SerializeReport(Session& session,
                     const PlyDuplicateFileWorkflowRequest& request,
                     const PlyDuplicateFileWorkflowLimits& limits,
                     bool conditional_publication);
void CopyBytes(Session&, std::span<const std::byte>, std::span<std::byte>,
               PlyWorkflowStage, const PlyByteWorkPolicy&,
               geometry::MaterializationExecutionStats&,
               const allocation::NativeWorkerBackend&);
}  // namespace meshvale::repair::ply_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_PLY_WORKFLOW_INTERNAL_H_
