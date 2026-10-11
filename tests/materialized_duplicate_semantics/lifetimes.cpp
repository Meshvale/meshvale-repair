// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

#include "fixture_support.h"

namespace meshvale::repair::materialized_semantics {
namespace {
constexpr std::array<DuplicateTarget, 1> kRemoveSecond = {{{0, 1}}};
constexpr std::array<geo::index_t, 2> kFaceMap = {0, 0};
constexpr std::array<geo::index_t, 6> kCornerMap = {0, 1, 2, 0, 1, 2};
constexpr std::array<geo::index_t, 3> kRetainedCorners = {0, 1, 2};
constexpr std::array<geo::index_t, 1> kRetainedFaces = {0};
constexpr std::uint64_t kNegativeZeroBits = 0x8000000000000000ULL;
constexpr std::string_view kRetainedName = "retained_label";
constexpr std::string_view kRetainedMetadataKey = "authored_key";
constexpr std::string_view kRetainedMetadataValue = "authored_value";

void OwnerLifetimes() {
  std::optional<MaterializedDuplicateRepair> retained;
  std::optional<geo::MaterializedMesh> candidate;
  std::optional<geo::MeshInspection> inspection;
  std::optional<geo::ExecutionContext> ledger;
  {
    auto fixture = TwoTriangles();
    fixture.positions[0] = -0.0;
    AttributeFixture channel;
    channel.domain = geo::AttributeDomain::face;
    channel.name = kRetainedName;
    channel.values =
        std::vector<std::uint64_t>{9007199254740993ULL, 9007199254740993ULL};
    channel.metadata = {{std::string(kRetainedMetadataKey),
                         std::string(kRetainedMetadataValue)}};
    fixture.attributes = {channel};
    auto context = Context();
    const auto source = Capture(fixture, context);
    auto result = Repair(source, kRemoveSecond);
    Accepted(result);
    retained = result.repair;
    candidate = retained->Candidate();
    inspection = retained->CandidateInspection();
    ledger = source.ShareExecutionContext();
    Require(geo::SharesExecutionBudget(context, *ledger),
            "Repair source no longer owns the retained execution ledger");
  }
  ExpectIndices(retained->FaceMap(), kFaceMap);
  ExpectIndices(retained->CornerMap(), kCornerMap);
  ExpectIndices(retained->SourceFaces(), kRetainedFaces);
  ExpectIndices(retained->SourceCorners(), kRetainedCorners);
  ExpectRawRows(retained->Source().View(), candidate->View(), kRetainedFaces,
                kRetainedCorners);
  const auto shared_bytes = ledger->ActiveTrackedPayload();
  auto copied = retained;
  Require(ledger->ActiveTrackedPayload() == shared_bytes,
          "Copying an immutable Repair owner allocated new charged blocks");
  retained.reset();
  ExpectIndices(copied->CornerMap(), kCornerMap);
  copied.reset();
  const auto view = candidate->View();
  Require(
      std::bit_cast<std::uint64_t>(view.positions[0]) == kNegativeZeroBits &&
          view.attributes[0].name == kRetainedName &&
          view.attributes[0].metadata[0].key == kRetainedMetadataKey &&
          view.attributes[0].metadata[0].value == kRetainedMetadataValue,
      "Independent candidate lost owned input bytes");
  constexpr std::array<std::uint64_t, 1> kRetainedScalar = {
      9007199254740993ULL};
  const auto values =
      std::get<std::span<const std::uint64_t>>(view.attributes[0].values);
  Require(values.size() == kRetainedScalar.size() &&
              values[0] == kRetainedScalar[0],
          "Independent candidate changed the wide integer");
  candidate.reset();
  Require(inspection->IsStorageValid(), "Retained inspection lost its source");
  ExpectIndices(inspection->Source().View().corner_vertices, kRetainedCorners);
  inspection.reset();
  Require(ledger->ActiveTrackedPayload() == 0 && ledger->ActiveWorkers() == 0,
          "Destroyed semantic owners retained payload or worker leases");
}
void NameByteAdmission() {
  auto fixture = TwoTriangles();
  const std::string embedded_name("alpha\0beta", 10);
  AttributeFixture channel;
  channel.domain = geo::AttributeDomain::face;
  channel.name = embedded_name;
  channel.semantic = kUnknownSemantic;
  channel.values = std::vector<std::uint8_t>{17, 17};
  fixture.attributes = {channel};
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const std::array<RowLocalAttributeDeclaration, 2> declarations = {
      {{geo::AttributeDomain::face, embedded_name},
       {geo::AttributeDomain::corner, "unused"}}};
  constexpr std::uint64_t kExactNameBytes = 16;
  auto limits = RepairLimits();
  limits.max_declaration_name_bytes = kExactNameBytes;
  auto result = RemoveMaterializedDuplicateFaces(
      source, kRemoveSecond, {declarations}, limits, SerialWork());
  Accepted(result);
  limits.max_declaration_name_bytes = kExactNameBytes - 1;
  result = RemoveMaterializedDuplicateFaces(
      source, kRemoveSecond, {declarations}, limits, SerialWork());
  Require(
      result.status == MaterializedDuplicateStatus::kLimitExceeded &&
          !result.repair &&
          result.diagnostic.code ==
              MaterializedDuplicateDiagnosticCode::kDeclarationNameByteLimit,
      "Name bytes were truncated at an embedded zero or not admitted");
  const std::array<RowLocalAttributeDeclaration, 2> repeated = {
      {{geo::AttributeDomain::face, embedded_name},
       {geo::AttributeDomain::face, embedded_name}}};
  constexpr std::uint64_t kRepeatedNameBytes = 20;
  limits.max_declaration_name_bytes = kRepeatedNameBytes - 1;
  result = RemoveMaterializedDuplicateFaces(source, kRemoveSecond, {repeated},
                                            limits, SerialWork());
  Require(
      result.status == MaterializedDuplicateStatus::kLimitExceeded &&
          !result.repair &&
          result.diagnostic.code ==
              MaterializedDuplicateDiagnosticCode::kDeclarationNameByteLimit,
      "Duplicate declaration bytes bypassed byte admission");
  limits.max_declaration_name_bytes = kRepeatedNameBytes;
  result = RemoveMaterializedDuplicateFaces(source, kRemoveSecond, {repeated},
                                            limits, SerialWork());
  Require(result.status == MaterializedDuplicateStatus::kInvalidConfiguration &&
              !result.repair &&
              result.diagnostic.code ==
                  MaterializedDuplicateDiagnosticCode::kDeclarationDuplicate,
          "Repeated declaration shape did not follow byte admission");
  limits.max_row_local_declarations = 1;
  limits.max_declaration_name_bytes = 1;
  result = RemoveMaterializedDuplicateFaces(source, kRemoveSecond, {repeated},
                                            limits, SerialWork());
  Require(result.status == MaterializedDuplicateStatus::kLimitExceeded &&
              result.diagnostic.code ==
                  MaterializedDuplicateDiagnosticCode::kDeclarationLimit &&
              !result.repair,
          "Declaration count did not precede byte traversal");
  limits = RepairLimits();
  limits.max_declaration_name_bytes = 0;
  result =
      RemoveMaterializedDuplicateFaces(source, {}, {}, limits, SerialWork());
  Require(result.status == MaterializedDuplicateStatus::kInvalidConfiguration &&
              result.diagnostic.code ==
                  MaterializedDuplicateDiagnosticCode::kRequiredFieldZero &&
              !result.repair,
          "A required positive name-byte limit was omitted");
}
void CheckedComparisonBound() {
  const auto context = Context();
  const auto source = Capture(TwoTriangles(), context);
  auto limits = RepairLimits();
  // No attributes: three indices per rotation times three rotations, plus
  // one loop-length comparison. Actual early success cannot lower admission.
  constexpr std::uint64_t kTriangleWorstCaseSteps = 10;
  limits.max_comparison_steps_per_target = kTriangleWorstCaseSteps;
  Accepted(RemoveMaterializedDuplicateFaces(source, kRemoveSecond, {}, limits,
                                            SerialWork()));
  limits.max_comparison_steps_per_target = kTriangleWorstCaseSteps - 1;
  const auto result = RemoveMaterializedDuplicateFaces(
      source, kRemoveSecond, {}, limits, SerialWork());
  Require(result.status == MaterializedDuplicateStatus::kLimitExceeded &&
              result.diagnostic.code ==
                  MaterializedDuplicateDiagnosticCode::kComparisonLimit &&
              result.diagnostic.target == 0 && !result.repair,
          "Comparison admission used successful early exit or non-equivalence");
  auto fixture = TwoTriangles();
  AttributeFixture face;
  face.domain = geo::AttributeDomain::face;
  face.name = "face_pair";
  face.components = 2;
  face.values = std::vector<std::int32_t>{10, 20, 10, 20};
  AttributeFixture corner;
  corner.domain = geo::AttributeDomain::corner;
  corner.name = "uneven_corner_rows";
  corner.offsets = std::vector<geo::index_t>{0, 0, 2, 7, 7, 9, 14};
  corner.values =
      std::vector<std::int32_t>{1, 2, 3, 4, 5, 6, 7, 1, 2, 3, 4, 5, 6, 7};
  fixture.attributes = {face, corner};
  const auto rows = Capture(fixture, context);
  // Face: presence+length+two scalars = 4. Each rotation: three indices,
  // three presences, three lengths and seven representative scalars = 16.
  // Three rotations plus face work and loop length: 3*16+4+1 = 53.
  constexpr std::uint64_t kAuthoredRaggedWorstCaseSteps = 53;
  limits.max_comparison_steps_per_target = kAuthoredRaggedWorstCaseSteps;
  Accepted(RemoveMaterializedDuplicateFaces(rows, kRemoveSecond, {}, limits,
                                            SerialWork()));
  limits.max_comparison_steps_per_target = kAuthoredRaggedWorstCaseSteps - 1;
  const auto ragged_limited = RemoveMaterializedDuplicateFaces(
      rows, kRemoveSecond, {}, limits, SerialWork());
  Require(
      ragged_limited.status == MaterializedDuplicateStatus::kLimitExceeded &&
          ragged_limited.diagnostic.code ==
              MaterializedDuplicateDiagnosticCode::kComparisonLimit &&
          !ragged_limited.repair,
      "Representative ragged scalars were omitted from work admission");
  fixture.attributes[1].present = std::vector<std::uint8_t>{1, 1, 0, 1, 1, 0};
  // Missing row contributes only presence: per rotation 3+(2+4+1) = 10.
  constexpr std::uint64_t kMissingRaggedWorstCaseSteps = 35;
  limits.max_comparison_steps_per_target = kMissingRaggedWorstCaseSteps;
  Accepted(RemoveMaterializedDuplicateFaces(
      Capture(fixture, context), kRemoveSecond, {}, limits, SerialWork()));
}
void RawStorageDiagnostic() {
  auto fixture = TwoTriangles();
  fixture.positions[0] = std::numeric_limits<double>::infinity();
  fixture.corner_vertices.back() = 999;
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, kRemoveSecond);
  Require(result.status == MaterializedDuplicateStatus::kInvalidSourceStorage &&
              !result.repair &&
              result.diagnostic.code ==
                  MaterializedDuplicateDiagnosticCode::kStorageIssues &&
              result.diagnostic.geometry_status ==
                  geo::MaterializationStatus::kSuccess &&
              result.diagnostic.geometry_diagnostic &&
              result.diagnostic.geometry_diagnostic->code ==
                  geo::MaterializationDiagnosticCode::kNone &&
              result.diagnostic.storage_issue &&
              result.diagnostic.storage_issue->code ==
                  geo::StorageIssueCode::kPositionNonfinite &&
              result.diagnostic.storage_issue->element == 0,
          "Raw defects lost ordered typed issue or genuine inspection success");
  AttributeFixture attribute;
  attribute.domain = geo::AttributeDomain::face;
  attribute.name = "duplicate_metadata";
  attribute.values = std::vector<float>{1, 1};
  attribute.metadata = {{"duplicate", "first"}, {"duplicate", "second"}};
  fixture = TwoTriangles();
  fixture.attributes = {attribute};
  const auto metadata_result = Repair(Capture(fixture, context), kRemoveSecond);
  Require(metadata_result.status ==
                  MaterializedDuplicateStatus::kInvalidSourceStorage &&
              !metadata_result.repair &&
              metadata_result.diagnostic.storage_issue &&
              metadata_result.diagnostic.storage_issue->code ==
                  geo::StorageIssueCode::kMetadataDuplicateKey &&
              metadata_result.diagnostic.geometry_status ==
                  geo::MaterializationStatus::kSuccess,
          "Duplicate raw metadata was silently compacted or relabeled");
}
void FreshTokenAndPhysicalLimits() {
  const auto context = Context();
  const auto source = Capture(TwoTriangles(), context);
  const auto source_bytes = context.ActiveTrackedPayload();
  std::stop_source stopped;
  stopped.request_stop();
  const auto canceled = RemoveMaterializedDuplicateFaces(
      source, kRemoveSecond, {}, RepairLimits(), SerialWork(),
      stopped.get_token());
  Require(canceled.status == MaterializedDuplicateStatus::kCanceled &&
              !canceled.repair &&
              context.ActiveTrackedPayload() == source_bytes,
          "Canceled operation published or retained a partial result");
  Accepted(Repair(source, kRemoveSecond));
  auto limits = RepairLimits();
  constexpr std::uint64_t kUnchangedVertexCount = 4;
  limits.selection.max_vertices = kUnchangedVertexCount;
  Accepted(RemoveMaterializedDuplicateFaces(source, kRemoveSecond, {}, limits,
                                            SerialWork()));
  limits.selection.max_vertices = kUnchangedVertexCount - 1;
  const auto limited = RemoveMaterializedDuplicateFaces(
      source, kRemoveSecond, {}, limits, SerialWork());
  Require(
      limited.status == MaterializedDuplicateStatus::kGeometryFailure &&
          !limited.repair &&
          limited.diagnostic.geometry_status ==
              geo::MaterializationStatus::kLimitExceeded &&
          limited.diagnostic.geometry_diagnostic &&
          limited.diagnostic.geometry_diagnostic->code ==
              geo::MaterializationDiagnosticCode::kCountLimit,
      "Selection ignored shared unused vertices or changed delegated status");
}
}  // namespace
void CheckLifetimesAndAdmission() {
  OwnerLifetimes();
  NameByteAdmission();
  CheckedComparisonBound();
  RawStorageDiagnostic();
  FreshTokenAndPhysicalLimits();
}
}  // namespace meshvale::repair::materialized_semantics
