// SPDX-License-Identifier: Apache-2.0
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
#include <ios>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "ply_interchange_fixture.h"
#include "ply_workflow_observations.h"

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace rep = meshvale::repair;
namespace f = ic::ply_fixture;
namespace obs = rep::test_ply_observations;
namespace fs = std::filesystem;
using f::Require;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kLedger = 256 * kMiB;
constexpr std::uint64_t kCount = 65536;
constexpr std::uint32_t kWorkers = 4;
constexpr std::uint32_t kParallelFloor = 1;
constexpr std::uint64_t kNameByteLimit = 256;
constexpr std::uint64_t kNativePathCodeUnitLimit = 4096;
constexpr std::uint64_t kIoBlockBytes = 64;
constexpr std::uint64_t kParallelMinBytes = 1;
constexpr std::uint64_t kPartitionBytes = 128;
constexpr std::uint64_t kCancellationBlockBytes = 16;
constexpr std::uint64_t kMaxJobs = 64;
constexpr std::uint64_t kInputIdBytes = 128;
constexpr std::uint64_t kRefusedOutputBytes = 1;
constexpr std::uint64_t kWrapperBytes = 64 * kMiB;
constexpr std::uint64_t kFileOperationBytes = 64 * kMiB;
constexpr std::uint64_t kRepairAggregateBytes = 32 * kMiB;
constexpr std::size_t kSha256HexBytes = 64;
constexpr std::size_t kPublicationMembers = 2;
constexpr std::array kEditStates{false, true};
constexpr std::size_t kMemoryChecks = 9;
constexpr std::size_t kPublicationChecks = 11;
constexpr std::size_t kCoverageCount = 12;
constexpr std::string_view kInputId = "literal-ply-workflow";
constexpr std::string_view kSourceName = "source.ply";
constexpr std::string_view kSnapshotName = "captured.ply";
constexpr std::string_view kReportName = "report.json";
constexpr std::string_view kMemoryOutputName = "memory-output.ply";
constexpr std::string_view kNativeObservationName = "native.json";
constexpr std::string_view kReceiptName = "meshvale-report.json";
constexpr std::string_view kBundleName = "bundle";
constexpr std::string_view kReportsArgument = "--write-reports";
constexpr std::string_view kFixtureRootPrefix = "ply-workflow-fixture-";
constexpr std::array kTargets{rep::DuplicateTarget{0, 1}};
// Explicit safety assertions belong to this measurement corpus's caller. No
// wrapper rule derives safety or a declaration from an arbitrary property name.
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
constexpr std::array kIdentityFaces{geo::index_t{0}, geo::index_t{1},
                                    geo::index_t{2}};
constexpr std::array<geo::index_t, 11> kIdentityCorners{0, 1, 2, 3, 4, 5,
                                                        6, 7, 8, 9, 10};
ic::PlyFileLimits FileLimits() {
  return {kMiB,
          kMiB,
          kPublicationMembers * kMiB,
          kNameByteLimit,
          kNameByteLimit,
          kNativePathCodeUnitLimit,
          kFileOperationBytes};
}
ic::PlyFileWorkPolicy FileWork() {
  return {kIoBlockBytes, kParallelMinBytes, kPartitionBytes,
          kCancellationBlockBytes, kMaxJobs};
}
rep::PlyDuplicateFileWorkflowLimits Limits() {
  return {kWrapperBytes,
          kInputIdBytes,
          kMiB,
          kMiB,
          kPublicationMembers * kMiB,
          kCount,
          kCount,
          kCount,
          {kCount,
           kCount,
           f::kDescriptorLimit,
           kCount,
           kRepairAggregateBytes,
           {kCount, f::kRequestLimit},
           f::MeshLimits()},
          f::WriteLimits(),
          f::ReadLimits(),
          {FileLimits(), {kCount, f::kRequestLimit}, f::ReadLimits()}};
}
rep::PlyDuplicateFileWorkflowWorkPolicy Work() {
  const rep::PlyByteWorkPolicy bytes{kParallelMinBytes, kPartitionBytes,
                                     kCancellationBlockBytes, kMaxJobs};
  return {f::Work().geometry,
          f::Work(),
          f::Work(),
          bytes,
          bytes,
          bytes,
          {FileWork(), f::Work().geometry, f::Work()}};
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary);
  Require(stream.good(), "fixture opens exclusively owned file");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(stream.good(), "fixture writes and closes complete bytes");
}
std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  Require(stream.good(), "physical read opens");
  const auto count = stream.tellg();
  Require(count >= 0 && count <= static_cast<std::streamoff>(2 * kMiB),
          "bounded fixture read");
  std::vector<std::byte> bytes(static_cast<std::size_t>(count));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(stream.good() || (bytes.empty() && stream.eof()),
          "complete physical read");
  return bytes;
}
ic::PlyFileReadResult Capture(const fs::path& path,
                              const geo::ExecutionContext& context) {
  return ic::ReadPlyFileAsset(path, {FileLimits(), f::ReadLimits()},
                              {FileWork(), f::Work()}, context);
}
void Released(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "last owner releases the shared ledger and all workers");
}
void Coverage(const rep::PlyDuplicateFileWorkflowRecord& record,
              bool published) {
  Require(record.Coverage().size() == kCoverageCount,
          "all twelve coverage observations retained");
  const auto required = published ? kPublicationChecks : kMemoryChecks;
  for (std::size_t i = 0; i < required; ++i) {
    const auto& item = record.Coverage()[i];
    Require(item.check == static_cast<rep::PlyWorkflowCheck>(i) &&
                item.state == rep::PlyWorkflowCoverageState::kPerformed &&
                item.passed == true,
            "every required predicate actually performed and passed");
  }
  Require(
      record.Coverage().back().check == rep::PlyWorkflowCheck::kSolidValidity &&
          record.Coverage().back().state ==
              rep::PlyWorkflowCoverageState::kUnsupported,
      "solid validity stays explicitly unsupported");
}
void CheckRepair(const rep::MaterializedDuplicateRepair& repair, bool changed) {
  f::CheckMesh(repair.Source());
  f::CheckMesh(repair.Candidate(), {.changed = changed});
  Require(repair.Outcome() == (changed ? rep::CandidateOutcome::accepted
                                       : rep::CandidateOutcome::unchanged),
          "real changed or unchanged engine outcome");
  if (changed) {
    Require(std::ranges::equal(repair.FaceMap(), f::kFaceMap) &&
                std::ranges::equal(repair.CornerMap(), f::kCornerMap) &&
                std::ranges::equal(repair.SourceFaces(), f::kSourceFaces) &&
                std::ranges::equal(repair.SourceCorners(), f::kSourceCorners),
            "four complete actual maps equal independent literal arrays");
  } else {
    Require(std::ranges::equal(repair.FaceMap(), kIdentityFaces) &&
                std::ranges::equal(repair.CornerMap(), kIdentityCorners) &&
                std::ranges::equal(repair.SourceFaces(), kIdentityFaces) &&
                std::ranges::equal(repair.SourceCorners(), kIdentityCorners),
            "four actual unchanged arrays are complete identities");
  }
}
void Corpus(const fs::path& root) {
  std::size_t completed = 0;
  for (auto source_encoding : f::kEncodings)
    for (bool changed : kEditStates)
      for (auto encoding : f::kEncodings) {
        const auto name = std::string(f::EncodingWord(source_encoding)) +
                          (changed ? "-changed-" : "-unchanged-") +
                          std::string(f::EncodingWord(encoding));
        const auto directory = root / name;
        Require(fs::create_directory(directory), "fresh case directory");
        const auto source = directory / kSourceName;
        const auto bundle = directory / kBundleName;
        const auto literal = f::Wire(source_encoding);
        Write(source, literal);
        Write(directory / kSnapshotName, literal);
        geo::ExecutionContext context({kWorkers, kParallelFloor, kLedger});
        {
          const auto captured = Capture(source, context);
          Require(captured.asset.has_value() &&
                      captured.status == ic::PlyFileStatus::kSuccess,
                  "real physical PLY capture");
          Require(std::ranges::equal(captured.asset->EntryBytes(), literal),
                  "captured source bytes exact");
          Require(fs::remove(source),
                  "source path removed after actual capture");
          const auto declarations =
              changed ? std::span<const rep::RowLocalAttributeDeclaration>(
                            kDeclarations)
                      : std::span<const rep::RowLocalAttributeDeclaration>{};
          const auto targets =
              changed ? std::span<const rep::DuplicateTarget>(kTargets)
                      : std::span<const rep::DuplicateTarget>{};
          const rep::PlyDuplicateFileWorkflowRequest request{
              *captured.asset,
              kInputId,
              targets,
              f::Options(encoding),
              declarations,
              rep::PlyWorkflowPublication{bundle, kReceiptName}};
          const auto result =
              rep::RunPlyDuplicateFileWorkflow(request, Limits(), Work());
          Require(result.status == rep::PlyWorkflowStatus::kSuccess &&
                      result.record &&
                      result.outcomes.exit ==
                          rep::PlyWorkflowExitMeaning::kSuccess &&
                      result.outcomes.publication ==
                          rep::PlyWorkflowPublicationOutcome::kPublished,
                  "physical workflow actually completed and committed");
          const auto& record = *result.record;
          Require(record.RepairFacts() && record.Output() && record.Reload() &&
                      record.Published(),
                  "all actual completed owners retained");
          CheckRepair(*record.RepairFacts(), changed);
          Coverage(record, true);
          Require(record.Reload()->Encoding() == encoding &&
                      record.Published()->Document().Encoding() == encoding,
                  "actual memory and physical reload encodings equal the "
                  "explicit request");
          f::Check(*record.Reload(), {.changed = changed});
          f::Check(record.Published()->Document(), {.changed = changed});
          f::CheckOutput(*record.Output(), source_encoding, encoding, {},
                         {.changed = changed});
          Require(record.InputSha256().has_value() &&
                      record.InputSha256()->size() == kSha256HexBytes,
                  "actual source hash retained");
          Require(std::ranges::equal(Read(bundle / kSourceName),
                                     record.Output()->Bytes()),
                  "physical entry equals verified memory output");
          Require(std::ranges::equal(Read(bundle / kReceiptName),
                                     record.ReportJson()),
                  "returned receipt equals native committed receipt");
          Require(std::ranges::distance(fs::directory_iterator(bundle)) ==
                      static_cast<std::ptrdiff_t>(kPublicationMembers),
                  "complete flat two-member inventory");
          Write(directory / kReportName, record.ReportJson());
          Write(directory / kMemoryOutputName, record.Output()->Bytes());
          obs::WriteNative(directory / kNativeObservationName, *captured.asset,
                           result);
        }
        Released(context);
        ++completed;
      }
  constexpr std::size_t kPhysicalChains = 18;
  Require(completed == kPhysicalChains,
          "all eighteen physical encoding/edit chains executed");
}
void EmptyCorpus(const fs::path& root) {
  constexpr std::string_view kEmptyPrefix = "empty-";
  for (auto source_encoding : f::kEncodings) {
    for (auto encoding : f::kEncodings) {
      const auto directory =
          root / (std::string(kEmptyPrefix) +
                  std::string(f::EncodingWord(source_encoding)) + "-" +
                  std::string(f::EncodingWord(encoding)));
      Require(fs::create_directory(directory),
              "fresh true-empty case directory");
      const auto source = directory / kSourceName,
                 bundle = directory / kBundleName;
      const auto literal = f::Wire(source_encoding, {.empty = true});
      Write(source, literal);
      Write(directory / kSnapshotName, literal);
      geo::ExecutionContext context({kWorkers, kParallelFloor, kLedger});
      {
        const auto captured = Capture(source, context);
        Require(captured.asset.has_value(), "real zero-row typed PLY capture");
        Require(fs::remove(source), "true-empty source removed after capture");
        const auto result = rep::RunPlyDuplicateFileWorkflow(
            {*captured.asset,
             kInputId,
             {},
             f::Options(encoding),
             {},
             rep::PlyWorkflowPublication{bundle, kReceiptName}},
            Limits(), Work());
        Require(
            result.status == rep::PlyWorkflowStatus::kSuccess &&
                result.record && result.record->RepairFacts() &&
                result.record->Output() && result.record->Reload() &&
                result.record->Published(),
            "true-empty actual repair/write/reload/publication all complete");
        const auto& record = *result.record;
        Require(record.RepairFacts()->Outcome() ==
                        rep::CandidateOutcome::unchanged &&
                    record.RepairFacts()->FaceMap().empty() &&
                    record.RepairFacts()->CornerMap().empty() &&
                    record.RepairFacts()->SourceFaces().empty() &&
                    record.RepairFacts()->SourceCorners().empty(),
                "genuine empty candidate retains four actual empty arrays");
        f::CheckMesh(record.RepairFacts()->Candidate(), {.empty = true});
        f::Check(*record.Reload(), {.empty = true});
        f::Check(record.Published()->Document(), {.empty = true});
        f::CheckOutput(*record.Output(), source_encoding, encoding,
                       {.empty = true}, {.empty = true});
        Require(record.Reload()->Encoding() == encoding &&
                    record.Published()->Document().Encoding() == encoding,
                "actual true-empty destination encodings observed");
        Coverage(record, true);
        Require(std::ranges::equal(Read(bundle / kReceiptName),
                                   record.ReportJson()),
                "true-empty receipt is the actual committed receipt");
        Write(directory / kReportName, record.ReportJson());
        Write(directory / kMemoryOutputName, record.Output()->Bytes());
        obs::WriteNative(directory / kNativeObservationName, *captured.asset,
                         result);
      }
      Released(context);
    }
  }
}
void Lifetime(const fs::path& root) {
  constexpr std::string_view kLifetimeDirectory = "lifetime";
  const auto directory = root / kLifetimeDirectory;
  Require(fs::create_directory(directory), "fresh owner-lifetime directory");
  const auto source = directory / kSourceName;
  Write(source, f::Wire(ic::PlyEncoding::kAscii));
  std::optional<rep::PlyDuplicateFileWorkflowRecord> held;
  {
    geo::ExecutionContext original({kWorkers, kParallelFloor, kLedger});
    const auto captured = Capture(source, original);
    Require(captured.asset.has_value(), "lifetime capture");
    const auto result = rep::RunPlyDuplicateFileWorkflow(
        {*captured.asset,
         kInputId,
         kTargets,
         f::Options(ic::PlyEncoding::kBinaryBigEndian),
         kDeclarations,
         {}},
        Limits(), Work());
    Require(result.status == rep::PlyWorkflowStatus::kSuccess && result.record,
            "real memory workflow retained before original owners die");
    held.emplace(*result.record);
  }
  auto ledger = held->Source().Document().Mesh().ShareExecutionContext();
  {
    const auto copy = *held;
    held.reset();
    Require(copy.RepairFacts() && copy.Output() && copy.Reload() &&
                !copy.ReportJson().empty(),
            "record copy retains complete facts after source/context/original "
            "record destruction");
    CheckRepair(*copy.RepairFacts(), true);
    f::Check(*copy.Reload(), {.changed = true});
    Require(ledger.ActiveTrackedPayload() > 0 && ledger.ActiveWorkers() == 0,
            "actual retained ledger stays charged and synchronous workers are "
            "joined");
  }
  Released(ledger);
}
void SaveControl(const fs::path& directory, std::string_view name,
                 const ic::PlyFileAsset& source,
                 const rep::PlyDuplicateFileWorkflowResult& result) {
  const auto path = directory / name;
  Require(fs::create_directory(path),
          "fresh actual control observation directory");
  Write(path / kSnapshotName, source.EntryBytes());
  if (result.record && !result.record->ReportJson().empty())
    Write(path / kReportName, result.record->ReportJson());
  if (result.record && result.record->Output())
    Write(path / kMemoryOutputName, result.record->Output()->Bytes());
  obs::WriteNative(path / kNativeObservationName, source, result);
}
void Controls(const fs::path& root) {
  constexpr std::string_view kControlDirectory = "controls";
  constexpr std::string_view kMemoryUnchanged = "memory-unchanged";
  constexpr std::string_view kMissingDeclaration = "missing-declaration";
  constexpr std::string_view kUnusedDeclaration = "unused-declaration";
  constexpr std::string_view kDuplicateDeclaration = "duplicate-declaration";
  constexpr std::string_view kInvalidDeclaration = "invalid-declaration";
  constexpr std::string_view kLaterWriteLimit = "later-write-limit";
  const auto directory = root / kControlDirectory;
  Require(fs::create_directory(directory), "fresh control directory");
  const auto source = directory / kSourceName;
  Write(source, f::Wire(ic::PlyEncoding::kAscii));
  geo::ExecutionContext context({kWorkers, kParallelFloor, kLedger});
  {
    const auto captured = Capture(source, context);
    Require(captured.asset.has_value(), "control capture");
    const auto invoke =
        [&](std::span<const rep::DuplicateTarget> targets,
            std::span<const rep::RowLocalAttributeDeclaration> declarations,
            const rep::PlyDuplicateFileWorkflowLimits& limits) {
          return rep::RunPlyDuplicateFileWorkflow(
              {*captured.asset,
               kInputId,
               targets,
               f::Options(ic::PlyEncoding::kAscii),
               declarations,
               {}},
              limits, Work());
        };
    {
      const auto result = invoke({}, {}, Limits());
      Require(result.status == rep::PlyWorkflowStatus::kSuccess &&
                  result.record && result.record->RepairFacts(),
              "empty targets truly unchanged without declarations");
      CheckRepair(*result.record->RepairFacts(), false);
      Coverage(*result.record, false);
      SaveControl(directory, kMemoryUnchanged, *captured.asset, result);
      // The existing engine still checks finite values on the unchanged path;
      // those real jobs share eligibility telemetry. Unknown properties with no
      // declarations establish this no-edit case; a phase counter is not a
      // witness that semantic attribute-policy evaluation ran.
    }
    {
      const auto result = invoke(kTargets, {}, Limits());
      Require(result.status == rep::PlyWorkflowStatus::kRejected &&
                  result.record && !result.record->RepairFacts(),
              "unknown properties are refused without caller declarations");
      SaveControl(directory, kMissingDeclaration, *captured.asset, result);
    }
    {
      auto declarations = std::vector<rep::RowLocalAttributeDeclaration>(
          kDeclarations.begin(), kDeclarations.end());
      declarations.push_back(
          {geo::AttributeDomain::corner, "unused_measurement"});
      const auto result = invoke(kTargets, declarations, Limits());
      Require(result.status == rep::PlyWorkflowStatus::kSuccess &&
                  result.record && result.record->RepairFacts(),
              "structurally valid unused declaration is allowed");
      CheckRepair(*result.record->RepairFacts(), true);
      SaveControl(directory, kUnusedDeclaration, *captured.asset, result);
      declarations.push_back(declarations.back());
      const auto duplicate = invoke({}, declarations, Limits());
      Require(duplicate.status == rep::PlyWorkflowStatus::kInvalidRequest &&
                  duplicate.diagnostic.code ==
                      rep::PlyWorkflowDiagnosticCode::kDeclaration &&
                  !duplicate.record,
              "duplicate domain/name pair rejected before work even with empty "
              "targets");
      SaveControl(directory, kDuplicateDeclaration, *captured.asset, duplicate);
    }
    {
      constexpr std::array invalid{rep::RowLocalAttributeDeclaration{
          geo::AttributeDomain::vertex, "bad-name"}};
      const auto result = invoke({}, invalid, Limits());
      Require(result.status == rep::PlyWorkflowStatus::kInvalidRequest &&
                  !result.record,
              "invalid declaration grammar refused before work");
      SaveControl(directory, kInvalidDeclaration, *captured.asset, result);
    }
    {
      auto limits = Limits();
      limits.writing.max_output_bytes = kRefusedOutputBytes;
      const auto result = invoke(kTargets, kDeclarations, limits);
      Require(result.status == rep::PlyWorkflowStatus::kLimitExceeded &&
                  result.diagnostic.stage == rep::PlyWorkflowStage::kWrite &&
                  result.record && result.record->RepairFacts() &&
                  !result.record->Output(),
              "later real writer refusal retains completed repair and "
              "withholds unverified output");
      CheckRepair(*result.record->RepairFacts(), true);
      SaveControl(directory, kLaterWriteLimit, *captured.asset, result);
    }
  }
  Released(context);
}
static_assert(
    std::is_nothrow_copy_constructible_v<rep::PlyDuplicateFileWorkflowRecord>);
static_assert(
    std::is_nothrow_copy_assignable_v<rep::PlyDuplicateFileWorkflowRecord>);
static_assert(
    std::is_nothrow_destructible_v<rep::PlyDuplicateFileWorkflowRecord>);
}  // namespace
int main(int argc, char** argv) {
  try {
    fs::path root;
    if (argc == 3 && std::string_view(argv[1]) == kReportsArgument)
      root = fs::path(argv[2]);
    else if (argc == 1)
      root = fs::current_path() /
             (std::string(kFixtureRootPrefix) +
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()));
    else
      throw std::runtime_error("unknown PLY native fixture argument");
    // Each run exclusively creates its root. Retain outputs for passive report
    // inspection; no recursive deletion, external download or service is used.
    Require(fs::create_directory(root), "exclusive owned native fixture root");
    Corpus(root);
    EmptyCorpus(root);
    Controls(root);
    Lifetime(root);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
