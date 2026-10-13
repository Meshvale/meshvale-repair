// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_TEST_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_TEST_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/repair/gltf_workflow.h>

#include <stop_token>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::gltf_workflow_detail {
// Per-call wrapper injection only. Delegates retain their own public operation
// backends. Allocation state outlives every returned record allocated through
// it; native backend state outlives the synchronous call and all joined
// handles. Callbacks satisfy the owning allocation/worker backend contracts.
struct WorkflowTestOptions {
  materialized_duplicates_detail::AllocationBackend allocation_backend =
      materialized_duplicates_detail::DefaultAllocationBackend();
  materialized_duplicates_detail::NativeWorkerBackend source_worker_backend =
      materialized_duplicates_detail::DefaultNativeWorkerBackend();
  materialized_duplicates_detail::NativeWorkerBackend report_worker_backend =
      materialized_duplicates_detail::DefaultNativeWorkerBackend();
};
[[nodiscard]] GltfDuplicateWorkflowResult RunGltfDuplicateWorkflowForTest(
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const WorkflowTestOptions& options);
}  // namespace meshvale::repair::gltf_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_TEST_INTERNAL_H_
