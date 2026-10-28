// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_OFF_SURFACE_EXPORT_FIXTURE_H_
#define MESHVALE_REPAIR_TESTS_OFF_SURFACE_EXPORT_FIXTURE_H_
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_surface_export.h>
#include <meshvale/repair/off_workflow.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
namespace meshvale::repair::test_off_surface {
namespace geo = geometry;
namespace ic = interchange;
namespace fs = std::filesystem;
inline constexpr std::uint64_t kMiB = 1024 * 1024;
inline constexpr std::uint64_t kLedgerBytes = 2ULL * 1024 * kMiB;
inline constexpr double kShiftError = 0x1p-30;
inline constexpr std::string_view kEntry = "source.off";

// Keys belong to the independent GLB/capsule/receipt reader.
inline constexpr std::string_view kPositionAttributeKey = "POSITION";
inline constexpr std::string_view kAccessorsKey = "accessors";
inline constexpr std::string_view kAnimationsKey = "animations";
inline constexpr std::string_view kAssetKey = "asset";
inline constexpr std::string_view kAttributesKey = "attributes";
inline constexpr std::string_view kBufferKey = "buffer";
inline constexpr std::string_view kBufferViewKey = "bufferView";
inline constexpr std::string_view kBufferViewsKey = "bufferViews";
inline constexpr std::string_view kBuffersKey = "buffers";
inline constexpr std::string_view kGltfByteLengthKey = "byteLength";
inline constexpr std::string_view kByteOffsetKey = "byteOffset";
inline constexpr std::string_view kByteStrideKey = "byteStride";
inline constexpr std::string_view kByteLengthKey = "byte_length";
inline constexpr std::string_view kBytesKey = "bytes";
inline constexpr std::string_view kCallerReportKey = "caller_report";
inline constexpr std::string_view kChannelsKey = "channels";
inline constexpr std::string_view kCleanupKey = "cleanup";
inline constexpr std::string_view kCommitKey = "commit";
inline constexpr std::string_view kComponentTypeKey = "componentType";
inline constexpr std::string_view kCoordinatesKey = "coordinates";
inline constexpr std::string_view kCornerCornersKey = "corner_corners";
inline constexpr std::string_view kCornerSourcesKey = "corner_sources";
inline constexpr std::string_view kCornerVerticesKey = "corner_vertices";
inline constexpr std::string_view kCornersKey = "corners";
inline constexpr std::string_view kCountKey = "count";
inline constexpr std::string_view kCountsKey = "counts";
inline constexpr std::string_view kEntryPathKey = "entry_path";
inline constexpr std::string_view kFaceOffsetsKey = "face_offsets";
inline constexpr std::string_view kFaceOutputOffsetsKey = "face_output_offsets";
inline constexpr std::string_view kFaceSourcesKey = "face_sources";
inline constexpr std::string_view kFacesKey = "faces";
inline constexpr std::string_view kImagesKey = "images";
inline constexpr std::string_view kIndicesKey = "indices";
inline constexpr std::string_view kKindKey = "kind";
inline constexpr std::string_view kMapsKey = "maps";
inline constexpr std::string_view kMaterialKey = "material";
inline constexpr std::string_view kMaterialsKey = "materials";
inline constexpr std::string_view kMaxKey = "max";
inline constexpr std::string_view kMembersKey = "members";
inline constexpr std::string_view kMeshesKey = "meshes";
inline constexpr std::string_view kMinKey = "min";
inline constexpr std::string_view kModeKey = "mode";
inline constexpr std::string_view kNodesKey = "nodes";
inline constexpr std::string_view kNormalizedKey = "normalized";
inline constexpr std::string_view kOffsetKey = "offset";
inline constexpr std::string_view kOriginalKey = "original";
inline constexpr std::string_view kPathKey = "path";
inline constexpr std::string_view kPayloadMembersKey = "payload_members";
inline constexpr std::string_view kPhaseKey = "phase";
inline constexpr std::string_view kPositionsKey = "positions";
inline constexpr std::string_view kPrimitivesKey = "primitives";
inline constexpr std::string_view kSamplersKey = "samplers";
inline constexpr std::string_view kScalarKey = "scalar";
inline constexpr std::string_view kSceneKey = "scene";
inline constexpr std::string_view kScenesKey = "scenes";
inline constexpr std::string_view kSchemaKey = "schema";
inline constexpr std::string_view kSha256Key = "sha256";
inline constexpr std::string_view kSkinsKey = "skins";
inline constexpr std::string_view kSourceCornerMembersKey =
    "source_corner_members";
inline constexpr std::string_view kSourceCornerOffsetsKey =
    "source_corner_offsets";
inline constexpr std::string_view kSourceFaceMembersKey = "source_face_members";
inline constexpr std::string_view kSourceFaceOffsetsKey = "source_face_offsets";
inline constexpr std::string_view kSourceVertexMembersKey =
    "source_vertex_members";
inline constexpr std::string_view kSourceVertexOffsetsKey =
    "source_vertex_offsets";
inline constexpr std::string_view kSparseKey = "sparse";
inline constexpr std::string_view kTextKey = "text";
inline constexpr std::string_view kTexturesKey = "textures";
inline constexpr std::string_view kTriangleFacesKey = "triangle_faces";
inline constexpr std::string_view kTriangleSourcesKey = "triangle_sources";
inline constexpr std::string_view kTriangulatedKey = "triangulated";
inline constexpr std::string_view kTriangulationKey = "triangulation";
inline constexpr std::string_view kTypeKey = "type";
inline constexpr std::string_view kUriKey = "uri";
inline constexpr std::string_view kUvMappingKey = "uv_mapping";
inline constexpr std::string_view kVersionKey = "version";
inline constexpr std::string_view kVertexSourcesKey = "vertex_sources";
inline constexpr std::string_view kVerticesKey = "vertices";
inline constexpr std::string_view kWireKey = "wire";
inline constexpr std::string_view kUnsupportedArgument =
    "--unsupported-profile";
inline constexpr std::string_view kGlbPath = "surface.glb";
inline constexpr std::string_view kCapsuleBinaryPath = "surface.bin";
inline constexpr std::string_view kCapsuleJsonPath = "surface.json";
inline constexpr std::string_view kReceiptPath = "publication.json";
inline constexpr std::string_view kPublicationSchema =
    "meshvale.surface-publication/1";
inline constexpr std::string_view kBundleSchema = "meshvale.surface-bundle/1";
inline constexpr std::string_view kReportSchema = "meshvale.report/1";
inline constexpr std::string_view kVerifiedPhase = "staged-payload-verified";
inline constexpr std::string_view kCoordinateWire = "declared-meters-rh-y-up";
inline constexpr std::string_view kF64Wire = "f64";
inline constexpr std::string_view kU64Wire = "u64";

inline constexpr std::array<DuplicateTarget, 1> kTargets{{{0, 1}}};
inline void Require(
    bool value, std::string_view message,
    std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
// Failure-only observations use the current public enum values. They do not
// replace assertions or infer the outcome of an uninvoked delegate.
inline void LogUnsupportedFacts(const OffGlbSurfaceWorkflowResult& result,
                                std::string_view context) {
  std::cerr << "OFF to GLB unsupported observation: " << context
            << " status=" << static_cast<int>(result.status)
            << " stage=" << static_cast<int>(result.diagnostic.stage)
            << " code=" << static_cast<int>(result.diagnostic.code)
            << " record=" << result.record.has_value() << '\n';
  if (!result.record) return;
  const auto& facts = result.record->Facts();
  std::cerr << "reached off=" << facts.off.has_value()
            << " triangulation=" << facts.triangulation.has_value()
            << " encoding=" << facts.encoding.has_value()
            << " inspection=" << facts.inspection.has_value()
            << " publication=" << facts.publication.has_value() << '\n';
  if (facts.off)
    std::cerr << "off status=" << static_cast<int>(facts.off->status)
              << " stage=" << static_cast<int>(facts.off->diagnostic.stage)
              << " code=" << static_cast<int>(facts.off->diagnostic.code)
              << " record=" << facts.off->record.has_value() << '\n';
  if (facts.triangulation)
    std::cerr << "triangulation status="
              << static_cast<int>(facts.triangulation->status) << " stage="
              << static_cast<int>(facts.triangulation->diagnostic.stage)
              << " code="
              << static_cast<int>(facts.triangulation->diagnostic.code)
              << " owner=" << facts.triangulation->triangulation.has_value()
              << '\n';
  if (facts.encoding)
    std::cerr << "encoding status=" << static_cast<int>(facts.encoding->status)
              << " stage=" << static_cast<int>(facts.encoding->diagnostic.stage)
              << " code=" << static_cast<int>(facts.encoding->diagnostic.code)
              << " output=" << facts.encoding->output.has_value() << '\n';
  if (facts.inspection)
    std::cerr << "inspection status="
              << static_cast<int>(facts.inspection->status) << " stage="
              << static_cast<int>(facts.inspection->diagnostic.stage)
              << " code=" << static_cast<int>(facts.inspection->diagnostic.code)
              << " owner=" << facts.inspection->inspection.has_value() << '\n';
  if (facts.publication)
    std::cerr << "publication outcome="
              << static_cast<int>(facts.publication->outcome) << " status="
              << static_cast<int>(facts.publication->diagnostic.status)
              << " stage="
              << static_cast<int>(facts.publication->diagnostic.stage)
              << " code="
              << static_cast<int>(facts.publication->diagnostic.code)
              << " bundle=" << facts.publication->bundle.has_value() << '\n';
}
inline std::span<const std::byte> Bytes(std::string_view value) {
  return std::as_bytes(std::span(value));
}
inline std::string_view Text(std::span<const std::byte> value) {
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}
inline constexpr std::string_view kMixed = R"off(OFF
14 4 0
0 0 0
2 0 0
2 2 0
0 2 0
4 0 0
5 0 0
4 1 0
8 0 0
11 0 0
11 3 0
9 1 0
8 3 0
-0.0 0 7
42 43 44
4 0 1 2 3
4 2 3 0 1
3 4 5 6
5 7 8 9 10 11
)off";
inline constexpr std::string_view kMixedShift = R"off(OFF
14 4 0
0 0 0
2 0 0
2 2 0
0 2 0
4 0 0
5.000000000931322574615478515625 0 0
4 1 0
8 0 0
11 0 0
11 3 0
9 1 0
8 3 0
-0.0 0 7
42 43 44
4 0 1 2 3
4 2 3 0 1
3 4 5 6
5 7 8 9 10 11
)off";
inline constexpr std::string_view kReversed = R"off(OFF
14 4 0
0 0 0
2 0 0
2 2 0
0 2 0
4 0 0
5 0 0
4 1 0
8 0 0
11 0 0
11 3 0
9 1 0
8 3 0
-0.0 0 7
42 43 44
4 0 1 2 3
4 0 3 2 1
3 4 5 6
5 7 8 9 10 11
)off";
inline constexpr std::string_view kDifferentIds = R"off(OFF
8 2 0
0 0 0
2 0 0
2 2 0
0 2 0
0 0 0
2 0 0
2 2 0
0 2 0
4 0 1 2 3
4 4 5 6 7
)off";
inline constexpr std::string_view kNewCoincidence = R"off(OFF
6 2 0
1 0 0
2 0 0
1 1 0
1.000000000931322574615478515625 0 0
2 0 1
1 1 1
3 0 1 2
3 3 4 5
)off";
inline constexpr std::string_view kCollapse = R"off(OFF
3 1 0
1 1 0
2 2 0
3 3.000000000931322574615478515625 0
3 0 1 2
)off";
inline constexpr std::string_view kOrientation = R"off(OFF
3 1 0
1 1 0
2 2.0000000894069671630859375 0
3 3.0000001490116119384765625 0
3 0 1 2
)off";
inline constexpr std::string_view kRange = R"off(OFF
3 1 0
340282366920938463463374607431768211456 0 0
0 1 0
0 0 1
3 0 1 2
)off";
inline constexpr std::string_view kUnderflow = R"off(OFF
3 1 0
0 0 0
1 0 0
1 0.000000000000000000000000000000000000000000000700649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015625 0
3 0 1 2
)off";
inline constexpr std::string_view kSubnormal = R"off(OFF
3 1 0
0 0 0
1 0 0
1 0.00000000000000000000000000000000000000000000140129846432481707092372958328991613128026194187651577175706828388979108268586060148663818836212158203125 0
3 0 1 2
)off";
inline constexpr std::string_view kNonplanar = R"off(OFF
14 4 0
0 0 0
2 0 0
2 2 0.000000000931322574615478515625
0 2 0
4 0 0
5 0 0
4 1 0
8 0 0
11 0 0
11 3 0
9 1 0
8 3 0
-0.0 0 7
42 43 44
4 0 1 2 3
4 2 3 0 1
3 4 5 6
5 7 8 9 10 11
)off";
inline constexpr std::string_view kEmpty = R"off(OFF
0 0 0
)off";
inline constexpr std::string_view kPoints = R"off(OFF
2 0 0
-0.0 0 7
42 43 44
)off";

// Finite balanced-v1 request allowances; assertions below are independent
// literals.
inline ic::OffFileReadLimits CaptureLimits() {
  return {{67108864, 16777216, 83886080, 1024, 1024, 4096, 134217728},
          {67108864,
           1000000,
           1000000,
           3000000,
           65536,
           16777216,
           256,
           1048576,
           134217728,
           209715200,
           {1000000, 1000000, 3000000, 1000001, 1, 1, 1, 1, 1, 1, 67108864},
           {1024, 8388608}}};
}
inline ic::OffFileReadWorkPolicy CaptureWork() {
  return {{65536, 262144, 262144, 65536, 64},
          {65536, 256, 256, {262144, 262144, 8192, 64}}};
}
inline OffGlbSurfaceWorkflowLimits Limits() {
  return {
      134217728,
      {134217728,
       128,
       67108864,
       4194304,
       134217728,
       1000000,
       1000000,
       3000000,
       {1000000,
        1,
        1,
        16000000,
        100663296,
        {1024, 8388608},
        {1000000, 1000000, 3000000, 1000001, 1, 1, 1, 1, 1, 1, 67108864}},
       {67108864,
        1000000,
        1000000,
        3000000,
        65536,
        16777216,
        256,
        268435456,
        276824064,
        {1024, 8388608}},
       {67108864,
        1000000,
        1000000,
        3000000,
        65536,
        16777216,
        256,
        1048576,
        134217728,
        209715200,
        {1000000, 1000000, 3000000, 1000001, 1, 1, 1, 1, 1, 1, 67108864},
        {1024, 8388608}},
       {{67108864, 16777216, 83886080, 1024, 1024, 4096, 134217728},
        {1024, 8388608},
        {67108864,
         1000000,
         1000000,
         3000000,
         65536,
         16777216,
         256,
         1048576,
         134217728,
         209715200,
         {1000000, 1000000, 3000000, 1000001, 1, 1, 1, 1, 1, 1, 67108864},
         {1024, 8388608}}}},
      {{1000000, 1000000, 3000000, 1000001, 1, 1, 1, 1, 1, 1, 67108864},
       {1024, 8388608},
       {1000000, 3000000, 9000000, 3000001, 1, 1, 1, 1, 1, 1, 134217728},
       {1024, 8388608},
       15000001,
       268435456,
       134217728},
      {{1000000,   3000000,   9000000,   3000001,   1,    1,         1,
        1,         1,         1,         4096,      1,    9000000,   9000000,
        100000000, 3,         268435456, 536870912, 1,    268435456, 67108864,
        268435456, 67108864,  268435456, 1,         4096, 4000000,   64,
        4096,      100000000, 100000000},
       268435456,
       402653184,
       ic::GltfSurfaceEncodeLimits{{268435456, 8, 134217728}}},
      {{1000000,   3000000,   9000000,   3000001,   1,    1,         1,
        1,         1,         1,         4096,      1,    9000000,   9000000,
        100000000, 3,         268435456, 536870912, 1,    268435456, 67108864,
        268435456, 67108864,  268435456, 1,         4096, 4000000,   64,
        4096,      100000000, 100000000},
       268435456,
       671088640,
       ic::GltfSurfaceInspectLimits{
           {268435456, 8, 134217728},
           {{67108864, 268435456, 268435456, 4000000, 64, 67108864, 4096, 1024,
             16, 1000000, 4000000, 1, 9000000, 134217728},
            134217728},
           {27000000, 9000000, 134217728, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}}},
      {{3, 268435456, 536870912, 33554432, 4194304, 1024, 8192, 4096, 16, 64,
        268435456},
       {{1000000,   3000000,   9000000,   3000001,   1,    1,         1,
         1,         1,         1,         4096,      1,    9000000,   9000000,
         100000000, 3,         268435456, 536870912, 1,    268435456, 67108864,
         268435456, 67108864,  268435456, 1,         4096, 4000000,   64,
         4096,      100000000, 100000000},
        268435456,
        671088640,
        ic::GltfSurfaceInspectLimits{
            {268435456, 8, 134217728},
            {{67108864, 268435456, 268435456, 4000000, 64, 67108864, 4096, 1024,
              16, 1000000, 4000000, 1, 9000000, 134217728},
             134217728},
            {27000000, 9000000, 134217728, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}}},
       939524096}};
}
inline OffGlbSurfaceWorkflowWorkPolicy Work() {
  return {{{262144, 262144, 8192, 64},
           {65536, 256, 256, {262144, 262144, 8192, 64}},
           {65536, 256, 256, {262144, 262144, 8192, 64}},
           {262144, 262144, 65536, 64},
           {262144, 262144, 65536, 64},
           {65536, 65536, 16384, 64},
           {{65536, 262144, 262144, 65536, 64},
            {262144, 262144, 8192, 64},
            {65536, 256, 256, {262144, 262144, 8192, 64}}}},
          {262144, 262144, 8192, 64},
          {{262144, 262144, 8192, 64},
           {262144, 262144},
           {0, 0, 0, 0, {0, 0, 0, 0}},
           {0, 0, 0, {0, 0, 0, 0}}},
          {{262144, 262144, 8192, 64},
           {262144, 262144},
           {262144, 262144, 65536, 65536, {0, 0, 0, 0}},
           {0, 0, 0, {0, 0, 0, 0}}},
          {{65536, 262144, 262144, 65536, 64},
           {{262144, 262144, 8192, 64},
            {262144, 262144},
            {262144, 262144, 65536, 65536, {0, 0, 0, 0}},
            {0, 0, 0, {0, 0, 0, 0}}}}};
}

inline OffGlbSurfaceWorkflowRequest Request(
    const ic::OffFileAsset& asset, const fs::path& destination,
    std::span<const DuplicateTarget> targets = kTargets,
    std::string_view id = "literal-off-glb") {
  return {asset,
          id,
          targets,
          destination,
          ic::OffTextPolicy::kCanonicalAscii,
          ic::OffEdgeCountPolicy::kUnknownZero,
          ic::SurfaceCoordinates::kDeclaredMetersRightHandedYUp,
          {ic::SurfaceNarrowing::kExact32, 0, {}},
          true,
          true,
          true,
          {4096, 1}};
}
class Root {
 public:
  Root() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto parent = fs::canonical(fs::temp_directory_path());
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
      const auto tick =
          std::chrono::steady_clock::now().time_since_epoch().count();
      const auto candidate =
          parent / ("meshvale-off-surface-" + std::to_string(tick) + "-" +
                    std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(candidate, error)) {
        path_ = fs::canonical(candidate);
        return;
      }
    }
    Require(false, "exclusive temporary fixture directory");
  }
  Root(const Root&) = delete;
  Root& operator=(const Root&) = delete;
  ~Root() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
};
inline void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  Require(file.good(), "physical fixture opens");
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  file.close();
  Require(file.good(), "physical fixture writes and closes");
}
inline void Write(const fs::path& path, std::string_view text) {
  Write(path, Bytes(text));
}
inline std::vector<std::byte> Read(const fs::path& path) {
  const auto size = fs::file_size(path);
  Require(size <= 8 * kMiB, "bounded physical artifact");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream file(path, std::ios::binary);
  Require(file.good(), "physical artifact reopens");
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(file.gcount() == static_cast<std::streamsize>(bytes.size()) &&
              file.peek() == std::char_traits<char>::eof(),
          "exact physical extent");
  return bytes;
}
inline ic::OffFileReadResult Capture(const fs::path& path,
                                     const geo::ExecutionContext& context) {
  return ic::ReadOffFileAsset(path, CaptureLimits(), CaptureWork(), context);
}
inline void Released(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "last genuine owner frees shared ledger and joined worker slots");
}
// Generic bounded JSON reader for independent artifact assertions. It does not
// call a producer parser or derive geometry expectations from serialized facts.
struct Json {
  enum class Kind { kNull, kBool, kNumber, kString, kArray, kObject };
  Kind kind = Kind::kNull;
  std::string value;
  std::vector<Json> array;
  std::map<std::string, Json, std::less<>> object;
  const Json& At(std::string_view key) const {
    Require(kind == Kind::kObject, "JSON object required");
    const auto it = object.find(key);
    Require(it != object.end(), "required JSON key");
    return it->second;
  }
  bool Has(std::string_view key) const { return object.contains(key); }
  const Json& At(std::size_t index) const {
    Require(kind == Kind::kArray && index < array.size(), "JSON array index");
    return array[index];
  }
  std::string_view String() const {
    Require(kind == Kind::kString, "JSON string");
    return value;
  }
  std::uint64_t U64() const {
    Require(kind == Kind::kString || kind == Kind::kNumber, "decimal integer");
    Require(!value.empty() && (value == "0" || value[0] != '0'),
            "canonical unsigned integer");
    std::uint64_t result = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), result);
    Require(error == std::errc{} && end == value.data() + value.size(),
            "uint64 extent");
    return result;
  }
};
class JsonReader {
 public:
  explicit JsonReader(std::string_view text) : text_(text) {
    Require(text.size() <= 8 * kMiB, "JSON cap");
  }
  Json Read() {
    auto result = Value(0);
    Space();
    Require(position_ == text_.size(), "closed JSON document");
    return result;
  }

 private:
  void Space() {
    while (position_ < text_.size() &&
           (text_[position_] == ' ' || text_[position_] == '\r' ||
            text_[position_] == '\n' || text_[position_] == '\t'))
      ++position_;
  }
  char Take() {
    Require(position_ < text_.size(), "JSON token extent");
    return text_[position_++];
  }
  bool Consume(char c) {
    Space();
    if (position_ < text_.size() && text_[position_] == c) {
      ++position_;
      return true;
    }
    return false;
  }
  unsigned Hex4() {
    unsigned result = 0;
    for (unsigned i = 0; i < 4; ++i) {
      const char c = Take();
      unsigned digit = 0;
      if (c >= '0' && c <= '9')
        digit = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f')
        digit = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        digit = static_cast<unsigned>(c - 'A' + 10);
      else {
        Require(false, "JSON hex escape");
      }
      result = result * 16 + digit;
    }
    return result;
  }
  std::string String() {
    Require(Take() == '"', "JSON string opener");
    std::string result;
    for (;;) {
      const char c = Take();
      if (c == '"') return result;
      Require(static_cast<unsigned char>(c) >= 32, "JSON unescaped control");
      if (c != '\\') {
        result.push_back(c);
        continue;
      }
      const char escape = Take();
      switch (escape) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escape);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u': {
          auto point = Hex4();
          if (point >= 0xd800 && point <= 0xdbff) {
            Require(Take() == '\\' && Take() == 'u', "JSON surrogate pair");
            const auto low = Hex4();
            Require(low >= 0xdc00 && low <= 0xdfff, "low surrogate");
            point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
          } else
            Require(point < 0xdc00 || point > 0xdfff, "no isolated surrogate");
          if (point <= 0x7f)
            result.push_back(static_cast<char>(point));
          else if (point <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (point >> 6)));
            result.push_back(static_cast<char>(0x80 | (point & 63)));
          } else if (point <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (point >> 12)));
            result.push_back(static_cast<char>(0x80 | ((point >> 6) & 63)));
            result.push_back(static_cast<char>(0x80 | (point & 63)));
          } else {
            result.push_back(static_cast<char>(0xf0 | (point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((point >> 12) & 63)));
            result.push_back(static_cast<char>(0x80 | ((point >> 6) & 63)));
            result.push_back(static_cast<char>(0x80 | (point & 63)));
          }
          break;
        }
        default:
          Require(false, "known JSON escape");
      }
    }
  }
  Json Value(unsigned depth) {
    constexpr unsigned kMaxJsonDepth = 64;
    constexpr std::size_t kMaxJsonNodes = 65536;
    Require(depth <= kMaxJsonDepth && ++nodes_ <= kMaxJsonNodes,
            "bounded JSON nodes/depth");
    Space();
    Require(position_ < text_.size(), "JSON value");
    Json result;
    if (text_[position_] == '"') {
      result.kind = Json::Kind::kString;
      result.value = String();
      return result;
    }
    if (Consume('{')) {
      result.kind = Json::Kind::kObject;
      if (!Consume('}')) do {
          Space();
          const auto key = String();
          Require(Consume(':'), "JSON key separator");
          Require(result.object.emplace(key, Value(depth + 1)).second,
                  "unique JSON key");
          if (Consume('}')) return result;
        } while (Consume(','));
      else
        return result;
      Require(false, "JSON object separator");
    }
    if (Consume('[')) {
      result.kind = Json::Kind::kArray;
      if (!Consume(']')) do {
          result.array.push_back(Value(depth + 1));
          if (Consume(']')) return result;
        } while (Consume(','));
      else
        return result;
      Require(false, "JSON array separator");
    }
    for (const auto literal :
         {std::string_view("null"), std::string_view("true"),
          std::string_view("false")}) {
      if (text_.substr(position_).starts_with(literal)) {
        position_ += literal.size();
        result.value = literal;
        result.kind = literal == "null" ? Json::Kind::kNull : Json::Kind::kBool;
        return result;
      }
    }
    const auto begin = position_;
    if (text_[position_] == '-') ++position_;
    auto digits = [&] {
      const auto before = position_;
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9')
        ++position_;
      Require(position_ > before, "JSON decimal digits");
    };
    if (position_ < text_.size() && text_[position_] == '0')
      ++position_;
    else
      digits();
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      digits();
    }
    if (position_ < text_.size() &&
        (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() &&
          (text_[position_] == '+' || text_[position_] == '-'))
        ++position_;
      digits();
    }
    result.kind = Json::Kind::kNumber;
    result.value = text_.substr(begin, position_ - begin);
    return result;
  }
  std::string_view text_;
  std::size_t position_ = 0;
  std::size_t nodes_ = 0;
};
inline Json Parse(std::span<const std::byte> bytes) {
  return JsonReader(Text(bytes)).Read();
}
inline std::uint64_t Little(std::span<const std::byte> bytes,
                            std::size_t offset, std::size_t width) {
  Require(width <= sizeof(std::uint64_t) && offset <= bytes.size() &&
              width <= bytes.size() - offset,
          "little-endian bounded range");
  std::uint64_t result = 0;
  for (std::size_t i = 0; i < width; ++i)
    result |= std::uint64_t(std::to_integer<unsigned char>(bytes[offset + i]))
              << (8 * i);
  return result;
}
// FIPS 180-4 SHA-256, independent of the product's digest implementation.
inline std::string DigestHex(std::span<const std::byte> bytes) {
  constexpr std::string_view kHexDigits = "0123456789abcdef";
  std::string result;
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned char>(byte);
    result.push_back(kHexDigits[value >> 4]);
    result.push_back(kHexDigits[value & 15]);
  }
  return result;
}
inline std::string Sha(std::span<const std::byte> bytes) {
  constexpr std::array<std::uint32_t, 64> kRounds{
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                     0xa54ff53a, 0x510e527f, 0x9b05688c,
                                     0x1f83d9ab, 0x5be0cd19};
  std::vector<std::byte> padded(bytes.begin(), bytes.end());
  padded.push_back(std::byte{0x80});
  while ((padded.size() % 64) != 56) padded.push_back(std::byte{0});
  const auto bits = static_cast<std::uint64_t>(bytes.size()) * 8;
  for (int i = 7; i >= 0; --i)
    padded.push_back(std::byte((bits >> (8 * i)) & 255));
  for (std::size_t start = 0; start < padded.size(); start += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i)
      for (std::size_t j = 0; j < 4; ++j)
        words[i] = (words[i] << 8) |
                   std::to_integer<unsigned char>(padded[start + 4 * i + j]);
    for (std::size_t i = 16; i < words.size(); ++i) {
      const auto first = std::rotr(words[i - 15], 7) ^
                         std::rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
      const auto second = std::rotr(words[i - 2], 17) ^
                          std::rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
      words[i] = words[i - 16] + first + words[i - 7] + second;
    }
    auto [a, b, c, d, e, f, g, h] = state;
    for (std::size_t i = 0; i < words.size(); ++i) {
      const auto t1 = h +
                      (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                      ((e & f) ^ (~e & g)) + kRounds[i] + words[i];
      const auto t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    const std::array values{a, b, c, d, e, f, g, h};
    for (std::size_t i = 0; i < state.size(); ++i) state[i] += values[i];
  }
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string result;
  for (const auto word : state)
    for (int i = 7; i >= 0; --i) result.push_back(kHex[(word >> (i * 4)) & 15]);
  return result;
}
inline void CheckReader() {
  Require(Sha(Bytes("")) ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852"
                  "b855" &&
              Sha(Bytes("abc")) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f200"
                  "15ad",
          "independent SHA standard vectors");
  Require(
      Sha(Bytes(kMixed)) ==
          "b3e469058538ff698aef44b544dad0ac01903ca9102020334f8b761f7a18c889",
      "literal OFF digest");
}

struct LiteralMesh {
  std::span<const double> positions;
  std::span<const geo::index_t> offsets, corners;
};
inline constexpr std::array<double, 42> kMixedPositions{
    0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 2.0, 2.0, 0.0,  0.0, 2.0,  0.0,  4.0,  0.0,
    0.0, 5.0, 0.0, 0.0, 4.0, 1.0, 0.0, 8.0, 0.0,  0.0, 11.0, 0.0,  0.0,  11.0,
    3.0, 0.0, 9.0, 1.0, 0.0, 8.0, 3.0, 0.0, -0.0, 0.0, 7.0,  42.0, 43.0, 44.0};
inline constexpr std::array<geo::index_t, 5> kMixedOffsets{0, 4, 8, 11, 16};
inline constexpr std::array<geo::index_t, 16> kMixedCorners{
    0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::array<double, 42> kMixedShiftPositions{
    0.0,  0.0, 0.0, 2.0,  0.0,  0.0, 2.0, 2.0,
    0.0,  0.0, 2.0, 0.0,  4.0,  0.0, 0.0, 5.000000000931322574615478515625,
    0.0,  0.0, 4.0, 1.0,  0.0,  8.0, 0.0, 0.0,
    11.0, 0.0, 0.0, 11.0, 3.0,  0.0, 9.0, 1.0,
    0.0,  8.0, 3.0, 0.0,  -0.0, 0.0, 7.0, 42.0,
    43.0, 44.0};
inline constexpr std::array<geo::index_t, 5> kMixedShiftOffsets{0, 4, 8, 11,
                                                                16};
inline constexpr std::array<geo::index_t, 16> kMixedShiftCorners{
    0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::array<double, 42> kReversedPositions{
    0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 2.0, 2.0, 0.0,  0.0, 2.0,  0.0,  4.0,  0.0,
    0.0, 5.0, 0.0, 0.0, 4.0, 1.0, 0.0, 8.0, 0.0,  0.0, 11.0, 0.0,  0.0,  11.0,
    3.0, 0.0, 9.0, 1.0, 0.0, 8.0, 3.0, 0.0, -0.0, 0.0, 7.0,  42.0, 43.0, 44.0};
inline constexpr std::array<geo::index_t, 5> kReversedOffsets{0, 4, 8, 11, 16};
inline constexpr std::array<geo::index_t, 16> kReversedCorners{
    0, 1, 2, 3, 0, 3, 2, 1, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::array<double, 24> kDifferentIdsPositions{
    0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 2.0, 0.0,
    0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 2.0, 0.0};
inline constexpr std::array<geo::index_t, 3> kDifferentIdsOffsets{0, 4, 8};
inline constexpr std::array<geo::index_t, 8> kDifferentIdsCorners{0, 1, 2, 3,
                                                                  4, 5, 6, 7};
inline constexpr std::array<double, 18> kNewCoincidencePositions{
    1.0, 0.0, 0.0, 2.0, 0.0,
    0.0, 1.0, 1.0, 0.0, 1.000000000931322574615478515625,
    0.0, 0.0, 2.0, 0.0, 1.0,
    1.0, 1.0, 1.0};
inline constexpr std::array<geo::index_t, 3> kNewCoincidenceOffsets{0, 3, 6};
inline constexpr std::array<geo::index_t, 6> kNewCoincidenceCorners{0, 1, 2,
                                                                    3, 4, 5};
inline constexpr std::array<double, 9> kCollapsePositions{
    1.0, 1.0, 0.0, 2.0, 2.0, 0.0, 3.0, 3.000000000931322574615478515625, 0.0};
inline constexpr std::array<geo::index_t, 2> kCollapseOffsets{0, 3};
inline constexpr std::array<geo::index_t, 3> kCollapseCorners{0, 1, 2};
inline constexpr std::array<double, 9> kOrientationPositions{
    1.0,
    1.0,
    0.0,
    2.0,
    2.0000000894069671630859375,
    0.0,
    3.0,
    3.0000001490116119384765625,
    0.0};
inline constexpr std::array<geo::index_t, 2> kOrientationOffsets{0, 3};
inline constexpr std::array<geo::index_t, 3> kOrientationCorners{0, 1, 2};
inline constexpr std::array<double, 9> kRangePositions{
    340282366920938463463374607431768211456.0,
    0.0,
    0.0,
    0.0,
    1.0,
    0.0,
    0.0,
    0.0,
    1.0};
inline constexpr std::array<geo::index_t, 2> kRangeOffsets{0, 3};
inline constexpr std::array<geo::index_t, 3> kRangeCorners{0, 1, 2};
inline constexpr std::array<double, 9> kUnderflowPositions{
    0.0,
    0.0,
    0.0,
    1.0,
    0.0,
    0.0,
    1.0,
    0.000000000000000000000000000000000000000000000700649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015625,
    0.0};
inline constexpr std::array<geo::index_t, 2> kUnderflowOffsets{0, 3};
inline constexpr std::array<geo::index_t, 3> kUnderflowCorners{0, 1, 2};
inline constexpr std::array<double, 9> kSubnormalPositions{
    0.0,
    0.0,
    0.0,
    1.0,
    0.0,
    0.0,
    1.0,
    0.00000000000000000000000000000000000000000000140129846432481707092372958328991613128026194187651577175706828388979108268586060148663818836212158203125,
    0.0};
inline constexpr std::array<geo::index_t, 2> kSubnormalOffsets{0, 3};
inline constexpr std::array<geo::index_t, 3> kSubnormalCorners{0, 1, 2};
inline constexpr std::array<double, 42> kNonplanarPositions{
    0.0,  0.0,  0.0,
    2.0,  0.0,  0.0,
    2.0,  2.0,  0.000000000931322574615478515625,
    0.0,  2.0,  0.0,
    4.0,  0.0,  0.0,
    5.0,  0.0,  0.0,
    4.0,  1.0,  0.0,
    8.0,  0.0,  0.0,
    11.0, 0.0,  0.0,
    11.0, 3.0,  0.0,
    9.0,  1.0,  0.0,
    8.0,  3.0,  0.0,
    -0.0, 0.0,  7.0,
    42.0, 43.0, 44.0};
inline constexpr std::array<geo::index_t, 5> kNonplanarOffsets{0, 4, 8, 11, 16};
inline constexpr std::array<geo::index_t, 16> kNonplanarCorners{
    0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::array<double, 0> kEmptyPositions{};
inline constexpr std::array<geo::index_t, 1> kEmptyOffsets{0};
inline constexpr std::array<geo::index_t, 0> kEmptyCorners{};
inline constexpr std::array<double, 6> kPointsPositions{-0.0, 0.0,  7.0,
                                                        42.0, 43.0, 44.0};
inline constexpr std::array<geo::index_t, 1> kPointsOffsets{0};
inline constexpr std::array<geo::index_t, 0> kPointsCorners{};
inline LiteralMesh Literal(std::string_view source) {
  if (source == kMixed) return {kMixedPositions, kMixedOffsets, kMixedCorners};
  if (source == kMixedShift)
    return {kMixedShiftPositions, kMixedShiftOffsets, kMixedShiftCorners};
  if (source == kReversed)
    return {kReversedPositions, kReversedOffsets, kReversedCorners};
  if (source == kDifferentIds)
    return {kDifferentIdsPositions, kDifferentIdsOffsets, kDifferentIdsCorners};
  if (source == kNewCoincidence)
    return {kNewCoincidencePositions, kNewCoincidenceOffsets,
            kNewCoincidenceCorners};
  if (source == kCollapse)
    return {kCollapsePositions, kCollapseOffsets, kCollapseCorners};
  if (source == kOrientation)
    return {kOrientationPositions, kOrientationOffsets, kOrientationCorners};
  if (source == kRange) return {kRangePositions, kRangeOffsets, kRangeCorners};
  if (source == kUnderflow)
    return {kUnderflowPositions, kUnderflowOffsets, kUnderflowCorners};
  if (source == kSubnormal)
    return {kSubnormalPositions, kSubnormalOffsets, kSubnormalCorners};
  if (source == kNonplanar)
    return {kNonplanarPositions, kNonplanarOffsets, kNonplanarCorners};
  if (source == kEmpty) return {kEmptyPositions, kEmptyOffsets, kEmptyCorners};
  if (source == kPoints)
    return {kPointsPositions, kPointsOffsets, kPointsCorners};
  throw std::runtime_error("known literal source required");
}
inline constexpr std::array<geo::index_t, 4> kRepairOffsets{0, 4, 7, 12};
inline constexpr std::array<geo::index_t, 12> kRepairCorners{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::array<geo::index_t, 4> kFaceMap{0, 0, 1, 2};
inline constexpr std::array<geo::index_t, 16> kCornerMap{
    0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::array<geo::index_t, 3> kSourceFaces{0, 2, 3};
inline constexpr std::array<geo::index_t, 12> kSourceCorners{
    0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15};
inline void CheckMesh(const geo::MaterializedMesh& actual,
                      LiteralMesh expected) {
  const auto view = actual.View();
  Require(view.attributes.empty(), "OFF attributes stay absent");
  Require(std::ranges::equal(view.face_offsets, expected.offsets) &&
              std::ranges::equal(view.corner_vertices, expected.corners) &&
              view.positions.size() == expected.positions.size(),
          "literal complete polygon storage");
  for (std::size_t i = 0; i < view.positions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) ==
                std::bit_cast<std::uint64_t>(expected.positions[i]),
            "every F64 position bit including unused signed zero");
}
inline void CheckOff(const OffGlbSurfaceWorkflowRecord& record,
                     std::string_view source, bool changed) {
  const auto& facts = record.Facts();
  Require(facts.off && facts.off->record,
          "genuine OFF call and record retained");
  const auto& off = *facts.off;
  const auto& retained = *off.record;
  Require(off.status == OffWorkflowStatus::kSuccess && retained.RepairFacts() &&
              retained.Output() && retained.Reload() &&
              off.outcomes.profile == OffWorkflowProfileOutcome::kPassed &&
              off.outcomes.publication ==
                  OffWorkflowPublicationOutcome::kNotRequested &&
              !off.publication && !retained.Published(),
          "actual OFF memory verification succeeded with publication not "
          "requested");
  Require(std::ranges::equal(record.Source().EntryBytes(), Bytes(source)) &&
              std::ranges::equal(retained.Source().EntryBytes(), Bytes(source)),
          "original captured bytes retained verbatim");
  const auto& repair = *retained.RepairFacts();
  const auto literal = Literal(source);
  CheckMesh(repair.Source(), literal);
  CheckMesh(repair.Candidate(),
            changed
                ? LiteralMesh{literal.positions, kRepairOffsets, kRepairCorners}
                : literal);
  Require(repair.Outcome() == (changed ? CandidateOutcome::accepted
                                       : CandidateOutcome::unchanged),
          "actual Repair outcome");
  if (changed) {
    Require(std::ranges::equal(repair.FaceMap(), kFaceMap) &&
                std::ranges::equal(repair.CornerMap(), kCornerMap) &&
                std::ranges::equal(repair.SourceFaces(), kSourceFaces) &&
                std::ranges::equal(repair.SourceCorners(), kSourceCorners),
            "four independent literal O-to-R forward and inverse maps");
  } else {
    Require(repair.FaceMap().size() + 1 == literal.offsets.size() &&
                repair.SourceFaces().size() == repair.FaceMap().size() &&
                repair.CornerMap().size() == literal.corners.size() &&
                repair.SourceCorners().size() == literal.corners.size(),
            "complete unchanged map extents");
    for (const auto map : {repair.FaceMap(), repair.CornerMap(),
                           repair.SourceFaces(), repair.SourceCorners()})
      for (std::size_t i = 0; i < map.size(); ++i)
        Require(map[i] == i, "unchanged explicit identity correspondence");
  }
  Require(retained.InputSha256() == Sha(Bytes(source)) &&
              !retained.ReportJson().empty(),
          "genuine OFF report and independent source hash");
  const auto report = Parse(retained.ReportJson());
  Require(report.At(kSchemaKey).String() == kReportSchema,
          "unchanged OFF report core schema");
}
inline void CheckCsr(std::span<const std::uint64_t> forward,
                     std::size_t source_rows,
                     std::span<const std::uint64_t> offsets,
                     std::span<const std::uint64_t> members) {
  Require(offsets.size() == source_rows + 1 && offsets.front() == 0 &&
              offsets.back() == forward.size() &&
              members.size() == forward.size(),
          "complete inverse CSR extents including unused rows");
  std::vector<bool> seen(forward.size());
  for (std::size_t row = 0; row < source_rows; ++row) {
    Require(
        offsets[row] <= offsets[row + 1] && offsets[row + 1] <= members.size(),
        "monotonic bounded CSR");
    for (auto i = offsets[row]; i < offsets[row + 1]; ++i) {
      const auto member = members[static_cast<std::size_t>(i)];
      Require(member < forward.size() &&
                  !seen[static_cast<std::size_t>(member)] &&
                  forward[static_cast<std::size_t>(member)] == row,
              "exact once inverse relation by authored identity");
      seen[static_cast<std::size_t>(member)] = true;
    }
  }
  Require(std::ranges::all_of(seen, [](bool value) { return value; }),
          "inverse covers every wire member");
}
struct Point {
  double x, y;
};
inline double Cross(Point a, Point b, Point c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
inline bool StrictInside(Point p, const std::array<Point, 3>& t) {
  return Cross(t[0], t[1], p) > 0 && Cross(t[1], t[2], p) > 0 &&
         Cross(t[2], t[0], p) > 0;
}
inline bool ProperCross(Point a, Point b, Point c, Point d) {
  const auto x = Cross(a, b, c), y = Cross(a, b, d), z = Cross(c, d, a),
             w = Cross(c, d, b);
  return ((x < 0 && y > 0) || (x > 0 && y < 0)) &&
         ((z < 0 && w > 0) || (z > 0 && w < 0));
}
inline void CheckTriangles(const geo::MaterializedMesh& original,
                           const geo::ChargedTriangulation& triangles) {
  const auto r = original.View(), t = triangles.Mesh().View();
  Require(t.positions.size() == r.positions.size() && t.attributes.empty(),
          "triangulation retains all position rows and absent channels");
  for (std::size_t i = 0; i < r.positions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(r.positions[i]) ==
                std::bit_cast<std::uint64_t>(t.positions[i]),
            "T retains complete R full precision bits");
  Require(triangles.FaceSources().size() + 1 == t.face_offsets.size() &&
              triangles.CornerSources().size() == t.corner_vertices.size() &&
              triangles.FaceOutputOffsets().size() == r.face_offsets.size(),
          "R-to-T map extents");
  Require(triangles.FaceOutputOffsets().front() == 0 &&
              triangles.FaceOutputOffsets().back() ==
                  triangles.FaceSources().size(),
          "R face offsets cover T");
  for (std::size_t face = 0; face + 1 < r.face_offsets.size(); ++face) {
    const auto first = r.face_offsets[face], last = r.face_offsets[face + 1];
    const auto begin = triangles.FaceOutputOffsets()[face],
               end = triangles.FaceOutputOffsets()[face + 1];
    Require(end - begin == last - first - 2,
            "each authored n-gon has n-2 triangles");
    auto point = [&](geo::index_t corner) {
      const auto vertex = r.corner_vertices[static_cast<std::size_t>(corner)];
      return Point{r.positions[static_cast<std::size_t>(3 * vertex)],
                   r.positions[static_cast<std::size_t>(3 * vertex + 1)]};
    };
    double polygon_area = 0;
    std::map<std::pair<geo::index_t, geo::index_t>, int> boundary;
    for (auto c = first; c < last; ++c) {
      const auto next = c + 1 == last ? first : c + 1;
      const auto a = point(c), b = point(next);
      polygon_area += a.x * b.y - a.y * b.x;
      boundary[{c, next}] += 1;
    }
    // This exact XY oracle is deliberately restricted to these planar dyadic
    // literals.
    Require(polygon_area > 0,
            "literal polygons have positive directed XY winding");
    std::map<std::pair<geo::index_t, geo::index_t>, int> edges;
    std::vector<std::array<Point, 3>> shapes;
    double triangle_area = 0;
    for (auto triangle = begin; triangle < end; ++triangle) {
      Require(
          triangles.FaceSources()[static_cast<std::size_t>(triangle)] == face,
          "T face refers to R face");
      Require(
          t.face_offsets[static_cast<std::size_t>(triangle)] == 3 * triangle &&
              t.face_offsets[static_cast<std::size_t>(triangle + 1)] ==
                  3 * (triangle + 1),
          "triangular storage");
      std::array<geo::index_t, 3> authored{};
      std::array<Point, 3> shape{};
      for (std::size_t c = 0; c < 3; ++c) {
        const auto tc = static_cast<std::size_t>(3 * triangle) + c;
        authored[c] = triangles.CornerSources()[tc];
        Require(
            authored[c] >= first && authored[c] < last &&
                t.corner_vertices[tc] ==
                    r.corner_vertices[static_cast<std::size_t>(authored[c])],
            "T corner maps to actual authored R corner and vertex ID");
        shape[c] = point(authored[c]);
      }
      const auto area = Cross(shape[0], shape[1], shape[2]);
      Require(area > 0, "nondegenerate same winding triangle");
      triangle_area += area;
      for (std::size_t c = 0; c < 3; ++c)
        ++edges[{authored[c], authored[(c + 1) % 3]}];
      const Point sum{shape[0].x + shape[1].x + shape[2].x,
                      shape[0].y + shape[1].y + shape[2].y};
      bool inside = false;
      for (auto c = first; c < last; ++c) {
        const auto a = point(c), b = point(c + 1 == last ? first : c + 1);
        const Point aa{3 * a.x, 3 * a.y}, bb{3 * b.x, 3 * b.y};
        if ((aa.y > sum.y) != (bb.y > sum.y)) {
          const auto cross = Cross(aa, bb, sum);
          if ((bb.y > aa.y && cross > 0) || (bb.y < aa.y && cross < 0))
            inside = !inside;
        }
      }
      Require(inside, "exact scaled centroid contained in authored polygon");
      shapes.push_back(shape);
    }
    Require(triangle_area == polygon_area,
            "exact dyadic directed area sum without pinned ear order");
    for (const auto& [edge, count] : edges) {
      const auto reverse = edges.find({edge.second, edge.first});
      const auto opposite = reverse == edges.end() ? 0 : reverse->second;
      const auto wanted = boundary.find(edge);
      const auto expected = wanted == boundary.end() ? 0 : wanted->second;
      Require(count - opposite == expected,
              "internal directed edges cancel; each authored boundary appears "
              "once");
    }
    for (const auto& [edge, count] : boundary)
      Require(edges[edge] - edges[{edge.second, edge.first}] == count,
              "no authored boundary disappears");
    for (std::size_t a = 0; a < shapes.size(); ++a)
      for (std::size_t b = a + 1; b < shapes.size(); ++b) {
        for (const auto p : shapes[a])
          Require(!StrictInside(p, shapes[b]), "disjoint triangle interiors");
        for (const auto p : shapes[b])
          Require(!StrictInside(p, shapes[a]),
                  "disjoint triangle interiors reciprocal");
        for (std::size_t x = 0; x < 3; ++x)
          for (std::size_t y = 0; y < 3; ++y)
            Require(!ProperCross(shapes[a][x], shapes[a][(x + 1) % 3],
                                 shapes[b][y], shapes[b][(y + 1) % 3]),
                    "no crossing triangulation diagonals");
      }
  }
}
inline void CheckWire(const ic::SurfaceOutput& output) {
  const auto& triangles = output.Triangles();
  const auto r = output.Original().Source().View();
  const auto corners = triangles.CornerSources();
  const auto faces = triangles.FaceSources();
  Require(output.Encoding() == ic::SurfaceEncoding::kGlb &&
              output.Representation() ==
                  ic::SurfaceRepresentation::kAnnotatedBundleV1 &&
              output.UvMapping().empty() && output.MaterialFactors().empty(),
          "one explicit attribute-free annotated GLB route");
  Require(output.WireTriangleFaces().size() == faces.size() &&
              output.WireTriangleSources().size() == faces.size() &&
              output.WireCornerCorners().size() == corners.size() &&
              output.WireCornerSources().size() == corners.size() &&
              output.WireVertexSources().size() == corners.size(),
          "one wire row for each T corner without welding");
  for (std::size_t i = 0; i < faces.size(); ++i)
    Require(output.WireTriangleSources()[i] == i &&
                output.WireTriangleFaces()[i] == faces[i],
            "wire triangle Source means T; Face means R");
  for (std::size_t i = 0; i < corners.size(); ++i)
    Require(output.WireCornerSources()[i] == i &&
                output.WireCornerCorners()[i] == corners[i] &&
                output.WireVertexSources()[i] ==
                    r.corner_vertices[static_cast<std::size_t>(corners[i])],
            "wire corner Source means T; Corner means R; vertex means authored "
            "R ID");
  CheckCsr(output.WireVertexSources(), r.positions.size() / 3,
           output.SourceVertexOffsets(), output.SourceVertexWireMembers());
  CheckCsr(output.WireTriangleFaces(), r.face_offsets.size() - 1,
           output.SourceFaceOffsets(), output.SourceFaceWireTriangles());
  CheckCsr(output.WireCornerCorners(), r.corner_vertices.size(),
           output.SourceCornerOffsets(), output.SourceCornerWireCorners());
}
inline std::vector<std::uint64_t> Slice(const Json& descriptor,
                                        std::span<const std::byte> binary,
                                        std::string_view scalar) {
  Require(descriptor.At(kScalarKey).String() == scalar,
          "capsule scalar representation");
  const auto count = descriptor.At(kCountKey).U64(),
             offset = descriptor.At(kOffsetKey).U64(),
             size = descriptor.At(kBytesKey).U64();
  constexpr std::size_t kCapsuleScalarBytes = 8;
  Require(count <= binary.size() / kCapsuleScalarBytes &&
              size == count * kCapsuleScalarBytes && offset <= binary.size() &&
              size <= binary.size() - offset,
          "capsule checked physical scalar range");
  std::vector<std::uint64_t> values;
  values.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t i = 0; i < count; ++i)
    values.push_back(Little(
        binary, static_cast<std::size_t>(offset + i * kCapsuleScalarBytes),
        kCapsuleScalarBytes));
  return values;
}
inline void CapsuleMesh(const Json& object, std::span<const std::byte> binary,
                        const geo::MaterializedMesh& mesh) {
  const auto view = mesh.View();
  const auto& counts = object.At(kCountsKey);
  Require(counts.At(kVerticesKey).U64() == view.positions.size() / 3 &&
              counts.At(kFacesKey).U64() + 1 == view.face_offsets.size() &&
              counts.At(kCornersKey).U64() == view.corner_vertices.size() &&
              counts.At(kAttributesKey).U64() == 0 &&
              object.At(kAttributesKey).array.empty(),
          "physical capsule complete R/T counts and absent channels");
  const auto positions = Slice(object.At(kPositionsKey), binary, kF64Wire);
  Require(positions.size() == view.positions.size(),
          "capsule retains every used/unused coordinate");
  for (std::size_t i = 0; i < positions.size(); ++i)
    Require(positions[i] == std::bit_cast<std::uint64_t>(view.positions[i]),
            "physical capsule full precision and signed-zero words");
  Require(
      std::ranges::equal(Slice(object.At(kFaceOffsetsKey), binary, kU64Wire),
                         view.face_offsets) &&
          std::ranges::equal(
              Slice(object.At(kCornerVerticesKey), binary, kU64Wire),
              view.corner_vertices),
      "physical capsule authored polygon/triangle storage");
}
inline void PhysicalGlb(std::span<const std::byte> bytes,
                        const ic::SurfaceOutput& expected) {
  constexpr std::uint32_t kGlbMagic = 0x46546c67, kJsonChunk = 0x4e4f534a,
                          kBinChunk = 0x004e4942;
  constexpr std::uint32_t kF32Component = 5126, kU32Component = 5125,
                          kTriangleMode = 4;
  constexpr std::string_view kGltfVersion = "2.0", kPositionShape = "VEC3",
                             kIndexShape = "SCALAR";
  Require(bytes.size() >= 20 && Little(bytes, 0, 4) == kGlbMagic &&
              Little(bytes, 4, 4) == 2 && Little(bytes, 8, 4) == bytes.size(),
          "actual GLB magic/version/total extent");
  const auto json_size = static_cast<std::size_t>(Little(bytes, 12, 4));
  Require(json_size % 4 == 0 && Little(bytes, 16, 4) == kJsonChunk &&
              json_size <= bytes.size() - 20,
          "bounded first JSON chunk");
  const auto document = Parse(bytes.subspan(20, json_size));
  Require(document.At(kAssetKey).At(kVersionKey).String() == kGltfVersion &&
              document.At(kSceneKey).U64() == 0 &&
              document.At(kScenesKey).array.size() == 1,
          "ordinary default static GLB scene");
  for (const auto key : {kMaterialsKey, kImagesKey, kTexturesKey, kSamplersKey,
                         kSkinsKey, kAnimationsKey})
    Require(!document.Has(key),
            "attribute-free OFF invents no authored scene semantics");
  const auto wire_rows = expected.WireVertexSources().size();
  if (wire_rows == 0) {
    Require(20 + json_size == bytes.size() &&
                document.At(kScenesKey).At(std::size_t{0}).object.empty(),
            "genuine empty JSON-only GLB scene");
    for (const auto key :
         {kNodesKey, kMeshesKey, kAccessorsKey, kBufferViewsKey, kBuffersKey})
      Require(!document.Has(key),
              "empty GLB fabricates no geometry or BIN buffer");
    return;
  }
  const auto bin_header = 20 + json_size;
  Require(bin_header + 8 <= bytes.size() &&
              Little(bytes, bin_header + 4, 4) == kBinChunk,
          "actual second BIN chunk");
  const auto bin_size = static_cast<std::size_t>(Little(bytes, bin_header, 4));
  Require(bin_size % 4 == 0 && bin_size == bytes.size() - bin_header - 8,
          "single bounded BIN chunk and no opaque extra chunks");
  const auto binary = bytes.subspan(bin_header + 8, bin_size);
  Require(document.At(kBuffersKey).array.size() == 1 &&
              !document.At(kBuffersKey).At(std::size_t{0}).Has(kUriKey) &&
              document.At(kBuffersKey)
                      .At(std::size_t{0})
                      .At(kGltfByteLengthKey)
                      .U64() <= binary.size(),
          "single embedded geometry buffer");
  const auto& meshes = document.At(kMeshesKey);
  Require(meshes.array.size() == 1 &&
              meshes.At(std::size_t{0}).At(kPrimitivesKey).array.size() == 1,
          "one nonempty no-material primitive");
  const auto& primitive =
      meshes.At(std::size_t{0}).At(kPrimitivesKey).At(std::size_t{0});
  Require(!primitive.Has(kMaterialKey) &&
              (!primitive.Has(kModeKey) ||
               primitive.At(kModeKey).U64() == kTriangleMode) &&
              primitive.At(kAttributesKey).object.size() == 1 &&
              primitive.At(kAttributesKey).Has(kPositionAttributeKey),
          "POSITION-only native triangular projection");
  auto accessor = [&](std::uint64_t index, std::uint32_t component,
                      std::string_view type, std::size_t width) {
    const auto& a =
        document.At(kAccessorsKey).At(static_cast<std::size_t>(index));
    Require(
        a.At(kComponentTypeKey).U64() == component &&
            a.At(kTypeKey).String() == type &&
            a.At(kCountKey).U64() == wire_rows && !a.Has(kSparseKey) &&
            (!a.Has(kNormalizedKey) || a.At(kNormalizedKey).value == "false"),
        "exact accessor component/type/count");
    const auto& v =
        document.At(kBufferViewsKey)
            .At(static_cast<std::size_t>(a.At(kBufferViewKey).U64()));
    const auto base = v.Has(kByteOffsetKey) ? v.At(kByteOffsetKey).U64() : 0;
    const auto offset = a.Has(kByteOffsetKey) ? a.At(kByteOffsetKey).U64() : 0;
    const auto length = v.At(kGltfByteLengthKey).U64();
    Require(v.At(kBufferKey).U64() == 0 && !v.Has(kByteStrideKey) &&
                base <= binary.size() && length <= binary.size() - base &&
                offset <= length && wire_rows <= (length - offset) / width,
            "bounded packed accessor extent");
    return std::pair{static_cast<std::size_t>(base + offset), a};
  };
  const auto [position_offset, positions] =
      accessor(primitive.At(kAttributesKey).At(kPositionAttributeKey).U64(),
               kF32Component, kPositionShape, 12);
  const auto [index_offset, indices] =
      accessor(primitive.At(kIndicesKey).U64(), kU32Component, kIndexShape, 4);
  static_cast<void>(indices);
  const auto r = expected.Original().Source().View();
  std::array<double, 3> minimum{std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::infinity()},
      maximum{-std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity()};
  for (std::size_t row = 0; row < wire_rows; ++row) {
    Require(Little(binary, index_offset + 4 * row, 4) == row,
            "unique sequential wire indices without deduplication");
    const auto vertex =
        static_cast<std::size_t>(expected.WireVertexSources()[row]);
    for (std::size_t component = 0; component < 3; ++component) {
      const auto source = r.positions[3 * vertex + component];
      // Literal test values round to this independently selected F32 value.
      // The shifted rich source is the only nonexact successful narrowing case.
      const auto target = source == 5.0 + kShiftError ? 5.0 : source;
      const auto word = static_cast<std::uint32_t>(
          Little(binary, position_offset + 12 * row + 4 * component, 4));
      const auto decoded = static_cast<double>(std::bit_cast<float>(word));
      Require(decoded == target && (target != 0 || std::signbit(decoded) ==
                                                       std::signbit(target)),
              "physical F32 words equal independent exact/bounded literal "
              "coordinates");
      minimum[component] = std::min(minimum[component], decoded);
      maximum[component] = std::max(maximum[component], decoded);
    }
  }
  for (std::size_t component = 0; component < 3; ++component) {
    auto numeric = [](const Json& n) {
      double result = 0;
      const auto [end, error] = std::from_chars(
          n.value.data(), n.value.data() + n.value.size(), result);
      Require(error == std::errc{} && end == n.value.data() + n.value.size(),
              "finite numeric bound");
      return result;
    };
    Require(
        numeric(positions.At(kMinKey).At(component)) == minimum[component] &&
            numeric(positions.At(kMaxKey).At(component)) == maximum[component],
        "accessor bounds match actual decoded wire domain, excluding unused "
        "vertices");
  }
}
inline void Physical(const fs::path& destination,
                     const OffGlbSurfaceWorkflowRecord& record) {
  const auto& facts = record.Facts();
  Require(facts.encoding && facts.encoding->output && facts.inspection &&
              facts.inspection->inspection && facts.publication &&
              facts.publication->bundle,
          "complete genuine E/V/P owners");
  const auto& output = *facts.encoding->output;
  const auto& published = *facts.publication->bundle;
  constexpr std::size_t kPayloadMemberCount = 3, kPublishedMemberCount = 4,
                        kCapsuleHashedMemberCount = 2;
  const auto receipt = published.Receipt();
  std::map<std::string, std::vector<std::byte>, std::less<>> files;
  for (const auto& entry : fs::directory_iterator(destination)) {
    Require(entry.is_regular_file() && !entry.is_symlink(),
            "closed directory contains only regular payload/receipt files");
    files.emplace(entry.path().filename().string(), Read(entry.path()));
  }
  Require(files.size() == kPublishedMemberCount && files.contains(kGlbPath) &&
              files.contains(kCapsuleBinaryPath) &&
              files.contains(kCapsuleJsonPath) &&
              files.contains(std::string(receipt.path)),
          "exact annotated GLB payload plus one receipt");
  for (const auto& member : output.Members())
    Require(files.contains(std::string(member.path)) &&
                std::ranges::equal(files.at(std::string(member.path)),
                                   member.bytes),
            "actual published payload equals genuine encoder bytes");
  Require(
      std::ranges::equal(files.at(std::string(receipt.path)), receipt.bytes),
      "physical receipt equals returned immutable bytes");
  Require(DigestHex(receipt.sha256) == Sha(receipt.bytes),
          "returned receipt digest independently matches physical bytes");
  const auto memory_members = facts.inspection->inspection->CapturedMembers();
  const auto physical_members = published.Inspection().CapturedMembers();
  Require(memory_members.size() == kPayloadMemberCount &&
              physical_members.size() == kPayloadMemberCount,
          "distinct memory and physical inspections retain three actual "
          "captures each");
  for (std::size_t i = 0; i < memory_members.size(); ++i)
    Require(
        memory_members[i].bytes.empty() ||
            memory_members[i].bytes.data() != physical_members[i].bytes.data(),
        "publisher fresh V captures physical bytes independently of memory V");
  const auto receipt_json = Parse(receipt.bytes);
  Require(receipt_json.At(kSchemaKey).String() == kPublicationSchema &&
              receipt_json.At(kPhaseKey).String() == kVerifiedPhase &&
              receipt_json.At(kEntryPathKey).String() == kGlbPath,
          "receipt truthfully certifies precommit GLB payload");
  Require(!receipt_json.Has(kCommitKey) && !receipt_json.Has(kCleanupKey),
          "receipt does not certify later native outcomes");
  std::set<std::string> payloads;
  for (const auto& member : receipt_json.At(kPayloadMembersKey).array) {
    const auto path = std::string(member.At(kPathKey).String());
    Require(path != receipt.path && files.contains(path) &&
                payloads.insert(path).second,
            "receipt closed unique payload inventory excludes itself");
    Require(member.At(kByteLengthKey).U64() == files.at(path).size() &&
                member.At(kSha256Key).String() == Sha(files.at(path)),
            "independent physical length and SHA for every payload");
  }
  Require(payloads == std::set<std::string>{std::string(kCapsuleBinaryPath),
                                            std::string(kGlbPath),
                                            std::string(kCapsuleJsonPath)},
          "receipt covers all three payloads");
  const auto report = record.Facts().off->record->ReportJson();
  const auto& caller = receipt_json.At(kCallerReportKey);
  Require(caller.At(kTextKey).String() == Text(report) &&
              caller.At(kByteLengthKey).U64() == report.size() &&
              caller.At(kSha256Key).String() == Sha(report),
          "verbatim OFF report remains uninterpreted caller text");
  const auto capsule = Parse(files.at(std::string(kCapsuleJsonPath)));
  const auto& binary = files.at(std::string(kCapsuleBinaryPath));
  Require(capsule.At(kSchemaKey).String() == kBundleSchema &&
              capsule.At(kCoordinatesKey).At(kKindKey).String() ==
                  kCoordinateWire &&
              capsule.At(kChannelsKey).array.empty() &&
              capsule.At(kUvMappingKey).array.empty(),
          "physical capsule preserves explicit coordinate declaration and "
          "absent channels");
  CapsuleMesh(capsule.At(kOriginalKey), binary, output.Original().Source());
  CapsuleMesh(capsule.At(kTriangulatedKey), binary, output.Triangles().Mesh());
  const auto& triangulation = capsule.At(kTriangulationKey);
  Require(std::ranges::equal(
              Slice(triangulation.At(kFaceSourcesKey), binary, kU64Wire),
              output.Triangles().FaceSources()) &&
              std::ranges::equal(
                  Slice(triangulation.At(kCornerSourcesKey), binary, kU64Wire),
                  output.Triangles().CornerSources()) &&
              std::ranges::equal(Slice(triangulation.At(kFaceOutputOffsetsKey),
                                       binary, kU64Wire),
                                 output.Triangles().FaceOutputOffsets()),
          "capsule R-to-T maps match independently validated genuine "
          "triangulation");
  const auto& maps = capsule.At(kWireKey).At(kMapsKey);
  for (const auto& [key, values] :
       std::array<std::pair<std::string_view, std::span<const std::uint64_t>>,
                  11>{
           {{kTriangleFacesKey, output.WireTriangleFaces()},
            {kTriangleSourcesKey, output.WireTriangleSources()},
            {kCornerCornersKey, output.WireCornerCorners()},
            {kCornerSourcesKey, output.WireCornerSources()},
            {kVertexSourcesKey, output.WireVertexSources()},
            {kSourceVertexOffsetsKey, output.SourceVertexOffsets()},
            {kSourceVertexMembersKey, output.SourceVertexWireMembers()},
            {kSourceFaceOffsetsKey, output.SourceFaceOffsets()},
            {kSourceFaceMembersKey, output.SourceFaceWireTriangles()},
            {kSourceCornerOffsetsKey, output.SourceCornerOffsets()},
            {kSourceCornerMembersKey, output.SourceCornerWireCorners()}}})
    Require(std::ranges::equal(Slice(maps.At(key), binary, kU64Wire), values),
            "capsule complete validated forward/inverse wire maps");
  std::set<std::string> capsule_members;
  for (const auto& member : capsule.At(kMembersKey).array) {
    const auto path = std::string(member.At(kPathKey).String());
    Require((path == kCapsuleBinaryPath || path == kGlbPath) &&
                capsule_members.insert(path).second,
            "capsule member DAG excludes itself/receipt");
    Require(member.At(kBytesKey).U64() == files.at(path).size() &&
                member.At(kSha256Key).String() == Sha(files.at(path)),
            "independent capsule member hashes");
  }
  Require(capsule_members.size() == kCapsuleHashedMemberCount,
          "capsule covers both binary payloads");
  PhysicalGlb(files.at(std::string(kGlbPath)), output);
}
inline void Complete(const OffGlbSurfaceWorkflowResult& result,
                     const fs::path& destination, std::string_view source,
                     bool changed, bool xy = true) {
  Require(result.status == OffGlbSurfaceStatus::kSuccess &&
              result.diagnostic.stage == OffGlbSurfaceStage::kComplete &&
              result.diagnostic.code == OffGlbSurfaceCode::kNone &&
              result.record,
          "actual overall success requires complete published export");
  CheckOff(*result.record, source, changed);
  const auto& facts = result.record->Facts();
  Require(facts.triangulation &&
              facts.triangulation->status ==
                  geo::ChargedTriangulationStatus::kAccepted &&
              facts.triangulation->triangulation &&
              !facts.triangulation->source_capture,
          "genuine retained charged triangulation without recapture");
  const auto& t = *facts.triangulation->triangulation;
  const auto& candidate = facts.off->record->RepairFacts()->Candidate();
  if (xy) CheckTriangles(candidate, t);
  Require(facts.encoding &&
              facts.encoding->status == ic::SurfaceStatus::kSuccess &&
              facts.encoding->output && facts.inspection &&
              facts.inspection->status == ic::SurfaceStatus::kSuccess &&
              facts.inspection->inspection,
          "actual E and independent memory V succeeded");
  CheckWire(*facts.encoding->output);
  Require(geo::SharesExecutionBudget(candidate.ShareExecutionContext(),
                                     t.ShareExecutionContext()) &&
              geo::SharesExecutionBudget(candidate.ShareExecutionContext(),
                                         facts.encoding->output->Original()
                                             .Source()
                                             .ShareExecutionContext()),
          "same genuine strong shared Geometry ledger across R/T/E");
  Require(facts.publication &&
              facts.publication->outcome ==
                  ic::SurfaceFilePublicationOutcome::kPublished &&
              facts.publication->bundle && facts.publication->facts.commit &&
              facts.publication->facts.commit->outcome ==
                  ic::SurfaceFileCommitOutcome::kCommitted,
          "physical no-replace commit is actual typed success");
  Require(facts.publication->facts.payload_reread &&
              facts.publication->facts.inspection &&
              facts.publication->facts.inspection->status ==
                  ic::SurfaceStatus::kSuccess &&
              facts.publication->facts.final_payload_verification &&
              facts.publication->facts.final_receipt_verification,
          "publisher performed its distinct physical reread/fresh V/final byte "
          "passes");
  Physical(destination, *result.record);
}
}  // namespace meshvale::repair::test_off_surface
#endif  // MESHVALE_REPAIR_TESTS_OFF_SURFACE_EXPORT_FIXTURE_H_
