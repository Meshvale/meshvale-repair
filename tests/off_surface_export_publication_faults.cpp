// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_surface_export.h>
#include <meshvale/repair/off_workflow.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "off_surface_export_fixture.h"
#include "off_surface_export_internal.h"
#include "off_workflow_test_internal.h"
// Deliberately source-coupled test target only. This producer header must never
// enter the installed consumer or the installed public include set.
#include "surface_files_test_internal.h"

namespace {
namespace rep = meshvale::repair;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace f = rep::test_off_surface;
namespace bridge = rep::off_surface_export_detail;
namespace publisher = ic::surface_files_detail;
namespace fs = std::filesystem;
using f::Require;
using FileStage = ic::SurfaceFileStage;
using FileStatus = ic::SurfaceFileStatus;
using FileCode = ic::SurfaceFileCode;
using Phase = ic::SurfaceFilePhaseOutcome;
constexpr std::string_view kInputId = "composed-publication-fault";
constexpr std::string_view kForeignPath = "foreign-member";
constexpr std::string_view kForeignBytes = "preserve foreign publisher owner";
constexpr std::uint64_t kPayloadMembers = 3;
constexpr std::uint64_t kFirstPayloadOrdinal = 0;
constexpr std::uint64_t kGlbOrdinal = 1;  // P's sorted bin/glb/json inventory.
constexpr std::uint64_t kReceiptOrdinal = kPayloadMembers;
constexpr std::uint64_t kIoBlockBytes = 64;
constexpr std::uint64_t kParallelJobs = 4;
constexpr std::size_t kSuccessfulLaunchesBeforeFailure = 1;
constexpr auto kFinalProgressWait = std::chrono::seconds{30};

enum class Fault {
  kNone,
  kCloseAfterStore,
  kPayloadBeforeFreshInspection,
  kReceiptBeforeReread,
  kInventoryFileKind,
  kInventoryExtraCleanupDenied,
  kFinalPayload,
  kFinalReceipt,
  kDestinationRace,
  kFinalPartialLaunch,
  kFinalReadStop
};

struct Fixture {
  f::Root root;
  geo::ExecutionContext context{{4, 1, f::kLedgerBytes}};
  fs::path source = root.Path() / f::kEntry;
  fs::path destination = root.Path() / "destination";
  ic::OffFileReadResult capture;
  std::vector<std::byte> reference_report;
  Fixture() {
    f::Write(source, f::kMixed);
    capture = f::Capture(source, context);
    Require(capture.asset.has_value(), "genuine retained OFF source capture");
    const auto limits = f::Limits();
    const auto work = f::Work();
    // A separate genuine reference operation, outside the composed call. Its
    // result and every derived owner are released before the measured attempt.
    {
      const rep::OffDuplicateFileWorkflowRequest reference{
          *capture.asset,
          kInputId,
          f::kTargets,
          ic::OffTextPolicy::kCanonicalAscii,
          ic::OffEdgeCountPolicy::kUnknownZero,
          std::nullopt};
      const auto result =
          rep::RunOffDuplicateFileWorkflow(reference, limits.off, work.off);
      Require(result.status == rep::OffWorkflowStatus::kSuccess &&
                  result.record && result.record->RepairFacts() &&
                  result.outcomes.publication ==
                      rep::OffWorkflowPublicationOutcome::kNotRequested,
              "separate memory-only reference has genuine accepted OFF facts");
      const auto bytes = result.record->ReportJson();
      Require(!bytes.empty(), "reference retains actual complete report bytes");
      reference_report.assign(bytes.begin(), bytes.end());
    }
    Require(context.ActiveWorkers() == 0,
            "reference operation joins every worker");
  }
  rep::OffGlbSurfaceWorkflowRequest Request() const {
    return f::Request(*capture.asset, destination, f::kTargets, kInputId);
  }
};

struct Probe {
  explicit Probe(Fault selected, const Fixture& fixture)
      : fault(selected),
        destination(fixture.destination),
        reference_report(fixture.reference_report) {}
  Fault fault;
  const fs::path& destination;
  std::span<const std::byte> reference_report;
  fs::path stage;
  std::optional<rep::MaterializedDuplicateRepair> repair;
  std::optional<ic::SurfaceOutputInspection> memory_inspection;
  std::vector<std::byte> receipt_before_mutation;
  std::uint64_t payload_bytes = 0;
  std::size_t composed_repairs = 0, publication_calls = 0;
  std::atomic<std::uint64_t> opened{0}, closed{0}, stores{0};
  std::atomic<std::uint64_t> stored_payload_bytes{0};
  std::atomic<bool> receipt_stored{false}, changed{false}, close_fired{false};
  std::atomic<bool> committed{false}, final_entered{false},
      final_released{false};
  std::atomic<std::uint64_t> final_read{0}, final_joined{0};
  std::atomic<std::uint64_t> forbidden_callbacks{0};
  std::atomic<bool> proceed{true}, finished{false}, stop_observed{false},
      timed_out{false};
  std::stop_source stop;
  std::size_t cleanup_calls = 0;
  ic::SurfaceFileCleanupOutcome cleanup_outcome =
      ic::SurfaceFileCleanupOutcome::kNotRequired;
  std::error_code cleanup_error;
  publisher::TestOptions testing;

  void CallbackBoundary() noexcept {
    if (committed.load(std::memory_order_acquire) ||
        (final_entered.load(std::memory_order_acquire) &&
         !final_released.load(std::memory_order_acquire)))
      forbidden_callbacks.fetch_add(1, std::memory_order_relaxed);
  }
  void CheckStagedPayloads() const {
    Require(memory_inspection.has_value(),
            "real memory V retained before P entrance");
    const auto& output = memory_inspection->Expected();
    const auto members = memory_inspection->CapturedMembers();
    Require(members.size() == kPayloadMembers,
            "three actual annotated GLB members");
    for (const auto& member : members)
      Require(
          std::ranges::equal(f::Read(stage / member.path), member.bytes),
          "closed physical payload equals genuine memory V bytes before fault");
    f::PhysicalGlb(f::Read(stage / f::kGlbPath), output);
    const auto capsule = f::Parse(f::Read(stage / f::kCapsuleJsonPath));
    const auto binary = f::Read(stage / f::kCapsuleBinaryPath);
    f::CapsuleMesh(capsule.At(f::kOriginalKey), binary,
                   output.Original().Source());
    f::CapsuleMesh(capsule.At(f::kTriangulatedKey), binary,
                   output.Triangles().Mesh());
  }
  void SnapshotReceipt() {
    Require(receipt_before_mutation.empty(),
            "one actual closed receipt snapshot");
    receipt_before_mutation = f::Read(stage / f::kReceiptPath);
    const auto document = f::Parse(receipt_before_mutation);
    Require(document.At(f::kSchemaKey).String() == f::kPublicationSchema &&
                document.At(f::kPhaseKey).String() == f::kVerifiedPhase &&
                !document.Has(f::kCommitKey) && !document.Has(f::kCleanupKey),
            "actual precommit receipt certifies staged payload, never "
            "commit/cleanup");
    const auto& report = document.At(f::kCallerReportKey);
    Require(
        report.At(f::kTextKey).String() == f::Text(reference_report) &&
            report.At(f::kByteLengthKey).U64() == reference_report.size() &&
            report.At(f::kSha256Key).String() == f::Sha(reference_report),
        "fixed noexcept provider imports exact separate OFF reference bytes");
    Require(document.At(f::kPayloadMembersKey).array.size() == kPayloadMembers,
            "physical receipt covers the three actual payloads");
    for (const auto& member : document.At(f::kPayloadMembersKey).array) {
      const auto bytes = f::Read(stage / member.At(f::kPathKey).String());
      Require(member.At(f::kByteLengthKey).U64() == bytes.size() &&
                  member.At(f::kSha256Key).String() == f::Sha(bytes),
              "independent staged lengths/hashes before substitution");
    }
  }
  static void AfterRepair(const rep::MaterializedDuplicateResult& actual,
                          void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    ++self.composed_repairs;
    Require(actual.status == rep::MaterializedDuplicateStatus::kSuccess &&
                actual.repair,
            "composed call enters the actual Repair engine once");
    self.repair.emplace(*actual.repair);
  }
  static void StageReady(publisher::NativeView native, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    self.stage = fs::path{std::basic_string<publisher::NativeChar>{native}};
  }
  static void BeforeOpen(std::uint64_t ordinal, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    if (self.fault != Fault::kReceiptBeforeReread ||
        ordinal != kReceiptOrdinal ||
        !self.receipt_stored.load(std::memory_order_acquire) ||
        self.changed.exchange(true))
      return;
    // Range handles precede stores; the next receipt open follows the joined,
    // closed write and occurs before the publisher's own physical reread.
    self.SnapshotReceipt();
    auto bytes = self.receipt_before_mutation;
    Require(!bytes.empty(), "actual receipt bytes available before corruption");
    bytes.back() ^= std::byte{1};
    f::Write(self.stage / f::kReceiptPath, bytes);
  }
  static void Opened(std::uint64_t, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    ++self.opened;
  }
  static void Closed(std::uint64_t, void* opaque) noexcept {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    ++self.closed;
  }
  static void AfterWrite(const publisher::IoProgress& progress, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    Require(progress.bytes > 0,
            "fault follows actual completed physical store");
    ++self.stores;
    if (progress.stage == FileStage::kStaging)
      self.stored_payload_bytes.fetch_add(progress.bytes);
    if (progress.stage == FileStage::kReceipt &&
        progress.member_ordinal == kReceiptOrdinal)
      self.receipt_stored.store(true, std::memory_order_release);
  }
  static std::error_code IoError(publisher::IoOperation operation,
                                 FileStage stage, std::uint64_t ordinal,
                                 std::uint64_t, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    if (self.fault == Fault::kCloseAfterStore &&
        operation == publisher::IoOperation::kClose &&
        stage == FileStage::kStaging && ordinal == kFirstPayloadOrdinal &&
        self.stored_payload_bytes.load() > 0 &&
        !self.close_fired.exchange(true))
      return std::make_error_code(std::errc::io_error);
    return {};
  }
  static void BeforeInventory(publisher::NativeView, bool final, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    if (final) return;
    self.CheckStagedPayloads();
    if (self.fault == Fault::kPayloadBeforeFreshInspection) {
      Require(!self.changed.exchange(true), "one payload substitution");
      auto bytes = f::Read(self.stage / f::kGlbPath);
      Require(!bytes.empty(),
              "genuine complete GLB before bad-magic substitution");
      bytes.front() ^= std::byte{1};
      f::Write(self.stage / f::kGlbPath, bytes);
    }
    if (self.fault == Fault::kInventoryFileKind) {
      Require(!self.changed.exchange(true) &&
                  fs::remove(self.stage / f::kCapsuleBinaryPath) &&
                  fs::create_directory(self.stage / f::kCapsuleBinaryPath),
              "actual expected capsule file replaced with a directory");
    }
    if (self.fault == Fault::kInventoryExtraCleanupDenied) {
      Require(!self.changed.exchange(true), "one unknown inventory member");
      f::Write(self.stage / kForeignPath, kForeignBytes);
    }
  }
  static void BeforeFinal(publisher::NativeView, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    self.SnapshotReceipt();
    if (self.fault == Fault::kFinalPayload ||
        self.fault == Fault::kFinalReceipt) {
      Require(!self.changed.exchange(true),
              "one final-window preflight substitution");
      const auto path =
          self.stage /
          (self.fault == Fault::kFinalPayload ? f::kGlbPath : f::kReceiptPath);
      auto bytes = f::Read(path);
      Require(!bytes.empty(), "actual closed final member");
      if (self.fault == Fault::kFinalPayload)
        bytes.front() ^= std::byte{1};
      else
        bytes.back() ^= std::byte{1};
      f::Write(path, bytes);
    }
  }
  static std::error_code CommitError(void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    if (self.fault == Fault::kDestinationRace) {
      Require(
          !self.changed.exchange(true) &&
              fs::create_directory(self.destination),
          "real racing destination appears before native no-replace commit");
      f::Write(self.destination / kForeignPath, kForeignBytes);
    }
    return {};  // The real native commit, rather than an injected result,
                // fails.
  }
  static std::error_code CleanupError(publisher::NativeView,
                                      void* opaque) noexcept {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    return self.fault == Fault::kInventoryExtraCleanupDenied
               ? std::make_error_code(std::errc::permission_denied)
               : std::error_code{};
  }
  static void CleanupFinished(ic::SurfaceFileCleanupOutcome outcome,
                              std::error_code error, void* opaque) noexcept {
    auto& self = *static_cast<Probe*>(opaque);
    self.CallbackBoundary();
    ++self.cleanup_calls;
    self.cleanup_outcome = outcome;
    self.cleanup_error = error;
  }
  void Configure() {
    testing.state = this;
    testing.files = {BeforeOpen, Opened, Closed, AfterWrite, IoError, this};
    testing.stage_ready = StageReady;
    testing.before_inventory = BeforeInventory;
    testing.before_final_verify = BeforeFinal;
    testing.commit_error = CommitError;
    testing.cleanup_error = CleanupError;
    testing.cleanup_finished = CleanupFinished;
    testing.native_commit_observed = &committed;
    testing.final_window_entered = &final_entered;
    testing.final_read_bytes = &final_read;
    testing.final_joined_workers = &final_joined;
    testing.final_lease_released = &final_released;
    if (fault == Fault::kFinalPartialLaunch)
      testing.final_launch_fail_after = kSuccessfulLaunchesBeforeFailure;
    if (fault == Fault::kFinalReadStop) {
      proceed.store(false);
      testing.final_continue = &proceed;
    }
  }
};

ic::SurfaceFilePublicationResult InvokePublication(
    const ic::SurfaceOutputInspection& inspection, const fs::path& destination,
    const ic::SurfaceFilePublicationOptions& options,
    const ic::SurfaceFilePublicationLimits& limits,
    const ic::SurfaceFilePublicationWorkPolicy& work, std::stop_token stop,
    void* opaque) {
  auto& probe = *static_cast<Probe*>(opaque);
  ++probe.publication_calls;
  Require(destination == probe.destination &&
              options.receipt_path == f::kReceiptPath &&
              options.receipt.provide != nullptr,
          "normal P entrance retains exact public destination/options and "
          "fixed provider");
  probe.memory_inspection.emplace(inspection);
  for (const auto& member : inspection.CapturedMembers())
    probe.payload_bytes += member.bytes.size();
  // No callback, observation, serializer, allocation or other fallible work is
  // performed after this genuine producer return, including committed success.
  return publisher::PublishForTest(inspection, destination, options, limits,
                                   work, stop, probe.testing);
}
static_assert(
    std::is_same_v<decltype(&InvokePublication), bridge::PublicationInvoker>);

rep::OffGlbSurfaceWorkflowWorkPolicy Work(Fault fault) {
  auto work = f::Work();
  const bool final_parallel = fault == Fault::kFinalPartialLaunch ||
                              fault == Fault::kFinalReadStop ||
                              fault == Fault::kNone;
  work.publication.files = {
      kIoBlockBytes,
      final_parallel ? 1
                     : static_cast<std::uint64_t>(
                           std::numeric_limits<std::ptrdiff_t>::max()),
      kIoBlockBytes, kIoBlockBytes, final_parallel ? kParallelJobs : 1};
  return work;
}

void CheckIndependentRepair(const Probe& probe) {
  Require(probe.composed_repairs == 1 && probe.repair,
          "exactly one actual Repair invocation inside composed attempt");
  const auto& repair = *probe.repair;
  f::CheckMesh(repair.Source(), f::Literal(f::kMixed));
  f::CheckMesh(repair.Candidate(),
               {f::kMixedPositions, f::kRepairOffsets, f::kRepairCorners});
  Require(repair.Outcome() == rep::CandidateOutcome::accepted &&
              std::ranges::equal(repair.FaceMap(), f::kFaceMap) &&
              std::ranges::equal(repair.CornerMap(), f::kCornerMap) &&
              std::ranges::equal(repair.SourceFaces(), f::kSourceFaces) &&
              std::ranges::equal(repair.SourceCorners(), f::kSourceCorners),
          "independently held genuine Repair keeps full literal O/R storage "
          "and four maps");
}
void CheckJoined(const ic::SurfaceFileFacts& facts) {
  for (const auto* observation :
       {&facts.payload_write, &facts.payload_reread, &facts.report_import,
        &facts.receipt_write, &facts.receipt_reread,
        &facts.final_payload_verification, &facts.final_receipt_verification})
    if (*observation)
      Require((*observation)->execution.launched_workers ==
                  (*observation)->execution.joined_workers,
              "all reached physical jobs retain equal actual launch/join "
              "observations");
}
void CheckRetained(const rep::OffGlbSurfaceWorkflowResult& result,
                   const Probe& probe, const Fixture& fixture) {
  Require(result.record && probe.publication_calls == 1,
          "one genuine publisher return is retained in the outer record");
  f::CheckOff(*result.record, f::kMixed, true);
  Require(std::ranges::equal(result.record->Facts().off->record->ReportJson(),
                             fixture.reference_report),
          "composed late failure preserves the complete exact OFF reference "
          "report");
  CheckIndependentRepair(probe);
  const auto& facts = result.record->Facts();
  Require(facts.triangulation && facts.triangulation->triangulation &&
              facts.encoding && facts.encoding->output && facts.inspection &&
              facts.inspection->inspection && facts.publication,
          "genuine R/T/E/memory-V facts remain readable after P return");
  f::CheckTriangles(facts.off->record->RepairFacts()->Candidate(),
                    *facts.triangulation->triangulation);
  f::CheckWire(*facts.encoding->output);
  CheckJoined(facts.publication->facts);
  Require(geo::SharesExecutionBudget(fixture.context,
                                     facts.encoding->output->Original()
                                         .Source()
                                         .ShareExecutionContext()) &&
              fixture.context.ActiveWorkers() == 0,
          "actual composition shares the original ledger and releases every "
          "worker lease");
}
void CheckCleanup(const ic::SurfaceFilePublicationResult& result,
                  const Probe& probe, bool racing) {
  Require(!result.bundle && !probe.committed.load() && !probe.stage.empty() &&
              probe.opened.load() == probe.closed.load() &&
              probe.forbidden_callbacks.load() == 0 && probe.cleanup_calls == 1,
          "failure creates no committed owner and closes all actual "
          "hook-bearing files");
  if (probe.fault == Fault::kInventoryExtraCleanupDenied) {
    Require(result.cleanup.outcome == ic::SurfaceFileCleanupOutcome::kFailed &&
                result.cleanup.error ==
                    std::make_error_code(std::errc::permission_denied) &&
                probe.cleanup_outcome == result.cleanup.outcome &&
                probe.cleanup_error == *result.cleanup.error &&
                fs::is_directory(probe.stage) &&
                f::Text(f::Read(probe.stage / kForeignPath)) == kForeignBytes,
            "typed cleanup denial retains primary failure and actual "
            "stage/foreign residue");
  } else {
    Require(result.cleanup.outcome == ic::SurfaceFileCleanupOutcome::kRemoved &&
                !result.cleanup.error &&
                probe.cleanup_outcome == result.cleanup.outcome &&
                !probe.cleanup_error && !fs::exists(probe.stage),
            "actual owned staging tree is removed exactly once on failure");
  }
  if (racing) {
    std::size_t members = 0;
    for (const auto& member : fs::directory_iterator(probe.destination)) {
      Require(
          member.path().filename() == kForeignPath && member.is_regular_file(),
          "foreign destination inventory is preserved without repaired "
          "payloads");
      ++members;
    }
    Require(members == 1 && f::Text(f::Read(probe.destination /
                                            kForeignPath)) == kForeignBytes,
            "real no-replace race preserves exact foreign sentinel bytes");
  } else
    Require(!fs::exists(probe.destination),
            "precommit failure leaves no destination");
}

class StopController {
 public:
  explicit StopController(Probe& probe)
      : probe_(probe), thread_([&probe] {
          const auto deadline =
              std::chrono::steady_clock::now() + kFinalProgressWait;
          while (!probe.finished.load(std::memory_order_acquire)) {
            if (probe.final_read.load(std::memory_order_acquire) > 0) {
              probe.stop_observed.store(true, std::memory_order_release);
              probe.stop.request_stop();
              probe.proceed.store(true, std::memory_order_release);
              return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
              probe.timed_out.store(true, std::memory_order_release);
              probe.stop.request_stop();
              probe.proceed.store(true, std::memory_order_release);
              return;
            }
            std::this_thread::yield();
          }
          probe.proceed.store(true, std::memory_order_release);
        }) {}
  StopController(const StopController&) = delete;
  StopController& operator=(const StopController&) = delete;
  ~StopController() {
    probe_.finished.store(true, std::memory_order_release);
    probe_.proceed.store(true, std::memory_order_release);
    thread_.join();
  }

 private:
  Probe& probe_;
  std::thread thread_;
};

void CheckReceiptReached(const ic::SurfaceFileFacts& facts,
                         const Probe& probe) {
  Require(facts.payload_write &&
              facts.payload_write->outcome == Phase::kCompleted &&
              facts.payload_reread &&
              facts.payload_reread->outcome == Phase::kCompleted &&
              facts.inspection &&
              facts.inspection->status == ic::SurfaceStatus::kSuccess &&
              facts.provider && facts.provider->outcome == Phase::kCompleted &&
              facts.provider->returned_bytes == probe.reference_report.size() &&
              facts.report_import &&
              facts.report_import->outcome == Phase::kCompleted &&
              facts.receipt_write &&
              facts.receipt_write->outcome == Phase::kCompleted,
          "actual closed payload/fresh physical V/fixed "
          "provider/report/receipt precede later fault");
}
void CheckFinalReads(const ic::SurfaceFileFacts& facts, const Probe& probe) {
  Require(probe.final_entered.load() && probe.final_released.load() &&
              probe.final_read.load() > 0 && facts.final_inventory &&
              facts.final_inventory->outcome == Phase::kCompleted &&
              (facts.final_payload_verification ||
               facts.final_receipt_verification),
          "genuine protected final group entered, read, joined and released "
          "its lease");
  const auto& payload = facts.final_payload_verification;
  const auto& receipt = facts.final_receipt_verification;
  const auto read =
      (payload ? payload->bytes_read : 0) + (receipt ? receipt->bytes_read : 0);
  Require(read == probe.final_read.load(),
          "atomic read witness equals disjoint retained payload/receipt byte "
          "observations");
  const auto& execution = payload ? payload->execution : receipt->execution;
  Require(execution.launched_workers == execution.joined_workers &&
              execution.joined_workers == probe.final_joined.load(),
          "actual final group join witness agrees with returned execution");
  if (payload && receipt)
    Require(
        payload->execution.planned_jobs == receipt->execution.planned_jobs &&
            payload->execution.launched_workers ==
                receipt->execution.launched_workers &&
            payload->execution.joined_workers ==
                receipt->execution.joined_workers &&
            payload->execution.caller_jobs == receipt->execution.caller_jobs &&
            payload->execution.completed_jobs ==
                receipt->execution.completed_jobs,
        "payload and receipt expose the same nonadditive final execution "
        "snapshot");
}
void CheckOrdinaryFailure(const rep::OffGlbSurfaceWorkflowResult& result,
                          const Probe& probe) {
  const auto& publication = *result.record->Facts().publication;
  const auto& facts = publication.facts;
  const auto& diagnostic = publication.diagnostic;
  const bool canceled = probe.fault == Fault::kFinalReadStop;
  Require(
      result.status == (canceled
                            ? rep::OffGlbSurfaceStatus::kCanceled
                            : rep::OffGlbSurfaceStatus::kDelegatedFailure) &&
          result.diagnostic.stage == rep::OffGlbSurfaceStage::kPublication &&
          result.diagnostic.code == rep::OffGlbSurfaceCode::kPublication &&
          publication.outcome ==
              (canceled ? ic::SurfaceFilePublicationOutcome::kCanceled
                        : ic::SurfaceFilePublicationOutcome::kFailed),
      "outer result preserves actual reached P failure/cancel domain");
  switch (probe.fault) {
    case Fault::kCloseAfterStore:
      Require(probe.close_fired.load() &&
                  probe.stored_payload_bytes.load() > 0 &&
                  diagnostic.status == FileStatus::kIoFailure &&
                  diagnostic.stage == FileStage::kStaging &&
                  diagnostic.code == FileCode::kFileClose &&
                  diagnostic.member_ordinal == kFirstPayloadOrdinal &&
                  diagnostic.system_error ==
                      std::make_error_code(std::errc::io_error) &&
                  facts.payload_write &&
                  facts.payload_write->outcome == Phase::kFailed &&
                  facts.payload_write->bytes_written ==
                      probe.stored_payload_bytes.load() &&
                  facts.payload_write->completed_files == 0 &&
                  !facts.payload_inventory && !facts.inspection &&
                  !facts.provider && !facts.commit,
              "genuine checked close fails after actual first payload stores "
              "with exact I/O cause");
      break;
    case Fault::kPayloadBeforeFreshInspection:
      Require(probe.changed.load() &&
                  diagnostic.status == FileStatus::kVerificationFailed &&
                  diagnostic.stage == FileStage::kInspection &&
                  diagnostic.code == FileCode::kInspection &&
                  diagnostic.inspection_status ==
                      ic::SurfaceStatus::kVerificationFailed &&
                  diagnostic.inspection_diagnostic &&
                  diagnostic.inspection_diagnostic->stage ==
                      ic::SurfaceStage::kContainerRead &&
                  diagnostic.inspection_diagnostic->code ==
                      ic::SurfaceCode::kGlbDelegate &&
                  facts.payload_inventory &&
                  facts.payload_inventory->outcome == Phase::kCompleted &&
                  facts.payload_reread &&
                  facts.payload_reread->outcome == Phase::kCompleted &&
                  facts.payload_reread->bytes_read == probe.payload_bytes &&
                  facts.inspection &&
                  facts.inspection->status ==
                      ic::SurfaceStatus::kVerificationFailed &&
                  facts.inspection->diagnostic.code ==
                      ic::SurfaceCode::kGlbDelegate &&
                  !facts.provider && !facts.receipt_write &&
                  !facts.final_inventory && !facts.commit,
              "actual changed GLB magic survives reread and fails fresh native "
              "GLB inspection");
      break;
    case Fault::kReceiptBeforeReread:
      CheckReceiptReached(facts, probe);
      Require(probe.changed.load() && !probe.receipt_before_mutation.empty() &&
                  diagnostic.status == FileStatus::kVerificationFailed &&
                  diagnostic.stage == FileStage::kReceipt &&
                  diagnostic.code == FileCode::kReceiptBytes &&
                  diagnostic.member_ordinal == kReceiptOrdinal &&
                  facts.receipt_reread &&
                  facts.receipt_reread->outcome == Phase::kFailed &&
                  facts.receipt_reread->bytes_read ==
                      probe.receipt_before_mutation.size() &&
                  facts.receipt_reread->completed_files == 0 &&
                  !facts.final_inventory && !facts.commit,
              "actual closed receipt substitution fails byte reread, never "
              "claims completed file");
      break;
    case Fault::kInventoryFileKind:
    case Fault::kInventoryExtraCleanupDenied:
      Require(probe.changed.load() &&
                  diagnostic.status == FileStatus::kVerificationFailed &&
                  diagnostic.stage == FileStage::kInventory &&
                  diagnostic.code == (probe.fault == Fault::kInventoryFileKind
                                          ? FileCode::kFileKind
                                          : FileCode::kMemberInventory) &&
                  facts.payload_write &&
                  facts.payload_write->outcome == Phase::kCompleted &&
                  facts.payload_inventory &&
                  facts.payload_inventory->outcome == Phase::kFailed &&
                  !facts.payload_reread && !facts.inspection &&
                  !facts.provider && !facts.commit,
              "actual kind/unknown-member substitutions retain distinct "
              "closed-inventory diagnoses");
      break;
    case Fault::kFinalPayload:
    case Fault::kFinalReceipt: {
      CheckReceiptReached(facts, probe);
      CheckFinalReads(facts, probe);
      const bool payload = probe.fault == Fault::kFinalPayload;
      const auto offset =
          payload ? 0 : probe.receipt_before_mutation.size() - 1;
      Require(probe.changed.load() &&
                  diagnostic.status == FileStatus::kVerificationFailed &&
                  diagnostic.stage == FileStage::kInventory &&
                  diagnostic.code == (payload ? FileCode::kFileChanged
                                              : FileCode::kReceiptBytes) &&
                  diagnostic.member_ordinal ==
                      (payload ? kGlbOrdinal : kReceiptOrdinal) &&
                  diagnostic.byte_offset == offset && !facts.commit &&
                  facts.receipt_reread &&
                  facts.receipt_reread->outcome == Phase::kCompleted,
              "final protected comparison refuses changed payload/receipt "
              "after genuine fresh V and reread");
      break;
    }
    case Fault::kDestinationRace:
      CheckReceiptReached(facts, probe);
      CheckFinalReads(facts, probe);
      Require(
          probe.changed.load() &&
              diagnostic.status == FileStatus::kDestinationExists &&
              diagnostic.stage == FileStage::kPublication &&
              diagnostic.code == FileCode::kDestinationExists && facts.commit &&
              facts.commit->outcome ==
                  ic::SurfaceFileCommitOutcome::kDestinationExists &&
              facts.final_payload_verification &&
              facts.final_payload_verification->outcome == Phase::kCompleted &&
              facts.final_receipt_verification &&
              facts.final_receipt_verification->outcome == Phase::kCompleted,
          "real no-replace commit loses actual destination race after complete "
          "final checks");
      break;
    case Fault::kFinalReadStop: {
      CheckReceiptReached(facts, probe);
      CheckFinalReads(facts, probe);
      const auto& execution = facts.final_payload_verification
                                  ? facts.final_payload_verification->execution
                                  : facts.final_receipt_verification->execution;
      Require(probe.stop_observed.load() && !probe.timed_out.load() &&
                  diagnostic.status == FileStatus::kCanceled &&
                  diagnostic.stage == FileStage::kInventory &&
                  diagnostic.code == FileCode::kStopRequested &&
                  !facts.commit && execution.launched_workers > 1 &&
                  execution.completed_jobs < execution.planned_jobs &&
                  probe.final_read.load() < probe.payload_bytes,
              "external stop follows real bounded final reads and "
              "aborts/join/releases actual group");
      break;
    }
    case Fault::kNone:
    case Fault::kFinalPartialLaunch:
      Require(false, "success and unexpected exception have separate oracles");
  }
  CheckCleanup(publication, probe, probe.fault == Fault::kDestinationRace);
}

void CheckPhysicalSource(const Fixture& fixture) {
  const auto status = fs::symlink_status(fixture.source);
  Require(
      fs::exists(status) && fs::is_regular_file(status) &&
          !fs::is_symlink(status),
      "controlled physical source remains an existing regular nonsymlink file");
  Require(std::ranges::equal(f::Read(fixture.source), f::Bytes(f::kMixed)),
          "physical source retains the complete independent literal OFF bytes "
          "after attempt");
}

void CheckRootInventory(const Fixture& fixture, const Probe& probe) {
  CheckPhysicalSource(fixture);
  for (const auto& child : fs::directory_iterator(fixture.root.Path()))
    Require(
        child.path() == fixture.source ||
            ((probe.fault == Fault::kNone ||
              probe.fault == Fault::kDestinationRace) &&
             child.path() == fixture.destination) ||
            (probe.fault == Fault::kInventoryExtraCleanupDenied &&
             child.path() == probe.stage),
        "complete fixture root has no unreported stage or extra publication");
}
void ReleaseHeldAndCheck(Fixture& fixture, Probe& probe, std::size_t baseline) {
  probe.memory_inspection.reset();
  probe.repair.reset();
  Require(fixture.context.ActiveWorkers() == 0 &&
              fixture.context.ActiveTrackedPayload() == baseline,
          "every reached composed/P/fresh-V owner releases to captured-source "
          "baseline");
  CheckRootInventory(fixture, probe);
  fixture.capture.asset.reset();
  f::Released(fixture.context);
}

void Ordinary(Fault fault) {
  Fixture fixture;
  const auto baseline = fixture.context.ActiveTrackedPayload();
  Probe probe(fault, fixture);
  probe.Configure();
  rep::off_workflow_detail::WorkflowTestOptions off;
  off.after_repair = Probe::AfterRepair;
  off.state = &probe;
  bridge::WorkflowTestOptions options;
  options.off = &off;
  options.publication_invoker = InvokePublication;
  options.publication_state = &probe;
  {
    std::optional<StopController> controller;
    if (fault == Fault::kFinalReadStop) controller.emplace(probe);
    const auto result = bridge::RunOffGlbSurfaceWorkflowForTest(
        fixture.Request(), f::Limits(), Work(fault), probe.stop.get_token(),
        options);
    controller.reset();
    CheckRetained(result, probe, fixture);
    if (fault == Fault::kNone) {
      f::Complete(result, fixture.destination, f::kMixed, true);
      const auto& publication = *result.record->Facts().publication;
      CheckFinalReads(publication.facts, probe);
      Require(probe.committed.load() && probe.forbidden_callbacks.load() == 0 &&
                  probe.opened.load() == probe.closed.load() &&
                  probe.cleanup_calls == 0 && probe.final_joined.load() > 1 &&
                  !fs::exists(probe.stage),
              "genuine adapter success performs parallel final native work "
              "without late injected callbacks");
    } else
      CheckOrdinaryFailure(result, probe);
  }
  if (fault == Fault::kInventoryExtraCleanupDenied)
    Require(probe.cleanup_calls == 1 && fs::is_directory(probe.stage),
            "outer/result destruction never silently retries failed cleanup");
  ReleaseHeldAndCheck(fixture, probe, baseline);
}

void PartialFinalLaunch() {
  Fixture fixture;
  const auto baseline = fixture.context.ActiveTrackedPayload();
  Probe probe(Fault::kFinalPartialLaunch, fixture);
  probe.Configure();
  rep::off_workflow_detail::WorkflowTestOptions off;
  off.after_repair = Probe::AfterRepair;
  off.state = &probe;
  bridge::WorkflowTestOptions options;
  options.off = &off;
  options.publication_invoker = InvokePublication;
  options.publication_state = &probe;
  bool propagated = false, returned = false;
  try {
    static_cast<void>(bridge::RunOffGlbSurfaceWorkflowForTest(
        fixture.Request(), f::Limits(), Work(probe.fault), {}, options));
    returned = true;
  } catch (const std::system_error& error) {
    if (error.code() !=
        std::make_error_code(std::errc::resource_unavailable_try_again))
      throw;
    propagated = true;
  }
  // No returned wrapper is promised or fabricated for this unexpected throw.
  CheckIndependentRepair(probe);
  Require(
      propagated && !returned && probe.publication_calls == 1 &&
          probe.memory_inspection && !probe.receipt_before_mutation.empty() &&
          !probe.final_entered.load() && probe.final_read.load() == 0 &&
          probe.final_joined.load() == kSuccessfulLaunchesBeforeFailure &&
          probe.final_released.load() && !probe.committed.load() &&
          probe.opened.load() == probe.closed.load() &&
          probe.forbidden_callbacks.load() == 0 && probe.cleanup_calls == 1 &&
          probe.cleanup_outcome == ic::SurfaceFileCleanupOutcome::kRemoved &&
          !probe.cleanup_error && !probe.stage.empty() &&
          !fs::exists(probe.stage) && !fs::exists(fixture.destination) &&
          fixture.context.ActiveWorkers() == 0,
      "real final default launch fails after one launch; gate aborts, real "
      "worker joins, lease releases and stage cleans before exact exception "
      "propagates");
  f::CheckTriangles(probe.repair->Candidate(),
                    probe.memory_inspection->Expected().Triangles());
  f::CheckWire(probe.memory_inspection->Expected());
  ReleaseHeldAndCheck(fixture, probe, baseline);
}
}  // namespace
int main() {
  try {
    f::CheckReader();
    for (const auto fault :
         {Fault::kNone, Fault::kCloseAfterStore,
          Fault::kPayloadBeforeFreshInspection, Fault::kReceiptBeforeReread,
          Fault::kInventoryFileKind, Fault::kInventoryExtraCleanupDenied,
          Fault::kFinalPayload, Fault::kFinalReceipt, Fault::kDestinationRace,
          Fault::kFinalReadStop})
      Ordinary(fault);
    PartialFinalLaunch();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
