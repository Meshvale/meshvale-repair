// SPDX-License-Identifier: Apache-2.0
// Keep repetitions and order to verify include-guard compatibility.
// clang-format off
#include "meshvale/repair/duplicates.hpp"
#include "meshvale/repair/duplicates.hpp"
#include "meshvale/repair/duplicates.h"
// clang-format on

meshvale::repair::DuplicateResult TestLegacyHeader() {
  const meshvale::geometry::Mesh source;
  return meshvale::repair::remove_duplicate_faces(source, {});
}
