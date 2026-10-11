// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_MATERIALIZED_DUPLICATE_SEMANTICS_FIXTURE_SUPPORT_H_
#define MESHVALE_REPAIR_TESTS_MATERIALIZED_DUPLICATE_SEMANTICS_FIXTURE_SUPPORT_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace meshvale::repair::materialized_semantics {
namespace geo = meshvale::geometry;

inline constexpr std::string_view kLabelSemantic = "label";
inline constexpr std::string_view kIdentitySemantic = "identity";
inline constexpr std::string_view kTexcoordSemantic = "texcoord";
inline constexpr std::string_view kNormalSemantic = "normal";
inline constexpr std::string_view kColorSemantic = "color";
inline constexpr std::string_view kMaterialSemantic = "material_index";
inline constexpr std::string_view kJointSemantic = "joint_indices";
inline constexpr std::string_view kWeightSemantic = "joint_weights";
inline constexpr std::string_view kUnknownSemantic =
    "authored_custom_reference";
inline constexpr std::uint64_t kOperationBudgetBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kLedgerBudgetBytes = 256 * 1024 * 1024;
inline constexpr std::size_t kWorkerBudget = 2;

using OwnedScalars =
    std::variant<std::vector<float>, std::vector<double>,
                 std::vector<std::int32_t>, std::vector<std::uint8_t>,
                 std::vector<std::uint16_t>, std::vector<std::uint32_t>,
                 std::vector<std::uint64_t>>;

struct AttributeFixture {
  geo::AttributeDomain domain = geo::AttributeDomain::vertex;
  std::string name;
  std::string semantic = std::string(kLabelSemantic);
  std::optional<std::uint32_t> set_index;
  std::uint32_t components = 1;
  OwnedScalars values;
  std::optional<std::vector<geo::index_t>> offsets;
  std::optional<std::vector<std::uint8_t>> present;
  std::vector<std::pair<std::string, std::string>> metadata;
};
struct Fixture {
  std::vector<double> positions;
  std::vector<geo::index_t> face_offsets;
  std::vector<geo::index_t> corner_vertices;
  std::vector<AttributeFixture> attributes;
};

void Require(bool condition, const char* message);
[[nodiscard]] geo::ExecutionContext Context();
[[nodiscard]] geo::MeshMaterializationLimits CaptureLimits();
[[nodiscard]] geo::MeshMaterializationWorkPolicy SerialWork();
[[nodiscard]] MaterializedDuplicateLimits RepairLimits();
[[nodiscard]] Fixture TwoTriangles();
[[nodiscard]] Fixture RotatedPolygons();
[[nodiscard]] geo::MaterializedMesh Capture(
    const Fixture& fixture, const geo::ExecutionContext& context);
[[nodiscard]] MaterializedDuplicateResult Repair(
    const geo::MaterializedMesh& source,
    std::span<const DuplicateTarget> targets,
    std::span<const RowLocalAttributeDeclaration> declarations = {});
void Accepted(const MaterializedDuplicateResult& result,
              CandidateOutcome outcome = CandidateOutcome::accepted);
void Rejected(const MaterializedDuplicateResult& result,
              MaterializedDuplicateDiagnosticCode code);
void ExpectIndices(std::span<const geo::index_t> actual,
                   std::span<const geo::index_t> expected);
void ExpectRawRows(const geo::MeshReadView& source,
                   const geo::MeshReadView& candidate,
                   std::span<const geo::index_t> retained_faces,
                   std::span<const geo::index_t> retained_corners);
void CheckEquality();
void CheckAttributes();
void CheckCorrespondence();
void CheckLifetimesAndAdmission();

}  // namespace meshvale::repair::materialized_semantics
#endif  // MESHVALE_REPAIR_TESTS_MATERIALIZED_DUPLICATE_SEMANTICS_FIXTURE_SUPPORT_H_
