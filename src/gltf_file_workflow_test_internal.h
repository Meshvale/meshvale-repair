// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_GLTF_FILE_WORKFLOW_TEST_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_GLTF_FILE_WORKFLOW_TEST_INTERNAL_H_

#include <meshvale/repair/gltf_file_workflow.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

#include "gltf_workflow_test_internal.h"
#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::gltf_file_workflow_detail {
// Original allocator state outlives the last returned owner free. Worker and
// borrowed observer state outlive this synchronous call and all joined jobs.
// These hooks belong to Repair; the real public publisher is never replaced.
struct FileWorkflowTestOptions {
  materialized_duplicates_detail::AllocationBackend allocation_backend =
      materialized_duplicates_detail::DefaultAllocationBackend();
  gltf_workflow_detail::WorkflowTestOptions memory;
  materialized_duplicates_detail::NativeWorkerBackend hash_worker_backend =
      materialized_duplicates_detail::DefaultNativeWorkerBackend();
  materialized_duplicates_detail::NativeWorkerBackend report_worker_backend =
      materialized_duplicates_detail::DefaultNativeWorkerBackend();
  // Called only after full source-framed + actual staged artifact work is
  // admitted, before the first hash job. Content borrows the actual publisher.
  void (*after_hash_admission)(const interchange::GltfFileVerifiedContent&,
                               std::uint64_t bytes, void* state) = nullptr;
  // Fully prepared conditional bytes, before real dynamic import/commit.
  void (*prepared_report)(std::span<const std::byte>, void* state) = nullptr;
  void* state = nullptr;
};
[[nodiscard]] GltfDuplicateFileWorkflowResult
RunGltfDuplicateFileWorkflowForTest(
    const GltfDuplicateFileWorkflowRequest& request,
    const GltfDuplicateFileWorkflowLimits& limits,
    const GltfDuplicateFileWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const FileWorkflowTestOptions& options);
}  // namespace meshvale::repair::gltf_file_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_GLTF_FILE_WORKFLOW_TEST_INTERNAL_H_
