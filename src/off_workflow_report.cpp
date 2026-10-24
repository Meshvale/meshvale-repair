// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_workflow.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gltf_file_sha256_internal.h"
#include "materialized_duplicate_allocation.h"
#include "off_workflow_internal.h"
#include "off_workflow_test_internal.h"

namespace meshvale::repair::off_workflow_detail {
namespace {
using Stage = OffWorkflowStage;
using Code = OffWorkflowDiagnosticCode;
using Status = OffWorkflowStatus;
using Check = OffWorkflowCheck;
constexpr std::array<std::string_view, kCoverageCount> kCheckNames{
    "source-storage",          "target-policy",
    "candidate-storage",       "repair-correspondence",
    "canonical-preservation",  "reload-candidate",
    "source-unchanged",        "staged-candidate",
    "staged-physical-members", "solid-validity"};
constexpr std::array<std::string_view, 10> kStageNames{
    "configuration", "source-protection",
    "repair",        "write",
    "reload",        "verify",
    "hash",          "report",
    "publication",   "complete"};
constexpr std::string_view kInput = "input", kCandidate = "candidate",
                           kOutput = "output", kMesh = "surface";
constexpr std::string_view kFailure =
    "The native OFF workflow stopped at its recorded stage and diagnostic.";
constexpr std::string_view kBlocked =
    "An earlier native stage blocked this required predicate.";
constexpr std::string_view kUnsupported =
    "This profile provides no solid-validity predicate.";
constexpr std::string_view kHex = "0123456789abcdef";
constexpr unsigned char kControlEnd = 0x20;
constexpr unsigned kHexBits = 4, kHexMask = 15;
constexpr std::size_t kEscapeBytes = 6;
constexpr std::size_t kIntegerBytes =
    std::numeric_limits<std::uint64_t>::digits10 + 1;
// Fixed four uint64 counters plus names/braces. Charged reserved capacity;
// the report-byte cap always applies to the actual completed UTF-8 length.
constexpr std::size_t kStatsTailCapacity = kReportStatsTailBytes;
constexpr std::string_view kJsonTrue = "true", kJsonFalse = "false";
constexpr std::string_view kPublicationOffField = ",\"publication_off\":";
// Exact version-1 JSON fragments belong to this OFF serializer.
constexpr std::string_view kJsonSnapshotObjectFields = "{\"snapshot\":";
constexpr std::string_view kJsonNull = "null";
constexpr std::string_view kJsonMeshFields = ",\"mesh\":";
constexpr std::string_view kJsonSubjectFields = ",\"subject\":";
constexpr std::string_view kJsonIdObjectFields = "{\"id\":";
constexpr std::string_view kJsonRoleFields = ",\"role\":";
constexpr std::string_view kJsonMeshesIdFields = ",\"meshes\":[{\"id\":";
constexpr std::string_view kJsonVertexCountFields = ",\"vertex_count\":";
constexpr std::string_view kJsonFaceCountFields = ",\"face_count\":";
constexpr std::string_view kJsonCornerCountFields = ",\"corner_count\":";
constexpr std::string_view kJsonFromSnapshotFields = ",\"from\":{\"snapshot\":";
constexpr std::string_view kJsonToSnapshotFields = "},\"to\":{\"snapshot\":";
constexpr std::string_view kJsonDomainFields = "},\"domain\":";
constexpr std::string_view kJsonKindFields = ",\"kind\":";
constexpr std::string_view kJsonEntriesFields = ",\"entries\":";
constexpr std::string_view kJsonPlannedJobsObjectFields = "{\"planned_jobs\":";
constexpr std::string_view kJsonLaunchedWorkersFields =
    ",\"launched_workers\":";
constexpr std::string_view kJsonJoinedWorkersFields = ",\"joined_workers\":";
constexpr std::string_view kJsonCallerJobsFields = ",\"caller_jobs\":";
constexpr std::string_view kJsonCodecObjectFields = "{\"codec\":";
constexpr std::string_view kJsonMaterializationFields = ",\"materialization\":";
constexpr std::string_view kJsonInspectionFields = ",\"inspection\":";
constexpr std::string_view kJsonCodeObjectFields = "{\"code\":";
constexpr std::string_view kJsonStageFields = ",\"stage\":";
constexpr std::string_view kJsonTargetFields = ",\"target\":";
constexpr std::string_view kJsonAttributeFields = ",\"attribute\":";
constexpr std::string_view kJsonElementFields = ",\"element\":";
constexpr std::string_view kJsonByteOffsetFields = ",\"byte_offset\":";
constexpr std::string_view kJsonVertexFields = ",\"vertex\":";
constexpr std::string_view kJsonFaceFields = ",\"face\":";
constexpr std::string_view kJsonGeometryStatusFields = ",\"geometry_status\":";
constexpr std::string_view kJsonGeometryDiagnosticFields =
    ",\"geometry_diagnostic\":";
constexpr std::string_view kJsonStorageIssueFields = ",\"storage_issue\":";
constexpr std::string_view kJsonStatusObjectFields = "{\"status\":";
constexpr std::string_view kJsonCodeFields = ",\"code\":";
constexpr std::string_view kJsonFileIndexFields = ",\"file_index\":";
constexpr std::string_view kJsonSystemErrorFields = ",\"system_error\":";
constexpr std::string_view kJsonValueObjectFields = "{\"value\":";
constexpr std::string_view kJsonCategoryFields = ",\"category\":";
constexpr std::string_view kJsonOffStatusFields = ",\"off_status\":";
constexpr std::string_view kJsonOffDiagnosticFields = ",\"off_diagnostic\":";
constexpr std::string_view kJsonTotalFields = ",\"total\":";
constexpr std::string_view kJsonFeatureKindNameDomainSetIndexFromFields =
    "[{\"feature\":{\"kind\":\"geometry\",\"name\":\"vertex-position-bits\","
    "\"domain\":\"vertex\",\"set_index\":null},\"from\":";
constexpr std::string_view kJsonToFields = ",\"to\":";
constexpr std::string_view kJsonOutcomeReasonMapsFields =
    ",\"outcome\":\"preserved\",\"reason\":null,\"maps\":[\"repair-vertices\"]}"
    ",";
constexpr std::string_view kJsonFeatureKindNameDomainSetIndexFromObjectFields =
    "{\"feature\":{\"kind\":\"geometry\",\"name\":\"retained-directed-polygon-"
    "loops\",\"domain\":null,\"set_index\":null},\"from\":";
constexpr std::string_view kJsonOutcomeReasonMapsFieldsValueStart =
    ",\"outcome\":\"preserved\",\"reason\":null,\"maps\":[\"repair-faces\","
    "\"repair-corners\"]},";
constexpr std::string_view
    kJsonFeatureKindNameDomainSetIndexFromObjectFieldsValueStart =
        "{\"feature\":{\"kind\":\"geometry\",\"name\":\"off-lexical-and-edge-"
        "count\",\"domain\":null,\"set_index\":null},\"from\":";
constexpr std::string_view kJsonOutcomeReasonMapsFieldsArrayEnd =
    ",\"outcome\":\"converted\",\"reason\":\"Canonical OFF drops comments, "
    "whitespace and numeric spelling and writes unknown edge count zero; units "
    "remain "
    "unknown.\",\"maps\":[\"repair-vertices\",\"repair-faces\",\"repair-"
    "corners\"]}]";
constexpr std::string_view kJsonSchemaToolNameVersionObjectFields =
    "{\"schema\":\"meshvale.report/"
    "1\",\"tool\":{\"name\":\"meshvale-repair\",\"version\":";
constexpr std::string_view kJsonInputIdFields = "},\"input\":{\"id\":";
constexpr std::string_view kJsonSha256Fields = ",\"sha256\":";
constexpr std::string_view kJsonOperationKindNameVersionOptionsTextFields =
    "},\"operation\":{\"kind\":\"repair\",\"name\":\"remove-duplicate-faces\","
    "\"version\":\"1\",\"options\":{\"text\":\"canonical-ascii\",\"edge_"
    "count\":\"unknown-zero\",\"targets\":[";
constexpr std::string_view kJsonKeepObjectFields = "{\"keep\":";
constexpr std::string_view kJsonRemoveFields = ",\"remove\":";
constexpr std::string_view kJsonTargetsFields = "]},\"targets\":[";
constexpr std::string_view kJsonSnapshotMeshDomainIndexObjectFields =
    "{\"snapshot\":\"input\",\"mesh\":\"surface\",\"domain\":\"face\","
    "\"index\":";
constexpr std::string_view kJsonExecutionOutcomeFields =
    "]},\"execution\":{\"outcome\":";
constexpr std::string_view kJsonReasonFields = ",\"reason\":";
constexpr std::string_view kJsonCandidateOutcomeFields =
    "},\"candidate\":{\"outcome\":";
constexpr std::string_view kJsonSnapshotFields = ",\"snapshot\":";
constexpr std::string_view kJsonProfileNameFields = "},\"profile\":{\"name\":";
constexpr std::string_view kJsonVersionRequiredChecksFields =
    ",\"version\":\"1\",\"required_checks\":[";
constexpr std::string_view kJsonOutcomeFields = "],\"outcome\":";
constexpr std::string_view kJsonSnapshotsFields = "},\"snapshots\":[";
constexpr std::string_view kJsonDiagnosticsFields = "],\"diagnostics\":[";
constexpr std::string_view kJsonIdCodeSeverityMessageObjectFields =
    "{\"id\":\"native-failure\",\"code\":\"off.workflow\",\"severity\":"
    "\"error\",\"message\":";
constexpr std::string_view kJsonScopeFields = ",\"scope\":";
constexpr std::string_view kJsonElementDataStageFields =
    ",\"element\":null,\"data\":{\"stage\":";
constexpr std::string_view kJsonElementValueFields = ",\"element_value\":";
constexpr std::string_view kJsonCoverageFields = "],\"coverage\":[";
constexpr std::string_view kJsonCheckFields = ",\"check\":";
constexpr std::string_view kJsonVersionScopeFields =
    ",\"version\":\"1\",\"scope\":";
constexpr std::string_view kJsonStatusFields = ",\"status\":";
constexpr std::string_view kJsonFindingFields = ",\"finding\":";
constexpr std::string_view kJsonDiagnosticsFieldsObjectEnd =
    ",\"diagnostics\":[]}";
constexpr std::string_view kJsonMapsFields = "],\"maps\":[";
constexpr std::string_view kJsonPreservationFields = "],\"preservation\":";
constexpr std::string_view kJsonPublicationRequestedFields =
    ",\"publication\":{\"requested\":";
constexpr std::string_view kJsonOutcomeFieldsValueStart = ",\"outcome\":";
constexpr std::string_view kJsonArtifactsFields = ",\"artifacts\":[";
constexpr std::string_view kJsonIdSha256ObjectFields =
    "{\"id\":\"entry\",\"sha256\":";
constexpr std::string_view kJsonVerifiedFields = "],\"verified\":";
constexpr std::string_view
    kJsonExtensionsMeshvaleRepairOffVersionNativeStatusFields =
        "},\"extensions\":{\"meshvale.repair.off\":{\"version\":1,\"native_"
        "status\":";
constexpr std::string_view kJsonNativeStageFields = ",\"native_stage\":";
constexpr std::string_view kJsonSourcePortableNameFields =
    ",\"source\":{\"portable_name\":";
constexpr std::string_view kJsonProfileDeclaredEdgeCountFields =
    ",\"profile\":\"ascii-records\",\"declared_edge_count\":";
constexpr std::string_view kJsonPoliciesTextEdgeCountUnitsFields =
    ",\"policies\":{\"text\":\"canonical-ascii\",\"edge_count\":\"unknown-"
    "zero\",\"units\":\"unknown\"}";
constexpr std::string_view kJsonMemoryOutputVerifiedFields =
    ",\"memory_output_verified\":";
constexpr std::string_view kJsonSourceFacesFields = ",\"source_faces\":";
constexpr std::string_view kJsonSourceCornersFields = ",\"source_corners\":";
constexpr std::string_view kJsonWriteFactsFields = ",\"write_facts\":";
constexpr std::string_view kJsonProfileObjectFields = "{\"profile\":";
constexpr std::string_view kJsonTextPolicyFields = ",\"text_policy\":";
constexpr std::string_view kJsonEdgeCountPolicyFields =
    ",\"edge_count_policy\":";
constexpr std::string_view kJsonEmittedEdgeCountFields =
    ",\"emitted_edge_count\":";
constexpr std::string_view kJsonCorrespondenceFields = ",\"correspondence\":";
constexpr std::string_view kJsonSourceDeclaredEdgeCountFields =
    ",\"source_declared_edge_count\":";
constexpr std::string_view kJsonPhaseDiagnosticsRepairFields =
    ",\"phase_diagnostics\":{\"repair\":";
constexpr std::string_view kJsonDiagnosticFields = ",\"diagnostic\":";
constexpr std::string_view kJsonWritingFields = ",\"writing\":";
constexpr std::string_view kJsonReloadFields = ",\"reload\":";
constexpr std::string_view kJsonPublisherFields = ",\"publisher\":";
constexpr std::string_view kJsonOutcomeObjectFields = "{\"outcome\":";
constexpr std::string_view kJsonCleanupFields = ",\"cleanup\":";
constexpr std::string_view kJsonCleanupErrorFields = ",\"cleanup_error\":";
constexpr std::string_view kJsonArtifactPathsFields = ",\"artifact_paths\":{";
constexpr std::string_view kJsonEntryFields = "\"entry\":";
constexpr std::string_view kJsonHashBytesProcessedFields =
    ",\"hash_bytes_processed\":";
constexpr std::string_view kJsonRequestedBytesBeforeReportCopyWrapperFields =
    ",\"requested_bytes_before_report_copy\":{\"wrapper\":";
constexpr std::string_view kJsonSourceFilesFields = ",\"source_files\":";
constexpr std::string_view kJsonSourceOffFields = ",\"source_off\":";
constexpr std::string_view kJsonRepairRepairFields = ",\"repair\":{\"repair\":";
constexpr std::string_view kJsonSourceInspectionFields =
    ",\"source_inspection\":";
constexpr std::string_view kJsonSelectionFields = ",\"selection\":";
constexpr std::string_view kJsonCandidateInspectionFields =
    ",\"candidate_inspection\":";
constexpr std::string_view kJsonPublicationFilesFields =
    ",\"publication_files\":";
constexpr std::string_view kJsonPhaseExecutionRepairSourceInspectionFields =
    ",\"phase_execution\":{\"repair\":{\"source_inspection\":";
constexpr std::string_view kJsonEligibilityFields = ",\"eligibility\":";
constexpr std::string_view kJsonComparisonFields = ",\"comparison\":";
constexpr std::string_view kJsonVerificationFields = ",\"verification\":";
constexpr std::string_view kJsonDirectedVerificationFields =
    ",\"directed_verification\":";
constexpr std::string_view kJsonSourceProtectionFields =
    ",\"source_protection\":";
constexpr std::string_view kJsonRepairCorrespondenceFields =
    ",\"repair_correspondence\":";
constexpr std::string_view kJsonCanonicalPreservationFields =
    ",\"canonical_preservation\":";
constexpr std::string_view kJsonReloadVerificationFields =
    ",\"reload_verification\":";
constexpr std::string_view kJsonSourceVerificationFields =
    ",\"source_verification\":";
constexpr std::string_view kJsonStagedCandidateFields =
    ",\"staged_candidate\":";
constexpr std::string_view kJsonHashingFields = ",\"hashing\":";
constexpr std::string_view kJsonReportCopyFields = ",\"report_copy\":";

constexpr std::string_view kAccepted = "accepted";
constexpr std::string_view kUnchanged = "unchanged";
constexpr std::string_view kRejected = "rejected";
constexpr std::string_view kNotAttempted = "not-attempted";
constexpr std::string_view kPassed = "passed";
constexpr std::string_view kFailed = "failed";
constexpr std::string_view kIncomplete = "incomplete";
constexpr std::string_view kNotRequested = "not-requested";
constexpr std::string_view kCancelled = "cancelled";
constexpr std::string_view kPublished = "published";
constexpr std::string_view kCompleteStage = "complete";
constexpr std::string_view kImportStage = "import";
constexpr std::string_view kInspectStage = "inspect";
constexpr std::string_view kRepairStage = "repair";
constexpr std::string_view kVerifyStage = "verify";
constexpr std::string_view kReportStage = "report";
constexpr std::string_view kExportStage = "export";
constexpr std::string_view kCompleted = "completed";
constexpr std::string_view kPerformed = "performed";
constexpr std::string_view kSkipped = "skipped";
constexpr std::string_view kUnsupportedCoverage = "unsupported";
constexpr std::string_view kPublicationProfile =
    "off-indexed-duplicate-publication";
constexpr std::string_view kPreservationProfile =
    "off-indexed-duplicate-preservation";
constexpr std::string_view kPositionBitsSubject = "position-bits";
constexpr std::string_view kRetainedLoopsSubject = "retained-loops";
constexpr std::string_view kSourceTextSubject = "source-text";
constexpr std::string_view kCanonicalSerializationSubject =
    "canonical-memory-serialization";
constexpr std::string_view kNativeWorkflowSubject = "native-off-workflow";
constexpr std::string_view kPredicateSubject = "preservation-predicate";
constexpr std::string_view kRepairVertexMap = "repair-vertices";
constexpr std::string_view kRepairFaceMap = "repair-faces";
constexpr std::string_view kRepairCornerMap = "repair-corners";
constexpr std::string_view kExportVertexMap = "export-vertices";
constexpr std::string_view kExportFaceMap = "export-faces";
constexpr std::string_view kExportCornerMap = "export-corners";
constexpr std::string_view kVertexDomain = "vertex";
constexpr std::string_view kFaceDomain = "face";
constexpr std::string_view kCornerDomain = "corner";
constexpr std::string_view kIdentityMap = "identity";
constexpr std::string_view kExplicitMap = "explicit";

class Sink {
 public:
  Sink(Session& session, std::uint64_t cap, std::span<std::byte> output = {})
      : session_(session), cap_(cap), output_(output) {}
  void Raw(std::string_view value) {
    if (size_ > cap_ || value.size() > cap_ - size_)
      Fail(Status::kLimitExceeded, Stage::kReport, Code::kReportExtent);
    if (!output_.empty() && value.size() > output_.size() - size_)
      Fail(Status::kLimitExceeded, Stage::kReport, Code::kReportExtent);
    for (unsigned char c : value) {
      if (size_ % session_.work.report.cancellation_block_bytes == 0)
        session_.Check(Stage::kReport, size_);
      if (!output_.empty()) output_[size_] = static_cast<std::byte>(c);
      ++size_;
    }
  }
  void String(std::string_view value) {
    Raw("\"");
    for (unsigned char c : value) {
      if (c == '"')
        Raw("\\\"");
      else if (c == '\\')
        Raw("\\\\");
      else if (c < kControlEnd) {
        const std::array<char, kEscapeBytes> escape{
            '\\', 'u', '0', '0', kHex[c >> kHexBits], kHex[c & kHexMask]};
        Raw({escape.data(), escape.size()});
      } else {
        const char byte = static_cast<char>(c);
        Raw({&byte, 1});
      }
    }
    Raw("\"");
  }
  void Integer(std::uint64_t value) {
    std::array<char, kIntegerBytes> bytes;
    const auto converted =
        std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
    Raw({bytes.data(), static_cast<std::size_t>(converted.ptr - bytes.data())});
  }
  void Signed(std::int64_t value) {
    constexpr std::size_t kSignedBytes =
        std::numeric_limits<std::int64_t>::digits10 + 2;
    std::array<char, kSignedBytes> bytes;
    const auto converted =
        std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
    Raw({bytes.data(), static_cast<std::size_t>(converted.ptr - bytes.data())});
  }
  void Boolean(bool value) { Raw(value ? kJsonTrue : kJsonFalse); }
  template <class T>
  void Optional(const std::optional<T>& value) {
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
  std::size_t Size() const noexcept { return size_; }

 private:
  Session& session_;
  std::uint64_t cap_;
  std::span<std::byte> output_;
  std::size_t size_ = 0;
};
std::string_view Candidate(const State& state) {
  if (!state.outcomes.candidate) return kNotAttempted;
  switch (*state.outcomes.candidate) {
    case CandidateOutcome::accepted:
      return kAccepted;
    case CandidateOutcome::unchanged:
      return kUnchanged;
    case CandidateOutcome::rejected:
      return kRejected;
  }
  std::terminate();
}
std::string_view Profile(OffWorkflowProfileOutcome value) {
  switch (value) {
    case OffWorkflowProfileOutcome::kPassed:
      return kPassed;
    case OffWorkflowProfileOutcome::kFailed:
      return kFailed;
    case OffWorkflowProfileOutcome::kIncomplete:
      return kIncomplete;
  }
  std::terminate();
}
std::string_view Publication(OffWorkflowPublicationOutcome value) {
  switch (value) {
    case OffWorkflowPublicationOutcome::kNotRequested:
      return kNotRequested;
    case OffWorkflowPublicationOutcome::kNotAttempted:
      return kNotAttempted;
    case OffWorkflowPublicationOutcome::kFailed:
      return kFailed;
    case OffWorkflowPublicationOutcome::kCanceled:
      return kCancelled;
    case OffWorkflowPublicationOutcome::kPublished:
      return kPublished;
  }
  std::terminate();
}
std::string_view CoreStage(const State& state, bool conditional) {
  if (conditional ||
      state.outcomes.execution == OffWorkflowExecutionOutcome::kCompleted)
    return kCompleteStage;
  switch (state.outcomes.stage) {
    case Stage::kConfiguration:
    case Stage::kSourceProtection:
      return kImportStage;
    case Stage::kRepair:
      return state.repair_diagnostic.stage ==
                     MaterializedDuplicateStage::kSourceInspection
                 ? kInspectStage
                 : kRepairStage;
    case Stage::kWrite:
    case Stage::kReload:
    case Stage::kVerify:
      return kVerifyStage;
    case Stage::kHash:
      return state.coverage[static_cast<std::size_t>(Check::kStagedCandidate)]
                         .passed == true
                 ? kReportStage
                 : kVerifyStage;
    case Stage::kReport:
    case Stage::kComplete:
      return kReportStage;
    case Stage::kPublication:
      return kExportStage;
  }
  std::terminate();
}
void Scope(Sink& out, std::string_view snapshot, std::string_view subject) {
  out.Raw(kJsonSnapshotObjectFields);
  if (snapshot.empty())
    out.Raw(kJsonNull);
  else
    out.String(snapshot);
  out.Raw(kJsonMeshFields);
  if (snapshot.empty())
    out.Raw(kJsonNull);
  else
    out.String(kMesh);
  out.Raw(kJsonSubjectFields);
  out.String(subject);
  out.Raw("}");
}
void Snapshot(Sink& out, std::string_view role,
              const geometry::MaterializedMesh& mesh) {
  const auto view = mesh.View();
  out.Raw(kJsonIdObjectFields);
  out.String(role);
  out.Raw(kJsonRoleFields);
  out.String(role);
  out.Raw(kJsonMeshesIdFields);
  out.String(kMesh);
  out.Raw(kJsonVertexCountFields);
  out.Integer(view.positions.size() / kPositionWidth);
  out.Raw(kJsonFaceCountFields);
  out.Integer(view.face_offsets.size() - 1);
  out.Raw(kJsonCornerCountFields);
  out.Integer(view.corner_vertices.size());
  out.Raw("}]}");
}
void Map(Sink& out, std::string_view id, std::string_view domain,
         std::string_view from, std::string_view to, bool identity,
         std::span<const geometry::index_t> entries = {}) {
  out.Raw(kJsonIdObjectFields);
  out.String(id);
  out.Raw(kJsonFromSnapshotFields);
  out.String(from);
  out.Raw(kJsonMeshFields);
  out.String(kMesh);
  out.Raw(kJsonToSnapshotFields);
  out.String(to);
  out.Raw(kJsonMeshFields);
  out.String(kMesh);
  out.Raw(kJsonDomainFields);
  out.String(domain);
  out.Raw(kJsonKindFields);
  out.String(identity ? kIdentityMap : kExplicitMap);
  out.Raw(kJsonEntriesFields);
  if (identity)
    out.Raw(kJsonNull);
  else
    out.Integers(entries);
  out.Raw("}");
}
template <class T>
void Stats(Sink& out, const T& value) {
  out.Raw(kJsonPlannedJobsObjectFields);
  out.Integer(value.planned_jobs);
  out.Raw(kJsonLaunchedWorkersFields);
  out.Integer(value.launched_workers);
  out.Raw(kJsonJoinedWorkersFields);
  out.Integer(value.joined_workers);
  out.Raw(kJsonCallerJobsFields);
  out.Integer(value.caller_jobs);
  out.Raw("}");
}
void OffStats(Sink& out, const interchange::OffExecution& value) {
  out.Raw(kJsonCodecObjectFields);
  Stats(out, value.codec);
  out.Raw(kJsonMaterializationFields);
  Stats(out, value.materialization);
  out.Raw(kJsonInspectionFields);
  Stats(out, value.inspection);
  out.Raw("}");
}
template <class T>
void Diagnostic(Sink& out, const T& value) {
  out.Raw(kJsonCodeObjectFields);
  out.Integer(static_cast<std::uint64_t>(value.code));
  if constexpr (requires { value.stage; }) {
    out.Raw(kJsonStageFields);
    out.Integer(static_cast<std::uint64_t>(value.stage));
  }
  if constexpr (requires { value.target; }) {
    out.Raw(kJsonTargetFields);
    out.Optional(value.target);
  }
  if constexpr (requires { value.attribute; }) {
    out.Raw(kJsonAttributeFields);
    out.Optional(value.attribute);
  }
  if constexpr (requires { value.element; }) {
    out.Raw(kJsonElementFields);
    out.Optional(value.element);
  }
  if constexpr (requires { value.byte_offset; }) {
    out.Raw(kJsonByteOffsetFields);
    out.Optional(value.byte_offset);
  }
  if constexpr (requires { value.vertex; }) {
    out.Raw(kJsonVertexFields);
    out.Optional(value.vertex);
  }
  if constexpr (requires { value.face; }) {
    out.Raw(kJsonFaceFields);
    out.Optional(value.face);
  }
  if constexpr (requires { value.geometry_status; }) {
    out.Raw(kJsonGeometryStatusFields);
    out.Optional(value.geometry_status);
    out.Raw(kJsonGeometryDiagnosticFields);
    if (value.geometry_diagnostic)
      Diagnostic(out, *value.geometry_diagnostic);
    else
      out.Raw(kJsonNull);
  }
  if constexpr (requires { value.storage_issue; }) {
    out.Raw(kJsonStorageIssueFields);
    if (value.storage_issue)
      Diagnostic(out, *value.storage_issue);
    else
      out.Raw(kJsonNull);
  }
  out.Raw("}");
}
void FileDiagnostic(Sink& out, const interchange::OffFileDiagnostic& value) {
  out.Raw(kJsonStatusObjectFields);
  out.Integer(static_cast<std::uint64_t>(value.status));
  out.Raw(kJsonStageFields);
  out.Integer(static_cast<std::uint64_t>(value.stage));
  out.Raw(kJsonCodeFields);
  out.Integer(static_cast<std::uint64_t>(value.code));
  out.Raw(kJsonFileIndexFields);
  out.Optional(value.file_index);
  out.Raw(kJsonByteOffsetFields);
  out.Optional(value.byte_offset);
  out.Raw(kJsonSystemErrorFields);
  if (value.system_error) {
    out.Raw(kJsonValueObjectFields);
    out.Signed(value.system_error->value());
    out.Raw(kJsonCategoryFields);
    out.String(value.system_error->category().name());
    out.Raw("}");
  } else
    out.Raw(kJsonNull);
  out.Raw(kJsonOffStatusFields);
  out.Optional(value.off_status);
  out.Raw(kJsonOffDiagnosticFields);
  if (value.off_diagnostic)
    Diagnostic(out, *value.off_diagnostic);
  else
    out.Raw(kJsonNull);
  out.Raw(kJsonGeometryStatusFields);
  out.Optional(value.geometry_status);
  out.Raw(kJsonGeometryDiagnosticFields);
  if (value.geometry_diagnostic)
    Diagnostic(out, *value.geometry_diagnostic);
  else
    out.Raw(kJsonNull);
  out.Raw("}");
}
void OffBytes(Sink& out, const interchange::OffRequestedBytes& value) {
  out.Raw(kJsonCodecObjectFields);
  out.Integer(value.codec);
  out.Raw(kJsonMaterializationFields);
  out.Integer(value.materialization);
  out.Raw(kJsonInspectionFields);
  out.Integer(value.inspection);
  out.Raw(kJsonTotalFields);
  out.Integer(value.total);
  out.Raw("}");
}
void Claims(Sink& out, const State& state) {
  if (!state.memory_verified) {
    out.Raw("[]");
    return;
  }
  out.Raw(kJsonFeatureKindNameDomainSetIndexFromFields);
  Scope(out, kInput, kPositionBitsSubject);
  out.Raw(kJsonToFields);
  Scope(out, kCandidate, kPositionBitsSubject);
  out.Raw(kJsonOutcomeReasonMapsFields);
  out.Raw(kJsonFeatureKindNameDomainSetIndexFromObjectFields);
  Scope(out, kInput, kRetainedLoopsSubject);
  out.Raw(kJsonToFields);
  Scope(out, kCandidate, kRetainedLoopsSubject);
  out.Raw(kJsonOutcomeReasonMapsFieldsValueStart);
  out.Raw(kJsonFeatureKindNameDomainSetIndexFromObjectFieldsValueStart);
  Scope(out, kInput, kSourceTextSubject);
  out.Raw(kJsonToFields);
  Scope(out, kCandidate, kCanonicalSerializationSubject);
  out.Raw(kJsonOutcomeReasonMapsFieldsArrayEnd);
}
void Prefix(Sink& out, const State& state,
            const OffDuplicateFileWorkflowRequest& request, bool conditional,
            const OffWorkflowRequestedBytes& bytes) {
  const bool published =
      conditional ||
      state.outcomes.publication == OffWorkflowPublicationOutcome::kPublished;
  const bool requested = request.publication.has_value();
  const auto execution = conditional ? OffWorkflowExecutionOutcome::kCompleted
                                     : state.outcomes.execution;
  out.Raw(kJsonSchemaToolNameVersionObjectFields);
  out.String(MESHVALE_REPAIR_VERSION);
  out.Raw(kJsonInputIdFields);
  out.String({state.input_id.data(), state.input_id.size()});
  out.Raw(kJsonSha256Fields);
  if (state.input_hash_available)
    out.String({state.input_hash.data(), state.input_hash.size()});
  else
    out.Raw(kJsonNull);
  out.Raw(kJsonOperationKindNameVersionOptionsTextFields);
  bool comma = false;
  for (const auto& target : request.targets) {
    if (comma) out.Raw(",");
    comma = true;
    out.Raw(kJsonKeepObjectFields);
    out.Integer(target.keep);
    out.Raw(kJsonRemoveFields);
    out.Integer(target.remove);
    out.Raw("}");
  }
  out.Raw(kJsonTargetsFields);
  comma = false;
  for (const auto& target : request.targets) {
    if (comma) out.Raw(",");
    comma = true;
    out.Raw(kJsonSnapshotMeshDomainIndexObjectFields);
    out.Integer(target.remove);
    out.Raw("}");
  }
  out.Raw(kJsonExecutionOutcomeFields);
  out.String(execution == OffWorkflowExecutionOutcome::kCompleted  ? kCompleted
             : execution == OffWorkflowExecutionOutcome::kCanceled ? kCancelled
                                                                   : kFailed);
  out.Raw(kJsonStageFields);
  out.String(CoreStage(state, conditional));
  out.Raw(kJsonReasonFields);
  if (execution == OffWorkflowExecutionOutcome::kCompleted)
    out.Raw(kJsonNull);
  else
    out.String(kFailure);
  out.Raw(kJsonCandidateOutcomeFields);
  out.String(Candidate(state));
  out.Raw(kJsonSnapshotFields);
  if (state.repair)
    out.String(kCandidate);
  else
    out.Raw(kJsonNull);
  out.Raw(kJsonProfileNameFields);
  out.String(requested ? kPublicationProfile : kPreservationProfile);
  out.Raw(kJsonVersionRequiredChecksFields);
  const auto required = requested ? kPublicationChecks : kMemoryChecks;
  for (std::size_t i = 0; i < required; ++i) {
    if (i) out.Raw(",");
    out.String(kCheckNames[i]);
  }
  out.Raw(kJsonOutcomeFields);
  out.String(conditional ? kPassed : Profile(state.outcomes.profile));
  out.Raw(kJsonSnapshotsFields);
  Snapshot(out, kInput, state.source.Document().Mesh());
  if (state.repair) {
    out.Raw(",");
    Snapshot(out, kCandidate, state.repair->Candidate());
  }
  if (published) {
    out.Raw(",");
    Snapshot(out, kOutput, state.repair->Candidate());
  }
  out.Raw(kJsonDiagnosticsFields);
  if (state.diagnostic.code != Code::kNone) {
    out.Raw(kJsonIdCodeSeverityMessageObjectFields);
    out.String(kFailure);
    out.Raw(kJsonScopeFields);
    Scope(out, kInput, kNativeWorkflowSubject);
    out.Raw(kJsonElementDataStageFields);
    out.String(kStageNames[static_cast<std::size_t>(state.diagnostic.stage)]);
    out.Raw(kJsonCodeFields);
    out.Integer(static_cast<std::uint64_t>(state.diagnostic.code));
    out.Raw(kJsonElementValueFields);
    out.Optional(state.diagnostic.element);
    out.Raw("}}");
  }
  out.Raw(kJsonCoverageFields);
  for (std::size_t i = 0; i < state.coverage.size(); ++i) {
    if (i) out.Raw(",");
    auto coverage = state.coverage[i];
    if (conditional && coverage.check == Check::kStagedPhysicalMembers) {
      coverage.state = OffWorkflowCoverageState::kPerformed;
      coverage.passed = true;
      coverage.reason = Code::kNone;
    }
    out.Raw(kJsonIdObjectFields);
    out.String(kCheckNames[i]);
    out.Raw(kJsonCheckFields);
    out.String(kCheckNames[i]);
    out.Raw(kJsonVersionScopeFields);
    const bool input = coverage.check == Check::kSourceStorage ||
                       coverage.check == Check::kTargetPolicy ||
                       coverage.check == Check::kSourceUnchanged;
    Scope(out,
          input          ? kInput
          : state.repair ? kCandidate
                         : std::string_view{},
          kPredicateSubject);
    out.Raw(kJsonStatusFields);
    switch (coverage.state) {
      case OffWorkflowCoverageState::kPerformed:
        out.String(kPerformed);
        break;
      case OffWorkflowCoverageState::kSkipped:
        out.String(kSkipped);
        break;
      case OffWorkflowCoverageState::kUnsupported:
        out.String(kUnsupportedCoverage);
        break;
      case OffWorkflowCoverageState::kFailed:
        out.String(kFailed);
        break;
    }
    out.Raw(kJsonFindingFields);
    if (coverage.state == OffWorkflowCoverageState::kPerformed)
      out.String(coverage.passed == true ? kPassed : kFailed);
    else
      out.Raw(kJsonNull);
    out.Raw(kJsonReasonFields);
    if (coverage.state == OffWorkflowCoverageState::kPerformed)
      out.Raw(kJsonNull);
    else
      out.String(coverage.state == OffWorkflowCoverageState::kUnsupported
                     ? kUnsupported
                     : kBlocked);
    out.Raw(kJsonDiagnosticsFieldsObjectEnd);
  }
  out.Raw(kJsonMapsFields);
  if (state.repair) {
    const bool identity =
        state.repair->Outcome() == CandidateOutcome::unchanged;
    Map(out, kRepairVertexMap, kVertexDomain, kInput, kCandidate, true);
    out.Raw(",");
    Map(out, kRepairFaceMap, kFaceDomain, kInput, kCandidate, identity,
        state.repair->FaceMap());
    out.Raw(",");
    Map(out, kRepairCornerMap, kCornerDomain, kInput, kCandidate, identity,
        state.repair->CornerMap());
    if (published) {
      out.Raw(",");
      Map(out, kExportVertexMap, kVertexDomain, kCandidate, kOutput, true);
      out.Raw(",");
      Map(out, kExportFaceMap, kFaceDomain, kCandidate, kOutput, true);
      out.Raw(",");
      Map(out, kExportCornerMap, kCornerDomain, kCandidate, kOutput, true);
    }
  }
  out.Raw(kJsonPreservationFields);
  Claims(out, state);
  out.Raw(kJsonPublicationRequestedFields);
  out.Boolean(requested);
  out.Raw(kJsonOutcomeFieldsValueStart);
  out.String(published ? kPublished : Publication(state.outcomes.publication));
  out.Raw(kJsonSnapshotFields);
  if (published)
    out.String(kOutput);
  else
    out.Raw(kJsonNull);
  out.Raw(kJsonArtifactsFields);
  if (published) {
    out.Raw(kJsonIdSha256ObjectFields);
    out.String({state.output_hash.data(), state.output_hash.size()});
    out.Raw("}");
  }
  out.Raw(kJsonVerifiedFields);
  out.Boolean(published);
  out.Raw(kJsonReasonFields);
  if (published || !requested)
    out.Raw(kJsonNull);
  else
    out.String(kFailure);
  out.Raw(kJsonExtensionsMeshvaleRepairOffVersionNativeStatusFields);
  out.Integer(static_cast<std::uint64_t>(conditional ? Status::kSuccess
                                                     : state.status));
  out.Raw(kJsonNativeStageFields);
  out.String(kStageNames[static_cast<std::size_t>(
      conditional ? Stage::kComplete : state.outcomes.stage)]);
  out.Raw(kJsonSourcePortableNameFields);
  out.String(state.source.EntryPath());
  out.Raw(kJsonProfileDeclaredEdgeCountFields);
  out.Integer(state.source.Document().DeclaredEdgeCount());
  out.Raw("}");
  out.Raw(kJsonPoliciesTextEdgeCountUnitsFields);
  out.Raw(kJsonMemoryOutputVerifiedFields);
  out.Boolean(state.memory_verified);
  out.Raw(kJsonSourceFacesFields);
  if (state.repair)
    out.Integers(state.repair->SourceFaces());
  else
    out.Raw(kJsonNull);
  out.Raw(kJsonSourceCornersFields);
  if (state.repair)
    out.Integers(state.repair->SourceCorners());
  else
    out.Raw(kJsonNull);
  out.Raw(kJsonWriteFactsFields);
  if (state.written) {
    const auto& facts = state.written->Facts();
    out.Raw(kJsonProfileObjectFields);
    out.Integer(static_cast<std::uint64_t>(facts.profile));
    out.Raw(kJsonVertexCountFields);
    out.Integer(facts.vertex_count);
    out.Raw(kJsonFaceCountFields);
    out.Integer(facts.face_count);
    out.Raw(kJsonCornerCountFields);
    out.Integer(facts.corner_count);
    out.Raw(kJsonTextPolicyFields);
    out.Integer(static_cast<std::uint64_t>(facts.text_policy));
    out.Raw(kJsonEdgeCountPolicyFields);
    out.Integer(static_cast<std::uint64_t>(facts.edge_count_policy));
    out.Raw(kJsonEmittedEdgeCountFields);
    out.Integer(facts.emitted_edge_count);
    out.Raw(kJsonCorrespondenceFields);
    out.Integer(static_cast<std::uint64_t>(facts.correspondence));
    out.Raw(kJsonSourceDeclaredEdgeCountFields);
    out.Optional(facts.source_declared_edge_count);
    out.Raw("}");
  } else
    out.Raw(kJsonNull);
  out.Raw(kJsonPhaseDiagnosticsRepairFields);
  if (state.repair_status) {
    out.Raw(kJsonStatusObjectFields);
    out.Integer(static_cast<std::uint64_t>(*state.repair_status));
    out.Raw(kJsonDiagnosticFields);
    Diagnostic(out, state.repair_diagnostic);
    out.Raw("}");
  } else
    out.Raw(kJsonNull);
  out.Raw(kJsonWritingFields);
  if (state.write_status) {
    out.Raw(kJsonStatusObjectFields);
    out.Integer(static_cast<std::uint64_t>(*state.write_status));
    out.Raw(kJsonDiagnosticFields);
    Diagnostic(out, state.write_diagnostic);
    out.Raw("}");
  } else
    out.Raw(kJsonNull);
  out.Raw(kJsonReloadFields);
  if (state.reload_status) {
    out.Raw(kJsonStatusObjectFields);
    out.Integer(static_cast<std::uint64_t>(*state.reload_status));
    out.Raw(kJsonDiagnosticFields);
    Diagnostic(out, state.reload_diagnostic);
    out.Raw("}");
  } else
    out.Raw(kJsonNull);
  out.Raw("}");
  out.Raw(kJsonPublisherFields);
  if (state.publication && !conditional) {
    const auto& publication = *state.publication;
    out.Raw(kJsonOutcomeObjectFields);
    out.Integer(static_cast<std::uint64_t>(publication.outcome));
    out.Raw(kJsonDiagnosticFields);
    FileDiagnostic(out, publication.diagnostic);
    out.Raw(kJsonCleanupFields);
    out.Integer(static_cast<std::uint64_t>(publication.cleanup));
    out.Raw(kJsonCleanupErrorFields);
    if (publication.cleanup_error) {
      out.Raw(kJsonValueObjectFields);
      out.Signed(publication.cleanup_error->value());
      out.Raw(kJsonCategoryFields);
      out.String(publication.cleanup_error->category().name());
      out.Raw("}");
    } else
      out.Raw(kJsonNull);
    out.Raw("}");
  } else
    out.Raw(kJsonNull);
  out.Raw(kJsonArtifactPathsFields);
  if (published) {
    out.Raw(kJsonEntryFields);
    out.String(state.source.EntryPath());
  }
  out.Raw("}");
  out.Raw(kJsonHashBytesProcessedFields);
  out.Integer(state.hash_bytes);
  out.Raw(kJsonRequestedBytesBeforeReportCopyWrapperFields);
  out.Integer(bytes.wrapper);
  out.Raw(kJsonSourceFilesFields);
  out.Integer(bytes.source_files);
  out.Raw(kJsonSourceOffFields);
  OffBytes(out, bytes.source_off);
  out.Raw(kJsonRepairRepairFields);
  out.Integer(bytes.repair.repair);
  out.Raw(kJsonSourceInspectionFields);
  out.Integer(bytes.repair.source_inspection);
  out.Raw(kJsonSelectionFields);
  out.Integer(bytes.repair.selection);
  out.Raw(kJsonCandidateInspectionFields);
  out.Integer(bytes.repair.candidate_inspection);
  out.Raw(kJsonTotalFields);
  out.Integer(bytes.repair.total);
  out.Raw("}");
  out.Raw(kJsonWritingFields);
  OffBytes(out, bytes.writing);
  out.Raw(kJsonReloadFields);
  OffBytes(out, bytes.reload);
  out.Raw(kJsonPublicationFilesFields);
  // The publisher has not returned its owning bundle during conditional
  // receipt preparation; do not invent live operation counters from zero.
  if (conditional)
    out.Raw(kJsonNull);
  else
    out.Integer(bytes.publication_files);
  out.Raw(kPublicationOffField);
  if (conditional)
    out.Raw(kJsonNull);
  else
    OffBytes(out, bytes.publication_off);
  out.Raw("}");
  const auto& execution_stats = state.execution;
  out.Raw(kJsonPhaseExecutionRepairSourceInspectionFields);
  Stats(out, execution_stats.repair.source_inspection);
  out.Raw(kJsonEligibilityFields);
  Stats(out, execution_stats.repair.eligibility);
  out.Raw(kJsonComparisonFields);
  Stats(out, execution_stats.repair.comparison);
  out.Raw(kJsonSelectionFields);
  Stats(out, execution_stats.repair.selection);
  out.Raw(kJsonCorrespondenceFields);
  Stats(out, execution_stats.repair.correspondence);
  out.Raw(kJsonCandidateInspectionFields);
  Stats(out, execution_stats.repair.candidate_inspection);
  out.Raw(kJsonVerificationFields);
  Stats(out, execution_stats.repair.verification);
  out.Raw(kJsonDirectedVerificationFields);
  Stats(out, execution_stats.repair.directed_verification);
  out.Raw("}");
  out.Raw(kJsonWritingFields);
  OffStats(out, execution_stats.writing);
  out.Raw(kJsonReloadFields);
  OffStats(out, execution_stats.reload);
  out.Raw(kJsonSourceProtectionFields);
  Stats(out, execution_stats.source_protection);
  out.Raw(kJsonRepairCorrespondenceFields);
  Stats(out, execution_stats.repair_correspondence);
  out.Raw(kJsonCanonicalPreservationFields);
  Stats(out, execution_stats.canonical_preservation);
  out.Raw(kJsonReloadVerificationFields);
  Stats(out, execution_stats.reload_verification);
  out.Raw(kJsonSourceVerificationFields);
  Stats(out, execution_stats.source_verification);
  out.Raw(kJsonStagedCandidateFields);
  Stats(out, execution_stats.staged_candidate);
  out.Raw(kJsonHashingFields);
  Stats(out, execution_stats.hashing);
  out.Raw(kJsonReportCopyFields);
}
}  // namespace
void SerializeReport(Session& session,
                     const OffDuplicateFileWorkflowRequest& request,
                     const OffDuplicateFileWorkflowLimits& limits,
                     bool conditional) {
  session.Check(Stage::kReport);
  // Counting traverses only fixed/native bounded strings and actual map rows;
  // the same sink and owning stop checks are used in the emission pass.
  const auto sampled_bytes = session.state.Bytes();
  Sink count(session, limits.max_report_bytes);
  Prefix(count, session.state, request, conditional, sampled_bytes);
  const auto size = count.Size();
  Buffer staging(session.state.allocation, size);
  Sink prefix(session, limits.max_report_bytes, staging.Bytes());
  Prefix(prefix, session.state, request, conditional, sampled_bytes);
  if (prefix.Size() != size)
    Fail(Status::kVerificationFailure, Stage::kReport, Code::kReportExtent);
  const auto capacity = allocation::CheckedSum(size, kStatsTailCapacity);
  Buffer final(session.state.allocation, capacity);
  CopyBytes(session, staging.Bytes(), final.Bytes().first(size), Stage::kReport,
            session.work.report, session.state.execution.report_copy,
            session.testing.report_worker_backend);
  Sink tail(session, limits.max_report_bytes - size,
            final.Bytes().subspan(size));
  Stats(tail, session.state.execution.report_copy);
  tail.Raw("}}}}");
  final.size = allocation::CheckedSum(size, tail.Size());
  session.Check(Stage::kReport, final.size);
  session.state.report.emplace(std::move(final));
}
}  // namespace meshvale::repair::off_workflow_detail
