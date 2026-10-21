// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
using Transform = ic::GltfLocalTransformLayout;

// Deliberately bounded example budgets, independent of workflow presets. The
// fixture's largest native array contains only six triangle indices.
constexpr std::uint64_t kFileBytes = 1024 * 1024;
constexpr std::uint64_t kStageBytes = 16 * kFileBytes;
constexpr std::size_t kLedgerBytes = 64 * kFileBytes;
constexpr std::uint64_t kDocumentNodes = 4096;
constexpr std::uint64_t kDocumentDepth = 64;
constexpr std::uint64_t kStringBytes = 64 * 1024;
constexpr std::uint64_t kNumericTokenBytes = 128;
constexpr std::uint64_t kCoefficientDigits = 64;
constexpr std::uint64_t kExponentDigits = 32;
constexpr std::uint64_t kTableRows = 512;
constexpr std::uint64_t kGraphEdges = 1024;
constexpr std::uint64_t kInstances = 64;
constexpr std::uint64_t kFiles = 32;
constexpr std::uint64_t kBindings = 64;
constexpr std::uint64_t kPathBytes = 1024;
constexpr std::uint64_t kAllPathBytes = 8192;
constexpr std::uint64_t kNativePathUnits = 4096;
constexpr std::uint64_t kParallelMinimum = 128;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCancellationBlock = 32;
constexpr std::uint64_t kMaximumJobs = 64;
constexpr std::size_t kWorkers = 2;
constexpr std::uint64_t kTriangleMode = 4;
constexpr std::string_view kSceneName = "shared-scene";
constexpr std::string_view kPositionSemantic = "POSITION";
constexpr std::string_view kTextureSemantic = "TEXCOORD_0";
#ifdef _WIN32
using ArgumentChar = wchar_t;
constexpr std::wstring_view kChangedMode = L"changed";
constexpr std::wstring_view kUnchangedMode = L"unchanged";
#else
using ArgumentChar = char;
constexpr std::string_view kChangedMode = "changed";
constexpr std::string_view kUnchangedMode = "unchanged";
#endif

// Independent literal fixture expectations from the public walkthrough
// contract.
constexpr std::array<std::string_view, 4> kNodeNames{
    "scaled-shared", "matrix-shared", "unrelated", "unused-node"};
constexpr std::array<std::uint64_t, 3> kSceneRoots{0, 1, 2};
constexpr std::array<std::uint64_t, 3> kInstanceMeshes{0, 0, 1};
constexpr std::array<float, Transform::kSlotCount> kScaledTrs{2, 0, 0, 0, 0,
                                                              0, 1, 2, 1, 1};
constexpr std::array<float, Transform::kSlotCount> kMatrix{
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -3, 4, 0, 1};
constexpr std::array<float, Transform::kSlotCount> kUnrelatedTrs{0, 0, 5, 0, 0,
                                                                 0, 1, 1, 1, 1};
constexpr std::array<float, Transform::kSlotCount> kDefaultTrs{0, 0, 0, 0, 0,
                                                               0, 1, 1, 1, 1};
constexpr std::array<float, 9> kSharedPositions{0, 0, 0, 1, 0, 0, 0, 1, 0};
constexpr std::array<float, 9> kOtherPositions{10, 0, 0, 12, 0, 0, 10, 2, 0};
constexpr std::array<float, 6> kTextureCoordinates{0, 0, 1, 0, 0, 1};
constexpr std::array<std::uint32_t, 6> kDuplicateTriangles{0, 1, 2, 0, 1, 2};
constexpr std::array<std::uint32_t, 3> kOneTriangle{0, 1, 2};

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

ic::GltfFileReadLimits ReadLimits() {
  const ic::GltfReadLimits asset{
      kFileBytes,      kFileBytes,   kStageBytes,        kDocumentNodes,
      kDocumentDepth,  kStringBytes, kNumericTokenBytes, kCoefficientDigits,
      kExponentDigits, kTableRows,   kGraphEdges,        kInstances,
      kFileBytes,      kStageBytes};
  return {{kFiles, kBindings, kFileBytes, kStageBytes, kPathBytes,
           kAllPathBytes, kNativePathUnits, kStageBytes},
          {asset, kStageBytes},
          {kFileBytes, kFiles, kStageBytes}};
}

ic::GltfFileReadWorkPolicy ReadWork() {
  return {
      {kPartitionBytes, kParallelMinimum, kPartitionBytes, kCancellationBlock,
       kMaximumJobs},
      {kParallelMinimum, kPartitionBytes, kParallelMinimum, kPartitionBytes},
      {kParallelMinimum, kPartitionBytes}};
}

ic::GltfFileAsset Capture(const std::filesystem::path& entry,
                          const geo::ExecutionContext& context) {
  const auto result =
      ic::ReadGltfFileAsset(entry, {}, ReadLimits(), ReadWork(), context);
  if (!result.asset) {
    std::cerr << "Capture status=" << static_cast<int>(result.diagnostic.status)
              << " code=" << static_cast<int>(result.diagnostic.code) << '\n';
    throw std::runtime_error("Native file capture failed");
  }
  return *result.asset;
}

void CheckTransform(const ic::GltfNodeView& node, ic::GltfTransformKind kind,
                    std::uint8_t mask,
                    const std::array<float, Transform::kSlotCount>& expected) {
  Require(node.transform_kind == kind && node.authored_transform_mask == mask,
          "Authored transform presence changed");
  for (std::size_t slot = 0; slot < expected.size(); ++slot)
    Require(node.local_transform_bits[slot] ==
                std::bit_cast<std::uint32_t>(expected[slot]),
            "Local transform binary32 bits differ");
}

void CheckGraph(const ic::GltfAsset& asset) {
  const auto nodes = asset.Nodes();
  Require(nodes.size() == kNodeNames.size(), "Expected four retained nodes");
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    Require(node.table_index == index && node.name == kNodeNames[index],
            "Node identity differs");
    Require(!node.parent && node.children.empty() && !node.skin,
            "Unexpected hierarchy or skin binding");
    const std::optional<std::uint64_t> mesh =
        index < kInstanceMeshes.size()
            ? std::optional<std::uint64_t>(kInstanceMeshes[index])
            : std::nullopt;
    Require(node.mesh == mesh, "Shared or unused node mesh binding differs");
  }
  CheckTransform(nodes[0], ic::GltfTransformKind::kTrs,
                 Transform::kAuthoredTranslation |
                     Transform::kAuthoredRotation | Transform::kAuthoredScale,
                 kScaledTrs);
  CheckTransform(nodes[1], ic::GltfTransformKind::kMatrix,
                 Transform::kAuthoredMatrix, kMatrix);
  CheckTransform(nodes[2], ic::GltfTransformKind::kTrs,
                 Transform::kAuthoredTranslation, kUnrelatedTrs);
  CheckTransform(nodes[3], ic::GltfTransformKind::kDefaultTrs, 0, kDefaultTrs);
  const auto scenes = asset.Scenes();
  Require(scenes.size() == 1 && scenes[0].table_index == 0 &&
              scenes[0].name == kSceneName &&
              std::ranges::equal(scenes[0].roots, kSceneRoots),
          "Scene identity or root ordering differs");
  Require(asset.PrimitiveCount() == 2 && asset.InstanceCount() == 3,
          "Primitive or instance count differs");
  for (std::uint64_t ordinal = 0; ordinal < asset.PrimitiveCount(); ++ordinal) {
    const auto primitive = asset.Primitive(ordinal);
    Require(primitive && primitive->MeshIndex() == ordinal &&
                primitive->PrimitiveIndex() == 0,
            "Primitive ordinal identity differs");
  }
  for (std::uint64_t ordinal = 0; ordinal < asset.InstanceCount(); ++ordinal) {
    const auto instance = asset.Instance(ordinal);
    Require(instance && instance->SceneIndex() == 0 &&
                instance->NodeIndex() == ordinal,
            "Instance scene/node identity differs");
    const auto primitive = instance->Primitive();
    Require(primitive.MeshIndex() == kInstanceMeshes[ordinal] &&
                primitive.PrimitiveIndex() == 0,
            "Instance shared primitive binding differs");
  }
}

void CheckChannel(const ic::GltfChannelView& channel,
                  ic::GltfAccessorShape shape, std::optional<std::uint64_t> set,
                  std::span<const float> expected) {
  Require(channel.shape == shape && channel.set_index == set &&
              channel.component_type == ic::GltfComponentType::kFloat32 &&
              !channel.normalized && channel.element_count == 3 &&
              channel.normalized_values.empty(),
          "Vertex channel descriptor differs");
  const auto* values = std::get_if<std::span<const float>>(&channel.raw);
  Require(values && values->size() == expected.size(),
          "Vertex channel extent differs");
  for (std::size_t component = 0; component < expected.size(); ++component)
    Require(std::bit_cast<std::uint32_t>((*values)[component]) ==
                std::bit_cast<std::uint32_t>(expected[component]),
            "Vertex channel binary32 rows differ");
}

void CheckPrimitive(const ic::GltfAsset& asset, std::uint64_t ordinal,
                    bool changed) {
  const auto handle = asset.Primitive(ordinal);
  Require(handle.has_value(), "Expected primitive handle");
  const auto result = ic::ExtractGltfPrimitiveCapped(
      asset, *handle,
      {ic::GltfTriangleRequest::kRequireTriangles, ic::GltfValueRequest::kRaw},
      {kFileBytes, kFileBytes, kStageBytes});
  Require(result.extraction.has_value(), "Native primitive extraction failed");
  const auto& extraction = *result.extraction;
  Require(extraction.SourceMode() == kTriangleMode &&
              extraction.MaterialIndex() == ordinal,
          "Triangle mode or material binding differs");
  const auto expected =
      ordinal == 0 && !changed
          ? std::span<const std::uint32_t>(kDuplicateTriangles)
          : std::span<const std::uint32_t>(kOneTriangle);
  Require(std::ranges::equal(extraction.TriangleIndices(), expected),
          "Selected or unrelated triangle rows differ");
  const auto channels = extraction.Channels();
  Require(channels.size() == 2, "Expected position and texture channels");
  bool position = false;
  bool texture = false;
  for (const auto& channel : channels) {
    if (channel.semantic == kPositionSemantic) {
      Require(!position, "Duplicate position channel");
      position = true;
      CheckChannel(channel, ic::GltfAccessorShape::kVec3, std::nullopt,
                   ordinal == 0 ? kSharedPositions : kOtherPositions);
    } else {
      Require(channel.semantic == kTextureSemantic && !texture,
              "Unexpected vertex channel");
      texture = true;
      CheckChannel(channel, ic::GltfAccessorShape::kVec2, 0,
                   kTextureCoordinates);
    }
  }
  Require(position && texture, "Missing vertex channel");
}
int RunInspector(int argc, ArgumentChar** argv) {
  try {
    Require(argc == 4, "Expected SOURCE_ENTRY OUTPUT_ENTRY changed|unchanged");
    const std::basic_string_view<ArgumentChar> mode(argv[3]);
    Require(mode == kChangedMode || mode == kUnchangedMode,
            "Invalid expected edit");
    const geo::ExecutionContext context({kWorkers, 1, kLedgerBytes});
    const auto source = Capture(std::filesystem::path(argv[1]), context);
    const auto output = Capture(std::filesystem::path(argv[2]), context);
    Require(source.Format() == output.Format(), "Container format changed");
    CheckGraph(source.Asset());
    CheckGraph(output.Asset());
    for (std::uint64_t ordinal = 0; ordinal < 2; ++ordinal) {
      CheckPrimitive(source.Asset(), ordinal, false);
      CheckPrimitive(output.Asset(), ordinal, mode == kChangedMode);
    }
    if (mode == kUnchangedMode)
      Require(std::ranges::equal(source.EntryBytes(), output.EntryBytes()),
              "Unchanged entry/container bytes differ");
    Require(context.ActiveWorkers() == 0, "Native workers remain active");
    std::cout
        << "Native static scene inspection passed: four nodes, one scene, "
           "three instances, two primitives, exact transforms and channels\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Static scene inspection failed: " << error.what() << '\n';
    return 2;
  }
}
}  // namespace

#ifdef _WIN32
// Preserve native Windows path code units rather than passing through the CRT
// ANSI argv conversion. The native capture still owns path admission.
int wmain(int argc, wchar_t** argv) { return RunInspector(argc, argv); }
#else
int main(int argc, char** argv) { return RunInspector(argc, argv); }
#endif
