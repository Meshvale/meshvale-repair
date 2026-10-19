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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

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

namespace fs = std::filesystem;
constexpr std::string_view kEntryPath = "source.gltf";
constexpr std::string_view kReportPath = "receipts/report.json";
constexpr std::string_view kReportSchema = "\"schema\":\"meshvale.report/1\"";
constexpr std::size_t kExpectedPhysicalFiles = 3;
constexpr std::uint64_t kMaximumPhysicalFiles = 32;
constexpr std::uint64_t kMaximumResourceBindings = 32;
// Native path extent also bounds preallocated rollback traversal storage.
constexpr std::uint64_t kNativePathCodeUnits = 4096;
constexpr std::uint64_t kTotalRelativePathBytes = 4096;
constexpr std::array<std::uint32_t, 3> kExpectedTriangle{0, 1, 2};
class Fixture {
 public:
  Fixture() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = fs::canonical(fs::temp_directory_path()) /
            ("meshvale-installed-repair-gltf-file-" + std::to_string(stamp));
    Require(fs::create_directory(root_), "Create fixture root");
  }
  ~Fixture() noexcept {
    std::error_code error;
    fs::remove_all(root_, error);
  }
  const fs::path& Root() const noexcept { return root_; }

 private:
  fs::path root_;
};
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(static_cast<bool>(stream), "Write fixture bytes");
}
std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  Require(static_cast<bool>(stream), "Read committed receipt");
  const auto size = stream.tellg();
  Require(size >= 0, "Receipt extent");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(static_cast<bool>(stream), "Read complete receipt bytes");
  return bytes;
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
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == kUnsupportedProfileOption;
    Require(argc == 1 || unsupported,
            "Expected no option or --unsupported-profile");
    Fixture fixture;
    std::optional<repair::GltfDuplicateFileWorkflowRecord> retained;
    {
      const auto input = fixture.Root() / "input";
      Require(fs::create_directory(input), "Create input directory");
      auto bytes = SourceBuffer();
      Write(input / kEntryPath, std::as_bytes(std::span(kJson)));
      Write(input / "mesh.bin", bytes);
      const geo::ExecutionContext context({kWorkers, 1, kLedgerBytes});
      ic::GltfFileReadOptions source_options;
      source_options.resource_root = input;
      auto captured =
          ic::ReadGltfFileAsset(input / kEntryPath, source_options,
                                FileReadLimits(), FileReadWork(), context);
      Require(captured.asset.has_value(), "Capture owned source files");
      const auto source = *captured.asset;
      captured.asset.reset();
      Require(fs::remove(input / kEntryPath) &&
                  fs::remove(input / "mesh.bin") && fs::remove(input),
              "Delete source disk before workflow");
      bytes.fill(std::byte{0});
      const std::array targets{repair::DuplicateTarget{0, 1}};
      const auto destination = fixture.Root() / "published";
      const repair::GltfDuplicateFilePublication publication{
          destination, kReportPath, {}};
      const repair::GltfDuplicateFileWorkflowRequest request{
          source,
          kInputId,
          0,
          {ic::GltfTriangleRequest::kRequireTriangles,
           ic::GltfValueRequest::kRaw},
          targets,
          {},
          &publication};
      auto result = repair::RunGltfDuplicateFileWorkflow(request, Limits(),
                                                         Work(), context);
      Require(result.record && result.record->Memory(),
              "Retain real memory suboperation");
      if (unsupported) {
        Require(result.diagnostic.memory &&
                    result.diagnostic.memory->stage ==
                        repair::GltfWorkflowStage::kMaterialize &&
                    result.outcomes.execution ==
                        repair::GltfWorkflowExecutionOutcome::kFailed &&
                    result.outcomes.exit ==
                        repair::GltfWorkflowExitMeaning::kExecutionFailure &&
                    !result.record->Published() && !result.publication &&
                    !fs::exists(destination),
                "Actual unsupported materialization precedes file publication");
      } else {
        Require(
            result.status == repair::GltfWorkflowStatus::kSuccess &&
                result.outcomes.candidate ==
                    repair::CandidateOutcome::accepted &&
                result.outcomes.publication ==
                    repair::GltfFileWorkflowPublicationOutcome::kPublished &&
                result.outcomes.profile ==
                    repair::GltfWorkflowProfileOutcome::kPassed &&
                result.outcomes.exit ==
                    repair::GltfWorkflowExitMeaning::kSuccess &&
                result.publication && result.publication->bundle &&
                result.record->Published() &&
                result.record->PublishedReportPath() == kReportPath,
            "Verified changed file workflow publication");
        const auto receipt = Read(destination / kReportPath);
        Require(std::ranges::equal(receipt, result.record->ReportJson()),
                "Returned receipt equals actual committed physical bytes");
        constexpr std::array opaque{kReportPath};
        auto reopened = ic::ReadGltfFileAsset(
            destination / kEntryPath,
            {destination, ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
             opaque},
            FileReadLimits(), FileReadWork(), context);
        Require(reopened.asset &&
                    reopened.asset->Files().size() == kExpectedPhysicalFiles,
                "Reopen complete actual output inventory");
        auto extracted = ic::ExtractGltfPrimitiveCapped(
            reopened.asset->Asset(), *reopened.asset->Asset().Primitive(0),
            {ic::GltfTriangleRequest::kRequireTriangles,
             ic::GltfValueRequest::kRaw},
            MemoryLimits().reload_extraction);
        Require(extracted.extraction &&
                    std::ranges::equal(extracted.extraction->TriangleIndices(),
                                       kExpectedTriangle),
                "Actual output retains only representative triangle");
      }
      const auto attribution = result.record->NewRequestedBytes();
      Require(attribution.memory.read == 0 &&
                  attribution.retained_source.files > 0 &&
                  attribution.retained_source.capped_reader &&
                  attribution.wrapper <= kWorkflowBytes &&
                  context.ActiveWorkers() == 0,
              "Fresh retained-source attribution and joined workers");
      retained = *result.record;
    }
    Require(
        retained->Source().Asset().InstanceCount() == 2 &&
            std::ranges::equal(retained->Source().Asset().Resources()[0].bytes,
                               SourceBuffer()),
        "Copied record owns immutable source after caller context dies");
    if (!retained->ReportJson().empty())
      Require(Text(retained->ReportJson()).find(kReportSchema) !=
                  std::string_view::npos,
              "Owned report envelope");
    if (!unsupported) {
      const auto& edit = retained->Memory()->Repair();
      Require(edit && edit->FaceMap().size() == 2 && edit->FaceMap()[0] == 0 &&
                  edit->FaceMap()[1] == 0 && retained->Published(),
              "Copied published record retains exact correspondence");
    }
    std::cout << "Installed retained file workflow, actual receipt, reopen and "
                 "lifetime passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
