// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <stop_token>
#include <vector>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace {
namespace detail = meshvale::repair::materialized_duplicates_detail;
namespace geometry = meshvale::geometry;
constexpr std::size_t kMaximumObservedBlocks = 256;
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kMinimumParallelVertices = 1;
constexpr std::size_t kRequestBudget = 1024 * 1024;
constexpr std::size_t kFillElements = 8192;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kPartitionBytes = 1024;
constexpr std::uint64_t kCancellationBlockScalars = 64;
constexpr std::uint64_t kMaximumJobs = 32;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
geometry::ExecutionOptions Options() {
  return {.worker_budget = kWorkerBudget,
          .minimum_parallel_vertices = kMinimumParallelVertices,
          .tracked_payload_budget_bytes = kRequestBudget};
}
geometry::MeshMaterializationWorkPolicy Work() {
  return {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
          kMaximumJobs};
}
struct Backend {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
  };
  std::array<Block, kMaximumObservedBlocks> blocks{};
  detail::AllocationBackend original = detail::DefaultAllocationBackend();
  const geometry::ExecutionContext* context = nullptr;
  std::size_t calls = 0;
  std::size_t fail_ordinal = 0;
  std::size_t live = 0;
  std::size_t peak = 0;
  bool incorrect_free = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* argument) {
    auto& self = *static_cast<Backend*>(argument);
    ++self.calls;
    Require(self.context->ActiveTrackedPayload() >= self.live + bytes,
            "explicit original request was not charged before allocation");
    if (self.calls == self.fail_ordinal) throw std::bad_alloc();
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks) {
      if (!block.pointer) {
        block = {pointer, bytes, alignment};
        self.live += bytes;
        self.peak = std::max(self.peak, self.context->ActiveTrackedPayload());
        return pointer;
      }
    }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::runtime_error("test observation capacity exhausted");
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* argument) noexcept {
    auto& self = *static_cast<Backend*>(argument);
    for (auto& block : self.blocks) {
      if (block.pointer == pointer) {
        if (block.bytes != bytes || block.alignment != alignment ||
            self.context->ActiveTrackedPayload() < self.live)
          self.incorrect_free = true;
        self.original.deallocate(pointer, bytes, alignment,
                                 self.original.state);
        self.live -= bytes;
        block = {};
        return;
      }
    }
    self.incorrect_free = true;
  }
  detail::AllocationBackend Hooks() { return {Allocate, Free, this}; }
};
void Allocations(const geometry::ExecutionContext& context, Backend& backend,
                 std::size_t cap) {
  auto operation = detail::AllocationContext(context, {}, backend.Hooks())
                       .WithRequestLimit(cap);
  std::vector<std::uint64_t, detail::Allocator<std::uint64_t>> values{
      detail::Allocator<std::uint64_t>(operation)};
  values.reserve(7);
  values.resize(7);
  values.reserve(
      17);  // Old/new replacement requests overlap at original entry.
  auto control = std::allocate_shared<std::uint64_t>(
      detail::Allocator<std::uint64_t>(operation), 42);
  Require(*control == 42 && operation.OperationActiveBytes() == backend.live,
          "rebound/control storage did not belong to the operation");
}
void RequestCases() {
  geometry::ExecutionContext context(Options());
  Backend baseline;
  baseline.context = &context;
  Allocations(context, baseline, kRequestBudget);
  Require(baseline.calls != 0 && baseline.live == 0 &&
              !baseline.incorrect_free && context.ActiveTrackedPayload() == 0,
          "explicit request baseline did not free before charge release");
  Backend exact;
  exact.context = &context;
  Allocations(context, exact, baseline.peak);
  Require(exact.live == 0 && context.ActiveTrackedPayload() == 0,
          "observed inclusive explicit peak rejected or leaked");
  Backend below;
  below.context = &context;
  bool rejected = false;
  try {
    Allocations(context, below, baseline.peak - 1);
  } catch (const detail::AdmissionFailure& failure) {
    rejected = failure.Code() == detail::AdmissionCode::kBudgetExceeded;
  }
  Require(rejected && below.live == 0 && !below.incorrect_free &&
              context.ActiveTrackedPayload() == 0,
          "one byte below observed explicit peak did not roll back");
  for (std::size_t ordinal = 1; ordinal <= baseline.calls; ++ordinal) {
    Backend failing;
    failing.context = &context;
    failing.fail_ordinal = ordinal;
    bool failed = false;
    try {
      Allocations(context, failing, kRequestBudget);
    } catch (const std::bad_alloc&) {
      failed = true;
    }
    Require(failed && failing.calls == ordinal && failing.live == 0 &&
                !failing.incorrect_free && context.ActiveTrackedPayload() == 0,
            "an explicit original request failure lost rollback");
  }
}
void AggregateBarrierCase() {
  geometry::ExecutionContext context(Options());
  constexpr std::size_t kDelegatedBytes = 71;
  constexpr std::size_t kOwnPayloadBytes = 53;
  const auto own_request =
      detail::AllocationRequestBytes(kOwnPayloadBytes, alignof(std::uint64_t));
  std::size_t seed = 0;
  {
    auto operation =
        detail::AllocationContext(context, {}).WithRequestLimit(kRequestBudget);
    seed = operation.OperationActiveBytes();
  }
  {
    auto operation =
        detail::AllocationContext(context, {})
            .WithRequestLimit(seed + kDelegatedBytes + own_request);
    auto delegated = geometry::TryReservePayload(context, kDelegatedBytes);
    Require(delegated.status == geometry::ReservationStatus::kAccepted,
            "delegate reservation failed");
    operation.SetDelegatedBytes(kDelegatedBytes);
    operation.SetFrozen(true);
    bool frozen = false;
    try {
      (void)operation.Allocate(kOwnPayloadBytes, alignof(std::uint64_t));
    } catch (const detail::AdmissionFailure&) {
      frozen = true;
    }
    operation.SetFrozen(false);
    Require(frozen && operation.RemainingBytes() == own_request,
            "delegated barrier allowed Repair growth");
    auto* block = operation.Allocate(kOwnPayloadBytes, alignof(std::uint64_t));
    Require(operation.RemainingBytes() == 0 &&
                context.ActiveTrackedPayload() ==
                    seed + kDelegatedBytes + own_request,
            "aggregate delegate contribution was not counted once");
    bool rejected = false;
    try {
      (void)operation.Allocate(1, alignof(std::uint8_t));
    } catch (const detail::AdmissionFailure& failure) {
      rejected = failure.Code() == detail::AdmissionCode::kBudgetExceeded;
    }
    detail::AllocationContext::Deallocate(block);
    Require(rejected && operation.RemainingBytes() == own_request,
            "aggregate rejection changed live ownership");
  }
  Require(context.ActiveTrackedPayload() == 0, "aggregate barrier leaked");
}
struct NativeBackend {
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::size_t launches = 0;
  std::size_t joins = 0;
  std::size_t fail_launch = 0;
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* argument) {
    auto& self = *static_cast<NativeBackend*>(argument);
    if (self.launches + 1 == self.fail_launch)
      throw std::runtime_error("injected native launch failure");
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launches;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* argument) noexcept {
    auto& self = *static_cast<NativeBackend*>(argument);
    self.original.join(handle, self.original.state);
    ++self.joins;
  }
  detail::NativeWorkerBackend Hooks() { return {Launch, Join, this}; }
};
void WorkerCases() {
  geometry::ExecutionContext context(Options());
  std::array<std::uint64_t, kFillElements> values{};
  auto fill = [](void* argument, std::size_t begin, std::size_t end) {
    auto& rows =
        *static_cast<std::array<std::uint64_t, kFillElements>*>(argument);
    for (auto i = begin; i < end; ++i) rows[i] = i;
  };
  {
    auto operation =
        detail::AllocationContext(context, {}).WithRequestLimit(kRequestBudget);
    const auto before = operation.OperationActiveBytes();
    geometry::MaterializationExecutionStats stats;
    NativeBackend native;
    detail::RunRange(context, operation, Work(), values.size(),
                     sizeof(values[0]), fill, &values, {}, stats,
                     native.Hooks());
    Require(stats.launched_workers == kWorkerBudget &&
                stats.joined_workers == stats.launched_workers &&
                native.launches == native.joins &&
                context.ActiveWorkers() == 0 &&
                operation.OperationActiveBytes() == before,
            "actual substantial worker join or request restoration failed");
    for (std::size_t i = 0; i < values.size(); ++i)
      Require(values[i] == i, "actual worker fill missed a partition");
    geometry::MaterializationExecutionStats failed_stats;
    NativeBackend failing;
    failing.fail_launch = kWorkerBudget;
    bool failed = false;
    try {
      detail::RunRange(context, operation, Work(), values.size(),
                       sizeof(values[0]), fill, &values, {}, failed_stats,
                       failing.Hooks());
    } catch (const std::runtime_error&) {
      failed = true;
    }
    Require(failed && failing.launches == 1 && failing.joins == 1 &&
                failed_stats.launched_workers == failed_stats.joined_workers &&
                context.ActiveWorkers() == 0 &&
                operation.OperationActiveBytes() == before,
            "partial native launch did not join and restore charges");
  }
  Require(context.ActiveTrackedPayload() == 0,
          "worker callable storage leaked");
}
}  // namespace
int main() {
  try {
    RequestCases();
    AggregateBarrierCase();
    WorkerCases();
    std::cout
        << "Charged Repair requested-block and actual-worker checks passed\n";
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
