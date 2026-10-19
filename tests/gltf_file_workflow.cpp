// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "gltf_file_workflow_fixture.h"

namespace {
namespace repair = meshvale::repair;
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fixture = repair::test_file_workflow;
namespace fs = std::filesystem;
using fixture::Bytes;
using fixture::Fixture;
using fixture::Limits;
using fixture::Require;
using fixture::Text;
using fixture::Work;
constexpr std::string_view kWriteReportsArgument = "--write-reports";
constexpr std::string_view kUnsupportedArgument = "--unsupported-profile";
constexpr std::string_view kManifestName = "cases.json";
constexpr std::string_view kReportName = "report.json";
constexpr std::uint64_t kOutsideFace = 999;
constexpr std::uint32_t kNegativeZeroBits = 0x80000000;
constexpr std::array<std::uint32_t, 9> kPositionBits{
    kNegativeZeroBits, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0};
constexpr std::size_t kExpectedSourceFiles = 4;
constexpr std::size_t kExpectedPositionChannels = 1;
constexpr std::size_t kExpectedPositionScalars = 9;
constexpr std::size_t kExpectedInstances = 2;
constexpr std::string_view kPositionSemantic = "POSITION";
constexpr std::string_view kNumberLexeme = "1.2300e+04";
constexpr std::string_view kStringLexeme = "keep\\u0020lexeme";
const std::array<ic::GltfFileView, 1> kStaticFiles{
    {{fixture::kStaticPath, Bytes(fixture::kStaticBytes)}}};
struct Reports {
  fs::path root;
  std::vector<std::string> cases;
  void Save(std::string_view name, const Fixture& source,
            const repair::GltfDuplicateFileWorkflowResult& result,
            bool published) {
    if (root.empty()) return;
    const auto directory = root / name;
    for (const auto& file : source.source->Files())
      fixture::Write(directory / "source" / file.path, file.bytes);
    if (result.record && !result.record->ReportJson().empty())
      fixture::Write(directory / kReportName, result.record->ReportJson());
    if (published) {
      Require(result.record && result.record->Published(),
              "actual owner before report export");
      for (const auto& file : result.record->Published()->Files())
        fixture::Write(directory / "bundle" / file.path,
                       fixture::Read(source.Destination() / file.path));
    }
    cases.emplace_back(name);
  }
  void Finish() const {
    if (root.empty()) return;
    fs::create_directories(root);
    std::ofstream stream(root / kManifestName, std::ios::binary);
    stream << "{\"version\":1,\"cases\":[";
    for (std::size_t i = 0; i < cases.size(); ++i) {
      if (i) stream << ',';
      stream << '"' << cases[i] << '"';
    }
    stream << "]}\n";
    Require(static_cast<bool>(stream),
            "independent report-case manifest saved");
  }
};
void CheckMemory(const Fixture& source,
                 const repair::GltfDuplicateFileWorkflowRecord& record,
                 bool changed) {
  Require(record.Memory() && record.Memory()->ReadAsset() &&
              record.Memory()->Repair() &&
              record.Memory()->ReloadedPrimitive() &&
              record.Memory()->OutputInspection(),
          "actual retained memory engine owners");
  const auto& memory = *record.Memory();
  Require(memory.ReadAsset()->OriginalJson().data() ==
              source.source->Asset().OriginalJson().data(),
          "exact canonical source owner adopted without reread");
  const auto expected =
      changed ? std::span<const std::uint32_t>(fixture::kChangedTriangles)
              : std::span<const std::uint32_t>(fixture::kOriginalTriangles);
  Require(std::ranges::equal(memory.ReloadedPrimitive()->TriangleIndices(),
                             expected),
          "literal selected output triangle indices");
  if (changed)
    Require(std::ranges::equal(memory.Repair()->FaceMap(),
                               fixture::kChangedFaceMap) &&
                std::ranges::equal(memory.Repair()->CornerMap(),
                                   fixture::kChangedCornerMap),
            "literal forward many-to-one maps, not inverse writer maps");
  const auto channels = memory.ReloadedPrimitive()->Channels();
  Require(channels.size() == kExpectedPositionChannels &&
              channels[0].semantic == kPositionSemantic,
          "literal position channel descriptor retained");
  const auto* positions = std::get_if<std::span<const float>>(&channels[0].raw);
  Require(
      positions && positions->size() == kExpectedPositionScalars &&
          std::bit_cast<std::uint32_t>((*positions)[0]) == kNegativeZeroBits,
      "literal complete float32 channel and signed zero preserved");
  for (std::size_t scalar = 0; scalar < kPositionBits.size(); ++scalar)
    Require(std::bit_cast<std::uint32_t>((*positions)[scalar]) ==
                kPositionBits[scalar],
            "each literal raw float32 position component preserved");
  Require(memory.ReadAsset()->InstanceCount() == kExpectedInstances &&
              memory.ReloadedAsset()->InstanceCount() == kExpectedInstances &&
              Text(memory.Output()->Json()).find(kNumberLexeme) !=
                  std::string_view::npos &&
              Text(memory.Output()->Json()).find(kStringLexeme) !=
                  std::string_view::npos,
          "actual shared instances and original opaque numeric/string lexemes");
  const auto bytes = record.NewRequestedBytes();
  Require(
      bytes.memory.read == 0 &&
          bytes.memory.total <= Limits().memory.max_new_requested_bytes &&
          bytes.wrapper <= Limits().max_wrapper_new_requested_bytes &&
          bytes.retained_source.files == source.source->NewRequestedBytes() &&
          bytes.retained_source.capped_reader ==
              source.source->Asset().NewRequestedBytes(),
      "fresh read is zero with separate positive retained source accounting");
  source.CheckSource();
}
void PublicationAndLifetime(Reports& reports) {
  for (const bool changed : {true, false}) {
    std::optional<repair::GltfDuplicateFileWorkflowRecord> retained;
    {
      Fixture source;
      const auto destination = source.Destination();
      const repair::GltfDuplicateFilePublication publication{
          destination, fixture::kReportPath, kStaticFiles};
      fs::remove_all(source.SourceRoot());
      const auto result = repair::RunGltfDuplicateFileWorkflow(
          source.Request(changed ? std::span<const repair::DuplicateTarget>(
                                       fixture::kTargets)
                                 : std::span<const repair::DuplicateTarget>{},
                         &publication),
          Limits(), Work(), source.context);
      Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                  result.record &&
                  result.outcomes.candidate ==
                      (changed ? repair::CandidateOutcome::accepted
                               : repair::CandidateOutcome::unchanged) &&
                  result.outcomes.execution ==
                      repair::GltfWorkflowExecutionOutcome::kCompleted &&
                  result.outcomes.profile ==
                      repair::GltfWorkflowProfileOutcome::kPassed &&
                  result.outcomes.exit ==
                      repair::GltfWorkflowExitMeaning::kSuccess &&
                  result.outcomes.publication ==
                      repair::GltfFileWorkflowPublicationOutcome::kPublished,
              "actual complete requested JSON publication, independent of "
              "edit/no-op");
      Require(
          result.publication && result.publication->bundle &&
              result.record->PublishedReportPath() == fixture::kReportPath &&
              fixture::Read(destination / fixture::kReportPath) ==
                  std::vector<std::byte>(result.record->ReportJson().begin(),
                                         result.record->ReportJson().end()),
          "returned success report is exact byte-verified committed bundle "
          "member");
      CheckMemory(source, *result.record, changed);
      for (const auto& file : result.record->Published()->Files()) {
        Require(std::ranges::equal(file.bytes,
                                   fixture::Read(destination / file.path)),
                "independently reopened full committed physical byte span");
        if (file.path == fixture::kBufferPath)
          Require(std::ranges::equal(file.bytes, fixture::kBinary),
                  "full original buffer tail preserved");
      }
      reports.Save(changed ? "changed-published" : "unchanged-published",
                   source, result, true);
      retained = *result.record;
    }
    auto copy = *retained;
    retained.reset();
    Require(copy.Source().Files().size() == kExpectedSourceFiles &&
                copy.Published() && !copy.ReportJson().empty() &&
                copy.PublishedReportPath() == fixture::kReportPath,
            "all caller source/options/context/root and original result die "
            "before copy is used");
  }
}
void NoExportAndRejected(Reports& reports) {
  Fixture source;
  for (const bool changed : {true, false}) {
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        source.Request(changed ? std::span<const repair::DuplicateTarget>(
                                     fixture::kTargets)
                               : std::span<const repair::DuplicateTarget>{}),
        Limits(), Work(), source.context);
    Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                result.record && !result.publication &&
                !result.record->Published() &&
                result.outcomes.publication ==
                    repair::GltfFileWorkflowPublicationOutcome::kNotRequested &&
                !fs::exists(source.Destination()),
            "genuine memory result when publication not requested");
    CheckMemory(source, *result.record, changed);
    Require(std::ranges::equal(result.record->ReportJson(),
                               result.record->Memory()->ReportJson()),
            "no-export retains exact owning memory report bytes");
    reports.Save(changed ? "changed-no-export" : "unchanged-no-export", source,
                 result, false);
  }
  {
    Fixture glb(ic::GltfFileFormat::kGlb);
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        glb.Request(), Limits(), Work(), glb.context);
    Require(
        result.status == repair::GltfWorkflowStatus::kSuccess &&
            result.record && !result.publication &&
            result.outcomes.publication ==
                repair::GltfFileWorkflowPublicationOutcome::kNotRequested &&
            std::ranges::equal(result.record->ReportJson(),
                               result.record->Memory()->ReportJson()) &&
            std::ranges::equal(result.record->Source().EntryBytes(), glb.entry),
        "actual retained GLB memory workflow preserves full literal "
        "wire/opaque chunk without export");
    CheckMemory(glb, *result.record, true);
    reports.Save("glb-no-export", glb, result, false);
  }
  const std::array rejected_targets{repair::DuplicateTarget{0, kOutsideFace}};
  const auto destination = source.Destination();
  const repair::GltfDuplicateFilePublication publication{
      destination, fixture::kReportPath, {}};
  const auto rejected = repair::RunGltfDuplicateFileWorkflow(
      source.Request(rejected_targets, &publication), Limits(), Work(),
      source.context);
  Require(
      rejected.status == repair::GltfWorkflowStatus::kRejected &&
          rejected.outcomes.candidate == repair::CandidateOutcome::rejected &&
          rejected.outcomes.exit ==
              repair::GltfWorkflowExitMeaning::kRejectedOrIncomplete &&
          rejected.outcomes.publication ==
              repair::GltfFileWorkflowPublicationOutcome::kNotAttempted &&
          !rejected.publication && !fs::exists(destination),
      "finite rejected target never exports partial edit");
  reports.Save("rejected", source, rejected, false);
  source.CheckSource();
}
void AdmissionPrecedence() {
  Fixture source;
  const auto charge = source.context.ActiveTrackedPayload();
  geo::ExecutionContext foreign({fixture::kWorkers, 1, fixture::kLedgerBytes});
  auto moving = source.context;
  const auto moved = std::move(moving);
  std::stop_source stop;
  stop.request_stop();
  for (const auto* context : {&foreign, &moving}) {
    for (const bool canceled : {false, true}) {
      const auto result = repair::RunGltfDuplicateFileWorkflow(
          source.Request(), Limits(), Work(), *context,
          canceled ? stop.get_token() : std::stop_token{});
      Require(
          result.status == repair::GltfWorkflowStatus::kSourceContractFailure &&
              result.diagnostic.stage ==
                  repair::GltfFileWorkflowStage::kSource &&
              result.diagnostic.code ==
                  repair::GltfFileWorkflowDiagnosticCode::kSourceLedger &&
              !result.record && !result.publication &&
              source.context.ActiveTrackedPayload() == charge,
          "foreign/equal-numeric and moved context refusal precedes "
          "stop/allocation");
    }
  }
  auto zero = Limits();
  zero.memory.max_source_protection_bytes = 0;
  const auto invalid = repair::RunGltfDuplicateFileWorkflow(
      source.Request(), zero, Work(), foreign, stop.get_token());
  Require(invalid.status == repair::GltfWorkflowStatus::kInvalidConfiguration &&
              invalid.diagnostic.code ==
                  repair::GltfFileWorkflowDiagnosticCode::kRequiredFieldZero,
          "positive configuration precedes foreign ledger and initial stop");
  const auto canceled = repair::RunGltfDuplicateFileWorkflow(
      source.Request(), Limits(), Work(), moved, stop.get_token());
  Require(canceled.status == repair::GltfWorkflowStatus::kCanceled &&
              !canceled.record && !canceled.publication,
          "same-ledger copied context reaches actual initial cancellation");
  source.CheckSource();
}
void ExportFailures(Reports& reports) {
  for (const auto format :
       {ic::GltfFileFormat::kGltfJson, ic::GltfFileFormat::kGlb}) {
    Fixture source(format);
    const auto destination = source.Destination();
    if (format == ic::GltfFileFormat::kGltfJson)
      Require(fs::create_directory(destination), "actual occupied destination");
    const repair::GltfDuplicateFilePublication publication{
        destination, fixture::kReportPath, kStaticFiles};
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        source.Request(fixture::kTargets, &publication), Limits(), Work(),
        source.context);
    Require(
        result.outcomes.candidate == repair::CandidateOutcome::accepted &&
            result.publication && !result.publication->bundle &&
            (!result.record || !result.record->Published()) &&
            result.outcomes.execution ==
                repair::GltfWorkflowExecutionOutcome::kFailed &&
            result.outcomes.exit ==
                repair::GltfWorkflowExitMeaning::kExecutionFailure,
        "accepted candidate survives real unsupported/occupied export failure");
    Require(result.publication->diagnostic.code ==
                (format == ic::GltfFileFormat::kGlb
                     ? ic::GltfFileDiagnosticCode::kPublicationUnsupported
                     : ic::GltfFileDiagnosticCode::kDestinationExists),
            "actual pinned publisher GLB-unavailable or occupied-directory "
            "diagnostic");
    reports.Save(format == ic::GltfFileFormat::kGlb ? "glb-unsupported-export"
                                                    : "destination-exists",
                 source, result, false);
    source.CheckSource();
  }
  for (const auto path : {fixture::kBufferPath, fixture::kReportPath}) {
    Fixture source;
    const auto destination = source.Destination();
    const std::array<ic::GltfFileView, 1> statics{
        {{path, Bytes(fixture::kStaticBytes)}}};
    const repair::GltfDuplicateFilePublication publication{
        destination, fixture::kReportPath, statics};
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        source.Request(fixture::kTargets, &publication), Limits(), Work(),
        source.context);
    Require(result.outcomes.candidate == repair::CandidateOutcome::accepted &&
                result.publication &&
                result.publication->diagnostic.code ==
                    ic::GltfFileDiagnosticCode::kPathCollision &&
                !result.publication->bundle && !fs::exists(destination),
            "real static/source or conditional-report collision");
    reports.Save(
        path == fixture::kBufferPath ? "static-collision" : "report-collision",
        source, result, false);
    source.CheckSource();
  }
}
void MemoryFailureAndCaps(Reports& reports) {
  {
    Fixture source(ic::GltfFileFormat::kGltfJson, true);
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        source.Request(), Limits(), Work(), source.context);
    Require(result.status != repair::GltfWorkflowStatus::kSuccess &&
                result.diagnostic.memory &&
                result.diagnostic.memory->stage ==
                    repair::GltfWorkflowStage::kExtract &&
                !result.publication,
            "real captured malformed selected index fails extraction");
    reports.Save("malformed-extraction", source, result, false);
  }
  Fixture source;
  const auto destination = source.Destination();
  const repair::GltfDuplicateFilePublication publication{
      destination, fixture::kReportPath, kStaticFiles};
  auto limits = Limits();
  limits.max_hash_input_bytes = 1;
  const auto refused = repair::RunGltfDuplicateFileWorkflow(
      source.Request(fixture::kTargets, &publication), limits, Work(),
      source.context);
  Require(refused.outcomes.candidate == repair::CandidateOutcome::accepted &&
              refused.status != repair::GltfWorkflowStatus::kSuccess &&
              !fs::exists(destination) &&
              refused.execution.hashing.launched_workers == 0 &&
              refused.execution.hashing.caller_jobs == 0,
          "complete hash work refused before any hash job or publication");
  reports.Save("hash-work-refused", source, refused, false);
  limits = Limits();
  limits.max_report_bytes = 1;
  const auto report_refused = repair::RunGltfDuplicateFileWorkflow(
      source.Request(fixture::kTargets, &publication), limits, Work(),
      source.context);
  Require(report_refused.status != repair::GltfWorkflowStatus::kSuccess &&
              !fs::exists(destination) &&
              (!report_refused.record ||
               report_refused.record->ReportJson().empty()),
          "report admission cannot expose a fabricated success or emergency "
          "report");
  source.CheckSource();
}
void Unsupported(Reports& reports) {
  for (const auto format :
       {ic::GltfFileFormat::kGltfJson, ic::GltfFileFormat::kGlb}) {
    Fixture source(format);
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        source.Request(), Limits(), Work(), source.context);
    Require(
        result.status == repair::GltfWorkflowStatus::kDelegatedFailure &&
            result.diagnostic.memory &&
            result.diagnostic.memory->stage ==
                repair::GltfWorkflowStage::kMaterialize &&
            !result.publication && !fs::exists(source.Destination()),
        "genuine nonempty dependency materialization refuses before publisher");
    const auto* status = std::get_if<ic::GltfMaterializationStatus>(
        &result.diagnostic.memory->delegated_status);
    const auto* diagnostic = std::get_if<ic::GltfMaterializationDiagnostic>(
        &result.diagnostic.memory->delegated);
    Require(status &&
                *status == ic::GltfMaterializationStatus::kGeometryFailure &&
                diagnostic &&
                diagnostic->geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile,
            "original dependency profile refusal fields remain actual");
    reports.Save(format == ic::GltfFileFormat::kGlb ? "unsupported-glb-source"
                                                    : "unsupported-json-source",
                 source, result, false);
    source.CheckSource();
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Reports reports;
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == kUnsupportedArgument;
    if (argc == 3 && std::string_view(argv[1]) == kWriteReportsArgument)
      reports.root = fs::path(argv[2]);
    Require(argc == 1 || unsupported || !reports.root.empty(),
            "known independent file-workflow invocation");
    if (unsupported)
      Unsupported(reports);
    else {
      AdmissionPrecedence();
      PublicationAndLifetime(reports);
      NoExportAndRejected(reports);
      ExportFailures(reports);
      MemoryFailureAndCaps(reports);
    }
    reports.Finish();
    std::cout << "retained literal source, real workflow/publication and "
                 "independent reopened report cases passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
