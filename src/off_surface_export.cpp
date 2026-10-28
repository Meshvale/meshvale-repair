// SPDX-License-Identifier: Apache-2.0
#include "meshvale/repair/off_surface_export.h"

#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/geometry/triangulation.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_workflow.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
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
#include "off_surface_export_internal.h"
#include "off_workflow_test_internal.h"

namespace meshvale::repair::off_surface_export_detail {
namespace geo = geometry;
namespace ic = interchange;
namespace allocation = materialized_duplicates_detail;
using Stage = OffGlbSurfaceStage;
using Status = OffGlbSurfaceStatus;
using Code = OffGlbSurfaceCode;

namespace {
constexpr std::uint64_t kReportStatsTailBytes = 256;
constexpr std::uint64_t kReportInputIdBytes = 256;
constexpr std::uint64_t kMinimumPolygonCorners = 3;
constexpr std::uint64_t kMaximumTriangulationCorners = 4096;
constexpr std::uint64_t kMaximumJsonEscapeBytes = 6;
constexpr std::size_t kNumberedDeviceLength = 4;
constexpr std::array<std::string_view, 4> kReservedDevices{"con", "prn", "aux",
                                                           "nul"};
constexpr std::string_view kComDevicePrefix = "com";
constexpr std::string_view kLptDevicePrefix = "lpt";
constexpr std::array<std::string_view, 3> kPayloadPaths{
    "surface.glb", "surface.json", "surface.bin"};
struct Failure {
  Status status;
  OffGlbSurfaceDiagnostic diagnostic;
};
[[noreturn]] void Invalid(Code code) {
  throw Failure{Status::kInvalidConfiguration, {Stage::kConfiguration, code}};
}
std::size_t Host(std::uint64_t n) {
  if (n > static_cast<std::uint64_t>(
              std::numeric_limits<std::ptrdiff_t>::max()) ||
      n > std::numeric_limits<std::size_t>::max())
    Invalid(Code::kConfigurationExtent);
  return static_cast<std::size_t>(n);
}
void Positive(std::uint64_t n) {
  if (!n) Invalid(Code::kRequiredFieldZero);
  (void)Host(n);
}
void Positives(std::initializer_list<std::uint64_t> values) {
  for (auto value : values) Positive(value);
}
std::uint64_t Add(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    Invalid(Code::kConfigurationExtent);
  (void)Host(a + b);
  return a + b;
}
void Extent(bool valid) {
  if (!valid) Invalid(Code::kConfigurationExtent);
}
void Validate(const geo::MeshMaterializationLimits& c) {
  Positives({c.max_vertices, c.max_faces, c.max_corners, c.max_face_offsets,
             c.max_attributes, c.max_attribute_scalars, c.max_attribute_offsets,
             c.max_presence_bytes, c.max_metadata_entries,
             c.max_descriptor_bytes, c.max_new_requested_bytes});
}
void Validate(const geo::MeshInspectionLimits& c) {
  Positives({c.max_issues, c.max_new_requested_bytes});
}
void Validate(const geo::MeshMaterializationWorkPolicy& w) {
  Positives({w.parallel_min_bytes, w.partition_bytes,
             w.cancellation_block_scalars, w.max_jobs});
}
void Validate(const OffByteWorkPolicy& w) {
  Positives({w.parallel_min_bytes, w.partition_bytes,
             w.cancellation_block_bytes, w.max_jobs});
}
void Validate(const ic::OffWorkPolicy& w) {
  Positives({w.cancellation_block_bytes, w.cancellation_block_tokens,
             w.cancellation_block_records});
  Validate(w.geometry);
}
void Validate(const ic::OffWriteLimits& c) {
  Positives({c.max_output_bytes, c.max_vertices, c.max_faces, c.max_corners,
             c.max_corners_per_face, c.max_line_bytes, c.max_token_bytes,
             c.max_codec_new_requested_bytes, c.max_total_new_requested_bytes});
  Validate(c.inspection);
}
void Validate(const ic::OffReadLimits& c) {
  Positives({c.max_source_bytes, c.max_vertices, c.max_faces, c.max_corners,
             c.max_corners_per_face, c.max_line_bytes, c.max_token_bytes,
             c.max_comment_bytes, c.max_codec_new_requested_bytes,
             c.max_total_new_requested_bytes});
  Validate(c.materialization);
  Validate(c.inspection);
}
void Validate(const OffDuplicateFileWorkflowLimits& c,
              const OffDuplicateFileWorkflowWorkPolicy& w) {
  Positives({c.max_wrapper_new_requested_bytes, c.max_input_id_bytes,
             c.max_source_protection_bytes, c.max_report_bytes,
             c.max_hash_input_bytes, c.max_verification_vertices,
             c.max_verification_faces, c.max_verification_corners,
             c.repair.max_targets, c.repair.max_row_local_declarations,
             c.repair.max_declaration_name_bytes,
             c.repair.max_comparison_steps_per_target,
             c.repair.max_new_requested_bytes});
  Validate(c.repair.inspection);
  Validate(c.repair.selection);
  Validate(w.repair);
  Validate(c.writing);
  Validate(w.writing);
  Validate(c.reload);
  Validate(w.reload);
  const auto& f = c.publication.files;
  const auto& fw = w.publication.files;
  Positives({f.max_entry_bytes, f.max_receipt_bytes, f.max_total_file_bytes,
             f.max_entry_name_bytes, f.max_receipt_name_bytes,
             f.max_native_path_code_units, f.max_file_new_requested_bytes,
             fw.io_block_bytes, fw.parallel_min_bytes, fw.partition_bytes,
             fw.cancellation_block_bytes, fw.max_jobs});
  Extent(
      f.max_native_path_code_units <=
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          sizeof(std::filesystem::path::value_type));
  Validate(c.publication.expected_inspection);
  Validate(c.publication.reload);
  Validate(w.publication.expected_inspection);
  Validate(w.publication.reload);
  Validate(w.verification);
  Validate(w.hashing);
  Validate(w.report);
}
void Validate(const geo::ChargedTriangulationLimits& c,
              const geo::MeshMaterializationWorkPolicy& w) {
  Validate(c.source);
  Validate(c.output);
  Validate(c.source_inspection);
  Validate(c.output_inspection);
  Positives({c.max_map_entries, c.max_core_new_requested_bytes,
             c.max_bridge_new_requested_bytes});
  Validate(w);
}
void Validate(const ic::SurfaceCountLimits& c) {
  Positives({c.max_vertices,
             c.max_faces,
             c.max_corners,
             c.max_face_offsets,
             c.max_attributes,
             c.max_attribute_scalars,
             c.max_attribute_bytes,
             c.max_attribute_offsets,
             c.max_presence_bytes,
             c.max_metadata_entries,
             c.max_descriptor_bytes,
             c.max_wire_primitives,
             c.max_wire_vertices,
             c.max_wire_corners,
             c.max_map_entries,
             c.max_members,
             c.max_member_bytes,
             c.max_total_member_bytes,
             c.max_resources,
             c.max_resource_bytes,
             c.max_json_bytes,
             c.max_binary_bytes,
             c.max_capsule_json_bytes,
             c.max_capsule_binary_bytes,
             c.max_material_bindings,
             c.max_numeric_token_bytes,
             c.max_json_nodes,
             c.max_json_depth,
             c.max_numeric_integer_bits,
             c.max_coincidence_comparisons,
             c.max_relationship_steps});
  Extent(c.max_json_depth <=
         static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
}
void Validate(const ic::GlbLimits& c, const ic::GlbWorkPolicy& w) {
  Positives({c.max_container_bytes, c.max_chunks, c.max_requested_bytes,
             w.parallel_min_bytes, w.partition_bytes});
}
void Validate(const ic::GltfReadLimits& c) {
  Positives({c.max_json_bytes, c.max_resource_bytes,
             c.max_owned_source_resource_bytes, c.max_document_nodes,
             c.max_document_depth, c.max_decoded_string_bytes,
             c.max_numeric_token_bytes, c.max_decimal_coefficient_digits,
             c.max_decimal_exponent_digits, c.max_table_rows, c.max_graph_edges,
             c.max_instances, c.max_accessor_elements, c.max_projection_bytes});
  Extent(c.max_document_depth <=
         static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
}
void Validate(const ic::SurfaceEncodeLimits& c,
              const ic::SurfaceWorkPolicy& w) {
  Validate(c.counts);
  Positives({c.max_new_requested_bytes, c.max_total_requested_allowance});
  Validate(w.surface);
  if (const auto* d = std::get_if<ic::GltfSurfaceEncodeLimits>(&c.delegates))
    Validate(d->container, w.container);
  else
    Positives({w.container.parallel_min_bytes, w.container.partition_bytes});
}
void Validate(const ic::SurfaceInspectLimits& c,
              const ic::SurfaceWorkPolicy& w) {
  Validate(c.counts);
  Positives({c.max_new_requested_bytes, c.max_total_requested_allowance});
  Validate(w.surface);
  if (const auto* d = std::get_if<ic::GltfSurfaceInspectLimits>(&c.delegates)) {
    Validate(d->read.source);
    Positives({d->read.max_new_requested_bytes, d->extraction.max_components,
               d->extraction.max_triangle_corners,
               d->extraction.max_output_requested_bytes,
               w.gltf.parallel_min_resource_bytes,
               w.gltf.resource_partition_bytes,
               w.gltf.parallel_min_channel_components,
               w.gltf.channel_partition_elements});
    Validate(d->container, w.container);
  } else {
    Positives({w.gltf.parallel_min_resource_bytes,
               w.gltf.resource_partition_bytes,
               w.gltf.parallel_min_channel_components,
               w.gltf.channel_partition_elements,
               w.container.parallel_min_bytes, w.container.partition_bytes});
  }
}
void Validate(const ic::SurfaceFilePublicationLimits& c,
              const ic::SurfaceFilePublicationWorkPolicy& w) {
  const auto& f = c.files;
  const auto& fw = w.files;
  Positives({f.max_payload_members, f.max_payload_member_bytes,
             f.max_total_payload_bytes, f.max_receipt_bytes,
             f.max_caller_report_bytes, f.max_path_bytes,
             f.max_total_path_bytes, f.max_native_path_code_units,
             f.max_inventory_entries, f.max_stage_name_attempts,
             f.max_new_requested_bytes, fw.io_block_bytes,
             fw.parallel_min_bytes, fw.partition_bytes,
             fw.cancellation_block_bytes, fw.max_jobs});
  Extent(
      f.max_caller_report_bytes <=
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          kMaximumJsonEscapeBytes);
  Extent(
      f.max_native_path_code_units <=
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
          sizeof(std::filesystem::path::value_type));
  Validate(c.inspection, w.inspection);
  Positive(c.max_total_requested_allowance);
}
void Allowance(const ic::OffReadLimits& c) {
  Extent(Add(Add(c.max_codec_new_requested_bytes,
                 c.materialization.max_new_requested_bytes),
             c.inspection.max_new_requested_bytes) <=
         c.max_total_new_requested_bytes);
}
void Allowance(const ic::SurfaceEncodeLimits& c) {
  if (const auto* d = std::get_if<ic::GltfSurfaceEncodeLimits>(&c.delegates))
    Extent(Add(c.max_new_requested_bytes, d->container.max_requested_bytes) <=
           c.max_total_requested_allowance);
}
void Allowance(const ic::SurfaceInspectLimits& c) {
  if (const auto* d = std::get_if<ic::GltfSurfaceInspectLimits>(&c.delegates))
    Extent(
        Add(Add(Add(c.max_new_requested_bytes, d->read.max_new_requested_bytes),
                d->extraction.max_output_requested_bytes),
            d->container.max_requested_bytes) <=
        c.max_total_requested_allowance);
}
bool Zero(const geo::MeshMaterializationWorkPolicy& w) noexcept {
  return !w.parallel_min_bytes && !w.partition_bytes &&
         !w.cancellation_block_scalars && !w.max_jobs;
}
bool Zero(const ic::GltfDirectTranslationWorkPolicy& w) noexcept {
  return !w.parallel_min_keys && !w.partition_keys &&
         !w.cancellation_block_keys && !w.max_jobs;
}
bool Zero(const ic::GltfDirectTranslationLimits& c) noexcept {
  return !c.max_node_rows && !c.max_mesh_users && !c.max_channel_references &&
         !c.max_distinct_samplers && !c.max_keys_per_sampler &&
         !c.max_total_keys && !c.max_relationship_steps &&
         !c.max_metadata_bytes && !c.max_decoded_bytes &&
         !c.max_new_requested_bytes;
}
bool Zero(const ic::GltfWorkPolicy& w) noexcept {
  return !w.parallel_min_resource_bytes && !w.resource_partition_bytes &&
         !w.parallel_min_channel_components && !w.channel_partition_elements &&
         Zero(w.direct_translation);
}
bool Zero(const ic::StlWorkPolicy& w) noexcept {
  return !w.cancellation_block_bytes && !w.cancellation_block_tokens &&
         !w.cancellation_block_facets && Zero(w.geometry);
}
void Policy(const ic::SurfaceInspectLimits& c, const ic::SurfaceWorkPolicy& w) {
  const auto* d = std::get_if<ic::GltfSurfaceInspectLimits>(&c.delegates);
  if (!d || !Zero(d->extraction.direct_translation) ||
      !Zero(w.gltf.direct_translation) || !Zero(w.stl))
    Invalid(Code::kPolicy);
}
char Lower(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool EqualFold(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (Lower(a[i]) != Lower(b[i])) return false;
  return true;
}
bool Alias(std::string_view a, std::string_view b) noexcept {
  const auto n = std::min(a.size(), b.size());
  if (!EqualFold(a.substr(0, n), b.substr(0, n))) return false;
  return a.size() == b.size() ||
         (a.size() < b.size() ? b[n] == '/' : a[n] == '/');
}
bool Alnum(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}
void ReceiptPath(std::string_view path, std::uint64_t maximum) {
  if (path.empty() || path.size() > maximum) Invalid(Code::kReceiptPath);
  std::size_t at = 0;
  while (at < path.size()) {
    auto end = path.find('/', at);
    if (end == path.npos) end = path.size();
    const auto part = path.substr(at, end - at);
    if (part.empty() || !Alnum(part.front()) || part.back() == '.')
      Invalid(Code::kReceiptPath);
    for (auto c : part)
      if (!Alnum(c) && c != '.' && c != '_' && c != '-')
        Invalid(Code::kReceiptPath);
    const auto stem = part.substr(0, part.find('.'));
    for (auto device : kReservedDevices)
      if (EqualFold(stem, device)) Invalid(Code::kReceiptPath);
    if (stem.size() == kNumberedDeviceLength &&
        (EqualFold(stem.substr(0, kNumberedDeviceLength - 1),
                   kComDevicePrefix) ||
         EqualFold(stem.substr(0, kNumberedDeviceLength - 1),
                   kLptDevicePrefix)) &&
        stem.back() >= '1' && stem.back() <= '9')
      Invalid(Code::kReceiptPath);
    at = end + 1;
    if (at == path.size()) Invalid(Code::kReceiptPath);
  }
  for (auto member : kPayloadPaths)
    if (Alias(member, path)) Invalid(Code::kReceiptPath);
}
void Validate(const OffGlbSurfaceWorkflowRequest& r,
              const OffGlbSurfaceWorkflowLimits& c,
              const OffGlbSurfaceWorkflowWorkPolicy& w) {
  Positive(c.max_wrapper_new_requested_bytes);
  Validate(c.off, w.off);
  Validate(c.triangulation, w.triangulation);
  Validate(c.encoding, w.encoding);
  Validate(c.inspection, w.inspection);
  Validate(c.publication, w.publication);
  // Allowance arithmetic follows every required active leaf/host check.
  Extent(Add(c.off.writing.max_codec_new_requested_bytes,
             c.off.writing.inspection.max_new_requested_bytes) <=
         c.off.writing.max_total_new_requested_bytes);
  Allowance(c.off.reload);
  (void)Add(c.off.publication.files.max_entry_bytes,
            c.off.publication.files.max_receipt_bytes);
  Allowance(c.off.publication.reload);
  (void)Add(c.off.max_report_bytes, kReportStatsTailBytes);
  Allowance(c.encoding);
  Allowance(c.inspection);
  Allowance(c.publication.inspection);
  Extent(Add(c.publication.files.max_new_requested_bytes,
             c.publication.inspection.max_total_requested_allowance) <=
         c.publication.max_total_requested_allowance);
  if (r.text != ic::OffTextPolicy::kCanonicalAscii ||
      r.edge_count != ic::OffEdgeCountPolicy::kUnknownZero ||
      !std::holds_alternative<ic::GltfSurfaceEncodeLimits>(
          c.encoding.delegates) ||
      !Zero(w.encoding.gltf) || !Zero(w.encoding.stl))
    Invalid(Code::kPolicy);
  Policy(c.inspection, w.inspection);
  Policy(c.publication.inspection, w.publication.inspection);
  if (r.triangulation_options.max_corners_per_face < kMinimumPolygonCorners ||
      r.triangulation_options.max_corners_per_face >
          kMaximumTriangulationCorners)
    Invalid(Code::kPolicy);
  (void)Host(r.triangulation_options.minimum_parallel_faces);
  if (r.coordinates != ic::SurfaceCoordinates::kDeclaredMetersRightHandedYUp)
    Invalid(Code::kCoordinates);
  if ((r.numeric.positions != ic::SurfaceNarrowing::kExact32 &&
       r.numeric.positions != ic::SurfaceNarrowing::kBoundedRne32) ||
      !std::isfinite(r.numeric.max_position_linf_error) ||
      r.numeric.max_position_linf_error < 0 ||
      (r.numeric.positions == ic::SurfaceNarrowing::kExact32 &&
       r.numeric.max_position_linf_error != 0) ||
      !r.numeric.max_uv_component_errors.empty())
    Invalid(Code::kNumericPolicy);
  if (!r.acknowledge_triangle_conversion || !r.acknowledge_corner_expansion)
    Invalid(Code::kProjectionAcknowledgment);
  if (r.input_id.empty() ||
      r.input_id.size() >
          std::min(c.off.max_input_id_bytes, kReportInputIdBytes))
    Invalid(Code::kInputIdentity);
  for (std::size_t i = 0; i < r.input_id.size(); ++i) {
    const auto ch = r.input_id[i];
    if (!Alnum(ch) &&
        (i == 0 || (ch != '_' && ch != '.' && ch != ':' && ch != '-')))
      Invalid(Code::kInputIdentity);
  }
  ReceiptPath(r.receipt_path, c.publication.files.max_path_bytes);
}
}  // namespace

struct State {
  // Keep these inert lifetimes and the member plan until the record's last
  // release. Returning after P can then release only nonfinal local aliases.
  allocation::AllocationContext allocation;
  geo::ExecutionContext context;
  std::stop_token stop;
  ic::OffFileAsset source;
  OffGlbSurfaceFacts facts;
  std::vector<ic::SuppliedSurfaceMember,
              allocation::Allocator<ic::SuppliedSurfaceMember>>
      members;

  State(const allocation::AllocationContext& a, const geo::ExecutionContext& c,
        std::stop_token token, const ic::OffFileAsset& input)
      : allocation(a),
        context(c),
        stop(token),
        source(input),
        members(allocation::Allocator<ic::SuppliedSurfaceMember>(a)) {}
};
struct Access {
  static OffGlbSurfaceWorkflowRecord Make(
      std::shared_ptr<const State> state) noexcept {
    return OffGlbSurfaceWorkflowRecord(std::move(state));
  }
};
namespace {
template <class T>
void Retain(std::optional<T>& slot, const T& value) noexcept {
  static_assert(std::is_nothrow_copy_constructible_v<T>);
  static_assert(std::is_nothrow_destructible_v<T>);
  slot.emplace(value);
}
Status Map(OffWorkflowStatus s) noexcept {
  switch (s) {
    case OffWorkflowStatus::kSuccess:
      return Status::kSuccess;
    case OffWorkflowStatus::kLimitExceeded:
      return Status::kLimitExceeded;
    case OffWorkflowStatus::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case OffWorkflowStatus::kCanceled:
      return Status::kCanceled;
    case OffWorkflowStatus::kRejected:
      return Status::kRejected;
    default:
      return Status::kDelegatedFailure;
  }
}
Status Map(const OffDuplicateFileWorkflowResult& result) noexcept {
  const auto mapped = Map(result.status);
  if (mapped != Status::kDelegatedFailure) return mapped;
  if (const auto* off =
          std::get_if<ic::OffStatus>(&result.diagnostic.delegated_status)) {
    if (*off == ic::OffStatus::kUnsupportedVariant) return Status::kUnsupported;
  }
  if (const auto* repair = std::get_if<MaterializedDuplicateDiagnostic>(
          &result.diagnostic.delegated)) {
    if (repair->geometry_status ==
        geo::MaterializationStatus::kUnsupportedAllocationProfile)
      return Status::kUnsupported;
  }
  return mapped;
}
Status Map(geo::ChargedTriangulationStatus s) noexcept {
  switch (s) {
    case geo::ChargedTriangulationStatus::kAccepted:
      return Status::kSuccess;
    case geo::ChargedTriangulationStatus::kLimitExceeded:
      return Status::kLimitExceeded;
    case geo::ChargedTriangulationStatus::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case geo::ChargedTriangulationStatus::kCanceled:
      return Status::kCanceled;
    case geo::ChargedTriangulationStatus::kUnsupportedAllocationProfile:
      return Status::kUnsupported;
    default:
      return Status::kDelegatedFailure;
  }
}
Status Map(ic::SurfaceStatus s) noexcept {
  switch (s) {
    case ic::SurfaceStatus::kSuccess:
      return Status::kSuccess;
    case ic::SurfaceStatus::kLimitExceeded:
      return Status::kLimitExceeded;
    case ic::SurfaceStatus::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case ic::SurfaceStatus::kCanceled:
      return Status::kCanceled;
    case ic::SurfaceStatus::kUnsupported:
      return Status::kUnsupported;
    default:
      return Status::kDelegatedFailure;
  }
}
Status Map(ic::SurfaceFileStatus s) noexcept {
  switch (s) {
    case ic::SurfaceFileStatus::kSuccess:
      return Status::kSuccess;
    case ic::SurfaceFileStatus::kLimitExceeded:
      return Status::kLimitExceeded;
    case ic::SurfaceFileStatus::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case ic::SurfaceFileStatus::kCanceled:
      return Status::kCanceled;
    case ic::SurfaceFileStatus::kUnsupportedProfile:
      return Status::kUnsupported;
    default:
      return Status::kDelegatedFailure;
  }
}
void Require(bool success, Status status, Stage stage, Code code) {
  if (!success)
    throw Failure{
        status == Status::kSuccess ? Status::kDelegatedFailure : status,
        {stage, code}};
}
void Checkpoint(const WorkflowTestOptions& options, State& state, Stage stage) {
  if (options.checkpoint) options.checkpoint(stage, options.state);
  state.allocation.Checkpoint();
}
std::span<const std::byte> Provide(const ic::SurfaceFileVerifiedContent&,
                                   void* opaque) noexcept {
  const auto& state = *static_cast<const State*>(opaque);
  return state.facts.off->record->ReportJson();
}
}  // namespace

OffGlbSurfaceWorkflowResult RunOffGlbSurfaceWorkflowForTest(
    const OffGlbSurfaceWorkflowRequest& request,
    const OffGlbSurfaceWorkflowLimits& limits,
    const OffGlbSurfaceWorkflowWorkPolicy& work, std::stop_token stop,
    const WorkflowTestOptions& testing) {
  OffGlbSurfaceWorkflowResult result;
  std::shared_ptr<State> state;
  Stage stage = Stage::kConfiguration;
  try {
    Validate(request, limits, work);
    if (stop.stop_requested())
      throw Failure{Status::kCanceled, {stage, Code::kStopRequested}};
    const auto context =
        request.source.Document().Mesh().ShareExecutionContext();
    const auto allocator =
        allocation::AllocationContext(context, stop, testing.allocation_backend)
            .WithRequestLimit(Host(limits.max_wrapper_new_requested_bytes));
    state =
        std::allocate_shared<State>(allocation::Allocator<State>(allocator),
                                    allocator, context, stop, request.source);
    // Construct the final public strong slot before entering any delegate.
    result.record.emplace(Access::Make(state));
    stage = Stage::kOffWorkflow;
    const OffDuplicateFileWorkflowRequest off_request{
        state->source, request.input_id,   request.targets,
        request.text,  request.edge_count, std::nullopt};
    if (testing.off) {
      const auto off = off_workflow_detail::RunOffDuplicateFileWorkflowForTest(
          off_request, limits.off, work.off, stop, *testing.off);
      Retain(state->facts.off, off);
    } else {
      const auto off =
          RunOffDuplicateFileWorkflow(off_request, limits.off, work.off, stop);
      Retain(state->facts.off, off);
    }
    const auto& off = *state->facts.off;
    Require(off.status == OffWorkflowStatus::kSuccess && off.record, Map(off),
            stage, Code::kOffWorkflow);
    const auto& off_record = *off.record;
    const auto* repair = off_record.RepairFacts();
    const auto outcomes = off_record.Outcomes();
    Require(outcomes.profile == OffWorkflowProfileOutcome::kPassed &&
                outcomes.publication ==
                    OffWorkflowPublicationOutcome::kNotRequested &&
                off_record.Output() && off_record.Reload() && repair &&
                (repair->Outcome() == CandidateOutcome::accepted ||
                 repair->Outcome() == CandidateOutcome::unchanged),
            Status::kDelegatedFailure, stage, Code::kOffWorkflow);
    Checkpoint(testing, *state, stage);
    stage = Stage::kTriangulation;
    {
      const auto triangulation = geo::TriangulateCharged(
          repair->Candidate(), request.triangulation_options,
          limits.triangulation, work.triangulation, stop);
      Retain(state->facts.triangulation, triangulation);
    }
    const auto& triangulation = *state->facts.triangulation;
    Require(
        triangulation.status == geo::ChargedTriangulationStatus::kAccepted &&
            triangulation.triangulation,
        Map(triangulation.status), stage, Code::kTriangulation);
    Checkpoint(testing, *state, stage);
    stage = Stage::kEncoding;
    {
      const ic::GltfSurfaceOptions destination{
          ic::SurfaceEncoding::kGlb, request.coordinates, {}, nullptr, {}, {}};
      const ic::SurfaceEncodeRequest encode_request{
          repair->CandidateInspection(),
          *triangulation.triangulation,
          ic::SurfaceRepresentation::kAnnotatedBundleV1,
          request.acknowledge_triangle_conversion,
          request.acknowledge_corner_expansion,
          request.acknowledge_unused_vertices,
          {},
          request.numeric,
          destination};
      const auto encoding = ic::EncodeSurface(encode_request, limits.encoding,
                                              work.encoding, stop);
      Retain(state->facts.encoding, encoding);
    }
    const auto& encoding = *state->facts.encoding;
    Require(encoding.status == ic::SurfaceStatus::kSuccess && encoding.output,
            Map(encoding.status), stage, Code::kEncoding);
    Checkpoint(testing, *state, stage);
    stage = Stage::kInspection;
    const auto members = encoding.output->Members();
    state->members.reserve(members.size());
    // This bounded descriptor-only plan borrows the already retained E bytes;
    // the substantive copies/comparisons run in the genuine V worker phases.
    for (const auto& member : members) {
      state->allocation.Checkpoint();
      state->members.push_back({member.path, member.bytes});
    }
    {
      const auto inspection =
          ic::InspectSurfaceOutput(*encoding.output, state->members,
                                   limits.inspection, work.inspection, stop);
      Retain(state->facts.inspection, inspection);
    }
    const auto& inspection = *state->facts.inspection;
    Require(inspection.status == ic::SurfaceStatus::kSuccess &&
                inspection.inspection,
            Map(inspection.status), stage, Code::kInspection);
    Checkpoint(testing, *state, stage);
    stage = Stage::kPublication;
    const ic::SurfaceFilePublicationOptions publication_options{
        request.receipt_path, {Provide, state.get()}};
    {
      const auto publication =
          testing.publication_invoker
              ? testing.publication_invoker(
                    *inspection.inspection, request.destination,
                    publication_options, limits.publication, work.publication,
                    stop, testing.publication_state)
              : ic::PublishSurfaceBundle(
                    *inspection.inspection, request.destination,
                    publication_options, limits.publication, work.publication,
                    stop);
      Retain(state->facts.publication, publication);
    }
    const auto& publication = *state->facts.publication;
    Require(
        publication.outcome == ic::SurfaceFilePublicationOutcome::kPublished &&
            publication.bundle,
        Map(publication.diagnostic.status), stage, Code::kPublication);
    result.status = Status::kSuccess;
    result.diagnostic = {Stage::kComplete, Code::kNone};
  } catch (const Failure& failure) {
    result.status = failure.status;
    result.diagnostic = failure.diagnostic;
  } catch (const allocation::AdmissionFailure& failure) {
    switch (failure.Code()) {
      case allocation::AdmissionCode::kBudgetExceeded:
        result.status = Status::kBudgetExceeded;
        result.diagnostic = {stage, Code::kRequestBudget};
        break;
      case allocation::AdmissionCode::kCanceled:
        result.status = Status::kCanceled;
        result.diagnostic = {stage, Code::kStopRequested};
        break;
      case allocation::AdmissionCode::kSizeLimit:
        result.status = Status::kLimitExceeded;
        result.diagnostic = {stage, Code::kConfigurationExtent};
        break;
      case allocation::AdmissionCode::kInvalidObject:
        throw;
    }
  }
  return result;
}
}  // namespace meshvale::repair::off_surface_export_detail

namespace meshvale::repair {
OffGlbSurfaceWorkflowRecord::OffGlbSurfaceWorkflowRecord(
    std::shared_ptr<const off_surface_export_detail::State> state) noexcept
    : state_(std::move(state)) {}
OffGlbSurfaceWorkflowRecord::OffGlbSurfaceWorkflowRecord(
    const OffGlbSurfaceWorkflowRecord&) noexcept = default;
OffGlbSurfaceWorkflowRecord& OffGlbSurfaceWorkflowRecord::operator=(
    const OffGlbSurfaceWorkflowRecord&) noexcept = default;
OffGlbSurfaceWorkflowRecord::~OffGlbSurfaceWorkflowRecord() noexcept = default;
const interchange::OffFileAsset& OffGlbSurfaceWorkflowRecord::Source()
    const noexcept {
  return state_->source;
}
const OffGlbSurfaceFacts& OffGlbSurfaceWorkflowRecord::Facts() const noexcept {
  return state_->facts;
}
std::uint64_t OffGlbSurfaceWorkflowRecord::NewRequestedBytes() const noexcept {
  return state_->allocation.OperationActiveBytes();
}
OffGlbSurfaceWorkflowResult RunOffGlbSurfaceWorkflow(
    const OffGlbSurfaceWorkflowRequest& request,
    const OffGlbSurfaceWorkflowLimits& limits,
    const OffGlbSurfaceWorkflowWorkPolicy& work, std::stop_token stop) {
  return off_surface_export_detail::RunOffGlbSurfaceWorkflowForTest(
      request, limits, work, stop, {});
}
}  // namespace meshvale::repair
