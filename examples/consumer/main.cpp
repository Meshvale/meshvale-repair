// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/duplicates.h"
#include <iostream>
#include <vector>

bool CheckLegacyHeader();

int main() {
    if (!CheckLegacyHeader()) return 1;
    meshvale::geometry::Mesh input;
    input.positions = {{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    input.face_offsets = {0,4,8};
    input.corner_vertices = {0,1,2,3,2,3,0,1};
    const auto result = meshvale::repair::remove_duplicate_faces(input, {{0,1}});
    if (result.outcome != meshvale::repair::CandidateOutcome::accepted || !result.candidate ||
        result.candidate->face_count() != 1 || input.face_count() != 2 ||
        result.corner_map != std::vector<meshvale::geometry::index_t>({0,1,2,3,2,3,0,1})) return 1;
    std::cout << "Removed the targeted quad copy; retained source and cyclic corner correspondence\n";
}
