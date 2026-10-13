// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/repair/gltf_workflow.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gltf_workflow_test_internal.h"
#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace {
namespace repair = meshvale::repair;
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = repair::materialized_duplicates_detail;
namespace workflow = repair::gltf_workflow_detail;
constexpr std::uint64_t kPhysical = 1024 * 1024;
constexpr std::uint64_t kStage = 4 * kPhysical;
constexpr std::uint64_t kAggregate = 64 * kPhysical;
constexpr std::size_t kLedger = 128 * kPhysical;
constexpr std::size_t kWorkers = 4;
constexpr std::uint64_t kDepth = 64;
constexpr std::uint64_t kIdentity = 256;
constexpr std::size_t kOpaqueBytes = 65536;
constexpr std::size_t kBlockCapacity = 256;
constexpr std::size_t kNativeCapacity = 128;
constexpr std::uint64_t kPartition = 128;
constexpr std::uint64_t kCheckpoint = 32;
constexpr std::uint64_t kJobs = 32;
constexpr std::size_t kBinary32Bytes = 4;
constexpr std::uint32_t kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 255;
constexpr std::array kTargets{repair::DuplicateTarget{0, 1}};
void Require(bool accepted, const char* message) {
  if (!accepted) throw std::runtime_error(message);
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
geo::MeshMaterializationLimits CaptureLimits() {
  return {kPhysical, kPhysical, kPhysical, kPhysical, kPhysical, kPhysical,
          kPhysical, kPhysical, kPhysical, kPhysical, kStage};
}
repair::GltfDuplicateWorkflowLimits Limits() {
  repair::GltfDuplicateWorkflowLimits limits;
  limits.max_new_requested_bytes = kAggregate;
  limits.max_input_id_bytes = kIdentity;
  limits.max_reload_bindings = kPhysical;
  limits.max_report_bytes = kPhysical;
  const ic::GltfReadLimits read{kPhysical, kPhysical, kPhysical, kPhysical,
                                kDepth,    kPhysical, kIdentity, kIdentity,
                                kIdentity, kPhysical, kPhysical, kPhysical,
                                kPhysical, kStage};
  limits.read = limits.reload = {read, kStage};
  limits.extraction = limits.reload_extraction = {kPhysical, kPhysical, kStage};
  limits.materialization = {
      kPhysical, kStage, CaptureLimits(), {kPhysical, kStage}};
  limits.repair = {kPhysical, kPhysical,           kPhysical,      kPhysical,
                   kStage,    {kPhysical, kStage}, CaptureLimits()};
  limits.writing = {kPhysical,
                    kPhysical,
                    kDepth,
                    kPhysical,
                    kPhysical,
                    kPhysical,
                    kPhysical,
                    kPhysical,
                    kPhysical,
                    kPhysical,
                    {kPhysical, kPhysical, kPhysical, kPhysical, kStage}};
  limits.verification = {kPhysical, kPhysical, kDepth,    kPhysical, kPhysical,
                         kPhysical, kPhysical, kPhysical, kPhysical, kPhysical,
                         kPhysical, kPhysical, kStage};
  return limits;
}
repair::GltfDuplicateWorkflowWorkPolicy Work(bool serial = false) {
  const auto threshold = serial
                             ? static_cast<std::uint64_t>(
                                   std::numeric_limits<std::ptrdiff_t>::max())
                             : 1;
  return {{threshold, kPartition, threshold, kPartition},
          {threshold, kPartition, kCheckpoint, kJobs},
          {threshold, kPartition, kCheckpoint, kJobs},
          {threshold, kPartition, kCheckpoint, kJobs},
          {threshold, kPartition, kCheckpoint, kJobs}};
}
struct Fixture {
  std::string json =
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":48}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"extras":{"number":1.2300e+04,"long":")";
  std::vector<std::byte> binary;
  std::array<ic::SuppliedGltfResource, 1> resources;
  Fixture() {
    json.append(kOpaqueBytes, 'x');
    json += "\"}}";
    // Independently authored IEEE binary32 positions and uint16 directed loops.
    for (const auto bits : std::array<std::uint32_t, 9>{0, 0, 0, 0x3f800000, 0,
                                                        0, 0, 0x3f800000, 0})
      for (std::size_t byte = 0; byte < kBinary32Bytes; ++byte)
        binary.push_back(static_cast<std::byte>(
            (bits >> (byte * kBitsPerByte)) & kByteMask));
    for (const auto index : std::array<std::uint16_t, 6>{0, 1, 2, 0, 1, 2}) {
      binary.push_back(static_cast<std::byte>(index));
      binary.push_back(std::byte{});
    }
    resources[0] = {ic::GltfResourceKind::kBuffer, 0, binary};
  }
  repair::GltfDuplicateWorkflowRequest Request() const {
    return {Bytes(json),
            resources,
            "request-fixture",
            0,
            {ic::GltfTriangleRequest::kRequireTriangles,
             ic::GltfValueRequest::kRaw},
            kTargets,
            {}};
  }
};
struct Requests {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
  };
  std::array<Block, kBlockCapacity> blocks{};
  detail::AllocationBackend original = detail::DefaultAllocationBackend();
  const geo::ExecutionContext& context;
  std::size_t calls = 0;
  std::size_t fail_at = 0;
  std::size_t live = 0;
  std::size_t peak = 0;
  std::size_t initial_owned = 0;
  bool incorrect_free = false;
  const std::atomic<std::size_t>* source_active = nullptr;
  const std::atomic<std::size_t>* report_active = nullptr;
  explicit Requests(const geo::ExecutionContext& context) : context(context) {}
  static void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
    auto& self = *static_cast<Requests*>(state);
    ++self.calls;
    Require(self.context.ActiveTrackedPayload() >= self.live + bytes,
            "wrapper original allocation preceded admission");
    if (self.calls == self.fail_at) throw std::bad_alloc();
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks) {
      if (!block.pointer) {
        block = {pointer, bytes, alignment};
        self.live += bytes;
        self.peak = std::max(self.peak, self.live);
        if (self.calls == 2) self.initial_owned = self.live;
        return pointer;
      }
    }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::runtime_error("wrapper observer capacity exhausted");
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* state) noexcept {
    auto& self = *static_cast<Requests*>(state);
    for (auto& block : self.blocks) {
      if (block.pointer != pointer) continue;
      if (block.bytes != bytes || block.alignment != alignment ||
          self.context.ActiveTrackedPayload() < self.live ||
          (self.source_active && *self.source_active != 0) ||
          (self.report_active && *self.report_active != 0))
        self.incorrect_free = true;
      self.original.deallocate(pointer, bytes, alignment, self.original.state);
      self.live -= bytes;
      block = {};
      return;
    }
    self.incorrect_free = true;
  }
  detail::AllocationBackend Backend() { return {Allocate, Free, this}; }
  void Check(std::size_t expected) const {
    Require(!incorrect_free && live == expected,
            "wrapper original blocks/live counter/free ordering disagree");
  }
};
struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  std::size_t entered = 0;
  bool release = false;
  bool finished = false;
  void Enter() {
    std::unique_lock lock(mutex);
    ++entered;
    changed.notify_all();
    changed.wait(lock, [&] { return release; });
  }
  bool Wait() {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return entered >= kWorkers || finished; });
    return entered >= kWorkers;
  }
  void Release() {
    std::lock_guard lock(mutex);
    release = true;
    changed.notify_all();
  }
  void Finish() {
    std::lock_guard lock(mutex);
    finished = true;
    changed.notify_all();
  }
};
struct LaunchFailure {};
struct Workers {
  struct Entry {
    detail::NativeWorkerRecord original;
    Workers* owner = nullptr;
  };
  std::array<Entry, kNativeCapacity> entries{};
  detail::NativeWorkerBackend original = detail::DefaultNativeWorkerBackend();
  std::size_t calls = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t fail_at = 0;
  std::atomic<std::size_t> entered{0};
  std::atomic<std::size_t> active{0};
  std::stop_source* cancel = nullptr;
  Gate* gate = nullptr;
  static void Enter(detail::NativeWorkerRecord& record) noexcept {
    auto& entry = *static_cast<Entry*>(record.state);
    ++entry.owner->entered;
    if (entry.owner->cancel) entry.owner->cancel->request_stop();
    if (entry.owner->gate) entry.owner->gate->Enter();
    entry.original.entry(entry.original);
  }
  static std::uintptr_t Launch(detail::NativeWorkerRecord* record,
                               void* state) {
    auto& self = *static_cast<Workers*>(state);
    ++self.calls;
    if (self.calls == self.fail_at) throw LaunchFailure{};
    Require(self.calls <= self.entries.size(), "native observer capacity");
    auto& entry = self.entries[self.calls - 1];
    entry = {*record, &self};
    record->entry = Enter;
    record->state = &entry;
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launched;
    ++self.active;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* state) noexcept {
    auto& self = *static_cast<Workers*>(state);
    self.original.join(handle, self.original.state);
    --self.active;
    ++self.joined;
  }
  detail::NativeWorkerBackend Backend() { return {Launch, Join, this}; }
  void Check(bool native) const {
    Require(active == 0 && launched == joined && (launched > 0) == native &&
                entered == launched,
            "actual wrapper native handles/entry/join disagree");
  }
};
repair::GltfDuplicateWorkflowResult Run(
    const Fixture& fixture, const geo::ExecutionContext& context,
    Requests& requests, Workers& source, Workers& report,
    const repair::GltfDuplicateWorkflowLimits& limits = Limits(),
    const repair::GltfDuplicateWorkflowWorkPolicy& work = Work(),
    std::stop_token stop = {}) {
  requests.source_active = &source.active;
  requests.report_active = &report.active;
  const auto result = workflow::RunGltfDuplicateWorkflowForTest(
      fixture.Request(), limits, work, context, stop,
      {requests.Backend(), source.Backend(), report.Backend()});
  Require(
      result.execution.source_protection.launched_workers == source.launched &&
          result.execution.source_protection.joined_workers == source.joined &&
          result.execution.report_copy.launched_workers == report.launched &&
          result.execution.report_copy.joined_workers == report.joined,
      "published wrapper stats do not describe actual native handles");
  return result;
}
void Accepted(const repair::GltfDuplicateWorkflowResult& result,
              const Requests& requests) {
  Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
              result.record && result.record->Output() &&
              result.record->OutputInspection(),
          "independent wrapper request fixture did not complete");
  const auto& record = *result.record;
  Require(record.Outcomes().candidate == repair::CandidateOutcome::accepted &&
              record.Outcomes().profile ==
                  repair::GltfWorkflowProfileOutcome::kPassed &&
              record.Repair()->SourceFaces().size() == 1 &&
              record.Repair()->SourceFaces()[0] == 0 &&
              std::ranges::equal(record.Repair()->SourceCorners(),
                                 std::array<std::uint32_t, 3>{0, 1, 2}) &&
              std::ranges::equal(record.ReloadedPrimitive()->TriangleIndices(),
                                 std::array<std::uint32_t, 3>{0, 1, 2}),
          "literal independently authored selection/maps/reload changed");
  requests.Check(record.NewRequestedBytes().workflow);
  Require(record.NewRequestedBytes().total >= requests.live,
          "wrapper live bytes missing from complete operation total");
}
std::string StableReport(const repair::GltfDuplicateWorkflowRecord& record) {
  const auto bytes = record.ReportJson();
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                              bytes.size());
  const auto stats = text.find(",\"phase_execution\":");
  Require(stats != std::string_view::npos,
          "actual report missing separate phase observations");
  return std::string(text.substr(0, stats));
}
std::uint64_t OriginalRequestFaultsAndAllowance() {
  Fixture fixture;
  geo::ExecutionContext context({kWorkers, 1, kLedger});
  Requests baseline(context);
  Workers source, report;
  std::size_t calls = 0;
  std::uint64_t boundary = 0;
  {
    auto result = Run(fixture, context, baseline, source, report);
    Accepted(result, baseline);
    source.Check(true);
    report.Check(true);
    const auto& record = *result.record;
    Require(record.NewRequestedBytes().total == context.ActiveTrackedPayload(),
            "one workflow totals do not match its exclusive ledger");
    calls = baseline.calls;
    boundary =
        baseline.initial_owned + record.NewRequestedBytes().read +
        record.NewRequestedBytes().extraction +
        Limits().materialization.max_bridge_new_requested_bytes +
        Limits().materialization.materialization.max_new_requested_bytes +
        Limits().materialization.inspection.max_new_requested_bytes;
    const auto copied = record;
    Require(copied.ReportJson().data() == record.ReportJson().data() &&
                copied.NewRequestedBytes().total ==
                    record.NewRequestedBytes().total,
            "copy reallocated or duplicated fresh wrapper charges");
  }
  baseline.Check(0);
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "successful wrapper retained requests/workers after owner release");
  {
    Requests tiny(context);
    Workers source_tiny, report_tiny;
    auto limits = Limits();
    limits.max_new_requested_bytes = 1;
    const auto result =
        Run(fixture, context, tiny, source_tiny, report_tiny, limits);
    Require(result.status == repair::GltfWorkflowStatus::kBudgetExceeded &&
                result.diagnostic.stage ==
                    repair::GltfWorkflowStage::kConfiguration &&
                result.diagnostic.code ==
                    repair::GltfWorkflowDiagnosticCode::kRequestBudget &&
                !result.record && tiny.calls == 0 &&
                context.ActiveTrackedPayload() == 0,
            "insufficient aggregate seed space reached original allocation");
    tiny.Check(0);
  }
  for (std::size_t ordinal = 1; ordinal <= calls; ++ordinal) {
    Requests failing(context);
    Workers source_failure, report_failure;
    failing.fail_at = ordinal;
    bool threw = false;
    try {
      (void)Run(fixture, context, failing, source_failure, report_failure);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    Require(threw && failing.calls == ordinal &&
                source_failure.launched == source_failure.joined &&
                report_failure.launched == report_failure.joined &&
                context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == 0,
            "original wrapper fault did not propagate after complete rollback");
    failing.Check(0);
  }
  for (const bool smaller : {false, true}) {
    Requests exact(context);
    Workers source_exact, report_exact;
    auto limits = Limits();
    limits.max_new_requested_bytes = boundary - (smaller ? 1 : 0);
    {
      const auto result =
          Run(fixture, context, exact, source_exact, report_exact, limits);
      if (!smaller) {
        Accepted(result, exact);
      } else {
        Require(
            result.status == repair::GltfWorkflowStatus::kBudgetExceeded &&
                result.diagnostic.stage ==
                    repair::GltfWorkflowStage::kMaterialize &&
                result.diagnostic.code == repair::GltfWorkflowDiagnosticCode::
                                              kMaterializationAllowance &&
                result.record && !result.record->Source(),
            "one byte below literal overlapping allowance invoked transfer");
      }
    }
    exact.Check(0);
    Require(context.ActiveTrackedPayload() == 0,
            "materialization allowance boundary retained charges");
  }
  std::cout << "wrapper original request peak " << baseline.peak
            << ", ordinals " << calls << ", materialization allowance boundary "
            << boundary << '\n';
  return boundary;
}
void NativeFaultCancellationAndAgreement() {
  Fixture fixture;
  geo::ExecutionContext context({kWorkers, 1, kLedger});
  std::string expected_report;
  std::vector<std::byte> expected_json;
  for (const int mode : {0, 1, 2}) {
    Requests requests(context);
    Workers source, report;
    auto lease = mode == 2 ? geo::TryReserveWorkers(context, kWorkers)
                           : geo::WorkerLease();
    {
      auto result = Run(fixture, context, requests, source, report, Limits(),
                        Work(mode == 1));
      Accepted(result, requests);
      source.Check(mode == 0);
      report.Check(mode == 0);
      const auto json = result.record->Output()->Json();
      if (mode == 0) {
        expected_report = StableReport(*result.record);
        expected_json.assign(json.begin(), json.end());
      } else {
        Require(expected_report == StableReport(*result.record) &&
                    std::ranges::equal(expected_json, json) &&
                    result.execution.source_protection.launched_workers == 0 &&
                    result.execution.report_copy.launched_workers == 0 &&
                    result.execution.source_protection.caller_jobs > 0 &&
                    result.execution.report_copy.caller_jobs > 0,
                "serial/saturated caller result differs from native authority");
      }
    }
    requests.Check(0);
    Require(context.ActiveTrackedPayload() == 0,
            "work policy agreement left wrapper charges");
  }
  for (const bool at_report : {false, true}) {
    Requests requests(context);
    Workers source, report;
    auto& failing = at_report ? report : source;
    failing.fail_at = 2;
    bool threw = false;
    try {
      (void)Run(fixture, context, requests, source, report);
    } catch (const LaunchFailure&) {
      threw = true;
    }
    Require(threw && failing.calls == 2 && failing.launched == 1 &&
                failing.joined == 1 && context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == 0,
            "partial wrapper launch failed to join before rollback/rethrow");
    requests.Check(0);
  }
  for (const bool at_report : {false, true}) {
    Requests requests(context);
    Workers source, report;
    std::stop_source stop;
    (at_report ? report : source).cancel = &stop;
    const auto result = Run(fixture, context, requests, source, report,
                            Limits(), Work(), stop.get_token());
    Require(
        result.status == repair::GltfWorkflowStatus::kCanceled &&
            !result.record &&
            result.outcomes.candidate == repair::CandidateOutcome::accepted &&
            result.outcomes.exit ==
                repair::GltfWorkflowExitMeaning::kCanceled &&
            result.diagnostic.stage ==
                (at_report ? repair::GltfWorkflowStage::kReport
                           : repair::GltfWorkflowStage::kVerify) &&
            context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
        "native wrapper cancellation lost stage/facts or published output");
    source.Check(true);
    report.Check(at_report);
    requests.Check(0);
  }
}
void ConcurrentWorkflowCaps(std::uint64_t allowance_boundary) {
  Fixture fixture;
  std::size_t measured_ledger_peak = 0;
  {
    geo::ExecutionContext context({kWorkers, 1, kLedger});
    Requests requests(context);
    Workers source, report;
    const auto result = Run(fixture, context, requests, source, report);
    Accepted(result, requests);
    // A physical shared-ledger boundary, never a fresh workflow-cap estimate.
    measured_ledger_peak = context.PeakTrackedPayload();
  }
  for (const bool refuse_second : {false, true}) {
    geo::ExecutionContext context(
        {kWorkers, 1, refuse_second ? measured_ledger_peak + 1 : kLedger});
    Requests first_requests(context);
    Workers first_source, first_report;
    Gate gate;
    first_report.gate = &gate;
    std::optional<repair::GltfDuplicateWorkflowResult> first_result;
    std::exception_ptr first_exception;
    std::jthread first([&] {
      try {
        first_result =
            Run(fixture, context, first_requests, first_source, first_report);
      } catch (...) {
        first_exception = std::current_exception();
      }
      gate.Finish();
    });
    const auto ready = gate.Wait();
    std::exception_ptr second_exception;
    try {
      Require(ready, "first workflow never reached actual report workers");
      const auto before = context.ActiveTrackedPayload();
      Requests second_requests(context);
      Workers second_source, second_report;
      auto second_limits = Limits();
      if (!refuse_second) {
        second_limits.max_new_requested_bytes = allowance_boundary;
      }
      {
        const auto second = Run(fixture, context, second_requests,
                                second_source, second_report, second_limits);
        if (refuse_second) {
          Require(
              second.status == repair::GltfWorkflowStatus::kBudgetExceeded &&
                  second.diagnostic.code ==
                      repair::GltfWorkflowDiagnosticCode::kRequestBudget &&
                  !second.record,
              "concurrent shared ledger refusal lost actual budget outcome");
          std::cout << "concurrent ledger refusal stage "
                    << static_cast<int>(second.diagnostic.stage) << ", code "
                    << static_cast<int>(second.diagnostic.code) << '\n';
        } else {
          Accepted(second, second_requests);
          Require(
              second.record->NewRequestedBytes().total <= allowance_boundary &&
                  context.ActiveTrackedPayload() ==
                      before + second.record->NewRequestedBytes().total,
              "foreign live workflow charges consumed the fresh cap");
          second_source.Check(false);
          second_report.Check(false);
        }
      }
      second_requests.Check(0);
      Require(
          context.ActiveTrackedPayload() == before,
          "second workflow rollback/release changed concurrent first owner");
    } catch (...) {
      second_exception = std::current_exception();
    }
    gate.Release();
    first.join();
    if (first_exception) std::rethrow_exception(first_exception);
    if (second_exception) std::rethrow_exception(second_exception);
    Require(first_result.has_value(), "first workflow lost result after joins");
    Accepted(*first_result, first_requests);
    first_source.Check(true);
    first_report.Check(true);
    first_result.reset();
    first_requests.Check(0);
    Require(
        context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
        "concurrent workflow owners retained charge or worker after release");
  }
}
}  // namespace
int main() {
  try {
    const auto allowance_boundary = OriginalRequestFaultsAndAllowance();
    NativeFaultCancellationAndAgreement();
    ConcurrentWorkflowCaps(allowance_boundary);
    std::cout << "independent glTF workflow wrapper requests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "unexpected native workflow exception\n";
    return 1;
  }
}
