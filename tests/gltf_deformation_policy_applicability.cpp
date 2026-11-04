// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>

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
constexpr std::uint64_t kMaximumPhysicalFiles = 1024;
constexpr std::uint64_t kMaximumResourceBindings = 1024;
constexpr std::uint64_t kTotalRelativePathBytes = 65536;
constexpr std::uint64_t kNativePathCodeUnits = 65536;
constexpr ic::GltfDeformationPolicy kDeformation{
    {65536, 65536, 65536, 65536, 16, 64, 65536, 65536, 65536, 65536, 65536,
     kPhysicalLimit, 65536, 65536, kStageBytes, kStageBytes, kStageBytes,
     kStageBytes},
    {64, 64, 64, 64, 64, 64, 64, 16}};
void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
geo::MeshMaterializationLimits CaptureLimits() {
  return {kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
          kPhysicalLimit, kPhysicalLimit, kPhysicalLimit, kPhysicalLimit,
          kPhysicalLimit, kPhysicalLimit, kStageBytes};
}
repair::GltfDuplicateWorkflowLimits MemoryLimits() {
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
repair::GltfDuplicateWorkflowWorkPolicy MemoryWork() {
  return {
      {kParallelMinimum, kPartitionBytes, kParallelMinimum, kPartitionBytes},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs},
      {kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs}};
}
ic::GltfFileLimits FileLimits() {
  return {kMaximumPhysicalFiles, kMaximumResourceBindings,
          kPhysicalLimit,        kStageBytes,
          kIdentityBytes,        kTotalRelativePathBytes,
          kNativePathCodeUnits,  kStageBytes};
}
ic::GltfFileReadLimits FileReadLimits() {
  return {FileLimits(),
          MemoryLimits().read,
          {kPhysicalLimit, kPhysicalLimit, kStageBytes}};
}
ic::GltfFileReadWorkPolicy FileReadWork() {
  return {{kPartitionBytes, kParallelMinimum, kPartitionBytes,
           kCancellationBlock, kMaximumJobs},
          MemoryWork().interchange,
          {kParallelMinimum, kPartitionBytes}};
}
repair::GltfDuplicateFileWorkflowLimits Limits() {
  const auto existing = MemoryLimits();
  repair::GltfDuplicateFileWorkflowLimits limits;
  limits.memory.max_new_requested_bytes = existing.max_new_requested_bytes;
  limits.memory.max_input_id_bytes = existing.max_input_id_bytes;
  limits.memory.max_source_protection_bytes = kStageBytes;
  limits.memory.max_source_binding_records = kPhysicalLimit;
  limits.memory.max_reload_bindings = existing.max_reload_bindings;
  limits.memory.max_report_bytes = existing.max_report_bytes;
  limits.memory.extraction = existing.extraction;
  limits.memory.materialization = existing.materialization;
  limits.memory.repair = existing.repair;
  limits.memory.writing = existing.writing;
  limits.memory.reload = existing.reload;
  limits.memory.reload_extraction = existing.reload_extraction;
  limits.memory.verification = existing.verification;
  limits.max_wrapper_new_requested_bytes = kWorkflowBytes;
  limits.max_source_files = kPhysicalLimit;
  limits.max_supplemental_files = kPhysicalLimit;
  limits.max_report_path_bytes = kIdentityBytes;
  limits.max_report_bytes = kPhysicalLimit;
  limits.max_hash_input_bytes = kStageBytes;
  limits.publication = {FileLimits(),
                        existing.reload,
                        existing.reload_extraction,
                        existing.verification,
                        {kPhysicalLimit, kPhysicalLimit, kStageBytes}};
  return limits;
}
repair::GltfDuplicateFileWorkflowWorkPolicy Work() {
  const auto files = FileReadWork();
  const repair::GltfWorkflowByteWorkPolicy bytes{
      kParallelMinimum, kPartitionBytes, kCancellationBlock, kMaximumJobs};
  return {
      MemoryWork(),
      bytes,
      bytes,
      {files.files, files.asset, MemoryWork().verification, files.container}};
}

void Exercise(const std::filesystem::path& path,
              const geo::ExecutionContext& context) {
  ic::GltfFileReadOptions options;
  options.read_request = {ic::GltfAdmissionProfile::kRetainedCoreDeformationV1,
                          kDeformation};
  auto source = ic::ReadGltfFileAsset(path, options, FileReadLimits(),
                                      FileReadWork(), context);
  Require(source.asset.has_value(),
          "real supported retained deformation asset");
  const auto baseline = context.ActiveTrackedPayload();
  ic::GltfExtractionRequest extraction{
      ic::GltfTriangleRequest::kRequireTriangles,
      ic::GltfValueRequest::kRawAndNormalized,
      ic::GltfProcessingProfile::kDeformationRetainedTrianglesV1,
      kDeformation,
      {}};
  repair::GltfDuplicateFileWorkflowRequest retained{
      *source.asset, "retained-zero-policy", 0, extraction, {}, {}};
  retained.deformation = {{},           kDeformation, kDeformation,
                          kDeformation, kDeformation, kDeformation};
  {
    auto result = repair::RunGltfDuplicateFileWorkflow(retained, Limits(),
                                                       Work(), context, {});
    Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                result.record && result.record->Memory() &&
                result.record->Memory()->OutputInspection(),
            "retained source_read zero succeeds through real verification");
    const auto* facts = result.record->DeformationFacts();
    Require(facts && !facts->source_read && facts->capture_read &&
                facts->source && facts->writing && facts->verification &&
                facts->verification->predicates.output_reload == true,
            "retained source read remains genuinely uncalled/null");
  }
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "retained result and joins release to actual source baseline");
  std::stop_source stopped;
  stopped.request_stop();
  {
    repair::GltfDuplicateWorkflowRequest request{
        source.asset->Asset().OriginalJson(),
        {},
        "nonretained-zero-policy",
        0,
        extraction,
        {},
        {}};
    request.deformation = retained.deformation;
    auto result = repair::RunGltfDuplicateWorkflow(
        request, MemoryLimits(), MemoryWork(), context, stopped.get_token());
    Require(
        result.status == repair::GltfWorkflowStatus::kInvalidConfiguration &&
            result.diagnostic.stage ==
                repair::GltfWorkflowStage::kConfiguration &&
            result.diagnostic.code ==
                repair::GltfWorkflowDiagnosticCode::kRequiredFieldZero &&
            (!result.deformation || !result.deformation->source_read),
        "nonretained source_read zero still fails before stop/work");
  }
  for (const bool extraction_policy : {false, true}) {
    auto request = retained;
    if (extraction_policy)
      request.extraction.deformation = {};
    else
      request.deformation.materialization = {};
    auto result = repair::RunGltfDuplicateFileWorkflow(
        request, Limits(), Work(), context, stopped.get_token());
    Require(
        result.status == repair::GltfWorkflowStatus::kInvalidConfiguration &&
            result.diagnostic.stage ==
                repair::GltfFileWorkflowStage::kConfiguration &&
            result.diagnostic.code ==
                repair::GltfFileWorkflowDiagnosticCode::kRequiredFieldZero &&
            (!result.deformation || (!result.deformation->source_read &&
                                     !result.deformation->materialization)),
        "applicable policy zero still fails configuration before stop/work");
  }
  Require(context.ActiveTrackedPayload() == baseline &&
              context.ActiveWorkers() == 0,
          "all invalid configurations leave original source/ledger intact");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "public fixture path");
    const geo::ExecutionContext context({4, 1, kLedgerBytes});
    Exercise(argv[1], context);
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "all source/result owners released");
    std::cout << "retained-zero success/uncalled; nonretained and two "
                 "applicable-zero refusals PASS; workers=0 payload=0\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
