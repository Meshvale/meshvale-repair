// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "fixture_support.h"

namespace meshvale::repair::materialized_semantics {
namespace {
constexpr std::array<DuplicateTarget, 1> kRemoveSecond = {{{0, 1}}};
constexpr std::array<geo::index_t, 1> kFirstFace = {0};
constexpr std::array<geo::index_t, 3> kFirstCorners = {0, 1, 2};
constexpr std::array<geo::AttributeDomain, 3> kDomains = {
    geo::AttributeDomain::vertex, geo::AttributeDomain::face,
    geo::AttributeDomain::corner};
constexpr std::array<std::size_t, 3> kDomainRows = {4, 2, 6};
constexpr std::uint32_t kPairWidth = 2;
constexpr std::uint64_t kAdjacentWideInteger = 9007199254740993ULL;

template <class T>
void AddEncoding(Fixture& fixture, std::string_view encoding) {
  for (std::size_t domain = 0; domain < kDomains.size(); ++domain) {
    AttributeFixture channel;
    channel.domain = kDomains[domain];
    channel.name = std::string(encoding);
    channel.components = kPairWidth;
    channel.set_index = 0;
    channel.metadata = {{"source_label", std::string(encoding)},
                        {"authored_space", std::string("raw\0bits", 8)}};
    std::vector<T> values;
    for (std::size_t row = 0; row < kDomainRows[domain]; ++row) {
      values.push_back(std::numeric_limits<T>::lowest());
      values.push_back(std::numeric_limits<T>::max());
    }
    channel.values = values;
    fixture.attributes.push_back(channel);
    channel.name += "_ragged";
    channel.offsets.emplace();
    for (std::size_t row = 0; row <= kDomainRows[domain]; ++row)
      channel.offsets->push_back(row * kPairWidth);
    fixture.attributes.push_back(channel);
  }
}
void SevenTypesEveryDomain() {
  auto fixture = TwoTriangles();
  AddEncoding<float>(fixture, "float32_extrema");
  AddEncoding<double>(fixture, "float64_extrema");
  AddEncoding<std::int32_t>(fixture, "int32_extrema");
  AddEncoding<std::uint8_t>(fixture, "uint8_extrema");
  AddEncoding<std::uint16_t>(fixture, "uint16_extrema");
  AddEncoding<std::uint32_t>(fixture, "uint32_extrema");
  AddEncoding<std::uint64_t>(fixture, "uint64_extrema");
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, kRemoveSecond);
  Accepted(result);
  ExpectRawRows(source.View(), result.repair->Candidate().View(), kFirstFace,
                kFirstCorners);
  AttributeFixture wide;
  wide.domain = geo::AttributeDomain::face;
  wide.name = "adjacent_uint64";
  wide.values = std::vector<std::uint64_t>{kAdjacentWideInteger - 1,
                                           kAdjacentWideInteger};
  fixture.attributes.push_back(wide);
  Rejected(Repair(Capture(fixture, context), kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
  AttributeFixture close;
  close.domain = geo::AttributeDomain::face;
  close.name = "no_tolerance";
  close.values =
      std::vector<float>{0.0F, std::numeric_limits<float>::denorm_min()};
  fixture.attributes = {close};
  Rejected(Repair(Capture(fixture, context), kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
}
void AllSetsAndRepresentations() {
  auto fixture = TwoTriangles();
  AttributeFixture first;
  first.domain = geo::AttributeDomain::corner;
  first.name = "uv_primary";
  first.semantic = kTexcoordSemantic;
  first.components = kPairWidth;
  first.values = std::vector<float>{-0.0F, 2, 1, 3, 4, 5, 0, 2, 1, 3, 4, 5};
  AttributeFixture secondary = first;
  secondary.name = "uv_secondary";
  secondary.set_index = 7;
  AttributeFixture color = first;
  color.name = "color_secondary";
  color.semantic = kColorSemantic;
  color.set_index = 0;
  color.present = std::vector<std::uint8_t>{1, 1, 1, 1, 1, 1};
  AttributeFixture joints;
  joints.name = "variable_joints";
  joints.semantic = kJointSemantic;
  joints.set_index = 2;
  joints.offsets = std::vector<geo::index_t>{0, 7, 7, 9, 9};
  joints.values = std::vector<std::uint16_t>{0, 1, 2, 3, 4, 5, 6, 17, 19};
  joints.metadata = {{"association", "unvalidated_skin"}};
  AttributeFixture weights = joints;
  weights.name = "variable_weights";
  weights.semantic = kWeightSemantic;
  weights.values = std::vector<double>{2, -1, 0.25, 0.5, 3, 4, 5, 8, 9};
  AttributeFixture normals = first;
  normals.name = "unnormalized_normals";
  normals.semantic = kNormalSemantic;
  AttributeFixture material;
  material.domain = geo::AttributeDomain::face;
  material.name = "material_reference";
  material.semantic = kMaterialSemantic;
  material.values = std::vector<std::uint32_t>{4000000000U, 4000000000U};
  AttributeFixture identity = material;
  identity.name = "authored_identity";
  identity.semantic = kIdentitySemantic;
  fixture.attributes = {first,   secondary, color,    joints,
                        weights, normals,   material, identity};
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, kRemoveSecond);
  Accepted(result);
  ExpectRawRows(source.View(), result.repair->Candidate().View(), kFirstFace,
                kFirstCorners);
  std::get<std::vector<float>>(fixture.attributes[1].values)[6] = 99;
  Rejected(Repair(Capture(fixture, context), kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
}
void AuthoredRaggedRows() {
  auto fixture = TwoTriangles();
  AttributeFixture ragged;
  ragged.domain = geo::AttributeDomain::face;
  ragged.name = "ragged_face";
  ragged.offsets = std::vector<geo::index_t>{0, 2, 4};
  ragged.values = std::vector<std::uint64_t>{kAdjacentWideInteger, 19,
                                             kAdjacentWideInteger, 19};
  ragged.present = std::vector<std::uint8_t>{1, 1};
  fixture.attributes = {ragged};
  const auto context = Context();
  const auto source = Capture(fixture, context);
  const auto result = Repair(source, kRemoveSecond);
  Accepted(result);
  ExpectRawRows(source.View(), result.repair->Candidate().View(), kFirstFace,
                kFirstCorners);
  fixture.attributes[0].offsets = std::vector<geo::index_t>{0, 1, 4};
  Rejected(Repair(Capture(fixture, context), kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
  fixture.attributes[0].values = std::vector<std::uint64_t>{};
  fixture.attributes[0].offsets = std::vector<geo::index_t>{0, 0, 0};
  Accepted(Repair(Capture(fixture, context), kRemoveSecond));
  fixture.attributes[0].present = std::vector<std::uint8_t>{1, 0};
  Rejected(Repair(Capture(fixture, context), kRemoveSecond),
           MaterializedDuplicateDiagnosticCode::kNotEquivalent);
}
void UnknownPolicy() {
  const auto context = Context();
  for (std::size_t domain = 0; domain < kDomains.size(); ++domain) {
    auto fixture = TwoTriangles();
    AttributeFixture custom;
    custom.domain = kDomains[domain];
    custom.name = "custom_channel";
    custom.semantic = kUnknownSemantic;
    custom.values = std::vector<std::int32_t>(kDomainRows[domain], 13);
    fixture.attributes = {custom};
    const auto source = Capture(fixture, context);
    const auto blocked = Repair(source, kRemoveSecond);
    Rejected(blocked,
             MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute);
    Require(blocked.diagnostic.attribute == 0,
            "Unknown channel diagnostic lost its ordinal");
    Accepted(Repair(source, {}), CandidateOutcome::unchanged);
    const std::array<RowLocalAttributeDeclaration, 1> declared = {
        {{kDomains[domain], "custom_channel"}}};
    Accepted(Repair(source, kRemoveSecond, declared));
    const std::array<RowLocalAttributeDeclaration, 1> wrong_domain = {
        {{kDomains[(domain + 1) % kDomains.size()], "custom_channel"}}};
    Rejected(Repair(source, kRemoveSecond, wrong_domain),
             MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute);
    if (kDomains[domain] != geo::AttributeDomain::vertex) {
      std::get<std::vector<std::int32_t>>(fixture.attributes[0].values).back() =
          29;
      Rejected(Repair(Capture(fixture, context), kRemoveSecond, declared),
               MaterializedDuplicateDiagnosticCode::kNotEquivalent);
    }
    constexpr std::array<std::string_view, 2> kOtherUnknownLabels = {"",
                                                                     "Normal"};
    for (const auto semantic : kOtherUnknownLabels) {
      fixture.attributes[0].semantic = semantic;
      Rejected(Repair(Capture(fixture, context), kRemoveSecond),
               MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute);
    }
  }
}
}  // namespace
void CheckAttributes() {
  SevenTypesEveryDomain();
  AllSetsAndRepresentations();
  AuthoredRaggedRows();
  UnknownPolicy();
}
}  // namespace meshvale::repair::materialized_semantics
