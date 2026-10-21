// SPDX-License-Identifier: Apache-2.0
#include "gltf_binding_allocation.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif
namespace {
std::atomic<std::size_t> allocation_calls{0};
std::atomic<bool> disallow_allocation{false};
void CountAllocation() {
  ++allocation_calls;
  if (disallow_allocation.load()) throw std::bad_alloc();
}
}  // namespace
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

namespace meshvale::repair::python::binding_test_allocation {
std::size_t Calls() noexcept { return allocation_calls.load(); }
void Refuse(bool value) noexcept { disallow_allocation = value; }
}  // namespace meshvale::repair::python::binding_test_allocation
