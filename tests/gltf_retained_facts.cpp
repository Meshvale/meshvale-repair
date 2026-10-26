// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_file_workflow_fixture.h"
#include "gltf_file_workflow_test_internal.h"
#include "gltf_translation_workflow_fixture.h"
#include "gltf_workflow_test_internal.h"
#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace {
namespace repair = meshvale::repair;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace fixture = repair::test_file_workflow;
namespace file = repair::gltf_file_workflow_detail;
namespace memory = repair::gltf_workflow_detail;
namespace own = repair::materialized_duplicates_detail;
using fixture::Fixture;
using fixture::Require;
constexpr std::size_t kAllocationSlots = 2048;
constexpr std::size_t kWorkerSlots = 256;
constexpr std::uint64_t kPartitionBytes = 128;
constexpr std::size_t kPositionComponents = 3;
constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
constexpr std::array<geo::index_t, 1> kSelectedFaces{0};
constexpr std::array<geo::index_t, 3> kSelectedCorners{0, 1, 2};
constexpr std::array<geo::index_t, 3> kSourceOffsets{0, 3, 6};
constexpr std::array<repair::DuplicateTarget, 1> kInvalidTargets{{{0, 9}}};
constexpr int kUnknownPolicy = 927;
using Policy = repair::GltfReportFailureRetention;
using Status = repair::GltfWorkflowStatus;
using Stage = repair::GltfWorkflowStage;
using Code = repair::GltfWorkflowDiagnosticCode;
enum class Fault { kVerifyStop, kReportStop, kReportExtent, kReportBudget };
constexpr std::array kFaults{Fault::kVerifyStop, Fault::kReportStop,
                             Fault::kReportExtent, Fault::kReportBudget};

struct Allocations {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
  };
  std::array<Block, kAllocationSlots> blocks{};
  std::mutex mutex;
  std::atomic<bool> frozen{false};
  std::size_t requests = 0;
  std::size_t refused = 0;
  std::size_t live = 0;
  bool bad_free = false;
  const geo::ExecutionContext* context = nullptr;
  const std::size_t* source_joins = nullptr;
  std::size_t exhaust_after_joins = 0;
  bool exhausted = false;
  geo::PayloadLease held_budget;
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<Allocations*>(opaque);
    std::lock_guard lock(self.mutex);
    ++self.requests;
    if (self.frozen.load()) {
      ++self.refused;
      throw std::bad_alloc{};
    }
    auto slot = std::ranges::find_if(
        self.blocks, [](const Block& block) { return !block.pointer; });
    Require(slot != self.blocks.end(),
            "independent allocation observer capacity");
    const auto backend = own::DefaultAllocationBackend();
    void* pointer = backend.allocate(bytes, alignment, backend.state);
    *slot = {pointer, bytes, alignment};
    self.live += bytes;
    // After the calibrated last source-verification join, this is the real
    // report staging request. Its original admission already succeeded. Hold
    // the remaining shared ledger before the next real report request; do not
    // fabricate an AdmissionFailure or replace a delegate allocator.
    if (!self.exhausted && self.exhaust_after_joins && self.source_joins &&
        *self.source_joins == self.exhaust_after_joins) {
      self.exhausted = true;
      auto reservation = geo::TryReservePayload(
          *self.context, self.context->TrackedPayloadBudget() -
                             self.context->ActiveTrackedPayload());
      Require(reservation.status == geo::ReservationStatus::kAccepted &&
                  reservation.lease.Bytes() > 0,
              "actual remaining ledger held before report growth");
      self.held_budget = std::move(reservation.lease);
    }
    return pointer;
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* opaque) noexcept {
    auto& self = *static_cast<Allocations*>(opaque);
    std::lock_guard lock(self.mutex);
    auto slot = std::ranges::find_if(self.blocks, [pointer](const Block& b) {
      return b.pointer == pointer;
    });
    if (slot == self.blocks.end() || slot->bytes != bytes ||
        slot->alignment != alignment || self.live < bytes) {
      self.bad_free = true;
    } else {
      self.live -= bytes;
      *slot = {};
    }
    const auto backend = own::DefaultAllocationBackend();
    backend.deallocate(pointer, bytes, alignment, backend.state);
  }
  own::AllocationBackend Backend() { return {Allocate, Free, this}; }
  void Empty() const {
    Require(live == 0 && !bad_free && refused == 0,
            "original backend blocks released once with exact size/alignment");
  }
};
struct LaunchFailure {};
struct Workers {
  struct Entry {
    own::NativeWorkerRecord original;
    Workers* owner = nullptr;
    std::size_t launch = 0;
  };
  std::array<Entry, kWorkerSlots> entries{};
  own::NativeWorkerBackend original = own::DefaultNativeWorkerBackend();
  std::size_t calls = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t cancel_at = 0;
  std::size_t fail_at = 0;
  std::atomic<std::size_t> entered{0};
  std::stop_source* stop = nullptr;
  Allocations* wrapper = nullptr;
  Allocations* memory = nullptr;
  static void Enter(own::NativeWorkerRecord& record) noexcept {
    auto& entry = *static_cast<Entry*>(record.state);
    ++entry.owner->entered;
    if (entry.owner->cancel_at && entry.launch >= entry.owner->cancel_at) {
      entry.owner->wrapper->frozen = true;
      entry.owner->memory->frozen = true;
      entry.owner->stop->request_stop();
    }
    entry.original.entry(entry.original);
  }
  static std::uintptr_t Launch(own::NativeWorkerRecord* record, void* opaque) {
    auto& self = *static_cast<Workers*>(opaque);
    ++self.calls;
    if (self.calls == self.fail_at) throw LaunchFailure{};
    Require(self.calls <= self.entries.size(), "independent worker capacity");
    auto& entry = self.entries[self.calls - 1];
    entry = {*record, &self, self.calls};
    record->entry = Enter;
    record->state = &entry;
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<Workers*>(opaque);
    self.original.join(handle, self.original.state);
    ++self.joined;
  }
  own::NativeWorkerBackend Backend() { return {Launch, Join, this}; }
  void Joined() const {
    Require(launched == joined && entered == launched,
            "every actual worker entered and joined before return/throw");
  }
};
struct Observers {
  Allocations wrapper, memory;
  Workers source, report;
  std::stop_source stop;
  explicit Observers(const geo::ExecutionContext& context) {
    memory.context = &context;
    memory.source_joins = &source.joined;
    for (auto* worker : {&source, &report}) {
      worker->stop = &stop;
      worker->wrapper = &wrapper;
      worker->memory = &memory;
    }
  }
  file::FileWorkflowTestOptions Options() {
    file::FileWorkflowTestOptions result;
    result.allocation_backend = wrapper.Backend();
    result.memory = {memory.Backend(), source.Backend(), report.Backend()};
    return result;
  }
  void Joined() const {
    source.Joined();
    report.Joined();
    Require(wrapper.refused == 0 && memory.refused == 0,
            "real expected-failure handoff made no frozen original request");
  }
  void Empty() const {
    wrapper.Empty();
    memory.Empty();
  }
};
auto Work() {
  auto work = fixture::Work();
  work.memory.verification.partition_bytes = kPartitionBytes;
  work.memory.report.partition_bytes = kPartitionBytes;
  return work;
}
std::span<const repair::DuplicateTarget> Targets(bool changed) {
  return changed ? std::span<const repair::DuplicateTarget>(fixture::kTargets)
                 : std::span<const repair::DuplicateTarget>{};
}
void Facts(const repair::MaterializedDuplicateRepair& facts, bool changed) {
  const auto source = facts.Source().View();
  const auto candidate = facts.Candidate().View();
  Require(source.positions.size() / kPositionComponents == 3 &&
              source.face_offsets.size() - 1 == 2 &&
              source.corner_vertices.size() == 6 &&
              candidate.positions.size() / kPositionComponents == 3 &&
              candidate.face_offsets.size() - 1 == (changed ? 1u : 2u) &&
              candidate.corner_vertices.size() == (changed ? 3u : 6u),
          "literal source/candidate vertex-face-corner counts");
  Require(std::ranges::equal(source.face_offsets, kSourceOffsets) &&
              std::ranges::equal(source.corner_vertices,
                                 fixture::kOriginalTriangles) &&
              std::ranges::equal(candidate.corner_vertices,
                                 changed ? std::span<const std::uint32_t>(
                                               fixture::kChangedTriangles)
                                         : std::span<const std::uint32_t>(
                                               fixture::kOriginalTriangles)),
          "retained actual directed duplicate triangle loops");
  Require(
      std::ranges::equal(
          facts.FaceMap(),
          changed ? std::span<const geo::index_t>(fixture::kChangedFaceMap)
                  : std::span<const geo::index_t>(kIdentityFaces)) &&
          std::ranges::equal(
              facts.CornerMap(),
              changed
                  ? std::span<const geo::index_t>(fixture::kChangedCornerMap)
                  : std::span<const geo::index_t>(kIdentityCorners)) &&
          std::ranges::equal(
              facts.SourceFaces(),
              changed ? std::span<const geo::index_t>(kSelectedFaces)
                      : std::span<const geo::index_t>(kIdentityFaces)) &&
          std::ranges::equal(
              facts.SourceCorners(),
              changed ? std::span<const geo::index_t>(kSelectedCorners)
                      : std::span<const geo::index_t>(kIdentityCorners)) &&
          facts.Outcome() == (changed ? repair::CandidateOutcome::accepted
                                      : repair::CandidateOutcome::unchanged),
      "literal forward and inverse maps survive without a native report");
  const auto bytes = facts.NewRequestedBytes();
  Require(bytes.total == bytes.repair + bytes.source_inspection +
                             bytes.selection + bytes.candidate_inspection &&
              bytes.total > 0 && facts.Source().NewRequestedBytes() > 0,
          "Repair operation components partition its live total");
}
std::uint64_t RetainedBytes(const repair::MaterializedDuplicateRepair& facts) {
  return facts.Source().NewRequestedBytes() + facts.NewRequestedBytes().total;
}
void ReachedOutcomes(const repair::GltfWorkflowOutcomes& outcomes, Fault fault,
                     bool changed) {
  const bool canceled =
      fault == Fault::kVerifyStop || fault == Fault::kReportStop;
  Require(
      outcomes.candidate == (changed ? repair::CandidateOutcome::accepted
                                     : repair::CandidateOutcome::unchanged) &&
          outcomes.execution ==
              (canceled ? repair::GltfWorkflowExecutionOutcome::kCanceled
                        : repair::GltfWorkflowExecutionOutcome::kFailed) &&
          outcomes.exit ==
              (canceled ? repair::GltfWorkflowExitMeaning::kCanceled
                        : repair::GltfWorkflowExitMeaning::kExecutionFailure) &&
          outcomes.profile ==
              (fault == Fault::kVerifyStop
                   ? repair::GltfWorkflowProfileOutcome::kIncomplete
                   : repair::GltfWorkflowProfileOutcome::kPassed) &&
          outcomes.stage ==
              (fault == Fault::kVerifyStop ? Stage::kVerify : Stage::kReport),
      "actual execution/profile/exit preserve reached verification facts "
      "independently of report availability");
}
void Fallback(const repair::GltfDuplicateFileWorkflowResult& result,
              const Fixture& source, bool changed, std::size_t baseline,
              const Observers& observers) {
  Require(result.record && !result.record->Memory() &&
              result.record->MemoryOutcomes() && result.record->RepairFacts() &&
              result.record->ReportJson().empty() && !result.publication &&
              !result.record->Published(),
          "fallback is real completed Repair, not a record/report/publication");
  const auto& record = *result.record;
  const auto* facts = record.RepairFacts();
  Facts(*facts, changed);
  const auto bytes = record.NewRequestedBytes();
  Require(bytes.retained_repair && bytes.memory.total == 0 &&
              bytes.memory.workflow == 0 &&
              bytes.memory.materialization.bridge == 0 &&
              bytes.memory.read == 0 && bytes.memory.extraction == 0 &&
              bytes.memory.writing == 0 && bytes.memory.reload == 0 &&
              bytes.memory.verification == 0 && !bytes.publication_files,
          "released memory/Interchange owners have no fallback attribution");
  Require(bytes.retained_repair->source_geometry ==
                  facts->Source().NewRequestedBytes() &&
              bytes.retained_repair->repair.total ==
                  facts->NewRequestedBytes().total &&
              bytes.retained_repair->repair.repair ==
                  facts->NewRequestedBytes().repair &&
              bytes.retained_repair->repair.source_inspection ==
                  facts->NewRequestedBytes().source_inspection &&
              bytes.retained_repair->repair.selection ==
                  facts->NewRequestedBytes().selection &&
              bytes.retained_repair->repair.candidate_inspection ==
                  facts->NewRequestedBytes().candidate_inspection &&
              bytes.retained_repair->total == RetainedBytes(*facts) &&
              source.context.ActiveTrackedPayload() ==
                  baseline + bytes.wrapper + bytes.retained_repair->total &&
              observers.wrapper.live == bytes.wrapper &&
              observers.memory.live == 0,
          "physical ledger delta is wrapper plus disjoint source "
          "Geometry/Repair; candidate not counted twice");
  Require(record.MemoryOutcomes()->candidate == facts->Outcome() &&
              record.Outcomes().candidate == facts->Outcome() &&
              source.context.ActiveWorkers() == 0,
          "outer/inner reached candidate facts remain genuine and joined");
}
std::size_t BaselineLaunches(const ic::GltfFileAsset& source) {
  const auto count = [](std::size_t bytes) {
    const auto jobs = (bytes + kPartitionBytes - 1) / kPartitionBytes;
    return jobs < 2 ? std::size_t{0} : std::min(jobs, fixture::kWorkers);
  };
  auto launches = count(source.Asset().OriginalJson().size());
  for (const auto& r : source.Asset().Resources())
    if (r.origin == ic::GltfResourceOrigin::kCallerBinding)
      launches += count(r.bytes.size());
  return launches;
}
std::size_t Normal(Fixture& source, bool changed) {
  Observers observers(source.context);
  const auto baseline = source.context.ActiveTrackedPayload();
  std::size_t joins = 0;
  {
    auto request = source.Request(Targets(changed));
    request.report_failure_retention = Policy::kRetainCompletedRepair;
    const auto result = file::RunGltfDuplicateFileWorkflowForTest(
        request, fixture::Limits(), Work(), source.context, {},
        observers.Options());
    Require(result.status == Status::kSuccess && result.record &&
                result.record->Memory() &&
                result.record->RepairFacts() ==
                    &*result.record->Memory()->Repair() &&
                !result.record->NewRequestedBytes().retained_repair &&
                !result.record->ReportJson().empty(),
            "ordinary successful owner excludes fallback even with opt-in");
    Facts(*result.record->RepairFacts(), changed);
    Require(result.record->MemoryOutcomes()->candidate ==
                    result.outcomes.candidate &&
                source.context.ActiveTrackedPayload() ==
                    baseline + result.record->NewRequestedBytes().wrapper +
                        result.record->Memory()->NewRequestedBytes().total,
            "normal record retains original accounting exactly once");
    joins = observers.source.joined;
  }
  observers.Joined();
  observers.Empty();
  Require(source.context.ActiveTrackedPayload() == baseline,
          "normal owner release restores captured-source baseline");
  return joins;
}
void Faults() {
  std::size_t cases = 0;
  for (const auto format :
       {ic::GltfFileFormat::kGltfJson, ic::GltfFileFormat::kGlb}) {
    for (bool changed : {false, true}) {
      Fixture source(format, false, fixture::kJson, fixture::kLedgerBytes,
                     format == ic::GltfFileFormat::kGlb);
      const auto source_joins = Normal(source, changed);
      for (const auto fault : kFaults)
        for (bool retain : {false, true}) {
          Observers observers(source.context);
          auto limits = fixture::Limits();
          if (fault == Fault::kVerifyStop)
            observers.source.cancel_at = BaselineLaunches(*source.source) + 1;
          if (fault == Fault::kReportStop) observers.report.cancel_at = 1;
          if (fault == Fault::kReportExtent) limits.memory.max_report_bytes = 1;
          if (fault == Fault::kReportBudget)
            observers.memory.exhaust_after_joins = source_joins;
          auto request = source.Request(Targets(changed));
          if (retain)
            request.report_failure_retention = Policy::kRetainCompletedRepair;
          const auto baseline = source.context.ActiveTrackedPayload();
          {
            const auto result = file::RunGltfDuplicateFileWorkflowForTest(
                request, limits, Work(), source.context,
                observers.stop.get_token(), observers.Options());
            observers.memory.held_budget.Reset();
            Require(result.record && result.record->MemoryOutcomes(),
                    "actual file fault returned inner outcomes");
            ReachedOutcomes(*result.record->MemoryOutcomes(), fault, changed);
            Require(result.outcomes.execution ==
                            result.record->MemoryOutcomes()->execution &&
                        result.outcomes.profile ==
                            result.record->MemoryOutcomes()->profile &&
                        result.outcomes.exit ==
                            result.record->MemoryOutcomes()->exit &&
                        result.outcomes.stage ==
                            repair::GltfFileWorkflowStage::kMemory &&
                        result.outcomes.publication ==
                            repair::GltfFileWorkflowPublicationOutcome::
                                kNotRequested,
                    "outer failure keeps inner reached execution/profile/exit "
                    "and genuine file phase");
            const auto expected =
                fault == Fault::kReportExtent   ? Status::kLimitExceeded
                : fault == Fault::kReportBudget ? Status::kBudgetExceeded
                                                : Status::kCanceled;
            const auto expected_stage =
                fault == Fault::kVerifyStop ? Stage::kVerify : Stage::kReport;
            const auto expected_code =
                fault == Fault::kReportExtent   ? Code::kReportExtent
                : fault == Fault::kReportBudget ? Code::kRequestBudget
                                                : Code::kStopRequested;
            Require(result.status == expected && result.record &&
                        result.diagnostic.memory &&
                        result.diagnostic.memory->stage == expected_stage &&
                        result.diagnostic.memory->code == expected_code &&
                        !result.record->Memory() &&
                        result.record->ReportJson().empty(),
                    "actual stopped/extent/shared-ledger report fault "
                    "preserves stage/code and no receipt");
            Require(result.record->MemoryOutcomes()->stage == expected_stage &&
                        result.outcomes.candidate ==
                            (changed ? repair::CandidateOutcome::accepted
                                     : repair::CandidateOutcome::unchanged),
                    "late failure preserves genuine actual reached outcomes");
            if (retain)
              Fallback(result, source, changed, baseline, observers);
            else
              Require(
                  !result.record->RepairFacts() &&
                      !result.record->NewRequestedBytes().retained_repair &&
                      source.context.ActiveTrackedPayload() ==
                          baseline + result.record->NewRequestedBytes().wrapper,
                  "default policy releases completed payload on the original "
                  "late-failure paths");
            if (fault == Fault::kReportBudget)
              Require(
                  observers.memory.exhausted,
                  "budget fault actually held original shared-ledger headroom");
            observers.Joined();
            source.CheckSource();
            ++cases;
          }
          observers.Empty();
          Require(
              source.context.ActiveTrackedPayload() == baseline,
              "last failed file record releases wrapper and fallback charges");
        }
    }
  }
  std::cout << "retained file faults " << cases << " passed\n";
}
repair::GltfDuplicateWorkflowLimits MemoryLimits() {
  const auto file_limits = fixture::Limits().memory;
  repair::GltfDuplicateWorkflowLimits limits;
  limits.max_new_requested_bytes = file_limits.max_new_requested_bytes;
  limits.max_input_id_bytes = file_limits.max_input_id_bytes;
  limits.max_reload_bindings = file_limits.max_reload_bindings;
  limits.max_report_bytes = file_limits.max_report_bytes;
  limits.read = fixture::ReadLimits();
  limits.extraction = file_limits.extraction;
  limits.materialization = file_limits.materialization;
  limits.repair = file_limits.repair;
  limits.writing = file_limits.writing;
  limits.reload = file_limits.reload;
  limits.reload_extraction = file_limits.reload_extraction;
  limits.verification = file_limits.verification;
  return limits;
}
std::vector<ic::SuppliedGltfResource> Resources(const Fixture& source) {
  std::vector<ic::SuppliedGltfResource> resources;
  for (const auto& resource : source.source->Asset().Resources())
    if (resource.origin == ic::GltfResourceOrigin::kCallerBinding)
      resources.push_back(
          {resource.kind, resource.table_index, resource.bytes});
  return resources;
}
void DirectMemory() {
  std::size_t cases = 0;
  std::optional<repair::MaterializedDuplicateRepair> retained;
  std::optional<geo::ExecutionContext> ledger;
  for (bool changed : {false, true}) {
    Fixture source;
    const auto resources = Resources(source);
    repair::GltfDuplicateWorkflowRequest request{
        source.source->Asset().OriginalJson(),
        resources,
        fixture::kInputId,
        0,
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRaw},
        Targets(changed),
        {}};
    const auto baseline = source.context.ActiveTrackedPayload();
    std::size_t source_joins = 0;
    {
      Observers observer(source.context);
      request.report_failure_retention = Policy::kRetainCompletedRepair;
      const auto normal = memory::RunGltfDuplicateWorkflowForTest(
          request, MemoryLimits(), Work().memory, source.context, {},
          observer.Options().memory);
      Require(normal.status == Status::kSuccess && normal.record &&
                  normal.record->Repair() && !normal.retained_repair,
              "direct memory ordinary/fallback paths are mutually exclusive");
      Facts(*normal.record->Repair(), changed);
      source_joins = observer.source.joined;
    }
    for (const auto fault : kFaults)
      for (bool retain : {false, true}) {
        Observers observer(source.context);
        auto limits = MemoryLimits();
        if (fault == Fault::kVerifyStop) observer.source.cancel_at = 1;
        if (fault == Fault::kReportStop) observer.report.cancel_at = 1;
        if (fault == Fault::kReportExtent) limits.max_report_bytes = 1;
        if (fault == Fault::kReportBudget)
          observer.memory.exhaust_after_joins = source_joins;
        request.report_failure_retention =
            retain ? Policy::kRetainCompletedRepair : Policy::kRelease;
        {
          const auto result = memory::RunGltfDuplicateWorkflowForTest(
              request, limits, Work().memory, source.context,
              observer.stop.get_token(), observer.Options().memory);
          observer.memory.held_budget.Reset();
          ReachedOutcomes(result.outcomes, fault, changed);
          Require(
              !result.record &&
                  result.diagnostic.stage == (fault == Fault::kVerifyStop
                                                  ? Stage::kVerify
                                                  : Stage::kReport) &&
                  result.status ==
                      (fault == Fault::kReportExtent   ? Status::kLimitExceeded
                       : fault == Fault::kReportBudget ? Status::kBudgetExceeded
                                                       : Status::kCanceled),
              "direct memory late failure remains recordless with actual "
              "native stage/status");
          Require(result.retained_repair.has_value() == retain &&
                      result.outcomes.candidate ==
                          (changed ? repair::CandidateOutcome::accepted
                                   : repair::CandidateOutcome::unchanged),
                  "direct opt-in facts are actual completed Repair, default "
                  "remains released");
          if (retain) {
            Facts(*result.retained_repair, changed);
            Require(source.context.ActiveTrackedPayload() ==
                        baseline + RetainedBytes(*result.retained_repair),
                    "standalone fallback owns exactly source Geometry and "
                    "disjoint Repair bytes");
          } else
            Require(source.context.ActiveTrackedPayload() == baseline,
                    "direct default retains zero fresh payload after no-report "
                    "failure");
          observer.Joined();
          observer.Empty();
          ++cases;
        }
        Require(source.context.ActiveTrackedPayload() == baseline,
                "direct last fallback copy releases its original requests");
      }
    Observers observer(source.context);
    auto limits = MemoryLimits();
    limits.max_report_bytes = 1;
    request.report_failure_retention = Policy::kRetainCompletedRepair;
    const auto last = memory::RunGltfDuplicateWorkflowForTest(
        request, limits, Work().memory, source.context, {},
        observer.Options().memory);
    Require(last.retained_repair.has_value(),
            "standalone lifetime fallback exists");
    observer.Empty();
    if (changed) {
      retained = *last.retained_repair;
      ledger = retained->Source().ShareExecutionContext();
    }
  }
  Facts(*retained, true);
  Require(ledger->ActiveTrackedPayload() == RetainedBytes(*retained),
          "standalone owner needs neither file source, caller bindings nor "
          "context");
  retained.reset();
  Require(ledger->ActiveTrackedPayload() == 0,
          "standalone final release drops every mesh/Repair charge");
  std::cout << "retained direct memory faults " << cases << " passed\n";
}
void MultipleResultsAndStateBoundary() {
  Fixture source;
  const auto baseline = source.context.ActiveTrackedPayload();
  auto request = source.Request();
  request.report_failure_retention = Policy::kRetainCompletedRepair;
  auto limits = fixture::Limits();
  limits.memory.max_report_bytes = 1;
  std::optional<repair::GltfDuplicateFileWorkflowRecord> first, second;
  {
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        request, limits, fixture::Work(true), source.context);
    Require(result.record && result.record->RepairFacts(),
            "first completed failed result exists");
    first = *result.record;
  }
  const auto first_bytes = first->NewRequestedBytes();
  {
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        request, limits, fixture::Work(true), source.context);
    Require(result.record && result.record->RepairFacts(),
            "second completed failed result exists");
    second = *result.record;
  }
  const auto second_bytes = second->NewRequestedBytes();
  const auto first_total =
      first_bytes.wrapper + first_bytes.retained_repair->total;
  const auto second_total =
      second_bytes.wrapper + second_bytes.retained_repair->total;
  Require(
      source.context.ActiveTrackedPayload() ==
          baseline + first_total + second_total,
      "separate failed results consume independent exact live ledger deltas");
  const auto current = source.context.ActiveTrackedPayload();
  auto held = geo::TryReservePayload(
      source.context, source.context.TrackedPayloadBudget() - current);
  Require(held.status == geo::ReservationStatus::kAccepted,
          "hold actual remaining headroom around retained failed owners");
  const auto refused = repair::RunGltfDuplicateFileWorkflow(
      request, limits, fixture::Work(true), source.context);
  Require(refused.status == Status::kBudgetExceeded && !refused.record,
          "next real workflow admission refuses when retained owners consume "
          "headroom");
  held.lease.Reset();
  first.reset();
  Require(
      source.context.ActiveTrackedPayload() == baseline + second_total,
      "dropping one failed result does not release a separate failed result");
  Facts(*second->RepairFacts(), true);
  second.reset();
  Require(source.context.ActiveTrackedPayload() == baseline,
          "all failed results released to captured-source baseline");
  // No-publication report-extent results retain only the two wrapper owners.
  // Their measured admitted bytes include the enlarged inline file state.
  for (bool below : {false, true}) {
    limits.max_wrapper_new_requested_bytes =
        first_bytes.wrapper - (below ? 1 : 0);
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        request, limits, fixture::Work(true), source.context);
    Require(result.status == (below ? Status::kBudgetExceeded
                                    : Status::kLimitExceeded) &&
                (below ? !result.record
                       : result.record && result.record->RepairFacts()),
            "enlarged wrapper state has an inclusive exact requested-byte "
            "admission boundary");
  }
  Require(source.context.ActiveTrackedPayload() == baseline,
          "state boundary controls release all new bytes");
}
void PostRepairFailureAndMissingReport() {
  Fixture source;
  auto request = source.Request();
  request.report_failure_retention = Policy::kRetainCompletedRepair;
  auto limits = fixture::Limits();
  limits.memory.writing.output.max_json_bytes = 1;
  const auto baseline = source.context.ActiveTrackedPayload();
  {
    const auto reported = repair::RunGltfDuplicateFileWorkflow(
        request, limits, fixture::Work(true), source.context);
    Require(reported.status == Status::kDelegatedFailure && reported.record &&
                reported.record->Memory() && reported.record->RepairFacts() &&
                !reported.record->NewRequestedBytes().retained_repair &&
                reported.diagnostic.memory &&
                reported.diagnostic.memory->stage == Stage::kWrite &&
                reported.diagnostic.memory->code == Code::kWrite &&
                std::get<ic::GltfTopologyWriteStatus>(
                    reported.diagnostic.memory->delegated_status) ==
                    ic::GltfTopologyWriteStatus::kLimitExceeded,
            "real post-Repair writer limit fails with genuine available native "
            "receipt");
    Facts(*reported.record->RepairFacts(), true);
  }
  limits.memory.max_report_bytes = 1;
  {
    Observers observers(source.context);
    const auto missing = file::RunGltfDuplicateFileWorkflowForTest(
        request, limits, fixture::Work(true), source.context, {},
        observers.Options());
    Require(missing.status == Status::kLimitExceeded && missing.record &&
                !missing.record->Memory() && missing.diagnostic.memory &&
                missing.diagnostic.memory->stage == Stage::kReport &&
                missing.diagnostic.memory->code == Code::kReportExtent &&
                std::get<ic::GltfTopologyWriteStatus>(
                    missing.diagnostic.memory->delegated_status) ==
                    ic::GltfTopologyWriteStatus::kLimitExceeded &&
                missing.record->MemoryOutcomes()->profile ==
                    repair::GltfWorkflowProfileOutcome::kIncomplete,
            "missing report exposes real report-extent diagnostic plus actual "
            "retained writer status, no inferred write-stage outcome");
    Fallback(missing, source, true, baseline, observers);
  }
  Require(source.context.ActiveTrackedPayload() == baseline,
          "combined post-Repair/report failure releases every fresh owner");
}
void AdmissionAndRejection() {
  Fixture source;
  for (bool invalid_policy : {false, true}) {
    Observers observers(source.context);
    auto request = source.Request(
        invalid_policy
            ? Targets(true)
            : std::span<const repair::DuplicateTarget>(kInvalidTargets));
    request.report_failure_retention = invalid_policy
                                           ? static_cast<Policy>(kUnknownPolicy)
                                           : Policy::kRetainCompletedRepair;
    const auto baseline = source.context.ActiveTrackedPayload();
    {
      const auto result = file::RunGltfDuplicateFileWorkflowForTest(
          request, fixture::Limits(), Work(), source.context, {},
          observers.Options());
      Require(result.status == (invalid_policy ? Status::kInvalidConfiguration
                                               : Status::kRejected),
              "unknown policy/rejected target retains actual native status");
      if (invalid_policy)
        Require(!result.record && observers.wrapper.requests == 0 &&
                    observers.memory.requests == 0,
                "unknown enum rejected before every original request");
      else
        Require(result.record && !result.record->RepairFacts() &&
                    !result.record->NewRequestedBytes().retained_repair,
                "rejected Repair creates no completed fallback");
    }
    observers.Joined();
    observers.Empty();
    Require(source.context.ActiveTrackedPayload() == baseline,
            "admission/rejection releases all fresh charges");
  }
  {
    const auto resources = Resources(source);
    repair::GltfDuplicateWorkflowRequest request{
        source.source->Asset().OriginalJson(),
        resources,
        fixture::kInputId,
        0,
        {ic::GltfTriangleRequest::kRequireTriangles,
         ic::GltfValueRequest::kRaw},
        Targets(true),
        {},
        static_cast<Policy>(kUnknownPolicy)};
    Observers direct(source.context);
    const auto baseline = source.context.ActiveTrackedPayload();
    const auto invalid = memory::RunGltfDuplicateWorkflowForTest(
        request, MemoryLimits(), Work().memory, source.context, {},
        direct.Options().memory);
    Require(invalid.status == Status::kInvalidConfiguration &&
                invalid.diagnostic.stage == Stage::kConfiguration &&
                invalid.diagnostic.code == Code::kConfigurationExtent &&
                !invalid.record && !invalid.retained_repair &&
                direct.memory.requests == 0 &&
                source.context.ActiveTrackedPayload() == baseline,
            "direct supplied-memory unknown policy fails before allocation or "
            "processing");
    direct.Joined();
    direct.Empty();
  }
  Observers observers(source.context);
  observers.stop.request_stop();
  auto request = source.Request();
  request.report_failure_retention = Policy::kRetainCompletedRepair;
  const auto canceled = file::RunGltfDuplicateFileWorkflowForTest(
      request, fixture::Limits(), Work(), source.context,
      observers.stop.get_token(), observers.Options());
  Require(canceled.status == Status::kCanceled && !canceled.record &&
              observers.wrapper.requests == 0,
          "pre-repair stop does not fabricate facts");
  observers.Empty();
}
void CopyLifetimeAndHeadroom() {
  Allocations backend;
  std::optional<repair::GltfDuplicateFileWorkflowRecord> retained, copy;
  std::optional<geo::ExecutionContext> ledger;
  std::uint64_t baseline = 0, fallback = 0, wrapper = 0;
  {
    Fixture source(ic::GltfFileFormat::kGlb, false, fixture::kJson,
                   fixture::kLedgerBytes, true);
    auto limits = fixture::Limits();
    limits.memory.max_report_bytes = 1;
    auto request = source.Request();
    request.report_failure_retention = Policy::kRetainCompletedRepair;
    file::FileWorkflowTestOptions options;
    options.allocation_backend = backend.Backend();
    const auto result = file::RunGltfDuplicateFileWorkflowForTest(
        request, limits, fixture::Work(true), source.context, {}, options);
    Require(result.record && result.record->RepairFacts(),
            "lifetime fixture reached completed fallback");
    retained = *result.record;
    ledger = retained->RepairFacts()->Source().ShareExecutionContext();
    baseline = source.context.ActiveTrackedPayload() -
               retained->NewRequestedBytes().wrapper -
               retained->NewRequestedBytes().retained_repair->total;
    fallback = retained->NewRequestedBytes().retained_repair->total;
    wrapper = retained->NewRequestedBytes().wrapper;
  }
  // Every caller asset/request/options/context and physical fixture is gone.
  Facts(*retained->RepairFacts(), true);
  Require(
      ledger->ActiveTrackedPayload() == baseline + fallback + wrapper,
      "retained source/Repair ledger survives original context destruction");
  backend.frozen = true;
  const auto calls = backend.requests;
  copy = *retained;
  retained.reset();
  Require(
      backend.requests == calls && backend.refused == 0 && copy->RepairFacts(),
      "record copy/reassignment/final intermediate release allocate no "
      "original block");
  Facts(*copy->RepairFacts(), true);
  const auto current = ledger->ActiveTrackedPayload();
  auto accepted =
      geo::TryReservePayload(*ledger, ledger->TrackedPayloadBudget() - current);
  Require(accepted.status == geo::ReservationStatus::kAccepted,
          "live owners leave exact remaining headroom");
  auto refused = geo::TryReservePayload(*ledger, 1);
  Require(refused.status == geo::ReservationStatus::kBudgetExceeded,
          "retained charges participate in subsequent shared-ledger refusal");
  accepted.lease.Reset();
  copy.reset();
  backend.Empty();
  Require(ledger->ActiveTrackedPayload() == 0,
          "last record releases original source, wrapper and fallback exactly "
          "once");
  auto restored =
      geo::TryReservePayload(*ledger, ledger->TrackedPayloadBudget());
  Require(restored.status == geo::ReservationStatus::kAccepted,
          "final release restores full later-operation headroom");
}
void UnexpectedExceptionsJoin() {
  Fixture source;
  Observers observers(source.context);
  observers.report.fail_at = 2;
  auto request = source.Request();
  request.report_failure_retention = Policy::kRetainCompletedRepair;
  const auto baseline = source.context.ActiveTrackedPayload();
  bool threw = false;
  try {
    (void)file::RunGltfDuplicateFileWorkflowForTest(request, fixture::Limits(),
                                                    Work(), source.context, {},
                                                    observers.Options());
  } catch (const LaunchFailure&) {
    threw = true;
  }
  Require(threw && observers.report.launched == 1 &&
              observers.report.joined == 1 &&
              source.context.ActiveWorkers() == 0 &&
              source.context.ActiveTrackedPayload() == baseline,
          "unexpected partial native launch remains joined exception, not "
          "fallback result");
  observers.Joined();
  observers.Empty();
}
void TranslationResourceAndStop() {
  namespace moving = repair::test_translation_workflow;
  constexpr std::size_t kActualLedgerBytes = 2 * moving::kMiB;
  constexpr std::size_t kRemainingBytes = 1;
  constexpr std::byte kOwnedValue{0x5a};
  using OwnedBytes = std::vector<std::byte, own::Allocator<std::byte>>;
  const auto occupy = [&](const geo::ExecutionContext& context) {
    const auto before = context.ActiveTrackedPayload();
    const auto available = context.TrackedPayloadBudget() - before;
    const auto prefix = own::AllocationRequestBytes(1, alignof(std::byte)) - 1;
    Require(available > prefix + kRemainingBytes,
            "real payload construction headroom");
    const auto count = available - prefix - kRemainingBytes;
    const auto charge = own::AllocationRequestBytes(count, alignof(std::byte));
    const own::AllocationContext allocation(context, {});
    OwnedBytes owner(count, kOwnedValue, own::Allocator<std::byte>(allocation));
    Require(
        owner.size() == count && owner.capacity() == count &&
            owner.front() == kOwnedValue && owner.back() == kOwnedValue &&
            owner.get_allocator().Context().SharesBudget(allocation) &&
            context.ActiveTrackedPayload() - before == charge &&
            context.TrackedPayloadBudget() - context.ActiveTrackedPayload() ==
                kRemainingBytes,
        "actual owned storage and shared ledger, not a synthetic reservation");
    return owner;
  };
  geo::ExecutionContext context({moving::kWorkers, 1, kActualLedgerBytes});
  moving::MovingFixture data;
  const auto resources = data.Resources();
  const auto original_bytes = data.binary;
  const auto original_json = data.json;
  const auto request = moving::Request(data, resources);
  const auto run = [&](const auto& limits) {
    return repair::RunGltfDuplicateWorkflow(request, limits, moving::Work(),
                                            context);
  };
  {
    const auto accepted = run(moving::Limits());
    Require(
        accepted.record &&
            accepted.status == repair::GltfWorkflowStatus::kSuccess,
        "unchanged positive operation caps admit real small-ledger workflow");
    moving::CheckFacts(accepted.translation, false);
  }
  Require(context.ActiveTrackedPayload() == 0,
          "positive workflow owners released");
  {
    auto capped = moving::Limits();
    capped.max_new_requested_bytes = 1;
    const auto limited = run(capped);
    Require(limited.status == repair::GltfWorkflowStatus::kBudgetExceeded &&
                !limited.record && !limited.translation.source &&
                !limited.translation.writing && !limited.translation.reload &&
                !limited.translation.verification &&
                !limited.translation.publication_reload &&
                !limited.translation.publication_verification,
            "own operation-cap refusal without fake helper work");
  }
  {
    geo::ExecutionContext foreign({moving::kWorkers, 1, kActualLedgerBytes});
    const auto owner = occupy(foreign);
    const auto accepted = run(moving::Limits());
    Require(accepted.status == repair::GltfWorkflowStatus::kSuccess &&
                !owner.empty(),
            "foreign actual owned payload cannot occupy workflow ledger");
  }
  Require(context.ActiveTrackedPayload() == 0,
          "foreign control releases only real workflow charges");
  {
    const auto owner = occupy(context);
    const auto occupied = context.ActiveTrackedPayload();
    const auto refused = run(moving::Limits());
    Require(refused.status == repair::GltfWorkflowStatus::kBudgetExceeded &&
                !refused.record && !refused.translation.source &&
                !refused.retained_repair && !owner.empty() &&
                context.ActiveTrackedPayload() == occupied &&
                context.ActiveWorkers() == 0,
            "actual shared-ledger seed refusal differs from operation cap with "
            "unchanged configuration");
  }
  Require(context.ActiveTrackedPayload() == 0,
          "last real payload storage free releases charge");
  {
    const auto retry = run(moving::Limits());
    Require(
        retry.status == repair::GltfWorkflowStatus::kSuccess && retry.record,
        "same positive operation caps succeed after pressure release");
    moving::CheckFacts(retry.translation, false);
    moving::CheckRepair(*retry.record->Repair(), true);
  }
  {
    constexpr std::size_t kSampleKeys = 4096, kPositionAndIndexBytes = 60;
    constexpr std::uint64_t kKeyPartition = 1024;
    constexpr std::size_t kOriginalUvOffset = 116, kTranslationWidth = 3;
    constexpr std::uint64_t kSerialThreshold =
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    moving::MovingFixture large;
    std::vector<std::byte> expanded(
        large.binary.begin(), large.binary.begin() + kPositionAndIndexBytes);
    for (std::size_t key = 0; key < kSampleKeys; ++key)
      moving::Word(expanded,
                   std::bit_cast<std::uint32_t>(static_cast<float>(key)));
    for (std::size_t axis : {std::size_t{0}, std::size_t{1}})
      for (std::size_t key = 0; key < kSampleKeys; ++key)
        for (std::size_t scalar = 0; scalar < kTranslationWidth; ++scalar)
          moving::Word(expanded, scalar == axis ? std::bit_cast<std::uint32_t>(
                                                      static_cast<float>(key))
                                                : 0);
    expanded.insert(expanded.end(), large.binary.begin() + kOriginalUvOffset,
                    large.binary.end());
    large.binary = std::move(expanded);
    const auto replace = [&](std::string_view old, std::string_view value) {
      const auto at = large.json.find(old);
      Require(at != std::string::npos, "large original literal site");
      large.json.replace(at, old.size(), value);
    };
    replace(R"("byteLength":148)", R"("byteLength":114780)");
    replace(R"("byteOffset":60,"byteLength":8)",
            R"("byteOffset":60,"byteLength":16384)");
    replace(R"("byteOffset":68,"byteLength":24)",
            R"("byteOffset":16444,"byteLength":49152)");
    replace(R"("byteOffset":92,"byteLength":24)",
            R"("byteOffset":65596,"byteLength":49152)");
    replace(R"("byteOffset":116,"byteLength":32)",
            R"("byteOffset":114748,"byteLength":32)");
    for (std::size_t accessor = 0; accessor < kTranslationWidth; ++accessor)
      replace(R"("count":2)", R"("count":4096)");
    replace(R"("type":"SCALAR","min":[0],"max":[1])",
            R"("type":"SCALAR","min":[0],"max":[4095])");
    const auto supplied = large.Resources();
    auto limits = moving::Limits();
    for (auto* cap : {&limits.extraction.direct_translation,
                      &limits.writing.direct_translation,
                      &limits.reload_extraction.direct_translation,
                      &limits.verification.direct_translation})
      cap->max_total_keys = 2 * kSampleKeys;
    auto work = moving::Work();
    work.interchange.parallel_min_resource_bytes = kSerialThreshold;
    work.interchange.parallel_min_channel_components = kSerialThreshold;
    work.geometry.parallel_min_bytes = work.writing.parallel_min_bytes =
        work.verification.parallel_min_bytes = work.report.parallel_min_bytes =
            kSerialThreshold;
    work.interchange.direct_translation.partition_keys = kKeyPartition;
    work.writing.direct_translation.partition_keys = kKeyPartition;
    work.verification.direct_translation.partition_keys = kKeyPartition;
    std::atomic<bool> done{false}, observed{false};
    std::stop_source stop;
    // Observe the real public shared worker lease. Other numerical/byte work is
    // serial here; this is a caller-owned witness thread, not a product pool.
    std::jthread witness([&](std::stop_token observer_stop) {
      while (!observer_stop.stop_requested() && !done.load() &&
             context.ActiveWorkers() == 0)
        std::this_thread::yield();
      if (!observer_stop.stop_requested() && !done.load()) {
        observed = true;
        stop.request_stop();
      }
    });
    const auto canceled = repair::RunGltfDuplicateWorkflow(
        moving::Request(large, supplied), limits, work, context,
        stop.get_token());
    done = true;
    witness.request_stop();
    witness.join();
    Require(observed &&
                canceled.status == repair::GltfWorkflowStatus::kCanceled &&
                !canceled.record &&
                canceled.outcomes.exit ==
                    repair::GltfWorkflowExitMeaning::kCanceled &&
                context.ActiveWorkers() == 0,
            "real validation worker admission triggers actual stop, joined "
            "result and no required JSON");
    Require(canceled.translation.source.has_value(),
            "validator was actually entered before worker admission");
    for (const auto* value :
         {&canceled.translation.source, &canceled.translation.writing,
          &canceled.translation.reload, &canceled.translation.verification})
      if (*value)
        Require((*value)->keys_checked <= 2 * kSampleKeys &&
                    (*value)->execution.launched_workers ==
                        (*value)->execution.joined_workers,
                "actual interrupted/completed work is bounded, retained and "
                "joined, not a fabricated prefix count");
  }
  {
    Observers observers(context);
    observers.report.cancel_at = 1;
    const auto stopped = memory::RunGltfDuplicateWorkflowForTest(
        request, moving::Limits(), moving::Work(), context,
        observers.stop.get_token(), observers.Options().memory);
    moving::CheckFacts(stopped.translation, false);
    Require(stopped.status == repair::GltfWorkflowStatus::kCanceled &&
                !stopped.record && stopped.retained_repair &&
                stopped.execution.report_copy.launched_workers > 0 &&
                stopped.execution.report_copy.launched_workers ==
                    stopped.execution.report_copy.joined_workers,
            "real report-worker stop keeps completed nonprefix-capable "
            "validator facts, joins and no invented JSON");
    moving::CheckRepair(*stopped.retained_repair, true);
    observers.Joined();
    observers.Empty();
  }
  Require(data.binary == original_bytes && data.json == original_json &&
              context.ActiveTrackedPayload() == 0 &&
              context.ActiveWorkers() == 0,
          "all resource/stop owners, workers and caller source preserved");
}

static_assert(
    std::is_nothrow_copy_constructible_v<repair::MaterializedDuplicateRepair>);
static_assert(
    std::is_nothrow_copy_assignable_v<repair::MaterializedDuplicateRepair>);
static_assert(
    std::is_nothrow_destructible_v<repair::MaterializedDuplicateRepair>);
static_assert(
    std::is_nothrow_move_constructible_v<repair::GltfDuplicateWorkflowResult>);
static_assert(
    std::is_nothrow_move_assignable_v<repair::GltfDuplicateWorkflowResult>);
static_assert(std::is_nothrow_copy_constructible_v<
              repair::GltfDuplicateFileWorkflowRecord>);
static_assert(
    std::is_nothrow_copy_assignable_v<repair::GltfDuplicateFileWorkflowRecord>);
}  // namespace
int main() {
  try {
    Faults();
    DirectMemory();
    MultipleResultsAndStateBoundary();
    PostRepairFailureAndMissingReport();
    AdmissionAndRejection();
    CopyLifetimeAndHeadroom();
    UnexpectedExceptionsJoin();
    TranslationResourceAndStop();
    std::cout << "independent retained Repair facts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "unexpected retained-facts test exception\n";
    return 1;
  }
}
