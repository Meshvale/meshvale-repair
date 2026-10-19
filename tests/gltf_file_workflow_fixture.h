// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_GLTF_FILE_WORKFLOW_FIXTURE_H_
#define MESHVALE_REPAIR_TESTS_GLTF_FILE_WORKFLOW_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_file_workflow.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace meshvale::repair::test_file_workflow {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace fs = std::filesystem;
inline constexpr std::uint64_t kPhysicalBytes = 1024 * 1024;
inline constexpr std::uint64_t kStageBytes = 4 * kPhysicalBytes;
inline constexpr std::uint64_t kMemoryBytes = 64 * kPhysicalBytes;
inline constexpr std::uint64_t kWrapperBytes = 64 * kPhysicalBytes;
inline constexpr std::uint64_t kLedgerBytes = 256 * kPhysicalBytes;
inline constexpr std::uint64_t kCount = 4096;
inline constexpr std::uint64_t kDepth = 64;
inline constexpr std::uint64_t kIdentityBytes = 256;
inline constexpr std::uint64_t kPartitionBytes = 4096;
inline constexpr std::uint64_t kCheckpointBytes = 32;
inline constexpr std::uint64_t kJobs = 64;
inline constexpr std::size_t kWorkers = 4;
inline constexpr std::size_t kOpaqueBytes = 65536;
inline constexpr std::size_t kTextureBytes = 4096;
inline constexpr std::array<std::byte, 8> kPngSignature{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
    std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
inline constexpr std::size_t kIndexStartBytes = 36;
inline constexpr std::string_view kEntry = "scene.gltf";
inline constexpr std::string_view kBufferPath = "mesh.bin";
inline constexpr std::string_view kTexturePath = "texture.bin";
inline constexpr std::string_view kOpaquePath = "notes/original.bin";
inline constexpr std::string_view kReportPath = "receipts/final.json";
inline constexpr std::string_view kStaticPath = "receipts/static.bin";
inline constexpr std::string_view kStaticBytes =
    "literal static receipt input\n";
inline constexpr std::string_view kInputId = "literal-file-workflow";
inline constexpr std::string_view kJson =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":48}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],"images":[{"uri":"texture.bin","mimeType":"image/png"}],"materials":[{"name":"authored-material"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"nodes":[{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1]}],"extras":{"number":1.2300e+04,"string":"keep\u0020lexeme"}})";
// Independent literal float32 position bits, duplicate UInt16 indices, and
// eight unused physical buffer-tail bytes. No writer generates this oracle.
inline constexpr std::array<std::byte, 56> kBinary{
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0x80},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0x80}, std::byte{0x3f},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{0x80}, std::byte{0x3f},
    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{0},    std::byte{0},    std::byte{1},    std::byte{0},
    std::byte{2},    std::byte{0},    std::byte{0},    std::byte{0},
    std::byte{1},    std::byte{0},    std::byte{2},    std::byte{0},
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef},
    std::byte{0xca}, std::byte{0xfe}, std::byte{0xba}, std::byte{0xbe}};
inline constexpr std::size_t kEmbeddedLogicalBytes = 49;
inline constexpr std::size_t kEmbeddedPhysicalBytes = 52;
inline constexpr std::size_t kEmbeddedChunkCount = 5;
inline constexpr std::array<std::byte, kEmbeddedPhysicalBytes> kEmbeddedBin =
    [] {
      std::array<std::byte, kEmbeddedPhysicalBytes> bytes{};
      std::copy_n(kBinary.begin(), kEmbeddedLogicalBytes - 1, bytes.begin());
      bytes[kEmbeddedLogicalBytes - 1] = std::byte{0xd3};
      return bytes;
    }();
inline constexpr std::array<std::uint32_t, 6> kOriginalTriangles{0, 1, 2,
                                                                 0, 1, 2};
inline constexpr std::array<std::uint32_t, 3> kChangedTriangles{0, 1, 2};
inline constexpr std::array<geo::index_t, 2> kChangedFaceMap{0, 0};
inline constexpr std::array<geo::index_t, 6> kChangedCornerMap{0, 1, 2,
                                                               0, 1, 2};
inline constexpr std::array<DuplicateTarget, 1> kTargets{{{0, 1}}};
inline constexpr std::array<std::string_view, 1> kOpaquePaths{kOpaquePath};
inline std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
inline std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
inline void Require(
    bool value, std::string_view message,
    std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
inline void Write(const fs::path& path, std::span<const std::byte> bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  Require(stream.is_open(), "literal fixture file opened");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(!stream.fail(), "literal fixture full bytes written");
}
inline std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  Require(stream.is_open(), "actual committed file reopened");
  const std::string text{std::istreambuf_iterator<char>(stream), {}};
  return {Bytes(text).begin(), Bytes(text).end()};
}
inline geo::MeshMaterializationLimits CaptureLimits() {
  return {kCount, kCount, kCount, kCount,         kCount,     kCount,
          kCount, kCount, kCount, kPhysicalBytes, kStageBytes};
}
inline ic::GltfReadOperationLimits ReadLimits() {
  return {{kPhysicalBytes, kPhysicalBytes, kPhysicalBytes, kCount, kDepth,
           kPhysicalBytes, kIdentityBytes, kIdentityBytes, kIdentityBytes,
           kCount, kCount, kCount, kCount, kStageBytes},
          kStageBytes};
}
inline ic::GltfFileLimits FileLimits() {
  return {kCount,         kCount,         kPhysicalBytes, kPhysicalBytes,
          kPhysicalBytes, kPhysicalBytes, kPhysicalBytes, kWrapperBytes};
}
inline std::uint64_t Threshold(bool serial) {
  return serial ? static_cast<std::uint64_t>(
                      std::numeric_limits<std::ptrdiff_t>::max())
                : 1;
}
inline GltfDuplicateFileWorkflowLimits Limits() {
  GltfDuplicateFileWorkflowLimits limits;
  auto& memory = limits.memory;
  memory.max_new_requested_bytes = kMemoryBytes;
  memory.max_input_id_bytes = kIdentityBytes;
  memory.max_source_protection_bytes = kPhysicalBytes;
  memory.max_source_binding_records = kCount;
  memory.max_reload_bindings = kCount;
  memory.max_report_bytes = kPhysicalBytes;
  memory.extraction = memory.reload_extraction = {kCount, kCount, kStageBytes};
  memory.materialization = {
      kCount, kStageBytes, CaptureLimits(), {kCount, kStageBytes}};
  memory.repair = {kCount,         kCount,      kIdentityBytes,
                   kCount,         kStageBytes, {kCount, kStageBytes},
                   CaptureLimits()};
  memory.writing = {
      kPhysicalBytes,
      kCount,
      kDepth,
      kCount,
      kCount,
      kCount,
      kPhysicalBytes,
      kCount,
      kCount,
      kPhysicalBytes,
      {kPhysicalBytes, kCount, kPhysicalBytes, kPhysicalBytes, kStageBytes}};
  memory.reload = ReadLimits();
  memory.verification = {kPhysicalBytes, kCount, kDepth, kCount,
                         kPhysicalBytes, kCount, kCount, kCount,
                         kPhysicalBytes, kCount, kCount, kPhysicalBytes,
                         kStageBytes};
  limits.max_wrapper_new_requested_bytes = kWrapperBytes;
  limits.max_source_files = kCount;
  limits.max_supplemental_files = kCount;
  limits.max_report_path_bytes = kIdentityBytes;
  limits.max_report_bytes = kPhysicalBytes;
  limits.max_hash_input_bytes = 4 * kPhysicalBytes;
  limits.publication = {FileLimits(),
                        ReadLimits(),
                        {kCount, kCount, kStageBytes},
                        memory.verification,
                        {kPhysicalBytes, kCount, kStageBytes}};
  return limits;
}
inline GltfDuplicateFileWorkflowWorkPolicy Work(bool serial = false) {
  const auto threshold = Threshold(serial);
  const GltfWorkflowByteWorkPolicy bytes{threshold, kPartitionBytes,
                                         kCheckpointBytes, kJobs};
  return {
      {{threshold, kPartitionBytes, threshold, kPartitionBytes},
       {threshold, kPartitionBytes, kCheckpointBytes, kJobs},
       {threshold, kPartitionBytes, kCheckpointBytes, kJobs},
       {threshold, kPartitionBytes, kCheckpointBytes, kJobs},
       bytes},
      bytes,
      bytes,
      {{kPartitionBytes, threshold, kPartitionBytes, kCheckpointBytes, kJobs},
       {threshold, kPartitionBytes, threshold, kPartitionBytes},
       {threshold, kPartitionBytes, kCheckpointBytes, kJobs},
       {threshold, kPartitionBytes}}};
}
inline std::vector<std::byte> LiteralGlb(std::string_view json) {
  constexpr std::uint32_t kMagic = 0x46546c67;
  constexpr std::uint32_t kVersion = 2;
  constexpr std::uint32_t kJsonType = 0x4e4f534a;
  constexpr std::uint32_t kOpaqueType = 0x12345678;
  constexpr std::size_t kHeaderBytes = 12;
  constexpr std::size_t kChunkHeaderBytes = 8;
  constexpr std::size_t kWordBytes = 4;
  constexpr std::size_t kBitsPerByte = 8;
  constexpr std::uint32_t kByteMask = 0xff;
  constexpr std::array<std::byte, 4> kOpaqueChunk{
      std::byte{0x7f}, std::byte{0}, std::byte{0xde}, std::byte{0xad}};
  auto padded = std::string(json);
  while (padded.size() % kWordBytes) padded.push_back(' ');
  std::vector<std::byte> wire;
  const auto word = [&](std::uint32_t value) {
    for (std::size_t byte = 0; byte < kWordBytes; ++byte)
      wire.push_back(
          static_cast<std::byte>((value >> (byte * kBitsPerByte)) & kByteMask));
  };
  word(kMagic);
  word(kVersion);
  word(static_cast<std::uint32_t>(kHeaderBytes + 2 * kChunkHeaderBytes +
                                  padded.size() + kOpaqueChunk.size()));
  word(static_cast<std::uint32_t>(padded.size()));
  word(kJsonType);
  wire.insert(wire.end(), Bytes(padded).begin(), Bytes(padded).end());
  word(static_cast<std::uint32_t>(kOpaqueChunk.size()));
  word(kOpaqueType);
  wire.insert(wire.end(), kOpaqueChunk.begin(), kOpaqueChunk.end());
  return wire;
}
// Independent GLB2 encoder for an embedded source. Full payload literals remain
// separate from production assembly and the selected writer's JSON prefix.
inline std::vector<std::byte> LiteralEmbeddedGlb(
    std::span<const std::byte> json) {
  constexpr std::uint32_t kMagic = 0x46546c67;
  constexpr std::uint32_t kVersion = 2;
  constexpr std::uint32_t kJsonType = 0x4e4f534a;
  constexpr std::uint32_t kBinType = 0x004e4942;
  constexpr std::uint32_t kOpaqueType = 0x12345678;
  constexpr std::uint32_t kOtherOpaqueType = 0xdecafbad;
  constexpr std::size_t kHeaderBytes = 12;
  constexpr std::size_t kChunkHeaderBytes = 8;
  constexpr std::size_t kWordBytes = 4;
  constexpr std::size_t kBitsPerByte = 8;
  constexpr std::uint32_t kByteMask = 0xff;
  constexpr std::array<std::byte, 4> kFirstOpaque{
      std::byte{0x7f}, std::byte{0}, std::byte{0xde}, std::byte{0xad}};
  constexpr std::array<std::byte, 8> kSecondOpaque{
      std::byte{1},    std::byte{2},    std::byte{3}, std::byte{4},
      std::byte{0x80}, std::byte{0xff}, std::byte{0}, std::byte{0xaa}};
  constexpr std::array<std::byte, 4> kThirdOpaque{
      std::byte{0}, std::byte{1}, std::byte{2}, std::byte{0xff}};
  const auto padding = (kWordBytes - json.size() % kWordBytes) % kWordBytes;
  std::vector<std::byte> wire;
  const auto word = [&](std::uint32_t value) {
    for (std::size_t byte = 0; byte < kWordBytes; ++byte)
      wire.push_back(
          static_cast<std::byte>((value >> (byte * kBitsPerByte)) & kByteMask));
  };
  word(kMagic);
  word(kVersion);
  word(static_cast<std::uint32_t>(
      kHeaderBytes + kEmbeddedChunkCount * kChunkHeaderBytes + json.size() +
      padding + kEmbeddedBin.size() + kFirstOpaque.size() +
      kSecondOpaque.size() + kThirdOpaque.size()));
  word(static_cast<std::uint32_t>(json.size() + padding));
  word(kJsonType);
  wire.insert(wire.end(), json.begin(), json.end());
  wire.insert(wire.end(), padding, std::byte{' '});
  const auto chunk = [&](std::uint32_t type, std::span<const std::byte> bytes) {
    word(static_cast<std::uint32_t>(bytes.size()));
    word(type);
    wire.insert(wire.end(), bytes.begin(), bytes.end());
  };
  chunk(kBinType, kEmbeddedBin);
  chunk(kOpaqueType, kFirstOpaque);
  chunk(kOtherOpaqueType, kSecondOpaque);
  chunk(kOpaqueType, kThirdOpaque);
  return wire;
}
struct RootRemoval {
  const fs::path& path;
  ~RootRemoval() noexcept {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};
struct Fixture {
  fs::path root;
  RootRemoval cleanup{root};
  geo::ExecutionContext context;
  std::optional<ic::GltfFileAsset> source;
  std::vector<std::byte> entry;
  std::array<std::byte, kBinary.size()> binary = kBinary;
  std::vector<std::byte> texture{kTextureBytes, std::byte{0x5a}};
  std::vector<std::byte> opaque{kOpaqueBytes, std::byte{0xa7}};
  bool embedded_glb = false;
  explicit Fixture(ic::GltfFileFormat format = ic::GltfFileFormat::kGltfJson,
                   bool malformed_indices = false,
                   std::string_view json = kJson,
                   std::uint64_t budget = kLedgerBytes, bool embedded = false)
      : context({kWorkers, 1, budget}), embedded_glb(embedded) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-repair-file-fixture-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated owning fixture root");
    std::ranges::copy(kPngSignature, texture.begin());
    if (malformed_indices) binary[kIndexStartBytes] = std::byte{9};
    entry =
        format == ic::GltfFileFormat::kGlb
            ? LiteralGlb(json)
            : std::vector<std::byte>(Bytes(json).begin(), Bytes(json).end());
    if (embedded_glb) {
      Require(format == ic::GltfFileFormat::kGlb && !malformed_indices,
              "embedded fixture uses literal valid GLB indices");
      std::string embedded_json(json);
      constexpr std::string_view kExternalBuffer =
          R"("buffers":[{"uri":"mesh.bin","byteLength":48}])";
      constexpr std::string_view kEmbeddedBuffers =
          R"("buffers":[{"byteLength":49},{"uri":"mesh.bin","byteLength":48}])";
      const auto buffer = embedded_json.find(kExternalBuffer);
      Require(buffer != std::string::npos, "literal external buffer replaced");
      embedded_json.replace(buffer, kExternalBuffer.size(), kEmbeddedBuffers);
      embedded_json += " \t\n";
      entry = LiteralEmbeddedGlb(Bytes(embedded_json));
    }
    Write(SourceRoot() / kEntry, entry);
    Write(SourceRoot() / kBufferPath, binary);
    Write(SourceRoot() / kTexturePath, texture);
    Write(SourceRoot() / kOpaquePath, opaque);
    const auto work = Work(true).publication;
    auto captured = ic::ReadGltfFileAsset(
        SourceRoot() / kEntry,
        {{},
         ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
         kOpaquePaths},
        {FileLimits(), ReadLimits(), {kPhysicalBytes, kCount, kStageBytes}},
        {work.files, work.reload, work.container}, context);
    Require(captured.asset.has_value(), "actual public retained file capture");
    source = *captured.asset;
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  fs::path SourceRoot() const { return root / "input"; }
  fs::path Destination() const { return root / "published"; }
  GltfDuplicateFileWorkflowRequest Request(
      std::span<const DuplicateTarget> targets = kTargets,
      const GltfDuplicateFilePublication* publication = nullptr) const {
    return {*source,
            kInputId,
            0,
            {ic::GltfTriangleRequest::kRequireTriangles,
             ic::GltfValueRequest::kRaw},
            targets,
            {},
            publication};
  }
  void CheckSource() const {
    Require(std::ranges::equal(source->EntryBytes(), entry),
            "retained exact source entry");
    for (const auto& file : source->Files()) {
      if (file.path == kBufferPath)
        Require(std::ranges::equal(file.bytes, binary),
                "retained full buffer tail");
      else if (file.path == kTexturePath)
        Require(std::ranges::equal(file.bytes, texture),
                "retained opaque image bytes");
      else if (file.path == kOpaquePath)
        Require(std::ranges::equal(file.bytes, opaque),
                "retained explicit opaque member");
    }
  }
};
}  // namespace meshvale::repair::test_file_workflow
#endif  // MESHVALE_REPAIR_TESTS_GLTF_FILE_WORKFLOW_FIXTURE_H_
