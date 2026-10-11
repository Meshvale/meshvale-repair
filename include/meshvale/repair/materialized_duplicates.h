// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_MATERIALIZED_DUPLICATES_H_
#define MESHVALE_REPAIR_MATERIALIZED_DUPLICATES_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

namespace meshvale::repair {
namespace materialized_duplicates_detail {
struct Access;
struct RepairState;
}  // namespace materialized_duplicates_detail

// Borrowed declaration: values do not refer to changed face/corner identities.
struct RowLocalAttributeDeclaration {
  geometry::AttributeDomain domain = geometry::AttributeDomain::vertex;
  std::string_view name;
};
struct MaterializedDuplicateOptions {
  std::span<const RowLocalAttributeDeclaration> row_local_attributes;
};

// Every count/cap and every nested Geometry field is required and positive.
// The aggregate cap includes concurrent live new blocks of every phase.
struct MaterializedDuplicateLimits {
  std::uint64_t max_targets = 0;
  std::uint64_t max_row_local_declarations = 0;
  std::uint64_t max_declaration_name_bytes = 0;
  // Inclusive checked worst-case logical comparison work for one target.
  std::uint64_t max_comparison_steps_per_target = 0;
  std::uint64_t max_new_requested_bytes = 0;
  geometry::MeshInspectionLimits inspection;
  geometry::MeshMaterializationLimits selection;
};

enum class MaterializedDuplicateStage {
  kConfiguration,
  kSourceInspection,
  kEligibility,
  kTargets,
  kComparison,
  kSelection,
  kCorrespondence,
  kCandidateInspection,
  kVerification,
  kPublication
};
enum class MaterializedDuplicateStatus {
  kSuccess,
  kInvalidConfiguration,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled,
  kInvalidSourceStorage,
  kRejected,
  kGeometryFailure,
  kVerificationFailure
};
enum class MaterializedDuplicateDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kDeclarationDomain,
  kDeclarationNameEmpty,
  kDeclarationDuplicate,
  kTargetLimit,
  kDeclarationLimit,
  kDeclarationNameByteLimit,
  kComparisonLimit,
  kRequestBudget,
  kStopRequested,
  kStorageIssues,
  kNonfiniteAttribute,
  kUnsupportedAttribute,
  kTargetRange,
  kSelfTarget,
  kRepeatedTarget,
  kRemovedRepresentative,
  kNotEquivalent,
  kGeometry,
  kPostcondition
};
struct MaterializedDuplicateDiagnostic {
  MaterializedDuplicateStage stage = MaterializedDuplicateStage::kConfiguration;
  MaterializedDuplicateDiagnosticCode code =
      MaterializedDuplicateDiagnosticCode::kNone;
  // Original request/attribute/source element ordinals; never a borrowed name.
  std::optional<geometry::index_t> target;
  std::optional<geometry::index_t> attribute;
  std::optional<geometry::index_t> element;
  // Genuine delegated result; successful inspection with issues retains
  // kSuccess and its diagnostic, plus the first typed issue.
  std::optional<geometry::MaterializationStatus> geometry_status;
  std::optional<geometry::MaterializationDiagnostic> geometry_diagnostic;
  std::optional<geometry::StorageIssue> storage_issue;
};
struct MaterializedDuplicateExecution {
  geometry::MaterializationExecutionStats source_inspection;
  geometry::MaterializationExecutionStats eligibility;
  geometry::MaterializationExecutionStats comparison;
  geometry::MaterializationExecutionStats selection;
  geometry::MaterializationExecutionStats correspondence;
  geometry::MaterializationExecutionStats candidate_inspection;
  geometry::MaterializationExecutionStats verification;
  geometry::MaterializationExecutionStats directed_verification;
};
struct MaterializedDuplicateRequestedBytes {
  std::uint64_t repair = 0;
  std::uint64_t source_inspection = 0;
  std::uint64_t selection = 0;
  std::uint64_t candidate_inspection = 0;
  std::uint64_t total = 0;
};

// Implemented development candidate; complete profile, requested-block,
// sanitizer and installed qualification remain pending. Copy-only immutable
// owners share blocks and charges.
class MaterializedDuplicateRepair {
 public:
  MaterializedDuplicateRepair(const MaterializedDuplicateRepair&) noexcept;
  MaterializedDuplicateRepair& operator=(
      const MaterializedDuplicateRepair&) noexcept;
  ~MaterializedDuplicateRepair() noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& Source() const noexcept;
  [[nodiscard]] const geometry::MaterializedMesh& Candidate() const noexcept;
  [[nodiscard]] const geometry::MeshInspection& SourceInspection()
      const noexcept;
  [[nodiscard]] const geometry::MeshInspection& CandidateInspection()
      const noexcept;
  [[nodiscard]] CandidateOutcome Outcome() const noexcept;
  // Complete input element -> output element, including removed copies.
  [[nodiscard]] std::span<const geometry::index_t> FaceMap() const noexcept;
  [[nodiscard]] std::span<const geometry::index_t> CornerMap() const noexcept;
  // Complete output element -> retained input element from Geometry selection.
  [[nodiscard]] std::span<const geometry::index_t> SourceFaces() const noexcept;
  [[nodiscard]] std::span<const geometry::index_t> SourceCorners()
      const noexcept;
  [[nodiscard]] MaterializedDuplicateRequestedBytes NewRequestedBytes()
      const noexcept;

 private:
  friend struct materialized_duplicates_detail::Access;
  explicit MaterializedDuplicateRepair(
      std::shared_ptr<const materialized_duplicates_detail::RepairState>
          state) noexcept;
  std::shared_ptr<const materialized_duplicates_detail::RepairState> state_;
};
struct MaterializedDuplicateResult {
  MaterializedDuplicateStatus status =
      MaterializedDuplicateStatus::kInvalidConfiguration;
  std::optional<MaterializedDuplicateRepair> repair;
  MaterializedDuplicateDiagnostic diagnostic;
  MaterializedDuplicateExecution execution;
};

// Synchronous targeted exact repair over the source's retained execution ledger
// and a fresh token. Borrowed inputs remain readable and unchanged until
// return. Success publishes only after independent preservation verification.
// Actual runtime exceptions propagate after rollback and every launched worker
// joins.
[[nodiscard]] MaterializedDuplicateResult RemoveMaterializedDuplicateFaces(
    const geometry::MaterializedMesh& source,
    std::span<const DuplicateTarget> targets,
    const MaterializedDuplicateOptions& options,
    const MaterializedDuplicateLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work,
    std::stop_token stop = {});

}  // namespace meshvale::repair
#endif  // MESHVALE_REPAIR_MATERIALIZED_DUPLICATES_H_
