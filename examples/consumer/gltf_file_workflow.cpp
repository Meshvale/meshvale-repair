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
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

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
constexpr std::string_view kGlbJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":49}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],"extras":{"opaque":1.2300e+04}})";
constexpr std::size_t kGlbLogicalBufferBytes = kSourceBytes + 1;
constexpr std::size_t kGlbPhysicalBufferBytes = 52;
constexpr std::size_t kGlbChunkCount = 3;
constexpr std::size_t kGlbBinChunkIndex = 1;
constexpr std::uint32_t kOpaqueChunkType = 0x12345678;
constexpr std::byte kUnusedLogicalByte{0xde};
constexpr std::array<std::byte, 4> kOpaqueChunk{
    std::byte{0x7f}, std::byte{0}, std::byte{0xde}, std::byte{0xad}};

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
std::vector<std::byte> SourceGlb(const geo::ExecutionContext& context) {
  std::array<std::byte, kGlbLogicalBufferBytes> bin{};
  std::ranges::copy(SourceBuffer(), bin.begin());
  bin.back() = kUnusedLogicalByte;
  const std::array chunks{
      ic::GlbChunk{ic::kGlbJsonChunkType, std::as_bytes(std::span(kGlbJson))},
      ic::GlbChunk{ic::kGlbBinChunkType, bin},
      ic::GlbChunk{kOpaqueChunkType, kOpaqueChunk}};
  auto result = ic::WriteGlbContainer(
      chunks, {kPhysicalLimit, kGlbChunkCount, kStageBytes},
      {kParallelMinimum, kPartitionBytes}, context);
  Require(result.status == ic::GlbStatus::kSuccess,
          "Assemble admitted source GLB with embedded BIN");
  const auto bytes = result.container.Bytes();
  return {bytes.begin(), bytes.end()};
}
void CheckSource(const ic::GltfFileAsset& source, bool glb) {
  const auto resources = source.Asset().Resources();
  Require(!resources.empty(), "Retained source buffer resource");
  if (!glb) {
    Require(std::ranges::equal(resources[0].bytes, SourceBuffer()),
            "Retained complete external buffer");
    return;
  }
  Require(
      source.Container() && source.Container()->ChunkCount() == kGlbChunkCount,
      "Retained source GLB chunks");
  const auto bin = source.Container()->Chunk(kGlbBinChunkIndex);
  Require(
      bin.type == ic::kGlbBinChunkType &&
          bin.bytes.size() == kGlbPhysicalBufferBytes &&
          std::ranges::equal(bin.bytes.first(kSourceBytes), SourceBuffer()) &&
          bin.bytes[kSourceBytes] == kUnusedLogicalByte &&
          std::ranges::all_of(
              bin.bytes.subspan(kGlbLogicalBufferBytes),
              [](std::byte value) { return value == std::byte{0}; }) &&
          std::ranges::equal(resources[0].bytes, bin.bytes),
      "Retained logical unused byte and complete BIN padding");
  const auto opaque = source.Container()->Chunk(kGlbChunkCount - 1);
  Require(opaque.type == kOpaqueChunkType &&
              std::ranges::equal(opaque.bytes, kOpaqueChunk),
          "Retained opaque chunk");
}

namespace fs = std::filesystem;
constexpr std::string_view kJsonEntryPath = "source.gltf";
constexpr std::string_view kGlbEntryPath = "source.glb";
constexpr std::string_view kReportPath = "receipts/report.json";
constexpr std::string_view kReportSchema = "\"schema\":\"meshvale.report/1\"";
constexpr std::size_t kJsonPhysicalFiles = 3;
constexpr std::size_t kGlbPhysicalFiles = 2;
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
  for (bool glb : {false, true})
    for (bool changed : {false, true}) {
      const auto case_name = std::string(glb ? "glb-" : "json-") +
                             (changed ? "changed" : "unchanged");
      const auto case_root = root / case_name;
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
          constexpr std::uint32_t kJsonType = 0x4e4f534a, kBinType = 0x004e4942;
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
            {moving::kCount, moving::kCount, moving::kMiB, moving::kStageBytes,
             moving::kMiB, moving::kMiB, moving::kMiB, moving::kWorkflowBytes},
            moving::ReadLimits(),
            memory.extraction,
            memory.verification,
            {moving::kMiB, moving::kCount, moving::kStageBytes}};
        const auto work_memory = moving::Work();
        constexpr std::uint64_t kFilePartitionBytes = 256, kFileStopBytes = 16;
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
        auto captured = interchange::ReadGltfFileAsset(
            input / moving::kEntry, {}, capture_limits, capture_work, context);
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
             interchange::GltfProcessingProfile::kDirectTranslationTrianglesV1},
            changed ? std::span<const mv::DuplicateTarget>(moving::kTargets)
                    : std::span<const mv::DuplicateTarget>{},
            {moving::kRowLocalAttributes},
            &publication};
        auto result =
            mv::RunGltfDuplicateFileWorkflow(request, limits, work, context);
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
            std::ranges::equal(read(publication.destination / moving::kReport),
                               result.record->ReportJson()),
            "committed immutable report matches returned exact receipt");
        moving::Require(
            !result.record->ReportJson().empty() &&
                moving::Text(result.record->ReportJson())
                        .find("\"publication_reload\":{\"status\":0") !=
                    std::string_view::npos,
            "conditional receipt records actual staged facts before commit");
        if (!changed)
          moving::Require(std::ranges::equal(
                              result.record->Published()->EntryBytes(), entry),
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
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == kUnsupportedProfileOption;
    Require(argc == 1 || unsupported,
            "Expected no option or --unsupported-profile");
    for (const auto format :
         {ic::GltfFileFormat::kGltfJson, ic::GltfFileFormat::kGlb}) {
      const bool glb = format == ic::GltfFileFormat::kGlb;
      const auto entry_path = glb ? kGlbEntryPath : kJsonEntryPath;
      for (const bool changed : {true, false}) {
        Fixture fixture;
        std::optional<repair::GltfDuplicateFileWorkflowRecord> retained;
        {
          const auto input = fixture.Root() / "input";
          Require(fs::create_directory(input), "Create input directory");
          auto bytes = SourceBuffer();
          const geo::ExecutionContext context({kWorkers, 1, kLedgerBytes});
          auto entry = glb ? SourceGlb(context)
                           : std::vector<std::byte>(
                                 std::as_bytes(std::span(kJson)).begin(),
                                 std::as_bytes(std::span(kJson)).end());
          Write(input / entry_path, entry);
          if (!glb) Write(input / "mesh.bin", bytes);
          ic::GltfFileReadOptions source_options;
          source_options.resource_root = input;
          auto captured =
              ic::ReadGltfFileAsset(input / entry_path, source_options,
                                    FileReadLimits(), FileReadWork(), context);
          Require(captured.asset.has_value(), "Capture owned source files");
          const auto source = *captured.asset;
          captured.asset.reset();
          Require(fs::remove(input / entry_path) &&
                      (glb || fs::remove(input / "mesh.bin")) &&
                      fs::remove(input),
                  "Delete source disk before workflow");
          bytes.fill(std::byte{0});
          std::ranges::fill(entry, std::byte{0});
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
              changed ? std::span<const repair::DuplicateTarget>(targets)
                      : std::span<const repair::DuplicateTarget>{},
              {},
              &publication};
          auto result = repair::RunGltfDuplicateFileWorkflow(request, Limits(),
                                                             Work(), context);
          Require(result.record && result.record->Memory(),
                  "Retain real memory suboperation");
          if (unsupported) {
            Require(
                result.diagnostic.memory &&
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
            Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                        result.outcomes.candidate ==
                            (changed ? repair::CandidateOutcome::accepted
                                     : repair::CandidateOutcome::unchanged) &&
                        result.outcomes.publication ==
                            repair::GltfFileWorkflowPublicationOutcome::
                                kPublished &&
                        result.outcomes.profile ==
                            repair::GltfWorkflowProfileOutcome::kPassed &&
                        result.outcomes.exit ==
                            repair::GltfWorkflowExitMeaning::kSuccess &&
                        result.publication && result.publication->bundle &&
                        result.record->Published() &&
                        result.record->PublishedReportPath() == kReportPath,
                    "Verified changed/no-op file workflow publication");
            if (!changed)
              Require(
                  std::ranges::equal(result.record->Published()->EntryBytes(),
                                     source.EntryBytes()),
                  "No-op publication preserves complete source entry bytes");
            if (glb) {
              const auto* original = source.Container();
              const auto* published = result.record->Published()->Container();
              Require(original && published &&
                          original->ChunkCount() == kGlbChunkCount &&
                          published->ChunkCount() == kGlbChunkCount,
                      "Actual source and published GLB containers");
              for (std::size_t chunk = kGlbBinChunkIndex;
                   chunk < kGlbChunkCount; ++chunk)
                Require(original->Chunk(chunk).type ==
                                published->Chunk(chunk).type &&
                            std::ranges::equal(original->Chunk(chunk).bytes,
                                               published->Chunk(chunk).bytes),
                        "Published ordered BIN/padding/opaque bytes unchanged");
            }
            const auto receipt = Read(destination / kReportPath);
            Require(std::ranges::equal(receipt, result.record->ReportJson()),
                    "Returned receipt equals actual committed physical bytes");
            constexpr std::array opaque{kReportPath};
            auto reopened = ic::ReadGltfFileAsset(
                destination / entry_path,
                {destination,
                 ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory, opaque},
                FileReadLimits(), FileReadWork(), context);
            Require(reopened.asset &&
                        reopened.asset->Files().size() ==
                            (glb ? kGlbPhysicalFiles : kJsonPhysicalFiles) &&
                        reopened.asset->Format() == format,
                    "Reopen complete actual output inventory");
            auto extracted = ic::ExtractGltfPrimitiveCapped(
                reopened.asset->Asset(), *reopened.asset->Asset().Primitive(0),
                {ic::GltfTriangleRequest::kRequireTriangles,
                 ic::GltfValueRequest::kRaw},
                MemoryLimits().reload_extraction);
            constexpr std::array<std::uint32_t, 6> kOriginalTriangles{0, 1, 2,
                                                                      0, 1, 2};
            Require(
                extracted.extraction &&
                    std::ranges::equal(extracted.extraction->TriangleIndices(),
                                       changed ? std::span<const std::uint32_t>(
                                                     kExpectedTriangle)
                                               : std::span<const std::uint32_t>(
                                                     kOriginalTriangles)),
                "Actual reopened output has exact changed/no-op triangles");
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
            retained->Source().Asset().InstanceCount() == 2,
            "Copied record owns immutable source after caller context dies");
        CheckSource(retained->Source(), glb);
        if (!retained->ReportJson().empty())
          Require(Text(retained->ReportJson()).find(kReportSchema) !=
                      std::string_view::npos,
                  "Owned report envelope");
        if (!unsupported) {
          const auto& edit = retained->Memory()->Repair();
          Require(edit && edit->FaceMap().size() == 2 &&
                      edit->FaceMap()[0] == 0 &&
                      edit->FaceMap()[1] == (changed ? 0 : 1) &&
                      retained->Published(),
                  "Copied published record retains exact correspondence");
        }
      }
    }
    if (!unsupported) TranslationPublication();
    std::cout << "Installed JSON/GLB changed/no-op workflows, actual receipts, "
                 "reopen and "
                 "lifetime passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
