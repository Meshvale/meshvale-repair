// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_ALLOCATION_H_
#define MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_ALLOCATION_H_
#include <cstddef>
namespace meshvale::repair::python::binding_test_allocation {
[[nodiscard]] std::size_t Calls() noexcept;
void Refuse(bool value) noexcept;
}  // namespace meshvale::repair::python::binding_test_allocation
#endif  // MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_ALLOCATION_H_
