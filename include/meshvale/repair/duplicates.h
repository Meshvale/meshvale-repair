// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_DUPLICATES_H_
#define MESHVALE_REPAIR_DUPLICATES_H_

#include <optional>
#include <string>
#include <vector>

#include "meshvale/geometry/attributes.h"
#include "meshvale/geometry/mesh.h"

namespace meshvale::repair {

struct DuplicateTarget {
    geometry::index_t keep;
    geometry::index_t remove;
};
struct AttributeKey {
    geometry::AttributeDomain domain;
    std::string name;
};
struct DuplicateOptions {
    // Explicit declaration: these values have no references to changed element indices.
    std::vector<AttributeKey> row_local_attributes;
};
enum class CandidateOutcome { accepted, unchanged, rejected };
struct DuplicateResult {
    CandidateOutcome outcome = CandidateOutcome::rejected;
    std::optional<geometry::Mesh> candidate;
    std::vector<geometry::Diagnostic> diagnostics;
    // Input element index -> output element index, including removed copies.
    std::vector<geometry::index_t> face_map;
    std::vector<geometry::index_t> corner_map;
};

[[nodiscard]] DuplicateResult remove_duplicate_faces(
    const geometry::Mesh& source, const std::vector<DuplicateTarget>& targets,
    const DuplicateOptions& options = {});

}  // namespace meshvale::repair

#endif  // MESHVALE_REPAIR_DUPLICATES_H_
