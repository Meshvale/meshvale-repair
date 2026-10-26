// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/repair/gltf_workflow.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gltf_translation_workflow_fixture.h"

namespace {
namespace repair = meshvale::repair;
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
constexpr std::uint64_t kStageCap = 4 * kPhysicalLimit;
constexpr std::uint64_t kAggregateCap = 64 * kPhysicalLimit;
constexpr std::uint64_t kLedgerCap = 128 * kPhysicalLimit;
constexpr std::uint64_t kIdentityLimit = 256;
constexpr std::uint64_t kDocumentDepth = 64;
constexpr std::uint64_t kParallelThreshold = 128;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCancellationBlock = 32;
constexpr std::uint64_t kJobs = 64;
constexpr std::size_t kWorkers = 4;
constexpr std::size_t kFloatBytes = 4;
constexpr std::size_t kByteBits = 8;
constexpr std::uint32_t kByteMask = 255;
constexpr std::string_view kInputId = "workflow-fixture";
constexpr std::string_view kUnsupportedProfileOption = "--unsupported-profile";
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
std::span<const std::byte> Bytes(std::string_view s) {
  return std::as_bytes(std::span(s.data(), s.size()));
}
std::string_view Text(std::span<const std::byte> s) {
  return {reinterpret_cast<const char*>(s.data()), s.size()};
}
geo::MeshMaterializationLimits CaptureLimits() {
  return {kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
          kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
          kPhysicalLimit, kPhysicalLimit, kStageCap};
}
repair::GltfDuplicateWorkflowLimits Limits() {
  repair::GltfDuplicateWorkflowLimits l;
  l.max_new_requested_bytes = kAggregateCap;
  l.max_input_id_bytes = kIdentityLimit;
  l.max_reload_bindings = kPhysicalLimit;
  l.max_report_bytes = kPhysicalLimit;
  ic::GltfReadLimits read{kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                          kPhysicalLimit, kDocumentDepth, kPhysicalLimit,
                          kIdentityLimit, kIdentityLimit, kIdentityLimit,
                          kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                          kPhysicalLimit, kStageCap};
  l.read = l.reload = {read, kStageCap};
  l.extraction =
      l.reload_extraction = {kPhysicalLimit, kPhysicalLimit, kStageCap};
  l.materialization = {
      kPhysicalLimit, kStageCap, CaptureLimits(), {kPhysicalLimit, kStageCap}};
  l.repair = {kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
              kPhysicalLimit, kStageCap,      {kPhysicalLimit, kStageCap},
              CaptureLimits()};
  l.writing = {kPhysicalLimit,
               kPhysicalLimit,
               kDocumentDepth,
               kPhysicalLimit,
               kPhysicalLimit,
               kPhysicalLimit,
               kPhysicalLimit,
               kPhysicalLimit,
               kPhysicalLimit,
               kPhysicalLimit,
               {kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                kStageCap}};
  l.verification = {kPhysicalLimit, kPhysicalLimit, kDocumentDepth,
                    kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                    kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                    kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                    kStageCap};
  return l;
}
repair::GltfDuplicateWorkflowWorkPolicy Work() {
  return {{kParallelThreshold, kPartitionBytes, kParallelThreshold,
           kPartitionBytes},
          {kParallelThreshold, kPartitionBytes, kCancellationBlock, kJobs},
          {kParallelThreshold, kPartitionBytes, kCancellationBlock, kJobs},
          {kParallelThreshold, kPartitionBytes, kCancellationBlock, kJobs},
          {kParallelThreshold, kPartitionBytes, kCancellationBlock, kJobs}};
}
struct Fixture {
  std::string json =
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":48}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],"extras":{"opaque":1.2300e+04,"escaped":"keep\u0020lexeme"}})";
  std::vector<std::byte> binary;
  Fixture() {
    for (float value :
         std::array{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) {
      const auto bits = std::bit_cast<std::uint32_t>(value);
      for (std::size_t byte = 0; byte < kFloatBytes; ++byte)
        binary.push_back(
            static_cast<std::byte>((bits >> (kByteBits * byte)) & kByteMask));
    }
    for (std::uint16_t index : std::array<std::uint16_t, 6>{0, 1, 2, 0, 1, 2}) {
      binary.push_back(static_cast<std::byte>(index));
      binary.push_back(std::byte{});
    }
  }
};
repair::GltfDuplicateWorkflowResult Run(
    Fixture& f, std::span<const repair::DuplicateTarget> targets,
    const repair::GltfDuplicateWorkflowLimits& limits = Limits(),
    std::stop_token stop = {}) {
  geo::ExecutionContext context({kWorkers, 1, kLedgerCap});
  const std::array bindings{
      ic::SuppliedGltfResource{ic::GltfResourceKind::kBuffer, 0, f.binary}};
  repair::GltfDuplicateWorkflowRequest request{
      Bytes(f.json),
      bindings,
      kInputId,
      0,
      {ic::GltfTriangleRequest::kRequireTriangles, ic::GltfValueRequest::kRaw},
      targets,
      {}};
  return repair::RunGltfDuplicateWorkflow(request, limits, Work(), context,
                                          stop);
}
void Save(const std::filesystem::path& directory, std::string_view name,
          const repair::GltfDuplicateWorkflowResult& result) {
  if (directory.empty() || !result.record) return;
  std::ofstream stream(directory / name, std::ios::binary);
  const auto bytes = result.record->ReportJson();
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  Require(static_cast<bool>(stream), "Could not retain report fixture");
}
void SuccessAndLifetime(const std::filesystem::path& reports) {
  std::optional<repair::GltfDuplicateWorkflowRecord> retained;
  {
    Fixture fixture;
    const std::array targets{repair::DuplicateTarget{0, 1}};
    auto result = Run(fixture, targets);
    if (result.status != repair::GltfWorkflowStatus::kSuccess)
      throw std::runtime_error(
          "changed workflow failed: stage=" +
          std::to_string(static_cast<int>(result.diagnostic.stage)) +
          " code=" + std::to_string(static_cast<int>(result.diagnostic.code)));
    Require(result.record && result.record->Output() &&
                result.record->OutputInspection(),
            "Verified output owners missing");
    Require(result.outcomes.exit == repair::GltfWorkflowExitMeaning::kSuccess,
            "Successful exit mismatch");
    Require(result.record->Repair()->FaceMap().size() == 2 &&
                result.record->Repair()->FaceMap()[1] == 0,
            "Many-to-one repair map missing");
    Require(
        result.record->Repair()->Candidate().View().corner_vertices.size() == 3,
        "Duplicate triangle retained");
    Require(result.record->ReloadExtractionPolicy()->triangles ==
                ic::GltfTriangleRequest::kRequireTriangles,
            "Changed reload policy wrong");
    Require(Text(result.record->Output()->Json()).find("1.2300e+04") !=
                std::string_view::npos,
            "Opaque numeric lexeme changed");
    Require(Text(result.record->Output()->Json()).find("keep\\u0020lexeme") !=
                std::string_view::npos,
            "Opaque string lexeme changed");
    Require(result.record->ReadAsset()->InstanceCount() == 2 &&
                result.record->ReloadedAsset()->InstanceCount() == 2,
            "Shared primitive instances lost");
    Require(result.record->NewRequestedBytes().total <=
                Limits().max_new_requested_bytes,
            "Aggregate live requests exceed cap");
    Require(result.execution.report_copy.launched_workers > 0 &&
                result.execution.report_copy.launched_workers ==
                    result.execution.report_copy.joined_workers,
            "Report work did not use joined leased workers");
    Save(reports, "accepted.json", result);
    retained = result.record;
  }
  // Input bytes, request spans, context and original result have all died.
  Require(retained->Output()->Json().size() > 0 &&
              retained->ReportJson().size() > 0,
          "Record lost lifetime ownership");
  auto copy = *retained;
  Require(copy.ReportJson().data() == retained->ReportJson().data(),
          "Copy allocated report storage");
  Require(copy.NewRequestedBytes().total == retained->NewRequestedBytes().total,
          "Copy duplicated operation charges");
}
void FailuresAndNoOp(const std::filesystem::path& reports) {
  Fixture fixture;
  auto noop = Run(fixture, {});
  Require(noop.status == repair::GltfWorkflowStatus::kSuccess && noop.record,
          "No-op workflow failed");
  Require(noop.outcomes.candidate == repair::CandidateOutcome::unchanged,
          "No-op outcome changed");
  Require(std::equal(Bytes(fixture.json).begin(), Bytes(fixture.json).end(),
                     noop.record->Output()->Json().begin(),
                     noop.record->Output()->Json().end()),
          "No-op JSON was not exact");
  Save(reports, "unchanged.json", noop);
  const std::array invalid{repair::DuplicateTarget{0, 999}};
  auto rejected = Run(fixture, invalid);
  Require(rejected.status == repair::GltfWorkflowStatus::kRejected &&
              rejected.record,
          "Finite invalid target not reported rejection");
  Require(rejected.outcomes.exit ==
                  repair::GltfWorkflowExitMeaning::kRejectedOrIncomplete &&
              rejected.outcomes.candidate == repair::CandidateOutcome::rejected,
          "Rejected outcomes collapsed");
  Require(!rejected.record->Output(), "Rejected request exposed output");
  const auto* native = std::get_if<repair::MaterializedDuplicateDiagnostic>(
      &rejected.diagnostic.delegated);
  Require(
      native && native->code ==
                    repair::MaterializedDuplicateDiagnosticCode::kTargetRange,
      "Real target diagnostic lost");
  Save(reports, "rejected.json", rejected);
  const std::array targets{repair::DuplicateTarget{0, 1}};
  auto limits = Limits();
  limits.writing.max_candidate_faces = 1;
  limits.writing.output.max_json_bytes = 1;
  auto after_candidate = Run(fixture, targets, limits);
  Require(after_candidate.status != repair::GltfWorkflowStatus::kSuccess &&
              after_candidate.record,
          "Writer failure did not retain report");
  Require(after_candidate.outcomes.candidate ==
                  repair::CandidateOutcome::accepted &&
              after_candidate.outcomes.exit ==
                  repair::GltfWorkflowExitMeaning::kExecutionFailure,
          "Writer failure lost accepted candidate");
  Require(
      !after_candidate.record->Output() &&
          after_candidate.diagnostic.stage == repair::GltfWorkflowStage::kWrite,
      "Failed writer exposed output/wrong stage");
  Save(reports, "accepted-then-failed.json", after_candidate);
  limits = Limits();
  limits.max_report_bytes = 1;
  auto report_failure = Run(fixture, targets, limits);
  Require(!report_failure.record &&
              report_failure.diagnostic.stage ==
                  repair::GltfWorkflowStage::kReport &&
              report_failure.diagnostic.code ==
                  repair::GltfWorkflowDiagnosticCode::kReportExtent,
          "Report extent failure invented an emergency report");
  Require(
      report_failure.outcomes.candidate == repair::CandidateOutcome::accepted,
      "Report failure lost accepted candidate facts");
  limits = Limits();
  limits.max_new_requested_bytes = 8 * kPhysicalLimit;
  auto allowance = Run(fixture, targets, limits);
  Require(
      allowance.diagnostic.stage == repair::GltfWorkflowStage::kMaterialize &&
          allowance.diagnostic.code ==
              repair::GltfWorkflowDiagnosticCode::kMaterializationAllowance,
      "Overlapping materialization caps were clamped independently");
  Require(allowance.record && !allowance.record->Source(),
          "Rejected materialization allowance invoked adapter");
  Save(reports, "materialization-allowance.json", allowance);
  limits = Limits();
  limits.materialization.materialization.max_new_requested_bytes =
      std::numeric_limits<std::uint64_t>::max();
  auto overflow = Run(fixture, targets, limits);
  Require(
      overflow.status == repair::GltfWorkflowStatus::kInvalidConfiguration &&
          !overflow.record,
      "Overflowing simultaneous allowance accepted");
  Fixture malformed;
  malformed.json = "{";
  auto read_failure = Run(malformed, targets);
  Require(read_failure.record &&
              read_failure.diagnostic.stage == repair::GltfWorkflowStage::kRead,
          "Read failure stage/report lost");
  Save(reports, "malformed.json", read_failure);
  std::stop_source stopped;
  stopped.request_stop();
  auto canceled = Run(fixture, targets, Limits(), stopped.get_token());
  Require(canceled.status == repair::GltfWorkflowStatus::kCanceled &&
              canceled.outcomes.exit ==
                  repair::GltfWorkflowExitMeaning::kCanceled &&
              !canceled.record,
          "Canceled admission did not retain typed outcomes");
}
void LedgerAndConfiguration() {
  Fixture fixture;
  geo::ExecutionContext context({kWorkers, 1, kLedgerCap});
  const std::array targets{repair::DuplicateTarget{0, 1}};
  const std::array bindings{ic::SuppliedGltfResource{
      ic::GltfResourceKind::kBuffer, 0, fixture.binary}};
  repair::GltfDuplicateWorkflowRequest request{
      Bytes(fixture.json),
      bindings,
      kInputId,
      0,
      {ic::GltfTriangleRequest::kRequireTriangles, ic::GltfValueRequest::kRaw},
      targets,
      {}};
  {
    auto result =
        repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
    Require(
        result.status == repair::GltfWorkflowStatus::kSuccess && result.record,
        "Ledger fixture failed");
    Require(result.record->NewRequestedBytes().total ==
                context.ActiveTrackedPayload(),
            "Workflow live counters omitted or counted shared storage twice");
    auto copy = *result.record;
    Require(copy.NewRequestedBytes().total == context.ActiveTrackedPayload(),
            "Record copy added fresh requests");
  }
  Require(context.ActiveTrackedPayload() == 0,
          "Destroyed workflow retained a charge");
  request.primitive_ordinal.reset();
  auto missing_selection =
      repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
  Require(missing_selection.status ==
                  repair::GltfWorkflowStatus::kInvalidConfiguration &&
              missing_selection.diagnostic.code ==
                  repair::GltfWorkflowDiagnosticCode::kPrimitiveSelection,
          "Missing primitive selected the first entry implicitly");
  request.primitive_ordinal = 0;
  request.input_id = "folder/source.gltf";
  auto path_identity =
      repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
  Require(path_identity.status ==
                  repair::GltfWorkflowStatus::kInvalidConfiguration &&
              path_identity.diagnostic.code ==
                  repair::GltfWorkflowDiagnosticCode::kInputIdentity,
          "Report input identity accepted a locator");
  request.input_id = kInputId;
  const std::array invalid_name_bytes{static_cast<char>(0xff)};
  const std::array declarations{repair::RowLocalAttributeDeclaration{
      geo::AttributeDomain::vertex,
      std::string_view(invalid_name_bytes.data(), invalid_name_bytes.size())}};
  request.repair.row_local_attributes = declarations;
  auto invalid_encoding =
      repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
  Require(invalid_encoding.status ==
                  repair::GltfWorkflowStatus::kInvalidConfiguration &&
              invalid_encoding.diagnostic.code ==
                  repair::GltfWorkflowDiagnosticCode::kReportStringEncoding,
          "Invalid UTF-8 declaration could escape into an owned UTF-8 report");
  Require(context.ActiveTrackedPayload() == 0,
          "Configuration failure allocated live workflow storage");
}
void ChannelsAndAliases(const std::filesystem::path& reports) {
  constexpr std::array kPngSignature{
      std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
      std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
  Fixture fixture;
  fixture.json =
      R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":104}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":24},{"buffer":0,"byteOffset":72,"byteLength":12},{"buffer":0,"byteOffset":84,"byteLength":12},{"buffer":0,"byteOffset":96,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"normalized":true,"count":3,"type":"VEC2"},{"bufferView":4,"componentType":5121,"normalized":true,"count":3,"type":"VEC4"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":2,"TEXCOORD_1":3,"COLOR_0":4},"indices":1}]}],"images":[{"bufferView":5,"mimeType":"image/png"},{"uri":"data:image/png;base64,iVBORw0KGgo="},{"uri":"color.png"}],"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],"extras":{"opaque":-0.000e+00}})";
  for (float value : std::array{0.f, 0.f, 1.f, 0.f, 0.f, 1.f}) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (std::size_t byte = 0; byte < kFloatBytes; ++byte)
      fixture.binary.push_back(
          static_cast<std::byte>((bits >> (kByteBits * byte)) & kByteMask));
  }
  for (std::uint16_t value :
       std::array<std::uint16_t, 6>{0, 0, 65535, 0, 0, 65535}) {
    fixture.binary.push_back(static_cast<std::byte>(value & kByteMask));
    fixture.binary.push_back(static_cast<std::byte>(value >> kByteBits));
  }
  for (std::uint8_t value : std::array<std::uint8_t, 12>{
           255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255})
    fixture.binary.push_back(static_cast<std::byte>(value));
  // Image aliases remain opaque and use a view with no selected vertex role.
  for (auto value : kPngSignature) fixture.binary.push_back(value);
  geo::ExecutionContext context({kWorkers, 1, kLedgerCap});
  const std::array targets{repair::DuplicateTarget{0, 1}};
  const std::array bindings{
      ic::SuppliedGltfResource{ic::GltfResourceKind::kBuffer, 0,
                               fixture.binary},
      ic::SuppliedGltfResource{ic::GltfResourceKind::kImage, 2, kPngSignature}};
  repair::GltfDuplicateWorkflowRequest request{
      Bytes(fixture.json),
      bindings,
      kInputId,
      0,
      {ic::GltfTriangleRequest::kRequireTriangles,
       ic::GltfValueRequest::kRawAndNormalized},
      targets,
      {}};
  auto unsupported =
      repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
  if (const auto* delegated =
          std::get_if<ic::GltfDiagnostic>(&unsupported.diagnostic.delegated);
      delegated &&
      unsupported.diagnostic.stage != repair::GltfWorkflowStage::kRepair)
    throw std::runtime_error(
        "undeclared fixture failed before Repair: status=" +
        std::to_string(static_cast<int>(delegated->status)) +
        " code=" + std::to_string(static_cast<int>(delegated->code)) +
        " rule=" + std::to_string(delegated->rule_id) + " resource=" +
        (delegated->resource_index ? std::to_string(*delegated->resource_index)
                                   : "none"));
  if (unsupported.status != repair::GltfWorkflowStatus::kRejected ||
      !unsupported.record ||
      unsupported.record->Coverage()[1].state !=
          repair::GltfWorkflowCoverageState::kUnsupported)
    throw std::runtime_error(
        "undeclared channel policy mismatch: status=" +
        std::to_string(static_cast<int>(unsupported.status)) + " stage=" +
        std::to_string(static_cast<int>(unsupported.diagnostic.stage)) +
        " code=" +
        std::to_string(static_cast<int>(unsupported.diagnostic.code)));
  Save(reports, "undeclared-channels.json", unsupported);
  const std::array declarations{repair::RowLocalAttributeDeclaration{
                                    geo::AttributeDomain::vertex, "TEXCOORD_0"},
                                repair::RowLocalAttributeDeclaration{
                                    geo::AttributeDomain::vertex, "TEXCOORD_1"},
                                repair::RowLocalAttributeDeclaration{
                                    geo::AttributeDomain::vertex, "COLOR_0"}};
  request.repair.row_local_attributes = declarations;
  auto accepted =
      repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
  if (accepted.status != repair::GltfWorkflowStatus::kSuccess)
    throw std::runtime_error(
        "channel/alias workflow failed: stage=" +
        std::to_string(static_cast<int>(accepted.diagnostic.stage)) +
        " code=" + std::to_string(static_cast<int>(accepted.diagnostic.code)));
  Require(accepted.record->ExtractedPrimitive()->Channels().size() == 4 &&
              accepted.record->ReloadedPrimitive()->Channels().size() == 4,
          "Channel set count changed across composition");
  Require(accepted.record->ReadAsset()->Resources().size() == 4 &&
              accepted.record->ReloadedAsset()->Resources().size() == 5,
          "Image aliases/data URI resources lost across composition");
  Require(accepted.record->ReloadExtractionPolicy()->values ==
              ic::GltfValueRequest::kRawAndNormalized,
          "Caller normalization view policy was discarded during reload");
  Save(reports, "channels-and-aliases.json", accepted);
}
void ConvertedTopology(const std::filesystem::path& reports) {
  constexpr std::uint64_t kStripMode = 5;
  constexpr std::uint64_t kFanMode = 6;
  constexpr std::uint64_t kTrianglesMode = 4;
  constexpr std::string_view kIndexBinding = "\"indices\":1}";
  for (auto mode : {kStripMode, kFanMode}) {
    Fixture fixture;
    const auto binding = fixture.json.find(kIndexBinding);
    Require(binding != std::string::npos, "Missing fixture index binding");
    fixture.json.replace(
        binding, kIndexBinding.size(),
        "\"indices\":1,\"mode\":" + std::to_string(mode) + "}");
    geo::ExecutionContext context({kWorkers, 1, kLedgerCap});
    const std::array bindings{ic::SuppliedGltfResource{
        ic::GltfResourceKind::kBuffer, 0, fixture.binary}};
    const std::array targets{
        repair::DuplicateTarget{0, mode == kStripMode ? 2u : 3u}};
    repair::GltfDuplicateWorkflowRequest request{
        Bytes(fixture.json),
        bindings,
        kInputId,
        0,
        {ic::GltfTriangleRequest::kConvertStripOrFan,
         ic::GltfValueRequest::kRaw},
        {},
        {}};
    auto noop =
        repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
    Require(
        noop.status == repair::GltfWorkflowStatus::kSuccess && noop.record &&
            noop.record->ReloadExtractionPolicy()->triangles ==
                ic::GltfTriangleRequest::kConvertStripOrFan &&
            noop.record->ReloadedPrimitive()->SourceMode() == mode,
        "Identity conversion policy/source mode was replaced during reload");
    Save(reports,
         mode == kStripMode ? "strip-unchanged.json" : "fan-unchanged.json",
         noop);
    request.targets = targets;
    auto changed =
        repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
    if (changed.status != repair::GltfWorkflowStatus::kSuccess)
      throw std::runtime_error(
          "converted workflow failed: stage=" +
          std::to_string(static_cast<int>(changed.diagnostic.stage)) +
          " code=" + std::to_string(static_cast<int>(changed.diagnostic.code)));
    Require(
        changed.record->ReloadExtractionPolicy()->triangles ==
                ic::GltfTriangleRequest::kRequireTriangles &&
            changed.record->ReloadedPrimitive()->SourceMode() == kTrianglesMode,
        "Changed strip/fan did not require independently observed TRIANGLES");
    Require(changed.record->OutputInspection()->OriginalCorners().size() == 9,
            "Converted source-corner provenance was lost");
    Save(reports,
         mode == kStripMode ? "strip-changed.json" : "fan-changed.json",
         changed);
  }
}
void UnsupportedProfile() {
  Fixture fixture;
  const auto original_json = fixture.json;
  const auto original_bytes = fixture.binary;
  geo::ExecutionContext context({kWorkers, 1, kLedgerCap});
  const std::array bindings{ic::SuppliedGltfResource{
      ic::GltfResourceKind::kBuffer, 0, fixture.binary}};
  const std::array targets{repair::DuplicateTarget{0, 1}};
  const repair::GltfDuplicateWorkflowRequest request{
      Bytes(fixture.json),
      bindings,
      kInputId,
      0,
      {ic::GltfTriangleRequest::kRequireTriangles, ic::GltfValueRequest::kRaw},
      targets,
      {}};
  {
    const auto result =
        repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
    const auto* delegated = std::get_if<ic::GltfMaterializationDiagnostic>(
        &result.diagnostic.delegated);
    const auto* delegated_status = std::get_if<ic::GltfMaterializationStatus>(
        &result.diagnostic.delegated_status);
    Require(
        result.status == repair::GltfWorkflowStatus::kDelegatedFailure &&
            result.diagnostic.stage ==
                repair::GltfWorkflowStage::kMaterialize &&
            result.diagnostic.code ==
                repair::GltfWorkflowDiagnosticCode::kMaterialize &&
            delegated && delegated_status &&
            *delegated_status ==
                ic::GltfMaterializationStatus::kGeometryFailure &&
            delegated->stage == ic::GltfMaterializationStage::kCapture &&
            delegated->code ==
                ic::GltfMaterializationDiagnosticCode::kGeometry &&
            delegated->geometry_status ==
                geo::MaterializationStatus::kUnsupportedAllocationProfile &&
            delegated->geometry_diagnostic &&
            delegated->geometry_diagnostic->code ==
                geo::MaterializationDiagnosticCode::kAllocationProfile,
        "Unsupported route requires the actual delegated Geometry profile "
        "failure; this option cannot make a qualified producer unsupported");
    Require(
        result.record && result.record->ReadAsset() &&
            result.record->ExtractedPrimitive() && !result.record->Source() &&
            !result.record->Repair() && !result.record->Output() &&
            !result.outcomes.candidate &&
            result.outcomes.execution ==
                repair::GltfWorkflowExecutionOutcome::kFailed &&
            result.outcomes.profile ==
                repair::GltfWorkflowProfileOutcome::kIncomplete &&
            result.outcomes.exit ==
                repair::GltfWorkflowExitMeaning::kExecutionFailure &&
            Text(result.record->ReportJson())
                    .find("\"publication\":{\"requested\":false") !=
                std::string_view::npos,
        "Unsupported profile lost native failure facts or published output");
    Require(fixture.json == original_json && fixture.binary == original_bytes &&
                context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() ==
                    result.record->NewRequestedBytes().total,
            "Unsupported profile changed caller storage or retained untracked "
            "charges/workers");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "Unsupported profile leaked returned workflow storage");
}
void DirectTranslationFailures() {
  namespace moving = repair::test_translation_workflow;
  geo::ExecutionContext context({moving::kWorkers, 1, moving::kLedgerBytes});
  const auto run = [&](moving::MovingFixture& data, const auto& limits) {
    const auto resources = data.Resources();
    const auto original = data.binary;
    const auto json = data.json;
    auto result = repair::RunGltfDuplicateWorkflow(
        moving::Request(data, resources), limits, moving::Work(), context);
    Require(
        data.binary == original && data.json == json &&
            context.ActiveWorkers() == 0,
        "translation failures preserve caller source and join actual workers");
    return result;
  };
  const auto replace = [](std::string& text, std::string_view old,
                          std::string_view value) {
    const auto at = text.find(old);
    Require(at != std::string::npos, "original authored mutation site");
    text.replace(at, old.size(), value);
  };
  for (bool ancestor : {false, true}) {
    moving::MovingFixture data;
    if (ancestor)
      replace(data.json, R"("node":1,"path":"translation")",
              R"("node":0,"path":"translation")");
    else {
      replace(data.json, R"("children":[1,2])", R"("children":[1])");
      replace(data.json, R"("node":2,"path":"translation")",
              R"("node":2,"path":"scale")");
    }
    const auto result = run(data, moving::Limits());
    Require(
        result.status == repair::GltfWorkflowStatus::kUnsupported &&
            result.translation.source && !result.translation.source->complete &&
            !result.translation.writing && result.record &&
            result.record->Coverage().back().state ==
                repair::GltfWorkflowCoverageState::kUnsupported,
        "actual hidden user or strict ancestor prevents translation repair");
  }
  {
    moving::MovingFixture data;
    // Core-valid four-row JOINTS_0 / WEIGHTS_0 with one joint and default
    // identity inverse binds; only one of the two shared users selects a skin.
    constexpr std::size_t kTailBytes = 8, kVertexRows = 4, kVectorWidth = 4;
    constexpr std::uint32_t kOneBits = 0x3f800000;
    std::vector<std::byte> attributes(kVertexRows * kVectorWidth, std::byte{});
    for (std::size_t row = 0; row < kVertexRows; ++row) {
      moving::Word(attributes, kOneBits);
      for (std::size_t scalar = 1; scalar < kVectorWidth; ++scalar)
        moving::Word(attributes, 0);
    }
    data.binary.insert(data.binary.end() - kTailBytes, attributes.begin(),
                       attributes.end());
    replace(data.json, R"("byteLength":148)", R"("byteLength":228)");
    replace(
        data.json, R"(],"accessors":[)",
        R"(,{"buffer":0,"byteOffset":148,"byteLength":16},{"buffer":0,"byteOffset":164,"byteLength":64}],"accessors":[)");
    replace(
        data.json,
        R"({"bufferView":5,"componentType":5126,"count":4,"type":"VEC2"}])",
        R"({"bufferView":5,"componentType":5126,"count":4,"type":"VEC2"},{"bufferView":6,"componentType":5121,"count":4,"type":"VEC4"},{"bufferView":7,"componentType":5126,"count":4,"type":"VEC4"}])");
    replace(data.json, R"("POSITION":0,"TEXCOORD_0":5)",
            R"("POSITION":0,"TEXCOORD_0":5,"JOINTS_0":6,"WEIGHTS_0":7)");
    replace(data.json, R"({"mesh":0,"translation":[1,0,0]})",
            R"({"mesh":0,"skin":0,"translation":[1,0,0]})");
    replace(data.json, R"("scenes":[)",
            R"("skins":[{"joints":[3]}],"scenes":[)");
    const auto result = run(data, moving::Limits());
    Require(result.status == repair::GltfWorkflowStatus::kUnsupported &&
                result.translation.source && !result.translation.writing,
            "actual skin on a shared user refuses new profile");
  }
  {
    moving::MovingFixture data;
    replace(data.json, R"("indices":1,"material":0)",
            R"("indices":1,"material":0,"targets":[{"POSITION":0}])");
    const auto result = run(data, moving::Limits());
    Require(result.status == repair::GltfWorkflowStatus::kUnsupported &&
                result.translation.source && !result.translation.writing,
            "actual morph declaration refuses new profile");
  }
  {
    moving::MovingFixture data;
    const auto resources = data.Resources();
    auto request = moving::Request(data, resources);
    request.repair.row_local_attributes = {};
    const auto result = repair::RunGltfDuplicateWorkflow(
        request, moving::Limits(), moving::Work(), context);
    const auto* diagnostic =
        std::get_if<repair::MaterializedDuplicateDiagnostic>(
            &result.diagnostic.delegated);
    Require(
        result.status == repair::GltfWorkflowStatus::kRejected && diagnostic &&
            diagnostic->stage ==
                repair::MaterializedDuplicateStage::kEligibility &&
            diagnostic->code == repair::MaterializedDuplicateDiagnosticCode::
                                    kUnsupportedAttribute &&
            diagnostic->attribute == 0 && result.translation.source &&
            result.translation.source->complete &&
            !result.translation.writing && result.record &&
            result.record->Source() &&
            result.outcomes.exit ==
                repair::GltfWorkflowExitMeaning::kRejectedOrIncomplete,
        "removing the explicit raw UV declaration keeps genuine old "
        "eligibility refusal");
    const auto attributes = result.record->Source()->Mesh().View().attributes;
    Require(!attributes.empty(), "actual rejected source attribute exists");
    const auto& attribute = attributes[0];
    Require(attribute.domain == geo::AttributeDomain::vertex &&
                attribute.name == "TEXCOORD_0" &&
                attribute.semantic == "TEXCOORD_0" && attribute.components == 2,
            "actual rejected attribute is the declared original UV row, not an "
            "inferred channel");
  }
  {
    moving::MovingFixture data;
    constexpr std::size_t kSecondTimeByte = 64;
    std::fill_n(data.binary.begin() + kSecondTimeByte, moving::kFloatBytes,
                std::byte{});
    const auto result = run(data, moving::Limits());
    Require(
        result.status == repair::GltfWorkflowStatus::kDelegatedFailure &&
            result.translation.source &&
            result.translation.source->status == ic::GltfStatus::kMalformed &&
            result.record && result.record->Coverage().back().passed == false,
        "actual duplicate time is malformed, evaluated translation predicate "
        "false");
  }
  for (bool inspection : {false, true}) {
    moving::MovingFixture data;
    auto limits = moving::Limits();
    if (inspection)
      limits.verification.direct_translation.max_total_keys = 1;
    else
      limits.writing.direct_translation.max_total_keys = 1;
    const auto result = run(data, limits);
    const auto& failed = inspection ? result.translation.verification
                                    : result.translation.writing;
    Require(
        failed && failed->status == ic::GltfStatus::kUnsupportedProfile &&
            result.status ==
                (inspection ? repair::GltfWorkflowStatus::kVerificationFailure
                            : repair::GltfWorkflowStatus::kDelegatedFailure) &&
            result.record && result.record->Repair() &&
            result.record->Coverage().back().state ==
                repair::GltfWorkflowCoverageState::kFailed &&
            !result.record->Coverage().back().passed,
        "real W/V logical count failure keeps actual internal and outer "
        "projections distinct");
  }
  {
    moving::MovingFixture data;
    auto limits = moving::Limits();
    limits.max_report_bytes = 1;
    const auto result = run(data, limits);
    moving::CheckFacts(result.translation, false);
    Require(result.status == repair::GltfWorkflowStatus::kLimitExceeded &&
                !result.record && result.retained_repair &&
                result.diagnostic.stage == repair::GltfWorkflowStage::kReport,
            "real report refusal retains all completed translation and Repair "
            "facts without JSON");
    moving::CheckRepair(*result.retained_repair, true);
  }
  {
    moving::MovingFixture data;
    auto limits = moving::Limits();
    limits.report_publication_reload.max_total_keys = 0;
    const auto result = run(data, limits);
    Require(
        result.status == repair::GltfWorkflowStatus::kInvalidConfiguration &&
            !result.translation.source && !result.record,
        "explicit carrier admission precedes all asset work");
  }
  {
    moving::MovingFixture data;
    const auto resources = data.Resources();
    std::stop_source stop;
    stop.request_stop();
    const auto result = repair::RunGltfDuplicateWorkflow(
        moving::Request(data, resources), moving::Limits(), moving::Work(),
        context, stop.get_token());
    Require(result.status == repair::GltfWorkflowStatus::kCanceled &&
                !result.translation.source && !result.record,
            "actual pre-entry cancellation has no invented validator facts");
  }
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all new failure owners and leases released");
}

}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == kUnsupportedProfileOption) {
      UnsupportedProfile();
      std::cout << "Actual unsupported Geometry workflow profile passed\n";
      return 0;
    }
    Require(argc <= 2,
            "Expected an optional report directory or profile option");
    std::filesystem::path reports;
    if (argc == 2) {
      reports = argv[1];
      std::filesystem::create_directories(reports);
    }
    SuccessAndLifetime(reports);
    FailuresAndNoOp(reports);
    LedgerAndConfiguration();
    ChannelsAndAliases(reports);
    ConvertedTopology(reports);
    repair::test_translation_workflow::MemoryPositive();
    DirectTranslationFailures();
    std::cout << "Reported native glTF workflow checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
