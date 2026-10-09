// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/duplicates.hpp"
#include "meshvale/repair/duplicates.hpp"
#include "meshvale/repair/duplicates.h"

meshvale::repair::DuplicateResult TestLegacyHeader() {
  const meshvale::geometry::Mesh source;
  return meshvale::repair::remove_duplicate_faces(source, {});
}
