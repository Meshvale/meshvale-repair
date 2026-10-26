// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_workflow.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>

#include "../../tests/gltf_translation_workflow_fixture.h"

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace repair = meshvale::repair;
constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
constexpr std::uint64_t kStageBytes = 4 * kPhysicalLimit;
constexpr std::uint64_t kWorkflowBytes = 64 * kPhysicalLimit;
constexpr std::uint64_t kLedgerBytes = 128 * kPhysicalLimit;
constexpr std::uint64_t kIdentityBytes = 256;
constexpr std::uint64_t kDocumentDepth = 64;
constexpr std::uint64_t kParallelMinimum = 128;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCancellationBlock = 32;
constexpr std::uint64_t kMaximumJobs = 64;
constexpr std::size_t kWorkers = 2;
constexpr std::size_t kPositionBytes = 36;
constexpr std::size_t kSourceBytes = 48;
constexpr std::size_t kFloatBytes = 4;
constexpr std::size_t kByteBits = 8;
constexpr std::uint32_t kByteMask = 255;
constexpr std::string_view kInputId = "installed-workflow";
constexpr std::string_view kUnsupportedProfileOption = "--unsupported-profile";
constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":48}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],"extras":{"opaque":1.2300e+04}})";

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
geo::MeshMaterializationLimits CaptureLimits() {
  return {kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
          kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
          kPhysicalLimit, kPhysicalLimit, kStageBytes};
}
repair::GltfDuplicateWorkflowLimits Limits() {
  repair::GltfDuplicateWorkflowLimits limits;
  limits.max_new_requested_bytes = kWorkflowBytes;
  limits.max_input_id_bytes = kIdentityBytes;
  limits.max_reload_bindings = kPhysicalLimit;
  limits.max_report_bytes = kPhysicalLimit;
  const ic::GltfReadLimits read{kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                                kPhysicalLimit, kDocumentDepth, kPhysicalLimit,
                                kIdentityBytes, kIdentityBytes, kIdentityBytes,
                                kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                                kPhysicalLimit, kStageBytes};
  limits.read = limits.reload = {read, kStageBytes};
  limits.extraction =
      limits.reload_extraction = {kPhysicalLimit, kPhysicalLimit, kStageBytes};
  limits.materialization = {kPhysicalLimit,
                            kStageBytes,
                            CaptureLimits(),
                            {kPhysicalLimit, kStageBytes}};
  limits.repair = {kPhysicalLimit, kPhysicalLimit,
                   kPhysicalLimit, kPhysicalLimit,
                   kStageBytes,    {kPhysicalLimit, kStageBytes},
                   CaptureLimits()};
  limits.writing = {kPhysicalLimit,
                    kPhysicalLimit,
                    kDocumentDepth,
                    kPhysicalLimit,
                    kPhysicalLimit,
                    kPhysicalLimit,
                    kPhysicalLimit,
                    kPhysicalLimit,
                    kPhysicalLimit,
                    kPhysicalLimit,
                    {kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                     kPhysicalLimit, kStageBytes}};
  limits.verification = {kPhysicalLimit, kPhysicalLimit, kDocumentDepth,
                         kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                         kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                         kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
                         kStageBytes};
  return limits;
}
repair::GltfDuplicateWorkflowWorkPolicy Work() {
  return {
      {kParallelMinimum, kPartitionBytes, kParallelMinimum, kPartitionBytes},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs}};
}
std::array<std::byte, kSourceBytes> SourceBuffer() {
  std::array<std::byte, kSourceBytes> bytes{};
  constexpr std::array positions{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const auto bits = std::bit_cast<std::uint32_t>(positions[i]);
    for (std::size_t byte = 0; byte < kFloatBytes; ++byte)
      bytes[i * kFloatBytes + byte] =
          static_cast<std::byte>((bits >> (byte * kByteBits)) & kByteMask);
  }
  constexpr std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 1, 2};
  for (std::size_t i = 0; i < indices.size(); ++i)
    bytes[kPositionBytes + i * sizeof(std::uint16_t)] =
        static_cast<std::byte>(indices[i]);
  return bytes;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == kUnsupportedProfileOption;
    Require(argc == 1 || unsupported,
            "Expected no option or --unsupported-profile");
    std::optional<repair::GltfDuplicateWorkflowRecord> retained;
    {
      auto source = SourceBuffer();
      const auto original = source;
      const std::array resources{
          ic::SuppliedGltfResource{ic::GltfResourceKind::kBuffer, 0, source}};
      const std::array targets{repair::DuplicateTarget{0, 1}};
      const repair::GltfDuplicateWorkflowRequest request{
          std::as_bytes(std::span(kJson.data(), kJson.size())),
          resources,
          kInputId,
          0,
          {ic::GltfTriangleRequest::kRequireTriangles,
           ic::GltfValueRequest::kRaw},
          targets,
          {}};
      const geo::ExecutionContext context({kWorkers, 1, kLedgerBytes});
      auto result =
          repair::RunGltfDuplicateWorkflow(request, Limits(), Work(), context);
      Require(result.record.has_value(), "Installed workflow lost its report");
      if (unsupported) {
        const auto* delegated = std::get_if<ic::GltfMaterializationDiagnostic>(
            &result.diagnostic.delegated);
        const auto* native_status = std::get_if<ic::GltfMaterializationStatus>(
            &result.diagnostic.delegated_status);
        Require(
            result.status == repair::GltfWorkflowStatus::kDelegatedFailure &&
                result.diagnostic.stage ==
                    repair::GltfWorkflowStage::kMaterialize &&
                result.diagnostic.code ==
                    repair::GltfWorkflowDiagnosticCode::kMaterialize &&
                native_status &&
                *native_status ==
                    ic::GltfMaterializationStatus::kGeometryFailure &&
                delegated &&
                delegated->stage == ic::GltfMaterializationStage::kCapture &&
                delegated->code ==
                    ic::GltfMaterializationDiagnosticCode::kGeometry &&
                delegated->geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile &&
                delegated->geometry_diagnostic &&
                delegated->geometry_diagnostic->code ==
                    geo::MaterializationDiagnosticCode::kAllocationProfile &&
                !result.outcomes.candidate &&
                result.outcomes.execution ==
                    repair::GltfWorkflowExecutionOutcome::kFailed &&
                result.outcomes.profile ==
                    repair::GltfWorkflowProfileOutcome::kIncomplete &&
                result.outcomes.exit ==
                    repair::GltfWorkflowExitMeaning::kExecutionFailure,
            "Installed unsupported route did not retain the actual Geometry "
            "profile failure");
      } else {
        Require(
            result.status == repair::GltfWorkflowStatus::kSuccess &&
                result.record->Output() && result.record->OutputInspection() &&
                result.outcomes.candidate == repair::CandidateOutcome::accepted,
            "Installed workflow failed to verify changed memory output");
      }
      Require(context.ActiveWorkers() == 0 &&
                  result.record->NewRequestedBytes().total <= kWorkflowBytes &&
                  context.ActiveTrackedPayload() ==
                      result.record->NewRequestedBytes().total &&
                  source == original,
              "Installed workflow changed input, exceeded allowance or left "
              "untracked storage/native workers");
      retained = *result.record;
    }
    if (unsupported) {
      const auto expected_bytes = SourceBuffer();
      Require(
          retained->ReadAsset() && retained->ExtractedPrimitive() &&
              !retained->Source() && !retained->Repair() &&
              !retained->Output() && !retained->OutputInspection() &&
              retained->ReadAsset()->Resources().size() == 1 &&
              std::equal(retained->ReadAsset()->Resources()[0].bytes.begin(),
                         retained->ReadAsset()->Resources()[0].bytes.end(),
                         expected_bytes.begin(), expected_bytes.end()),
          "Copied unsupported record lost source ownership or exposed a "
          "candidate/output");
      Require(Text(retained->ReportJson())
                          .find("\"schema\":\"meshvale.report/1\"") !=
                      std::string_view::npos &&
                  Text(retained->ReportJson())
                          .find("\"native_stage\":\"materialize\"") !=
                      std::string_view::npos &&
                  Text(retained->ReportJson())
                          .find("\"memory_output_verified\":false") !=
                      std::string_view::npos &&
                  Text(retained->ReportJson())
                          .find("\"publication\":{\"requested\":false") !=
                      std::string_view::npos,
              "Copied unsupported record lost truthful report facts");
      std::cout << "Installed actual unsupported Geometry workflow passed\n";
      return 0;
    }
    Require(retained->Repair() && retained->ReloadedAsset() &&
                retained->ReloadedPrimitive() && retained->Output() &&
                retained->OutputInspection(),
            "Copied workflow record lost strong owners");
    const auto* edit = &*retained->Repair();
    Require(edit->Candidate().View().corner_vertices.size() == 3 &&
                edit->FaceMap().size() == 2 && edit->FaceMap()[0] == 0 &&
                edit->FaceMap()[1] == 0 &&
                retained->ReadAsset()->InstanceCount() == 2 &&
                retained->ReloadedAsset()->InstanceCount() == 2,
            "Copied workflow lost inverse correspondence or shared instances");
    Require(Text(retained->Output()->Json()).find("1.2300e+04") !=
                    std::string_view::npos &&
                Text(retained->ReportJson())
                        .find("\"schema\":\"meshvale.report/1\"") !=
                    std::string_view::npos &&
                Text(retained->ReportJson())
                        .find("\"memory_output_verified\":true") !=
                    std::string_view::npos,
            "Copied workflow lost exact lexemes or native report receipts");
    if (!unsupported)
      meshvale::repair::test_translation_workflow::MemoryPositive();
    std::cout << "Installed reported glTF workflow consumer passed\n";
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
