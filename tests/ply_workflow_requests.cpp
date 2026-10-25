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
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

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
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
