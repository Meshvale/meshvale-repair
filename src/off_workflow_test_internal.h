// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_OFF_WORKFLOW_TEST_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_OFF_WORKFLOW_TEST_INTERNAL_H_

#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_workflow.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::off_workflow_detail {
// Identifies actual correspondence payload checkpoints. Ordinals are element
// indices in that task, never fabricated byte progress.
enum class VerificationPart {
  kSourceFaces,
  kSourceCorners,
  kRetainedFaces,
  kRetainedCorners,
  kTargetPairs
};
// Allocation backend state outlives the last returned free. Worker/observer
// state remains alive through this synchronous call and all joined workers.
// Delegates are the actual public operations, never substituted by test fakes.
struct WorkflowTestOptions {
  materialized_duplicates_detail::AllocationBackend allocation_backend =
      materialized_duplicates_detail::DefaultAllocationBackend();
  materialized_duplicates_detail::NativeWorkerBackend source_worker_backend =
      materialized_duplicates_detail::DefaultNativeWorkerBackend();
  materialized_duplicates_detail::NativeWorkerBackend
      verification_worker_backend =
          materialized_duplicates_detail::DefaultNativeWorkerBackend();
  materialized_duplicates_detail::NativeWorkerBackend report_worker_backend =
      materialized_duplicates_detail::DefaultNativeWorkerBackend();
  void (*checkpoint)(OffWorkflowStage, std::uint64_t, void*) = nullptr;
  // Only after actual native return and nonthrowing completed-owner retention.
  void (*after_repair)(const MaterializedDuplicateResult&, void*) = nullptr;
  // Actual bytes, after complete conditional report preparation, before import.
  void (*prepared_report)(std::span<const std::byte>, void*) = nullptr;
  // Actual published result only. This observation must allocate nothing,
  // throw nothing and cannot replace success; production defaults to nullptr.
  void (*published)(const interchange::OffFilePublicationResult&,
                    void*) noexcept = nullptr;
  // Actual task checkpoint, immediately before the owning stop observation.
  void (*verification_checkpoint)(VerificationPart, std::uint64_t,
                                  void*) = nullptr;
  void* state = nullptr;
};
[[nodiscard]] OffDuplicateFileWorkflowResult RunOffDuplicateFileWorkflowForTest(
    const OffDuplicateFileWorkflowRequest& request,
    const OffDuplicateFileWorkflowLimits& limits,
    const OffDuplicateFileWorkflowWorkPolicy& work, std::stop_token stop,
    const WorkflowTestOptions& options);
}  // namespace meshvale::repair::off_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_OFF_WORKFLOW_TEST_INTERNAL_H_
