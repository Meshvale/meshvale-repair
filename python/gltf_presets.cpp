// SPDX-License-Identifier: Apache-2.0
#include "gltf_presets.h"

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>
#include <meshvale/interchange/gltf_write.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace meshvale::repair::python {
namespace {
// Application configuration: complete positive caps, independent of fixtures.
constexpr geometry::ExecutionOptions kExecution{
    .worker_budget = 4,
    .minimum_parallel_vertices = 65536,
    .tracked_payload_budget_bytes = 536870912,
};
constexpr interchange::GltfFileLimits kCaptureFiles{
    .max_files = 1024,
    .max_resource_bindings = 2048,
    .max_file_bytes = 67108864,
    .max_total_file_bytes = 268435456,
    .max_path_bytes = 1024,
    .max_total_path_bytes = 1048576,
    .max_native_path_code_units = 4096,
    .max_new_requested_bytes = 301989888,
};
constexpr interchange::GltfFileLimits kPublicationFiles{
    .max_files = 1041,
    .max_resource_bindings = 2048,
    .max_file_bytes = 67108864,
    .max_total_file_bytes = 268435456,
    .max_path_bytes = 1024,
    .max_total_path_bytes = 1048576,
    .max_native_path_code_units = 4096,
    .max_new_requested_bytes = 301989888,
};
constexpr interchange::GltfReadLimits kCaptureReadSource{
    .max_json_bytes = 16777216,
    .max_resource_bytes = 67108864,
    .max_owned_source_resource_bytes = 268435456,
    .max_document_nodes = 1000000,
    .max_document_depth = 128,
    .max_decoded_string_bytes = 16777216,
    .max_numeric_token_bytes = 256,
    .max_decimal_coefficient_digits = 128,
    .max_decimal_exponent_digits = 8,
    .max_table_rows = 65536,
    .max_graph_edges = 1000000,
    .max_instances = 100000,
    .max_accessor_elements = 3000000,
    .max_projection_bytes = 50331648,
};
constexpr interchange::GltfReadLimits kReloadReadSource{
    .max_json_bytes = 50331648,
    .max_resource_bytes = 67108864,
    .max_owned_source_resource_bytes = 285212672,
    .max_document_nodes = 1100000,
    .max_document_depth = 128,
    .max_decoded_string_bytes = 33554432,
    .max_numeric_token_bytes = 256,
    .max_decimal_coefficient_digits = 128,
    .max_decimal_exponent_digits = 8,
    .max_table_rows = 65537,
    .max_graph_edges = 1000000,
    .max_instances = 100000,
    .max_accessor_elements = 3000000,
    .max_projection_bytes = 50331648,
};
constexpr interchange::GltfReadOperationLimits kCaptureReader{
    .source = kCaptureReadSource,
    .max_new_requested_bytes = 134217728,
};
constexpr interchange::GltfReadOperationLimits kReloadReader{
    .source = kReloadReadSource,
    .max_new_requested_bytes = 100663296,
};
constexpr interchange::GlbLimits kContainer{
    .max_container_bytes = 67108864,
    .max_chunks = 1024,
    .max_requested_bytes = 201326592,
};
constexpr interchange::GltfFileReadLimits kCaptureLimits{
    .files = kCaptureFiles,
    .asset = kCaptureReader,
    .container = kContainer,
};
constexpr interchange::GltfExtractionLimits kExtraction{
    .max_components = 8000000,
    .max_triangle_corners = 3000000,
    .max_output_requested_bytes = 33554432,
};
constexpr geometry::MeshMaterializationLimits kGeometryMaterialization{
    .max_vertices = 1000000,
    .max_faces = 1000000,
    .max_corners = 3000000,
    .max_face_offsets = 1000001,
    .max_attributes = 32,
    .max_attribute_scalars = 8000000,
    .max_attribute_offsets = 3000032,
    .max_presence_bytes = 1048576,
    .max_metadata_entries = 1024,
    .max_descriptor_bytes = 1048576,
    .max_new_requested_bytes = 33554432,
};
constexpr geometry::MeshInspectionLimits kGeometryInspection{
    .max_issues = 1024,
    .max_new_requested_bytes = 8388608,
};
constexpr interchange::GltfMaterializationLimits kMaterialization{
    .max_channels = 32,
    .max_bridge_new_requested_bytes = 8388608,
    .materialization = kGeometryMaterialization,
    .inspection = kGeometryInspection,
};
constexpr MaterializedDuplicateLimits kRepair{
    .max_targets = 1000000,
    .max_row_local_declarations = 128,
    .max_declaration_name_bytes = 256,
    .max_comparison_steps_per_target = 16000000,
    .max_new_requested_bytes = 100663296,
    .inspection = kGeometryInspection,
    .selection = kGeometryMaterialization,
};
constexpr interchange::GltfWriteLimits kWriteOutput{
    .max_json_bytes = 50331648,
    .max_resources = 65537,
    .max_resource_bytes = 67108864,
    .max_total_resource_bytes = 285212672,
    .max_output_requested_bytes = 67108864,
};
constexpr interchange::GltfTopologyWriteLimits kWriting{
    .max_source_json_bytes = 16777216,
    .max_source_json_nodes = 1000000,
    .max_source_json_depth = 128,
    .max_vertices = 1000000,
    .max_channels = 32,
    .max_attribute_scalars = 8000000,
    .max_descriptor_bytes = 1048576,
    .max_candidate_faces = 1000000,
    .max_candidate_corners = 3000000,
    .max_index_bytes = 12582912,
    .output = kWriteOutput,
};
constexpr interchange::GltfTopologyOutputInspectionLimits kVerification{
    .max_json_bytes = 50331648,
    .max_json_nodes = 1100000,
    .max_json_depth = 128,
    .max_resources = 65537,
    .max_resource_bytes = 285212672,
    .max_vertices = 1000000,
    .max_channels = 32,
    .max_attribute_scalars = 8000000,
    .max_descriptor_bytes = 1048576,
    .max_candidate_faces = 1000000,
    .max_candidate_corners = 3000000,
    .max_index_bytes = 12582912,
    .max_new_requested_bytes = 33554432,
};
constexpr GltfDuplicateFileMemoryLimits kMemory{
    .max_new_requested_bytes = 268435456,
    .max_input_id_bytes = 128,
    .max_source_protection_bytes = 134217728,
    .max_source_binding_records = 2048,
    .max_reload_bindings = 2048,
    .max_report_bytes = 16777216,
    .extraction = kExtraction,
    .materialization = kMaterialization,
    .repair = kRepair,
    .writing = kWriting,
    .reload = kReloadReader,
    .reload_extraction = kExtraction,
    .verification = kVerification,
};
constexpr interchange::GltfFilePublicationLimits kPublication{
    .files = kPublicationFiles,
    .reload = kReloadReader,
    .extraction = kExtraction,
    .inspection = kVerification,
    .container = kContainer,
};
constexpr GltfDuplicateFileWorkflowLimits kWorkflowLimits{
    .memory = kMemory,
    .max_wrapper_new_requested_bytes = 67108864,
    .max_source_files = 1024,
    .max_supplemental_files = 16,
    .max_report_path_bytes = 1024,
    .max_report_bytes = 16777216,
    .max_hash_input_bytes = 1073741824,
    .publication = kPublication,
};
constexpr interchange::GltfFileWorkPolicy kFileWork{
    .io_block_bytes = 65536,
    .parallel_min_bytes = 262144,
    .partition_bytes = 262144,
    .cancellation_block_bytes = 65536,
    .max_jobs = 64,
};
constexpr interchange::GltfWorkPolicy kInterchangeWork{
    .parallel_min_resource_bytes = 262144,
    .resource_partition_bytes = 262144,
    .parallel_min_channel_components = 65536,
    .channel_partition_elements = 16384,
};
constexpr interchange::GlbWorkPolicy kContainerWork{
    .parallel_min_bytes = 262144,
    .partition_bytes = 1048576,
};
constexpr interchange::GltfFileReadWorkPolicy kCaptureWork{
    .files = kFileWork,
    .asset = kInterchangeWork,
    .container = kContainerWork,
};
constexpr geometry::MeshMaterializationWorkPolicy kGeometryWork{
    .parallel_min_bytes = 262144,
    .partition_bytes = 262144,
    .cancellation_block_scalars = 8192,
    .max_jobs = 64,
};
constexpr interchange::GltfTopologyWriteWorkPolicy kWritingWork{
    .parallel_min_bytes = 262144,
    .partition_bytes = 262144,
    .cancellation_block_bytes = 65536,
    .max_jobs = 64,
};
constexpr interchange::GltfTopologyOutputInspectionWorkPolicy kVerificationWork{
    .parallel_min_bytes = 262144,
    .partition_bytes = 262144,
    .cancellation_block_bytes = 65536,
    .max_jobs = 64,
};
constexpr GltfWorkflowByteWorkPolicy kReportWork{
    .parallel_min_bytes = 65536,
    .partition_bytes = 65536,
    .cancellation_block_bytes = 16384,
    .max_jobs = 64,
};
constexpr GltfWorkflowByteWorkPolicy kHashWork{
    .parallel_min_bytes = 262144,
    .partition_bytes = 1048576,
    .cancellation_block_bytes = 65536,
    .max_jobs = 64,
};
constexpr GltfDuplicateWorkflowWorkPolicy kMemoryWork{
    .interchange = kInterchangeWork,
    .geometry = kGeometryWork,
    .writing = kWritingWork,
    .verification = kVerificationWork,
    .report = kReportWork,
};
constexpr interchange::GltfFilePublicationWorkPolicy kPublicationWork{
    .files = kFileWork,
    .reload = kInterchangeWork,
    .inspection = kVerificationWork,
    .container = kContainerWork,
};
constexpr GltfDuplicateFileWorkflowWorkPolicy kWorkflowWork{
    .memory = kMemoryWork,
    .hashing = kHashWork,
    .report = kReportWork,
    .publication = kPublicationWork,
};
constexpr std::uint64_t kTranslationWorkflowAllowanceBytes = 301989888;
constexpr std::uint64_t kTranslationExtractionOutputAllowanceBytes = 41943040;
constexpr std::uint64_t kTranslationWriterOutputAllowanceBytes = 75497472;
constexpr std::uint64_t kTranslationInspectionAllowanceBytes = 41943040;
constexpr interchange::GltfDirectTranslationLimits kTranslationLimits{
    .max_node_rows = 65536,
    .max_mesh_users = 65536,
    .max_channel_references = 4096,
    .max_distinct_samplers = 4096,
    .max_keys_per_sampler = 65536,
    .max_total_keys = 262144,
    .max_relationship_steps = 1048576,
    .max_metadata_bytes = 2097152,
    .max_decoded_bytes = 4194304,
    .max_new_requested_bytes = 8388608,
};
constexpr interchange::GltfDirectTranslationWorkPolicy kTranslationWork{
    .parallel_min_keys = 16384,
    .partition_keys = 16384,
    .cancellation_block_keys = 256,
    .max_jobs = 64,
};
constexpr std::uint64_t kSerialThreshold =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
#include "gltf_deformation_presets.inc"
}  // namespace

GltfPreset MakeGltfPreset(std::string_view name) {
  if (name == kBalancedDeformationGltfPreset ||
      name == kSerialDeformationGltfPreset) {
    if (name == kSerialDeformationGltfPreset &&
        std::numeric_limits<std::size_t>::max() < kSerialThreshold)
      throw std::invalid_argument("serial reference requires a 64-bit host");
    return MakeDeformationPreset(name == kSerialDeformationGltfPreset);
  }
  GltfPreset preset{kExecution, kCaptureLimits, kCaptureWork, kWorkflowLimits,
                    kWorkflowWork};
  const bool translation = name == kBalancedTranslationGltfPreset ||
                           name == kSerialTranslationGltfPreset;
  if (translation) {
    preset.workflow_limits.memory.max_new_requested_bytes =
        kTranslationWorkflowAllowanceBytes;
    preset.workflow_limits.memory.extraction.max_output_requested_bytes =
        kTranslationExtractionOutputAllowanceBytes;
    preset.workflow_limits.memory.writing.output.max_output_requested_bytes =
        kTranslationWriterOutputAllowanceBytes;
    preset.workflow_limits.memory.reload_extraction.max_output_requested_bytes =
        kTranslationExtractionOutputAllowanceBytes;
    preset.workflow_limits.memory.verification.max_new_requested_bytes =
        kTranslationInspectionAllowanceBytes;
    preset.workflow_limits.publication.extraction.max_output_requested_bytes =
        kTranslationExtractionOutputAllowanceBytes;
    preset.workflow_limits.publication.inspection.max_new_requested_bytes =
        kTranslationInspectionAllowanceBytes;
    preset.workflow_limits.memory.extraction.direct_translation =
        kTranslationLimits;
    preset.workflow_limits.memory.writing.direct_translation =
        kTranslationLimits;
    preset.workflow_limits.memory.reload_extraction.direct_translation =
        kTranslationLimits;
    preset.workflow_limits.memory.verification.direct_translation =
        kTranslationLimits;
    preset.workflow_limits.publication.extraction.direct_translation =
        kTranslationLimits;
    preset.workflow_limits.publication.inspection.direct_translation =
        kTranslationLimits;
    preset.capture_work.asset.direct_translation = kTranslationWork;
    preset.workflow_work.memory.interchange.direct_translation =
        kTranslationWork;
    preset.workflow_work.memory.writing.direct_translation = kTranslationWork;
    preset.workflow_work.memory.verification.direct_translation =
        kTranslationWork;
    preset.workflow_work.publication.reload.direct_translation =
        kTranslationWork;
    preset.workflow_work.publication.inspection.direct_translation =
        kTranslationWork;
  }
  if (name == kBalancedGltfPreset || name == kBalancedTranslationGltfPreset)
    return preset;
  if (name != kSerialGltfPreset && name != kSerialTranslationGltfPreset)
    throw std::invalid_argument("unknown glTF execution preset");
  if (std::numeric_limits<std::size_t>::max() < kSerialThreshold)
    throw std::invalid_argument("serial reference requires a 64-bit host");
  preset.execution.worker_budget = 1;
  preset.execution.minimum_parallel_vertices = kSerialThreshold;
  preset.capture_work.files.parallel_min_bytes = kSerialThreshold;
  preset.capture_work.files.max_jobs = 1;
  preset.capture_work.asset.parallel_min_resource_bytes = kSerialThreshold;
  preset.capture_work.asset.parallel_min_channel_components = kSerialThreshold;
  preset.capture_work.container.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.memory.interchange.parallel_min_resource_bytes =
      kSerialThreshold;
  preset.workflow_work.memory.interchange.parallel_min_channel_components =
      kSerialThreshold;
  preset.workflow_work.memory.geometry.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.memory.geometry.max_jobs = 1;
  preset.workflow_work.memory.writing.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.memory.writing.max_jobs = 1;
  preset.workflow_work.memory.verification.parallel_min_bytes =
      kSerialThreshold;
  preset.workflow_work.memory.verification.max_jobs = 1;
  preset.workflow_work.memory.report.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.memory.report.max_jobs = 1;
  preset.workflow_work.hashing.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.hashing.max_jobs = 1;
  preset.workflow_work.report.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.report.max_jobs = 1;
  preset.workflow_work.publication.files.parallel_min_bytes = kSerialThreshold;
  preset.workflow_work.publication.files.max_jobs = 1;
  preset.workflow_work.publication.reload.parallel_min_resource_bytes =
      kSerialThreshold;
  preset.workflow_work.publication.reload.parallel_min_channel_components =
      kSerialThreshold;
  preset.workflow_work.publication.inspection.parallel_min_bytes =
      kSerialThreshold;
  preset.workflow_work.publication.inspection.max_jobs = 1;
  preset.workflow_work.publication.container.parallel_min_bytes =
      kSerialThreshold;
  if (translation) {
    const interchange::GltfDirectTranslationWorkPolicy serial_work{
        .parallel_min_keys = kSerialThreshold,
        .partition_keys = kTranslationWork.partition_keys,
        .cancellation_block_keys = kTranslationWork.cancellation_block_keys,
        .max_jobs = 1,
    };
    preset.capture_work.asset.direct_translation = serial_work;
    preset.workflow_work.memory.interchange.direct_translation = serial_work;
    preset.workflow_work.memory.writing.direct_translation = serial_work;
    preset.workflow_work.memory.verification.direct_translation = serial_work;
    preset.workflow_work.publication.reload.direct_translation = serial_work;
    preset.workflow_work.publication.inspection.direct_translation =
        serial_work;
  }
  return preset;
}
}  // namespace meshvale::repair::python
