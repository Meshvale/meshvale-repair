// SPDX-License-Identifier: Apache-2.0
#include "materialized_duplicate_allocation.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <limits>
#include <new>
#include <utility>

namespace meshvale::repair::materialized_duplicates_detail {
struct OperationBudget {
  std::atomic<std::size_t> references{1};
  std::atomic<std::size_t> active;
  std::atomic<std::size_t> delegated{0};
  std::atomic<bool> frozen{false};
  std::size_t maximum;
  OperationBudget(std::size_t seed, std::size_t limit)
      : active(seed), maximum(limit) {}
};
namespace {
struct Header {
  geometry::PayloadLease lease;
  OperationOwner operation;
  AllocationBackend backend;
  void* block;
  std::size_t request_bytes;
  std::size_t alignment;
};

void* AllocateDefault(std::size_t bytes, std::size_t alignment, void*) {
  return ::operator new(bytes, std::align_val_t(alignment));
}
void DeallocateDefault(void* block, std::size_t, std::size_t alignment,
                       void*) noexcept {
  ::operator delete(block, std::align_val_t(alignment));
}
std::size_t BlockAlignment(std::size_t alignment) {
  if (!std::has_single_bit(alignment))
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  return std::max(alignment, alignof(Header));
}
std::size_t PrefixBytes(std::size_t alignment) {
  const auto remainder = sizeof(Header) % alignment;
  const auto padding = remainder == 0 ? 0 : alignment - remainder;
  if (padding > std::numeric_limits<std::size_t>::max() - sizeof(Header))
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  return sizeof(Header) + padding;
}
}  // namespace

AdmissionFailure::AdmissionFailure(AdmissionCode code) noexcept : code_(code) {}
AdmissionCode AdmissionFailure::Code() const noexcept { return code_; }
const char* AdmissionFailure::what() const noexcept {
  return "materialized duplicate allocation admission failed";
}
std::size_t CheckedProduct(std::size_t count, std::size_t size) {
  if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  return count * size;
}
std::size_t CheckedSum(std::size_t first, std::size_t second) {
  if (second > std::numeric_limits<std::size_t>::max() - first)
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  return first + second;
}
AllocationBackend DefaultAllocationBackend() noexcept {
  return {AllocateDefault, DeallocateDefault, nullptr};
}
std::size_t AllocationRequestBytes(std::size_t bytes, std::size_t alignment) {
  const auto prefix = PrefixBytes(BlockAlignment(alignment));
  if (bytes > std::numeric_limits<std::size_t>::max() - prefix)
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  return prefix + bytes;
}
AllocationContext::AllocationContext() noexcept
    : backend_(DefaultAllocationBackend()) {}
AllocationContext::AllocationContext(const geometry::ExecutionContext& context,
                                     std::stop_token stop,
                                     AllocationBackend backend)
    : context_(context), stop_(stop), backend_(backend) {
  (void)context.WorkerBudget();  // Preserve Geometry's moved-object exception.
  if (!backend_.allocate || !backend_.deallocate)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
}
AllocationContext::AllocationContext(const AllocationContext& other) noexcept =
    default;
AllocationContext::AllocationContext(AllocationContext&& other) noexcept =
    default;
AllocationContext& AllocationContext::operator=(
    const AllocationContext& other) noexcept = default;
AllocationContext& AllocationContext::operator=(
    AllocationContext&& other) noexcept = default;
AllocationContext::~AllocationContext() noexcept = default;
void* AllocationContext::Allocate(std::size_t bytes,
                                  std::size_t alignment) const {
  if (!context_) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  const auto block_alignment = BlockAlignment(alignment);
  const auto prefix = PrefixBytes(block_alignment);
  const auto request_bytes = AllocationRequestBytes(bytes, alignment);
  if (!operation_.Reserve(request_bytes))
    throw AdmissionFailure(AdmissionCode::kBudgetExceeded);
  // Operation admission is independent from the root ledger and takes no
  // ledger lock. Every exceptional exit rolls back the actual requested block.
  struct Reservation {
    const OperationOwner& operation;
    std::size_t bytes;
    bool handed_off = false;
    ~Reservation() {
      if (!handed_off) operation.Release(bytes);
    }
  } reservation{operation_, request_bytes};
  auto result = geometry::TryReservePayload(*context_, request_bytes, stop_);
  switch (result.status) {
    case geometry::ReservationStatus::kBudgetExceeded:
      throw AdmissionFailure(AdmissionCode::kBudgetExceeded);
    case geometry::ReservationStatus::kCanceled:
      throw AdmissionFailure(AdmissionCode::kCanceled);
    case geometry::ReservationStatus::kAccepted:
      break;
  }
  void* block =
      backend_.allocate(request_bytes, block_alignment, backend_.state);
  if (!block) throw std::bad_alloc();
  if (stop_.stop_requested()) {
    backend_.deallocate(block, request_bytes, block_alignment, backend_.state);
    throw AdmissionFailure(AdmissionCode::kCanceled);
  }
  auto* payload = static_cast<std::byte*>(block) + prefix;
  auto* header = new (payload - sizeof(Header))
      Header{std::move(result.lease), operation_,     backend_, block,
             request_bytes,           block_alignment};
  (void)header;
  reservation.handed_off = true;
  return payload;
}
void AllocationContext::Checkpoint() const {
  if (!context_) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  if (stop_.stop_requested()) throw AdmissionFailure(AdmissionCode::kCanceled);
}
void AllocationContext::Deallocate(void* pointer) noexcept {
  if (!pointer) return;
  auto* header = reinterpret_cast<Header*>(static_cast<std::byte*>(pointer) -
                                           sizeof(Header));
  auto lease = std::move(header->lease);
  auto operation = std::move(header->operation);
  const auto backend = header->backend;
  const auto block = header->block;
  const auto request_bytes = header->request_bytes;
  const auto alignment = header->alignment;
  header->~Header();
  backend.deallocate(block, request_bytes, alignment, backend.state);
  operation.Release(request_bytes);
  lease.Reset();
}
bool AllocationContext::SharesBudget(
    const AllocationContext& other) const noexcept {
  if (!context_ || !other.context_) return !context_ && !other.context_;
  return geometry::SharesExecutionBudget(*context_, *other.context_) &&
         operation_.SameOwner(other.operation_);
}
OperationOwner::OperationOwner() noexcept = default;
OperationOwner::OperationOwner(OperationBudget* budget) noexcept
    : budget_(budget) {}
OperationOwner::OperationOwner(const OperationOwner& other) noexcept
    : budget_(other.budget_) {
  if (budget_) budget_->references.fetch_add(1, std::memory_order_relaxed);
}
OperationOwner& OperationOwner::operator=(
    const OperationOwner& other) noexcept {
  if (this == &other) return *this;
  OperationOwner copy(other);
  std::swap(budget_, copy.budget_);
  return *this;
}
OperationOwner::OperationOwner(OperationOwner&& other) noexcept
    : budget_(std::exchange(other.budget_, nullptr)) {}
OperationOwner& OperationOwner::operator=(OperationOwner&& other) noexcept {
  if (this == &other) return *this;
  Reset();
  budget_ = std::exchange(other.budget_, nullptr);
  return *this;
}
OperationOwner::~OperationOwner() noexcept { Reset(); }
void OperationOwner::Reset() noexcept {
  auto* budget = std::exchange(budget_, nullptr);
  if (budget &&
      budget->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    budget->~OperationBudget();
    AllocationContext::Deallocate(budget);
  }
}
bool OperationOwner::Reserve(std::size_t bytes) const noexcept {
  if (!budget_) return true;
  if (budget_->frozen.load(std::memory_order_acquire)) return false;
  const auto delegated = budget_->delegated.load(std::memory_order_relaxed);
  auto current = budget_->active.load(std::memory_order_relaxed);
  do {
    if (delegated > budget_->maximum ||
        current > budget_->maximum - delegated ||
        bytes > budget_->maximum - delegated - current)
      return false;
  } while (!budget_->active.compare_exchange_weak(current, current + bytes,
                                                  std::memory_order_relaxed));
  return true;
}
void OperationOwner::Release(std::size_t bytes) const noexcept {
  if (budget_) budget_->active.fetch_sub(bytes, std::memory_order_relaxed);
}
std::size_t OperationOwner::ActiveBytes() const noexcept {
  return budget_ ? budget_->active.load(std::memory_order_relaxed) : 0;
}
std::size_t OperationOwner::RemainingBytes() const noexcept {
  if (!budget_) return 0;
  const auto active = budget_->active.load(std::memory_order_relaxed);
  const auto delegated = budget_->delegated.load(std::memory_order_relaxed);
  if (delegated > budget_->maximum || active > budget_->maximum - delegated)
    return 0;
  return budget_->maximum - delegated - active;
}
void OperationOwner::SetDelegatedBytes(std::size_t bytes) const {
  if (!budget_ || budget_->frozen.load(std::memory_order_acquire))
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  const auto active = budget_->active.load(std::memory_order_relaxed);
  if (bytes > budget_->maximum || active > budget_->maximum - bytes)
    throw AdmissionFailure(AdmissionCode::kBudgetExceeded);
  budget_->delegated.store(bytes, std::memory_order_relaxed);
}
void OperationOwner::SetFrozen(bool frozen) const {
  if (!budget_) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  budget_->frozen.store(frozen, std::memory_order_release);
}
bool OperationOwner::SameOwner(const OperationOwner& other) const noexcept {
  return budget_ == other.budget_;
}
AllocationContext AllocationContext::WithRequestLimit(std::size_t bytes) const {
  if (operation_.budget_) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  const auto seed =
      AllocationRequestBytes(sizeof(OperationBudget), alignof(OperationBudget));
  if (bytes < seed) throw AdmissionFailure(AdmissionCode::kBudgetExceeded);
  AllocationContext result(*this);
  auto* storage = Allocate(sizeof(OperationBudget), alignof(OperationBudget));
  auto* budget = new (storage) OperationBudget(seed, bytes);
  result.operation_ = OperationOwner(budget);
  return result;
}
std::size_t AllocationContext::OperationActiveBytes() const noexcept {
  return operation_.ActiveBytes();
}
std::size_t AllocationContext::RemainingBytes() const noexcept {
  return operation_.RemainingBytes();
}
void AllocationContext::SetDelegatedBytes(std::size_t bytes) const {
  operation_.SetDelegatedBytes(bytes);
}
void AllocationContext::SetFrozen(bool frozen) const {
  operation_.SetFrozen(frozen);
}
}  // namespace meshvale::repair::materialized_duplicates_detail
