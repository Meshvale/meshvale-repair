// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_GLTF_DEFORMATION_STAGED_FIXTURE_H_
#define MESHVALE_REPAIR_TESTS_GLTF_DEFORMATION_STAGED_FIXTURE_H_

#include <meshvale/interchange/gltf.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gltf_file_workflow_fixture.h"

namespace meshvale::repair::test_deformation_staged {
namespace base = test_file_workflow;
namespace ic = interchange;
namespace fs = std::filesystem;
using base::Require;
inline constexpr std::uint64_t kCardinality = 65536;
inline constexpr std::uint64_t kInfluenceSets = 16;
inline constexpr std::uint64_t kLanes = 64;
inline constexpr std::uint64_t kRelationshipBlock = 16;
inline constexpr std::uint64_t kDeformationPartition = 64;
inline constexpr std::uint64_t kNativeNameBytes = 4096;
inline constexpr std::string_view kOpaquePath = "guard.bin";
inline constexpr std::string_view kOpaqueBytes =
    "original staged physical member\n";
inline constexpr std::string_view kSentinel =
    "caller-owned no-replace sentinel\n";
inline constexpr std::string_view kReportPath = "report.json";
inline constexpr ic::GltfDeformationPolicy kPolicy{
    {kCardinality, kCardinality, kCardinality, kCardinality, kInfluenceSets,
     kLanes, kCardinality, kCardinality, kCardinality, kCardinality,
     kCardinality, base::kPhysicalBytes, kCardinality, kCardinality,
     base::kStageBytes, base::kStageBytes, base::kStageBytes,
     base::kStageBytes},
    {kDeformationPartition, kDeformationPartition, kDeformationPartition,
     kDeformationPartition, kDeformationPartition, kDeformationPartition,
     kDeformationPartition, kRelationshipBlock}};
inline const std::array<RowLocalAttributeDeclaration, 3> kDeclarations{
    {{geometry::AttributeDomain::vertex, "gltf/0/0/base/7:5f4d4154524958"},
     {geometry::AttributeDomain::vertex, "gltf/0/0/base/5:5f41e4b8ad"},
     {geometry::AttributeDomain::vertex, "gltf/0/0/morph/0/7:5f4d4154524958"}}};
inline const std::array<ic::GltfDeformationRowLocalDeclaration, 3>
    kExtractionDeclarations{{{"gltf/0/0/base/7:5f4d4154524958"},
                             {"gltf/0/0/base/5:5f41e4b8ad"},
                             {"gltf/0/0/morph/0/7:5f4d4154524958"}}};

inline std::vector<std::byte> Wire(const fs::path& fixtures,
                                   std::string_view name, bool glb) {
  auto bytes =
      base::Read(fixtures / (std::string(name) + (glb ? ".glb.hex" : ".gltf")));
  if (!glb) return bytes;
  std::vector<std::byte> decoded;
  std::optional<unsigned> high;
  for (const auto c : base::Text(bytes)) {
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
    const auto position = std::string_view("0123456789abcdef").find(c);
    Require(position != std::string_view::npos, "literal lowercase GLB hex");
    if (high) {
      decoded.push_back(static_cast<std::byte>((*high << 4) | position));
      high.reset();
    } else {
      high = static_cast<unsigned>(position);
    }
  }
  Require(!high, "complete GLB hex bytes");
  return decoded;
}
inline GltfDuplicateFileWorkflowLimits Limits() {
  auto limits = base::Limits();
  limits.memory.repair.max_declaration_name_bytes = kNativeNameBytes;
  return limits;
}
inline GltfDuplicateWorkflowLimits MemoryLimits() {
  const auto file = Limits().memory;
  return {file.max_new_requested_bytes,
          file.max_input_id_bytes,
          file.max_reload_bindings,
          file.max_report_bytes,
          base::ReadLimits(),
          file.extraction,
          file.materialization,
          file.repair,
          file.writing,
          file.reload,
          file.reload_extraction,
          file.verification};
}
inline ic::GltfExtractionRequest Extraction(
    const ic::GltfDeformationPolicy& policy = kPolicy, bool rich = false) {
  return {ic::GltfTriangleRequest::kRequireTriangles,
          ic::GltfValueRequest::kRawAndNormalized,
          ic::GltfProcessingProfile::kDeformationRetainedTrianglesV1, policy,
          rich ? std::span(kExtractionDeclarations)
               : std::span<const ic::GltfDeformationRowLocalDeclaration>{}};
}
inline GltfWorkflowDeformationPolicies Policies(
    const ic::GltfDeformationPolicy& policy = kPolicy) {
  return {policy, policy, policy, policy, policy, policy};
}
inline void CheckStaged(const GltfWorkflowDeformationFacts& facts) {
  Require(facts.publication_read && facts.publication_read->complete &&
              facts.publication_reload && facts.publication_reload->complete &&
              facts.publication_verification &&
              facts.publication_verification->complete &&
              facts.publication_verification->predicates.staged_reload == true,
          "actual read, extraction and publisher-OWN staged facts complete");
}
}  // namespace meshvale::repair::test_deformation_staged
#endif  // MESHVALE_REPAIR_TESTS_GLTF_DEFORMATION_STAGED_FIXTURE_H_
