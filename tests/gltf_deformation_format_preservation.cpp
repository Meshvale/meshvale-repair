// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_deformation.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_workflow_support.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <algorithm>
#include <array>
#include <atomic>
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
#include <type_traits>
#include <variant>
#include <vector>

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace repair = meshvale::repair;
namespace fs = std::filesystem;
constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
constexpr std::uint64_t kStageBytes = 4 * kPhysicalLimit;
constexpr std::uint64_t kWorkflowBytes = 64 * kPhysicalLimit;
constexpr std::uint64_t kLedgerBytes = 512 * kPhysicalLimit;
constexpr std::uint64_t kIdentityBytes = 256;
constexpr std::uint64_t kDocumentDepth = 64;
constexpr std::uint64_t kParallelMinimum = 128;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCancellationBlock = 32;
constexpr std::uint64_t kMaximumJobs = 64;
constexpr std::size_t kWorkers = 4;
constexpr std::uint64_t kMaximumPhysicalFiles = 1024;
constexpr std::uint64_t kMaximumResourceBindings = 1024;
constexpr std::uint64_t kTotalRelativePathBytes = 65536;
constexpr std::uint64_t kNativePathCodeUnits = 65536;
constexpr std::uint64_t kNativeNameBound = 4096;
constexpr std::uint64_t kCardinalityLimit = 65536;
constexpr std::uint64_t kInfluenceSetLimit = 16;
constexpr std::uint64_t kLaneLimit = 64;
constexpr std::uint64_t kDeformationPartition = 64;
constexpr std::uint64_t kRelationshipBlock = 16;
constexpr std::uint64_t kTriangleMode = 4;
constexpr std::size_t kRetainedChunkCount = 4;
constexpr std::size_t kContainerChunkCount = kRetainedChunkCount + 1;
constexpr std::size_t kResourceCount = 3;
constexpr std::uint64_t kExternalLogicalBytes = 4;
constexpr std::size_t kScenarioCount = 9;
constexpr std::size_t kAccessorOracleCount = 66;
constexpr std::size_t kMemoryCaseCount = 8;
constexpr unsigned kHexShift = 4;
constexpr unsigned kHexMask = 15;
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr std::string_view kOpaqueScenario = "opaque-tail-alias";
constexpr std::string_view kSharedFile = "shared.bin";
constexpr std::string_view kEscapedSharedFile = "%73hared.bin";
constexpr std::string_view kSharedHex = "317200ffa55a";
// Finite native test policies; these are not application presets.
constexpr ic::GltfDeformationPolicy kDeformation{
    {kCardinalityLimit, kCardinalityLimit, kCardinalityLimit, kCardinalityLimit,
     kInfluenceSetLimit, kLaneLimit, kCardinalityLimit, kCardinalityLimit,
     kCardinalityLimit, kCardinalityLimit, kCardinalityLimit, kPhysicalLimit,
     kCardinalityLimit, kCardinalityLimit, kStageBytes, kStageBytes,
     kStageBytes, kStageBytes},
    {kDeformationPartition, kDeformationPartition, kDeformationPartition,
     kDeformationPartition, kDeformationPartition, kDeformationPartition,
     kDeformationPartition, kRelationshipBlock}};

void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
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

class TemporaryRoot {
 public:
  TemporaryRoot() {
    const auto parent = fs::canonical(fs::temp_directory_path());
    static std::atomic<std::uint64_t> sequence{0};
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    constexpr std::size_t kCreationAttempts = 16;
    for (std::size_t attempt = 0; attempt < kCreationAttempts; ++attempt) {
      const auto candidate =
          parent / ("meshvale-deformation-format-" + std::to_string(tick) +
                    "-" + std::to_string(sequence.fetch_add(1)));
      Require(candidate.parent_path() == parent,
              "temporary root stays within resolved parent");
      std::error_code error;
      if (fs::create_directory(candidate, error)) {
        try {
          Require(fs::canonical(candidate) == candidate,
                  "exclusive root has no redirected components");
          path_ = candidate;
          return;
        } catch (...) {
          try {
            fs::remove_all(candidate, error);
          } catch (...) {
          }
          throw;
        }
      }
      Require(!error, "exclusive temporary root creation");
    }
    throw std::runtime_error("exclusive temporary root attempts exhausted");
  }
  ~TemporaryRoot() noexcept {
    try {
      if (!path_.empty()) {
        std::error_code error;
        fs::remove_all(path_, error);
      }
    } catch (...) {
      // Failure cleanup must not throw during another exception.
    }
  }
  TemporaryRoot(const TemporaryRoot&) = delete;
  TemporaryRoot& operator=(const TemporaryRoot&) = delete;
  const fs::path& Path() const { return path_; }
  void CheckCleanup() {
    std::error_code error;
    const auto removed = fs::remove_all(path_, error);
    Require(!error && removed > 0 && !fs::exists(path_),
            "checked positive cleanup of owned test tree");
    path_.clear();
  }

 private:
  fs::path path_;
};

std::vector<std::byte> ReadBytes(const fs::path& path,
                                 bool hexadecimal = false) {
  const auto size = fs::file_size(path);
  Require(size > 0 && size <= kPhysicalLimit, "bounded nonempty fixture/file");
  std::vector<char> text(static_cast<std::size_t>(size));
  std::ifstream stream(path, std::ios::binary);
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  Require(stream.gcount() == static_cast<std::streamsize>(size),
          "complete file read");
  std::vector<std::byte> bytes;
  if (!hexadecimal) {
    const auto view = std::as_bytes(std::span(text));
    bytes.assign(view.begin(), view.end());
    return bytes;
  }
  std::optional<unsigned> high;
  for (const char digit : text) {
    if (digit == '\n' || digit == '\r') continue;
    const auto value = kHexDigits.find(digit);
    Require(value != std::string_view::npos,
            "lowercase hexadecimal fixture digit");
    if (high) {
      bytes.push_back(static_cast<std::byte>((*high << kHexShift) | value));
      high.reset();
    } else {
      high = static_cast<unsigned>(value);
    }
  }
  Require(!high && !bytes.empty(), "complete nonempty hexadecimal wire bytes");
  return bytes;
}

void WriteBytes(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(bool(stream), "complete physical file write");
}

struct AccessorExpected {
  std::uint64_t accessor;
  ic::GltfComponentType component;
  ic::GltfAccessorShape shape;
  bool normalized;
  std::uint64_t count;
  std::uint64_t stride;
  std::string_view hex;
  bool source_indices;
};
struct Scenario {
  std::string_view id;
  std::uint64_t mode;
  bool glb;
  std::optional<geo::index_t> removed;
  std::vector<std::uint32_t> triangles;
  std::vector<geo::index_t> source_faces;
  std::vector<geo::index_t> source_corners;
  std::vector<geo::index_t> face_map;
  std::vector<geo::index_t> corner_map;
  std::vector<std::uint32_t> candidate_triangles;
  std::vector<ic::GltfSourceCorner> original_corners;
  std::vector<AccessorExpected> accessors;
};
struct RoleExpected {
  std::uint64_t accessor;
  std::string_view semantic;
  std::optional<std::uint64_t> target, set;
  ic::GltfDeformationAccessorRole role;
};
struct RoleInventory {
  std::string_view scenario;
  std::vector<RoleExpected> roles;
};
// Literal source primitive0 attributes and morph targets, independently
// indexed.
const std::array<RoleInventory, kScenarioCount> kRoleInventories{{
    {"indexed-strip-edit",
     {
         {0, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {1, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {2, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
    {"nonindexed-strip-identity",
     {
         {0, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {1, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {2, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
    {"indexed-fan-edit",
     {
         {0, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {1, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {2, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
    {"nonindexed-fan-identity",
     {
         {0, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {1, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {2, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
    {"influences-unorm16",
     {
         {2, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {3, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {4, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
         {5, "JOINTS_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kJoints},
         {6, "WEIGHTS_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kWeights},
         {7, "JOINTS_2", std::nullopt, 2,
          ic::GltfDeformationAccessorRole::kJoints},
         {1, "WEIGHTS_2", std::nullopt, 2,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
    {"influences-cross-width",
     {
         {2, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {3, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {4, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
         {5, "JOINTS_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kJoints},
         {1, "WEIGHTS_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
    {"influences-mixed",
     {
         {2, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {3, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {4, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
         {5, "JOINTS_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kJoints},
         {6, "WEIGHTS_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kWeights},
         {7, "JOINTS_2", std::nullopt, 2,
          ic::GltfDeformationAccessorRole::kJoints},
         {1, "WEIGHTS_2", std::nullopt, 2,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
    {"morph-encodings",
     {
         {0, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {1, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {2, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
         {4, "TEXCOORD_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {6, "TEXCOORD_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {8, "TEXCOORD_2", std::nullopt, 2,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {10, "TEXCOORD_3", std::nullopt, 3,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {12, "TEXCOORD_4", std::nullopt, 4,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {14, "COLOR_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {16, "COLOR_1", std::nullopt, 1,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {18, "COLOR_2", std::nullopt, 2,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {20, "COLOR_3", std::nullopt, 3,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {22, "COLOR_4", std::nullopt, 4,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {24, "COLOR_5", std::nullopt, 5,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {5, "TEXCOORD_0", 0, 0, ic::GltfDeformationAccessorRole::kMorphDelta},
         {7, "TEXCOORD_1", 0, 1, ic::GltfDeformationAccessorRole::kMorphDelta},
         {9, "TEXCOORD_2", 0, 2, ic::GltfDeformationAccessorRole::kMorphDelta},
         {11, "TEXCOORD_3", 0, 3, ic::GltfDeformationAccessorRole::kMorphDelta},
         {13, "TEXCOORD_4", 0, 4, ic::GltfDeformationAccessorRole::kMorphDelta},
         {15, "COLOR_0", 0, 0, ic::GltfDeformationAccessorRole::kMorphDelta},
         {17, "COLOR_1", 0, 1, ic::GltfDeformationAccessorRole::kMorphDelta},
         {19, "COLOR_2", 0, 2, ic::GltfDeformationAccessorRole::kMorphDelta},
         {21, "COLOR_3", 0, 3, ic::GltfDeformationAccessorRole::kMorphDelta},
         {23, "COLOR_4", 0, 4, ic::GltfDeformationAccessorRole::kMorphDelta},
         {25, "COLOR_5", 0, 5, ic::GltfDeformationAccessorRole::kMorphDelta},
     }},
    {"opaque-tail-alias",
     {
         {0, "POSITION", std::nullopt, std::nullopt,
          ic::GltfDeformationAccessorRole::kBaseAttribute},
         {1, "JOINTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kJoints},
         {2, "WEIGHTS_0", std::nullopt, 0,
          ic::GltfDeformationAccessorRole::kWeights},
     }},
}};
struct ExpectedChunk {
  std::uint32_t type;
  std::string_view hex;
};
// Independent original words/layout, expansion and correspondence literals.
const std::vector<Scenario> kScenarios{
    {"indexed-strip-edit",
     5,
     false,
     2,
     {0, 1, 2, 2, 1, 0, 2, 0, 1},
     {0, 1},
     {0, 1, 2, 3, 4, 5},
     {0, 1, 0},
     {0, 1, 2, 3, 4, 5, 2, 0, 1},
     {0, 1, 2, 2, 1, 0},
     {{0, 0}, {1, 1}, {2, 2}, {2, 2}, {1, 1}, {3, 0}},
     {{0, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "0",
       false},
      {1, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 3,
       4, "000000000000000000000000", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       3, 16,
       "0000803f0000000000000000000000000000803f0000000000000000000000000000803"
       "f000000000000000000000000",
       false},
      {3, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kScalar, false,
       5, 2, "00000100020000000100", true}}},
    {"nonindexed-strip-identity",
     5,
     false,
     std::nullopt,
     {0, 1, 2, 2, 1, 3},
     {0, 1},
     {0, 1, 2, 3, 4, 5},
     {0, 1},
     {0, 1, 2, 3, 4, 5},
     {0, 1, 2, 2, 1, 3},
     {{0, 0}, {1, 1}, {2, 2}, {2, 2}, {1, 1}, {3, 3}},
     {{0, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       4, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "00000803f0000803f00000000",
       false},
      {1, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 4,
       4, "00000000000000000000000000000000", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       4, 16,
       "0000803f0000000000000000000000000000803f0000000000000000000000000000803"
       "f0000000000000000000000000000803f000000000000000000000000",
       false}}},
    {"indexed-fan-edit",
     6,
     false,
     2,
     {0, 1, 2, 0, 2, 1, 0, 1, 2},
     {0, 1},
     {0, 1, 2, 3, 4, 5},
     {0, 1, 0},
     {0, 1, 2, 3, 4, 5, 0, 1, 2},
     {0, 1, 2, 0, 2, 1},
     {{0, 0}, {1, 1}, {2, 2}, {0, 0}, {2, 2}, {3, 1}},
     {{0, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "0",
       false},
      {1, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 3,
       4, "000000000000000000000000", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       3, 16,
       "0000803f0000000000000000000000000000803f0000000000000000000000000000803"
       "f000000000000000000000000",
       false},
      {3, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kScalar, false,
       5, 2, "00000100020001000200", true}}},
    {"nonindexed-fan-identity",
     6,
     false,
     std::nullopt,
     {0, 1, 2, 0, 2, 3},
     {0, 1},
     {0, 1, 2, 3, 4, 5},
     {0, 1},
     {0, 1, 2, 3, 4, 5},
     {0, 1, 2, 0, 2, 3},
     {{0, 0}, {1, 1}, {2, 2}, {0, 0}, {2, 2}, {3, 3}},
     {{0, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       4, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "00000803f0000803f00000000",
       false},
      {1, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 4,
       4, "00000000000000000000000000000000", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       4, 16,
       "0000803f0000000000000000000000000000803f0000000000000000000000000000803"
       "f0000000000000000000000000000803f000000000000000000000000",
       false}}},
    {"influences-unorm16",
     4,
     true,
     1,
     {0, 1, 2, 1, 2, 0, 0, 2, 1},
     {0, 2},
     {0, 1, 2, 6, 7, 8},
     {0, 0, 1},
     {0, 1, 2, 1, 2, 0, 3, 4, 5},
     {0, 1, 2, 0, 2, 1},
     {{0, 0}, {1, 1}, {2, 2}, {6, 0}, {7, 2}, {8, 1}},
     {{0, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kScalar, false,
       9, 2, "000001000200010002000000000002000100", true},
      {1, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, true, 3,
       8, "00000000000000000000000000000000000000000100feff", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "0",
       false},
      {3, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, false,
       3, 8, "000101010201030100010101020103010001010102010301", false},
      {4, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, true, 3,
       8, "002000200020002055550000000000000000000000000000", false},
      {5, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, false,
       3, 8, "040105010601070104010501060107010401050106010701", false},
      {6, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, true, 3,
       8, "002000200020ff1faaaa0000000000000000000000000000", false},
      {7, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, false,
       3, 8, "080109010a010b01080109010a010b01080109010a010b01", false}}},
    {"influences-cross-width",
     4,
     true,
     1,
     {0, 1, 2, 1, 2, 0, 0, 2, 1},
     {0, 2},
     {0, 1, 2, 6, 7, 8},
     {0, 0, 1},
     {0, 1, 2, 1, 2, 0, 3, 4, 5},
     {0, 1, 2, 0, 2, 1},
     {{0, 0}, {1, 1}, {2, 2}, {6, 0}, {7, 2}, {8, 1}},
     {{0, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kScalar, false,
       9, 2, "000001000200010002000000000002000100", true},
      {1, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, true, 3,
       8, "aaaa000000000000fefe0000000000000101000000000000", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "0",
       false},
      {3, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 3,
       4, "000102030001020300010203", false},
      {4, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, true, 3,
       4, "5500000001000000fe000000", false},
      {5, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 3,
       4, "040506070405060704050607", false}}},
    {"influences-mixed",
     4,
     true,
     1,
     {0, 1, 2, 1, 2, 0, 0, 2, 1},
     {0, 2},
     {0, 1, 2, 6, 7, 8},
     {0, 0, 1},
     {0, 1, 2, 1, 2, 0, 3, 4, 5},
     {0, 1, 2, 0, 2, 1},
     {{0, 0}, {1, 1}, {2, 2}, {6, 0}, {7, 2}, {8, 1}},
     {{0, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kScalar, false,
       9, 2, "000001000200010002000000000002000100", true},
      {1, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       3, 16,
       "0000003f0000008000000000000000000000800d0000008000000000000000000000803"
       "f000000800000000000000000",
       false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "0",
       false},
      {3, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, false,
       3, 8, "000101010201030100010101020103010001010102010301", false},
      {4, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, true, 3,
       4, "40000000ff00000000000000", false},
      {5, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, false,
       3, 8, "040105010601070104010501060107010401050106010701", false},
      {6, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, true, 3,
       8, "004000000000000000000000000000000000000000000000", false},
      {7, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, false,
       3, 8, "080109010a010b01080109010a010b01080109010a010b01", false}}},
    {"morph-encodings",
     4,
     true,
     1,
     {0, 1, 2, 1, 2, 0},
     {0},
     {0, 1, 2},
     {0, 0},
     {0, 1, 2, 1, 2, 0},
     {0, 1, 2},
     {{0, 0}, {1, 1}, {2, 2}},
     {{0, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "0",
       false},
      {1, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 3,
       4, "000000000000000000000000", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       3, 16,
       "0000803f0000000000000000000000000000803f0000000000000000000000000000803"
       "f000000000000000000000000",
       false},
      {3, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kScalar, false,
       6, 2, "000001000200010002000000", true},
      {4, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec2, true, 3,
       4, "408040804080", false},
      {5, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec2, false,
       3, 8, "0000003e000080bd0000003e000080bd0000003e000080bd", false},
      {6, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec2, true, 3,
       4, "004000800040008000400080", false},
      {7, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec2, true, 3,
       4, "010002000100020001000200", false},
      {8, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec2, false,
       3, 8, "0000803e0000003f0000803e0000003f0000803e0000003f", false},
      {9, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec2, true, 3,
       4, "010201020102", false},
      {10, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec2, false,
       3, 8, "0000803e0000003f0000803e0000003f0000803e0000003f", false},
      {11, ic::GltfComponentType::kInt8, ic::GltfAccessorShape::kVec2, true, 3,
       4, "01fe01fe01fe", false},
      {12, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec2, false,
       3, 8, "0000803e0000003f0000803e0000003f0000803e0000003f", false},
      {13, ic::GltfComponentType::kInt16, ic::GltfAccessorShape::kVec2, true, 3,
       4, "0100feff0100feff0100feff", false},
      {14, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec3, true, 3,
       4, "4080ff4080ff4080ff", false},
      {15, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, true,
       3, 8, "010002000300040001000200030004000100020003000400", false},
      {16, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec4, true,
       3, 8, "00400080ffffffff00400080ffffffff00400080ffffffff", false},
      {17, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec3, true, 3,
       4, "010203010203010203", false},
      {18, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000803e0000003f0000803f0000803e0000003f0000803f0000803e0000003f0000803"
       "f",
       false},
      {19, ic::GltfComponentType::kInt8, ic::GltfAccessorShape::kVec4, true, 3,
       4, "01fe03fc01fe03fc01fe03fc", false},
      {20, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, true, 3,
       4, "4080ffff4080ffff4080ffff", false},
      {21, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       3, 16,
       "0000003e000080bd0000803e000000000000003e000080bd0000803e000000000000003"
       "e000080bd0000803e00000000",
       false},
      {22, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec3, true,
       3, 8, "00400080ffff00400080ffff00400080ffff", false},
      {23, ic::GltfComponentType::kInt16, ic::GltfAccessorShape::kVec3, true, 3,
       8, "0100feff03000100feff03000100feff0300", false},
      {24, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       3, 16,
       "0000803e0000003f0000803f0000803f0000803e0000003f0000803f0000803f0000803"
       "e0000003f0000803f0000803f",
       false},
      {25, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kVec3, true,
       3, 8, "010002000300010002000300010002000300", false}}},
    {"opaque-tail-alias",
     4,
     true,
     1,
     {0, 1, 2, 1, 2, 0},
     {0},
     {0, 1, 2},
     {0, 0},
     {0, 1, 2, 1, 2, 0},
     {0, 1, 2},
     {{0, 0}, {1, 1}, {2, 2}},
     {{0, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec3, false,
       3, 12,
       "0000000000000000000000000000803f0000000000000000000000000000803f0000000"
       "0",
       false},
      {1, ic::GltfComponentType::kUint8, ic::GltfAccessorShape::kVec4, false, 3,
       4, "000000000000000000000000", false},
      {2, ic::GltfComponentType::kFloat32, ic::GltfAccessorShape::kVec4, false,
       3, 16,
       "0000803f0000000000000000000000000000803f0000000000000000000000000000803"
       "f000000000000000000000000",
       false},
      {3, ic::GltfComponentType::kUint16, ic::GltfAccessorShape::kScalar, false,
       6, 2, "000001000200010002000000", true}}},
};

const std::array<ExpectedChunk, kRetainedChunkCount> kChunks{
    {{5130562U,
      "0000000000000000000000000000803f0000000000000000000000000000803f00000000"
      "0000000000000000000000000000803f0000000000000000000000000000803f00000000"
      "00000000000000000000803f000000000000000000000000000001000200010002000000"
      "a55a0000"},
     {305419896U, "11223344"},
     {270544960U, "09080706"},
     {305419896U, "55667788"}}};

std::string Hex(std::span<const std::byte> bytes) {
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned>(byte);
    result.push_back(kHexDigits[value >> kHexShift]);
    result.push_back(kHexDigits[value & kHexMask]);
  }
  return result;
}
std::string RawHex(const ic::GltfDeformationRawValues& raw) {
  return std::visit(
      [](const auto& view) -> std::string {
        if constexpr (std::is_same_v<std::decay_t<decltype(view)>,
                                     ic::GltfFloat32BitWords>) {
          return Hex(view.little_endian_scalar_words);
        } else {
          return Hex(std::as_bytes(view));
        }
      },
      raw);
}
template <class A, class B>
bool Equal(const A& first, const B& second) {
  return first.size() == second.size() &&
         std::equal(first.begin(), first.end(), second.begin());
}
bool Domains(const ic::GltfDeformationObservation& observation) {
  return observation.predicates.influences == true &&
         observation.predicates.bindings == true &&
         observation.predicates.morphs == true &&
         observation.predicates.affecting_animation == true;
}

void CheckPrimitive(const ic::GltfPrimitiveExtraction& extraction,
                    const Scenario& scenario, bool reloaded) {
  const auto* proof = extraction.Deformation();
  Require(proof != nullptr, "real typed deformation owner");
  for (const auto& expected : scenario.accessors) {
    if (reloaded && scenario.removed && expected.source_indices) continue;
    const auto rows = proof->Accessors();
    const auto found = std::find_if(
        rows.begin(), rows.end(),
        [&](const auto& row) { return row.accessor == expected.accessor; });
    Require(found != rows.end(),
            "every independently indexed original accessor retained");
    Require(found->component_type == expected.component &&
                found->shape == expected.shape &&
                found->normalized == expected.normalized &&
                found->element_count == expected.count &&
                found->base_byte_stride == expected.stride &&
                RawHex(found->raw) == expected.hex,
            "exact original/reloaded scalar words, types, shapes, "
            "normalization and stride");
  }
  Require(Equal(extraction.TriangleIndices(),
                reloaded ? scenario.candidate_triangles : scenario.triangles),
          "independent strip/fan expansion or reloaded triangle rows");
}

void CheckAliasResources(const ic::GltfAsset& asset) {
  const auto resources = asset.Resources();
  Require(resources.size() >= kResourceCount,
          "original resource identities retained alongside added geometry");
  for (std::size_t i = 1; i < kResourceCount; ++i) {
    const auto identity = [&](const auto& resource) {
      return resource.kind == ic::GltfResourceKind::kBuffer &&
             resource.table_index == i;
    };
    Require(std::count_if(resources.begin(), resources.end(), identity) == 1,
            "one original external buffer identity at each literal table row");
    const auto found =
        std::find_if(resources.begin(), resources.end(), identity);
    Require(
        found->authored_uri == (i == 1 ? kEscapedSharedFile : kSharedFile) &&
            found->logical_bytes == kExternalLogicalBytes &&
            Hex(found->bytes) == kSharedHex,
        "original authored URI alias, logical bounds and full physical tail");
  }
}

void CheckRecord(const repair::GltfDuplicateWorkflowRecord& record,
                 const Scenario& scenario) {
  Require(record.ExtractedPrimitive() && record.Source() && record.Repair() &&
              record.Output() && record.ReloadedAsset() &&
              record.ReloadedPrimitive() && record.OutputInspection(),
          "complete returned native owners");
  CheckPrimitive(*record.ExtractedPrimitive(), scenario, false);
  CheckPrimitive(*record.ReloadedPrimitive(), scenario, true);
  Require(record.ExtractedPrimitive()->SourceMode() == scenario.mode &&
              record.ReloadedPrimitive()->SourceMode() ==
                  (scenario.removed ? kTriangleMode : scenario.mode),
          "identity source mode or explicit edited triangle conversion");
  if (scenario.id.starts_with("nonindexed"))
    Require(!record.ReloadedPrimitive()->SourceIndices(),
            "identity preserves authored index absence");
  Require(
      Equal(record.Repair()->SourceFaces(), scenario.source_faces) &&
          Equal(record.Repair()->SourceCorners(), scenario.source_corners) &&
          Equal(record.Repair()->FaceMap(), scenario.face_map) &&
          Equal(record.Repair()->CornerMap(), scenario.corner_map),
      "complete independently expected Repair maps");
  const auto corners = record.OutputInspection()->OriginalCorners();
  Require(corners.size() == scenario.original_corners.size(),
          "original correspondence count");
  for (std::size_t i = 0; i < corners.size(); ++i) {
    Require(corners[i].primitive_stream_corner ==
                    scenario.original_corners[i].primitive_stream_corner &&
                corners[i].accessor_row ==
                    scenario.original_corners[i].accessor_row,
            "original stream/accessor correspondence");
  }
  Require(record.Source()->SourceAsset().OriginalJson().data() !=
              record.ReloadedAsset()->OriginalJson().data(),
          "independent memory reload has distinct source ownership");
  const auto* proof = record.ExtractedPrimitive()->Deformation();
  const auto associations = record.Source()->DeformationAssociations();
  constexpr std::uint64_t kSelectedMesh = 0, kSelectedPrimitive = 0;
  constexpr std::string_view kIndicesSemantic = "indices";
  const auto inventory = std::find_if(
      kRoleInventories.begin(), kRoleInventories.end(),
      [&](const auto& row) { return row.scenario == scenario.id; });
  Require(inventory != kRoleInventories.end(),
          "independent role inventory for every scenario");
  const auto uses = proof->AccessorUses();
  const auto selected = [&](const auto& use) {
    using Role = ic::GltfDeformationAccessorRole;
    return use.mesh == kSelectedMesh && use.primitive == kSelectedPrimitive &&
           use.semantic != kIndicesSemantic &&
           (use.role == Role::kBaseAttribute || use.role == Role::kJoints ||
            use.role == Role::kWeights || use.role == Role::kMorphDelta);
  };
  Require(static_cast<std::size_t>(std::count_if(
              uses.begin(), uses.end(), selected)) == inventory->roles.size() &&
              associations.size() == inventory->roles.size(),
          "exact independently expected selected role and association counts");
  for (const auto& expected : inventory->roles) {
    Require(std::count_if(uses.begin(), uses.end(),
                          [&](const auto& use) {
                            return selected(use) &&
                                   use.accessor == expected.accessor &&
                                   use.semantic == expected.semantic &&
                                   use.target == expected.target &&
                                   use.set == expected.set &&
                                   use.role == expected.role;
                          }) == 1,
            "one-to-one independent authored role key in actual accessor uses");
    const auto matches = [&](const auto& association) {
      return association.mesh == kSelectedMesh &&
             association.primitive == kSelectedPrimitive &&
             association.accessor == expected.accessor &&
             association.semantic == expected.semantic &&
             association.target == expected.target &&
             association.set == expected.set;
    };
    Require(
        std::count_if(associations.begin(), associations.end(), matches) == 1,
        "one-to-one independent authored role key in actual associations");
    const auto found =
        std::find_if(associations.begin(), associations.end(), matches);
    Require(!found->declaration_name.empty() ||
                found->destination ==
                    ic::GltfDeformationDestination::kCanonicalPositions,
            "owned association name or canonical position carrier");
    if (found->canonical_attribute) {
      Require(*found->canonical_attribute <
                  record.Source()->Mesh().View().attributes.size(),
              "association names a real canonical attribute");
    }
  }
  const auto checked = inventory->roles.size();
  if (scenario.id == kOpaqueScenario) {
    CheckAliasResources(*record.ReloadedAsset());
  }
  const auto* facts = record.DeformationFacts();
  Require(facts && facts->source && facts->writing && facts->verification &&
              Domains(*facts->source) && Domains(*facts->writing) &&
              Domains(*facts->verification) &&
              facts->verification->predicates.output_reload == true &&
              facts->verification->predicates.canonical_association == true,
          "own source, writer and independent verification preservation "
          "predicates");
  std::cout << scenario.id << " accessors=" << proof->Accessors().size()
            << " associations=" << checked
            << " faces=" << record.Repair()->SourceFaces().size() << '\n';
}

ic::GltfExtractionRequest Extraction(const Scenario& scenario) {
  return {scenario.mode == kTriangleMode
              ? ic::GltfTriangleRequest::kRequireTriangles
              : ic::GltfTriangleRequest::kConvertStripOrFan,
          ic::GltfValueRequest::kRawAndNormalized,
          ic::GltfProcessingProfile::kDeformationRetainedTrianglesV1,
          kDeformation,
          {}};
}
void CheckMemory(const fs::path& fixtures, const fs::path& root,
                 const Scenario& scenario,
                 const geo::ExecutionContext& context) {
  const auto wire = ReadBytes(fixtures / (std::string(scenario.id) + ".gltf"));
  const std::array target{
      repair::DuplicateTarget{0, scenario.removed.value_or(0)}};
  const auto selected = scenario.removed
                            ? std::span(target)
                            : std::span<const repair::DuplicateTarget>{};
  repair::GltfDuplicateWorkflowRequest request{
      wire, {}, scenario.id, 0, Extraction(scenario), selected, {}};
  request.deformation = {kDeformation, kDeformation, kDeformation,
                         kDeformation, kDeformation, kDeformation};
  auto limits = MemoryLimits();
  limits.repair.max_declaration_name_bytes = kNativeNameBound;
  const auto result = repair::RunGltfDuplicateWorkflow(
      request, limits, MemoryWork(), context, {});
  Require(
      result.status == repair::GltfWorkflowStatus::kSuccess && result.record,
      "successful supplied-memory pipeline");
  CheckRecord(*result.record, scenario);
  WriteBytes(root / (std::string(scenario.id) + "-memory-report.json"),
             result.record->ReportJson());
  WriteBytes(root / (std::string(scenario.id) + "-memory-output.gltf"),
             result.record->Output()->Json());
}

void CheckOpaque(const ic::GltfFileAsset& source,
                 const ic::GltfPublishedBundle& published) {
  Require(source.Container() && published.Container() &&
              source.Container()->ChunkCount() == kContainerChunkCount &&
              published.Container()->ChunkCount() == kContainerChunkCount,
          "ordered JSON/BIN/A/B/A chunk inventory");
  for (std::size_t i = 0; i < kChunks.size(); ++i) {
    const auto original = source.Container()->Chunk(i + 1);
    const auto output = published.Container()->Chunk(i + 1);
    Require(original.type == kChunks[i].type &&
                output.type == kChunks[i].type &&
                Hex(original.bytes) == kChunks[i].hex &&
                Hex(output.bytes) == kChunks[i].hex,
            "full BIN tail/padding and distinct ordered duplicate-type opaque "
            "payloads");
  }
  const auto resources = source.Asset().Resources();
  Require(resources.size() == kResourceCount,
          "distinct BIN and two external buffer identities");
  CheckAliasResources(source.Asset());
  CheckAliasResources(published.Inspection().ReloadedAsset());
  std::size_t shared = 0;
  for (const auto& file : published.Files()) {
    if (file.path != kSharedFile) continue;
    ++shared;
    Require(Hex(file.bytes) == kSharedHex, "full staged external alias bytes");
  }
  Require(shared == 1, "one physical file serves both resource identities");
}
void CheckPhysical(const fs::path& fixtures, const fs::path& root,
                   const Scenario& scenario,
                   const geo::ExecutionContext& context) {
  const auto area = root / std::string(scenario.id);
  Require(fs::create_directory(area) && fs::create_directory(area / "input"),
          "exclusive physical variant directories");
  const auto extension = scenario.glb ? ".glb" : ".gltf";
  const auto path = area / "input" / (std::string("source") + extension);
  const auto wire = ReadBytes(fixtures / (std::string(scenario.id) + extension +
                                          (scenario.glb ? ".hex" : "")),
                              scenario.glb);
  WriteBytes(path, wire);
  if (scenario.id == kOpaqueScenario)
    WriteBytes(area / "input" / kSharedFile,
               ReadBytes(fixtures / "shared.bin.hex", true));
  ic::GltfFileReadOptions options;
  options.read_request = {ic::GltfAdmissionProfile::kRetainedCoreDeformationV1,
                          kDeformation};
  auto captured = ic::ReadGltfFileAsset(path, options, FileReadLimits(),
                                        FileReadWork(), context);
  Require(captured.asset.has_value(), "legal physical fixture admission");
  const std::array target{
      repair::DuplicateTarget{0, scenario.removed.value_or(0)}};
  const auto selected = scenario.removed
                            ? std::span(target)
                            : std::span<const repair::DuplicateTarget>{};
  repair::GltfDuplicateFilePublication publication{
      area / "published",
      "report.json",
      {},
      {kDeformation, kDeformation, kDeformation}};
  repair::GltfDuplicateFileWorkflowRequest request{
      *captured.asset, scenario.id, 0,           Extraction(scenario),
      selected,        {},          &publication};
  request.deformation = {{},           kDeformation, kDeformation,
                         kDeformation, kDeformation, kDeformation};
  auto limits = Limits();
  limits.memory.repair.max_declaration_name_bytes = kNativeNameBound;
  const auto result = repair::RunGltfDuplicateFileWorkflow(request, limits,
                                                           Work(), context, {});
  Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
              result.record && result.record->Memory() &&
              result.record->Published(),
          "successful physical memory and staged pipeline");
  CheckRecord(*result.record->Memory(), scenario);
  const auto* facts = result.record->DeformationFacts();
  Require(
      facts && !facts->source_read && facts->publication_verification &&
          Domains(*facts->publication_verification) &&
          facts->publication_verification->predicates.canonical_association ==
              true &&
          facts->publication_verification->predicates.staged_reload == true,
      "own staged comparisons and inapplicable retained source-read absence");
  Require(ReadBytes(path) == wire,
          "protected original wire remains byte-identical");
  Require(ReadBytes(area / "published/report.json") ==
              std::vector<std::byte>(result.record->ReportJson().begin(),
                                     result.record->ReportJson().end()),
          "physical staged report authority");
  if (scenario.id == kOpaqueScenario)
    CheckOpaque(*captured.asset, *result.record->Published());
  std::cout << scenario.id << ' ' << (scenario.glb ? "GLB" : "JSON")
            << " published PASS\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2, "fixture directory argument required");
    const auto fixtures = fs::canonical(fs::path(argv[1]));
    Require(fs::is_directory(fixtures), "resolved fixture directory");
    TemporaryRoot owned;
    const geo::ExecutionContext context({kWorkers, 1, kLedgerBytes});
    constexpr std::size_t kRoleOracleCount = 59;
    std::size_t original_roles = 0;
    for (const auto& inventory : kRoleInventories) {
      original_roles += inventory.roles.size();
    }
    Require(original_roles == kRoleOracleCount,
            "all independent selected role keys present");
    std::size_t original_accessors = 0, memory_cases = 0;
    Require(kScenarios.size() == kScenarioCount,
            "all nine independent discriminators present");
    for (const auto& scenario : kScenarios) {
      original_accessors += scenario.accessors.size();
      if (scenario.id != kOpaqueScenario) {
        CheckMemory(fixtures, owned.Path(), scenario, context);
        ++memory_cases;
      }
      Require(
          context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
          "memory owners/workers released");
      CheckPhysical(fixtures, owned.Path(), scenario, context);
      Require(
          context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
          "physical owners/workers released");
    }
    Require(original_accessors == kAccessorOracleCount &&
                memory_cases == kMemoryCaseCount,
            "complete literal accessor and memory-case inventories");
    owned.CheckCleanup();
    std::cout
        << "format-preservation variants=9 memory=8 physical=9 "
           "accessor-oracles=66 PASS workers=0 payload=0 cleanup=checked\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "format preservation: " << error.what() << '\n';
    return 1;
  }
}
