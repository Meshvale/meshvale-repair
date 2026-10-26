// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>

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
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "../src/gltf_file_workflow_test_internal.h"
#include "gltf_file_workflow_fixture.h"
#include "gltf_translation_workflow_fixture.h"

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
void GlbPublicationAndLifetime(Reports& reports) {
  constexpr std::array<geo::index_t, 2> kIdentityFaceMap{0, 1};
  for (const bool changed : {true, false}) {
    std::optional<repair::GltfDuplicateFileWorkflowRecord> retained;
    std::vector<std::byte> expected;
    {
      Fixture source(ic::GltfFileFormat::kGlb, false, fixture::kJson,
                     fixture::kLedgerBytes, true);
      const auto* original = source.source->Container();
      Require(original &&
                  original->ChunkCount() == fixture::kEmbeddedChunkCount &&
                  std::ranges::equal(original->Chunk(1).bytes,
                                     fixture::kEmbeddedBin),
              "literal embedded BIN includes logical unused byte and padding");
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
                  result.record && result.publication &&
                  result.publication->bundle &&
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
              "actual changed/no-op GLB repair publishes a verified receipt");
      CheckMemory(source, *result.record, changed);
      const auto& published = *result.record->Published();
      const auto prefix = result.record->Memory()->Output()->Json();
      expected = changed ? fixture::LiteralEmbeddedGlb(prefix) : source.entry;
      const auto* container = published.Container();
      Require(
          published.Format() == ic::GltfFileFormat::kGlb && container &&
              std::ranges::equal(published.EntryBytes(), expected) &&
              std::ranges::equal(container->Bytes(), expected) &&
              container->ChunkCount() == fixture::kEmbeddedChunkCount &&
              std::ranges::equal(
                  published.Inspection().ReloadedAsset().OriginalJson(),
                  prefix),
          "GLB full physical wire is independent of exact inspected prefix");
      for (std::size_t chunk = 1; chunk < original->ChunkCount(); ++chunk)
        Require(container->Chunk(chunk).type == original->Chunk(chunk).type &&
                    std::ranges::equal(container->Chunk(chunk).bytes,
                                       original->Chunk(chunk).bytes),
                "all ordered non-JSON types/full bytes and duplicate types "
                "retained");
      constexpr std::size_t kWordBytes = 4;
      const auto padding =
          (kWordBytes - prefix.size() % kWordBytes) % kWordBytes;
      const auto json = container->Chunk(0).bytes;
      Require(
          json.size() == prefix.size() + padding &&
              std::ranges::equal(json.first(prefix.size()), prefix) &&
              std::ranges::all_of(json.subspan(prefix.size()),
                                  [](auto b) { return b == std::byte{' '}; }),
          "actual container keeps writer prefix plus only derived space "
          "padding");
      Require(
          published.Files().size() == kExpectedSourceFiles + 2 &&
              result.record->PublishedReportPath() == fixture::kReportPath &&
              std::ranges::equal(
                  fixture::Read(destination / fixture::kReportPath),
                  result.record->ReportJson()),
          "GLB report is the exact actual committed member with full "
          "inventory");
      for (const auto& file : published.Files()) {
        Require(
            std::ranges::equal(file.bytes,
                               fixture::Read(destination / file.path)),
            "independently reopen every committed GLB/resource/report byte");
        if (file.path == fixture::kBufferPath)
          Require(
              std::ranges::equal(file.bytes, fixture::kBinary),
              "unused external buffer retains all eight physical tail bytes");
        if (file.path == fixture::kTexturePath)
          Require(
              std::ranges::equal(file.bytes, source.texture),
              "external image retains PNG signature and all opaque tail bytes");
      }
      constexpr std::array final_opaque{
          fixture::kOpaquePath, fixture::kStaticPath, fixture::kReportPath};
      const auto work = Work(true).publication;
      const auto reopened = ic::ReadGltfFileAsset(
          destination / fixture::kEntry,
          {{},
           ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
           final_opaque},
          {fixture::FileLimits(),
           fixture::ReadLimits(),
           {fixture::kPhysicalBytes, fixture::kCount, fixture::kStageBytes}},
          {work.files, work.reload, work.container}, source.context);
      Require(
          reopened.asset &&
              reopened.asset->Format() == ic::GltfFileFormat::kGlb &&
              reopened.asset->Files().size() == published.Files().size() &&
              reopened.asset->Container() &&
              std::ranges::equal(reopened.asset->EntryBytes(), expected) &&
              std::ranges::equal(reopened.asset->Asset().OriginalJson(),
                                 reopened.asset->Container()->Chunk(0).bytes),
          "ordinary finalized GLB capture again retains complete padded JSON");
      const auto extracted = ic::ExtractGltfPrimitiveCapped(
          reopened.asset->Asset(), *reopened.asset->Asset().Primitive(0),
          {ic::GltfTriangleRequest::kRequireTriangles,
           ic::GltfValueRequest::kRaw},
          Limits().memory.reload_extraction);
      Require(
          extracted.extraction &&
              std::ranges::equal(extracted.extraction->TriangleIndices(),
                                 changed ? std::span<const std::uint32_t>(
                                               fixture::kChangedTriangles)
                                         : std::span<const std::uint32_t>(
                                               fixture::kOriginalTriangles)),
          "actual reopened GLB decodes literal selected canonical triangles");
      const auto counters = result.record->NewRequestedBytes();
      Require(
          counters.retained_source.container ==
                  repair::GltfContainerCounterAvailability::kUnavailable &&
              counters.publication_files && *counters.publication_files > 0 &&
              source.context.ActiveWorkers() == 0,
          "GLB counters retain unavailable container scope without fake total");
      reports.Save(
          changed ? "glb-changed-published" : "glb-unchanged-published", source,
          result, true);
      retained = *result.record;
    }
    auto copy = *retained;
    retained.reset();
    Require(
        copy.Source().Format() == ic::GltfFileFormat::kGlb &&
            copy.Source().Container() && copy.Published() &&
            copy.Published()->Container() &&
            std::ranges::equal(copy.Published()->EntryBytes(), expected) &&
            std::ranges::equal(copy.Published()->Container()->Chunk(1).bytes,
                               fixture::kEmbeddedBin) &&
            copy.Memory()->Repair() && !copy.ReportJson().empty() &&
            copy.PublishedReportPath() == fixture::kReportPath,
        "GLB source/output/container/receipt outlive all caller owners and "
        "disk");
    Require(
        std::ranges::equal(
            copy.Memory()->Repair()->FaceMap(),
            changed ? std::span<const geo::index_t>(fixture::kChangedFaceMap)
                    : std::span<const geo::index_t>(kIdentityFaceMap)),
        "retained GLB workflow preserves actual changed/identity "
        "correspondence");
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
    Require(fs::create_directory(destination), "actual occupied destination");
    const repair::GltfDuplicateFilePublication publication{
        destination, fixture::kReportPath, kStaticFiles};
    const auto result = repair::RunGltfDuplicateFileWorkflow(
        source.Request(fixture::kTargets, &publication), Limits(), Work(),
        source.context);
    Require(result.outcomes.candidate == repair::CandidateOutcome::accepted &&
                result.publication && !result.publication->bundle &&
                (!result.record || !result.record->Published()) &&
                result.outcomes.execution ==
                    repair::GltfWorkflowExecutionOutcome::kFailed &&
                result.outcomes.exit ==
                    repair::GltfWorkflowExitMeaning::kExecutionFailure,
            "accepted candidate survives real occupied export failure");
    Require(result.publication->diagnostic.code ==
                    ic::GltfFileDiagnosticCode::kDestinationExists &&
                result.publication->cleanup ==
                    ic::GltfFileCleanupOutcome::kNotRequired &&
                fs::is_empty(destination),
            "actual JSON/GLB occupied-directory failure leaves destination "
            "untouched");
    reports.Save(format == ic::GltfFileFormat::kGlb ? "glb-destination-exists"
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
// Original moving-asset physical controls; the memory fixture owns only public
// data/policy. This file's existing generic publisher oracles remain separate.
void TranslationPublication() {
  namespace moving = meshvale::repair::test_translation_workflow;
  namespace mv = meshvale::repair;
  namespace interchange = meshvale::interchange;
  namespace filesystem = std::filesystem;
  static constexpr std::string_view kRootPrefix =
      "meshvale-moving-publication-";
  const auto temporary =
      filesystem::canonical(filesystem::temp_directory_path());
  const auto root =
      temporary /
      (std::string(kRootPrefix) +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  moving::Require(filesystem::create_directory(root),
                  "fresh owned moving publication root");
  struct Cleanup {
    filesystem::path root, parent;
    ~Cleanup() {
      std::error_code error;
      const auto resolved = filesystem::weakly_canonical(root, error);
      if (!error && resolved.parent_path() == parent &&
          resolved.filename().string().starts_with(kRootPrefix))
        filesystem::remove_all(resolved, error);
    }
  } cleanup{root, temporary};
  const auto write = [](const filesystem::path& path,
                        std::span<const std::byte> bytes) {
    moving::Require(bytes.size() <= moving::kMiB,
                    "bounded physical literal write");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    moving::Require(stream.is_open(), "physical literal opened");
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    moving::Require(!stream.fail(), "complete physical literal close");
  };
  const auto read = [](const filesystem::path& path) {
    moving::Require(filesystem::file_size(path) <= moving::kMiB,
                    "bounded physical reopen");
    std::ifstream stream(path, std::ios::binary);
    moving::Require(stream.is_open(), "physical bundle member reopened");
    const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
    moving::Require(!stream.bad(), "physical full bundle read");
    return std::vector<std::byte>(moving::Bytes(bytes).begin(),
                                  moving::Bytes(bytes).end());
  };
  constexpr std::size_t kStagedExtractionLimit = 1;
  constexpr std::size_t kStagedInspectionLimit = 2;
  constexpr std::size_t kLateCommitCollision = 3;
  static constexpr std::string_view kSentinelName = "sentinel";
  static constexpr std::string_view kSentinelBytes = "original destination";
  for (std::size_t fault : {std::size_t{0}, kStagedExtractionLimit,
                            kStagedInspectionLimit, kLateCommitCollision})
    for (bool glb : {false, true})
      for (bool changed : {false, true}) {
        if (fault && (glb || !changed)) continue;
        const auto case_name = std::string(glb ? "glb-" : "json-") +
                               (changed ? "changed" : "unchanged");
        const auto case_root = root / (std::to_string(fault) + "-" + case_name);
        const auto input = case_root / "input";
        moving::Require(filesystem::create_directory(case_root) &&
                            filesystem::create_directory(input),
                        "distinct owned physical case");
        std::optional<mv::GltfDuplicateFileWorkflowRecord> retained;
        {
          meshvale::geometry::ExecutionContext context(
              {moving::kWorkers, 1, moving::kLedgerBytes});
          moving::MovingFixture data;
          std::vector<std::byte> entry;
          if (glb) {
            constexpr std::string_view kOriginal =
                R"({"uri":"moving.bin","byteLength":148})";
            constexpr std::string_view kEmbedded = R"({"byteLength":156})";
            const auto at = data.json.find(kOriginal);
            moving::Require(at != std::string::npos,
                            "actual literal first-buffer rewrite only");
            data.json.replace(at, kOriginal.size(), kEmbedded);
            constexpr std::uint32_t kMagic = 0x46546c67, kVersion = 2;
            constexpr std::uint32_t kJsonType = 0x4e4f534a,
                                    kBinType = 0x004e4942;
            constexpr std::uint32_t kOpaqueType = 0x31415926,
                                    kOpaqueWord = 0x1100fe42;
            constexpr std::size_t kAlignment = 4, kHeaderBytes = 12,
                                  kChunkHeaderBytes = 8;
            constexpr std::size_t kOriginalChunkCount = 3;
            auto json = data.json;
            while (json.size() % kAlignment) json.push_back(' ');
            const auto size =
                kHeaderBytes + kOriginalChunkCount * kChunkHeaderBytes +
                json.size() + data.binary.size() + moving::kFloatBytes;
            moving::Require(size <= moving::kMiB,
                            "bounded exact original GLB framing");
            moving::Word(entry, kMagic);
            moving::Word(entry, kVersion);
            moving::Word(entry, static_cast<std::uint32_t>(size));
            moving::Word(entry, static_cast<std::uint32_t>(json.size()));
            moving::Word(entry, kJsonType);
            entry.insert(entry.end(), moving::Bytes(json).begin(),
                         moving::Bytes(json).end());
            moving::Word(entry, static_cast<std::uint32_t>(data.binary.size()));
            moving::Word(entry, kBinType);
            entry.insert(entry.end(), data.binary.begin(), data.binary.end());
            moving::Word(entry, moving::kFloatBytes);
            moving::Word(entry, kOpaqueType);
            moving::Word(entry, kOpaqueWord);
          } else {
            entry.assign(moving::Bytes(data.json).begin(),
                         moving::Bytes(data.json).end());
            write(input / moving::kBuffer, data.binary);
          }
          write(input / moving::kEntry, entry);
          write(input / moving::kUnusedBuffer, data.unused);
          write(input / moving::kRedImage, data.red);
          write(input / moving::kBlueImage, data.blue);
          mv::GltfDuplicateFileWorkflowLimits limits;
          const auto memory = moving::Limits();
          limits.memory = {memory.max_new_requested_bytes,
                           memory.max_input_id_bytes,
                           moving::kMiB,
                           moving::kCount,
                           memory.max_reload_bindings,
                           memory.max_report_bytes,
                           memory.extraction,
                           memory.materialization,
                           memory.repair,
                           memory.writing,
                           memory.reload,
                           memory.reload_extraction,
                           memory.verification};
          limits.max_wrapper_new_requested_bytes = moving::kWorkflowBytes;
          limits.max_source_files = moving::kCount;
          limits.max_supplemental_files = moving::kCount;
          limits.max_report_path_bytes = moving::kIdentityBytes;
          limits.max_report_bytes = moving::kMiB;
          limits.max_hash_input_bytes = moving::kStageBytes;
          limits.publication = {
              {moving::kCount, moving::kCount, moving::kMiB,
               moving::kStageBytes, moving::kMiB, moving::kMiB, moving::kMiB,
               moving::kWorkflowBytes},
              moving::ReadLimits(),
              memory.extraction,
              memory.verification,
              {moving::kMiB, moving::kCount, moving::kStageBytes}};
          if (fault == kStagedExtractionLimit)
            limits.publication.extraction.direct_translation.max_total_keys = 1;
          if (fault == kStagedInspectionLimit)
            limits.publication.inspection.direct_translation.max_total_keys = 1;
          const auto work_memory = moving::Work();
          constexpr std::uint64_t kFilePartitionBytes = 256,
                                  kFileStopBytes = 16;
          const mv::GltfWorkflowByteWorkPolicy byte_work{
              1, kFilePartitionBytes, kFileStopBytes, moving::kJobs};
          const mv::GltfDuplicateFileWorkflowWorkPolicy work{
              work_memory,
              byte_work,
              byte_work,
              {{kFilePartitionBytes, 1, kFilePartitionBytes, kFileStopBytes,
                moving::kJobs},
               work_memory.interchange,
               work_memory.verification,
               {1, kFilePartitionBytes}}};
          const interchange::GltfFileReadLimits capture_limits{
              limits.publication.files, moving::ReadLimits(),
              limits.publication.container};
          const interchange::GltfFileReadWorkPolicy capture_work{
              work.publication.files, work_memory.interchange,
              work.publication.container};
          auto captured = interchange::ReadGltfFileAsset(input / moving::kEntry,
                                                         {}, capture_limits,
                                                         capture_work, context);
          moving::Require(captured.asset.has_value(),
                          "real moving JSON/GLB file capture");
          moving::Require(filesystem::weakly_canonical(input).parent_path() ==
                              filesystem::canonical(case_root),
                          "owned source removal stays inside exact case root");
          filesystem::remove_all(input);
          moving::Require(!filesystem::exists(input),
                          "captured source removed before real repair");
          const mv::GltfDuplicateFilePublication publication{
              case_root / "published", moving::kReport, {}};
          const mv::GltfDuplicateFileWorkflowRequest request{
              *captured.asset,
              moving::kInputId,
              0,
              {interchange::GltfTriangleRequest::kRequireTriangles,
               interchange::GltfValueRequest::kRaw,
               interchange::GltfProcessingProfile::
                   kDirectTranslationTrianglesV1},
              changed ? std::span<const mv::DuplicateTarget>(moving::kTargets)
                      : std::span<const mv::DuplicateTarget>{},
              {moving::kRowLocalAttributes},
              &publication};
          mv::gltf_file_workflow_detail::FileWorkflowTestOptions options;
          auto destination = publication.destination;
          if (fault == kLateCommitCollision) {
            options.state = &destination;
            options.prepared_report = [](std::span<const std::byte>,
                                         void* opaque) {
              const auto& path = *static_cast<const filesystem::path*>(opaque);
              moving::Require(filesystem::create_directory(path),
                              "actual no-replace race destination created");
              std::ofstream sentinel(path / kSentinelName, std::ios::binary);
              sentinel << kSentinelBytes;
              sentinel.close();
              moving::Require(!sentinel.fail(),
                              "actual race sentinel complete close");
            };
          }
          auto result = mv::gltf_file_workflow_detail::
              RunGltfDuplicateFileWorkflowForTest(request, limits, work,
                                                  context, {}, options);
          if (fault) {
            moving::Require(result.status != mv::GltfWorkflowStatus::kSuccess &&
                                result.record && result.publication &&
                                !result.publication->bundle &&
                                !result.record->Published() &&
                                !result.record->PublishedReportPath() &&
                                result.record->RepairFacts(),
                            "actual later publication failure retains Repair, "
                            "never committed output claims");
            moving::CheckRepair(*result.record->RepairFacts(), true);
            moving::CheckOutput(*result.record->Memory(), true);
            moving::Require(
                result.publication->cleanup ==
                        interchange::GltfFileCleanupOutcome::kRemoved &&
                    result.record->Outcomes().exit ==
                        mv::GltfWorkflowExitMeaning::kExecutionFailure &&
                    context.ActiveWorkers() == 0,
                "real staged failure cleanup, outer exit2 and joined workers");
            if (fault == kLateCommitCollision) {
              moving::CheckFacts(result.translation, true);
              moving::Require(
                  result.record->Outcomes().profile ==
                          mv::GltfWorkflowProfileOutcome::kPassed &&
                      filesystem::exists(destination / kSentinelName),
                  "passed preservation survives real late no-replace failure "
                  "and sentinel remains");
            } else {
              moving::Require(
                  result.translation.publication_reload &&
                      !filesystem::exists(destination) &&
                      result.record->Outcomes().profile ==
                          (fault == kStagedExtractionLimit
                               ? mv::GltfWorkflowProfileOutcome::kIncomplete
                               : mv::GltfWorkflowProfileOutcome::kFailed),
                  "actual staged count cap refuses without published "
                  "destination");
              for (const auto* prior :
                   {&result.translation.source, &result.translation.writing,
                    &result.translation.reload,
                    &result.translation.verification})
                moving::Require(
                    prior->has_value() &&
                        (*prior)->status ==
                            interchange::GltfStatus::kAccepted &&
                        (*prior)->complete &&
                        (*prior)->execution.launched_workers ==
                            (*prior)->execution.joined_workers,
                    "four real completed memory phases survive staged refusal");
              constexpr std::uint32_t kTranslationTotalKeyRule = 317;
              if (fault == kStagedExtractionLimit)
                moving::Require(
                    !result.translation.publication_reload->complete &&
                        result.translation.publication_reload->status ==
                            interchange::GltfStatus::kUnsupportedProfile &&
                        result.translation.publication_reload->diagnostic
                                .code ==
                            interchange::GltfDiagnosticCode::kLimitExceeded &&
                        result.translation.publication_reload->diagnostic
                                .rule_id == kTranslationTotalKeyRule &&
                        !result.translation.publication_verification &&
                        result.status == mv::GltfWorkflowStatus::kUnsupported,
                    "staged E logical refusal retains only actual E return");
              else {
                moving::Require(
                    result.translation.publication_reload->complete &&
                        result.translation.publication_reload->status ==
                            interchange::GltfStatus::kAccepted &&
                        result.translation.publication_verification &&
                        !result.translation.publication_verification
                             ->complete &&
                        result.translation.publication_verification->status ==
                            interchange::GltfStatus::kUnsupportedProfile &&
                        result.translation.publication_verification->diagnostic
                                .code ==
                            interchange::GltfDiagnosticCode::kLimitExceeded &&
                        result.translation.publication_verification->diagnostic
                                .rule_id == kTranslationTotalKeyRule &&
                        result.status == mv::GltfWorkflowStatus::kLimitExceeded,
                    "staged V logical count retains five completed phases and "
                    "the actual partial V return");
                const auto report = moving::Text(result.record->ReportJson());
                moving::Require(report.size() <= limits.max_report_bytes,
                                "bounded actual failure report view");
                constexpr std::string_view kTranslationRow =
                    "\"id\":\"translation-preservation\",";
                constexpr std::string_view kLegacyStagedRow =
                    "\"id\":\"staged-selected-primitive\",";
                constexpr std::string_view kRowEnd = R"(,"diagnostics":[]})";
                const auto require_finding = [&](std::string_view id,
                                                 std::string_view finding) {
                  const auto begin = report.find(id);
                  moving::Require(begin != std::string_view::npos,
                                  "actual coverage row is present");
                  const auto end = report.find(kRowEnd, begin);
                  moving::Require(end != std::string_view::npos,
                                  "bounded coverage row terminator");
                  const auto row = report.substr(begin, end - begin);
                  moving::Require(
                      row.find(finding) != std::string_view::npos,
                      "actual distinct coverage status and finding");
                };
                constexpr std::string_view kFailedNull =
                    R"("status":"failed","finding":null)";
                constexpr std::string_view kPerformedFailed =
                    R"("status":"performed","finding":"failed")";
                require_finding(kTranslationRow, kFailedNull);
                require_finding(kLegacyStagedRow, kPerformedFailed);
              }
            }
            continue;
          }
          moving::Require(
              result.status == mv::GltfWorkflowStatus::kSuccess &&
                  result.record && result.publication &&
                  result.publication->bundle && result.record->Published(),
              "actual moving file publication commits verified bundle");
          moving::CheckFacts(result.translation, true);
          moving::CheckRepair(*result.record->RepairFacts(), changed);
          moving::CheckOutput(*result.record->Memory(), changed);
          for (const auto& member : result.record->Published()->Files())
            moving::Require(
                std::ranges::equal(read(publication.destination / member.path),
                                   member.bytes),
                "physical published member equals actual retained bytes");
          moving::Require(
              std::ranges::equal(
                  read(publication.destination / moving::kReport),
                  result.record->ReportJson()),
              "committed immutable report matches returned exact receipt");
          moving::Require(
              !result.record->ReportJson().empty() &&
                  moving::Text(result.record->ReportJson())
                          .find("\"publication_reload\":{\"status\":0") !=
                      std::string_view::npos,
              "conditional receipt records actual staged facts before commit");
          if (!changed)
            moving::Require(
                std::ranges::equal(result.record->Published()->EntryBytes(),
                                   entry),
                "unchanged JSON/GLB retains exact original wire");
          moving::Require(context.ActiveWorkers() == 0,
                          "all actual physical workers joined");
          retained = *result.record;
        }
        moving::CheckFacts(retained->TranslationFacts(), true);
        moving::CheckRepair(*retained->RepairFacts(), changed);
        moving::Require(
            retained->Published() && !retained->Published()->Files().empty(),
            "published source/inspection/report owners survive caller "
            "destruction");
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
      GlbPublicationAndLifetime(reports);
      NoExportAndRejected(reports);
      ExportFailures(reports);
      MemoryFailureAndCaps(reports);
      TranslationPublication();
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
