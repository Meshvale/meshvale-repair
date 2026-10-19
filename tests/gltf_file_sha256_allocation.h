// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_GLTF_FILE_SHA256_ALLOCATION_H_
#define MESHVALE_REPAIR_TESTS_GLTF_FILE_SHA256_ALLOCATION_H_

#include <cstddef>

namespace meshvale::repair::test_sha256_allocation {
// Test-executable-only observers. TSan owns its allocation operators and only
// reports intercepted allocations; the original observer also refuses new.
enum class ObserverKind { kOriginalNewRefusal, kSanitizerMallocHook };
[[nodiscard]] ObserverKind Kind() noexcept;
// Call exactly once on the main thread before test work or any user threads.
[[nodiscard]] bool Initialize() noexcept;
[[nodiscard]] std::size_t AllocationCalls() noexcept;
// Only original mode supports refusal. Any call in hook mode terminates.
void SetOriginalAllocationDisallowed(bool disallowed) noexcept;
}  // namespace meshvale::repair::test_sha256_allocation
#endif  // MESHVALE_REPAIR_TESTS_GLTF_FILE_SHA256_ALLOCATION_H_
