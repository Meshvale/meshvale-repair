// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/gltf_workflow.h"

#include <meshvale/repair/materialized_duplicates.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_workflow_internal.h"
#include "gltf_workflow_test_internal.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::gltf_workflow_detail {
SourceBaselineBinding::SourceBaselineBinding(
    interchange::GltfResourceKind kind, std::uint64_t index,
    const allocation::AllocationContext& allocation)
    : kind(kind),
      index(index),
      bytes(allocation::Allocator<std::byte>(allocation)) {}
WorkflowState::WorkflowState(const allocation::AllocationContext& allocator)
    : allocation(allocator),
      baseline_json(allocation::Allocator<std::byte>(allocator)),
      baseline_bindings(
          allocation::Allocator<SourceBaselineBinding>(allocator)),
      baseline_views(
          allocation::Allocator<interchange::SuppliedGltfResource>(allocator)),
      report(allocation::Allocator<std::byte>(allocator)) {
  for (std::size_t i = 0; i < coverage.size(); ++i) {
    coverage[i].check = static_cast<GltfWorkflowCheck>(i);
    coverage[i].reason = GltfWorkflowDiagnosticCode::kBlockedByEarlierStage;
  }
  coverage.back().state = GltfWorkflowCoverageState::kUnsupported;
  coverage.back().reason = GltfWorkflowDiagnosticCode::kSolidCheckUnavailable;
}
GltfWorkflowRequestedBytes WorkflowState::Bytes() const noexcept {
  GltfWorkflowRequestedBytes bytes;
  bytes.workflow = allocation.OperationActiveBytes();
  if (read && !retained_read)
    bytes.read = read->NewRequestedBytes().value_or(0);
  if (extraction) bytes.extraction = extraction->NewRequestedBytes();
  if (source) bytes.materialization = source->NewRequestedBytes();
  if (repair) bytes.repair = repair->NewRequestedBytes().total;
  if (written) bytes.writing = written->NewRequestedBytes();
  if (reload) bytes.reload = reload->NewRequestedBytes().value_or(0);
  if (reload_extraction)
    bytes.reload_extraction = reload_extraction->NewRequestedBytes();
  if (inspection) bytes.verification = inspection->NewRequestedBytes();
  bytes.total =
      bytes.workflow + bytes.read + bytes.extraction +
      bytes.materialization.bridge + bytes.materialization.materialization +
      bytes.materialization.inspection + bytes.repair + bytes.writing +
      bytes.reload + bytes.reload_extraction + bytes.verification;
  return bytes;
}
void WorkflowState::UpdateDelegates() {
  const auto bytes = Bytes();
  allocation.SetDelegatedBytes(
      static_cast<std::size_t>(bytes.total - bytes.workflow));
}
}  // namespace meshvale::repair::gltf_workflow_detail

namespace meshvale::repair {
GltfDuplicateWorkflowRecord::GltfDuplicateWorkflowRecord(
    std::shared_ptr<const gltf_workflow_detail::WorkflowState> state) noexcept
    : state_(std::move(state)) {}
GltfDuplicateWorkflowRecord::GltfDuplicateWorkflowRecord(
    const GltfDuplicateWorkflowRecord&) noexcept = default;
GltfDuplicateWorkflowRecord& GltfDuplicateWorkflowRecord::operator=(
    const GltfDuplicateWorkflowRecord&) noexcept = default;
GltfDuplicateWorkflowRecord::~GltfDuplicateWorkflowRecord() noexcept = default;
GltfWorkflowOutcomes GltfDuplicateWorkflowRecord::Outcomes() const noexcept {
  return state_->outcomes;
}
std::span<const GltfWorkflowCoverage> GltfDuplicateWorkflowRecord::Coverage()
    const noexcept {
  return state_->coverage;
}
std::span<const std::byte> GltfDuplicateWorkflowRecord::ReportJson()
    const noexcept {
  return state_->report;
}
const std::optional<interchange::GltfAsset>&
GltfDuplicateWorkflowRecord::ReadAsset() const noexcept {
  return state_->read;
}
const std::optional<interchange::GltfPrimitiveExtraction>&
GltfDuplicateWorkflowRecord::ExtractedPrimitive() const noexcept {
  return state_->extraction;
}
const std::optional<interchange::GltfMaterializedPrimitive>&
GltfDuplicateWorkflowRecord::Source() const noexcept {
  return state_->source;
}
const std::optional<MaterializedDuplicateRepair>&
GltfDuplicateWorkflowRecord::Repair() const noexcept {
  return state_->repair;
}
const std::optional<interchange::GltfTopologyWriteBundle>&
GltfDuplicateWorkflowRecord::Output() const noexcept {
  return state_->output;
}
const std::optional<interchange::GltfAsset>&
GltfDuplicateWorkflowRecord::ReloadedAsset() const noexcept {
  return state_->reload;
}
const std::optional<interchange::GltfPrimitiveExtraction>&
GltfDuplicateWorkflowRecord::ReloadedPrimitive() const noexcept {
  return state_->reload_extraction;
}
std::optional<interchange::GltfExtractionRequest>
GltfDuplicateWorkflowRecord::ReloadExtractionPolicy() const noexcept {
  return state_->reload_policy;
}
const std::optional<interchange::GltfTopologyOutputInspection>&
GltfDuplicateWorkflowRecord::OutputInspection() const noexcept {
  return state_->inspection;
}
GltfWorkflowRequestedBytes GltfDuplicateWorkflowRecord::NewRequestedBytes()
    const noexcept {
  return state_->Bytes();
}
namespace {
namespace detail = gltf_workflow_detail;
namespace allocation = materialized_duplicates_detail;
namespace ic = interchange;
using Stage = GltfWorkflowStage;
using Status = GltfWorkflowStatus;
using Code = GltfWorkflowDiagnosticCode;
using Check = GltfWorkflowCheck;
using State = GltfWorkflowCoverageState;
struct ExpectedFailure {
  Status status;
  GltfWorkflowDiagnostic diagnostic;
};
[[noreturn]] void Fail(Status status, Stage stage, Code code) {
  GltfWorkflowDiagnostic diagnostic;
  diagnostic.stage = stage;
  diagnostic.code = code;
  throw ExpectedFailure{status, diagnostic};
}
void Positive(std::uint64_t value) {
  if (value == 0)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRequiredFieldZero);
  if (value > std::numeric_limits<std::size_t>::max())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const geometry::MeshMaterializationLimits& x) {
  for (auto n :
       {x.max_vertices, x.max_faces, x.max_corners, x.max_face_offsets,
        x.max_attributes, x.max_attribute_scalars, x.max_attribute_offsets,
        x.max_presence_bytes, x.max_metadata_entries, x.max_descriptor_bytes,
        x.max_new_requested_bytes})
    Positive(n);
}
void Validate(const geometry::MeshInspectionLimits& x) {
  Positive(x.max_issues);
  Positive(x.max_new_requested_bytes);
}
void Validate(const ic::GltfReadOperationLimits& x) {
  const auto& s = x.source;
  for (auto n :
       {x.max_new_requested_bytes, s.max_json_bytes, s.max_resource_bytes,
        s.max_owned_source_resource_bytes, s.max_document_nodes,
        s.max_document_depth, s.max_decoded_string_bytes,
        s.max_numeric_token_bytes, s.max_decimal_coefficient_digits,
        s.max_decimal_exponent_digits, s.max_table_rows, s.max_graph_edges,
        s.max_instances, s.max_accessor_elements, s.max_projection_bytes})
    Positive(n);
}
void Validate(const ic::GltfExtractionLimits& x) {
  Positive(x.max_components);
  Positive(x.max_triangle_corners);
  Positive(x.max_output_requested_bytes);
}
template <class T>
void ValidateByteWork(const T& x) {
  Positive(x.parallel_min_bytes);
  Positive(x.partition_bytes);
  Positive(x.cancellation_block_bytes);
  Positive(x.max_jobs);
}
void Validate(const GltfDuplicateWorkflowLimits& x,
              const GltfDuplicateWorkflowWorkPolicy& w, bool retained = false) {
  for (auto n : {x.max_new_requested_bytes, x.max_input_id_bytes,
                 x.max_reload_bindings, x.max_report_bytes})
    Positive(n);
  if (!retained) Validate(x.read);
  Validate(x.extraction);
  Validate(x.reload);
  Validate(x.reload_extraction);
  Positive(x.materialization.max_channels);
  Positive(x.materialization.max_bridge_new_requested_bytes);
  Validate(x.materialization.materialization);
  Validate(x.materialization.inspection);
  Validate(x.repair.inspection);
  Validate(x.repair.selection);
  for (auto n : {x.repair.max_targets, x.repair.max_row_local_declarations,
                 x.repair.max_declaration_name_bytes,
                 x.repair.max_comparison_steps_per_target,
                 x.repair.max_new_requested_bytes})
    Positive(n);
  const auto& t = x.writing;
  for (auto n :
       {t.max_source_json_bytes, t.max_source_json_nodes,
        t.max_source_json_depth, t.max_vertices, t.max_channels,
        t.max_attribute_scalars, t.max_descriptor_bytes, t.max_candidate_faces,
        t.max_candidate_corners, t.max_index_bytes, t.output.max_json_bytes,
        t.output.max_resources, t.output.max_resource_bytes,
        t.output.max_total_resource_bytes, t.output.max_output_requested_bytes})
    Positive(n);
  const auto& v = x.verification;
  for (auto n :
       {v.max_json_bytes, v.max_json_nodes, v.max_json_depth, v.max_resources,
        v.max_resource_bytes, v.max_vertices, v.max_channels,
        v.max_attribute_scalars, v.max_descriptor_bytes, v.max_candidate_faces,
        v.max_candidate_corners, v.max_index_bytes, v.max_new_requested_bytes})
    Positive(n);
  for (auto n : {w.interchange.parallel_min_resource_bytes,
                 w.interchange.resource_partition_bytes,
                 w.interchange.parallel_min_channel_components,
                 w.interchange.channel_partition_elements,
                 w.geometry.parallel_min_bytes, w.geometry.partition_bytes,
                 w.geometry.cancellation_block_scalars, w.geometry.max_jobs})
    Positive(n);
  ValidateByteWork(w.writing);
  ValidateByteWork(w.verification);
  ValidateByteWork(w.report);
  const auto bridge = x.materialization.max_bridge_new_requested_bytes;
  const auto capture =
      x.materialization.materialization.max_new_requested_bytes;
  const auto inspection = x.materialization.inspection.max_new_requested_bytes;
  const auto bound =
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
  if (capture > bound - bridge || inspection > bound - bridge - capture)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
bool AsciiAlphaNumeric(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}
bool ValidUtf8(std::string_view value) {
  constexpr std::uint32_t kContinuationMask = 0xc0;
  constexpr std::uint32_t kContinuationPrefix = 0x80;
  constexpr std::uint32_t kContinuationPayloadMask = 0x3f;
  constexpr std::size_t kContinuationPayloadBits = 6;
  constexpr std::uint32_t kUnicodeMaximum = 0x10ffff;
  constexpr std::uint32_t kSurrogateFirst = 0xd800;
  constexpr std::uint32_t kSurrogateLast = 0xdfff;
  struct Utf8LeadRange {
    unsigned char first;
    unsigned char last;
    std::size_t continuations;
    std::uint32_t minimum_scalar;
  };
  constexpr std::array kLeadRanges{Utf8LeadRange{0xc2, 0xdf, 1, 0x80},
                                   Utf8LeadRange{0xe0, 0xef, 2, 0x800},
                                   Utf8LeadRange{0xf0, 0xf4, 3, 0x10000}};
  for (std::size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i++]);
    if (first < kContinuationPrefix) continue;
    const auto range = std::find_if(
        kLeadRanges.begin(), kLeadRanges.end(), [first](const auto& range) {
          return first >= range.first && first <= range.last;
        });
    if (range == kLeadRanges.end() || range->continuations > value.size() - i)
      return false;
    std::uint32_t scalar =
        first & ((1u << (kContinuationPayloadBits - range->continuations)) - 1);
    for (std::size_t continuation = 0; continuation < range->continuations;
         ++continuation) {
      const auto byte = static_cast<unsigned char>(value[i++]);
      if ((byte & kContinuationMask) != kContinuationPrefix) return false;
      scalar = (scalar << kContinuationPayloadBits) |
               (byte & kContinuationPayloadMask);
    }
    if (scalar < range->minimum_scalar || scalar > kUnicodeMaximum ||
        (scalar >= kSurrogateFirst && scalar <= kSurrogateLast))
      return false;
  }
  return true;
}
void ValidateRequest(const GltfDuplicateWorkflowRequest& r,
                     const GltfDuplicateWorkflowLimits& l,
                     bool retained = false) {
  if (r.report_failure_retention != GltfReportFailureRetention::kRelease &&
      r.report_failure_retention !=
          GltfReportFailureRetention::kRetainCompletedRepair)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  if (!r.primitive_ordinal)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kPrimitiveSelection);
  if (r.input_id.empty() || r.input_id.size() > detail::kReportIdentityBytes ||
      r.input_id.size() > l.max_input_id_bytes ||
      !AsciiAlphaNumeric(r.input_id.front()))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kInputIdentity);
  for (char c : r.input_id)
    if (!AsciiAlphaNumeric(c) && c != '_' && c != '.' && c != ':' && c != '-')
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kInputIdentity);
  if ((r.extraction.triangles != ic::GltfTriangleRequest::kRequireTriangles &&
       r.extraction.triangles != ic::GltfTriangleRequest::kConvertStripOrFan) ||
      (r.extraction.values != ic::GltfValueRequest::kRaw &&
       r.extraction.values != ic::GltfValueRequest::kRawAndNormalized))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration, Code::kExtract);
  if ((!retained && r.json.size() > l.read.source.max_json_bytes) ||
      r.targets.size() > l.repair.max_targets ||
      r.repair.row_local_attributes.size() >
          l.repair.max_row_local_declarations)
    Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
  // Check all borrowed descriptor extents before traversing any names/bytes.
  const auto host = std::numeric_limits<std::size_t>::max();
  if (r.resources.size() > host / sizeof(ic::SuppliedGltfResource) ||
      r.targets.size() > host / sizeof(DuplicateTarget) ||
      r.repair.row_local_attributes.size() >
          host / sizeof(RowLocalAttributeDeclaration))
    Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
  // Buffers and images each obey the reader's table-row bound. Avoid an
  // overflowing doubled limit while bounding the entire borrowed inventory.
  if (!retained && r.resources.size() > l.read.source.max_table_rows &&
      r.resources.size() - l.read.source.max_table_rows >
          l.read.source.max_table_rows)
    Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
  std::uint64_t resource_bytes = 0, name_bytes = 0;
  for (const auto& resource : r.resources) {
    if (retained) break;
    if (resource.bytes.size() > l.read.source.max_resource_bytes ||
        resource.bytes.size() >
            l.read.source.max_owned_source_resource_bytes - resource_bytes)
      Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
    resource_bytes += resource.bytes.size();
  }
  for (const auto& declaration : r.repair.row_local_attributes) {
    if (declaration.name.size() >
        l.repair.max_declaration_name_bytes - name_bytes)
      Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
    name_bytes += declaration.name.size();
  }
  for (const auto& declaration : r.repair.row_local_attributes)
    if (!ValidUtf8(declaration.name))
      Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
           Code::kReportStringEncoding);
}
void Observe(detail::WorkflowState& s, Check check, bool passed) {
  auto& c = s.coverage[static_cast<std::size_t>(check)];
  c.state = State::kPerformed;
  c.passed = passed;
  c.reason = Code::kNone;
}
void ComputeOutcomes(detail::WorkflowState& s, Status status) {
  bool incomplete = false, failed = false;
  for (std::size_t i = 0; i < detail::kRequiredCheckCount; ++i) {
    const auto& c = s.coverage[i];
    failed |= c.state == State::kPerformed && c.passed == false;
    incomplete |= c.state != State::kPerformed;
  }
  s.outcomes.profile = failed       ? GltfWorkflowProfileOutcome::kFailed
                       : incomplete ? GltfWorkflowProfileOutcome::kIncomplete
                                    : GltfWorkflowProfileOutcome::kPassed;
  s.outcomes.execution =
      status == Status::kCanceled ? GltfWorkflowExecutionOutcome::kCanceled
      : (status == Status::kSuccess || status == Status::kRejected)
          ? GltfWorkflowExecutionOutcome::kCompleted
          : GltfWorkflowExecutionOutcome::kFailed;
  s.outcomes.exit =
      status == Status::kCanceled ? GltfWorkflowExitMeaning::kCanceled
      : s.outcomes.execution == GltfWorkflowExecutionOutcome::kFailed
          ? GltfWorkflowExitMeaning::kExecutionFailure
      : (status == Status::kRejected ||
         s.outcomes.profile != GltfWorkflowProfileOutcome::kPassed)
          ? GltfWorkflowExitMeaning::kRejectedOrIncomplete
          : GltfWorkflowExitMeaning::kSuccess;
}
template <class T, class D>
[[noreturn]] void DelegateFailure(Status status, Stage stage, Code code,
                                  T native_status, const D& diagnostic) {
  GltfWorkflowDiagnostic d;
  d.stage = stage;
  d.code = code;
  d.delegated = diagnostic;
  d.delegated_status = native_status;
  throw ExpectedFailure{status, d};
}
Status Translate(ic::GltfStatus s) {
  if (s == ic::GltfStatus::kCanceled) return Status::kCanceled;
  if (s == ic::GltfStatus::kBudgetExceeded) return Status::kBudgetExceeded;
  if (s == ic::GltfStatus::kUnsupportedProfile) return Status::kUnsupported;
  return Status::kDelegatedFailure;
}
Status TranslateAdmission(const allocation::AdmissionFailure& failure) {
  switch (failure.Code()) {
    case allocation::AdmissionCode::kSizeLimit:
      return Status::kLimitExceeded;
    case allocation::AdmissionCode::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case allocation::AdmissionCode::kCanceled:
      return Status::kCanceled;
    case allocation::AdmissionCode::kInvalidObject:
      throw;
  }
  std::terminate();
}
// Allocation growth remains frozen while a delegate owns the remaining cap.
class DelegateAllowance {
 public:
  explicit DelegateAllowance(detail::WorkflowState& s) : state_(s) {
    s.UpdateDelegates();
    s.allocation.SetFrozen(true);
  }
  ~DelegateAllowance() { state_.allocation.SetFrozen(false); }
  std::uint64_t Cap(std::uint64_t configured) const {
    const auto available = state_.allocation.RemainingBytes();
    if (available == 0)
      Fail(Status::kBudgetExceeded, state_.outcomes.stage,
           Code::kRequestBudget);
    return std::min(configured, static_cast<std::uint64_t>(available));
  }

 private:
  detail::WorkflowState& state_;
};
struct ByteComparison {
  std::span<const std::byte> caller, owned;
  std::atomic<std::size_t> first;
  std::stop_token stop;
  std::size_t block;
};
void CompareBytes(void* opaque, std::size_t begin, std::size_t end) {
  auto& c = *static_cast<ByteComparison*>(opaque);
  for (std::size_t i = begin; i < end; ++i) {
    if ((i - begin) % c.block == 0 && c.stop.stop_requested())
      throw allocation::AdmissionFailure(allocation::AdmissionCode::kCanceled);
    if (c.caller[i] != c.owned[i]) {
      auto first = c.first.load(std::memory_order_relaxed);
      while (i < first && !c.first.compare_exchange_weak(
                              first, i, std::memory_order_relaxed)) {
      }
      return;
    }
  }
}
std::optional<std::size_t> Compare(
    detail::WorkflowState& s, std::span<const std::byte> a,
    std::span<const std::byte> b, const GltfWorkflowByteWorkPolicy& w,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const allocation::NativeWorkerBackend& backend) {
  if (a.size() != b.size()) return std::min(a.size(), b.size());
  ByteComparison comparison{
      a, b, a.size(), stop,
      static_cast<std::size_t>(w.cancellation_block_bytes)};
  geometry::MeshMaterializationWorkPolicy policy{
      w.parallel_min_bytes, w.partition_bytes, w.cancellation_block_bytes,
      w.max_jobs};
  geometry::MaterializationExecutionStats stats;
  auto& total = s.execution.source_protection;
  const auto record_stats = [&] {
    total.planned_jobs += stats.planned_jobs;
    total.launched_workers += stats.launched_workers;
    total.joined_workers += stats.joined_workers;
    total.caller_jobs += stats.caller_jobs;
  };
  try {
    allocation::RunRange(context, s.allocation, policy, a.size(),
                         sizeof(std::byte), CompareBytes, &comparison, stop,
                         stats, backend, &stats);
  } catch (...) {
    record_stats();
    throw;
  }
  record_stats();
  const auto first = comparison.first.load(std::memory_order_relaxed);
  return first == a.size() ? std::nullopt : std::optional<std::size_t>(first);
}
void ProtectSource(detail::WorkflowState& s,
                   const GltfDuplicateWorkflowRequest& request,
                   const GltfWorkflowByteWorkPolicy& work,
                   const geometry::ExecutionContext& context,
                   std::stop_token stop,
                   const allocation::NativeWorkerBackend& backend) {
  s.UpdateDelegates();
  GltfWorkflowDiagnostic d;
  d.stage = Stage::kVerify;
  d.code = Code::kSourceChanged;
  if (auto offset = Compare(s, request.json, s.read->OriginalJson(), work,
                            context, stop, backend)) {
    d.json_span = ic::GltfSourceSpan{*offset, 1};
    Observe(s, Check::kSourceUnchanged, false);
    throw ExpectedFailure{Status::kVerificationFailure, d};
  }
  for (const auto& supplied : request.resources) {
    auto resources = s.read->Resources();
    auto it = std::find_if(
        resources.begin(), resources.end(), [&](const auto& resource) {
          return resource.kind == supplied.kind &&
                 resource.table_index == supplied.table_index &&
                 resource.origin == ic::GltfResourceOrigin::kCallerBinding;
        });
    if (it == resources.end())
      Fail(Status::kSourceContractFailure, Stage::kVerify,
           Code::kSourceAccounting);
    if (auto offset = Compare(s, supplied.bytes, it->bytes, work, context, stop,
                              backend)) {
      d.resource_kind = supplied.kind;
      d.resource_index = supplied.table_index;
      d.element = *offset;
      Observe(s, Check::kSourceUnchanged, false);
      throw ExpectedFailure{Status::kVerificationFailure, d};
    }
  }
  Observe(s, Check::kSourceUnchanged, true);
}
void ProjectInspection(detail::WorkflowState& s,
                       const ic::GltfTopologyOutputInspectionResult& r) {
  using NativeStage = ic::GltfTopologyOutputInspectionStage;
  using NativeStatus = ic::GltfTopologyOutputInspectionStatus;
  using Coverage = ic::GltfTopologyOutputCheckCoverage;
  const bool mismatch = r.status == NativeStatus::kInvalidCorrespondence ||
                        r.status == NativeStatus::kOutputMismatch ||
                        r.status == NativeStatus::kSourceContractFailure ||
                        r.status == NativeStatus::kInvalidCandidateStorage;
  auto project = [&](Check check, bool completed, bool at_phase) {
    if (completed)
      Observe(s, check, true);
    else if (at_phase && mismatch)
      Observe(s, check, false);
    else if (at_phase && r.status != NativeStatus::kCanceled) {
      auto& c = s.coverage[static_cast<std::size_t>(check)];
      c.state = r.status == NativeStatus::kUnsupportedSelection
                    ? State::kUnsupported
                    : State::kFailed;
      c.passed.reset();
      c.reason = Code::kBlockedByEarlierStage;
    }
  };
  project(Check::kCanonicalPreservation,
          r.coverage.source_candidate == Coverage::kComplete &&
              r.coverage.provenance == Coverage::kComplete,
          r.diagnostic.stage == NativeStage::kSourceCandidate ||
              r.diagnostic.stage == NativeStage::kProvenance);
  project(Check::kReloadSelectedPrimitive,
          r.coverage.reloaded_primitive == Coverage::kComplete,
          r.diagnostic.stage == NativeStage::kReloadedPrimitive);
  project(Check::kRetainedAssetPreservation,
          r.coverage.lexical == Coverage::kComplete &&
              r.coverage.resources == Coverage::kComplete,
          r.diagnostic.stage == NativeStage::kLexical ||
              r.diagnostic.stage == NativeStage::kResources);
  if (r.coverage.correspondence != Coverage::kComplete &&
      r.diagnostic.stage == NativeStage::kCorrespondence && mismatch)
    Observe(s, Check::kRepairCorrespondence, false);
}
void RunStages(detail::WorkflowState& s,
               const GltfDuplicateWorkflowRequest& request,
               const GltfDuplicateWorkflowLimits& limits,
               const GltfDuplicateWorkflowWorkPolicy& work,
               const geometry::ExecutionContext& context, std::stop_token stop,
               const detail::WorkflowTestOptions& options) {
  s.outcomes.stage = Stage::kRead;
  if (!s.retained_read) {
    DelegateAllowance allowance(s);
    auto effective = limits.read;
    effective.max_new_requested_bytes =
        allowance.Cap(effective.max_new_requested_bytes);
    auto r = ic::ReadGltfAssetCapped(request.json, request.resources, effective,
                                     work.interchange, context, stop);
    if (!r.asset)
      DelegateFailure(Translate(r.diagnostic.status), Stage::kRead, Code::kRead,
                      r.diagnostic.status, r.diagnostic);
    s.read = r.asset;
  }
  if (!s.read->NewRequestedBytes())
    Fail(Status::kSourceContractFailure, Stage::kRead, Code::kSourceAccounting);
  auto primitive = s.read->Primitive(*request.primitive_ordinal);
  if (!primitive)
    Fail(Status::kLimitExceeded, Stage::kRead, Code::kPrimitiveSelection);
  s.primitive = primitive;
  s.outcomes.stage = Stage::kExtract;
  {
    DelegateAllowance allowance(s);
    auto effective = limits.extraction;
    effective.max_output_requested_bytes =
        allowance.Cap(effective.max_output_requested_bytes);
    auto r = ic::ExtractGltfPrimitiveCapped(
        *s.read, *primitive, request.extraction, effective, stop);
    if (!r.extraction)
      DelegateFailure(Translate(r.diagnostic.status), Stage::kExtract,
                      Code::kExtract, r.diagnostic.status, r.diagnostic);
    s.extraction = r.extraction;
  }
  s.outcomes.stage = Stage::kMaterialize;
  {
    DelegateAllowance allowance(s);
    const auto required =
        limits.materialization.max_bridge_new_requested_bytes +
        limits.materialization.materialization.max_new_requested_bytes +
        limits.materialization.inspection.max_new_requested_bytes;
    if (required > s.allocation.RemainingBytes())
      Fail(Status::kBudgetExceeded, Stage::kMaterialize,
           Code::kMaterializationAllowance);
    auto r = ic::MaterializeGltfPrimitive(*s.extraction, limits.materialization,
                                          work.geometry, stop);
    s.execution.materialization = r.execution;
    if (!r.primitive) {
      if (r.status == ic::GltfMaterializationStatus::kInvalidStorage)
        Observe(s, Check::kSourceStorage, false);
      auto status =
          r.status == ic::GltfMaterializationStatus::kCanceled
              ? Status::kCanceled
          : r.status == ic::GltfMaterializationStatus::kBudgetExceeded
              ? Status::kBudgetExceeded
          : r.status ==
                  ic::GltfMaterializationStatus::kUnsupportedRepresentation
              ? Status::kUnsupported
              : Status::kDelegatedFailure;
      DelegateFailure(status, Stage::kMaterialize, Code::kMaterialize, r.status,
                      r.diagnostic);
    }
    s.source = r.primitive;
    Observe(s, Check::kSourceStorage, true);
  }
  s.outcomes.stage = Stage::kRepair;
  {
    DelegateAllowance allowance(s);
    auto effective = limits.repair;
    effective.max_new_requested_bytes =
        allowance.Cap(effective.max_new_requested_bytes);
    auto r = RemoveMaterializedDuplicateFaces(s.source->Mesh(), request.targets,
                                              request.repair, effective,
                                              work.geometry, stop);
    s.execution.repair = r.execution;
    if (!r.repair) {
      if (r.status == MaterializedDuplicateStatus::kRejected) {
        s.outcomes.candidate = CandidateOutcome::rejected;
        if (r.diagnostic.code ==
            MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute) {
          auto& coverage =
              s.coverage[static_cast<std::size_t>(Check::kTargetPolicy)];
          coverage.state = State::kUnsupported;
          coverage.passed.reset();
          coverage.reason = Code::kRepair;
        } else {
          Observe(s, Check::kTargetPolicy, false);
        }
      }
      auto status = r.status == MaterializedDuplicateStatus::kCanceled
                        ? Status::kCanceled
                    : r.status == MaterializedDuplicateStatus::kRejected
                        ? Status::kRejected
                    : r.status == MaterializedDuplicateStatus::kBudgetExceeded
                        ? Status::kBudgetExceeded
                        : Status::kDelegatedFailure;
      DelegateFailure(status, Stage::kRepair, Code::kRepair, r.status,
                      r.diagnostic);
    }
    s.repair = r.repair;
    s.outcomes.candidate = r.repair->Outcome();
    Observe(s, Check::kTargetPolicy, true);
    Observe(s, Check::kCandidateStorage, true);
    Observe(s, Check::kRepairCorrespondence, true);
  }
  // Resolve expected reload policy only from independently accepted maps.
  s.reload_policy = request.extraction;
  auto kept_faces = s.repair->SourceFaces();
  auto kept_corners = s.repair->SourceCorners();
  bool identity =
      kept_faces.size() == s.source->Mesh().View().face_offsets.size() - 1 &&
      kept_corners.size() == s.source->Mesh().View().corner_vertices.size();
  for (std::size_t i = 0; identity && i < kept_faces.size(); ++i)
    identity = kept_faces[i] == i;
  for (std::size_t i = 0; identity && i < kept_corners.size(); ++i)
    identity = kept_corners[i] == i;
  if (!identity)
    s.reload_policy->triangles = ic::GltfTriangleRequest::kRequireTriangles;
  s.outcomes.stage = Stage::kWrite;
  {
    DelegateAllowance allowance(s);
    auto effective = limits.writing;
    effective.output.max_output_requested_bytes =
        allowance.Cap(effective.output.max_output_requested_bytes);
    auto r = ic::WriteGltfTopologySelection(
        {*s.source, s.repair->CandidateInspection(), kept_faces, kept_corners},
        effective, work.writing, stop);
    s.execution.writing = r.execution;
    if (!r.bundle) {
      auto status =
          r.status == ic::GltfTopologyWriteStatus::kCanceled ? Status::kCanceled
          : r.status == ic::GltfTopologyWriteStatus::kBudgetExceeded
              ? Status::kBudgetExceeded
          : (r.status == ic::GltfTopologyWriteStatus::kUnsupportedProfile ||
             r.status == ic::GltfTopologyWriteStatus::kUnsupportedSelection)
              ? Status::kUnsupported
              : Status::kDelegatedFailure;
      DelegateFailure(status, Stage::kWrite, Code::kWrite, r.status,
                      r.diagnostic);
    }
    s.written = r.bundle;
  }
  s.outcomes.stage = Stage::kReload;
  s.UpdateDelegates();
  detail::Vector<ic::SuppliedGltfResource> bindings{
      allocation::Allocator<ic::SuppliedGltfResource>(s.allocation)};
  std::size_t count = 0;
  for (const auto& resource : s.written->Resources())
    count += resource.origin == ic::GltfResourceOrigin::kCallerBinding;
  if (count > limits.max_reload_bindings)
    Fail(Status::kLimitExceeded, Stage::kReload, Code::kCountLimit);
  bindings.reserve(count);
  for (const auto& resource : s.written->Resources())
    if (resource.origin == ic::GltfResourceOrigin::kCallerBinding)
      bindings.push_back({resource.kind, resource.table_index, resource.bytes});
  {
    DelegateAllowance allowance(s);
    auto effective = limits.reload;
    effective.max_new_requested_bytes =
        allowance.Cap(effective.max_new_requested_bytes);
    auto r = ic::ReadGltfAssetCapped(s.written->Json(), bindings, effective,
                                     work.interchange, context, stop);
    if (!r.asset)
      DelegateFailure(Translate(r.diagnostic.status), Stage::kReload,
                      Code::kReload, r.diagnostic.status, r.diagnostic);
    s.reload = r.asset;
  }
  if (!s.reload->NewRequestedBytes())
    Fail(Status::kSourceContractFailure, Stage::kReload,
         Code::kSourceAccounting);
  std::optional<ic::GltfPrimitiveHandle> output_primitive;
  for (std::uint64_t i = 0; i < s.reload->PrimitiveCount(); ++i) {
    if (stop.stop_requested())
      Fail(Status::kCanceled, Stage::kReload, Code::kStopRequested);
    auto p = s.reload->Primitive(i);
    if (p && p->MeshIndex() == primitive->MeshIndex() &&
        p->PrimitiveIndex() == primitive->PrimitiveIndex()) {
      output_primitive = p;
      break;
    }
  }
  if (!output_primitive)
    Fail(Status::kVerificationFailure, Stage::kReload,
         Code::kPrimitiveIdentityMismatch);
  s.outcomes.stage = Stage::kReloadExtract;
  {
    DelegateAllowance allowance(s);
    auto effective = limits.reload_extraction;
    effective.max_output_requested_bytes =
        allowance.Cap(effective.max_output_requested_bytes);
    auto r = ic::ExtractGltfPrimitiveCapped(*s.reload, *output_primitive,
                                            *s.reload_policy, effective, stop);
    if (!r.extraction)
      DelegateFailure(Translate(r.diagnostic.status), Stage::kReloadExtract,
                      Code::kReloadExtract, r.diagnostic.status, r.diagnostic);
    s.reload_extraction = r.extraction;
  }
  s.outcomes.stage = Stage::kVerify;
  ProtectSource(
      s, request,
      {work.verification.parallel_min_bytes, work.verification.partition_bytes,
       work.verification.cancellation_block_bytes, work.verification.max_jobs},
      context, stop, options.source_worker_backend);
  {
    DelegateAllowance allowance(s);
    auto effective = limits.verification;
    effective.max_new_requested_bytes =
        allowance.Cap(effective.max_new_requested_bytes);
    auto r = ic::InspectGltfTopologyOutput(
        {*s.source, s.repair->CandidateInspection(), kept_faces, kept_corners,
         *s.written, *s.reload, *s.reload_extraction},
        effective, work.verification, stop);
    s.execution.verification = r.execution;
    ProjectInspection(s, r);
    if (!r.inspection) {
      auto status =
          r.status == ic::GltfTopologyOutputInspectionStatus::kCanceled
              ? Status::kCanceled
          : r.status == ic::GltfTopologyOutputInspectionStatus::kBudgetExceeded
              ? Status::kBudgetExceeded
          : r.status ==
                  ic::GltfTopologyOutputInspectionStatus::kUnsupportedSelection
              ? Status::kUnsupported
              : Status::kVerificationFailure;
      DelegateFailure(status, Stage::kVerify, Code::kReloadMismatch, r.status,
                      r.diagnostic);
    }
    s.inspection = r.inspection;
  }
  ComputeOutcomes(s, Status::kSuccess);
  if (s.outcomes.profile != GltfWorkflowProfileOutcome::kPassed)
    Fail(Status::kVerificationFailure, Stage::kVerify, Code::kReloadMismatch);
  s.output = s.written;
  s.outcomes.stage = Stage::kComplete;
}
}  // namespace

namespace gltf_workflow_detail {
namespace {
struct BaselineCopy {
  std::span<const std::byte> input;
  std::span<std::byte> output;
  std::stop_token stop;
  std::size_t block;
};
void CopyBaseline(void* opaque, std::size_t begin, std::size_t end) {
  auto& copy = *static_cast<BaselineCopy*>(opaque);
  while (begin < end) {
    if (copy.stop.stop_requested())
      throw allocation::AdmissionFailure(allocation::AdmissionCode::kCanceled);
    const auto count = std::min(copy.block, end - begin);
    std::copy_n(copy.input.begin() + static_cast<std::ptrdiff_t>(begin), count,
                copy.output.begin() + static_cast<std::ptrdiff_t>(begin));
    begin += count;
  }
}
void PrepareBaseline(WorkflowState& state, Vector<std::byte>& output,
                     std::span<const std::byte> input,
                     const GltfDuplicateWorkflowWorkPolicy& work,
                     const geometry::ExecutionContext& context,
                     std::stop_token stop, const WorkflowTestOptions& options) {
  output.resize(input.size());
  BaselineCopy copy{
      input, output, stop,
      static_cast<std::size_t>(work.verification.cancellation_block_bytes)};
  geometry::MeshMaterializationWorkPolicy policy{
      work.verification.parallel_min_bytes, work.verification.partition_bytes,
      work.verification.cancellation_block_bytes, work.verification.max_jobs};
  geometry::MaterializationExecutionStats stats;
  const auto account = [&] {
    auto& total = state.execution.source_protection;
    total.planned_jobs += stats.planned_jobs;
    total.launched_workers += stats.launched_workers;
    total.joined_workers += stats.joined_workers;
    total.caller_jobs += stats.caller_jobs;
  };
  try {
    allocation::RunRange(context, state.allocation, policy, input.size(),
                         sizeof(std::byte), CopyBaseline, &copy, stop, stats,
                         options.source_worker_backend, &stats);
  } catch (...) {
    account();
    throw;
  }
  account();
}
void ValidateRetained(const RetainedSourceInitialization& retained) {
  Positive(retained.max_protection_bytes);
  Positive(retained.max_binding_records);
  auto remaining = retained.max_protection_bytes;
  if (retained.source.OriginalJson().size() > remaining)
    Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
  remaining -= retained.source.OriginalJson().size();
  std::uint64_t count = 0;
  for (const auto& resource : retained.source.Resources()) {
    if (resource.origin != ic::GltfResourceOrigin::kCallerBinding) continue;
    if (count >= retained.max_binding_records ||
        resource.bytes.size() > remaining)
      Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
    ++count;
    remaining -= resource.bytes.size();
  }
  if (count > std::numeric_limits<std::size_t>::max() /
                  sizeof(SourceBaselineBinding) ||
      count > std::numeric_limits<std::size_t>::max() /
                  sizeof(ic::SuppliedGltfResource))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void RetainCompletedRepair(const GltfDuplicateWorkflowRequest& request,
                           const detail::WorkflowState* state,
                           GltfDuplicateWorkflowResult& result) noexcept {
  if (request.report_failure_retention ==
          GltfReportFailureRetention::kRetainCompletedRepair &&
      !result.record && state && state->repair)
    // Only the contained shared owner is copied. Optional copy signatures do
    // not portably expose the contained type's nonthrowing copy guarantee.
    result.retained_repair.emplace(*state->repair);
}
static_assert(
    std::is_nothrow_copy_constructible_v<MaterializedDuplicateRepair>);
static_assert(std::is_nothrow_copy_assignable_v<MaterializedDuplicateRepair>);
static_assert(std::is_nothrow_destructible_v<MaterializedDuplicateRepair>);
static_assert(
    std::is_nothrow_move_constructible_v<GltfDuplicateWorkflowResult>);
static_assert(std::is_nothrow_move_assignable_v<GltfDuplicateWorkflowResult>);
static_assert(std::is_nothrow_destructible_v<GltfDuplicateWorkflowResult>);

GltfDuplicateWorkflowResult RunWorkflow(
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const WorkflowTestOptions& options,
    const RetainedSourceInitialization* retained) {
  GltfDuplicateWorkflowResult result;
  std::shared_ptr<detail::WorkflowState> state;
  try {
    Validate(limits, work, retained != nullptr);
    ValidateRequest(request, limits, retained != nullptr);
    if (retained) ValidateRetained(*retained);
    auto allocator =
        allocation::AllocationContext(context, stop, options.allocation_backend)
            .WithRequestLimit(
                static_cast<std::size_t>(limits.max_new_requested_bytes));
    state = std::allocate_shared<detail::WorkflowState>(
        allocation::Allocator<detail::WorkflowState>(allocator), allocator);
    try {
      auto stage_request = request;
      if (retained) {
        state->retained_read = true;
        state->read = retained->source;
        PrepareBaseline(*state, state->baseline_json,
                        retained->source.OriginalJson(), work, context, stop,
                        options);
        std::size_t count = 0;
        for (const auto& resource : retained->source.Resources())
          count += resource.origin == ic::GltfResourceOrigin::kCallerBinding;
        state->baseline_bindings.reserve(count);
        state->baseline_views.reserve(count);
        for (const auto& resource : retained->source.Resources()) {
          if (resource.origin != ic::GltfResourceOrigin::kCallerBinding)
            continue;
          auto& binding = state->baseline_bindings.emplace_back(
              resource.kind, resource.table_index, state->allocation);
          PrepareBaseline(*state, binding.bytes, resource.bytes, work, context,
                          stop, options);
          state->baseline_views.push_back(
              {binding.kind, binding.index, binding.bytes});
        }
        stage_request.json = state->baseline_json;
        stage_request.resources = state->baseline_views;
      }
      RunStages(*state, stage_request, limits, work, context, stop, options);
      result.status = Status::kSuccess;
    } catch (const ExpectedFailure& failure) {
      result.status = failure.status;
      state->diagnostic = failure.diagnostic;
      state->outcomes.stage = failure.diagnostic.stage;
    } catch (const allocation::AdmissionFailure& failure) {
      result.status = TranslateAdmission(failure);
      state->diagnostic.stage = state->outcomes.stage;
      state->diagnostic.code = result.status == Status::kCanceled
                                   ? Code::kStopRequested
                                   : Code::kRequestBudget;
    }
    ComputeOutcomes(*state, result.status);
    result.outcomes = state->outcomes;
    result.diagnostic = state->diagnostic;
    // A stopped token cannot admit or serialize a fresh report. Keep outcomes.
    if (stop.stop_requested()) {
      const bool canceled_in_stage = result.status == Status::kCanceled;
      result.status = Status::kCanceled;
      if (!canceled_in_stage) {
        state->outcomes.stage = Stage::kReport;
        state->diagnostic.stage = Stage::kReport;
        state->diagnostic.code = Code::kStopRequested;
      }
      result.diagnostic = state->diagnostic;
      ComputeOutcomes(*state, result.status);
      result.outcomes = state->outcomes;
      result.execution = state->execution;
      RetainCompletedRepair(request, state.get(), result);
      return result;
    }
    state->UpdateDelegates();
    detail::SerializeReport(*state, request, limits, work, context, stop,
                            options);
    result.execution = state->execution;
    result.record = detail::Access::Make(state);
  } catch (const detail::ReportExtentFailure&) {
    result.status = Status::kLimitExceeded;
    result.diagnostic.stage = Stage::kReport;
    result.diagnostic.code = Code::kReportExtent;
  } catch (const ExpectedFailure& failure) {
    result.status = failure.status;
    result.diagnostic = failure.diagnostic;
    result.outcomes.stage = failure.diagnostic.stage;
  } catch (const allocation::AdmissionFailure& failure) {
    result.status = TranslateAdmission(failure);
    result.diagnostic.stage = state ? Stage::kReport : Stage::kConfiguration;
    result.diagnostic.code = result.status == Status::kCanceled
                                 ? Code::kStopRequested
                                 : Code::kRequestBudget;
    result.outcomes.stage = result.diagnostic.stage;
  }
  if (!result.record && state) {
    state->outcomes.stage = result.diagnostic.stage;
    ComputeOutcomes(*state, result.status);
    result.outcomes = state->outcomes;
    result.execution = state->execution;
  } else if (!state && result.status == Status::kCanceled) {
    result.outcomes.execution = GltfWorkflowExecutionOutcome::kCanceled;
    result.outcomes.exit = GltfWorkflowExitMeaning::kCanceled;
  }
  RetainCompletedRepair(request, state.get(), result);
  return result;
}
}  // namespace
bool IsWorkflowReportUtf8(std::string_view value) noexcept {
  return ValidUtf8(value);
}
std::optional<GltfDuplicateWorkflowResult> ValidateRetainedWorkflow(
    const RetainedSourceInitialization& retained,
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work) {
  try {
    Validate(limits, work, true);
    ValidateRequest(request, limits, true);
    ValidateRetained(retained);
    return std::nullopt;
  } catch (const ExpectedFailure& failure) {
    GltfDuplicateWorkflowResult result;
    result.status = failure.status;
    result.diagnostic = failure.diagnostic;
    result.outcomes.stage = failure.diagnostic.stage;
    return result;
  }
}
GltfDuplicateWorkflowResult RunGltfDuplicateWorkflowForTest(
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const WorkflowTestOptions& options) {
  return RunWorkflow(request, limits, work, context, stop, options, nullptr);
}
GltfDuplicateWorkflowResult RunRetainedGltfDuplicateWorkflow(
    const RetainedSourceInitialization& retained,
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const WorkflowTestOptions& options) {
  return RunWorkflow(request, limits, work, context, stop, options, &retained);
}
}  // namespace gltf_workflow_detail
GltfDuplicateWorkflowResult RunGltfDuplicateWorkflow(
    const GltfDuplicateWorkflowRequest& request,
    const GltfDuplicateWorkflowLimits& limits,
    const GltfDuplicateWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop) {
  return gltf_workflow_detail::RunGltfDuplicateWorkflowForTest(
      request, limits, work, context, stop, {});
}
}  // namespace meshvale::repair
