// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_GLTF_FILE_SHA256_ALLOCATION_H_
#define MESHVALE_REPAIR_TESTS_GLTF_FILE_SHA256_ALLOCATION_H_

#include <cstddef>

namespace meshvale::repair::test_sha256_allocation {
// Test-executable-only original new observer. Definitions stay in a separate
// translation unit so allocation controls do not expose replacement bodies.
[[nodiscard]] std::size_t AllocationCalls() noexcept;
void SetAllocationDisallowed(bool disallowed) noexcept;
}  // namespace meshvale::repair::test_sha256_allocation
#endif  // MESHVALE_REPAIR_TESTS_GLTF_FILE_SHA256_ALLOCATION_H_
