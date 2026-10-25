// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/ply_workflow.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>
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
#include "ply_workflow_internal.h"
#include "ply_workflow_test_internal.h"

namespace meshvale::repair::ply_workflow_detail {
namespace geo = geometry;
namespace ic = interchange;
using Stage = PlyWorkflowStage;
using Status = PlyWorkflowStatus;
using Code = PlyWorkflowDiagnosticCode;
using Check = PlyWorkflowCheck;
using Coverage = PlyWorkflowCoverageState;
using Publication = PlyWorkflowPublicationOutcome;

[[noreturn]] void Fail(Status status, Stage stage, Code code,
                       std::optional<geo::index_t> element) {
  PlyWorkflowDiagnostic diagnostic;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.element = element;
  throw Failure{status, diagnostic};
}
State::State(const allocation::AllocationContext& allocator,
             const ic::PlyFileAsset& input)
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
PlyWorkflowRequestedBytes State::Bytes() const noexcept {
  PlyWorkflowRequestedBytes bytes;
  bytes.wrapper = allocation.OperationActiveBytes();
  bytes.source_files = source.NewRequestedBytes();
  bytes.source_ply = source.Document().NewRequestedBytes();
  if (repair) bytes.repair = repair->NewRequestedBytes();
  if (written) bytes.writing = written->NewRequestedBytes();
  if (reloaded) bytes.reload = reloaded->NewRequestedBytes();
  if (published) {
    bytes.publication_files = published->NewRequestedBytes();
    bytes.publication_ply = published->Document().NewRequestedBytes();
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
  outcomes.profile = failed       ? PlyWorkflowProfileOutcome::kFailed
                     : incomplete ? PlyWorkflowProfileOutcome::kIncomplete
                                  : PlyWorkflowProfileOutcome::kPassed;
  outcomes.execution =
      state.status == Status::kCanceled ? PlyWorkflowExecutionOutcome::kCanceled
      : state.status == Status::kSuccess || state.status == Status::kRejected
          ? PlyWorkflowExecutionOutcome::kCompleted
          : PlyWorkflowExecutionOutcome::kFailed;
  outcomes.exit = outcomes.execution == PlyWorkflowExecutionOutcome::kCanceled
                      ? PlyWorkflowExitMeaning::kCanceled
                  : outcomes.execution == PlyWorkflowExecutionOutcome::kFailed
                      ? PlyWorkflowExitMeaning::kExecutionFailure
                  : !outcomes.candidate ||
                          outcomes.candidate == CandidateOutcome::rejected ||
                          outcomes.profile != PlyWorkflowProfileOutcome::kPassed
                      ? PlyWorkflowExitMeaning::kRejectedOrIncomplete
                      : PlyWorkflowExitMeaning::kSuccess;
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
void Validate(const PlyByteWorkPolicy& work) {
  for (auto value : {work.parallel_min_bytes, work.partition_bytes,
                     work.cancellation_block_bytes, work.max_jobs})
    Positive(value);
}
void Validate(const ic::PlyWorkPolicy& work) {
  Positive(work.cancellation_block_bytes);
  Positive(work.cancellation_block_values);
  Positive(work.cancellation_block_records);
  Validate(work.geometry);
}
void Validate(const ic::PlyReadLimits& limits) {
  for (auto value :
       {limits.max_source_bytes, limits.max_header_bytes,
        limits.max_header_lines, limits.max_line_bytes, limits.max_token_bytes,
        limits.max_comment_bytes, limits.max_name_bytes,
        limits.max_total_name_bytes, limits.max_properties, limits.max_vertices,
        limits.max_faces, limits.max_corners, limits.max_corners_per_face,
        limits.max_list_rows, limits.max_list_items_per_row,
        limits.max_property_values, limits.max_codec_new_requested_bytes,
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
void Validate(const ic::PlyWriteLimits& limits) {
  for (auto value :
       {limits.max_output_bytes, limits.max_header_bytes,
        limits.max_header_lines, limits.max_line_bytes, limits.max_token_bytes,
        limits.max_name_bytes, limits.max_total_name_bytes,
        limits.max_properties, limits.max_vertices, limits.max_faces,
        limits.max_corners, limits.max_corners_per_face, limits.max_list_rows,
        limits.max_list_items_per_row, limits.max_property_values,
        limits.max_codec_new_requested_bytes,
        limits.max_total_new_requested_bytes})
    Positive(value);
  Validate(limits.inspection);
  if (Add(limits.max_codec_new_requested_bytes,
          limits.inspection.max_new_requested_bytes) >
      limits.max_total_new_requested_bytes)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
void Validate(const ic::PlyFilePublicationLimits& limits,
              const ic::PlyFilePublicationWorkPolicy& work) {
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
  constexpr std::array<std::string_view, 4> kReservedDeviceNames{"con", "prn",
                                                                 "aux", "nul"};
  constexpr std::string_view kComDevicePrefix = "com";
  constexpr std::string_view kLptDevicePrefix = "lpt";
  for (const auto device : kReservedDeviceNames)
    if (FoldEqual(stem, device, stop)) refuse();
  if (stem.size() == kNumberedDeviceLength &&
      (FoldEqual(stem.substr(0, kNumberedDeviceLength - 1), kComDevicePrefix,
                 stop) ||
       FoldEqual(stem.substr(0, kNumberedDeviceLength - 1), kLptDevicePrefix,
                 stop)) &&
      stem.back() >= '1' && stem.back() <= '9')
    refuse();
}
void Declarations(std::span<const RowLocalAttributeDeclaration> declarations,
                  const MaterializedDuplicateLimits& limits,
                  std::stop_token stop) {
  Count(declarations.size(), limits.max_row_local_declarations);
  (void)allocation::CheckedProduct(declarations.size(),
                                   sizeof(RowLocalAttributeDeclaration));
  std::uint64_t names = 0;
  for (std::size_t i = 0; i < declarations.size(); ++i) {
    Stop(stop);
    const auto& declaration = declarations[i];
    const auto refuse = [&] {
      Fail(Status::kInvalidRequest, Stage::kConfiguration, Code::kDeclaration,
           i);
    };
    if (declaration.domain != geo::AttributeDomain::vertex &&
        declaration.domain != geo::AttributeDomain::face &&
        declaration.domain != geo::AttributeDomain::corner)
      refuse();
    if (declaration.name.empty()) refuse();
    names = Add(names, declaration.name.size());
    Count(names, limits.max_declaration_name_bytes);
    for (std::size_t j = 0; j < declaration.name.size(); ++j) {
      Stop(stop);
      const char c = declaration.name[j];
      const bool identifier = (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') || c == '_' ||
                              (j && c >= '0' && c <= '9');
      if (!identifier) refuse();
    }
    for (std::size_t j = 0; j < i; ++j) {
      Stop(stop);
      const auto& prior = declarations[j];
      if (prior.domain != declaration.domain ||
          prior.name.size() != declaration.name.size())
        continue;
      bool equal = true;
      for (std::size_t k = 0; k < prior.name.size(); ++k) {
        Stop(stop);
        if (prior.name[k] != declaration.name[k]) {
          equal = false;
          break;
        }
      }
      if (equal) refuse();
    }
  }
}
// Admit current physical metadata before any typed payload comparison. Original
// schema row provenance is deliberately not used as the candidate's row count.
void BoundCurrent(const geo::MeshReadView& view,
                  std::span<const ic::PlyElementView> schema,
                  const PlyDuplicateFileWorkflowLimits& limits,
                  std::stop_token stop, Stage stage) {
  const auto check = [&] {
    if (stop.stop_requested())
      Fail(Status::kCanceled, stage, Code::kStopRequested);
  };
  const auto& ply = limits.reload;
  const auto& material = ply.materialization;
  const auto count = [&](std::uint64_t value, std::uint64_t cap) {
    Count(value, cap, stage);
  };
  if (view.positions.size() % kPositionWidth || view.face_offsets.empty())
    Fail(Status::kVerificationFailure, stage, Code::kCanonicalPreservation);
  const auto vertices = view.positions.size() / kPositionWidth;
  const auto faces = view.face_offsets.size() - 1;
  const auto corners = view.corner_vertices.size();
  count(vertices,
        std::min(limits.max_verification_vertices, material.max_vertices));
  count(faces, std::min(limits.max_verification_faces, material.max_faces));
  count(corners,
        std::min(limits.max_verification_corners, material.max_corners));
  count(vertices, ply.max_vertices);
  count(faces, ply.max_faces);
  count(corners, ply.max_corners);
  count(view.face_offsets.size(), material.max_face_offsets);
  count(view.attributes.size(), material.max_attributes);
  (void)allocation::CheckedProduct(view.positions.size(), sizeof(double));
  (void)allocation::CheckedProduct(view.face_offsets.size(),
                                   sizeof(geo::index_t));
  (void)allocation::CheckedProduct(corners, sizeof(geo::index_t));
  (void)allocation::CheckedProduct(view.attributes.size(),
                                   sizeof(geo::AttributeReadView));
  std::uint64_t scalars = 0, offsets = 0, presence = 0, metadata = 0,
                descriptors = 0;
  const auto descriptor = [&](std::string_view text) {
    descriptors = Add(descriptors, text.size());
    count(descriptors, material.max_descriptor_bytes);
  };
  for (const auto& attribute : view.attributes) {
    check();
    descriptor(attribute.name);
    descriptor(attribute.semantic);
    const auto n = std::visit(
        [](const auto values) {
          (void)allocation::CheckedProduct(
              values.size(), sizeof(typename decltype(values)::element_type));
          return values.size();
        },
        attribute.values);
    scalars = Add(scalars, n);
    count(scalars, material.max_attribute_scalars);
    count(scalars, ply.max_property_values);
    if (attribute.offsets) {
      offsets = Add(offsets, attribute.offsets->size());
      count(offsets, material.max_attribute_offsets);
      (void)allocation::CheckedProduct(attribute.offsets->size(),
                                       sizeof(geo::index_t));
    }
    if (attribute.present) {
      presence = Add(presence, attribute.present->size());
      count(presence, material.max_presence_bytes);
    }
    metadata = Add(metadata, attribute.metadata.size());
    count(metadata, material.max_metadata_entries);
    for (const auto& item : attribute.metadata) {
      check();
      descriptor(item.key);
      descriptor(item.value);
    }
  }
  std::uint64_t properties = 0, names = 0, list_rows = 0;
  const auto schema_name = [&](std::string_view name) {
    count(name.size(), ply.max_name_bytes);
    names = Add(names, name.size());
    count(names, ply.max_total_name_bytes);
  };
  // PLY admits exactly these two distinct element kinds. Count provenance is
  // retained by the original document, separately from this current mesh.
  constexpr std::uint64_t kPlyElementKindCount = 2;
  count(schema.size(), kPlyElementKindCount);
  constexpr std::string_view kVertexElementName = "vertex";
  constexpr std::string_view kFaceElementName = "face";
  for (const auto& element : schema) {
    check();
    schema_name(element.kind == ic::PlyElementKind::kVertex ? kVertexElementName
                                                            : kFaceElementName);
    properties = Add(properties, element.properties.size());
    count(properties, ply.max_properties);
    for (const auto& property : element.properties) {
      check();
      schema_name(property.name);
      if (property.kind != ic::PlyPropertyKind::kList) continue;
      const auto rows =
          element.kind == ic::PlyElementKind::kVertex ? vertices : faces;
      list_rows = Add(list_rows, rows);
      count(list_rows, ply.max_list_rows);
      if (property.binding == ic::PlyBinding::kVertexIndices) {
        for (std::size_t row = 0; row < faces; ++row) {
          check();
          const auto begin = view.face_offsets[row],
                     end = view.face_offsets[row + 1];
          if (begin > end || end > corners)
            Fail(Status::kVerificationFailure, stage,
                 Code::kCanonicalPreservation, row);
          count(end - begin, ply.max_corners_per_face);
          count(end - begin, ply.max_list_items_per_row);
        }
      } else {
        if (!property.attribute ||
            *property.attribute >= view.attributes.size())
          Fail(Status::kVerificationFailure, stage, Code::kWireSchema);
        const auto& attribute = view.attributes[Host(*property.attribute)];
        if (!attribute.offsets || attribute.offsets->size() != rows + 1)
          Fail(Status::kVerificationFailure, stage, Code::kTypedProperties);
        const auto n = std::visit(
            [](const auto values) { return values.size(); }, attribute.values);
        for (std::size_t row = 0; row < rows; ++row) {
          check();
          const auto begin = (*attribute.offsets)[row],
                     end = (*attribute.offsets)[row + 1];
          if (begin > end || end > n)
            Fail(Status::kVerificationFailure, stage, Code::kTypedProperties,
                 row);
          count(end - begin, ply.max_list_items_per_row);
        }
      }
    }
  }
}
void Validate(const PlyDuplicateFileWorkflowRequest& request,
              const PlyDuplicateFileWorkflowLimits& limits,
              const PlyDuplicateFileWorkflowWorkPolicy& work,
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
  if ((request.writing.encoding != ic::PlyEncoding::kAscii &&
       request.writing.encoding != ic::PlyEncoding::kBinaryLittleEndian &&
       request.writing.encoding != ic::PlyEncoding::kBinaryBigEndian) ||
      request.writing.rewrite_policy != ic::PlyRewritePolicy::kCanonical)
    Fail(Status::kInvalidRequest, Stage::kConfiguration, Code::kPolicy);
  Declarations(request.row_local_attributes, limits.repair, stop);
  Count(request.targets.size(), limits.repair.max_targets);
  (void)allocation::CheckedProduct(request.targets.size(),
                                   sizeof(DuplicateTarget));
  Count(request.source.EntryBytes().size(), limits.max_source_protection_bytes);
  BoundCurrent(request.source.Document().Mesh().View(),
               request.source.Document().Schema(), limits, stop,
               Stage::kConfiguration);
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
geo::MeshMaterializationWorkPolicy BytePolicy(const PlyByteWorkPolicy& work) {
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
               const PlyByteWorkPolicy& work,
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
  CompareBytes(session, std::as_bytes(source.positions),
               std::as_bytes(candidate.positions), Stage::kVerify,
               Code::kCanonicalPreservation,
               session.state.execution.canonical_preservation);
  // Retained source loops/starts and vertex identities were independently
  // checked above; position bytes include every unused and signed-zero row.
  Passed(session.state, Check::kCanonicalPreservation);
}
// Attribute payload work partitions actual scalar elements. Ragged offsets are
// checked separately before binary lookup, so empty rows never create a guessed
// scalar source. Every borrowed span remains alive through RunRange's joins.
struct AttributeTask {
  Session& session;
  const geo::AttributeReadView& source;
  const geo::AttributeReadView& candidate;
  std::span<const geo::index_t> rows;
  bool identity;
  Code code;
  std::size_t block;
};
std::size_t SourceRow(const AttributeTask& task, std::size_t row) {
  return task.identity ? row : Host(task.rows[row]);
}
void TypedFailure(Code code, std::optional<geo::index_t> element = {}) {
  Fail(Status::kVerificationFailure, Stage::kVerify, code, element);
}
void AttributeRows(void* opaque, std::size_t begin, std::size_t end) {
  auto& task = *static_cast<AttributeTask*>(opaque);
  for (auto row = begin; row < end; ++row) {
    if ((row - begin) % task.block == 0)
      task.session.Check(Stage::kVerify, row);
    const auto source_row = SourceRow(task, row);
    if (task.source.offsets) {
      const auto a = (*task.source.offsets)[source_row],
                 b = (*task.source.offsets)[source_row + 1],
                 c = (*task.candidate.offsets)[row],
                 d = (*task.candidate.offsets)[row + 1];
      if (b - a != d - c) TypedFailure(task.code, row);
    }
    if (task.source.present &&
        (*task.source.present)[source_row] != (*task.candidate.present)[row])
      TypedFailure(task.code, row);
  }
}
template <class T>
struct ScalarTask {
  AttributeTask& attribute;
  std::span<const T> source, candidate;
};
template <class T>
void AttributeScalars(void* opaque, std::size_t begin, std::size_t end) {
  auto& task = *static_cast<ScalarTask<T>*>(opaque);
  auto& attribute = task.attribute;
  const auto block = ElementBlock(
      attribute.session.work.verification.cancellation_block_bytes, sizeof(T));
  for (auto i = begin; i < end; ++i) {
    if ((i - begin) % block == 0) attribute.session.Check(Stage::kVerify, i);
    std::size_t row = 0, scalar = 0;
    if (attribute.candidate.offsets) {
      const auto offsets = *attribute.candidate.offsets;
      const auto upper = std::upper_bound(offsets.begin(), offsets.end(), i);
      if (upper == offsets.begin() || upper == offsets.end())
        TypedFailure(attribute.code, i);
      row = static_cast<std::size_t>(upper - offsets.begin() - 1);
      scalar = Host((*attribute.source.offsets)[SourceRow(attribute, row)]) +
               i - Host(offsets[row]);
    } else {
      row = i / attribute.candidate.components;
      scalar = SourceRow(attribute, row) * attribute.source.components +
               i % attribute.candidate.components;
    }
    if (scalar >= task.source.size()) TypedFailure(attribute.code, i);
    const auto first = std::as_bytes(task.source.subspan(scalar, 1));
    const auto second = std::as_bytes(task.candidate.subspan(i, 1));
    for (std::size_t byte = 0; byte < sizeof(T); ++byte)
      if (first[byte] != second[byte]) TypedFailure(attribute.code, i);
  }
}
void TypedAttributes(Session& session, const geo::MeshReadView& source,
                     const geo::MeshReadView& candidate, bool identity,
                     geo::MaterializationExecutionStats& stats, Code code) {
  if (source.attributes.size() != candidate.attributes.size())
    TypedFailure(code);
  const auto compare_text = [&](std::string_view a, std::string_view b) {
    CompareBytes(session, std::as_bytes(std::span(a.data(), a.size())),
                 std::as_bytes(std::span(b.data(), b.size())), Stage::kVerify,
                 code, stats);
  };
  const auto policy = BytePolicy(session.work.verification);
  for (std::size_t ordinal = 0; ordinal < source.attributes.size(); ++ordinal) {
    session.Check(Stage::kVerify, ordinal);
    const auto& a = source.attributes[ordinal];
    const auto& b = candidate.attributes[ordinal];
    if (a.domain != b.domain || a.set_index != b.set_index ||
        a.components != b.components || !a.components ||
        a.values.index() != b.values.index() ||
        a.offsets.has_value() != b.offsets.has_value() ||
        a.present.has_value() != b.present.has_value() ||
        a.metadata.size() != b.metadata.size())
      TypedFailure(code, ordinal);
    compare_text(a.name, b.name);
    compare_text(a.semantic, b.semantic);
    for (std::size_t item = 0; item < a.metadata.size(); ++item) {
      session.Check(Stage::kVerify, item);
      compare_text(a.metadata[item].key, b.metadata[item].key);
      compare_text(a.metadata[item].value, b.metadata[item].value);
    }
    std::size_t source_rows = 0, rows = 0;
    std::span<const geo::index_t> mapping;
    switch (a.domain) {
      case geo::AttributeDomain::vertex:
        source_rows = source.positions.size() / kPositionWidth;
        rows = candidate.positions.size() / kPositionWidth;
        if (source_rows != rows) TypedFailure(code, ordinal);
        break;
      case geo::AttributeDomain::face:
        source_rows = source.face_offsets.size() - 1;
        rows = candidate.face_offsets.size() - 1;
        if (!identity) mapping = session.state.repair->SourceFaces();
        break;
      case geo::AttributeDomain::corner:
        source_rows = source.corner_vertices.size();
        rows = candidate.corner_vertices.size();
        if (!identity) mapping = session.state.repair->SourceCorners();
        break;
      default:
        TypedFailure(code, ordinal);
    }
    const bool row_identity =
        identity || a.domain == geo::AttributeDomain::vertex;
    if (row_identity ? source_rows != rows : mapping.size() != rows)
      TypedFailure(code, ordinal);
    const auto shape = [&](const geo::AttributeReadView& attribute,
                           std::size_t n) {
      const auto scalars = std::visit([](auto values) { return values.size(); },
                                      attribute.values);
      (void)allocation::CheckedProduct(n, attribute.components);
      if (attribute.present && attribute.present->size() != n)
        TypedFailure(code, ordinal);
      if (!attribute.offsets) {
        if (scalars != n * attribute.components) TypedFailure(code, ordinal);
      } else {
        const auto offsets = *attribute.offsets;
        if (offsets.size() != n + 1 || offsets.front() != 0 ||
            offsets.back() != scalars)
          TypedFailure(code, ordinal);
        for (std::size_t row = 0; row < n; ++row) {
          session.Check(Stage::kVerify, row);
          if (offsets[row] > offsets[row + 1] || offsets[row + 1] > scalars ||
              (offsets[row + 1] - offsets[row]) % attribute.components)
            TypedFailure(code, row);
        }
      }
    };
    shape(a, source_rows);
    shape(b, rows);
    if (!row_identity)
      for (auto original : mapping) {
        session.Check(Stage::kVerify, original);
        if (original >= source_rows) TypedFailure(code, ordinal);
      }
    AttributeTask task{
        session,
        a,
        b,
        mapping,
        row_identity,
        code,
        ElementBlock(session.work.verification.cancellation_block_bytes,
                     sizeof(geo::index_t))};
    if (a.offsets || a.present)
      allocation::RunRange(session.context, session.state.allocation, policy,
                           rows, sizeof(geo::index_t), AttributeRows, &task,
                           session.stop, stats,
                           session.testing.verification_worker_backend, &stats);
    std::visit(
        [&](const auto values) {
          using T =
              std::remove_const_t<typename decltype(values)::element_type>;
          const auto expected = std::get<std::span<const T>>(a.values);
          ScalarTask<T> scalar_task{task, expected, values};
          allocation::RunRange(
              session.context, session.state.allocation, policy, values.size(),
              sizeof(T), AttributeScalars<T>, &scalar_task, session.stop, stats,
              session.testing.verification_worker_backend, &stats);
        },
        b.values);
    session.Check(Stage::kVerify, ordinal);
  }
}
void WireSchema(Session& session, const ic::PlyDocument& document,
                geo::MaterializationExecutionStats& stats, Code code) {
  const auto& state = session.state;
  const auto original = state.source.Document().Schema(),
             actual = document.Schema();
  const auto expected = state.repair->Candidate().View();
  const auto& facts = state.written->Facts();
  std::uint64_t properties = 0;
  if (original.size() != actual.size() ||
      facts.source_encoding != state.source.Document().Encoding() ||
      facts.source_vertex_count !=
          state.source.Document().Mesh().View().positions.size() /
              kPositionWidth ||
      facts.source_face_count !=
          state.source.Document().Mesh().View().face_offsets.size() - 1 ||
      facts.encoding != state.requested_writing.encoding ||
      document.Encoding() != state.requested_writing.encoding ||
      facts.rewrite_policy != ic::PlyRewritePolicy::kCanonical ||
      facts.correspondence != ic::PlyCorrespondence::kIdentity ||
      facts.vertex_count != expected.positions.size() / kPositionWidth ||
      facts.face_count != expected.face_offsets.size() - 1 ||
      facts.corner_count != expected.corner_vertices.size())
    TypedFailure(code);
  for (std::size_t e = 0; e < original.size(); ++e) {
    session.Check(Stage::kVerify, e);
    const auto& a = original[e];
    const auto& b = actual[e];
    const auto rows = a.kind == ic::PlyElementKind::kVertex
                          ? expected.positions.size() / kPositionWidth
                          : expected.face_offsets.size() - 1;
    if (a.kind != b.kind || b.original_count != rows ||
        a.properties.size() != b.properties.size())
      TypedFailure(code, e);
    properties = Add(properties, a.properties.size());
    for (std::size_t p = 0; p < a.properties.size(); ++p) {
      session.Check(Stage::kVerify, p);
      const auto& first = a.properties[p];
      const auto& second = b.properties[p];
      if (first.kind != second.kind || first.value_type != second.value_type ||
          first.count_type != second.count_type ||
          first.binding != second.binding ||
          first.attribute != second.attribute)
        TypedFailure(code, p);
      CompareBytes(
          session,
          std::as_bytes(std::span(first.name.data(), first.name.size())),
          std::as_bytes(std::span(second.name.data(), second.name.size())),
          Stage::kVerify, code, stats);
    }
  }
  if (facts.property_count != properties) TypedFailure(code);
  session.Check(Stage::kVerify);
}
void VerifyReload(Session& session, const ic::PlyDocument& document,
                  Stage stage, geo::MaterializationExecutionStats& stats) {
  const auto& state = session.state;
  const auto expected = state.repair->Candidate().View(),
             actual = document.Mesh().View();
  CompareBytes(session, std::as_bytes(expected.positions),
               std::as_bytes(actual.positions), stage, Code::kReloadMismatch,
               stats);
  CompareBytes(session, std::as_bytes(expected.face_offsets),
               std::as_bytes(actual.face_offsets), stage, Code::kReloadMismatch,
               stats);
  CompareBytes(session, std::as_bytes(expected.corner_vertices),
               std::as_bytes(actual.corner_vertices), stage,
               Code::kReloadMismatch, stats);
  TypedAttributes(session, expected, actual, true, stats,
                  Code::kReloadMismatch);
}
void Hash(Session& session, std::span<const std::byte> bytes,
          gltf_file_detail::Sha256Hex& result,
          const PlyDuplicateFileWorkflowLimits& limits) {
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
Status DelegateStatus(ic::PlyStatus status) noexcept {
  if (status == ic::PlyStatus::kCanceled) return Status::kCanceled;
  if (status == ic::PlyStatus::kBudgetExceeded) return Status::kBudgetExceeded;
  if (status == ic::PlyStatus::kLimitExceeded) return Status::kLimitExceeded;
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
  const PlyDuplicateFileWorkflowRequest& request;
  const PlyDuplicateFileWorkflowLimits& limits;
  std::exception_ptr unexpected;
  bool own_failure = false;
};
std::span<const std::byte> Provide(const ic::PlyFileVerifiedContent& content,
                                   void* opaque) {
  auto& provider = *static_cast<Provider*>(opaque);
  auto& session = provider.session;
  auto& state = session.state;
  try {
    state.outcomes.stage = Stage::kVerify;
    VerifyReload(session, content.document, Stage::kVerify,
                 state.execution.staged_candidate);
    WireSchema(session, content.document, state.execution.staged_candidate,
               Code::kReloadMismatch);
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
    PlyWorkflowDiagnostic diagnostic;
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
                        const ic::PlyFilePublicationResult& result) noexcept {
  if (result.diagnostic.stage == ic::PlyFileStage::kPublication)
    Passed(state, Check::kStagedPhysicalMembers);
  else if (result.diagnostic.status ==
           ic::PlyFileStatus::kPreservationMismatch) {
    if (result.diagnostic.code == ic::PlyFileDiagnosticCode::kMeshMismatch ||
        result.diagnostic.code ==
            ic::PlyFileDiagnosticCode::kAttributeMismatch ||
        result.diagnostic.code == ic::PlyFileDiagnosticCode::kSchemaMismatch)
      Failed(state, Check::kStagedCandidate, Code::kPublication);
    else if (result.diagnostic.stage == ic::PlyFileStage::kInventory ||
             result.diagnostic.code == ic::PlyFileDiagnosticCode::kByteMismatch)
      Failed(state, Check::kStagedPhysicalMembers, Code::kPublication);
  } else if (result.diagnostic.stage == ic::PlyFileStage::kReload &&
             result.diagnostic.status != ic::PlyFileStatus::kCanceled) {
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
    else if (failure.diagnostic.code == Code::kTypedProperties)
      Failed(state, Check::kTypedProperties, Code::kTypedProperties);
    else if (failure.diagnostic.code == Code::kWireSchema)
      Failed(state, Check::kWireSchema, Code::kWireSchema);
  }
  Compute(state);
}
void FailureReport(Session& session,
                   const PlyDuplicateFileWorkflowRequest& request,
                   const PlyDuplicateFileWorkflowLimits& limits) {
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
    PlyWorkflowDiagnostic diagnostic;
    diagnostic.stage = Stage::kReport;
    diagnostic.code = Admission(failure) == Status::kCanceled
                          ? Code::kStopRequested
                          : Code::kRequestBudget;
    state.report_diagnostic = diagnostic;
  }
}
}  // namespace
PlyDuplicateFileWorkflowResult RunPlyDuplicateFileWorkflowForTest(
    const PlyDuplicateFileWorkflowRequest& request,
    const PlyDuplicateFileWorkflowLimits& limits,
    const PlyDuplicateFileWorkflowWorkPolicy& work, std::stop_token stop,
    const WorkflowTestOptions& testing) {
  PlyDuplicateFileWorkflowResult result;
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
    state->requested_writing = request.writing;
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
    MaterializedDuplicateOptions repair_options;
    repair_options.row_local_attributes = request.row_local_attributes;
    const auto repaired = RemoveMaterializedDuplicateFaces(
        request.source.Document().Mesh(), request.targets, repair_options,
        limits.repair, work.repair, stop);
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
      BoundCurrent(state->repair->Candidate().View(),
                   state->source.Document().Schema(), limits, stop,
                   Stage::kWrite);
      const auto written =
          ic::WritePly(state->repair->Candidate(), request.source.Document(),
                       request.writing, limits.writing, work.writing, stop);
      state->write_status = written.status;
      state->write_diagnostic = written.diagnostic;
      state->execution.writing = written.execution;
      if (written.status != ic::PlyStatus::kSuccess || !written.output) {
        PlyWorkflowDiagnostic diagnostic{Stage::kWrite,
                                         Code::kWrite,
                                         {},
                                         written.status,
                                         written.diagnostic};
        throw Failure{DelegateStatus(written.status), diagnostic};
      }
      state->written.emplace(*written.output);
      state->outcomes.stage = Stage::kReload;
      const auto reloaded = ic::ReadPly(state->written->Bytes(), limits.reload,
                                        work.reload, *context, stop);
      state->reload_status = reloaded.status;
      state->reload_diagnostic = reloaded.diagnostic;
      state->execution.reload = reloaded.execution;
      if (reloaded.status != ic::PlyStatus::kSuccess || !reloaded.document) {
        PlyWorkflowDiagnostic diagnostic{Stage::kReload,
                                         Code::kReload,
                                         {},
                                         reloaded.status,
                                         reloaded.diagnostic};
        throw Failure{DelegateStatus(reloaded.status), diagnostic};
      }
      state->reloaded.emplace(*reloaded.document);
      state->outcomes.stage = Stage::kVerify;
      VerifyRepair(session, request.targets);
      TypedAttributes(session, state->repair->Source().View(),
                      state->repair->Candidate().View(), false,
                      state->execution.typed_properties,
                      Code::kTypedProperties);
      Passed(*state, Check::kTypedProperties);
      WireSchema(session, *state->reloaded, state->execution.wire_schema,
                 Code::kWireSchema);
      Passed(*state, Check::kWireSchema);
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
        const ic::PlyFilePublicationOptions options{ic::PlyFileReceiptProvider{
            std::string_view(state->receipt_name.data(),
                             state->receipt_name.size()),
            Provide, &provider}};
        state->outcomes.stage = Stage::kPublication;
        const auto publication = ic::PublishPlyBundle(
            {state->source, state->repair->Candidate(), *state->written},
            request.publication->destination, options, limits.publication,
            work.publication, stop);
        if (provider.unexpected) std::rethrow_exception(provider.unexpected);
        state->publication.emplace(publication);
        if (publication.outcome == ic::PlyFilePublicationOutcome::kPublished) {
          if (testing.published) testing.published(publication, testing.state);
          static_assert(
              std::is_nothrow_copy_constructible_v<ic::PlyPublishedBundle>);
          state->published.emplace(*publication.bundle);
          Passed(*state, Check::kStagedPhysicalMembers);
          state->status = Status::kSuccess;
          state->outcomes.publication = Publication::kPublished;
          state->outcomes.stage = Stage::kComplete;
          Compute(*state);
          state->report_available = true;
        } else {
          state->outcomes.publication =
              publication.outcome == ic::PlyFilePublicationOutcome::kCanceled
                  ? Publication::kCanceled
                  : Publication::kFailed;
          if (!provider.own_failure) {
            state->status =
                publication.outcome == ic::PlyFilePublicationOutcome::kCanceled
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
    PlyWorkflowDiagnostic diagnostic;
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
    result.outcomes.execution = PlyWorkflowExecutionOutcome::kCanceled;
    result.outcomes.exit = PlyWorkflowExitMeaning::kCanceled;
  }
  return result;
}
}  // namespace meshvale::repair::ply_workflow_detail

namespace meshvale::repair {
PlyDuplicateFileWorkflowRecord::PlyDuplicateFileWorkflowRecord(
    std::shared_ptr<const ply_workflow_detail::State> state) noexcept
    : state_(std::move(state)) {}
PlyDuplicateFileWorkflowRecord::PlyDuplicateFileWorkflowRecord(
    const PlyDuplicateFileWorkflowRecord&) noexcept = default;
PlyDuplicateFileWorkflowRecord& PlyDuplicateFileWorkflowRecord::operator=(
    const PlyDuplicateFileWorkflowRecord&) noexcept = default;
PlyDuplicateFileWorkflowRecord::~PlyDuplicateFileWorkflowRecord() noexcept =
    default;
const interchange::PlyFileAsset& PlyDuplicateFileWorkflowRecord::Source()
    const noexcept {
  return state_->source;
}
const MaterializedDuplicateRepair* PlyDuplicateFileWorkflowRecord::RepairFacts()
    const noexcept {
  return state_->repair ? &*state_->repair : nullptr;
}
const interchange::PlyWriteFacts* PlyDuplicateFileWorkflowRecord::WriteFacts()
    const noexcept {
  return state_->written ? &state_->written->Facts() : nullptr;
}
const std::optional<interchange::PlyOutput>&
PlyDuplicateFileWorkflowRecord::Output() const noexcept {
  return state_->memory_verified ? state_->written : state_->absent_output;
}
const std::optional<interchange::PlyDocument>&
PlyDuplicateFileWorkflowRecord::Reload() const noexcept {
  return state_->memory_verified ? state_->reloaded : state_->absent_reload;
}
const std::optional<interchange::PlyPublishedBundle>&
PlyDuplicateFileWorkflowRecord::Published() const noexcept {
  return state_->published;
}
PlyWorkflowOutcomes PlyDuplicateFileWorkflowRecord::Outcomes() const noexcept {
  return state_->outcomes;
}
std::span<const PlyWorkflowCoverage> PlyDuplicateFileWorkflowRecord::Coverage()
    const noexcept {
  return state_->coverage;
}
std::span<const std::byte> PlyDuplicateFileWorkflowRecord::ReportJson()
    const noexcept {
  return state_->report_available && state_->report
             ? state_->report->Bytes()
             : std::span<const std::byte>{};
}
std::optional<std::string_view>
PlyDuplicateFileWorkflowRecord::PublishedReportPath() const noexcept {
  if (!state_->published) return {};
  return std::string_view(state_->receipt_name.data(),
                          state_->receipt_name.size());
}
std::optional<std::string_view> PlyDuplicateFileWorkflowRecord::InputSha256()
    const noexcept {
  if (!state_->input_hash_available) return {};
  return std::string_view(state_->input_hash.data(), state_->input_hash.size());
}
PlyWorkflowRequestedBytes PlyDuplicateFileWorkflowRecord::NewRequestedBytes()
    const noexcept {
  return state_->Bytes();
}
PlyWorkflowExecution PlyDuplicateFileWorkflowRecord::Execution()
    const noexcept {
  return state_->execution;
}
PlyDuplicateFileWorkflowResult RunPlyDuplicateFileWorkflow(
    const PlyDuplicateFileWorkflowRequest& request,
    const PlyDuplicateFileWorkflowLimits& limits,
    const PlyDuplicateFileWorkflowWorkPolicy& work, std::stop_token stop) {
  return ply_workflow_detail::RunPlyDuplicateFileWorkflowForTest(
      request, limits, work, stop, {});
}
}  // namespace meshvale::repair
