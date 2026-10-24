// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_OFF_WORKFLOW_H_
#define MESHVALE_REPAIR_OFF_WORKFLOW_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
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
namespace off_workflow_detail {
struct State;
struct Access;
}  // namespace off_workflow_detail

struct OffWorkflowPublication {
  const std::filesystem::path& destination;
  std::string_view receipt_basename;
};
// Every borrowed owner/descriptor remains readable and unchanged through
// synchronous return, provider import and all worker joins. No source reread.
struct OffDuplicateFileWorkflowRequest {
  const interchange::OffFileAsset& source;
  std::string_view input_id;
  std::span<const DuplicateTarget> targets;
  interchange::OffTextPolicy text = interchange::OffTextPolicy::kInvalid;
  interchange::OffEdgeCountPolicy edge_count =
      interchange::OffEdgeCountPolicy::kInvalid;
  std::optional<OffWorkflowPublication> publication;
};
struct OffDuplicateFileWorkflowLimits {
  std::uint64_t max_wrapper_new_requested_bytes = 0;
  std::uint64_t max_input_id_bytes = 0;
  std::uint64_t max_source_protection_bytes = 0;
  std::uint64_t max_report_bytes = 0;
  std::uint64_t max_hash_input_bytes = 0;
  std::uint64_t max_verification_vertices = 0;
  std::uint64_t max_verification_faces = 0;
  std::uint64_t max_verification_corners = 0;
  MaterializedDuplicateLimits repair;
  interchange::OffWriteLimits writing;
  interchange::OffReadLimits reload;
  interchange::OffFilePublicationLimits publication;
};
struct OffByteWorkPolicy {
  std::uint64_t parallel_min_bytes = 0;
  std::uint64_t partition_bytes = 0;
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t max_jobs = 0;
};
struct OffDuplicateFileWorkflowWorkPolicy {
  geometry::MeshMaterializationWorkPolicy repair;
  interchange::OffWorkPolicy writing;
  interchange::OffWorkPolicy reload;
  OffByteWorkPolicy verification;
  OffByteWorkPolicy hashing;
  OffByteWorkPolicy report;
  interchange::OffFilePublicationWorkPolicy publication;
};
enum class OffWorkflowStage {
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
enum class OffWorkflowStatus {
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
enum class OffWorkflowDiagnosticCode {
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
  kSolidUnavailable
};
using OffWorkflowDelegatedStatus =
    std::variant<std::monostate, MaterializedDuplicateStatus,
                 interchange::OffStatus>;
using OffWorkflowDelegatedDiagnostic =
    std::variant<std::monostate, MaterializedDuplicateDiagnostic,
                 interchange::OffDiagnostic>;
struct OffWorkflowDiagnostic {
  OffWorkflowStage stage = OffWorkflowStage::kConfiguration;
  OffWorkflowDiagnosticCode code = OffWorkflowDiagnosticCode::kNone;
  std::optional<geometry::index_t> element;
  OffWorkflowDelegatedStatus delegated_status;
  OffWorkflowDelegatedDiagnostic delegated;
};
enum class OffWorkflowExecutionOutcome { kCompleted, kFailed, kCanceled };
enum class OffWorkflowProfileOutcome { kPassed, kFailed, kIncomplete };
enum class OffWorkflowPublicationOutcome {
  kNotRequested,
  kNotAttempted,
  kFailed,
  kCanceled,
  kPublished
};
enum class OffWorkflowExitMeaning {
  kSuccess = 0,
  kRejectedOrIncomplete = 1,
  kExecutionFailure = 2,
  kCanceled = 130
};
struct OffWorkflowOutcomes {
  OffWorkflowExecutionOutcome execution = OffWorkflowExecutionOutcome::kFailed;
  std::optional<CandidateOutcome> candidate;
  OffWorkflowProfileOutcome profile = OffWorkflowProfileOutcome::kIncomplete;
  OffWorkflowPublicationOutcome publication =
      OffWorkflowPublicationOutcome::kNotRequested;
  OffWorkflowStage stage = OffWorkflowStage::kConfiguration;
  OffWorkflowExitMeaning exit = OffWorkflowExitMeaning::kExecutionFailure;
};
enum class OffWorkflowCheck {
  kSourceStorage,
  kTargetPolicy,
  kCandidateStorage,
  kRepairCorrespondence,
  kCanonicalPreservation,
  kReloadCandidate,
  kSourceUnchanged,
  kStagedCandidate,
  kStagedPhysicalMembers,
  kSolidValidity
};
enum class OffWorkflowCoverageState {
  kPerformed,
  kSkipped,
  kUnsupported,
  kFailed
};
struct OffWorkflowCoverage {
  OffWorkflowCheck check = OffWorkflowCheck::kSourceStorage;
  OffWorkflowCoverageState state = OffWorkflowCoverageState::kSkipped;
  std::optional<bool> passed;
  OffWorkflowDiagnosticCode reason = OffWorkflowDiagnosticCode::kBlocked;
};
struct OffWorkflowExecution {
  MaterializedDuplicateExecution repair;
  interchange::OffExecution writing;
  interchange::OffExecution reload;
  geometry::MaterializationExecutionStats source_protection;
  geometry::MaterializationExecutionStats repair_correspondence;
  geometry::MaterializationExecutionStats canonical_preservation;
  geometry::MaterializationExecutionStats reload_verification;
  geometry::MaterializationExecutionStats source_verification;
  geometry::MaterializationExecutionStats staged_candidate;
  // One serial SHA stream per actual input; launched/joined workers stay zero.
  geometry::MaterializationExecutionStats hashing;
  geometry::MaterializationExecutionStats report_copy;
};
// Live disjoint attributed scopes, not a summed peak/RSS promise. Source mesh
// is already included in source_off and is never added through Repair again.
struct OffWorkflowRequestedBytes {
  std::uint64_t wrapper = 0;
  std::uint64_t source_files = 0;
  interchange::OffRequestedBytes source_off;
  MaterializedDuplicateRequestedBytes repair;
  interchange::OffRequestedBytes writing;
  interchange::OffRequestedBytes reload;
  std::uint64_t publication_files = 0;
  interchange::OffRequestedBytes publication_off;
};
// Copy-only immutable record. Copies allocate nothing; original backend state
// remains alive until the last free. Views borrow a live unreassigned copy.
// Separate copies support concurrent reads; reassignment needs synchronization.
class OffDuplicateFileWorkflowRecord {
 public:
  OffDuplicateFileWorkflowRecord(
      const OffDuplicateFileWorkflowRecord&) noexcept;
  OffDuplicateFileWorkflowRecord& operator=(
      const OffDuplicateFileWorkflowRecord&) noexcept;
  ~OffDuplicateFileWorkflowRecord() noexcept;
  [[nodiscard]] const interchange::OffFileAsset& Source() const noexcept;
  [[nodiscard]] const MaterializedDuplicateRepair* RepairFacts() const noexcept;
  // Present only after all required memory verification has passed.
  [[nodiscard]] const std::optional<interchange::OffOutput>& Output()
      const noexcept;
  [[nodiscard]] const std::optional<interchange::OffDocument>& Reload()
      const noexcept;
  [[nodiscard]] const std::optional<interchange::OffPublishedBundle>&
  Published() const noexcept;
  [[nodiscard]] OffWorkflowOutcomes Outcomes() const noexcept;
  [[nodiscard]] std::span<const OffWorkflowCoverage> Coverage() const noexcept;
  // Empty on report refusal. Conditional precommit bytes never escape failure.
  [[nodiscard]] std::span<const std::byte> ReportJson() const noexcept;
  [[nodiscard]] std::optional<std::string_view> PublishedReportPath()
      const noexcept;
  [[nodiscard]] std::optional<std::string_view> InputSha256() const noexcept;
  [[nodiscard]] OffWorkflowRequestedBytes NewRequestedBytes() const noexcept;
  [[nodiscard]] OffWorkflowExecution Execution() const noexcept;

 private:
  friend struct off_workflow_detail::Access;
  explicit OffDuplicateFileWorkflowRecord(
      std::shared_ptr<const off_workflow_detail::State>) noexcept;
  std::shared_ptr<const off_workflow_detail::State> state_;
};
struct OffDuplicateFileWorkflowResult {
  OffWorkflowStatus status = OffWorkflowStatus::kInvalidConfiguration;
  OffWorkflowOutcomes outcomes;
  OffWorkflowDiagnostic diagnostic;
  // Secondary delivery observation never replaces primary engine/file failure.
  std::optional<OffWorkflowDiagnostic> report_diagnostic;
  std::optional<OffDuplicateFileWorkflowRecord> record;
  std::optional<interchange::OffFilePublicationResult> publication;
  OffWorkflowExecution execution;
};
// All fields are required/positive, including nested branches not executed.
// Derives the source's strong ledger, invokes the existing engine exactly once,
// and retains completed facts before any later fallible work. Unexpected
// allocation/native exceptions propagate after joins and publisher cleanup.
// No postcommit allocation, external callback, hash, patch or mandatory reread.
[[nodiscard]] OffDuplicateFileWorkflowResult RunOffDuplicateFileWorkflow(
    const OffDuplicateFileWorkflowRequest& request,
    const OffDuplicateFileWorkflowLimits& limits,
    const OffDuplicateFileWorkflowWorkPolicy& work, std::stop_token stop = {});
}  // namespace meshvale::repair
#endif  // MESHVALE_REPAIR_OFF_WORKFLOW_H_
