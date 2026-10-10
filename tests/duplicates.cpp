// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/duplicates.h"

#include <meshvale/geometry/position_buffer.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

using namespace meshvale::geometry;
using namespace meshvale::repair;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
bool PositionBytesEqual(const PositionBuffer& left,
                        const PositionBuffer& right) {
  if (left.size() != right.size()) return false;
  std::vector<std::byte> left_bytes(left.size() * 3 * sizeof(double));
  std::vector<std::byte> right_bytes(left_bytes.size());
  left.CopyBytesTo(left_bytes);
  right.CopyBytesTo(right_bytes);
  return left_bytes == right_bytes;
}
bool has(const DuplicateResult& result, const char* code) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [&](const auto& d) { return d.code == code; });
}
Mesh fixture() {
  Mesh mesh;
  mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {2, 0, 0}, {2, 1, 0},
                    {4, 0, 0}, {6, 0, 0}, {6, 2, 0}, {5, 1, 0}, {4, 2, 0}};
  // Triangle, quad, concave pentagon, rotated duplicate of the quad.
  mesh.face_offsets = {0, 3, 7, 12, 16};
  mesh.corner_vertices = {0, 1, 2, 1, 3, 4, 2, 5, 6, 7, 8, 9, 4, 2, 1, 3};
  Attribute uv;
  uv.domain = AttributeDomain::corner;
  uv.name = "uv0";
  uv.semantic = "texcoord";
  uv.set_index = 0;
  uv.components = 2;
  uv.values = std::vector<float>{
      0, 0, 1, 0, 0,    1,    0.25F, 0, 1, 0, 1,     1, 0.25F, 1, 0, 0,
      1, 0, 1, 1, 0.5F, 0.5F, 0,     1, 1, 1, 0.25F, 1, 0.25F, 0, 1, 0};
  Attribute uv1 = uv;
  uv1.name = "lightmap";
  uv1.set_index = 1;
  auto& second = std::get<meshvale::geometry::ScalarBuffer<float>>(uv1.values);
  for (auto& value : second) value *= 0.5F;
  Attribute normal;
  normal.domain = AttributeDomain::corner;
  normal.name = "normal";
  normal.semantic = "normal";
  normal.components = 3;
  std::vector<double> normals;
  for (index_t i = 0; i < 16; ++i) normals.insert(normals.end(), {0, 0, 1});
  normal.values = normals;
  Attribute material;
  material.domain = AttributeDomain::face;
  material.name = "material";
  material.semantic = "material_index";
  material.values = std::vector<std::uint32_t>{0, 1, 2, 1};
  Attribute label = material;
  label.name = "label";
  label.semantic = "label";
  label.values = std::vector<std::int32_t>{10, 20, 30, 20};
  Attribute joints;
  joints.name = "joints";
  joints.semantic = "joint_indices";
  joints.offsets = std::vector<index_t>{0, 0, 2, 7, 7, 7, 7, 7, 7, 7, 7};
  joints.values = std::vector<std::uint16_t>{1, 2, 0, 1, 2, 3, 4};
  joints.metadata["association"] = "body_skin";
  Attribute weights = joints;
  weights.name = "weights";
  weights.semantic = "joint_weights";
  weights.values = std::vector<double>{0.25, 0.75, 0.1, 0.2, 0.3, 0.4, 0.5};
  mesh.attributes = {uv, uv1, normal, material, label, joints, weights};
  return mesh;
}
void accepted_rotation_and_preservation() {
  const auto source = fixture();
  const auto result = remove_duplicate_faces(source, {{1, 3}});
  require(result.outcome == CandidateOutcome::accepted &&
              result.candidate.has_value(),
          "edit rejected");
  const auto& out = *result.candidate;
  require(out.face_offsets == std::vector<index_t>({0, 3, 7, 12}),
          "polygon sizes changed");
  require(out.corner_vertices ==
              std::vector<index_t>({0, 1, 2, 1, 3, 4, 2, 5, 6, 7, 8, 9}),
          "loops changed");
  require(PositionBytesEqual(out.positions, source.positions) &&
              source.face_count() == 4,
          "source/positions changed");
  require(result.face_map == std::vector<index_t>({0, 1, 2, 1}),
          "bad face provenance");
  require(result.corner_map == std::vector<index_t>({0, 1, 2, 3, 4, 5, 6, 7, 8,
                                                     9, 10, 11, 5, 6, 3, 4}),
          "bad cyclic maps");
  for (std::size_t a = 5; a < 7; ++a) {
    require(out.attributes[a].values == source.attributes[a].values,
            "influences truncated/normalized");
    require(out.attributes[a].offsets == source.attributes[a].offsets,
            "ragged skin rows changed");
    require(out.attributes[a].metadata == source.attributes[a].metadata,
            "skin association lost");
  }
  auto reverse = remove_duplicate_faces(source, {{3, 1}});
  require(reverse.outcome == CandidateOutcome::accepted,
          "later representative rejected");
  require(reverse.face_map == std::vector<index_t>({0, 2, 1, 2}),
          "later representative order wrong");
  require(reverse.corner_map[3] == 10 && reverse.corner_map[4] == 11 &&
              reverse.corner_map[5] == 8 && reverse.corner_map[6] == 9,
          "reverse corner map wrong");
}
void distinctions_block_edits() {
  for (std::size_t channel : {std::size_t{0}, std::size_t{1}}) {
    auto mesh = fixture();
    std::get<meshvale::geometry::ScalarBuffer<float>>(
        mesh.attributes[channel].values)[24] += 0.125F;
    require(
        has(remove_duplicate_faces(mesh, {{1, 3}}), "repair.not_equivalent"),
        "UV-set difference ignored");
  }
  auto mesh = fixture();
  std::get<meshvale::geometry::ScalarBuffer<double>>(
      mesh.attributes[2].values)[38] = -1;
  require(has(remove_duplicate_faces(mesh, {{1, 3}}), "repair.not_equivalent"),
          "normal difference ignored");
  mesh = fixture();
  std::get<meshvale::geometry::ScalarBuffer<std::uint32_t>>(
      mesh.attributes[3].values)[3] = 2;
  require(has(remove_duplicate_faces(mesh, {{1, 3}}), "repair.not_equivalent"),
          "material difference ignored");
  mesh = fixture();
  std::get<meshvale::geometry::ScalarBuffer<std::int32_t>>(
      mesh.attributes[4].values)[3] = 99;
  require(has(remove_duplicate_faces(mesh, {{1, 3}}), "repair.not_equivalent"),
          "face label difference ignored");
  mesh = fixture();
  std::reverse(mesh.corner_vertices.begin() + 12, mesh.corner_vertices.end());
  require(has(remove_duplicate_faces(mesh, {{1, 3}}), "repair.not_equivalent"),
          "opposite winding eligible");
  mesh = fixture();
  mesh.positions.Append(mesh.positions.Get(4));
  mesh.corner_vertices[12] = 10;
  for (std::size_t a = 5; a < 7; ++a)
    mesh.attributes[a].offsets->push_back(mesh.attributes[a].offsets->back());
  require(inspect_storage(mesh).empty(),
          "coincident-vertex fixture has malformed attributes");
  require(has(remove_duplicate_faces(mesh, {{1, 3}}), "repair.not_equivalent"),
          "coincident vertices welded");
}
void missingness_and_ragged_selection() {
  auto mesh = fixture();
  auto& a = mesh.attributes[0];
  a.present = std::vector<std::uint8_t>(16, 1);
  a.present->at(5) = 0;
  require(has(remove_duplicate_faces(mesh, {{1, 3}}), "repair.not_equivalent"),
          "missing equals authored");
  a.present->at(12) = 0;
  auto& values = std::get<meshvale::geometry::ScalarBuffer<float>>(a.values);
  values[10] = -0.0F;
  values[24] = std::numeric_limits<float>::quiet_NaN();
  auto result = remove_duplicate_faces(mesh, {{1, 3}});
  require(result.outcome == CandidateOutcome::accepted,
          "missing backing values affected equality");
  const auto retained = std::get<meshvale::geometry::ScalarBuffer<float>>(
      result.candidate->attributes[0].values)[10];
  require(std::bit_cast<std::uint32_t>(retained) ==
              std::bit_cast<std::uint32_t>(-0.0F),
          "retained signed zero changed");
  Attribute ragged;
  ragged.domain = AttributeDomain::face;
  ragged.name = "tags";
  ragged.semantic = "label";
  ragged.offsets = std::vector<index_t>{0, 0, 2, 3, 5};
  ragged.values = std::vector<std::uint64_t>{42, 43, 99, 42, 43};
  mesh.attributes.push_back(ragged);
  result = remove_duplicate_faces(mesh, {{1, 3}});
  require(result.outcome == CandidateOutcome::accepted,
          "ragged face rows rejected");
  require(result.candidate->attributes.back().offsets ==
              std::optional<std::vector<index_t>>({{0, 0, 2, 3}}),
          "ragged offsets not selected");
  require(std::get<meshvale::geometry::ScalarBuffer<std::uint64_t>>(
              result.candidate->attributes.back().values) ==
              std::vector<std::uint64_t>({42, 43, 99}),
          "ragged values not selected");
}
void transactional_and_invalid_requests() {
  const auto mesh = fixture();
  for (const std::vector<DuplicateTarget>& targets :
       std::vector<std::vector<DuplicateTarget>>{{{1, 3}, {0, 2}},
                                                 {{1, 3}, {1, 3}},
                                                 {{1, 3}, {3, 1}},
                                                 {{1, 1}},
                                                 {{1, 99}}}) {
    const auto result = remove_duplicate_faces(mesh, targets);
    require(result.outcome == CandidateOutcome::rejected && !result.candidate &&
                result.face_map.empty() && result.corner_map.empty(),
            "partial candidate escaped");
    require(mesh.face_count() == 4, "rejection modified input");
  }
  auto malformed = mesh;
  malformed.corner_vertices[12] = 99;
  require(has(remove_duplicate_faces(malformed, {{1, 3}}), "mesh.vertex_range"),
          "bad storage processed");
  malformed = mesh;
  malformed.attributes[0].offsets = std::vector<index_t>{};
  require(has(remove_duplicate_faces(malformed, {{1, 3}}),
              "attribute.empty_offsets"),
          "bad ragged rows processed");
  malformed = mesh;
  std::get<meshvale::geometry::ScalarBuffer<double>>(
      malformed.attributes[6].values)[0] =
      std::numeric_limits<double>::infinity();
  require(has(remove_duplicate_faces(malformed, {{1, 3}}),
              "repair.nonfinite_attribute"),
          "infinite payload accepted");
}
void custom_policy_and_noop() {
  auto mesh = fixture();
  mesh.attributes[4].semantic = "custom";
  require(has(remove_duplicate_faces(mesh, {{1, 3}}),
              "repair.unsupported_attribute"),
          "unknown semantics ignored");
  const auto noop = remove_duplicate_faces(mesh, {});
  require(noop.outcome == CandidateOutcome::unchanged &&
              noop.face_map == std::vector<index_t>({0, 1, 2, 3}),
          "no-op not independent");
  DuplicateOptions options{{{AttributeDomain::face, "label"}}};
  require(remove_duplicate_faces(mesh, {{1, 3}}, options).outcome ==
              CandidateOutcome::accepted,
          "row-local policy rejected");
  std::get<meshvale::geometry::ScalarBuffer<std::int32_t>>(
      mesh.attributes[4].values)[3] = 99;
  require(has(remove_duplicate_faces(mesh, {{1, 3}}, options),
              "repair.not_equivalent"),
          "policy bypassed custom equality");
  const auto empty = remove_duplicate_faces(Mesh{}, {});
  require(empty.outcome == CandidateOutcome::unchanged &&
              empty.candidate->face_offsets == std::vector<index_t>{0},
          "empty mesh no-op failed");
}
void nonmanifold_input_is_retained() {
  Mesh mesh;
  mesh.positions = {{0, 0, 0}, {1, 0, 0},  {0, 1, 0},  {0, -1, 0},
                    {0, 0, 1}, {-1, 0, 0}, {-1, -1, 0}};
  // Four faces share edge 0--1; a disconnected fan also meets vertex 0.
  mesh.face_offsets = {0, 3, 6, 9, 12, 15};
  mesh.corner_vertices = {0, 1, 2, 1, 0, 3, 0, 1, 4, 0, 5, 6, 1, 2, 0};
  require(inspect_storage(mesh).empty(),
          "raw nonmanifold fixture cannot be represented");
  const auto result = remove_duplicate_faces(mesh, {{0, 4}});
  require(result.outcome == CandidateOutcome::accepted &&
              result.candidate->face_count() == 4,
          "nonmanifold input was forced into manifold storage");
  require(result.candidate->corner_vertices ==
              std::vector<index_t>({0, 1, 2, 1, 0, 3, 0, 1, 4, 0, 5, 6}),
          "unrelated fan or face rewritten");
  require(mesh.face_count() == 5 &&
              PositionBytesEqual(result.candidate->positions, mesh.positions),
          "nonmanifold input implicitly split or welded");
}
void mixed_polygon_group() {
  auto mesh = fixture();
  mesh.attributes.clear();
  mesh.face_offsets = {0, 3, 7, 12, 15, 20, 24};
  mesh.corner_vertices = {0, 1, 2, 1, 3, 4, 2, 5, 6, 7, 8, 9,
                          1, 2, 0, 7, 8, 9, 5, 6, 1, 3, 4, 2};
  const auto result = remove_duplicate_faces(mesh, {{2, 4}, {0, 3}, {1, 5}});
  require(result.outcome == CandidateOutcome::accepted &&
              result.candidate->face_count() == 3,
          "mixed triangle/quad/concave-pentagon group rejected");
  require(result.face_map == std::vector<index_t>({0, 1, 2, 0, 2, 1}),
          "group face map wrong");
  require(result.corner_map ==
              std::vector<index_t>({0, 1, 2, 3, 4,  5,  6, 7, 8, 9, 10, 11,
                                    1, 2, 0, 9, 10, 11, 7, 8, 3, 4, 5,  6}),
          "mixed group cyclic maps wrong");
}
int main() {
  try {
    accepted_rotation_and_preservation();
    distinctions_block_edits();
    missingness_and_ragged_selection();
    transactional_and_invalid_requests();
    custom_policy_and_noop();
    nonmanifold_input_is_retained();
    mixed_polygon_group();
    std::cout << "Seven targeted duplicate suites passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
