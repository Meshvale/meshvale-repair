// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <variant>

#include "gltf_workflow_internal.h"
#include "gltf_workflow_test_internal.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::gltf_workflow_detail {
namespace {
using Check = GltfWorkflowCheck;
using Stage = GltfWorkflowStage;
constexpr std::string_view kInputSnapshotId = "input";
constexpr std::string_view kCandidateSnapshotId = "candidate";
constexpr std::string_view kSelectedMeshId = "selected";
constexpr std::string_view kVertexMapId = "repair-vertices";
constexpr std::string_view kFaceMapId = "repair-faces";
constexpr std::string_view kCornerMapId = "repair-corners";
constexpr std::string_view kVertexDomain = "vertex";
constexpr std::string_view kFaceDomain = "face";
constexpr std::string_view kCornerDomain = "corner";
constexpr std::array<std::string_view, kCoverageCount> kCheckNames{
    "source-storage",
    "target-policy",
    "candidate-storage",
    "repair-correspondence",
    "canonical-preservation",
    "reload-selected-primitive",
    "retained-asset-preservation",
    "source-unchanged",
    "solid-validity"};
constexpr std::array<std::string_view, 11> kStageNames{
    "configuration", "read",   "extract", "materialize",
    "repair",        "write",  "reload",  "reload-extract",
    "verify",        "report", "complete"};
constexpr std::size_t kIntegerTokenBytes =
    std::numeric_limits<std::uint64_t>::digits10 + 1;
constexpr std::size_t kPositionWidth = 3;
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr unsigned char kJsonControlCharacterEnd = 0x20;
constexpr std::size_t kJsonUnicodeEscapeBytes = 6;
constexpr unsigned kHexDigitBits = 4;
constexpr unsigned kHexDigitMask = 15;
// Fixed tail holds four full uint64 stats plus JSON property spelling. It is
// allocated under the wrapper cap; max_report_bytes bounds actual UTF-8 length.
constexpr std::size_t kMaximumStatsTailBytes = 256;
constexpr std::string_view kFailureReason =
    "The native workflow stopped at the recorded stage and diagnostic.";
constexpr std::string_view kBlockedReason =
    "Blocked by the recorded native stage before this predicate completed.";
constexpr std::string_view kUnsupportedReason =
    "The selected capability is unsupported by the owning operation.";
constexpr std::string_view kSolidReason =
    "This profile supplies no closure, manifoldness or intersection predicate.";
// A nonallocating streaming sink is used for both exact extent planning and
// emission; it preserves canonical integer tokens and escapes caller strings.
class Sink {
 public:
  explicit Sink(std::span<std::byte> output = {}, std::stop_token stop = {})
      : output_(output), stop_(stop) {}
  void Raw(std::string_view value) {
    if (stop_.stop_requested())
      throw allocation::AdmissionFailure(allocation::AdmissionCode::kCanceled);
    if (value.size() > std::numeric_limits<std::size_t>::max() - size_)
      throw ReportExtentFailure{};
    if (!output_.empty()) {
      if (value.size() > output_.size() - size_) throw ReportExtentFailure{};
      std::transform(
          value.begin(), value.end(),
          output_.begin() + static_cast<std::ptrdiff_t>(size_), [](char c) {
            return static_cast<std::byte>(static_cast<unsigned char>(c));
          });
    }
    size_ += value.size();
  }
  void String(std::string_view value) {
    Raw("\"");
    for (unsigned char c : value) {
      if (c == '"')
        Raw("\\\"");
      else if (c == '\\')
        Raw("\\\\");
      else if (c < kJsonControlCharacterEnd) {
        std::array<char, kJsonUnicodeEscapeBytes> escape{
            '\\',
            'u',
            '0',
            '0',
            kHexDigits[c >> kHexDigitBits],
            kHexDigits[c & kHexDigitMask]};
        Raw({escape.data(), escape.size()});
      } else {
        const char byte = static_cast<char>(c);
        Raw({&byte, 1});
      }
    }
    Raw("\"");
  }
  void Integer(std::uint64_t value) {
    std::array<char, kIntegerTokenBytes> token;
    auto result =
        std::to_chars(token.data(), token.data() + token.size(), value);
    Raw({token.data(), static_cast<std::size_t>(result.ptr - token.data())});
  }
  void Boolean(bool value) { Raw(value ? "true" : "false"); }
  template <class T>
  void OptionalInteger(const std::optional<T>& value) {
    if (value)
      Integer(static_cast<std::uint64_t>(*value));
    else
      Raw("null");
  }
  template <class T>
  void Integers(std::span<const T> values) {
    Raw("[");
    bool comma = false;
    for (auto value : values) {
      if (comma) Raw(",");
      comma = true;
      Integer(value);
    }
    Raw("]");
  }
  std::size_t size() const { return size_; }

 private:
  std::span<std::byte> output_;
  std::stop_token stop_;
  std::size_t size_ = 0;
};
std::string_view CandidateName(const WorkflowState& s) {
  constexpr std::string_view kNotAttempted = "not-attempted";
  constexpr std::string_view kAccepted = "accepted";
  constexpr std::string_view kUnchanged = "unchanged";
  constexpr std::string_view kRejected = "rejected";

  if (!s.outcomes.candidate) return kNotAttempted;
  switch (*s.outcomes.candidate) {
    case CandidateOutcome::accepted:
      return kAccepted;
    case CandidateOutcome::unchanged:
      return kUnchanged;
    case CandidateOutcome::rejected:
      return kRejected;
  }
  std::terminate();
}
std::string_view ProfileName(GltfWorkflowProfileOutcome outcome) {
  constexpr std::string_view kPassed = "passed";
  constexpr std::string_view kFailed = "failed";
  constexpr std::string_view kIncomplete = "incomplete";

  switch (outcome) {
    case GltfWorkflowProfileOutcome::kPassed:
      return kPassed;
    case GltfWorkflowProfileOutcome::kFailed:
      return kFailed;
    case GltfWorkflowProfileOutcome::kIncomplete:
      return kIncomplete;
  }
  std::terminate();
}
std::string_view ReportStage(const WorkflowState& s) {
  constexpr std::string_view kComplete = "complete";
  constexpr std::string_view kImport = "import";
  constexpr std::string_view kInspect = "inspect";
  constexpr std::string_view kRepair = "repair";
  constexpr std::string_view kVerify = "verify";
  constexpr std::string_view kReport = "report";

  if (s.outcomes.execution == GltfWorkflowExecutionOutcome::kCompleted)
    return kComplete;
  switch (s.outcomes.stage) {
    case Stage::kConfiguration:
    case Stage::kRead:
    case Stage::kExtract:
      return kImport;
    case Stage::kMaterialize:
      return kInspect;
    case Stage::kRepair:
      return s.repair ? kVerify : kRepair;
    case Stage::kWrite:
    case Stage::kReload:
    case Stage::kReloadExtract:
    case Stage::kVerify:
      return kVerify;
    case Stage::kReport:
      return kReport;
    case Stage::kComplete:
      return kReport;
  }
  std::terminate();
}
std::string_view DomainName(geometry::AttributeDomain domain) {
  constexpr std::string_view kInvalidDomain = "invalid";

  switch (domain) {
    case geometry::AttributeDomain::vertex:
      return kVertexDomain;
    case geometry::AttributeDomain::face:
      return kFaceDomain;
    case geometry::AttributeDomain::corner:
      return kCornerDomain;
  }
  return kInvalidDomain;
}
void Scope(Sink& out, std::string_view snapshot, std::string_view subject) {
  constexpr std::string_view kSnapshotFields = "{\"snapshot\":";
  constexpr std::string_view kMeshFields = ",\"mesh\":";
  constexpr std::string_view kSubjectFields = ",\"subject\":";

  out.Raw(kSnapshotFields);
  if (snapshot.empty())
    out.Raw("null");
  else
    out.String(snapshot);
  out.Raw(kMeshFields);
  if (snapshot.empty())
    out.Raw("null");
  else
    out.String(kSelectedMeshId);
  out.Raw(kSubjectFields);
  out.String(subject);
  out.Raw("}");
}
void Snapshot(Sink& out, std::string_view role,
              const geometry::MaterializedMesh& mesh) {
  constexpr std::string_view kIdFields = "{\"id\":";
  constexpr std::string_view kRoleFields = ",\"role\":";
  constexpr std::string_view kMeshesIdVertexCountFields =
      ",\"meshes\":[{\"id\":\"selected\",\"vertex_count\":";
  constexpr std::string_view kFaceCountFields = ",\"face_count\":";
  constexpr std::string_view kCornerCountFields = ",\"corner_count\":";

  const auto view = mesh.View();
  out.Raw(kIdFields);
  out.String(role);
  out.Raw(kRoleFields);
  out.String(role);
  out.Raw(kMeshesIdVertexCountFields);
  out.Integer(view.positions.size() / kPositionWidth);
  out.Raw(kFaceCountFields);
  out.Integer(view.face_offsets.size() - 1);
  out.Raw(kCornerCountFields);
  out.Integer(view.corner_vertices.size());
  out.Raw("}]}");
}
void TargetPairs(Sink& out, std::span<const DuplicateTarget> targets) {
  constexpr std::string_view kKeepFields = "{\"keep\":";
  constexpr std::string_view kRemoveFields = ",\"remove\":";

  out.Raw("[");
  bool comma = false;
  for (const auto& target : targets) {
    if (comma) out.Raw(",");
    comma = true;
    out.Raw(kKeepFields);
    out.Integer(target.keep);
    out.Raw(kRemoveFields);
    out.Integer(target.remove);
    out.Raw("}");
  }
  out.Raw("]");
}
void ExtractionPolicy(Sink& out, const interchange::GltfExtractionRequest& p) {
  constexpr std::string_view kTrianglesFields = "{\"triangles\":";
  constexpr std::string_view kValuesFields = ",\"values\":";
  constexpr std::string_view kRequireTriangles = "require-triangles";
  constexpr std::string_view kConvertStripOrFan = "convert-strip-or-fan";
  constexpr std::string_view kRawValues = "raw";
  constexpr std::string_view kRawAndNormalizedValues = "raw-and-normalized";

  out.Raw(kTrianglesFields);
  out.String(p.triangles == interchange::GltfTriangleRequest::kRequireTriangles
                 ? kRequireTriangles
                 : kConvertStripOrFan);
  out.Raw(kValuesFields);
  out.String(p.values == interchange::GltfValueRequest::kRaw
                 ? kRawValues
                 : kRawAndNormalizedValues);
  out.Raw("}");
}
void Map(Sink& out, std::string_view id, std::string_view domain, bool identity,
         std::span<const geometry::index_t> entries) {
  constexpr std::string_view kIdFields = "{\"id\":";
  constexpr std::string_view kFromSnapshotMeshToSnapshotMeshDomainFields =
      ",\"from\":{\"snapshot\":\"input\",\"mesh\":\"selected\"},\"to\":{"
      "\"snapshot\":\"candidate\",\"mesh\":\"selected\"},\"domain\":";
  constexpr std::string_view kKindFields = ",\"kind\":";
  constexpr std::string_view kEntriesFields = ",\"entries\":";
  constexpr std::string_view kIdentityMap = "identity";
  constexpr std::string_view kExplicitMap = "explicit";

  out.Raw(kIdFields);
  out.String(id);
  out.Raw(kFromSnapshotMeshToSnapshotMeshDomainFields);
  out.String(domain);
  out.Raw(kKindFields);
  out.String(identity ? kIdentityMap : kExplicitMap);
  out.Raw(kEntriesFields);
  if (identity)
    out.Raw("null");
  else
    out.Integers(entries);
  out.Raw("}");
}
template <class T>
void Stats(Sink& out, const T& stats) {
  constexpr std::string_view kPlannedJobsFields = "{\"planned_jobs\":";
  constexpr std::string_view kLaunchedWorkersFields = ",\"launched_workers\":";
  constexpr std::string_view kJoinedWorkersFields = ",\"joined_workers\":";
  constexpr std::string_view kCallerJobsFields = ",\"caller_jobs\":";

  out.Raw(kPlannedJobsFields);
  out.Integer(stats.planned_jobs);
  out.Raw(kLaunchedWorkersFields);
  out.Integer(stats.launched_workers);
  out.Raw(kJoinedWorkersFields);
  out.Integer(stats.joined_workers);
  out.Raw(kCallerJobsFields);
  out.Integer(stats.caller_jobs);
  out.Raw("}");
}
template <class T>
void SpanReceipt(Sink& out, const std::optional<T>& span) {
  constexpr std::string_view kOffsetFields = "{\"offset\":";
  constexpr std::string_view kLengthFields = ",\"length\":";

  if (!span) {
    out.Raw("null");
    return;
  }
  out.Raw(kOffsetFields);
  out.Integer(span->offset);
  out.Raw(kLengthFields);
  out.Integer(span->length);
  out.Raw("}");
}
template <class T>
void DiagnosticReceipt(Sink& out, const T& diagnostic) {
  constexpr std::string_view kCodeOrdinalFields = "{\"code_ordinal\":";
  constexpr std::string_view kStageOrdinalFields = ",\"stage_ordinal\":";
  constexpr std::string_view kAttributeFields = ",\"attribute\":";
  constexpr std::string_view kElementFields = ",\"element\":";
  constexpr std::string_view kTargetFields = ",\"target\":";
  constexpr std::string_view kSourceChannelFields = ",\"source_channel\":";
  constexpr std::string_view kResourceFields = ",\"resource\":";
  constexpr std::string_view kResourceKindOrdinalFields =
      ",\"resource_kind_ordinal\":";
  constexpr std::string_view kResourceIndexFields = ",\"resource_index\":";
  constexpr std::string_view kResourceSpanFields = ",\"resource_span\":";
  constexpr std::string_view kRuleIdFields = ",\"rule_id\":";
  constexpr std::string_view kJsonSpanFields = ",\"json_span\":";
  constexpr std::string_view kGeometryStatusOrdinalFields =
      ",\"geometry_status_ordinal\":";
  constexpr std::string_view kGeometryDiagnosticFields =
      ",\"geometry_diagnostic\":";
  constexpr std::string_view kStorageIssueFields = ",\"storage_issue\":";

  // The version-1 receipt stores published native enum ordinals, together with
  // the discriminated owner type. Core report fields keep their wire strings.
  out.Raw(kCodeOrdinalFields);
  out.Integer(static_cast<std::uint64_t>(diagnostic.code));
  if constexpr (requires { diagnostic.stage; }) {
    out.Raw(kStageOrdinalFields);
    out.Integer(static_cast<std::uint64_t>(diagnostic.stage));
  }
  if constexpr (requires { diagnostic.attribute; }) {
    out.Raw(kAttributeFields);
    out.OptionalInteger(diagnostic.attribute);
  }
  if constexpr (requires { diagnostic.element; }) {
    out.Raw(kElementFields);
    out.OptionalInteger(diagnostic.element);
  }
  if constexpr (requires { diagnostic.target; }) {
    out.Raw(kTargetFields);
    out.OptionalInteger(diagnostic.target);
  }
  if constexpr (requires { diagnostic.source_channel; }) {
    out.Raw(kSourceChannelFields);
    out.OptionalInteger(diagnostic.source_channel);
  }
  if constexpr (requires { diagnostic.resource; }) {
    out.Raw(kResourceFields);
    out.OptionalInteger(diagnostic.resource);
  }
  if constexpr (requires { diagnostic.resource_kind; }) {
    out.Raw(kResourceKindOrdinalFields);
    out.OptionalInteger(diagnostic.resource_kind);
    out.Raw(kResourceIndexFields);
    out.OptionalInteger(diagnostic.resource_index);
    out.Raw(kResourceSpanFields);
    SpanReceipt(out, diagnostic.resource_span);
    out.Raw(kRuleIdFields);
    out.Integer(diagnostic.rule_id);
  }
  if constexpr (requires { diagnostic.json_span; }) {
    out.Raw(kJsonSpanFields);
    SpanReceipt(out, diagnostic.json_span);
  }
  if constexpr (requires { diagnostic.geometry_status; }) {
    out.Raw(kGeometryStatusOrdinalFields);
    out.OptionalInteger(diagnostic.geometry_status);
    out.Raw(kGeometryDiagnosticFields);
    if (diagnostic.geometry_diagnostic)
      DiagnosticReceipt(out, *diagnostic.geometry_diagnostic);
    else
      out.Raw("null");
  }
  if constexpr (requires { diagnostic.storage_issue; }) {
    out.Raw(kStorageIssueFields);
    if (diagnostic.storage_issue)
      DiagnosticReceipt(out, *diagnostic.storage_issue);
    else
      out.Raw("null");
  }
  out.Raw("}");
}
void DelegatedReceipt(Sink& out, const GltfWorkflowDiagnostic& d) {
  constexpr std::string_view kOwnerFields = "{\"owner\":";
  constexpr std::string_view kStatusOrdinalFields = ",\"status_ordinal\":";
  constexpr std::string_view kDiagnosticFields = ",\"diagnostic\":";

  constexpr std::array<std::string_view, 6> kDelegateNames{
      "none",
      "interchange-gltf",
      "interchange-materialization",
      "repair-materialized-duplicates",
      "interchange-topology-write",
      "interchange-topology-output-inspection"};
  out.Raw(kOwnerFields);
  out.String(kDelegateNames[d.delegated.index()]);
  out.Raw(kStatusOrdinalFields);
  std::visit(
      [&](auto status) {
        if constexpr (std::is_same_v<decltype(status), std::monostate>)
          out.Raw("null");
        else
          out.Integer(static_cast<std::uint64_t>(status));
      },
      d.delegated_status);
  out.Raw(kDiagnosticFields);
  std::visit(
      [&](const auto& diagnostic) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(diagnostic)>,
                                     std::monostate>)
          out.Raw("null");
        else
          DiagnosticReceipt(out, diagnostic);
      },
      d.delegated);
  out.Raw("}");
}
void Preservation(Sink& out, const WorkflowState& s) {
  constexpr std::string_view kGeometryFeature = "geometry";
  constexpr std::string_view kPositionsFeature = "positions";
  constexpr std::string_view kRetainedLoopsFeature = "retained-directed-loops";
  constexpr std::string_view kAttributeFeature = "attribute";

  constexpr std::string_view kFeatureKindFields = "{\"feature\":{\"kind\":";
  constexpr std::string_view kNameFields = ",\"name\":";
  constexpr std::string_view kDomainFields = ",\"domain\":";
  constexpr std::string_view kSetIndexFields = ",\"set_index\":";
  constexpr std::string_view kFromFields = "},\"from\":";
  constexpr std::string_view kToFields = ",\"to\":";
  constexpr std::string_view kOutcomeReasonMapsFields =
      ",\"outcome\":\"preserved\",\"reason\":null,\"maps\":[";

  out.Raw("[");
  if (s.repair) {
    bool comma = false;
    const auto emit = [&](std::string_view kind, std::string_view name,
                          std::string_view domain,
                          std::optional<std::uint32_t> set,
                          std::string_view map) {
      if (comma) out.Raw(",");
      comma = true;
      out.Raw(kFeatureKindFields);
      out.String(kind);
      out.Raw(kNameFields);
      out.String(name);
      out.Raw(kDomainFields);
      out.String(domain);
      out.Raw(kSetIndexFields);
      out.OptionalInteger(set);
      out.Raw(kFromFields);
      Scope(out, kInputSnapshotId, name);
      out.Raw(kToFields);
      Scope(out, kCandidateSnapshotId, name);
      out.Raw(kOutcomeReasonMapsFields);
      out.String(map);
      out.Raw("]}");
    };
    emit(kGeometryFeature, kPositionsFeature, kVertexDomain, {}, kVertexMapId);
    emit(kGeometryFeature, kRetainedLoopsFeature, kCornerDomain, {},
         kCornerMapId);
    for (const auto& attribute : s.repair->Source().View().attributes) {
      const auto domain = DomainName(attribute.domain);
      const auto map =
          attribute.domain == geometry::AttributeDomain::vertex ? kVertexMapId
          : attribute.domain == geometry::AttributeDomain::face ? kFaceMapId
                                                                : kCornerMapId;
      emit(kAttributeFeature, attribute.name, domain, attribute.set_index, map);
    }
  }
  out.Raw("]");
}
void ChannelReceipts(Sink& out, const WorkflowState& s) {
  constexpr std::string_view kSemanticFields = "{\"semantic\":";
  constexpr std::string_view kSetIndexFields = ",\"set_index\":";
  constexpr std::string_view kAccessorIndexFields = ",\"accessor_index\":";
  constexpr std::string_view kComponentTypeOrdinalFields =
      ",\"component_type_ordinal\":";
  constexpr std::string_view kShapeOrdinalFields = ",\"shape_ordinal\":";
  constexpr std::string_view kNormalizedFields = ",\"normalized\":";
  constexpr std::string_view kElementCountFields = ",\"element_count\":";
  constexpr std::string_view kBaseBufferViewFields = ",\"base_buffer_view\":";
  constexpr std::string_view kBaseByteOffsetFields = ",\"base_byte_offset\":";
  constexpr std::string_view kBaseByteStrideFields = ",\"base_byte_stride\":";
  constexpr std::string_view kPackedSparseStepFields =
      ",\"packed_sparse_step\":";
  constexpr std::string_view kSparseDestinationsFields =
      ",\"sparse_destinations\":";
  constexpr std::string_view kRawScalarCountFields = ",\"raw_scalar_count\":";
  constexpr std::string_view kNormalizedScalarCountFields =
      ",\"normalized_scalar_count\":";

  out.Raw("[");
  if (s.extraction) {
    bool comma = false;
    for (const auto& channel : s.extraction->Channels()) {
      if (comma) out.Raw(",");
      comma = true;
      out.Raw(kSemanticFields);
      out.String(channel.semantic);
      out.Raw(kSetIndexFields);
      out.OptionalInteger(channel.set_index);
      out.Raw(kAccessorIndexFields);
      out.Integer(channel.accessor_index);
      out.Raw(kComponentTypeOrdinalFields);
      out.Integer(static_cast<std::uint64_t>(channel.component_type));
      out.Raw(kShapeOrdinalFields);
      out.Integer(static_cast<std::uint64_t>(channel.shape));
      out.Raw(kNormalizedFields);
      out.Boolean(channel.normalized);
      out.Raw(kElementCountFields);
      out.Integer(channel.element_count);
      out.Raw(kBaseBufferViewFields);
      out.OptionalInteger(channel.base_buffer_view);
      out.Raw(kBaseByteOffsetFields);
      out.Integer(channel.base_byte_offset);
      out.Raw(kBaseByteStrideFields);
      out.Integer(channel.base_byte_stride);
      out.Raw(kPackedSparseStepFields);
      out.Integer(channel.packed_sparse_step);
      out.Raw(kSparseDestinationsFields);
      out.Integers(channel.sparse_destinations);
      out.Raw(kRawScalarCountFields);
      std::visit([&](auto values) { out.Integer(values.size()); }, channel.raw);
      out.Raw(kNormalizedScalarCountFields);
      out.Integer(channel.normalized_values.size());
      out.Raw("}");
    }
  }
  out.Raw("]");
}
void PhaseStats(Sink& out, const WorkflowState& s) {
  constexpr std::string_view kMaterializationCaptureFields =
      "{\"materialization\":{\"capture\":";
  constexpr std::string_view kInspectionFields = ",\"inspection\":";
  constexpr std::string_view kRepairSourceInspectionFields =
      "},\"repair\":{\"source_inspection\":";
  constexpr std::string_view kEligibilityFields = ",\"eligibility\":";
  constexpr std::string_view kComparisonFields = ",\"comparison\":";
  constexpr std::string_view kSelectionFields = ",\"selection\":";
  constexpr std::string_view kCorrespondenceFields = ",\"correspondence\":";
  constexpr std::string_view kCandidateInspectionFields =
      ",\"candidate_inspection\":";
  constexpr std::string_view kVerificationFields = ",\"verification\":";
  constexpr std::string_view kDirectedVerificationFields =
      ",\"directed_verification\":";
  constexpr std::string_view kWritingVerificationFields =
      "},\"writing\":{\"verification\":";
  constexpr std::string_view kIndexEncodingFields = ",\"index_encoding\":";
  constexpr std::string_view kBase64EncodingFields = ",\"base64_encoding\":";
  constexpr std::string_view kOutputCopyFields = ",\"output_copy\":";
  constexpr std::string_view kVerificationSourceCandidateFields =
      "},\"verification\":{\"source_candidate\":";
  constexpr std::string_view kLexicalFields = ",\"lexical\":";
  constexpr std::string_view kResourcesFields = ",\"resources\":";
  constexpr std::string_view kReloadedPrimitiveFields =
      ",\"reloaded_primitive\":";
  constexpr std::string_view kProvenanceFields = ",\"provenance\":";
  constexpr std::string_view kSourceProtectionFields =
      "},\"source_protection\":";
  constexpr std::string_view kReportCopyFields = ",\"report_copy\":";

  out.Raw(kMaterializationCaptureFields);
  Stats(out, s.execution.materialization.capture);
  out.Raw(kInspectionFields);
  Stats(out, s.execution.materialization.inspection);
  out.Raw(kRepairSourceInspectionFields);
  Stats(out, s.execution.repair.source_inspection);
  out.Raw(kEligibilityFields);
  Stats(out, s.execution.repair.eligibility);
  out.Raw(kComparisonFields);
  Stats(out, s.execution.repair.comparison);
  out.Raw(kSelectionFields);
  Stats(out, s.execution.repair.selection);
  out.Raw(kCorrespondenceFields);
  Stats(out, s.execution.repair.correspondence);
  out.Raw(kCandidateInspectionFields);
  Stats(out, s.execution.repair.candidate_inspection);
  out.Raw(kVerificationFields);
  Stats(out, s.execution.repair.verification);
  out.Raw(kDirectedVerificationFields);
  Stats(out, s.execution.repair.directed_verification);
  out.Raw(kWritingVerificationFields);
  Stats(out, s.execution.writing.verification);
  out.Raw(kIndexEncodingFields);
  Stats(out, s.execution.writing.index_encoding);
  out.Raw(kBase64EncodingFields);
  Stats(out, s.execution.writing.base64_encoding);
  out.Raw(kOutputCopyFields);
  Stats(out, s.execution.writing.output_copy);
  out.Raw(kVerificationSourceCandidateFields);
  Stats(out, s.execution.verification.source_candidate);
  out.Raw(kCorrespondenceFields);
  Stats(out, s.execution.verification.correspondence);
  out.Raw(kLexicalFields);
  Stats(out, s.execution.verification.lexical);
  out.Raw(kResourcesFields);
  Stats(out, s.execution.verification.resources);
  out.Raw(kReloadedPrimitiveFields);
  Stats(out, s.execution.verification.reloaded_primitive);
  out.Raw(kProvenanceFields);
  Stats(out, s.execution.verification.provenance);
  out.Raw(kSourceProtectionFields);
  Stats(out, s.execution.source_protection);
  // Report copying is appended after workers have actually joined.
  out.Raw(kReportCopyFields);
}
void Prefix(Sink& out, const WorkflowState& s,
            const GltfDuplicateWorkflowRequest& r) {
  constexpr std::string_view kCompletedExecution = "completed";
  constexpr std::string_view kCancelledExecution = "cancelled";
  constexpr std::string_view kFailedOutcome = "failed";
  constexpr std::string_view kPassedFinding = "passed";
  constexpr std::string_view kPerformedCoverage = "performed";
  constexpr std::string_view kSkippedCoverage = "skipped";
  constexpr std::string_view kUnsupportedCoverage = "unsupported";
  constexpr std::string_view kWorkflowSubject = "native-workflow";
  constexpr std::string_view kExactEncoding = "exact";
  constexpr std::string_view kFloatPositionWidened = "float-position-widened";
  constexpr std::string_view kSignedIntegerWidened = "signed-integer-widened";

  constexpr std::string_view kSchemaToolNameVersionFields =
      "{\"schema\":\"meshvale.report/"
      "1\",\"tool\":{\"name\":\"meshvale-repair\",\"version\":";
  constexpr std::string_view kInputIdFields = "},\"input\":{\"id\":";
  constexpr std::string_view
      kSha256OperationKindNameVersionOptionsExtractionFields =
          ",\"sha256\":null},\"operation\":{\"kind\":\"repair\",\"name\":"
          "\"remove-duplicate-faces\",\"version\":\"1\",\"options\":{"
          "\"extraction\":";
  constexpr std::string_view kRowLocalAttributesFields =
      ",\"row_local_attributes\":[";
  constexpr std::string_view kDomainFields = "{\"domain\":";
  constexpr std::string_view kNameFields = ",\"name\":";
  constexpr std::string_view kTargetsFields = "],\"targets\":";
  constexpr std::string_view kObjectTargetsFields = "},\"targets\":[";
  constexpr std::string_view kSnapshotMeshDomainIndexFields =
      "{\"snapshot\":\"input\",\"mesh\":\"selected\",\"domain\":\"face\","
      "\"index\":";
  constexpr std::string_view kExecutionOutcomeFields =
      "]},\"execution\":{\"outcome\":";
  constexpr std::string_view kStageFields = ",\"stage\":";
  constexpr std::string_view kReasonFields = ",\"reason\":";
  constexpr std::string_view kCandidateOutcomeFields =
      "},\"candidate\":{\"outcome\":";
  constexpr std::string_view kSnapshotFields = ",\"snapshot\":";
  constexpr std::string_view kProfileNameVersionRequiredChecksFields =
      "},\"profile\":{\"name\":\"gltf-static-duplicate-preservation\","
      "\"version\":\"1\",\"required_checks\":[";
  constexpr std::string_view kOutcomeFields = "],\"outcome\":";
  constexpr std::string_view kSnapshotsFields = "},\"snapshots\":[";
  constexpr std::string_view kDiagnosticsFields = "],\"diagnostics\":[";
  constexpr std::string_view kIdCodeSeverityMessageFields =
      "{\"id\":\"native-failure\",\"code\":\"gltf.workflow\",\"severity\":"
      "\"error\",\"message\":";
  constexpr std::string_view kScopeFields = ",\"scope\":";
  constexpr std::string_view kElementDataNativeStageFields =
      ",\"element\":null,\"data\":{\"native_stage\":";
  constexpr std::string_view kCodeOrdinalFields = ",\"code_ordinal\":";
  constexpr std::string_view kElementValueFields = ",\"element_value\":";
  constexpr std::string_view kAttributeValueFields = ",\"attribute_value\":";
  constexpr std::string_view kJsonSpanFields = ",\"json_span\":";
  constexpr std::string_view kResourceKindOrdinalFields =
      ",\"resource_kind_ordinal\":";
  constexpr std::string_view kResourceIndexFields = ",\"resource_index\":";
  constexpr std::string_view kDelegatedFields = ",\"delegated\":";
  constexpr std::string_view kCoverageFields = "],\"coverage\":[";
  constexpr std::string_view kIdFields = "{\"id\":";
  constexpr std::string_view kCheckFields = ",\"check\":";
  constexpr std::string_view kVersionScopeFields =
      ",\"version\":\"1\",\"scope\":";
  constexpr std::string_view kStatusFields = ",\"status\":";
  constexpr std::string_view kFindingFields = ",\"finding\":";
  constexpr std::string_view kValueDiagnosticsFields = ",\"diagnostics\":[]}";
  constexpr std::string_view kMapsFields = "],\"maps\":[";
  constexpr std::string_view kPreservationFields = "],\"preservation\":";
  constexpr std::string_view
      kPublicationRequestedOutcomeSnapshotNativeStageFields =
          ",\"publication\":{\"requested\":false,\"outcome\":\"not-requested\","
          "\"snapshot\":null,\"artifacts\":[],\"verified\":false,\"reason\":"
          "null},\"extensions\":{\"meshvale.repair.gltf\":{\"version\":\"1\","
          "\"native_stage\":";
  constexpr std::string_view kRequestedPrimitiveOrdinalFields =
      ",\"requested_primitive_ordinal\":";
  constexpr std::string_view kUnresolvedTargetsFields =
      ",\"unresolved_targets\":";
  constexpr std::string_view kMeshIndexFields = ",\"mesh_index\":";
  constexpr std::string_view kPrimitiveIndexFields = ",\"primitive_index\":";
  constexpr std::string_view kSourceModeFields = ",\"source_mode\":";
  constexpr std::string_view kInitialExtractionFields =
      ",\"initial_extraction\":";
  constexpr std::string_view kReloadExtractionFields =
      ",\"reload_extraction\":";
  constexpr std::string_view kChannelsFields = ",\"channels\":";
  constexpr std::string_view kMaterializationEncodingsFields =
      ",\"materialization_encodings\":[";
  constexpr std::string_view kSourceChannelFields = "{\"source_channel\":";
  constexpr std::string_view kEncodingFields = ",\"encoding\":";
  constexpr std::string_view kSourceFacesFields = "],\"source_faces\":";
  constexpr std::string_view kSourceCornersFields = ",\"source_corners\":";
  constexpr std::string_view kOriginalCornersFields = ",\"original_corners\":[";
  constexpr std::string_view kPrimitiveStreamCornerFields =
      "{\"primitive_stream_corner\":";
  constexpr std::string_view kAccessorRowFields = ",\"accessor_row\":";
  constexpr std::string_view kChangedBindingFields = "],\"changed_binding\":";
  constexpr std::string_view kValueSourceModeFields = "{\"source_mode\":";
  constexpr std::string_view kSourceIndicesAccessorFields =
      ",\"source_indices_accessor\":";
  constexpr std::string_view kOutputModeFields = ",\"output_mode\":";
  constexpr std::string_view kOutputIndicesAccessorFields =
      ",\"output_indices_accessor\":";
  constexpr std::string_view kAppendedBufferFields = ",\"appended_buffer\":";
  constexpr std::string_view kAppendedBufferViewFields =
      ",\"appended_buffer_view\":";
  constexpr std::string_view kValueReasonFields =
      ",\"reason\":\"Changed ordered triangle selection appends a UINT32 index "
      "stream.\"}";
  constexpr std::string_view kMemoryOutputVerifiedFields =
      ",\"memory_output_verified\":";
  constexpr std::string_view kPhaseExecutionFields = ",\"phase_execution\":";

  out.Raw(kSchemaToolNameVersionFields);
  out.String(MESHVALE_REPAIR_VERSION);
  out.Raw(kInputIdFields);
  out.String(r.input_id);
  out.Raw(kSha256OperationKindNameVersionOptionsExtractionFields);
  ExtractionPolicy(out, r.extraction);
  out.Raw(kRowLocalAttributesFields);
  bool comma = false;
  for (const auto& declaration : r.repair.row_local_attributes) {
    if (comma) out.Raw(",");
    comma = true;
    out.Raw(kDomainFields);
    out.String(DomainName(declaration.domain));
    out.Raw(kNameFields);
    out.String(declaration.name);
    out.Raw("}");
  }
  out.Raw(kTargetsFields);
  TargetPairs(out, r.targets);
  out.Raw(kObjectTargetsFields);
  comma = false;
  if (s.source)
    for (const auto& target : r.targets) {
      if (comma) out.Raw(",");
      comma = true;
      out.Raw(kSnapshotMeshDomainIndexFields);
      out.Integer(target.remove);
      out.Raw("}");
    }
  out.Raw(kExecutionOutcomeFields);
  const bool completed =
      s.outcomes.execution == GltfWorkflowExecutionOutcome::kCompleted;
  out.String(completed ? kCompletedExecution
             : s.outcomes.execution == GltfWorkflowExecutionOutcome::kCanceled
                 ? kCancelledExecution
                 : kFailedOutcome);
  out.Raw(kStageFields);
  out.String(ReportStage(s));
  out.Raw(kReasonFields);
  if (completed)
    out.Raw("null");
  else
    out.String(kFailureReason);
  out.Raw(kCandidateOutcomeFields);
  out.String(CandidateName(s));
  out.Raw(kSnapshotFields);
  if (s.repair)
    out.String(kCandidateSnapshotId);
  else
    out.Raw("null");
  out.Raw(kProfileNameVersionRequiredChecksFields);
  for (std::size_t i = 0; i < kRequiredCheckCount; ++i) {
    if (i) out.Raw(",");
    out.String(kCheckNames[i]);
  }
  out.Raw(kOutcomeFields);
  out.String(ProfileName(s.outcomes.profile));
  out.Raw(kSnapshotsFields);
  if (s.source) Snapshot(out, kInputSnapshotId, s.source->Mesh());
  if (s.repair) {
    out.Raw(",");
    Snapshot(out, kCandidateSnapshotId, s.repair->Candidate());
  }
  out.Raw(kDiagnosticsFields);
  if (s.diagnostic.code != GltfWorkflowDiagnosticCode::kNone) {
    out.Raw(kIdCodeSeverityMessageFields);
    out.String(kFailureReason);
    out.Raw(kScopeFields);
    Scope(out, s.source ? kInputSnapshotId : "", kWorkflowSubject);
    out.Raw(kElementDataNativeStageFields);
    out.String(kStageNames[static_cast<std::size_t>(s.diagnostic.stage)]);
    out.Raw(kCodeOrdinalFields);
    out.Integer(static_cast<std::uint64_t>(s.diagnostic.code));
    out.Raw(kElementValueFields);
    out.OptionalInteger(s.diagnostic.element);
    out.Raw(kAttributeValueFields);
    out.OptionalInteger(s.diagnostic.attribute);
    out.Raw(kJsonSpanFields);
    SpanReceipt(out, s.diagnostic.json_span);
    out.Raw(kResourceKindOrdinalFields);
    out.OptionalInteger(s.diagnostic.resource_kind);
    out.Raw(kResourceIndexFields);
    out.OptionalInteger(s.diagnostic.resource_index);
    out.Raw(kDelegatedFields);
    DelegatedReceipt(out, s.diagnostic);
    out.Raw("}}");
  }
  out.Raw(kCoverageFields);
  for (std::size_t i = 0; i < s.coverage.size(); ++i) {
    if (i) out.Raw(",");
    const auto& c = s.coverage[i];
    out.Raw(kIdFields);
    out.String(kCheckNames[i]);
    out.Raw(kCheckFields);
    out.String(kCheckNames[i]);
    out.Raw(kVersionScopeFields);
    const bool input_scope = c.check == Check::kSourceStorage ||
                             c.check == Check::kTargetPolicy ||
                             c.check == Check::kSourceUnchanged;
    Scope(out,
          input_scope ? (s.source ? kInputSnapshotId : "")
                      : (s.repair ? kCandidateSnapshotId : ""),
          kCheckNames[i]);
    out.Raw(kStatusFields);
    out.String(
        c.state == GltfWorkflowCoverageState::kPerformed ? kPerformedCoverage
        : c.state == GltfWorkflowCoverageState::kBlocked ? kSkippedCoverage
        : c.state == GltfWorkflowCoverageState::kUnsupported
            ? kUnsupportedCoverage
            : kFailedOutcome);
    out.Raw(kFindingFields);
    if (c.passed)
      out.String(*c.passed ? kPassedFinding : kFailedOutcome);
    else
      out.Raw("null");
    out.Raw(kReasonFields);
    if (c.state == GltfWorkflowCoverageState::kPerformed)
      out.Raw("null");
    else
      out.String(c.check == Check::kSolidValidity ? kSolidReason
                 : c.state == GltfWorkflowCoverageState::kUnsupported
                     ? kUnsupportedReason
                     : kBlockedReason);
    out.Raw(kValueDiagnosticsFields);
  }
  out.Raw(kMapsFields);
  if (s.repair) {
    const bool identity = s.repair->Outcome() == CandidateOutcome::unchanged;
    Map(out, kVertexMapId, kVertexDomain, true, {});
    out.Raw(",");
    Map(out, kFaceMapId, kFaceDomain, identity, s.repair->FaceMap());
    out.Raw(",");
    Map(out, kCornerMapId, kCornerDomain, identity, s.repair->CornerMap());
  }
  out.Raw(kPreservationFields);
  Preservation(out, s);
  out.Raw(kPublicationRequestedOutcomeSnapshotNativeStageFields);
  out.String(kStageNames[static_cast<std::size_t>(s.outcomes.stage)]);
  out.Raw(kRequestedPrimitiveOrdinalFields);
  out.OptionalInteger(r.primitive_ordinal);
  out.Raw(kUnresolvedTargetsFields);
  if (s.source)
    out.Raw("[]");
  else
    TargetPairs(out, r.targets);
  out.Raw(kMeshIndexFields);
  if (s.primitive)
    out.Integer(s.primitive->MeshIndex());
  else
    out.Raw("null");
  out.Raw(kPrimitiveIndexFields);
  if (s.primitive)
    out.Integer(s.primitive->PrimitiveIndex());
  else
    out.Raw("null");
  out.Raw(kSourceModeFields);
  if (s.extraction)
    out.Integer(s.extraction->SourceMode());
  else
    out.Raw("null");
  out.Raw(kInitialExtractionFields);
  ExtractionPolicy(out, r.extraction);
  out.Raw(kReloadExtractionFields);
  if (s.reload_policy)
    ExtractionPolicy(out, *s.reload_policy);
  else
    out.Raw("null");
  out.Raw(kChannelsFields);
  ChannelReceipts(out, s);
  out.Raw(kMaterializationEncodingsFields);
  comma = false;
  if (s.source)
    for (const auto& mapping : s.source->ChannelMappings()) {
      if (comma) out.Raw(",");
      comma = true;
      out.Raw(kSourceChannelFields);
      out.Integer(mapping.source_channel);
      out.Raw(kEncodingFields);
      out.String(
          mapping.encoding == interchange::GltfMaterializationEncoding::kExact
              ? kExactEncoding
          : mapping.encoding == interchange::GltfMaterializationEncoding::
                                    kFloatPositionWidened
              ? kFloatPositionWidened
              : kSignedIntegerWidened);
      out.Raw(kReasonFields);
      if (mapping.encoding == interchange::GltfMaterializationEncoding::kExact)
        out.Raw("null");
      else
        out.String(
            "Exact raw values use the canonical Geometry scalar "
            "representation; original wire accessor bytes are retained.");
      out.Raw("}");
    }
  out.Raw(kSourceFacesFields);
  if (s.repair)
    out.Integers(s.repair->SourceFaces());
  else
    out.Raw("[]");
  out.Raw(kSourceCornersFields);
  if (s.repair)
    out.Integers(s.repair->SourceCorners());
  else
    out.Raw("[]");
  out.Raw(kOriginalCornersFields);
  comma = false;
  if (s.repair)
    for (auto corner : s.repair->SourceCorners()) {
      const auto provenance = s.extraction->SourceCorners()[corner];
      if (comma) out.Raw(",");
      comma = true;
      out.Raw(kPrimitiveStreamCornerFields);
      out.Integer(provenance.primitive_stream_corner);
      out.Raw(kAccessorRowFields);
      out.Integer(provenance.accessor_row);
      out.Raw("}");
    }
  out.Raw(kChangedBindingFields);
  if (s.written && s.written->ChangedBinding()) {
    const auto change = *s.written->ChangedBinding();
    out.Raw(kValueSourceModeFields);
    out.Integer(change.source_mode);
    out.Raw(kSourceIndicesAccessorFields);
    out.OptionalInteger(change.source_indices_accessor);
    out.Raw(kOutputModeFields);
    out.Integer(change.output_mode);
    out.Raw(kOutputIndicesAccessorFields);
    out.Integer(change.output_indices_accessor);
    out.Raw(kAppendedBufferFields);
    out.Integer(change.appended_buffer);
    out.Raw(kAppendedBufferViewFields);
    out.Integer(change.appended_buffer_view);
    out.Raw(kValueReasonFields);
  } else
    out.Raw("null");
  out.Raw(kMemoryOutputVerifiedFields);
  out.Boolean(s.output.has_value());
  out.Raw(kPhaseExecutionFields);
  PhaseStats(out, s);
}
void Tail(Sink& out, const WorkflowState& s) {
  Stats(out, s.execution.report_copy);
  out.Raw("}}}}");
}
struct CopyPlan {
  std::span<const std::byte> source;
  std::span<std::byte> destination;
  std::stop_token stop;
  std::size_t block;
};
void CopyBytes(void* opaque, std::size_t begin, std::size_t end) {
  auto& plan = *static_cast<CopyPlan*>(opaque);
  while (begin < end) {
    if (plan.stop.stop_requested())
      throw allocation::AdmissionFailure(allocation::AdmissionCode::kCanceled);
    const auto count = std::min(plan.block, end - begin);
    std::copy_n(plan.source.begin() + static_cast<std::ptrdiff_t>(begin), count,
                plan.destination.begin() + static_cast<std::ptrdiff_t>(begin));
    begin += count;
  }
}
}  // namespace
void SerializeReport(WorkflowState& state,
                     const GltfDuplicateWorkflowRequest& request,
                     const GltfDuplicateWorkflowLimits& limits,
                     const GltfDuplicateWorkflowWorkPolicy& work,
                     const geometry::ExecutionContext& context,
                     std::stop_token stop, const WorkflowTestOptions& options) {
  Sink count({}, stop);
  Prefix(count, state, request);
  if (count.size() > limits.max_report_bytes) throw ReportExtentFailure{};
  Vector<std::byte> staging(count.size(), std::byte{},
                            allocation::Allocator<std::byte>(state.allocation));
  Sink prefix(staging, stop);
  Prefix(prefix, state, request);
  state.report.resize(
      allocation::CheckedSum(count.size(), kMaximumStatsTailBytes));
  CopyPlan plan{staging,
                std::span<std::byte>(state.report).first(staging.size()), stop,
                static_cast<std::size_t>(work.report.cancellation_block_bytes)};
  geometry::MeshMaterializationWorkPolicy policy{
      work.report.parallel_min_bytes, work.report.partition_bytes,
      work.report.cancellation_block_bytes, work.report.max_jobs};
  geometry::MaterializationExecutionStats stats;
  const auto record_stats = [&] {
    state.execution.report_copy = {stats.planned_jobs, stats.launched_workers,
                                   stats.joined_workers, stats.caller_jobs};
  };
  try {
    allocation::RunRange(context, state.allocation, policy, staging.size(),
                         sizeof(std::byte), CopyBytes, &plan, stop, stats,
                         options.report_worker_backend, &stats);
  } catch (...) {
    record_stats();
    throw;
  }
  record_stats();
  Sink tail(std::span<std::byte>(state.report).subspan(staging.size()), stop);
  Tail(tail, state);
  const auto size = allocation::CheckedSum(staging.size(), tail.size());
  if (size > limits.max_report_bytes) throw ReportExtentFailure{};
  state.report.resize(size);
}
}  // namespace meshvale::repair::gltf_workflow_detail
