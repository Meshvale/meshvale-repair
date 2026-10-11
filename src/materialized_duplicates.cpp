// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/materialized_duplicates.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/geometry/vector_buffer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_test_internal.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::materialized_duplicates_detail {
template <class T>
using Vector = std::vector<T, Allocator<T>>;
struct RepairState {
  AllocationContext allocation;
  geometry::MaterializedMesh source;
  std::optional<geometry::MeshInspection> source_inspection;
  std::optional<geometry::MaterializedFaceSelection> selection;
  std::optional<geometry::MeshInspection> candidate_inspection;
  CandidateOutcome outcome = CandidateOutcome::rejected;
  Vector<geometry::index_t> face_map;
  Vector<geometry::index_t> corner_map;
  RepairState(const AllocationContext& allocator,
              const geometry::MaterializedMesh& original)
      : allocation(allocator),
        source(original),
        face_map(Allocator<geometry::index_t>(allocator)),
        corner_map(Allocator<geometry::index_t>(allocator)) {}
  MaterializedDuplicateRequestedBytes Bytes() const noexcept {
    MaterializedDuplicateRequestedBytes bytes;
    bytes.repair = allocation.OperationActiveBytes();
    if (source_inspection)
      bytes.source_inspection = source_inspection->NewRequestedBytes();
    if (selection) bytes.selection = selection->NewRequestedBytes();
    if (candidate_inspection)
      bytes.candidate_inspection = candidate_inspection->NewRequestedBytes();
    // Admission already proves the sum fits the aggregate size_t cap.
    bytes.total = bytes.repair + bytes.source_inspection + bytes.selection +
                  bytes.candidate_inspection;
    return bytes;
  }
  void UpdateDelegates() {
    const auto bytes = Bytes();
    allocation.SetDelegatedBytes(
        CheckedSum(CheckedSum(static_cast<std::size_t>(bytes.source_inspection),
                              static_cast<std::size_t>(bytes.selection)),
                   static_cast<std::size_t>(bytes.candidate_inspection)));
  }
};
struct Access {
  static MaterializedDuplicateRepair Make(
      std::shared_ptr<const RepairState> state) noexcept {
    return MaterializedDuplicateRepair(std::move(state));
  }
};
}  // namespace meshvale::repair::materialized_duplicates_detail

namespace meshvale::repair {
MaterializedDuplicateRepair::MaterializedDuplicateRepair(
    std::shared_ptr<const materialized_duplicates_detail::RepairState>
        state) noexcept
    : state_(std::move(state)) {}
MaterializedDuplicateRepair::MaterializedDuplicateRepair(
    const MaterializedDuplicateRepair&) noexcept = default;
MaterializedDuplicateRepair& MaterializedDuplicateRepair::operator=(
    const MaterializedDuplicateRepair&) noexcept = default;
MaterializedDuplicateRepair::~MaterializedDuplicateRepair() noexcept = default;
const geometry::MaterializedMesh& MaterializedDuplicateRepair::Source()
    const noexcept {
  return state_->source;
}
const geometry::MaterializedMesh& MaterializedDuplicateRepair::Candidate()
    const noexcept {
  return state_->selection->Candidate();
}
const geometry::MeshInspection& MaterializedDuplicateRepair::SourceInspection()
    const noexcept {
  return *state_->source_inspection;
}
const geometry::MeshInspection&
MaterializedDuplicateRepair::CandidateInspection() const noexcept {
  return *state_->candidate_inspection;
}
CandidateOutcome MaterializedDuplicateRepair::Outcome() const noexcept {
  return state_->outcome;
}
std::span<const geometry::index_t> MaterializedDuplicateRepair::FaceMap()
    const noexcept {
  return state_->face_map;
}
std::span<const geometry::index_t> MaterializedDuplicateRepair::CornerMap()
    const noexcept {
  return state_->corner_map;
}
std::span<const geometry::index_t> MaterializedDuplicateRepair::SourceFaces()
    const noexcept {
  return state_->selection->SourceFaces();
}
std::span<const geometry::index_t> MaterializedDuplicateRepair::SourceCorners()
    const noexcept {
  return state_->selection->SourceCorners();
}
MaterializedDuplicateRequestedBytes
MaterializedDuplicateRepair::NewRequestedBytes() const noexcept {
  return state_->Bytes();
}

namespace {
namespace detail = materialized_duplicates_detail;
using geometry::AttributeDomain;
using geometry::AttributeReadView;
using geometry::index_t;
using geometry::MaterializationExecutionStats;
using geometry::MeshMaterializationWorkPolicy;
using geometry::MeshReadView;
using Stage = MaterializedDuplicateStage;
using Code = MaterializedDuplicateDiagnosticCode;
using Status = MaterializedDuplicateStatus;
using Diagnostic = MaterializedDuplicateDiagnostic;
constexpr std::size_t kLogicalComparisonStepBytes = sizeof(std::uint64_t);
constexpr std::uint8_t kRotationDifferent = 0;
constexpr std::uint8_t kRotationEqual = 1;
constexpr std::uint8_t kRotationUnexamined = 2;
constexpr std::uint32_t kFloatExponentMask = UINT32_C(0x7f800000);
constexpr std::uint64_t kDoubleExponentMask = UINT64_C(0x7ff0000000000000);
constexpr std::string_view kTexcoordSemantic = "texcoord";
constexpr std::string_view kNormalSemantic = "normal";
constexpr std::string_view kColorSemantic = "color";
constexpr std::string_view kMaterialSemantic = "material_index";
constexpr std::string_view kLabelSemantic = "label";
constexpr std::string_view kIdentitySemantic = "identity";
constexpr std::string_view kJointsSemantic = "joint_indices";
constexpr std::string_view kWeightsSemantic = "joint_weights";
constexpr std::array kKnownSemantics{
    kTexcoordSemantic, kNormalSemantic,   kColorSemantic,  kMaterialSemantic,
    kLabelSemantic,    kIdentitySemantic, kJointsSemantic, kWeightsSemantic};

Diagnostic Failure(Stage stage, Code code, std::optional<index_t> target = {},
                   std::optional<index_t> attribute = {},
                   std::optional<index_t> element = {}) {
  Diagnostic result;
  result.stage = stage;
  result.code = code;
  result.target = target;
  result.attribute = attribute;
  result.element = element;
  return result;
}
class ValidationFailure : public std::exception {
 public:
  ValidationFailure(Status status, Diagnostic diagnostic) noexcept
      : status_(status), diagnostic_(std::move(diagnostic)) {}
  Status StatusValue() const noexcept { return status_; }
  const Diagnostic& DiagnosticValue() const noexcept { return diagnostic_; }
  const char* what() const noexcept override {
    return "materialized duplicate validation failed";
  }

 private:
  Status status_;
  Diagnostic diagnostic_;
};
[[noreturn]] void Reject(Status status, Diagnostic diagnostic) {
  throw ValidationFailure(status, std::move(diagnostic));
}
bool DomainValid(AttributeDomain domain) {
  return domain == AttributeDomain::vertex || domain == AttributeDomain::face ||
         domain == AttributeDomain::corner;
}
void Required(std::uint64_t value) {
  if (value == 0)
    Reject(Status::kInvalidConfiguration,
           Failure(Stage::kConfiguration, Code::kRequiredFieldZero));
  if (value > std::numeric_limits<std::size_t>::max())
    Reject(Status::kInvalidConfiguration,
           Failure(Stage::kConfiguration, Code::kConfigurationExtent));
}
void Configure(const MaterializedDuplicateLimits& limits,
               const MeshMaterializationWorkPolicy& work) {
  Required(limits.max_targets);
  Required(limits.max_row_local_declarations);
  Required(limits.max_declaration_name_bytes);
  Required(limits.max_comparison_steps_per_target);
  Required(limits.max_new_requested_bytes);
  Required(limits.inspection.max_issues);
  Required(limits.inspection.max_new_requested_bytes);
  const auto& selection = limits.selection;
  Required(selection.max_vertices);
  Required(selection.max_faces);
  Required(selection.max_corners);
  Required(selection.max_face_offsets);
  Required(selection.max_attributes);
  Required(selection.max_attribute_scalars);
  Required(selection.max_attribute_offsets);
  Required(selection.max_presence_bytes);
  Required(selection.max_metadata_entries);
  Required(selection.max_descriptor_bytes);
  Required(selection.max_new_requested_bytes);
  Required(work.parallel_min_bytes);
  Required(work.partition_bytes);
  Required(work.cancellation_block_scalars);
  Required(work.max_jobs);
}
struct ScanSlot {
  std::size_t actual_begin = 0;
  std::size_t actual_end = 0;
  std::size_t first = 0;
  std::optional<Diagnostic> failure;
};
class Session {
 public:
  Session(detail::RepairState& state,
          const geometry::ExecutionContext& execution,
          const MeshMaterializationWorkPolicy& policy, std::stop_token token,
          const detail::TestHooks& injection,
          MaterializedDuplicateExecution& telemetry)
      : state_(state),
        context_(execution),
        work_(policy),
        stop_(token),
        hooks_(injection),
        telemetry_(telemetry) {}
  void Checkpoint(std::size_t ordinal = 0, bool finalizing = false) const {
    if (!finalizing && ordinal % work_.cancellation_block_scalars == 0)
      state_.allocation.Checkpoint();
  }
  template <class Function>
  void Parallel(std::size_t count, std::size_t width, Function& function,
                MaterializationExecutionStats& stats) {
    struct Call {
      Session* session;
      Function* function;
    } call{this, &function};
    detail::RunRange(
        context_, state_.allocation, work_, count, width,
        [](void* argument, std::size_t begin, std::size_t end) {
          auto& item = *static_cast<Call*>(argument);
          for (auto i = begin; i < end; ++i) {
            item.session->Checkpoint(i);
            (*item.function)(i);
          }
        },
        &call, stop_, stats, hooks_.workers, hooks_.worker_observer);
  }
  template <class Predicate>
  std::optional<Diagnostic> Scan(std::size_t count, std::size_t width,
                                 Predicate& predicate,
                                 MaterializationExecutionStats& stats) {
    if (count == 0) return {};
    const auto partition =
        std::max<std::size_t>(1, work_.partition_bytes / width);
    auto jobs = count / partition + (count % partition != 0 ? 1 : 0);
    jobs = std::min<std::size_t>(jobs, work_.max_jobs);
    detail::Vector<ScanSlot> slots{
        detail::Allocator<ScanSlot>(state_.allocation)};
    slots.resize(jobs);
    struct Call {
      Session* session;
      Predicate* predicate;
      std::size_t count;
      std::size_t jobs;
      ScanSlot* slots;
    } call{this, &predicate, count, jobs, slots.data()};
    auto callback = [](void* argument, std::size_t begin, std::size_t end) {
      auto& item = *static_cast<Call*>(argument);
      // The actual caller fallback spans the complete count, not theoretical
      // job zero. Derive native ordinal from its certified dispatched start.
      std::size_t ordinal = 0;
      if (begin != 0) {
        const auto quotient = item.count / item.jobs;
        const auto remainder = item.count % item.jobs;
        const auto wide_prefix = (quotient + 1) * remainder;
        ordinal = begin < wide_prefix
                      ? begin / (quotient + 1)
                      : remainder + (begin - wide_prefix) / quotient;
      }
      auto& slot = item.slots[ordinal];
      slot.actual_begin = begin;
      slot.actual_end = end;
      for (auto i = begin; i < end; ++i) {
        item.session->Checkpoint(i);
        const auto failure = (*item.predicate)(i, false);
        if (failure) {
          slot.first = i;
          slot.failure = failure;
          const auto& hooks = item.session->hooks_;
          if (hooks.failure_observer)
            hooks.failure_observer(*failure, hooks.failure_observer_state);
          return;
        }
      }
    };
    try {
      detail::RunRange(context_, state_.allocation, work_, count, width,
                       callback, &call, stop_, stats, hooks_.workers,
                       hooks_.worker_observer);
    } catch (const detail::AdmissionFailure& failure) {
      if (failure.Code() != detail::AdmissionCode::kCanceled) throw;
      std::optional<std::size_t> known;
      for (const auto& slot : slots)
        if (slot.failure && (!known || slot.first < *known)) known = slot.first;
      if (!known) throw;
      // No allocations, stop checks or discovery callbacks during necessary
      // source-prefix finalization after every worker has joined.
      for (std::size_t i = 0; i <= *known; ++i)
        if (auto first = predicate(i, true)) return first;
      throw;
    }
    std::optional<std::size_t> first;
    for (std::size_t i = 0; i < slots.size(); ++i)
      if (slots[i].failure && (!first || slots[i].first < slots[*first].first))
        first = i;
    return first ? slots[*first].failure : std::optional<Diagnostic>{};
  }
  bool BytesEqual(std::string_view first, std::string_view second,
                  MaterializationExecutionStats& stats) {
    if (first.size() != second.size()) return false;
    detail::Vector<std::uint8_t> equal{
        detail::Allocator<std::uint8_t>(state_.allocation)};
    const auto block = static_cast<std::size_t>(work_.partition_bytes);
    const auto count =
        first.size() / block + (first.size() % block != 0 ? 1 : 0);
    equal.resize(count, 1);
    auto compare = [&](std::size_t ordinal) {
      const auto begin = ordinal * block;
      const auto end = begin + std::min(block, first.size() - begin);
      for (auto i = begin; i < end; ++i) {
        Checkpoint(i);
        if (first[i] != second[i]) {
          equal[ordinal] = 0;
          break;
        }
      }
    };
    Parallel(count, block, compare, stats);
    for (std::size_t i = 0; i < count; ++i) {
      Checkpoint(i);
      if (!equal[i]) return false;
    }
    return true;
  }
  std::size_t DelegateCap(std::uint64_t phase_cap) {
    state_.UpdateDelegates();
    const auto remaining = state_.allocation.RemainingBytes();
    if (remaining == 0)
      throw detail::AdmissionFailure(detail::AdmissionCode::kBudgetExceeded);
    return std::min<std::size_t>(remaining, phase_cap);
  }
  void Freeze(bool value) { state_.allocation.SetFrozen(value); }
  const MeshMaterializationWorkPolicy& Work() const { return work_; }
  detail::RepairState& State() { return state_; }
  MaterializedDuplicateExecution& Telemetry() { return telemetry_; }

 private:
  detail::RepairState& state_;
  const geometry::ExecutionContext& context_;
  const MeshMaterializationWorkPolicy& work_;
  std::stop_token stop_;
  const detail::TestHooks& hooks_;
  MaterializedDuplicateExecution& telemetry_;
};
class Barrier {
 public:
  explicit Barrier(Session& session) : session_(session) {
    session_.Freeze(true);
  }
  ~Barrier() { session_.Freeze(false); }

 private:
  Session& session_;
};
void DelegatedFailure(Stage stage, geometry::MaterializationStatus status,
                      const geometry::MaterializationDiagnostic& diagnostic) {
  auto failed = Failure(stage, Code::kGeometry);
  failed.geometry_status = status;
  failed.geometry_diagnostic = diagnostic;
  Reject(Status::kGeometryFailure, std::move(failed));
}
void Inspection(Session& session, bool candidate,
                const geometry::MeshInspectionLimits& limits,
                std::stop_token stop) {
  const auto stage =
      candidate ? Stage::kCandidateInspection : Stage::kSourceInspection;
  auto effective = limits;
  effective.max_new_requested_bytes =
      session.DelegateCap(limits.max_new_requested_bytes);
  geometry::MeshInspectionResult result;
  {
    Barrier barrier(session);
    result = geometry::InspectMaterializedMesh(
        candidate ? session.State().selection->Candidate()
                  : session.State().source,
        effective, session.Work(), stop);
    if (result.inspection) {
      if (candidate)
        session.State().candidate_inspection = result.inspection;
      else
        session.State().source_inspection = result.inspection;
    }
  }
  if (candidate)
    session.Telemetry().candidate_inspection = result.execution;
  else
    session.Telemetry().source_inspection = result.execution;
  session.State().UpdateDelegates();
  if (result.status != geometry::MaterializationStatus::kSuccess)
    DelegatedFailure(stage, result.status, result.diagnostic);
  if (!result.inspection->IsStorageValid()) {
    auto failed = Failure(stage, Code::kStorageIssues);
    failed.geometry_status = result.status;
    failed.geometry_diagnostic = result.diagnostic;
    failed.storage_issue = result.inspection->Issues().front();
    Reject(candidate ? Status::kVerificationFailure
                     : Status::kInvalidSourceStorage,
           std::move(failed));
  }
}
std::pair<std::size_t, std::size_t> Row(const AttributeReadView& attribute,
                                        std::size_t row) {
  if (attribute.offsets)
    return {static_cast<std::size_t>((*attribute.offsets)[row]),
            static_cast<std::size_t>((*attribute.offsets)[row + 1])};
  return {row * attribute.components, (row + 1) * attribute.components};
}
bool Authored(const AttributeReadView& attribute, std::size_t row) {
  return !attribute.present || (*attribute.present)[row] != 0;
}
std::size_t Rows(const MeshReadView& mesh, AttributeDomain domain) {
  if (domain == AttributeDomain::vertex)
    return mesh.positions.size() / geometry::kVector3Components;
  if (domain == AttributeDomain::face) return mesh.face_offsets.size() - 1;
  return mesh.corner_vertices.size();
}
template <class Scalar>
bool Finite(Scalar value) {
  if constexpr (std::is_same_v<Scalar, float>)
    return (std::bit_cast<std::uint32_t>(value) & kFloatExponentMask) !=
           kFloatExponentMask;
  else if constexpr (std::is_same_v<Scalar, double>)
    return (std::bit_cast<std::uint64_t>(value) & kDoubleExponentMask) !=
           kDoubleExponentMask;
  else
    return true;
}
template <class Scalar>
bool ScalarEqual(Scalar first, Scalar second, bool bits) {
  if constexpr (std::is_same_v<Scalar, float>) {
    if (bits)
      return std::bit_cast<std::uint32_t>(first) ==
             std::bit_cast<std::uint32_t>(second);
  } else if constexpr (std::is_same_v<Scalar, double>) {
    if (bits)
      return std::bit_cast<std::uint64_t>(first) ==
             std::bit_cast<std::uint64_t>(second);
  }
  return first == second;
}
bool EqualRows(Session& session, const AttributeReadView& first,
               std::size_t left, const AttributeReadView& second,
               std::size_t right, bool bits, bool finalizing = false) {
  if (Authored(first, left) != Authored(second, right)) return false;
  if (!bits && !Authored(first, left)) return true;
  if (first.values.index() != second.values.index()) return false;
  const auto [begin, end] = Row(first, left);
  const auto [other, other_end] = Row(second, right);
  if (end - begin != other_end - other) return false;
  return std::visit(
      [&](const auto& values) {
        using Span = std::decay_t<decltype(values)>;
        const auto& rhs = std::get<Span>(second.values);
        for (auto i = begin; i < end; ++i) {
          session.Checkpoint(i - begin, finalizing);
          if (!ScalarEqual(values[i], rhs[other + i - begin], bits))
            return false;
        }
        return true;
      },
      first.values);
}
void CheckFinite(Session& session, const MeshReadView& mesh, Stage stage,
                 MaterializationExecutionStats& stats) {
  for (std::size_t ordinal = 0; ordinal < mesh.attributes.size(); ++ordinal) {
    session.Checkpoint(ordinal);
    const auto& attribute = mesh.attributes[ordinal];
    std::visit(
        [&](const auto& values) {
          using Scalar = typename std::decay_t<decltype(values)>::value_type;
          if constexpr (std::is_floating_point_v<Scalar>) {
            auto check = [&](std::size_t scalar,
                             bool) -> std::optional<Diagnostic> {
              std::size_t row;
              if (attribute.offsets) {
                const auto found =
                    std::upper_bound(attribute.offsets->begin(),
                                     attribute.offsets->end(), scalar);
                row = static_cast<std::size_t>(found -
                                               attribute.offsets->begin() - 1);
              } else {
                row = scalar / attribute.components;
              }
              if (Authored(attribute, row) && !Finite(values[scalar]))
                return Failure(stage, Code::kNonfiniteAttribute, {}, ordinal,
                               row);
              return {};
            };
            if (auto failure =
                    session.Scan(values.size(), sizeof(Scalar), check, stats))
              Reject(stage == Stage::kEligibility
                         ? Status::kRejected
                         : Status::kVerificationFailure,
                     *failure);
          }
        },
        attribute.values);
  }
}
void Declarations(Session& session,
                  std::span<const RowLocalAttributeDeclaration> declarations) {
  for (std::size_t i = 0; i < declarations.size(); ++i) {
    session.Checkpoint(i);
    if (!DomainValid(declarations[i].domain))
      Reject(
          Status::kInvalidConfiguration,
          Failure(Stage::kConfiguration, Code::kDeclarationDomain, {}, {}, i));
    if (declarations[i].name.empty())
      Reject(Status::kInvalidConfiguration,
             Failure(Stage::kConfiguration, Code::kDeclarationNameEmpty, {}, {},
                     i));
    for (std::size_t earlier = 0; earlier < i; ++earlier) {
      session.Checkpoint(earlier);
      if (declarations[i].domain == declarations[earlier].domain &&
          session.BytesEqual(declarations[i].name, declarations[earlier].name,
                             session.Telemetry().eligibility))
        Reject(Status::kInvalidConfiguration,
               Failure(Stage::kConfiguration, Code::kDeclarationDuplicate, {},
                       {}, i));
    }
  }
}
void CheckSemantics(
    Session& session, const MeshReadView& source,
    std::span<const RowLocalAttributeDeclaration> declarations) {
  for (std::size_t i = 0; i < source.attributes.size(); ++i) {
    session.Checkpoint(i);
    const auto& attribute = source.attributes[i];
    bool supported = false;
    for (const auto semantic : kKnownSemantics)
      if (session.BytesEqual(attribute.semantic, semantic,
                             session.Telemetry().eligibility)) {
        supported = true;
        break;
      }
    if (supported) continue;
    for (std::size_t declaration = 0; declaration < declarations.size();
         ++declaration) {
      session.Checkpoint(declaration);
      if (attribute.domain == declarations[declaration].domain &&
          session.BytesEqual(attribute.name, declarations[declaration].name,
                             session.Telemetry().eligibility)) {
        supported = true;
        break;
      }
    }
    if (!supported)
      Reject(Status::kRejected,
             Failure(Stage::kEligibility, Code::kUnsupportedAttribute, {}, i));
  }
}
struct FacePlan {
  std::optional<index_t> representative;
  std::size_t rotation = 0;
};
struct TargetPlan {
  std::size_t begin = 0;
  std::size_t end = 0;
};
std::size_t RowWork(const AttributeReadView& attribute, std::size_t row) {
  if (!Authored(attribute, row)) return 1;
  const auto [begin, end] = Row(attribute, row);
  return detail::CheckedSum(2, end - begin);
}
std::size_t Rotated(std::size_t index, std::size_t rotation,
                    std::size_t count) {
  return index < count - rotation ? index + rotation
                                  : index - (count - rotation);
}
bool RotationEqual(Session& session, const MeshReadView& first,
                   std::size_t keep, const MeshReadView& second,
                   std::size_t remove, std::size_t rotation,
                   bool finalizing = false, bool include_face_rows = true) {
  const auto left = static_cast<std::size_t>(first.face_offsets[keep]);
  const auto right = static_cast<std::size_t>(second.face_offsets[remove]);
  const auto count =
      static_cast<std::size_t>(first.face_offsets[keep + 1]) - left;
  if (include_face_rows && count != second.face_offsets[remove + 1] - right)
    return false;
  for (std::size_t a = 0; a < first.attributes.size(); ++a) {
    session.Checkpoint(a, finalizing);
    if (include_face_rows &&
        first.attributes[a].domain == AttributeDomain::face &&
        !EqualRows(session, first.attributes[a], keep, second.attributes[a],
                   remove, false, finalizing))
      return false;
  }
  for (std::size_t i = 0; i < count; ++i) {
    session.Checkpoint(i, finalizing);
    const auto representative = left + Rotated(i, rotation, count);
    const auto removed = right + i;
    if (first.corner_vertices[representative] !=
        second.corner_vertices[removed])
      return false;
    for (std::size_t a = 0; a < first.attributes.size(); ++a) {
      session.Checkpoint(a, finalizing);
      if (first.attributes[a].domain == AttributeDomain::corner &&
          !EqualRows(session, first.attributes[a], representative,
                     second.attributes[a], removed, false, finalizing))
        return false;
    }
  }
  return true;
}
bool FaceRowsEqual(Session& session, const MeshReadView& first,
                   std::size_t keep, const MeshReadView& second,
                   std::size_t remove, const Diagnostic& diagnostic,
                   MaterializationExecutionStats& stats) {
  if (first.face_offsets[keep + 1] - first.face_offsets[keep] !=
      second.face_offsets[remove + 1] - second.face_offsets[remove])
    return false;
  for (std::size_t a = 0; a < first.attributes.size(); ++a) {
    session.Checkpoint(a);
    const auto& attribute = first.attributes[a];
    const auto& other_attribute = second.attributes[a];
    if (attribute.domain != AttributeDomain::face) continue;
    if (Authored(attribute, keep) != Authored(other_attribute, remove))
      return false;
    if (!Authored(attribute, keep)) continue;
    const auto [begin, end] = Row(attribute, keep);
    const auto [other, other_end] = Row(other_attribute, remove);
    if (end - begin != other_end - other) return false;
    bool equal = true;
    std::visit(
        [&](const auto& values) {
          using Scalar = typename std::decay_t<decltype(values)>::value_type;
          const auto& rhs =
              std::get<std::span<const Scalar>>(other_attribute.values);
          auto compare = [&](std::size_t i, bool) -> std::optional<Diagnostic> {
            if (!ScalarEqual(values[begin + i], rhs[other + i], false))
              return diagnostic;
            return {};
          };
          equal = !session.Scan(end - begin, sizeof(Scalar), compare, stats);
        },
        attribute.values);
    if (!equal) return false;
  }
  return true;
}
void CompareTargets(Session& session, const MeshReadView& source,
                    std::span<const DuplicateTarget> targets,
                    const MaterializedDuplicateLimits& limits,
                    detail::Vector<FacePlan>& faces) {
  detail::Vector<TargetPlan> plans{
      detail::Allocator<TargetPlan>(session.State().allocation)};
  plans.resize(targets.size());
  std::size_t rotations = 0;
  std::size_t total_work = 0;
  for (std::size_t target = 0; target < targets.size(); ++target) {
    session.Checkpoint(target);
    const auto keep = static_cast<std::size_t>(targets[target].keep);
    const auto start = static_cast<std::size_t>(source.face_offsets[keep]);
    const auto count =
        static_cast<std::size_t>(source.face_offsets[keep + 1]) - start;
    std::size_t face_work = 0;
    std::size_t rotation_work = count;
    try {
      for (std::size_t ordinal = 0; ordinal < source.attributes.size();
           ++ordinal) {
        session.Checkpoint(ordinal);
        const auto& attribute = source.attributes[ordinal];
        if (attribute.domain == AttributeDomain::face)
          face_work = detail::CheckedSum(face_work, RowWork(attribute, keep));
        else if (attribute.domain == AttributeDomain::corner)
          for (std::size_t corner = start; corner < start + count; ++corner) {
            session.Checkpoint(corner - start);
            rotation_work =
                detail::CheckedSum(rotation_work, RowWork(attribute, corner));
          }
      }
      const auto bound =
          detail::CheckedSum(detail::CheckedSum(1, face_work),
                             detail::CheckedProduct(count, rotation_work));
      if (bound > limits.max_comparison_steps_per_target)
        Reject(Status::kLimitExceeded,
               Failure(Stage::kComparison, Code::kComparisonLimit, target));
      total_work = detail::CheckedSum(total_work, bound);
      plans[target].begin = rotations;
      rotations = detail::CheckedSum(rotations, count);
      plans[target].end = rotations;
    } catch (const detail::AdmissionFailure& failure) {
      if (failure.Code() != detail::AdmissionCode::kSizeLimit) throw;
      Reject(Status::kLimitExceeded,
             Failure(Stage::kComparison, Code::kComparisonLimit, target));
    }
  }
  detail::Vector<std::uint8_t> matches{
      detail::Allocator<std::uint8_t>(session.State().allocation)};
  matches.resize(rotations, kRotationUnexamined);
  detail::Vector<std::uint8_t> face_matches{
      detail::Allocator<std::uint8_t>(session.State().allocation)};
  face_matches.resize(targets.size(), kRotationUnexamined);
  auto compare = [&](std::size_t ordinal) {
    const auto found =
        std::upper_bound(plans.begin(), plans.end(), ordinal,
                         [](std::size_t index, const TargetPlan& plan) {
                           return index < plan.end;
                         });
    const auto target = static_cast<std::size_t>(found - plans.begin());
    matches[ordinal] =
        face_matches[target] == kRotationDifferent
            ? kRotationDifferent
            : (RotationEqual(session, source, targets[target].keep, source,
                             targets[target].remove, ordinal - found->begin,
                             false, false)
                   ? kRotationEqual
                   : kRotationDifferent);
  };
  const auto average =
      rotations == 0 ? 1 : std::max<std::size_t>(1, total_work / rotations);
  const auto scheduling_width =
      detail::CheckedProduct(average, kLogicalComparisonStepBytes);
  try {
    for (std::size_t target = 0; target < targets.size(); ++target) {
      session.Checkpoint(target);
      face_matches[target] =
          FaceRowsEqual(session, source, targets[target].keep, source,
                        targets[target].remove,
                        Failure(Stage::kComparison, Code::kNotEquivalent,
                                target, {}, targets[target].remove),
                        session.Telemetry().comparison)
              ? kRotationEqual
              : kRotationDifferent;
    }
    session.Parallel(rotations, scheduling_width, compare,
                     session.Telemetry().comparison);
    for (std::size_t target = 0; target < targets.size(); ++target) {
      session.Checkpoint(target);
      std::optional<std::size_t> chosen;
      for (auto ordinal = plans[target].begin; ordinal < plans[target].end;
           ++ordinal) {
        session.Checkpoint(ordinal - plans[target].begin);
        if (matches[ordinal] == kRotationEqual) {
          chosen = ordinal - plans[target].begin;
          break;
        }
      }
      if (!chosen)
        Reject(Status::kRejected,
               Failure(Stage::kComparison, Code::kNotEquivalent, target, {},
                       targets[target].remove));
      faces[targets[target].remove].rotation = *chosen;
    }
  } catch (const detail::AdmissionFailure& failure) {
    if (failure.Code() != detail::AdmissionCode::kCanceled) throw;
    std::optional<std::size_t> known;
    for (std::size_t target = 0; target < targets.size(); ++target) {
      bool failed = face_matches[target] == kRotationDifferent;
      if (face_matches[target] == kRotationEqual) {
        failed = true;
        for (auto i = plans[target].begin; i < plans[target].end; ++i)
          if (matches[i] != kRotationDifferent) {
            failed = false;
            break;
          }
      }
      if (failed) {
        known = target;
        break;
      }
    }
    if (!known) throw;
    // A known rejected target establishes failure; finalize only the earlier
    // target prefix without allocating, observing stop or invoking callbacks.
    for (std::size_t target = 0; target <= *known; ++target) {
      bool equal = false;
      for (std::size_t rotation = 0;
           rotation < plans[target].end - plans[target].begin; ++rotation)
        if (RotationEqual(session, source, targets[target].keep, source,
                          targets[target].remove, rotation, true)) {
          equal = true;
          break;
        }
      if (!equal)
        Reject(Status::kRejected,
               Failure(Stage::kComparison, Code::kNotEquivalent, target, {},
                       targets[target].remove));
    }
    throw;
  }
}
void Select(Session& session, std::span<const index_t> kept,
            const geometry::MeshMaterializationLimits& limits,
            std::stop_token stop) {
  auto effective = limits;
  effective.max_new_requested_bytes =
      session.DelegateCap(limits.max_new_requested_bytes);
  geometry::MeshFaceSelectionResult result;
  {
    Barrier barrier(session);
    result = geometry::SelectMeshFaces(session.State().source, kept, effective,
                                       session.Work(), stop);
    if (result.selection) session.State().selection = result.selection;
  }
  session.Telemetry().selection = result.execution;
  session.State().UpdateDelegates();
  if (result.status != geometry::MaterializationStatus::kSuccess)
    DelegatedFailure(Stage::kSelection, result.status, result.diagnostic);
}
template <class T>
void Initialize(Session& session, detail::Vector<T>& values,
                std::size_t count) {
  values.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    session.Checkpoint(i);
    values.emplace_back();
  }
}
void Targets(Session& session, const MeshReadView& source,
             std::span<const DuplicateTarget> targets,
             detail::Vector<FacePlan>& faces) {
  Initialize(session, faces, source.face_offsets.size() - 1);
  for (std::size_t ordinal = 0; ordinal < targets.size(); ++ordinal) {
    session.Checkpoint(ordinal);
    const auto& target = targets[ordinal];
    if (target.keep >= faces.size() || target.remove >= faces.size())
      Reject(Status::kRejected, Failure(Stage::kTargets, Code::kTargetRange,
                                        ordinal, {}, target.remove));
    if (target.keep == target.remove)
      Reject(Status::kRejected, Failure(Stage::kTargets, Code::kSelfTarget,
                                        ordinal, {}, target.remove));
    if (faces[target.remove].representative)
      Reject(Status::kRejected, Failure(Stage::kTargets, Code::kRepeatedTarget,
                                        ordinal, {}, target.remove));
    faces[target.remove].representative = target.keep;
  }
  for (std::size_t ordinal = 0; ordinal < targets.size(); ++ordinal) {
    session.Checkpoint(ordinal);
    if (faces[targets[ordinal].keep].representative)
      Reject(Status::kRejected,
             Failure(Stage::kTargets, Code::kRemovedRepresentative, ordinal, {},
                     targets[ordinal].keep));
  }
}
void Correspondence(Session& session, const MeshReadView& source,
                    const detail::Vector<FacePlan>& faces,
                    std::span<const index_t> kept,
                    std::span<const DuplicateTarget> targets) {
  auto& state = session.State();
  const auto output = state.selection->Candidate().View();
  auto retained = [&](std::size_t ordinal) {
    state.face_map[kept[ordinal]] = ordinal;
  };
  session.Parallel(kept.size(), sizeof(index_t), retained,
                   session.Telemetry().correspondence);
  auto removed = [&](std::size_t ordinal) {
    state.face_map[targets[ordinal].remove] =
        state.face_map[targets[ordinal].keep];
  };
  session.Parallel(targets.size(), sizeof(index_t), removed,
                   session.Telemetry().correspondence);
  auto corners = [&](std::size_t corner) {
    const auto found = std::upper_bound(source.face_offsets.begin(),
                                        source.face_offsets.end(), corner);
    const auto face =
        static_cast<std::size_t>(found - source.face_offsets.begin() - 1);
    const auto start = static_cast<std::size_t>(source.face_offsets[face]);
    const auto count =
        static_cast<std::size_t>(source.face_offsets[face + 1]) - start;
    state.corner_map[corner] =
        output.face_offsets[state.face_map[face]] +
        Rotated(corner - start, faces[face].rotation, count);
  };
  session.Parallel(source.corner_vertices.size(), sizeof(index_t), corners,
                   session.Telemetry().correspondence);
}
void Postcondition(bool condition, std::optional<index_t> attribute = {},
                   std::optional<index_t> element = {}) {
  if (!condition)
    Reject(Status::kVerificationFailure,
           Failure(Stage::kVerification, Code::kPostcondition, {}, attribute,
                   element));
}
void VerifyBytes(Session& session, std::string_view first,
                 std::string_view second, std::size_t attribute,
                 std::optional<index_t> entry = {}) {
  Postcondition(first.size() == second.size(), attribute, entry);
  auto check = [&](std::size_t byte, bool) -> std::optional<Diagnostic> {
    if (first[byte] != second[byte])
      return Failure(Stage::kVerification, Code::kPostcondition, {}, attribute,
                     entry);
    return {};
  };
  if (auto failed = session.Scan(first.size(), sizeof(char), check,
                                 session.Telemetry().verification))
    Reject(Status::kVerificationFailure, *failed);
}
void VerifyAttributes(Session& session, const MeshReadView& source,
                      const MeshReadView& candidate) {
  const auto source_faces = session.State().selection->SourceFaces();
  const auto source_corners = session.State().selection->SourceCorners();
  Postcondition(source.attributes.size() == candidate.attributes.size());
  for (std::size_t a = 0; a < source.attributes.size(); ++a) {
    session.Checkpoint(a);
    const auto& first = source.attributes[a];
    const auto& second = candidate.attributes[a];
    Postcondition(first.domain == second.domain &&
                      first.set_index == second.set_index &&
                      first.components == second.components &&
                      first.values.index() == second.values.index() &&
                      first.offsets.has_value() == second.offsets.has_value() &&
                      first.present.has_value() == second.present.has_value() &&
                      first.metadata.size() == second.metadata.size(),
                  a);
    VerifyBytes(session, first.name, second.name, a);
    VerifyBytes(session, first.semantic, second.semantic, a);
    for (std::size_t entry = 0; entry < first.metadata.size(); ++entry) {
      session.Checkpoint(entry);
      VerifyBytes(session, first.metadata[entry].key,
                  second.metadata[entry].key, a, entry);
      VerifyBytes(session, first.metadata[entry].value,
                  second.metadata[entry].value, a, entry);
    }
    auto source_row = [&](std::size_t row) {
      if (first.domain == AttributeDomain::face)
        return static_cast<std::size_t>(source_faces[row]);
      if (first.domain == AttributeDomain::corner)
        return static_cast<std::size_t>(source_corners[row]);
      return row;
    };
    auto rows = [&](std::size_t row, bool) -> std::optional<Diagnostic> {
      const auto original = source_row(row);
      const auto left = Row(first, original);
      const auto right = Row(second, row);
      if (left.second - left.first != right.second - right.first ||
          (first.present &&
           (*first.present)[original] != (*second.present)[row]))
        return Failure(Stage::kVerification, Code::kPostcondition, {}, a, row);
      return {};
    };
    if (auto failed =
            session.Scan(Rows(candidate, second.domain), sizeof(index_t), rows,
                         session.Telemetry().verification))
      Reject(Status::kVerificationFailure, *failed);
    std::visit(
        [&](const auto& values) {
          using Span = std::decay_t<decltype(values)>;
          using Scalar = typename Span::value_type;
          const auto& original = std::get<Span>(first.values);
          if (first.domain == AttributeDomain::vertex)
            Postcondition(original.data() == values.data(), a);
          auto scalars = [&](std::size_t scalar,
                             bool) -> std::optional<Diagnostic> {
            std::size_t row;
            if (second.offsets) {
              const auto found = std::upper_bound(
                  second.offsets->begin(), second.offsets->end(), scalar);
              row =
                  static_cast<std::size_t>(found - second.offsets->begin() - 1);
            } else {
              row = scalar / second.components;
            }
            const auto before = Row(first, source_row(row));
            const auto after = Row(second, row);
            if (!ScalarEqual(original[before.first + scalar - after.first],
                             values[scalar], true))
              return Failure(Stage::kVerification, Code::kPostcondition, {}, a,
                             row);
            return {};
          };
          if (auto failed = session.Scan(values.size(), sizeof(Scalar), scalars,
                                         session.Telemetry().verification))
            Reject(Status::kVerificationFailure, *failed);
        },
        second.values);
  }
}
void Verify(Session& session, const MeshReadView& source,
            std::span<const DuplicateTarget> targets) {
  const auto candidate = session.State().selection->Candidate().View();
  const auto source_faces = session.State().selection->SourceFaces();
  const auto source_corners = session.State().selection->SourceCorners();
  const auto& face_map = session.State().face_map;
  const auto& corner_map = session.State().corner_map;
  Postcondition(source.positions.size() == candidate.positions.size() &&
                source.positions.data() == candidate.positions.data() &&
                source_faces.size() + targets.size() ==
                    source.face_offsets.size() - 1 &&
                source_faces.size() == candidate.face_offsets.size() - 1 &&
                source_corners.size() == candidate.corner_vertices.size() &&
                face_map.size() == source.face_offsets.size() - 1 &&
                corner_map.size() == source.corner_vertices.size());
  auto face_sources = [&](std::size_t face, bool) -> std::optional<Diagnostic> {
    if (source_faces[face] >= source.face_offsets.size() - 1)
      return Failure(Stage::kVerification, Code::kPostcondition, {}, {}, face);
    return {};
  };
  if (auto failed =
          session.Scan(source_faces.size(), sizeof(index_t), face_sources,
                       session.Telemetry().verification))
    Reject(Status::kVerificationFailure, *failed);
  auto corner_sources = [&](std::size_t corner,
                            bool) -> std::optional<Diagnostic> {
    if (source_corners[corner] >= source.corner_vertices.size())
      return Failure(Stage::kVerification, Code::kPostcondition, {}, {},
                     corner);
    return {};
  };
  if (auto failed =
          session.Scan(source_corners.size(), sizeof(index_t), corner_sources,
                       session.Telemetry().verification))
    Reject(Status::kVerificationFailure, *failed);
  auto positions = [&](std::size_t scalar, bool) -> std::optional<Diagnostic> {
    if (!ScalarEqual(source.positions[scalar], candidate.positions[scalar],
                     true))
      return Failure(Stage::kVerification, Code::kPostcondition, {}, {},
                     scalar / geometry::kVector3Components);
    return {};
  };
  if (auto failed = session.Scan(source.positions.size(), sizeof(double),
                                 positions, session.Telemetry().verification))
    Reject(Status::kVerificationFailure, *failed);
  VerifyAttributes(session, source, candidate);
  CheckFinite(session, candidate, Stage::kVerification,
              session.Telemetry().verification);
  // This separately owned ordinal table is rebuilt from borrowed original
  // request records, never from construction's representative/rotation plan.
  detail::Vector<index_t> ordered_targets{
      detail::Allocator<index_t>(session.State().allocation)};
  ordered_targets.reserve(targets.size());
  for (std::size_t i = 0; i < targets.size(); ++i) {
    session.Checkpoint(i);
    ordered_targets.push_back(i);
  }
  std::sort(ordered_targets.begin(), ordered_targets.end(),
            [&](index_t first, index_t second) {
              session.Checkpoint();
              return targets[first].remove < targets[second].remove;
            });
  struct VerificationFace {
    std::size_t output = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t rotation = 0;
    bool retained = false;
  };
  detail::Vector<VerificationFace> faces{
      detail::Allocator<VerificationFace>(session.State().allocation)};
  Initialize(session, faces, source.face_offsets.size() - 1);
  std::size_t rotations = 0;
  std::size_t readable_bytes = 0;
  // Ordered fixed-width planning certifies original representative identities
  // and assigns independent rotation ranges. Payload comparisons launch below.
  for (std::size_t face = 0; face < faces.size(); ++face) {
    session.Checkpoint(face);
    const auto target =
        std::lower_bound(ordered_targets.begin(), ordered_targets.end(), face,
                         [&](index_t ordinal, std::size_t index) {
                           return targets[ordinal].remove < index;
                         });
    const auto representative =
        target != ordered_targets.end() && targets[*target].remove == face
            ? static_cast<std::size_t>(targets[*target].keep)
            : face;
    const auto preceding = std::lower_bound(
        ordered_targets.begin(), ordered_targets.end(), representative,
        [&](index_t ordinal, std::size_t index) {
          return targets[ordinal].remove < index;
        });
    const auto out = representative - static_cast<std::size_t>(
                                          preceding - ordered_targets.begin());
    if (out >= source_faces.size() || source_faces[out] != representative ||
        face_map[face] != out)
      Postcondition(false, {}, face);
    const auto start = static_cast<std::size_t>(source.face_offsets[face]);
    const auto count =
        static_cast<std::size_t>(source.face_offsets[face + 1]) - start;
    const auto destination =
        static_cast<std::size_t>(candidate.face_offsets[out]);
    if (count != candidate.face_offsets[out + 1] - destination)
      Postcondition(false, {}, face);
    const auto diagnostic =
        Failure(Stage::kVerification, Code::kPostcondition, {}, {}, face);
    if (!FaceRowsEqual(session, candidate, out, source, face, diagnostic,
                       session.Telemetry().directed_verification))
      Reject(Status::kVerificationFailure, diagnostic);
    const auto rotation_count = count;
    faces[face].output = out;
    faces[face].retained = representative == face;
    faces[face].begin = rotations;
    rotations = detail::CheckedSum(rotations, rotation_count);
    faces[face].end = rotations;
    auto bytes = detail::CheckedProduct(count, sizeof(index_t));
    for (std::size_t a = 0; a < candidate.attributes.size(); ++a) {
      session.Checkpoint(a);
      const auto& attribute = candidate.attributes[a];
      if (attribute.domain != AttributeDomain::corner) continue;
      std::size_t scalars = 0;
      for (std::size_t i = 0; i < count; ++i) {
        session.Checkpoint(i);
        if (Authored(attribute, destination + i)) {
          const auto row = Row(attribute, destination + i);
          scalars = detail::CheckedSum(scalars, row.second - row.first);
        }
      }
      bytes = detail::CheckedSum(
          bytes, std::visit(
                     [&](const auto& values) {
                       using Scalar =
                           typename std::decay_t<decltype(values)>::value_type;
                       return detail::CheckedProduct(scalars, sizeof(Scalar));
                     },
                     attribute.values));
    }
    readable_bytes = detail::CheckedSum(
        readable_bytes, detail::CheckedProduct(rotation_count, bytes));
  }
  detail::Vector<std::uint8_t> matches{
      detail::Allocator<std::uint8_t>(session.State().allocation)};
  matches.reserve(rotations);
  for (std::size_t i = 0; i < rotations; ++i) {
    session.Checkpoint(i);
    matches.push_back(kRotationUnexamined);
  }
  const auto width = rotations == 0
                         ? 1
                         : readable_bytes / rotations +
                               (readable_bytes % rotations != 0 ? 1 : 0);
  auto compare = [&](std::size_t ordinal) {
    const auto found =
        std::upper_bound(faces.begin(), faces.end(), ordinal,
                         [](std::size_t index, const VerificationFace& face) {
                           return index < face.end;
                         });
    const auto face = static_cast<std::size_t>(found - faces.begin());
    matches[ordinal] = RotationEqual(session, candidate, found->output, source,
                                     face, ordinal - found->begin, false, false)
                           ? kRotationEqual
                           : kRotationDifferent;
  };
  try {
    session.Parallel(rotations, width, compare,
                     session.Telemetry().directed_verification);
    for (std::size_t face = 0; face < faces.size(); ++face) {
      session.Checkpoint(face);
      std::optional<std::size_t> chosen;
      for (auto i = faces[face].begin; i < faces[face].end; ++i) {
        session.Checkpoint(i - faces[face].begin);
        if (matches[i] == kRotationEqual) {
          chosen = i - faces[face].begin;
          break;
        }
      }
      Postcondition(chosen.has_value(), {}, face);
      Postcondition(!faces[face].retained || *chosen == 0, {}, face);
      faces[face].rotation = *chosen;
    }
  } catch (const detail::AdmissionFailure& failure) {
    if (failure.Code() != detail::AdmissionCode::kCanceled) throw;
    std::optional<std::size_t> known;
    for (std::size_t face = 0; face < faces.size(); ++face) {
      bool failed = faces[face].retained &&
                    matches[faces[face].begin] == kRotationDifferent;
      if (!failed) {
        failed = true;
        for (auto i = faces[face].begin; i < faces[face].end; ++i)
          if (matches[i] != kRotationDifferent) {
            failed = false;
            break;
          }
      }
      if (failed) {
        known = face;
        break;
      }
    }
    if (!known) throw;
    // All workers are joined. Finalize only the necessary ordered prefix,
    // without allocation, cancellation or discovery observer side effects.
    for (std::size_t face = 0; face <= *known; ++face) {
      bool equal = false;
      const auto count =
          faces[face].retained ? 1 : faces[face].end - faces[face].begin;
      for (std::size_t rotation = 0; rotation < count; ++rotation)
        if (RotationEqual(session, candidate, faces[face].output, source, face,
                          rotation, true)) {
          equal = true;
          break;
        }
      Postcondition(equal, {}, face);
    }
    throw;
  }
  auto original_corners = [&](std::size_t corner,
                              bool) -> std::optional<Diagnostic> {
    const auto found = std::upper_bound(source.face_offsets.begin(),
                                        source.face_offsets.end(), corner);
    const auto face =
        static_cast<std::size_t>(found - source.face_offsets.begin() - 1);
    const auto start = static_cast<std::size_t>(source.face_offsets[face]);
    const auto count =
        static_cast<std::size_t>(source.face_offsets[face + 1]) - start;
    const auto destination = candidate.face_offsets[faces[face].output];
    if (corner_map[corner] !=
        destination + Rotated(corner - start, faces[face].rotation, count))
      return Failure(Stage::kVerification, Code::kPostcondition, {}, {},
                     corner);
    return {};
  };
  if (auto failed = session.Scan(source.corner_vertices.size(), sizeof(index_t),
                                 original_corners,
                                 session.Telemetry().directed_verification))
    Reject(Status::kVerificationFailure, *failed);
  auto retained_corners = [&](std::size_t corner,
                              bool) -> std::optional<Diagnostic> {
    const auto original = source_corners[corner];
    if (original >= source.corner_vertices.size() ||
        source.corner_vertices[original] != candidate.corner_vertices[corner] ||
        corner_map[original] != corner)
      return Failure(Stage::kVerification, Code::kPostcondition, {}, {},
                     corner);
    return {};
  };
  if (auto failed =
          session.Scan(source_corners.size(), sizeof(index_t), retained_corners,
                       session.Telemetry().directed_verification))
    Reject(Status::kVerificationFailure, *failed);
}
MaterializedDuplicateResult Execute(const geometry::MaterializedMesh& source,
                                    std::span<const DuplicateTarget> targets,
                                    const MaterializedDuplicateOptions& options,
                                    const MaterializedDuplicateLimits& limits,
                                    const MeshMaterializationWorkPolicy& work,
                                    std::stop_token stop,
                                    const detail::TestHooks& hooks) {
  MaterializedDuplicateResult result;
  Stage current = Stage::kConfiguration;
  try {
    Configure(limits, work);
    if (targets.size() > limits.max_targets)
      Reject(Status::kLimitExceeded, Failure(current, Code::kTargetLimit));
    if (options.row_local_attributes.size() > limits.max_row_local_declarations)
      Reject(Status::kLimitExceeded, Failure(current, Code::kDeclarationLimit));
    std::size_t declaration_bytes = 0;
    for (std::size_t i = 0; i < options.row_local_attributes.size(); ++i) {
      if (i % work.cancellation_block_scalars == 0 && stop.stop_requested())
        throw detail::AdmissionFailure(detail::AdmissionCode::kCanceled);
      const auto bytes = options.row_local_attributes[i].name.size();
      if (bytes > limits.max_declaration_name_bytes - declaration_bytes)
        Reject(Status::kLimitExceeded,
               Failure(current, Code::kDeclarationNameByteLimit));
      declaration_bytes += bytes;
    }
    const auto context = source.ShareExecutionContext();
    auto allocation = detail::AllocationContext(context, stop, hooks.allocation)
                          .WithRequestLimit(limits.max_new_requested_bytes);
    auto state = std::allocate_shared<detail::RepairState>(
        detail::Allocator<detail::RepairState>(allocation), allocation, source);
    Session session(*state, context, work, stop, hooks, result.execution);
    Declarations(session, options.row_local_attributes);
    current = Stage::kSourceInspection;
    Inspection(session, false, limits.inspection, stop);
    const auto view = source.View();
    current = Stage::kEligibility;
    CheckFinite(session, view, current, result.execution.eligibility);
    if (!targets.empty())
      CheckSemantics(session, view, options.row_local_attributes);
    current = Stage::kTargets;
    detail::Vector<FacePlan> faces{detail::Allocator<FacePlan>(allocation)};
    Targets(session, view, targets, faces);
    current = Stage::kComparison;
    CompareTargets(session, view, targets, limits, faces);
    detail::Vector<index_t> kept{detail::Allocator<index_t>(allocation)};
    kept.reserve(faces.size() - targets.size());
    for (std::size_t face = 0; face < faces.size(); ++face) {
      session.Checkpoint(face);
      if (!faces[face].representative) kept.push_back(face);
    }
    Initialize(session, state->face_map, faces.size());
    Initialize(session, state->corner_map, view.corner_vertices.size());
    current = Stage::kSelection;
    Select(session, kept, limits.selection, stop);
    current = Stage::kCorrespondence;
    Correspondence(session, view, faces, kept, targets);
    current = Stage::kCandidateInspection;
    Inspection(session, true, limits.inspection, stop);
    current = Stage::kVerification;
    Verify(session, view, targets);
    state->outcome = targets.empty() ? CandidateOutcome::unchanged
                                     : CandidateOutcome::accepted;
    current = Stage::kPublication;
    session.Checkpoint();
    result.status = Status::kSuccess;
    result.repair = detail::Access::Make(std::move(state));
  } catch (const ValidationFailure& failure) {
    result.status = failure.StatusValue();
    result.diagnostic = failure.DiagnosticValue();
  } catch (const detail::AdmissionFailure& failure) {
    switch (failure.Code()) {
      case detail::AdmissionCode::kCanceled:
        result.status = Status::kCanceled;
        result.diagnostic = Failure(current, Code::kStopRequested);
        break;
      case detail::AdmissionCode::kBudgetExceeded:
        result.status = Status::kBudgetExceeded;
        result.diagnostic = Failure(current, Code::kRequestBudget);
        break;
      case detail::AdmissionCode::kSizeLimit:
        result.status = Status::kLimitExceeded;
        result.diagnostic = Failure(current, Code::kRequestBudget);
        break;
      case detail::AdmissionCode::kInvalidObject:
        throw;
    }
  }
  return result;
}
}  // namespace
MaterializedDuplicateResult RemoveMaterializedDuplicateFaces(
    const geometry::MaterializedMesh& source,
    std::span<const DuplicateTarget> targets,
    const MaterializedDuplicateOptions& options,
    const MaterializedDuplicateLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop) {
  return Execute(source, targets, options, limits, work, stop, {});
}
namespace materialized_duplicates_detail {
MaterializedDuplicateResult TestRemoveMaterializedDuplicateFaces(
    const geometry::MaterializedMesh& source,
    std::span<const DuplicateTarget> targets,
    const MaterializedDuplicateOptions& options,
    const MaterializedDuplicateLimits& limits,
    const geometry::MeshMaterializationWorkPolicy& work, std::stop_token stop,
    const TestHooks& hooks) {
  return Execute(source, targets, options, limits, work, stop, hooks);
}
}  // namespace materialized_duplicates_detail
}  // namespace meshvale::repair
