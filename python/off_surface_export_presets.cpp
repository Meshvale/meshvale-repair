// SPDX-License-Identifier: Apache-2.0
#include "off_surface_export_presets.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>
namespace meshvale::repair::python {
OffGlbPreset MakeOffGlbPreset(std::string_view name) {
  if (std::cmp_less(std::numeric_limits<std::size_t>::max(),
                    std::numeric_limits<std::int64_t>::max()))
    throw std::invalid_argument("OFF GLB presets require a 64-bit host");
  OffGlbPreset result{};
  if (name == kBalancedOffGlbPreset) {
    // Complete finite configuration: docs/off-surface-export-presets.json.
    result.execution.worker_budget = 4ULL;
    result.execution.minimum_parallel_vertices = 65536ULL;
    result.execution.tracked_payload_budget_bytes = 2147483648ULL;
    result.capture_limits.files.max_entry_bytes = 67108864ULL;
    result.capture_limits.files.max_receipt_bytes = 16777216ULL;
    result.capture_limits.files.max_total_file_bytes = 83886080ULL;
    result.capture_limits.files.max_entry_name_bytes = 1024ULL;
    result.capture_limits.files.max_receipt_name_bytes = 1024ULL;
    result.capture_limits.files.max_native_path_code_units = 4096ULL;
    result.capture_limits.files.max_file_new_requested_bytes = 134217728ULL;
    result.capture_limits.off.max_source_bytes = 67108864ULL;
    result.capture_limits.off.max_vertices = 1000000ULL;
    result.capture_limits.off.max_faces = 1000000ULL;
    result.capture_limits.off.max_corners = 3000000ULL;
    result.capture_limits.off.max_corners_per_face = 65536ULL;
    result.capture_limits.off.max_line_bytes = 16777216ULL;
    result.capture_limits.off.max_token_bytes = 256ULL;
    result.capture_limits.off.max_comment_bytes = 1048576ULL;
    result.capture_limits.off.max_codec_new_requested_bytes = 134217728ULL;
    result.capture_limits.off.max_total_new_requested_bytes = 209715200ULL;
    result.capture_limits.off.materialization.max_vertices = 1000000ULL;
    result.capture_limits.off.materialization.max_faces = 1000000ULL;
    result.capture_limits.off.materialization.max_corners = 3000000ULL;
    result.capture_limits.off.materialization.max_face_offsets = 1000001ULL;
    result.capture_limits.off.materialization.max_attributes = 1ULL;
    result.capture_limits.off.materialization.max_attribute_scalars = 1ULL;
    result.capture_limits.off.materialization.max_attribute_offsets = 1ULL;
    result.capture_limits.off.materialization.max_presence_bytes = 1ULL;
    result.capture_limits.off.materialization.max_metadata_entries = 1ULL;
    result.capture_limits.off.materialization.max_descriptor_bytes = 1ULL;
    result.capture_limits.off.materialization.max_new_requested_bytes =
        67108864ULL;
    result.capture_limits.off.inspection.max_issues = 1024ULL;
    result.capture_limits.off.inspection.max_new_requested_bytes = 8388608ULL;
    result.capture_work.files.io_block_bytes = 65536ULL;
    result.capture_work.files.parallel_min_bytes = 262144ULL;
    result.capture_work.files.partition_bytes = 262144ULL;
    result.capture_work.files.cancellation_block_bytes = 65536ULL;
    result.capture_work.files.max_jobs = 64ULL;
    result.capture_work.off.cancellation_block_bytes = 65536ULL;
    result.capture_work.off.cancellation_block_tokens = 256ULL;
    result.capture_work.off.cancellation_block_records = 256ULL;
    result.capture_work.off.geometry.parallel_min_bytes = 262144ULL;
    result.capture_work.off.geometry.partition_bytes = 262144ULL;
    result.capture_work.off.geometry.cancellation_block_scalars = 8192ULL;
    result.capture_work.off.geometry.max_jobs = 64ULL;
    result.triangulation_options.max_corners_per_face = 4096ULL;
    result.triangulation_options.minimum_parallel_faces = 64ULL;
    result.limits.max_wrapper_new_requested_bytes = 134217728ULL;
    result.limits.off.max_wrapper_new_requested_bytes = 134217728ULL;
    result.limits.off.max_input_id_bytes = 128ULL;
    result.limits.off.max_source_protection_bytes = 67108864ULL;
    result.limits.off.max_report_bytes = 4194304ULL;
    result.limits.off.max_hash_input_bytes = 134217728ULL;
    result.limits.off.max_verification_vertices = 1000000ULL;
    result.limits.off.max_verification_faces = 1000000ULL;
    result.limits.off.max_verification_corners = 3000000ULL;
    result.limits.off.repair.max_targets = 1000000ULL;
    result.limits.off.repair.max_row_local_declarations = 1ULL;
    result.limits.off.repair.max_declaration_name_bytes = 1ULL;
    result.limits.off.repair.max_comparison_steps_per_target = 16000000ULL;
    result.limits.off.repair.max_new_requested_bytes = 100663296ULL;
    result.limits.off.repair.inspection.max_issues = 1024ULL;
    result.limits.off.repair.inspection.max_new_requested_bytes = 8388608ULL;
    result.limits.off.repair.selection.max_vertices = 1000000ULL;
    result.limits.off.repair.selection.max_faces = 1000000ULL;
    result.limits.off.repair.selection.max_corners = 3000000ULL;
    result.limits.off.repair.selection.max_face_offsets = 1000001ULL;
    result.limits.off.repair.selection.max_attributes = 1ULL;
    result.limits.off.repair.selection.max_attribute_scalars = 1ULL;
    result.limits.off.repair.selection.max_attribute_offsets = 1ULL;
    result.limits.off.repair.selection.max_presence_bytes = 1ULL;
    result.limits.off.repair.selection.max_metadata_entries = 1ULL;
    result.limits.off.repair.selection.max_descriptor_bytes = 1ULL;
    result.limits.off.repair.selection.max_new_requested_bytes = 67108864ULL;
    result.limits.off.writing.max_output_bytes = 67108864ULL;
    result.limits.off.writing.max_vertices = 1000000ULL;
    result.limits.off.writing.max_faces = 1000000ULL;
    result.limits.off.writing.max_corners = 3000000ULL;
    result.limits.off.writing.max_corners_per_face = 65536ULL;
    result.limits.off.writing.max_line_bytes = 16777216ULL;
    result.limits.off.writing.max_token_bytes = 256ULL;
    result.limits.off.writing.max_codec_new_requested_bytes = 268435456ULL;
    result.limits.off.writing.max_total_new_requested_bytes = 276824064ULL;
    result.limits.off.writing.inspection.max_issues = 1024ULL;
    result.limits.off.writing.inspection.max_new_requested_bytes = 8388608ULL;
    result.limits.off.reload.max_source_bytes = 67108864ULL;
    result.limits.off.reload.max_vertices = 1000000ULL;
    result.limits.off.reload.max_faces = 1000000ULL;
    result.limits.off.reload.max_corners = 3000000ULL;
    result.limits.off.reload.max_corners_per_face = 65536ULL;
    result.limits.off.reload.max_line_bytes = 16777216ULL;
    result.limits.off.reload.max_token_bytes = 256ULL;
    result.limits.off.reload.max_comment_bytes = 1048576ULL;
    result.limits.off.reload.max_codec_new_requested_bytes = 134217728ULL;
    result.limits.off.reload.max_total_new_requested_bytes = 209715200ULL;
    result.limits.off.reload.materialization.max_vertices = 1000000ULL;
    result.limits.off.reload.materialization.max_faces = 1000000ULL;
    result.limits.off.reload.materialization.max_corners = 3000000ULL;
    result.limits.off.reload.materialization.max_face_offsets = 1000001ULL;
    result.limits.off.reload.materialization.max_attributes = 1ULL;
    result.limits.off.reload.materialization.max_attribute_scalars = 1ULL;
    result.limits.off.reload.materialization.max_attribute_offsets = 1ULL;
    result.limits.off.reload.materialization.max_presence_bytes = 1ULL;
    result.limits.off.reload.materialization.max_metadata_entries = 1ULL;
    result.limits.off.reload.materialization.max_descriptor_bytes = 1ULL;
    result.limits.off.reload.materialization.max_new_requested_bytes =
        67108864ULL;
    result.limits.off.reload.inspection.max_issues = 1024ULL;
    result.limits.off.reload.inspection.max_new_requested_bytes = 8388608ULL;
    result.limits.off.publication.files.max_entry_bytes = 67108864ULL;
    result.limits.off.publication.files.max_receipt_bytes = 16777216ULL;
    result.limits.off.publication.files.max_total_file_bytes = 83886080ULL;
    result.limits.off.publication.files.max_entry_name_bytes = 1024ULL;
    result.limits.off.publication.files.max_receipt_name_bytes = 1024ULL;
    result.limits.off.publication.files.max_native_path_code_units = 4096ULL;
    result.limits.off.publication.files.max_file_new_requested_bytes =
        134217728ULL;
    result.limits.off.publication.expected_inspection.max_issues = 1024ULL;
    result.limits.off.publication.expected_inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.off.publication.reload.max_source_bytes = 67108864ULL;
    result.limits.off.publication.reload.max_vertices = 1000000ULL;
    result.limits.off.publication.reload.max_faces = 1000000ULL;
    result.limits.off.publication.reload.max_corners = 3000000ULL;
    result.limits.off.publication.reload.max_corners_per_face = 65536ULL;
    result.limits.off.publication.reload.max_line_bytes = 16777216ULL;
    result.limits.off.publication.reload.max_token_bytes = 256ULL;
    result.limits.off.publication.reload.max_comment_bytes = 1048576ULL;
    result.limits.off.publication.reload.max_codec_new_requested_bytes =
        134217728ULL;
    result.limits.off.publication.reload.max_total_new_requested_bytes =
        209715200ULL;
    result.limits.off.publication.reload.materialization.max_vertices =
        1000000ULL;
    result.limits.off.publication.reload.materialization.max_faces = 1000000ULL;
    result.limits.off.publication.reload.materialization.max_corners =
        3000000ULL;
    result.limits.off.publication.reload.materialization.max_face_offsets =
        1000001ULL;
    result.limits.off.publication.reload.materialization.max_attributes = 1ULL;
    result.limits.off.publication.reload.materialization.max_attribute_scalars =
        1ULL;
    result.limits.off.publication.reload.materialization.max_attribute_offsets =
        1ULL;
    result.limits.off.publication.reload.materialization.max_presence_bytes =
        1ULL;
    result.limits.off.publication.reload.materialization.max_metadata_entries =
        1ULL;
    result.limits.off.publication.reload.materialization.max_descriptor_bytes =
        1ULL;
    result.limits.off.publication.reload.materialization
        .max_new_requested_bytes = 67108864ULL;
    result.limits.off.publication.reload.inspection.max_issues = 1024ULL;
    result.limits.off.publication.reload.inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.triangulation.source.max_vertices = 1000000ULL;
    result.limits.triangulation.source.max_faces = 1000000ULL;
    result.limits.triangulation.source.max_corners = 3000000ULL;
    result.limits.triangulation.source.max_face_offsets = 1000001ULL;
    result.limits.triangulation.source.max_attributes = 1ULL;
    result.limits.triangulation.source.max_attribute_scalars = 1ULL;
    result.limits.triangulation.source.max_attribute_offsets = 1ULL;
    result.limits.triangulation.source.max_presence_bytes = 1ULL;
    result.limits.triangulation.source.max_metadata_entries = 1ULL;
    result.limits.triangulation.source.max_descriptor_bytes = 1ULL;
    result.limits.triangulation.source.max_new_requested_bytes = 67108864ULL;
    result.limits.triangulation.source_inspection.max_issues = 1024ULL;
    result.limits.triangulation.source_inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.triangulation.output.max_vertices = 1000000ULL;
    result.limits.triangulation.output.max_faces = 3000000ULL;
    result.limits.triangulation.output.max_corners = 9000000ULL;
    result.limits.triangulation.output.max_face_offsets = 3000001ULL;
    result.limits.triangulation.output.max_attributes = 1ULL;
    result.limits.triangulation.output.max_attribute_scalars = 1ULL;
    result.limits.triangulation.output.max_attribute_offsets = 1ULL;
    result.limits.triangulation.output.max_presence_bytes = 1ULL;
    result.limits.triangulation.output.max_metadata_entries = 1ULL;
    result.limits.triangulation.output.max_descriptor_bytes = 1ULL;
    result.limits.triangulation.output.max_new_requested_bytes = 134217728ULL;
    result.limits.triangulation.output_inspection.max_issues = 1024ULL;
    result.limits.triangulation.output_inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.triangulation.max_map_entries = 15000001ULL;
    result.limits.triangulation.max_core_new_requested_bytes = 268435456ULL;
    result.limits.triangulation.max_bridge_new_requested_bytes = 134217728ULL;
    result.limits.encoding.counts.max_vertices = 1000000ULL;
    result.limits.encoding.counts.max_faces = 3000000ULL;
    result.limits.encoding.counts.max_corners = 9000000ULL;
    result.limits.encoding.counts.max_face_offsets = 3000001ULL;
    result.limits.encoding.counts.max_attributes = 1ULL;
    result.limits.encoding.counts.max_attribute_scalars = 1ULL;
    result.limits.encoding.counts.max_attribute_bytes = 1ULL;
    result.limits.encoding.counts.max_attribute_offsets = 1ULL;
    result.limits.encoding.counts.max_presence_bytes = 1ULL;
    result.limits.encoding.counts.max_metadata_entries = 1ULL;
    result.limits.encoding.counts.max_descriptor_bytes = 4096ULL;
    result.limits.encoding.counts.max_wire_primitives = 1ULL;
    result.limits.encoding.counts.max_wire_vertices = 9000000ULL;
    result.limits.encoding.counts.max_wire_corners = 9000000ULL;
    result.limits.encoding.counts.max_map_entries = 100000000ULL;
    result.limits.encoding.counts.max_members = 3ULL;
    result.limits.encoding.counts.max_member_bytes = 268435456ULL;
    result.limits.encoding.counts.max_total_member_bytes = 536870912ULL;
    result.limits.encoding.counts.max_resources = 1ULL;
    result.limits.encoding.counts.max_resource_bytes = 268435456ULL;
    result.limits.encoding.counts.max_json_bytes = 67108864ULL;
    result.limits.encoding.counts.max_binary_bytes = 268435456ULL;
    result.limits.encoding.counts.max_capsule_json_bytes = 67108864ULL;
    result.limits.encoding.counts.max_capsule_binary_bytes = 268435456ULL;
    result.limits.encoding.counts.max_material_bindings = 1ULL;
    result.limits.encoding.counts.max_numeric_token_bytes = 4096ULL;
    result.limits.encoding.counts.max_json_nodes = 4000000ULL;
    result.limits.encoding.counts.max_json_depth = 64ULL;
    result.limits.encoding.counts.max_numeric_integer_bits = 4096ULL;
    result.limits.encoding.counts.max_coincidence_comparisons = 100000000ULL;
    result.limits.encoding.counts.max_relationship_steps = 100000000ULL;
    result.limits.encoding.max_new_requested_bytes = 268435456ULL;
    result.limits.encoding.max_total_requested_allowance = 402653184ULL;
    result.limits.encoding.delegates
        .emplace<interchange::GltfSurfaceEncodeLimits>();
    std::get<interchange::GltfSurfaceEncodeLimits>(
        result.limits.encoding.delegates)
        .container.max_container_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceEncodeLimits>(
        result.limits.encoding.delegates)
        .container.max_chunks = 8ULL;
    std::get<interchange::GltfSurfaceEncodeLimits>(
        result.limits.encoding.delegates)
        .container.max_requested_bytes = 134217728ULL;
    result.limits.inspection.counts.max_vertices = 1000000ULL;
    result.limits.inspection.counts.max_faces = 3000000ULL;
    result.limits.inspection.counts.max_corners = 9000000ULL;
    result.limits.inspection.counts.max_face_offsets = 3000001ULL;
    result.limits.inspection.counts.max_attributes = 1ULL;
    result.limits.inspection.counts.max_attribute_scalars = 1ULL;
    result.limits.inspection.counts.max_attribute_bytes = 1ULL;
    result.limits.inspection.counts.max_attribute_offsets = 1ULL;
    result.limits.inspection.counts.max_presence_bytes = 1ULL;
    result.limits.inspection.counts.max_metadata_entries = 1ULL;
    result.limits.inspection.counts.max_descriptor_bytes = 4096ULL;
    result.limits.inspection.counts.max_wire_primitives = 1ULL;
    result.limits.inspection.counts.max_wire_vertices = 9000000ULL;
    result.limits.inspection.counts.max_wire_corners = 9000000ULL;
    result.limits.inspection.counts.max_map_entries = 100000000ULL;
    result.limits.inspection.counts.max_members = 3ULL;
    result.limits.inspection.counts.max_member_bytes = 268435456ULL;
    result.limits.inspection.counts.max_total_member_bytes = 536870912ULL;
    result.limits.inspection.counts.max_resources = 1ULL;
    result.limits.inspection.counts.max_resource_bytes = 268435456ULL;
    result.limits.inspection.counts.max_json_bytes = 67108864ULL;
    result.limits.inspection.counts.max_binary_bytes = 268435456ULL;
    result.limits.inspection.counts.max_capsule_json_bytes = 67108864ULL;
    result.limits.inspection.counts.max_capsule_binary_bytes = 268435456ULL;
    result.limits.inspection.counts.max_material_bindings = 1ULL;
    result.limits.inspection.counts.max_numeric_token_bytes = 4096ULL;
    result.limits.inspection.counts.max_json_nodes = 4000000ULL;
    result.limits.inspection.counts.max_json_depth = 64ULL;
    result.limits.inspection.counts.max_numeric_integer_bits = 4096ULL;
    result.limits.inspection.counts.max_coincidence_comparisons = 100000000ULL;
    result.limits.inspection.counts.max_relationship_steps = 100000000ULL;
    result.limits.inspection.max_new_requested_bytes = 268435456ULL;
    result.limits.inspection.max_total_requested_allowance = 671088640ULL;
    result.limits.inspection.delegates
        .emplace<interchange::GltfSurfaceInspectLimits>();
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .container.max_container_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .container.max_chunks = 8ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .container.max_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_json_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_owned_source_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_document_nodes = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_document_depth = 64ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_decoded_string_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_numeric_token_bytes = 4096ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_decimal_coefficient_digits = 1024ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_decimal_exponent_digits = 16ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_table_rows = 1000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_graph_edges = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_instances = 1ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_accessor_elements = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_projection_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.max_new_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.max_components = 27000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.max_triangle_corners = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.max_output_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_node_rows = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_mesh_users = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_channel_references = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_distinct_samplers = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_keys_per_sampler = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_total_keys = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_relationship_steps = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_metadata_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_decoded_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_new_requested_bytes = 0ULL;
    result.limits.publication.files.max_payload_members = 3ULL;
    result.limits.publication.files.max_payload_member_bytes = 268435456ULL;
    result.limits.publication.files.max_total_payload_bytes = 536870912ULL;
    result.limits.publication.files.max_receipt_bytes = 33554432ULL;
    result.limits.publication.files.max_caller_report_bytes = 4194304ULL;
    result.limits.publication.files.max_path_bytes = 1024ULL;
    result.limits.publication.files.max_total_path_bytes = 8192ULL;
    result.limits.publication.files.max_native_path_code_units = 4096ULL;
    result.limits.publication.files.max_inventory_entries = 16ULL;
    result.limits.publication.files.max_stage_name_attempts = 64ULL;
    result.limits.publication.files.max_new_requested_bytes = 268435456ULL;
    result.limits.publication.inspection.counts.max_vertices = 1000000ULL;
    result.limits.publication.inspection.counts.max_faces = 3000000ULL;
    result.limits.publication.inspection.counts.max_corners = 9000000ULL;
    result.limits.publication.inspection.counts.max_face_offsets = 3000001ULL;
    result.limits.publication.inspection.counts.max_attributes = 1ULL;
    result.limits.publication.inspection.counts.max_attribute_scalars = 1ULL;
    result.limits.publication.inspection.counts.max_attribute_bytes = 1ULL;
    result.limits.publication.inspection.counts.max_attribute_offsets = 1ULL;
    result.limits.publication.inspection.counts.max_presence_bytes = 1ULL;
    result.limits.publication.inspection.counts.max_metadata_entries = 1ULL;
    result.limits.publication.inspection.counts.max_descriptor_bytes = 4096ULL;
    result.limits.publication.inspection.counts.max_wire_primitives = 1ULL;
    result.limits.publication.inspection.counts.max_wire_vertices = 9000000ULL;
    result.limits.publication.inspection.counts.max_wire_corners = 9000000ULL;
    result.limits.publication.inspection.counts.max_map_entries = 100000000ULL;
    result.limits.publication.inspection.counts.max_members = 3ULL;
    result.limits.publication.inspection.counts.max_member_bytes = 268435456ULL;
    result.limits.publication.inspection.counts.max_total_member_bytes =
        536870912ULL;
    result.limits.publication.inspection.counts.max_resources = 1ULL;
    result.limits.publication.inspection.counts.max_resource_bytes =
        268435456ULL;
    result.limits.publication.inspection.counts.max_json_bytes = 67108864ULL;
    result.limits.publication.inspection.counts.max_binary_bytes = 268435456ULL;
    result.limits.publication.inspection.counts.max_capsule_json_bytes =
        67108864ULL;
    result.limits.publication.inspection.counts.max_capsule_binary_bytes =
        268435456ULL;
    result.limits.publication.inspection.counts.max_material_bindings = 1ULL;
    result.limits.publication.inspection.counts.max_numeric_token_bytes =
        4096ULL;
    result.limits.publication.inspection.counts.max_json_nodes = 4000000ULL;
    result.limits.publication.inspection.counts.max_json_depth = 64ULL;
    result.limits.publication.inspection.counts.max_numeric_integer_bits =
        4096ULL;
    result.limits.publication.inspection.counts.max_coincidence_comparisons =
        100000000ULL;
    result.limits.publication.inspection.counts.max_relationship_steps =
        100000000ULL;
    result.limits.publication.inspection.max_new_requested_bytes = 268435456ULL;
    result.limits.publication.inspection.max_total_requested_allowance =
        671088640ULL;
    result.limits.publication.inspection.delegates
        .emplace<interchange::GltfSurfaceInspectLimits>();
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .container.max_container_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .container.max_chunks = 8ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .container.max_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_json_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_owned_source_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_document_nodes = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_document_depth = 64ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_decoded_string_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_numeric_token_bytes = 4096ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_decimal_coefficient_digits = 1024ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_decimal_exponent_digits = 16ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_table_rows = 1000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_graph_edges = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_instances = 1ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_accessor_elements = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_projection_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.max_new_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.max_components = 27000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.max_triangle_corners = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.max_output_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_node_rows = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_mesh_users = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_channel_references = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_distinct_samplers = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_keys_per_sampler = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_total_keys = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_relationship_steps = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_metadata_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_decoded_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_new_requested_bytes = 0ULL;
    result.limits.publication.max_total_requested_allowance = 939524096ULL;
    result.work.off.repair.parallel_min_bytes = 262144ULL;
    result.work.off.repair.partition_bytes = 262144ULL;
    result.work.off.repair.cancellation_block_scalars = 8192ULL;
    result.work.off.repair.max_jobs = 64ULL;
    result.work.off.writing.cancellation_block_bytes = 65536ULL;
    result.work.off.writing.cancellation_block_tokens = 256ULL;
    result.work.off.writing.cancellation_block_records = 256ULL;
    result.work.off.writing.geometry.parallel_min_bytes = 262144ULL;
    result.work.off.writing.geometry.partition_bytes = 262144ULL;
    result.work.off.writing.geometry.cancellation_block_scalars = 8192ULL;
    result.work.off.writing.geometry.max_jobs = 64ULL;
    result.work.off.reload.cancellation_block_bytes = 65536ULL;
    result.work.off.reload.cancellation_block_tokens = 256ULL;
    result.work.off.reload.cancellation_block_records = 256ULL;
    result.work.off.reload.geometry.parallel_min_bytes = 262144ULL;
    result.work.off.reload.geometry.partition_bytes = 262144ULL;
    result.work.off.reload.geometry.cancellation_block_scalars = 8192ULL;
    result.work.off.reload.geometry.max_jobs = 64ULL;
    result.work.off.verification.parallel_min_bytes = 262144ULL;
    result.work.off.verification.partition_bytes = 262144ULL;
    result.work.off.verification.cancellation_block_bytes = 65536ULL;
    result.work.off.verification.max_jobs = 64ULL;
    result.work.off.hashing.parallel_min_bytes = 262144ULL;
    result.work.off.hashing.partition_bytes = 262144ULL;
    result.work.off.hashing.cancellation_block_bytes = 65536ULL;
    result.work.off.hashing.max_jobs = 64ULL;
    result.work.off.report.parallel_min_bytes = 65536ULL;
    result.work.off.report.partition_bytes = 65536ULL;
    result.work.off.report.cancellation_block_bytes = 16384ULL;
    result.work.off.report.max_jobs = 64ULL;
    result.work.off.publication.files.io_block_bytes = 65536ULL;
    result.work.off.publication.files.parallel_min_bytes = 262144ULL;
    result.work.off.publication.files.partition_bytes = 262144ULL;
    result.work.off.publication.files.cancellation_block_bytes = 65536ULL;
    result.work.off.publication.files.max_jobs = 64ULL;
    result.work.off.publication.expected_inspection.parallel_min_bytes =
        262144ULL;
    result.work.off.publication.expected_inspection.partition_bytes = 262144ULL;
    result.work.off.publication.expected_inspection.cancellation_block_scalars =
        8192ULL;
    result.work.off.publication.expected_inspection.max_jobs = 64ULL;
    result.work.off.publication.reload.cancellation_block_bytes = 65536ULL;
    result.work.off.publication.reload.cancellation_block_tokens = 256ULL;
    result.work.off.publication.reload.cancellation_block_records = 256ULL;
    result.work.off.publication.reload.geometry.parallel_min_bytes = 262144ULL;
    result.work.off.publication.reload.geometry.partition_bytes = 262144ULL;
    result.work.off.publication.reload.geometry.cancellation_block_scalars =
        8192ULL;
    result.work.off.publication.reload.geometry.max_jobs = 64ULL;
    result.work.triangulation.parallel_min_bytes = 262144ULL;
    result.work.triangulation.partition_bytes = 262144ULL;
    result.work.triangulation.cancellation_block_scalars = 8192ULL;
    result.work.triangulation.max_jobs = 64ULL;
    result.work.encoding.surface.parallel_min_bytes = 262144ULL;
    result.work.encoding.surface.partition_bytes = 262144ULL;
    result.work.encoding.surface.cancellation_block_scalars = 8192ULL;
    result.work.encoding.surface.max_jobs = 64ULL;
    result.work.encoding.container.parallel_min_bytes = 262144ULL;
    result.work.encoding.container.partition_bytes = 262144ULL;
    result.work.encoding.gltf.parallel_min_resource_bytes = 0ULL;
    result.work.encoding.gltf.resource_partition_bytes = 0ULL;
    result.work.encoding.gltf.parallel_min_channel_components = 0ULL;
    result.work.encoding.gltf.channel_partition_elements = 0ULL;
    result.work.encoding.gltf.direct_translation.parallel_min_keys = 0ULL;
    result.work.encoding.gltf.direct_translation.partition_keys = 0ULL;
    result.work.encoding.gltf.direct_translation.cancellation_block_keys = 0ULL;
    result.work.encoding.gltf.direct_translation.max_jobs = 0ULL;
    result.work.encoding.stl.cancellation_block_bytes = 0ULL;
    result.work.encoding.stl.cancellation_block_tokens = 0ULL;
    result.work.encoding.stl.cancellation_block_facets = 0ULL;
    result.work.encoding.stl.geometry.parallel_min_bytes = 0ULL;
    result.work.encoding.stl.geometry.partition_bytes = 0ULL;
    result.work.encoding.stl.geometry.cancellation_block_scalars = 0ULL;
    result.work.encoding.stl.geometry.max_jobs = 0ULL;
    result.work.inspection.surface.parallel_min_bytes = 262144ULL;
    result.work.inspection.surface.partition_bytes = 262144ULL;
    result.work.inspection.surface.cancellation_block_scalars = 8192ULL;
    result.work.inspection.surface.max_jobs = 64ULL;
    result.work.inspection.container.parallel_min_bytes = 262144ULL;
    result.work.inspection.container.partition_bytes = 262144ULL;
    result.work.inspection.gltf.parallel_min_resource_bytes = 262144ULL;
    result.work.inspection.gltf.resource_partition_bytes = 262144ULL;
    result.work.inspection.gltf.parallel_min_channel_components = 65536ULL;
    result.work.inspection.gltf.channel_partition_elements = 65536ULL;
    result.work.inspection.gltf.direct_translation.parallel_min_keys = 0ULL;
    result.work.inspection.gltf.direct_translation.partition_keys = 0ULL;
    result.work.inspection.gltf.direct_translation.cancellation_block_keys =
        0ULL;
    result.work.inspection.gltf.direct_translation.max_jobs = 0ULL;
    result.work.inspection.stl.cancellation_block_bytes = 0ULL;
    result.work.inspection.stl.cancellation_block_tokens = 0ULL;
    result.work.inspection.stl.cancellation_block_facets = 0ULL;
    result.work.inspection.stl.geometry.parallel_min_bytes = 0ULL;
    result.work.inspection.stl.geometry.partition_bytes = 0ULL;
    result.work.inspection.stl.geometry.cancellation_block_scalars = 0ULL;
    result.work.inspection.stl.geometry.max_jobs = 0ULL;
    result.work.publication.files.io_block_bytes = 65536ULL;
    result.work.publication.files.parallel_min_bytes = 262144ULL;
    result.work.publication.files.partition_bytes = 262144ULL;
    result.work.publication.files.cancellation_block_bytes = 65536ULL;
    result.work.publication.files.max_jobs = 64ULL;
    result.work.publication.inspection.surface.parallel_min_bytes = 262144ULL;
    result.work.publication.inspection.surface.partition_bytes = 262144ULL;
    result.work.publication.inspection.surface.cancellation_block_scalars =
        8192ULL;
    result.work.publication.inspection.surface.max_jobs = 64ULL;
    result.work.publication.inspection.container.parallel_min_bytes = 262144ULL;
    result.work.publication.inspection.container.partition_bytes = 262144ULL;
    result.work.publication.inspection.gltf.parallel_min_resource_bytes =
        262144ULL;
    result.work.publication.inspection.gltf.resource_partition_bytes =
        262144ULL;
    result.work.publication.inspection.gltf.parallel_min_channel_components =
        65536ULL;
    result.work.publication.inspection.gltf.channel_partition_elements =
        65536ULL;
    result.work.publication.inspection.gltf.direct_translation
        .parallel_min_keys = 0ULL;
    result.work.publication.inspection.gltf.direct_translation.partition_keys =
        0ULL;
    result.work.publication.inspection.gltf.direct_translation
        .cancellation_block_keys = 0ULL;
    result.work.publication.inspection.gltf.direct_translation.max_jobs = 0ULL;
    result.work.publication.inspection.stl.cancellation_block_bytes = 0ULL;
    result.work.publication.inspection.stl.cancellation_block_tokens = 0ULL;
    result.work.publication.inspection.stl.cancellation_block_facets = 0ULL;
    result.work.publication.inspection.stl.geometry.parallel_min_bytes = 0ULL;
    result.work.publication.inspection.stl.geometry.partition_bytes = 0ULL;
    result.work.publication.inspection.stl.geometry.cancellation_block_scalars =
        0ULL;
    result.work.publication.inspection.stl.geometry.max_jobs = 0ULL;
    return result;
  }
  if (name == kSerialOffGlbPreset) {
    // Complete finite configuration: docs/off-surface-export-presets.json.
    result.execution.worker_budget = 1ULL;
    result.execution.minimum_parallel_vertices = 9223372036854775807ULL;
    result.execution.tracked_payload_budget_bytes = 2147483648ULL;
    result.capture_limits.files.max_entry_bytes = 67108864ULL;
    result.capture_limits.files.max_receipt_bytes = 16777216ULL;
    result.capture_limits.files.max_total_file_bytes = 83886080ULL;
    result.capture_limits.files.max_entry_name_bytes = 1024ULL;
    result.capture_limits.files.max_receipt_name_bytes = 1024ULL;
    result.capture_limits.files.max_native_path_code_units = 4096ULL;
    result.capture_limits.files.max_file_new_requested_bytes = 134217728ULL;
    result.capture_limits.off.max_source_bytes = 67108864ULL;
    result.capture_limits.off.max_vertices = 1000000ULL;
    result.capture_limits.off.max_faces = 1000000ULL;
    result.capture_limits.off.max_corners = 3000000ULL;
    result.capture_limits.off.max_corners_per_face = 65536ULL;
    result.capture_limits.off.max_line_bytes = 16777216ULL;
    result.capture_limits.off.max_token_bytes = 256ULL;
    result.capture_limits.off.max_comment_bytes = 1048576ULL;
    result.capture_limits.off.max_codec_new_requested_bytes = 134217728ULL;
    result.capture_limits.off.max_total_new_requested_bytes = 209715200ULL;
    result.capture_limits.off.materialization.max_vertices = 1000000ULL;
    result.capture_limits.off.materialization.max_faces = 1000000ULL;
    result.capture_limits.off.materialization.max_corners = 3000000ULL;
    result.capture_limits.off.materialization.max_face_offsets = 1000001ULL;
    result.capture_limits.off.materialization.max_attributes = 1ULL;
    result.capture_limits.off.materialization.max_attribute_scalars = 1ULL;
    result.capture_limits.off.materialization.max_attribute_offsets = 1ULL;
    result.capture_limits.off.materialization.max_presence_bytes = 1ULL;
    result.capture_limits.off.materialization.max_metadata_entries = 1ULL;
    result.capture_limits.off.materialization.max_descriptor_bytes = 1ULL;
    result.capture_limits.off.materialization.max_new_requested_bytes =
        67108864ULL;
    result.capture_limits.off.inspection.max_issues = 1024ULL;
    result.capture_limits.off.inspection.max_new_requested_bytes = 8388608ULL;
    result.capture_work.files.io_block_bytes = 65536ULL;
    result.capture_work.files.parallel_min_bytes = 9223372036854775807ULL;
    result.capture_work.files.partition_bytes = 262144ULL;
    result.capture_work.files.cancellation_block_bytes = 65536ULL;
    result.capture_work.files.max_jobs = 1ULL;
    result.capture_work.off.cancellation_block_bytes = 65536ULL;
    result.capture_work.off.cancellation_block_tokens = 256ULL;
    result.capture_work.off.cancellation_block_records = 256ULL;
    result.capture_work.off.geometry.parallel_min_bytes =
        9223372036854775807ULL;
    result.capture_work.off.geometry.partition_bytes = 262144ULL;
    result.capture_work.off.geometry.cancellation_block_scalars = 8192ULL;
    result.capture_work.off.geometry.max_jobs = 1ULL;
    result.triangulation_options.max_corners_per_face = 4096ULL;
    result.triangulation_options.minimum_parallel_faces =
        9223372036854775807ULL;
    result.limits.max_wrapper_new_requested_bytes = 134217728ULL;
    result.limits.off.max_wrapper_new_requested_bytes = 134217728ULL;
    result.limits.off.max_input_id_bytes = 128ULL;
    result.limits.off.max_source_protection_bytes = 67108864ULL;
    result.limits.off.max_report_bytes = 4194304ULL;
    result.limits.off.max_hash_input_bytes = 134217728ULL;
    result.limits.off.max_verification_vertices = 1000000ULL;
    result.limits.off.max_verification_faces = 1000000ULL;
    result.limits.off.max_verification_corners = 3000000ULL;
    result.limits.off.repair.max_targets = 1000000ULL;
    result.limits.off.repair.max_row_local_declarations = 1ULL;
    result.limits.off.repair.max_declaration_name_bytes = 1ULL;
    result.limits.off.repair.max_comparison_steps_per_target = 16000000ULL;
    result.limits.off.repair.max_new_requested_bytes = 100663296ULL;
    result.limits.off.repair.inspection.max_issues = 1024ULL;
    result.limits.off.repair.inspection.max_new_requested_bytes = 8388608ULL;
    result.limits.off.repair.selection.max_vertices = 1000000ULL;
    result.limits.off.repair.selection.max_faces = 1000000ULL;
    result.limits.off.repair.selection.max_corners = 3000000ULL;
    result.limits.off.repair.selection.max_face_offsets = 1000001ULL;
    result.limits.off.repair.selection.max_attributes = 1ULL;
    result.limits.off.repair.selection.max_attribute_scalars = 1ULL;
    result.limits.off.repair.selection.max_attribute_offsets = 1ULL;
    result.limits.off.repair.selection.max_presence_bytes = 1ULL;
    result.limits.off.repair.selection.max_metadata_entries = 1ULL;
    result.limits.off.repair.selection.max_descriptor_bytes = 1ULL;
    result.limits.off.repair.selection.max_new_requested_bytes = 67108864ULL;
    result.limits.off.writing.max_output_bytes = 67108864ULL;
    result.limits.off.writing.max_vertices = 1000000ULL;
    result.limits.off.writing.max_faces = 1000000ULL;
    result.limits.off.writing.max_corners = 3000000ULL;
    result.limits.off.writing.max_corners_per_face = 65536ULL;
    result.limits.off.writing.max_line_bytes = 16777216ULL;
    result.limits.off.writing.max_token_bytes = 256ULL;
    result.limits.off.writing.max_codec_new_requested_bytes = 268435456ULL;
    result.limits.off.writing.max_total_new_requested_bytes = 276824064ULL;
    result.limits.off.writing.inspection.max_issues = 1024ULL;
    result.limits.off.writing.inspection.max_new_requested_bytes = 8388608ULL;
    result.limits.off.reload.max_source_bytes = 67108864ULL;
    result.limits.off.reload.max_vertices = 1000000ULL;
    result.limits.off.reload.max_faces = 1000000ULL;
    result.limits.off.reload.max_corners = 3000000ULL;
    result.limits.off.reload.max_corners_per_face = 65536ULL;
    result.limits.off.reload.max_line_bytes = 16777216ULL;
    result.limits.off.reload.max_token_bytes = 256ULL;
    result.limits.off.reload.max_comment_bytes = 1048576ULL;
    result.limits.off.reload.max_codec_new_requested_bytes = 134217728ULL;
    result.limits.off.reload.max_total_new_requested_bytes = 209715200ULL;
    result.limits.off.reload.materialization.max_vertices = 1000000ULL;
    result.limits.off.reload.materialization.max_faces = 1000000ULL;
    result.limits.off.reload.materialization.max_corners = 3000000ULL;
    result.limits.off.reload.materialization.max_face_offsets = 1000001ULL;
    result.limits.off.reload.materialization.max_attributes = 1ULL;
    result.limits.off.reload.materialization.max_attribute_scalars = 1ULL;
    result.limits.off.reload.materialization.max_attribute_offsets = 1ULL;
    result.limits.off.reload.materialization.max_presence_bytes = 1ULL;
    result.limits.off.reload.materialization.max_metadata_entries = 1ULL;
    result.limits.off.reload.materialization.max_descriptor_bytes = 1ULL;
    result.limits.off.reload.materialization.max_new_requested_bytes =
        67108864ULL;
    result.limits.off.reload.inspection.max_issues = 1024ULL;
    result.limits.off.reload.inspection.max_new_requested_bytes = 8388608ULL;
    result.limits.off.publication.files.max_entry_bytes = 67108864ULL;
    result.limits.off.publication.files.max_receipt_bytes = 16777216ULL;
    result.limits.off.publication.files.max_total_file_bytes = 83886080ULL;
    result.limits.off.publication.files.max_entry_name_bytes = 1024ULL;
    result.limits.off.publication.files.max_receipt_name_bytes = 1024ULL;
    result.limits.off.publication.files.max_native_path_code_units = 4096ULL;
    result.limits.off.publication.files.max_file_new_requested_bytes =
        134217728ULL;
    result.limits.off.publication.expected_inspection.max_issues = 1024ULL;
    result.limits.off.publication.expected_inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.off.publication.reload.max_source_bytes = 67108864ULL;
    result.limits.off.publication.reload.max_vertices = 1000000ULL;
    result.limits.off.publication.reload.max_faces = 1000000ULL;
    result.limits.off.publication.reload.max_corners = 3000000ULL;
    result.limits.off.publication.reload.max_corners_per_face = 65536ULL;
    result.limits.off.publication.reload.max_line_bytes = 16777216ULL;
    result.limits.off.publication.reload.max_token_bytes = 256ULL;
    result.limits.off.publication.reload.max_comment_bytes = 1048576ULL;
    result.limits.off.publication.reload.max_codec_new_requested_bytes =
        134217728ULL;
    result.limits.off.publication.reload.max_total_new_requested_bytes =
        209715200ULL;
    result.limits.off.publication.reload.materialization.max_vertices =
        1000000ULL;
    result.limits.off.publication.reload.materialization.max_faces = 1000000ULL;
    result.limits.off.publication.reload.materialization.max_corners =
        3000000ULL;
    result.limits.off.publication.reload.materialization.max_face_offsets =
        1000001ULL;
    result.limits.off.publication.reload.materialization.max_attributes = 1ULL;
    result.limits.off.publication.reload.materialization.max_attribute_scalars =
        1ULL;
    result.limits.off.publication.reload.materialization.max_attribute_offsets =
        1ULL;
    result.limits.off.publication.reload.materialization.max_presence_bytes =
        1ULL;
    result.limits.off.publication.reload.materialization.max_metadata_entries =
        1ULL;
    result.limits.off.publication.reload.materialization.max_descriptor_bytes =
        1ULL;
    result.limits.off.publication.reload.materialization
        .max_new_requested_bytes = 67108864ULL;
    result.limits.off.publication.reload.inspection.max_issues = 1024ULL;
    result.limits.off.publication.reload.inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.triangulation.source.max_vertices = 1000000ULL;
    result.limits.triangulation.source.max_faces = 1000000ULL;
    result.limits.triangulation.source.max_corners = 3000000ULL;
    result.limits.triangulation.source.max_face_offsets = 1000001ULL;
    result.limits.triangulation.source.max_attributes = 1ULL;
    result.limits.triangulation.source.max_attribute_scalars = 1ULL;
    result.limits.triangulation.source.max_attribute_offsets = 1ULL;
    result.limits.triangulation.source.max_presence_bytes = 1ULL;
    result.limits.triangulation.source.max_metadata_entries = 1ULL;
    result.limits.triangulation.source.max_descriptor_bytes = 1ULL;
    result.limits.triangulation.source.max_new_requested_bytes = 67108864ULL;
    result.limits.triangulation.source_inspection.max_issues = 1024ULL;
    result.limits.triangulation.source_inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.triangulation.output.max_vertices = 1000000ULL;
    result.limits.triangulation.output.max_faces = 3000000ULL;
    result.limits.triangulation.output.max_corners = 9000000ULL;
    result.limits.triangulation.output.max_face_offsets = 3000001ULL;
    result.limits.triangulation.output.max_attributes = 1ULL;
    result.limits.triangulation.output.max_attribute_scalars = 1ULL;
    result.limits.triangulation.output.max_attribute_offsets = 1ULL;
    result.limits.triangulation.output.max_presence_bytes = 1ULL;
    result.limits.triangulation.output.max_metadata_entries = 1ULL;
    result.limits.triangulation.output.max_descriptor_bytes = 1ULL;
    result.limits.triangulation.output.max_new_requested_bytes = 134217728ULL;
    result.limits.triangulation.output_inspection.max_issues = 1024ULL;
    result.limits.triangulation.output_inspection.max_new_requested_bytes =
        8388608ULL;
    result.limits.triangulation.max_map_entries = 15000001ULL;
    result.limits.triangulation.max_core_new_requested_bytes = 268435456ULL;
    result.limits.triangulation.max_bridge_new_requested_bytes = 134217728ULL;
    result.limits.encoding.counts.max_vertices = 1000000ULL;
    result.limits.encoding.counts.max_faces = 3000000ULL;
    result.limits.encoding.counts.max_corners = 9000000ULL;
    result.limits.encoding.counts.max_face_offsets = 3000001ULL;
    result.limits.encoding.counts.max_attributes = 1ULL;
    result.limits.encoding.counts.max_attribute_scalars = 1ULL;
    result.limits.encoding.counts.max_attribute_bytes = 1ULL;
    result.limits.encoding.counts.max_attribute_offsets = 1ULL;
    result.limits.encoding.counts.max_presence_bytes = 1ULL;
    result.limits.encoding.counts.max_metadata_entries = 1ULL;
    result.limits.encoding.counts.max_descriptor_bytes = 4096ULL;
    result.limits.encoding.counts.max_wire_primitives = 1ULL;
    result.limits.encoding.counts.max_wire_vertices = 9000000ULL;
    result.limits.encoding.counts.max_wire_corners = 9000000ULL;
    result.limits.encoding.counts.max_map_entries = 100000000ULL;
    result.limits.encoding.counts.max_members = 3ULL;
    result.limits.encoding.counts.max_member_bytes = 268435456ULL;
    result.limits.encoding.counts.max_total_member_bytes = 536870912ULL;
    result.limits.encoding.counts.max_resources = 1ULL;
    result.limits.encoding.counts.max_resource_bytes = 268435456ULL;
    result.limits.encoding.counts.max_json_bytes = 67108864ULL;
    result.limits.encoding.counts.max_binary_bytes = 268435456ULL;
    result.limits.encoding.counts.max_capsule_json_bytes = 67108864ULL;
    result.limits.encoding.counts.max_capsule_binary_bytes = 268435456ULL;
    result.limits.encoding.counts.max_material_bindings = 1ULL;
    result.limits.encoding.counts.max_numeric_token_bytes = 4096ULL;
    result.limits.encoding.counts.max_json_nodes = 4000000ULL;
    result.limits.encoding.counts.max_json_depth = 64ULL;
    result.limits.encoding.counts.max_numeric_integer_bits = 4096ULL;
    result.limits.encoding.counts.max_coincidence_comparisons = 100000000ULL;
    result.limits.encoding.counts.max_relationship_steps = 100000000ULL;
    result.limits.encoding.max_new_requested_bytes = 268435456ULL;
    result.limits.encoding.max_total_requested_allowance = 402653184ULL;
    result.limits.encoding.delegates
        .emplace<interchange::GltfSurfaceEncodeLimits>();
    std::get<interchange::GltfSurfaceEncodeLimits>(
        result.limits.encoding.delegates)
        .container.max_container_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceEncodeLimits>(
        result.limits.encoding.delegates)
        .container.max_chunks = 8ULL;
    std::get<interchange::GltfSurfaceEncodeLimits>(
        result.limits.encoding.delegates)
        .container.max_requested_bytes = 134217728ULL;
    result.limits.inspection.counts.max_vertices = 1000000ULL;
    result.limits.inspection.counts.max_faces = 3000000ULL;
    result.limits.inspection.counts.max_corners = 9000000ULL;
    result.limits.inspection.counts.max_face_offsets = 3000001ULL;
    result.limits.inspection.counts.max_attributes = 1ULL;
    result.limits.inspection.counts.max_attribute_scalars = 1ULL;
    result.limits.inspection.counts.max_attribute_bytes = 1ULL;
    result.limits.inspection.counts.max_attribute_offsets = 1ULL;
    result.limits.inspection.counts.max_presence_bytes = 1ULL;
    result.limits.inspection.counts.max_metadata_entries = 1ULL;
    result.limits.inspection.counts.max_descriptor_bytes = 4096ULL;
    result.limits.inspection.counts.max_wire_primitives = 1ULL;
    result.limits.inspection.counts.max_wire_vertices = 9000000ULL;
    result.limits.inspection.counts.max_wire_corners = 9000000ULL;
    result.limits.inspection.counts.max_map_entries = 100000000ULL;
    result.limits.inspection.counts.max_members = 3ULL;
    result.limits.inspection.counts.max_member_bytes = 268435456ULL;
    result.limits.inspection.counts.max_total_member_bytes = 536870912ULL;
    result.limits.inspection.counts.max_resources = 1ULL;
    result.limits.inspection.counts.max_resource_bytes = 268435456ULL;
    result.limits.inspection.counts.max_json_bytes = 67108864ULL;
    result.limits.inspection.counts.max_binary_bytes = 268435456ULL;
    result.limits.inspection.counts.max_capsule_json_bytes = 67108864ULL;
    result.limits.inspection.counts.max_capsule_binary_bytes = 268435456ULL;
    result.limits.inspection.counts.max_material_bindings = 1ULL;
    result.limits.inspection.counts.max_numeric_token_bytes = 4096ULL;
    result.limits.inspection.counts.max_json_nodes = 4000000ULL;
    result.limits.inspection.counts.max_json_depth = 64ULL;
    result.limits.inspection.counts.max_numeric_integer_bits = 4096ULL;
    result.limits.inspection.counts.max_coincidence_comparisons = 100000000ULL;
    result.limits.inspection.counts.max_relationship_steps = 100000000ULL;
    result.limits.inspection.max_new_requested_bytes = 268435456ULL;
    result.limits.inspection.max_total_requested_allowance = 671088640ULL;
    result.limits.inspection.delegates
        .emplace<interchange::GltfSurfaceInspectLimits>();
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .container.max_container_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .container.max_chunks = 8ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .container.max_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_json_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_owned_source_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_document_nodes = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_document_depth = 64ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_decoded_string_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_numeric_token_bytes = 4096ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_decimal_coefficient_digits = 1024ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_decimal_exponent_digits = 16ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_table_rows = 1000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_graph_edges = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_instances = 1ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_accessor_elements = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.source.max_projection_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .read.max_new_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.max_components = 27000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.max_triangle_corners = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.max_output_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_node_rows = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_mesh_users = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_channel_references = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_distinct_samplers = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_keys_per_sampler = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_total_keys = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_relationship_steps = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_metadata_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_decoded_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.inspection.delegates)
        .extraction.direct_translation.max_new_requested_bytes = 0ULL;
    result.limits.publication.files.max_payload_members = 3ULL;
    result.limits.publication.files.max_payload_member_bytes = 268435456ULL;
    result.limits.publication.files.max_total_payload_bytes = 536870912ULL;
    result.limits.publication.files.max_receipt_bytes = 33554432ULL;
    result.limits.publication.files.max_caller_report_bytes = 4194304ULL;
    result.limits.publication.files.max_path_bytes = 1024ULL;
    result.limits.publication.files.max_total_path_bytes = 8192ULL;
    result.limits.publication.files.max_native_path_code_units = 4096ULL;
    result.limits.publication.files.max_inventory_entries = 16ULL;
    result.limits.publication.files.max_stage_name_attempts = 64ULL;
    result.limits.publication.files.max_new_requested_bytes = 268435456ULL;
    result.limits.publication.inspection.counts.max_vertices = 1000000ULL;
    result.limits.publication.inspection.counts.max_faces = 3000000ULL;
    result.limits.publication.inspection.counts.max_corners = 9000000ULL;
    result.limits.publication.inspection.counts.max_face_offsets = 3000001ULL;
    result.limits.publication.inspection.counts.max_attributes = 1ULL;
    result.limits.publication.inspection.counts.max_attribute_scalars = 1ULL;
    result.limits.publication.inspection.counts.max_attribute_bytes = 1ULL;
    result.limits.publication.inspection.counts.max_attribute_offsets = 1ULL;
    result.limits.publication.inspection.counts.max_presence_bytes = 1ULL;
    result.limits.publication.inspection.counts.max_metadata_entries = 1ULL;
    result.limits.publication.inspection.counts.max_descriptor_bytes = 4096ULL;
    result.limits.publication.inspection.counts.max_wire_primitives = 1ULL;
    result.limits.publication.inspection.counts.max_wire_vertices = 9000000ULL;
    result.limits.publication.inspection.counts.max_wire_corners = 9000000ULL;
    result.limits.publication.inspection.counts.max_map_entries = 100000000ULL;
    result.limits.publication.inspection.counts.max_members = 3ULL;
    result.limits.publication.inspection.counts.max_member_bytes = 268435456ULL;
    result.limits.publication.inspection.counts.max_total_member_bytes =
        536870912ULL;
    result.limits.publication.inspection.counts.max_resources = 1ULL;
    result.limits.publication.inspection.counts.max_resource_bytes =
        268435456ULL;
    result.limits.publication.inspection.counts.max_json_bytes = 67108864ULL;
    result.limits.publication.inspection.counts.max_binary_bytes = 268435456ULL;
    result.limits.publication.inspection.counts.max_capsule_json_bytes =
        67108864ULL;
    result.limits.publication.inspection.counts.max_capsule_binary_bytes =
        268435456ULL;
    result.limits.publication.inspection.counts.max_material_bindings = 1ULL;
    result.limits.publication.inspection.counts.max_numeric_token_bytes =
        4096ULL;
    result.limits.publication.inspection.counts.max_json_nodes = 4000000ULL;
    result.limits.publication.inspection.counts.max_json_depth = 64ULL;
    result.limits.publication.inspection.counts.max_numeric_integer_bits =
        4096ULL;
    result.limits.publication.inspection.counts.max_coincidence_comparisons =
        100000000ULL;
    result.limits.publication.inspection.counts.max_relationship_steps =
        100000000ULL;
    result.limits.publication.inspection.max_new_requested_bytes = 268435456ULL;
    result.limits.publication.inspection.max_total_requested_allowance =
        671088640ULL;
    result.limits.publication.inspection.delegates
        .emplace<interchange::GltfSurfaceInspectLimits>();
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .container.max_container_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .container.max_chunks = 8ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .container.max_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_json_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_owned_source_resource_bytes = 268435456ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_document_nodes = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_document_depth = 64ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_decoded_string_bytes = 67108864ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_numeric_token_bytes = 4096ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_decimal_coefficient_digits = 1024ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_decimal_exponent_digits = 16ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_table_rows = 1000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_graph_edges = 4000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_instances = 1ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_accessor_elements = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.source.max_projection_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .read.max_new_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.max_components = 27000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.max_triangle_corners = 9000000ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.max_output_requested_bytes = 134217728ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_node_rows = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_mesh_users = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_channel_references = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_distinct_samplers = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_keys_per_sampler = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_total_keys = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_relationship_steps = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_metadata_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_decoded_bytes = 0ULL;
    std::get<interchange::GltfSurfaceInspectLimits>(
        result.limits.publication.inspection.delegates)
        .extraction.direct_translation.max_new_requested_bytes = 0ULL;
    result.limits.publication.max_total_requested_allowance = 939524096ULL;
    result.work.off.repair.parallel_min_bytes = 9223372036854775807ULL;
    result.work.off.repair.partition_bytes = 262144ULL;
    result.work.off.repair.cancellation_block_scalars = 8192ULL;
    result.work.off.repair.max_jobs = 1ULL;
    result.work.off.writing.cancellation_block_bytes = 65536ULL;
    result.work.off.writing.cancellation_block_tokens = 256ULL;
    result.work.off.writing.cancellation_block_records = 256ULL;
    result.work.off.writing.geometry.parallel_min_bytes =
        9223372036854775807ULL;
    result.work.off.writing.geometry.partition_bytes = 262144ULL;
    result.work.off.writing.geometry.cancellation_block_scalars = 8192ULL;
    result.work.off.writing.geometry.max_jobs = 1ULL;
    result.work.off.reload.cancellation_block_bytes = 65536ULL;
    result.work.off.reload.cancellation_block_tokens = 256ULL;
    result.work.off.reload.cancellation_block_records = 256ULL;
    result.work.off.reload.geometry.parallel_min_bytes = 9223372036854775807ULL;
    result.work.off.reload.geometry.partition_bytes = 262144ULL;
    result.work.off.reload.geometry.cancellation_block_scalars = 8192ULL;
    result.work.off.reload.geometry.max_jobs = 1ULL;
    result.work.off.verification.parallel_min_bytes = 9223372036854775807ULL;
    result.work.off.verification.partition_bytes = 262144ULL;
    result.work.off.verification.cancellation_block_bytes = 65536ULL;
    result.work.off.verification.max_jobs = 1ULL;
    result.work.off.hashing.parallel_min_bytes = 9223372036854775807ULL;
    result.work.off.hashing.partition_bytes = 262144ULL;
    result.work.off.hashing.cancellation_block_bytes = 65536ULL;
    result.work.off.hashing.max_jobs = 1ULL;
    result.work.off.report.parallel_min_bytes = 9223372036854775807ULL;
    result.work.off.report.partition_bytes = 65536ULL;
    result.work.off.report.cancellation_block_bytes = 16384ULL;
    result.work.off.report.max_jobs = 1ULL;
    result.work.off.publication.files.io_block_bytes = 65536ULL;
    result.work.off.publication.files.parallel_min_bytes =
        9223372036854775807ULL;
    result.work.off.publication.files.partition_bytes = 262144ULL;
    result.work.off.publication.files.cancellation_block_bytes = 65536ULL;
    result.work.off.publication.files.max_jobs = 1ULL;
    result.work.off.publication.expected_inspection.parallel_min_bytes =
        9223372036854775807ULL;
    result.work.off.publication.expected_inspection.partition_bytes = 262144ULL;
    result.work.off.publication.expected_inspection.cancellation_block_scalars =
        8192ULL;
    result.work.off.publication.expected_inspection.max_jobs = 1ULL;
    result.work.off.publication.reload.cancellation_block_bytes = 65536ULL;
    result.work.off.publication.reload.cancellation_block_tokens = 256ULL;
    result.work.off.publication.reload.cancellation_block_records = 256ULL;
    result.work.off.publication.reload.geometry.parallel_min_bytes =
        9223372036854775807ULL;
    result.work.off.publication.reload.geometry.partition_bytes = 262144ULL;
    result.work.off.publication.reload.geometry.cancellation_block_scalars =
        8192ULL;
    result.work.off.publication.reload.geometry.max_jobs = 1ULL;
    result.work.triangulation.parallel_min_bytes = 9223372036854775807ULL;
    result.work.triangulation.partition_bytes = 262144ULL;
    result.work.triangulation.cancellation_block_scalars = 8192ULL;
    result.work.triangulation.max_jobs = 1ULL;
    result.work.encoding.surface.parallel_min_bytes = 9223372036854775807ULL;
    result.work.encoding.surface.partition_bytes = 262144ULL;
    result.work.encoding.surface.cancellation_block_scalars = 8192ULL;
    result.work.encoding.surface.max_jobs = 1ULL;
    result.work.encoding.container.parallel_min_bytes = 9223372036854775807ULL;
    result.work.encoding.container.partition_bytes = 262144ULL;
    result.work.encoding.gltf.parallel_min_resource_bytes = 0ULL;
    result.work.encoding.gltf.resource_partition_bytes = 0ULL;
    result.work.encoding.gltf.parallel_min_channel_components = 0ULL;
    result.work.encoding.gltf.channel_partition_elements = 0ULL;
    result.work.encoding.gltf.direct_translation.parallel_min_keys = 0ULL;
    result.work.encoding.gltf.direct_translation.partition_keys = 0ULL;
    result.work.encoding.gltf.direct_translation.cancellation_block_keys = 0ULL;
    result.work.encoding.gltf.direct_translation.max_jobs = 0ULL;
    result.work.encoding.stl.cancellation_block_bytes = 0ULL;
    result.work.encoding.stl.cancellation_block_tokens = 0ULL;
    result.work.encoding.stl.cancellation_block_facets = 0ULL;
    result.work.encoding.stl.geometry.parallel_min_bytes = 0ULL;
    result.work.encoding.stl.geometry.partition_bytes = 0ULL;
    result.work.encoding.stl.geometry.cancellation_block_scalars = 0ULL;
    result.work.encoding.stl.geometry.max_jobs = 0ULL;
    result.work.inspection.surface.parallel_min_bytes = 9223372036854775807ULL;
    result.work.inspection.surface.partition_bytes = 262144ULL;
    result.work.inspection.surface.cancellation_block_scalars = 8192ULL;
    result.work.inspection.surface.max_jobs = 1ULL;
    result.work.inspection.container.parallel_min_bytes =
        9223372036854775807ULL;
    result.work.inspection.container.partition_bytes = 262144ULL;
    result.work.inspection.gltf.parallel_min_resource_bytes =
        9223372036854775807ULL;
    result.work.inspection.gltf.resource_partition_bytes = 262144ULL;
    result.work.inspection.gltf.parallel_min_channel_components =
        9223372036854775807ULL;
    result.work.inspection.gltf.channel_partition_elements = 65536ULL;
    result.work.inspection.gltf.direct_translation.parallel_min_keys = 0ULL;
    result.work.inspection.gltf.direct_translation.partition_keys = 0ULL;
    result.work.inspection.gltf.direct_translation.cancellation_block_keys =
        0ULL;
    result.work.inspection.gltf.direct_translation.max_jobs = 0ULL;
    result.work.inspection.stl.cancellation_block_bytes = 0ULL;
    result.work.inspection.stl.cancellation_block_tokens = 0ULL;
    result.work.inspection.stl.cancellation_block_facets = 0ULL;
    result.work.inspection.stl.geometry.parallel_min_bytes = 0ULL;
    result.work.inspection.stl.geometry.partition_bytes = 0ULL;
    result.work.inspection.stl.geometry.cancellation_block_scalars = 0ULL;
    result.work.inspection.stl.geometry.max_jobs = 0ULL;
    result.work.publication.files.io_block_bytes = 65536ULL;
    result.work.publication.files.parallel_min_bytes = 9223372036854775807ULL;
    result.work.publication.files.partition_bytes = 262144ULL;
    result.work.publication.files.cancellation_block_bytes = 65536ULL;
    result.work.publication.files.max_jobs = 1ULL;
    result.work.publication.inspection.surface.parallel_min_bytes =
        9223372036854775807ULL;
    result.work.publication.inspection.surface.partition_bytes = 262144ULL;
    result.work.publication.inspection.surface.cancellation_block_scalars =
        8192ULL;
    result.work.publication.inspection.surface.max_jobs = 1ULL;
    result.work.publication.inspection.container.parallel_min_bytes =
        9223372036854775807ULL;
    result.work.publication.inspection.container.partition_bytes = 262144ULL;
    result.work.publication.inspection.gltf.parallel_min_resource_bytes =
        9223372036854775807ULL;
    result.work.publication.inspection.gltf.resource_partition_bytes =
        262144ULL;
    result.work.publication.inspection.gltf.parallel_min_channel_components =
        9223372036854775807ULL;
    result.work.publication.inspection.gltf.channel_partition_elements =
        65536ULL;
    result.work.publication.inspection.gltf.direct_translation
        .parallel_min_keys = 0ULL;
    result.work.publication.inspection.gltf.direct_translation.partition_keys =
        0ULL;
    result.work.publication.inspection.gltf.direct_translation
        .cancellation_block_keys = 0ULL;
    result.work.publication.inspection.gltf.direct_translation.max_jobs = 0ULL;
    result.work.publication.inspection.stl.cancellation_block_bytes = 0ULL;
    result.work.publication.inspection.stl.cancellation_block_tokens = 0ULL;
    result.work.publication.inspection.stl.cancellation_block_facets = 0ULL;
    result.work.publication.inspection.stl.geometry.parallel_min_bytes = 0ULL;
    result.work.publication.inspection.stl.geometry.partition_bytes = 0ULL;
    result.work.publication.inspection.stl.geometry.cancellation_block_scalars =
        0ULL;
    result.work.publication.inspection.stl.geometry.max_jobs = 0ULL;
    return result;
  }
  throw std::invalid_argument("unknown OFF GLB execution preset");
}
}  // namespace meshvale::repair::python
