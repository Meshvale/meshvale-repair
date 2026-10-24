// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_workflow.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"
#include "off_workflow_fixture.h"
#include "off_workflow_test_internal.h"

namespace {
namespace rep = meshvale::repair;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace detail = rep::off_workflow_detail;
namespace own = rep::materialized_duplicates_detail;
namespace f = rep::test_off_workflow;
namespace fs = std::filesystem;
using f::Require;
using Stage = rep::OffWorkflowStage;
using Status = rep::OffWorkflowStatus;
using Code = rep::OffWorkflowDiagnosticCode;
struct RuntimeMarker {};
struct Backend {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
  };
  std::array<Block, 512> blocks{};
  own::AllocationBackend original = own::DefaultAllocationBackend();
  std::mutex mutex;
  std::atomic<bool> frozen{false};
  std::size_t calls = 0, live = 0, fail_at = 0;
  bool runtime = false, bad_free = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<Backend*>(opaque);
    std::lock_guard guard(self.mutex);
    ++self.calls;
    if (self.frozen || self.calls == self.fail_at) {
      if (self.runtime) throw RuntimeMarker{};
      throw std::bad_alloc{};
    }
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks)
      if (!block.pointer) {
        block = {pointer, bytes};
        self.live += bytes;
        return pointer;
      }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::bad_alloc{};
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* opaque) noexcept {
    auto& self = *static_cast<Backend*>(opaque);
    std::lock_guard guard(self.mutex);
    bool found = false;
    for (auto& block : self.blocks)
      if (block.pointer == pointer) {
        if (block.bytes != bytes) self.bad_free = true;
        self.live -= block.bytes;
        block = {};
        found = true;
        break;
      }
    if (!found) self.bad_free = true;
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
  }
  own::AllocationBackend Options() { return {Allocate, Free, this}; }
  void Empty() const {
    Require(live == 0 && !bad_free,
            "original wrapper backend has exact final release");
  }
};
struct Workers {
  struct Entry {
    own::NativeWorkerRecord original;
    Workers* owner = nullptr;
  };
  std::array<Entry, 256> entries{};
  own::NativeWorkerBackend original = own::DefaultNativeWorkerBackend();
  std::size_t attempts = 0, launched = 0, joined = 0, fail_at = 0;
  std::atomic<std::size_t> entered{0};
  static void Enter(own::NativeWorkerRecord& record) noexcept {
    auto& entry = *static_cast<Entry*>(record.state);
    ++entry.owner->entered;
    entry.original.entry(entry.original);
  }
  static std::uintptr_t Launch(own::NativeWorkerRecord* record, void* opaque) {
    auto& self = *static_cast<Workers*>(opaque);
    ++self.attempts;
    if (self.attempts == self.fail_at) throw RuntimeMarker{};
    Require(self.attempts <= self.entries.size(),
            "bounded independent native worker records");
    auto& entry = self.entries[self.attempts - 1];
    entry = {*record, &self};
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
  own::NativeWorkerBackend Options() { return {Launch, Join, this}; }
  void Joined() const {
    Require(launched == joined && entered == launched,
            "each genuine launched worker entered and joined, including throw "
            "paths");
  }
};
enum class PublicationAction {
  kNone,
  kRace,
  kEntryCorruption,
  kExtra,
  kThrow,
  kStop,
};
struct Probe {
  Backend backend;
  Workers source, verification, report;
  std::stop_source stop;
  Stage stop_stage = Stage::kComplete;
  std::uint64_t stop_at = std::numeric_limits<std::uint64_t>::max();
  std::optional<detail::VerificationPart> stop_part;
  std::uint64_t part_stop_at = 0;
  std::optional<std::uint64_t> witnessed_part_element;
  std::atomic<bool> stopped{false};
  std::size_t repair_calls = 0, prepared_calls = 0, published_calls = 0;
  bool stop_after_repair = false, late_stop = false, throw_after_repair = false;
  const geo::ExecutionContext* context = nullptr;
  bool occupy_after_repair = false;
  geo::PayloadLease occupied;
  fs::path root, destination, observed_stage;
  PublicationAction action = PublicationAction::kNone;
  std::vector<std::byte> prepared;
  static void Checkpoint(Stage stage, std::uint64_t ordinal, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    if (stage == self.stop_stage && ordinal >= self.stop_at &&
        !self.stopped.exchange(true))
      self.stop.request_stop();
  }
  static void After(const rep::MaterializedDuplicateResult& actual,
                    void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    ++self.repair_calls;
    if (actual.repair) {
      if (self.stop_after_repair) self.stop.request_stop();
      if (self.throw_after_repair) throw RuntimeMarker{};
      if (self.occupy_after_repair) {
        Require(self.context != nullptr,
                "occupied ledger fixture owns context");
        auto lease = geo::TryReservePayload(
            *self.context, self.context->TrackedPayloadBudget() -
                               self.context->ActiveTrackedPayload());
        Require(lease.status == geo::ReservationStatus::kAccepted,
                "real postengine remaining payload reservation");
        self.occupied = std::move(lease.lease);
      }
    }
  }
  static void Verification(detail::VerificationPart part, std::uint64_t element,
                           void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    if (self.stop_part == part && element >= self.part_stop_at &&
        !self.stopped.exchange(true)) {
      self.witnessed_part_element = element;
      self.stop.request_stop();
    }
  }
  static void Prepared(std::span<const std::byte> bytes, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    ++self.prepared_calls;
    self.prepared.assign(bytes.begin(), bytes.end());
    if (self.action == PublicationAction::kRace) {
      fs::create_directory(self.destination);
      f::Write(self.destination / "sentinel", "preserve");
    } else if (self.action == PublicationAction::kStop)
      self.stop.request_stop();
    else if (self.action == PublicationAction::kThrow)
      throw RuntimeMarker{};
    else if (self.action != PublicationAction::kNone) {
      std::optional<fs::path> stage;
      for (const auto& item : fs::directory_iterator(self.root))
        if (item.is_directory() && item.path() != self.destination) {
          Require(!stage, "fixture finds exactly one actual owned stage");
          stage = item.path();
        }
      Require(stage.has_value(), "provider observes actual staged directory");
      self.observed_stage = *stage;
      if (self.action == PublicationAction::kEntryCorruption) {
        auto physical = f::Read(*stage / f::kEntry);
        Require(!physical.empty(), "actual staged bytes exist");
        physical[0] ^= std::byte{1};
        f::Write(*stage / f::kEntry, physical);
      } else if (self.action == PublicationAction::kExtra)
        f::Write(*stage / "foreign", "new extra");
    }
  }
  static void Published(const ic::OffFilePublicationResult& actual,
                        void* opaque) noexcept {
    auto& self = *static_cast<Probe*>(opaque);
    if (actual.outcome == ic::OffFilePublicationOutcome::kPublished)
      ++self.published_calls;
    if (self.late_stop) {
      self.stop.request_stop();
      self.backend.frozen = true;
    }
  }
  detail::WorkflowTestOptions Options() {
    detail::WorkflowTestOptions options;
    options.allocation_backend = backend.Options();
    options.source_worker_backend = source.Options();
    options.verification_worker_backend = verification.Options();
    options.report_worker_backend = report.Options();
    options.checkpoint = Checkpoint;
    options.after_repair = After;
    options.prepared_report = Prepared;
    options.published = Published;
    options.state = this;
    options.verification_checkpoint = Verification;
    return options;
  }
  void Joined() const {
    source.Joined();
    verification.Joined();
    report.Joined();
  }
};
struct Fixture {
  f::Root root;
  fs::path input = root.Path() / f::kEntry;
  fs::path destination = root.Path() / "published";
  geo::ExecutionContext context{geo::ExecutionOptions{4, 1, f::kLedger}};
  std::optional<ic::OffFileAsset> source;
  explicit Fixture(bool empty = false, bool large = false,
                   std::string_view custom = {}) {
    std::string text;
    if (large)
      for (std::size_t i = 0; i < 4096; ++i)
        text += "# real transfer padding for source baseline\n";
    text += custom.empty() ? (empty ? f::kEmpty : f::kSource) : custom;
    f::Write(input, text);
    auto captured = f::Capture(input, context);
    Require(captured.asset.has_value(), "actual control fixture capture");
    source.emplace(*captured.asset);
  }
  rep::OffDuplicateFileWorkflowRequest Request(bool changed,
                                               bool publish = false) const {
    return f::Request(*source, changed, publish ? &destination : nullptr);
  }
};
void Retained(const rep::OffDuplicateFileWorkflowResult& result, bool changed,
              bool empty) {
  Require(
      result.record && result.record->RepairFacts(),
      "completed genuine Repair always retained after later expected failure");
  f::CheckRepair(*result.record->RepairFacts(), changed, empty);
  f::Joined(result.execution);
}
void Accounting(const rep::OffDuplicateFileWorkflowRecord& record,
                const geo::ExecutionContext& context, const Backend& backend,
                std::size_t occupied = 0) {
  const auto bytes = record.NewRequestedBytes();
  Require(bytes.wrapper == backend.live && !backend.bad_free,
          "actual physical wrapper requests equal attributed live scope");
  Require(bytes.source_files == record.Source().NewRequestedBytes() &&
              bytes.source_off.total ==
                  record.Source().Document().NewRequestedBytes().total,
          "source counted once in actual file and OFF scopes");
  if (record.RepairFacts())
    Require(
        bytes.repair.total == record.RepairFacts()->NewRequestedBytes().total,
        "Repair attribution excludes already held source storage");
  Require(context.ActiveTrackedPayload() ==
              occupied + bytes.wrapper + bytes.source_files +
                  bytes.source_off.total + bytes.repair.total +
                  bytes.writing.total + bytes.reload.total +
                  bytes.publication_files + bytes.publication_off.total,
          "disjoint actual scopes exactly partition live ledger without "
          "duplicated source/staged mesh");
}
void Configuration(bool unsupported) {
  Fixture fixture(unsupported);
  const auto baseline = fixture.context.ActiveTrackedPayload();
  for (unsigned kind = 0; kind < 12; ++kind) {
    Probe probe;
    auto limits = f::Limits();
    auto work = f::Work();
    auto request = fixture.Request(false);
    switch (kind) {
      case 0:
        limits.max_wrapper_new_requested_bytes = 0;
        break;
      case 1:
        limits.max_report_bytes = 0;
        break;
      case 2:
        limits.repair.inspection.max_new_requested_bytes = 0;
        break;
      case 3:
        limits.reload.max_source_bytes = 0;
        break;
      case 4:
        limits.publication.files.max_receipt_bytes = 0;
        break;
      case 5:
        work.hashing.max_jobs = 0;
        break;
      case 6:
        request.input_id = "has space";
        break;
      case 7:
        request.input_id = "";
        break;
      case 8:
        request.text = static_cast<ic::OffTextPolicy>(997);
        break;
      case 9:
        request.edge_count = static_cast<ic::OffEdgeCountPolicy>(997);
        break;
      case 10:
        limits.writing.max_total_new_requested_bytes = 1;
        break;
      case 11:
        limits.publication.files.max_entry_bytes =
            std::numeric_limits<std::uint64_t>::max();
        break;
    }
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        request, limits, work, {}, probe.Options());
    Require(
        (result.status == Status::kInvalidConfiguration ||
         result.status == Status::kInvalidRequest) &&
            !result.record && probe.repair_calls == 0 &&
            probe.backend.calls == 0 &&
            result.diagnostic.stage == Stage::kConfiguration,
        "all nested/enum/input configurations validated before actual work");
    probe.Joined();
    probe.backend.Empty();
    Require(fixture.context.ActiveTrackedPayload() == baseline,
            "configuration refused without fresh ledger payload");
  }
  for (const std::string_view name : {"../report.json", "CON", "source.off",
                                      "SOURCE.OFF", "a/b", "trailing."}) {
    Probe probe;
    auto request = fixture.Request(false, true);
    request.publication->receipt_basename = name;
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        request, f::Limits(), f::Work(), {}, probe.Options());
    Require(
        result.status == Status::kInvalidRequest && !result.record &&
            probe.repair_calls == 0 && probe.backend.calls == 0,
        "portable flat receipt and casefold collision rejected before engine");
  }
}
enum class Fault {
  kWrite,
  kReload,
  kHash,
  kReport,
  kReportAndWrite,
  kAfterRepairStop,
  kVerifyStop,
  kHashStop,
  kReportStop,
  kBudget
};
void CountAndBudgetBoundaries(bool unsupported) {
  Fixture fixture(unsupported);
  const auto baseline = fixture.context.ActiveTrackedPayload();
  for (unsigned kind = 0; kind < 6; ++kind) {
    if (unsupported && kind >= 3) continue;
    Probe probe;
    auto limits = f::Limits();
    auto request = fixture.Request(false);
    constexpr std::array<rep::DuplicateTarget, 2> excessive{{{0, 1}, {0, 2}}};
    if (kind == 0)
      limits.max_source_protection_bytes =
          fixture.source->EntryBytes().size() - 1;
    if (kind == 1) limits.max_input_id_bytes = 1;
    if (kind == 2) {
      limits.repair.max_targets = 1;
      request.targets = excessive;
    }
    if (kind == 3) limits.max_verification_vertices = 7;
    if (kind == 4) limits.max_verification_faces = 2;
    if (kind == 5) limits.max_verification_corners = 10;
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        request, limits, f::Work(), {}, probe.Options());
    Require(result.status == Status::kLimitExceeded &&
                result.diagnostic.stage == Stage::kConfiguration &&
                result.diagnostic.code == Code::kCountLimit && !result.record &&
                probe.repair_calls == 0 && probe.backend.calls == 0,
            "actual source/ID/target/verification extent refuses before "
            "allocation or engine");
    probe.backend.Empty();
  }
  for (unsigned scope = 0; scope < 4; ++scope) {
    Probe probe;
    auto limits = f::Limits();
    if (scope == 0) limits.max_wrapper_new_requested_bytes = 1;
    if (scope == 1) limits.repair.max_new_requested_bytes = 1;
    if (scope == 2) limits.writing.max_codec_new_requested_bytes = 1;
    if (scope == 3) limits.reload.max_codec_new_requested_bytes = 1;
    {
      const auto result = detail::RunOffDuplicateFileWorkflowForTest(
          fixture.Request(false), limits, f::Work(true), {}, probe.Options());
      Require(result.status == (scope < 2 ? Status::kBudgetExceeded
                                          : Status::kLimitExceeded) &&
                  result.outcomes.execution ==
                      rep::OffWorkflowExecutionOutcome::kFailed &&
                  result.outcomes.exit ==
                      rep::OffWorkflowExitMeaning::kExecutionFailure,
              "positive fresh-request scope cap causes real typed budget "
              "refusal: scope=" +
                  std::to_string(scope) +
                  " status=" + std::to_string(static_cast<int>(result.status)));
      if (scope == 0)
        Require(!result.record && probe.repair_calls == 0,
                "wrapper state admission refusal precedes engine");
      else {
        Require(result.record && probe.repair_calls == 1 &&
                    !result.record->Output() && !result.record->Reload() &&
                    !result.record->InputSha256(),
                "delegate budget failure preserves reached actual state "
                "without unverified output");
        if (scope == 1)
          Require(
              !result.record->RepairFacts(),
              "actual engine budget failure has no invented completed owner");
        else
          f::CheckRepair(*result.record->RepairFacts(), false, unsupported);
        if (scope >= 2) {
          Require(std::get<ic::OffStatus>(result.diagnostic.delegated_status) ==
                          ic::OffStatus::kLimitExceeded &&
                      std::get<ic::OffDiagnostic>(result.diagnostic.delegated)
                              .code == ic::OffDiagnosticCode::kRequestBudget &&
                      result.diagnostic.stage ==
                          (scope == 2 ? Stage::kWrite : Stage::kReload),
                  "codec allowance below its required accounting seed "
                  "preserves precise size-limit diagnostic");
        }
        Accounting(*result.record, fixture.context, probe.backend);
      }
      probe.Joined();
      f::Joined(result.execution);
    }
    probe.backend.Empty();
    Require(fixture.context.ActiveTrackedPayload() == baseline,
            "positive request-cap refusal restores captured-source baseline");
  }
  {
    Probe probe;
    auto limits = f::Limits();
    limits.max_source_protection_bytes = fixture.source->EntryBytes().size();
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        fixture.Request(false), limits, f::Work(true), {}, probe.Options());
    Require(result.status == Status::kSuccess && probe.repair_calls == 1,
            "raw source byte ceiling is inclusive at exact actual extent");
    Accounting(*result.record, fixture.context, probe.backend);
  }
  fixture.source.reset();
  f::Released(fixture.context);
}
void LaterFailures(bool unsupported) {
  for (bool changed : {false, true}) {
    if (unsupported && changed) continue;
    Fixture fixture(unsupported);
    const auto baseline = fixture.context.ActiveTrackedPayload();
    for (const auto fault :
         {Fault::kWrite, Fault::kReload, Fault::kHash, Fault::kReport,
          Fault::kReportAndWrite, Fault::kAfterRepairStop, Fault::kVerifyStop,
          Fault::kHashStop, Fault::kReportStop, Fault::kBudget}) {
      Probe probe;
      probe.context = &fixture.context;
      auto limits = f::Limits();
      switch (fault) {
        case Fault::kWrite:
          limits.writing.max_output_bytes = 1;
          break;
        case Fault::kReload:
          limits.reload.max_source_bytes = 1;
          break;
        case Fault::kHash:
          limits.max_hash_input_bytes = 1;
          break;
        case Fault::kReport:
          limits.max_report_bytes = 1;
          break;
        case Fault::kReportAndWrite:
          limits.writing.max_output_bytes = 1;
          limits.max_report_bytes = 1;
          break;
        case Fault::kAfterRepairStop:
          probe.stop_after_repair = true;
          break;
        case Fault::kVerifyStop:
          probe.stop_stage = Stage::kVerify;
          probe.stop_at = 0;
          break;
        case Fault::kHashStop:
          probe.stop_stage = Stage::kHash;
          probe.stop_at = 16;
          break;
        case Fault::kReportStop:
          probe.stop_stage = Stage::kReport;
          probe.stop_at = 64;
          break;
        case Fault::kBudget:
          probe.occupy_after_repair = true;
          break;
      }
      {
        const auto result = detail::RunOffDuplicateFileWorkflowForTest(
            fixture.Request(changed), limits, f::Work(true),
            probe.stop.get_token(), probe.Options());
        Retained(result, changed, unsupported);
        Require(probe.repair_calls == 1,
                "existing actual engine returned exactly once despite later "
                "failure");
        const bool canceled =
            fault == Fault::kAfterRepairStop || fault == Fault::kVerifyStop ||
            fault == Fault::kHashStop || fault == Fault::kReportStop;
        Require(
            result.status != Status::kSuccess &&
                result.outcomes.candidate ==
                    (changed ? rep::CandidateOutcome::accepted
                             : rep::CandidateOutcome::unchanged) &&
                result.outcomes.execution ==
                    (canceled ? rep::OffWorkflowExecutionOutcome::kCanceled
                              : rep::OffWorkflowExecutionOutcome::kFailed) &&
                result.outcomes.exit ==
                    (canceled ? rep::OffWorkflowExitMeaning::kCanceled
                              : rep::OffWorkflowExitMeaning::kExecutionFailure),
            "expected later failure preserves actual candidate and precise "
            "execution/exit meaning");
        if (fault == Fault::kReport || fault == Fault::kReportStop)
          Require(result.record->Output() && result.record->Reload() &&
                      result.record->InputSha256() &&
                      result.record->ReportJson().empty() &&
                      result.outcomes.profile ==
                          rep::OffWorkflowProfileOutcome::kPassed,
                  "missing report does not erase verified memory, hash or "
                  "passed required profile");
        if (fault == Fault::kWrite || fault == Fault::kReload ||
            fault == Fault::kReportAndWrite ||
            fault == Fault::kAfterRepairStop || fault == Fault::kVerifyStop)
          Require(!result.record->Output() && !result.record->Reload() &&
                      !result.record->InputSha256(),
                  "unverified outputs/hash never exposed on earlier failure");
        if (fault == Fault::kReportAndWrite)
          Require(result.diagnostic.stage == Stage::kWrite &&
                      result.diagnostic.code == Code::kWrite &&
                      result.report_diagnostic &&
                      result.report_diagnostic->stage == Stage::kReport &&
                      result.report_diagnostic->code == Code::kReportExtent,
                  "secondary report refusal cannot replace real delegated "
                  "write failure");
        if (fault == Fault::kHash)
          Require(
              result.diagnostic.stage == Stage::kHash &&
                  result.diagnostic.code == Code::kHashExtent &&
                  !result.record->InputSha256(),
              "hash cap refuses complete raw stream without guessed digest");
        Accounting(*result.record, fixture.context, probe.backend,
                   probe.occupied.Bytes());
        probe.Joined();
      }
      probe.occupied.Reset();
      probe.backend.Empty();
      Require(fixture.context.ActiveTrackedPayload() == baseline &&
                  fixture.context.ActiveWorkers() == 0,
              "later failure releases every fresh scope to captured-source "
              "baseline");
    }
  }
}
void StopsAndWorkers(bool unsupported) {
  Fixture fixture(unsupported, true);
  const auto baseline = fixture.context.ActiveTrackedPayload();
  {
    Probe probe;
    probe.stop.request_stop();
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        fixture.Request(false), f::Limits(), f::Work(), probe.stop.get_token(),
        probe.Options());
    Require(result.status == Status::kCanceled && !result.record &&
                probe.backend.calls == 0 && probe.repair_calls == 0,
            "prestop before state admission/actual engine");
  }
  {
    Probe probe;
    probe.stop_stage = Stage::kSourceProtection;
    probe.stop_at = 128;
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        fixture.Request(false), f::Limits(), f::Work(true),
        probe.stop.get_token(), probe.Options());
    Require(result.status == Status::kCanceled && probe.stopped &&
                probe.repair_calls == 0 && result.record &&
                !result.record->RepairFacts(),
            "real interior source protection stop precedes engine; no "
            "fabricated progress");
    f::Joined(result.execution);
    probe.Joined();
  }
  for (unsigned selected = 0; selected < 3; ++selected) {
    Probe probe;
    auto* workers = selected == 0
                        ? &probe.source
                        : (selected == 1 ? &probe.verification : &probe.report);
    workers->fail_at = 2;
    bool propagated = false;
    try {
      static_cast<void>(detail::RunOffDuplicateFileWorkflowForTest(
          fixture.Request(false), f::Limits(), f::Work(), {}, probe.Options()));
    } catch (const RuntimeMarker&) {
      propagated = true;
    }
    Require(propagated && workers->attempts == 2 && workers->launched == 1,
            "actual second native launch throws only after first genuine "
            "worker launch");
    probe.Joined();
    probe.backend.Empty();
    Require(fixture.context.ActiveTrackedPayload() == baseline &&
                fixture.context.ActiveWorkers() == 0,
            "partial launch exception joins and releases all wrapper/delegate "
            "work");
  }
  Probe probe;
  {
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        fixture.Request(false), f::Limits(), f::Work(), {}, probe.Options());
    Require(result.status == Status::kSuccess && probe.source.launched > 0 &&
                probe.verification.launched > 0 && probe.report.launched > 0,
            "substantial supported source, verification and report copying use "
            "real shared workers");
    probe.Joined();
    f::Joined(result.execution);
  }
  probe.backend.Empty();
  auto slots = geo::TryReserveWorkers(fixture.context, 4);
  Require(slots.Count() == 4, "genuinely occupy all shared worker leases");
  Probe fallback;
  {
    const auto result = detail::RunOffDuplicateFileWorkflowForTest(
        fixture.Request(false), f::Limits(), f::Work(), {}, fallback.Options());
    Require(result.status == Status::kSuccess &&
                fallback.source.launched == 0 &&
                fallback.verification.launched == 0 &&
                fallback.report.launched == 0 &&
                result.execution.source_protection.caller_jobs == 1 &&
                result.execution.report_copy.caller_jobs == 1,
            "occupied workers perform each real range on the caller without "
            "inventing leases");
    Accounting(*result.record, fixture.context, fallback.backend);
  }
  fallback.Joined();
  fallback.backend.Empty();
  slots.Reset();
}
void ByteCadence() {
  constexpr std::size_t kPolygonCorners = 64;
  std::string long_polygon = "OFF\n64 2 17\n";
  for (std::size_t i = 0; i < kPolygonCorners; ++i)
    long_polygon += std::to_string(i) + " 0 0\n";
  for (std::size_t rotation = 0; rotation < 2; ++rotation) {
    long_polygon += "64";
    for (std::size_t i = 0; i < kPolygonCorners; ++i)
      long_polygon += " " + std::to_string((i + rotation) % kPolygonCorners);
    long_polygon += '\n';
  }
  constexpr std::size_t kTargetCount = 8;
  std::string many_targets = "OFF\n4 9 17\n-0 0 0\n1 0 0\n1 1 0\n0 1 0\n";
  for (std::size_t i = 0; i <= kTargetCount; ++i) {
    many_targets += "4";
    for (std::size_t corner = 0; corner < 4; ++corner)
      many_targets += " " + std::to_string((i + corner) % 4);
    many_targets += '\n';
  }
  for (bool target_part : {false, true})
    for (const std::uint64_t quantum : {16, 1}) {
      Fixture fixture(false, false, target_part ? many_targets : long_polygon);
      const auto baseline = fixture.context.ActiveTrackedPayload();
      Probe probe;
      probe.stop_part = target_part ? detail::VerificationPart::kTargetPairs
                                    : detail::VerificationPart::kSourceCorners;
      const std::uint64_t expected = target_part ? 1 : (quantum == 16 ? 2 : 1);
      probe.part_stop_at = expected;
      auto work = f::Work(true);
      work.verification.cancellation_block_bytes = quantum;
      std::vector<rep::DuplicateTarget> targets;
      if (target_part)
        for (std::size_t i = 1; i <= kTargetCount; ++i)
          targets.push_back({0, i});
      else
        targets.push_back({0, 1});
      auto request = fixture.Request(true);
      request.targets = targets;
      {
        const auto result = detail::RunOffDuplicateFileWorkflowForTest(
            request, f::Limits(), work, probe.stop.get_token(),
            probe.Options());
        Require(probe.witnessed_part_element == expected &&
                    probe.repair_calls == 1 &&
                    result.status == Status::kCanceled && result.record &&
                    result.record->RepairFacts() &&
                    result.diagnostic.stage == Stage::kVerify &&
                    result.diagnostic.code == Code::kStopRequested &&
                    !result.record->Output() && !result.record->Reload() &&
                    !result.record->InputSha256() &&
                    result.record->ReportJson().empty(),
                "actual typed index/target task stop obeys byte quantum before "
                "next complete element: expected=" +
                    std::to_string(expected) + " actual=" +
                    (probe.witnessed_part_element
                         ? std::to_string(*probe.witnessed_part_element)
                         : "none"));
        const auto& repair = *result.record->RepairFacts();
        Require(repair.Outcome() == rep::CandidateOutcome::accepted &&
                    repair.SourceFaces().size() == 1 &&
                    repair.SourceFaces()[0] == 0 &&
                    repair.Candidate().View().face_offsets.size() == 2,
                "interior stop retains actual accepted candidate and inverse "
                "selection");
        for (std::size_t i = 0; i < repair.FaceMap().size(); ++i)
          Require(
              repair.FaceMap()[i] == 0,
              "all genuinely selected duplicates map to retained face zero");
        for (std::size_t i = 0; i < repair.SourceCorners().size(); ++i)
          Require(repair.SourceCorners()[i] == i,
                  "actual retained polygon corners preserve identity");
        if (!target_part) {
          Require(repair.CornerMap().size() == 2 * kPolygonCorners,
                  "complete actual long polygon map");
          for (std::size_t i = 0; i < kPolygonCorners; ++i)
            Require(repair.CornerMap()[i] == i &&
                        repair.CornerMap()[kPolygonCorners + i] ==
                            (i + 1) % kPolygonCorners,
                    "literal cyclic rotation maps survive genuine interior "
                    "corner cancellation");
        }
        Accounting(*result.record, fixture.context, probe.backend);
        f::Joined(result.execution);
        probe.Joined();
      }
      probe.backend.Empty();
      Require(fixture.context.ActiveTrackedPayload() == baseline &&
                  fixture.context.ActiveWorkers() == 0,
              "typed task cancellation releases all new scopes");
      fixture.source.reset();
      f::Released(fixture.context);
    }
}
void AllocationAndOccupied(bool unsupported) {
  Fixture fixture(unsupported);
  const auto baseline = fixture.context.ActiveTrackedPayload();
  for (bool runtime : {false, true}) {
    bool reached_success = false;
    for (std::size_t fail = 1; fail <= 256; ++fail) {
      Probe probe;
      probe.backend.fail_at = fail;
      probe.backend.runtime = runtime;
      bool propagated = false, success = false;
      try {
        const auto result = detail::RunOffDuplicateFileWorkflowForTest(
            fixture.Request(false), f::Limits(), f::Work(true), {},
            probe.Options());
        success = result.status == Status::kSuccess;
      } catch (const RuntimeMarker&) {
        Require(runtime, "only selected runtime marker propagates");
        propagated = true;
      } catch (const std::bad_alloc&) {
        Require(!runtime, "original bad_alloc not flattened");
        propagated = true;
      }
      if (success) {
        Require(probe.backend.calls < fail,
                "fault walk reaches actual completed call beyond all "
                "allocation sites");
        reached_success = true;
      } else
        Require(
            propagated,
            "unexpected allocation/backend exceptions propagate after joins");
      probe.Joined();
      probe.backend.Empty();
      Require(fixture.context.ActiveTrackedPayload() == baseline &&
                  fixture.context.ActiveWorkers() == 0,
              "every physical wrapper allocation fault restores source-only "
              "ledger");
      if (success) break;
    }
    Require(reached_success,
            "bounded allocation fault walk covers every actual "
            "original-backend request");
  }
  auto lease = geo::TryReservePayload(
      fixture.context, fixture.context.TrackedPayloadBudget() - baseline);
  Require(lease.status == geo::ReservationStatus::kAccepted,
          "actual occupied common ledger");
  Probe probe;
  auto result = detail::RunOffDuplicateFileWorkflowForTest(
      fixture.Request(false), f::Limits(), f::Work(), {}, probe.Options());
  Require(
      result.status == Status::kBudgetExceeded && !result.record &&
          probe.repair_calls == 0 && probe.backend.calls == 0,
      "fully occupied real ledger refuses before original allocator/engine");
  lease.lease.Reset();
  probe.backend.Empty();
}
void Publication(bool unsupported) {
  for (unsigned refusal = 0; refusal < 4; ++refusal) {
    Fixture fixture(unsupported);
    Probe probe;
    auto limits = f::Limits();
    if (refusal == 0) {
      fs::create_directory(fixture.destination);
      f::Write(fixture.destination / "sentinel", "preserve");
    } else if (refusal == 1)
      limits.publication.files.max_receipt_bytes = 1;
    else if (refusal == 2)
      limits.max_report_bytes = 1;
    else
      limits.max_hash_input_bytes = fixture.source->EntryBytes().size();
    {
      const auto result = detail::RunOffDuplicateFileWorkflowForTest(
          fixture.Request(!unsupported, true), limits, f::Work(), {},
          probe.Options());
      Retained(result, !unsupported, unsupported);
      Require(result.status != Status::kSuccess && result.publication &&
                  !result.publication->bundle && !result.record->Published() &&
                  result.record->Output() && result.record->Reload() &&
                  result.record->InputSha256() && probe.repair_calls == 1,
              "publication refusal preserves actual memory verification, "
              "captured digest and once-only engine");
      if (refusal == 0) {
        Require(probe.prepared_calls == 0 &&
                    result.publication->diagnostic.status ==
                        ic::OffFileStatus::kDestinationExists &&
                    f::Text(f::Read(fixture.destination / "sentinel")) ==
                        "preserve",
                "existing destination stays untouched before staged provider");
      } else {
        Require(result.publication->cleanup ==
                        ic::OffFileCleanupOutcome::kRemoved &&
                    !fs::exists(fixture.destination),
                "receipt/hash/report refusal removes real stage");
        if (refusal == 1)
          Require(probe.prepared_calls == 1 &&
                      result.publication->diagnostic.status ==
                          ic::OffFileStatus::kLimitExceeded,
                  "genuinely completed bounded report reaches actual receipt "
                  "import cap");
        if (refusal == 2)
          Require(probe.prepared_calls == 0 &&
                      result.diagnostic.stage == Stage::kReport &&
                      result.diagnostic.code == Code::kReportExtent,
                  "own report extent failure keeps wrapper stage through "
                  "provider cleanup");
        if (refusal == 3)
          Require(probe.prepared_calls == 0 &&
                      result.diagnostic.stage == Stage::kHash &&
                      result.diagnostic.code == Code::kHashExtent,
                  "raw source hash counted once, staged output stream "
                  "separately refuses exact aggregate cap");
      }
      probe.Joined();
    }
    probe.backend.Empty();
  }
  for (const auto action :
       {PublicationAction::kRace, PublicationAction::kEntryCorruption,
        PublicationAction::kExtra, PublicationAction::kThrow,
        PublicationAction::kStop}) {
    Fixture fixture(unsupported);
    const auto baseline = fixture.context.ActiveTrackedPayload();
    Probe probe;
    probe.root = fixture.root.Path();
    probe.destination = fixture.destination;
    probe.action = action;
    bool propagated = false;
    {
      try {
        const auto result = detail::RunOffDuplicateFileWorkflowForTest(
            fixture.Request(!unsupported, true), f::Limits(), f::Work(),
            probe.stop.get_token(), probe.Options());
        Retained(result, !unsupported, unsupported);
        Require(probe.prepared_calls == 1 && probe.published_calls == 0 &&
                    result.publication && !result.publication->bundle &&
                    !result.record->Published() &&
                    !result.record->PublishedReportPath(),
                "one real precommit report never promises a publication");
        const bool canceled = action == PublicationAction::kStop;
        Require(
            result.status == (canceled ? Status::kCanceled
                                       : Status::kDelegatedFailure) &&
                result.outcomes.execution ==
                    (canceled ? rep::OffWorkflowExecutionOutcome::kCanceled
                              : rep::OffWorkflowExecutionOutcome::kFailed) &&
                result.outcomes.exit ==
                    (canceled
                         ? rep::OffWorkflowExitMeaning::kCanceled
                         : rep::OffWorkflowExitMeaning::kExecutionFailure) &&
                result.outcomes.publication ==
                    (canceled ? rep::OffWorkflowPublicationOutcome::kCanceled
                              : rep::OffWorkflowPublicationOutcome::kFailed) &&
                result.diagnostic.stage == Stage::kPublication &&
                result.diagnostic.code == Code::kPublication &&
                result.record->Output() && result.record->Reload() &&
                result.record->InputSha256(),
            "physical failure preserves actual verified memory/hash with exact "
            "execution/publication/exit tuple");
        Require(!probe.prepared.empty() &&
                    f::Text(probe.prepared).find("\"outcome\":\"published\"") !=
                        std::string_view::npos &&
                    (result.record->ReportJson().empty() ||
                     (f::Text(result.record->ReportJson())
                              .find("\"role\":\"output\"") ==
                          std::string_view::npos &&
                      f::Text(result.record->ReportJson())
                              .find("\"outcome\":\"published\"") ==
                          std::string_view::npos)),
                "genuine conditional success bytes never escape a failed "
                "publication as final receipt");
        Require(
            result.outcomes.publication !=
                    rep::OffWorkflowPublicationOutcome::kPublished &&
                result.publication->cleanup ==
                    (action == PublicationAction::kExtra
                         ? ic::OffFileCleanupOutcome::kFailed
                         : ic::OffFileCleanupOutcome::kRemoved),
            "actual failed transaction removes its owned stage: action=" +
                std::to_string(static_cast<int>(action)) + " cleanup=" +
                std::to_string(static_cast<int>(result.publication->cleanup)) +
                " status=" +
                std::to_string(
                    static_cast<int>(result.publication->diagnostic.status)) +
                " code=" +
                std::to_string(
                    static_cast<int>(result.publication->diagnostic.code)));
        if (action == PublicationAction::kExtra)
          Require(result.publication->diagnostic.status ==
                          ic::OffFileStatus::kPreservationMismatch &&
                      result.publication->diagnostic.code ==
                          ic::OffFileDiagnosticCode::kStagedInventory &&
                      result.publication->cleanup_error &&
                      f::Text(f::Read(probe.observed_stage / "foreign")) ==
                          "new extra" &&
                      !fs::exists(probe.observed_stage / f::kEntry) &&
                      !fs::exists(probe.observed_stage / f::kReceipt),
                  "primary inventory mismatch survives secondary cleanup "
                  "failure; foreign child preserved, owned members removed");
        if (action == PublicationAction::kRace) {
          Require(result.publication->diagnostic.status ==
                          ic::OffFileStatus::kDestinationExists &&
                      f::Text(f::Read(fixture.destination / "sentinel")) ==
                          "preserve" &&
                      result.outcomes.profile ==
                          rep::OffWorkflowProfileOutcome::kPassed,
                  "real racing commit failure preserves passed predicates and "
                  "foreign destination");
        } else
          Require(!fs::exists(fixture.destination),
                  "precommit failure exposes no destination");
      } catch (const RuntimeMarker&) {
        propagated = true;
      }
      Require(
          propagated == (action == PublicationAction::kThrow),
          "own unexpected provider exception rethrows after publisher cleanup");
    }
    for (const auto& entry : fs::directory_iterator(fixture.root.Path()))
      Require(entry.path() == fixture.input ||
                  (action == PublicationAction::kExtra &&
                   entry.path() == probe.observed_stage) ||
                  (action == PublicationAction::kRace &&
                   entry.path() == fixture.destination),
              "no hidden stage survives failure or rethrow");
    probe.Joined();
    probe.backend.Empty();
    Require(fixture.context.ActiveTrackedPayload() == baseline,
            "failed physical publication releases fresh delegates and wrapper");
  }
  Fixture fixture(unsupported);
  Probe probe;
  probe.late_stop = true;
  auto result = detail::RunOffDuplicateFileWorkflowForTest(
      fixture.Request(!unsupported, true), f::Limits(), f::Work(),
      probe.stop.get_token(), probe.Options());
  Require(probe.published_calls == 1 && probe.backend.frozen &&
              probe.stop.stop_requested() &&
              result.status == Status::kSuccess && result.record &&
              result.record->Published(),
          "actual committed success survives late stop and noalloc handoff");
  const auto calls = probe.backend.calls;
  auto copy = *result.record;
  Require(
      probe.backend.calls == calls && !copy.ReportJson().empty(),
      "native immutable result copy allocates nothing after frozen handoff");
  Accounting(copy, fixture.context, probe.backend);
  bool refused = false;
  try {
    static_cast<void>(detail::RunOffDuplicateFileWorkflowForTest(
        fixture.Request(false), f::Limits(), f::Work(), {}, probe.Options()));
  } catch (const std::bad_alloc&) {
    refused = true;
  }
  Require(refused && probe.backend.calls == calls + 1,
          "frozen original backend is active and refuses subsequent real "
          "allocation");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    bool unsupported = false;
    for (int i = 1; i < argc; ++i) {
      Require(std::string_view(argv[i]) == "--unsupported-profile",
              "known request fixture argument");
      unsupported = true;
    }
    Configuration(unsupported);
    CountAndBudgetBoundaries(unsupported);
    LaterFailures(unsupported);
    StopsAndWorkers(unsupported);
    if (!unsupported) ByteCadence();
    AllocationAndOccupied(unsupported);
    Publication(unsupported);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
