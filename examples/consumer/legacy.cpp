// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/duplicates.hpp"

bool CheckLegacyHeader() {
  const meshvale::geometry::Mesh source;
  const auto result = meshvale::repair::remove_duplicate_faces(source, {});
  return result.outcome == meshvale::repair::CandidateOutcome::unchanged &&
         result.candidate && result.candidate->face_count() == 0 &&
         result.face_map.empty() && result.corner_map.empty();
}
