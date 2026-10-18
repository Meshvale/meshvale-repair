// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstddef>
#include <span>
#include <stop_token>

#include "gltf_file_workflow_internal.h"
#include "gltf_file_workflow_test_internal.h"
#include "gltf_workflow_report_internal.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::gltf_file_workflow_detail {
namespace {
// Four full uint64 execution tokens + JSON keys/closing braces. This is charged
// reserved capacity; the public byte limit applies to actual emitted length.
constexpr std::size_t kStatsTailCapacity = 256;
struct Copy {
  std::span<const std::byte> source;
  std::span<std::byte> destination;
  std::stop_token stop;
  std::size_t block;
};
void CopyReport(void* opaque, std::size_t begin, std::size_t end) {
  auto& copy = *static_cast<Copy*>(opaque);
  while (begin < end) {
    if (copy.stop.stop_requested())
      throw allocation::AdmissionFailure(allocation::AdmissionCode::kCanceled);
    const auto count = std::min(copy.block, end - begin);
    std::copy_n(copy.source.begin() + static_cast<std::ptrdiff_t>(begin), count,
                copy.destination.begin() + static_cast<std::ptrdiff_t>(begin));
    begin += count;
  }
}
}  // namespace
void PrepareFileReport(State& state,
                       const GltfDuplicateWorkflowRequest& memory_request,
                       gltf_workflow_detail::FileReportProjection projection,
                       const GltfDuplicateFileWorkflowLimits& limits,
                       const GltfDuplicateFileWorkflowWorkPolicy& work,
                       const geometry::ExecutionContext& context,
                       std::stop_token stop,
                       const FileWorkflowTestOptions& options) {
  namespace report = gltf_workflow_detail;
  const auto& memory = report::Access::StateOf(*state.memory);
  projection.wrapper_pre_report_bytes = state.allocation.OperationActiveBytes();
  projection.hashing = state.execution.hashing;
  report::ReportSink count({}, stop);
  report::EmitFileWorkflowReportPrefix(count, memory, memory_request,
                                       projection);
  if (count.size() > limits.max_report_bytes)
    throw report::ReportExtentFailure{};
  Vector<std::byte> staging(count.size(), std::byte{},
                            allocation::Allocator<std::byte>(state.allocation));
  report::ReportSink prefix(staging, stop);
  report::EmitFileWorkflowReportPrefix(prefix, memory, memory_request,
                                       projection);
  state.report.resize(allocation::CheckedSum(count.size(), kStatsTailCapacity));
  Copy copy{staging, std::span<std::byte>(state.report).first(staging.size()),
            stop,
            static_cast<std::size_t>(work.report.cancellation_block_bytes)};
  const geometry::MeshMaterializationWorkPolicy policy{
      work.report.parallel_min_bytes, work.report.partition_bytes,
      work.report.cancellation_block_bytes, work.report.max_jobs};
  geometry::MaterializationExecutionStats stats;
  const auto account = [&] {
    state.execution.report_copy = {stats.planned_jobs, stats.launched_workers,
                                   stats.joined_workers, stats.caller_jobs};
  };
  try {
    allocation::RunRange(context, state.allocation, policy, staging.size(),
                         sizeof(std::byte), CopyReport, &copy, stop, stats,
                         options.report_worker_backend, &stats);
  } catch (...) {
    account();
    throw;
  }
  account();
  projection.report_copy = state.execution.report_copy;
  report::ReportSink tail(
      std::span<std::byte>(state.report).subspan(staging.size()), stop);
  report::EmitFileWorkflowReportTail(tail, projection);
  const auto size = allocation::CheckedSum(staging.size(), tail.size());
  if (size > limits.max_report_bytes) throw report::ReportExtentFailure{};
  state.report.resize(size);
}
}  // namespace meshvale::repair::gltf_file_workflow_detail
