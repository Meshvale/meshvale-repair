// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_TEST_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_TEST_INTERNAL_H_

#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <span>
#include <stop_token>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::materialized_duplicates_detail {
// Private per-call only. Backend state outlives every retained result block;
// observer state outlives the synchronous joined call. No observer may
// allocate, throw, mutate source bytes or recursively access this operation's
// allocator.
struct TestHooks {
  AllocationBackend allocation = DefaultAllocationBackend();
  NativeWorkerBackend workers = DefaultNativeWorkerBackend();
  void (*failure_observer)(const MaterializedDuplicateDiagnostic&,
                           void*) noexcept = nullptr;
  void* failure_observer_state = nullptr;
  geometry::MaterializationExecutionStats* worker_observer = nullptr;
};
[[nodiscard]] MaterializedDuplicateResult TestRemoveMaterializedDuplicateFaces(
    const geometry::MaterializedMesh& source,
    std::span<const DuplicateTarget> targets,
    const MaterializedDuplicateOptions& options,
    const MaterializedDuplicateLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop,
    const TestHooks& hooks);
}  // namespace meshvale::repair::materialized_duplicates_detail
#endif  // MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_TEST_INTERNAL_H_
