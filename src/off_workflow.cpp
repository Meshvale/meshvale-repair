// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/off_workflow.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_file_sha256_internal.h"
#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"
#include "off_workflow_internal.h"
#include "off_workflow_test_internal.h"

namespace meshvale::repair::off_workflow_detail {
namespace geo = geometry;
namespace ic = interchange;
using Stage = OffWorkflowStage;
using Status = OffWorkflowStatus;
using Code = OffWorkflowDiagnosticCode;
using Check = OffWorkflowCheck;
using Coverage = OffWorkflowCoverageState;
using Publication = OffWorkflowPublicationOutcome;

[[noreturn]] void Fail(Status status, Stage stage, Code code,
                       std::optional<geo::index_t> element) {
  OffWorkflowDiagnostic diagnostic;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.element = element;
  throw Failure{status, diagnostic};
}
State::State(const allocation::AllocationContext& allocator,
             const ic::OffFileAsset& input)
    : allocation(allocator),
      source(input),
      input_id(allocation::Allocator<char>(allocator)),
      receipt_name(allocation::Allocator<char>(allocator)) {
  for (std::size_t i = 0; i < coverage.size(); ++i)
    coverage[i].check = static_cast<Check>(i);
  auto& solid = coverage[static_cast<std::size_t>(Check::kSolidValidity)];
  solid.state = Coverage::kUnsupported;
  solid.reason = Code::kSolidUnavailable;
}
OffWorkflowRequestedBytes State::Bytes() const noexcept {
  OffWorkflowRequestedBytes bytes;
  bytes.wrapper = allocation.OperationActiveBytes();
  bytes.source_files = source.NewRequestedBytes();
  bytes.source_off = source.Document().NewRequestedBytes();
  if (repair) bytes.repair = repair->NewRequestedBytes();
  if (written) bytes.writing = written->NewRequestedBytes();
  if (reloaded) bytes.reload = reloaded->NewRequestedBytes();
  if (published) {
    bytes.publication_files = published->NewRequestedBytes();
    bytes.publication_off = published->Document().NewRequestedBytes();
  }
  return bytes;
}
void Session::Check(Stage stage, std::uint64_t ordinal) const {
  if (testing.checkpoint) testing.checkpoint(stage, ordinal, testing.state);
  state.allocation.Checkpoint();
}
void Passed(State& state, Check check) noexcept {
  auto& coverage = state.coverage[static_cast<std::size_t>(check)];
  coverage.state = Coverage::kPerformed;
  coverage.passed = true;
  coverage.reason = Code::kNone;
}
void Failed(State& state, Check check, Code reason) noexcept {
  auto& coverage = state.coverage[static_cast<std::size_t>(check)];
  coverage.state = Coverage::kPerformed;
  coverage.passed = false;
  coverage.reason = reason;
}
void Compute(State& state) noexcept {
  auto& outcomes = state.outcomes;
  const auto count = outcomes.publication == Publication::kNotRequested
                         ? kMemoryChecks
                         : kPublicationChecks;
  bool failed = false, incomplete = false;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& coverage = state.coverage[i];
    failed |=
        coverage.state == Coverage::kPerformed && coverage.passed == false;
    incomplete |= coverage.state != Coverage::kPerformed;
  }
  outcomes.profile = failed       ? OffWorkflowProfileOutcome::kFailed
                     : incomplete ? OffWorkflowProfileOutcome::kIncomplete
                                  : OffWorkflowProfileOutcome::kPassed;
  outcomes.execution =
      state.status == Status::kCanceled ? OffWorkflowExecutionOutcome::kCanceled
      : state.status == Status::kSuccess || state.status == Status::kRejected
          ? OffWorkflowExecutionOutcome::kCompleted
          : OffWorkflowExecutionOutcome::kFailed;
  outcomes.exit = outcomes.execution == OffWorkflowExecutionOutcome::kCanceled
                      ? OffWorkflowExitMeaning::kCanceled
                  : outcomes.execution == OffWorkflowExecutionOutcome::kFailed
                      ? OffWorkflowExitMeaning::kExecutionFailure
                  : !outcomes.candidate ||
                          outcomes.candidate == CandidateOutcome::rejected ||
                          outcomes.profile != OffWorkflowProfileOutcome::kPassed
                      ? OffWorkflowExitMeaning::kRejectedOrIncomplete
                      : OffWorkflowExitMeaning::kSuccess;
}
namespace {
constexpr std::uint64_t kAsciiEnd = 0x80;
constexpr std::uint64_t kControlEnd = 0x20;
constexpr std::uint64_t kDelete = 0x7f;
constexpr std::uint32_t kUnicodeEnd = 0x10ffff;
constexpr std::uint32_t kSurrogateFirst = 0xd800, kSurrogateLast = 0xdfff;
constexpr std::uint32_t kLatinControlEnd = 0xa0;
constexpr std::size_t kNumberedDeviceLength = 4;
constexpr std::string_view kProhibitedName = "/\\:<>\"|?*";
std::size_t Host(std::uint64_t value) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  return static_cast<std::size_t>(value);
}
void Positive(std::uint64_t value) {
  if (!value)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRequiredFieldZero);
  (void)Host(value);
}
std::uint64_t Add(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  (void)Host(a + b);
  return a + b;
}
void Count(std::uint64_t count, std::uint64_t cap,
           Stage stage = Stage::kConfiguration) {
  if (count > cap) Fail(Status::kLimitExceeded, stage, Code::kCountLimit);
}
void Validate(const geo::MeshInspectionLimits& limits) {
  Positive(limits.max_issues);
  Positive(limits.max_new_requested_bytes);
}
void Validate(const geo::MeshMaterializationLimits& limits) {
  for (auto value :
       {limits.max_vertices, limits.max_faces, limits.max_corners,
        limits.max_face_offsets, limits.max_attributes,
        limits.max_attribute_scalars, limits.max_attribute_offsets,
        limits.max_presence_bytes, limits.max_metadata_entries,
        limits.max_descriptor_bytes, limits.max_new_requested_bytes})
    Positive(value);
}
void Validate(const geo::MeshMaterializationWorkPolicy& work) {
  for (auto value : {work.parallel_min_bytes, work.partition_bytes,
                     work.cancellation_block_scalars, work.max_jobs})
    Positive(value);
}
void Validate(const OffByteWorkPolicy& work) {
  for (auto value : {work.parallel_min_bytes, work.partition_bytes,
                     work.cancellation_block_bytes, work.max_jobs})
    Positive(value);
}
void Validate(const ic::OffWorkPolicy& work) {
  Positive(work.cancellation_block_bytes);
  Positive(work.cancellation_block_tokens);
  Positive(work.cancellation_block_records);
  Validate(work.geometry);
}
void Validate(const ic::OffReadLimits& limits) {
  for (auto value :
       {limits.max_source_bytes, limits.max_vertices, limits.max_faces,
        limits.max_corners, limits.max_corners_per_face, limits.max_line_bytes,
        limits.max_token_bytes, limits.max_comment_bytes,
        limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.materialization);
  Validate(limits.inspection);
  if (Add(Add(limits.max_codec_new_requested_bytes,
              limits.materialization.max_new_requested_bytes),
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const ic::OffWriteLimits& limits) {
  for (auto value :
       {limits.max_output_bytes, limits.max_vertices, limits.max_faces,
        limits.max_corners, limits.max_corners_per_face, limits.max_line_bytes,
        limits.max_token_bytes, limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.inspection);
  if (Add(limits.max_codec_new_requested_bytes,
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const ic::OffFilePublicationLimits& limits,
              const ic::OffFilePublicationWorkPolicy& work) {
  const auto& f = limits.files;
  for (auto value : {f.max_entry_bytes, f.max_receipt_bytes,
                     f.max_total_file_bytes, f.max_entry_name_bytes,
                     f.max_receipt_name_bytes, f.max_native_path_code_units,
                     f.max_file_new_requested_bytes, work.files.io_block_bytes,
                     work.files.parallel_min_bytes, work.files.partition_bytes,
                     work.files.cancellation_block_bytes, work.files.max_jobs})
    Positive(value);
  (void)Add(f.max_entry_bytes, f.max_receipt_bytes);
  if (f.max_native_path_code_units >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          sizeof(std::filesystem::path::value_type))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  Validate(limits.expected_inspection);
  Validate(limits.reload);
  Validate(work.expected_inspection);
  Validate(work.reload);
}
void Stop(std::stop_token stop) {
  if (stop.stop_requested())
    Fail(Status::kCanceled, Stage::kConfiguration, Code::kStopRequested);
}
char Fold(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool FoldEqual(std::string_view a, std::string_view b, std::stop_token stop) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    Stop(stop);
    if (Fold(a[i]) != Fold(b[i])) return false;
  }
  return true;
}
void Name(std::string_view name, std::uint64_t cap, std::stop_token stop) {
  Count(name.size(), cap);
  const auto refuse = [] {
    Fail(Status::kInvalidRequest, Stage::kConfiguration,
         Code::kNameUnsupported);
  };
  if (name.empty() || name == "." || name == ".." || name.back() == '.' ||
      name.back() == ' ')
    refuse();
  std::size_t stem_end = name.size();
  for (std::size_t i = 0; i < name.size();) {
    Stop(stop);
    const auto lead = static_cast<unsigned char>(name[i++]);
    if (lead < kAsciiEnd) {
      if (lead < kControlEnd || lead == kDelete ||
          kProhibitedName.find(static_cast<char>(lead)) != kProhibitedName.npos)
        refuse();
      if (lead == '.' && stem_end == name.size()) stem_end = i - 1;
      continue;
    }
    constexpr unsigned char kTailTag = 0x80, kTailMask = 0xc0,
                            kTailPayload = 0x3f;
    constexpr unsigned kTailBits = 6;
    std::size_t tails = 0;
    std::uint32_t scalar = 0, minimum = 0;
    struct Utf8Lead {
      unsigned char first, last, mask;
      std::size_t tails;
      std::uint32_t minimum;
    };
    constexpr std::array kUtf8Leads{Utf8Lead{0xc2, 0xdf, 0x1f, 1, 0x80},
                                    Utf8Lead{0xe0, 0xef, 0x0f, 2, 0x800},
                                    Utf8Lead{0xf0, 0xf4, 0x07, 3, 0x10000}};
    bool found = false;
    for (const auto& range : kUtf8Leads) {
      if (lead >= range.first && lead <= range.last) {
        tails = range.tails;
        scalar = lead & range.mask;
        minimum = range.minimum;
        found = true;
        break;
      }
    }
    if (!found) refuse();
    if (tails > name.size() - i) refuse();
    for (std::size_t t = 0; t < tails; ++t) {
      const auto c = static_cast<unsigned char>(name[i++]);
      if ((c & kTailMask) != kTailTag) refuse();
      scalar = (scalar << kTailBits) | (c & kTailPayload);
    }
    if (scalar < minimum || scalar > kUnicodeEnd ||
        (scalar >= kSurrogateFirst && scalar <= kSurrogateLast) ||
        scalar < kLatinControlEnd)
      refuse();
  }
  const auto stem = name.substr(0, stem_end);
  for (const auto device : {"con", "prn", "aux", "nul"})
    if (FoldEqual(stem, device, stop)) refuse();
  if (stem.size() == kNumberedDeviceLength &&
      (FoldEqual(stem.substr(0, kNumberedDeviceLength - 1), "com", stop) ||
       FoldEqual(stem.substr(0, kNumberedDeviceLength - 1), "lpt", stop)) &&
      stem.back() >= '1' && stem.back() <= '9')
    refuse();
}
void Validate(const OffDuplicateFileWorkflowRequest& request,
              const OffDuplicateFileWorkflowLimits& limits,
              const OffDuplicateFileWorkflowWorkPolicy& work,
              std::stop_token stop) {
  for (auto value :
       {limits.max_wrapper_new_requested_bytes, limits.max_input_id_bytes,
        limits.max_source_protection_bytes, limits.max_report_bytes,
        limits.max_hash_input_bytes, limits.max_verification_vertices,
        limits.max_verification_faces, limits.max_verification_corners,
        limits.repair.max_targets, limits.repair.max_row_local_declarations,
        limits.repair.max_declaration_name_bytes,
        limits.repair.max_comparison_steps_per_target,
        limits.repair.max_new_requested_bytes})
    Positive(value);
  Validate(limits.repair.inspection);
  Validate(limits.repair.selection);
  Validate(work.repair);
  Validate(limits.writing);
  Validate(work.writing);
  Validate(limits.reload);
  Validate(work.reload);
  Validate(limits.publication, work.publication);
  Validate(work.verification);
  Validate(work.hashing);
  Validate(work.report);
  (void)Add(limits.max_report_bytes, kReportStatsTailBytes);
  Count(request.input_id.size(),
        std::min<std::uint64_t>(limits.max_input_id_bytes, kSchemaIdBytes));
  if (request.input_id.empty())
    Fail(Status::kInvalidRequest, Stage::kConfiguration, Code::kInputIdentity);
  for (std::size_t i = 0; i < request.input_id.size(); ++i) {
    const auto c = request.input_id[i];
    const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9');
    if (!alpha && (i == 0 || (c != '_' && c != '.' && c != ':' && c != '-')))
      Fail(Status::kInvalidRequest, Stage::kConfiguration,
           Code::kInputIdentity);
  }
  if (request.text != ic::OffTextPolicy::kCanonicalAscii ||
      request.edge_count != ic::OffEdgeCountPolicy::kUnknownZero)
    Fail(Status::kInvalidRequest, Stage::kConfiguration, Code::kPolicy);
  Count(request.targets.size(), limits.repair.max_targets);
  (void)allocation::CheckedProduct(request.targets.size(),
                                   sizeof(DuplicateTarget));
  Count(request.source.EntryBytes().size(), limits.max_source_protection_bytes);
  const auto view = request.source.Document().Mesh().View();
  Count(view.positions.size() / kPositionWidth,
        limits.max_verification_vertices);
  Count(view.face_offsets.size() - 1, limits.max_verification_faces);
  Count(view.corner_vertices.size(), limits.max_verification_corners);
  if (request.publication) {
    Count(request.publication->destination.native().size(),
          limits.publication.files.max_native_path_code_units - 1);
    Name(request.publication->receipt_basename,
         limits.publication.files.max_receipt_name_bytes, stop);
    if (FoldEqual(request.source.EntryPath(),
                  request.publication->receipt_basename, stop))
      Fail(Status::kInvalidRequest, Stage::kConfiguration,
           Code::kNameCollision);
  }
}
Status Admission(const allocation::AdmissionFailure& failure) noexcept {
  switch (failure.Code()) {
    case allocation::AdmissionCode::kSizeLimit:
      return Status::kLimitExceeded;
    case allocation::AdmissionCode::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case allocation::AdmissionCode::kCanceled:
      return Status::kCanceled;
    case allocation::AdmissionCode::kInvalidObject:
      return Status::kInvalidRequest;
  }
  std::terminate();
}
void Latch(State& state, const Failure& failure) noexcept {
  state.status = failure.status;
  state.diagnostic = failure.diagnostic;
  state.outcomes.stage = failure.diagnostic.stage;
}
struct ByteCopy {
  Session& session;
  Stage stage;
  std::span<const std::byte> source;
  std::span<std::byte> destination;
  std::size_t block;
};
void Copy(void* opaque, std::size_t begin, std::size_t end) {
  auto& copy = *static_cast<ByteCopy*>(opaque);
  while (begin < end) {
    copy.session.Check(copy.stage, begin);
    const auto count = std::min(copy.block, end - begin);
    std::copy_n(copy.source.data() + begin, count,
                copy.destination.data() + begin);
    begin += count;
  }
}
struct ByteCompare {
  Session& session;
  Stage stage;
  Code code;
  std::span<const std::byte> first, second;
  std::size_t block;
};
void Compare(void* opaque, std::size_t begin, std::size_t end) {
  auto& compare = *static_cast<ByteCompare*>(opaque);
  for (auto i = begin; i < end; ++i) {
    if ((i - begin) % compare.block == 0)
      compare.session.Check(compare.stage, i);
    if (compare.first[i] != compare.second[i])
      Fail(Status::kVerificationFailure, compare.stage, compare.code, i);
  }
}
geo::MeshMaterializationWorkPolicy BytePolicy(const OffByteWorkPolicy& work) {
  return {work.parallel_min_bytes, work.partition_bytes,
          work.cancellation_block_bytes, work.max_jobs};
}
void CompareBytes(Session& session, std::span<const std::byte> first,
                  std::span<const std::byte> second, Stage stage, Code code,
                  geo::MaterializationExecutionStats& stats) {
  if (first.size() != second.size())
    Fail(Status::kVerificationFailure, stage, code);
  ByteCompare compare{
      session, stage,
      code,    first,
      second,  Host(session.work.verification.cancellation_block_bytes)};
  allocation::RunRange(session.context, session.state.allocation,
                       BytePolicy(session.work.verification), first.size(),
                       sizeof(std::byte), Compare, &compare, session.stop,
                       stats, session.testing.verification_worker_backend,
                       &stats);
  session.Check(stage, first.size());
}
}  // namespace
void CopyBytes(Session& session, std::span<const std::byte> source,
               std::span<std::byte> destination, Stage stage,
               const OffByteWorkPolicy& work,
               geo::MaterializationExecutionStats& stats,
               const allocation::NativeWorkerBackend& backend) {
  if (source.size() != destination.size())
    Fail(Status::kVerificationFailure, stage, Code::kCountLimit);
  ByteCopy copy{session, stage, source, destination,
                Host(work.cancellation_block_bytes)};
  allocation::RunRange(session.context, session.state.allocation,
                       BytePolicy(work), source.size(), sizeof(std::byte), Copy,
                       &copy, session.stop, stats, backend, &stats);
  session.Check(stage, source.size());
}
namespace {
struct Correspondence {
  Session& session;
  const MaterializedDuplicateRepair& repair;
  std::span<const DuplicateTarget> targets;
  std::size_t index_block;
  std::size_t target_block;
};
std::size_t ElementBlock(std::uint64_t byte_cap, std::size_t element_bytes) {
  // One complete payload element is the minimum granularity. Fixed-size
  // metadata and the bounded binary lookup remain part of that element's work.
  return std::max(std::size_t{1}, Host(byte_cap) / element_bytes);
}
void CorrespondenceCheck(Correspondence& task, VerificationPart part,
                         std::size_t ordinal) {
  if (task.session.testing.verification_checkpoint)
    task.session.testing.verification_checkpoint(part, ordinal,
                                                 task.session.testing.state);
  task.session.Check(Stage::kVerify, ordinal);
}
void CorrespondenceFailure(geo::index_t element) {
  Fail(Status::kVerificationFailure, Stage::kVerify, Code::kCorrespondence,
       element);
}
void SourceFaces(void* opaque, std::size_t begin, std::size_t end) {
  auto& task = *static_cast<Correspondence*>(opaque);
  const auto source = task.repair.Source().View(),
             candidate = task.repair.Candidate().View();
  const auto faces = task.repair.FaceMap(), corners = task.repair.CornerMap();
  for (auto face = begin; face < end; ++face) {
    CorrespondenceCheck(task, VerificationPart::kSourceFaces, face);
    const auto mapped = faces[face];
    if (mapped >= candidate.face_offsets.size() - 1)
      CorrespondenceFailure(face);
    const auto a = Host(source.face_offsets[face]),
               b = Host(source.face_offsets[face + 1]);
    const auto c = Host(candidate.face_offsets[Host(mapped)]),
               d = Host(candidate.face_offsets[Host(mapped) + 1]);
    if (b - a != d - c || b == a) CorrespondenceFailure(face);
    for (auto corner = a; corner < b; ++corner) {
      if ((corner - a) % task.index_block == 0)
        CorrespondenceCheck(task, VerificationPart::kSourceCorners, corner);
      const auto mapped_corner = corners[corner];
      if (mapped_corner < c || mapped_corner >= d ||
          source.corner_vertices[corner] !=
              candidate.corner_vertices[Host(mapped_corner)])
        CorrespondenceFailure(corner);
      const auto next = corner + 1 == b ? a : corner + 1;
      const auto expected =
          Host(mapped_corner) + 1 == d ? c : Host(mapped_corner) + 1;
      if (corners[next] != expected) CorrespondenceFailure(corner);
    }
  }
}
void RetainedFaces(void* opaque, std::size_t begin, std::size_t end) {
  auto& task = *static_cast<Correspondence*>(opaque);
  const auto source = task.repair.Source().View(),
             candidate = task.repair.Candidate().View();
  const auto inverse = task.repair.SourceFaces();
  for (auto face = begin; face < end; ++face) {
    if ((face - begin) % task.index_block == 0)
      CorrespondenceCheck(task, VerificationPart::kRetainedFaces, face);
    const auto original = inverse[face];
    if (original >= source.face_offsets.size() - 1 ||
        (face && inverse[face - 1] >= original) ||
        task.repair.FaceMap()[Host(original)] != face)
      CorrespondenceFailure(face);
    const auto a = Host(source.face_offsets[Host(original)]),
               b = Host(source.face_offsets[Host(original) + 1]);
    const auto c = Host(candidate.face_offsets[face]),
               d = Host(candidate.face_offsets[face + 1]);
    if (b - a != d - c || (c < d && task.repair.SourceCorners()[c] != a))
      CorrespondenceFailure(face);
  }
}
void RetainedCorners(void* opaque, std::size_t begin, std::size_t end) {
  auto& task = *static_cast<Correspondence*>(opaque);
  const auto source = task.repair.Source().View(),
             candidate = task.repair.Candidate().View();
  for (auto corner = begin; corner < end; ++corner) {
    if ((corner - begin) % task.index_block == 0)
      CorrespondenceCheck(task, VerificationPart::kRetainedCorners, corner);
    // Binary search takes at most size_t's bit width; no linear hidden scan.
    const auto upper = std::upper_bound(candidate.face_offsets.begin(),
                                        candidate.face_offsets.end(), corner);
    if (upper == candidate.face_offsets.begin() ||
        upper == candidate.face_offsets.end())
      CorrespondenceFailure(corner);
    const auto face =
        static_cast<std::size_t>(upper - candidate.face_offsets.begin() - 1);
    const auto original_face = task.repair.SourceFaces()[face];
    const auto original = task.repair.SourceCorners()[corner];
    const auto expected = source.face_offsets[Host(original_face)] + corner -
                          candidate.face_offsets[face];
    if (original != expected || original >= source.corner_vertices.size() ||
        task.repair.CornerMap()[Host(original)] != corner ||
        candidate.corner_vertices[corner] !=
            source.corner_vertices[Host(original)])
      CorrespondenceFailure(corner);
  }
}
void TargetPairs(void* opaque, std::size_t begin, std::size_t end) {
  auto& task = *static_cast<Correspondence*>(opaque);
  const auto faces = task.repair.FaceMap();
  for (auto i = begin; i < end; ++i) {
    if ((i - begin) % task.target_block == 0)
      CorrespondenceCheck(task, VerificationPart::kTargetPairs, i);
    const auto target = task.targets[i];
    if (target.keep >= faces.size() || target.remove >= faces.size() ||
        faces[Host(target.keep)] != faces[Host(target.remove)] ||
        task.repair.SourceFaces()[Host(faces[Host(target.remove)])] ==
            target.remove)
      CorrespondenceFailure(i);
  }
}
void VerifyRepair(Session& session, std::span<const DuplicateTarget> targets) {
  const auto& repair = *session.state.repair;
  const auto source = repair.Source().View(),
             candidate = repair.Candidate().View();
  if (source.face_offsets.empty() || candidate.face_offsets.empty() ||
      repair.FaceMap().size() != source.face_offsets.size() - 1 ||
      repair.CornerMap().size() != source.corner_vertices.size() ||
      repair.SourceFaces().size() != candidate.face_offsets.size() - 1 ||
      repair.SourceCorners().size() != candidate.corner_vertices.size() ||
      targets.size() > source.face_offsets.size() - 1 ||
      candidate.face_offsets.size() - 1 !=
          (source.face_offsets.size() - 1) - targets.size())
    CorrespondenceFailure(0);
  const auto byte_cap = session.work.verification.cancellation_block_bytes;
  Correspondence task{session, repair, targets,
                      ElementBlock(byte_cap, sizeof(geo::index_t)),
                      ElementBlock(byte_cap, sizeof(DuplicateTarget))};
  auto& stats = session.state.execution.repair_correspondence;
  const auto policy = BytePolicy(session.work.verification);
  const auto run = [&](std::size_t count, std::size_t element_bytes,
                       allocation::RangeOperation operation) {
    allocation::RunRange(session.context, session.state.allocation, policy,
                         count, element_bytes, operation, &task, session.stop,
                         stats, session.testing.verification_worker_backend,
                         &stats);
  };
  run(source.face_offsets.size() - 1, sizeof(geo::index_t), SourceFaces);
  run(candidate.face_offsets.size() - 1, sizeof(geo::index_t), RetainedFaces);
  run(candidate.corner_vertices.size(), sizeof(geo::index_t), RetainedCorners);
  run(targets.size(), sizeof(DuplicateTarget), TargetPairs);
  session.Check(Stage::kVerify);
  Passed(session.state, Check::kRepairCorrespondence);
  if (!source.attributes.empty() || !candidate.attributes.empty())
    Fail(Status::kVerificationFailure, Stage::kVerify,
         Code::kCanonicalPreservation);
  CompareBytes(session, std::as_bytes(source.positions),
               std::as_bytes(candidate.positions), Stage::kVerify,
               Code::kCanonicalPreservation,
               session.state.execution.canonical_preservation);
  // Retained source loops/starts and vertex identities were independently
  // checked above; position bytes include every unused and signed-zero row.
  Passed(session.state, Check::kCanonicalPreservation);
}
void VerifyReload(Session& session, const ic::OffDocument& document,
                  Stage stage, geo::MaterializationExecutionStats& stats) {
  const auto& state = session.state;
  const auto expected = state.repair->Candidate().View(),
             actual = document.Mesh().View();
  const auto& facts = state.written->Facts();
  if (facts.profile != ic::OffProfile::kAsciiRecords ||
      facts.text_policy != ic::OffTextPolicy::kCanonicalAscii ||
      facts.edge_count_policy != ic::OffEdgeCountPolicy::kUnknownZero ||
      facts.correspondence != ic::OffCorrespondence::kIdentity ||
      facts.emitted_edge_count != 0 || document.DeclaredEdgeCount() != 0 ||
      !facts.source_declared_edge_count ||
      *facts.source_declared_edge_count !=
          state.source.Document().DeclaredEdgeCount() ||
      facts.vertex_count != expected.positions.size() / kPositionWidth ||
      facts.face_count != expected.face_offsets.size() - 1 ||
      facts.corner_count != expected.corner_vertices.size() ||
      !actual.attributes.empty())
    Fail(Status::kVerificationFailure, stage, Code::kReloadMismatch);
  CompareBytes(session, std::as_bytes(expected.positions),
               std::as_bytes(actual.positions), stage, Code::kReloadMismatch,
               stats);
  CompareBytes(session, std::as_bytes(expected.face_offsets),
               std::as_bytes(actual.face_offsets), stage, Code::kReloadMismatch,
               stats);
  CompareBytes(session, std::as_bytes(expected.corner_vertices),
               std::as_bytes(actual.corner_vertices), stage,
               Code::kReloadMismatch, stats);
}
void Hash(Session& session, std::span<const std::byte> bytes,
          gltf_file_detail::Sha256Hex& result,
          const OffDuplicateFileWorkflowLimits& limits) {
  auto& state = session.state;
  state.outcomes.stage = Stage::kHash;
  if (state.hash_bytes > limits.max_hash_input_bytes ||
      bytes.size() > limits.max_hash_input_bytes - state.hash_bytes ||
      !gltf_file_detail::CanAppendSha256Bytes(0, bytes.size()))
    Fail(Status::kLimitExceeded, Stage::kHash, Code::kHashExtent);
  session.Check(Stage::kHash);
  gltf_file_detail::Sha256 hash;
  ++state.execution.hashing.planned_jobs;
  ++state.execution.hashing.caller_jobs;
  const auto block = Host(session.work.hashing.cancellation_block_bytes);
  for (std::size_t offset = 0; offset < bytes.size();) {
    session.Check(Stage::kHash, offset);
    const auto count = std::min(block, bytes.size() - offset);
    if (!hash.Update(bytes.subspan(offset, count)))
      Fail(Status::kLimitExceeded, Stage::kHash, Code::kHashExtent);
    state.hash_bytes += count;
    offset += count;
  }
  session.Check(Stage::kHash, bytes.size());
  result = gltf_file_detail::LowercaseSha256Hex(hash.Digest());
}
void EngineCoverage(State& state,
                    const MaterializedDuplicateResult& result) noexcept {
  using RepairStage = MaterializedDuplicateStage;
  if (state.repair) {
    if (state.repair->SourceInspection().IsStorageValid())
      Passed(state, Check::kSourceStorage);
    else
      Failed(state, Check::kSourceStorage, Code::kRepair);
    Passed(state, Check::kTargetPolicy);
    if (state.repair->CandidateInspection().IsStorageValid())
      Passed(state, Check::kCandidateStorage);
    else
      Failed(state, Check::kCandidateStorage, Code::kRepair);
    return;
  }
  if (result.diagnostic.stage > RepairStage::kSourceInspection)
    Passed(state, Check::kSourceStorage);
  if (result.status == MaterializedDuplicateStatus::kInvalidSourceStorage)
    Failed(state, Check::kSourceStorage, Code::kRepair);
  if (result.status == MaterializedDuplicateStatus::kRejected)
    Failed(state, Check::kTargetPolicy, Code::kRepair);
}
Status DelegateStatus(ic::OffStatus status) noexcept {
  if (status == ic::OffStatus::kCanceled) return Status::kCanceled;
  if (status == ic::OffStatus::kBudgetExceeded) return Status::kBudgetExceeded;
  if (status == ic::OffStatus::kLimitExceeded) return Status::kLimitExceeded;
  return Status::kDelegatedFailure;
}
Status DelegateStatus(MaterializedDuplicateStatus status) noexcept {
  if (status == MaterializedDuplicateStatus::kCanceled)
    return Status::kCanceled;
  if (status == MaterializedDuplicateStatus::kBudgetExceeded)
    return Status::kBudgetExceeded;
  if (status == MaterializedDuplicateStatus::kLimitExceeded)
    return Status::kLimitExceeded;
  if (status == MaterializedDuplicateStatus::kRejected)
    return Status::kRejected;
  return Status::kDelegatedFailure;
}
struct ProviderSignal {};
struct Provider {
  Session& session;
  const OffDuplicateFileWorkflowRequest& request;
  const OffDuplicateFileWorkflowLimits& limits;
  std::exception_ptr unexpected;
  bool own_failure = false;
};
std::span<const std::byte> Provide(const ic::OffFileVerifiedContent& content,
                                   void* opaque) {
  auto& provider = *static_cast<Provider*>(opaque);
  auto& session = provider.session;
  auto& state = session.state;
  try {
    state.outcomes.stage = Stage::kVerify;
    VerifyReload(session, content.document, Stage::kVerify,
                 state.execution.staged_candidate);
    CompareBytes(session, state.written->Bytes(), content.entry_bytes,
                 Stage::kVerify, Code::kReloadMismatch,
                 state.execution.staged_candidate);
    // Complete memory source/candidate correspondence remains retained; this
    // real staged mesh has now been compared in full against that candidate.
    Passed(state, Check::kStagedCandidate);
    Hash(session, content.entry_bytes, state.output_hash, provider.limits);
    state.output_hash_available = true;
    state.outcomes.stage = Stage::kReport;
    SerializeReport(session, provider.request, provider.limits, true);
    if (session.testing.prepared_report)
      session.testing.prepared_report(state.report->Bytes(),
                                      session.testing.state);
    state.outcomes.stage = Stage::kPublication;
    return state.report->Bytes();
  } catch (const Failure& failure) {
    Latch(state, failure);
    if (failure.status == Status::kVerificationFailure &&
        failure.diagnostic.code == Code::kReloadMismatch)
      Failed(state, Check::kStagedCandidate, Code::kReloadMismatch);
    provider.own_failure = true;
  } catch (const allocation::AdmissionFailure& failure) {
    const auto status = Admission(failure);
    OffWorkflowDiagnostic diagnostic;
    diagnostic.stage = state.outcomes.stage;
    diagnostic.code = status == Status::kCanceled ? Code::kStopRequested
                                                  : Code::kRequestBudget;
    Latch(state, {status, diagnostic});
    provider.own_failure = true;
  } catch (...) {
    provider.unexpected = std::current_exception();
  }
  throw ProviderSignal{};
}
void PublicationFailure(State& state,
                        const ic::OffFilePublicationResult& result) noexcept {
  if (result.diagnostic.stage == ic::OffFileStage::kPublication)
    Passed(state, Check::kStagedPhysicalMembers);
  else if (result.diagnostic.status ==
           ic::OffFileStatus::kPreservationMismatch) {
    if (result.diagnostic.code == ic::OffFileDiagnosticCode::kMeshMismatch)
      Failed(state, Check::kStagedCandidate, Code::kPublication);
    else if (result.diagnostic.stage == ic::OffFileStage::kInventory ||
             result.diagnostic.code == ic::OffFileDiagnosticCode::kByteMismatch)
      Failed(state, Check::kStagedPhysicalMembers, Code::kPublication);
  } else if (result.diagnostic.stage == ic::OffFileStage::kReload &&
             result.diagnostic.status != ic::OffFileStatus::kCanceled) {
    auto& check =
        state.coverage[static_cast<std::size_t>(Check::kStagedCandidate)];
    check.state = Coverage::kFailed;
    check.reason = Code::kPublication;
  }
}
void MarkFailure(State& state, const Failure& failure) noexcept {
  Latch(state, failure);
  if (failure.status == Status::kVerificationFailure) {
    if (failure.diagnostic.code == Code::kCorrespondence)
      Failed(state, Check::kRepairCorrespondence, Code::kCorrespondence);
    else if (failure.diagnostic.code == Code::kCanonicalPreservation)
      Failed(state, Check::kCanonicalPreservation,
             Code::kCanonicalPreservation);
    else if (failure.diagnostic.code == Code::kSourceChanged)
      Failed(state, Check::kSourceUnchanged, Code::kSourceChanged);
    else if (failure.diagnostic.code == Code::kReloadMismatch)
      Failed(state, Check::kReloadCandidate, Code::kReloadMismatch);
  }
  Compute(state);
}
void FailureReport(Session& session,
                   const OffDuplicateFileWorkflowRequest& request,
                   const OffDuplicateFileWorkflowLimits& limits) {
  auto& state = session.state;
  state.report_available = false;
  state.report.reset();
  if (session.stop.stop_requested()) return;
  try {
    SerializeReport(session, request, limits, false);
    state.report_available = true;
  } catch (const Failure& failure) {
    state.report_diagnostic = failure.diagnostic;
  } catch (const allocation::AdmissionFailure& failure) {
    OffWorkflowDiagnostic diagnostic;
    diagnostic.stage = Stage::kReport;
    diagnostic.code = Admission(failure) == Status::kCanceled
                          ? Code::kStopRequested
                          : Code::kRequestBudget;
    state.report_diagnostic = diagnostic;
  }
}
}  // namespace
OffDuplicateFileWorkflowResult RunOffDuplicateFileWorkflowForTest(
    const OffDuplicateFileWorkflowRequest& request,
    const OffDuplicateFileWorkflowLimits& limits,
    const OffDuplicateFileWorkflowWorkPolicy& work, std::stop_token stop,
    const WorkflowTestOptions& testing) {
  OffDuplicateFileWorkflowResult result;
  result.outcomes.publication = request.publication
                                    ? Publication::kNotAttempted
                                    : Publication::kNotRequested;
  std::shared_ptr<State> state;
  std::optional<geo::ExecutionContext> context;
  try {
    Validate(request, limits, work, stop);
    Stop(stop);
    context.emplace(request.source.Document().Mesh().ShareExecutionContext());
    auto allocator =
        allocation::AllocationContext(*context, stop,
                                      testing.allocation_backend)
            .WithRequestLimit(Host(limits.max_wrapper_new_requested_bytes));
    state = std::allocate_shared<State>(allocation::Allocator<State>(allocator),
                                        allocator, request.source);
    state->outcomes.publication = result.outcomes.publication;
    Session session{*state, *context, work, stop, testing};
    state->input_id.reserve(request.input_id.size());
    for (auto c : request.input_id) {
      session.Check(Stage::kConfiguration);
      state->input_id.push_back(c);
    }
    if (request.publication) {
      state->receipt_name.reserve(request.publication->receipt_basename.size());
      for (auto c : request.publication->receipt_basename) {
        session.Check(Stage::kConfiguration);
        state->receipt_name.push_back(c);
      }
    }
    state->outcomes.stage = Stage::kSourceProtection;
    state->baseline.emplace(allocator, request.source.EntryBytes().size());
    CopyBytes(session, request.source.EntryBytes(), state->baseline->Bytes(),
              Stage::kSourceProtection, work.verification,
              state->execution.source_protection,
              testing.source_worker_backend);
    state->outcomes.stage = Stage::kRepair;
    const auto repaired = RemoveMaterializedDuplicateFaces(
        request.source.Document().Mesh(), request.targets, {}, limits.repair,
        work.repair, stop);
    static_assert(
        std::is_nothrow_copy_constructible_v<MaterializedDuplicateRepair>);
    static_assert(std::is_nothrow_destructible_v<MaterializedDuplicateRepair>);
    if (repaired.repair) state->repair.emplace(*repaired.repair);
    state->repair_status = repaired.status;
    state->repair_diagnostic = repaired.diagnostic;
    state->execution.repair = repaired.execution;
    if (state->repair)
      state->outcomes.candidate = state->repair->Outcome();
    else if (repaired.status == MaterializedDuplicateStatus::kRejected)
      state->outcomes.candidate = CandidateOutcome::rejected;
    EngineCoverage(*state, repaired);
    if (testing.after_repair) testing.after_repair(repaired, testing.state);
    if (repaired.status != MaterializedDuplicateStatus::kSuccess ||
        !state->repair) {
      state->diagnostic = {Stage::kRepair,
                           Code::kRepair,
                           {},
                           repaired.status,
                           repaired.diagnostic};
      state->status = DelegateStatus(repaired.status);
      if (state->status == Status::kRejected)
        state->outcomes.stage = Stage::kComplete;
      Compute(*state);
      FailureReport(session, request, limits);
    } else {
      session.Check(Stage::kRepair);
      state->outcomes.stage = Stage::kWrite;
      const auto written = ic::WriteOff(
          state->repair->Candidate(),
          {request.text, request.edge_count, &request.source.Document()},
          limits.writing, work.writing, stop);
      state->write_status = written.status;
      state->write_diagnostic = written.diagnostic;
      state->execution.writing = written.execution;
      if (written.status != ic::OffStatus::kSuccess || !written.output) {
        OffWorkflowDiagnostic diagnostic{Stage::kWrite,
                                         Code::kWrite,
                                         {},
                                         written.status,
                                         written.diagnostic};
        throw Failure{DelegateStatus(written.status), diagnostic};
      }
      state->written.emplace(*written.output);
      state->outcomes.stage = Stage::kReload;
      const auto reloaded = ic::ReadOff(state->written->Bytes(), limits.reload,
                                        work.reload, *context, stop);
      state->reload_status = reloaded.status;
      state->reload_diagnostic = reloaded.diagnostic;
      state->execution.reload = reloaded.execution;
      if (reloaded.status != ic::OffStatus::kSuccess || !reloaded.document) {
        OffWorkflowDiagnostic diagnostic{Stage::kReload,
                                         Code::kReload,
                                         {},
                                         reloaded.status,
                                         reloaded.diagnostic};
        throw Failure{DelegateStatus(reloaded.status), diagnostic};
      }
      state->reloaded.emplace(*reloaded.document);
      state->outcomes.stage = Stage::kVerify;
      VerifyRepair(session, request.targets);
      VerifyReload(session, *state->reloaded, Stage::kVerify,
                   state->execution.reload_verification);
      Passed(*state, Check::kReloadCandidate);
      CompareBytes(session, state->baseline->Bytes(),
                   state->source.EntryBytes(), Stage::kVerify,
                   Code::kSourceChanged, state->execution.source_verification);
      Passed(*state, Check::kSourceUnchanged);
      state->memory_verified = true;
      Hash(session, state->source.EntryBytes(), state->input_hash, limits);
      state->input_hash_available = true;
      if (!request.publication) {
        state->status = Status::kSuccess;
        state->outcomes.stage = Stage::kComplete;
        Compute(*state);
        state->outcomes.stage = Stage::kReport;
        SerializeReport(session, request, limits, false);
        state->outcomes.stage = Stage::kComplete;
        session.Check(Stage::kComplete);
        state->report_available = true;
      } else {
        Provider provider{session, request, limits, {}};
        const ic::OffFilePublicationOptions options{ic::OffFileReceiptProvider{
            std::string_view(state->receipt_name.data(),
                             state->receipt_name.size()),
            Provide, &provider}};
        state->outcomes.stage = Stage::kPublication;
        const auto publication = ic::PublishOffBundle(
            {state->source, state->repair->Candidate(), *state->written},
            request.publication->destination, options, limits.publication,
            work.publication, stop);
        if (provider.unexpected) std::rethrow_exception(provider.unexpected);
        state->publication.emplace(publication);
        if (publication.outcome == ic::OffFilePublicationOutcome::kPublished) {
          if (testing.published) testing.published(publication, testing.state);
          static_assert(
              std::is_nothrow_copy_constructible_v<ic::OffPublishedBundle>);
          state->published.emplace(*publication.bundle);
          Passed(*state, Check::kStagedPhysicalMembers);
          state->status = Status::kSuccess;
          state->outcomes.publication = Publication::kPublished;
          state->outcomes.stage = Stage::kComplete;
          Compute(*state);
          state->report_available = true;
        } else {
          state->outcomes.publication =
              publication.outcome == ic::OffFilePublicationOutcome::kCanceled
                  ? Publication::kCanceled
                  : Publication::kFailed;
          if (!provider.own_failure) {
            state->status =
                publication.outcome == ic::OffFilePublicationOutcome::kCanceled
                    ? Status::kCanceled
                    : Status::kDelegatedFailure;
            state->diagnostic = {
                Stage::kPublication, Code::kPublication, {}, {}, {}};
            state->outcomes.stage = Stage::kPublication;
          }
          PublicationFailure(*state, publication);
          Compute(*state);
          FailureReport(session, request, limits);
        }
      }
    }
  } catch (const Failure& failure) {
    if (state) {
      MarkFailure(*state, failure);
      Session session{*state, *context, work, stop, testing};
      FailureReport(session, request, limits);
    } else {
      result.status = failure.status;
      result.diagnostic = failure.diagnostic;
      result.outcomes.stage = failure.diagnostic.stage;
    }
  } catch (const allocation::AdmissionFailure& failure) {
    const auto status = Admission(failure);
    OffWorkflowDiagnostic diagnostic;
    diagnostic.stage = state ? state->outcomes.stage : Stage::kConfiguration;
    diagnostic.code = status == Status::kCanceled ? Code::kStopRequested
                                                  : Code::kRequestBudget;
    if (state) {
      MarkFailure(*state, {status, diagnostic});
      Session session{*state, *context, work, stop, testing};
      FailureReport(session, request, limits);
    } else {
      result.status = status;
      result.diagnostic = diagnostic;
      result.outcomes.stage = diagnostic.stage;
    }
  }
  if (state) {
    result.status = state->status;
    result.outcomes = state->outcomes;
    result.diagnostic = state->diagnostic;
    result.report_diagnostic = state->report_diagnostic;
    result.execution = state->execution;
    if (state->publication) result.publication.emplace(*state->publication);
    result.record.emplace(Access::Make(std::move(state)));
  } else if (result.status == Status::kCanceled) {
    result.outcomes.execution = OffWorkflowExecutionOutcome::kCanceled;
    result.outcomes.exit = OffWorkflowExitMeaning::kCanceled;
  }
  return result;
}
}  // namespace meshvale::repair::off_workflow_detail

namespace meshvale::repair {
OffDuplicateFileWorkflowRecord::OffDuplicateFileWorkflowRecord(
    std::shared_ptr<const off_workflow_detail::State> state) noexcept
    : state_(std::move(state)) {}
OffDuplicateFileWorkflowRecord::OffDuplicateFileWorkflowRecord(
    const OffDuplicateFileWorkflowRecord&) noexcept = default;
OffDuplicateFileWorkflowRecord& OffDuplicateFileWorkflowRecord::operator=(
    const OffDuplicateFileWorkflowRecord&) noexcept = default;
OffDuplicateFileWorkflowRecord::~OffDuplicateFileWorkflowRecord() noexcept =
    default;
const interchange::OffFileAsset& OffDuplicateFileWorkflowRecord::Source()
    const noexcept {
  return state_->source;
}
const MaterializedDuplicateRepair* OffDuplicateFileWorkflowRecord::RepairFacts()
    const noexcept {
  return state_->repair ? &*state_->repair : nullptr;
}
const std::optional<interchange::OffOutput>&
OffDuplicateFileWorkflowRecord::Output() const noexcept {
  return state_->memory_verified ? state_->written : state_->absent_output;
}
const std::optional<interchange::OffDocument>&
OffDuplicateFileWorkflowRecord::Reload() const noexcept {
  return state_->memory_verified ? state_->reloaded : state_->absent_reload;
}
const std::optional<interchange::OffPublishedBundle>&
OffDuplicateFileWorkflowRecord::Published() const noexcept {
  return state_->published;
}
OffWorkflowOutcomes OffDuplicateFileWorkflowRecord::Outcomes() const noexcept {
  return state_->outcomes;
}
std::span<const OffWorkflowCoverage> OffDuplicateFileWorkflowRecord::Coverage()
    const noexcept {
  return state_->coverage;
}
std::span<const std::byte> OffDuplicateFileWorkflowRecord::ReportJson()
    const noexcept {
  return state_->report_available && state_->report
             ? state_->report->Bytes()
             : std::span<const std::byte>{};
}
std::optional<std::string_view>
OffDuplicateFileWorkflowRecord::PublishedReportPath() const noexcept {
  if (!state_->published) return {};
  return std::string_view(state_->receipt_name.data(),
                          state_->receipt_name.size());
}
std::optional<std::string_view> OffDuplicateFileWorkflowRecord::InputSha256()
    const noexcept {
  if (!state_->input_hash_available) return {};
  return std::string_view(state_->input_hash.data(), state_->input_hash.size());
}
OffWorkflowRequestedBytes OffDuplicateFileWorkflowRecord::NewRequestedBytes()
    const noexcept {
  return state_->Bytes();
}
OffWorkflowExecution OffDuplicateFileWorkflowRecord::Execution()
    const noexcept {
  return state_->execution;
}
OffDuplicateFileWorkflowResult RunOffDuplicateFileWorkflow(
    const OffDuplicateFileWorkflowRequest& request,
    const OffDuplicateFileWorkflowLimits& limits,
    const OffDuplicateFileWorkflowWorkPolicy& work, std::stop_token stop) {
  return off_workflow_detail::RunOffDuplicateFileWorkflowForTest(
      request, limits, work, stop, {});
}
}  // namespace meshvale::repair
