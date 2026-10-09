// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/duplicates.h"
#include "meshvale/repair/duplicates.h"
#include "meshvale/repair/duplicates.hpp"

meshvale::repair::DuplicateResult TestCanonicalHeader() {
  const meshvale::geometry::Mesh source;
  return meshvale::repair::remove_duplicate_faces(source, {});
}
