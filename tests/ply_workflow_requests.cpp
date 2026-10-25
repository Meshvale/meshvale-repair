// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/ply_workflow.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "materialized_duplicate_allocation.h"
#include "ply_interchange_fixture.h"
#include "ply_workflow_observations.h"
#include "ply_workflow_test_internal.h"

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace rep = meshvale::repair;
namespace detail = rep::ply_workflow_detail;
namespace f = ic::ply_fixture;
namespace obs = rep::test_ply_observations;
namespace fs = std::filesystem;
using f::Require;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kLedgerBytes = 256 * kMiB;
constexpr std::uint64_t kCountCap = 65536;
constexpr std::uint64_t kNameBytes = 256;
constexpr std::uint64_t kPathCodeUnits = 4096;
constexpr std::uint64_t kIoBlockBytes = 64;
constexpr std::uint64_t kPartitionBytes = 128;
constexpr std::uint64_t kCancellationBytes = 16;
constexpr std::uint64_t kMaxJobs = 64;
constexpr std::uint64_t kParallelMinBytes = 1;
constexpr std::size_t kParallelMinVertices = 1;
constexpr std::uint64_t kSerialJobs = 1;
constexpr std::uint64_t kInputIdBytes = 128;
constexpr std::uint64_t kWrapperBytes = 64 * kMiB;
constexpr std::uint64_t kFileBytes = 64 * kMiB;
constexpr std::uint64_t kFixtureReadBytes = 2 * kMiB;
constexpr std::uint64_t kRepairBytes = 32 * kMiB;
constexpr std::uint64_t kSerialMinBytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr std::uint64_t kStopCorner = 2;
constexpr std::size_t kSourceVertices = 8;
constexpr std::size_t kSourceFaces = 3;
constexpr std::size_t kCandidateFaces = 2;
constexpr std::size_t kCandidateCorners = 7;
constexpr std::size_t kProperties = 14;
constexpr std::uint32_t kWorkers = 4;
constexpr std::size_t kPublicationMembers = 2;
constexpr std::byte kMutationMask{0x01};
constexpr std::string_view kInputId = "ply-request-controls";
constexpr std::string_view kSourceName = "source.ply";
constexpr std::string_view kBundleName = "bundle";
constexpr std::string_view kReceiptName = "meshvale-report.json";
constexpr std::string_view kReportName = "report.json";
constexpr std::string_view kNativeName = "native.json";
constexpr std::string_view kCapturedName = "captured.ply";
constexpr std::string_view kSentinelName = "sentinel.bin";
constexpr std::string_view kBeforeName = "stage-before.ply";
constexpr std::string_view kAfterName = "stage-after.ply";
constexpr std::string_view kPreparedName = "prepared-report.json";
constexpr std::string_view kReportsArgument = "--write-reports";
constexpr std::string_view kRootPrefix = "ply-workflow-requests-";
constexpr std::string_view kExistingCase = "existing-destination";
constexpr std::string_view kStopCase = "interior-stop";
constexpr std::string_view kCorruptionCase = "staged-entry-corruption";
constexpr std::array kSentinel{std::byte{0x53}, std::byte{0x00},
                               std::byte{0xff}, std::byte{0x31}};
constexpr std::array kTargets{rep::DuplicateTarget{0, 1}};
// Caller assertions for the independently specified measurement corpus only.
constexpr std::array kDeclarations{
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex,
                                      "sample_i8"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex,
                                      "sample_u8"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex,
                                      "sample_i16"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex,
                                      "sample_u16"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex,
                                      "sample_i32"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex,
                                      "sample_u32"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex,
                                      "sample_f64"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::vertex, "samples"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::face, "weight"},
    rep::RowLocalAttributeDeclaration{geo::AttributeDomain::face, "bins"}};
static_assert(kCancellationBytes / sizeof(geo::index_t) == kStopCorner);

ic::PlyFileLimits FileLimits() {
  return {kMiB,       kMiB,       kPublicationMembers * kMiB,
          kNameBytes, kNameBytes, kPathCodeUnits,
          kFileBytes};
}
ic::PlyFileWorkPolicy FileWork() {
  return {kIoBlockBytes, kParallelMinBytes, kPartitionBytes, kCancellationBytes,
          kMaxJobs};
}
rep::PlyDuplicateFileWorkflowLimits Limits() {
  return {kWrapperBytes,
          kInputIdBytes,
          kMiB,
          kMiB,
          kPublicationMembers * kMiB,
          kCountCap,
          kCountCap,
          kCountCap,
          {kCountCap,
           kCountCap,
           f::kDescriptorLimit,
           kCountCap,
           kRepairBytes,
           {kCountCap, f::kRequestLimit},
           f::MeshLimits()},
          f::WriteLimits(),
          f::ReadLimits(),
          {FileLimits(), {kCountCap, f::kRequestLimit}, f::ReadLimits()}};
}
rep::PlyDuplicateFileWorkflowWorkPolicy Work() {
  const rep::PlyByteWorkPolicy bytes{kParallelMinBytes, kPartitionBytes,
                                     kCancellationBytes, kMaxJobs};
  return {f::Work().geometry,
          f::Work(),
          f::Work(),
          bytes,
          bytes,
          bytes,
          {FileWork(), f::Work().geometry, f::Work()}};
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  Require(!fs::exists(path), "fixture creates a new owned file");
  std::ofstream stream(path, std::ios::binary);
  Require(stream.good(), "fixture output opens");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(stream.good(), "fixture output writes and closes");
}
std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  Require(stream.good(), "fixture input opens");
  const auto size = stream.tellg();
  Require(size >= 0 && size <= static_cast<std::streamoff>(kFixtureReadBytes),
          "bounded fixture input");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(stream.good() || (bytes.empty() && stream.eof()),
          "complete fixture input");
  stream.close();
  Require(!stream.fail(), "fixture input closes");
  return bytes;
}
std::set<fs::path> Names(const fs::path& directory) {
  std::set<fs::path> names;
  for (const auto& entry : fs::directory_iterator(directory))
    names.insert(entry.path().filename());
  return names;
}
ic::PlyFileReadResult Capture(const fs::path& path,
                              const geo::ExecutionContext& context) {
  return ic::ReadPlyFileAsset(path, {FileLimits(), f::ReadLimits()},
                              {FileWork(), f::Work()}, context);
}
void Joined(const geo::MaterializationExecutionStats& stats) {
  Require(stats.launched_workers == stats.joined_workers,
          "actual launched workers all joined");
}
void Joined(const ic::PlyExecution& stats) {
  Joined(stats.codec);
  Joined(stats.materialization);
  Joined(stats.inspection);
}
void Joined(const rep::PlyWorkflowExecution& stats) {
  const auto& repair = stats.repair;
  for (const auto& item :
       {repair.source_inspection, repair.eligibility, repair.comparison,
        repair.selection, repair.correspondence, repair.candidate_inspection,
        repair.verification, repair.directed_verification,
        stats.source_protection, stats.repair_correspondence,
        stats.canonical_preservation, stats.typed_properties, stats.wire_schema,
        stats.reload_verification, stats.source_verification,
        stats.staged_candidate, stats.hashing, stats.report_copy})
    Joined(item);
  Joined(stats.writing);
  Joined(stats.reload);
}
void Joined(const ic::PlyFileExecution& stats) {
  for (const auto& item :
       {stats.capture, stats.copying, stats.byte_verification,
        stats.mesh_verification, stats.expected_inspection})
    Joined(item);
  Joined(stats.ply);
}
void Released(const geo::ExecutionContext& context) {
  Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
          "all owners released and workers joined");
}
void Facts(const rep::PlyDuplicateFileWorkflowRecord& record, bool verified) {
  const auto* repair = record.RepairFacts();
  Require(repair && repair->Outcome() == rep::CandidateOutcome::accepted,
          "one completed genuine accepted Repair owner retained");
  f::CheckMesh(repair->Source());
  f::CheckMesh(repair->Candidate(), {.changed = true});
  Require(std::ranges::equal(repair->FaceMap(), f::kFaceMap) &&
              std::ranges::equal(repair->CornerMap(), f::kCornerMap) &&
              std::ranges::equal(repair->SourceFaces(), f::kSourceFaces) &&
              std::ranges::equal(repair->SourceCorners(), f::kSourceCorners),
          "all four actual maps equal the literal corpus");
  const auto* facts = record.WriteFacts();
  Require(facts && facts->source_vertex_count == kSourceVertices &&
              facts->source_face_count == kSourceFaces &&
              facts->source_encoding == ic::PlyEncoding::kAscii &&
              facts->encoding == ic::PlyEncoding::kBinaryBigEndian &&
              facts->vertex_count == kSourceVertices &&
              facts->face_count == kCandidateFaces &&
              facts->corner_count == kCandidateCorners &&
              facts->property_count == kProperties &&
              facts->rewrite_policy == ic::PlyRewritePolicy::kCanonical &&
              facts->correspondence == ic::PlyCorrespondence::kIdentity,
          "actual writer facts survive the later failure");
  Require(record.Output().has_value() == verified &&
              record.Reload().has_value() == verified && !record.Published(),
          "only verified memory owners exposed, no published owner");
  if (verified) f::Check(*record.Reload(), {.changed = true});
  Joined(record.Execution());
}
const rep::PlyWorkflowCoverage& Coverage(
    const rep::PlyDuplicateFileWorkflowRecord& record,
    rep::PlyWorkflowCheck check) {
  for (const auto& item : record.Coverage())
    if (item.check == check) return item;
  throw std::runtime_error("missing actual coverage item");
}
void Save(const fs::path& directory, const ic::PlyFileAsset& source,
          const rep::PlyDuplicateFileWorkflowResult& result, bool report) {
  Require(result.record.has_value(), "actual record available for observation");
  obs::WriteNative(directory / kNativeName, source, result);
  Write(directory / kCapturedName, source.EntryBytes());
  if (report) {
    Require(!result.record->ReportJson().empty(), "real final failure report");
    Write(directory / kReportName, result.record->ReportJson());
  }
}
void Prepared(std::span<const std::byte>, void* state) {
  ++*static_cast<std::size_t*>(state);
}
void ExistingDestination(const fs::path& root) {
  const auto directory = root / kExistingCase;
  Require(fs::create_directory(directory), "new existing-destination case");
  const auto source_path = directory / kSourceName;
  const auto bundle = directory / kBundleName;
  Write(source_path, f::Wire(ic::PlyEncoding::kAscii));
  Require(fs::create_directory(bundle), "real pre-existing destination");
  Write(bundle / kSentinelName, kSentinel);
  const auto initial = Names(directory);
  geo::ExecutionContext ledger({kWorkers, kParallelMinVertices, kLedgerBytes});
  std::optional<rep::PlyDuplicateFileWorkflowRecord> retained;
  {
    auto context = ledger;
    const auto captured = Capture(source_path, context);
    Require(captured.asset.has_value(), "real typed source captured");
    Joined(captured.execution);
    std::size_t provider_calls = 0;
    detail::WorkflowTestOptions testing;
    testing.prepared_report = Prepared;
    testing.state = &provider_calls;
    const rep::PlyDuplicateFileWorkflowRequest request{
        *captured.asset, kInputId,
        kTargets,        f::Options(ic::PlyEncoding::kBinaryBigEndian),
        kDeclarations,   rep::PlyWorkflowPublication{bundle, kReceiptName}};
    const auto result = detail::RunPlyDuplicateFileWorkflowForTest(
        request, Limits(), Work(), {}, testing);
    Require(
        result.record &&
            result.status == rep::PlyWorkflowStatus::kDelegatedFailure &&
            result.diagnostic.stage == rep::PlyWorkflowStage::kPublication &&
            result.diagnostic.code ==
                rep::PlyWorkflowDiagnosticCode::kPublication &&
            result.outcomes.execution ==
                rep::PlyWorkflowExecutionOutcome::kFailed &&
            result.outcomes.candidate == rep::CandidateOutcome::accepted &&
            result.outcomes.exit ==
                rep::PlyWorkflowExitMeaning::kExecutionFailure &&
            result.outcomes.publication ==
                rep::PlyWorkflowPublicationOutcome::kFailed,
        "actual pre-existing destination is a typed publication failure");
    Require(result.publication &&
                result.publication->outcome ==
                    ic::PlyFilePublicationOutcome::kFailed &&
                !result.publication->bundle &&
                result.publication->diagnostic.status ==
                    ic::PlyFileStatus::kDestinationExists &&
                result.publication->diagnostic.stage ==
                    ic::PlyFileStage::kPathPlanning &&
                result.publication->diagnostic.code ==
                    ic::PlyFileDiagnosticCode::kDestinationExists &&
                result.publication->cleanup ==
                    ic::PlyFileCleanupOutcome::kNotRequired,
            "actual native no-replace refusal before staging");
    Require(provider_calls == 0 && Names(directory) == initial &&
                Names(bundle) == std::set<fs::path>{fs::path(kSentinelName)} &&
                std::ranges::equal(Read(bundle / kSentinelName), kSentinel),
            "provider never ran, no stage or foreign destination mutation");
    Facts(*result.record, true);
    Require(Coverage(*result.record, rep::PlyWorkflowCheck::kStagedCandidate)
                        .state == rep::PlyWorkflowCoverageState::kSkipped &&
                Coverage(*result.record,
                         rep::PlyWorkflowCheck::kStagedPhysicalMembers)
                        .state == rep::PlyWorkflowCoverageState::kSkipped,
            "no staged success invented before provider");
    Joined(result.publication->execution);
    Save(directory, *captured.asset, result, true);
    retained.emplace(*result.record);
  }
  Facts(*retained, true);
  Require(ledger.ActiveTrackedPayload() > 0 && ledger.ActiveWorkers() == 0,
          "retained result survives original source/request/context/result");
  retained.reset();
  Released(ledger);
}

struct StopState {
  std::stop_source stop;
  std::array<std::uint64_t, 2> observed{};
  std::size_t count = 0;
};
void StopAtCorner(detail::VerificationPart part, std::uint64_t ordinal,
                  void* state) {
  if (part != detail::VerificationPart::kSourceCorners) return;
  auto& observed = *static_cast<StopState*>(state);
  Require(observed.count < observed.observed.size(),
          "bounded actual checkpoints");
  observed.observed[observed.count++] = ordinal;
  if (ordinal == kStopCorner)
    Require(observed.stop.request_stop(),
            "first genuine interior stop request");
}
void InteriorStop(const fs::path& root) {
  const auto directory = root / kStopCase;
  Require(fs::create_directory(directory), "new interior-stop case");
  const auto path = directory / kSourceName;
  const auto bundle = directory / kBundleName;
  Write(path, f::Wire(ic::PlyEncoding::kAscii));
  geo::ExecutionContext context({kWorkers, kParallelMinVertices, kLedgerBytes});
  {
    const auto captured = Capture(path, context);
    Require(captured.asset.has_value(), "real stop source capture");
    StopState state;
    detail::WorkflowTestOptions testing;
    testing.verification_checkpoint = StopAtCorner;
    testing.state = &state;
    auto work = Work();
    work.verification = {kSerialMinBytes, kPartitionBytes, kCancellationBytes,
                         kSerialJobs};
    const rep::PlyDuplicateFileWorkflowRequest request{
        *captured.asset, kInputId,
        kTargets,        f::Options(ic::PlyEncoding::kBinaryBigEndian),
        kDeclarations,   rep::PlyWorkflowPublication{bundle, kReceiptName}};
    const auto result = detail::RunPlyDuplicateFileWorkflowForTest(
        request, Limits(), work, state.stop.get_token(), testing);
    Require(
        state.count == state.observed.size() &&
            state.observed == std::array<std::uint64_t, 2>{0, kStopCorner} &&
            result.record &&
            result.status == rep::PlyWorkflowStatus::kCanceled &&
            result.diagnostic.stage == rep::PlyWorkflowStage::kVerify &&
            result.diagnostic.code ==
                rep::PlyWorkflowDiagnosticCode::kStopRequested &&
            result.outcomes.exit == rep::PlyWorkflowExitMeaning::kCanceled,
        "real source corners0/1 checked before ordinal2 stop at16 bytes");
    Require(!result.publication && !fs::exists(bundle) &&
                !result.record->InputSha256() &&
                result.record->Execution().hashing.caller_jobs == 0,
            "cancellation precedes hashing and actual publisher");
    Facts(*result.record, false);
    Joined(captured.execution);
    Save(directory, *captured.asset, result, false);
    // No JSON is required under an already requested stop. This sidecar comes
    // only from real typed getters, not a synthesized cancellation receipt.
  }
  Released(context);
}

struct CorruptionState {
  fs::path directory;
  fs::path destination;
  fs::path stage;
  std::size_t calls = 0;
  std::vector<std::byte> before;
  std::vector<std::byte> after;
};
void CorruptPrepared(std::span<const std::byte> report, void* data) {
  auto& state = *static_cast<CorruptionState*>(data);
  Require(++state.calls == 1 && !report.empty(),
          "one real prepared conditional report");
  for (const auto& item : fs::directory_iterator(state.directory)) {
    if (!item.is_directory()) continue;
    Require(state.stage.empty() && item.path() != state.destination,
            "exactly one actual owned stage discovered");
    state.stage = item.path();
  }
  Require(!state.stage.empty(), "actual stage exists during provider");
  const auto entry = state.stage / kSourceName;
  state.before = Read(entry);
  state.after = state.before;
  Require(!state.after.empty(), "nonempty actual staged entry");
  state.after.back() ^= kMutationMask;
  std::ofstream stream(entry, std::ios::binary | std::ios::trunc);
  Require(stream.good(), "fixture opens its actual stage entry");
  stream.write(reinterpret_cast<const char*>(state.after.data()),
               static_cast<std::streamsize>(state.after.size()));
  stream.close();
  Require(stream.good() && Read(entry) == state.after &&
              state.before.size() == state.after.size() &&
              state.before != state.after,
          "real equal-size staged byte mutation closed and observed");
  Write(state.directory / kBeforeName, state.before);
  Write(state.directory / kAfterName, state.after);
  Write(state.directory / kPreparedName, report);
}
void StagedCorruption(const fs::path& root) {
  const auto directory = root / kCorruptionCase;
  Require(fs::create_directory(directory), "new staged-corruption case");
  const auto path = directory / kSourceName;
  const auto bundle = directory / kBundleName;
  Write(path, f::Wire(ic::PlyEncoding::kAscii));
  geo::ExecutionContext context({kWorkers, kParallelMinVertices, kLedgerBytes});
  {
    const auto captured = Capture(path, context);
    Require(captured.asset.has_value(), "real corruption source capture");
    CorruptionState state{directory, bundle, {}, 0, {}, {}};
    detail::WorkflowTestOptions testing;
    testing.prepared_report = CorruptPrepared;
    testing.state = &state;
    const rep::PlyDuplicateFileWorkflowRequest request{
        *captured.asset, kInputId,
        kTargets,        f::Options(ic::PlyEncoding::kBinaryBigEndian),
        kDeclarations,   rep::PlyWorkflowPublication{bundle, kReceiptName}};
    const auto result = detail::RunPlyDuplicateFileWorkflowForTest(
        request, Limits(), Work(), {}, testing);
    Require(
        state.calls == 1 && result.record &&
            result.status == rep::PlyWorkflowStatus::kDelegatedFailure &&
            result.outcomes.execution ==
                rep::PlyWorkflowExecutionOutcome::kFailed &&
            result.outcomes.candidate == rep::CandidateOutcome::accepted &&
            result.outcomes.exit ==
                rep::PlyWorkflowExitMeaning::kExecutionFailure &&
            result.diagnostic.stage == rep::PlyWorkflowStage::kPublication &&
            result.publication &&
            result.publication->outcome ==
                ic::PlyFilePublicationOutcome::kFailed &&
            !result.publication->bundle &&
            result.publication->diagnostic.status ==
                ic::PlyFileStatus::kPreservationMismatch &&
            result.publication->diagnostic.stage ==
                ic::PlyFileStage::kInventory &&
            result.publication->diagnostic.code ==
                ic::PlyFileDiagnosticCode::kByteMismatch &&
            result.publication->cleanup ==
                ic::PlyFileCleanupOutcome::kRemoved &&
            !result.publication->cleanup_error && !fs::exists(bundle) &&
            !fs::exists(state.stage),
        "actual inventory byte mismatch prevents commit and removes owned "
        "stage");
    Facts(*result.record, true);
    Require(
        std::ranges::equal(result.record->Output()->Bytes(), state.before) &&
            Coverage(*result.record, rep::PlyWorkflowCheck::kStagedCandidate)
                    .passed == true &&
            Coverage(*result.record,
                     rep::PlyWorkflowCheck::kStagedPhysicalMembers)
                    .state == rep::PlyWorkflowCoverageState::kPerformed &&
            Coverage(*result.record,
                     rep::PlyWorkflowCheck::kStagedPhysicalMembers)
                    .passed == false &&
            Coverage(*result.record,
                     rep::PlyWorkflowCheck::kStagedPhysicalMembers)
                    .reason == rep::PlyWorkflowDiagnosticCode::kPublication,
        "real staged candidate preceded mutation, final physical check failed");
    Joined(captured.execution);
    Joined(result.publication->execution);
    Save(directory, *captured.asset, result, true);
  }
  Released(context);
}
void PointOnly(const fs::path& root) {
  constexpr std::string_view kCase = "point-only";
  constexpr f::Shape kShape{.omit_face = true};
  constexpr std::size_t kPointProperties = 11;
  const auto directory = root / kCase;
  Require(fs::create_directory(directory), "new point-only case");
  const auto source_path = directory / kSourceName;
  const auto bundle = directory / kBundleName;
  const auto wire = f::Wire(ic::PlyEncoding::kAscii, kShape);
  Write(source_path, wire);
  geo::ExecutionContext ledger({kWorkers, kParallelMinVertices, kLedgerBytes});
  std::optional<rep::PlyDuplicateFileWorkflowRecord> retained;
  {
    auto context = ledger;
    const auto captured = Capture(source_path, context);
    Require(captured.asset.has_value(), "real point-only source captured");
    f::Check(captured.asset->Document(), kShape);
    const rep::PlyDuplicateFileWorkflowRequest request{
        *captured.asset,
        kInputId,
        {},
        f::Options(ic::PlyEncoding::kBinaryBigEndian),
        {},
        rep::PlyWorkflowPublication{bundle, kReceiptName}};
    const auto result =
        rep::RunPlyDuplicateFileWorkflow(request, Limits(), Work());
    Require(result.record && result.publication &&
                result.status == rep::PlyWorkflowStatus::kSuccess &&
                result.outcomes.execution ==
                    rep::PlyWorkflowExecutionOutcome::kCompleted &&
                result.outcomes.candidate == rep::CandidateOutcome::unchanged &&
                result.outcomes.profile ==
                    rep::PlyWorkflowProfileOutcome::kPassed &&
                result.outcomes.publication ==
                    rep::PlyWorkflowPublicationOutcome::kPublished &&
                result.outcomes.exit == rep::PlyWorkflowExitMeaning::kSuccess &&
                result.publication->outcome ==
                    ic::PlyFilePublicationOutcome::kPublished,
            "empty targets publish a genuine unchanged point-only owner");
    const auto& record = *result.record;
    const auto* repair = record.RepairFacts();
    Require(repair && repair->Outcome() == rep::CandidateOutcome::unchanged &&
                repair->FaceMap().empty() && repair->CornerMap().empty() &&
                repair->SourceFaces().empty() &&
                repair->SourceCorners().empty(),
            "all four real maps exist and are empty, vertex rows are retained");
    f::CheckMesh(repair->Source(), kShape);
    f::CheckMesh(repair->Candidate(), kShape);
    Require(
        record.Output() && record.Reload() && record.Published() &&
            record.WriteFacts() && record.InputSha256() &&
            record.WriteFacts()->property_count == kPointProperties &&
            record.WriteFacts()->source_vertex_count == kSourceVertices &&
            record.WriteFacts()->source_face_count == 0 &&
            record.WriteFacts()->face_count == 0 &&
            record.WriteFacts()->corner_count == 0,
        "point rows and eleven source properties reach actual writer facts");
    f::CheckOutput(*record.Output(), ic::PlyEncoding::kAscii,
                   ic::PlyEncoding::kBinaryBigEndian, kShape, kShape);
    f::Check(*record.Reload(), kShape);
    f::Check(record.Published()->Document(), kShape);
    Require(record.Reload()->Encoding() == ic::PlyEncoding::kBinaryBigEndian &&
                record.Published()->Document().Encoding() ==
                    ic::PlyEncoding::kBinaryBigEndian &&
                std::ranges::equal(record.Published()->EntryBytes(),
                                   record.Output()->Bytes()) &&
                record.Published()->ReceiptBytes() &&
                std::ranges::equal(*record.Published()->ReceiptBytes(),
                                   record.ReportJson()) &&
                Read(bundle / kSourceName) ==
                    f::Wire(ic::PlyEncoding::kBinaryBigEndian, kShape, true) &&
                std::ranges::equal(Read(bundle / kReceiptName),
                                   record.ReportJson()) &&
                Names(bundle) == std::set<fs::path>{fs::path(kSourceName),
                                                    fs::path(kReceiptName)} &&
                Read(source_path) == wire,
            "physical canonical big-endian entry and exact receipt, flat two "
            "members");
    Joined(captured.execution);
    Joined(result.execution);
    Joined(result.publication->execution);
    Save(directory, *captured.asset, result, true);
    retained = record;
  }
  Require(retained && ledger.ActiveWorkers() == 0 &&
              ledger.ActiveTrackedPayload() > 0,
          "immutable point-only record outlives source and original context");
  f::Check(retained->Published()->Document(), kShape);
  retained.reset();
  Released(ledger);
}

void CheckPropertySource(const ic::PlyDocument& document) {
  f::Check(document, {.different_face = true});
}

void CheckWindingSource(const ic::PlyDocument& document) {
  constexpr std::array<geo::index_t, 11> kReversedCorners{0, 1, 2, 3, 2, 1,
                                                          0, 3, 4, 5, 6};
  constexpr std::array<std::uint32_t, 4> kEqualBins{7, 4294967295, 7,
                                                    4294967295};
  constexpr std::array<geo::index_t, 4> kBinOffsets{0, 2, 4, 4};
  constexpr std::array<float, 3> kEqualWeights{0.5f, 0.5f, -0.0f};
  Require(document.Inspection().IsStorageValid(),
          "reversed loop storage is valid");
  f::CheckSchema(document);
  const auto view = document.Mesh().View();
  Require(view.positions.size() == f::kPositions.size() &&
              std::ranges::equal(view.face_offsets, f::kOffsets) &&
              std::ranges::equal(view.corner_vertices, kReversedCorners),
          "literal reversed directed cycle, unchanged other face loops");
  for (std::size_t i = 0; i < f::kPositions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) ==
                std::bit_cast<std::uint64_t>(f::kPositions[i]),
            "winding case retains every position bit including signed zero");
  const auto& bins = f::Attribute(view, geo::AttributeDomain::face, "bins");
  f::Values(bins, kEqualBins, false);
  f::Values(f::Attribute(view, geo::AttributeDomain::face, "weight"),
            kEqualWeights, false);
  Require(bins.offsets && std::ranges::equal(*bins.offsets, kBinOffsets),
          "face property rows agree; only the directed loop differs");
}

void CheckDistinctSource(const ic::PlyDocument& document) {
  constexpr std::array<double, 12> kPositions{0, 0, 0, 1, 0, 0,
                                              0, 1, 0, 0, 0, 0};
  constexpr std::array<geo::index_t, 3> kOffsets{0, 3, 6};
  constexpr std::array<geo::index_t, 6> kCorners{0, 1, 2, 3, 1, 2};
  const auto view = document.Mesh().View();
  Require(document.Inspection().IsStorageValid() && view.attributes.empty() &&
              view.positions.size() == kPositions.size() &&
              std::ranges::equal(view.face_offsets, kOffsets) &&
              std::ranges::equal(view.corner_vertices, kCorners),
          "four vertices, two valid triangles with coincident distinct IDs");
  for (std::size_t i = 0; i < kPositions.size(); ++i)
    Require(
        std::bit_cast<std::uint64_t>(view.positions[i]) ==
            std::bit_cast<std::uint64_t>(kPositions[i]),
        "independent exact coordinate bits, vertex zero equals vertex three");
}

void ComparisonRejection(
    const fs::path& root, std::string_view name,
    std::span<const std::byte> wire,
    std::span<const rep::RowLocalAttributeDeclaration> declarations,
    void (*check_source)(const ic::PlyDocument&)) {
  const auto directory = root / name;
  Require(fs::create_directory(directory), "new comparison rejection case");
  const auto source_path = directory / kSourceName;
  const auto bundle = directory / kBundleName;
  Write(source_path, wire);
  geo::ExecutionContext ledger({kWorkers, kParallelMinVertices, kLedgerBytes});
  std::optional<rep::PlyDuplicateFileWorkflowRecord> retained;
  {
    auto context = ledger;
    const auto captured = Capture(source_path, context);
    Require(captured.asset.has_value(), "real comparison source captured");
    check_source(captured.asset->Document());
    const rep::PlyDuplicateFileWorkflowRequest request{
        *captured.asset, kInputId,
        kTargets,        f::Options(ic::PlyEncoding::kBinaryBigEndian),
        declarations,    rep::PlyWorkflowPublication{bundle, kReceiptName}};
    const auto result =
        rep::RunPlyDuplicateFileWorkflow(request, Limits(), Work());
    const auto* status = std::get_if<rep::MaterializedDuplicateStatus>(
        &result.diagnostic.delegated_status);
    const auto* diagnostic = std::get_if<rep::MaterializedDuplicateDiagnostic>(
        &result.diagnostic.delegated);
    Require(
        result.record && result.status == rep::PlyWorkflowStatus::kRejected &&
            result.diagnostic.stage == rep::PlyWorkflowStage::kRepair &&
            result.diagnostic.code == rep::PlyWorkflowDiagnosticCode::kRepair &&
            status && *status == rep::MaterializedDuplicateStatus::kRejected &&
            diagnostic &&
            diagnostic->stage == rep::MaterializedDuplicateStage::kComparison &&
            diagnostic->code ==
                rep::MaterializedDuplicateDiagnosticCode::kNotEquivalent &&
            diagnostic->target == 0 &&
            result.outcomes.execution ==
                rep::PlyWorkflowExecutionOutcome::kCompleted &&
            result.outcomes.stage == rep::PlyWorkflowStage::kComplete &&
            result.outcomes.candidate == rep::CandidateOutcome::rejected &&
            result.outcomes.profile ==
                rep::PlyWorkflowProfileOutcome::kFailed &&
            result.outcomes.exit ==
                rep::PlyWorkflowExitMeaning::kRejectedOrIncomplete &&
            result.outcomes.publication ==
                rep::PlyWorkflowPublicationOutcome::kNotAttempted,
        "real comparison, not eligibility/storage/range refusal, target zero");
    const auto& record = *result.record;
    Require(
        !record.RepairFacts() && !record.WriteFacts() && !record.Output() &&
            !record.Reload() && !record.Published() && !record.InputSha256() &&
            !result.publication && !fs::exists(bundle) &&
            Coverage(record, rep::PlyWorkflowCheck::kTargetPolicy).passed ==
                false &&
            Coverage(record, rep::PlyWorkflowCheck::kSourceStorage).passed ==
                true &&
            result.execution.hashing.caller_jobs == 0 &&
            std::ranges::equal(Read(source_path), wire),
        "no candidate/maps, writer, reload, hash or publisher before those "
        "phases");
    Joined(captured.execution);
    Joined(result.execution);
    Save(directory, *captured.asset, result, true);
    retained = record;
  }
  Require(
      retained && ledger.ActiveWorkers() == 0 &&
          ledger.ActiveTrackedPayload() > 0,
      "rejected source and diagnostics remain owned after original context");
  check_source(retained->Source().Document());
  retained.reset();
  Released(ledger);
}

void PropertyDifference(const fs::path& root) {
  constexpr std::string_view kCase = "face-property-difference";
  const auto wire = f::Wire(ic::PlyEncoding::kAscii, {.different_face = true});
  ComparisonRejection(root, kCase, wire, kDeclarations, CheckPropertySource);
}

void OppositeWinding(const fs::path& root) {
  constexpr std::string_view kCase = "opposite-winding";
  constexpr std::string_view kForward = ".5 2 7 4294967295 4 2 3 0 1\n";
  constexpr std::string_view kReversed = ".5 2 7 4294967295 4 2 1 0 3\n";
  const auto original = f::Wire(ic::PlyEncoding::kAscii);
  std::string text(f::Text(original));
  const auto first = text.find(kForward);
  Require(
      first != std::string::npos &&
          text.find(kForward, first + kForward.size()) == std::string::npos,
      "exact unique face row replacement, no unrelated property/vertex edits");
  text.replace(first, kForward.size(), kReversed);
  const auto wire = f::Bytes(text);
  ComparisonRejection(root, kCase, wire, kDeclarations, CheckWindingSource);
}

void DistinctVertexIds(const fs::path& root) {
  constexpr std::string_view kCase = "distinct-coincident-ids";
  constexpr std::string_view kWire =
      "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\n"
      "property float y\nproperty float z\nelement face 2\n"
      "property list uchar int vertex_indices\nend_header\n"
      "0 0 0\n1 0 0\n0 1 0\n0 0 0\n3 0 1 2\n3 3 1 2\n";
  const auto wire = f::Bytes(kWire);
  ComparisonRejection(root, kCase, wire, {}, CheckDistinctSource);
}

namespace resources {
namespace allocation = rep::materialized_duplicates_detail;
// Fixture-only ledger, independent of every application preset and op cap.
constexpr std::size_t kLedgerLimitBytes = 4 * kMiB;
constexpr std::size_t kTrackedBlocks = 64;
constexpr std::size_t kPositionComponents = 3;
constexpr std::size_t kWitnessLimitBytes = 16 * 1024;
constexpr std::size_t kReportHeadroomDivisor = 4;
constexpr std::uint64_t kPressureCancellationScalars = 64;
constexpr std::uint8_t kPressureValue = 0x5a;
constexpr std::string_view kPressureAttribute = "resource_payload";
constexpr std::string_view kBudgetCase = "shared-budget-at-report";
constexpr std::string_view kAllocationCase = "allocation-failure-at-report";
constexpr std::string_view kBaseline = "baseline";
constexpr std::string_view kPressure = "pressure";
constexpr std::string_view kReleased = "released-with-foreign";
constexpr std::string_view kBudgetWitness = "budget-witness.json";
constexpr std::string_view kExceptionWitness = "exception-witness.json";

struct AllocationStats {
  std::size_t calls = 0, frees = 0, live_bytes = 0;
  std::size_t report_requests = 0, first_report_bytes = 0;
  std::size_t first_report_alignment = 0, failures = 0;
  bool bad_free = false;
};
struct Backend {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0, alignment = 0;
  };
  allocation::AllocationBackend original =
      allocation::DefaultAllocationBackend();
  std::array<Block, kTrackedBlocks> blocks{};
  std::mutex mutex;
  AllocationStats stats;
  bool report = false, fail_report = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<Backend*>(opaque);
    std::lock_guard guard(self.mutex);
    ++self.stats.calls;
    if (self.report) {
      ++self.stats.report_requests;
      if (self.stats.first_report_bytes == 0) {
        self.stats.first_report_bytes = bytes;
        self.stats.first_report_alignment = alignment;
      }
      if (self.fail_report) {
        self.fail_report = false;
        ++self.stats.failures;
        throw std::bad_alloc{};
      }
    }
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks)
      if (!block.pointer) {
        block = {pointer, bytes, alignment};
        self.stats.live_bytes += bytes;
        return pointer;
      }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::runtime_error("bounded resource allocation tracker exhausted");
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* opaque) noexcept {
    auto& self = *static_cast<Backend*>(opaque);
    std::lock_guard guard(self.mutex);
    bool found = false;
    for (auto& block : self.blocks)
      if (block.pointer == pointer) {
        self.stats.bad_free |=
            block.bytes != bytes || block.alignment != alignment;
        self.stats.live_bytes -= block.bytes;
        block = {};
        found = true;
        break;
      }
    self.stats.bad_free |= !found;
    ++self.stats.frees;
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
  }
  allocation::AllocationBackend Options() { return {Allocate, Free, this}; }
  void MarkReport() {
    std::lock_guard guard(mutex);
    report = true;
  }
  AllocationStats Snapshot() {
    std::lock_guard guard(mutex);
    return stats;
  }
  void Empty() {
    const auto observed = Snapshot();
    Require(observed.live_bytes == 0 && !observed.bad_free,
            "actual original wrapper allocations all freed exactly once");
  }
};

geo::MaterializedMesh MakePressure(const geo::ExecutionContext& context,
                                   std::size_t count) {
  Require(count > 0 && count <= kLedgerLimitBytes,
          "bounded real pressure scalar input");
  // This ordinary input vector is test preparation, outside native accounting.
  // The returned native mesh, not this vector or a bare lease, occupies it.
  const std::vector<std::uint8_t> values(count, kPressureValue);
  constexpr std::array<double, 3> kPositions{0, 0, 0};
  constexpr std::array<geo::index_t, 1> kFaceOffsets{0};
  const std::array<geo::index_t, 2> offsets{0,
                                            static_cast<geo::index_t>(count)};
  const std::array attributes{geo::AttributeMaterializationInput{
      .domain = geo::AttributeDomain::vertex,
      .name = kPressureAttribute,
      .semantic = {},
      .set_index = {},
      .components = 1,
      .values = std::span<const std::uint8_t>(values),
      .offsets = std::span<const geo::index_t>(offsets),
      .present = {},
      .metadata = {}}};
  const geo::MeshMaterializationInput input{
      std::span<const double>(kPositions),
      geo::PolygonTopologyInput{kFaceOffsets, {}}, attributes};
  const geo::MeshMaterializationLimits limits{
      1, 1, 1, 1, 1, count, offsets.size(), 1, 1, kMiB, kLedgerLimitBytes};
  const geo::MeshMaterializationWorkPolicy work{
      kSerialMinBytes, kPartitionBytes, kPressureCancellationScalars, 1};
  const auto result = geo::MaterializeMesh(input, limits, work, context);
  Require(result.status == geo::MaterializationStatus::kSuccess && result.mesh,
          "fixture pressure construction must genuinely succeed");
  Joined(result.execution);
  const auto& mesh = *result.mesh;
  const auto view = mesh.View();
  Require(geo::SharesExecutionBudget(context, mesh.ShareExecutionContext()) &&
              std::ranges::equal(view.positions, kPositions) &&
              std::ranges::equal(view.face_offsets, kFaceOffsets) &&
              view.corner_vertices.empty() && view.attributes.size() == 1,
          "real point mesh owns storage in its requested shared ledger");
  const auto& attribute = view.attributes.front();
  const auto* scalars =
      std::get_if<std::span<const std::uint8_t>>(&attribute.values);
  Require(attribute.domain == geo::AttributeDomain::vertex &&
              attribute.name == kPressureAttribute &&
              attribute.components == 1 && attribute.semantic.empty() &&
              !attribute.set_index && !attribute.present &&
              attribute.metadata.empty() && attribute.offsets &&
              std::ranges::equal(*attribute.offsets, offsets) && scalars &&
              scalars->size() == count &&
              std::ranges::all_of(
                  *scalars, [](auto value) { return value == kPressureValue; }),
          "pressure is an actual typed ragged row, never an unbacked charge");
  return mesh;
}
struct Pressure {
  const geo::ExecutionContext& root;
  const geo::ExecutionContext& foreign_context;
  std::size_t count, report_request;
  std::optional<geo::MaterializedMesh> shared, foreign;
  std::size_t before = 0, shared_bytes = 0, foreign_bytes = 0, available = 0;
  bool shares_root = false, foreign_shares_root = true;
  void Create() {
    Require(!shared && !foreign, "one real pressure construction epoch");
    foreign.emplace(MakePressure(foreign_context, count));
    foreign_bytes = static_cast<std::size_t>(foreign->NewRequestedBytes());
    foreign_shares_root =
        geo::SharesExecutionBudget(root, foreign->ShareExecutionContext());
    before = root.ActiveTrackedPayload();
    shared.emplace(MakePressure(root, count));
    shared_bytes = static_cast<std::size_t>(shared->NewRequestedBytes());
    shares_root =
        geo::SharesExecutionBudget(root, shared->ShareExecutionContext());
    const auto after = root.ActiveTrackedPayload();
    Require(
        shares_root && !foreign_shares_root && shared_bytes > 0 &&
            shared_bytes == foreign_bytes && after >= before &&
            after - before == shared_bytes &&
            after <= root.TrackedPayloadBudget(),
        "measured equal owners occupy only their actual corresponding ledgers");
    available = root.TrackedPayloadBudget() - after;
    Require(
        available < report_request,
        "fixture guard: real shared storage leaves less than report request");
  }
};
struct Hooks {
  Backend& backend;
  const geo::ExecutionContext& context;
  Pressure* pressure = nullptr;
  bool report_seen = false, retain_repair = false;
  std::size_t report_live = 0, wrapper_live = 0;
  std::size_t repair_calls = 0, workflow_calls = 0;
  std::optional<rep::MaterializedDuplicateRepair> repair;
  static void Checkpoint(rep::PlyWorkflowStage stage, std::uint64_t,
                         void* opaque) {
    auto& self = *static_cast<Hooks*>(opaque);
    if (stage != rep::PlyWorkflowStage::kReport || self.report_seen) return;
    self.report_seen = true;
    self.report_live = self.context.ActiveTrackedPayload();
    self.wrapper_live = self.backend.Snapshot().live_bytes;
    self.backend.MarkReport();
    if (self.pressure) self.pressure->Create();
  }
  static void AfterRepair(const rep::MaterializedDuplicateResult& result,
                          void* opaque) {
    auto& self = *static_cast<Hooks*>(opaque);
    ++self.repair_calls;
    Require(result.status == rep::MaterializedDuplicateStatus::kSuccess &&
                result.repair,
            "resource checkpoint follows one genuine completed repair");
    if (self.retain_repair) self.repair.emplace(*result.repair);
  }
  detail::WorkflowTestOptions Options() {
    detail::WorkflowTestOptions result;
    result.allocation_backend = backend.Options();
    result.checkpoint = Checkpoint;
    result.after_repair = AfterRepair;
    result.state = this;
    return result;
  }
};
rep::PlyDuplicateFileWorkflowResult RunMemory(const ic::PlyFileAsset& source,
                                              Hooks& hooks) {
  ++hooks.workflow_calls;
  return detail::RunPlyDuplicateFileWorkflowForTest(
      {source,
       kInputId,
       kTargets,
       f::Options(ic::PlyEncoding::kBinaryBigEndian),
       kDeclarations,
       {}},
      Limits(), Work(), {}, hooks.Options());
}
void CheckRepair(const rep::MaterializedDuplicateRepair& repair) {
  Require(
      repair.Outcome() == rep::CandidateOutcome::accepted &&
          std::ranges::equal(repair.FaceMap(), f::kFaceMap) &&
          std::ranges::equal(repair.CornerMap(), f::kCornerMap) &&
          std::ranges::equal(repair.SourceFaces(), f::kSourceFaces) &&
          std::ranges::equal(repair.SourceCorners(), f::kSourceCorners),
      "actual completed repair preserves all four literal correspondence maps");
  f::CheckMesh(repair.Source());
  f::CheckMesh(repair.Candidate(), {.changed = true});
}
void SaveIn(const fs::path& directory, const ic::PlyFileAsset& source,
            const rep::PlyDuplicateFileWorkflowResult& result, bool report) {
  Require(fs::create_directory(directory), "fresh real resource observation");
  Save(directory, source, result, report);
}
void WriteWitness(const fs::path& path, const std::string& text) {
  Require(text.size() <= kWitnessLimitBytes, "bounded actual resource witness");
  Write(path, f::Bytes(text));
}
void SharedBudget(const fs::path& root) {
  const auto directory = root / kBudgetCase;
  Require(fs::create_directory(directory), "new shared-budget case");
  const auto path = directory / kSourceName;
  const auto wire = f::Wire(ic::PlyEncoding::kAscii);
  Write(path, wire);
  geo::ExecutionContext ledger(
      {kWorkers, kParallelMinVertices, kLedgerLimitBytes});
  geo::ExecutionContext foreign_context(
      {kWorkers, kParallelMinVertices, kLedgerLimitBytes});
  Backend baseline_backend, pressure_backend, released_backend;
  std::optional<rep::PlyDuplicateFileWorkflowRecord> retained;
  Pressure pressure{ledger, foreign_context, 0, 0, {}, {}};
  std::size_t source_baseline = 0, baseline_request = 0, wrapper_live = 0;
  std::size_t report_bytes = 0, released_bytes = 0, calls = 0;
  std::optional<rep::PlyWorkflowDiagnostic> report_diagnostic;
  {
    auto context = ledger;
    const auto captured = Capture(path, context);
    Require(captured.asset.has_value(), "real resource source captured");
    source_baseline = ledger.ActiveTrackedPayload();
    std::size_t baseline_report_live = 0;
    {
      Hooks hooks{baseline_backend, context};
      const auto result = RunMemory(*captured.asset, hooks);
      calls += hooks.workflow_calls;
      Require(result.status == rep::PlyWorkflowStatus::kSuccess &&
                  result.record && hooks.report_seen && hooks.repair_calls == 1,
              "unchanged operation caps pass without same-ledger pressure");
      Facts(*result.record, true);
      baseline_request = baseline_backend.Snapshot().first_report_bytes;
      baseline_report_live = hooks.report_live;
      Require(
          baseline_request > kReportHeadroomDivisor &&
              baseline_report_live < ledger.TrackedPayloadBudget(),
          "actual successful report request and pre-report ledger observed");
      SaveIn(directory / kBaseline, *captured.asset, result, true);
    }
    baseline_backend.Empty();
    Require(ledger.ActiveTrackedPayload() == source_baseline,
            "baseline result fully released before pressure setup");
    std::size_t fixed_bytes = 0;
    {
      const auto calibration = MakePressure(foreign_context, 1);
      fixed_bytes = static_cast<std::size_t>(calibration.NewRequestedBytes());
    }
    Released(foreign_context);
    const auto headroom = baseline_request / kReportHeadroomDivisor;
    const auto remaining = ledger.TrackedPayloadBudget() - baseline_report_live;
    Require(remaining > headroom && remaining - headroom >= fixed_bytes,
            "fixture has positive bounded real payload construction extent");
    // Only the uint8 row length changes from the observed one-scalar owner.
    // Successful creation and observed charge/headroom are checked, never
    // assumed.
    pressure.count = remaining - headroom - fixed_bytes + 1;
    pressure.report_request = baseline_request;
    {
      Hooks hooks{pressure_backend, context, &pressure};
      const auto result = RunMemory(*captured.asset, hooks);
      calls += hooks.workflow_calls;
      wrapper_live = hooks.wrapper_live;
      Require(
          hooks.report_seen && hooks.repair_calls == 1 &&
              Limits().max_wrapper_new_requested_bytes >= wrapper_live &&
              baseline_request <=
                  (Limits().max_wrapper_new_requested_bytes - wrapper_live) / 2,
          "actual staging and final requests fit unchanged operation-local "
          "cap");
      Require(
          result.status == rep::PlyWorkflowStatus::kBudgetExceeded &&
              result.diagnostic.stage == rep::PlyWorkflowStage::kReport &&
              result.diagnostic.code ==
                  rep::PlyWorkflowDiagnosticCode::kRequestBudget &&
              result.outcomes.exit ==
                  rep::PlyWorkflowExitMeaning::kExecutionFailure &&
              result.outcomes.publication ==
                  rep::PlyWorkflowPublicationOutcome::kNotRequested &&
              result.record && !result.publication,
          "real common-ledger admission refuses report without publication");
      Facts(*result.record, true);
      Require(result.record->InputSha256() &&
                  result.record->ReportJson().empty() &&
                  result.report_diagnostic &&
                  result.report_diagnostic->stage ==
                      rep::PlyWorkflowStage::kReport &&
                  result.report_diagnostic->code ==
                      rep::PlyWorkflowDiagnosticCode::kRequestBudget &&
                  pressure_backend.Snapshot().report_requests == 0,
              "typed completed facts survive; both real report requests "
              "refused before backend");
      report_bytes = result.record->ReportJson().size();
      report_diagnostic = result.report_diagnostic;
      SaveIn(directory / kPressure, *captured.asset, result, false);
      retained.emplace(*result.record);
    }
    pressure.shared.reset();
    released_bytes = ledger.ActiveTrackedPayload();
    Require(pressure.foreign &&
                !geo::SharesExecutionBudget(
                    context, pressure.foreign->ShareExecutionContext()),
            "equivalent real foreign owner stays held during released control");
    {
      Hooks hooks{released_backend, context};
      const auto result = RunMemory(*captured.asset, hooks);
      calls += hooks.workflow_calls;
      Require(result.status == rep::PlyWorkflowStatus::kSuccess &&
                  result.record && hooks.repair_calls == 1,
              "same caps succeed after releasing only common-ledger pressure");
      Facts(*result.record, true);
      SaveIn(directory / kReleased, *captured.asset, result, true);
    }
    released_backend.Empty();
    Require(Read(path) == wire && context.ActiveWorkers() == 0,
            "resource controls leave source intact and every worker joined");
  }
  Require(retained && ledger.ActiveTrackedPayload() > 0 &&
              ledger.ActiveWorkers() == 0,
          "budget-refused record outlives original source/context owner");
  Facts(*retained, true);
  retained.reset();
  pressure_backend.Empty();
  Released(ledger);
  pressure.foreign.reset();
  Released(foreign_context);
  Require(calls == 3 && report_diagnostic,
          "three actual budget workflow calls");
  // Closed test witness; values are observations, never a native result/report.
  constexpr std::string_view kHeader =
      "{\"schema\":\"meshvale.repair.ply-budget-witness/"
      "1\",\"workflow_calls\":";
  constexpr std::string_view kBaselineReportRequestBytesJson =
      ",\"baseline_report_request_bytes\":";
  constexpr std::string_view kForeignBytesAfterAllOwnersJson =
      ",\"foreign_bytes_after_all_owners\":";
  constexpr std::string_view kForeignPressureBytesJson =
      ",\"foreign_pressure_bytes\":";
  constexpr std::string_view kForeignSharesRootJson =
      ",\"foreign_shares_root\":";
  constexpr std::string_view kPressureBackendReportRequestsJson =
      ",\"pressure_backend_report_requests\":";
  constexpr std::string_view kReportBytesJson = ",\"report_bytes\":";
  constexpr std::string_view kReportDiagnosticCodeJson =
      ",\"report_diagnostic_code\":";
  constexpr std::string_view kReportDiagnosticStageJson =
      ",\"report_diagnostic_stage\":";
  constexpr std::string_view kRootAvailableAfterPressureJson =
      ",\"root_available_after_pressure\":";
  constexpr std::string_view kRootBudgetBytesJson = ",\"root_budget_bytes\":";
  constexpr std::string_view kRootBytesAfterAllOwnersJson =
      ",\"root_bytes_after_all_owners\":";
  constexpr std::string_view kRootBytesAfterPressureReleaseJson =
      ",\"root_bytes_after_pressure_release\":";
  constexpr std::string_view kRootLiveBytesBeforePressureJson =
      ",\"root_live_bytes_before_pressure\":";
  constexpr std::string_view kSharedPressureBytesJson =
      ",\"shared_pressure_bytes\":";
  constexpr std::string_view kSharesRootJson = ",\"shares_root\":";
  constexpr std::string_view kSourceBaselineBytesJson =
      ",\"source_baseline_bytes\":";
  constexpr std::string_view kWrapperCapBytesJson = ",\"wrapper_cap_bytes\":";
  constexpr std::string_view kWrapperLiveBytesAtReportJson =
      ",\"wrapper_live_bytes_at_report\":";
  constexpr std::string_view kFalseJson = "false";
  constexpr std::string_view kTrueJson = "true";
  constexpr std::string_view kObjectEndJson = "}\n";
  std::ostringstream out;
  out << kHeader << calls << kSourceBaselineBytesJson << source_baseline
      << kRootBudgetBytesJson << ledger.TrackedPayloadBudget()
      << kBaselineReportRequestBytesJson << baseline_request
      << kWrapperCapBytesJson << Limits().max_wrapper_new_requested_bytes
      << kWrapperLiveBytesAtReportJson << wrapper_live
      << kRootLiveBytesBeforePressureJson << pressure.before
      << kSharedPressureBytesJson << pressure.shared_bytes
      << kForeignPressureBytesJson << pressure.foreign_bytes
      << kRootAvailableAfterPressureJson << pressure.available
      << kSharesRootJson << (pressure.shares_root ? kTrueJson : kFalseJson)
      << kForeignSharesRootJson
      << (pressure.foreign_shares_root ? kTrueJson : kFalseJson)
      << kPressureBackendReportRequestsJson
      << pressure_backend.Snapshot().report_requests
      << kRootBytesAfterPressureReleaseJson << released_bytes
      << kRootBytesAfterAllOwnersJson << ledger.ActiveTrackedPayload()
      << kForeignBytesAfterAllOwnersJson
      << foreign_context.ActiveTrackedPayload() << kReportBytesJson
      << report_bytes << kReportDiagnosticStageJson
      << static_cast<unsigned>(report_diagnostic->stage)
      << kReportDiagnosticCodeJson
      << static_cast<unsigned>(report_diagnostic->code) << kObjectEndJson;
  WriteWitness(directory / kBudgetWitness, out.str());
}
void AllocationFailure(const fs::path& root) {
  const auto directory = root / kAllocationCase;
  Require(fs::create_directory(directory),
          "new actual allocation failure case");
  const auto path = directory / kSourceName;
  const auto wire = f::Wire(ic::PlyEncoding::kAscii);
  Write(path, wire);
  geo::ExecutionContext ledger(
      {kWorkers, kParallelMinVertices, kLedgerLimitBytes});
  Backend backend;
  backend.fail_report = true;
  Hooks hooks{backend, ledger};
  hooks.retain_repair = true;
  bool returned = false, observed = false;
  {
    const auto captured = Capture(path, ledger);
    Require(captured.asset.has_value(), "real allocation source captured");
    Write(directory / kCapturedName, captured.asset->EntryBytes());
    try {
      const auto result = RunMemory(*captured.asset, hooks);
      returned = true;
      (void)result;
    } catch (const std::bad_alloc&) {
      observed = true;
    }
    const auto stats = backend.Snapshot();
    Require(observed && !returned && hooks.workflow_calls == 1 &&
                hooks.report_seen && hooks.repair_calls == 1 && hooks.repair &&
                stats.failures == 1 && stats.report_requests == 1 &&
                stats.first_report_bytes > 0 &&
                stats.first_report_alignment > 0,
            "bad_alloc came from the first actual wrapper request after real "
            "report entry");
    CheckRepair(*hooks.repair);
    Require(Read(path) == wire && ledger.ActiveWorkers() == 0 &&
                !fs::exists(directory / kNativeName) &&
                !fs::exists(directory / kReportName) &&
                !fs::exists(directory / kBundleName),
            "exception returns no fabricated result/report/publication");
    backend.Empty();
  }
  Require(hooks.repair && ledger.ActiveTrackedPayload() > 0 &&
              ledger.ActiveWorkers() == 0,
          "actual completed repair survives exception and captured source "
          "lifetime");
  CheckRepair(*hooks.repair);
  const auto source = hooks.repair->Source().View();
  const auto candidate = hooks.repair->Candidate().View();
  constexpr std::string_view kHeader =
      "{\"schema\":\"meshvale.repair.ply-allocation-witness/"
      "1\",\"exception\":\"std.bad_alloc\",\"native_result_returned\":";
  constexpr std::string_view kActiveBackendBytesAfterUnwindJson =
      ",\"active_backend_bytes_after_unwind\":";
  constexpr std::string_view kActualWrapperAllocationsJson =
      ",\"actual_wrapper_allocations\":";
  constexpr std::string_view kAfterRepairCallsJson = ",\"after_repair_calls\":";
  constexpr std::string_view kCornersJson = ",\"corners\":";
  constexpr std::string_view kExceptionObservedJson =
      ",\"exception_observed\":";
  constexpr std::string_view kFacesJson = ",\"faces\":";
  constexpr std::string_view kFailedRequestAlignmentJson =
      ",\"failed_request_alignment\":";
  constexpr std::string_view kFailedRequestBytesJson =
      ",\"failed_request_bytes\":";
  constexpr std::string_view kFailedRequestsJson = ",\"failed_requests\":";
  constexpr std::string_view kRootBytesAfterAllOwnersJson =
      ",\"root_bytes_after_all_owners\":";
  constexpr std::string_view kSourceCountsVerticesJson =
      ",\"source_counts\":{\"vertices\":";
  constexpr std::string_view kWorkflowCallsJson = ",\"workflow_calls\":";
  constexpr std::string_view kCornerMapKey = "corner_map";
  constexpr std::string_view kFaceMapKey = "face_map";
  constexpr std::string_view kFalseJson = "false";
  constexpr std::string_view kSourceCornersKey = "source_corners";
  constexpr std::string_view kSourceFacesKey = "source_faces";
  constexpr std::string_view kTrueJson = "true";
  constexpr std::string_view kCandidateCountsVerticesJson =
      "},\"candidate_counts\":{\"vertices\":";
  constexpr std::string_view kObjectEndJson = "}\n";
  std::ostringstream out;
  const auto stats = backend.Snapshot();
  out << kHeader << (returned ? kTrueJson : kFalseJson)
      << kExceptionObservedJson << (observed ? kTrueJson : kFalseJson)
      << kWorkflowCallsJson << hooks.workflow_calls << kAfterRepairCallsJson
      << hooks.repair_calls << kFailedRequestsJson << stats.failures
      << kFailedRequestBytesJson << stats.first_report_bytes
      << kFailedRequestAlignmentJson << stats.first_report_alignment
      << kActualWrapperAllocationsJson << stats.calls
      << kActiveBackendBytesAfterUnwindJson << stats.live_bytes
      << kSourceCountsVerticesJson
      << source.positions.size() / kPositionComponents << kFacesJson
      << source.face_offsets.size() - 1 << kCornersJson
      << source.corner_vertices.size() << kCandidateCountsVerticesJson
      << candidate.positions.size() / kPositionComponents << kFacesJson
      << candidate.face_offsets.size() - 1 << kCornersJson
      << candidate.corner_vertices.size() << '}';
  const auto map = [&](std::string_view key,
                       std::span<const geo::index_t> values) {
    out << ",\"" << key << "\":[";
    bool comma = false;
    for (auto value : values) {
      if (comma) out << ',';
      comma = true;
      out << value;
    }
    out << ']';
  };
  map(kFaceMapKey, hooks.repair->FaceMap());
  map(kCornerMapKey, hooks.repair->CornerMap());
  map(kSourceFacesKey, hooks.repair->SourceFaces());
  map(kSourceCornersKey, hooks.repair->SourceCorners());
  hooks.repair.reset();
  backend.Empty();
  Released(ledger);
  out << kRootBytesAfterAllOwnersJson << ledger.ActiveTrackedPayload()
      << kObjectEndJson;
  WriteWitness(directory / kExceptionWitness, out.str());
}
}  // namespace resources

}  // namespace

int main(int argc, char** argv) {
  try {
    fs::path root;
    if (argc == 3 && std::string_view(argv[1]) == kReportsArgument) {
      root = fs::path(argv[2]);
      Require(root.is_absolute(), "explicit absolute request fixture root");
    } else if (argc == 1) {
      root = fs::current_path() /
             (std::string(kRootPrefix) +
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()));
    } else
      throw std::runtime_error("unknown PLY request fixture argument");
    Require(fs::create_directory(root), "exclusive new request fixture root");
    ExistingDestination(root);
    InteriorStop(root);
    StagedCorruption(root);
    PointOnly(root);
    PropertyDifference(root);
    OppositeWinding(root);
    DistinctVertexIds(root);
    resources::SharedBudget(root);
    resources::AllocationFailure(root);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
