// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/duplicates.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace meshvale::repair {
namespace {
using namespace geometry;

std::pair<index_t, index_t> row_span(const Attribute& a, index_t row) {
    if (a.offsets) return {(*a.offsets)[row], (*a.offsets)[row + 1]};
    return {row * a.components, (row + 1) * a.components};
}
bool authored(const Attribute& a, index_t row) {
    return !a.present || (*a.present)[row] == 1;
}
bool rows_equal(const Attribute& a, index_t left, const Attribute& b,
                index_t right, bool compare_backing = false) {
    if (authored(a, left) != authored(b, right)) return false;
    if (!compare_backing && !authored(a, left)) return true;
    if (a.values.index() != b.values.index()) return false;
    const auto [begin, end] = row_span(a, left);
    const auto [other, other_end] = row_span(b, right);
    if (end - begin != other_end - other) return false;
    return std::visit([&](const auto& values) {
        using Data = std::decay_t<decltype(values)>;
        using Scalar = typename Data::value_type;
        const auto& rhs = std::get<Data>(b.values);
        for (index_t i = 0; i < end - begin; ++i) {
            if constexpr (std::is_floating_point_v<Scalar>) {
                if (compare_backing) {
                    if (std::bit_cast<std::array<std::byte, sizeof(Scalar)>>(values[begin + i]) !=
                        std::bit_cast<std::array<std::byte, sizeof(Scalar)>>(rhs[other + i]))
                        return false;
                    continue;
                }
            }
            if (values[begin + i] != rhs[other + i]) return false;
        }
        return true;
    }, a.values);
}
std::vector<Diagnostic> finite_attributes(const Mesh& mesh) {
    std::vector<Diagnostic> issues;
    for (const auto& a : mesh.attributes) {
        std::visit([&](const auto& values) {
            using Scalar = typename std::decay_t<decltype(values)>::value_type;
            if constexpr (std::is_floating_point_v<Scalar>) {
                for (index_t row = 0; row < mesh.row_count(a.domain); ++row) {
                    if (!authored(a, row)) continue;
                    const auto [begin, end] = row_span(a, row);
                    for (index_t i = begin; i < end; ++i) {
                        if (!std::isfinite(values[i])) {
                            issues.push_back({"repair.nonfinite_attribute", a.name, row});
                            break;
                        }
                    }
                }
            }
        }, a.values);
    }
    return issues;
}
bool supported(const Attribute& a, const DuplicateOptions& options) {
    const std::array known{"texcoord", "normal", "color", "material_index", "label",
                           "identity", "joint_indices", "joint_weights"};
    if (std::any_of(known.begin(), known.end(), [&](auto s) { return a.semantic == s; }))
        return true;
    return std::any_of(options.row_local_attributes.begin(), options.row_local_attributes.end(),
        [&](const auto& key) { return key.domain == a.domain && key.name == a.name; });
}
std::optional<index_t> matching_rotation(const Mesh& mesh, index_t keep, index_t remove) {
    const auto first = mesh.face_offsets[keep];
    const auto second = mesh.face_offsets[remove];
    const auto size = mesh.face_offsets[keep + 1] - first;
    if (size != mesh.face_offsets[remove + 1] - second) return std::nullopt;
    for (const auto& a : mesh.attributes)
        if (a.domain == AttributeDomain::face && !rows_equal(a, keep, a, remove))
            return std::nullopt;
    // Removed corner i maps to representative corner (i + rotation) modulo size.
    for (index_t rotation = 0; rotation < size; ++rotation) {
        bool equal = true;
        for (index_t i = 0; i < size && equal; ++i) {
            const auto k = first + (i + rotation) % size;
            const auto r = second + i;
            equal = mesh.corner_vertices[k] == mesh.corner_vertices[r];
            for (const auto& a : mesh.attributes)
                if (equal && a.domain == AttributeDomain::corner)
                    equal = rows_equal(a, k, a, r);
        }
        if (equal) return rotation;
    }
    return std::nullopt;
}
Attribute select_rows(const Attribute& source, const std::vector<index_t>& rows) {
    auto result = source;
    if (source.offsets) result.offsets = std::vector<index_t>{0};
    if (source.present) result.present = std::vector<std::uint8_t>{};
    result.values = std::visit([&](const auto& values) -> AttributeValues {
        std::decay_t<decltype(values)> output;
        for (auto row : rows) {
            const auto [begin, end] = row_span(source, row);
            using Difference = typename std::decay_t<decltype(values)>::difference_type;
            output.insert(output.end(), values.begin() + static_cast<Difference>(begin),
                          values.begin() + static_cast<Difference>(end));
            if (result.offsets) result.offsets->push_back(output.size());
            if (result.present) result.present->push_back((*source.present)[row]);
        }
        return output;
    }, source.values);
    return result;
}

bool verify(const Mesh& source, const Mesh& candidate,
            const std::vector<index_t>& kept, const DuplicateResult& result) {
    if (!inspect_storage(candidate).empty() || !finite_attributes(candidate).empty() ||
        candidate.face_count() != kept.size() || candidate.positions != source.positions ||
        candidate.attributes.size() != source.attributes.size() ||
        result.face_map.size() != source.face_count() ||
        result.corner_map.size() != source.corner_vertices.size()) return false;
    for (index_t out = 0; out < kept.size(); ++out) {
        const auto in = kept[out];
        if (result.face_map[in] != out) return false;
        const auto start = source.face_offsets[in];
        const auto destination = candidate.face_offsets[out];
        const auto size = source.face_offsets[in + 1] - start;
        if (candidate.face_offsets[out + 1] - destination != size) return false;
        for (index_t i = 0; i < size; ++i) {
            if (result.corner_map[start + i] != destination + i ||
                source.corner_vertices[start + i] != candidate.corner_vertices[destination + i])
                return false;
        }
    }
    for (index_t a = 0; a < source.attributes.size(); ++a) {
        const auto& before = source.attributes[a];
        const auto& after = candidate.attributes[a];
        if (before.domain != after.domain || before.name != after.name ||
            before.semantic != after.semantic || before.set_index != after.set_index ||
            before.components != after.components || before.metadata != after.metadata ||
            before.offsets.has_value() != after.offsets.has_value() ||
            before.present.has_value() != after.present.has_value()) return false;
        for (index_t row = 0; row < candidate.row_count(after.domain); ++row) {
            index_t input = row;
            if (after.domain == AttributeDomain::face) input = kept[row];
            if (after.domain == AttributeDomain::corner) {
                const auto upper = std::upper_bound(candidate.face_offsets.begin(),
                                                    candidate.face_offsets.end(), row);
                const auto face = static_cast<index_t>(upper - candidate.face_offsets.begin() - 1);
                input = source.face_offsets[kept[face]] + row - candidate.face_offsets[face];
            }
            if (!rows_equal(before, input, after, row, true)) return false;
        }
    }
    // Check all original elements through correspondence, including removed rows.
    for (index_t face = 0; face < source.face_count(); ++face) {
        const auto out = result.face_map[face];
        if (out >= candidate.face_count()) return false;
        if (source.face_offsets[face + 1] - source.face_offsets[face] !=
            candidate.face_offsets[out + 1] - candidate.face_offsets[out]) return false;
        for (const auto& a : source.attributes)
            if (a.domain == AttributeDomain::face) {
                const auto ordinal = static_cast<std::size_t>(&a - source.attributes.data());
                if (!rows_equal(a, face, candidate.attributes[ordinal], out)) return false;
            }
        const auto start = source.face_offsets[face];
        const auto output_start = candidate.face_offsets[out];
        const auto size = source.face_offsets[face + 1] - start;
        const auto first_mapped = result.corner_map[start];
        if (first_mapped < output_start || first_mapped >= candidate.face_offsets[out + 1])
            return false;
        for (index_t c = start; c < source.face_offsets[face + 1]; ++c) {
            const auto mapped = result.corner_map[c];
            if (mapped < candidate.face_offsets[out] || mapped >= candidate.face_offsets[out + 1] ||
                mapped != output_start + (first_mapped - output_start + c - start) % size ||
                source.corner_vertices[c] != candidate.corner_vertices[mapped]) return false;
            for (index_t a = 0; a < source.attributes.size(); ++a)
                if (source.attributes[a].domain == AttributeDomain::corner &&
                    !rows_equal(source.attributes[a], c, candidate.attributes[a], mapped)) return false;
        }
    }
    return true;
}
}  // namespace

DuplicateResult remove_duplicate_faces(const geometry::Mesh& source,
    const std::vector<DuplicateTarget>& targets, const DuplicateOptions& options) {
    using namespace geometry;
    DuplicateResult result;
    result.diagnostics = inspect_storage(source);
    if (!result.diagnostics.empty()) return result;
    result.diagnostics = finite_attributes(source);
    if (!result.diagnostics.empty()) return result;
    if (!targets.empty()) {
        for (const auto& a : source.attributes)
            if (!supported(a, options))
                result.diagnostics.push_back({"repair.unsupported_attribute", a.name, std::nullopt});
    }
    // A representative is always retained: reject duplicates, chains and cycles before editing.
    std::vector<std::optional<index_t>> representatives(source.face_count());
    std::vector<index_t> rotations(source.face_count(), 0);
    auto issue = [&](const char* code, index_t face) {
        result.diagnostics.push_back({code, "targets", face});
    };
    for (const auto& target : targets) {
        if (target.keep >= source.face_count() || target.remove >= source.face_count()) {
            issue("repair.target_range", target.remove);
            continue;
        }
        if (target.keep == target.remove) issue("repair.self_target", target.remove);
        if (representatives[target.remove]) issue("repair.repeated_target", target.remove);
        representatives[target.remove] = target.keep;
    }
    for (const auto& target : targets)
        if (target.keep < source.face_count() && representatives[target.keep])
            issue("repair.removed_representative", target.keep);
    if (!result.diagnostics.empty()) return result;
    for (const auto& target : targets) {
        const auto rotation = matching_rotation(source, target.keep, target.remove);
        if (!rotation) issue("repair.not_equivalent", target.remove);
        else rotations[target.remove] = *rotation;
    }
    if (!result.diagnostics.empty()) return result;

    std::vector<index_t> kept, corners;
    Mesh candidate;
    candidate.positions = source.positions;
    result.face_map.resize(source.face_count());
    result.corner_map.resize(source.corner_vertices.size());
    for (index_t face = 0; face < source.face_count(); ++face) {
        if (representatives[face]) continue;
        result.face_map[face] = kept.size();
        kept.push_back(face);
        for (index_t c = source.face_offsets[face]; c < source.face_offsets[face + 1]; ++c) {
            result.corner_map[c] = candidate.corner_vertices.size();
            corners.push_back(c);
            candidate.corner_vertices.push_back(source.corner_vertices[c]);
        }
        candidate.face_offsets.push_back(candidate.corner_vertices.size());
    }
    for (index_t face = 0; face < source.face_count(); ++face) {
        if (!representatives[face]) continue;
        const auto keep = *representatives[face];
        result.face_map[face] = result.face_map[keep];
        const auto start = source.face_offsets[face];
        const auto size = source.face_offsets[face + 1] - start;
        for (index_t i = 0; i < size; ++i)
            result.corner_map[start + i] = result.corner_map[
                source.face_offsets[keep] + (i + rotations[face]) % size];
    }
    for (const auto& a : source.attributes) {
        if (a.domain == AttributeDomain::vertex) candidate.attributes.push_back(a);
        else candidate.attributes.push_back(select_rows(a,
            a.domain == AttributeDomain::face ? kept : corners));
    }
    if (!verify(source, candidate, kept, result)) {
        result.face_map.clear();
        result.corner_map.clear();
        result.diagnostics.push_back({"repair.postcondition_failed", "candidate", std::nullopt});
        return result;
    }
    result.outcome = targets.empty() ? CandidateOutcome::unchanged : CandidateOutcome::accepted;
    result.candidate = std::move(candidate);
    return result;
}
}  // namespace meshvale::repair
