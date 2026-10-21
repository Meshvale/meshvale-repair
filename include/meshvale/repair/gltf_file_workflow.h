// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_GLTF_FILE_WORKFLOW_H_
#define MESHVALE_REPAIR_GLTF_FILE_WORKFLOW_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

namespace meshvale::repair {
namespace gltf_file_workflow_detail {
struct State;
struct Access;
}  // namespace gltf_file_workflow_detail

// Optional requested export. Paths/views are borrowed unchanged until return.
// Static files are publisher-admitted; report_path names the sole dynamic
// member.
struct GltfDuplicateFilePublication {
  const std::filesystem::path& destination;
  std::string_view report_path;
  std::span<const interchange::GltfFileView> supplemental_files;
};
struct GltfDuplicateFileWorkflowRequest {
  const interchange::GltfFileAsset& source;
  std::string_view input_id;
  std::optional<std::uint64_t> primitive_ordinal;
  interchange::GltfExtractionRequest extraction;
  std::span<const DuplicateTarget> targets;
  MaterializedDuplicateOptions repair;
  // Null means publication not requested. Borrowed through all joins/return.
  const GltfDuplicateFilePublication* publication = nullptr;
  GltfReportFailureRetention report_failure_retention =
      GltfReportFailureRetention::kRelease;
};

// Required positive fields. No initial read: exact source owner is retained.
// Complete concurrently-live fresh memory-engine cap includes charged source
// baselines and the memory suboperation report; excludes retained source
// owners.
struct GltfDuplicateFileMemoryLimits {
  std::uint64_t max_new_requested_bytes = 0;
  std::uint64_t max_input_id_bytes = 0;
  std::uint64_t max_source_protection_bytes = 0;
  std::uint64_t max_source_binding_records = 0;
  std::uint64_t max_reload_bindings = 0;
  std::uint64_t max_report_bytes = 0;
  interchange::GltfExtractionLimits extraction;
  // Checked sum of all three positive request caps fits remaining allowance;
  // pass unchanged, never clamp each independently to one remainder.
  interchange::GltfMaterializationLimits materialization;
  MaterializedDuplicateLimits repair;
  interchange::GltfTopologyWriteLimits writing;
  interchange::GltfReadOperationLimits reload;
  interchange::GltfExtractionLimits reload_extraction;
  interchange::GltfTopologyOutputInspectionLimits verification;
};
struct GltfDuplicateFileWorkflowLimits {
  GltfDuplicateFileMemoryLimits memory;
  // Complete fresh file-orchestration/hash/receipt/path/view/worker requests.
  // Separate from memory and publisher caps; not an end-to-end aggregate.
  std::uint64_t max_wrapper_new_requested_bytes = 0;
  std::uint64_t max_source_files = 0;
  std::uint64_t max_supplemental_files = 0;
  std::uint64_t max_report_path_bytes = 0;
  std::uint64_t max_report_bytes = 0;
  // Total bytes consumed by source-framed/artifact SHA streams, including
  // frames.
  std::uint64_t max_hash_input_bytes = 0;
  // Validate positive nested fields even when publication is not requested.
  interchange::GltfFilePublicationLimits publication;
};
struct GltfDuplicateFileWorkflowWorkPolicy {
  // Initial extraction uses the policy retained by source.Asset().
  GltfDuplicateWorkflowWorkPolicy memory;
  GltfWorkflowByteWorkPolicy hashing;
  GltfWorkflowByteWorkPolicy report;
  interchange::GltfFilePublicationWorkPolicy publication;
};

enum class GltfFileWorkflowStage {
  kConfiguration,
  kSource,
  kMemory,
  kHash,
  kReport,
  kPublication,
  kComplete
};
enum class GltfFileWorkflowPublicationOutcome {
  kNotRequested,
  kNotAttempted,
  kFailed,
  kCanceled,
  kPublished
};
enum class GltfFileWorkflowDiagnosticCode {
  kNone,
  kRequiredFieldZero,
  kConfigurationExtent,
  kSourceLedger,
  kSourceAccounting,
  kCountLimit,
  kRequestBudget,
  kStopRequested,
  kMemory,
  kHashInputExtent,
  kReportPreparation,
  kPublication
};
struct GltfFileWorkflowDiagnostic {
  GltfFileWorkflowStage stage = GltfFileWorkflowStage::kConfiguration;
  GltfFileWorkflowDiagnosticCode code = GltfFileWorkflowDiagnosticCode::kNone;
  // Exact reached-stage memory diagnostic, including its native stage/status.
  std::optional<GltfWorkflowDiagnostic> memory;
  // Actual publisher diagnostic. Cleanup never replaces its primary failure.
  std::optional<interchange::GltfFileDiagnostic> publisher;
};
struct GltfFileWorkflowOutcomes {
  GltfWorkflowExecutionOutcome execution =
      GltfWorkflowExecutionOutcome::kFailed;
  std::optional<CandidateOutcome> candidate;
  GltfWorkflowProfileOutcome profile = GltfWorkflowProfileOutcome::kIncomplete;
  GltfFileWorkflowPublicationOutcome publication =
      GltfFileWorkflowPublicationOutcome::kNotRequested;
  GltfFileWorkflowStage stage = GltfFileWorkflowStage::kConfiguration;
  GltfWorkflowExitMeaning exit = GltfWorkflowExitMeaning::kExecutionFailure;
};
struct GltfFileWorkflowByteExecution {
  // Actual wrapper jobs only; no delegated/import/write estimates.
  GltfWorkflowByteExecutionStats hashing;
  GltfWorkflowByteExecutionStats report_copy;
};
enum class GltfContainerCounterAvailability { kNotApplicable, kUnavailable };
struct GltfRetainedFileSourceRequestedBytes {
  std::uint64_t files = 0;
  std::optional<std::uint64_t> capped_reader;
  GltfContainerCounterAvailability container =
      GltfContainerCounterAvailability::kNotApplicable;
};
// Live fallback subset only. Source Geometry and Repair operation charges are
// disjoint; Repair already includes selection/candidate and both inspections.
struct GltfRetainedRepairRequestedBytes {
  std::uint64_t source_geometry = 0;
  MaterializedDuplicateRequestedBytes repair;
  std::uint64_t total = 0;
};
struct GltfFileWorkflowRequestedBytes {
  // Preexisting retained input requests: not fresh for this workflow.
  GltfRetainedFileSourceRequestedBytes retained_source;
  std::uint64_t wrapper = 0;
  // Includes each new memory owner exactly once; read is zero for retained
  // input.
  GltfWorkflowRequestedBytes memory;
  // File owner only; absent when no surviving published owner exists.
  std::optional<std::uint64_t> publication_files;
  // Present only without a memory-record owner. Zero memory attribution does
  // not imply zero payload when these completed facts remain alive.
  std::optional<GltfRetainedRepairRequestedBytes> retained_repair;
  // No overall total: container/composite delegated publication charges are
  // unavailable.
};

// Copy-only strong record, sharing without allocation. Caller source/options/
// context may die after return; backend state follows its original last free.
// Views borrow an alive unreassigned record. Concurrent reads through separate
// copies are supported; mutating/reassigning one copy needs synchronization.
class GltfDuplicateFileWorkflowRecord {
 public:
  GltfDuplicateFileWorkflowRecord(
      const GltfDuplicateFileWorkflowRecord&) noexcept;
  GltfDuplicateFileWorkflowRecord& operator=(
      const GltfDuplicateFileWorkflowRecord&) noexcept;
  ~GltfDuplicateFileWorkflowRecord() noexcept;
  [[nodiscard]] const interchange::GltfFileAsset& Source() const noexcept;
  // Same engine, retained-source entry. Its report is a memory suboperation
  // observation with publication not-requested, not the final file receipt.
  [[nodiscard]] const std::optional<GltfDuplicateWorkflowRecord>& Memory()
      const noexcept;
  // Exact inner outcomes, including when no memory report could be admitted.
  // Absent if the inner operation never returned.
  [[nodiscard]] const std::optional<GltfWorkflowOutcomes>& MemoryOutcomes()
      const noexcept;
  // Borrows this alive, unreassigned record. Normal memory Repair, otherwise
  // the opt-in completed fallback, otherwise null. The paths are exclusive.
  [[nodiscard]] const MaterializedDuplicateRepair* RepairFacts() const noexcept;
  [[nodiscard]] const std::optional<interchange::GltfPublishedBundle>&
  Published() const noexcept;
  [[nodiscard]] GltfFileWorkflowOutcomes Outcomes() const noexcept;
  // Actual byte-verified bundled report only on kPublished; otherwise genuine
  // no-export/failure bytes. Empty if report admission/cancellation prevented
  // it. Never exposes conditional draft success bytes after failed publication.
  [[nodiscard]] std::span<const std::byte> ReportJson() const noexcept;
  [[nodiscard]] std::optional<std::string_view> PublishedReportPath()
      const noexcept;
  [[nodiscard]] GltfFileWorkflowRequestedBytes NewRequestedBytes()
      const noexcept;
  [[nodiscard]] GltfFileWorkflowByteExecution Execution() const noexcept;

 private:
  friend struct gltf_file_workflow_detail::Access;
  explicit GltfDuplicateFileWorkflowRecord(
      std::shared_ptr<const gltf_file_workflow_detail::State>) noexcept;
  std::shared_ptr<const gltf_file_workflow_detail::State> state_;
};
struct GltfDuplicateFileWorkflowResult {
  GltfWorkflowStatus status = GltfWorkflowStatus::kInvalidConfiguration;
  GltfFileWorkflowOutcomes outcomes;
  std::optional<GltfDuplicateFileWorkflowRecord> record;
  GltfFileWorkflowDiagnostic diagnostic;
  // Actual publisher result including primary diagnostic, cleanup/error and
  // final capture/verification statistics. Separate from immutable receipt
  // bytes. Absent if publisher was never invoked; never synthesized from a stop
  // token.
  std::optional<interchange::GltfFilePublicationResult> publication;
  GltfFileWorkflowByteExecution execution;
};

// Development native retained-file interface; link
// meshvale::gltf_file_workflow. Configuration/count validation precedes
// source.Asset().SharesExecutionBudget; foreign/moved context returns
// kSourceContractFailure/kSource/kSourceLedger before initial stop or any fresh
// wrapper request. See the owning contract. Exact source.Asset() is reused by
// one existing Repair engine; never reread. All borrows remain alive/unchanged
// through return and every worker join. Expected failures preserve actual
// diagnostics; runtime/allocation exceptions propagate joined after best-effort
// cleanup without invented cleanup reports. Conditional final report/result
// storage is admitted before native commit; the same report bytes are verified
// dynamic bundle members. Only kPublished exposes them. No postcommit
// allocation/callback/hash/patch/mandatory reread.
[[nodiscard]] GltfDuplicateFileWorkflowResult RunGltfDuplicateFileWorkflow(
    const GltfDuplicateFileWorkflowRequest& request,
    const GltfDuplicateFileWorkflowLimits& limits,
    const GltfDuplicateFileWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop = {});

}  // namespace meshvale::repair
#endif  // MESHVALE_REPAIR_GLTF_FILE_WORKFLOW_H_
