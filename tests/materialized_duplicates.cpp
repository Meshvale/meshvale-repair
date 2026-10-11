// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <variant>
#include <vector>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_test_internal.h"
#include "materialized_duplicate_workers.h"

namespace {
namespace geo = meshvale::geometry;
namespace repair = meshvale::repair;
namespace detail = repair::materialized_duplicates_detail;
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kMinimumParallelVertices = 1;
constexpr std::uint64_t kLedgerBudgetBytes = 128 * 1024 * 1024;
constexpr std::uint64_t kOperationBudgetBytes = 32 * 1024 * 1024;
constexpr std::uint64_t kCountLimit = 1000000;
constexpr std::uint64_t kDeclarationByteLimit = 1024 * 1024;
constexpr std::uint64_t kComparisonStepLimit = 1000000;
constexpr std::uint64_t kIssueLimit = 1000;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kPartitionBytes = 1024;
constexpr std::uint64_t kCancellationBlockScalars = 32;
constexpr std::uint64_t kMaximumJobs = 32;
constexpr std::size_t kSubstantialFaces = 4096;
constexpr std::size_t kTriangleCorners = 3;
constexpr std::size_t kMaximumObservedBlocks = 1024;
constexpr std::size_t kLongRowScalars = 65536;
constexpr std::uint64_t kLongRowParallelMinimumBytes = 1024 * 1024;
constexpr std::uint32_t kInfinityBits = UINT32_C(0x7f800000);
constexpr char kFieldName[] = "authored_float";
constexpr char kLabelSemantic[] = "label";
constexpr char kLongRowName[] = "long_corner_row";
constexpr char kTexcoordSemantic[] = "texcoord";
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
geo::ExecutionContext Context() {
  return geo::ExecutionContext(
      {kWorkerBudget, kMinimumParallelVertices, kLedgerBudgetBytes});
}
geo::MeshMaterializationLimits CaptureLimits() {
  return {kCountLimit, kCountLimit, kCountLimit,          kCountLimit,
          kCountLimit, kCountLimit, kCountLimit,          kCountLimit,
          kCountLimit, kCountLimit, kOperationBudgetBytes};
}
repair::MaterializedDuplicateLimits Limits() {
  return {kCountLimit,           kCountLimit,
          kDeclarationByteLimit, kComparisonStepLimit,
          kOperationBudgetBytes, {kIssueLimit, kOperationBudgetBytes},
          CaptureLimits()};
}
geo::MeshMaterializationWorkPolicy Work(bool parallel) {
  return {parallel ? kParallelMinimumBytes : kOperationBudgetBytes,
          kPartitionBytes, kCancellationBlockScalars, kMaximumJobs};
}
geo::MaterializedMesh Capture(
    const geo::ExecutionContext& context, std::size_t faces = 2,
    std::span<const geo::AttributeMaterializationInput> attributes = {},
    bool invalid_corner = false) {
  const std::array<double, 12> positions{0, 0, 0, 1, 0, 0, 0, 1, 0, -0.0, 7, 7};
  std::vector<geo::index_t> offsets;
  std::vector<geo::index_t> corners;
  offsets.reserve(faces + 1);
  corners.reserve(faces * kTriangleCorners);
  offsets.push_back(0);
  for (std::size_t face = 0; face < faces; ++face) {
    corners.insert(corners.end(), {0, 1, 2});
    offsets.push_back(corners.size());
  }
  if (invalid_corner) corners[0] = 4;
  geo::MeshMaterializationInput input;
  input.positions = std::span<const double>(positions);
  input.topology = geo::PolygonTopologyInput{offsets, corners};
  input.attributes = attributes;
  const auto captured =
      geo::MaterializeMesh(input, CaptureLimits(), Work(false), context);
  Require(
      captured.status == geo::MaterializationStatus::kSuccess && captured.mesh,
      "Focused public Geometry capture failed");
  return *captured.mesh;
}
void Success(const repair::MaterializedDuplicateResult& result) {
  Require(result.status == repair::MaterializedDuplicateStatus::kSuccess &&
              result.repair &&
              result.repair->SourceInspection().IsStorageValid() &&
              result.repair->CandidateInspection().IsStorageValid(),
          "Direct charged Repair did not publish a verified owner");
}
struct Allocations {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
  };
  std::array<Block, kMaximumObservedBlocks> blocks{};
  detail::AllocationBackend original = detail::DefaultAllocationBackend();
  const geo::ExecutionContext* context = nullptr;
  std::size_t source_bytes = 0;
  std::size_t live = 0;
  std::size_t calls = 0;
  std::size_t fail_ordinal = 0;
  bool bad_free = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Allocations*>(state);
    ++self.calls;
    Require(self.context->ActiveTrackedPayload() >=
                self.source_bytes + self.live + bytes,
            "Repair original block was not admitted before allocation");
    if (self.calls == self.fail_ordinal) throw std::bad_alloc();
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks)
      if (!block.pointer) {
        block = {pointer, bytes, alignment};
        self.live += bytes;
        return pointer;
      }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::runtime_error("Fixed allocation observer capacity exhausted");
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Allocations*>(state);
    for (auto& block : self.blocks)
      if (block.pointer == pointer) {
        if (block.bytes != bytes || block.alignment != alignment ||
            self.context->ActiveTrackedPayload() <
                self.source_bytes + self.live)
          self.bad_free = true;
        self.original.deallocate(pointer, bytes, alignment,
                                 self.original.state);
        self.live -= bytes;
        block = {};
        return;
      }
    self.bad_free = true;
  }
  detail::AllocationBackend Hooks() { return {Allocate, Free, this}; }
};
void RequestFaults() {
  const auto context = Context();
  const auto source = Capture(context);
  const auto baseline = context.ActiveTrackedPayload();
  const std::array<repair::DuplicateTarget, 1> targets{{{0, 1}}};
  Allocations observed;
  observed.context = &context;
  observed.source_bytes = baseline;
  detail::TestHooks hooks;
  hooks.allocation = observed.Hooks();
  {
    auto result = detail::TestRemoveMaterializedDuplicateFaces(
        source, targets, {}, Limits(), Work(false), {}, hooks);
    Success(result);
    const auto bytes = result.repair->NewRequestedBytes();
    Require(
        bytes.repair == observed.live &&
            bytes.total == bytes.repair + bytes.source_inspection +
                               bytes.selection + bytes.candidate_inspection &&
            bytes.source_inspection != 0 && bytes.selection != 0 &&
            bytes.candidate_inspection != 0,
        "Live operation split must include each delegated owner exactly once");
    const auto calls = observed.calls;
    auto copied = *result.repair;
    result.repair.reset();
    Require(
        observed.calls == calls && copied.Candidate().View().positions.data() ==
                                       source.View().positions.data(),
        "Copying the immutable Repair owner allocated or lost shared vertices");
  }
  Require(observed.calls != 0 && observed.live == 0 && !observed.bad_free &&
              context.ActiveTrackedPayload() == baseline,
          "Repair block or delegated owner leaked after destruction");
  for (std::size_t ordinal = 1; ordinal <= observed.calls; ++ordinal) {
    Allocations fault;
    fault.context = &context;
    fault.source_bytes = baseline;
    fault.fail_ordinal = ordinal;
    hooks.allocation = fault.Hooks();
    bool failed = false;
    try {
      static_cast<void>(detail::TestRemoveMaterializedDuplicateFaces(
          source, targets, {}, Limits(), Work(false), {}, hooks));
    } catch (const std::bad_alloc&) {
      failed = true;
    }
    Require(failed && fault.calls == ordinal && fault.live == 0 &&
                !fault.bad_free && context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "Injected Repair request failure lost full transactional cleanup");
  }
}
bool BudgetFailure(const repair::MaterializedDuplicateResult& result) {
  return result.status ==
             repair::MaterializedDuplicateStatus::kBudgetExceeded ||
         (result.status ==
              repair::MaterializedDuplicateStatus::kGeometryFailure &&
          result.diagnostic.geometry_status ==
              geo::MaterializationStatus::kBudgetExceeded);
}
void CapBoundary() {
  const auto context = Context();
  const auto source = Capture(context);
  const auto baseline = context.ActiveTrackedPayload();
  const std::array<repair::DuplicateTarget, 1> targets{{{0, 1}}};
  auto limits = Limits();
  std::uint64_t low = 1, high = limits.max_new_requested_bytes;
  while (low < high) {
    const auto middle = low + (high - low) / 2;
    limits.max_new_requested_bytes = middle;
    const auto result = repair::RemoveMaterializedDuplicateFaces(
        source, targets, {}, limits, Work(false));
    if (result.status == repair::MaterializedDuplicateStatus::kSuccess) {
      Success(result);
      high = middle;
    } else {
      Require(BudgetFailure(result) && !result.repair,
              "Aggregate-cap boundary search changed semantic behavior");
      low = middle + 1;
    }
  }
  limits.max_new_requested_bytes = low;
  {
    const auto exact = repair::RemoveMaterializedDuplicateFaces(
        source, targets, {}, limits, Work(false));
    Success(exact);
    Require(exact.repair->NewRequestedBytes().total <= low,
            "Live published split exceeded admitted aggregate cap");
  }
  Require(low > 1, "Complete operation unexpectedly had a one-byte cap");
  limits.max_new_requested_bytes = low - 1;
  const auto below = repair::RemoveMaterializedDuplicateFaces(
      source, targets, {}, limits, Work(false));
  Require(
      BudgetFailure(below) && !below.repair &&
          context.ActiveTrackedPayload() == baseline,
      "One byte below the minimum accepted aggregate cap did not roll back");
  // This is a public admission boundary. Independent original-route observers
  // establish the complete requested-block peak, including Geometry requests.
  const auto remaining = context.TrackedPayloadBudget() - baseline;
  auto foreign = geo::TryReservePayload(context, remaining);
  Require(foreign.status == geo::ReservationStatus::kAccepted,
          "Foreign ledger lease failed before contention test");
  limits = Limits();
  const auto blocked = repair::RemoveMaterializedDuplicateFaces(
      source, targets, {}, limits, Work(false));
  Require(
      blocked.status == repair::MaterializedDuplicateStatus::kBudgetExceeded &&
          !blocked.repair &&
          context.ActiveTrackedPayload() == context.TrackedPayloadBudget(),
      "Concurrent foreign ledger charges were ignored");
  foreign.lease.Reset();
  Require(context.ActiveTrackedPayload() == baseline,
          "Foreign lease cleanup changed source charge");
}
struct Launches {
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::size_t launched = 0;
  std::size_t joined = 0;
  bool fail_second = false;
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* state) {
    auto& self = *static_cast<Launches*>(state);
    if (self.fail_second && self.launched == 1)
      throw std::runtime_error("Injected second actual Repair launch failure");
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<Launches*>(state);
    self.original.join(handle, self.original.state);
    ++self.joined;
  }
  detail::NativeWorkerBackend Hooks() { return {Launch, Join, this}; }
};
void Workers() {
  const auto context = Context();
  const auto source = Capture(context, kSubstantialFaces);
  const auto baseline = context.ActiveTrackedPayload();
  std::vector<repair::DuplicateTarget> targets;
  for (std::size_t face = 1; face < kSubstantialFaces; face += 2)
    targets.push_back({face - 1, face});
  auto serial = repair::RemoveMaterializedDuplicateFaces(source, targets, {},
                                                         Limits(), Work(false));
  Success(serial);
  Launches native;
  detail::TestHooks hooks;
  hooks.workers = native.Hooks();
  auto parallel = detail::TestRemoveMaterializedDuplicateFaces(
      source, targets, {}, Limits(), Work(true), {}, hooks);
  Success(parallel);
  Require(native.launched > 0 && native.joined == native.launched &&
              context.ActiveWorkers() == 0,
          "Substantial actual Repair worker launches were not joined");
  const auto& stats = parallel.execution;
  for (const auto* phase : {&stats.comparison, &stats.correspondence,
                            &stats.verification, &stats.directed_verification})
    Require(phase->launched_workers > 0 &&
                phase->launched_workers == phase->joined_workers,
            "Substantial phase lacked positive actual joined-worker evidence");
  Require(std::equal(serial.repair->FaceMap().begin(),
                     serial.repair->FaceMap().end(),
                     parallel.repair->FaceMap().begin(),
                     parallel.repair->FaceMap().end()) &&
              std::equal(serial.repair->CornerMap().begin(),
                         serial.repair->CornerMap().end(),
                         parallel.repair->CornerMap().begin(),
                         parallel.repair->CornerMap().end()),
          "Serial/parallel many-to-one correspondence differed");
  for (std::size_t face = 0; face < kSubstantialFaces; ++face) {
    Require(parallel.repair->FaceMap()[face] == face / 2,
            "Literal independent many-to-one face map changed");
    for (std::size_t corner = 0; corner < kTriangleCorners; ++corner)
      Require(parallel.repair->CornerMap()[face * kTriangleCorners + corner] ==
                  (face / 2) * kTriangleCorners + corner,
              "Literal independent many-to-one corner map changed");
  }
  const auto first = std::as_bytes(serial.repair->Candidate().View().positions);
  const auto second =
      std::as_bytes(parallel.repair->Candidate().View().positions);
  Require(std::equal(first.begin(), first.end(), second.begin(), second.end()),
          "Serial/parallel retained numerical bytes differed");
  serial.repair.reset();
  parallel.repair.reset();
  native = {};
  native.fail_second = true;
  hooks.workers = native.Hooks();
  geo::MaterializationExecutionStats observed;
  hooks.worker_observer = &observed;
  bool failed = false;
  try {
    static_cast<void>(detail::TestRemoveMaterializedDuplicateFaces(
        source, targets, {}, Limits(), Work(true), {}, hooks));
  } catch (const std::runtime_error&) {
    failed = true;
  }
  Require(failed && native.launched == 1 && native.joined == 1 &&
              observed.launched_workers == 1 && observed.joined_workers == 1 &&
              context.ActiveWorkers() == 0 &&
              context.ActiveTrackedPayload() == baseline,
          "Partial actual launch failed to join and restore source-only state");
}
void LongRowDirectedWorkers() {
  const auto context = Context();
  std::vector<double> values(6 * kLongRowScalars);
  for (std::size_t row = 0; row < 6; ++row)
    for (std::size_t scalar = 0; scalar < kLongRowScalars; ++scalar)
      values[row * kLongRowScalars + scalar] =
          static_cast<double>(row % 3) + static_cast<double>(scalar);
  const std::array<geo::index_t, 7> offsets{0,
                                            kLongRowScalars,
                                            2 * kLongRowScalars,
                                            3 * kLongRowScalars,
                                            4 * kLongRowScalars,
                                            5 * kLongRowScalars,
                                            6 * kLongRowScalars};
  geo::AttributeMaterializationInput attribute;
  attribute.domain = geo::AttributeDomain::corner;
  attribute.name = kLongRowName;
  attribute.semantic = kTexcoordSemantic;
  attribute.components = 1;
  attribute.values = std::span<const double>(values);
  attribute.offsets = std::span<const geo::index_t>(offsets);
  const std::array attributes{attribute};
  const auto source = Capture(context, 2, attributes);
  const std::array<repair::DuplicateTarget, 1> targets{{{0, 1}}};
  auto parallel_work = Work(true);
  parallel_work.parallel_min_bytes = kLongRowParallelMinimumBytes;
  const auto serial = repair::RemoveMaterializedDuplicateFaces(
      source, targets, {}, Limits(), Work(false));
  const auto parallel = repair::RemoveMaterializedDuplicateFaces(
      source, targets, {}, Limits(), parallel_work);
  Success(serial);
  Success(parallel);
  const auto& directed = parallel.execution.directed_verification;
  Require(directed.launched_workers > 0 &&
              directed.launched_workers == directed.joined_workers &&
              serial.execution.directed_verification.launched_workers == 0 &&
              context.ActiveWorkers() == 0,
          "Few long corner rows were treated as tiny directed face records");
  Require(std::equal(serial.repair->FaceMap().begin(),
                     serial.repair->FaceMap().end(),
                     parallel.repair->FaceMap().begin(),
                     parallel.repair->FaceMap().end()) &&
              std::equal(serial.repair->CornerMap().begin(),
                         serial.repair->CornerMap().end(),
                         parallel.repair->CornerMap().begin(),
                         parallel.repair->CornerMap().end()),
          "Long-row serial/parallel correspondence differed");
  const auto& a = serial.repair->Candidate().View().attributes[0];
  const auto& b = parallel.repair->Candidate().View().attributes[0];
  const auto first = std::as_bytes(std::get<std::span<const double>>(a.values));
  const auto second =
      std::as_bytes(std::get<std::span<const double>>(b.values));
  Require(std::equal(first.begin(), first.end(), second.begin(), second.end()),
          "Long-row serial/parallel retained bits differed");
  // One retained face still has three independently readable rotations.
  // Give that single face the same large ragged backing without requiring an
  // edit.
  attribute.values = std::span<const double>(values).first(3 * kLongRowScalars);
  attribute.offsets = std::span<const geo::index_t>(offsets).first(4);
  const std::array single_attributes{attribute};
  const auto single_long = Capture(context, 1, single_attributes);
  const auto unchanged = repair::RemoveMaterializedDuplicateFaces(
      single_long, {}, {}, Limits(), parallel_work);
  Success(unchanged);
  Require(
      unchanged.execution.directed_verification.launched_workers > 0 &&
          unchanged.execution.directed_verification.launched_workers ==
              unchanged.execution.directed_verification.joined_workers &&
          unchanged.repair->Outcome() == repair::CandidateOutcome::unchanged,
      "Single long face did not use real directed verification workers");
}
void FailuresAndCancellation() {
  const auto context = Context();
  const std::array<float, 4> values{std::bit_cast<float>(kInfinityBits), 0,
                                    std::bit_cast<float>(kInfinityBits), 0};
  geo::AttributeMaterializationInput attribute;
  attribute.domain = geo::AttributeDomain::vertex;
  attribute.name = kFieldName;
  attribute.semantic = kLabelSemantic;
  attribute.components = 1;
  attribute.values = std::span<const float>(values);
  const std::array attributes{attribute};
  const auto source = Capture(context, 2, attributes);
  const auto baseline = context.ActiveTrackedPayload();
  for (const bool parallel : {false, true}) {
    std::stop_source stop;
    detail::TestHooks hooks;
    hooks.failure_observer_state = &stop;
    hooks.failure_observer = [](const repair::MaterializedDuplicateDiagnostic&,
                                void* state) noexcept {
      static_cast<std::stop_source*>(state)->request_stop();
    };
    auto work = Work(parallel);
    work.partition_bytes = sizeof(float);
    work.cancellation_block_scalars = 1;
    const auto result = detail::TestRemoveMaterializedDuplicateFaces(
        source, {}, {}, Limits(), work, stop.get_token(), hooks);
    Require(result.status == repair::MaterializedDuplicateStatus::kRejected &&
                result.diagnostic.code ==
                    repair::MaterializedDuplicateDiagnosticCode::
                        kNonfiniteAttribute &&
                result.diagnostic.attribute == 0 &&
                result.diagnostic.element == 0 && !result.repair &&
                context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "Earlier source failure was replaced by cancellation or a later "
            "diagnostic");
  }
  std::stop_source stopped;
  stopped.request_stop();
  const auto canceled = repair::RemoveMaterializedDuplicateFaces(
      source, {}, {}, Limits(), Work(true), stopped.get_token());
  Require(canceled.status == repair::MaterializedDuplicateStatus::kCanceled &&
              !canceled.repair && context.ActiveTrackedPayload() == baseline,
          "Fresh precancellation did not roll back");
  const auto invalid = Capture(context, 2, {}, true);
  const auto bad = repair::RemoveMaterializedDuplicateFaces(
      invalid, {}, {}, Limits(), Work(false));
  Require(bad.status ==
                  repair::MaterializedDuplicateStatus::kInvalidSourceStorage &&
              bad.diagnostic.geometry_status ==
                  geo::MaterializationStatus::kSuccess &&
              bad.diagnostic.geometry_diagnostic &&
              bad.diagnostic.storage_issue &&
              bad.diagnostic.storage_issue->code ==
                  geo::StorageIssueCode::kCornerVertexRange &&
              !bad.repair,
          "Successful inspection with issues lost its genuine Geometry result");
}
}  // namespace
int main() {
  try {
    RequestFaults();
    CapBoundary();
    Workers();
    LongRowDirectedWorkers();
    FailuresAndCancellation();
    std::cout << "Direct charged Repair faults, caps, cancellation and workers "
                 "passed\n";
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
