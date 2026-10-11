// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_WORKERS_H_
#define MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_WORKERS_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <cstddef>
#include <cstdint>
#include <stop_token>

#include "materialized_duplicate_allocation.h"

namespace meshvale::repair::materialized_duplicates_detail {
using geometry::ExecutionContext;
using geometry::MaterializationExecutionStats;
using geometry::MeshMaterializationWorkPolicy;
struct NativeWorkerRecord {
  void (*entry)(NativeWorkerRecord&) noexcept = nullptr;
  void* state = nullptr;
  std::size_t ordinal = 0;
};
struct NativeWorkerBackend {
  std::uintptr_t (*launch)(NativeWorkerRecord* record, void* state);
  void (*join)(std::uintptr_t handle, void* state) noexcept;
  void* state;
};
[[nodiscard]] NativeWorkerBackend DefaultNativeWorkerBackend() noexcept;
using RangeOperation = void (*)(void* state, std::size_t begin,
                                std::size_t end);
// All callback input remains alive until this synchronous call has joined.
// Worker records, handles and exception slots are explicitly charged. Native
// thread/CRT implementation internals are outside C++ requested-block totals.
void RunRange(const ExecutionContext& context,
              const AllocationContext& allocation,
              const MeshMaterializationWorkPolicy& work, std::size_t count,
              std::size_t scalar_bytes, RangeOperation operation, void* state,
              std::stop_token stop, MaterializationExecutionStats& stats,
              const NativeWorkerBackend& backend,
              MaterializationExecutionStats* observer = nullptr);
}  // namespace meshvale::repair::materialized_duplicates_detail
#endif  // MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_WORKERS_H_
