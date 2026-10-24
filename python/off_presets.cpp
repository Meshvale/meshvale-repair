// SPDX-License-Identifier: Apache-2.0
#include "off_presets.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace meshvale::repair::python {
namespace {
// Complete application configuration owned by docs/off-presets.json.
constexpr OffPreset kBalanced = {
    .execution =
        {
            .worker_budget = 4,
            .minimum_parallel_vertices = 65536,
            .tracked_payload_budget_bytes = 536870912,
        },
    .capture_limits =
        {
            .files =
                {
                    .max_entry_bytes = 67108864,
                    .max_receipt_bytes = 16777216,
                    .max_total_file_bytes = 83886080,
                    .max_entry_name_bytes = 1024,
                    .max_receipt_name_bytes = 1024,
                    .max_native_path_code_units = 4096,
                    .max_file_new_requested_bytes = 134217728,
                },
            .off =
                {
                    .max_source_bytes = 67108864,
                    .max_vertices = 1000000,
                    .max_faces = 1000000,
                    .max_corners = 3000000,
                    .max_corners_per_face = 65536,
                    .max_line_bytes = 16777216,
                    .max_token_bytes = 256,
                    .max_comment_bytes = 1048576,
                    .max_codec_new_requested_bytes = 134217728,
                    .max_total_new_requested_bytes = 209715200,
                    .materialization =
                        {
                            .max_vertices = 1000000,
                            .max_faces = 1000000,
                            .max_corners = 3000000,
                            .max_face_offsets = 1000001,
                            .max_attributes = 1,
                            .max_attribute_scalars = 1,
                            .max_attribute_offsets = 1,
                            .max_presence_bytes = 1,
                            .max_metadata_entries = 1,
                            .max_descriptor_bytes = 1,
                            .max_new_requested_bytes = 67108864,
                        },
                    .inspection =
                        {
                            .max_issues = 1024,
                            .max_new_requested_bytes = 8388608,
                        },
                },
        },
    .capture_work =
        {
            .files =
                {
                    .io_block_bytes = 65536,
                    .parallel_min_bytes = 262144,
                    .partition_bytes = 262144,
                    .cancellation_block_bytes = 65536,
                    .max_jobs = 64,
                },
            .off =
                {
                    .cancellation_block_bytes = 65536,
                    .cancellation_block_tokens = 256,
                    .cancellation_block_records = 256,
                    .geometry =
                        {
                            .parallel_min_bytes = 262144,
                            .partition_bytes = 262144,
                            .cancellation_block_scalars = 8192,
                            .max_jobs = 64,
                        },
                },
        },
    .workflow_limits =
        {
            .max_wrapper_new_requested_bytes = 134217728,
            .max_input_id_bytes = 128,
            .max_source_protection_bytes = 67108864,
            .max_report_bytes = 16777216,
            .max_hash_input_bytes = 134217728,
            .max_verification_vertices = 1000000,
            .max_verification_faces = 1000000,
            .max_verification_corners = 3000000,
            .repair =
                {
                    .max_targets = 1000000,
                    .max_row_local_declarations = 1,
                    .max_declaration_name_bytes = 1,
                    .max_comparison_steps_per_target = 16000000,
                    .max_new_requested_bytes = 100663296,
                    .inspection =
                        {
                            .max_issues = 1024,
                            .max_new_requested_bytes = 8388608,
                        },
                    .selection =
                        {
                            .max_vertices = 1000000,
                            .max_faces = 1000000,
                            .max_corners = 3000000,
                            .max_face_offsets = 1000001,
                            .max_attributes = 1,
                            .max_attribute_scalars = 1,
                            .max_attribute_offsets = 1,
                            .max_presence_bytes = 1,
                            .max_metadata_entries = 1,
                            .max_descriptor_bytes = 1,
                            .max_new_requested_bytes = 67108864,
                        },
                },
            .writing =
                {
                    .max_output_bytes = 67108864,
                    .max_vertices = 1000000,
                    .max_faces = 1000000,
                    .max_corners = 3000000,
                    .max_corners_per_face = 65536,
                    .max_line_bytes = 16777216,
                    .max_token_bytes = 256,
                    .max_codec_new_requested_bytes = 268435456,
                    .max_total_new_requested_bytes = 276824064,
                    .inspection =
                        {
                            .max_issues = 1024,
                            .max_new_requested_bytes = 8388608,
                        },
                },
            .reload =
                {
                    .max_source_bytes = 67108864,
                    .max_vertices = 1000000,
                    .max_faces = 1000000,
                    .max_corners = 3000000,
                    .max_corners_per_face = 65536,
                    .max_line_bytes = 16777216,
                    .max_token_bytes = 256,
                    .max_comment_bytes = 1048576,
                    .max_codec_new_requested_bytes = 134217728,
                    .max_total_new_requested_bytes = 209715200,
                    .materialization =
                        {
                            .max_vertices = 1000000,
                            .max_faces = 1000000,
                            .max_corners = 3000000,
                            .max_face_offsets = 1000001,
                            .max_attributes = 1,
                            .max_attribute_scalars = 1,
                            .max_attribute_offsets = 1,
                            .max_presence_bytes = 1,
                            .max_metadata_entries = 1,
                            .max_descriptor_bytes = 1,
                            .max_new_requested_bytes = 67108864,
                        },
                    .inspection =
                        {
                            .max_issues = 1024,
                            .max_new_requested_bytes = 8388608,
                        },
                },
            .publication =
                {
                    .files =
                        {
                            .max_entry_bytes = 67108864,
                            .max_receipt_bytes = 16777216,
                            .max_total_file_bytes = 83886080,
                            .max_entry_name_bytes = 1024,
                            .max_receipt_name_bytes = 1024,
                            .max_native_path_code_units = 4096,
                            .max_file_new_requested_bytes = 134217728,
                        },
                    .expected_inspection =
                        {
                            .max_issues = 1024,
                            .max_new_requested_bytes = 8388608,
                        },
                    .reload =
                        {
                            .max_source_bytes = 67108864,
                            .max_vertices = 1000000,
                            .max_faces = 1000000,
                            .max_corners = 3000000,
                            .max_corners_per_face = 65536,
                            .max_line_bytes = 16777216,
                            .max_token_bytes = 256,
                            .max_comment_bytes = 1048576,
                            .max_codec_new_requested_bytes = 134217728,
                            .max_total_new_requested_bytes = 209715200,
                            .materialization =
                                {
                                    .max_vertices = 1000000,
                                    .max_faces = 1000000,
                                    .max_corners = 3000000,
                                    .max_face_offsets = 1000001,
                                    .max_attributes = 1,
                                    .max_attribute_scalars = 1,
                                    .max_attribute_offsets = 1,
                                    .max_presence_bytes = 1,
                                    .max_metadata_entries = 1,
                                    .max_descriptor_bytes = 1,
                                    .max_new_requested_bytes = 67108864,
                                },
                            .inspection =
                                {
                                    .max_issues = 1024,
                                    .max_new_requested_bytes = 8388608,
                                },
                        },
                },
        },
    .workflow_work =
        {
            .repair =
                {
                    .parallel_min_bytes = 262144,
                    .partition_bytes = 262144,
                    .cancellation_block_scalars = 8192,
                    .max_jobs = 64,
                },
            .writing =
                {
                    .cancellation_block_bytes = 65536,
                    .cancellation_block_tokens = 256,
                    .cancellation_block_records = 256,
                    .geometry =
                        {
                            .parallel_min_bytes = 262144,
                            .partition_bytes = 262144,
                            .cancellation_block_scalars = 8192,
                            .max_jobs = 64,
                        },
                },
            .reload =
                {
                    .cancellation_block_bytes = 65536,
                    .cancellation_block_tokens = 256,
                    .cancellation_block_records = 256,
                    .geometry =
                        {
                            .parallel_min_bytes = 262144,
                            .partition_bytes = 262144,
                            .cancellation_block_scalars = 8192,
                            .max_jobs = 64,
                        },
                },
            .verification =
                {
                    .parallel_min_bytes = 262144,
                    .partition_bytes = 262144,
                    .cancellation_block_bytes = 65536,
                    .max_jobs = 64,
                },
            .hashing =
                {
                    .parallel_min_bytes = 262144,
                    .partition_bytes = 262144,
                    .cancellation_block_bytes = 65536,
                    .max_jobs = 64,
                },
            .report =
                {
                    .parallel_min_bytes = 65536,
                    .partition_bytes = 65536,
                    .cancellation_block_bytes = 16384,
                    .max_jobs = 64,
                },
            .publication =
                {
                    .files =
                        {
                            .io_block_bytes = 65536,
                            .parallel_min_bytes = 262144,
                            .partition_bytes = 262144,
                            .cancellation_block_bytes = 65536,
                            .max_jobs = 64,
                        },
                    .expected_inspection =
                        {
                            .parallel_min_bytes = 262144,
                            .partition_bytes = 262144,
                            .cancellation_block_scalars = 8192,
                            .max_jobs = 64,
                        },
                    .reload =
                        {
                            .cancellation_block_bytes = 65536,
                            .cancellation_block_tokens = 256,
                            .cancellation_block_records = 256,
                            .geometry =
                                {
                                    .parallel_min_bytes = 262144,
                                    .partition_bytes = 262144,
                                    .cancellation_block_scalars = 8192,
                                    .max_jobs = 64,
                                },
                        },
                },
        },
};
constexpr auto kSerialThreshold = std::numeric_limits<std::int64_t>::max();
}  // namespace

OffPreset MakeOffPreset(std::string_view name) {
  if (std::numeric_limits<std::size_t>::max() < kSerialThreshold)
    throw std::invalid_argument("OFF execution presets require a 64-bit host");
  OffPreset preset = kBalanced;
  if (name == kBalancedOffPreset) return preset;
  if (name != kSerialOffPreset)
    throw std::invalid_argument("unknown OFF execution preset");
  preset.execution.worker_budget = 1;
  preset.execution.minimum_parallel_vertices = kSerialThreshold;
  preset.capture_work.files.parallel_min_bytes = kSerialThreshold;
  preset.capture_work.files.max_jobs = 1;
  preset.capture_work.off.geometry.parallel_min_bytes = kSerialThreshold;
  preset.capture_work.off.geometry.max_jobs = 1;
  preset.workflow_work.repair.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.repair.max_jobs = 1;
  preset.workflow_work.writing.geometry.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.writing.geometry.max_jobs = 1;
  preset.workflow_work.reload.geometry.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.reload.geometry.max_jobs = 1;
  preset.workflow_work.verification.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.verification.max_jobs = 1;
  preset.workflow_work.hashing.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.hashing.max_jobs = 1;
  preset.workflow_work.report.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.report.max_jobs = 1;
  preset.workflow_work.publication.files.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.publication.files.max_jobs = 1;
  preset.workflow_work.publication.expected_inspection.parallel_min_bytes =
      kSerialThreshold;
  preset.workflow_work.publication.expected_inspection.max_jobs = 1;
  preset.workflow_work.publication.reload.geometry.parallel_min_bytes =
      kSerialThreshold;
  preset.workflow_work.publication.reload.geometry.max_jobs = 1;
  return preset;
}
}  // namespace meshvale::repair::python
