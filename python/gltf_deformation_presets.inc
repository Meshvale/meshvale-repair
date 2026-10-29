// SPDX-License-Identifier: Apache-2.0
// Exact positive application-policy projection from
// docs/presets/*-deformation-v1.json. Included only inside the owning
// gltf_presets.cpp private namespace.
GltfPreset MakeDeformationPreset(bool serial) {
  GltfPreset preset{};
  preset.execution.worker_budget = 4ULL;
  preset.execution.minimum_parallel_vertices = 65536ULL;
  preset.execution.tracked_payload_budget_bytes = 536870912ULL;
  preset.capture_limits.files.max_files = 256ULL;
  preset.capture_limits.files.max_resource_bindings = 2048ULL;
  preset.capture_limits.files.max_file_bytes = 16777216ULL;
  preset.capture_limits.files.max_total_file_bytes = 33554432ULL;
  preset.capture_limits.files.max_path_bytes = 1024ULL;
  preset.capture_limits.files.max_total_path_bytes = 262144ULL;
  preset.capture_limits.files.max_native_path_code_units = 4096ULL;
  preset.capture_limits.files.max_new_requested_bytes = 67108864ULL;
  preset.capture_limits.asset.source.max_json_bytes = 2097152ULL;
  preset.capture_limits.asset.source.max_resource_bytes = 16777216ULL;
  preset.capture_limits.asset.source.max_owned_source_resource_bytes =
      33554432ULL;
  preset.capture_limits.asset.source.max_document_nodes = 262144ULL;
  preset.capture_limits.asset.source.max_document_depth = 128ULL;
  preset.capture_limits.asset.source.max_decoded_string_bytes = 2097152ULL;
  preset.capture_limits.asset.source.max_numeric_token_bytes = 256ULL;
  preset.capture_limits.asset.source.max_decimal_coefficient_digits = 128ULL;
  preset.capture_limits.asset.source.max_decimal_exponent_digits = 8ULL;
  preset.capture_limits.asset.source.max_table_rows = 16384ULL;
  preset.capture_limits.asset.source.max_graph_edges = 65536ULL;
  preset.capture_limits.asset.source.max_instances = 4096ULL;
  preset.capture_limits.asset.source.max_accessor_elements = 2097152ULL;
  preset.capture_limits.asset.source.max_projection_bytes = 8388608ULL;
  preset.capture_limits.asset.max_new_requested_bytes = 25165824ULL;
  preset.capture_limits.container.max_container_bytes = 16777216ULL;
  preset.capture_limits.container.max_chunks = 1024ULL;
  preset.capture_limits.container.max_requested_bytes = 33554432ULL;
  preset.capture_work.files.io_block_bytes = 65536ULL;
  preset.capture_work.files.parallel_min_bytes = 262144ULL;
  preset.capture_work.files.partition_bytes = 262144ULL;
  preset.capture_work.files.cancellation_block_bytes = 65536ULL;
  preset.capture_work.files.max_jobs = 64ULL;
  preset.capture_work.asset.parallel_min_resource_bytes = 262144ULL;
  preset.capture_work.asset.resource_partition_bytes = 262144ULL;
  preset.capture_work.asset.parallel_min_channel_components = 65536ULL;
  preset.capture_work.asset.channel_partition_elements = 16384ULL;
  preset.capture_work.asset.direct_translation.parallel_min_keys = 16384ULL;
  preset.capture_work.asset.direct_translation.partition_keys = 16384ULL;
  preset.capture_work.asset.direct_translation.cancellation_block_keys = 256ULL;
  preset.capture_work.asset.direct_translation.max_jobs = 64ULL;
  preset.capture_work.container.parallel_min_bytes = 262144ULL;
  preset.capture_work.container.partition_bytes = 1048576ULL;
  preset.workflow_limits.memory.max_new_requested_bytes = 184549376ULL;
  preset.workflow_limits.memory.max_input_id_bytes = 128ULL;
  preset.workflow_limits.memory.max_source_protection_bytes = 50331648ULL;
  preset.workflow_limits.memory.max_source_binding_records = 2048ULL;
  preset.workflow_limits.memory.max_reload_bindings = 2048ULL;
  preset.workflow_limits.memory.max_report_bytes = 16777216ULL;
  preset.workflow_limits.memory.extraction.max_components = 16777216ULL;
  preset.workflow_limits.memory.extraction.max_triangle_corners = 786432ULL;
  preset.workflow_limits.memory.extraction.max_output_requested_bytes =
      25165824ULL;
  preset.workflow_limits.memory.extraction.direct_translation.max_node_rows =
      65536ULL;
  preset.workflow_limits.memory.extraction.direct_translation.max_mesh_users =
      65536ULL;
  preset.workflow_limits.memory.extraction.direct_translation
      .max_channel_references = 4096ULL;
  preset.workflow_limits.memory.extraction.direct_translation
      .max_distinct_samplers = 4096ULL;
  preset.workflow_limits.memory.extraction.direct_translation
      .max_keys_per_sampler = 65536ULL;
  preset.workflow_limits.memory.extraction.direct_translation.max_total_keys =
      262144ULL;
  preset.workflow_limits.memory.extraction.direct_translation
      .max_relationship_steps = 1048576ULL;
  preset.workflow_limits.memory.extraction.direct_translation
      .max_metadata_bytes = 2097152ULL;
  preset.workflow_limits.memory.extraction.direct_translation
      .max_decoded_bytes = 4194304ULL;
  preset.workflow_limits.memory.extraction.direct_translation
      .max_new_requested_bytes = 8388608ULL;
  preset.workflow_limits.memory.materialization.max_channels = 128ULL;
  preset.workflow_limits.memory.materialization.max_bridge_new_requested_bytes =
      4194304ULL;
  preset.workflow_limits.memory.materialization.materialization.max_vertices =
      262144ULL;
  preset.workflow_limits.memory.materialization.materialization.max_faces =
      262144ULL;
  preset.workflow_limits.memory.materialization.materialization.max_corners =
      786432ULL;
  preset.workflow_limits.memory.materialization.materialization
      .max_face_offsets = 262145ULL;
  preset.workflow_limits.memory.materialization.materialization.max_attributes =
      128ULL;
  preset.workflow_limits.memory.materialization.materialization
      .max_attribute_scalars = 16777216ULL;
  preset.workflow_limits.memory.materialization.materialization
      .max_attribute_offsets = 3000032ULL;
  preset.workflow_limits.memory.materialization.materialization
      .max_presence_bytes = 1048576ULL;
  preset.workflow_limits.memory.materialization.materialization
      .max_metadata_entries = 1024ULL;
  preset.workflow_limits.memory.materialization.materialization
      .max_descriptor_bytes = 2097152ULL;
  preset.workflow_limits.memory.materialization.materialization
      .max_new_requested_bytes = 33554432ULL;
  preset.workflow_limits.memory.materialization.inspection.max_issues = 1024ULL;
  preset.workflow_limits.memory.materialization.inspection
      .max_new_requested_bytes = 4194304ULL;
  preset.workflow_limits.memory.repair.max_targets = 1000000ULL;
  preset.workflow_limits.memory.repair.max_row_local_declarations = 128ULL;
  preset.workflow_limits.memory.repair.max_declaration_name_bytes = 4096ULL;
  preset.workflow_limits.memory.repair.max_comparison_steps_per_target =
      16000000ULL;
  preset.workflow_limits.memory.repair.max_new_requested_bytes = 50331648ULL;
  preset.workflow_limits.memory.repair.inspection.max_issues = 1024ULL;
  preset.workflow_limits.memory.repair.inspection.max_new_requested_bytes =
      4194304ULL;
  preset.workflow_limits.memory.repair.selection.max_vertices = 262144ULL;
  preset.workflow_limits.memory.repair.selection.max_faces = 262144ULL;
  preset.workflow_limits.memory.repair.selection.max_corners = 786432ULL;
  preset.workflow_limits.memory.repair.selection.max_face_offsets = 262145ULL;
  preset.workflow_limits.memory.repair.selection.max_attributes = 128ULL;
  preset.workflow_limits.memory.repair.selection.max_attribute_scalars =
      16777216ULL;
  preset.workflow_limits.memory.repair.selection.max_attribute_offsets =
      3000032ULL;
  preset.workflow_limits.memory.repair.selection.max_presence_bytes =
      1048576ULL;
  preset.workflow_limits.memory.repair.selection.max_metadata_entries = 1024ULL;
  preset.workflow_limits.memory.repair.selection.max_descriptor_bytes =
      2097152ULL;
  preset.workflow_limits.memory.repair.selection.max_new_requested_bytes =
      33554432ULL;
  preset.workflow_limits.memory.writing.max_source_json_bytes = 2097152ULL;
  preset.workflow_limits.memory.writing.max_source_json_nodes = 262144ULL;
  preset.workflow_limits.memory.writing.max_source_json_depth = 128ULL;
  preset.workflow_limits.memory.writing.max_vertices = 262144ULL;
  preset.workflow_limits.memory.writing.max_channels = 128ULL;
  preset.workflow_limits.memory.writing.max_attribute_scalars = 16777216ULL;
  preset.workflow_limits.memory.writing.max_descriptor_bytes = 2097152ULL;
  preset.workflow_limits.memory.writing.max_candidate_faces = 262144ULL;
  preset.workflow_limits.memory.writing.max_candidate_corners = 786432ULL;
  preset.workflow_limits.memory.writing.max_index_bytes = 4194304ULL;
  preset.workflow_limits.memory.writing.output.max_json_bytes = 4194304ULL;
  preset.workflow_limits.memory.writing.output.max_resources = 16385ULL;
  preset.workflow_limits.memory.writing.output.max_resource_bytes = 16777216ULL;
  preset.workflow_limits.memory.writing.output.max_total_resource_bytes =
      37748736ULL;
  preset.workflow_limits.memory.writing.output.max_output_requested_bytes =
      41943040ULL;
  preset.workflow_limits.memory.writing.direct_translation.max_node_rows =
      65536ULL;
  preset.workflow_limits.memory.writing.direct_translation.max_mesh_users =
      65536ULL;
  preset.workflow_limits.memory.writing.direct_translation
      .max_channel_references = 4096ULL;
  preset.workflow_limits.memory.writing.direct_translation
      .max_distinct_samplers = 4096ULL;
  preset.workflow_limits.memory.writing.direct_translation
      .max_keys_per_sampler = 65536ULL;
  preset.workflow_limits.memory.writing.direct_translation.max_total_keys =
      262144ULL;
  preset.workflow_limits.memory.writing.direct_translation
      .max_relationship_steps = 1048576ULL;
  preset.workflow_limits.memory.writing.direct_translation.max_metadata_bytes =
      2097152ULL;
  preset.workflow_limits.memory.writing.direct_translation.max_decoded_bytes =
      4194304ULL;
  preset.workflow_limits.memory.writing.direct_translation
      .max_new_requested_bytes = 8388608ULL;
  preset.workflow_limits.memory.reload.source.max_json_bytes = 4194304ULL;
  preset.workflow_limits.memory.reload.source.max_resource_bytes = 16777216ULL;
  preset.workflow_limits.memory.reload.source.max_owned_source_resource_bytes =
      37748736ULL;
  preset.workflow_limits.memory.reload.source.max_document_nodes = 327680ULL;
  preset.workflow_limits.memory.reload.source.max_document_depth = 128ULL;
  preset.workflow_limits.memory.reload.source.max_decoded_string_bytes =
      4194304ULL;
  preset.workflow_limits.memory.reload.source.max_numeric_token_bytes = 256ULL;
  preset.workflow_limits.memory.reload.source.max_decimal_coefficient_digits =
      128ULL;
  preset.workflow_limits.memory.reload.source.max_decimal_exponent_digits =
      8ULL;
  preset.workflow_limits.memory.reload.source.max_table_rows = 16385ULL;
  preset.workflow_limits.memory.reload.source.max_graph_edges = 65536ULL;
  preset.workflow_limits.memory.reload.source.max_instances = 4096ULL;
  preset.workflow_limits.memory.reload.source.max_accessor_elements =
      2097152ULL;
  preset.workflow_limits.memory.reload.source.max_projection_bytes = 8388608ULL;
  preset.workflow_limits.memory.reload.max_new_requested_bytes = 25165824ULL;
  preset.workflow_limits.memory.reload_extraction.max_components = 16777216ULL;
  preset.workflow_limits.memory.reload_extraction.max_triangle_corners =
      786432ULL;
  preset.workflow_limits.memory.reload_extraction.max_output_requested_bytes =
      25165824ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_node_rows = 65536ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_mesh_users = 65536ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_channel_references = 4096ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_distinct_samplers = 4096ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_keys_per_sampler = 65536ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_total_keys = 262144ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_relationship_steps = 1048576ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_metadata_bytes = 2097152ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_decoded_bytes = 4194304ULL;
  preset.workflow_limits.memory.reload_extraction.direct_translation
      .max_new_requested_bytes = 8388608ULL;
  preset.workflow_limits.memory.verification.max_json_bytes = 4194304ULL;
  preset.workflow_limits.memory.verification.max_json_nodes = 327680ULL;
  preset.workflow_limits.memory.verification.max_json_depth = 128ULL;
  preset.workflow_limits.memory.verification.max_resources = 16385ULL;
  preset.workflow_limits.memory.verification.max_resource_bytes = 37748736ULL;
  preset.workflow_limits.memory.verification.max_vertices = 262144ULL;
  preset.workflow_limits.memory.verification.max_channels = 128ULL;
  preset.workflow_limits.memory.verification.max_attribute_scalars =
      16777216ULL;
  preset.workflow_limits.memory.verification.max_descriptor_bytes = 2097152ULL;
  preset.workflow_limits.memory.verification.max_candidate_faces = 262144ULL;
  preset.workflow_limits.memory.verification.max_candidate_corners = 786432ULL;
  preset.workflow_limits.memory.verification.max_index_bytes = 4194304ULL;
  preset.workflow_limits.memory.verification.max_new_requested_bytes =
      16777216ULL;
  preset.workflow_limits.memory.verification.direct_translation.max_node_rows =
      65536ULL;
  preset.workflow_limits.memory.verification.direct_translation.max_mesh_users =
      65536ULL;
  preset.workflow_limits.memory.verification.direct_translation
      .max_channel_references = 4096ULL;
  preset.workflow_limits.memory.verification.direct_translation
      .max_distinct_samplers = 4096ULL;
  preset.workflow_limits.memory.verification.direct_translation
      .max_keys_per_sampler = 65536ULL;
  preset.workflow_limits.memory.verification.direct_translation.max_total_keys =
      262144ULL;
  preset.workflow_limits.memory.verification.direct_translation
      .max_relationship_steps = 1048576ULL;
  preset.workflow_limits.memory.verification.direct_translation
      .max_metadata_bytes = 2097152ULL;
  preset.workflow_limits.memory.verification.direct_translation
      .max_decoded_bytes = 4194304ULL;
  preset.workflow_limits.memory.verification.direct_translation
      .max_new_requested_bytes = 8388608ULL;
  preset.workflow_limits.max_wrapper_new_requested_bytes = 41943040ULL;
  preset.workflow_limits.max_source_files = 256ULL;
  preset.workflow_limits.max_supplemental_files = 16ULL;
  preset.workflow_limits.max_report_path_bytes = 1024ULL;
  preset.workflow_limits.max_report_bytes = 16777216ULL;
  preset.workflow_limits.max_hash_input_bytes = 1073741824ULL;
  preset.workflow_limits.publication.files.max_files = 273ULL;
  preset.workflow_limits.publication.files.max_resource_bindings = 2048ULL;
  preset.workflow_limits.publication.files.max_file_bytes = 16777216ULL;
  preset.workflow_limits.publication.files.max_total_file_bytes = 67108864ULL;
  preset.workflow_limits.publication.files.max_path_bytes = 1024ULL;
  preset.workflow_limits.publication.files.max_total_path_bytes = 262144ULL;
  preset.workflow_limits.publication.files.max_native_path_code_units = 4096ULL;
  preset.workflow_limits.publication.files.max_new_requested_bytes =
      67108864ULL;
  preset.workflow_limits.publication.reload.source.max_json_bytes = 4194304ULL;
  preset.workflow_limits.publication.reload.source.max_resource_bytes =
      16777216ULL;
  preset.workflow_limits.publication.reload.source
      .max_owned_source_resource_bytes = 37748736ULL;
  preset.workflow_limits.publication.reload.source.max_document_nodes =
      327680ULL;
  preset.workflow_limits.publication.reload.source.max_document_depth = 128ULL;
  preset.workflow_limits.publication.reload.source.max_decoded_string_bytes =
      4194304ULL;
  preset.workflow_limits.publication.reload.source.max_numeric_token_bytes =
      256ULL;
  preset.workflow_limits.publication.reload.source
      .max_decimal_coefficient_digits = 128ULL;
  preset.workflow_limits.publication.reload.source.max_decimal_exponent_digits =
      8ULL;
  preset.workflow_limits.publication.reload.source.max_table_rows = 16385ULL;
  preset.workflow_limits.publication.reload.source.max_graph_edges = 65536ULL;
  preset.workflow_limits.publication.reload.source.max_instances = 4096ULL;
  preset.workflow_limits.publication.reload.source.max_accessor_elements =
      2097152ULL;
  preset.workflow_limits.publication.reload.source.max_projection_bytes =
      8388608ULL;
  preset.workflow_limits.publication.reload.max_new_requested_bytes =
      25165824ULL;
  preset.workflow_limits.publication.extraction.max_components = 16777216ULL;
  preset.workflow_limits.publication.extraction.max_triangle_corners =
      786432ULL;
  preset.workflow_limits.publication.extraction.max_output_requested_bytes =
      25165824ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_node_rows = 65536ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_mesh_users = 65536ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_channel_references = 4096ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_distinct_samplers = 4096ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_keys_per_sampler = 65536ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_total_keys = 262144ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_relationship_steps = 1048576ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_metadata_bytes = 2097152ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_decoded_bytes = 4194304ULL;
  preset.workflow_limits.publication.extraction.direct_translation
      .max_new_requested_bytes = 8388608ULL;
  preset.workflow_limits.publication.inspection.max_json_bytes = 4194304ULL;
  preset.workflow_limits.publication.inspection.max_json_nodes = 327680ULL;
  preset.workflow_limits.publication.inspection.max_json_depth = 128ULL;
  preset.workflow_limits.publication.inspection.max_resources = 16385ULL;
  preset.workflow_limits.publication.inspection.max_resource_bytes =
      37748736ULL;
  preset.workflow_limits.publication.inspection.max_vertices = 262144ULL;
  preset.workflow_limits.publication.inspection.max_channels = 128ULL;
  preset.workflow_limits.publication.inspection.max_attribute_scalars =
      16777216ULL;
  preset.workflow_limits.publication.inspection.max_descriptor_bytes =
      2097152ULL;
  preset.workflow_limits.publication.inspection.max_candidate_faces = 262144ULL;
  preset.workflow_limits.publication.inspection.max_candidate_corners =
      786432ULL;
  preset.workflow_limits.publication.inspection.max_index_bytes = 4194304ULL;
  preset.workflow_limits.publication.inspection.max_new_requested_bytes =
      16777216ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_node_rows = 65536ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_mesh_users = 65536ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_channel_references = 4096ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_distinct_samplers = 4096ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_keys_per_sampler = 65536ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_total_keys = 262144ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_relationship_steps = 1048576ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_metadata_bytes = 2097152ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_decoded_bytes = 4194304ULL;
  preset.workflow_limits.publication.inspection.direct_translation
      .max_new_requested_bytes = 8388608ULL;
  preset.workflow_limits.publication.container.max_container_bytes =
      16777216ULL;
  preset.workflow_limits.publication.container.max_chunks = 1024ULL;
  preset.workflow_limits.publication.container.max_requested_bytes =
      33554432ULL;
  preset.workflow_work.memory.interchange.parallel_min_resource_bytes =
      262144ULL;
  preset.workflow_work.memory.interchange.resource_partition_bytes = 262144ULL;
  preset.workflow_work.memory.interchange.parallel_min_channel_components =
      65536ULL;
  preset.workflow_work.memory.interchange.channel_partition_elements = 16384ULL;
  preset.workflow_work.memory.interchange.direct_translation.parallel_min_keys =
      16384ULL;
  preset.workflow_work.memory.interchange.direct_translation.partition_keys =
      16384ULL;
  preset.workflow_work.memory.interchange.direct_translation
      .cancellation_block_keys = 256ULL;
  preset.workflow_work.memory.interchange.direct_translation.max_jobs = 64ULL;
  preset.workflow_work.memory.geometry.parallel_min_bytes = 262144ULL;
  preset.workflow_work.memory.geometry.partition_bytes = 262144ULL;
  preset.workflow_work.memory.geometry.cancellation_block_scalars = 8192ULL;
  preset.workflow_work.memory.geometry.max_jobs = 64ULL;
  preset.workflow_work.memory.writing.parallel_min_bytes = 262144ULL;
  preset.workflow_work.memory.writing.partition_bytes = 262144ULL;
  preset.workflow_work.memory.writing.cancellation_block_bytes = 65536ULL;
  preset.workflow_work.memory.writing.max_jobs = 64ULL;
  preset.workflow_work.memory.writing.direct_translation.parallel_min_keys =
      16384ULL;
  preset.workflow_work.memory.writing.direct_translation.partition_keys =
      16384ULL;
  preset.workflow_work.memory.writing.direct_translation
      .cancellation_block_keys = 256ULL;
  preset.workflow_work.memory.writing.direct_translation.max_jobs = 64ULL;
  preset.workflow_work.memory.verification.parallel_min_bytes = 262144ULL;
  preset.workflow_work.memory.verification.partition_bytes = 262144ULL;
  preset.workflow_work.memory.verification.cancellation_block_bytes = 65536ULL;
  preset.workflow_work.memory.verification.max_jobs = 64ULL;
  preset.workflow_work.memory.verification.direct_translation
      .parallel_min_keys = 16384ULL;
  preset.workflow_work.memory.verification.direct_translation.partition_keys =
      16384ULL;
  preset.workflow_work.memory.verification.direct_translation
      .cancellation_block_keys = 256ULL;
  preset.workflow_work.memory.verification.direct_translation.max_jobs = 64ULL;
  preset.workflow_work.memory.report.parallel_min_bytes = 65536ULL;
  preset.workflow_work.memory.report.partition_bytes = 65536ULL;
  preset.workflow_work.memory.report.cancellation_block_bytes = 16384ULL;
  preset.workflow_work.memory.report.max_jobs = 64ULL;
  preset.workflow_work.hashing.parallel_min_bytes = 262144ULL;
  preset.workflow_work.hashing.partition_bytes = 1048576ULL;
  preset.workflow_work.hashing.cancellation_block_bytes = 65536ULL;
  preset.workflow_work.hashing.max_jobs = 64ULL;
  preset.workflow_work.report.parallel_min_bytes = 65536ULL;
  preset.workflow_work.report.partition_bytes = 65536ULL;
  preset.workflow_work.report.cancellation_block_bytes = 16384ULL;
  preset.workflow_work.report.max_jobs = 64ULL;
  preset.workflow_work.publication.files.io_block_bytes = 65536ULL;
  preset.workflow_work.publication.files.parallel_min_bytes = 262144ULL;
  preset.workflow_work.publication.files.partition_bytes = 262144ULL;
  preset.workflow_work.publication.files.cancellation_block_bytes = 65536ULL;
  preset.workflow_work.publication.files.max_jobs = 64ULL;
  preset.workflow_work.publication.reload.parallel_min_resource_bytes =
      262144ULL;
  preset.workflow_work.publication.reload.resource_partition_bytes = 262144ULL;
  preset.workflow_work.publication.reload.parallel_min_channel_components =
      65536ULL;
  preset.workflow_work.publication.reload.channel_partition_elements = 16384ULL;
  preset.workflow_work.publication.reload.direct_translation.parallel_min_keys =
      16384ULL;
  preset.workflow_work.publication.reload.direct_translation.partition_keys =
      16384ULL;
  preset.workflow_work.publication.reload.direct_translation
      .cancellation_block_keys = 256ULL;
  preset.workflow_work.publication.reload.direct_translation.max_jobs = 64ULL;
  preset.workflow_work.publication.inspection.parallel_min_bytes = 262144ULL;
  preset.workflow_work.publication.inspection.partition_bytes = 262144ULL;
  preset.workflow_work.publication.inspection.cancellation_block_bytes =
      65536ULL;
  preset.workflow_work.publication.inspection.max_jobs = 64ULL;
  preset.workflow_work.publication.inspection.direct_translation
      .parallel_min_keys = 16384ULL;
  preset.workflow_work.publication.inspection.direct_translation
      .partition_keys = 16384ULL;
  preset.workflow_work.publication.inspection.direct_translation
      .cancellation_block_keys = 256ULL;
  preset.workflow_work.publication.inspection.direct_translation.max_jobs =
      64ULL;
  preset.workflow_work.publication.container.parallel_min_bytes = 262144ULL;
  preset.workflow_work.publication.container.partition_bytes = 1048576ULL;
  preset.capture_options.read_request.deformation.limits.max_node_rows =
      4096ULL;
  preset.capture_options.read_request.deformation.limits.max_mesh_users =
      4096ULL;
  preset.capture_options.read_request.deformation.limits.max_skin_rows = 256ULL;
  preset.capture_options.read_request.deformation.limits.max_joint_references =
      16384ULL;
  preset.capture_options.read_request.deformation.limits.max_influence_sets =
      16ULL;
  preset.capture_options.read_request.deformation.limits.max_lanes_per_vertex =
      64ULL;
  preset.capture_options.read_request.deformation.limits.max_morph_targets =
      64ULL;
  preset.capture_options.read_request.deformation.limits.max_morph_channels =
      512ULL;
  preset.capture_options.read_request.deformation.limits
      .max_animation_channels = 1024ULL;
  preset.capture_options.read_request.deformation.limits.max_samplers = 1024ULL;
  preset.capture_options.read_request.deformation.limits.max_keys = 262144ULL;
  preset.capture_options.read_request.deformation.limits.max_scalar_values =
      16777216ULL;
  preset.capture_options.read_request.deformation.limits.max_sparse_writes =
      1048576ULL;
  preset.capture_options.read_request.deformation.limits.max_role_edges =
      65536ULL;
  preset.capture_options.read_request.deformation.limits
      .max_relationship_steps = 2097152ULL;
  preset.capture_options.read_request.deformation.limits.max_descriptor_bytes =
      2097152ULL;
  preset.capture_options.read_request.deformation.limits.max_decoded_bytes =
      16777216ULL;
  preset.capture_options.read_request.deformation.limits
      .max_new_requested_bytes = 4194304ULL;
  preset.capture_options.read_request.deformation.work.parallel_min_scalars =
      16384ULL;
  preset.capture_options.read_request.deformation.work.partition_scalars =
      16384ULL;
  preset.capture_options.read_request.deformation.work
      .cancellation_block_scalars = 256ULL;
  preset.capture_options.read_request.deformation.work.parallel_min_bytes =
      262144ULL;
  preset.capture_options.read_request.deformation.work.partition_bytes =
      262144ULL;
  preset.capture_options.read_request.deformation.work
      .cancellation_block_bytes = 16384ULL;
  preset.capture_options.read_request.deformation.work.max_jobs = 64ULL;
  preset.capture_options.read_request.deformation.work.max_relationship_block =
      256ULL;
  preset.deformation.source_read.limits.max_node_rows = 4096ULL;
  preset.deformation.source_read.limits.max_mesh_users = 4096ULL;
  preset.deformation.source_read.limits.max_skin_rows = 256ULL;
  preset.deformation.source_read.limits.max_joint_references = 16384ULL;
  preset.deformation.source_read.limits.max_influence_sets = 16ULL;
  preset.deformation.source_read.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation.source_read.limits.max_morph_targets = 64ULL;
  preset.deformation.source_read.limits.max_morph_channels = 512ULL;
  preset.deformation.source_read.limits.max_animation_channels = 1024ULL;
  preset.deformation.source_read.limits.max_samplers = 1024ULL;
  preset.deformation.source_read.limits.max_keys = 262144ULL;
  preset.deformation.source_read.limits.max_scalar_values = 16777216ULL;
  preset.deformation.source_read.limits.max_sparse_writes = 1048576ULL;
  preset.deformation.source_read.limits.max_role_edges = 65536ULL;
  preset.deformation.source_read.limits.max_relationship_steps = 2097152ULL;
  preset.deformation.source_read.limits.max_descriptor_bytes = 2097152ULL;
  preset.deformation.source_read.limits.max_decoded_bytes = 16777216ULL;
  preset.deformation.source_read.limits.max_new_requested_bytes = 4194304ULL;
  preset.deformation.source_read.work.parallel_min_scalars = 16384ULL;
  preset.deformation.source_read.work.partition_scalars = 16384ULL;
  preset.deformation.source_read.work.cancellation_block_scalars = 256ULL;
  preset.deformation.source_read.work.parallel_min_bytes = 262144ULL;
  preset.deformation.source_read.work.partition_bytes = 262144ULL;
  preset.deformation.source_read.work.cancellation_block_bytes = 16384ULL;
  preset.deformation.source_read.work.max_jobs = 64ULL;
  preset.deformation.source_read.work.max_relationship_block = 256ULL;
  preset.deformation.materialization.limits.max_node_rows = 4096ULL;
  preset.deformation.materialization.limits.max_mesh_users = 4096ULL;
  preset.deformation.materialization.limits.max_skin_rows = 256ULL;
  preset.deformation.materialization.limits.max_joint_references = 16384ULL;
  preset.deformation.materialization.limits.max_influence_sets = 16ULL;
  preset.deformation.materialization.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation.materialization.limits.max_morph_targets = 64ULL;
  preset.deformation.materialization.limits.max_morph_channels = 512ULL;
  preset.deformation.materialization.limits.max_animation_channels = 1024ULL;
  preset.deformation.materialization.limits.max_samplers = 1024ULL;
  preset.deformation.materialization.limits.max_keys = 262144ULL;
  preset.deformation.materialization.limits.max_scalar_values = 16777216ULL;
  preset.deformation.materialization.limits.max_sparse_writes = 1048576ULL;
  preset.deformation.materialization.limits.max_role_edges = 65536ULL;
  preset.deformation.materialization.limits.max_relationship_steps = 2097152ULL;
  preset.deformation.materialization.limits.max_descriptor_bytes = 2097152ULL;
  preset.deformation.materialization.limits.max_decoded_bytes = 16777216ULL;
  preset.deformation.materialization.limits.max_new_requested_bytes =
      2097152ULL;
  preset.deformation.materialization.work.parallel_min_scalars = 16384ULL;
  preset.deformation.materialization.work.partition_scalars = 16384ULL;
  preset.deformation.materialization.work.cancellation_block_scalars = 256ULL;
  preset.deformation.materialization.work.parallel_min_bytes = 262144ULL;
  preset.deformation.materialization.work.partition_bytes = 262144ULL;
  preset.deformation.materialization.work.cancellation_block_bytes = 16384ULL;
  preset.deformation.materialization.work.max_jobs = 64ULL;
  preset.deformation.materialization.work.max_relationship_block = 256ULL;
  preset.deformation.writing.limits.max_node_rows = 4096ULL;
  preset.deformation.writing.limits.max_mesh_users = 4096ULL;
  preset.deformation.writing.limits.max_skin_rows = 256ULL;
  preset.deformation.writing.limits.max_joint_references = 16384ULL;
  preset.deformation.writing.limits.max_influence_sets = 16ULL;
  preset.deformation.writing.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation.writing.limits.max_morph_targets = 64ULL;
  preset.deformation.writing.limits.max_morph_channels = 512ULL;
  preset.deformation.writing.limits.max_animation_channels = 1024ULL;
  preset.deformation.writing.limits.max_samplers = 1024ULL;
  preset.deformation.writing.limits.max_keys = 262144ULL;
  preset.deformation.writing.limits.max_scalar_values = 16777216ULL;
  preset.deformation.writing.limits.max_sparse_writes = 1048576ULL;
  preset.deformation.writing.limits.max_role_edges = 65536ULL;
  preset.deformation.writing.limits.max_relationship_steps = 2097152ULL;
  preset.deformation.writing.limits.max_descriptor_bytes = 2097152ULL;
  preset.deformation.writing.limits.max_decoded_bytes = 16777216ULL;
  preset.deformation.writing.limits.max_new_requested_bytes = 4194304ULL;
  preset.deformation.writing.work.parallel_min_scalars = 16384ULL;
  preset.deformation.writing.work.partition_scalars = 16384ULL;
  preset.deformation.writing.work.cancellation_block_scalars = 256ULL;
  preset.deformation.writing.work.parallel_min_bytes = 262144ULL;
  preset.deformation.writing.work.partition_bytes = 262144ULL;
  preset.deformation.writing.work.cancellation_block_bytes = 16384ULL;
  preset.deformation.writing.work.max_jobs = 64ULL;
  preset.deformation.writing.work.max_relationship_block = 256ULL;
  preset.deformation.reload_read.limits.max_node_rows = 4096ULL;
  preset.deformation.reload_read.limits.max_mesh_users = 4096ULL;
  preset.deformation.reload_read.limits.max_skin_rows = 256ULL;
  preset.deformation.reload_read.limits.max_joint_references = 16384ULL;
  preset.deformation.reload_read.limits.max_influence_sets = 16ULL;
  preset.deformation.reload_read.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation.reload_read.limits.max_morph_targets = 64ULL;
  preset.deformation.reload_read.limits.max_morph_channels = 512ULL;
  preset.deformation.reload_read.limits.max_animation_channels = 1024ULL;
  preset.deformation.reload_read.limits.max_samplers = 1024ULL;
  preset.deformation.reload_read.limits.max_keys = 262144ULL;
  preset.deformation.reload_read.limits.max_scalar_values = 16777216ULL;
  preset.deformation.reload_read.limits.max_sparse_writes = 1048576ULL;
  preset.deformation.reload_read.limits.max_role_edges = 65536ULL;
  preset.deformation.reload_read.limits.max_relationship_steps = 2097152ULL;
  preset.deformation.reload_read.limits.max_descriptor_bytes = 2097152ULL;
  preset.deformation.reload_read.limits.max_decoded_bytes = 16777216ULL;
  preset.deformation.reload_read.limits.max_new_requested_bytes = 4194304ULL;
  preset.deformation.reload_read.work.parallel_min_scalars = 16384ULL;
  preset.deformation.reload_read.work.partition_scalars = 16384ULL;
  preset.deformation.reload_read.work.cancellation_block_scalars = 256ULL;
  preset.deformation.reload_read.work.parallel_min_bytes = 262144ULL;
  preset.deformation.reload_read.work.partition_bytes = 262144ULL;
  preset.deformation.reload_read.work.cancellation_block_bytes = 16384ULL;
  preset.deformation.reload_read.work.max_jobs = 64ULL;
  preset.deformation.reload_read.work.max_relationship_block = 256ULL;
  preset.deformation.reload.limits.max_node_rows = 4096ULL;
  preset.deformation.reload.limits.max_mesh_users = 4096ULL;
  preset.deformation.reload.limits.max_skin_rows = 256ULL;
  preset.deformation.reload.limits.max_joint_references = 16384ULL;
  preset.deformation.reload.limits.max_influence_sets = 16ULL;
  preset.deformation.reload.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation.reload.limits.max_morph_targets = 64ULL;
  preset.deformation.reload.limits.max_morph_channels = 512ULL;
  preset.deformation.reload.limits.max_animation_channels = 1024ULL;
  preset.deformation.reload.limits.max_samplers = 1024ULL;
  preset.deformation.reload.limits.max_keys = 262144ULL;
  preset.deformation.reload.limits.max_scalar_values = 16777216ULL;
  preset.deformation.reload.limits.max_sparse_writes = 1048576ULL;
  preset.deformation.reload.limits.max_role_edges = 65536ULL;
  preset.deformation.reload.limits.max_relationship_steps = 2097152ULL;
  preset.deformation.reload.limits.max_descriptor_bytes = 2097152ULL;
  preset.deformation.reload.limits.max_decoded_bytes = 16777216ULL;
  preset.deformation.reload.limits.max_new_requested_bytes = 16777216ULL;
  preset.deformation.reload.work.parallel_min_scalars = 16384ULL;
  preset.deformation.reload.work.partition_scalars = 16384ULL;
  preset.deformation.reload.work.cancellation_block_scalars = 256ULL;
  preset.deformation.reload.work.parallel_min_bytes = 262144ULL;
  preset.deformation.reload.work.partition_bytes = 262144ULL;
  preset.deformation.reload.work.cancellation_block_bytes = 16384ULL;
  preset.deformation.reload.work.max_jobs = 64ULL;
  preset.deformation.reload.work.max_relationship_block = 256ULL;
  preset.deformation.verification.limits.max_node_rows = 4096ULL;
  preset.deformation.verification.limits.max_mesh_users = 4096ULL;
  preset.deformation.verification.limits.max_skin_rows = 256ULL;
  preset.deformation.verification.limits.max_joint_references = 16384ULL;
  preset.deformation.verification.limits.max_influence_sets = 16ULL;
  preset.deformation.verification.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation.verification.limits.max_morph_targets = 64ULL;
  preset.deformation.verification.limits.max_morph_channels = 512ULL;
  preset.deformation.verification.limits.max_animation_channels = 1024ULL;
  preset.deformation.verification.limits.max_samplers = 1024ULL;
  preset.deformation.verification.limits.max_keys = 262144ULL;
  preset.deformation.verification.limits.max_scalar_values = 16777216ULL;
  preset.deformation.verification.limits.max_sparse_writes = 1048576ULL;
  preset.deformation.verification.limits.max_role_edges = 65536ULL;
  preset.deformation.verification.limits.max_relationship_steps = 2097152ULL;
  preset.deformation.verification.limits.max_descriptor_bytes = 2097152ULL;
  preset.deformation.verification.limits.max_decoded_bytes = 16777216ULL;
  preset.deformation.verification.limits.max_new_requested_bytes = 8388608ULL;
  preset.deformation.verification.work.parallel_min_scalars = 16384ULL;
  preset.deformation.verification.work.partition_scalars = 16384ULL;
  preset.deformation.verification.work.cancellation_block_scalars = 256ULL;
  preset.deformation.verification.work.parallel_min_bytes = 262144ULL;
  preset.deformation.verification.work.partition_bytes = 262144ULL;
  preset.deformation.verification.work.cancellation_block_bytes = 16384ULL;
  preset.deformation.verification.work.max_jobs = 64ULL;
  preset.deformation.verification.work.max_relationship_block = 256ULL;
  preset.source_deformation.limits.max_node_rows = 4096ULL;
  preset.source_deformation.limits.max_mesh_users = 4096ULL;
  preset.source_deformation.limits.max_skin_rows = 256ULL;
  preset.source_deformation.limits.max_joint_references = 16384ULL;
  preset.source_deformation.limits.max_influence_sets = 16ULL;
  preset.source_deformation.limits.max_lanes_per_vertex = 64ULL;
  preset.source_deformation.limits.max_morph_targets = 64ULL;
  preset.source_deformation.limits.max_morph_channels = 512ULL;
  preset.source_deformation.limits.max_animation_channels = 1024ULL;
  preset.source_deformation.limits.max_samplers = 1024ULL;
  preset.source_deformation.limits.max_keys = 262144ULL;
  preset.source_deformation.limits.max_scalar_values = 16777216ULL;
  preset.source_deformation.limits.max_sparse_writes = 1048576ULL;
  preset.source_deformation.limits.max_role_edges = 65536ULL;
  preset.source_deformation.limits.max_relationship_steps = 2097152ULL;
  preset.source_deformation.limits.max_descriptor_bytes = 2097152ULL;
  preset.source_deformation.limits.max_decoded_bytes = 16777216ULL;
  preset.source_deformation.limits.max_new_requested_bytes = 16777216ULL;
  preset.source_deformation.work.parallel_min_scalars = 16384ULL;
  preset.source_deformation.work.partition_scalars = 16384ULL;
  preset.source_deformation.work.cancellation_block_scalars = 256ULL;
  preset.source_deformation.work.parallel_min_bytes = 262144ULL;
  preset.source_deformation.work.partition_bytes = 262144ULL;
  preset.source_deformation.work.cancellation_block_bytes = 16384ULL;
  preset.source_deformation.work.max_jobs = 64ULL;
  preset.source_deformation.work.max_relationship_block = 256ULL;
  preset.deformation_publication.read.limits.max_node_rows = 4096ULL;
  preset.deformation_publication.read.limits.max_mesh_users = 4096ULL;
  preset.deformation_publication.read.limits.max_skin_rows = 256ULL;
  preset.deformation_publication.read.limits.max_joint_references = 16384ULL;
  preset.deformation_publication.read.limits.max_influence_sets = 16ULL;
  preset.deformation_publication.read.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation_publication.read.limits.max_morph_targets = 64ULL;
  preset.deformation_publication.read.limits.max_morph_channels = 512ULL;
  preset.deformation_publication.read.limits.max_animation_channels = 1024ULL;
  preset.deformation_publication.read.limits.max_samplers = 1024ULL;
  preset.deformation_publication.read.limits.max_keys = 262144ULL;
  preset.deformation_publication.read.limits.max_scalar_values = 16777216ULL;
  preset.deformation_publication.read.limits.max_sparse_writes = 1048576ULL;
  preset.deformation_publication.read.limits.max_role_edges = 65536ULL;
  preset.deformation_publication.read.limits.max_relationship_steps =
      2097152ULL;
  preset.deformation_publication.read.limits.max_descriptor_bytes = 2097152ULL;
  preset.deformation_publication.read.limits.max_decoded_bytes = 16777216ULL;
  preset.deformation_publication.read.limits.max_new_requested_bytes =
      4194304ULL;
  preset.deformation_publication.read.work.parallel_min_scalars = 16384ULL;
  preset.deformation_publication.read.work.partition_scalars = 16384ULL;
  preset.deformation_publication.read.work.cancellation_block_scalars = 256ULL;
  preset.deformation_publication.read.work.parallel_min_bytes = 262144ULL;
  preset.deformation_publication.read.work.partition_bytes = 262144ULL;
  preset.deformation_publication.read.work.cancellation_block_bytes = 16384ULL;
  preset.deformation_publication.read.work.max_jobs = 64ULL;
  preset.deformation_publication.read.work.max_relationship_block = 256ULL;
  preset.deformation_publication.extraction.limits.max_node_rows = 4096ULL;
  preset.deformation_publication.extraction.limits.max_mesh_users = 4096ULL;
  preset.deformation_publication.extraction.limits.max_skin_rows = 256ULL;
  preset.deformation_publication.extraction.limits.max_joint_references =
      16384ULL;
  preset.deformation_publication.extraction.limits.max_influence_sets = 16ULL;
  preset.deformation_publication.extraction.limits.max_lanes_per_vertex = 64ULL;
  preset.deformation_publication.extraction.limits.max_morph_targets = 64ULL;
  preset.deformation_publication.extraction.limits.max_morph_channels = 512ULL;
  preset.deformation_publication.extraction.limits.max_animation_channels =
      1024ULL;
  preset.deformation_publication.extraction.limits.max_samplers = 1024ULL;
  preset.deformation_publication.extraction.limits.max_keys = 262144ULL;
  preset.deformation_publication.extraction.limits.max_scalar_values =
      16777216ULL;
  preset.deformation_publication.extraction.limits.max_sparse_writes =
      1048576ULL;
  preset.deformation_publication.extraction.limits.max_role_edges = 65536ULL;
  preset.deformation_publication.extraction.limits.max_relationship_steps =
      2097152ULL;
  preset.deformation_publication.extraction.limits.max_descriptor_bytes =
      2097152ULL;
  preset.deformation_publication.extraction.limits.max_decoded_bytes =
      16777216ULL;
  preset.deformation_publication.extraction.limits.max_new_requested_bytes =
      16777216ULL;
  preset.deformation_publication.extraction.work.parallel_min_scalars =
      16384ULL;
  preset.deformation_publication.extraction.work.partition_scalars = 16384ULL;
  preset.deformation_publication.extraction.work.cancellation_block_scalars =
      256ULL;
  preset.deformation_publication.extraction.work.parallel_min_bytes = 262144ULL;
  preset.deformation_publication.extraction.work.partition_bytes = 262144ULL;
  preset.deformation_publication.extraction.work.cancellation_block_bytes =
      16384ULL;
  preset.deformation_publication.extraction.work.max_jobs = 64ULL;
  preset.deformation_publication.extraction.work.max_relationship_block =
      256ULL;
  preset.deformation_publication.verification.limits.max_node_rows = 4096ULL;
  preset.deformation_publication.verification.limits.max_mesh_users = 4096ULL;
  preset.deformation_publication.verification.limits.max_skin_rows = 256ULL;
  preset.deformation_publication.verification.limits.max_joint_references =
      16384ULL;
  preset.deformation_publication.verification.limits.max_influence_sets = 16ULL;
  preset.deformation_publication.verification.limits.max_lanes_per_vertex =
      64ULL;
  preset.deformation_publication.verification.limits.max_morph_targets = 64ULL;
  preset.deformation_publication.verification.limits.max_morph_channels =
      512ULL;
  preset.deformation_publication.verification.limits.max_animation_channels =
      1024ULL;
  preset.deformation_publication.verification.limits.max_samplers = 1024ULL;
  preset.deformation_publication.verification.limits.max_keys = 262144ULL;
  preset.deformation_publication.verification.limits.max_scalar_values =
      16777216ULL;
  preset.deformation_publication.verification.limits.max_sparse_writes =
      1048576ULL;
  preset.deformation_publication.verification.limits.max_role_edges = 65536ULL;
  preset.deformation_publication.verification.limits.max_relationship_steps =
      2097152ULL;
  preset.deformation_publication.verification.limits.max_descriptor_bytes =
      2097152ULL;
  preset.deformation_publication.verification.limits.max_decoded_bytes =
      16777216ULL;
  preset.deformation_publication.verification.limits.max_new_requested_bytes =
      8388608ULL;
  preset.deformation_publication.verification.work.parallel_min_scalars =
      16384ULL;
  preset.deformation_publication.verification.work.partition_scalars = 16384ULL;
  preset.deformation_publication.verification.work.cancellation_block_scalars =
      256ULL;
  preset.deformation_publication.verification.work.parallel_min_bytes =
      262144ULL;
  preset.deformation_publication.verification.work.partition_bytes = 262144ULL;
  preset.deformation_publication.verification.work.cancellation_block_bytes =
      16384ULL;
  preset.deformation_publication.verification.work.max_jobs = 64ULL;
  preset.deformation_publication.verification.work.max_relationship_block =
      256ULL;
  if (serial) {
    preset.execution.worker_budget = 1ULL;
    preset.execution.minimum_parallel_vertices = 9223372036854775807ULL;
    preset.capture_work.files.parallel_min_bytes = 9223372036854775807ULL;
    preset.capture_work.files.max_jobs = 1ULL;
    preset.capture_work.asset.parallel_min_resource_bytes =
        9223372036854775807ULL;
    preset.capture_work.asset.parallel_min_channel_components =
        9223372036854775807ULL;
    preset.capture_work.asset.direct_translation.parallel_min_keys =
        9223372036854775807ULL;
    preset.capture_work.asset.direct_translation.max_jobs = 1ULL;
    preset.capture_work.container.parallel_min_bytes = 9223372036854775807ULL;
    preset.workflow_work.memory.interchange.parallel_min_resource_bytes =
        9223372036854775807ULL;
    preset.workflow_work.memory.interchange.parallel_min_channel_components =
        9223372036854775807ULL;
    preset.workflow_work.memory.interchange.direct_translation
        .parallel_min_keys = 9223372036854775807ULL;
    preset.workflow_work.memory.interchange.direct_translation.max_jobs = 1ULL;
    preset.workflow_work.memory.geometry.parallel_min_bytes =
        9223372036854775807ULL;
    preset.workflow_work.memory.geometry.max_jobs = 1ULL;
    preset.workflow_work.memory.writing.parallel_min_bytes =
        9223372036854775807ULL;
    preset.workflow_work.memory.writing.max_jobs = 1ULL;
    preset.workflow_work.memory.writing.direct_translation.parallel_min_keys =
        9223372036854775807ULL;
    preset.workflow_work.memory.writing.direct_translation.max_jobs = 1ULL;
    preset.workflow_work.memory.verification.parallel_min_bytes =
        9223372036854775807ULL;
    preset.workflow_work.memory.verification.max_jobs = 1ULL;
    preset.workflow_work.memory.verification.direct_translation
        .parallel_min_keys = 9223372036854775807ULL;
    preset.workflow_work.memory.verification.direct_translation.max_jobs = 1ULL;
    preset.workflow_work.memory.report.parallel_min_bytes =
        9223372036854775807ULL;
    preset.workflow_work.memory.report.max_jobs = 1ULL;
    preset.workflow_work.hashing.parallel_min_bytes = 9223372036854775807ULL;
    preset.workflow_work.hashing.max_jobs = 1ULL;
    preset.workflow_work.report.parallel_min_bytes = 9223372036854775807ULL;
    preset.workflow_work.report.max_jobs = 1ULL;
    preset.workflow_work.publication.files.parallel_min_bytes =
        9223372036854775807ULL;
    preset.workflow_work.publication.files.max_jobs = 1ULL;
    preset.workflow_work.publication.reload.parallel_min_resource_bytes =
        9223372036854775807ULL;
    preset.workflow_work.publication.reload.parallel_min_channel_components =
        9223372036854775807ULL;
    preset.workflow_work.publication.reload.direct_translation
        .parallel_min_keys = 9223372036854775807ULL;
    preset.workflow_work.publication.reload.direct_translation.max_jobs = 1ULL;
    preset.workflow_work.publication.inspection.parallel_min_bytes =
        9223372036854775807ULL;
    preset.workflow_work.publication.inspection.max_jobs = 1ULL;
    preset.workflow_work.publication.inspection.direct_translation
        .parallel_min_keys = 9223372036854775807ULL;
    preset.workflow_work.publication.inspection.direct_translation.max_jobs =
        1ULL;
    preset.workflow_work.publication.container.parallel_min_bytes =
        9223372036854775807ULL;
    preset.capture_options.read_request.deformation.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.capture_options.read_request.deformation.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.capture_options.read_request.deformation.work.max_jobs = 1ULL;
    preset.deformation.source_read.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation.source_read.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.deformation.source_read.work.max_jobs = 1ULL;
    preset.deformation.materialization.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation.materialization.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.deformation.materialization.work.max_jobs = 1ULL;
    preset.deformation.writing.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation.writing.work.parallel_min_bytes = 9223372036854775807ULL;
    preset.deformation.writing.work.max_jobs = 1ULL;
    preset.deformation.reload_read.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation.reload_read.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.deformation.reload_read.work.max_jobs = 1ULL;
    preset.deformation.reload.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation.reload.work.parallel_min_bytes = 9223372036854775807ULL;
    preset.deformation.reload.work.max_jobs = 1ULL;
    preset.deformation.verification.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation.verification.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.deformation.verification.work.max_jobs = 1ULL;
    preset.source_deformation.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.source_deformation.work.parallel_min_bytes = 9223372036854775807ULL;
    preset.source_deformation.work.max_jobs = 1ULL;
    preset.deformation_publication.read.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation_publication.read.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.deformation_publication.read.work.max_jobs = 1ULL;
    preset.deformation_publication.extraction.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation_publication.extraction.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.deformation_publication.extraction.work.max_jobs = 1ULL;
    preset.deformation_publication.verification.work.parallel_min_scalars =
        9223372036854775807ULL;
    preset.deformation_publication.verification.work.parallel_min_bytes =
        9223372036854775807ULL;
    preset.deformation_publication.verification.work.max_jobs = 1ULL;
  }
  return preset;
}
