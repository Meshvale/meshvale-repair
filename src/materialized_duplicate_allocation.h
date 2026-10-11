// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_ALLOCATION_H_
#define MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_ALLOCATION_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>

#include <cstddef>
#include <exception>
#include <optional>
#include <stop_token>
#include <type_traits>

namespace meshvale::repair::materialized_duplicates_detail {

enum class AdmissionCode {
  kSizeLimit,
  kBudgetExceeded,
  kCanceled,
  kInvalidObject
};
class AdmissionFailure : public std::exception {
 public:
  explicit AdmissionFailure(AdmissionCode code) noexcept;
  [[nodiscard]] AdmissionCode Code() const noexcept;
  [[nodiscard]] const char* what() const noexcept override;

 private:
  AdmissionCode code_;
};

[[nodiscard]] std::size_t CheckedProduct(std::size_t count, std::size_t size);
[[nodiscard]] std::size_t CheckedSum(std::size_t first, std::size_t second);

// Private injection seam. Production uses the stateless default backend.
// An injected state must outlive every block allocated through it; callbacks
// must match aligned allocation/free and deallocate must neither throw nor
// grow.
struct AllocationBackend {
  void* (*allocate)(std::size_t bytes, std::size_t alignment, void* state);
  void (*deallocate)(void* block, std::size_t bytes, std::size_t alignment,
                     void* state) noexcept;
  void* state;
};
struct OperationBudget;
// Intrusive ownership avoids an uncharged shared-pointer control block. The
// budget itself is a single charged allocation; blocks retain it until freed.
class OperationOwner {
 public:
  OperationOwner() noexcept;
  OperationOwner(const OperationOwner&) noexcept;
  OperationOwner& operator=(const OperationOwner&) noexcept;
  OperationOwner(OperationOwner&&) noexcept;
  OperationOwner& operator=(OperationOwner&&) noexcept;
  ~OperationOwner() noexcept;
  [[nodiscard]] bool Reserve(std::size_t bytes) const noexcept;
  void Release(std::size_t bytes) const noexcept;
  [[nodiscard]] std::size_t ActiveBytes() const noexcept;
  [[nodiscard]] bool SameOwner(const OperationOwner& other) const noexcept;
  [[nodiscard]] std::size_t RemainingBytes() const noexcept;
  void SetDelegatedBytes(std::size_t bytes) const;
  void SetFrozen(bool frozen) const;

 private:
  friend class AllocationContext;
  explicit OperationOwner(OperationBudget* budget) noexcept;
  void Reset() noexcept;
  OperationBudget* budget_ = nullptr;
};
[[nodiscard]] AllocationBackend DefaultAllocationBackend() noexcept;
[[nodiscard]] std::size_t AllocationRequestBytes(std::size_t bytes,
                                                 std::size_t alignment);

class AllocationContext {
 public:
  // Detached default enables standard rebound container construction without
  // creating an uncharged ledger. Allocation on it fails explicitly.
  AllocationContext() noexcept;
  AllocationContext(const geometry::ExecutionContext& context,
                    std::stop_token stop,
                    AllocationBackend backend = DefaultAllocationBackend());
  // Keep ledger-owner special members in the compiled allocation module.
  AllocationContext(const AllocationContext& other) noexcept;
  AllocationContext(AllocationContext&& other) noexcept;
  AllocationContext& operator=(const AllocationContext& other) noexcept;
  AllocationContext& operator=(AllocationContext&& other) noexcept;
  ~AllocationContext() noexcept;
  [[nodiscard]] void* Allocate(std::size_t bytes, std::size_t alignment) const;
  // Nonallocating operation checkpoint; cleanup must never call it.
  void Checkpoint() const;
  // A new operation cap includes its charged accounting state, full aligned
  // block requests and overlapping replacements. Root ledger remains shared.
  // Calling on an already capped context is invalid. Actual backend exceptions
  // preserve both counters. Copies/rebounds retain the same operation cap.
  [[nodiscard]] AllocationContext WithRequestLimit(std::size_t bytes) const;
  [[nodiscard]] std::size_t OperationActiveBytes() const noexcept;
  // Synchronous barriers freeze this operation's allocation growth. The caller
  // retains each delegate exactly once alongside its contribution. Mutation is
  // caller-only and happens after every Repair worker has joined.
  [[nodiscard]] std::size_t RemainingBytes() const noexcept;
  void SetDelegatedBytes(std::size_t bytes) const;
  void SetFrozen(bool frozen) const;
  static void Deallocate(void* pointer) noexcept;
  [[nodiscard]] bool SharesBudget(
      const AllocationContext& other) const noexcept;

 private:
  std::optional<geometry::ExecutionContext> context_;
  std::stop_token stop_;
  AllocationBackend backend_;
  OperationOwner operation_;
};

// Only standard allocator templates live here. Block ownership, arithmetic,
// admission, cleanup and backend implementation are compiled in the owning
// materialized_duplicate_allocation.cpp module.
template <class T>
class Allocator {
 public:
  using value_type = T;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::false_type;

  Allocator() noexcept = default;
  explicit Allocator(const AllocationContext& context) : context_(context) {}
  template <class U>
  Allocator(const Allocator<U>& other) : context_(other.Context()) {}
  [[nodiscard]] T* allocate(std::size_t count) {
    return static_cast<T*>(
        context_.Allocate(CheckedProduct(count, sizeof(T)), alignof(T)));
  }
  void deallocate(T* pointer, std::size_t) noexcept {
    AllocationContext::Deallocate(pointer);
  }
  [[nodiscard]] const AllocationContext& Context() const noexcept {
    return context_;
  }
  template <class U>
  [[nodiscard]] bool operator==(const Allocator<U>& other) const noexcept {
    return context_.SharesBudget(other.Context());
  }

 private:
  AllocationContext context_;
};

}  // namespace meshvale::repair::materialized_duplicates_detail
#endif  // MESHVALE_REPAIR_SRC_MATERIALIZED_DUPLICATE_ALLOCATION_H_
