// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "gltf_file_workflow_fixture.h"
#include "gltf_file_workflow_test_internal.h"
#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace {
namespace repair = meshvale::repair;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace test = repair::test_file_workflow;
namespace detail = repair::gltf_file_workflow_detail;
namespace own = repair::materialized_duplicates_detail;
namespace fs = std::filesystem;
using test::Fixture;
using test::Require;
constexpr std::size_t kBlockCapacity = 4096;
constexpr std::size_t kLaunchCapacity = 512;
constexpr std::size_t kLengthBytes = 8;
constexpr std::string_view kHashDomain = "meshvale.gltf-file-snapshot/1";
constexpr std::size_t kTerminatorBytes = 1;
constexpr std::uint64_t kBoundaryDifference = 1;
constexpr std::size_t kPartialLaunchOrdinal = 2;
constexpr std::uint64_t kMemoryObserverPartitionBytes = 128;
constexpr std::chrono::seconds kGateTimeout{30};
constexpr std::size_t kReadbackFaultByteOffset = 37;
constexpr std::uint64_t kReadbackFaultFileOrdinal = 1;
constexpr std::string_view kWriteReadbackArgument = "--write-readback-report";
constexpr std::string_view kReadbackCase = "staged-byte-readback-failure";
constexpr std::string_view kManifestName = "cases.json";
constexpr std::string_view kReportName = "report.json";
constexpr std::string_view kReadbackManifest =
    "{\"version\":1,\"cases\":[\"staged-byte-readback-failure\"]}\n";
struct OriginalFailure : std::exception {
  const char* what() const noexcept override {
    return "original fixture allocation exception";
  }
};
struct LaunchFailure : std::exception {
  const char* what() const noexcept override {
    return "original fixture native launch exception";
  }
};
struct Block {
  void* pointer = nullptr;
  std::size_t bytes = 0;
  std::size_t alignment = 0;
};
// This observes only the named Repair original backend. Delegated Geometry
// and Interchange allocators remain the real qualified public implementations.
struct OriginalRequests {
  std::mutex mutex;
  std::array<Block, kBlockCapacity> blocks;
  std::size_t calls = 0;
  std::size_t fail_at = 0;
  std::size_t live = 0;
  std::size_t peak = 0;
  bool distinctive = false;
  bool frozen = false;
  bool invalid_free = false;
  const geo::ExecutionContext* context = nullptr;
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<OriginalRequests*>(opaque);
    std::lock_guard lock(self.mutex);
    ++self.calls;
    Require(!self.frozen,
            "no original Repair request after conditional report prepared");
    if (self.calls == self.fail_at) {
      if (self.distinctive) throw OriginalFailure{};
      throw std::bad_alloc{};
    }
    auto slot = std::ranges::find_if(
        self.blocks, [](const Block& block) { return !block.pointer; });
    Require(slot != self.blocks.end(),
            "preallocated original request observer capacity");
    const auto backend = own::DefaultAllocationBackend();
    void* pointer = backend.allocate(bytes, alignment, backend.state);
    *slot = {pointer, bytes, alignment};
    self.live += bytes;
    self.peak = std::max(self.peak, self.live);
    return pointer;
  }
  static void Deallocate(void* pointer, std::size_t bytes,
                         std::size_t alignment, void* opaque) noexcept {
    auto& self = *static_cast<OriginalRequests*>(opaque);
    std::lock_guard lock(self.mutex);
    auto slot = std::ranges::find_if(
        self.blocks,
        [pointer](const Block& block) { return block.pointer == pointer; });
    if (slot == self.blocks.end() || slot->bytes != bytes ||
        slot->alignment != alignment || self.live < bytes ||
        (self.context && self.context->ActiveTrackedPayload() < bytes)) {
      self.invalid_free = true;
    } else {
      self.live -= bytes;
      *slot = {};
    }
    const auto backend = own::DefaultAllocationBackend();
    backend.deallocate(pointer, bytes, alignment, backend.state);
  }
  own::AllocationBackend Backend() { return {Allocate, Deallocate, this}; }
  void CheckEmpty() const {
    Require(
        !invalid_free && live == 0,
        "exact original bytes/alignment, free before ledger release, no leak");
  }
};
struct Gate {
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false;
  bool released = false;
  void Enter() noexcept {
    std::unique_lock lock(mutex);
    entered = true;
    condition.notify_all();
    condition.wait_for(lock, kGateTimeout, [&] { return released; });
  }
  void Wait() {
    std::unique_lock lock(mutex);
    Require(condition.wait_for(lock, kGateTimeout, [&] { return entered; }),
            "real native hash entry reached");
  }
  void Release() noexcept {
    std::lock_guard lock(mutex);
    released = true;
    condition.notify_all();
  }
};
struct Workers {
  struct Slot {
    own::NativeWorkerRecord wrapped;
    own::NativeWorkerRecord* original = nullptr;
    Workers* owner = nullptr;
  };
  std::array<Slot, kLaunchCapacity> slots;
  std::size_t attempts = 0;
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::size_t fail_at = 0;
  std::atomic<std::size_t> entered{0};
  std::atomic<std::size_t> exited{0};
  std::stop_source* cancel = nullptr;
  Gate* gate = nullptr;
  static void Entry(own::NativeWorkerRecord& record) noexcept {
    auto& slot = *static_cast<Slot*>(record.state);
    auto& self = *slot.owner;
    const auto entered = self.entered.fetch_add(1);
    if (self.cancel) self.cancel->request_stop();
    if (self.gate && entered == 0) self.gate->Enter();
    slot.original->entry(*slot.original);
    self.exited.fetch_add(1);
  }
  static std::uintptr_t Launch(own::NativeWorkerRecord* record, void* opaque) {
    auto& self = *static_cast<Workers*>(opaque);
    ++self.attempts;
    if (self.attempts == self.fail_at) throw LaunchFailure{};
    Require(self.launched < self.slots.size(),
            "preallocated native observer capacity");
    auto& slot = self.slots[self.launched];
    slot.original = record;
    slot.owner = &self;
    slot.wrapped = {Entry, &slot, record->ordinal};
    const auto backend = own::DefaultNativeWorkerBackend();
    const auto handle = backend.launch(&slot.wrapped, backend.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<Workers*>(opaque);
    const auto backend = own::DefaultNativeWorkerBackend();
    backend.join(handle, backend.state);
    ++self.joined;
  }
  own::NativeWorkerBackend Backend() { return {Launch, Join, this}; }
  void CheckJoined() const {
    Require(launched == joined && entered.load() == launched &&
                exited.load() == launched,
            "all actual own native entries exit and join, including partial "
            "launch");
  }
};
enum class PreparedAction {
  kNone,
  kFreezeOriginals,
  kCancel,
  kOccupy,
  kThrow,
  kReplaceStaging,
  kCorruptStagedBytes
};
struct Witness {
  OriginalRequests wrapper;
  OriginalRequests memory;
  Workers hash;
  Workers report;
  Workers memory_source;
  Workers memory_report;
  const Fixture* fixture = nullptr;
  std::stop_source* stop = nullptr;
  bool cancel_at_admission = false;
  bool fail_original_at_admission = false;
  PreparedAction action = PreparedAction::kNone;
  std::size_t admitted = 0;
  std::size_t prepared = 0;
  std::uint64_t work_bytes = 0;
  std::size_t report_bytes = 0;
  std::vector<std::byte> conditional_report;
  bool staged_bytes_corrupted = false;
  explicit Witness(const Fixture& source) : fixture(&source) {
    wrapper.context = memory.context = &source.context;
  }
  static void Admitted(const ic::GltfFileVerifiedContent& content,
                       std::uint64_t bytes, void* opaque) {
    auto& self = *static_cast<Witness*>(opaque);
    Require(self.hash.attempts == 0 && self.hash.entered.load() == 0,
            "full hash admission precedes every actual hash job launch");
    const auto format =
        self.fixture->source->Format() == ic::GltfFileFormat::kGlb
            ? std::string_view("glb")
            : std::string_view("gltf-json");
    std::uint64_t expected = kHashDomain.size() + kTerminatorBytes +
                             kLengthBytes + format.size() + kLengthBytes;
    for (const auto& file : self.fixture->source->Files())
      expected +=
          kLengthBytes + file.path.size() + kLengthBytes + file.bytes.size();
    for (const auto& file : content.files) expected += file.bytes.size();
    Require(expected == bytes,
            "literal full framed source plus actual staged artifact bytes");
    ++self.admitted;
    self.work_bytes = bytes;
    if (self.fail_original_at_admission) {
      self.wrapper.fail_at = self.wrapper.calls + 1;
      self.wrapper.distinctive = true;
    }
    if (self.cancel_at_admission) self.stop->request_stop();
  }
  static void Prepared(std::span<const std::byte> bytes, void* opaque) {
    auto& self = *static_cast<Witness*>(opaque);
    ++self.prepared;
    self.report_bytes = bytes.size();
    Require(!bytes.empty() && !fs::exists(self.fixture->Destination()),
            "conditional report prepared before actual native commit");
    if (self.action == PreparedAction::kFreezeOriginals)
      self.wrapper.frozen = self.memory.frozen = true;
    else if (self.action == PreparedAction::kCancel)
      self.stop->request_stop();
    else if (self.action == PreparedAction::kOccupy)
      Require(fs::create_directory(self.fixture->Destination()),
              "caller occupies final destination at real barrier");
    else if (self.action == PreparedAction::kThrow)
      throw OriginalFailure{};
    else if (self.action == PreparedAction::kCorruptStagedBytes) {
      // The callback observes only Repair's existing prepared-report barrier.
      // Find the real native stage within this fixture, then modify an existing
      // opaque physical member before the public publisher's final reverify.
      fs::path stage;
      for (const auto& entry : fs::directory_iterator(self.fixture->root))
        if (entry.path() != self.fixture->SourceRoot()) {
          Require(stage.empty() && entry.is_directory(),
                  "sole actual exclusive staging directory before readback");
          stage = entry.path();
        }
      Require(!stage.empty(), "actual stage exists after receipt preparation");
      self.conditional_report.assign(bytes.begin(), bytes.end());
      auto opaque_bytes = test::Read(stage / test::kOpaquePath);
      Require(opaque_bytes.size() == test::kOpaqueBytes &&
                  opaque_bytes[kReadbackFaultByteOffset] == std::byte{0xa7},
              "actual original opaque member before physical corruption");
      opaque_bytes[kReadbackFaultByteOffset] ^= std::byte{1};
      test::Write(stage / test::kOpaquePath, opaque_bytes);
      self.staged_bytes_corrupted = true;
    } else if (self.action == PreparedAction::kReplaceStaging) {
      // Deliberate caller interference observes a real cleanup identity
      // failure; this is not a promise of hostile-filesystem protection.
      fs::path stage;
      for (const auto& entry : fs::directory_iterator(self.fixture->root))
        if (entry.path() != self.fixture->SourceRoot()) {
          Require(stage.empty(), "sole actual exclusive staging directory");
          stage = entry.path();
        }
      Require(!stage.empty(),
              "real publisher staging found only at prepared barrier");
      fs::rename(stage, self.fixture->root / "caller-retained-stage");
      Require(fs::create_directory(stage),
              "caller replaces native staging identity");
      test::Write(stage / "caller-owned.bin",
                  test::Bytes("foreign identity\n"));
      self.stop->request_stop();
    }
  }
  detail::FileWorkflowTestOptions Options() {
    detail::FileWorkflowTestOptions options;
    options.allocation_backend = wrapper.Backend();
    options.memory.allocation_backend = memory.Backend();
    options.memory.source_worker_backend = memory_source.Backend();
    options.memory.report_worker_backend = memory_report.Backend();
    options.hash_worker_backend = hash.Backend();
    options.report_worker_backend = report.Backend();
    options.after_hash_admission = Admitted;
    options.prepared_report = Prepared;
    options.state = this;
    return options;
  }
  void CheckJoined() const {
    hash.CheckJoined();
    report.CheckJoined();
    memory_source.CheckJoined();
    memory_report.CheckJoined();
  }
  void CheckEmpty() const {
    CheckJoined();
    wrapper.CheckEmpty();
    memory.CheckEmpty();
  }
};
repair::GltfDuplicateFileWorkflowResult Run(
    Fixture& source, Witness& witness, bool publication = true,
    bool serial = false,
    const repair::GltfDuplicateFileWorkflowLimits& limits = test::Limits(),
    std::stop_token stop = {}) {
  const auto destination = source.Destination();
  const std::array<ic::GltfFileView, 1> statics{
      {{test::kStaticPath, test::Bytes(test::kStaticBytes)}}};
  const repair::GltfDuplicateFilePublication output{destination,
                                                    test::kReportPath, statics};
  auto work = test::Work(serial);
  work.memory.verification.partition_bytes = kMemoryObserverPartitionBytes;
  work.memory.report.partition_bytes = kMemoryObserverPartitionBytes;
  return detail::RunGltfDuplicateFileWorkflowForTest(
      source.Request(test::kTargets, publication ? &output : nullptr), limits,
      work, source.context, stop, witness.Options());
}
void CheckFilesystem(const Fixture& source, bool occupied = false) {
  for (const auto& entry : fs::directory_iterator(source.root))
    Require(entry.path() == source.SourceRoot() ||
                (occupied && entry.path() == source.Destination()),
            "actual exclusive staging is removed before synchronous "
            "return/rethrow");
  if (!occupied)
    Require(!fs::exists(source.Destination()), "no unsuccessful commit");
  source.CheckSource();
}
void StagedByteReadbackFailure(const fs::path& reports = {}) {
  Fixture source;
  const auto retained = source.context.ActiveTrackedPayload();
  auto witness = std::make_unique<Witness>(source);
  witness->action = PreparedAction::kCorruptStagedBytes;
  {
    const auto result = Run(source, *witness);
    Require(
        witness->prepared == 1 && witness->staged_bytes_corrupted &&
            !witness->conditional_report.empty(),
        "real physical corruption follows actual conditional success receipt");
    Require(result.status == repair::GltfWorkflowStatus::kVerificationFailure &&
                result.diagnostic.stage ==
                    repair::GltfFileWorkflowStage::kPublication &&
                result.diagnostic.code ==
                    repair::GltfFileWorkflowDiagnosticCode::kPublication &&
                result.diagnostic.publisher && result.publication,
            "real publisher byte mismatch retains owning Repair diagnostic");
    const auto& actual = result.publication->diagnostic;
    const auto& primary = *result.diagnostic.publisher;
    Require(actual.status == ic::GltfFileStatus::kPreservationMismatch &&
                actual.stage == ic::GltfFileStage::kInventory &&
                actual.code == ic::GltfFileDiagnosticCode::kResourceBytes &&
                actual.file_index == kReadbackFaultFileOrdinal &&
                actual.byte_offset == kReadbackFaultByteOffset &&
                !actual.resource_kind && !actual.resource_index &&
                !actual.system_error && primary.status == actual.status &&
                primary.stage == actual.stage && primary.code == actual.code &&
                primary.file_index == actual.file_index &&
                primary.byte_offset == actual.byte_offset,
            "actual primary opaque physical file ordinal and earliest mismatch "
            "offset");
    Require(result.outcomes.candidate == repair::CandidateOutcome::accepted &&
                result.outcomes.execution ==
                    repair::GltfWorkflowExecutionOutcome::kFailed &&
                result.outcomes.exit ==
                    repair::GltfWorkflowExitMeaning::kExecutionFailure &&
                result.record && result.record->Memory() &&
                result.record->Memory()->Repair() &&
                std::ranges::equal(result.record->Memory()->Repair()->FaceMap(),
                                   test::kChangedFaceMap) &&
                !result.record->Published() && !result.publication->bundle,
            "accepted candidate and correspondence survive actual readback "
            "failure without published owner");
    Require(!result.record->ReportJson().empty() &&
                !std::ranges::equal(result.record->ReportJson(),
                                    witness->conditional_report) &&
                result.publication->cleanup ==
                    ic::GltfFileCleanupOutcome::kRemoved &&
                !result.publication->cleanup_error,
            "distinct admitted failure report replaces private success draft "
            "and actual stage is removed");
    CheckFilesystem(source);
    witness->CheckJoined();
    if (!reports.empty()) {
      Require(!fs::exists(reports),
              "fresh independent readback report directory");
      const auto directory = reports / kReadbackCase;
      for (const auto& file : source.source->Files())
        test::Write(directory / "source" / file.path, file.bytes);
      test::Write(directory / kReportName, result.record->ReportJson());
      test::Write(reports / kManifestName, test::Bytes(kReadbackManifest));
    }
  }
  witness->CheckEmpty();
  Require(source.context.ActiveTrackedPayload() == retained,
          "readback failure releases all fresh owners and keeps retained "
          "source ledger");
}
void PreallocationRefusals() {
  Fixture source;
  const auto retained = source.context.ActiveTrackedPayload();
  geo::ExecutionContext foreign({test::kWorkers, 1, test::kLedgerBytes});
  auto moving = source.context;
  auto moved = std::move(moving);
  std::stop_source stop;
  stop.request_stop();
  for (const auto* context : {&foreign, &moving}) {
    auto witness = std::make_unique<Witness>(source);
    witness->wrapper.fail_at = witness->memory.fail_at = 1;
    const auto result = detail::RunGltfDuplicateFileWorkflowForTest(
        source.Request(), test::Limits(), test::Work(), *context,
        stop.get_token(), witness->Options());
    Require(
        result.status == repair::GltfWorkflowStatus::kSourceContractFailure &&
            result.diagnostic.code ==
                repair::GltfFileWorkflowDiagnosticCode::kSourceLedger &&
            witness->wrapper.calls == 0 && witness->memory.calls == 0 &&
            source.context.ActiveTrackedPayload() == retained,
        "foreign/equal numeric and moved-from ledger refusal before original "
        "requests and stop");
  }
  auto witness = std::make_unique<Witness>(source);
  auto limits = test::Limits();
  limits.publication.files.max_files = 0;
  const auto invalid = detail::RunGltfDuplicateFileWorkflowForTest(
      source.Request(), limits, test::Work(), foreign, stop.get_token(),
      witness->Options());
  Require(invalid.status == repair::GltfWorkflowStatus::kInvalidConfiguration &&
              witness->wrapper.calls == 0 && witness->memory.calls == 0,
          "nested positive configuration wins even without export before "
          "ledger/stop/allocation");
  limits = test::Limits();
  limits.max_source_files = source.source->Files().size() - kBoundaryDifference;
  const auto count = detail::RunGltfDuplicateFileWorkflowForTest(
      source.Request(), limits, test::Work(), foreign, stop.get_token(),
      witness->Options());
  Require(count.status == repair::GltfWorkflowStatus::kLimitExceeded &&
              count.diagnostic.code ==
                  repair::GltfFileWorkflowDiagnosticCode::kCountLimit &&
              witness->wrapper.calls == 0 && witness->memory.calls == 0,
          "source count extent precedes foreign ledger/stop and requests");
  const auto canceled = detail::RunGltfDuplicateFileWorkflowForTest(
      source.Request(), test::Limits(), test::Work(), moved, stop.get_token(),
      witness->Options());
  Require(canceled.status == repair::GltfWorkflowStatus::kCanceled &&
              witness->wrapper.calls == 0,
          "copied then moved-to same ledger reaches actual initial stop "
          "without allocation");
}
void SuccessAndOwnerLifetime() {
  Fixture source;
  const auto retained = source.context.ActiveTrackedPayload();
  auto witness = std::make_unique<Witness>(source);
  witness->action = PreparedAction::kFreezeOriginals;
  std::optional<repair::GltfDuplicateFileWorkflowRecord> last;
  std::size_t requests = 0;
  {
    auto result = Run(source, *witness);
    Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                result.record && result.record->Published() &&
                witness->admitted == 1 && witness->prepared == 1 &&
                witness->hash.launched > 0 && witness->report.launched > 0 &&
                witness->memory_source.launched > 0 &&
                witness->memory_report.launched > 0,
            "real native hash/receipt/baseline/memory-report roles and actual "
            "successful bundled report");
    witness->CheckJoined();
    const auto bytes = result.record->NewRequestedBytes();
    Require(bytes.wrapper == witness->wrapper.live &&
                bytes.memory.workflow == witness->memory.live &&
                bytes.memory.read == 0 &&
                bytes.retained_source.capped_reader.has_value(),
            "exact own live request accounting, retained capped source, no "
            "fabricated aggregate");
    requests = witness->wrapper.calls + witness->memory.calls;
    last = *result.record;
    auto copy = *last;
    Require(witness->wrapper.calls + witness->memory.calls == requests &&
                copy.ReportJson().data() == last->ReportJson().data(),
            "strong copies share exact report bytes without original requests");
  }
  Require(witness->wrapper.live > 0 && witness->memory.live > 0,
          "original backend charges survive until final record release");
  last.reset();
  witness->CheckEmpty();
  Require(source.context.ActiveTrackedPayload() == retained &&
              witness->wrapper.calls + witness->memory.calls == requests,
          "last-free follows original state and canonical ledger, no "
          "postcommit original request");
}
void RequestFaults() {
  for (const bool serial : {true, false}) {
    for (const bool publication : {false, true}) {
      Fixture source;
      const auto retained = source.context.ActiveTrackedPayload();
      auto baseline = std::make_unique<Witness>(source);
      {
        const auto result = Run(source, *baseline, publication, serial);
        Require(result.status == repair::GltfWorkflowStatus::kSuccess,
                "real original request baseline");
      }
      baseline->CheckEmpty();
      if (publication) fs::remove_all(source.Destination());
      const std::array counts{baseline->wrapper.calls, baseline->memory.calls};
      for (std::size_t owner = 0; owner < counts.size(); ++owner) {
        Require(counts[owner] > 0,
                "each Repair original allocator really reached");
        for (std::size_t ordinal = 1; ordinal <= counts[owner]; ++ordinal) {
          auto witness = std::make_unique<Witness>(source);
          auto& allocator = owner == 0 ? witness->wrapper : witness->memory;
          allocator.fail_at = ordinal;
          bool caught = false;
          try {
            const auto result = Run(source, *witness, publication, serial);
            (void)result;
          } catch (const std::bad_alloc&) {
            caught = true;
          }
          Require(caught && allocator.calls == ordinal,
                  "every baseline original request fault propagates unchanged, "
                  "no fallback allocation");
          witness->CheckEmpty();
          CheckFilesystem(source);
          Require(source.context.ActiveTrackedPayload() == retained,
                  "fault unwinds new wrapper/memory/delegated owners and "
                  "canonical charge");
        }
      }
    }
  }
}
void HashAndReportBounds() {
  Fixture source;
  auto baseline = std::make_unique<Witness>(source);
  {
    const auto result = Run(source, *baseline, true, true);
    Require(result.status == repair::GltfWorkflowStatus::kSuccess,
            "literal hash-work/report baseline");
  }
  fs::remove_all(source.Destination());
  baseline->CheckEmpty();
  Require(baseline->work_bytes > 0 && baseline->report_bytes > 0 &&
              baseline->wrapper.peak > 0,
          "real admitted full work/report and whole own live request peak "
          "observed");
  for (const bool inclusive : {false, true}) {
    auto witness = std::make_unique<Witness>(source);
    auto limits = test::Limits();
    limits.max_hash_input_bytes =
        baseline->work_bytes - (inclusive ? 0 : kBoundaryDifference);
    {
      const auto result = Run(source, *witness, true, true, limits);
      if (inclusive)
        Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                    witness->admitted == 1,
                "inclusive full framed source plus staged physical byte sum");
      else
        Require(result.status == repair::GltfWorkflowStatus::kLimitExceeded &&
                    witness->admitted == 0 &&
                    result.execution.hashing.planned_jobs == 0 &&
                    witness->hash.attempts == 0,
                "minus-one whole hash work refuses before any job, not "
                "per-stream reset");
    }
    if (inclusive) fs::remove_all(source.Destination());
    witness->CheckEmpty();
    CheckFilesystem(source);
  }
  for (const bool inclusive : {false, true}) {
    auto witness = std::make_unique<Witness>(source);
    auto limits = test::Limits();
    limits.max_wrapper_new_requested_bytes =
        baseline->wrapper.peak - (inclusive ? 0 : kBoundaryDifference);
    {
      const auto result = Run(source, *witness, true, true, limits);
      Require(
          (result.status == repair::GltfWorkflowStatus::kSuccess) == inclusive,
          "separate whole wrapper fresh request cap inclusive versus "
          "minus-one");
      if (!inclusive)
        Require(!result.record || !result.record->Published(),
                "wrapper cap refuses commit");
    }
    if (inclusive) fs::remove_all(source.Destination());
    witness->CheckEmpty();
    CheckFilesystem(source);
  }
  auto refused = std::make_unique<Witness>(source);
  auto limits = test::Limits();
  limits.max_report_bytes = 1;
  {
    const auto result = Run(source, *refused, true, true, limits);
    Require(
        result.status == repair::GltfWorkflowStatus::kLimitExceeded &&
            refused->prepared == 0 &&
            (!result.record || result.record->ReportJson().empty()),
        "report extent cannot expose conditional or emergency success bytes");
  }
  refused->CheckEmpty();
  CheckFilesystem(source);
  for (const bool inclusive : {false, true}) {
    auto witness = std::make_unique<Witness>(source);
    auto report_limits = test::Limits();
    report_limits.max_report_bytes =
        baseline->report_bytes - (inclusive ? 0 : kBoundaryDifference);
    {
      const auto result = Run(source, *witness, true, true, report_limits);
      Require(
          (result.status == repair::GltfWorkflowStatus::kSuccess) == inclusive,
          "actual emitted final report inclusive extent versus minus-one");
      if (!inclusive)
        Require(witness->prepared == 0 &&
                    (!result.record || !result.record->Published()),
                "report minus-one never returns conditional success bytes");
    }
    if (inclusive) fs::remove_all(source.Destination());
    witness->CheckEmpty();
    CheckFilesystem(source);
  }
}
void SourceBaselineBounds() {
  Fixture source;
  constexpr std::uint64_t kExpectedCallerBindings = 2;
  const auto expected_bytes =
      test::kJson.size() + test::kBinary.size() + test::kTextureBytes;
  for (const bool inclusive : {false, true}) {
    for (const bool binding_count : {false, true}) {
      auto witness = std::make_unique<Witness>(source);
      auto limits = test::Limits();
      if (binding_count)
        limits.memory.max_source_binding_records =
            kExpectedCallerBindings - (inclusive ? 0 : kBoundaryDifference);
      else
        limits.memory.max_source_protection_bytes =
            expected_bytes - (inclusive ? 0 : kBoundaryDifference);
      {
        const auto result = Run(source, *witness, false, true, limits);
        Require((result.status == repair::GltfWorkflowStatus::kSuccess) ==
                    inclusive,
                "literal retained JSON plus complete caller resource baseline "
                "admission inclusive/minus-one");
        if (!inclusive)
          Require(result.status == repair::GltfWorkflowStatus::kLimitExceeded &&
                      witness->memory.calls == 0 && !result.publication,
                  "baseline bytes/binding refusal before any memory original "
                  "request or delegated stage");
      }
      witness->CheckEmpty();
      CheckFilesystem(source);
    }
  }
  auto witness = std::make_unique<Witness>(source);
  auto limits = test::Limits();
  limits.memory.max_new_requested_bytes =
      3 * test::kStageBytes - kBoundaryDifference;
  {
    const auto result = Run(source, *witness, false, true, limits);
    Require(
        result.status == repair::GltfWorkflowStatus::kBudgetExceeded &&
            result.diagnostic.memory &&
            result.diagnostic.memory->stage ==
                repair::GltfWorkflowStage::kMaterialize &&
            result.diagnostic.memory->code ==
                repair::GltfWorkflowDiagnosticCode::kMaterializationAllowance &&
            !result.publication,
        "unchanged three positive materialization caps do not individually "
        "clamp to one remainder");
  }
  witness->CheckEmpty();
  CheckFilesystem(source);
}
void CancellationAndNativeFaults() {
  for (const auto point : {PreparedAction::kCancel, PreparedAction::kOccupy,
                           PreparedAction::kThrow}) {
    Fixture source;
    auto witness = std::make_unique<Witness>(source);
    std::stop_source stop;
    witness->stop = &stop;
    witness->action = point;
    bool caught = false;
    try {
      const auto result =
          Run(source, *witness, true, false, test::Limits(), stop.get_token());
      Require(
          point != PreparedAction::kThrow && result.publication &&
              result.outcomes.candidate == repair::CandidateOutcome::accepted &&
              (!result.record || !result.record->Published()),
          "actual prepared-stage failure retains accepted candidate, no "
          "published owner");
      if (point == PreparedAction::kCancel)
        Require(result.status == repair::GltfWorkflowStatus::kCanceled &&
                    result.publication->outcome ==
                        ic::GltfFilePublicationOutcome::kCanceled &&
                    (!result.record || result.record->ReportJson().empty()),
                "real publisher cancellation and no exposed conditional "
                "success report");
      else
        Require(result.publication->diagnostic.code ==
                    ic::GltfFileDiagnosticCode::kDestinationExists,
                "real last native no-replace action refuses newly occupied "
                "destination");
    } catch (const OriginalFailure&) {
      caught = true;
    }
    Require(
        caught == (point == PreparedAction::kThrow) && witness->prepared == 1,
        "distinct provider exception identity resumes after real publisher "
        "cleanup");
    witness->CheckEmpty();
    CheckFilesystem(source, point == PreparedAction::kOccupy);
  }
  enum class CancelPoint { kAdmission, kHashNative, kReportNative };
  for (const auto point : {CancelPoint::kAdmission, CancelPoint::kHashNative,
                           CancelPoint::kReportNative}) {
    Fixture source;
    auto witness = std::make_unique<Witness>(source);
    std::stop_source stop;
    witness->stop = &stop;
    if (point == CancelPoint::kHashNative)
      witness->hash.cancel = &stop;
    else if (point == CancelPoint::kReportNative)
      witness->report.cancel = &stop;
    else
      witness->cancel_at_admission = true;
    {
      const auto result =
          Run(source, *witness, true, point == CancelPoint::kAdmission,
              test::Limits(), stop.get_token());
      Require(result.status == repair::GltfWorkflowStatus::kCanceled &&
                  witness->admitted == 1 && witness->prepared == 0 &&
                  (!result.record || result.record->ReportJson().empty()),
              "bounded hash/receipt checkpoints observe actual "
              "admission/native-entry "
              "stop before receipt");
    }
    witness->CheckEmpty();
    CheckFilesystem(source);
  }
  enum class WorkerRole { kHash, kReport, kSource, kMemoryReport };
  for (const auto role : {WorkerRole::kHash, WorkerRole::kReport,
                          WorkerRole::kSource, WorkerRole::kMemoryReport}) {
    Fixture source;
    auto witness = std::make_unique<Witness>(source);
    const std::array workers{&witness->hash, &witness->report,
                             &witness->memory_source, &witness->memory_report};
    auto& worker = *workers[static_cast<std::size_t>(role)];
    worker.fail_at = kPartialLaunchOrdinal;
    bool caught = false;
    try {
      const auto result = Run(source, *witness);
      (void)result;
    } catch (const LaunchFailure&) {
      caught = true;
    }
    Require(caught && worker.launched == kPartialLaunchOrdinal - 1,
            "actual second launch fails, original exception escapes only after "
            "first native join");
    witness->CheckEmpty();
    CheckFilesystem(source);
  }
  {
    Fixture source;
    const auto retained = source.context.ActiveTrackedPayload();
    auto witness = std::make_unique<Witness>(source);
    witness->fail_original_at_admission = true;
    bool caught = false;
    try {
      const auto result = Run(source, *witness);
      (void)result;
    } catch (const OriginalFailure&) {
      caught = true;
    }
    Require(caught && witness->admitted == 1 && witness->prepared == 0 &&
                witness->wrapper.calls == witness->wrapper.fail_at,
            "distinct original backend exception within actual provider "
            "propagates unchanged after cleanup");
    witness->CheckEmpty();
    CheckFilesystem(source);
    Require(source.context.ActiveTrackedPayload() == retained,
            "provider backend exception returns every fresh charge after real "
            "staging cleanup");
  }
  {
    Fixture source;
    auto witness = std::make_unique<Witness>(source);
    std::stop_source stop;
    witness->stop = &stop;
    witness->action = PreparedAction::kReplaceStaging;
    {
      const auto result =
          Run(source, *witness, true, true, test::Limits(), stop.get_token());
      Require(result.status == repair::GltfWorkflowStatus::kCanceled &&
                  result.publication &&
                  result.publication->diagnostic.code ==
                      ic::GltfFileDiagnosticCode::kStopRequested &&
                  result.publication->cleanup ==
                      ic::GltfFileCleanupOutcome::kFailed &&
                  result.publication->cleanup_error &&
                  (!result.record || result.record->ReportJson().empty()) &&
                  !fs::exists(source.Destination()),
              "real cleanup failure remains separate from actual primary "
              "cancellation, no conditional report exposure");
    }
    witness->CheckEmpty();
    source.CheckSource();
    Require(
        fs::exists(source.root / "caller-retained-stage"),
        "caller-retained changed staging is visible, not mislabeled removed");
  }
}
void SharedLedgerAndWorkers() {
  Fixture source;
  const auto retained = source.context.ActiveTrackedPayload();
  {
    auto occupied = geo::TryReserveWorkers(source.context, test::kWorkers);
    Require(occupied.Count() == test::kWorkers,
            "actual shared worker ledger occupied");
    auto witness = std::make_unique<Witness>(source);
    {
      const auto result = Run(source, *witness);
      Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                  witness->hash.attempts == 0 &&
                  witness->report.attempts == 0 &&
                  result.execution.hashing.caller_jobs > 0 &&
                  result.execution.report_copy.caller_jobs > 0,
              "occupied shared workers cause truthful caller fallback in both "
              "own roles");
    }
    witness->CheckEmpty();
    fs::remove_all(source.Destination());
    Require(occupied.Count() == test::kWorkers,
            "unrelated occupied lease preserved");
  }
  {
    const auto reserve = static_cast<std::size_t>(
        test::kLedgerBytes - retained - kBoundaryDifference);
    auto occupied = geo::TryReservePayload(source.context, reserve);
    Require(occupied.status == geo::ReservationStatus::kAccepted,
            "actual shared payload ledger nearly full");
    auto witness = std::make_unique<Witness>(source);
    {
      const auto result = Run(source, *witness);
      Require(result.status == repair::GltfWorkflowStatus::kBudgetExceeded &&
                  witness->wrapper.calls == 0,
              "canonical ledger admission precedes original backend, "
              "independently of wrapper cap");
    }
    witness->CheckEmpty();
    CheckFilesystem(source);
    Require(occupied.lease.Bytes() == reserve &&
                source.context.ActiveTrackedPayload() == retained + reserve,
            "failed new call preserves unrelated shared payload charge");
  }
  auto first = std::make_unique<Witness>(source);
  Gate gate;
  first->hash.gate = &gate;
  std::optional<repair::GltfDuplicateFileWorkflowResult> first_result;
  std::exception_ptr first_error;
  std::thread worker([&] {
    try {
      first_result = Run(source, *first);
    } catch (...) {
      first_error = std::current_exception();
    }
  });
  struct Finish {
    Gate& gate;
    std::thread& worker;
    ~Finish() noexcept {
      gate.Release();
      if (worker.joinable()) worker.join();
    }
  } finish{gate, worker};
  gate.Wait();
  const auto first_live = source.context.ActiveTrackedPayload();
  auto second = std::make_unique<Witness>(source);
  {
    const auto result = Run(source, *second, false);
    Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                second->memory_source.attempts == 0 &&
                second->memory_report.attempts == 0,
            "independent concurrent no-export operation falls back while "
            "actual first hash workers hold slots");
  }
  second->CheckEmpty();
  Require(source.context.ActiveTrackedPayload() == first_live,
          "second call returns its own charge and preserves paused first call");
  gate.Release();
  worker.join();
  if (first_error) std::rethrow_exception(first_error);
  Require(first_result &&
              first_result->status == repair::GltfWorkflowStatus::kSuccess,
          "paused first workflow resumes and commits after concurrent borrower "
          "joins");
  first_result.reset();
  first->CheckEmpty();
  Require(source.context.ActiveTrackedPayload() == retained,
          "concurrent own caps return canonical ledger to retained source");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == kWriteReadbackArgument) {
      StagedByteReadbackFailure(fs::path(argv[2]));
      std::cout
          << "actual prepared-receipt physical readback failure report saved\n";
      return 0;
    }
    Require(argc == 1, "known independent request fixture invocation");
    StagedByteReadbackFailure();
    PreallocationRefusals();
    SuccessAndOwnerLifetime();
    RequestFaults();
    HashAndReportBounds();
    SourceBaselineBounds();
    CancellationAndNativeFaults();
    SharedLedgerAndWorkers();
    std::cout << "independent Repair original requests, actual native joins, "
                 "full hash admission and real publication barriers passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
