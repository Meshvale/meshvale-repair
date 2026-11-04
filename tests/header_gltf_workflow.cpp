// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/repair/gltf_workflow.h>

#include <array>
#include <cstddef>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace {
namespace repair = meshvale::repair;
namespace interchange = meshvale::interchange;

// These are declaration/default compatibility controls, not complete positive
// runtime policies or executed requests. The old positional form still works.
static_assert(std::is_same_v<decltype(repair::GltfDuplicateWorkflowRequest{
                                 {},
                                 {},
                                 {},
                                 {},
                                 {},
                                 {},
                                 {},
                                 repair::GltfReportFailureRetention::kRelease}),
                             repair::GltfDuplicateWorkflowRequest>);
constexpr repair::GltfDuplicateWorkflowRequest kLegacyRequest{};
static_assert(kLegacyRequest.extraction.processing_profile ==
              interchange::GltfProcessingProfile::kStaticLocalTrianglesV1);
static_assert(kLegacyRequest.report_failure_retention ==
              repair::GltfReportFailureRetention::kRelease);
static_assert(kLegacyRequest.extraction.deformation.limits.max_node_rows == 0);
static_assert(kLegacyRequest.deformation.source_read.limits.max_node_rows == 0);

constexpr auto kNewRequest = [] {
  repair::GltfDuplicateWorkflowRequest request{};
  request.extraction.processing_profile =
      interchange::GltfProcessingProfile::kDeformationRetainedTrianglesV1;
  request.extraction.deformation.limits.max_node_rows = 1;
  request.deformation.source_read.limits.max_node_rows = 2;
  request.deformation.materialization.limits.max_node_rows = 3;
  request.deformation.writing.limits.max_node_rows = 4;
  request.deformation.reload_read.limits.max_node_rows = 5;
  request.deformation.reload.limits.max_node_rows = 6;
  request.deformation.verification.limits.max_node_rows = 7;
  return request;
}();
static_assert(kNewRequest.extraction.deformation.limits.max_node_rows == 1);
static_assert(kNewRequest.deformation.source_read.limits.max_node_rows == 2);
static_assert(kNewRequest.deformation.materialization.limits.max_node_rows ==
              3);
static_assert(kNewRequest.deformation.writing.limits.max_node_rows == 4);
static_assert(kNewRequest.deformation.reload_read.limits.max_node_rows == 5);
static_assert(kNewRequest.deformation.reload.limits.max_node_rows == 6);
static_assert(kNewRequest.deformation.verification.limits.max_node_rows == 7);

template <class T>
concept HasDuplicateSourcePolicy = requires(T value) { value.source; };
static_assert(
    !HasDuplicateSourcePolicy<repair::GltfWorkflowDeformationPolicies>);

constexpr std::array kChecks{
    repair::GltfWorkflowCheck::kSourceStorage,
    repair::GltfWorkflowCheck::kTargetPolicy,
    repair::GltfWorkflowCheck::kCandidateStorage,
    repair::GltfWorkflowCheck::kRepairCorrespondence,
    repair::GltfWorkflowCheck::kCanonicalPreservation,
    repair::GltfWorkflowCheck::kReloadSelectedPrimitive,
    repair::GltfWorkflowCheck::kRetainedAssetPreservation,
    repair::GltfWorkflowCheck::kSourceUnchanged,
    repair::GltfWorkflowCheck::kSolidValidity,
    repair::GltfWorkflowCheck::kTranslationPreservation,
    repair::GltfWorkflowCheck::kDeformationSourceAdmission,
    repair::GltfWorkflowCheck::kDeformationInfluences,
    repair::GltfWorkflowCheck::kDeformationBindings,
    repair::GltfWorkflowCheck::kDeformationMorphs,
    repair::GltfWorkflowCheck::kDeformationAnimation,
    repair::GltfWorkflowCheck::kDeformationAssociation,
    repair::GltfWorkflowCheck::kDeformationOutputReload,
    repair::GltfWorkflowCheck::kDeformationStagedReload};
static_assert([] {
  for (std::size_t i = 0; i < kChecks.size(); ++i) {
    if (static_cast<std::size_t>(kChecks[i]) != i) return false;
  }
  return true;
}());
static_assert(
    static_cast<int>(
        interchange::GltfProcessingProfile::kStaticLocalTrianglesV1) == 0);
static_assert(
    static_cast<int>(
        interchange::GltfProcessingProfile::kDirectTranslationTrianglesV1) ==
    1);
static_assert(
    static_cast<int>(
        interchange::GltfProcessingProfile::kDeformationRetainedTrianglesV1) ==
    2);
static_assert(static_cast<int>(repair::GltfWorkflowExitMeaning::kSuccess) == 0);
static_assert(static_cast<int>(
                  repair::GltfWorkflowExitMeaning::kRejectedOrIncomplete) == 1);
static_assert(
    static_cast<int>(repair::GltfWorkflowExitMeaning::kExecutionFailure) == 2);
static_assert(static_cast<int>(repair::GltfWorkflowExitMeaning::kCanceled) ==
              130);

using Facts = repair::GltfWorkflowDeformationFacts;
static_assert(std::is_nothrow_copy_constructible_v<Facts>);
static_assert(std::is_nothrow_copy_assignable_v<Facts>);
static_assert(std::is_nothrow_destructible_v<Facts>);
constexpr Facts kUncalled{};
static_assert(!kUncalled.capture_read && !kUncalled.source_read &&
              !kUncalled.source && !kUncalled.materialization &&
              !kUncalled.writing && !kUncalled.reload_read &&
              !kUncalled.reload && !kUncalled.verification &&
              !kUncalled.publication_read && !kUncalled.publication_reload &&
              !kUncalled.publication_verification);
static_assert(
    std::is_same_v<decltype(repair::GltfDuplicateWorkflowResult::deformation),
                   std::optional<Facts>>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const repair::GltfDuplicateWorkflowRecord&>()
                     .DeformationFacts()),
        const Facts*>);
static_assert(
    noexcept(std::declval<const repair::GltfDuplicateWorkflowRecord&>()
                 .DeformationFacts()));
using MemoryCall = repair::GltfDuplicateWorkflowResult (*)(
    const repair::GltfDuplicateWorkflowRequest&,
    const repair::GltfDuplicateWorkflowLimits&,
    const repair::GltfDuplicateWorkflowWorkPolicy&,
    const meshvale::geometry::ExecutionContext&, std::stop_token);
static_assert(
    std::is_same_v<decltype(&repair::RunGltfDuplicateWorkflow), MemoryCall>);
}  // namespace
