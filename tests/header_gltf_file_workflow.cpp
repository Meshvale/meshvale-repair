// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>

#include <filesystem>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace {
namespace repair = meshvale::repair;
namespace interchange = meshvale::interchange;

// Unevaluated old aggregate forms: no fake asset, publication or runtime call.
static_assert(
    std::is_same_v<decltype(repair::GltfDuplicateFileWorkflowRequest{
                       std::declval<const interchange::GltfFileAsset&>(),
                       {},
                       {},
                       {},
                       {},
                       {},
                       nullptr,
                       repair::GltfReportFailureRetention::kRelease}),
                   repair::GltfDuplicateFileWorkflowRequest>);
static_assert(
    std::is_same_v<decltype(repair::GltfDuplicateFilePublication{
                       std::declval<const std::filesystem::path&>(), {}, {}}),
                   repair::GltfDuplicateFilePublication>);
static_assert(std::is_same_v<
              decltype(repair::GltfDuplicateFileWorkflowRequest::deformation),
              repair::GltfWorkflowDeformationPolicies>);
static_assert(
    std::is_same_v<
        decltype(repair::GltfDuplicateFilePublication::deformation_publication),
        interchange::GltfDeformationPublicationPolicies>);
static_assert(std::is_same_v<
              decltype(repair::GltfDuplicateFileWorkflowResult::deformation),
              std::optional<repair::GltfWorkflowDeformationFacts>>);

constexpr interchange::GltfDeformationPublicationPolicies kInactivePolicies{};
static_assert(kInactivePolicies.read.limits.max_node_rows == 0 &&
              kInactivePolicies.extraction.limits.max_node_rows == 0 &&
              kInactivePolicies.verification.limits.max_node_rows == 0);
constexpr auto kPhasePolicies = [] {
  interchange::GltfDeformationPublicationPolicies policies{};
  policies.read.limits.max_node_rows = 1;
  policies.extraction.limits.max_node_rows = 2;
  policies.verification.limits.max_node_rows = 3;
  return policies;
}();
static_assert(kPhasePolicies.read.limits.max_node_rows == 1 &&
              kPhasePolicies.extraction.limits.max_node_rows == 2 &&
              kPhasePolicies.verification.limits.max_node_rows == 3);
static_assert(
    std::is_same_v<
        decltype(std::declval<const repair::GltfDuplicateFileWorkflowRecord&>()
                     .DeformationFacts()),
        const repair::GltfWorkflowDeformationFacts*>);
static_assert(
    noexcept(std::declval<const repair::GltfDuplicateFileWorkflowRecord&>()
                 .DeformationFacts()));
using FileCall = repair::GltfDuplicateFileWorkflowResult (*)(
    const repair::GltfDuplicateFileWorkflowRequest&,
    const repair::GltfDuplicateFileWorkflowLimits&,
    const repair::GltfDuplicateFileWorkflowWorkPolicy&,
    const meshvale::geometry::ExecutionContext&, std::stop_token);
static_assert(
    std::is_same_v<decltype(&repair::RunGltfDuplicateFileWorkflow), FileCall>);
}  // namespace
