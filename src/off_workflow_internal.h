// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_OFF_WORKFLOW_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_OFF_WORKFLOW_INTERNAL_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_workflow.h>

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
#include "off_workflow_test_internal.h"

namespace meshvale::repair::off_workflow_detail {
namespace allocation = materialized_duplicates_detail;
template <class T>
using Vector = std::vector<T, allocation::Allocator<T>>;
inline constexpr std::size_t kMemoryChecks = 7;
inline constexpr std::size_t kPublicationChecks = 9;
inline constexpr std::size_t kCoverageCount = 10;
inline constexpr std::size_t kPositionWidth = 3;
inline constexpr std::size_t kSchemaIdBytes = 256;
inline constexpr std::size_t kReportStatsTailBytes = 256;
struct Failure {
  OffWorkflowStatus status;
  OffWorkflowDiagnostic diagnostic;
};
[[noreturn]] void Fail(OffWorkflowStatus status, OffWorkflowStage stage,
                       OffWorkflowDiagnosticCode code,
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
  interchange::OffFileAsset source;
  Vector<char> input_id;
  Vector<char> receipt_name;
  std::optional<Buffer> baseline;
  std::optional<MaterializedDuplicateRepair> repair;
  std::optional<interchange::OffOutput> written;
  std::optional<interchange::OffDocument> reloaded;
  // These inline absences prevent unchecked delegate owners being exposed.
  std::optional<interchange::OffOutput> absent_output;
  std::optional<interchange::OffDocument> absent_reload;
  std::optional<interchange::OffPublishedBundle> published;
  std::optional<interchange::OffFilePublicationResult> publication;
  std::optional<MaterializedDuplicateStatus> repair_status;
  MaterializedDuplicateDiagnostic repair_diagnostic;
  std::optional<interchange::OffStatus> write_status, reload_status;
  interchange::OffDiagnostic write_diagnostic, reload_diagnostic;
  OffWorkflowStatus status = OffWorkflowStatus::kSuccess;
  OffWorkflowOutcomes outcomes;
  OffWorkflowDiagnostic diagnostic;
  std::optional<OffWorkflowDiagnostic> report_diagnostic;
  OffWorkflowExecution execution;
  std::array<OffWorkflowCoverage, kCoverageCount> coverage;
  std::optional<Buffer> report;
  gltf_file_detail::Sha256Hex input_hash{}, output_hash{};
  bool input_hash_available = false;
  bool output_hash_available = false;
  bool memory_verified = false;
  bool report_available = false;
  std::uint64_t hash_bytes = 0;
  State(const allocation::AllocationContext&, const interchange::OffFileAsset&);
  [[nodiscard]] OffWorkflowRequestedBytes Bytes() const noexcept;
};
struct Access {
  static OffDuplicateFileWorkflowRecord Make(
      std::shared_ptr<const State> state) noexcept {
    return OffDuplicateFileWorkflowRecord(std::move(state));
  }
};
struct Session {
  State& state;
  const geometry::ExecutionContext& context;
  const OffDuplicateFileWorkflowWorkPolicy& work;
  std::stop_token stop;
  const WorkflowTestOptions& testing;
  void Check(OffWorkflowStage stage, std::uint64_t ordinal = 0) const;
};
void Compute(State& state) noexcept;
void Passed(State& state, OffWorkflowCheck check) noexcept;
void Failed(State& state, OffWorkflowCheck check,
            OffWorkflowDiagnosticCode reason) noexcept;
void SerializeReport(Session& session,
                     const OffDuplicateFileWorkflowRequest& request,
                     const OffDuplicateFileWorkflowLimits& limits,
                     bool conditional_publication);
void CopyBytes(Session&, std::span<const std::byte>, std::span<std::byte>,
               OffWorkflowStage, const OffByteWorkPolicy&,
               geometry::MaterializationExecutionStats&,
               const allocation::NativeWorkerBackend&);
}  // namespace meshvale::repair::off_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_OFF_WORKFLOW_INTERNAL_H_
