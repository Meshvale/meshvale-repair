// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "fixture_support.h"

namespace meshvale::repair::materialized_semantics {
namespace {
constexpr std::array<DuplicateTarget, 1> kRemoveSecond = {{{0, 1}}};
constexpr std::array<geo::index_t, 1> kFirstFace = {0};
constexpr std::array<geo::index_t, 3> kFirstTriangleCorners = {0, 1, 2};
constexpr std::uint32_t kMissingNanBits = 0x7fc12345;

void DirectedIdentity() {
  auto fixture = TwoTriangles();
  const auto context = Context();
  fixture.corner_vertices = {0, 1, 2, 1, 2, 0};
  auto source = Capture(fixture, context);
  const auto rotated = Repair(source, kRemoveSecond);
  Accepted(rotated);
  constexpr std::array<geo::index_t, 6> kRotatedMap = {0, 1, 2, 1, 2, 0};
  ExpectIndices(rotated.repair->CornerMap(), kRotatedMap);
  fixture.corner_vertices = {0, 1, 2, 0, 2, 1};
  source = Capture(fixture, context);
  Rejected(Repair(source, kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
  fixture.positions.insert(fixture.positions.end(), {0, 0, 0});
  fixture.corner_vertices = {0, 1, 2, 4, 1, 2};
  source = Capture(fixture, context);
  Rejected(Repair(source, kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
}
void RepeatedRotation() {
  auto fixture = TwoTriangles();
  fixture.face_offsets = {0, 4, 8};
  fixture.corner_vertices = {0, 1, 0, 1, 0, 1, 0, 1};
  AttributeFixture corner;
  corner.domain = geo::AttributeDomain::corner;
  corner.name = "repeated_loop_label";
  corner.values = std::vector<std::int32_t>{10, 20, 30, 40, 30, 40, 10, 20};
  fixture.attributes = {corner};
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, kRemoveSecond);
  Accepted(result);
  constexpr std::array<geo::index_t, 8> kLaterFullMatch = {0, 1, 2, 3,
                                                           2, 3, 0, 1};
  ExpectIndices(result.repair->CornerMap(), kLaterFullMatch);
  fixture.attributes[0].values =
      std::vector<std::int32_t>{10, 20, 10, 20, 10, 20, 10, 20};
  const auto tied = Repair(Capture(fixture, context), kRemoveSecond);
  Accepted(tied);
  constexpr std::array<geo::index_t, 8> kFirstFullMatch = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
  ExpectIndices(tied.repair->CornerMap(), kFirstFullMatch);
}
void SignedZeroAndMissingBacking() {
  auto fixture = TwoTriangles();
  fixture.positions[0] = -0.0;
  AttributeFixture zero;
  zero.domain = geo::AttributeDomain::face;
  zero.name = "signed_zero";
  zero.values = std::vector<float>{-0.0F, 0.0F};
  AttributeFixture missing;
  missing.domain = geo::AttributeDomain::face;
  missing.name = "missing_backing";
  missing.values =
      std::vector<float>{std::bit_cast<float>(kMissingNanBits),
                         std::numeric_limits<float>::infinity(), -0.0F, 13.0F};
  missing.offsets = std::vector<geo::index_t>{0, 1, 4};
  missing.present = std::vector<std::uint8_t>{0, 0};
  fixture.attributes = {zero, missing};
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, kRemoveSecond);
  Accepted(result);
  ExpectRawRows(source.View(), result.repair->Candidate().View(), kFirstFace,
                kFirstTriangleCorners);
  const auto values = std::get<std::span<const float>>(
      result.repair->Candidate().View().attributes[1].values);
  Require(std::bit_cast<std::uint32_t>(values[0]) == kMissingNanBits,
          "Missing NaN payload was changed");
  fixture.attributes[1].present = std::vector<std::uint8_t>{0, 1};
  Rejected(Repair(Capture(fixture, context), kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNonfiniteAttribute);
  fixture.attributes[1].values = std::vector<float>{17, 18, 19, 20};
  Rejected(Repair(Capture(fixture, context), kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
}
void GlobalAuthoredFiniteness() {
  auto fixture = TwoTriangles();
  AttributeFixture authored;
  authored.name = "unused_vertex_value";
  authored.values =
      std::vector<double>{0, 1, 2, std::numeric_limits<double>::infinity()};
  fixture.attributes = {authored};
  const auto context = Context();
  auto source = Capture(fixture, context);
  const auto empty = Repair(source, {});
  Rejected(empty, MaterializedDuplicateDiagnosticCode::kNonfiniteAttribute);
  Require(empty.diagnostic.attribute == 0 && empty.diagnostic.element == 3,
          "Unused authored vertex did not retain its source ordinal");
  fixture.attributes[0].present = std::vector<std::uint8_t>{1, 1, 1, 0};
  source = Capture(fixture, context);
  Accepted(Repair(source, kRemoveSecond));
  Accepted(Repair(source, {}), CandidateOutcome::unchanged);
  // Signaling NaN input is created/read only through bit operations; the
  // operation must not quiet retained source backing while classifying it.
  constexpr std::uint64_t kSignalingNanBits = 0x7ff0000000000123ULL;
  fixture.attributes[0].values =
      std::vector<double>{0, 1, std::bit_cast<double>(kSignalingNanBits), 3};
  fixture.attributes[0].present.reset();
  source = Capture(fixture, context);
  const auto failed = Repair(source, kRemoveSecond);
  Rejected(failed, MaterializedDuplicateDiagnosticCode::kNonfiniteAttribute);
  const auto captured =
      std::get<std::span<const double>>(source.View().attributes[0].values);
  Require(std::bit_cast<std::uint64_t>(captured[2]) == kSignalingNanBits,
          "Source signaling NaN bits were quieted");
}
void WholeRequestRejection() {
  const auto fixture = RotatedPolygons();
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto baseline_bytes = context.ActiveTrackedPayload();
  struct Case {
    std::array<DuplicateTarget, 2> targets;
    MaterializedDuplicateDiagnosticCode code;
    geo::index_t failed_target;
  };
  constexpr std::array<Case, 5> kCases = {
      {{{{{0, 1}, {0, 4}}},
        MaterializedDuplicateDiagnosticCode::kTargetRange,
        1},
       {{{{0, 1}, {2, 2}}},
        MaterializedDuplicateDiagnosticCode::kSelfTarget,
        1},
       {{{{0, 1}, {3, 1}}},
        MaterializedDuplicateDiagnosticCode::kRepeatedTarget,
        1},
       {{{{0, 1}, {1, 3}}},
        MaterializedDuplicateDiagnosticCode::kRemovedRepresentative,
        1},
       {{{{0, 1}, {0, 2}}},
        MaterializedDuplicateDiagnosticCode::kNotEquivalent,
        1}}};
  for (const auto& item : kCases) {
    const auto result = Repair(source, item.targets);
    Rejected(result, item.code);
    Require(result.diagnostic.target == item.failed_target,
            "Request diagnostic order changed");
    Require(context.ActiveTrackedPayload() == baseline_bytes &&
                context.ActiveWorkers() == 0,
            "Rejected request retained charges or workers");
    ExpectIndices(source.View().corner_vertices, fixture.corner_vertices);
  }
  constexpr std::array<DuplicateTarget, 2> kCycle = {{{0, 1}, {1, 0}}};
  Rejected(Repair(source, kCycle),
           MaterializedDuplicateDiagnosticCode::kRemovedRepresentative);
}
}  // namespace
void CheckEquality() {
  DirectedIdentity();
  RepeatedRotation();
  SignedZeroAndMissingBacking();
  GlobalAuthoredFiniteness();
  WholeRequestRejection();
}
}  // namespace meshvale::repair::materialized_semantics
