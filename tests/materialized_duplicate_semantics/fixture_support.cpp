// SPDX-License-Identifier: Apache-2.0
#include "fixture_support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace meshvale::repair::materialized_semantics {
namespace {
constexpr std::uint64_t kPhysicalCountLimit = 1000000;
constexpr std::uint64_t kIssueCountLimit = 1000;
constexpr std::uint64_t kTargetCountLimit = 1000;
constexpr std::uint64_t kDeclarationByteLimit = 1024 * 1024;
constexpr std::uint64_t kComparisonStepLimit = 10000000;
constexpr std::uint64_t kSerialThresholdBytes = kOperationBudgetBytes;
constexpr std::uint64_t kPartitionBytes = 4096;
constexpr std::uint64_t kCancellationScalars = 64;
constexpr std::uint64_t kJobLimit = 64;
constexpr std::size_t kPositionWidth = 3;

template <class T>
bool SameBytes(std::span<const T> left, std::span<const T> right) {
  const auto a = std::as_bytes(left), b = std::as_bytes(right);
  return std::equal(a.begin(), a.end(), b.begin(), b.end());
}
std::span<const geo::index_t> Rows(geo::AttributeDomain domain,
                                   const geo::MeshReadView& source,
                                   std::span<const geo::index_t> faces,
                                   std::span<const geo::index_t> corners,
                                   std::vector<geo::index_t>& vertices) {
  if (domain == geo::AttributeDomain::face) return faces;
  if (domain == geo::AttributeDomain::corner) return corners;
  vertices.resize(source.positions.size() / kPositionWidth);
  for (std::size_t row = 0; row < vertices.size(); ++row) vertices[row] = row;
  return vertices;
}
std::pair<std::size_t, std::size_t> RowRange(
    const geo::AttributeReadView& attribute, geo::index_t row) {
  if (attribute.offsets)
    return {(*attribute.offsets)[row], (*attribute.offsets)[row + 1]};
  const auto begin = static_cast<std::size_t>(row * attribute.components);
  return {begin, begin + attribute.components};
}
void AttributeRows(const geo::AttributeReadView& before,
                   const geo::AttributeReadView& after,
                   std::span<const geo::index_t> rows) {
  Require(before.domain == after.domain && before.name == after.name &&
              before.semantic == after.semantic &&
              before.set_index == after.set_index &&
              before.components == after.components &&
              before.values.index() == after.values.index(),
          "Attribute descriptor or encoding changed");
  Require(before.offsets.has_value() == after.offsets.has_value() &&
              before.present.has_value() == after.present.has_value(),
          "Optional attribute representation changed");
  Require(before.metadata.size() == after.metadata.size(),
          "Metadata entry count changed");
  for (std::size_t entry = 0; entry < before.metadata.size(); ++entry)
    Require(before.metadata[entry].key == after.metadata[entry].key &&
                before.metadata[entry].value == after.metadata[entry].value,
            "Metadata bytes or order changed");
  if (before.present) {
    Require(after.present->size() == rows.size(), "Presence count changed");
    for (std::size_t row = 0; row < rows.size(); ++row)
      Require((*after.present)[row] == (*before.present)[rows[row]],
              "Presence value changed");
  }
  if (before.offsets) {
    Require(
        after.offsets->size() == rows.size() + 1 && (*after.offsets)[0] == 0,
        "Ragged offset shape changed");
    geo::index_t expected_end = 0;
    for (std::size_t row = 0; row < rows.size(); ++row) {
      const auto [begin, end] = RowRange(before, rows[row]);
      expected_end += end - begin;
      Require((*after.offsets)[row + 1] == expected_end,
              "Retained backing row length changed");
    }
  }
  std::visit(
      [&](auto values) {
        using T = typename decltype(values)::value_type;
        const auto output = std::get<std::span<const T>>(after.values);
        std::size_t written = 0;
        for (const auto source_row : rows) {
          const auto [begin, end] = RowRange(before, source_row);
          Require(written <= output.size() &&
                      end - begin <= output.size() - written,
                  "Retained scalar count changed");
          Require(SameBytes<T>(values.subspan(begin, end - begin),
                               output.subspan(written, end - begin)),
                  "Retained row bits changed");
          written += end - begin;
        }
        Require(written == output.size(), "Extra retained scalars");
        if (before.domain == geo::AttributeDomain::vertex)
          Require(values.data() == output.data(),
                  "Unchanged vertex attribute storage was copied");
      },
      before.values);
}
}  // namespace

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
geo::ExecutionContext Context() {
  geo::ExecutionOptions options;
  options.worker_budget = kWorkerBudget;
  options.tracked_payload_budget_bytes = kLedgerBudgetBytes;
  return geo::ExecutionContext(options);
}
geo::MeshMaterializationLimits CaptureLimits() {
  return {kPhysicalCountLimit, kPhysicalCountLimit,  kPhysicalCountLimit,
          kPhysicalCountLimit, kPhysicalCountLimit,  kPhysicalCountLimit,
          kPhysicalCountLimit, kPhysicalCountLimit,  kPhysicalCountLimit,
          kPhysicalCountLimit, kOperationBudgetBytes};
}
geo::MeshMaterializationWorkPolicy SerialWork() {
  return {kSerialThresholdBytes, kPartitionBytes, kCancellationScalars,
          kJobLimit};
}
MaterializedDuplicateLimits RepairLimits() {
  return {kTargetCountLimit,     kTargetCountLimit,
          kDeclarationByteLimit, kComparisonStepLimit,
          kOperationBudgetBytes, {kIssueCountLimit, kOperationBudgetBytes},
          CaptureLimits()};
}
Fixture TwoTriangles() {
  return {
      {0, 0, 0, 1, 0, 0, 0, 1, 0, 7, 7, 7}, {0, 3, 6}, {0, 1, 2, 0, 1, 2}, {}};
}
Fixture RotatedPolygons() {
  return {{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 2, 0, 0, 9, 9, 9},
          {0, 4, 8, 11, 15},
          {0, 1, 2, 3, 2, 3, 0, 1, 0, 3, 4, 3, 0, 1, 2},
          {}};
}
geo::MaterializedMesh Capture(const Fixture& fixture,
                              const geo::ExecutionContext& context) {
  std::vector<std::vector<geo::MetadataEntryView>> metadata(
      fixture.attributes.size());
  std::vector<geo::AttributeMaterializationInput> attributes;
  attributes.reserve(fixture.attributes.size());
  for (std::size_t ordinal = 0; ordinal < fixture.attributes.size();
       ++ordinal) {
    const auto& authored = fixture.attributes[ordinal];
    for (const auto& [key, value] : authored.metadata)
      metadata[ordinal].push_back({key, value});
    geo::AttributeMaterializationInput input;
    input.domain = authored.domain;
    input.name = authored.name;
    input.semantic = authored.semantic;
    input.set_index = authored.set_index;
    input.components = authored.components;
    input.values = std::visit(
        [](const auto& values) -> geo::ScalarIngress {
          using T = typename std::decay_t<decltype(values)>::value_type;
          return std::span<const T>(values);
        },
        authored.values);
    if (authored.offsets) input.offsets = std::span(*authored.offsets);
    if (authored.present) input.present = std::span(*authored.present);
    input.metadata = metadata[ordinal];
    attributes.push_back(input);
  }
  geo::MeshMaterializationInput input;
  input.positions = std::span<const double>(fixture.positions);
  input.topology =
      geo::PolygonTopologyInput{fixture.face_offsets, fixture.corner_vertices};
  input.attributes = attributes;
  const auto result =
      geo::MaterializeMesh(input, CaptureLimits(), SerialWork(), context);
  Require(result.status == geo::MaterializationStatus::kSuccess && result.mesh,
          "Public fixture capture failed");
  return *result.mesh;
}
MaterializedDuplicateResult Repair(
    const geo::MaterializedMesh& source,
    std::span<const DuplicateTarget> targets,
    std::span<const RowLocalAttributeDeclaration> declarations) {
  return RemoveMaterializedDuplicateFaces(source, targets, {declarations},
                                          RepairLimits(), SerialWork());
}
void Accepted(const MaterializedDuplicateResult& result,
              CandidateOutcome outcome) {
  Require(result.status == MaterializedDuplicateStatus::kSuccess &&
              result.repair && result.repair->Outcome() == outcome,
          "Expected a successful immutable repair owner");
  Require(result.repair->SourceInspection().IsStorageValid() &&
              result.repair->CandidateInspection().IsStorageValid(),
          "Published inspections are invalid");
}
void Rejected(const MaterializedDuplicateResult& result,
              MaterializedDuplicateDiagnosticCode code) {
  Require(result.status == MaterializedDuplicateStatus::kRejected &&
              !result.repair && result.diagnostic.code == code,
          "Expected whole-request semantic rejection");
}
void ExpectIndices(std::span<const geo::index_t> actual,
                   std::span<const geo::index_t> expected) {
  Require(std::equal(actual.begin(), actual.end(), expected.begin(),
                     expected.end()),
          "Literal topology or correspondence differs");
}
void ExpectRawRows(const geo::MeshReadView& source,
                   const geo::MeshReadView& candidate,
                   std::span<const geo::index_t> retained_faces,
                   std::span<const geo::index_t> retained_corners) {
  Require(SameBytes<double>(source.positions, candidate.positions) &&
              source.positions.data() == candidate.positions.data(),
          "Unchanged vertex positions were copied or changed");
  Require(source.attributes.size() == candidate.attributes.size(),
          "An authored channel was dropped");
  std::vector<geo::index_t> vertices;
  for (std::size_t ordinal = 0; ordinal < source.attributes.size(); ++ordinal) {
    const auto rows = Rows(source.attributes[ordinal].domain, source,
                           retained_faces, retained_corners, vertices);
    AttributeRows(source.attributes[ordinal], candidate.attributes[ordinal],
                  rows);
  }
}
}  // namespace meshvale::repair::materialized_semantics
