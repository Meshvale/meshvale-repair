// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <array>
#include <cstdint>
#include <vector>

#include "fixture_support.h"

namespace meshvale::repair::materialized_semantics {
namespace {
constexpr std::array<geo::index_t, 2> kRetainedFaces = {0, 2};
constexpr std::array<geo::index_t, 7> kRetainedCorners = {0, 1, 2, 3, 8, 9, 10};
constexpr std::array<geo::index_t, 4> kExpectedFaceMap = {0, 0, 1, 0};
constexpr std::array<geo::index_t, 15> kExpectedCornerMap = {
    0, 1, 2, 3, 2, 3, 0, 1, 4, 5, 6, 3, 0, 1, 2};
constexpr std::array<geo::index_t, 3> kCandidateOffsets = {0, 4, 7};
constexpr std::array<geo::index_t, 7> kCandidateCorners = {0, 1, 2, 3, 0, 3, 4};
constexpr std::array<DuplicateTarget, 2> kTargets = {{{0, 1}, {0, 3}}};

void ManyToOne() {
  auto fixture = RotatedPolygons();
  AttributeFixture material;
  material.domain = geo::AttributeDomain::face;
  material.name = "material";
  material.semantic = kMaterialSemantic;
  material.values = std::vector<std::uint32_t>{17, 17, 23, 17};
  AttributeFixture uv;
  uv.domain = geo::AttributeDomain::corner;
  uv.name = "secondary_uv";
  uv.semantic = kTexcoordSemantic;
  uv.set_index = 3;
  uv.values = std::vector<float>{10, 20, 30, 40, 30, 40, 10, 20,
                                 50, 60, 70, 40, 10, 20, 30};
  fixture.attributes = {material, uv};
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, kTargets);
  Accepted(result);
  const auto& owner = *result.repair;
  ExpectIndices(owner.FaceMap(), kExpectedFaceMap);
  ExpectIndices(owner.CornerMap(), kExpectedCornerMap);
  ExpectIndices(owner.SourceFaces(), kRetainedFaces);
  ExpectIndices(owner.SourceCorners(), kRetainedCorners);
  ExpectIndices(owner.Candidate().View().face_offsets, kCandidateOffsets);
  ExpectIndices(owner.Candidate().View().corner_vertices, kCandidateCorners);
  ExpectRawRows(source.View(), owner.Candidate().View(), kRetainedFaces,
                kRetainedCorners);
  constexpr std::array<DuplicateTarget, 2> kReverseRequest = {{{0, 3}, {0, 1}}};
  const auto reversed = Repair(source, kReverseRequest);
  Accepted(reversed);
  ExpectIndices(reversed.repair->FaceMap(), kExpectedFaceMap);
  ExpectIndices(reversed.repair->CornerMap(), kExpectedCornerMap);
}
void LaterRepresentative() {
  auto fixture = RotatedPolygons();
  const auto context = Context();
  const auto source = Capture(fixture, context);
  constexpr std::array<DuplicateTarget, 2> kLaterTargets = {{{3, 0}, {3, 1}}};
  const auto result = Repair(source, kLaterTargets);
  Accepted(result);
  constexpr std::array<geo::index_t, 4> kFaces = {1, 1, 0, 1};
  constexpr std::array<geo::index_t, 15> kCorners = {4, 5, 6, 3, 6, 3, 4, 5,
                                                     0, 1, 2, 3, 4, 5, 6};
  constexpr std::array<geo::index_t, 2> kRetained = {2, 3};
  ExpectIndices(result.repair->FaceMap(), kFaces);
  ExpectIndices(result.repair->CornerMap(), kCorners);
  ExpectIndices(result.repair->SourceFaces(), kRetained);
}
void EmptyRequest() {
  const auto fixture = RotatedPolygons();
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, {});
  Accepted(result, CandidateOutcome::unchanged);
  constexpr std::array<geo::index_t, 4> kFaces = {0, 1, 2, 3};
  constexpr std::array<geo::index_t, 15> kCorners = {0, 1, 2,  3,  4,  5,  6, 7,
                                                     8, 9, 10, 11, 12, 13, 14};
  ExpectIndices(result.repair->FaceMap(), kFaces);
  ExpectIndices(result.repair->CornerMap(), kCorners);
  ExpectIndices(result.repair->SourceFaces(), kFaces);
  ExpectIndices(result.repair->SourceCorners(), kCorners);
  ExpectRawRows(source.View(), result.repair->Candidate().View(), kFaces,
                kCorners);
}
}  // namespace
void CheckCorrespondence() {
  ManyToOne();
  LaterRepresentative();
  EmptyRequest();
}
}  // namespace meshvale::repair::materialized_semantics
