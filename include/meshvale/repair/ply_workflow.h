// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PLY_WORKFLOW_H_
#define MESHVALE_REPAIR_PLY_WORKFLOW_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <variant>

namespace meshvale::repair {
namespace ply_workflow_detail {
struct State;
struct Access;
}  // namespace ply_workflow_detail

struct PlyWorkflowPublication {
  const std::filesystem::path& destination;
  std::string_view receipt_basename;
};
// Every borrowed owner/descriptor remains readable and unchanged through
// synchronous return, provider import and all worker joins. No source reread.
struct PlyDuplicateFileWorkflowRequest {
  const interchange::PlyFileAsset& source;
  std::string_view input_id;
  std::span<const DuplicateTarget> targets;
  interchange::PlyWriteOptions writing;
  std::span<const RowLocalAttributeDeclaration> row_local_attributes;
  std::optional<PlyWorkflowPublication> publication;
};
struct PlyDuplicateFileWorkflowLimits {
  std::uint64_t max_wrapper_new_requested_bytes = 0;
  std::uint64_t max_input_id_bytes = 0;
  std::uint64_t max_source_protection_bytes = 0;
  std::uint64_t max_report_bytes = 0;
  std::uint64_t max_hash_input_bytes = 0;
  std::uint64_t max_verification_vertices = 0;
  std::uint64_t max_verification_faces = 0;
  std::uint64_t max_verification_corners = 0;
  MaterializedDuplicateLimits repair;
  interchange::PlyWriteLimits writing;
  interchange::PlyReadLimits reload;
  interchange::PlyFilePublicationLimits publication;
};
struct PlyByteWorkPolicy {
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct PlyDuplicateFileWorkflowWorkPolicy {
  geometry::MeshMaterializationWorkPolicy repair;
  interchange::PlyWorkPolicy writing;
  interchange::PlyWorkPolicy reload;
  PlyByteWorkPolicy verification;
  PlyByteWorkPolicy hashing;
  PlyByteWorkPolicy report;
  interchange::PlyFilePublicationWorkPolicy publication;
};
enum class PlyWorkflowStage {
  kConfiguration,
  kSourceProtection,
  kRepair,
  kWrite,
  kReload,
  kVerify,
  kHash,
  kReport,
  kPublication,
  kComplete
};
enum class PlyWorkflowStatus {
  kSuccess,
  kInvalidConfiguration,
  kInvalidRequest,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled,
  kRejected,
  kDelegatedFailure,
  kVerificationFailure
};
enum class PlyWorkflowDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kInputIdentity,
  kPolicy,
  kNameUnsupported,
  kNameCollision,
  kCountLimit,
  kRequestBudget,
  kStopRequested,
  kRepair,
  kWrite,
  kReload,
  kCorrespondence,
  kCanonicalPreservation,
  kReloadMismatch,
  kSourceChanged,
  kHashExtent,
  kReportExtent,
  kReportStringEncoding,
  kPublication,
  kBlocked,
  kSolidUnavailable,
  kDeclaration,
  kTypedProperties,
  kWireSchema
};
using PlyWorkflowDelegatedStatus =
    std::variant<std::monostate, MaterializedDuplicateStatus,
                 interchange::PlyStatus>;
using PlyWorkflowDelegatedDiagnostic =
    std::variant<std::monostate, MaterializedDuplicateDiagnostic,
                 interchange::PlyDiagnostic>;
struct PlyWorkflowDiagnostic {
  PlyWorkflowStage stage = PlyWorkflowStage::kConfiguration;
  PlyWorkflowDiagnosticCode code = PlyWorkflowDiagnosticCode::kNone;
  std::optional<geometry::index_t> element;
  PlyWorkflowDelegatedStatus delegated_status;
  PlyWorkflowDelegatedDiagnostic delegated;
};
enum class PlyWorkflowExecutionOutcome { kCompleted, kFailed, kCanceled };
enum class PlyWorkflowProfileOutcome { kPassed, kFailed, kIncomplete };
enum class PlyWorkflowPublicationOutcome {
  kNotRequested,
  kNotAttempted,
  kFailed,
  kCanceled,
  kPublished
};
enum class PlyWorkflowExitMeaning {
  kSuccess = 0,
  kRejectedOrIncomplete = 1,
  kExecutionFailure = 2,
  kCanceled = 130
};
struct PlyWorkflowOutcomes {
  PlyWorkflowExecutionOutcome execution = PlyWorkflowExecutionOutcome::kFailed;
  std::optional<CandidateOutcome> candidate;
  PlyWorkflowProfileOutcome profile = PlyWorkflowProfileOutcome::kIncomplete;
  PlyWorkflowPublicationOutcome publication =
      PlyWorkflowPublicationOutcome::kNotRequested;
  PlyWorkflowStage stage = PlyWorkflowStage::kConfiguration;
  PlyWorkflowExitMeaning exit = PlyWorkflowExitMeaning::kExecutionFailure;
};
enum class PlyWorkflowCheck {
  kSourceStorage,
  kTargetPolicy,
  kCandidateStorage,
  kRepairCorrespondence,
  kCanonicalPreservation,
  kReloadCandidate,
  kSourceUnchanged,
  kTypedProperties,
  kWireSchema,
  kStagedCandidate,
  kStagedPhysicalMembers,
  kSolidValidity
};
enum class PlyWorkflowCoverageState {
  kPerformed,
  kSkipped,
  kUnsupported,
  kFailed
};
struct PlyWorkflowCoverage {
  PlyWorkflowCheck check = PlyWorkflowCheck::kSourceStorage;
  PlyWorkflowCoverageState state = PlyWorkflowCoverageState::kSkipped;
  std::optional<bool> passed;
  PlyWorkflowDiagnosticCode reason = PlyWorkflowDiagnosticCode::kBlocked;
};
struct PlyWorkflowExecution {
  MaterializedDuplicateExecution repair;
  interchange::PlyExecution writing;
  interchange::PlyExecution reload;
  geometry::MaterializationExecutionStats source_protection;
  geometry::MaterializationExecutionStats repair_correspondence;
  geometry::MaterializationExecutionStats canonical_preservation;
  geometry::MaterializationExecutionStats typed_properties;
  geometry::MaterializationExecutionStats wire_schema;
  geometry::MaterializationExecutionStats reload_verification;
  geometry::MaterializationExecutionStats source_verification;
  geometry::MaterializationExecutionStats staged_candidate;
  // One serial SHA stream per actual input; launched/joined workers stay zero.
  geometry::MaterializationExecutionStats hashing;
  geometry::MaterializationExecutionStats report_copy;
};
// Live disjoint attributed scopes, not a summed peak/RSS promise. Source mesh
// is already included in source_ply and is never added through Repair again.
struct PlyWorkflowRequestedBytes {
  std::uint64_t wrapper = 0;
  std::uint64_t source_files = 0;
  interchange::PlyRequestedBytes source_ply;
  MaterializedDuplicateRequestedBytes repair;
  interchange::PlyRequestedBytes writing;
  interchange::PlyRequestedBytes reload;
  std::uint64_t publication_files = 0;
  interchange::PlyRequestedBytes publication_ply;
};
// Copy-only immutable record. Copies allocate nothing; original backend state
// remains alive until the last free. Views borrow a live unreassigned copy.
// Separate copies support concurrent reads; reassignment needs synchronization.
class PlyDuplicateFileWorkflowRecord {
 public:
  PlyDuplicateFileWorkflowRecord(
      const PlyDuplicateFileWorkflowRecord&) noexcept;
  PlyDuplicateFileWorkflowRecord& operator=(
      const PlyDuplicateFileWorkflowRecord&) noexcept;
  ~PlyDuplicateFileWorkflowRecord() noexcept;
  [[nodiscard]] const interchange::PlyFileAsset& Source() const noexcept;
  [[nodiscard]] const MaterializedDuplicateRepair* RepairFacts() const noexcept;
  // Actual completed writer facts, even if later reload/verification fails.
  // Borrows this live record; its presence does not verify output bytes.
  [[nodiscard]] const interchange::PlyWriteFacts* WriteFacts() const noexcept;
  // Present only after all required memory verification has passed.
  [[nodiscard]] const std::optional<interchange::PlyOutput>& Output()
      const noexcept;
  [[nodiscard]] const std::optional<interchange::PlyDocument>& Reload()
      const noexcept;
  [[nodiscard]] const std::optional<interchange::PlyPublishedBundle>&
  Published() const noexcept;
  [[nodiscard]] PlyWorkflowOutcomes Outcomes() const noexcept;
  [[nodiscard]] std::span<const PlyWorkflowCoverage> Coverage() const noexcept;
  // Empty on report refusal. Conditional precommit bytes never escape failure.
  [[nodiscard]] std::span<const std::byte> ReportJson() const noexcept;
  [[nodiscard]] std::optional<std::string_view> PublishedReportPath()
      const noexcept;
  [[nodiscard]] std::optional<std::string_view> InputSha256() const noexcept;
  [[nodiscard]] PlyWorkflowRequestedBytes NewRequestedBytes() const noexcept;
  [[nodiscard]] PlyWorkflowExecution Execution() const noexcept;

 private:
  friend struct ply_workflow_detail::Access;
  explicit PlyDuplicateFileWorkflowRecord(
      std::shared_ptr<const ply_workflow_detail::State>) noexcept;
  std::shared_ptr<const ply_workflow_detail::State> state_;
};
struct PlyDuplicateFileWorkflowResult {
  PlyWorkflowStatus status = PlyWorkflowStatus::kInvalidConfiguration;
  PlyWorkflowOutcomes outcomes;
  PlyWorkflowDiagnostic diagnostic;
  // Secondary delivery observation never replaces primary engine/file failure.
  std::optional<PlyWorkflowDiagnostic> report_diagnostic;
  std::optional<PlyDuplicateFileWorkflowRecord> record;
  std::optional<interchange::PlyFilePublicationResult> publication;
  PlyWorkflowExecution execution;
};
// All fields are required/positive, including nested branches not executed.
// Derives the source's strong ledger, invokes the existing engine exactly once,
// and retains completed facts before any later fallible work. Unexpected
// allocation/native exceptions propagate after joins and publisher cleanup.
// No postcommit allocation, external callback, hash, patch or mandatory reread.
[[nodiscard]] PlyDuplicateFileWorkflowResult RunPlyDuplicateFileWorkflow(
    const PlyDuplicateFileWorkflowRequest& request,
    const PlyDuplicateFileWorkflowLimits& limits,
    const PlyDuplicateFileWorkflowWorkPolicy& work, std::stop_token stop = {});
}  // namespace meshvale::repair
#endif  // MESHVALE_REPAIR_PLY_WORKFLOW_H_
