// SPDX-License-Identifier: Apache-2.0
#include <exception>
#include <iostream>

#include "fixture_support.h"

int main() {
  try {
    meshvale::repair::materialized_semantics::CheckEquality();
    meshvale::repair::materialized_semantics::CheckAttributes();
    meshvale::repair::materialized_semantics::CheckCorrespondence();
    meshvale::repair::materialized_semantics::CheckLifetimesAndAdmission();
    std::cout << "Materialized duplicate semantic fixtures passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
