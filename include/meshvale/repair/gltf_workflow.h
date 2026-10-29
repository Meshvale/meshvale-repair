// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_GLTF_WORKFLOW_H_
#define MESHVALE_REPAIR_GLTF_WORKFLOW_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_deformation.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <variant>

namespace meshvale::repair {
namespace gltf_workflow_detail {
struct Access;
struct WorkflowState;
}  // namespace gltf_workflow_detail

// Per-invocation ownership after an expected failure prevents a report record.
// Retention preserves only an actually completed Repair, never partial wire.
enum class GltfReportFailureRetention { kRelease, kRetainCompletedRepair };

// The source extraction policy belongs only to request.extraction.deformation.
// These separate policies govern the other actual memory-stage calls. All
// fields default to inactive zero policy for legacy processing; the selected
// deformation profile requires positive policy at each applicable phase.
struct GltfWorkflowDeformationPolicies {
  interchange::GltfDeformationPolicy source_read;
  interchange::GltfDeformationPolicy materialization;
  interchange::GltfDeformationPolicy writing;
  interchange::GltfDeformationPolicy reload_read;
  interchange::GltfDeformationPolicy reload;
  interchange::GltfDeformationPolicy verification;
};

// Fixed inline copies of actual returned observations, not successful owners
// or recomputed measurements. Uncalled helpers leave their slots empty;
// expected later failures preserve earlier returned facts without a record.
// Revision/policy views refer to immutable compiled metadata, never caller or
// asset buffers. Copies allocate nothing and do not retain released proofs.
struct GltfWorkflowDeformationFacts {
  interchange::GltfProcessingProfile profile =
      interchange::GltfProcessingProfile::kDeformationRetainedTrianglesV1;
  interchange::GltfAdmissionProfile read_admission_profile =
      interchange::GltfAdmissionProfile::kRetainedCoreDeformationV1;
  interchange::GltfNormativeRevision normative_revision;
  std::string_view quaternion_policy;
  std::optional<interchange::GltfDeformationObservation> capture_read;
  std::optional<interchange::GltfDeformationObservation> source_read;
  std::optional<interchange::GltfDeformationObservation> source;
  std::optional<interchange::GltfDeformationObservation> materialization;
  std::optional<interchange::GltfDeformationObservation> writing;
  std::optional<interchange::GltfDeformationObservation> reload_read;
  std::optional<interchange::GltfDeformationObservation> reload;
  std::optional<interchange::GltfDeformationObservation> verification;
  std::optional<interchange::GltfDeformationObservation> publication_read;
  std::optional<interchange::GltfDeformationObservation> publication_reload;
  std::optional<interchange::GltfDeformationObservation>
      publication_verification;
};

// Development native interface; link optional meshvale::gltf_workflow.
// All views are borrowed, readable and unchanged until synchronous return.
struct GltfDuplicateWorkflowRequest {
  std::span<const std::byte> json;
  std::span<const interchange::SuppliedGltfResource> resources;
  // Opaque report identity, not a path; see the report owner for its grammar.
  std::string_view input_id;
  // Ordinal in GltfAsset::Primitive(), not a mesh index or instance index.
  std::optional<std::uint64_t> primitive_ordinal;
  interchange::GltfExtractionRequest extraction;
  std::span<const DuplicateTarget> targets;
  MaterializedDuplicateOptions repair;
  GltfReportFailureRetention report_failure_retention =
      GltfReportFailureRetention::kRelease;
  GltfWorkflowDeformationPolicies deformation{};
};

// Required positive fields, including all nested limits. The aggregate cap
// includes every concurrently live fresh request across all delegated stages.
struct GltfDuplicateWorkflowLimits {
  std::uint64_t max_new_requested_bytes = 0;
  std::uint64_t max_input_id_bytes = 0;
  std::uint64_t max_reload_bindings = 0;
  std::uint64_t max_report_bytes = 0;
  // Requires the additive Interchange workflow-support producer.
  interchange::GltfReadOperationLimits read;
  interchange::GltfExtractionLimits extraction;
  // Its three positive request caps must have a checked sum fitting the
  // remaining workflow allowance before transfer; no independent clamping.
  interchange::GltfMaterializationLimits materialization;
  MaterializedDuplicateLimits repair;
  interchange::GltfTopologyWriteLimits writing;
  interchange::GltfReadOperationLimits reload;
  interchange::GltfExtractionLimits reload_extraction;
  interchange::GltfTopologyOutputInspectionLimits verification;
  // Explicit report configuration only: this memory operation never calls
  // publication validators. Required positive in direct-translation mode;
  // static mode ignores zero defaults. File wrappers supply their actual caps.
  interchange::GltfDirectTranslationLimits report_publication_reload{};
  interchange::GltfDirectTranslationLimits report_publication_verification{};
};
struct GltfWorkflowByteWorkPolicy {
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct GltfDuplicateWorkflowWorkPolicy {
  interchange::GltfWorkPolicy interchange;
  geometry::MeshMaterializationWorkPolicy geometry;
  interchange::GltfTopologyWriteWorkPolicy writing;
  interchange::GltfTopologyOutputInspectionWorkPolicy verification;
  GltfWorkflowByteWorkPolicy report;
};

enum class GltfWorkflowStage {
  kConfiguration,
  kRead,
  kExtract,
  kMaterialize,
  kRepair,
  kWrite,
  kReload,
  kReloadExtract,
  kVerify,
  kReport,
  kComplete
};
enum class GltfWorkflowStatus {
  kSuccess,
  kInvalidConfiguration,
  kLimitExceeded,
  kUnsupported,
  kBudgetExceeded,
  kCanceled,
  kRejected,
  kDelegatedFailure,
  kSourceContractFailure,
  kVerificationFailure
};
enum class GltfWorkflowDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kInputIdentity,
  kCountLimit,
  kRequestBudget,
  kStopRequested,
  kPrimitiveSelection,
  kSourceAccounting,
  kMaterializationAllowance,
  kRead,
  kExtract,
  kMaterialize,
  kRepair,
  kWrite,
  kReload,
  kReloadExtract,
  kSourceChanged,
  kReloadMismatch,
  kPrimitiveIdentityMismatch,
  kTopologyMismatch,
  kChannelDescriptorMismatch,
  kChannelBitsMismatch,
  kResourceBytesMismatch,
  kRetainedJsonMismatch,
  kBlockedByEarlierStage,
  kSolidCheckUnavailable,
  kReportExtent,
  kReportStringEncoding
};
using GltfWorkflowDelegatedDiagnostic =
    std::variant<std::monostate, interchange::GltfDiagnostic,
                 interchange::GltfMaterializationDiagnostic,
                 MaterializedDuplicateDiagnostic,
                 interchange::GltfTopologyWriteDiagnostic,
                 interchange::GltfTopologyOutputInspectionDiagnostic>;
using GltfWorkflowDelegatedStatus =
    std::variant<std::monostate, interchange::GltfStatus,
                 interchange::GltfMaterializationStatus,
                 MaterializedDuplicateStatus,
                 interchange::GltfTopologyWriteStatus,
                 interchange::GltfTopologyOutputInspectionStatus>;
struct GltfWorkflowDiagnostic {
  GltfWorkflowStage stage = GltfWorkflowStage::kConfiguration;
  GltfWorkflowDiagnosticCode code = GltfWorkflowDiagnosticCode::kNone;
  // Original ordinals; names and caller bytes are never borrowed here.
  std::optional<geometry::index_t> attribute;
  std::optional<geometry::index_t> element;
  std::optional<interchange::GltfResourceKind> resource_kind;
  std::optional<std::uint64_t> resource_index;
  std::optional<interchange::GltfSourceSpan> json_span;
  // Exact original delegated diagnostic, including successful storage
  // inspection with issues; no fabricated lower-level status.
  GltfWorkflowDelegatedDiagnostic delegated;
  GltfWorkflowDelegatedStatus delegated_status;
};
enum class GltfWorkflowExecutionOutcome { kCompleted, kFailed, kCanceled };
enum class GltfWorkflowProfileOutcome { kPassed, kFailed, kIncomplete };
enum class GltfWorkflowCoverageState {
  kPerformed,
  kBlocked,
  kUnsupported,
  kFailed
};
enum class GltfWorkflowCheck {
  kSourceStorage,
  kTargetPolicy,
  kCandidateStorage,
  kRepairCorrespondence,
  kCanonicalPreservation,
  kReloadSelectedPrimitive,
  kRetainedAssetPreservation,
  kSourceUnchanged,
  kSolidValidity,
  kTranslationPreservation,
  kDeformationSourceAdmission = 10,
  kDeformationInfluences = 11,
  kDeformationBindings = 12,
  kDeformationMorphs = 13,
  kDeformationAnimation = 14,
  kDeformationAssociation = 15,
  kDeformationOutputReload = 16,
  kDeformationStagedReload = 17
};
// Inline actual-return observations, not successful-owner or report
// projections. Static processing keeps every slot empty. A selected helper
// remains empty until it actually returns, including configuration/allowance
// failures before entry. Copies allocate nothing and retain facts after later
// report refusal.
struct GltfWorkflowTranslationFacts {
  interchange::GltfProcessingProfile profile =
      interchange::GltfProcessingProfile::kStaticLocalTrianglesV1;
  std::optional<interchange::GltfDirectTranslationObservation> source;
  std::optional<interchange::GltfDirectTranslationObservation> writing;
  std::optional<interchange::GltfDirectTranslationObservation> reload;
  std::optional<interchange::GltfDirectTranslationObservation> verification;
  std::optional<interchange::GltfDirectTranslationObservation>
      publication_reload;
  std::optional<interchange::GltfDirectTranslationObservation>
      publication_verification;
};
struct GltfWorkflowCoverage {
  GltfWorkflowCheck check = GltfWorkflowCheck::kSourceStorage;
  GltfWorkflowCoverageState state = GltfWorkflowCoverageState::kBlocked;
  // Present only for performed checks; observed completion alone is not pass.
  std::optional<bool> passed;
  GltfWorkflowDiagnosticCode reason = GltfWorkflowDiagnosticCode::kNone;
};
// Geometry report/1's exit meanings, not process exit or filesystem effects.
enum class GltfWorkflowExitMeaning {
  kSuccess = 0,
  kRejectedOrIncomplete = 1,
  kExecutionFailure = 2,
  kCanceled = 130
};
struct GltfWorkflowOutcomes {
  GltfWorkflowExecutionOutcome execution =
      GltfWorkflowExecutionOutcome::kFailed;
  // Empty means not-attempted. Acceptance survives a later export failure.
  std::optional<CandidateOutcome> candidate;
  GltfWorkflowProfileOutcome profile = GltfWorkflowProfileOutcome::kIncomplete;
  GltfWorkflowStage stage = GltfWorkflowStage::kConfiguration;
  GltfWorkflowExitMeaning exit = GltfWorkflowExitMeaning::kExecutionFailure;
};
struct GltfWorkflowByteExecutionStats {
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t caller_jobs = 0;
};
struct GltfWorkflowExecutionStats {
  interchange::GltfMaterializationExecution materialization;
  MaterializedDuplicateExecution repair;
  interchange::GltfTopologyWriteExecution writing;
  interchange::GltfTopologyOutputInspectionExecution verification;
  GltfWorkflowByteExecutionStats source_protection;
  GltfWorkflowByteExecutionStats report_copy;
};
// Current live fresh charges, not cumulative traffic, peak or runtime RSS.
// Shared blocks are counted once; delegated component totals are not added
// again to workflow or verification. Missing/failed owners contribute zero.
struct GltfWorkflowRequestedBytes {
  std::uint64_t workflow = 0;
  std::uint64_t read = 0;
  std::uint64_t extraction = 0;
  interchange::GltfMaterializationRequestedBytes materialization;
  std::uint64_t repair = 0;
  std::uint64_t writing = 0;
  std::uint64_t reload = 0;
  std::uint64_t reload_extraction = 0;
  std::uint64_t verification = 0;
  std::uint64_t total = 0;
};

// Copy-only immutable record. Strong owners, report bytes and charges
// survive caller/input/context destruction. Copies share without allocation.
// Accessor views require an alive, unreassigned visible record copy. Separate
// copies support concurrent reads; assigning the same copy needs external sync.
class GltfDuplicateWorkflowRecord {
 public:
  GltfDuplicateWorkflowRecord(const GltfDuplicateWorkflowRecord&) noexcept;
  GltfDuplicateWorkflowRecord& operator=(
      const GltfDuplicateWorkflowRecord&) noexcept;
  ~GltfDuplicateWorkflowRecord() noexcept;
  [[nodiscard]] GltfWorkflowOutcomes Outcomes() const noexcept;
  [[nodiscard]] std::span<const GltfWorkflowCoverage> Coverage() const noexcept;
  [[nodiscard]] std::span<const std::byte> ReportJson() const noexcept;
  [[nodiscard]] const std::optional<interchange::GltfAsset>& ReadAsset()
      const noexcept;
  [[nodiscard]] const std::optional<interchange::GltfPrimitiveExtraction>&
  ExtractedPrimitive() const noexcept;
  [[nodiscard]] const std::optional<interchange::GltfMaterializedPrimitive>&
  Source() const noexcept;
  [[nodiscard]] const std::optional<MaterializedDuplicateRepair>& Repair()
      const noexcept;
  // Present only after independent reload and complete preservation pass.
  [[nodiscard]] const std::optional<interchange::GltfTopologyWriteBundle>&
  Output() const noexcept;
  [[nodiscard]] const std::optional<interchange::GltfAsset>& ReloadedAsset()
      const noexcept;
  [[nodiscard]] const std::optional<interchange::GltfPrimitiveExtraction>&
  ReloadedPrimitive() const noexcept;
  // Applied reload policy, resolved from verified identity/changed selection.
  // Absent before resolution; extraction/inspection verify the actual mode.
  [[nodiscard]] std::optional<interchange::GltfExtractionRequest>
  ReloadExtractionPolicy() const noexcept;
  [[nodiscard]] const std::optional<interchange::GltfTopologyOutputInspection>&
  OutputInspection() const noexcept;
  [[nodiscard]] GltfWorkflowRequestedBytes NewRequestedBytes() const noexcept;
  // Borrows this alive, unreassigned record; no validation or allocation.
  [[nodiscard]] const GltfWorkflowTranslationFacts& TranslationFacts()
      const noexcept;
  // Null outside retained deformation processing. Borrows this alive,
  // unreassigned record; no validation, allocation or numerical work.
  [[nodiscard]] const GltfWorkflowDeformationFacts* DeformationFacts()
      const noexcept;

 private:
  friend struct gltf_workflow_detail::Access;
  explicit GltfDuplicateWorkflowRecord(
      std::shared_ptr<const gltf_workflow_detail::WorkflowState>
          state) noexcept;
  std::shared_ptr<const gltf_workflow_detail::WorkflowState> state_;
};
struct GltfDuplicateWorkflowResult {
  GltfWorkflowStatus status = GltfWorkflowStatus::kInvalidConfiguration;
  // Available even when report admission fails; never predicts future stages.
  GltfWorkflowOutcomes outcomes;
  // Absent for invalid configuration, insufficient report admission or
  // cancellation before any report can be admitted. No emergency OOM report.
  std::optional<GltfDuplicateWorkflowRecord> record;
  GltfWorkflowDiagnostic diagnostic;
  GltfWorkflowExecutionStats execution;
  // Opt-in completed facts only when record is absent. Shared immutable owners
  // retain their source, candidate, maps and original ledger without
  // allocation. This is not a report or proof of later workflow/publication
  // completion.
  std::optional<MaterializedDuplicateRepair> retained_repair;
  GltfWorkflowTranslationFacts translation;
  std::optional<GltfWorkflowDeformationFacts> deformation{};
};

// Supplied-memory pipeline: read -> extract -> materialize -> repair ->
// checked write -> independent public reload/verification -> report/1.
// No filesystem publication. Uses one retained ledger and this fresh token.
// Caps cover fresh explicit requests attributed by each owner, including the
// wrapper's accounting state, scratch/report buffers and native worker records,
// handles and exception slots. Opaque OS/CRT thread internals follow the owning
// execution exclusions; unrelated caller allocations and existing input views
// are outside this operation's fresh-request totals.
// Expected failures return typed observations; unexpected runtime/allocation
// exceptions propagate after rollback and joining every started worker.
[[nodiscard]] GltfDuplicateWorkflowResult RunGltfDuplicateWorkflow(
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop = {});

}  // namespace meshvale::repair
#endif  // MESHVALE_REPAIR_GLTF_WORKFLOW_H_
