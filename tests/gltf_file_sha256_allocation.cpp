// SPDX-License-Identifier: Apache-2.0
#include "gltf_file_sha256_allocation.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <limits>
#include <new>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MESHVALE_REPAIR_TEST_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__) && !defined(MESHVALE_REPAIR_TEST_TSAN)
#define MESHVALE_REPAIR_TEST_TSAN 1
#endif
#ifndef MESHVALE_REPAIR_TEST_TSAN
#define MESHVALE_REPAIR_TEST_TSAN 0
#endif

#if MESHVALE_REPAIR_TEST_TSAN
#include <sanitizer/allocator_interface.h>
#elif defined(_WIN32)
#include <malloc.h>
#endif

namespace {
#if MESHVALE_REPAIR_TEST_TSAN
static_assert(std::atomic<std::size_t>::is_always_lock_free);
constinit std::atomic<std::size_t> allocation_calls{0};
// Count every delivered callback without allocations, exceptions or filters.
void SANITIZER_CDECL CountSanitizerAllocation(const volatile void*,
                                              std::size_t) noexcept {
  allocation_calls.fetch_add(1, std::memory_order_relaxed);
}
void SANITIZER_CDECL ObserveSanitizerFree(const volatile void*) noexcept {}
#else
std::atomic<std::size_t> allocation_calls{0};
std::atomic<bool> disallow_allocation{false};
void CountAllocation() {
  ++allocation_calls;
  if (disallow_allocation.load()) throw std::bad_alloc();
}
#endif
}  // namespace
#if !MESHVALE_REPAIR_TEST_TSAN
// Test-executable-only original new observer. No production hook or allocator.
void* operator new(std::size_t bytes) {
  CountAllocation();
  if (auto* pointer = std::malloc(std::max<std::size_t>(bytes, 1)))
    return pointer;
  throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void* operator new(std::size_t bytes, std::align_val_t alignment) {
  CountAllocation();
  const auto size = std::max<std::size_t>(bytes, 1);
  const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
  if (auto* pointer = _aligned_malloc(size, align)) return pointer;
#else
  if (size > std::numeric_limits<std::size_t>::max() - (align - 1))
    throw std::bad_alloc();
  const auto padded = (size + align - 1) / align * align;
  if (auto* pointer = std::aligned_alloc(align, padded)) return pointer;
#endif
  throw std::bad_alloc();
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
  return ::operator new(bytes, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}
void operator delete(void* pointer, std::size_t,
                     std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}
void operator delete[](void* pointer, std::size_t,
                       std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}

#endif
namespace meshvale::repair::test_sha256_allocation {
ObserverKind Kind() noexcept {
#if MESHVALE_REPAIR_TEST_TSAN
  return ObserverKind::kSanitizerMallocHook;
#else
  return ObserverKind::kOriginalNewRefusal;
#endif
}
bool Initialize() noexcept {
#if MESHVALE_REPAIR_TEST_TSAN
  return __sanitizer_install_malloc_and_free_hooks(CountSanitizerAllocation,
                                                   ObserveSanitizerFree) != 0;
#else
  return true;
#endif
}
std::size_t AllocationCalls() noexcept {
#if MESHVALE_REPAIR_TEST_TSAN
  return allocation_calls.load(std::memory_order_relaxed);
#else
  return allocation_calls.load();
#endif
}
void SetOriginalAllocationDisallowed(bool disallowed) noexcept {
#if MESHVALE_REPAIR_TEST_TSAN
  (void)disallowed;
  std::terminate();
#else
  disallow_allocation = disallowed;
#endif
}
}  // namespace meshvale::repair::test_sha256_allocation
