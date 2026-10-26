// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_GLTF_TRANSLATION_WORKFLOW_FIXTURE_H_
#define MESHVALE_REPAIR_TESTS_GLTF_TRANSLATION_WORKFLOW_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace meshvale::repair::test_translation_workflow {
namespace geo = geometry;
namespace ic = interchange;
inline constexpr std::uint64_t kMiB = 1024 * 1024;
inline constexpr std::uint64_t kStageBytes = 4 * kMiB;
inline constexpr std::uint64_t kWorkflowBytes = 64 * kMiB;
inline constexpr std::uint64_t kLedgerBytes = 128 * kMiB;
inline constexpr std::uint64_t kCount = 4096;
inline constexpr std::uint64_t kDepth = 64;
inline constexpr std::uint64_t kIdentityBytes = 256;
inline constexpr std::uint64_t kJobs = 16;
inline constexpr std::size_t kWorkers = 4;
inline constexpr unsigned kByteBits = 8;
inline constexpr std::uint32_t kByteMask = 255;
inline constexpr std::size_t kFloatBytes = sizeof(std::uint32_t);
inline constexpr std::string_view kEntry = "moving.gltf";
inline constexpr std::string_view kBuffer = "moving.bin";
inline constexpr std::string_view kUnusedBuffer = "unused.bin";
inline constexpr std::string_view kRedImage = "red.png";
inline constexpr std::string_view kBlueImage = "blue.png";
inline constexpr std::string_view kInputId = "moving-shared-primitive";
inline constexpr std::string_view kReport = "receipts/report.json";
inline constexpr std::array<DuplicateTarget, 1> kTargets{{{0, 1}}};
// Caller assertion for these authored two-component UV rows: no value refers
// to changed face/corner indices. This is not an inferred generic engine
// policy.
inline constexpr std::array<RowLocalAttributeDeclaration, 1>
    kRowLocalAttributes{{{geo::AttributeDomain::vertex, "TEXCOORD_0"}}};
inline constexpr std::array<geo::index_t, 2> kFaceMap{0, 0};
inline constexpr std::array<geo::index_t, 6> kCornerMap{0, 1, 2, 1, 2, 0};
inline constexpr std::array<geo::index_t, 1> kSourceFaces{0};
inline constexpr std::array<geo::index_t, 3> kSourceCorners{0, 1, 2};
inline constexpr std::string_view kAnimations =
    R"("animations":[{"samplers":[{"input":2,"output":3,"interpolation":"LINEAR"},{"input":2,"output":4,"interpolation":"STEP"}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}},{"sampler":1,"target":{"node":2,"path":"translation"}}]}])";
inline constexpr std::string_view kNodes =
    R"("nodes":[{"children":[1,2],"scale":[2,2,2]},{"mesh":0,"translation":[1,0,0]},{"mesh":0,"translation":[0,1,0]},{"mesh":1,"translation":[7,0,0]}])";
inline constexpr std::string_view kRedPng =
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
    "0000000d49444154789c63f8cfc0f01f00050001ff89993d1d0000000049454e44ae42608"
    "2";
inline constexpr std::string_view kBluePng =
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
    "0000000d49444154789c636060f8ff1f00030201ffe6770bae0000000049454e44ae42608"
    "2";
inline void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
inline std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
inline std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
inline std::vector<std::byte> Hex(std::string_view text) {
  constexpr unsigned kNibbleBits = 4;
  constexpr unsigned kHexLetterOffset = 10;
  constexpr std::size_t kHexOctetCharacters = 2;
  const auto digit = [](char c) -> unsigned {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f')
      return static_cast<unsigned>(c - 'a') + kHexLetterOffset;
    throw std::runtime_error("invalid authored hex digit");
  };
  Require(text.size() % kHexOctetCharacters == 0,
          "complete authored hex octets");
  std::vector<std::byte> result;
  for (std::size_t i = 0; i < text.size(); i += kHexOctetCharacters)
    result.push_back(static_cast<std::byte>((digit(text[i]) << kNibbleBits) |
                                            digit(text[i + 1])));
  return result;
}
inline void Word(std::vector<std::byte>& bytes, std::uint32_t value) {
  for (std::size_t i = 0; i < kFloatBytes; ++i)
    bytes.push_back(
        static_cast<std::byte>((value >> (i * kByteBits)) & kByteMask));
}
// Original independent little-endian float/index literals. The selected mesh
// has one cyclic duplicate and an unused signed-zero vertex. Mesh1 is
// unrelated.
struct MovingFixture {
  std::string json;
  std::vector<std::byte> binary;
  std::vector<std::byte> red = Hex(kRedPng), blue = Hex(kBluePng);
  std::array<std::byte, 4> unused{std::byte{0x52}, std::byte{0},
                                  std::byte{0xfe}, std::byte{0x17}};
  MovingFixture() {
    constexpr std::array<std::uint32_t, 12> kPositionBits{
        0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0, 0x80000000, 0x40000000, 0};
    constexpr std::array<std::uint16_t, 6> kIndices{0, 1, 2, 1, 2, 0};
    constexpr std::array<std::uint32_t, 2> kTimeBits{0, 0x3f800000};
    constexpr std::array<std::uint32_t, 6> kLinearBits{0,          0, 0,
                                                       0x40000000, 0, 0};
    constexpr std::array<std::uint32_t, 6> kStepBits{0, 0, 0, 0, 0x40800000, 0};
    constexpr std::array<std::uint32_t, 8> kUvBits{
        0, 0, 0x3f800000, 0, 0, 0x3f800000, 0, 0};
    constexpr std::array<std::uint32_t, 2> kUnusedTail{0xdeadbeef, 0x12345678};
    for (auto value : kPositionBits) Word(binary, value);
    for (auto value : kIndices) {
      binary.push_back(static_cast<std::byte>(value & kByteMask));
      binary.push_back(static_cast<std::byte>(value >> kByteBits));
    }
    for (auto value : kTimeBits) Word(binary, value);
    for (auto value : kLinearBits) Word(binary, value);
    for (auto value : kStepBits) Word(binary, value);
    for (auto value : kUvBits) Word(binary, value);
    for (auto value : kUnusedTail) Word(binary, value);
    json =
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"moving.bin","byteLength":148},{"uri":"unused.bin","byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":12},{"buffer":0,"byteOffset":60,"byteLength":8},{"buffer":0,"byteOffset":68,"byteLength":24},{"buffer":0,"byteOffset":92,"byteLength":24},{"buffer":0,"byteOffset":116,"byteLength":32}],"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,2,0]},{"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":3,"componentType":5126,"count":2,"type":"VEC3"},{"bufferView":4,"componentType":5126,"count":2,"type":"VEC3"},{"bufferView":5,"componentType":5126,"count":4,"type":"VEC2"}],"images":[{"uri":"red.png","mimeType":"image/png"},{"uri":"blue.png","mimeType":"image/png"}],"textures":[{"source":0},{"source":1}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{"pbrMetallicRoughness":{"baseColorTexture":{"index":1}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":5},"indices":1,"material":0}]},{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":5},"indices":1,"material":1}]}],)";
    json += kNodes;
    json += ',';
    json += kAnimations;
    json += R"(,"scenes":[{"nodes":[0,3]}],"extras":{"authored":1.2300e+04}})";
    Require(binary.size() == 156,
            "literal layout with eight physical tail bytes");
  }
  std::array<ic::SuppliedGltfResource, 4> Resources() const {
    return {{{ic::GltfResourceKind::kBuffer, 0, binary},
             {ic::GltfResourceKind::kBuffer, 1, unused},
             {ic::GltfResourceKind::kImage, 0, red},
             {ic::GltfResourceKind::kImage, 1, blue}}};
  }
};
inline ic::GltfDirectTranslationLimits TranslationLimits() {
  return {kCount, kCount, kCount, kCount, kCount,
          kCount, kCount, kMiB,   kMiB,   2 * kMiB};
}
inline ic::GltfDirectTranslationWorkPolicy TranslationWork() {
  return {1, 1, 1, kJobs};
}
inline geo::MeshMaterializationLimits CaptureLimits() {
  return {kCount, kCount, kCount, kCount, kCount,     kCount,
          kCount, kCount, kCount, kMiB,   kStageBytes};
}
inline ic::GltfReadOperationLimits ReadLimits() {
  return {
      {kMiB, kMiB, kMiB, kCount, kDepth, kMiB, kIdentityBytes, kIdentityBytes,
       kIdentityBytes, kCount, kCount, kCount, kCount, kStageBytes},
      kStageBytes};
}
inline GltfDuplicateWorkflowLimits Limits() {
  GltfDuplicateWorkflowLimits x;
  x.max_new_requested_bytes = kWorkflowBytes;
  x.max_input_id_bytes = kIdentityBytes;
  x.max_reload_bindings = kCount;
  x.max_report_bytes = kMiB;
  x.read = x.reload = ReadLimits();
  x.extraction =
      x.reload_extraction = {kCount, kCount, kStageBytes, TranslationLimits()};
  x.materialization = {
      kCount, kStageBytes, CaptureLimits(), {kCount, kStageBytes}};
  x.repair = {kCount,         kCount,      kIdentityBytes,
              kCount,         kStageBytes, {kCount, kStageBytes},
              CaptureLimits()};
  x.writing = {kMiB,
               kCount,
               kDepth,
               kCount,
               kCount,
               kCount,
               kMiB,
               kCount,
               kCount,
               kMiB,
               {kMiB, kCount, kMiB, kMiB, kStageBytes},
               TranslationLimits()};
  x.verification = {
      kMiB,   kCount, kDepth, kCount, kMiB, kCount,      kCount,
      kCount, kMiB,   kCount, kCount, kMiB, kStageBytes, TranslationLimits()};
  x.report_publication_reload = TranslationLimits();
  x.report_publication_verification = TranslationLimits();
  return x;
}
inline GltfDuplicateWorkflowWorkPolicy Work() {
  constexpr std::uint64_t kPartitionBytes = 256;
  constexpr std::uint64_t kStopBlockBytes = 16;
  const GltfWorkflowByteWorkPolicy bytes{1, kPartitionBytes, kStopBlockBytes,
                                         kJobs};
  return {{1, kPartitionBytes, 1, kPartitionBytes, TranslationWork()},
          {1, kPartitionBytes, kStopBlockBytes, kJobs},
          {1, kPartitionBytes, kStopBlockBytes, kJobs, TranslationWork()},
          {1, kPartitionBytes, kStopBlockBytes, kJobs, TranslationWork()},
          bytes};
}
inline GltfDuplicateWorkflowRequest Request(
    const MovingFixture& fixture,
    std::span<const ic::SuppliedGltfResource> resources, bool changed = true) {
  GltfDuplicateWorkflowRequest request{
      Bytes(fixture.json),
      resources,
      kInputId,
      0,
      {ic::GltfTriangleRequest::kRequireTriangles, ic::GltfValueRequest::kRaw,
       ic::GltfProcessingProfile::kDirectTranslationTrianglesV1},
      changed ? std::span<const DuplicateTarget>(kTargets)
              : std::span<const DuplicateTarget>{},
      {kRowLocalAttributes}};
  request.report_failure_retention =
      GltfReportFailureRetention::kRetainCompletedRepair;
  return request;
}
inline void CheckFacts(const GltfWorkflowTranslationFacts& facts,
                       bool publication) {
  Require(
      facts.profile == ic::GltfProcessingProfile::kDirectTranslationTrianglesV1,
      "actual selected processing profile");
  for (const auto* value :
       {&facts.source, &facts.writing, &facts.reload, &facts.verification}) {
    Require(value->has_value() &&
                (*value)->status == ic::GltfStatus::kAccepted &&
                (*value)->complete,
            "four actual successful memory observations");
    Require((*value)->execution.launched_workers ==
                (*value)->execution.joined_workers,
            "actual translation workers joined");
  }
  Require(facts.source->node_rows == 4 && facts.source->mesh_users == 2 &&
              facts.source->scanned_channels == 2 &&
              facts.source->affected_channels == 2 &&
              facts.source->distinct_samplers == 2 &&
              facts.source->keys_checked == 4,
          "literal source inventory and all four distinct sampler keys");
  if (publication)
    for (const auto* value :
         {&facts.publication_reload, &facts.publication_verification})
      Require(value->has_value() && (*value)->complete,
              "actual staged validation observations");
  else
    Require(!facts.publication_reload && !facts.publication_verification,
            "memory report carriers do not invent staged execution");
}
inline void CheckRepair(const MaterializedDuplicateRepair& repair,
                        bool changed) {
  Require(repair.Source().View().positions.size() == 12 &&
              repair.Candidate().View().positions.size() == 12,
          "all four vertex rows including unused signed zero remain");
  if (changed) {
    Require(std::ranges::equal(repair.FaceMap(), kFaceMap) &&
                std::ranges::equal(repair.CornerMap(), kCornerMap) &&
                std::ranges::equal(repair.SourceFaces(), kSourceFaces) &&
                std::ranges::equal(repair.SourceCorners(), kSourceCorners),
            "independent literal four maps");
  } else {
    Require(repair.Outcome() == CandidateOutcome::unchanged &&
                repair.SourceFaces().size() == 2,
            "explicit empty-target unchanged request");
    for (std::size_t i = 0; i < repair.FaceMap().size(); ++i)
      Require(repair.FaceMap()[i] == i, "unchanged face identity");
    for (std::size_t i = 0; i < repair.CornerMap().size(); ++i)
      Require(repair.CornerMap()[i] == i, "unchanged corner identity");
  }
}
inline void CheckOutput(const GltfDuplicateWorkflowRecord& record,
                        bool changed) {
  Require(record.Output() && record.OutputInspection() && record.Repair(),
          "genuine verified output owners");
  CheckFacts(record.TranslationFacts(), false);
  CheckRepair(*record.Repair(), changed);
  Require(
      Text(record.Output()->Json()).find(kAnimations) !=
              std::string_view::npos &&
          Text(record.Output()->Json()).find(kNodes) != std::string_view::npos,
      "exact authored animation and node/TRS slices remain");
  for (const auto& source : record.ReadAsset()->Resources()) {
    const auto& output = record.Output()->Resources();
    const auto match = std::ranges::find_if(output, [&](const auto& value) {
      return value.kind == source.kind &&
             value.table_index == source.table_index;
    });
    Require(
        match != output.end() && std::ranges::equal(source.bytes, match->bytes),
        "original resource bytes including narrow binary samples/tails/images "
        "remain");
  }
  Require(record.Coverage().size() == 10 &&
              record.Coverage().back().check ==
                  GltfWorkflowCheck::kTranslationPreservation &&
              record.Coverage().back().passed == true,
          "required appended check9, solid8 remains separate");
  const auto report = Text(record.ReportJson());
  Require(
      report.find("\"processing\":{\"profile\":\"direct-translation-v1\"") !=
              std::string_view::npos &&
          report.find("\"publication_reload\":null") !=
              std::string_view::npos &&
          report.find("\"meshvale.repair.gltf\":{\"version\":\"2\"") !=
              std::string_view::npos,
      "new-mode report version and actual temporal availability");
}
inline void MemoryPositive() {
  for (bool changed : {false, true}) {
    std::optional<GltfDuplicateWorkflowRecord> retained;
    {
      geo::ExecutionContext context({kWorkers, 1, kLedgerBytes});
      {
        MovingFixture data;
        const auto resources = data.Resources();
        auto result = RunGltfDuplicateWorkflow(
            Request(data, resources, changed), Limits(), Work(), context);
        Require(result.status == GltfWorkflowStatus::kSuccess && result.record,
                "moving memory workflow succeeds");
        CheckFacts(result.translation, false);
        CheckOutput(*result.record, changed);
        retained = *result.record;
      }
      Require(context.ActiveWorkers() == 0,
              "all memory workers joined before return");
    }
    CheckOutput(*retained, changed);
  }
}
}  // namespace meshvale::repair::test_translation_workflow
#endif  // MESHVALE_REPAIR_TESTS_GLTF_TRANSLATION_WORKFLOW_FIXTURE_H_
