// SPDX-License-Identifier: Apache-2.0
#include "off_surface_export_bindings.h"

#include <Python.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_surface_export.h>
#include <meshvale/repair/off_workflow.h>
#include <nanobind/stl/string_view.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "off_bindings.h"
#include "off_surface_export_presets.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace meshvale::repair::python {
namespace {
namespace nb = nanobind;
namespace geo = geometry;
namespace ic = interchange;
constexpr std::uint64_t kPathUtf8Limit = 4096;
constexpr std::uint64_t kInputIdLimit = 128;
constexpr std::uint64_t kReceiptPathLimit = 1024;
constexpr std::uint64_t kTargetLimit = 1000000;
constexpr std::size_t kPositionComponents = 3;
constexpr char kHexDigits[] = "0123456789abcdef";
constexpr std::string_view kCanonicalText = "canonical-ascii";
constexpr std::string_view kUnknownZeroEdgeCount = "unknown-zero";
constexpr std::string_view kDeclaredCoordinates = "meters-right-handed-y-up";
constexpr std::string_view kExact32 = "exact32";
constexpr std::string_view kBoundedRne32 = "bounded-rne32";

constexpr std::string_view kWireNull = "null";
constexpr std::string_view kWireTrue = "true";
constexpr std::string_view kWireFalse = "false";
constexpr std::string_view kNativeOutcomePrefix = "native:";
constexpr std::string_view kCaptureOutcomePrefix = "capture:";
constexpr std::size_t kJsonQuoteBytes = 2;
constexpr std::size_t kJsonContainerDelimiterBytes = 2;
constexpr std::size_t kJsonSeparatorBytes = 1;
constexpr std::size_t kJsonColonBytes = 1;
constexpr std::size_t kHexDigitsPerByte = 2;
constexpr std::size_t kJsonControlEscapeBytes = 6;
constexpr std::size_t kJsonShortEscapeBytes = 2;
constexpr unsigned char kFirstPrintableAscii = 0x20;
constexpr int kSuccessExit = static_cast<int>(OffWorkflowExitMeaning::kSuccess);
constexpr int kRejectedOrIncompleteExit =
    static_cast<int>(OffWorkflowExitMeaning::kRejectedOrIncomplete);
constexpr int kExecutionFailureExit =
    static_cast<int>(OffWorkflowExitMeaning::kExecutionFailure);
constexpr int kCanceledExit =
    static_cast<int>(OffWorkflowExitMeaning::kCanceled);

namespace observation_keys {
constexpr char kActiveTrackedPayload[] = "active_tracked_payload";
constexpr char kActiveWorkers[] = "active_workers";
constexpr char kAdaptationExecution[] = "adaptation_execution";
constexpr char kAttribute[] = "attribute";
constexpr char kAttributeBytes[] = "attribute_bytes";
constexpr char kAttributeCount[] = "attribute_count";
constexpr char kAttributeOffsets[] = "attribute_offsets";
constexpr char kAttributeScalars[] = "attribute_scalars";
constexpr char kAttributes[] = "attributes";
constexpr char kAuthored[] = "authored";
constexpr char kByteOffset[] = "byte_offset";
constexpr char kByteVerification[] = "byte_verification";
constexpr char kBytes[] = "bytes";
constexpr char kBytesCompared[] = "bytes_compared";
constexpr char kBytesCopied[] = "bytes_copied";
constexpr char kBytesHashed[] = "bytes_hashed";
constexpr char kBytesRead[] = "bytes_read";
constexpr char kBytesWritten[] = "bytes_written";
constexpr char kCallOrdinal[] = "call_ordinal";
constexpr char kCallerJobs[] = "caller_jobs";
constexpr char kCancellationBlockBytes[] = "cancellation_block_bytes";
constexpr char kCancellationBlockFacets[] = "cancellation_block_facets";
constexpr char kCancellationBlockKeys[] = "cancellation_block_keys";
constexpr char kCancellationBlockRecords[] = "cancellation_block_records";
constexpr char kCancellationBlockScalars[] = "cancellation_block_scalars";
constexpr char kCancellationBlockTokens[] = "cancellation_block_tokens";
constexpr char kCandidate[] = "candidate";
constexpr char kCandidateInspection[] = "candidate_inspection";
constexpr char kCanonicalPreservation[] = "canonical_preservation";
constexpr char kCapture[] = "capture";
constexpr char kCaptureLimits[] = "capture_limits";
constexpr char kCaptureWork[] = "capture_work";
constexpr char kCapturedMembers[] = "captured_members";
constexpr char kCategory[] = "category";
constexpr char kChangedValues[] = "changed_values";
constexpr char kChannelPartitionElements[] = "channel_partition_elements";
constexpr char kCleanup[] = "cleanup";
constexpr char kClosedFiles[] = "closed_files";
constexpr char kCode[] = "code";
constexpr char kCodec[] = "codec";
constexpr char kCoincidenceComparisons[] = "coincidence_comparisons";
constexpr char kCommit[] = "commit";
constexpr char kComparison[] = "comparison";
constexpr char kComplete[] = "complete";
constexpr char kCompletedFiles[] = "completed_files";
constexpr char kCompletedJobs[] = "completed_jobs";
constexpr char kComponent[] = "component";
constexpr char kContainer[] = "container";
constexpr char kCopying[] = "copying";
constexpr char kCore[] = "core";
constexpr char kCoreF32[] = "core_f32";
constexpr char kCornerCount[] = "corner_count";
constexpr char kCornerExpansionAcknowledged[] = "corner_expansion_acknowledged";
constexpr char kCornerVertices[] = "corner_vertices";
constexpr char kCorners[] = "corners";
constexpr char kCorrespondence[] = "correspondence";
constexpr char kCounts[] = "counts";
constexpr char kDazDisabled[] = "daz_disabled";
constexpr char kDelegated[] = "delegated";
constexpr char kDelegatedStatus[] = "delegated_status";
constexpr char kDelegates[] = "delegates";
constexpr char kDescriptorBytePairs[] = "descriptor_byte_pairs";
constexpr char kDescriptorBytes[] = "descriptor_bytes";
constexpr char kDiagnostic[] = "diagnostic";
constexpr char kDirectTranslation[] = "direct_translation";
constexpr char kDirectedVerification[] = "directed_verification";
constexpr char kElement[] = "element";
constexpr char kEligibility[] = "eligibility";
constexpr char kEncoding[] = "encoding";
constexpr char kError[] = "error";
constexpr char kErrorByteOffset[] = "error_byte_offset";
constexpr char kExaminedValues[] = "examined_values";
constexpr char kExaminedWork[] = "examined_work";
constexpr char kExecution[] = "execution";
constexpr char kExit[] = "exit";
constexpr char kExpected[] = "expected";
constexpr char kExpectedInspection[] = "expected_inspection";
constexpr char kExponent2[] = "exponent2";
constexpr char kExtraction[] = "extraction";
constexpr char kFace[] = "face";
constexpr char kFaceCount[] = "face_count";
constexpr char kFaceNormalAttribute[] = "face_normal_attribute";
constexpr char kFaceOffsets[] = "face_offsets";
constexpr char kFaces[] = "faces";
constexpr char kFacet[] = "facet";
constexpr char kFactorComponent[] = "factor_component";
constexpr char kFacts[] = "facts";
constexpr char kFailure[] = "failure";
constexpr char kFileIndex[] = "file_index";
constexpr char kFiles[] = "files";
constexpr char kFinalInventory[] = "final_inventory";
constexpr char kFinalPayloadVerification[] = "final_payload_verification";
constexpr char kFinalReceiptVerification[] = "final_receipt_verification";
constexpr char kFirstWireTriangle[] = "first_wire_triangle";
constexpr char kFirstWireVertex[] = "first_wire_vertex";
constexpr char kFtzDisabled[] = "ftz_disabled";
constexpr char kGeometry[] = "geometry";
constexpr char kGeometryDiagnostic[] = "geometry_diagnostic";
constexpr char kGeometryStatus[] = "geometry_status";
constexpr char kGltf[] = "gltf";
constexpr char kHashing[] = "hashing";
constexpr char kImages[] = "images";
constexpr char kIndex[] = "index";
constexpr char kInspection[] = "inspection";
constexpr char kInspectionDiagnostic[] = "inspection_diagnostic";
constexpr char kInspectionStatus[] = "inspection_status";
constexpr char kIoBlockBytes[] = "io_block_bytes";
constexpr char kIssues[] = "issues";
constexpr char kJoinedWorkers[] = "joined_workers";
constexpr char kJsonSpan[] = "json_span";
constexpr char kKind[] = "kind";
constexpr char kLaunchedWorkers[] = "launched_workers";
constexpr char kLength[] = "length";
constexpr char kLexicalCalls[] = "lexical_calls";
constexpr char kLimits[] = "limits";
constexpr char kMagnitude[] = "magnitude";
constexpr char kMapEntries[] = "map_entries";
constexpr char kMaterial[] = "material";
constexpr char kMaterialization[] = "materialization";
constexpr char kMaterials[] = "materials";
constexpr char kMaxAbsoluteError[] = "max_absolute_error";
constexpr char kMaxAccessorElements[] = "max_accessor_elements";
constexpr char kMaxAttributeBytes[] = "max_attribute_bytes";
constexpr char kMaxAttributeOffsets[] = "max_attribute_offsets";
constexpr char kMaxAttributeScalars[] = "max_attribute_scalars";
constexpr char kMaxAttributes[] = "max_attributes";
constexpr char kMaxBinaryBytes[] = "max_binary_bytes";
constexpr char kMaxBridgeNewRequestedBytes[] = "max_bridge_new_requested_bytes";
constexpr char kMaxCallerReportBytes[] = "max_caller_report_bytes";
constexpr char kMaxCapsuleBinaryBytes[] = "max_capsule_binary_bytes";
constexpr char kMaxCapsuleJsonBytes[] = "max_capsule_json_bytes";
constexpr char kMaxChannelReferences[] = "max_channel_references";
constexpr char kMaxChunks[] = "max_chunks";
constexpr char kMaxCodecNewRequestedBytes[] = "max_codec_new_requested_bytes";
constexpr char kMaxCoincidenceComparisons[] = "max_coincidence_comparisons";
constexpr char kMaxCommentBytes[] = "max_comment_bytes";
constexpr char kMaxComparisonStepsPerTarget[] =
    "max_comparison_steps_per_target";
constexpr char kMaxComponents[] = "max_components";
constexpr char kMaxContainerBytes[] = "max_container_bytes";
constexpr char kMaxCoreNewRequestedBytes[] = "max_core_new_requested_bytes";
constexpr char kMaxCorners[] = "max_corners";
constexpr char kMaxCornersPerFace[] = "max_corners_per_face";
constexpr char kMaxDecimalCoefficientDigits[] =
    "max_decimal_coefficient_digits";
constexpr char kMaxDecimalExponentDigits[] = "max_decimal_exponent_digits";
constexpr char kMaxDeclarationNameBytes[] = "max_declaration_name_bytes";
constexpr char kMaxDecodedBytes[] = "max_decoded_bytes";
constexpr char kMaxDecodedStringBytes[] = "max_decoded_string_bytes";
constexpr char kMaxDescriptorBytes[] = "max_descriptor_bytes";
constexpr char kMaxDistinctSamplers[] = "max_distinct_samplers";
constexpr char kMaxDocumentDepth[] = "max_document_depth";
constexpr char kMaxDocumentNodes[] = "max_document_nodes";
constexpr char kMaxEntryBytes[] = "max_entry_bytes";
constexpr char kMaxEntryNameBytes[] = "max_entry_name_bytes";
constexpr char kMaxFaceOffsets[] = "max_face_offsets";
constexpr char kMaxFaces[] = "max_faces";
constexpr char kMaxFileNewRequestedBytes[] = "max_file_new_requested_bytes";
constexpr char kMaxGraphEdges[] = "max_graph_edges";
constexpr char kMaxHashInputBytes[] = "max_hash_input_bytes";
constexpr char kMaxInputIdBytes[] = "max_input_id_bytes";
constexpr char kMaxInstances[] = "max_instances";
constexpr char kMaxInventoryEntries[] = "max_inventory_entries";
constexpr char kMaxIssues[] = "max_issues";
constexpr char kMaxJobs[] = "max_jobs";
constexpr char kMaxJsonBytes[] = "max_json_bytes";
constexpr char kMaxJsonDepth[] = "max_json_depth";
constexpr char kMaxJsonNodes[] = "max_json_nodes";
constexpr char kMaxKeysPerSampler[] = "max_keys_per_sampler";
constexpr char kMaxLineBytes[] = "max_line_bytes";
constexpr char kMaxMapEntries[] = "max_map_entries";
constexpr char kMaxMaterialBindings[] = "max_material_bindings";
constexpr char kMaxMemberBytes[] = "max_member_bytes";
constexpr char kMaxMembers[] = "max_members";
constexpr char kMaxMeshUsers[] = "max_mesh_users";
constexpr char kMaxMetadataBytes[] = "max_metadata_bytes";
constexpr char kMaxMetadataEntries[] = "max_metadata_entries";
constexpr char kMaxNativePathCodeUnits[] = "max_native_path_code_units";
constexpr char kMaxNewRequestedBytes[] = "max_new_requested_bytes";
constexpr char kMaxNodeRows[] = "max_node_rows";
constexpr char kMaxNumericIntegerBits[] = "max_numeric_integer_bits";
constexpr char kMaxNumericTokenBytes[] = "max_numeric_token_bytes";
constexpr char kMaxOutputBytes[] = "max_output_bytes";
constexpr char kMaxOutputRequestedBytes[] = "max_output_requested_bytes";
constexpr char kMaxOwnedSourceResourceBytes[] =
    "max_owned_source_resource_bytes";
constexpr char kMaxPathBytes[] = "max_path_bytes";
constexpr char kMaxPayloadMemberBytes[] = "max_payload_member_bytes";
constexpr char kMaxPayloadMembers[] = "max_payload_members";
constexpr char kMaxPresenceBytes[] = "max_presence_bytes";
constexpr char kMaxProjectionBytes[] = "max_projection_bytes";
constexpr char kMaxReceiptBytes[] = "max_receipt_bytes";
constexpr char kMaxReceiptNameBytes[] = "max_receipt_name_bytes";
constexpr char kMaxRelationshipSteps[] = "max_relationship_steps";
constexpr char kMaxReportBytes[] = "max_report_bytes";
constexpr char kMaxRequestedBytes[] = "max_requested_bytes";
constexpr char kMaxResourceBytes[] = "max_resource_bytes";
constexpr char kMaxResources[] = "max_resources";
constexpr char kMaxRowLocalDeclarations[] = "max_row_local_declarations";
constexpr char kMaxSourceBytes[] = "max_source_bytes";
constexpr char kMaxSourceProtectionBytes[] = "max_source_protection_bytes";
constexpr char kMaxStageNameAttempts[] = "max_stage_name_attempts";
constexpr char kMaxTableRows[] = "max_table_rows";
constexpr char kMaxTargets[] = "max_targets";
constexpr char kMaxTokenBytes[] = "max_token_bytes";
constexpr char kMaxTotalFileBytes[] = "max_total_file_bytes";
constexpr char kMaxTotalKeys[] = "max_total_keys";
constexpr char kMaxTotalMemberBytes[] = "max_total_member_bytes";
constexpr char kMaxTotalNewRequestedBytes[] = "max_total_new_requested_bytes";
constexpr char kMaxTotalPathBytes[] = "max_total_path_bytes";
constexpr char kMaxTotalPayloadBytes[] = "max_total_payload_bytes";
constexpr char kMaxTotalRequestedAllowance[] = "max_total_requested_allowance";
constexpr char kMaxTriangleCorners[] = "max_triangle_corners";
constexpr char kMaxVerificationCorners[] = "max_verification_corners";
constexpr char kMaxVerificationFaces[] = "max_verification_faces";
constexpr char kMaxVerificationVertices[] = "max_verification_vertices";
constexpr char kMaxVertices[] = "max_vertices";
constexpr char kMaxWireCorners[] = "max_wire_corners";
constexpr char kMaxWirePrimitives[] = "max_wire_primitives";
constexpr char kMaxWireVertices[] = "max_wire_vertices";
constexpr char kMaxWrapperNewRequestedBytes[] =
    "max_wrapper_new_requested_bytes";
constexpr char kMemberBytes[] = "member_bytes";
constexpr char kMemberOrdinal[] = "member_ordinal";
constexpr char kMembers[] = "members";
constexpr char kMeshVerification[] = "mesh_verification";
constexpr char kMetadataEntries[] = "metadata_entries";
constexpr char kMetadataPolicy[] = "metadata_policy";
constexpr char kMinimumParallelFaces[] = "minimum_parallel_faces";
constexpr char kMinimumParallelVertices[] = "minimum_parallel_vertices";
constexpr char kNative[] = "native";
constexpr char kNearestEven[] = "nearest_even";
constexpr char kNewRequestedBytes[] = "new_requested_bytes";
constexpr char kNewRequestedBytesAtReturn[] = "new_requested_bytes_at_return";
constexpr char kNewRequestedPeakBytes[] = "new_requested_peak_bytes";
constexpr char kNumericDelegateProfiles[] = "numeric_delegate_profiles";
constexpr char kObserved[] = "observed";
constexpr char kObservedDirectories[] = "observed_directories";
constexpr char kObservedFiles[] = "observed_files";
constexpr char kOff[] = "off";
constexpr char kOffDiagnostic[] = "off_diagnostic";
constexpr char kOffReportBytes[] = "off_report_bytes";
constexpr char kOffStatus[] = "off_status";
constexpr char kOffset[] = "offset";
constexpr char kOrdinal[] = "ordinal";
constexpr char kOriginalCounts[] = "original_counts";
constexpr char kOutcome[] = "outcome";
constexpr char kOutcomes[] = "outcomes";
constexpr char kOutput[] = "output";
constexpr char kOutputCorners[] = "output_corners";
constexpr char kOutputFaces[] = "output_faces";
constexpr char kOutputInspection[] = "output_inspection";
constexpr char kOutputMaterialization[] = "output_materialization";
constexpr char kOutputVertices[] = "output_vertices";
constexpr char kParallelMinBytes[] = "parallel_min_bytes";
constexpr char kParallelMinChannelComponents[] =
    "parallel_min_channel_components";
constexpr char kParallelMinKeys[] = "parallel_min_keys";
constexpr char kParallelMinResourceBytes[] = "parallel_min_resource_bytes";
constexpr char kPartitionBytes[] = "partition_bytes";
constexpr char kPartitionKeys[] = "partition_keys";
constexpr char kPath[] = "path";
constexpr char kPayloadInventory[] = "payload_inventory";
constexpr char kPayloadReread[] = "payload_reread";
constexpr char kPayloadWrite[] = "payload_write";
constexpr char kPlannedJobs[] = "planned_jobs";
constexpr char kPositionNumeric[] = "position_numeric";
constexpr char kPositions[] = "positions";
constexpr char kPresenceBytes[] = "presence_bytes";
constexpr char kPreset[] = "preset";
constexpr char kPrimitive[] = "primitive";
constexpr char kPrimitives[] = "primitives";
constexpr char kProfile[] = "profile";
constexpr char kProof[] = "proof";
constexpr char kProvider[] = "provider";
constexpr char kPublication[] = "publication";
constexpr char kPublicationReceiptBytes[] = "publication_receipt_bytes";
constexpr char kPublished[] = "published";
constexpr char kRead[] = "read";
constexpr char kReceipt[] = "receipt";
constexpr char kReceiptReread[] = "receipt_reread";
constexpr char kReceiptWrite[] = "receipt_write";
constexpr char kRelationshipSteps[] = "relationship_steps";
constexpr char kReload[] = "reload";
constexpr char kReloadVerification[] = "reload_verification";
constexpr char kRepair[] = "repair";
constexpr char kRepairCornerMap[] = "repair_corner_map";
constexpr char kRepairCorrespondence[] = "repair_correspondence";
constexpr char kRepairFaceMap[] = "repair_face_map";
constexpr char kRepairSourceCorners[] = "repair_source_corners";
constexpr char kRepairSourceFaces[] = "repair_source_faces";
constexpr char kRepairVertexIdentity[] = "repair_vertex_identity";
constexpr char kReport[] = "report";
constexpr char kReportCopy[] = "report_copy";
constexpr char kReportDiagnostic[] = "report_diagnostic";
constexpr char kReportImport[] = "report_import";
constexpr char kRepresentation[] = "representation";
constexpr char kRequestedBytesAtReturn[] = "requested_bytes_at_return";
constexpr char kResolvedPreset[] = "resolved_preset";
constexpr char kResourceIndex[] = "resource_index";
constexpr char kResourceKind[] = "resource_kind";
constexpr char kResourcePartitionBytes[] = "resource_partition_bytes";
constexpr char kResourceSpan[] = "resource_span";
constexpr char kReturnedBytes[] = "returned_bytes";
constexpr char kRuleId[] = "rule_id";
constexpr char kSamplers[] = "samplers";
constexpr char kSecondaryIndex[] = "secondary_index";
constexpr char kSelection[] = "selection";
constexpr char kSerialReason[] = "serial_reason";
constexpr char kSha256[] = "sha256";
constexpr char kSource[] = "source";
constexpr char kSourceAttribute[] = "source_attribute";
constexpr char kSourceCapture[] = "source_capture";
constexpr char kSourceCornerOffsets[] = "source_corner_offsets";
constexpr char kSourceCornerWireCorners[] = "source_corner_wire_corners";
constexpr char kSourceCorners[] = "source_corners";
constexpr char kSourceEncoding[] = "source_encoding";
constexpr char kSourceFaceCount[] = "source_face_count";
constexpr char kSourceFaceOffsets[] = "source_face_offsets";
constexpr char kSourceFaceWireTriangles[] = "source_face_wire_triangles";
constexpr char kSourceFaces[] = "source_faces";
constexpr char kSourceInspection[] = "source_inspection";
constexpr char kSourceMesh[] = "source_mesh";
constexpr char kSourceProtection[] = "source_protection";
constexpr char kSourceRow[] = "source_row";
constexpr char kSourceScalarPairs[] = "source_scalar_pairs";
constexpr char kSourceVerification[] = "source_verification";
constexpr char kSourceVertexCount[] = "source_vertex_count";
constexpr char kSourceVertexOffsets[] = "source_vertex_offsets";
constexpr char kSourceVertexWireMembers[] = "source_vertex_wire_members";
constexpr char kSourceVertices[] = "source_vertices";
constexpr char kStage[] = "stage";
constexpr char kStagedCandidate[] = "staged_candidate";
constexpr char kStatus[] = "status";
constexpr char kStl[] = "stl";
constexpr char kStorageIssue[] = "storage_issue";
constexpr char kStorageValid[] = "storage_valid";
constexpr char kSubject[] = "subject";
constexpr char kSurface[] = "surface";
constexpr char kSystemError[] = "system_error";
constexpr char kTarget[] = "target";
constexpr char kTextures[] = "textures";
constexpr char kTotal[] = "total";
constexpr char kTrackedPayloadBudgetBytes[] = "tracked_payload_budget_bytes";
constexpr char kTransferExecution[] = "transfer_execution";
constexpr char kTriangleCounts[] = "triangle_counts";
constexpr char kTriangles[] = "triangles";
constexpr char kTriangulatedInspection[] = "triangulated_inspection";
constexpr char kTriangulatedMesh[] = "triangulated_mesh";
constexpr char kTriangulation[] = "triangulation";
constexpr char kTriangulationCornerSources[] = "triangulation_corner_sources";
constexpr char kTriangulationFaceOutputOffsets[] =
    "triangulation_face_output_offsets";
constexpr char kTriangulationFaceSources[] = "triangulation_face_sources";
constexpr char kTriangulationOptions[] = "triangulation_options";
constexpr char kTriangulationVertexIdentity[] = "triangulation_vertex_identity";
constexpr char kUsedWords[] = "used_words";
constexpr char kUvNumeric[] = "uv_numeric";
constexpr char kValue[] = "value";
constexpr char kVerification[] = "verification";
constexpr char kVertex[] = "vertex";
constexpr char kVertexCount[] = "vertex_count";
constexpr char kVertices[] = "vertices";
constexpr char kWireCornerCorners[] = "wire_corner_corners";
constexpr char kWireCornerSources[] = "wire_corner_sources";
constexpr char kWireCounts[] = "wire_counts";
constexpr char kWireTriangleFaces[] = "wire_triangle_faces";
constexpr char kWireTriangleSources[] = "wire_triangle_sources";
constexpr char kWireTriangles[] = "wire_triangles";
constexpr char kWireVertexSources[] = "wire_vertex_sources";
constexpr char kWireVertices[] = "wire_vertices";
constexpr char kWork[] = "work";
constexpr char kWorkerBudget[] = "worker_budget";
constexpr char kWorkersUsed[] = "workers_used";
constexpr char kWriteFacts[] = "write_facts";
constexpr char kWriting[] = "writing";
}  // namespace observation_keys

namespace wire_symbols {
constexpr std::string_view kCandidateAccepted = "accepted";
constexpr std::string_view kAccepted = "kAccepted";
constexpr std::string_view kAllocationProfile = "kAllocationProfile";
constexpr std::string_view kAllowanceTooSmall = "kAllowanceTooSmall";
constexpr std::string_view kAscii = "kAscii";
constexpr std::string_view kAssetRead = "kAssetRead";
constexpr std::string_view kAttribute = "kAttribute";
constexpr std::string_view kAttributeComponentsZero =
    "kAttributeComponentsZero";
constexpr std::string_view kAttributeDenseShape = "kAttributeDenseShape";
constexpr std::string_view kAttributeDomain = "kAttributeDomain";
constexpr std::string_view kAttributeDuplicateName = "kAttributeDuplicateName";
constexpr std::string_view kAttributeNameEmpty = "kAttributeNameEmpty";
constexpr std::string_view kAttributeOffsetAlignment =
    "kAttributeOffsetAlignment";
constexpr std::string_view kAttributeOffsetOrder = "kAttributeOffsetOrder";
constexpr std::string_view kAttributeOffsetRange = "kAttributeOffsetRange";
constexpr std::string_view kAttributeOffsetsCount = "kAttributeOffsetsCount";
constexpr std::string_view kAttributeOffsetsEmpty = "kAttributeOffsetsEmpty";
constexpr std::string_view kAttributeOffsetsEnd = "kAttributeOffsetsEnd";
constexpr std::string_view kAttributeOffsetsStart = "kAttributeOffsetsStart";
constexpr std::string_view kAttributePresenceCount = "kAttributePresenceCount";
constexpr std::string_view kAttributePresenceValue = "kAttributePresenceValue";
constexpr std::string_view kAttributeTransfer = "kAttributeTransfer";
constexpr std::string_view kAttributeUnsupported = "kAttributeUnsupported";
constexpr std::string_view kAttributeWord = "kAttributeWord";
constexpr std::string_view kBacktrackingBoundary = "kBacktrackingBoundary";
constexpr std::string_view kBareUnrepresented = "kBareUnrepresented";
constexpr std::string_view kBelowFaceThreshold = "kBelowFaceThreshold";
constexpr std::string_view kBinary = "kBinary";
constexpr std::string_view kBinaryExtent = "kBinaryExtent";
constexpr std::string_view kBinaryLayout = "kBinaryLayout";
constexpr std::string_view kBinarySlice = "kBinarySlice";
constexpr std::string_view kBinaryValue = "kBinaryValue";
constexpr std::string_view kBlocked = "kBlocked";
constexpr std::string_view kBridgeRequestBudget = "kBridgeRequestBudget";
constexpr std::string_view kBudgetExceeded = "kBudgetExceeded";
constexpr std::string_view kBuffer = "kBuffer";
constexpr std::string_view kByteMismatch = "kByteMismatch";
constexpr std::string_view kCallbackFailure = "kCallbackFailure";
constexpr std::string_view kCallerReportEncoding = "kCallerReportEncoding";
constexpr std::string_view kCanceled = "kCanceled";
constexpr std::string_view kCandidateInspection = "kCandidateInspection";
constexpr std::string_view kCanonicalPreservation = "kCanonicalPreservation";
constexpr std::string_view kCapsuleRead = "kCapsuleRead";
constexpr std::string_view kCapsuleSource = "kCapsuleSource";
constexpr std::string_view kCapture = "kCapture";
constexpr std::string_view kCatalog = "kCatalog";
constexpr std::string_view kCatalogExtension = "kCatalogExtension";
constexpr std::string_view kCatalogField = "kCatalogField";
constexpr std::string_view kChanged = "kChanged";
constexpr std::string_view kChannelCoverage = "kChannelCoverage";
constexpr std::string_view kChannelMetadata = "kChannelMetadata";
constexpr std::string_view kChannelRole = "kChannelRole";
constexpr std::string_view kChannelShape = "kChannelShape";
constexpr std::string_view kClose = "kClose";
constexpr std::string_view kCollapsedEdge = "kCollapsedEdge";
constexpr std::string_view kCollapsedTriangle = "kCollapsedTriangle";
constexpr std::string_view kCommentLimit = "kCommentLimit";
constexpr std::string_view kCommit = "kCommit";
constexpr std::string_view kCommitFailed = "kCommitFailed";
constexpr std::string_view kCommitUnsupported = "kCommitUnsupported";
constexpr std::string_view kCommitted = "kCommitted";
constexpr std::string_view kCompare = "kCompare";
constexpr std::string_view kComparison = "kComparison";
constexpr std::string_view kComparisonLimit = "kComparisonLimit";
constexpr std::string_view kComplete = "kComplete";
constexpr std::string_view kCompleted = "kCompleted";
constexpr std::string_view kConfiguration = "kConfiguration";
constexpr std::string_view kConfigurationExtent = "kConfigurationExtent";
constexpr std::string_view kContainerRead = "kContainerRead";
constexpr std::string_view kCoordinates = "kCoordinates";
constexpr std::string_view kCoreArrayLength = "kCoreArrayLength";
constexpr std::string_view kCoreFieldRequired = "kCoreFieldRequired";
constexpr std::string_view kCoreFieldType = "kCoreFieldType";
constexpr std::string_view kCoreRange = "kCoreRange";
constexpr std::string_view kCoreReference = "kCoreReference";
constexpr std::string_view kCoreRequestBudget = "kCoreRequestBudget";
constexpr std::string_view kCorner = "kCorner";
constexpr std::string_view kCornerLimit = "kCornerLimit";
constexpr std::string_view kCornerVertexRange = "kCornerVertexRange";
constexpr std::string_view kCorrespondence = "kCorrespondence";
constexpr std::string_view kCountLimit = "kCountLimit";
constexpr std::string_view kDeclarationDomain = "kDeclarationDomain";
constexpr std::string_view kDeclarationDuplicate = "kDeclarationDuplicate";
constexpr std::string_view kDeclarationLimit = "kDeclarationLimit";
constexpr std::string_view kDeclarationNameByteLimit =
    "kDeclarationNameByteLimit";
constexpr std::string_view kDeclarationNameEmpty = "kDeclarationNameEmpty";
constexpr std::string_view kDecode = "kDecode";
constexpr std::string_view kDecodedDuplicateKey = "kDecodedDuplicateKey";
constexpr std::string_view kDecodedWire = "kDecodedWire";
constexpr std::string_view kDegenerateFace = "kDegenerateFace";
constexpr std::string_view kDegenerateResult = "kDegenerateResult";
constexpr std::string_view kDelegateFailure = "kDelegateFailure";
constexpr std::string_view kDelegatedFailure = "kDelegatedFailure";
constexpr std::string_view kDelegatedInspection = "kDelegatedInspection";
constexpr std::string_view kDelegatedOff = "kDelegatedOff";
constexpr std::string_view kDerivedMaterialization = "kDerivedMaterialization";
constexpr std::string_view kDescriptorLimit = "kDescriptorLimit";
constexpr std::string_view kDestinationExists = "kDestinationExists";
constexpr std::string_view kDestinationParent = "kDestinationParent";
constexpr std::string_view kDestinationPath = "kDestinationPath";
constexpr std::string_view kDisposition = "kDisposition";
constexpr std::string_view kDuplicateResource = "kDuplicateResource";
constexpr std::string_view kEdgeCountPolicy = "kEdgeCountPolicy";
constexpr std::string_view kEligibility = "kEligibility";
constexpr std::string_view kEmbeddedMediaMismatch = "kEmbeddedMediaMismatch";
constexpr std::string_view kEmbeddedUriSyntax = "kEmbeddedUriSyntax";
constexpr std::string_view kEmptyMesh = "kEmptyMesh";
constexpr std::string_view kEncode = "kEncode";
constexpr std::string_view kEncoding = "kEncoding";
constexpr std::string_view kEncodingLimitsMismatch = "kEncodingLimitsMismatch";
constexpr std::string_view kEnumeration = "kEnumeration";
constexpr std::string_view kErrorBound = "kErrorBound";
constexpr std::string_view kEstablishedBaseline = "kEstablishedBaseline";
constexpr std::string_view kExactInteger = "kExactInteger";
constexpr std::string_view kExactness = "kExactness";
constexpr std::string_view kExecutionFailure = "kExecutionFailure";
constexpr std::string_view kExpansionAcknowledgment =
    "kExpansionAcknowledgment";
constexpr std::string_view kExpectedInspection = "kExpectedInspection";
constexpr std::string_view kExplicitWithoutSource = "kExplicitWithoutSource";
constexpr std::string_view kExtraResource = "kExtraResource";
constexpr std::string_view kFace = "kFace";
constexpr std::string_view kFaceCornerLimit = "kFaceCornerLimit";
constexpr std::string_view kFaceLimit = "kFaceLimit";
constexpr std::string_view kFaceOffsetOrder = "kFaceOffsetOrder";
constexpr std::string_view kFaceOffsetRange = "kFaceOffsetRange";
constexpr std::string_view kFaceOffsetsEmpty = "kFaceOffsetsEmpty";
constexpr std::string_view kFaceOffsetsEnd = "kFaceOffsetsEnd";
constexpr std::string_view kFaceOffsetsStart = "kFaceOffsetsStart";
constexpr std::string_view kFaceTooShort = "kFaceTooShort";
constexpr std::string_view kFacetLimit = "kFacetLimit";
constexpr std::string_view kFailed = "kFailed";
constexpr std::string_view kFileChanged = "kFileChanged";
constexpr std::string_view kFileClose = "kFileClose";
constexpr std::string_view kFileKind = "kFileKind";
constexpr std::string_view kFileOpen = "kFileOpen";
constexpr std::string_view kFileRead = "kFileRead";
constexpr std::string_view kFileWrite = "kFileWrite";
constexpr std::string_view kForeignBudget = "kForeignBudget";
constexpr std::string_view kForeignLedger = "kForeignLedger";
constexpr std::string_view kForeignOwner = "kForeignOwner";
constexpr std::string_view kGeometry = "kGeometry";
constexpr std::string_view kGeometryDelegate = "kGeometryDelegate";
constexpr std::string_view kGeometryFailure = "kGeometryFailure";
constexpr std::string_view kGlbDelegate = "kGlbDelegate";
constexpr std::string_view kGlbRead = "kGlbRead";
constexpr std::string_view kGlbWrite = "kGlbWrite";
constexpr std::string_view kGltfDelegate = "kGltfDelegate";
constexpr std::string_view kGltfExtract = "kGltfExtract";
constexpr std::string_view kGltfRead = "kGltfRead";
constexpr std::string_view kGraphRelationship = "kGraphRelationship";
constexpr std::string_view kHash = "kHash";
constexpr std::string_view kHashExtent = "kHashExtent";
constexpr std::string_view kImage = "kImage";
constexpr std::string_view kIncomplete = "kIncomplete";
constexpr std::string_view kIndexRole = "kIndexRole";
constexpr std::string_view kInputIdentity = "kInputIdentity";
constexpr std::string_view kInspection = "kInspection";
constexpr std::string_view kIntegerRange = "kIntegerRange";
constexpr std::string_view kIntegerSyntax = "kIntegerSyntax";
constexpr std::string_view kInvalid = "kInvalid";
constexpr std::string_view kInvalidBindingKind = "kInvalidBindingKind";
constexpr std::string_view kInvalidBound = "kInvalidBound";
constexpr std::string_view kInvalidConfiguration = "kInvalidConfiguration";
constexpr std::string_view kInvalidLimits = "kInvalidLimits";
constexpr std::string_view kInvalidOptions = "kInvalidOptions";
constexpr std::string_view kInvalidRequest = "kInvalidRequest";
constexpr std::string_view kInvalidSelection = "kInvalidSelection";
constexpr std::string_view kInvalidSourceStorage = "kInvalidSourceStorage";
constexpr std::string_view kInvalidStorage = "kInvalidStorage";
constexpr std::string_view kInvalidTransport = "kInvalidTransport";
constexpr std::string_view kInventory = "kInventory";
constexpr std::string_view kIoFailure = "kIoFailure";
constexpr std::string_view kIssueLimit = "kIssueLimit";
constexpr std::string_view kJsonSyntax = "kJsonSyntax";
constexpr std::string_view kLedgerMismatch = "kLedgerMismatch";
constexpr std::string_view kLimitExceeded = "kLimitExceeded";
constexpr std::string_view kLimitNotRepresentable = "kLimitNotRepresentable";
constexpr std::string_view kLineLimit = "kLineLimit";
constexpr std::string_view kLossNotAcknowledged = "kLossNotAcknowledged";
constexpr std::string_view kMalformed = "kMalformed";
constexpr std::string_view kMapEntryLimit = "kMapEntryLimit";
constexpr std::string_view kMapLimit = "kMapLimit";
constexpr std::string_view kMaterial = "kMaterial";
constexpr std::string_view kMaterialBinding = "kMaterialBinding";
constexpr std::string_view kMaterialIds = "kMaterialIds";
constexpr std::string_view kMaterialReference = "kMaterialReference";
constexpr std::string_view kMaterialization = "kMaterialization";
constexpr std::string_view kMember = "kMember";
constexpr std::string_view kMemberDigest = "kMemberDigest";
constexpr std::string_view kMemberInventory = "kMemberInventory";
constexpr std::string_view kMemberPath = "kMemberPath";
constexpr std::string_view kMeshMismatch = "kMeshMismatch";
constexpr std::string_view kMetadata = "kMetadata";
constexpr std::string_view kMetadataDuplicateKey = "kMetadataDuplicateKey";
constexpr std::string_view kMissingResource = "kMissingResource";
constexpr std::string_view kNameCollision = "kNameCollision";
constexpr std::string_view kNameLimit = "kNameLimit";
constexpr std::string_view kNameUnsupported = "kNameUnsupported";
constexpr std::string_view kNewCoincidence = "kNewCoincidence";
constexpr std::string_view kNoGeometricEar = "kNoGeometricEar";
constexpr std::string_view kNone = "kNone";
constexpr std::string_view kNonfinite = "kNonfinite";
constexpr std::string_view kNonfiniteAttribute = "kNonfiniteAttribute";
constexpr std::string_view kNonplanarFace = "kNonplanarFace";
constexpr std::string_view kNonzeroUnderflow = "kNonzeroUnderflow";
constexpr std::string_view kNormalAbsent = "kNormalAbsent";
constexpr std::string_view kNormalMissing = "kNormalMissing";
constexpr std::string_view kNormalNotConstant = "kNormalNotConstant";
constexpr std::string_view kNormalSelection = "kNormalSelection";
constexpr std::string_view kNotAttempted = "kNotAttempted";
constexpr std::string_view kNotEquivalent = "kNotEquivalent";
constexpr std::string_view kNotRegular = "kNotRegular";
constexpr std::string_view kNotRequested = "kNotRequested";
constexpr std::string_view kNotRequired = "kNotRequired";
constexpr std::string_view kNotTriangles = "kNotTriangles";
constexpr std::string_view kNumeric = "kNumeric";
constexpr std::string_view kNumericConversion = "kNumericConversion";
constexpr std::string_view kNumericPolicy = "kNumericPolicy";
constexpr std::string_view kNumericProfile = "kNumericProfile";
constexpr std::string_view kNumericRange = "kNumericRange";
constexpr std::string_view kNumericSyntax = "kNumericSyntax";
constexpr std::string_view kOffFailure = "kOffFailure";
constexpr std::string_view kOffRead = "kOffRead";
constexpr std::string_view kOffWorkflow = "kOffWorkflow";
constexpr std::string_view kOpen = "kOpen";
constexpr std::string_view kOrientation = "kOrientation";
constexpr std::string_view kOutputCountLimit = "kOutputCountLimit";
constexpr std::string_view kOutputFacts = "kOutputFacts";
constexpr std::string_view kOutputInspection = "kOutputInspection";
constexpr std::string_view kOutputLimit = "kOutputLimit";
constexpr std::string_view kOutputMaterialization = "kOutputMaterialization";
constexpr std::string_view kOutputStorage = "kOutputStorage";
constexpr std::string_view kPassed = "kPassed";
constexpr std::string_view kPathCollision = "kPathCollision";
constexpr std::string_view kPathPlanning = "kPathPlanning";
constexpr std::string_view kPathUnsupported = "kPathUnsupported";
constexpr std::string_view kPayloadBudget = "kPayloadBudget";
constexpr std::string_view kPerFaceCornerLimit = "kPerFaceCornerLimit";
constexpr std::string_view kPlan = "kPlan";
constexpr std::string_view kPolicy = "kPolicy";
constexpr std::string_view kPositionExtent = "kPositionExtent";
constexpr std::string_view kPositionNonfinite = "kPositionNonfinite";
constexpr std::string_view kPostcondition = "kPostcondition";
constexpr std::string_view kPreparedFields = "kPreparedFields";
constexpr std::string_view kPreservationMismatch = "kPreservationMismatch";
constexpr std::string_view kPrimitive = "kPrimitive";
constexpr std::string_view kPrimitiveRead = "kPrimitiveRead";
constexpr std::string_view kProjectionAcknowledgment =
    "kProjectionAcknowledgment";
constexpr std::string_view kProvider = "kProvider";
constexpr std::string_view kProviderConfiguration = "kProviderConfiguration";
constexpr std::string_view kProviderFailure = "kProviderFailure";
constexpr std::string_view kPublication = "kPublication";
constexpr std::string_view kPublicationFailed = "kPublicationFailed";
constexpr std::string_view kPublished = "kPublished";
constexpr std::string_view kRead = "kRead";
constexpr std::string_view kReceipt = "kReceipt";
constexpr std::string_view kReceiptBytes = "kReceiptBytes";
constexpr std::string_view kReceiptPath = "kReceiptPath";
constexpr std::string_view kReceiptSize = "kReceiptSize";
constexpr std::string_view kRejected = "kRejected";
constexpr std::string_view kRejectedOrIncomplete = "kRejectedOrIncomplete";
constexpr std::string_view kReload = "kReload";
constexpr std::string_view kReloadMismatch = "kReloadMismatch";
constexpr std::string_view kRemoved = "kRemoved";
constexpr std::string_view kRemovedRepresentative = "kRemovedRepresentative";
constexpr std::string_view kRepair = "kRepair";
constexpr std::string_view kRepeatedPosition = "kRepeatedPosition";
constexpr std::string_view kRepeatedTarget = "kRepeatedTarget";
constexpr std::string_view kReplacedSourceMetadata = "kReplacedSourceMetadata";
constexpr std::string_view kReport = "kReport";
constexpr std::string_view kReportExtent = "kReportExtent";
constexpr std::string_view kReportStringEncoding = "kReportStringEncoding";
constexpr std::string_view kRequestBudget = "kRequestBudget";
constexpr std::string_view kRequiredExtension = "kRequiredExtension";
constexpr std::string_view kRequiredFieldZero = "kRequiredFieldZero";
constexpr std::string_view kReread = "kReread";
constexpr std::string_view kResourceBinding = "kResourceBinding";
constexpr std::string_view kResourceBindingMismatch =
    "kResourceBindingMismatch";
constexpr std::string_view kResourceBytes = "kResourceBytes";
constexpr std::string_view kResourceExtent = "kResourceExtent";
constexpr std::string_view kResourceMedia = "kResourceMedia";
constexpr std::string_view kRetainedSourceMetadata = "kRetainedSourceMetadata";
constexpr std::string_view kSampler = "kSampler";
constexpr std::string_view kScalar = "kScalar";
constexpr std::string_view kScan = "kScan";
constexpr std::string_view kSchema = "kSchema";
constexpr std::string_view kSelection = "kSelection";
constexpr std::string_view kSelectionOrder = "kSelectionOrder";
constexpr std::string_view kSelectionRange = "kSelectionRange";
constexpr std::string_view kSelfIntersection = "kSelfIntersection";
constexpr std::string_view kSelfTarget = "kSelfTarget";
constexpr std::string_view kSharedBudget = "kSharedBudget";
constexpr std::string_view kSharedWorkersUnavailableOrFewerThanTwoFaces =
    "kSharedWorkersUnavailableOrFewerThanTwoFaces";
constexpr std::string_view kSizeOverflow = "kSizeOverflow";
constexpr std::string_view kSolidUnavailable = "kSolidUnavailable";
constexpr std::string_view kSource = "kSource";
constexpr std::string_view kSourceCapture = "kSourceCapture";
constexpr std::string_view kSourceChanged = "kSourceChanged";
constexpr std::string_view kSourceContract = "kSourceContract";
constexpr std::string_view kSourceCorrespondence = "kSourceCorrespondence";
constexpr std::string_view kSourceCount = "kSourceCount";
constexpr std::string_view kSourceCountLimit = "kSourceCountLimit";
constexpr std::string_view kSourceCounts = "kSourceCounts";
constexpr std::string_view kSourceInspection = "kSourceInspection";
constexpr std::string_view kSourceLimit = "kSourceLimit";
constexpr std::string_view kSourceMaterialFactors = "kSourceMaterialFactors";
constexpr std::string_view kSourceProtection = "kSourceProtection";
constexpr std::string_view kSourceStorage = "kSourceStorage";
constexpr std::string_view kStageAttemptsExhausted = "kStageAttemptsExhausted";
constexpr std::string_view kStageCreate = "kStageCreate";
constexpr std::string_view kStagedInventory = "kStagedInventory";
constexpr std::string_view kStaging = "kStaging";
constexpr std::string_view kStlDelegate = "kStlDelegate";
constexpr std::string_view kStlRead = "kStlRead";
constexpr std::string_view kStlWrite = "kStlWrite";
constexpr std::string_view kStopRequested = "kStopRequested";
constexpr std::string_view kStorageExtent = "kStorageExtent";
constexpr std::string_view kStorageIssues = "kStorageIssues";
constexpr std::string_view kSuccess = "kSuccess";
constexpr std::string_view kSyntax = "kSyntax";
constexpr std::string_view kTargetLimit = "kTargetLimit";
constexpr std::string_view kTargetRange = "kTargetRange";
constexpr std::string_view kTargets = "kTargets";
constexpr std::string_view kTextPolicy = "kTextPolicy";
constexpr std::string_view kTexture = "kTexture";
constexpr std::string_view kTokenLimit = "kTokenLimit";
constexpr std::string_view kTrailingBytes = "kTrailingBytes";
constexpr std::string_view kTrailingData = "kTrailingData";
constexpr std::string_view kTransportSelector = "kTransportSelector";
constexpr std::string_view kTriangleCount = "kTriangleCount";
constexpr std::string_view kTriangleExtent = "kTriangleExtent";
constexpr std::string_view kTriangulation = "kTriangulation";
constexpr std::string_view kUnicodeScalar = "kUnicodeScalar";
constexpr std::string_view kUnits = "kUnits";
constexpr std::string_view kUnknownEnum = "kUnknownEnum";
constexpr std::string_view kUnqueryable = "kUnqueryable";
constexpr std::string_view kUnsupported = "kUnsupported";
constexpr std::string_view kUnsupportedAllocationProfile =
    "kUnsupportedAllocationProfile";
constexpr std::string_view kUnsupportedAttribute = "kUnsupportedAttribute";
constexpr std::string_view kUnsupportedCoreString = "kUnsupportedCoreString";
constexpr std::string_view kUnsupportedCornerLimit = "kUnsupportedCornerLimit";
constexpr std::string_view kUnsupportedEnum = "kUnsupportedEnum";
constexpr std::string_view kUnsupportedFlushState = "kUnsupportedFlushState";
constexpr std::string_view kUnsupportedNumberRepresentation =
    "kUnsupportedNumberRepresentation";
constexpr std::string_view kUnsupportedNumericConversion =
    "kUnsupportedNumericConversion";
constexpr std::string_view kUnsupportedPayload = "kUnsupportedPayload";
constexpr std::string_view kUnsupportedProfile = "kUnsupportedProfile";
constexpr std::string_view kUnsupportedResourceRepresentation =
    "kUnsupportedResourceRepresentation";
constexpr std::string_view kUnsupportedRounding = "kUnsupportedRounding";
constexpr std::string_view kUnsupportedVariant = "kUnsupportedVariant";
constexpr std::string_view kUnsupportedVersion = "kUnsupportedVersion";
constexpr std::string_view kUvMap = "kUvMap";
constexpr std::string_view kUvReference = "kUvReference";
constexpr std::string_view kUvSet = "kUvSet";
constexpr std::string_view kValidation = "kValidation";
constexpr std::string_view kVariant = "kVariant";
constexpr std::string_view kVerification = "kVerification";
constexpr std::string_view kVerificationFailed = "kVerificationFailed";
constexpr std::string_view kVerificationFailure = "kVerificationFailure";
constexpr std::string_view kVerify = "kVerify";
constexpr std::string_view kVertex = "kVertex";
constexpr std::string_view kVertexLimit = "kVertexLimit";
constexpr std::string_view kVertexRange = "kVertexRange";
constexpr std::string_view kWireMap = "kWireMap";
constexpr std::string_view kWireMaterialFactors = "kWireMaterialFactors";
constexpr std::string_view kWireShape = "kWireShape";
constexpr std::string_view kWireValue = "kWireValue";
constexpr std::string_view kWorkerBudgetOne = "kWorkerBudgetOne";
constexpr std::string_view kWrite = "kWrite";
constexpr std::string_view kZeroLimit = "kZeroLimit";
constexpr std::string_view kCandidateRejected = "rejected";
constexpr std::string_view kCandidateUnchanged = "unchanged";
}  // namespace wire_symbols

template <class T>
struct OffGlbObservation {
  T value;
};

class ObservationLimit : public std::exception {
 public:
  const char* what() const noexcept override {
    return "OFF GLB observation exceeds its byte limit";
  }
};
constexpr std::size_t kObservationByteLimit = 16777216;
class ObservationSize {
 public:
  void Add(std::size_t bytes) {
    if (bytes > kObservationByteLimit - size_) throw ObservationLimit();
    size_ += bytes;
  }
  void String(std::string_view value) {
    Add(kJsonQuoteBytes);
    for (unsigned char c : value) {
      if (c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' ||
          c == '\r' || c == '\t')
        Add(kJsonShortEscapeBytes);
      else
        Add(c < kFirstPrintableAscii ? kJsonControlEscapeBytes : 1);
    }
  }
  void Object(std::initializer_list<std::string_view> keys) {
    Add(kJsonContainerDelimiterBytes);
    bool first = true;
    for (auto key : keys) {
      if (!first) Add(kJsonSeparatorBytes);
      first = false;
      String(key);
      Add(kJsonColonBytes);
    }
  }

 private:
  std::size_t size_ = 0;
};
[[maybe_unused]] void Measure(const ic::OffFileReadResult& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::OffFileDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::OffDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const geo::MaterializationDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const geo::StorageIssue& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::OffFileExecution& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const geo::MaterializationExecutionStats& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::OffExecution& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const OffGlbSurfaceWorkflowResult& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const OffGlbSurfaceDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const OffDuplicateFileWorkflowResult& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const OffWorkflowOutcomes& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const OffWorkflowDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const MaterializedDuplicateDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const OffWorkflowExecution& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const MaterializedDuplicateExecution& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const geo::ChargedTriangulationResult& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const geo::ChargedTriangulationDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const geo::ChargedTriangulationCounts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(
    const geo::ChargedTriangulationMaterializationFacts& value,
    ObservationSize& size);
[[maybe_unused]] void Measure(
    const geo::ChargedTriangulationInspectionFacts& value,
    ObservationSize& size);
[[maybe_unused]] void Measure(const geo::ChargedTriangulationCoreFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceEncodeResult& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceInspectionResult& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFilePublicationResult& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileCleanupObservation& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileIoObservation& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileExecutionStats& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileInventoryObservation& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileInspectionObservation& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileProviderObservation& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFileCommitObservation& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfacePrimitiveView& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceMeshCounts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceWireCounts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceDelegateView& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceGeometryFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceGlbFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceGltfFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::GltfDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::GltfSourceSpan& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceStlFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::StlDiagnostic& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::StlExecution& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::StlRequestedBytes& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::StlWriteFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceCoreFactorFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceNumericSummary& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceDyadicError& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceWorkCounts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceDelegateProfileView& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceDelegateProfileFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const ic::SurfaceFacts& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(const std::error_code& value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::OffFileStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::OffFileStage value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::OffFileDiagnosticCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::OffStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::OffStage value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::OffDiagnosticCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::MaterializationStatus value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::MaterializationDiagnosticCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::StorageIssueCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(OffGlbSurfaceStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(OffGlbSurfaceStage value, ObservationSize& size);
[[maybe_unused]] void Measure(OffGlbSurfaceCode value, ObservationSize& size);
[[maybe_unused]] void Measure(OffWorkflowStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(OffWorkflowExecutionOutcome value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(CandidateOutcome value, ObservationSize& size);
[[maybe_unused]] void Measure(OffWorkflowProfileOutcome value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(OffWorkflowPublicationOutcome value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(OffWorkflowStage value, ObservationSize& size);
[[maybe_unused]] void Measure(OffWorkflowExitMeaning value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(OffWorkflowDiagnosticCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(MaterializedDuplicateStatus value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(MaterializedDuplicateStage value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(MaterializedDuplicateDiagnosticCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::ChargedTriangulationStatus value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::ChargedTriangulationStage value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::ChargedTriangulationCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::TriangulationStatus value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(geo::ChargedTriangulationSerialReason value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceStage value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceCode value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceSubject value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceFilePublicationOutcome value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceFileStatus value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceFileStage value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceFileCode value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceFileCleanupOutcome value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceFilePhaseOutcome value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceFileCommitOutcome value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceDelegateKind value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::GlbStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::GltfStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::GltfDiagnosticCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::GltfResourceKind value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::StlStatus value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::StlStage value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::StlDiagnosticCode value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::StlEncoding value, ObservationSize& size);
[[maybe_unused]] void Measure(ic::StlMetadataPolicy value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceNumericSummary::Kind value,
                              ObservationSize& size);
[[maybe_unused]] void Measure(ic::SurfaceDelegateProfileStatus value,
                              ObservationSize& size);
[[maybe_unused]] nb::object Wire(const ic::OffFileReadResult& value);
[[maybe_unused]] nb::object Wire(const ic::OffFileDiagnostic& value);
[[maybe_unused]] nb::object Wire(const ic::OffDiagnostic& value);
[[maybe_unused]] nb::object Wire(const geo::MaterializationDiagnostic& value);
[[maybe_unused]] nb::object Wire(const geo::StorageIssue& value);
[[maybe_unused]] nb::object Wire(const ic::OffFileExecution& value);
[[maybe_unused]] nb::object Wire(
    const geo::MaterializationExecutionStats& value);
[[maybe_unused]] nb::object Wire(const ic::OffExecution& value);
[[maybe_unused]] nb::object Wire(const OffGlbSurfaceWorkflowResult& value);
[[maybe_unused]] nb::object Wire(const OffGlbSurfaceDiagnostic& value);
[[maybe_unused]] nb::object Wire(const OffDuplicateFileWorkflowResult& value);
[[maybe_unused]] nb::object Wire(const OffWorkflowOutcomes& value);
[[maybe_unused]] nb::object Wire(const OffWorkflowDiagnostic& value);
[[maybe_unused]] nb::object Wire(const MaterializedDuplicateDiagnostic& value);
[[maybe_unused]] nb::object Wire(const OffWorkflowExecution& value);
[[maybe_unused]] nb::object Wire(const MaterializedDuplicateExecution& value);
[[maybe_unused]] nb::object Wire(const geo::ChargedTriangulationResult& value);
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationDiagnostic& value);
[[maybe_unused]] nb::object Wire(const geo::ChargedTriangulationCounts& value);
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationMaterializationFacts& value);
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationInspectionFacts& value);
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationCoreFacts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceEncodeResult& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceDiagnostic& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceInspectionResult& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceFilePublicationResult& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileDiagnostic& value);
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileCleanupObservation& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileFacts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileIoObservation& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileExecutionStats& value);
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileInventoryObservation& value);
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileInspectionObservation& value);
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileProviderObservation& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileCommitObservation& value);
[[maybe_unused]] nb::object Wire(const ic::SurfacePrimitiveView& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceMeshCounts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceWireCounts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceDelegateView& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceGeometryFacts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceGlbFacts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceGltfFacts& value);
[[maybe_unused]] nb::object Wire(const ic::GltfDiagnostic& value);
[[maybe_unused]] nb::object Wire(const ic::GltfSourceSpan& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceStlFacts& value);
[[maybe_unused]] nb::object Wire(const ic::StlDiagnostic& value);
[[maybe_unused]] nb::object Wire(const ic::StlExecution& value);
[[maybe_unused]] nb::object Wire(const ic::StlRequestedBytes& value);
[[maybe_unused]] nb::object Wire(const ic::StlWriteFacts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceCoreFactorFacts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceNumericSummary& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceDyadicError& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceWorkCounts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceDelegateProfileView& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceDelegateProfileFacts& value);
[[maybe_unused]] nb::object Wire(const ic::SurfaceFacts& value);
[[maybe_unused]] nb::object Wire(const std::error_code& value);
[[maybe_unused]] nb::object Wire(ic::OffFileStatus value);
[[maybe_unused]] nb::object Wire(ic::OffFileStage value);
[[maybe_unused]] nb::object Wire(ic::OffFileDiagnosticCode value);
[[maybe_unused]] nb::object Wire(ic::OffStatus value);
[[maybe_unused]] nb::object Wire(ic::OffStage value);
[[maybe_unused]] nb::object Wire(ic::OffDiagnosticCode value);
[[maybe_unused]] nb::object Wire(geo::MaterializationStatus value);
[[maybe_unused]] nb::object Wire(geo::MaterializationDiagnosticCode value);
[[maybe_unused]] nb::object Wire(geo::StorageIssueCode value);
[[maybe_unused]] nb::object Wire(OffGlbSurfaceStatus value);
[[maybe_unused]] nb::object Wire(OffGlbSurfaceStage value);
[[maybe_unused]] nb::object Wire(OffGlbSurfaceCode value);
[[maybe_unused]] nb::object Wire(OffWorkflowStatus value);
[[maybe_unused]] nb::object Wire(OffWorkflowExecutionOutcome value);
[[maybe_unused]] nb::object Wire(CandidateOutcome value);
[[maybe_unused]] nb::object Wire(OffWorkflowProfileOutcome value);
[[maybe_unused]] nb::object Wire(OffWorkflowPublicationOutcome value);
[[maybe_unused]] nb::object Wire(OffWorkflowStage value);
[[maybe_unused]] nb::object Wire(OffWorkflowExitMeaning value);
[[maybe_unused]] nb::object Wire(OffWorkflowDiagnosticCode value);
[[maybe_unused]] nb::object Wire(MaterializedDuplicateStatus value);
[[maybe_unused]] nb::object Wire(MaterializedDuplicateStage value);
[[maybe_unused]] nb::object Wire(MaterializedDuplicateDiagnosticCode value);
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationStatus value);
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationStage value);
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationCode value);
[[maybe_unused]] nb::object Wire(geo::TriangulationStatus value);
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationSerialReason value);
[[maybe_unused]] nb::object Wire(ic::SurfaceStatus value);
[[maybe_unused]] nb::object Wire(ic::SurfaceStage value);
[[maybe_unused]] nb::object Wire(ic::SurfaceCode value);
[[maybe_unused]] nb::object Wire(ic::SurfaceSubject value);
[[maybe_unused]] nb::object Wire(ic::SurfaceFilePublicationOutcome value);
[[maybe_unused]] nb::object Wire(ic::SurfaceFileStatus value);
[[maybe_unused]] nb::object Wire(ic::SurfaceFileStage value);
[[maybe_unused]] nb::object Wire(ic::SurfaceFileCode value);
[[maybe_unused]] nb::object Wire(ic::SurfaceFileCleanupOutcome value);
[[maybe_unused]] nb::object Wire(ic::SurfaceFilePhaseOutcome value);
[[maybe_unused]] nb::object Wire(ic::SurfaceFileCommitOutcome value);
[[maybe_unused]] nb::object Wire(ic::SurfaceDelegateKind value);
[[maybe_unused]] nb::object Wire(ic::GlbStatus value);
[[maybe_unused]] nb::object Wire(ic::GltfStatus value);
[[maybe_unused]] nb::object Wire(ic::GltfDiagnosticCode value);
[[maybe_unused]] nb::object Wire(ic::GltfResourceKind value);
[[maybe_unused]] nb::object Wire(ic::StlStatus value);
[[maybe_unused]] nb::object Wire(ic::StlStage value);
[[maybe_unused]] nb::object Wire(ic::StlDiagnosticCode value);
[[maybe_unused]] nb::object Wire(ic::StlEncoding value);
[[maybe_unused]] nb::object Wire(ic::StlMetadataPolicy value);
[[maybe_unused]] nb::object Wire(ic::SurfaceNumericSummary::Kind value);
[[maybe_unused]] nb::object Wire(ic::SurfaceDelegateProfileStatus value);

template <class T>
nb::object Value(const T& value);
template <class T>
nb::object Value(const std::optional<T>& value);
template <class... T>
nb::object Value(const std::variant<T...>& value);
template <class T, std::size_t N>
nb::object Value(const std::array<T, N>& value);
template <class T, std::size_t N>
nb::object Value(std::span<T, N> value);

template <class Range, class Function>
nb::tuple Tuple(const Range& values, Function&& function) {
  nb::tuple result =
      nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
  if (!result.is_valid()) throw nb::python_error();
  for (std::size_t i = 0; i < values.size(); ++i)
    PyTuple_SET_ITEM(result.ptr(), static_cast<Py_ssize_t>(i),
                     function(values[i]).release().ptr());
  return result;
}
template <class T>
nb::object Value(const T& value) {
  if constexpr (std::is_same_v<T, std::monostate>)
    return nb::none();
  else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
    return nb::cast(value);
  else
    return nb::cast(OffGlbObservation<T>{value});
}
template <class T>
nb::object Value(const std::optional<T>& value) {
  return value ? Value(*value) : nb::none();
}
template <class... T>
nb::object Value(const std::variant<T...>& value) {
  return std::visit([](const auto& item) { return Value(item); }, value);
}
template <class T, std::size_t N>
nb::object Value(const std::array<T, N>& value) {
  return Tuple(value, [](const auto& item) { return Value(item); });
}
template <class T, std::size_t N>
nb::object Value(std::span<T, N> value) {
  return Tuple(value, [](const auto& item) { return Value(item); });
}

template <class T>
[[maybe_unused]] nb::object Wire(const T& value);
template <class T>
[[maybe_unused]] nb::object Wire(const std::optional<T>& value);
template <class... T>
[[maybe_unused]] nb::object Wire(const std::variant<T...>& value);
template <class T, std::size_t N>
[[maybe_unused]] nb::object Wire(const std::array<T, N>& value);
template <class T, std::size_t N>
[[maybe_unused]] nb::object Wire(std::span<T, N> value);

template <class T>
[[maybe_unused]] nb::object Wire(const T& value) {
  if constexpr (std::is_same_v<T, std::monostate>)
    return nb::none();
  else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> &&
                     sizeof(T) == sizeof(std::uint64_t))
    return nb::str(std::to_string(value).c_str());
  else {
    static_assert(std::is_arithmetic_v<T>);
    return nb::cast(value);
  }
}
template <class T>
[[maybe_unused]] nb::object Wire(const std::optional<T>& value) {
  return value ? Wire(*value) : nb::none();
}
template <class... T>
[[maybe_unused]] nb::object Wire(const std::variant<T...>& value) {
  return std::visit([](const auto& item) { return Wire(item); }, value);
}
template <class T, std::size_t N>
[[maybe_unused]] nb::object Wire(const std::array<T, N>& value) {
  return Tuple(value, [](const auto& item) { return Wire(item); });
}
template <class T, std::size_t N>
[[maybe_unused]] nb::object Wire(std::span<T, N> value) {
  return Tuple(value, [](const auto& item) { return Wire(item); });
}
template <class T>
nb::str HexWord(T value) {
  std::array<char, sizeof(T) * kHexDigitsPerByte> text{};
  for (std::size_t i = 0; i < text.size(); ++i) {
    text[text.size() - 1 - i] = kHexDigits[value & 0xf];
    value >>= 4;
  }
  return nb::str(text.data(), text.size());
}

template <class T>
[[maybe_unused]] void Measure(const T& value, ObservationSize& size);
template <class T>
[[maybe_unused]] void Measure(const std::optional<T>& value,
                              ObservationSize& size);
template <class... T>
[[maybe_unused]] void Measure(const std::variant<T...>& value,
                              ObservationSize& size);
template <class T, std::size_t N>
[[maybe_unused]] void Measure(const std::array<T, N>& value,
                              ObservationSize& size);
template <class T, std::size_t N>
[[maybe_unused]] void Measure(std::span<T, N> value, ObservationSize& size);
template <class T, std::size_t N>
void MeasureHexWords(const std::array<T, N>& words, ObservationSize& size) {
  size.Add(kJsonContainerDelimiterBytes);
  bool first = true;
  for (const auto word : words) {
    (void)word;
    if (!first) size.Add(kJsonSeparatorBytes);
    first = false;
    size.Add(kJsonQuoteBytes + sizeof(T) * kHexDigitsPerByte);
  }
}

template <class T>
[[maybe_unused]] void Measure(const T& value, ObservationSize& size) {
  if constexpr (std::is_same_v<T, std::monostate>)
    size.Add(kWireNull.size());
  else if constexpr (std::is_same_v<T, bool>)
    size.Add(value ? kWireTrue.size() : kWireFalse.size());
  else {
    static_assert(std::is_integral_v<T>);
    std::array<char, 32> buffer{};
    auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    size.Add(static_cast<std::size_t>(converted.ptr - buffer.data()));
    if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(std::uint64_t))
      size.Add(kJsonQuoteBytes);
  }
}
template <class T>
[[maybe_unused]] void Measure(const std::optional<T>& value,
                              ObservationSize& size) {
  if (value)
    Measure(*value, size);
  else
    size.Add(kWireNull.size());
}
template <class... T>
[[maybe_unused]] void Measure(const std::variant<T...>& value,
                              ObservationSize& size) {
  std::visit([&](const auto& item) { Measure(item, size); }, value);
}
template <class Range>
void MeasureArray(const Range& value, ObservationSize& size) {
  size.Add(kJsonContainerDelimiterBytes);
  bool first = true;
  for (const auto& item : value) {
    if (!first) size.Add(kJsonSeparatorBytes);
    first = false;
    Measure(item, size);
  }
}
template <class T, std::size_t N>
[[maybe_unused]] void Measure(const std::array<T, N>& value,
                              ObservationSize& size) {
  MeasureArray(value, size);
}
template <class T, std::size_t N>
[[maybe_unused]] void Measure(std::span<T, N> value, ObservationSize& size) {
  MeasureArray(value, size);
}
std::string_view WireSymbol(ic::OffFileStatus value) {
  switch (value) {
    case ic::OffFileStatus::kSuccess:
      return wire_symbols::kSuccess;
    case ic::OffFileStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case ic::OffFileStatus::kInvalidRequest:
      return wire_symbols::kInvalidRequest;
    case ic::OffFileStatus::kUnsupportedProfile:
      return wire_symbols::kUnsupportedProfile;
    case ic::OffFileStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case ic::OffFileStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case ic::OffFileStatus::kForeignBudget:
      return wire_symbols::kForeignBudget;
    case ic::OffFileStatus::kIoFailure:
      return wire_symbols::kIoFailure;
    case ic::OffFileStatus::kFileChanged:
      return wire_symbols::kFileChanged;
    case ic::OffFileStatus::kOffFailure:
      return wire_symbols::kOffFailure;
    case ic::OffFileStatus::kGeometryFailure:
      return wire_symbols::kGeometryFailure;
    case ic::OffFileStatus::kPreservationMismatch:
      return wire_symbols::kPreservationMismatch;
    case ic::OffFileStatus::kProviderFailure:
      return wire_symbols::kProviderFailure;
    case ic::OffFileStatus::kDestinationExists:
      return wire_symbols::kDestinationExists;
    case ic::OffFileStatus::kPublicationFailed:
      return wire_symbols::kPublicationFailed;
    case ic::OffFileStatus::kCanceled:
      return wire_symbols::kCanceled;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::OffFileStatus value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::OffFileStage value) {
  switch (value) {
    case ic::OffFileStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case ic::OffFileStage::kPathPlanning:
      return wire_symbols::kPathPlanning;
    case ic::OffFileStage::kCapture:
      return wire_symbols::kCapture;
    case ic::OffFileStage::kOffRead:
      return wire_symbols::kOffRead;
    case ic::OffFileStage::kExpectedInspection:
      return wire_symbols::kExpectedInspection;
    case ic::OffFileStage::kStaging:
      return wire_symbols::kStaging;
    case ic::OffFileStage::kReload:
      return wire_symbols::kReload;
    case ic::OffFileStage::kVerification:
      return wire_symbols::kVerification;
    case ic::OffFileStage::kProvider:
      return wire_symbols::kProvider;
    case ic::OffFileStage::kInventory:
      return wire_symbols::kInventory;
    case ic::OffFileStage::kPublication:
      return wire_symbols::kPublication;
    case ic::OffFileStage::kComplete:
      return wire_symbols::kComplete;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::OffFileStage value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::OffFileDiagnosticCode value) {
  switch (value) {
    case ic::OffFileDiagnosticCode::kNone:
      return wire_symbols::kNone;
    case ic::OffFileDiagnosticCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case ic::OffFileDiagnosticCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case ic::OffFileDiagnosticCode::kCountLimit:
      return wire_symbols::kCountLimit;
    case ic::OffFileDiagnosticCode::kPathUnsupported:
      return wire_symbols::kPathUnsupported;
    case ic::OffFileDiagnosticCode::kNameUnsupported:
      return wire_symbols::kNameUnsupported;
    case ic::OffFileDiagnosticCode::kNameCollision:
      return wire_symbols::kNameCollision;
    case ic::OffFileDiagnosticCode::kNotRegular:
      return wire_symbols::kNotRegular;
    case ic::OffFileDiagnosticCode::kOpen:
      return wire_symbols::kOpen;
    case ic::OffFileDiagnosticCode::kRead:
      return wire_symbols::kRead;
    case ic::OffFileDiagnosticCode::kWrite:
      return wire_symbols::kWrite;
    case ic::OffFileDiagnosticCode::kClose:
      return wire_symbols::kClose;
    case ic::OffFileDiagnosticCode::kChanged:
      return wire_symbols::kChanged;
    case ic::OffFileDiagnosticCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case ic::OffFileDiagnosticCode::kSharedBudget:
      return wire_symbols::kSharedBudget;
    case ic::OffFileDiagnosticCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case ic::OffFileDiagnosticCode::kLedgerMismatch:
      return wire_symbols::kLedgerMismatch;
    case ic::OffFileDiagnosticCode::kDelegatedOff:
      return wire_symbols::kDelegatedOff;
    case ic::OffFileDiagnosticCode::kDelegatedInspection:
      return wire_symbols::kDelegatedInspection;
    case ic::OffFileDiagnosticCode::kAttributeUnsupported:
      return wire_symbols::kAttributeUnsupported;
    case ic::OffFileDiagnosticCode::kOutputFacts:
      return wire_symbols::kOutputFacts;
    case ic::OffFileDiagnosticCode::kByteMismatch:
      return wire_symbols::kByteMismatch;
    case ic::OffFileDiagnosticCode::kMeshMismatch:
      return wire_symbols::kMeshMismatch;
    case ic::OffFileDiagnosticCode::kProvider:
      return wire_symbols::kProvider;
    case ic::OffFileDiagnosticCode::kStagedInventory:
      return wire_symbols::kStagedInventory;
    case ic::OffFileDiagnosticCode::kDestinationExists:
      return wire_symbols::kDestinationExists;
    case ic::OffFileDiagnosticCode::kCommit:
      return wire_symbols::kCommit;
    case ic::OffFileDiagnosticCode::kCommitUnsupported:
      return wire_symbols::kCommitUnsupported;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::OffFileDiagnosticCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::OffStatus value) {
  switch (value) {
    case ic::OffStatus::kSuccess:
      return wire_symbols::kSuccess;
    case ic::OffStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case ic::OffStatus::kMalformed:
      return wire_symbols::kMalformed;
    case ic::OffStatus::kUnsupportedVariant:
      return wire_symbols::kUnsupportedVariant;
    case ic::OffStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case ic::OffStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case ic::OffStatus::kCanceled:
      return wire_symbols::kCanceled;
    case ic::OffStatus::kInvalidStorage:
      return wire_symbols::kInvalidStorage;
    case ic::OffStatus::kGeometryFailure:
      return wire_symbols::kGeometryFailure;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::OffStatus value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::OffStage value) {
  switch (value) {
    case ic::OffStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case ic::OffStage::kScan:
      return wire_symbols::kScan;
    case ic::OffStage::kDecode:
      return wire_symbols::kDecode;
    case ic::OffStage::kMaterialization:
      return wire_symbols::kMaterialization;
    case ic::OffStage::kInspection:
      return wire_symbols::kInspection;
    case ic::OffStage::kValidation:
      return wire_symbols::kValidation;
    case ic::OffStage::kEncode:
      return wire_symbols::kEncode;
    case ic::OffStage::kPublication:
      return wire_symbols::kPublication;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::OffStage value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::OffDiagnosticCode value) {
  switch (value) {
    case ic::OffDiagnosticCode::kNone:
      return wire_symbols::kNone;
    case ic::OffDiagnosticCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case ic::OffDiagnosticCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case ic::OffDiagnosticCode::kSourceLimit:
      return wire_symbols::kSourceLimit;
    case ic::OffDiagnosticCode::kVertexLimit:
      return wire_symbols::kVertexLimit;
    case ic::OffDiagnosticCode::kFaceLimit:
      return wire_symbols::kFaceLimit;
    case ic::OffDiagnosticCode::kCornerLimit:
      return wire_symbols::kCornerLimit;
    case ic::OffDiagnosticCode::kPerFaceCornerLimit:
      return wire_symbols::kPerFaceCornerLimit;
    case ic::OffDiagnosticCode::kLineLimit:
      return wire_symbols::kLineLimit;
    case ic::OffDiagnosticCode::kTokenLimit:
      return wire_symbols::kTokenLimit;
    case ic::OffDiagnosticCode::kCommentLimit:
      return wire_symbols::kCommentLimit;
    case ic::OffDiagnosticCode::kOutputLimit:
      return wire_symbols::kOutputLimit;
    case ic::OffDiagnosticCode::kSyntax:
      return wire_symbols::kSyntax;
    case ic::OffDiagnosticCode::kIntegerSyntax:
      return wire_symbols::kIntegerSyntax;
    case ic::OffDiagnosticCode::kIntegerRange:
      return wire_symbols::kIntegerRange;
    case ic::OffDiagnosticCode::kNumericSyntax:
      return wire_symbols::kNumericSyntax;
    case ic::OffDiagnosticCode::kNumericRange:
      return wire_symbols::kNumericRange;
    case ic::OffDiagnosticCode::kNonfinite:
      return wire_symbols::kNonfinite;
    case ic::OffDiagnosticCode::kVariant:
      return wire_symbols::kVariant;
    case ic::OffDiagnosticCode::kUnsupportedPayload:
      return wire_symbols::kUnsupportedPayload;
    case ic::OffDiagnosticCode::kTrailingData:
      return wire_symbols::kTrailingData;
    case ic::OffDiagnosticCode::kVertexRange:
      return wire_symbols::kVertexRange;
    case ic::OffDiagnosticCode::kFaceTooShort:
      return wire_symbols::kFaceTooShort;
    case ic::OffDiagnosticCode::kUnsupportedAttribute:
      return wire_symbols::kUnsupportedAttribute;
    case ic::OffDiagnosticCode::kTextPolicy:
      return wire_symbols::kTextPolicy;
    case ic::OffDiagnosticCode::kEdgeCountPolicy:
      return wire_symbols::kEdgeCountPolicy;
    case ic::OffDiagnosticCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case ic::OffDiagnosticCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case ic::OffDiagnosticCode::kGeometry:
      return wire_symbols::kGeometry;
    case ic::OffDiagnosticCode::kStorageIssues:
      return wire_symbols::kStorageIssues;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::OffDiagnosticCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::MaterializationStatus value) {
  switch (value) {
    case geo::MaterializationStatus::kSuccess:
      return wire_symbols::kSuccess;
    case geo::MaterializationStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case geo::MaterializationStatus::kInvalidTransport:
      return wire_symbols::kInvalidTransport;
    case geo::MaterializationStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case geo::MaterializationStatus::kUnsupportedAllocationProfile:
      return wire_symbols::kUnsupportedAllocationProfile;
    case geo::MaterializationStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case geo::MaterializationStatus::kCanceled:
      return wire_symbols::kCanceled;
    case geo::MaterializationStatus::kInvalidSourceStorage:
      return wire_symbols::kInvalidSourceStorage;
    case geo::MaterializationStatus::kInvalidSelection:
      return wire_symbols::kInvalidSelection;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::MaterializationStatus value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::MaterializationDiagnosticCode value) {
  switch (value) {
    case geo::MaterializationDiagnosticCode::kNone:
      return wire_symbols::kNone;
    case geo::MaterializationDiagnosticCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case geo::MaterializationDiagnosticCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case geo::MaterializationDiagnosticCode::kTransportSelector:
      return wire_symbols::kTransportSelector;
    case geo::MaterializationDiagnosticCode::kPositionExtent:
      return wire_symbols::kPositionExtent;
    case geo::MaterializationDiagnosticCode::kTriangleExtent:
      return wire_symbols::kTriangleExtent;
    case geo::MaterializationDiagnosticCode::kStorageExtent:
      return wire_symbols::kStorageExtent;
    case geo::MaterializationDiagnosticCode::kCountLimit:
      return wire_symbols::kCountLimit;
    case geo::MaterializationDiagnosticCode::kIssueLimit:
      return wire_symbols::kIssueLimit;
    case geo::MaterializationDiagnosticCode::kAllocationProfile:
      return wire_symbols::kAllocationProfile;
    case geo::MaterializationDiagnosticCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case geo::MaterializationDiagnosticCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case geo::MaterializationDiagnosticCode::kSourceStorage:
      return wire_symbols::kSourceStorage;
    case geo::MaterializationDiagnosticCode::kSelectionOrder:
      return wire_symbols::kSelectionOrder;
    case geo::MaterializationDiagnosticCode::kSelectionRange:
      return wire_symbols::kSelectionRange;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::MaterializationDiagnosticCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::StorageIssueCode value) {
  switch (value) {
    case geo::StorageIssueCode::kPositionNonfinite:
      return wire_symbols::kPositionNonfinite;
    case geo::StorageIssueCode::kFaceOffsetsEmpty:
      return wire_symbols::kFaceOffsetsEmpty;
    case geo::StorageIssueCode::kFaceOffsetsStart:
      return wire_symbols::kFaceOffsetsStart;
    case geo::StorageIssueCode::kFaceOffsetsEnd:
      return wire_symbols::kFaceOffsetsEnd;
    case geo::StorageIssueCode::kFaceOffsetRange:
      return wire_symbols::kFaceOffsetRange;
    case geo::StorageIssueCode::kFaceOffsetOrder:
      return wire_symbols::kFaceOffsetOrder;
    case geo::StorageIssueCode::kFaceTooShort:
      return wire_symbols::kFaceTooShort;
    case geo::StorageIssueCode::kCornerVertexRange:
      return wire_symbols::kCornerVertexRange;
    case geo::StorageIssueCode::kAttributeDuplicateName:
      return wire_symbols::kAttributeDuplicateName;
    case geo::StorageIssueCode::kAttributeDomain:
      return wire_symbols::kAttributeDomain;
    case geo::StorageIssueCode::kAttributeNameEmpty:
      return wire_symbols::kAttributeNameEmpty;
    case geo::StorageIssueCode::kAttributeComponentsZero:
      return wire_symbols::kAttributeComponentsZero;
    case geo::StorageIssueCode::kAttributeDenseShape:
      return wire_symbols::kAttributeDenseShape;
    case geo::StorageIssueCode::kAttributeOffsetsEmpty:
      return wire_symbols::kAttributeOffsetsEmpty;
    case geo::StorageIssueCode::kAttributeOffsetsCount:
      return wire_symbols::kAttributeOffsetsCount;
    case geo::StorageIssueCode::kAttributeOffsetsStart:
      return wire_symbols::kAttributeOffsetsStart;
    case geo::StorageIssueCode::kAttributeOffsetsEnd:
      return wire_symbols::kAttributeOffsetsEnd;
    case geo::StorageIssueCode::kAttributeOffsetRange:
      return wire_symbols::kAttributeOffsetRange;
    case geo::StorageIssueCode::kAttributeOffsetOrder:
      return wire_symbols::kAttributeOffsetOrder;
    case geo::StorageIssueCode::kAttributeOffsetAlignment:
      return wire_symbols::kAttributeOffsetAlignment;
    case geo::StorageIssueCode::kAttributePresenceCount:
      return wire_symbols::kAttributePresenceCount;
    case geo::StorageIssueCode::kAttributePresenceValue:
      return wire_symbols::kAttributePresenceValue;
    case geo::StorageIssueCode::kMetadataDuplicateKey:
      return wire_symbols::kMetadataDuplicateKey;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::StorageIssueCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffGlbSurfaceStatus value) {
  switch (value) {
    case OffGlbSurfaceStatus::kSuccess:
      return wire_symbols::kSuccess;
    case OffGlbSurfaceStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case OffGlbSurfaceStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case OffGlbSurfaceStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case OffGlbSurfaceStatus::kCanceled:
      return wire_symbols::kCanceled;
    case OffGlbSurfaceStatus::kRejected:
      return wire_symbols::kRejected;
    case OffGlbSurfaceStatus::kUnsupported:
      return wire_symbols::kUnsupported;
    case OffGlbSurfaceStatus::kDelegatedFailure:
      return wire_symbols::kDelegatedFailure;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffGlbSurfaceStatus value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffGlbSurfaceStage value) {
  switch (value) {
    case OffGlbSurfaceStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case OffGlbSurfaceStage::kOffWorkflow:
      return wire_symbols::kOffWorkflow;
    case OffGlbSurfaceStage::kTriangulation:
      return wire_symbols::kTriangulation;
    case OffGlbSurfaceStage::kEncoding:
      return wire_symbols::kEncoding;
    case OffGlbSurfaceStage::kInspection:
      return wire_symbols::kInspection;
    case OffGlbSurfaceStage::kPublication:
      return wire_symbols::kPublication;
    case OffGlbSurfaceStage::kComplete:
      return wire_symbols::kComplete;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffGlbSurfaceStage value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffGlbSurfaceCode value) {
  switch (value) {
    case OffGlbSurfaceCode::kNone:
      return wire_symbols::kNone;
    case OffGlbSurfaceCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case OffGlbSurfaceCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case OffGlbSurfaceCode::kPolicy:
      return wire_symbols::kPolicy;
    case OffGlbSurfaceCode::kCoordinates:
      return wire_symbols::kCoordinates;
    case OffGlbSurfaceCode::kNumericPolicy:
      return wire_symbols::kNumericPolicy;
    case OffGlbSurfaceCode::kProjectionAcknowledgment:
      return wire_symbols::kProjectionAcknowledgment;
    case OffGlbSurfaceCode::kInputIdentity:
      return wire_symbols::kInputIdentity;
    case OffGlbSurfaceCode::kReceiptPath:
      return wire_symbols::kReceiptPath;
    case OffGlbSurfaceCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case OffGlbSurfaceCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case OffGlbSurfaceCode::kOffWorkflow:
      return wire_symbols::kOffWorkflow;
    case OffGlbSurfaceCode::kTriangulation:
      return wire_symbols::kTriangulation;
    case OffGlbSurfaceCode::kEncoding:
      return wire_symbols::kEncoding;
    case OffGlbSurfaceCode::kInspection:
      return wire_symbols::kInspection;
    case OffGlbSurfaceCode::kPublication:
      return wire_symbols::kPublication;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffGlbSurfaceCode value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffWorkflowStatus value) {
  switch (value) {
    case OffWorkflowStatus::kSuccess:
      return wire_symbols::kSuccess;
    case OffWorkflowStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case OffWorkflowStatus::kInvalidRequest:
      return wire_symbols::kInvalidRequest;
    case OffWorkflowStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case OffWorkflowStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case OffWorkflowStatus::kCanceled:
      return wire_symbols::kCanceled;
    case OffWorkflowStatus::kRejected:
      return wire_symbols::kRejected;
    case OffWorkflowStatus::kDelegatedFailure:
      return wire_symbols::kDelegatedFailure;
    case OffWorkflowStatus::kVerificationFailure:
      return wire_symbols::kVerificationFailure;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffWorkflowStatus value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffWorkflowExecutionOutcome value) {
  switch (value) {
    case OffWorkflowExecutionOutcome::kCompleted:
      return wire_symbols::kCompleted;
    case OffWorkflowExecutionOutcome::kFailed:
      return wire_symbols::kFailed;
    case OffWorkflowExecutionOutcome::kCanceled:
      return wire_symbols::kCanceled;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffWorkflowExecutionOutcome value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(CandidateOutcome value) {
  switch (value) {
    case CandidateOutcome::accepted:
      return wire_symbols::kCandidateAccepted;
    case CandidateOutcome::unchanged:
      return wire_symbols::kCandidateUnchanged;
    case CandidateOutcome::rejected:
      return wire_symbols::kCandidateRejected;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(CandidateOutcome value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffWorkflowProfileOutcome value) {
  switch (value) {
    case OffWorkflowProfileOutcome::kPassed:
      return wire_symbols::kPassed;
    case OffWorkflowProfileOutcome::kFailed:
      return wire_symbols::kFailed;
    case OffWorkflowProfileOutcome::kIncomplete:
      return wire_symbols::kIncomplete;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffWorkflowProfileOutcome value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffWorkflowPublicationOutcome value) {
  switch (value) {
    case OffWorkflowPublicationOutcome::kNotRequested:
      return wire_symbols::kNotRequested;
    case OffWorkflowPublicationOutcome::kNotAttempted:
      return wire_symbols::kNotAttempted;
    case OffWorkflowPublicationOutcome::kFailed:
      return wire_symbols::kFailed;
    case OffWorkflowPublicationOutcome::kCanceled:
      return wire_symbols::kCanceled;
    case OffWorkflowPublicationOutcome::kPublished:
      return wire_symbols::kPublished;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffWorkflowPublicationOutcome value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffWorkflowStage value) {
  switch (value) {
    case OffWorkflowStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case OffWorkflowStage::kSourceProtection:
      return wire_symbols::kSourceProtection;
    case OffWorkflowStage::kRepair:
      return wire_symbols::kRepair;
    case OffWorkflowStage::kWrite:
      return wire_symbols::kWrite;
    case OffWorkflowStage::kReload:
      return wire_symbols::kReload;
    case OffWorkflowStage::kVerify:
      return wire_symbols::kVerify;
    case OffWorkflowStage::kHash:
      return wire_symbols::kHash;
    case OffWorkflowStage::kReport:
      return wire_symbols::kReport;
    case OffWorkflowStage::kPublication:
      return wire_symbols::kPublication;
    case OffWorkflowStage::kComplete:
      return wire_symbols::kComplete;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffWorkflowStage value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffWorkflowExitMeaning value) {
  switch (value) {
    case OffWorkflowExitMeaning::kSuccess:
      return wire_symbols::kSuccess;
    case OffWorkflowExitMeaning::kRejectedOrIncomplete:
      return wire_symbols::kRejectedOrIncomplete;
    case OffWorkflowExitMeaning::kExecutionFailure:
      return wire_symbols::kExecutionFailure;
    case OffWorkflowExitMeaning::kCanceled:
      return wire_symbols::kCanceled;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffWorkflowExitMeaning value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(OffWorkflowDiagnosticCode value) {
  switch (value) {
    case OffWorkflowDiagnosticCode::kNone:
      return wire_symbols::kNone;
    case OffWorkflowDiagnosticCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case OffWorkflowDiagnosticCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case OffWorkflowDiagnosticCode::kInputIdentity:
      return wire_symbols::kInputIdentity;
    case OffWorkflowDiagnosticCode::kPolicy:
      return wire_symbols::kPolicy;
    case OffWorkflowDiagnosticCode::kNameUnsupported:
      return wire_symbols::kNameUnsupported;
    case OffWorkflowDiagnosticCode::kNameCollision:
      return wire_symbols::kNameCollision;
    case OffWorkflowDiagnosticCode::kCountLimit:
      return wire_symbols::kCountLimit;
    case OffWorkflowDiagnosticCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case OffWorkflowDiagnosticCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case OffWorkflowDiagnosticCode::kRepair:
      return wire_symbols::kRepair;
    case OffWorkflowDiagnosticCode::kWrite:
      return wire_symbols::kWrite;
    case OffWorkflowDiagnosticCode::kReload:
      return wire_symbols::kReload;
    case OffWorkflowDiagnosticCode::kCorrespondence:
      return wire_symbols::kCorrespondence;
    case OffWorkflowDiagnosticCode::kCanonicalPreservation:
      return wire_symbols::kCanonicalPreservation;
    case OffWorkflowDiagnosticCode::kReloadMismatch:
      return wire_symbols::kReloadMismatch;
    case OffWorkflowDiagnosticCode::kSourceChanged:
      return wire_symbols::kSourceChanged;
    case OffWorkflowDiagnosticCode::kHashExtent:
      return wire_symbols::kHashExtent;
    case OffWorkflowDiagnosticCode::kReportExtent:
      return wire_symbols::kReportExtent;
    case OffWorkflowDiagnosticCode::kReportStringEncoding:
      return wire_symbols::kReportStringEncoding;
    case OffWorkflowDiagnosticCode::kPublication:
      return wire_symbols::kPublication;
    case OffWorkflowDiagnosticCode::kBlocked:
      return wire_symbols::kBlocked;
    case OffWorkflowDiagnosticCode::kSolidUnavailable:
      return wire_symbols::kSolidUnavailable;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(OffWorkflowDiagnosticCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(MaterializedDuplicateStatus value) {
  switch (value) {
    case MaterializedDuplicateStatus::kSuccess:
      return wire_symbols::kSuccess;
    case MaterializedDuplicateStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case MaterializedDuplicateStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case MaterializedDuplicateStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case MaterializedDuplicateStatus::kCanceled:
      return wire_symbols::kCanceled;
    case MaterializedDuplicateStatus::kInvalidSourceStorage:
      return wire_symbols::kInvalidSourceStorage;
    case MaterializedDuplicateStatus::kRejected:
      return wire_symbols::kRejected;
    case MaterializedDuplicateStatus::kGeometryFailure:
      return wire_symbols::kGeometryFailure;
    case MaterializedDuplicateStatus::kVerificationFailure:
      return wire_symbols::kVerificationFailure;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(MaterializedDuplicateStatus value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(MaterializedDuplicateStage value) {
  switch (value) {
    case MaterializedDuplicateStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case MaterializedDuplicateStage::kSourceInspection:
      return wire_symbols::kSourceInspection;
    case MaterializedDuplicateStage::kEligibility:
      return wire_symbols::kEligibility;
    case MaterializedDuplicateStage::kTargets:
      return wire_symbols::kTargets;
    case MaterializedDuplicateStage::kComparison:
      return wire_symbols::kComparison;
    case MaterializedDuplicateStage::kSelection:
      return wire_symbols::kSelection;
    case MaterializedDuplicateStage::kCorrespondence:
      return wire_symbols::kCorrespondence;
    case MaterializedDuplicateStage::kCandidateInspection:
      return wire_symbols::kCandidateInspection;
    case MaterializedDuplicateStage::kVerification:
      return wire_symbols::kVerification;
    case MaterializedDuplicateStage::kPublication:
      return wire_symbols::kPublication;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(MaterializedDuplicateStage value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(MaterializedDuplicateDiagnosticCode value) {
  switch (value) {
    case MaterializedDuplicateDiagnosticCode::kNone:
      return wire_symbols::kNone;
    case MaterializedDuplicateDiagnosticCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case MaterializedDuplicateDiagnosticCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case MaterializedDuplicateDiagnosticCode::kDeclarationDomain:
      return wire_symbols::kDeclarationDomain;
    case MaterializedDuplicateDiagnosticCode::kDeclarationNameEmpty:
      return wire_symbols::kDeclarationNameEmpty;
    case MaterializedDuplicateDiagnosticCode::kDeclarationDuplicate:
      return wire_symbols::kDeclarationDuplicate;
    case MaterializedDuplicateDiagnosticCode::kTargetLimit:
      return wire_symbols::kTargetLimit;
    case MaterializedDuplicateDiagnosticCode::kDeclarationLimit:
      return wire_symbols::kDeclarationLimit;
    case MaterializedDuplicateDiagnosticCode::kDeclarationNameByteLimit:
      return wire_symbols::kDeclarationNameByteLimit;
    case MaterializedDuplicateDiagnosticCode::kComparisonLimit:
      return wire_symbols::kComparisonLimit;
    case MaterializedDuplicateDiagnosticCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case MaterializedDuplicateDiagnosticCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case MaterializedDuplicateDiagnosticCode::kStorageIssues:
      return wire_symbols::kStorageIssues;
    case MaterializedDuplicateDiagnosticCode::kNonfiniteAttribute:
      return wire_symbols::kNonfiniteAttribute;
    case MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute:
      return wire_symbols::kUnsupportedAttribute;
    case MaterializedDuplicateDiagnosticCode::kTargetRange:
      return wire_symbols::kTargetRange;
    case MaterializedDuplicateDiagnosticCode::kSelfTarget:
      return wire_symbols::kSelfTarget;
    case MaterializedDuplicateDiagnosticCode::kRepeatedTarget:
      return wire_symbols::kRepeatedTarget;
    case MaterializedDuplicateDiagnosticCode::kRemovedRepresentative:
      return wire_symbols::kRemovedRepresentative;
    case MaterializedDuplicateDiagnosticCode::kNotEquivalent:
      return wire_symbols::kNotEquivalent;
    case MaterializedDuplicateDiagnosticCode::kGeometry:
      return wire_symbols::kGeometry;
    case MaterializedDuplicateDiagnosticCode::kPostcondition:
      return wire_symbols::kPostcondition;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(MaterializedDuplicateDiagnosticCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::ChargedTriangulationStatus value) {
  switch (value) {
    case geo::ChargedTriangulationStatus::kAccepted:
      return wire_symbols::kAccepted;
    case geo::ChargedTriangulationStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case geo::ChargedTriangulationStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case geo::ChargedTriangulationStatus::kBlocked:
      return wire_symbols::kBlocked;
    case geo::ChargedTriangulationStatus::kUnsupportedAllocationProfile:
      return wire_symbols::kUnsupportedAllocationProfile;
    case geo::ChargedTriangulationStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case geo::ChargedTriangulationStatus::kCanceled:
      return wire_symbols::kCanceled;
    case geo::ChargedTriangulationStatus::kDelegateFailure:
      return wire_symbols::kDelegateFailure;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::ChargedTriangulationStatus value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::ChargedTriangulationStage value) {
  switch (value) {
    case geo::ChargedTriangulationStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case geo::ChargedTriangulationStage::kSourceCounts:
      return wire_symbols::kSourceCounts;
    case geo::ChargedTriangulationStage::kSourceCapture:
      return wire_symbols::kSourceCapture;
    case geo::ChargedTriangulationStage::kSourceInspection:
      return wire_symbols::kSourceInspection;
    case geo::ChargedTriangulationStage::kTriangulation:
      return wire_symbols::kTriangulation;
    case geo::ChargedTriangulationStage::kCorrespondence:
      return wire_symbols::kCorrespondence;
    case geo::ChargedTriangulationStage::kAttributeTransfer:
      return wire_symbols::kAttributeTransfer;
    case geo::ChargedTriangulationStage::kOutputMaterialization:
      return wire_symbols::kOutputMaterialization;
    case geo::ChargedTriangulationStage::kOutputInspection:
      return wire_symbols::kOutputInspection;
    case geo::ChargedTriangulationStage::kComplete:
      return wire_symbols::kComplete;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::ChargedTriangulationStage value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::ChargedTriangulationCode value) {
  switch (value) {
    case geo::ChargedTriangulationCode::kNone:
      return wire_symbols::kNone;
    case geo::ChargedTriangulationCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case geo::ChargedTriangulationCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case geo::ChargedTriangulationCode::kUnsupportedCornerLimit:
      return wire_symbols::kUnsupportedCornerLimit;
    case geo::ChargedTriangulationCode::kSourceCountLimit:
      return wire_symbols::kSourceCountLimit;
    case geo::ChargedTriangulationCode::kOutputCountLimit:
      return wire_symbols::kOutputCountLimit;
    case geo::ChargedTriangulationCode::kMapEntryLimit:
      return wire_symbols::kMapEntryLimit;
    case geo::ChargedTriangulationCode::kCoreRequestBudget:
      return wire_symbols::kCoreRequestBudget;
    case geo::ChargedTriangulationCode::kBridgeRequestBudget:
      return wire_symbols::kBridgeRequestBudget;
    case geo::ChargedTriangulationCode::kSourceStorage:
      return wire_symbols::kSourceStorage;
    case geo::ChargedTriangulationCode::kFaceCornerLimit:
      return wire_symbols::kFaceCornerLimit;
    case geo::ChargedTriangulationCode::kRepeatedPosition:
      return wire_symbols::kRepeatedPosition;
    case geo::ChargedTriangulationCode::kDegenerateFace:
      return wire_symbols::kDegenerateFace;
    case geo::ChargedTriangulationCode::kNonplanarFace:
      return wire_symbols::kNonplanarFace;
    case geo::ChargedTriangulationCode::kBacktrackingBoundary:
      return wire_symbols::kBacktrackingBoundary;
    case geo::ChargedTriangulationCode::kSelfIntersection:
      return wire_symbols::kSelfIntersection;
    case geo::ChargedTriangulationCode::kNoGeometricEar:
      return wire_symbols::kNoGeometricEar;
    case geo::ChargedTriangulationCode::kDegenerateResult:
      return wire_symbols::kDegenerateResult;
    case geo::ChargedTriangulationCode::kSizeOverflow:
      return wire_symbols::kSizeOverflow;
    case geo::ChargedTriangulationCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case geo::ChargedTriangulationCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case geo::ChargedTriangulationCode::kMaterialization:
      return wire_symbols::kMaterialization;
    case geo::ChargedTriangulationCode::kInspection:
      return wire_symbols::kInspection;
    case geo::ChargedTriangulationCode::kOutputStorage:
      return wire_symbols::kOutputStorage;
    case geo::ChargedTriangulationCode::kAllocationProfile:
      return wire_symbols::kAllocationProfile;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::ChargedTriangulationCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::TriangulationStatus value) {
  switch (value) {
    case geo::TriangulationStatus::kAccepted:
      return wire_symbols::kAccepted;
    case geo::TriangulationStatus::kBlocked:
      return wire_symbols::kBlocked;
    case geo::TriangulationStatus::kCanceled:
      return wire_symbols::kCanceled;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::TriangulationStatus value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(geo::ChargedTriangulationSerialReason value) {
  switch (value) {
    case geo::ChargedTriangulationSerialReason::kNone:
      return wire_symbols::kNone;
    case geo::ChargedTriangulationSerialReason::kEmptyMesh:
      return wire_symbols::kEmptyMesh;
    case geo::ChargedTriangulationSerialReason::kWorkerBudgetOne:
      return wire_symbols::kWorkerBudgetOne;
    case geo::ChargedTriangulationSerialReason::kBelowFaceThreshold:
      return wire_symbols::kBelowFaceThreshold;
    case geo::ChargedTriangulationSerialReason::
        kSharedWorkersUnavailableOrFewerThanTwoFaces:
      return wire_symbols::kSharedWorkersUnavailableOrFewerThanTwoFaces;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(geo::ChargedTriangulationSerialReason value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceStatus value) {
  switch (value) {
    case ic::SurfaceStatus::kSuccess:
      return wire_symbols::kSuccess;
    case ic::SurfaceStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case ic::SurfaceStatus::kInvalidStorage:
      return wire_symbols::kInvalidStorage;
    case ic::SurfaceStatus::kUnsupported:
      return wire_symbols::kUnsupported;
    case ic::SurfaceStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case ic::SurfaceStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case ic::SurfaceStatus::kCanceled:
      return wire_symbols::kCanceled;
    case ic::SurfaceStatus::kDelegateFailure:
      return wire_symbols::kDelegateFailure;
    case ic::SurfaceStatus::kVerificationFailed:
      return wire_symbols::kVerificationFailed;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceStatus value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceStage value) {
  switch (value) {
    case ic::SurfaceStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case ic::SurfaceStage::kSource:
      return wire_symbols::kSource;
    case ic::SurfaceStage::kCorrespondence:
      return wire_symbols::kCorrespondence;
    case ic::SurfaceStage::kDisposition:
      return wire_symbols::kDisposition;
    case ic::SurfaceStage::kCatalog:
      return wire_symbols::kCatalog;
    case ic::SurfaceStage::kPlan:
      return wire_symbols::kPlan;
    case ic::SurfaceStage::kNumeric:
      return wire_symbols::kNumeric;
    case ic::SurfaceStage::kEncode:
      return wire_symbols::kEncode;
    case ic::SurfaceStage::kCapture:
      return wire_symbols::kCapture;
    case ic::SurfaceStage::kContainerRead:
      return wire_symbols::kContainerRead;
    case ic::SurfaceStage::kAssetRead:
      return wire_symbols::kAssetRead;
    case ic::SurfaceStage::kPrimitiveRead:
      return wire_symbols::kPrimitiveRead;
    case ic::SurfaceStage::kCapsuleRead:
      return wire_symbols::kCapsuleRead;
    case ic::SurfaceStage::kCompare:
      return wire_symbols::kCompare;
    case ic::SurfaceStage::kComplete:
      return wire_symbols::kComplete;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceStage value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceCode value) {
  switch (value) {
    case ic::SurfaceCode::kNone:
      return wire_symbols::kNone;
    case ic::SurfaceCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case ic::SurfaceCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case ic::SurfaceCode::kUnknownEnum:
      return wire_symbols::kUnknownEnum;
    case ic::SurfaceCode::kEncodingLimitsMismatch:
      return wire_symbols::kEncodingLimitsMismatch;
    case ic::SurfaceCode::kInvalidBound:
      return wire_symbols::kInvalidBound;
    case ic::SurfaceCode::kAllowanceTooSmall:
      return wire_symbols::kAllowanceTooSmall;
    case ic::SurfaceCode::kForeignLedger:
      return wire_symbols::kForeignLedger;
    case ic::SurfaceCode::kInvalidSourceStorage:
      return wire_symbols::kInvalidSourceStorage;
    case ic::SurfaceCode::kSourceCount:
      return wire_symbols::kSourceCount;
    case ic::SurfaceCode::kTriangleCount:
      return wire_symbols::kTriangleCount;
    case ic::SurfaceCode::kSourceCorrespondence:
      return wire_symbols::kSourceCorrespondence;
    case ic::SurfaceCode::kChannelCoverage:
      return wire_symbols::kChannelCoverage;
    case ic::SurfaceCode::kChannelRole:
      return wire_symbols::kChannelRole;
    case ic::SurfaceCode::kChannelShape:
      return wire_symbols::kChannelShape;
    case ic::SurfaceCode::kChannelMetadata:
      return wire_symbols::kChannelMetadata;
    case ic::SurfaceCode::kBareUnrepresented:
      return wire_symbols::kBareUnrepresented;
    case ic::SurfaceCode::kLossNotAcknowledged:
      return wire_symbols::kLossNotAcknowledged;
    case ic::SurfaceCode::kUvMap:
      return wire_symbols::kUvMap;
    case ic::SurfaceCode::kUvReference:
      return wire_symbols::kUvReference;
    case ic::SurfaceCode::kNormalAbsent:
      return wire_symbols::kNormalAbsent;
    case ic::SurfaceCode::kNormalMissing:
      return wire_symbols::kNormalMissing;
    case ic::SurfaceCode::kNormalNotConstant:
      return wire_symbols::kNormalNotConstant;
    case ic::SurfaceCode::kNonfinite:
      return wire_symbols::kNonfinite;
    case ic::SurfaceCode::kMaterialIds:
      return wire_symbols::kMaterialIds;
    case ic::SurfaceCode::kMaterialBinding:
      return wire_symbols::kMaterialBinding;
    case ic::SurfaceCode::kCatalogField:
      return wire_symbols::kCatalogField;
    case ic::SurfaceCode::kCatalogExtension:
      return wire_symbols::kCatalogExtension;
    case ic::SurfaceCode::kResourceBinding:
      return wire_symbols::kResourceBinding;
    case ic::SurfaceCode::kResourceMedia:
      return wire_symbols::kResourceMedia;
    case ic::SurfaceCode::kCountLimit:
      return wire_symbols::kCountLimit;
    case ic::SurfaceCode::kDescriptorLimit:
      return wire_symbols::kDescriptorLimit;
    case ic::SurfaceCode::kOutputLimit:
      return wire_symbols::kOutputLimit;
    case ic::SurfaceCode::kMapLimit:
      return wire_symbols::kMapLimit;
    case ic::SurfaceCode::kSizeOverflow:
      return wire_symbols::kSizeOverflow;
    case ic::SurfaceCode::kNumericRange:
      return wire_symbols::kNumericRange;
    case ic::SurfaceCode::kNonzeroUnderflow:
      return wire_symbols::kNonzeroUnderflow;
    case ic::SurfaceCode::kExactness:
      return wire_symbols::kExactness;
    case ic::SurfaceCode::kErrorBound:
      return wire_symbols::kErrorBound;
    case ic::SurfaceCode::kNewCoincidence:
      return wire_symbols::kNewCoincidence;
    case ic::SurfaceCode::kCollapsedEdge:
      return wire_symbols::kCollapsedEdge;
    case ic::SurfaceCode::kCollapsedTriangle:
      return wire_symbols::kCollapsedTriangle;
    case ic::SurfaceCode::kOrientation:
      return wire_symbols::kOrientation;
    case ic::SurfaceCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case ic::SurfaceCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case ic::SurfaceCode::kGeometryDelegate:
      return wire_symbols::kGeometryDelegate;
    case ic::SurfaceCode::kStlDelegate:
      return wire_symbols::kStlDelegate;
    case ic::SurfaceCode::kGlbDelegate:
      return wire_symbols::kGlbDelegate;
    case ic::SurfaceCode::kGltfDelegate:
      return wire_symbols::kGltfDelegate;
    case ic::SurfaceCode::kMemberInventory:
      return wire_symbols::kMemberInventory;
    case ic::SurfaceCode::kMemberPath:
      return wire_symbols::kMemberPath;
    case ic::SurfaceCode::kMemberDigest:
      return wire_symbols::kMemberDigest;
    case ic::SurfaceCode::kJsonSyntax:
      return wire_symbols::kJsonSyntax;
    case ic::SurfaceCode::kSchema:
      return wire_symbols::kSchema;
    case ic::SurfaceCode::kBinarySlice:
      return wire_symbols::kBinarySlice;
    case ic::SurfaceCode::kCapsuleSource:
      return wire_symbols::kCapsuleSource;
    case ic::SurfaceCode::kWireShape:
      return wire_symbols::kWireShape;
    case ic::SurfaceCode::kWireValue:
      return wire_symbols::kWireValue;
    case ic::SurfaceCode::kWireMap:
      return wire_symbols::kWireMap;
    case ic::SurfaceCode::kMaterialReference:
      return wire_symbols::kMaterialReference;
    case ic::SurfaceCode::kResourceBytes:
      return wire_symbols::kResourceBytes;
    case ic::SurfaceCode::kUnits:
      return wire_symbols::kUnits;
    case ic::SurfaceCode::kNumericProfile:
      return wire_symbols::kNumericProfile;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceCode value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceSubject value) {
  switch (value) {
    case ic::SurfaceSubject::kNone:
      return wire_symbols::kNone;
    case ic::SurfaceSubject::kVertex:
      return wire_symbols::kVertex;
    case ic::SurfaceSubject::kFace:
      return wire_symbols::kFace;
    case ic::SurfaceSubject::kCorner:
      return wire_symbols::kCorner;
    case ic::SurfaceSubject::kAttribute:
      return wire_symbols::kAttribute;
    case ic::SurfaceSubject::kScalar:
      return wire_symbols::kScalar;
    case ic::SurfaceSubject::kMember:
      return wire_symbols::kMember;
    case ic::SurfaceSubject::kMaterial:
      return wire_symbols::kMaterial;
    case ic::SurfaceSubject::kTexture:
      return wire_symbols::kTexture;
    case ic::SurfaceSubject::kImage:
      return wire_symbols::kImage;
    case ic::SurfaceSubject::kSampler:
      return wire_symbols::kSampler;
    case ic::SurfaceSubject::kPrimitive:
      return wire_symbols::kPrimitive;
    case ic::SurfaceSubject::kUvSet:
      return wire_symbols::kUvSet;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceSubject value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceFilePublicationOutcome value) {
  switch (value) {
    case ic::SurfaceFilePublicationOutcome::kFailed:
      return wire_symbols::kFailed;
    case ic::SurfaceFilePublicationOutcome::kCanceled:
      return wire_symbols::kCanceled;
    case ic::SurfaceFilePublicationOutcome::kPublished:
      return wire_symbols::kPublished;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceFilePublicationOutcome value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceFileStatus value) {
  switch (value) {
    case ic::SurfaceFileStatus::kSuccess:
      return wire_symbols::kSuccess;
    case ic::SurfaceFileStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case ic::SurfaceFileStatus::kInvalidStorage:
      return wire_symbols::kInvalidStorage;
    case ic::SurfaceFileStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case ic::SurfaceFileStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case ic::SurfaceFileStatus::kCanceled:
      return wire_symbols::kCanceled;
    case ic::SurfaceFileStatus::kUnsupportedProfile:
      return wire_symbols::kUnsupportedProfile;
    case ic::SurfaceFileStatus::kIoFailure:
      return wire_symbols::kIoFailure;
    case ic::SurfaceFileStatus::kVerificationFailed:
      return wire_symbols::kVerificationFailed;
    case ic::SurfaceFileStatus::kDestinationExists:
      return wire_symbols::kDestinationExists;
    case ic::SurfaceFileStatus::kPublicationFailed:
      return wire_symbols::kPublicationFailed;
    case ic::SurfaceFileStatus::kCallbackFailure:
      return wire_symbols::kCallbackFailure;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceFileStatus value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceFileStage value) {
  switch (value) {
    case ic::SurfaceFileStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case ic::SurfaceFileStage::kPathPlanning:
      return wire_symbols::kPathPlanning;
    case ic::SurfaceFileStage::kStaging:
      return wire_symbols::kStaging;
    case ic::SurfaceFileStage::kReread:
      return wire_symbols::kReread;
    case ic::SurfaceFileStage::kInspection:
      return wire_symbols::kInspection;
    case ic::SurfaceFileStage::kReceipt:
      return wire_symbols::kReceipt;
    case ic::SurfaceFileStage::kInventory:
      return wire_symbols::kInventory;
    case ic::SurfaceFileStage::kPublication:
      return wire_symbols::kPublication;
    case ic::SurfaceFileStage::kComplete:
      return wire_symbols::kComplete;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceFileStage value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceFileCode value) {
  switch (value) {
    case ic::SurfaceFileCode::kNone:
      return wire_symbols::kNone;
    case ic::SurfaceFileCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case ic::SurfaceFileCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case ic::SurfaceFileCode::kUnknownEnum:
      return wire_symbols::kUnknownEnum;
    case ic::SurfaceFileCode::kEncodingLimitsMismatch:
      return wire_symbols::kEncodingLimitsMismatch;
    case ic::SurfaceFileCode::kAllowanceTooSmall:
      return wire_symbols::kAllowanceTooSmall;
    case ic::SurfaceFileCode::kProviderConfiguration:
      return wire_symbols::kProviderConfiguration;
    case ic::SurfaceFileCode::kDestinationPath:
      return wire_symbols::kDestinationPath;
    case ic::SurfaceFileCode::kReceiptPath:
      return wire_symbols::kReceiptPath;
    case ic::SurfaceFileCode::kPathCollision:
      return wire_symbols::kPathCollision;
    case ic::SurfaceFileCode::kSourceContract:
      return wire_symbols::kSourceContract;
    case ic::SurfaceFileCode::kCountLimit:
      return wire_symbols::kCountLimit;
    case ic::SurfaceFileCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case ic::SurfaceFileCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case ic::SurfaceFileCode::kUnsupportedProfile:
      return wire_symbols::kUnsupportedProfile;
    case ic::SurfaceFileCode::kDestinationParent:
      return wire_symbols::kDestinationParent;
    case ic::SurfaceFileCode::kStageCreate:
      return wire_symbols::kStageCreate;
    case ic::SurfaceFileCode::kStageAttemptsExhausted:
      return wire_symbols::kStageAttemptsExhausted;
    case ic::SurfaceFileCode::kFileOpen:
      return wire_symbols::kFileOpen;
    case ic::SurfaceFileCode::kFileRead:
      return wire_symbols::kFileRead;
    case ic::SurfaceFileCode::kFileWrite:
      return wire_symbols::kFileWrite;
    case ic::SurfaceFileCode::kFileClose:
      return wire_symbols::kFileClose;
    case ic::SurfaceFileCode::kEnumeration:
      return wire_symbols::kEnumeration;
    case ic::SurfaceFileCode::kMemberInventory:
      return wire_symbols::kMemberInventory;
    case ic::SurfaceFileCode::kFileChanged:
      return wire_symbols::kFileChanged;
    case ic::SurfaceFileCode::kFileKind:
      return wire_symbols::kFileKind;
    case ic::SurfaceFileCode::kInspection:
      return wire_symbols::kInspection;
    case ic::SurfaceFileCode::kCallerReportEncoding:
      return wire_symbols::kCallerReportEncoding;
    case ic::SurfaceFileCode::kReceiptSize:
      return wire_symbols::kReceiptSize;
    case ic::SurfaceFileCode::kReceiptBytes:
      return wire_symbols::kReceiptBytes;
    case ic::SurfaceFileCode::kProvider:
      return wire_symbols::kProvider;
    case ic::SurfaceFileCode::kDestinationExists:
      return wire_symbols::kDestinationExists;
    case ic::SurfaceFileCode::kCommitFailed:
      return wire_symbols::kCommitFailed;
    case ic::SurfaceFileCode::kCommitUnsupported:
      return wire_symbols::kCommitUnsupported;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceFileCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceFileCleanupOutcome value) {
  switch (value) {
    case ic::SurfaceFileCleanupOutcome::kNotRequired:
      return wire_symbols::kNotRequired;
    case ic::SurfaceFileCleanupOutcome::kRemoved:
      return wire_symbols::kRemoved;
    case ic::SurfaceFileCleanupOutcome::kFailed:
      return wire_symbols::kFailed;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceFileCleanupOutcome value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceFilePhaseOutcome value) {
  switch (value) {
    case ic::SurfaceFilePhaseOutcome::kCompleted:
      return wire_symbols::kCompleted;
    case ic::SurfaceFilePhaseOutcome::kFailed:
      return wire_symbols::kFailed;
    case ic::SurfaceFilePhaseOutcome::kCanceled:
      return wire_symbols::kCanceled;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceFilePhaseOutcome value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceFileCommitOutcome value) {
  switch (value) {
    case ic::SurfaceFileCommitOutcome::kCommitted:
      return wire_symbols::kCommitted;
    case ic::SurfaceFileCommitOutcome::kDestinationExists:
      return wire_symbols::kDestinationExists;
    case ic::SurfaceFileCommitOutcome::kFailed:
      return wire_symbols::kFailed;
    case ic::SurfaceFileCommitOutcome::kUnsupported:
      return wire_symbols::kUnsupported;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceFileCommitOutcome value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceDelegateKind value) {
  switch (value) {
    case ic::SurfaceDelegateKind::kDerivedMaterialization:
      return wire_symbols::kDerivedMaterialization;
    case ic::SurfaceDelegateKind::kStlWrite:
      return wire_symbols::kStlWrite;
    case ic::SurfaceDelegateKind::kGlbWrite:
      return wire_symbols::kGlbWrite;
    case ic::SurfaceDelegateKind::kGlbRead:
      return wire_symbols::kGlbRead;
    case ic::SurfaceDelegateKind::kGltfRead:
      return wire_symbols::kGltfRead;
    case ic::SurfaceDelegateKind::kGltfExtract:
      return wire_symbols::kGltfExtract;
    case ic::SurfaceDelegateKind::kStlRead:
      return wire_symbols::kStlRead;
    case ic::SurfaceDelegateKind::kSourceMaterialFactors:
      return wire_symbols::kSourceMaterialFactors;
    case ic::SurfaceDelegateKind::kWireMaterialFactors:
      return wire_symbols::kWireMaterialFactors;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceDelegateKind value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::GlbStatus value) {
  switch (value) {
    case ic::GlbStatus::kSuccess:
      return wire_symbols::kSuccess;
    case ic::GlbStatus::kInvalidOptions:
      return wire_symbols::kInvalidOptions;
    case ic::GlbStatus::kMalformed:
      return wire_symbols::kMalformed;
    case ic::GlbStatus::kUnsupportedVersion:
      return wire_symbols::kUnsupportedVersion;
    case ic::GlbStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case ic::GlbStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case ic::GlbStatus::kCanceled:
      return wire_symbols::kCanceled;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::GlbStatus value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::GltfStatus value) {
  switch (value) {
    case ic::GltfStatus::kAccepted:
      return wire_symbols::kAccepted;
    case ic::GltfStatus::kInvalidLimits:
      return wire_symbols::kInvalidLimits;
    case ic::GltfStatus::kInvalidRequest:
      return wire_symbols::kInvalidRequest;
    case ic::GltfStatus::kMalformed:
      return wire_symbols::kMalformed;
    case ic::GltfStatus::kUnsupportedProfile:
      return wire_symbols::kUnsupportedProfile;
    case ic::GltfStatus::kResourceBindingMismatch:
      return wire_symbols::kResourceBindingMismatch;
    case ic::GltfStatus::kCanceled:
      return wire_symbols::kCanceled;
    case ic::GltfStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case ic::GltfStatus::kInvalidSelection:
      return wire_symbols::kInvalidSelection;
    case ic::GltfStatus::kForeignOwner:
      return wire_symbols::kForeignOwner;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::GltfStatus value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::GltfDiagnosticCode value) {
  switch (value) {
    case ic::GltfDiagnosticCode::kNone:
      return wire_symbols::kNone;
    case ic::GltfDiagnosticCode::kZeroLimit:
      return wire_symbols::kZeroLimit;
    case ic::GltfDiagnosticCode::kLimitNotRepresentable:
      return wire_symbols::kLimitNotRepresentable;
    case ic::GltfDiagnosticCode::kInvalidRequest:
      return wire_symbols::kInvalidRequest;
    case ic::GltfDiagnosticCode::kInvalidBindingKind:
      return wire_symbols::kInvalidBindingKind;
    case ic::GltfDiagnosticCode::kUnsupportedResourceRepresentation:
      return wire_symbols::kUnsupportedResourceRepresentation;
    case ic::GltfDiagnosticCode::kEmbeddedUriSyntax:
      return wire_symbols::kEmbeddedUriSyntax;
    case ic::GltfDiagnosticCode::kEmbeddedMediaMismatch:
      return wire_symbols::kEmbeddedMediaMismatch;
    case ic::GltfDiagnosticCode::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case ic::GltfDiagnosticCode::kSizeOverflow:
      return wire_symbols::kSizeOverflow;
    case ic::GltfDiagnosticCode::kJsonSyntax:
      return wire_symbols::kJsonSyntax;
    case ic::GltfDiagnosticCode::kUnicodeScalar:
      return wire_symbols::kUnicodeScalar;
    case ic::GltfDiagnosticCode::kDecodedDuplicateKey:
      return wire_symbols::kDecodedDuplicateKey;
    case ic::GltfDiagnosticCode::kCoreFieldType:
      return wire_symbols::kCoreFieldType;
    case ic::GltfDiagnosticCode::kCoreFieldRequired:
      return wire_symbols::kCoreFieldRequired;
    case ic::GltfDiagnosticCode::kExactInteger:
      return wire_symbols::kExactInteger;
    case ic::GltfDiagnosticCode::kCoreRange:
      return wire_symbols::kCoreRange;
    case ic::GltfDiagnosticCode::kCoreArrayLength:
      return wire_symbols::kCoreArrayLength;
    case ic::GltfDiagnosticCode::kCoreReference:
      return wire_symbols::kCoreReference;
    case ic::GltfDiagnosticCode::kGraphRelationship:
      return wire_symbols::kGraphRelationship;
    case ic::GltfDiagnosticCode::kBinaryLayout:
      return wire_symbols::kBinaryLayout;
    case ic::GltfDiagnosticCode::kBinaryValue:
      return wire_symbols::kBinaryValue;
    case ic::GltfDiagnosticCode::kIndexRole:
      return wire_symbols::kIndexRole;
    case ic::GltfDiagnosticCode::kUnsupportedEnum:
      return wire_symbols::kUnsupportedEnum;
    case ic::GltfDiagnosticCode::kUnsupportedNumberRepresentation:
      return wire_symbols::kUnsupportedNumberRepresentation;
    case ic::GltfDiagnosticCode::kUnsupportedCoreString:
      return wire_symbols::kUnsupportedCoreString;
    case ic::GltfDiagnosticCode::kRequiredExtension:
      return wire_symbols::kRequiredExtension;
    case ic::GltfDiagnosticCode::kMissingResource:
      return wire_symbols::kMissingResource;
    case ic::GltfDiagnosticCode::kDuplicateResource:
      return wire_symbols::kDuplicateResource;
    case ic::GltfDiagnosticCode::kExtraResource:
      return wire_symbols::kExtraResource;
    case ic::GltfDiagnosticCode::kResourceExtent:
      return wire_symbols::kResourceExtent;
    case ic::GltfDiagnosticCode::kCanceled:
      return wire_symbols::kCanceled;
    case ic::GltfDiagnosticCode::kPayloadBudget:
      return wire_symbols::kPayloadBudget;
    case ic::GltfDiagnosticCode::kInvalidSelection:
      return wire_symbols::kInvalidSelection;
    case ic::GltfDiagnosticCode::kForeignOwner:
      return wire_symbols::kForeignOwner;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::GltfDiagnosticCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::GltfResourceKind value) {
  switch (value) {
    case ic::GltfResourceKind::kInvalid:
      return wire_symbols::kInvalid;
    case ic::GltfResourceKind::kBuffer:
      return wire_symbols::kBuffer;
    case ic::GltfResourceKind::kImage:
      return wire_symbols::kImage;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::GltfResourceKind value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::StlStatus value) {
  switch (value) {
    case ic::StlStatus::kSuccess:
      return wire_symbols::kSuccess;
    case ic::StlStatus::kInvalidConfiguration:
      return wire_symbols::kInvalidConfiguration;
    case ic::StlStatus::kMalformed:
      return wire_symbols::kMalformed;
    case ic::StlStatus::kUnsupportedVariant:
      return wire_symbols::kUnsupportedVariant;
    case ic::StlStatus::kUnsupportedNumericConversion:
      return wire_symbols::kUnsupportedNumericConversion;
    case ic::StlStatus::kLimitExceeded:
      return wire_symbols::kLimitExceeded;
    case ic::StlStatus::kBudgetExceeded:
      return wire_symbols::kBudgetExceeded;
    case ic::StlStatus::kCanceled:
      return wire_symbols::kCanceled;
    case ic::StlStatus::kInvalidStorage:
      return wire_symbols::kInvalidStorage;
    case ic::StlStatus::kGeometryFailure:
      return wire_symbols::kGeometryFailure;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::StlStatus value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::StlStage value) {
  switch (value) {
    case ic::StlStage::kConfiguration:
      return wire_symbols::kConfiguration;
    case ic::StlStage::kScan:
      return wire_symbols::kScan;
    case ic::StlStage::kDecode:
      return wire_symbols::kDecode;
    case ic::StlStage::kMaterialization:
      return wire_symbols::kMaterialization;
    case ic::StlStage::kInspection:
      return wire_symbols::kInspection;
    case ic::StlStage::kValidation:
      return wire_symbols::kValidation;
    case ic::StlStage::kEncode:
      return wire_symbols::kEncode;
    case ic::StlStage::kPublication:
      return wire_symbols::kPublication;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::StlStage value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::StlDiagnosticCode value) {
  switch (value) {
    case ic::StlDiagnosticCode::kNone:
      return wire_symbols::kNone;
    case ic::StlDiagnosticCode::kRequiredFieldZero:
      return wire_symbols::kRequiredFieldZero;
    case ic::StlDiagnosticCode::kConfigurationExtent:
      return wire_symbols::kConfigurationExtent;
    case ic::StlDiagnosticCode::kEncoding:
      return wire_symbols::kEncoding;
    case ic::StlDiagnosticCode::kSourceLimit:
      return wire_symbols::kSourceLimit;
    case ic::StlDiagnosticCode::kFacetLimit:
      return wire_symbols::kFacetLimit;
    case ic::StlDiagnosticCode::kNameLimit:
      return wire_symbols::kNameLimit;
    case ic::StlDiagnosticCode::kLineLimit:
      return wire_symbols::kLineLimit;
    case ic::StlDiagnosticCode::kTokenLimit:
      return wire_symbols::kTokenLimit;
    case ic::StlDiagnosticCode::kOutputLimit:
      return wire_symbols::kOutputLimit;
    case ic::StlDiagnosticCode::kSyntax:
      return wire_symbols::kSyntax;
    case ic::StlDiagnosticCode::kNumericSyntax:
      return wire_symbols::kNumericSyntax;
    case ic::StlDiagnosticCode::kNumericRange:
      return wire_symbols::kNumericRange;
    case ic::StlDiagnosticCode::kNonfinite:
      return wire_symbols::kNonfinite;
    case ic::StlDiagnosticCode::kBinaryExtent:
      return wire_symbols::kBinaryExtent;
    case ic::StlDiagnosticCode::kTrailingBytes:
      return wire_symbols::kTrailingBytes;
    case ic::StlDiagnosticCode::kAttributeWord:
      return wire_symbols::kAttributeWord;
    case ic::StlDiagnosticCode::kRequestBudget:
      return wire_symbols::kRequestBudget;
    case ic::StlDiagnosticCode::kStopRequested:
      return wire_symbols::kStopRequested;
    case ic::StlDiagnosticCode::kGeometry:
      return wire_symbols::kGeometry;
    case ic::StlDiagnosticCode::kStorageIssues:
      return wire_symbols::kStorageIssues;
    case ic::StlDiagnosticCode::kNotTriangles:
      return wire_symbols::kNotTriangles;
    case ic::StlDiagnosticCode::kNormalSelection:
      return wire_symbols::kNormalSelection;
    case ic::StlDiagnosticCode::kUnsupportedAttribute:
      return wire_symbols::kUnsupportedAttribute;
    case ic::StlDiagnosticCode::kMetadata:
      return wire_symbols::kMetadata;
    case ic::StlDiagnosticCode::kExpansionAcknowledgment:
      return wire_symbols::kExpansionAcknowledgment;
    case ic::StlDiagnosticCode::kNumericConversion:
      return wire_symbols::kNumericConversion;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::StlDiagnosticCode value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::StlEncoding value) {
  switch (value) {
    case ic::StlEncoding::kInvalid:
      return wire_symbols::kInvalid;
    case ic::StlEncoding::kAscii:
      return wire_symbols::kAscii;
    case ic::StlEncoding::kBinary:
      return wire_symbols::kBinary;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::StlEncoding value, ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::StlMetadataPolicy value) {
  switch (value) {
    case ic::StlMetadataPolicy::kExplicitWithoutSource:
      return wire_symbols::kExplicitWithoutSource;
    case ic::StlMetadataPolicy::kRetainedSourceMetadata:
      return wire_symbols::kRetainedSourceMetadata;
    case ic::StlMetadataPolicy::kReplacedSourceMetadata:
      return wire_symbols::kReplacedSourceMetadata;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::StlMetadataPolicy value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceNumericSummary::Kind value) {
  switch (value) {
    case ic::SurfaceNumericSummary::Kind::kPreparedFields:
      return wire_symbols::kPreparedFields;
    case ic::SurfaceNumericSummary::Kind::kDecodedWire:
      return wire_symbols::kDecodedWire;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceNumericSummary::Kind value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
std::string_view WireSymbol(ic::SurfaceDelegateProfileStatus value) {
  switch (value) {
    case ic::SurfaceDelegateProfileStatus::kEstablishedBaseline:
      return wire_symbols::kEstablishedBaseline;
    case ic::SurfaceDelegateProfileStatus::kUnsupportedRounding:
      return wire_symbols::kUnsupportedRounding;
    case ic::SurfaceDelegateProfileStatus::kUnsupportedFlushState:
      return wire_symbols::kUnsupportedFlushState;
    case ic::SurfaceDelegateProfileStatus::kUnqueryable:
      return wire_symbols::kUnqueryable;
  }
  throw std::logic_error("unknown native enum value");
}
[[maybe_unused]] void Measure(ic::SurfaceDelegateProfileStatus value,
                              ObservationSize& size) {
  size.String(WireSymbol(value));
}
[[maybe_unused]] void Measure(const ic::OffFileReadResult& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kExecution});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.execution, size);
}
[[maybe_unused]] void Measure(const ic::OffFileDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kStage,
               observation_keys::kCode, observation_keys::kFileIndex,
               observation_keys::kByteOffset, observation_keys::kSystemError,
               observation_keys::kOffStatus, observation_keys::kOffDiagnostic,
               observation_keys::kGeometryStatus,
               observation_keys::kGeometryDiagnostic});
  Measure(value.status, size);
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.file_index, size);
  Measure(value.byte_offset, size);
  Measure(value.system_error, size);
  Measure(value.off_status, size);
  Measure(value.off_diagnostic, size);
  Measure(value.geometry_status, size);
  Measure(value.geometry_diagnostic, size);
}
[[maybe_unused]] void Measure(const ic::OffDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kCode,
               observation_keys::kByteOffset, observation_keys::kVertex,
               observation_keys::kFace, observation_keys::kGeometryStatus,
               observation_keys::kGeometryDiagnostic,
               observation_keys::kStorageIssue});
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.byte_offset, size);
  Measure(value.vertex, size);
  Measure(value.face, size);
  Measure(value.geometry_status, size);
  Measure(value.geometry_diagnostic, size);
  Measure(value.storage_issue, size);
}
[[maybe_unused]] void Measure(const geo::MaterializationDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kCode, observation_keys::kAttribute,
               observation_keys::kElement, observation_keys::kStorageIssue});
  Measure(value.code, size);
  Measure(value.attribute, size);
  Measure(value.element, size);
  Measure(value.storage_issue, size);
}
[[maybe_unused]] void Measure(const geo::StorageIssue& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kCode, observation_keys::kAttribute,
               observation_keys::kElement});
  Measure(value.code, size);
  Measure(value.attribute, size);
  Measure(value.element, size);
}
[[maybe_unused]] void Measure(const ic::OffFileExecution& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kCapture, observation_keys::kCopying,
               observation_keys::kByteVerification,
               observation_keys::kMeshVerification,
               observation_keys::kExpectedInspection, observation_keys::kOff});
  Measure(value.capture, size);
  Measure(value.copying, size);
  Measure(value.byte_verification, size);
  Measure(value.mesh_verification, size);
  Measure(value.expected_inspection, size);
  Measure(value.off, size);
}
[[maybe_unused]] void Measure(const geo::MaterializationExecutionStats& value,
                              ObservationSize& size) {
  size.Object(
      {observation_keys::kPlannedJobs, observation_keys::kLaunchedWorkers,
       observation_keys::kJoinedWorkers, observation_keys::kCallerJobs});
  Measure(value.planned_jobs, size);
  Measure(value.launched_workers, size);
  Measure(value.joined_workers, size);
  Measure(value.caller_jobs, size);
}
[[maybe_unused]] void Measure(const ic::OffExecution& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kCodec, observation_keys::kMaterialization,
               observation_keys::kInspection});
  Measure(value.codec, size);
  Measure(value.materialization, size);
  Measure(value.inspection, size);
}
[[maybe_unused]] void Measure(const OffGlbSurfaceWorkflowResult& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
}
[[maybe_unused]] void Measure(const OffGlbSurfaceDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kCode});
  Measure(value.stage, size);
  Measure(value.code, size);
}
[[maybe_unused]] void Measure(const OffDuplicateFileWorkflowResult& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kOutcomes,
               observation_keys::kDiagnostic,
               observation_keys::kReportDiagnostic,
               observation_keys::kExecution});
  Measure(value.status, size);
  Measure(value.outcomes, size);
  Measure(value.diagnostic, size);
  Measure(value.report_diagnostic, size);
  Measure(value.execution, size);
}
[[maybe_unused]] void Measure(const OffWorkflowOutcomes& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kExecution, observation_keys::kCandidate,
               observation_keys::kProfile, observation_keys::kPublication,
               observation_keys::kStage, observation_keys::kExit});
  Measure(value.execution, size);
  Measure(value.candidate, size);
  Measure(value.profile, size);
  Measure(value.publication, size);
  Measure(value.stage, size);
  Measure(value.exit, size);
}
[[maybe_unused]] void Measure(const OffWorkflowDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kCode,
               observation_keys::kElement, observation_keys::kDelegatedStatus,
               observation_keys::kDelegated});
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.element, size);
  Measure(value.delegated_status, size);
  Measure(value.delegated, size);
}
[[maybe_unused]] void Measure(const MaterializedDuplicateDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kCode,
               observation_keys::kTarget, observation_keys::kAttribute,
               observation_keys::kElement, observation_keys::kGeometryStatus,
               observation_keys::kGeometryDiagnostic,
               observation_keys::kStorageIssue});
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.target, size);
  Measure(value.attribute, size);
  Measure(value.element, size);
  Measure(value.geometry_status, size);
  Measure(value.geometry_diagnostic, size);
  Measure(value.storage_issue, size);
}
[[maybe_unused]] void Measure(const OffWorkflowExecution& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kRepair, observation_keys::kWriting,
               observation_keys::kReload, observation_keys::kSourceProtection,
               observation_keys::kRepairCorrespondence,
               observation_keys::kCanonicalPreservation,
               observation_keys::kReloadVerification,
               observation_keys::kSourceVerification,
               observation_keys::kStagedCandidate, observation_keys::kHashing,
               observation_keys::kReportCopy});
  Measure(value.repair, size);
  Measure(value.writing, size);
  Measure(value.reload, size);
  Measure(value.source_protection, size);
  Measure(value.repair_correspondence, size);
  Measure(value.canonical_preservation, size);
  Measure(value.reload_verification, size);
  Measure(value.source_verification, size);
  Measure(value.staged_candidate, size);
  Measure(value.hashing, size);
  Measure(value.report_copy, size);
}
[[maybe_unused]] void Measure(const MaterializedDuplicateExecution& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kSourceInspection,
               observation_keys::kEligibility, observation_keys::kComparison,
               observation_keys::kSelection, observation_keys::kCorrespondence,
               observation_keys::kCandidateInspection,
               observation_keys::kVerification,
               observation_keys::kDirectedVerification});
  Measure(value.source_inspection, size);
  Measure(value.eligibility, size);
  Measure(value.comparison, size);
  Measure(value.selection, size);
  Measure(value.correspondence, size);
  Measure(value.candidate_inspection, size);
  Measure(value.verification, size);
  Measure(value.directed_verification, size);
}
[[maybe_unused]] void Measure(const geo::ChargedTriangulationResult& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kCounts, observation_keys::kSourceCapture,
               observation_keys::kSourceInspection, observation_keys::kCore,
               observation_keys::kOutputMaterialization,
               observation_keys::kOutputInspection,
               observation_keys::kAdaptationExecution,
               observation_keys::kTransferExecution});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.counts, size);
  Measure(value.source_capture, size);
  Measure(value.source_inspection, size);
  Measure(value.core, size);
  Measure(value.output_materialization, size);
  Measure(value.output_inspection, size);
  Measure(value.adaptation_execution, size);
  Measure(value.transfer_execution, size);
}
[[maybe_unused]] void Measure(const geo::ChargedTriangulationDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kCode,
               observation_keys::kFace, observation_keys::kAttribute,
               observation_keys::kStorageIssue});
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.face, size);
  Measure(value.attribute, size);
  Measure(value.storage_issue, size);
}
[[maybe_unused]] void Measure(const geo::ChargedTriangulationCounts& value,
                              ObservationSize& size) {
  size.Object(
      {observation_keys::kSourceVertices, observation_keys::kSourceFaces,
       observation_keys::kSourceCorners, observation_keys::kOutputVertices,
       observation_keys::kOutputFaces, observation_keys::kOutputCorners});
  Measure(value.source_vertices, size);
  Measure(value.source_faces, size);
  Measure(value.source_corners, size);
  Measure(value.output_vertices, size);
  Measure(value.output_faces, size);
  Measure(value.output_corners, size);
}
[[maybe_unused]] void Measure(
    const geo::ChargedTriangulationMaterializationFacts& value,
    ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kExecution,
               observation_keys::kNewRequestedBytesAtReturn});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.execution, size);
  Measure(value.new_requested_bytes_at_return, size);
}
[[maybe_unused]] void Measure(
    const geo::ChargedTriangulationInspectionFacts& value,
    ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kExecution, observation_keys::kStorageValid,
               observation_keys::kNewRequestedBytesAtReturn});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.execution, size);
  Measure(value.storage_valid, size);
  Measure(value.new_requested_bytes_at_return, size);
}
[[maybe_unused]] void Measure(const geo::ChargedTriangulationCoreFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kWorkersUsed,
               observation_keys::kSerialReason,
               observation_keys::kNewRequestedPeakBytes});
  Measure(value.status, size);
  Measure(value.workers_used, size);
  Measure(value.serial_reason, size);
  Measure(value.new_requested_peak_bytes, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceEncodeResult& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kFacts});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.facts, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kCode,
               observation_keys::kSubject, observation_keys::kIndex,
               observation_keys::kSecondaryIndex,
               observation_keys::kComponent});
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.subject, size);
  Measure(value.index, size);
  Measure(value.secondary_index, size);
  Measure(value.component, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceInspectionResult& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kFacts});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.facts, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFilePublicationResult& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOutcome, observation_keys::kDiagnostic,
               observation_keys::kCleanup, observation_keys::kFacts});
  Measure(value.outcome, size);
  Measure(value.diagnostic, size);
  Measure(value.cleanup, size);
  Measure(value.facts, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kStage,
               observation_keys::kCode, observation_keys::kMemberOrdinal,
               observation_keys::kByteOffset, observation_keys::kSystemError,
               observation_keys::kInspectionStatus,
               observation_keys::kInspectionDiagnostic});
  Measure(value.status, size);
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.member_ordinal, size);
  Measure(value.byte_offset, size);
  Measure(value.system_error, size);
  Measure(value.inspection_status, size);
  Measure(value.inspection_diagnostic, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileCleanupObservation& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOutcome, observation_keys::kError});
  Measure(value.outcome, size);
  Measure(value.error, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileFacts& value,
                              ObservationSize& size) {
  size.Object(
      {observation_keys::kPayloadWrite, observation_keys::kPayloadInventory,
       observation_keys::kPayloadReread, observation_keys::kInspection,
       observation_keys::kProvider, observation_keys::kReportImport,
       observation_keys::kReceiptWrite, observation_keys::kReceiptReread,
       observation_keys::kFinalInventory,
       observation_keys::kFinalPayloadVerification,
       observation_keys::kFinalReceiptVerification, observation_keys::kCommit});
  Measure(value.payload_write, size);
  Measure(value.payload_inventory, size);
  Measure(value.payload_reread, size);
  Measure(value.inspection, size);
  Measure(value.provider, size);
  Measure(value.report_import, size);
  Measure(value.receipt_write, size);
  Measure(value.receipt_reread, size);
  Measure(value.final_inventory, size);
  Measure(value.final_payload_verification, size);
  Measure(value.final_receipt_verification, size);
  Measure(value.commit, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileIoObservation& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOutcome, observation_keys::kBytesRead,
               observation_keys::kBytesWritten, observation_keys::kBytesCopied,
               observation_keys::kBytesCompared, observation_keys::kBytesHashed,
               observation_keys::kCompletedFiles,
               observation_keys::kClosedFiles, observation_keys::kExecution});
  Measure(value.outcome, size);
  Measure(value.bytes_read, size);
  Measure(value.bytes_written, size);
  Measure(value.bytes_copied, size);
  Measure(value.bytes_compared, size);
  Measure(value.bytes_hashed, size);
  Measure(value.completed_files, size);
  Measure(value.closed_files, size);
  Measure(value.execution, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileExecutionStats& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kPlannedJobs,
               observation_keys::kLaunchedWorkers,
               observation_keys::kCallerJobs, observation_keys::kJoinedWorkers,
               observation_keys::kCompletedJobs});
  Measure(value.planned_jobs, size);
  Measure(value.launched_workers, size);
  Measure(value.caller_jobs, size);
  Measure(value.joined_workers, size);
  Measure(value.completed_jobs, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileInventoryObservation& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOutcome, observation_keys::kObservedFiles,
               observation_keys::kObservedDirectories});
  Measure(value.outcome, size);
  Measure(value.observed_files, size);
  Measure(value.observed_directories, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileInspectionObservation& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kFacts});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.facts, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileProviderObservation& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOutcome, observation_keys::kReturnedBytes});
  Measure(value.outcome, size);
  Measure(value.returned_bytes, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFileCommitObservation& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOutcome, observation_keys::kError});
  Measure(value.outcome, size);
  Measure(value.error, size);
}
[[maybe_unused]] void Measure(const ic::SurfacePrimitiveView& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOrdinal, observation_keys::kFirstWireVertex,
               observation_keys::kWireVertices,
               observation_keys::kFirstWireTriangle,
               observation_keys::kWireTriangles, observation_keys::kMaterial});
  Measure(value.ordinal, size);
  Measure(value.first_wire_vertex, size);
  Measure(value.wire_vertices, size);
  Measure(value.first_wire_triangle, size);
  Measure(value.wire_triangles, size);
  Measure(value.material, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceMeshCounts& value,
                              ObservationSize& size) {
  size.Object(
      {observation_keys::kVertices, observation_keys::kFaces,
       observation_keys::kCorners, observation_keys::kFaceOffsets,
       observation_keys::kAttributes, observation_keys::kAttributeScalars,
       observation_keys::kAttributeBytes, observation_keys::kAttributeOffsets,
       observation_keys::kPresenceBytes, observation_keys::kMetadataEntries,
       observation_keys::kDescriptorBytes});
  Measure(value.vertices, size);
  Measure(value.faces, size);
  Measure(value.corners, size);
  Measure(value.face_offsets, size);
  Measure(value.attributes, size);
  Measure(value.attribute_scalars, size);
  Measure(value.attribute_bytes, size);
  Measure(value.attribute_offsets, size);
  Measure(value.presence_bytes, size);
  Measure(value.metadata_entries, size);
  Measure(value.descriptor_bytes, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceWireCounts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kPrimitives, observation_keys::kVertices,
               observation_keys::kTriangles, observation_keys::kCorners,
               observation_keys::kMapEntries, observation_keys::kMaterials,
               observation_keys::kTextures, observation_keys::kImages,
               observation_keys::kSamplers, observation_keys::kMembers,
               observation_keys::kMemberBytes});
  Measure(value.primitives, size);
  Measure(value.vertices, size);
  Measure(value.triangles, size);
  Measure(value.corners, size);
  Measure(value.map_entries, size);
  Measure(value.materials, size);
  Measure(value.textures, size);
  Measure(value.images, size);
  Measure(value.samplers, size);
  Measure(value.members, size);
  Measure(value.member_bytes, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceDelegateView& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kKind,
               observation_keys::kCallOrdinal, observation_keys::kPrimitive,
               observation_keys::kFacts});
  Measure(value.stage, size);
  Measure(value.kind, size);
  Measure(value.call_ordinal, size);
  Measure(value.primitive, size);
  Measure(value.facts, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceGeometryFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kExecution,
               observation_keys::kRequestedBytesAtReturn});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.execution, size);
  Measure(value.requested_bytes_at_return, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceGlbFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kErrorByteOffset,
               observation_keys::kWorkersUsed});
  Measure(value.status, size);
  Measure(value.error_byte_offset, size);
  Measure(value.workers_used, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceGltfFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kDiagnostic,
               observation_keys::kRequestedBytesAtReturn});
  Measure(value.diagnostic, size);
  Measure(value.requested_bytes_at_return, size);
}
[[maybe_unused]] void Measure(const ic::GltfDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kCode,
               observation_keys::kJsonSpan, observation_keys::kResourceKind,
               observation_keys::kResourceIndex,
               observation_keys::kResourceSpan, observation_keys::kRuleId});
  Measure(value.status, size);
  Measure(value.code, size);
  Measure(value.json_span, size);
  Measure(value.resource_kind, size);
  Measure(value.resource_index, size);
  Measure(value.resource_span, size);
  Measure(value.rule_id, size);
}
[[maybe_unused]] void Measure(const ic::GltfSourceSpan& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOffset, observation_keys::kLength});
  Measure(value.offset, size);
  Measure(value.length, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceStlFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kDiagnostic,
               observation_keys::kExecution,
               observation_keys::kRequestedBytesAtReturn,
               observation_keys::kWriteFacts});
  Measure(value.status, size);
  Measure(value.diagnostic, size);
  Measure(value.execution, size);
  Measure(value.requested_bytes_at_return, size);
  Measure(value.write_facts, size);
}
[[maybe_unused]] void Measure(const ic::StlDiagnostic& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kCode,
               observation_keys::kByteOffset, observation_keys::kFacet,
               observation_keys::kAttribute, observation_keys::kGeometryStatus,
               observation_keys::kGeometryDiagnostic,
               observation_keys::kStorageIssue});
  Measure(value.stage, size);
  Measure(value.code, size);
  Measure(value.byte_offset, size);
  Measure(value.facet, size);
  Measure(value.attribute, size);
  Measure(value.geometry_status, size);
  Measure(value.geometry_diagnostic, size);
  Measure(value.storage_issue, size);
}
[[maybe_unused]] void Measure(const ic::StlExecution& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kCodec, observation_keys::kMaterialization,
               observation_keys::kInspection});
  Measure(value.codec, size);
  Measure(value.materialization, size);
  Measure(value.inspection, size);
}
[[maybe_unused]] void Measure(const ic::StlRequestedBytes& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kCodec, observation_keys::kMaterialization,
               observation_keys::kInspection, observation_keys::kTotal});
  Measure(value.codec, size);
  Measure(value.materialization, size);
  Measure(value.inspection, size);
  Measure(value.total, size);
}
[[maybe_unused]] void Measure(const ic::StlWriteFacts& value,
                              ObservationSize& size) {
  size.Object(
      {observation_keys::kEncoding, observation_keys::kFaceNormalAttribute,
       observation_keys::kSourceVertexCount, observation_keys::kSourceFaceCount,
       observation_keys::kCornerExpansionAcknowledged,
       observation_keys::kSourceEncoding, observation_keys::kMetadataPolicy});
  Measure(value.encoding, size);
  Measure(value.face_normal_attribute, size);
  Measure(value.source_vertex_count, size);
  Measure(value.source_face_count, size);
  Measure(value.corner_expansion_acknowledged, size);
  Measure(value.source_encoding, size);
  Measure(value.metadata_policy, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceCoreFactorFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kMaterial, observation_keys::kCoreF32,
               observation_keys::kAuthored, observation_keys::kObserved,
               observation_keys::kLexicalCalls, observation_keys::kComplete,
               observation_keys::kFailure});
  Measure(value.material, size);
  MeasureHexWords(value.core_f32, size);
  Measure(value.authored, size);
  Measure(value.observed, size);
  Measure(value.lexical_calls, size);
  Measure(value.complete, size);
  Measure(value.failure, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceNumericSummary& value,
                              ObservationSize& size) {
  size.Object(
      {observation_keys::kKind, observation_keys::kComplete,
       observation_keys::kExaminedValues, observation_keys::kChangedValues,
       observation_keys::kMaxAbsoluteError, observation_keys::kSourceAttribute,
       observation_keys::kSourceRow, observation_keys::kComponent});
  Measure(value.kind, size);
  Measure(value.complete, size);
  Measure(value.examined_values, size);
  Measure(value.changed_values, size);
  Measure(value.max_absolute_error, size);
  Measure(value.source_attribute, size);
  Measure(value.source_row, size);
  Measure(value.component, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceDyadicError& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kMagnitude, observation_keys::kUsedWords,
               observation_keys::kExponent2});
  MeasureHexWords(value.magnitude, size);
  Measure(value.used_words, size);
  Measure(value.exponent2, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceWorkCounts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kSourceScalarPairs,
               observation_keys::kDescriptorBytePairs,
               observation_keys::kCoincidenceComparisons,
               observation_keys::kRelationshipSteps});
  Measure(value.source_scalar_pairs, size);
  Measure(value.descriptor_byte_pairs, size);
  Measure(value.coincidence_comparisons, size);
  Measure(value.relationship_steps, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceDelegateProfileView& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStage, observation_keys::kKind,
               observation_keys::kCallOrdinal, observation_keys::kPrimitive,
               observation_keys::kFactorComponent, observation_keys::kFacts});
  Measure(value.stage, size);
  Measure(value.kind, size);
  Measure(value.call_ordinal, size);
  Measure(value.primitive, size);
  Measure(value.factor_component, size);
  Measure(value.facts, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceDelegateProfileFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kStatus, observation_keys::kNearestEven,
               observation_keys::kFtzDisabled, observation_keys::kDazDisabled});
  Measure(value.status, size);
  Measure(value.nearest_even, size);
  Measure(value.ftz_disabled, size);
  Measure(value.daz_disabled, size);
}
[[maybe_unused]] void Measure(const ic::SurfaceFacts& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kOriginalCounts,
               observation_keys::kTriangleCounts, observation_keys::kWireCounts,
               observation_keys::kDelegates, observation_keys::kPositionNumeric,
               observation_keys::kUvNumeric, observation_keys::kExecution,
               observation_keys::kExaminedWork,
               observation_keys::kNumericDelegateProfiles,
               observation_keys::kNewRequestedBytes});
  Measure(value.OriginalCounts(), size);
  Measure(value.TriangleCounts(), size);
  Measure(value.WireCounts(), size);
  Measure(value.Delegates(), size);
  Measure(value.PositionNumeric(), size);
  Measure(value.UvNumeric(), size);
  Measure(value.Execution(), size);
  Measure(value.ExaminedWork(), size);
  Measure(value.NumericDelegateProfiles(), size);
  Measure(value.NewRequestedBytes(), size);
}
[[maybe_unused]] void Measure(const std::error_code& value,
                              ObservationSize& size) {
  size.Object({observation_keys::kCategory, observation_keys::kValue});
  size.Add(kJsonQuoteBytes + sizeof(std::uint32_t) * kHexDigitsPerByte);
  size.String(value.category().name());
}
[[maybe_unused]] nb::object Wire(ic::OffFileStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::OffFileStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::OffFileDiagnosticCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::OffStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::OffStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::OffDiagnosticCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::MaterializationStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::MaterializationDiagnosticCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::StorageIssueCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffGlbSurfaceStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffGlbSurfaceStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffGlbSurfaceCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffWorkflowStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffWorkflowExecutionOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(CandidateOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffWorkflowProfileOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffWorkflowPublicationOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffWorkflowStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffWorkflowExitMeaning value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(OffWorkflowDiagnosticCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(MaterializedDuplicateStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(MaterializedDuplicateStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(MaterializedDuplicateDiagnosticCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::TriangulationStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(geo::ChargedTriangulationSerialReason value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceSubject value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceFilePublicationOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceFileStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceFileStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceFileCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceFileCleanupOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceFilePhaseOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceFileCommitOutcome value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceDelegateKind value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::GlbStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::GltfStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::GltfDiagnosticCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::GltfResourceKind value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::StlStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::StlStage value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::StlDiagnosticCode value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::StlEncoding value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::StlMetadataPolicy value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceNumericSummary::Kind value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(ic::SurfaceDelegateProfileStatus value) {
  auto symbol = WireSymbol(value);
  return nb::str(symbol.data(), symbol.size());
}
[[maybe_unused]] nb::object Wire(const ic::OffFileReadResult& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kExecution] = Wire(value.execution);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::OffFileDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kFileIndex] = Wire(value.file_index);
  result[observation_keys::kByteOffset] = Wire(value.byte_offset);
  result[observation_keys::kSystemError] = Wire(value.system_error);
  result[observation_keys::kOffStatus] = Wire(value.off_status);
  result[observation_keys::kOffDiagnostic] = Wire(value.off_diagnostic);
  result[observation_keys::kGeometryStatus] = Wire(value.geometry_status);
  result[observation_keys::kGeometryDiagnostic] =
      Wire(value.geometry_diagnostic);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::OffDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kByteOffset] = Wire(value.byte_offset);
  result[observation_keys::kVertex] = Wire(value.vertex);
  result[observation_keys::kFace] = Wire(value.face);
  result[observation_keys::kGeometryStatus] = Wire(value.geometry_status);
  result[observation_keys::kGeometryDiagnostic] =
      Wire(value.geometry_diagnostic);
  result[observation_keys::kStorageIssue] = Wire(value.storage_issue);
  return result;
}
[[maybe_unused]] nb::object Wire(const geo::MaterializationDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kAttribute] = Wire(value.attribute);
  result[observation_keys::kElement] = Wire(value.element);
  result[observation_keys::kStorageIssue] = Wire(value.storage_issue);
  return result;
}
[[maybe_unused]] nb::object Wire(const geo::StorageIssue& value) {
  nb::dict result;
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kAttribute] = Wire(value.attribute);
  result[observation_keys::kElement] = Wire(value.element);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::OffFileExecution& value) {
  nb::dict result;
  result[observation_keys::kCapture] = Wire(value.capture);
  result[observation_keys::kCopying] = Wire(value.copying);
  result[observation_keys::kByteVerification] = Wire(value.byte_verification);
  result[observation_keys::kMeshVerification] = Wire(value.mesh_verification);
  result[observation_keys::kExpectedInspection] =
      Wire(value.expected_inspection);
  result[observation_keys::kOff] = Wire(value.off);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const geo::MaterializationExecutionStats& value) {
  nb::dict result;
  result[observation_keys::kPlannedJobs] = Wire(value.planned_jobs);
  result[observation_keys::kLaunchedWorkers] = Wire(value.launched_workers);
  result[observation_keys::kJoinedWorkers] = Wire(value.joined_workers);
  result[observation_keys::kCallerJobs] = Wire(value.caller_jobs);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::OffExecution& value) {
  nb::dict result;
  result[observation_keys::kCodec] = Wire(value.codec);
  result[observation_keys::kMaterialization] = Wire(value.materialization);
  result[observation_keys::kInspection] = Wire(value.inspection);
  return result;
}
[[maybe_unused]] nb::object Wire(const OffGlbSurfaceWorkflowResult& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  return result;
}
[[maybe_unused]] nb::object Wire(const OffGlbSurfaceDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  return result;
}
[[maybe_unused]] nb::object Wire(const OffDuplicateFileWorkflowResult& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kOutcomes] = Wire(value.outcomes);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kReportDiagnostic] = Wire(value.report_diagnostic);
  result[observation_keys::kExecution] = Wire(value.execution);
  return result;
}
[[maybe_unused]] nb::object Wire(const OffWorkflowOutcomes& value) {
  nb::dict result;
  result[observation_keys::kExecution] = Wire(value.execution);
  result[observation_keys::kCandidate] = Wire(value.candidate);
  result[observation_keys::kProfile] = Wire(value.profile);
  result[observation_keys::kPublication] = Wire(value.publication);
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kExit] = Wire(value.exit);
  return result;
}
[[maybe_unused]] nb::object Wire(const OffWorkflowDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kElement] = Wire(value.element);
  result[observation_keys::kDelegatedStatus] = Wire(value.delegated_status);
  result[observation_keys::kDelegated] = Wire(value.delegated);
  return result;
}
[[maybe_unused]] nb::object Wire(const MaterializedDuplicateDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kTarget] = Wire(value.target);
  result[observation_keys::kAttribute] = Wire(value.attribute);
  result[observation_keys::kElement] = Wire(value.element);
  result[observation_keys::kGeometryStatus] = Wire(value.geometry_status);
  result[observation_keys::kGeometryDiagnostic] =
      Wire(value.geometry_diagnostic);
  result[observation_keys::kStorageIssue] = Wire(value.storage_issue);
  return result;
}
[[maybe_unused]] nb::object Wire(const OffWorkflowExecution& value) {
  nb::dict result;
  result[observation_keys::kRepair] = Wire(value.repair);
  result[observation_keys::kWriting] = Wire(value.writing);
  result[observation_keys::kReload] = Wire(value.reload);
  result[observation_keys::kSourceProtection] = Wire(value.source_protection);
  result[observation_keys::kRepairCorrespondence] =
      Wire(value.repair_correspondence);
  result[observation_keys::kCanonicalPreservation] =
      Wire(value.canonical_preservation);
  result[observation_keys::kReloadVerification] =
      Wire(value.reload_verification);
  result[observation_keys::kSourceVerification] =
      Wire(value.source_verification);
  result[observation_keys::kStagedCandidate] = Wire(value.staged_candidate);
  result[observation_keys::kHashing] = Wire(value.hashing);
  result[observation_keys::kReportCopy] = Wire(value.report_copy);
  return result;
}
[[maybe_unused]] nb::object Wire(const MaterializedDuplicateExecution& value) {
  nb::dict result;
  result[observation_keys::kSourceInspection] = Wire(value.source_inspection);
  result[observation_keys::kEligibility] = Wire(value.eligibility);
  result[observation_keys::kComparison] = Wire(value.comparison);
  result[observation_keys::kSelection] = Wire(value.selection);
  result[observation_keys::kCorrespondence] = Wire(value.correspondence);
  result[observation_keys::kCandidateInspection] =
      Wire(value.candidate_inspection);
  result[observation_keys::kVerification] = Wire(value.verification);
  result[observation_keys::kDirectedVerification] =
      Wire(value.directed_verification);
  return result;
}
[[maybe_unused]] nb::object Wire(const geo::ChargedTriangulationResult& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kCounts] = Wire(value.counts);
  result[observation_keys::kSourceCapture] = Wire(value.source_capture);
  result[observation_keys::kSourceInspection] = Wire(value.source_inspection);
  result[observation_keys::kCore] = Wire(value.core);
  result[observation_keys::kOutputMaterialization] =
      Wire(value.output_materialization);
  result[observation_keys::kOutputInspection] = Wire(value.output_inspection);
  result[observation_keys::kAdaptationExecution] =
      Wire(value.adaptation_execution);
  result[observation_keys::kTransferExecution] = Wire(value.transfer_execution);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kFace] = Wire(value.face);
  result[observation_keys::kAttribute] = Wire(value.attribute);
  result[observation_keys::kStorageIssue] = Wire(value.storage_issue);
  return result;
}
[[maybe_unused]] nb::object Wire(const geo::ChargedTriangulationCounts& value) {
  nb::dict result;
  result[observation_keys::kSourceVertices] = Wire(value.source_vertices);
  result[observation_keys::kSourceFaces] = Wire(value.source_faces);
  result[observation_keys::kSourceCorners] = Wire(value.source_corners);
  result[observation_keys::kOutputVertices] = Wire(value.output_vertices);
  result[observation_keys::kOutputFaces] = Wire(value.output_faces);
  result[observation_keys::kOutputCorners] = Wire(value.output_corners);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationMaterializationFacts& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kExecution] = Wire(value.execution);
  result[observation_keys::kNewRequestedBytesAtReturn] =
      Wire(value.new_requested_bytes_at_return);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationInspectionFacts& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kExecution] = Wire(value.execution);
  result[observation_keys::kStorageValid] = Wire(value.storage_valid);
  result[observation_keys::kNewRequestedBytesAtReturn] =
      Wire(value.new_requested_bytes_at_return);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const geo::ChargedTriangulationCoreFacts& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kWorkersUsed] = Wire(value.workers_used);
  result[observation_keys::kSerialReason] = Wire(value.serial_reason);
  result[observation_keys::kNewRequestedPeakBytes] =
      Wire(value.new_requested_peak_bytes);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceEncodeResult& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kFacts] = Wire(value.facts);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kSubject] = Wire(value.subject);
  result[observation_keys::kIndex] = Wire(value.index);
  result[observation_keys::kSecondaryIndex] = Wire(value.secondary_index);
  result[observation_keys::kComponent] = Wire(value.component);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceInspectionResult& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kFacts] = Wire(value.facts);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFilePublicationResult& value) {
  nb::dict result;
  result[observation_keys::kOutcome] = Wire(value.outcome);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kCleanup] = Wire(value.cleanup);
  result[observation_keys::kFacts] = Wire(value.facts);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kMemberOrdinal] = Wire(value.member_ordinal);
  result[observation_keys::kByteOffset] = Wire(value.byte_offset);
  result[observation_keys::kSystemError] = Wire(value.system_error);
  result[observation_keys::kInspectionStatus] = Wire(value.inspection_status);
  result[observation_keys::kInspectionDiagnostic] =
      Wire(value.inspection_diagnostic);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileCleanupObservation& value) {
  nb::dict result;
  result[observation_keys::kOutcome] = Wire(value.outcome);
  result[observation_keys::kError] = Wire(value.error);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileFacts& value) {
  nb::dict result;
  result[observation_keys::kPayloadWrite] = Wire(value.payload_write);
  result[observation_keys::kPayloadInventory] = Wire(value.payload_inventory);
  result[observation_keys::kPayloadReread] = Wire(value.payload_reread);
  result[observation_keys::kInspection] = Wire(value.inspection);
  result[observation_keys::kProvider] = Wire(value.provider);
  result[observation_keys::kReportImport] = Wire(value.report_import);
  result[observation_keys::kReceiptWrite] = Wire(value.receipt_write);
  result[observation_keys::kReceiptReread] = Wire(value.receipt_reread);
  result[observation_keys::kFinalInventory] = Wire(value.final_inventory);
  result[observation_keys::kFinalPayloadVerification] =
      Wire(value.final_payload_verification);
  result[observation_keys::kFinalReceiptVerification] =
      Wire(value.final_receipt_verification);
  result[observation_keys::kCommit] = Wire(value.commit);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileIoObservation& value) {
  nb::dict result;
  result[observation_keys::kOutcome] = Wire(value.outcome);
  result[observation_keys::kBytesRead] = Wire(value.bytes_read);
  result[observation_keys::kBytesWritten] = Wire(value.bytes_written);
  result[observation_keys::kBytesCopied] = Wire(value.bytes_copied);
  result[observation_keys::kBytesCompared] = Wire(value.bytes_compared);
  result[observation_keys::kBytesHashed] = Wire(value.bytes_hashed);
  result[observation_keys::kCompletedFiles] = Wire(value.completed_files);
  result[observation_keys::kClosedFiles] = Wire(value.closed_files);
  result[observation_keys::kExecution] = Wire(value.execution);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceFileExecutionStats& value) {
  nb::dict result;
  result[observation_keys::kPlannedJobs] = Wire(value.planned_jobs);
  result[observation_keys::kLaunchedWorkers] = Wire(value.launched_workers);
  result[observation_keys::kCallerJobs] = Wire(value.caller_jobs);
  result[observation_keys::kJoinedWorkers] = Wire(value.joined_workers);
  result[observation_keys::kCompletedJobs] = Wire(value.completed_jobs);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileInventoryObservation& value) {
  nb::dict result;
  result[observation_keys::kOutcome] = Wire(value.outcome);
  result[observation_keys::kObservedFiles] = Wire(value.observed_files);
  result[observation_keys::kObservedDirectories] =
      Wire(value.observed_directories);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileInspectionObservation& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kFacts] = Wire(value.facts);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileProviderObservation& value) {
  nb::dict result;
  result[observation_keys::kOutcome] = Wire(value.outcome);
  result[observation_keys::kReturnedBytes] = Wire(value.returned_bytes);
  return result;
}
[[maybe_unused]] nb::object Wire(
    const ic::SurfaceFileCommitObservation& value) {
  nb::dict result;
  result[observation_keys::kOutcome] = Wire(value.outcome);
  result[observation_keys::kError] = Wire(value.error);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfacePrimitiveView& value) {
  nb::dict result;
  result[observation_keys::kOrdinal] = Wire(value.ordinal);
  result[observation_keys::kFirstWireVertex] = Wire(value.first_wire_vertex);
  result[observation_keys::kWireVertices] = Wire(value.wire_vertices);
  result[observation_keys::kFirstWireTriangle] =
      Wire(value.first_wire_triangle);
  result[observation_keys::kWireTriangles] = Wire(value.wire_triangles);
  result[observation_keys::kMaterial] = Wire(value.material);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceMeshCounts& value) {
  nb::dict result;
  result[observation_keys::kVertices] = Wire(value.vertices);
  result[observation_keys::kFaces] = Wire(value.faces);
  result[observation_keys::kCorners] = Wire(value.corners);
  result[observation_keys::kFaceOffsets] = Wire(value.face_offsets);
  result[observation_keys::kAttributes] = Wire(value.attributes);
  result[observation_keys::kAttributeScalars] = Wire(value.attribute_scalars);
  result[observation_keys::kAttributeBytes] = Wire(value.attribute_bytes);
  result[observation_keys::kAttributeOffsets] = Wire(value.attribute_offsets);
  result[observation_keys::kPresenceBytes] = Wire(value.presence_bytes);
  result[observation_keys::kMetadataEntries] = Wire(value.metadata_entries);
  result[observation_keys::kDescriptorBytes] = Wire(value.descriptor_bytes);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceWireCounts& value) {
  nb::dict result;
  result[observation_keys::kPrimitives] = Wire(value.primitives);
  result[observation_keys::kVertices] = Wire(value.vertices);
  result[observation_keys::kTriangles] = Wire(value.triangles);
  result[observation_keys::kCorners] = Wire(value.corners);
  result[observation_keys::kMapEntries] = Wire(value.map_entries);
  result[observation_keys::kMaterials] = Wire(value.materials);
  result[observation_keys::kTextures] = Wire(value.textures);
  result[observation_keys::kImages] = Wire(value.images);
  result[observation_keys::kSamplers] = Wire(value.samplers);
  result[observation_keys::kMembers] = Wire(value.members);
  result[observation_keys::kMemberBytes] = Wire(value.member_bytes);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceDelegateView& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kKind] = Wire(value.kind);
  result[observation_keys::kCallOrdinal] = Wire(value.call_ordinal);
  result[observation_keys::kPrimitive] = Wire(value.primitive);
  result[observation_keys::kFacts] = Wire(value.facts);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceGeometryFacts& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kExecution] = Wire(value.execution);
  result[observation_keys::kRequestedBytesAtReturn] =
      Wire(value.requested_bytes_at_return);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceGlbFacts& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kErrorByteOffset] = Wire(value.error_byte_offset);
  result[observation_keys::kWorkersUsed] = Wire(value.workers_used);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceGltfFacts& value) {
  nb::dict result;
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kRequestedBytesAtReturn] =
      Wire(value.requested_bytes_at_return);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::GltfDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kJsonSpan] = Wire(value.json_span);
  result[observation_keys::kResourceKind] = Wire(value.resource_kind);
  result[observation_keys::kResourceIndex] = Wire(value.resource_index);
  result[observation_keys::kResourceSpan] = Wire(value.resource_span);
  result[observation_keys::kRuleId] = Wire(value.rule_id);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::GltfSourceSpan& value) {
  nb::dict result;
  result[observation_keys::kOffset] = Wire(value.offset);
  result[observation_keys::kLength] = Wire(value.length);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceStlFacts& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kDiagnostic] = Wire(value.diagnostic);
  result[observation_keys::kExecution] = Wire(value.execution);
  result[observation_keys::kRequestedBytesAtReturn] =
      Wire(value.requested_bytes_at_return);
  result[observation_keys::kWriteFacts] = Wire(value.write_facts);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::StlDiagnostic& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kCode] = Wire(value.code);
  result[observation_keys::kByteOffset] = Wire(value.byte_offset);
  result[observation_keys::kFacet] = Wire(value.facet);
  result[observation_keys::kAttribute] = Wire(value.attribute);
  result[observation_keys::kGeometryStatus] = Wire(value.geometry_status);
  result[observation_keys::kGeometryDiagnostic] =
      Wire(value.geometry_diagnostic);
  result[observation_keys::kStorageIssue] = Wire(value.storage_issue);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::StlExecution& value) {
  nb::dict result;
  result[observation_keys::kCodec] = Wire(value.codec);
  result[observation_keys::kMaterialization] = Wire(value.materialization);
  result[observation_keys::kInspection] = Wire(value.inspection);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::StlRequestedBytes& value) {
  nb::dict result;
  result[observation_keys::kCodec] = Wire(value.codec);
  result[observation_keys::kMaterialization] = Wire(value.materialization);
  result[observation_keys::kInspection] = Wire(value.inspection);
  result[observation_keys::kTotal] = Wire(value.total);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::StlWriteFacts& value) {
  nb::dict result;
  result[observation_keys::kEncoding] = Wire(value.encoding);
  result[observation_keys::kFaceNormalAttribute] =
      Wire(value.face_normal_attribute);
  result[observation_keys::kSourceVertexCount] =
      Wire(value.source_vertex_count);
  result[observation_keys::kSourceFaceCount] = Wire(value.source_face_count);
  result[observation_keys::kCornerExpansionAcknowledged] =
      Wire(value.corner_expansion_acknowledged);
  result[observation_keys::kSourceEncoding] = Wire(value.source_encoding);
  result[observation_keys::kMetadataPolicy] = Wire(value.metadata_policy);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceCoreFactorFacts& value) {
  nb::dict result;
  result[observation_keys::kMaterial] = Wire(value.material);
  result[observation_keys::kCoreF32] = Tuple(
      value.core_f32, [](auto word) -> nb::object { return HexWord(word); });
  result[observation_keys::kAuthored] = Wire(value.authored);
  result[observation_keys::kObserved] = Wire(value.observed);
  result[observation_keys::kLexicalCalls] = Wire(value.lexical_calls);
  result[observation_keys::kComplete] = Wire(value.complete);
  result[observation_keys::kFailure] = Wire(value.failure);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceNumericSummary& value) {
  nb::dict result;
  result[observation_keys::kKind] = Wire(value.kind);
  result[observation_keys::kComplete] = Wire(value.complete);
  result[observation_keys::kExaminedValues] = Wire(value.examined_values);
  result[observation_keys::kChangedValues] = Wire(value.changed_values);
  result[observation_keys::kMaxAbsoluteError] = Wire(value.max_absolute_error);
  result[observation_keys::kSourceAttribute] = Wire(value.source_attribute);
  result[observation_keys::kSourceRow] = Wire(value.source_row);
  result[observation_keys::kComponent] = Wire(value.component);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceDyadicError& value) {
  nb::dict result;
  result[observation_keys::kMagnitude] = Tuple(
      value.magnitude, [](auto word) -> nb::object { return HexWord(word); });
  result[observation_keys::kUsedWords] = Wire(value.used_words);
  result[observation_keys::kExponent2] = Wire(value.exponent2);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceWorkCounts& value) {
  nb::dict result;
  result[observation_keys::kSourceScalarPairs] =
      Wire(value.source_scalar_pairs);
  result[observation_keys::kDescriptorBytePairs] =
      Wire(value.descriptor_byte_pairs);
  result[observation_keys::kCoincidenceComparisons] =
      Wire(value.coincidence_comparisons);
  result[observation_keys::kRelationshipSteps] = Wire(value.relationship_steps);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceDelegateProfileView& value) {
  nb::dict result;
  result[observation_keys::kStage] = Wire(value.stage);
  result[observation_keys::kKind] = Wire(value.kind);
  result[observation_keys::kCallOrdinal] = Wire(value.call_ordinal);
  result[observation_keys::kPrimitive] = Wire(value.primitive);
  result[observation_keys::kFactorComponent] = Wire(value.factor_component);
  result[observation_keys::kFacts] = Wire(value.facts);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceDelegateProfileFacts& value) {
  nb::dict result;
  result[observation_keys::kStatus] = Wire(value.status);
  result[observation_keys::kNearestEven] = Wire(value.nearest_even);
  result[observation_keys::kFtzDisabled] = Wire(value.ftz_disabled);
  result[observation_keys::kDazDisabled] = Wire(value.daz_disabled);
  return result;
}
[[maybe_unused]] nb::object Wire(const ic::SurfaceFacts& value) {
  nb::dict result;
  result[observation_keys::kOriginalCounts] = Wire(value.OriginalCounts());
  result[observation_keys::kTriangleCounts] = Wire(value.TriangleCounts());
  result[observation_keys::kWireCounts] = Wire(value.WireCounts());
  result[observation_keys::kDelegates] = Wire(value.Delegates());
  result[observation_keys::kPositionNumeric] = Wire(value.PositionNumeric());
  result[observation_keys::kUvNumeric] = Wire(value.UvNumeric());
  result[observation_keys::kExecution] = Wire(value.Execution());
  result[observation_keys::kExaminedWork] = Wire(value.ExaminedWork());
  result[observation_keys::kNumericDelegateProfiles] =
      Wire(value.NumericDelegateProfiles());
  result[observation_keys::kNewRequestedBytes] =
      Wire(value.NewRequestedBytes());
  return result;
}
[[maybe_unused]] nb::object Wire(const std::error_code& value) {
  nb::dict result;
  result[observation_keys::kCategory] = nb::str(value.category().name());
  result[observation_keys::kValue] =
      HexWord(static_cast<std::uint32_t>(value.value()));
  return result;
}
nb::dict PresetObservation(const OffGlbPreset& value) {
  nb::dict result;
  nb::dict config_926;
  config_926[observation_keys::kWorkerBudget] =
      nb::cast(value.execution.worker_budget);
  config_926[observation_keys::kMinimumParallelVertices] =
      nb::cast(value.execution.minimum_parallel_vertices);
  config_926[observation_keys::kTrackedPayloadBudgetBytes] =
      nb::cast(value.execution.tracked_payload_budget_bytes);
  result[observation_keys::kExecution] = config_926;
  nb::dict config_931;
  nb::dict config_932;
  config_932[observation_keys::kMaxEntryBytes] =
      nb::cast(value.capture_limits.files.max_entry_bytes);
  config_932[observation_keys::kMaxReceiptBytes] =
      nb::cast(value.capture_limits.files.max_receipt_bytes);
  config_932[observation_keys::kMaxTotalFileBytes] =
      nb::cast(value.capture_limits.files.max_total_file_bytes);
  config_932[observation_keys::kMaxEntryNameBytes] =
      nb::cast(value.capture_limits.files.max_entry_name_bytes);
  config_932[observation_keys::kMaxReceiptNameBytes] =
      nb::cast(value.capture_limits.files.max_receipt_name_bytes);
  config_932[observation_keys::kMaxNativePathCodeUnits] =
      nb::cast(value.capture_limits.files.max_native_path_code_units);
  config_932[observation_keys::kMaxFileNewRequestedBytes] =
      nb::cast(value.capture_limits.files.max_file_new_requested_bytes);
  config_931[observation_keys::kFiles] = config_932;
  nb::dict config_941;
  config_941[observation_keys::kMaxSourceBytes] =
      nb::cast(value.capture_limits.off.max_source_bytes);
  config_941[observation_keys::kMaxVertices] =
      nb::cast(value.capture_limits.off.max_vertices);
  config_941[observation_keys::kMaxFaces] =
      nb::cast(value.capture_limits.off.max_faces);
  config_941[observation_keys::kMaxCorners] =
      nb::cast(value.capture_limits.off.max_corners);
  config_941[observation_keys::kMaxCornersPerFace] =
      nb::cast(value.capture_limits.off.max_corners_per_face);
  config_941[observation_keys::kMaxLineBytes] =
      nb::cast(value.capture_limits.off.max_line_bytes);
  config_941[observation_keys::kMaxTokenBytes] =
      nb::cast(value.capture_limits.off.max_token_bytes);
  config_941[observation_keys::kMaxCommentBytes] =
      nb::cast(value.capture_limits.off.max_comment_bytes);
  config_941[observation_keys::kMaxCodecNewRequestedBytes] =
      nb::cast(value.capture_limits.off.max_codec_new_requested_bytes);
  config_941[observation_keys::kMaxTotalNewRequestedBytes] =
      nb::cast(value.capture_limits.off.max_total_new_requested_bytes);
  nb::dict config_952;
  config_952[observation_keys::kMaxVertices] =
      nb::cast(value.capture_limits.off.materialization.max_vertices);
  config_952[observation_keys::kMaxFaces] =
      nb::cast(value.capture_limits.off.materialization.max_faces);
  config_952[observation_keys::kMaxCorners] =
      nb::cast(value.capture_limits.off.materialization.max_corners);
  config_952[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.capture_limits.off.materialization.max_face_offsets);
  config_952[observation_keys::kMaxAttributes] =
      nb::cast(value.capture_limits.off.materialization.max_attributes);
  config_952[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.capture_limits.off.materialization.max_attribute_scalars);
  config_952[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.capture_limits.off.materialization.max_attribute_offsets);
  config_952[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.capture_limits.off.materialization.max_presence_bytes);
  config_952[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.capture_limits.off.materialization.max_metadata_entries);
  config_952[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.capture_limits.off.materialization.max_descriptor_bytes);
  config_952[observation_keys::kMaxNewRequestedBytes] = nb::cast(
      value.capture_limits.off.materialization.max_new_requested_bytes);
  config_941[observation_keys::kMaterialization] = config_952;
  nb::dict config_965;
  config_965[observation_keys::kMaxIssues] =
      nb::cast(value.capture_limits.off.inspection.max_issues);
  config_965[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.capture_limits.off.inspection.max_new_requested_bytes);
  config_941[observation_keys::kInspection] = config_965;
  config_931[observation_keys::kOff] = config_941;
  result[observation_keys::kCaptureLimits] = config_931;
  nb::dict config_971;
  nb::dict config_972;
  config_972[observation_keys::kIoBlockBytes] =
      nb::cast(value.capture_work.files.io_block_bytes);
  config_972[observation_keys::kParallelMinBytes] =
      nb::cast(value.capture_work.files.parallel_min_bytes);
  config_972[observation_keys::kPartitionBytes] =
      nb::cast(value.capture_work.files.partition_bytes);
  config_972[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.capture_work.files.cancellation_block_bytes);
  config_972[observation_keys::kMaxJobs] =
      nb::cast(value.capture_work.files.max_jobs);
  config_971[observation_keys::kFiles] = config_972;
  nb::dict config_979;
  config_979[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.capture_work.off.cancellation_block_bytes);
  config_979[observation_keys::kCancellationBlockTokens] =
      nb::cast(value.capture_work.off.cancellation_block_tokens);
  config_979[observation_keys::kCancellationBlockRecords] =
      nb::cast(value.capture_work.off.cancellation_block_records);
  nb::dict config_983;
  config_983[observation_keys::kParallelMinBytes] =
      nb::cast(value.capture_work.off.geometry.parallel_min_bytes);
  config_983[observation_keys::kPartitionBytes] =
      nb::cast(value.capture_work.off.geometry.partition_bytes);
  config_983[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.capture_work.off.geometry.cancellation_block_scalars);
  config_983[observation_keys::kMaxJobs] =
      nb::cast(value.capture_work.off.geometry.max_jobs);
  config_979[observation_keys::kGeometry] = config_983;
  config_971[observation_keys::kOff] = config_979;
  result[observation_keys::kCaptureWork] = config_971;
  nb::dict config_991;
  config_991[observation_keys::kMaxCornersPerFace] =
      nb::cast(value.triangulation_options.max_corners_per_face);
  config_991[observation_keys::kMinimumParallelFaces] =
      nb::cast(value.triangulation_options.minimum_parallel_faces);
  result[observation_keys::kTriangulationOptions] = config_991;
  nb::dict config_995;
  config_995[observation_keys::kMaxWrapperNewRequestedBytes] =
      nb::cast(value.limits.max_wrapper_new_requested_bytes);
  nb::dict config_997;
  config_997[observation_keys::kMaxWrapperNewRequestedBytes] =
      nb::cast(value.limits.off.max_wrapper_new_requested_bytes);
  config_997[observation_keys::kMaxInputIdBytes] =
      nb::cast(value.limits.off.max_input_id_bytes);
  config_997[observation_keys::kMaxSourceProtectionBytes] =
      nb::cast(value.limits.off.max_source_protection_bytes);
  config_997[observation_keys::kMaxReportBytes] =
      nb::cast(value.limits.off.max_report_bytes);
  config_997[observation_keys::kMaxHashInputBytes] =
      nb::cast(value.limits.off.max_hash_input_bytes);
  config_997[observation_keys::kMaxVerificationVertices] =
      nb::cast(value.limits.off.max_verification_vertices);
  config_997[observation_keys::kMaxVerificationFaces] =
      nb::cast(value.limits.off.max_verification_faces);
  config_997[observation_keys::kMaxVerificationCorners] =
      nb::cast(value.limits.off.max_verification_corners);
  nb::dict config_1006;
  config_1006[observation_keys::kMaxTargets] =
      nb::cast(value.limits.off.repair.max_targets);
  config_1006[observation_keys::kMaxRowLocalDeclarations] =
      nb::cast(value.limits.off.repair.max_row_local_declarations);
  config_1006[observation_keys::kMaxDeclarationNameBytes] =
      nb::cast(value.limits.off.repair.max_declaration_name_bytes);
  config_1006[observation_keys::kMaxComparisonStepsPerTarget] =
      nb::cast(value.limits.off.repair.max_comparison_steps_per_target);
  config_1006[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.off.repair.max_new_requested_bytes);
  nb::dict config_1012;
  config_1012[observation_keys::kMaxIssues] =
      nb::cast(value.limits.off.repair.inspection.max_issues);
  config_1012[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.off.repair.inspection.max_new_requested_bytes);
  config_1006[observation_keys::kInspection] = config_1012;
  nb::dict config_1016;
  config_1016[observation_keys::kMaxVertices] =
      nb::cast(value.limits.off.repair.selection.max_vertices);
  config_1016[observation_keys::kMaxFaces] =
      nb::cast(value.limits.off.repair.selection.max_faces);
  config_1016[observation_keys::kMaxCorners] =
      nb::cast(value.limits.off.repair.selection.max_corners);
  config_1016[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.limits.off.repair.selection.max_face_offsets);
  config_1016[observation_keys::kMaxAttributes] =
      nb::cast(value.limits.off.repair.selection.max_attributes);
  config_1016[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.limits.off.repair.selection.max_attribute_scalars);
  config_1016[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.limits.off.repair.selection.max_attribute_offsets);
  config_1016[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.limits.off.repair.selection.max_presence_bytes);
  config_1016[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.limits.off.repair.selection.max_metadata_entries);
  config_1016[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.limits.off.repair.selection.max_descriptor_bytes);
  config_1016[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.off.repair.selection.max_new_requested_bytes);
  config_1006[observation_keys::kSelection] = config_1016;
  config_997[observation_keys::kRepair] = config_1006;
  nb::dict config_1030;
  config_1030[observation_keys::kMaxOutputBytes] =
      nb::cast(value.limits.off.writing.max_output_bytes);
  config_1030[observation_keys::kMaxVertices] =
      nb::cast(value.limits.off.writing.max_vertices);
  config_1030[observation_keys::kMaxFaces] =
      nb::cast(value.limits.off.writing.max_faces);
  config_1030[observation_keys::kMaxCorners] =
      nb::cast(value.limits.off.writing.max_corners);
  config_1030[observation_keys::kMaxCornersPerFace] =
      nb::cast(value.limits.off.writing.max_corners_per_face);
  config_1030[observation_keys::kMaxLineBytes] =
      nb::cast(value.limits.off.writing.max_line_bytes);
  config_1030[observation_keys::kMaxTokenBytes] =
      nb::cast(value.limits.off.writing.max_token_bytes);
  config_1030[observation_keys::kMaxCodecNewRequestedBytes] =
      nb::cast(value.limits.off.writing.max_codec_new_requested_bytes);
  config_1030[observation_keys::kMaxTotalNewRequestedBytes] =
      nb::cast(value.limits.off.writing.max_total_new_requested_bytes);
  nb::dict config_1040;
  config_1040[observation_keys::kMaxIssues] =
      nb::cast(value.limits.off.writing.inspection.max_issues);
  config_1040[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.off.writing.inspection.max_new_requested_bytes);
  config_1030[observation_keys::kInspection] = config_1040;
  config_997[observation_keys::kWriting] = config_1030;
  nb::dict config_1045;
  config_1045[observation_keys::kMaxSourceBytes] =
      nb::cast(value.limits.off.reload.max_source_bytes);
  config_1045[observation_keys::kMaxVertices] =
      nb::cast(value.limits.off.reload.max_vertices);
  config_1045[observation_keys::kMaxFaces] =
      nb::cast(value.limits.off.reload.max_faces);
  config_1045[observation_keys::kMaxCorners] =
      nb::cast(value.limits.off.reload.max_corners);
  config_1045[observation_keys::kMaxCornersPerFace] =
      nb::cast(value.limits.off.reload.max_corners_per_face);
  config_1045[observation_keys::kMaxLineBytes] =
      nb::cast(value.limits.off.reload.max_line_bytes);
  config_1045[observation_keys::kMaxTokenBytes] =
      nb::cast(value.limits.off.reload.max_token_bytes);
  config_1045[observation_keys::kMaxCommentBytes] =
      nb::cast(value.limits.off.reload.max_comment_bytes);
  config_1045[observation_keys::kMaxCodecNewRequestedBytes] =
      nb::cast(value.limits.off.reload.max_codec_new_requested_bytes);
  config_1045[observation_keys::kMaxTotalNewRequestedBytes] =
      nb::cast(value.limits.off.reload.max_total_new_requested_bytes);
  nb::dict config_1056;
  config_1056[observation_keys::kMaxVertices] =
      nb::cast(value.limits.off.reload.materialization.max_vertices);
  config_1056[observation_keys::kMaxFaces] =
      nb::cast(value.limits.off.reload.materialization.max_faces);
  config_1056[observation_keys::kMaxCorners] =
      nb::cast(value.limits.off.reload.materialization.max_corners);
  config_1056[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.limits.off.reload.materialization.max_face_offsets);
  config_1056[observation_keys::kMaxAttributes] =
      nb::cast(value.limits.off.reload.materialization.max_attributes);
  config_1056[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.limits.off.reload.materialization.max_attribute_scalars);
  config_1056[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.limits.off.reload.materialization.max_attribute_offsets);
  config_1056[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.limits.off.reload.materialization.max_presence_bytes);
  config_1056[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.limits.off.reload.materialization.max_metadata_entries);
  config_1056[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.limits.off.reload.materialization.max_descriptor_bytes);
  config_1056[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.off.reload.materialization.max_new_requested_bytes);
  config_1045[observation_keys::kMaterialization] = config_1056;
  nb::dict config_1069;
  config_1069[observation_keys::kMaxIssues] =
      nb::cast(value.limits.off.reload.inspection.max_issues);
  config_1069[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.off.reload.inspection.max_new_requested_bytes);
  config_1045[observation_keys::kInspection] = config_1069;
  config_997[observation_keys::kReload] = config_1045;
  nb::dict config_1074;
  nb::dict config_1075;
  config_1075[observation_keys::kMaxEntryBytes] =
      nb::cast(value.limits.off.publication.files.max_entry_bytes);
  config_1075[observation_keys::kMaxReceiptBytes] =
      nb::cast(value.limits.off.publication.files.max_receipt_bytes);
  config_1075[observation_keys::kMaxTotalFileBytes] =
      nb::cast(value.limits.off.publication.files.max_total_file_bytes);
  config_1075[observation_keys::kMaxEntryNameBytes] =
      nb::cast(value.limits.off.publication.files.max_entry_name_bytes);
  config_1075[observation_keys::kMaxReceiptNameBytes] =
      nb::cast(value.limits.off.publication.files.max_receipt_name_bytes);
  config_1075[observation_keys::kMaxNativePathCodeUnits] =
      nb::cast(value.limits.off.publication.files.max_native_path_code_units);
  config_1075[observation_keys::kMaxFileNewRequestedBytes] =
      nb::cast(value.limits.off.publication.files.max_file_new_requested_bytes);
  config_1074[observation_keys::kFiles] = config_1075;
  nb::dict config_1084;
  config_1084[observation_keys::kMaxIssues] =
      nb::cast(value.limits.off.publication.expected_inspection.max_issues);
  config_1084[observation_keys::kMaxNewRequestedBytes] = nb::cast(
      value.limits.off.publication.expected_inspection.max_new_requested_bytes);
  config_1074[observation_keys::kExpectedInspection] = config_1084;
  nb::dict config_1088;
  config_1088[observation_keys::kMaxSourceBytes] =
      nb::cast(value.limits.off.publication.reload.max_source_bytes);
  config_1088[observation_keys::kMaxVertices] =
      nb::cast(value.limits.off.publication.reload.max_vertices);
  config_1088[observation_keys::kMaxFaces] =
      nb::cast(value.limits.off.publication.reload.max_faces);
  config_1088[observation_keys::kMaxCorners] =
      nb::cast(value.limits.off.publication.reload.max_corners);
  config_1088[observation_keys::kMaxCornersPerFace] =
      nb::cast(value.limits.off.publication.reload.max_corners_per_face);
  config_1088[observation_keys::kMaxLineBytes] =
      nb::cast(value.limits.off.publication.reload.max_line_bytes);
  config_1088[observation_keys::kMaxTokenBytes] =
      nb::cast(value.limits.off.publication.reload.max_token_bytes);
  config_1088[observation_keys::kMaxCommentBytes] =
      nb::cast(value.limits.off.publication.reload.max_comment_bytes);
  config_1088[observation_keys::kMaxCodecNewRequestedBytes] = nb::cast(
      value.limits.off.publication.reload.max_codec_new_requested_bytes);
  config_1088[observation_keys::kMaxTotalNewRequestedBytes] = nb::cast(
      value.limits.off.publication.reload.max_total_new_requested_bytes);
  nb::dict config_1099;
  config_1099[observation_keys::kMaxVertices] = nb::cast(
      value.limits.off.publication.reload.materialization.max_vertices);
  config_1099[observation_keys::kMaxFaces] =
      nb::cast(value.limits.off.publication.reload.materialization.max_faces);
  config_1099[observation_keys::kMaxCorners] =
      nb::cast(value.limits.off.publication.reload.materialization.max_corners);
  config_1099[observation_keys::kMaxFaceOffsets] = nb::cast(
      value.limits.off.publication.reload.materialization.max_face_offsets);
  config_1099[observation_keys::kMaxAttributes] = nb::cast(
      value.limits.off.publication.reload.materialization.max_attributes);
  config_1099[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.limits.off.publication.reload.materialization
                   .max_attribute_scalars);
  config_1099[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.limits.off.publication.reload.materialization
                   .max_attribute_offsets);
  config_1099[observation_keys::kMaxPresenceBytes] = nb::cast(
      value.limits.off.publication.reload.materialization.max_presence_bytes);
  config_1099[observation_keys::kMaxMetadataEntries] = nb::cast(
      value.limits.off.publication.reload.materialization.max_metadata_entries);
  config_1099[observation_keys::kMaxDescriptorBytes] = nb::cast(
      value.limits.off.publication.reload.materialization.max_descriptor_bytes);
  config_1099[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.off.publication.reload.materialization
                   .max_new_requested_bytes);
  config_1088[observation_keys::kMaterialization] = config_1099;
  nb::dict config_1112;
  config_1112[observation_keys::kMaxIssues] =
      nb::cast(value.limits.off.publication.reload.inspection.max_issues);
  config_1112[observation_keys::kMaxNewRequestedBytes] = nb::cast(
      value.limits.off.publication.reload.inspection.max_new_requested_bytes);
  config_1088[observation_keys::kInspection] = config_1112;
  config_1074[observation_keys::kReload] = config_1088;
  config_997[observation_keys::kPublication] = config_1074;
  config_995[observation_keys::kOff] = config_997;
  nb::dict config_1119;
  nb::dict config_1120;
  config_1120[observation_keys::kMaxVertices] =
      nb::cast(value.limits.triangulation.source.max_vertices);
  config_1120[observation_keys::kMaxFaces] =
      nb::cast(value.limits.triangulation.source.max_faces);
  config_1120[observation_keys::kMaxCorners] =
      nb::cast(value.limits.triangulation.source.max_corners);
  config_1120[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.limits.triangulation.source.max_face_offsets);
  config_1120[observation_keys::kMaxAttributes] =
      nb::cast(value.limits.triangulation.source.max_attributes);
  config_1120[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.limits.triangulation.source.max_attribute_scalars);
  config_1120[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.limits.triangulation.source.max_attribute_offsets);
  config_1120[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.limits.triangulation.source.max_presence_bytes);
  config_1120[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.limits.triangulation.source.max_metadata_entries);
  config_1120[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.limits.triangulation.source.max_descriptor_bytes);
  config_1120[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.triangulation.source.max_new_requested_bytes);
  config_1119[observation_keys::kSource] = config_1120;
  nb::dict config_1133;
  config_1133[observation_keys::kMaxIssues] =
      nb::cast(value.limits.triangulation.source_inspection.max_issues);
  config_1133[observation_keys::kMaxNewRequestedBytes] = nb::cast(
      value.limits.triangulation.source_inspection.max_new_requested_bytes);
  config_1119[observation_keys::kSourceInspection] = config_1133;
  nb::dict config_1137;
  config_1137[observation_keys::kMaxVertices] =
      nb::cast(value.limits.triangulation.output.max_vertices);
  config_1137[observation_keys::kMaxFaces] =
      nb::cast(value.limits.triangulation.output.max_faces);
  config_1137[observation_keys::kMaxCorners] =
      nb::cast(value.limits.triangulation.output.max_corners);
  config_1137[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.limits.triangulation.output.max_face_offsets);
  config_1137[observation_keys::kMaxAttributes] =
      nb::cast(value.limits.triangulation.output.max_attributes);
  config_1137[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.limits.triangulation.output.max_attribute_scalars);
  config_1137[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.limits.triangulation.output.max_attribute_offsets);
  config_1137[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.limits.triangulation.output.max_presence_bytes);
  config_1137[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.limits.triangulation.output.max_metadata_entries);
  config_1137[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.limits.triangulation.output.max_descriptor_bytes);
  config_1137[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.triangulation.output.max_new_requested_bytes);
  config_1119[observation_keys::kOutput] = config_1137;
  nb::dict config_1150;
  config_1150[observation_keys::kMaxIssues] =
      nb::cast(value.limits.triangulation.output_inspection.max_issues);
  config_1150[observation_keys::kMaxNewRequestedBytes] = nb::cast(
      value.limits.triangulation.output_inspection.max_new_requested_bytes);
  config_1119[observation_keys::kOutputInspection] = config_1150;
  config_1119[observation_keys::kMaxMapEntries] =
      nb::cast(value.limits.triangulation.max_map_entries);
  config_1119[observation_keys::kMaxCoreNewRequestedBytes] =
      nb::cast(value.limits.triangulation.max_core_new_requested_bytes);
  config_1119[observation_keys::kMaxBridgeNewRequestedBytes] =
      nb::cast(value.limits.triangulation.max_bridge_new_requested_bytes);
  config_995[observation_keys::kTriangulation] = config_1119;
  nb::dict config_1158;
  nb::dict config_1159;
  config_1159[observation_keys::kMaxVertices] =
      nb::cast(value.limits.encoding.counts.max_vertices);
  config_1159[observation_keys::kMaxFaces] =
      nb::cast(value.limits.encoding.counts.max_faces);
  config_1159[observation_keys::kMaxCorners] =
      nb::cast(value.limits.encoding.counts.max_corners);
  config_1159[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.limits.encoding.counts.max_face_offsets);
  config_1159[observation_keys::kMaxAttributes] =
      nb::cast(value.limits.encoding.counts.max_attributes);
  config_1159[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.limits.encoding.counts.max_attribute_scalars);
  config_1159[observation_keys::kMaxAttributeBytes] =
      nb::cast(value.limits.encoding.counts.max_attribute_bytes);
  config_1159[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.limits.encoding.counts.max_attribute_offsets);
  config_1159[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.limits.encoding.counts.max_presence_bytes);
  config_1159[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.limits.encoding.counts.max_metadata_entries);
  config_1159[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.limits.encoding.counts.max_descriptor_bytes);
  config_1159[observation_keys::kMaxWirePrimitives] =
      nb::cast(value.limits.encoding.counts.max_wire_primitives);
  config_1159[observation_keys::kMaxWireVertices] =
      nb::cast(value.limits.encoding.counts.max_wire_vertices);
  config_1159[observation_keys::kMaxWireCorners] =
      nb::cast(value.limits.encoding.counts.max_wire_corners);
  config_1159[observation_keys::kMaxMapEntries] =
      nb::cast(value.limits.encoding.counts.max_map_entries);
  config_1159[observation_keys::kMaxMembers] =
      nb::cast(value.limits.encoding.counts.max_members);
  config_1159[observation_keys::kMaxMemberBytes] =
      nb::cast(value.limits.encoding.counts.max_member_bytes);
  config_1159[observation_keys::kMaxTotalMemberBytes] =
      nb::cast(value.limits.encoding.counts.max_total_member_bytes);
  config_1159[observation_keys::kMaxResources] =
      nb::cast(value.limits.encoding.counts.max_resources);
  config_1159[observation_keys::kMaxResourceBytes] =
      nb::cast(value.limits.encoding.counts.max_resource_bytes);
  config_1159[observation_keys::kMaxJsonBytes] =
      nb::cast(value.limits.encoding.counts.max_json_bytes);
  config_1159[observation_keys::kMaxBinaryBytes] =
      nb::cast(value.limits.encoding.counts.max_binary_bytes);
  config_1159[observation_keys::kMaxCapsuleJsonBytes] =
      nb::cast(value.limits.encoding.counts.max_capsule_json_bytes);
  config_1159[observation_keys::kMaxCapsuleBinaryBytes] =
      nb::cast(value.limits.encoding.counts.max_capsule_binary_bytes);
  config_1159[observation_keys::kMaxMaterialBindings] =
      nb::cast(value.limits.encoding.counts.max_material_bindings);
  config_1159[observation_keys::kMaxNumericTokenBytes] =
      nb::cast(value.limits.encoding.counts.max_numeric_token_bytes);
  config_1159[observation_keys::kMaxJsonNodes] =
      nb::cast(value.limits.encoding.counts.max_json_nodes);
  config_1159[observation_keys::kMaxJsonDepth] =
      nb::cast(value.limits.encoding.counts.max_json_depth);
  config_1159[observation_keys::kMaxNumericIntegerBits] =
      nb::cast(value.limits.encoding.counts.max_numeric_integer_bits);
  config_1159[observation_keys::kMaxCoincidenceComparisons] =
      nb::cast(value.limits.encoding.counts.max_coincidence_comparisons);
  config_1159[observation_keys::kMaxRelationshipSteps] =
      nb::cast(value.limits.encoding.counts.max_relationship_steps);
  config_1158[observation_keys::kCounts] = config_1159;
  config_1158[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.encoding.max_new_requested_bytes);
  config_1158[observation_keys::kMaxTotalRequestedAllowance] =
      nb::cast(value.limits.encoding.max_total_requested_allowance);
  nb::dict config_1194;
  nb::dict config_1195;
  config_1195[observation_keys::kMaxContainerBytes] = nb::cast(
      std::get<ic::GltfSurfaceEncodeLimits>(value.limits.encoding.delegates)
          .container.max_container_bytes);
  config_1195[observation_keys::kMaxChunks] = nb::cast(
      std::get<ic::GltfSurfaceEncodeLimits>(value.limits.encoding.delegates)
          .container.max_chunks);
  config_1195[observation_keys::kMaxRequestedBytes] = nb::cast(
      std::get<ic::GltfSurfaceEncodeLimits>(value.limits.encoding.delegates)
          .container.max_requested_bytes);
  config_1194[observation_keys::kContainer] = config_1195;
  config_1158[observation_keys::kDelegates] = config_1194;
  config_995[observation_keys::kEncoding] = config_1158;
  nb::dict config_1202;
  nb::dict config_1203;
  config_1203[observation_keys::kMaxVertices] =
      nb::cast(value.limits.inspection.counts.max_vertices);
  config_1203[observation_keys::kMaxFaces] =
      nb::cast(value.limits.inspection.counts.max_faces);
  config_1203[observation_keys::kMaxCorners] =
      nb::cast(value.limits.inspection.counts.max_corners);
  config_1203[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.limits.inspection.counts.max_face_offsets);
  config_1203[observation_keys::kMaxAttributes] =
      nb::cast(value.limits.inspection.counts.max_attributes);
  config_1203[observation_keys::kMaxAttributeScalars] =
      nb::cast(value.limits.inspection.counts.max_attribute_scalars);
  config_1203[observation_keys::kMaxAttributeBytes] =
      nb::cast(value.limits.inspection.counts.max_attribute_bytes);
  config_1203[observation_keys::kMaxAttributeOffsets] =
      nb::cast(value.limits.inspection.counts.max_attribute_offsets);
  config_1203[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.limits.inspection.counts.max_presence_bytes);
  config_1203[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.limits.inspection.counts.max_metadata_entries);
  config_1203[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.limits.inspection.counts.max_descriptor_bytes);
  config_1203[observation_keys::kMaxWirePrimitives] =
      nb::cast(value.limits.inspection.counts.max_wire_primitives);
  config_1203[observation_keys::kMaxWireVertices] =
      nb::cast(value.limits.inspection.counts.max_wire_vertices);
  config_1203[observation_keys::kMaxWireCorners] =
      nb::cast(value.limits.inspection.counts.max_wire_corners);
  config_1203[observation_keys::kMaxMapEntries] =
      nb::cast(value.limits.inspection.counts.max_map_entries);
  config_1203[observation_keys::kMaxMembers] =
      nb::cast(value.limits.inspection.counts.max_members);
  config_1203[observation_keys::kMaxMemberBytes] =
      nb::cast(value.limits.inspection.counts.max_member_bytes);
  config_1203[observation_keys::kMaxTotalMemberBytes] =
      nb::cast(value.limits.inspection.counts.max_total_member_bytes);
  config_1203[observation_keys::kMaxResources] =
      nb::cast(value.limits.inspection.counts.max_resources);
  config_1203[observation_keys::kMaxResourceBytes] =
      nb::cast(value.limits.inspection.counts.max_resource_bytes);
  config_1203[observation_keys::kMaxJsonBytes] =
      nb::cast(value.limits.inspection.counts.max_json_bytes);
  config_1203[observation_keys::kMaxBinaryBytes] =
      nb::cast(value.limits.inspection.counts.max_binary_bytes);
  config_1203[observation_keys::kMaxCapsuleJsonBytes] =
      nb::cast(value.limits.inspection.counts.max_capsule_json_bytes);
  config_1203[observation_keys::kMaxCapsuleBinaryBytes] =
      nb::cast(value.limits.inspection.counts.max_capsule_binary_bytes);
  config_1203[observation_keys::kMaxMaterialBindings] =
      nb::cast(value.limits.inspection.counts.max_material_bindings);
  config_1203[observation_keys::kMaxNumericTokenBytes] =
      nb::cast(value.limits.inspection.counts.max_numeric_token_bytes);
  config_1203[observation_keys::kMaxJsonNodes] =
      nb::cast(value.limits.inspection.counts.max_json_nodes);
  config_1203[observation_keys::kMaxJsonDepth] =
      nb::cast(value.limits.inspection.counts.max_json_depth);
  config_1203[observation_keys::kMaxNumericIntegerBits] =
      nb::cast(value.limits.inspection.counts.max_numeric_integer_bits);
  config_1203[observation_keys::kMaxCoincidenceComparisons] =
      nb::cast(value.limits.inspection.counts.max_coincidence_comparisons);
  config_1203[observation_keys::kMaxRelationshipSteps] =
      nb::cast(value.limits.inspection.counts.max_relationship_steps);
  config_1202[observation_keys::kCounts] = config_1203;
  config_1202[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.inspection.max_new_requested_bytes);
  config_1202[observation_keys::kMaxTotalRequestedAllowance] =
      nb::cast(value.limits.inspection.max_total_requested_allowance);
  nb::dict config_1238;
  nb::dict config_1239;
  config_1239[observation_keys::kMaxContainerBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .container.max_container_bytes);
  config_1239[observation_keys::kMaxChunks] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .container.max_chunks);
  config_1239[observation_keys::kMaxRequestedBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .container.max_requested_bytes);
  config_1238[observation_keys::kContainer] = config_1239;
  nb::dict config_1244;
  nb::dict config_1245;
  config_1245[observation_keys::kMaxJsonBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_json_bytes);
  config_1245[observation_keys::kMaxResourceBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_resource_bytes);
  config_1245[observation_keys::kMaxOwnedSourceResourceBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_owned_source_resource_bytes);
  config_1245[observation_keys::kMaxDocumentNodes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_document_nodes);
  config_1245[observation_keys::kMaxDocumentDepth] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_document_depth);
  config_1245[observation_keys::kMaxDecodedStringBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_decoded_string_bytes);
  config_1245[observation_keys::kMaxNumericTokenBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_numeric_token_bytes);
  config_1245[observation_keys::kMaxDecimalCoefficientDigits] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_decimal_coefficient_digits);
  config_1245[observation_keys::kMaxDecimalExponentDigits] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_decimal_exponent_digits);
  config_1245[observation_keys::kMaxTableRows] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_table_rows);
  config_1245[observation_keys::kMaxGraphEdges] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_graph_edges);
  config_1245[observation_keys::kMaxInstances] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_instances);
  config_1245[observation_keys::kMaxAccessorElements] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_accessor_elements);
  config_1245[observation_keys::kMaxProjectionBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.source.max_projection_bytes);
  config_1244[observation_keys::kSource] = config_1245;
  config_1244[observation_keys::kMaxNewRequestedBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .read.max_new_requested_bytes);
  config_1238[observation_keys::kRead] = config_1244;
  nb::dict config_1263;
  config_1263[observation_keys::kMaxComponents] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.max_components);
  config_1263[observation_keys::kMaxTriangleCorners] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.max_triangle_corners);
  config_1263[observation_keys::kMaxOutputRequestedBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.max_output_requested_bytes);
  nb::dict config_1267;
  config_1267[observation_keys::kMaxNodeRows] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_node_rows);
  config_1267[observation_keys::kMaxMeshUsers] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_mesh_users);
  config_1267[observation_keys::kMaxChannelReferences] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_channel_references);
  config_1267[observation_keys::kMaxDistinctSamplers] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_distinct_samplers);
  config_1267[observation_keys::kMaxKeysPerSampler] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_keys_per_sampler);
  config_1267[observation_keys::kMaxTotalKeys] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_total_keys);
  config_1267[observation_keys::kMaxRelationshipSteps] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_relationship_steps);
  config_1267[observation_keys::kMaxMetadataBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_metadata_bytes);
  config_1267[observation_keys::kMaxDecodedBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_decoded_bytes);
  config_1267[observation_keys::kMaxNewRequestedBytes] = nb::cast(
      std::get<ic::GltfSurfaceInspectLimits>(value.limits.inspection.delegates)
          .extraction.direct_translation.max_new_requested_bytes);
  config_1263[observation_keys::kDirectTranslation] = config_1267;
  config_1238[observation_keys::kExtraction] = config_1263;
  config_1202[observation_keys::kDelegates] = config_1238;
  config_995[observation_keys::kInspection] = config_1202;
  nb::dict config_1282;
  nb::dict config_1283;
  config_1283[observation_keys::kMaxPayloadMembers] =
      nb::cast(value.limits.publication.files.max_payload_members);
  config_1283[observation_keys::kMaxPayloadMemberBytes] =
      nb::cast(value.limits.publication.files.max_payload_member_bytes);
  config_1283[observation_keys::kMaxTotalPayloadBytes] =
      nb::cast(value.limits.publication.files.max_total_payload_bytes);
  config_1283[observation_keys::kMaxReceiptBytes] =
      nb::cast(value.limits.publication.files.max_receipt_bytes);
  config_1283[observation_keys::kMaxCallerReportBytes] =
      nb::cast(value.limits.publication.files.max_caller_report_bytes);
  config_1283[observation_keys::kMaxPathBytes] =
      nb::cast(value.limits.publication.files.max_path_bytes);
  config_1283[observation_keys::kMaxTotalPathBytes] =
      nb::cast(value.limits.publication.files.max_total_path_bytes);
  config_1283[observation_keys::kMaxNativePathCodeUnits] =
      nb::cast(value.limits.publication.files.max_native_path_code_units);
  config_1283[observation_keys::kMaxInventoryEntries] =
      nb::cast(value.limits.publication.files.max_inventory_entries);
  config_1283[observation_keys::kMaxStageNameAttempts] =
      nb::cast(value.limits.publication.files.max_stage_name_attempts);
  config_1283[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.publication.files.max_new_requested_bytes);
  config_1282[observation_keys::kFiles] = config_1283;
  nb::dict config_1296;
  nb::dict config_1297;
  config_1297[observation_keys::kMaxVertices] =
      nb::cast(value.limits.publication.inspection.counts.max_vertices);
  config_1297[observation_keys::kMaxFaces] =
      nb::cast(value.limits.publication.inspection.counts.max_faces);
  config_1297[observation_keys::kMaxCorners] =
      nb::cast(value.limits.publication.inspection.counts.max_corners);
  config_1297[observation_keys::kMaxFaceOffsets] =
      nb::cast(value.limits.publication.inspection.counts.max_face_offsets);
  config_1297[observation_keys::kMaxAttributes] =
      nb::cast(value.limits.publication.inspection.counts.max_attributes);
  config_1297[observation_keys::kMaxAttributeScalars] = nb::cast(
      value.limits.publication.inspection.counts.max_attribute_scalars);
  config_1297[observation_keys::kMaxAttributeBytes] =
      nb::cast(value.limits.publication.inspection.counts.max_attribute_bytes);
  config_1297[observation_keys::kMaxAttributeOffsets] = nb::cast(
      value.limits.publication.inspection.counts.max_attribute_offsets);
  config_1297[observation_keys::kMaxPresenceBytes] =
      nb::cast(value.limits.publication.inspection.counts.max_presence_bytes);
  config_1297[observation_keys::kMaxMetadataEntries] =
      nb::cast(value.limits.publication.inspection.counts.max_metadata_entries);
  config_1297[observation_keys::kMaxDescriptorBytes] =
      nb::cast(value.limits.publication.inspection.counts.max_descriptor_bytes);
  config_1297[observation_keys::kMaxWirePrimitives] =
      nb::cast(value.limits.publication.inspection.counts.max_wire_primitives);
  config_1297[observation_keys::kMaxWireVertices] =
      nb::cast(value.limits.publication.inspection.counts.max_wire_vertices);
  config_1297[observation_keys::kMaxWireCorners] =
      nb::cast(value.limits.publication.inspection.counts.max_wire_corners);
  config_1297[observation_keys::kMaxMapEntries] =
      nb::cast(value.limits.publication.inspection.counts.max_map_entries);
  config_1297[observation_keys::kMaxMembers] =
      nb::cast(value.limits.publication.inspection.counts.max_members);
  config_1297[observation_keys::kMaxMemberBytes] =
      nb::cast(value.limits.publication.inspection.counts.max_member_bytes);
  config_1297[observation_keys::kMaxTotalMemberBytes] = nb::cast(
      value.limits.publication.inspection.counts.max_total_member_bytes);
  config_1297[observation_keys::kMaxResources] =
      nb::cast(value.limits.publication.inspection.counts.max_resources);
  config_1297[observation_keys::kMaxResourceBytes] =
      nb::cast(value.limits.publication.inspection.counts.max_resource_bytes);
  config_1297[observation_keys::kMaxJsonBytes] =
      nb::cast(value.limits.publication.inspection.counts.max_json_bytes);
  config_1297[observation_keys::kMaxBinaryBytes] =
      nb::cast(value.limits.publication.inspection.counts.max_binary_bytes);
  config_1297[observation_keys::kMaxCapsuleJsonBytes] = nb::cast(
      value.limits.publication.inspection.counts.max_capsule_json_bytes);
  config_1297[observation_keys::kMaxCapsuleBinaryBytes] = nb::cast(
      value.limits.publication.inspection.counts.max_capsule_binary_bytes);
  config_1297[observation_keys::kMaxMaterialBindings] = nb::cast(
      value.limits.publication.inspection.counts.max_material_bindings);
  config_1297[observation_keys::kMaxNumericTokenBytes] = nb::cast(
      value.limits.publication.inspection.counts.max_numeric_token_bytes);
  config_1297[observation_keys::kMaxJsonNodes] =
      nb::cast(value.limits.publication.inspection.counts.max_json_nodes);
  config_1297[observation_keys::kMaxJsonDepth] =
      nb::cast(value.limits.publication.inspection.counts.max_json_depth);
  config_1297[observation_keys::kMaxNumericIntegerBits] = nb::cast(
      value.limits.publication.inspection.counts.max_numeric_integer_bits);
  config_1297[observation_keys::kMaxCoincidenceComparisons] = nb::cast(
      value.limits.publication.inspection.counts.max_coincidence_comparisons);
  config_1297[observation_keys::kMaxRelationshipSteps] = nb::cast(
      value.limits.publication.inspection.counts.max_relationship_steps);
  config_1296[observation_keys::kCounts] = config_1297;
  config_1296[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(value.limits.publication.inspection.max_new_requested_bytes);
  config_1296[observation_keys::kMaxTotalRequestedAllowance] = nb::cast(
      value.limits.publication.inspection.max_total_requested_allowance);
  nb::dict config_1332;
  nb::dict config_1333;
  config_1333[observation_keys::kMaxContainerBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .container.max_container_bytes);
  config_1333[observation_keys::kMaxChunks] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .container.max_chunks);
  config_1333[observation_keys::kMaxRequestedBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .container.max_requested_bytes);
  config_1332[observation_keys::kContainer] = config_1333;
  nb::dict config_1338;
  nb::dict config_1339;
  config_1339[observation_keys::kMaxJsonBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_json_bytes);
  config_1339[observation_keys::kMaxResourceBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_resource_bytes);
  config_1339[observation_keys::kMaxOwnedSourceResourceBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_owned_source_resource_bytes);
  config_1339[observation_keys::kMaxDocumentNodes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_document_nodes);
  config_1339[observation_keys::kMaxDocumentDepth] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_document_depth);
  config_1339[observation_keys::kMaxDecodedStringBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_decoded_string_bytes);
  config_1339[observation_keys::kMaxNumericTokenBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_numeric_token_bytes);
  config_1339[observation_keys::kMaxDecimalCoefficientDigits] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_decimal_coefficient_digits);
  config_1339[observation_keys::kMaxDecimalExponentDigits] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_decimal_exponent_digits);
  config_1339[observation_keys::kMaxTableRows] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_table_rows);
  config_1339[observation_keys::kMaxGraphEdges] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_graph_edges);
  config_1339[observation_keys::kMaxInstances] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_instances);
  config_1339[observation_keys::kMaxAccessorElements] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_accessor_elements);
  config_1339[observation_keys::kMaxProjectionBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.source.max_projection_bytes);
  config_1338[observation_keys::kSource] = config_1339;
  config_1338[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .read.max_new_requested_bytes);
  config_1332[observation_keys::kRead] = config_1338;
  nb::dict config_1357;
  config_1357[observation_keys::kMaxComponents] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.max_components);
  config_1357[observation_keys::kMaxTriangleCorners] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.max_triangle_corners);
  config_1357[observation_keys::kMaxOutputRequestedBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.max_output_requested_bytes);
  nb::dict config_1361;
  config_1361[observation_keys::kMaxNodeRows] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_node_rows);
  config_1361[observation_keys::kMaxMeshUsers] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_mesh_users);
  config_1361[observation_keys::kMaxChannelReferences] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_channel_references);
  config_1361[observation_keys::kMaxDistinctSamplers] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_distinct_samplers);
  config_1361[observation_keys::kMaxKeysPerSampler] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_keys_per_sampler);
  config_1361[observation_keys::kMaxTotalKeys] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_total_keys);
  config_1361[observation_keys::kMaxRelationshipSteps] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_relationship_steps);
  config_1361[observation_keys::kMaxMetadataBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_metadata_bytes);
  config_1361[observation_keys::kMaxDecodedBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_decoded_bytes);
  config_1361[observation_keys::kMaxNewRequestedBytes] =
      nb::cast(std::get<ic::GltfSurfaceInspectLimits>(
                   value.limits.publication.inspection.delegates)
                   .extraction.direct_translation.max_new_requested_bytes);
  config_1357[observation_keys::kDirectTranslation] = config_1361;
  config_1332[observation_keys::kExtraction] = config_1357;
  config_1296[observation_keys::kDelegates] = config_1332;
  config_1282[observation_keys::kInspection] = config_1296;
  config_1282[observation_keys::kMaxTotalRequestedAllowance] =
      nb::cast(value.limits.publication.max_total_requested_allowance);
  config_995[observation_keys::kPublication] = config_1282;
  result[observation_keys::kLimits] = config_995;
  nb::dict config_1379;
  nb::dict config_1380;
  nb::dict config_1381;
  config_1381[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.repair.parallel_min_bytes);
  config_1381[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.repair.partition_bytes);
  config_1381[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.off.repair.cancellation_block_scalars);
  config_1381[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.repair.max_jobs);
  config_1380[observation_keys::kRepair] = config_1381;
  nb::dict config_1387;
  config_1387[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.off.writing.cancellation_block_bytes);
  config_1387[observation_keys::kCancellationBlockTokens] =
      nb::cast(value.work.off.writing.cancellation_block_tokens);
  config_1387[observation_keys::kCancellationBlockRecords] =
      nb::cast(value.work.off.writing.cancellation_block_records);
  nb::dict config_1391;
  config_1391[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.writing.geometry.parallel_min_bytes);
  config_1391[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.writing.geometry.partition_bytes);
  config_1391[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.off.writing.geometry.cancellation_block_scalars);
  config_1391[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.writing.geometry.max_jobs);
  config_1387[observation_keys::kGeometry] = config_1391;
  config_1380[observation_keys::kWriting] = config_1387;
  nb::dict config_1398;
  config_1398[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.off.reload.cancellation_block_bytes);
  config_1398[observation_keys::kCancellationBlockTokens] =
      nb::cast(value.work.off.reload.cancellation_block_tokens);
  config_1398[observation_keys::kCancellationBlockRecords] =
      nb::cast(value.work.off.reload.cancellation_block_records);
  nb::dict config_1402;
  config_1402[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.reload.geometry.parallel_min_bytes);
  config_1402[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.reload.geometry.partition_bytes);
  config_1402[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.off.reload.geometry.cancellation_block_scalars);
  config_1402[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.reload.geometry.max_jobs);
  config_1398[observation_keys::kGeometry] = config_1402;
  config_1380[observation_keys::kReload] = config_1398;
  nb::dict config_1409;
  config_1409[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.verification.parallel_min_bytes);
  config_1409[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.verification.partition_bytes);
  config_1409[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.off.verification.cancellation_block_bytes);
  config_1409[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.verification.max_jobs);
  config_1380[observation_keys::kVerification] = config_1409;
  nb::dict config_1415;
  config_1415[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.hashing.parallel_min_bytes);
  config_1415[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.hashing.partition_bytes);
  config_1415[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.off.hashing.cancellation_block_bytes);
  config_1415[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.hashing.max_jobs);
  config_1380[observation_keys::kHashing] = config_1415;
  nb::dict config_1421;
  config_1421[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.report.parallel_min_bytes);
  config_1421[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.report.partition_bytes);
  config_1421[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.off.report.cancellation_block_bytes);
  config_1421[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.report.max_jobs);
  config_1380[observation_keys::kReport] = config_1421;
  nb::dict config_1427;
  nb::dict config_1428;
  config_1428[observation_keys::kIoBlockBytes] =
      nb::cast(value.work.off.publication.files.io_block_bytes);
  config_1428[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.publication.files.parallel_min_bytes);
  config_1428[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.publication.files.partition_bytes);
  config_1428[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.off.publication.files.cancellation_block_bytes);
  config_1428[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.publication.files.max_jobs);
  config_1427[observation_keys::kFiles] = config_1428;
  nb::dict config_1435;
  config_1435[observation_keys::kParallelMinBytes] = nb::cast(
      value.work.off.publication.expected_inspection.parallel_min_bytes);
  config_1435[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.publication.expected_inspection.partition_bytes);
  config_1435[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.off.publication.expected_inspection
                   .cancellation_block_scalars);
  config_1435[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.publication.expected_inspection.max_jobs);
  config_1427[observation_keys::kExpectedInspection] = config_1435;
  nb::dict config_1441;
  config_1441[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.off.publication.reload.cancellation_block_bytes);
  config_1441[observation_keys::kCancellationBlockTokens] =
      nb::cast(value.work.off.publication.reload.cancellation_block_tokens);
  config_1441[observation_keys::kCancellationBlockRecords] =
      nb::cast(value.work.off.publication.reload.cancellation_block_records);
  nb::dict config_1445;
  config_1445[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.off.publication.reload.geometry.parallel_min_bytes);
  config_1445[observation_keys::kPartitionBytes] =
      nb::cast(value.work.off.publication.reload.geometry.partition_bytes);
  config_1445[observation_keys::kCancellationBlockScalars] = nb::cast(
      value.work.off.publication.reload.geometry.cancellation_block_scalars);
  config_1445[observation_keys::kMaxJobs] =
      nb::cast(value.work.off.publication.reload.geometry.max_jobs);
  config_1441[observation_keys::kGeometry] = config_1445;
  config_1427[observation_keys::kReload] = config_1441;
  config_1380[observation_keys::kPublication] = config_1427;
  config_1379[observation_keys::kOff] = config_1380;
  nb::dict config_1454;
  config_1454[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.triangulation.parallel_min_bytes);
  config_1454[observation_keys::kPartitionBytes] =
      nb::cast(value.work.triangulation.partition_bytes);
  config_1454[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.triangulation.cancellation_block_scalars);
  config_1454[observation_keys::kMaxJobs] =
      nb::cast(value.work.triangulation.max_jobs);
  config_1379[observation_keys::kTriangulation] = config_1454;
  nb::dict config_1460;
  nb::dict config_1461;
  config_1461[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.encoding.surface.parallel_min_bytes);
  config_1461[observation_keys::kPartitionBytes] =
      nb::cast(value.work.encoding.surface.partition_bytes);
  config_1461[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.encoding.surface.cancellation_block_scalars);
  config_1461[observation_keys::kMaxJobs] =
      nb::cast(value.work.encoding.surface.max_jobs);
  config_1460[observation_keys::kSurface] = config_1461;
  nb::dict config_1467;
  config_1467[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.encoding.container.parallel_min_bytes);
  config_1467[observation_keys::kPartitionBytes] =
      nb::cast(value.work.encoding.container.partition_bytes);
  config_1460[observation_keys::kContainer] = config_1467;
  nb::dict config_1471;
  config_1471[observation_keys::kParallelMinResourceBytes] =
      nb::cast(value.work.encoding.gltf.parallel_min_resource_bytes);
  config_1471[observation_keys::kResourcePartitionBytes] =
      nb::cast(value.work.encoding.gltf.resource_partition_bytes);
  config_1471[observation_keys::kParallelMinChannelComponents] =
      nb::cast(value.work.encoding.gltf.parallel_min_channel_components);
  config_1471[observation_keys::kChannelPartitionElements] =
      nb::cast(value.work.encoding.gltf.channel_partition_elements);
  nb::dict config_1476;
  config_1476[observation_keys::kParallelMinKeys] =
      nb::cast(value.work.encoding.gltf.direct_translation.parallel_min_keys);
  config_1476[observation_keys::kPartitionKeys] =
      nb::cast(value.work.encoding.gltf.direct_translation.partition_keys);
  config_1476[observation_keys::kCancellationBlockKeys] = nb::cast(
      value.work.encoding.gltf.direct_translation.cancellation_block_keys);
  config_1476[observation_keys::kMaxJobs] =
      nb::cast(value.work.encoding.gltf.direct_translation.max_jobs);
  config_1471[observation_keys::kDirectTranslation] = config_1476;
  config_1460[observation_keys::kGltf] = config_1471;
  nb::dict config_1483;
  config_1483[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.encoding.stl.cancellation_block_bytes);
  config_1483[observation_keys::kCancellationBlockTokens] =
      nb::cast(value.work.encoding.stl.cancellation_block_tokens);
  config_1483[observation_keys::kCancellationBlockFacets] =
      nb::cast(value.work.encoding.stl.cancellation_block_facets);
  nb::dict config_1487;
  config_1487[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.encoding.stl.geometry.parallel_min_bytes);
  config_1487[observation_keys::kPartitionBytes] =
      nb::cast(value.work.encoding.stl.geometry.partition_bytes);
  config_1487[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.encoding.stl.geometry.cancellation_block_scalars);
  config_1487[observation_keys::kMaxJobs] =
      nb::cast(value.work.encoding.stl.geometry.max_jobs);
  config_1483[observation_keys::kGeometry] = config_1487;
  config_1460[observation_keys::kStl] = config_1483;
  config_1379[observation_keys::kEncoding] = config_1460;
  nb::dict config_1495;
  nb::dict config_1496;
  config_1496[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.inspection.surface.parallel_min_bytes);
  config_1496[observation_keys::kPartitionBytes] =
      nb::cast(value.work.inspection.surface.partition_bytes);
  config_1496[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.inspection.surface.cancellation_block_scalars);
  config_1496[observation_keys::kMaxJobs] =
      nb::cast(value.work.inspection.surface.max_jobs);
  config_1495[observation_keys::kSurface] = config_1496;
  nb::dict config_1502;
  config_1502[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.inspection.container.parallel_min_bytes);
  config_1502[observation_keys::kPartitionBytes] =
      nb::cast(value.work.inspection.container.partition_bytes);
  config_1495[observation_keys::kContainer] = config_1502;
  nb::dict config_1506;
  config_1506[observation_keys::kParallelMinResourceBytes] =
      nb::cast(value.work.inspection.gltf.parallel_min_resource_bytes);
  config_1506[observation_keys::kResourcePartitionBytes] =
      nb::cast(value.work.inspection.gltf.resource_partition_bytes);
  config_1506[observation_keys::kParallelMinChannelComponents] =
      nb::cast(value.work.inspection.gltf.parallel_min_channel_components);
  config_1506[observation_keys::kChannelPartitionElements] =
      nb::cast(value.work.inspection.gltf.channel_partition_elements);
  nb::dict config_1511;
  config_1511[observation_keys::kParallelMinKeys] =
      nb::cast(value.work.inspection.gltf.direct_translation.parallel_min_keys);
  config_1511[observation_keys::kPartitionKeys] =
      nb::cast(value.work.inspection.gltf.direct_translation.partition_keys);
  config_1511[observation_keys::kCancellationBlockKeys] = nb::cast(
      value.work.inspection.gltf.direct_translation.cancellation_block_keys);
  config_1511[observation_keys::kMaxJobs] =
      nb::cast(value.work.inspection.gltf.direct_translation.max_jobs);
  config_1506[observation_keys::kDirectTranslation] = config_1511;
  config_1495[observation_keys::kGltf] = config_1506;
  nb::dict config_1518;
  config_1518[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.inspection.stl.cancellation_block_bytes);
  config_1518[observation_keys::kCancellationBlockTokens] =
      nb::cast(value.work.inspection.stl.cancellation_block_tokens);
  config_1518[observation_keys::kCancellationBlockFacets] =
      nb::cast(value.work.inspection.stl.cancellation_block_facets);
  nb::dict config_1522;
  config_1522[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.inspection.stl.geometry.parallel_min_bytes);
  config_1522[observation_keys::kPartitionBytes] =
      nb::cast(value.work.inspection.stl.geometry.partition_bytes);
  config_1522[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.inspection.stl.geometry.cancellation_block_scalars);
  config_1522[observation_keys::kMaxJobs] =
      nb::cast(value.work.inspection.stl.geometry.max_jobs);
  config_1518[observation_keys::kGeometry] = config_1522;
  config_1495[observation_keys::kStl] = config_1518;
  config_1379[observation_keys::kInspection] = config_1495;
  nb::dict config_1530;
  nb::dict config_1531;
  config_1531[observation_keys::kIoBlockBytes] =
      nb::cast(value.work.publication.files.io_block_bytes);
  config_1531[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.publication.files.parallel_min_bytes);
  config_1531[observation_keys::kPartitionBytes] =
      nb::cast(value.work.publication.files.partition_bytes);
  config_1531[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.publication.files.cancellation_block_bytes);
  config_1531[observation_keys::kMaxJobs] =
      nb::cast(value.work.publication.files.max_jobs);
  config_1530[observation_keys::kFiles] = config_1531;
  nb::dict config_1538;
  nb::dict config_1539;
  config_1539[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.publication.inspection.surface.parallel_min_bytes);
  config_1539[observation_keys::kPartitionBytes] =
      nb::cast(value.work.publication.inspection.surface.partition_bytes);
  config_1539[observation_keys::kCancellationBlockScalars] = nb::cast(
      value.work.publication.inspection.surface.cancellation_block_scalars);
  config_1539[observation_keys::kMaxJobs] =
      nb::cast(value.work.publication.inspection.surface.max_jobs);
  config_1538[observation_keys::kSurface] = config_1539;
  nb::dict config_1545;
  config_1545[observation_keys::kParallelMinBytes] =
      nb::cast(value.work.publication.inspection.container.parallel_min_bytes);
  config_1545[observation_keys::kPartitionBytes] =
      nb::cast(value.work.publication.inspection.container.partition_bytes);
  config_1538[observation_keys::kContainer] = config_1545;
  nb::dict config_1549;
  config_1549[observation_keys::kParallelMinResourceBytes] = nb::cast(
      value.work.publication.inspection.gltf.parallel_min_resource_bytes);
  config_1549[observation_keys::kResourcePartitionBytes] =
      nb::cast(value.work.publication.inspection.gltf.resource_partition_bytes);
  config_1549[observation_keys::kParallelMinChannelComponents] = nb::cast(
      value.work.publication.inspection.gltf.parallel_min_channel_components);
  config_1549[observation_keys::kChannelPartitionElements] = nb::cast(
      value.work.publication.inspection.gltf.channel_partition_elements);
  nb::dict config_1554;
  config_1554[observation_keys::kParallelMinKeys] =
      nb::cast(value.work.publication.inspection.gltf.direct_translation
                   .parallel_min_keys);
  config_1554[observation_keys::kPartitionKeys] = nb::cast(
      value.work.publication.inspection.gltf.direct_translation.partition_keys);
  config_1554[observation_keys::kCancellationBlockKeys] =
      nb::cast(value.work.publication.inspection.gltf.direct_translation
                   .cancellation_block_keys);
  config_1554[observation_keys::kMaxJobs] = nb::cast(
      value.work.publication.inspection.gltf.direct_translation.max_jobs);
  config_1549[observation_keys::kDirectTranslation] = config_1554;
  config_1538[observation_keys::kGltf] = config_1549;
  nb::dict config_1561;
  config_1561[observation_keys::kCancellationBlockBytes] =
      nb::cast(value.work.publication.inspection.stl.cancellation_block_bytes);
  config_1561[observation_keys::kCancellationBlockTokens] =
      nb::cast(value.work.publication.inspection.stl.cancellation_block_tokens);
  config_1561[observation_keys::kCancellationBlockFacets] =
      nb::cast(value.work.publication.inspection.stl.cancellation_block_facets);
  nb::dict config_1565;
  config_1565[observation_keys::kParallelMinBytes] = nb::cast(
      value.work.publication.inspection.stl.geometry.parallel_min_bytes);
  config_1565[observation_keys::kPartitionBytes] =
      nb::cast(value.work.publication.inspection.stl.geometry.partition_bytes);
  config_1565[observation_keys::kCancellationBlockScalars] =
      nb::cast(value.work.publication.inspection.stl.geometry
                   .cancellation_block_scalars);
  config_1565[observation_keys::kMaxJobs] =
      nb::cast(value.work.publication.inspection.stl.geometry.max_jobs);
  config_1561[observation_keys::kGeometry] = config_1565;
  config_1538[observation_keys::kStl] = config_1561;
  config_1530[observation_keys::kInspection] = config_1538;
  config_1379[observation_keys::kPublication] = config_1530;
  result[observation_keys::kWork] = config_1379;
  return result;
}

class OffGlbExecution {
 public:
  explicit OffGlbExecution(std::string_view name)
      : name_(name),
        preset_(MakeOffGlbPreset(name)),
        context_(preset_.execution) {}
  const std::string& Name() const noexcept { return name_; }
  const OffGlbPreset& Preset() const noexcept { return preset_; }
  const geo::ExecutionContext& Context() const noexcept { return context_; }

 private:
  std::string name_;
  OffGlbPreset preset_;
  geo::ExecutionContext context_;
};
struct PreparedOffGlbExport {
  std::filesystem::path source;
  std::filesystem::path destination;
  std::vector<DuplicateTarget> targets;
  std::string input_id;
  std::string receipt_path;
  std::string preset_name;
  OffGlbPreset preset;
  ic::SurfaceNumericPolicy numeric;
};
struct OffGlbExportResult {
  explicit OffGlbExportResult(const geo::ExecutionContext& session,
                              std::stop_source cancellation)
      : context(session), stop(std::move(cancellation)) {}
  geo::ExecutionContext context;
  std::stop_source stop;
  ic::OffFileReadResult capture;
  std::optional<OffGlbSurfaceWorkflowResult> native;
  const OffGlbSurfaceFacts* Facts() const noexcept {
    return native && native->record ? &native->record->Facts() : nullptr;
  }
  const OffDuplicateFileWorkflowRecord* Off() const noexcept {
    auto* f = Facts();
    return f && f->off && f->off->record ? &*f->off->record : nullptr;
  }
  const MaterializedDuplicateRepair* Repair() const noexcept {
    auto* o = Off();
    return o ? o->RepairFacts() : nullptr;
  }
  const geo::ChargedTriangulation* Triangles() const noexcept {
    auto* f = Facts();
    return f && f->triangulation && f->triangulation->triangulation
               ? &*f->triangulation->triangulation
               : nullptr;
  }
  int Exit() const noexcept {
    if (!native) {
      if (capture.status == ic::OffFileStatus::kCanceled) return kCanceledExit;
      return capture.status == ic::OffFileStatus::kUnsupportedProfile
                 ? kRejectedOrIncompleteExit
                 : kExecutionFailureExit;
    }
    if (native->status == OffGlbSurfaceStatus::kCanceled) return kCanceledExit;
    if (native->status == OffGlbSurfaceStatus::kSuccess) return kSuccessExit;
    if (native->status == OffGlbSurfaceStatus::kRejected ||
        native->status == OffGlbSurfaceStatus::kUnsupported)
      return kRejectedOrIncompleteExit;
    auto* f = Facts();
    if (f &&
        ((f->triangulation && f->triangulation->status ==
                                  geo::ChargedTriangulationStatus::kBlocked) ||
         (f->off && f->off->outcomes.exit ==
                        OffWorkflowExitMeaning::kRejectedOrIncomplete)))
      return kRejectedOrIncompleteExit;
    return kExecutionFailureExit;
  }
};
static_assert(std::is_nothrow_move_assignable_v<ic::OffFileReadResult>);
static_assert(std::is_nothrow_move_assignable_v<OffGlbSurfaceWorkflowResult>);
static_assert(
    std::is_nothrow_move_constructible_v<OffGlbSurfaceWorkflowResult>);

std::string Text(nb::handle value, std::uint64_t limit) {
  if (!PyUnicode_Check(value.ptr()))
    throw nb::type_error("text arguments must be Unicode strings");
  if (static_cast<std::uint64_t>(PyUnicode_GET_LENGTH(value.ptr())) > limit)
    throw nb::value_error("text exceeds its UTF-8 limit");
  Py_ssize_t size = 0;
  const char* data = PyUnicode_AsUTF8AndSize(value.ptr(), &size);
  if (!data) throw nb::python_error();
  if (static_cast<std::uint64_t>(size) > limit ||
      std::memchr(data, 0, static_cast<std::size_t>(size)))
    throw nb::value_error("text exceeds its UTF-8 limit or contains NUL");
  return {data, static_cast<std::size_t>(size)};
}
struct PythonMemoryDeleter {
  void operator()(wchar_t* value) const noexcept { PyMem_Free(value); }
};
std::filesystem::path Path(nb::handle value, std::uint64_t units) {
  nb::object text = nb::steal(PyOS_FSPath(value.ptr()));
  if (!text.is_valid()) throw nb::python_error();
  (void)Text(text, kPathUtf8Limit);
#if defined(_WIN32)
  Py_ssize_t count = 0;
  std::unique_ptr<wchar_t, PythonMemoryDeleter> data(
      PyUnicode_AsWideCharString(text.ptr(), &count));
  if (!data) throw nb::python_error();
  if (static_cast<std::uint64_t>(count) >= units)
    throw nb::value_error("path exceeds native code-unit limit");
  return std::filesystem::path(data.get(), data.get() + count);
#else
  nb::object bytes = nb::steal(PyUnicode_EncodeFSDefault(text.ptr()));
  if (!bytes.is_valid()) throw nb::python_error();
  const auto count = PyBytes_GET_SIZE(bytes.ptr());
  if (static_cast<std::uint64_t>(count) >= units)
    throw nb::value_error("path exceeds native code-unit limit");
  return std::filesystem::path(PyBytes_AS_STRING(bytes.ptr()),
                               PyBytes_AS_STRING(bytes.ptr()) + count);
#endif
}
std::uint64_t Index(nb::handle value) {
  if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
    throw nb::type_error("indices must be exact integers, not bool");
  auto number = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  return number;
}
bool AsciiAlphaNumeric(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}
void InputId(std::string_view text) {
  if (text.empty() || !AsciiAlphaNumeric(text.front()))
    throw nb::value_error("input_id must start with an ASCII letter or digit");
  for (char c : text)
    if (!AsciiAlphaNumeric(c) && c != '_' && c != '.' && c != ':' && c != '-')
      throw nb::value_error("input_id must be an opaque ASCII identifier");
}
void Acknowledge(nb::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw nb::type_error("acknowledgments must be bool");
  if (value.ptr() != Py_True)
    throw nb::value_error("all projection acknowledgments must be true");
}
bool SameComponent(const std::filesystem::path& a,
                   const std::filesystem::path& b) {
#if defined(_WIN32)
  const auto& x = a.native();
  const auto& y = b.native();
  return CompareStringOrdinal(x.data(), static_cast<int>(x.size()), y.data(),
                              static_cast<int>(y.size()), TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}
bool Contains(const std::filesystem::path& parent,
              const std::filesystem::path& child) {
  auto p = parent.begin();
  auto c = child.begin();
  for (; p != parent.end() && c != child.end(); ++p, ++c)
    if (!SameComponent(*p, *c)) return false;
  return p == parent.end();
}
void Isolate(const std::filesystem::path& source,
             const std::filesystem::path& destination) {
  std::error_code error;
  auto a = std::filesystem::weakly_canonical(source, error);
  if (error) return;
  auto b = std::filesystem::weakly_canonical(destination, error);
  if (error) return;
  if (Contains(a, b) || Contains(b, a))
    throw nb::value_error("source entry and destination must not overlap");
}
// Match the publisher's ASCII segment grammar, including payload aliases.
void ReceiptPath(std::string_view value) {
  if (value.empty())
    throw nb::value_error("receipt_path must be a portable ASCII member path");
  constexpr std::string_view kForbidden = "\\:<>\"|?*";
  std::string folded(value);
  for (char& c : folded) {
    if (static_cast<unsigned char>(c) < 0x20 ||
        static_cast<unsigned char>(c) >= 0x7f ||
        kForbidden.find(c) != kForbidden.npos)
      throw nb::value_error(
          "receipt_path must be a portable ASCII member path");
    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
  }
  std::size_t start = 0;
  while (start <= folded.size()) {
    auto end = folded.find('/', start);
    if (end == std::string::npos) end = folded.size();
    auto part = std::string_view(folded).substr(start, end - start);
    if (part.empty() || !AsciiAlphaNumeric(part.front()) || part.back() == '.')
      throw nb::value_error("receipt_path contains a prohibited segment");
    for (char c : part)
      if (!AsciiAlphaNumeric(c) && c != '.' && c != '_' && c != '-')
        throw nb::value_error("receipt_path contains a prohibited segment");
    auto stem = part.substr(0, part.find('.'));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        (stem.size() == 4 &&
         (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
         stem[3] >= '1' && stem[3] <= '9'))
      throw nb::value_error("receipt_path contains a reserved device name");
    if (end == folded.size()) break;
    start = end + 1;
  }
  for (auto payload :
       {std::string_view("surface.glb"), std::string_view("surface.json"),
        std::string_view("surface.bin")})
    if (folded == payload ||
        (folded.size() > payload.size() && folded.starts_with(payload) &&
         folded[payload.size()] == '/') ||
        (payload.size() > folded.size() && payload.starts_with(folded) &&
         payload[folded.size()] == '/'))
      throw nb::value_error("receipt_path collides with a payload member");
}
nb::object Prepare(nb::handle source, nb::handle destination,
                   nb::handle targets, nb::handle text, nb::handle edge_count,
                   nb::handle coordinates, nb::handle precision,
                   nb::handle max_error, nb::handle ack_triangles,
                   nb::handle ack_corners, nb::handle ack_unused,
                   nb::handle input_id, nb::handle receipt_path,
                   nb::handle preset_name) {
  PreparedOffGlbExport result;
  result.preset_name = Text(preset_name, kSerialOffGlbPreset.size());
  result.preset = MakeOffGlbPreset(result.preset_name);
  if (Text(text, kCanonicalText.size()) != kCanonicalText ||
      Text(edge_count, kUnknownZeroEdgeCount.size()) != kUnknownZeroEdgeCount)
    throw nb::value_error("OFF GLB requires canonical-ascii and unknown-zero");
  if (Text(coordinates, kDeclaredCoordinates.size()) != kDeclaredCoordinates)
    throw nb::value_error("coordinates must be meters-right-handed-y-up");
  auto selected = Text(precision, kBoundedRne32.size());
  if (selected == kExact32)
    result.numeric.positions = ic::SurfaceNarrowing::kExact32;
  else if (selected == kBoundedRne32)
    result.numeric.positions = ic::SurfaceNarrowing::kBoundedRne32;
  else
    throw nb::value_error("precision must be exact32 or bounded-rne32");
  if (PyBool_Check(max_error.ptr()) ||
      (!PyFloat_Check(max_error.ptr()) && !PyLong_Check(max_error.ptr())))
    throw nb::type_error(
        "max_position_error must be a binary64 number, not bool");
  result.numeric.max_position_linf_error = PyFloat_AsDouble(max_error.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  if (!std::isfinite(result.numeric.max_position_linf_error) ||
      result.numeric.max_position_linf_error < 0 ||
      (selected == kExact32 && result.numeric.max_position_linf_error != 0))
    throw nb::value_error(
        "max_position_error must be finite nonnegative, and zero for exact32");
  Acknowledge(ack_triangles);
  Acknowledge(ack_corners);
  Acknowledge(ack_unused);
  result.input_id = Text(input_id, kInputIdLimit);
  InputId(result.input_id);
  result.receipt_path = Text(receipt_path, kReceiptPathLimit);
  ReceiptPath(result.receipt_path);
  auto cwd = std::filesystem::current_path();
  result.source = Path(
      source, result.preset.capture_limits.files.max_native_path_code_units);
  result.destination =
      Path(destination,
           result.preset.limits.publication.files.max_native_path_code_units);
  if (result.source.is_relative()) result.source = cwd / result.source;
  if (result.destination.is_relative())
    result.destination = cwd / result.destination;
  if (result.source.native().size() >=
          result.preset.capture_limits.files.max_native_path_code_units ||
      result.destination.native().size() >=
          result.preset.limits.publication.files.max_native_path_code_units)
    throw nb::value_error(
        "absolute logical path exceeds native code-unit limit");
  nb::object iterator = nb::steal(PyObject_GetIter(targets.ptr()));
  if (!iterator.is_valid()) throw nb::python_error();
  while (true) {
    nb::object item = nb::steal(PyIter_Next(iterator.ptr()));
    if (!item.is_valid()) {
      if (PyErr_Occurred()) throw nb::python_error();
      break;
    }
    if (result.targets.size() == kTargetLimit)
      throw nb::value_error("target count exceeds preset");
    if (!((PyTuple_Check(item.ptr()) && PyTuple_GET_SIZE(item.ptr()) == 2) ||
          (PyList_Check(item.ptr()) && PyList_GET_SIZE(item.ptr()) == 2)))
      throw nb::value_error(
          "targets must contain two-element tuple/list pairs");
    nb::handle keep = PyTuple_Check(item.ptr())
                          ? PyTuple_GET_ITEM(item.ptr(), 0)
                          : PyList_GET_ITEM(item.ptr(), 0);
    nb::handle remove = PyTuple_Check(item.ptr())
                            ? PyTuple_GET_ITEM(item.ptr(), 1)
                            : PyList_GET_ITEM(item.ptr(), 1);
    result.targets.push_back({Index(keep), Index(remove)});
  }
  Isolate(result.source, result.destination);
  return nb::cast(std::move(result));
}
nb::object Run(const PreparedOffGlbExport& prepared, nb::handle execution,
               nb::handle cancellation) {
  std::optional<OffGlbExecution> owned;
  const OffGlbExecution* session = nullptr;
  if (execution.is_none()) {
    owned.emplace(prepared.preset_name);
    session = &*owned;
  } else {
    if (!nb::isinstance<OffGlbExecution>(execution))
      throw nb::type_error("execution must be an OffGlbExecution");
    session = &nb::cast<const OffGlbExecution&>(execution);
    if (session->Name() != prepared.preset_name)
      throw nb::value_error("execution preset must match prepared operation");
  }
  auto stop = ShareOffCancellation(cancellation);
  nb::object object = nb::inst_alloc(nb::type<OffGlbExportResult>());
  auto* result = new (nb::inst_ptr<OffGlbExportResult>(object))
      OffGlbExportResult(session->Context(), std::move(stop));
  nb::inst_mark_ready(object);
  {
    nb::gil_scoped_release release;
    result->capture =
        ic::ReadOffFileAsset(prepared.source, prepared.preset.capture_limits,
                             prepared.preset.capture_work, result->context,
                             result->stop.get_token());
    if (result->capture.status == ic::OffFileStatus::kSuccess) {
      if (!result->capture.asset)
        throw std::logic_error("successful OFF capture has no asset");
      OffGlbSurfaceWorkflowRequest request{
          *result->capture.asset,
          prepared.input_id,
          prepared.targets,
          prepared.destination,
          ic::OffTextPolicy::kCanonicalAscii,
          ic::OffEdgeCountPolicy::kUnknownZero,
          ic::SurfaceCoordinates::kDeclaredMetersRightHandedYUp,
          prepared.numeric,
          true,
          true,
          true,
          prepared.preset.triangulation_options,
          prepared.receipt_path};
      result->native.emplace(RunOffGlbSurfaceWorkflow(
          request, prepared.preset.limits, prepared.preset.work,
          result->stop.get_token()));
    }
  }
  if (result->native &&
      result->native->status == OffGlbSurfaceStatus::kSuccess) {
    auto* f = result->Facts();
    if (!f || !f->publication ||
        f->publication->outcome !=
            ic::SurfaceFilePublicationOutcome::kPublished ||
        !f->publication->bundle)
      throw std::logic_error(
          "successful OFF GLB export has no committed bundle");
  }
  return object;
}

// Each buffer exports the real native span and independently owns its backing.
using BufferBacking =
    std::variant<geo::MaterializedMesh, MaterializedDuplicateRepair,
                 geo::ChargedTriangulation, ic::SurfaceOutput,
                 ic::SurfaceOutputInspection, ic::SurfacePublishedBundle>;
struct ReadonlyBuffer {
  PyObject_HEAD std::optional<BufferBacking> backing;
  const void* data;
  Py_ssize_t bytes;
  Py_ssize_t length;
  Py_ssize_t stride;
  char format[2];
};
int GetBuffer(PyObject* object, Py_buffer* view, int flags) {
  auto* self = reinterpret_cast<ReadonlyBuffer*>(object);
  if (flags & PyBUF_WRITABLE) {
    PyErr_SetString(PyExc_BufferError, "OFF GLB views are readonly");
    return -1;
  }
  if (PyBuffer_FillInfo(view, object, const_cast<void*>(self->data),
                        self->bytes, 1, flags) < 0)
    return -1;
  view->itemsize = self->stride;
  view->ndim = 1;
  view->format = (flags & PyBUF_FORMAT) ? self->format : nullptr;
  view->shape = (flags & PyBUF_ND) ? &self->length : nullptr;
  view->strides =
      ((flags & PyBUF_STRIDES) == PyBUF_STRIDES) ? &self->stride : nullptr;
  return 0;
}
void FreeBuffer(PyObject* object) {
  auto* self = reinterpret_cast<ReadonlyBuffer*>(object);
  self->backing.~optional();
  auto* type = Py_TYPE(object);
  type->tp_free(object);
  Py_DECREF(type);
}
PyObject* RefuseBufferConstruction(PyTypeObject*, PyObject*, PyObject*) {
  PyErr_SetString(PyExc_TypeError,
                  "native readonly buffer views cannot be constructed");
  return nullptr;
}
nb::object RegisterBuffer(nb::module_& module) {
  static PyType_Slot slots[] = {
      {Py_tp_dealloc, reinterpret_cast<void*>(FreeBuffer)},
      {Py_bf_getbuffer, reinterpret_cast<void*>(GetBuffer)},
      {Py_tp_new, reinterpret_cast<void*>(RefuseBufferConstruction)},
      {0, nullptr}};
  static PyType_Spec spec = {"meshvale_repair._repair._OffGlbReadonlyBuffer",
                             sizeof(ReadonlyBuffer), 0, Py_TPFLAGS_DEFAULT,
                             slots};
  nb::object type = nb::steal(PyType_FromSpec(&spec));
  if (!type.is_valid()) throw nb::python_error();
  module.attr("_OffGlbReadonlyBuffer") = type;
  return type;
}
template <class Owner, class T>
nb::object Memory(const nb::object& buffer_type, const Owner& owner,
                  std::span<const T> values) {
  if (values.size() > static_cast<std::size_t>(PY_SSIZE_T_MAX) / sizeof(T))
    throw nb::value_error("view exceeds Python buffer extent");
  auto* type = reinterpret_cast<PyTypeObject*>(buffer_type.ptr());
  auto* self = reinterpret_cast<ReadonlyBuffer*>(type->tp_alloc(type, 0));
  if (!self) throw nb::python_error();
  new (&self->backing) std::optional<BufferBacking>(std::in_place, owner);
  self->data = values.data();
  self->length = static_cast<Py_ssize_t>(values.size());
  self->stride = sizeof(T);
  self->bytes = self->length * self->stride;
  self->format[0] = std::is_same_v<T, double>      ? 'd'
                    : std::is_same_v<T, std::byte> ? 'B'
                                                   : 'Q';
  self->format[1] = 0;
  nb::object backing = nb::steal(reinterpret_cast<PyObject*>(self));
  nb::object memory = nb::steal(PyMemoryView_FromObject(backing.ptr()));
  if (!memory.is_valid()) throw nb::python_error();
  return memory;
}
struct MeshView {
  geo::MaterializedMesh owner;
};
struct MeshInspectionView {
  geo::MeshInspection owner;
};
struct OutputView {
  ic::SurfaceOutput owner;
};
struct InspectionView {
  ic::SurfaceOutputInspection owner;
};
struct PublishedView {
  ic::SurfacePublishedBundle owner;
};
using MemberBacking =
    std::variant<ic::SurfaceOutput, ic::SurfaceOutputInspection>;
struct MemberView {
  MemberBacking owner;
  ic::SurfaceMemberView member;
};
struct ReceiptView {
  ic::SurfacePublishedBundle owner;
};
nb::object Digest(const ic::SurfaceDigest& value) {
  return nb::bytes(value.data(), value.size());
}
nb::object Members(const ic::SurfaceOutput& output) {
  return Tuple(output.Members(), [&](const auto& member) {
    return nb::cast(MemberView{output, member});
  });
}
nb::object Members(const ic::SurfaceOutputInspection& proof) {
  return Tuple(proof.CapturedMembers(), [&](const auto& member) {
    return nb::cast(MemberView{proof, member});
  });
}
nb::object Identity(std::uint64_t count) {
  nb::object stop = nb::steal(PyLong_FromUnsignedLongLong(count));
  if (!stop.is_valid()) throw nb::python_error();
  nb::object result = nb::steal(PyObject_CallOneArg(
      reinterpret_cast<PyObject*>(&PyRange_Type), stop.ptr()));
  if (!result.is_valid()) throw nb::python_error();
  return result;
}

nb::dict ObserveResult(const OffGlbExportResult& result) {
  ObservationSize size;
  constexpr std::array<std::string_view, 9> kRootObservationFields = {
      observation_keys::kStatus,     observation_keys::kStage,
      observation_keys::kCode,       observation_keys::kCapture,
      observation_keys::kOff,        observation_keys::kTriangulation,
      observation_keys::kEncoding,   observation_keys::kInspection,
      observation_keys::kPublication};
  size.Add(kJsonContainerDelimiterBytes +
           (kRootObservationFields.size() - 1) * kJsonSeparatorBytes);
  for (auto field : kRootObservationFields) {
    size.String(field);
    size.Add(kJsonColonBytes);
  }
  constexpr std::size_t kRootOutcomeFieldCount = 3;
  constexpr std::size_t kDelegateObservationFieldCount = 5;
  if (result.native) {
    Measure(result.native->status, size);
    Measure(result.native->diagnostic.stage, size);
    Measure(result.native->diagnostic.code, size);
    size.Add(kRootOutcomeFieldCount * kNativeOutcomePrefix.size());
  } else {
    Measure(result.capture.status, size);
    Measure(result.capture.diagnostic.stage, size);
    Measure(result.capture.diagnostic.code, size);
    size.Add(kRootOutcomeFieldCount * kCaptureOutcomePrefix.size());
  }
  Measure(result.capture, size);
  auto* reached = result.Facts();
  if (reached) {
    Measure(reached->off, size);
    Measure(reached->triangulation, size);
    Measure(reached->encoding, size);
    Measure(reached->inspection, size);
    Measure(reached->publication, size);
  } else
    size.Add(kDelegateObservationFieldCount * kWireNull.size());
  nb::dict value;
  auto qualify = [](std::string_view prefix, nb::object symbol) {
    std::string text(prefix);
    text += nb::cast<std::string_view>(symbol);
    return nb::str(text.c_str());
  };
  if (result.native) {
    value[observation_keys::kStatus] =
        qualify(kNativeOutcomePrefix, Wire(result.native->status));
    value[observation_keys::kStage] =
        qualify(kNativeOutcomePrefix, Wire(result.native->diagnostic.stage));
    value[observation_keys::kCode] =
        qualify(kNativeOutcomePrefix, Wire(result.native->diagnostic.code));
  } else {
    value[observation_keys::kStatus] =
        qualify(kCaptureOutcomePrefix, Wire(result.capture.status));
    value[observation_keys::kStage] =
        qualify(kCaptureOutcomePrefix, Wire(result.capture.diagnostic.stage));
    value[observation_keys::kCode] =
        qualify(kCaptureOutcomePrefix, Wire(result.capture.diagnostic.code));
  }
  value[observation_keys::kCapture] = Wire(result.capture);
  auto* facts = result.Facts();
  value[observation_keys::kOff] = facts ? Wire(facts->off) : nb::none();
  value[observation_keys::kTriangulation] =
      facts ? Wire(facts->triangulation) : nb::none();
  value[observation_keys::kEncoding] =
      facts ? Wire(facts->encoding) : nb::none();
  value[observation_keys::kInspection] =
      facts ? Wire(facts->inspection) : nb::none();
  value[observation_keys::kPublication] =
      facts ? Wire(facts->publication) : nb::none();
  return value;
}
}  // namespace

void RegisterOffSurfaceExportBindings(nb::module_& module) {
  nb::exception<ObservationLimit>(module, "OffGlbObservationLimitError",
                                  PyExc_ValueError);
  if (!nb::type<ic::SurfaceMemberKind>().is_valid()) {
    auto type =
        nb::enum_<ic::SurfaceMemberKind>(module, "OffGlbSurfaceMemberKind");
    type.value("kGltf", ic::SurfaceMemberKind::kGltf);
    type.value("kGlb", ic::SurfaceMemberKind::kGlb);
    type.value("kStl", ic::SurfaceMemberKind::kStl);
    type.value("kGeometryBuffer", ic::SurfaceMemberKind::kGeometryBuffer);
    type.value(wire_symbols::kImage.data(), ic::SurfaceMemberKind::kImage);
    type.value("kCapsuleJson", ic::SurfaceMemberKind::kCapsuleJson);
    type.value("kCapsuleBinary", ic::SurfaceMemberKind::kCapsuleBinary);
  }
  if (!nb::type<ic::SurfaceRepresentation>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceRepresentation>(
        module, "OffGlbSurfaceRepresentation");
    type.value(wire_symbols::kInvalid.data(),
               ic::SurfaceRepresentation::kInvalid);
    type.value("kStrictBare", ic::SurfaceRepresentation::kStrictBare);
    type.value("kAnnotatedBundleV1",
               ic::SurfaceRepresentation::kAnnotatedBundleV1);
  }
  if (!nb::type<ic::SurfaceEncoding>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceEncoding>(module, "OffGlbSurfaceEncoding");
    type.value(wire_symbols::kInvalid.data(), ic::SurfaceEncoding::kInvalid);
    type.value("kGltfJson", ic::SurfaceEncoding::kGltfJson);
    type.value("kGlb", ic::SurfaceEncoding::kGlb);
    type.value("kStlAscii", ic::SurfaceEncoding::kStlAscii);
    type.value("kStlBinary", ic::SurfaceEncoding::kStlBinary);
  }
  if (!nb::type<ic::OffFileStatus>().is_valid()) {
    auto type = nb::enum_<ic::OffFileStatus>(module, "OffGlbOffFileStatus");
    type.value(wire_symbols::kSuccess.data(), ic::OffFileStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               ic::OffFileStatus::kInvalidConfiguration);
    type.value(wire_symbols::kInvalidRequest.data(),
               ic::OffFileStatus::kInvalidRequest);
    type.value(wire_symbols::kUnsupportedProfile.data(),
               ic::OffFileStatus::kUnsupportedProfile);
    type.value(wire_symbols::kLimitExceeded.data(),
               ic::OffFileStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               ic::OffFileStatus::kBudgetExceeded);
    type.value(wire_symbols::kForeignBudget.data(),
               ic::OffFileStatus::kForeignBudget);
    type.value(wire_symbols::kIoFailure.data(), ic::OffFileStatus::kIoFailure);
    type.value(wire_symbols::kFileChanged.data(),
               ic::OffFileStatus::kFileChanged);
    type.value(wire_symbols::kOffFailure.data(),
               ic::OffFileStatus::kOffFailure);
    type.value(wire_symbols::kGeometryFailure.data(),
               ic::OffFileStatus::kGeometryFailure);
    type.value(wire_symbols::kPreservationMismatch.data(),
               ic::OffFileStatus::kPreservationMismatch);
    type.value(wire_symbols::kProviderFailure.data(),
               ic::OffFileStatus::kProviderFailure);
    type.value(wire_symbols::kDestinationExists.data(),
               ic::OffFileStatus::kDestinationExists);
    type.value(wire_symbols::kPublicationFailed.data(),
               ic::OffFileStatus::kPublicationFailed);
    type.value(wire_symbols::kCanceled.data(), ic::OffFileStatus::kCanceled);
  }
  if (!nb::type<ic::OffFileStage>().is_valid()) {
    auto type = nb::enum_<ic::OffFileStage>(module, "OffGlbOffFileStage");
    type.value(wire_symbols::kConfiguration.data(),
               ic::OffFileStage::kConfiguration);
    type.value(wire_symbols::kPathPlanning.data(),
               ic::OffFileStage::kPathPlanning);
    type.value(wire_symbols::kCapture.data(), ic::OffFileStage::kCapture);
    type.value(wire_symbols::kOffRead.data(), ic::OffFileStage::kOffRead);
    type.value(wire_symbols::kExpectedInspection.data(),
               ic::OffFileStage::kExpectedInspection);
    type.value(wire_symbols::kStaging.data(), ic::OffFileStage::kStaging);
    type.value(wire_symbols::kReload.data(), ic::OffFileStage::kReload);
    type.value(wire_symbols::kVerification.data(),
               ic::OffFileStage::kVerification);
    type.value(wire_symbols::kProvider.data(), ic::OffFileStage::kProvider);
    type.value(wire_symbols::kInventory.data(), ic::OffFileStage::kInventory);
    type.value(wire_symbols::kPublication.data(),
               ic::OffFileStage::kPublication);
    type.value(wire_symbols::kComplete.data(), ic::OffFileStage::kComplete);
  }
  if (!nb::type<ic::OffFileDiagnosticCode>().is_valid()) {
    auto type = nb::enum_<ic::OffFileDiagnosticCode>(
        module, "OffGlbOffFileDiagnosticCode");
    type.value(wire_symbols::kNone.data(), ic::OffFileDiagnosticCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               ic::OffFileDiagnosticCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               ic::OffFileDiagnosticCode::kConfigurationExtent);
    type.value(wire_symbols::kCountLimit.data(),
               ic::OffFileDiagnosticCode::kCountLimit);
    type.value(wire_symbols::kPathUnsupported.data(),
               ic::OffFileDiagnosticCode::kPathUnsupported);
    type.value(wire_symbols::kNameUnsupported.data(),
               ic::OffFileDiagnosticCode::kNameUnsupported);
    type.value(wire_symbols::kNameCollision.data(),
               ic::OffFileDiagnosticCode::kNameCollision);
    type.value(wire_symbols::kNotRegular.data(),
               ic::OffFileDiagnosticCode::kNotRegular);
    type.value(wire_symbols::kOpen.data(), ic::OffFileDiagnosticCode::kOpen);
    type.value(wire_symbols::kRead.data(), ic::OffFileDiagnosticCode::kRead);
    type.value(wire_symbols::kWrite.data(), ic::OffFileDiagnosticCode::kWrite);
    type.value(wire_symbols::kClose.data(), ic::OffFileDiagnosticCode::kClose);
    type.value(wire_symbols::kChanged.data(),
               ic::OffFileDiagnosticCode::kChanged);
    type.value(wire_symbols::kRequestBudget.data(),
               ic::OffFileDiagnosticCode::kRequestBudget);
    type.value(wire_symbols::kSharedBudget.data(),
               ic::OffFileDiagnosticCode::kSharedBudget);
    type.value(wire_symbols::kStopRequested.data(),
               ic::OffFileDiagnosticCode::kStopRequested);
    type.value(wire_symbols::kLedgerMismatch.data(),
               ic::OffFileDiagnosticCode::kLedgerMismatch);
    type.value(wire_symbols::kDelegatedOff.data(),
               ic::OffFileDiagnosticCode::kDelegatedOff);
    type.value(wire_symbols::kDelegatedInspection.data(),
               ic::OffFileDiagnosticCode::kDelegatedInspection);
    type.value(wire_symbols::kAttributeUnsupported.data(),
               ic::OffFileDiagnosticCode::kAttributeUnsupported);
    type.value(wire_symbols::kOutputFacts.data(),
               ic::OffFileDiagnosticCode::kOutputFacts);
    type.value(wire_symbols::kByteMismatch.data(),
               ic::OffFileDiagnosticCode::kByteMismatch);
    type.value(wire_symbols::kMeshMismatch.data(),
               ic::OffFileDiagnosticCode::kMeshMismatch);
    type.value(wire_symbols::kProvider.data(),
               ic::OffFileDiagnosticCode::kProvider);
    type.value(wire_symbols::kStagedInventory.data(),
               ic::OffFileDiagnosticCode::kStagedInventory);
    type.value(wire_symbols::kDestinationExists.data(),
               ic::OffFileDiagnosticCode::kDestinationExists);
    type.value(wire_symbols::kCommit.data(),
               ic::OffFileDiagnosticCode::kCommit);
    type.value(wire_symbols::kCommitUnsupported.data(),
               ic::OffFileDiagnosticCode::kCommitUnsupported);
  }
  if (!nb::type<ic::OffStatus>().is_valid()) {
    auto type = nb::enum_<ic::OffStatus>(module, "OffGlbOffStatus");
    type.value(wire_symbols::kSuccess.data(), ic::OffStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               ic::OffStatus::kInvalidConfiguration);
    type.value(wire_symbols::kMalformed.data(), ic::OffStatus::kMalformed);
    type.value(wire_symbols::kUnsupportedVariant.data(),
               ic::OffStatus::kUnsupportedVariant);
    type.value(wire_symbols::kLimitExceeded.data(),
               ic::OffStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               ic::OffStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(), ic::OffStatus::kCanceled);
    type.value(wire_symbols::kInvalidStorage.data(),
               ic::OffStatus::kInvalidStorage);
    type.value(wire_symbols::kGeometryFailure.data(),
               ic::OffStatus::kGeometryFailure);
  }
  if (!nb::type<ic::OffStage>().is_valid()) {
    auto type = nb::enum_<ic::OffStage>(module, "OffGlbOffStage");
    type.value(wire_symbols::kConfiguration.data(),
               ic::OffStage::kConfiguration);
    type.value(wire_symbols::kScan.data(), ic::OffStage::kScan);
    type.value(wire_symbols::kDecode.data(), ic::OffStage::kDecode);
    type.value(wire_symbols::kMaterialization.data(),
               ic::OffStage::kMaterialization);
    type.value(wire_symbols::kInspection.data(), ic::OffStage::kInspection);
    type.value(wire_symbols::kValidation.data(), ic::OffStage::kValidation);
    type.value(wire_symbols::kEncode.data(), ic::OffStage::kEncode);
    type.value(wire_symbols::kPublication.data(), ic::OffStage::kPublication);
  }
  if (!nb::type<ic::OffDiagnosticCode>().is_valid()) {
    auto type =
        nb::enum_<ic::OffDiagnosticCode>(module, "OffGlbOffDiagnosticCode");
    type.value(wire_symbols::kNone.data(), ic::OffDiagnosticCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               ic::OffDiagnosticCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               ic::OffDiagnosticCode::kConfigurationExtent);
    type.value(wire_symbols::kSourceLimit.data(),
               ic::OffDiagnosticCode::kSourceLimit);
    type.value(wire_symbols::kVertexLimit.data(),
               ic::OffDiagnosticCode::kVertexLimit);
    type.value(wire_symbols::kFaceLimit.data(),
               ic::OffDiagnosticCode::kFaceLimit);
    type.value(wire_symbols::kCornerLimit.data(),
               ic::OffDiagnosticCode::kCornerLimit);
    type.value(wire_symbols::kPerFaceCornerLimit.data(),
               ic::OffDiagnosticCode::kPerFaceCornerLimit);
    type.value(wire_symbols::kLineLimit.data(),
               ic::OffDiagnosticCode::kLineLimit);
    type.value(wire_symbols::kTokenLimit.data(),
               ic::OffDiagnosticCode::kTokenLimit);
    type.value(wire_symbols::kCommentLimit.data(),
               ic::OffDiagnosticCode::kCommentLimit);
    type.value(wire_symbols::kOutputLimit.data(),
               ic::OffDiagnosticCode::kOutputLimit);
    type.value(wire_symbols::kSyntax.data(), ic::OffDiagnosticCode::kSyntax);
    type.value(wire_symbols::kIntegerSyntax.data(),
               ic::OffDiagnosticCode::kIntegerSyntax);
    type.value(wire_symbols::kIntegerRange.data(),
               ic::OffDiagnosticCode::kIntegerRange);
    type.value(wire_symbols::kNumericSyntax.data(),
               ic::OffDiagnosticCode::kNumericSyntax);
    type.value(wire_symbols::kNumericRange.data(),
               ic::OffDiagnosticCode::kNumericRange);
    type.value(wire_symbols::kNonfinite.data(),
               ic::OffDiagnosticCode::kNonfinite);
    type.value(wire_symbols::kVariant.data(), ic::OffDiagnosticCode::kVariant);
    type.value(wire_symbols::kUnsupportedPayload.data(),
               ic::OffDiagnosticCode::kUnsupportedPayload);
    type.value(wire_symbols::kTrailingData.data(),
               ic::OffDiagnosticCode::kTrailingData);
    type.value(wire_symbols::kVertexRange.data(),
               ic::OffDiagnosticCode::kVertexRange);
    type.value(wire_symbols::kFaceTooShort.data(),
               ic::OffDiagnosticCode::kFaceTooShort);
    type.value(wire_symbols::kUnsupportedAttribute.data(),
               ic::OffDiagnosticCode::kUnsupportedAttribute);
    type.value(wire_symbols::kTextPolicy.data(),
               ic::OffDiagnosticCode::kTextPolicy);
    type.value(wire_symbols::kEdgeCountPolicy.data(),
               ic::OffDiagnosticCode::kEdgeCountPolicy);
    type.value(wire_symbols::kRequestBudget.data(),
               ic::OffDiagnosticCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               ic::OffDiagnosticCode::kStopRequested);
    type.value(wire_symbols::kGeometry.data(),
               ic::OffDiagnosticCode::kGeometry);
    type.value(wire_symbols::kStorageIssues.data(),
               ic::OffDiagnosticCode::kStorageIssues);
  }
  if (!nb::type<geo::MaterializationStatus>().is_valid()) {
    auto type = nb::enum_<geo::MaterializationStatus>(
        module, "OffGlbGeometryMaterializationStatus");
    type.value(wire_symbols::kSuccess.data(),
               geo::MaterializationStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               geo::MaterializationStatus::kInvalidConfiguration);
    type.value(wire_symbols::kInvalidTransport.data(),
               geo::MaterializationStatus::kInvalidTransport);
    type.value(wire_symbols::kLimitExceeded.data(),
               geo::MaterializationStatus::kLimitExceeded);
    type.value(wire_symbols::kUnsupportedAllocationProfile.data(),
               geo::MaterializationStatus::kUnsupportedAllocationProfile);
    type.value(wire_symbols::kBudgetExceeded.data(),
               geo::MaterializationStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(),
               geo::MaterializationStatus::kCanceled);
    type.value(wire_symbols::kInvalidSourceStorage.data(),
               geo::MaterializationStatus::kInvalidSourceStorage);
    type.value(wire_symbols::kInvalidSelection.data(),
               geo::MaterializationStatus::kInvalidSelection);
  }
  if (!nb::type<geo::MaterializationDiagnosticCode>().is_valid()) {
    auto type = nb::enum_<geo::MaterializationDiagnosticCode>(
        module, "OffGlbGeometryMaterializationDiagnosticCode");
    type.value(wire_symbols::kNone.data(),
               geo::MaterializationDiagnosticCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               geo::MaterializationDiagnosticCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               geo::MaterializationDiagnosticCode::kConfigurationExtent);
    type.value(wire_symbols::kTransportSelector.data(),
               geo::MaterializationDiagnosticCode::kTransportSelector);
    type.value(wire_symbols::kPositionExtent.data(),
               geo::MaterializationDiagnosticCode::kPositionExtent);
    type.value(wire_symbols::kTriangleExtent.data(),
               geo::MaterializationDiagnosticCode::kTriangleExtent);
    type.value(wire_symbols::kStorageExtent.data(),
               geo::MaterializationDiagnosticCode::kStorageExtent);
    type.value(wire_symbols::kCountLimit.data(),
               geo::MaterializationDiagnosticCode::kCountLimit);
    type.value(wire_symbols::kIssueLimit.data(),
               geo::MaterializationDiagnosticCode::kIssueLimit);
    type.value(wire_symbols::kAllocationProfile.data(),
               geo::MaterializationDiagnosticCode::kAllocationProfile);
    type.value(wire_symbols::kRequestBudget.data(),
               geo::MaterializationDiagnosticCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               geo::MaterializationDiagnosticCode::kStopRequested);
    type.value(wire_symbols::kSourceStorage.data(),
               geo::MaterializationDiagnosticCode::kSourceStorage);
    type.value(wire_symbols::kSelectionOrder.data(),
               geo::MaterializationDiagnosticCode::kSelectionOrder);
    type.value(wire_symbols::kSelectionRange.data(),
               geo::MaterializationDiagnosticCode::kSelectionRange);
  }
  if (!nb::type<geo::StorageIssueCode>().is_valid()) {
    auto type = nb::enum_<geo::StorageIssueCode>(
        module, "OffGlbGeometryStorageIssueCode");
    type.value(wire_symbols::kPositionNonfinite.data(),
               geo::StorageIssueCode::kPositionNonfinite);
    type.value(wire_symbols::kFaceOffsetsEmpty.data(),
               geo::StorageIssueCode::kFaceOffsetsEmpty);
    type.value(wire_symbols::kFaceOffsetsStart.data(),
               geo::StorageIssueCode::kFaceOffsetsStart);
    type.value(wire_symbols::kFaceOffsetsEnd.data(),
               geo::StorageIssueCode::kFaceOffsetsEnd);
    type.value(wire_symbols::kFaceOffsetRange.data(),
               geo::StorageIssueCode::kFaceOffsetRange);
    type.value(wire_symbols::kFaceOffsetOrder.data(),
               geo::StorageIssueCode::kFaceOffsetOrder);
    type.value(wire_symbols::kFaceTooShort.data(),
               geo::StorageIssueCode::kFaceTooShort);
    type.value(wire_symbols::kCornerVertexRange.data(),
               geo::StorageIssueCode::kCornerVertexRange);
    type.value(wire_symbols::kAttributeDuplicateName.data(),
               geo::StorageIssueCode::kAttributeDuplicateName);
    type.value(wire_symbols::kAttributeDomain.data(),
               geo::StorageIssueCode::kAttributeDomain);
    type.value(wire_symbols::kAttributeNameEmpty.data(),
               geo::StorageIssueCode::kAttributeNameEmpty);
    type.value(wire_symbols::kAttributeComponentsZero.data(),
               geo::StorageIssueCode::kAttributeComponentsZero);
    type.value(wire_symbols::kAttributeDenseShape.data(),
               geo::StorageIssueCode::kAttributeDenseShape);
    type.value(wire_symbols::kAttributeOffsetsEmpty.data(),
               geo::StorageIssueCode::kAttributeOffsetsEmpty);
    type.value(wire_symbols::kAttributeOffsetsCount.data(),
               geo::StorageIssueCode::kAttributeOffsetsCount);
    type.value(wire_symbols::kAttributeOffsetsStart.data(),
               geo::StorageIssueCode::kAttributeOffsetsStart);
    type.value(wire_symbols::kAttributeOffsetsEnd.data(),
               geo::StorageIssueCode::kAttributeOffsetsEnd);
    type.value(wire_symbols::kAttributeOffsetRange.data(),
               geo::StorageIssueCode::kAttributeOffsetRange);
    type.value(wire_symbols::kAttributeOffsetOrder.data(),
               geo::StorageIssueCode::kAttributeOffsetOrder);
    type.value(wire_symbols::kAttributeOffsetAlignment.data(),
               geo::StorageIssueCode::kAttributeOffsetAlignment);
    type.value(wire_symbols::kAttributePresenceCount.data(),
               geo::StorageIssueCode::kAttributePresenceCount);
    type.value(wire_symbols::kAttributePresenceValue.data(),
               geo::StorageIssueCode::kAttributePresenceValue);
    type.value(wire_symbols::kMetadataDuplicateKey.data(),
               geo::StorageIssueCode::kMetadataDuplicateKey);
  }
  if (!nb::type<OffGlbSurfaceStatus>().is_valid()) {
    auto type =
        nb::enum_<OffGlbSurfaceStatus>(module, "OffGlbOffGlbSurfaceStatus");
    type.value(wire_symbols::kSuccess.data(), OffGlbSurfaceStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               OffGlbSurfaceStatus::kInvalidConfiguration);
    type.value(wire_symbols::kLimitExceeded.data(),
               OffGlbSurfaceStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               OffGlbSurfaceStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(), OffGlbSurfaceStatus::kCanceled);
    type.value(wire_symbols::kRejected.data(), OffGlbSurfaceStatus::kRejected);
    type.value(wire_symbols::kUnsupported.data(),
               OffGlbSurfaceStatus::kUnsupported);
    type.value(wire_symbols::kDelegatedFailure.data(),
               OffGlbSurfaceStatus::kDelegatedFailure);
  }
  if (!nb::type<OffGlbSurfaceStage>().is_valid()) {
    auto type =
        nb::enum_<OffGlbSurfaceStage>(module, "OffGlbOffGlbSurfaceStage");
    type.value(wire_symbols::kConfiguration.data(),
               OffGlbSurfaceStage::kConfiguration);
    type.value(wire_symbols::kOffWorkflow.data(),
               OffGlbSurfaceStage::kOffWorkflow);
    type.value(wire_symbols::kTriangulation.data(),
               OffGlbSurfaceStage::kTriangulation);
    type.value(wire_symbols::kEncoding.data(), OffGlbSurfaceStage::kEncoding);
    type.value(wire_symbols::kInspection.data(),
               OffGlbSurfaceStage::kInspection);
    type.value(wire_symbols::kPublication.data(),
               OffGlbSurfaceStage::kPublication);
    type.value(wire_symbols::kComplete.data(), OffGlbSurfaceStage::kComplete);
  }
  if (!nb::type<OffGlbSurfaceCode>().is_valid()) {
    auto type = nb::enum_<OffGlbSurfaceCode>(module, "OffGlbOffGlbSurfaceCode");
    type.value(wire_symbols::kNone.data(), OffGlbSurfaceCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               OffGlbSurfaceCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               OffGlbSurfaceCode::kConfigurationExtent);
    type.value(wire_symbols::kPolicy.data(), OffGlbSurfaceCode::kPolicy);
    type.value(wire_symbols::kCoordinates.data(),
               OffGlbSurfaceCode::kCoordinates);
    type.value(wire_symbols::kNumericPolicy.data(),
               OffGlbSurfaceCode::kNumericPolicy);
    type.value(wire_symbols::kProjectionAcknowledgment.data(),
               OffGlbSurfaceCode::kProjectionAcknowledgment);
    type.value(wire_symbols::kInputIdentity.data(),
               OffGlbSurfaceCode::kInputIdentity);
    type.value(wire_symbols::kReceiptPath.data(),
               OffGlbSurfaceCode::kReceiptPath);
    type.value(wire_symbols::kRequestBudget.data(),
               OffGlbSurfaceCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               OffGlbSurfaceCode::kStopRequested);
    type.value(wire_symbols::kOffWorkflow.data(),
               OffGlbSurfaceCode::kOffWorkflow);
    type.value(wire_symbols::kTriangulation.data(),
               OffGlbSurfaceCode::kTriangulation);
    type.value(wire_symbols::kEncoding.data(), OffGlbSurfaceCode::kEncoding);
    type.value(wire_symbols::kInspection.data(),
               OffGlbSurfaceCode::kInspection);
    type.value(wire_symbols::kPublication.data(),
               OffGlbSurfaceCode::kPublication);
  }
  if (!nb::type<OffWorkflowStatus>().is_valid()) {
    auto type = nb::enum_<OffWorkflowStatus>(module, "OffGlbOffWorkflowStatus");
    type.value(wire_symbols::kSuccess.data(), OffWorkflowStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               OffWorkflowStatus::kInvalidConfiguration);
    type.value(wire_symbols::kInvalidRequest.data(),
               OffWorkflowStatus::kInvalidRequest);
    type.value(wire_symbols::kLimitExceeded.data(),
               OffWorkflowStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               OffWorkflowStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(), OffWorkflowStatus::kCanceled);
    type.value(wire_symbols::kRejected.data(), OffWorkflowStatus::kRejected);
    type.value(wire_symbols::kDelegatedFailure.data(),
               OffWorkflowStatus::kDelegatedFailure);
    type.value(wire_symbols::kVerificationFailure.data(),
               OffWorkflowStatus::kVerificationFailure);
  }
  if (!nb::type<OffWorkflowExecutionOutcome>().is_valid()) {
    auto type = nb::enum_<OffWorkflowExecutionOutcome>(
        module, "OffGlbOffWorkflowExecutionOutcome");
    type.value(wire_symbols::kCompleted.data(),
               OffWorkflowExecutionOutcome::kCompleted);
    type.value(wire_symbols::kFailed.data(),
               OffWorkflowExecutionOutcome::kFailed);
    type.value(wire_symbols::kCanceled.data(),
               OffWorkflowExecutionOutcome::kCanceled);
  }
  if (!nb::type<CandidateOutcome>().is_valid()) {
    auto type = nb::enum_<CandidateOutcome>(module, "OffGlbCandidateOutcome");
    type.value(wire_symbols::kCandidateAccepted.data(),
               CandidateOutcome::accepted);
    type.value(wire_symbols::kCandidateUnchanged.data(),
               CandidateOutcome::unchanged);
    type.value(wire_symbols::kCandidateRejected.data(),
               CandidateOutcome::rejected);
  }
  if (!nb::type<OffWorkflowProfileOutcome>().is_valid()) {
    auto type = nb::enum_<OffWorkflowProfileOutcome>(
        module, "OffGlbOffWorkflowProfileOutcome");
    type.value(wire_symbols::kPassed.data(),
               OffWorkflowProfileOutcome::kPassed);
    type.value(wire_symbols::kFailed.data(),
               OffWorkflowProfileOutcome::kFailed);
    type.value(wire_symbols::kIncomplete.data(),
               OffWorkflowProfileOutcome::kIncomplete);
  }
  if (!nb::type<OffWorkflowPublicationOutcome>().is_valid()) {
    auto type = nb::enum_<OffWorkflowPublicationOutcome>(
        module, "OffGlbOffWorkflowPublicationOutcome");
    type.value(wire_symbols::kNotRequested.data(),
               OffWorkflowPublicationOutcome::kNotRequested);
    type.value(wire_symbols::kNotAttempted.data(),
               OffWorkflowPublicationOutcome::kNotAttempted);
    type.value(wire_symbols::kFailed.data(),
               OffWorkflowPublicationOutcome::kFailed);
    type.value(wire_symbols::kCanceled.data(),
               OffWorkflowPublicationOutcome::kCanceled);
    type.value(wire_symbols::kPublished.data(),
               OffWorkflowPublicationOutcome::kPublished);
  }
  if (!nb::type<OffWorkflowStage>().is_valid()) {
    auto type = nb::enum_<OffWorkflowStage>(module, "OffGlbOffWorkflowStage");
    type.value(wire_symbols::kConfiguration.data(),
               OffWorkflowStage::kConfiguration);
    type.value(wire_symbols::kSourceProtection.data(),
               OffWorkflowStage::kSourceProtection);
    type.value(wire_symbols::kRepair.data(), OffWorkflowStage::kRepair);
    type.value(wire_symbols::kWrite.data(), OffWorkflowStage::kWrite);
    type.value(wire_symbols::kReload.data(), OffWorkflowStage::kReload);
    type.value(wire_symbols::kVerify.data(), OffWorkflowStage::kVerify);
    type.value(wire_symbols::kHash.data(), OffWorkflowStage::kHash);
    type.value(wire_symbols::kReport.data(), OffWorkflowStage::kReport);
    type.value(wire_symbols::kPublication.data(),
               OffWorkflowStage::kPublication);
    type.value(wire_symbols::kComplete.data(), OffWorkflowStage::kComplete);
  }
  if (!nb::type<OffWorkflowExitMeaning>().is_valid()) {
    auto type = nb::enum_<OffWorkflowExitMeaning>(
        module, "OffGlbOffWorkflowExitMeaning");
    type.value(wire_symbols::kSuccess.data(), OffWorkflowExitMeaning::kSuccess);
    type.value(wire_symbols::kRejectedOrIncomplete.data(),
               OffWorkflowExitMeaning::kRejectedOrIncomplete);
    type.value(wire_symbols::kExecutionFailure.data(),
               OffWorkflowExitMeaning::kExecutionFailure);
    type.value(wire_symbols::kCanceled.data(),
               OffWorkflowExitMeaning::kCanceled);
  }
  if (!nb::type<OffWorkflowDiagnosticCode>().is_valid()) {
    auto type = nb::enum_<OffWorkflowDiagnosticCode>(
        module, "OffGlbOffWorkflowDiagnosticCode");
    type.value(wire_symbols::kNone.data(), OffWorkflowDiagnosticCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               OffWorkflowDiagnosticCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               OffWorkflowDiagnosticCode::kConfigurationExtent);
    type.value(wire_symbols::kInputIdentity.data(),
               OffWorkflowDiagnosticCode::kInputIdentity);
    type.value(wire_symbols::kPolicy.data(),
               OffWorkflowDiagnosticCode::kPolicy);
    type.value(wire_symbols::kNameUnsupported.data(),
               OffWorkflowDiagnosticCode::kNameUnsupported);
    type.value(wire_symbols::kNameCollision.data(),
               OffWorkflowDiagnosticCode::kNameCollision);
    type.value(wire_symbols::kCountLimit.data(),
               OffWorkflowDiagnosticCode::kCountLimit);
    type.value(wire_symbols::kRequestBudget.data(),
               OffWorkflowDiagnosticCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               OffWorkflowDiagnosticCode::kStopRequested);
    type.value(wire_symbols::kRepair.data(),
               OffWorkflowDiagnosticCode::kRepair);
    type.value(wire_symbols::kWrite.data(), OffWorkflowDiagnosticCode::kWrite);
    type.value(wire_symbols::kReload.data(),
               OffWorkflowDiagnosticCode::kReload);
    type.value(wire_symbols::kCorrespondence.data(),
               OffWorkflowDiagnosticCode::kCorrespondence);
    type.value(wire_symbols::kCanonicalPreservation.data(),
               OffWorkflowDiagnosticCode::kCanonicalPreservation);
    type.value(wire_symbols::kReloadMismatch.data(),
               OffWorkflowDiagnosticCode::kReloadMismatch);
    type.value(wire_symbols::kSourceChanged.data(),
               OffWorkflowDiagnosticCode::kSourceChanged);
    type.value(wire_symbols::kHashExtent.data(),
               OffWorkflowDiagnosticCode::kHashExtent);
    type.value(wire_symbols::kReportExtent.data(),
               OffWorkflowDiagnosticCode::kReportExtent);
    type.value(wire_symbols::kReportStringEncoding.data(),
               OffWorkflowDiagnosticCode::kReportStringEncoding);
    type.value(wire_symbols::kPublication.data(),
               OffWorkflowDiagnosticCode::kPublication);
    type.value(wire_symbols::kBlocked.data(),
               OffWorkflowDiagnosticCode::kBlocked);
    type.value(wire_symbols::kSolidUnavailable.data(),
               OffWorkflowDiagnosticCode::kSolidUnavailable);
  }
  if (!nb::type<MaterializedDuplicateStatus>().is_valid()) {
    auto type = nb::enum_<MaterializedDuplicateStatus>(
        module, "OffGlbMaterializedDuplicateStatus");
    type.value(wire_symbols::kSuccess.data(),
               MaterializedDuplicateStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               MaterializedDuplicateStatus::kInvalidConfiguration);
    type.value(wire_symbols::kLimitExceeded.data(),
               MaterializedDuplicateStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               MaterializedDuplicateStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(),
               MaterializedDuplicateStatus::kCanceled);
    type.value(wire_symbols::kInvalidSourceStorage.data(),
               MaterializedDuplicateStatus::kInvalidSourceStorage);
    type.value(wire_symbols::kRejected.data(),
               MaterializedDuplicateStatus::kRejected);
    type.value(wire_symbols::kGeometryFailure.data(),
               MaterializedDuplicateStatus::kGeometryFailure);
    type.value(wire_symbols::kVerificationFailure.data(),
               MaterializedDuplicateStatus::kVerificationFailure);
  }
  if (!nb::type<MaterializedDuplicateStage>().is_valid()) {
    auto type = nb::enum_<MaterializedDuplicateStage>(
        module, "OffGlbMaterializedDuplicateStage");
    type.value(wire_symbols::kConfiguration.data(),
               MaterializedDuplicateStage::kConfiguration);
    type.value(wire_symbols::kSourceInspection.data(),
               MaterializedDuplicateStage::kSourceInspection);
    type.value(wire_symbols::kEligibility.data(),
               MaterializedDuplicateStage::kEligibility);
    type.value(wire_symbols::kTargets.data(),
               MaterializedDuplicateStage::kTargets);
    type.value(wire_symbols::kComparison.data(),
               MaterializedDuplicateStage::kComparison);
    type.value(wire_symbols::kSelection.data(),
               MaterializedDuplicateStage::kSelection);
    type.value(wire_symbols::kCorrespondence.data(),
               MaterializedDuplicateStage::kCorrespondence);
    type.value(wire_symbols::kCandidateInspection.data(),
               MaterializedDuplicateStage::kCandidateInspection);
    type.value(wire_symbols::kVerification.data(),
               MaterializedDuplicateStage::kVerification);
    type.value(wire_symbols::kPublication.data(),
               MaterializedDuplicateStage::kPublication);
  }
  if (!nb::type<MaterializedDuplicateDiagnosticCode>().is_valid()) {
    auto type = nb::enum_<MaterializedDuplicateDiagnosticCode>(
        module, "OffGlbMaterializedDuplicateDiagnosticCode");
    type.value(wire_symbols::kNone.data(),
               MaterializedDuplicateDiagnosticCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               MaterializedDuplicateDiagnosticCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               MaterializedDuplicateDiagnosticCode::kConfigurationExtent);
    type.value(wire_symbols::kDeclarationDomain.data(),
               MaterializedDuplicateDiagnosticCode::kDeclarationDomain);
    type.value(wire_symbols::kDeclarationNameEmpty.data(),
               MaterializedDuplicateDiagnosticCode::kDeclarationNameEmpty);
    type.value(wire_symbols::kDeclarationDuplicate.data(),
               MaterializedDuplicateDiagnosticCode::kDeclarationDuplicate);
    type.value(wire_symbols::kTargetLimit.data(),
               MaterializedDuplicateDiagnosticCode::kTargetLimit);
    type.value(wire_symbols::kDeclarationLimit.data(),
               MaterializedDuplicateDiagnosticCode::kDeclarationLimit);
    type.value(wire_symbols::kDeclarationNameByteLimit.data(),
               MaterializedDuplicateDiagnosticCode::kDeclarationNameByteLimit);
    type.value(wire_symbols::kComparisonLimit.data(),
               MaterializedDuplicateDiagnosticCode::kComparisonLimit);
    type.value(wire_symbols::kRequestBudget.data(),
               MaterializedDuplicateDiagnosticCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               MaterializedDuplicateDiagnosticCode::kStopRequested);
    type.value(wire_symbols::kStorageIssues.data(),
               MaterializedDuplicateDiagnosticCode::kStorageIssues);
    type.value(wire_symbols::kNonfiniteAttribute.data(),
               MaterializedDuplicateDiagnosticCode::kNonfiniteAttribute);
    type.value(wire_symbols::kUnsupportedAttribute.data(),
               MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute);
    type.value(wire_symbols::kTargetRange.data(),
               MaterializedDuplicateDiagnosticCode::kTargetRange);
    type.value(wire_symbols::kSelfTarget.data(),
               MaterializedDuplicateDiagnosticCode::kSelfTarget);
    type.value(wire_symbols::kRepeatedTarget.data(),
               MaterializedDuplicateDiagnosticCode::kRepeatedTarget);
    type.value(wire_symbols::kRemovedRepresentative.data(),
               MaterializedDuplicateDiagnosticCode::kRemovedRepresentative);
    type.value(wire_symbols::kNotEquivalent.data(),
               MaterializedDuplicateDiagnosticCode::kNotEquivalent);
    type.value(wire_symbols::kGeometry.data(),
               MaterializedDuplicateDiagnosticCode::kGeometry);
    type.value(wire_symbols::kPostcondition.data(),
               MaterializedDuplicateDiagnosticCode::kPostcondition);
  }
  if (!nb::type<geo::ChargedTriangulationStatus>().is_valid()) {
    auto type = nb::enum_<geo::ChargedTriangulationStatus>(
        module, "OffGlbGeometryChargedTriangulationStatus");
    type.value(wire_symbols::kAccepted.data(),
               geo::ChargedTriangulationStatus::kAccepted);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               geo::ChargedTriangulationStatus::kInvalidConfiguration);
    type.value(wire_symbols::kLimitExceeded.data(),
               geo::ChargedTriangulationStatus::kLimitExceeded);
    type.value(wire_symbols::kBlocked.data(),
               geo::ChargedTriangulationStatus::kBlocked);
    type.value(wire_symbols::kUnsupportedAllocationProfile.data(),
               geo::ChargedTriangulationStatus::kUnsupportedAllocationProfile);
    type.value(wire_symbols::kBudgetExceeded.data(),
               geo::ChargedTriangulationStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(),
               geo::ChargedTriangulationStatus::kCanceled);
    type.value(wire_symbols::kDelegateFailure.data(),
               geo::ChargedTriangulationStatus::kDelegateFailure);
  }
  if (!nb::type<geo::ChargedTriangulationStage>().is_valid()) {
    auto type = nb::enum_<geo::ChargedTriangulationStage>(
        module, "OffGlbGeometryChargedTriangulationStage");
    type.value(wire_symbols::kConfiguration.data(),
               geo::ChargedTriangulationStage::kConfiguration);
    type.value(wire_symbols::kSourceCounts.data(),
               geo::ChargedTriangulationStage::kSourceCounts);
    type.value(wire_symbols::kSourceCapture.data(),
               geo::ChargedTriangulationStage::kSourceCapture);
    type.value(wire_symbols::kSourceInspection.data(),
               geo::ChargedTriangulationStage::kSourceInspection);
    type.value(wire_symbols::kTriangulation.data(),
               geo::ChargedTriangulationStage::kTriangulation);
    type.value(wire_symbols::kCorrespondence.data(),
               geo::ChargedTriangulationStage::kCorrespondence);
    type.value(wire_symbols::kAttributeTransfer.data(),
               geo::ChargedTriangulationStage::kAttributeTransfer);
    type.value(wire_symbols::kOutputMaterialization.data(),
               geo::ChargedTriangulationStage::kOutputMaterialization);
    type.value(wire_symbols::kOutputInspection.data(),
               geo::ChargedTriangulationStage::kOutputInspection);
    type.value(wire_symbols::kComplete.data(),
               geo::ChargedTriangulationStage::kComplete);
  }
  if (!nb::type<geo::ChargedTriangulationCode>().is_valid()) {
    auto type = nb::enum_<geo::ChargedTriangulationCode>(
        module, "OffGlbGeometryChargedTriangulationCode");
    type.value(wire_symbols::kNone.data(),
               geo::ChargedTriangulationCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               geo::ChargedTriangulationCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               geo::ChargedTriangulationCode::kConfigurationExtent);
    type.value(wire_symbols::kUnsupportedCornerLimit.data(),
               geo::ChargedTriangulationCode::kUnsupportedCornerLimit);
    type.value(wire_symbols::kSourceCountLimit.data(),
               geo::ChargedTriangulationCode::kSourceCountLimit);
    type.value(wire_symbols::kOutputCountLimit.data(),
               geo::ChargedTriangulationCode::kOutputCountLimit);
    type.value(wire_symbols::kMapEntryLimit.data(),
               geo::ChargedTriangulationCode::kMapEntryLimit);
    type.value(wire_symbols::kCoreRequestBudget.data(),
               geo::ChargedTriangulationCode::kCoreRequestBudget);
    type.value(wire_symbols::kBridgeRequestBudget.data(),
               geo::ChargedTriangulationCode::kBridgeRequestBudget);
    type.value(wire_symbols::kSourceStorage.data(),
               geo::ChargedTriangulationCode::kSourceStorage);
    type.value(wire_symbols::kFaceCornerLimit.data(),
               geo::ChargedTriangulationCode::kFaceCornerLimit);
    type.value(wire_symbols::kRepeatedPosition.data(),
               geo::ChargedTriangulationCode::kRepeatedPosition);
    type.value(wire_symbols::kDegenerateFace.data(),
               geo::ChargedTriangulationCode::kDegenerateFace);
    type.value(wire_symbols::kNonplanarFace.data(),
               geo::ChargedTriangulationCode::kNonplanarFace);
    type.value(wire_symbols::kBacktrackingBoundary.data(),
               geo::ChargedTriangulationCode::kBacktrackingBoundary);
    type.value(wire_symbols::kSelfIntersection.data(),
               geo::ChargedTriangulationCode::kSelfIntersection);
    type.value(wire_symbols::kNoGeometricEar.data(),
               geo::ChargedTriangulationCode::kNoGeometricEar);
    type.value(wire_symbols::kDegenerateResult.data(),
               geo::ChargedTriangulationCode::kDegenerateResult);
    type.value(wire_symbols::kSizeOverflow.data(),
               geo::ChargedTriangulationCode::kSizeOverflow);
    type.value(wire_symbols::kRequestBudget.data(),
               geo::ChargedTriangulationCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               geo::ChargedTriangulationCode::kStopRequested);
    type.value(wire_symbols::kMaterialization.data(),
               geo::ChargedTriangulationCode::kMaterialization);
    type.value(wire_symbols::kInspection.data(),
               geo::ChargedTriangulationCode::kInspection);
    type.value(wire_symbols::kOutputStorage.data(),
               geo::ChargedTriangulationCode::kOutputStorage);
    type.value(wire_symbols::kAllocationProfile.data(),
               geo::ChargedTriangulationCode::kAllocationProfile);
  }
  if (!nb::type<geo::TriangulationStatus>().is_valid()) {
    auto type = nb::enum_<geo::TriangulationStatus>(
        module, "OffGlbGeometryTriangulationStatus");
    type.value(wire_symbols::kAccepted.data(),
               geo::TriangulationStatus::kAccepted);
    type.value(wire_symbols::kBlocked.data(),
               geo::TriangulationStatus::kBlocked);
    type.value(wire_symbols::kCanceled.data(),
               geo::TriangulationStatus::kCanceled);
  }
  if (!nb::type<geo::ChargedTriangulationSerialReason>().is_valid()) {
    auto type = nb::enum_<geo::ChargedTriangulationSerialReason>(
        module, "OffGlbGeometryChargedTriangulationSerialReason");
    type.value(wire_symbols::kNone.data(),
               geo::ChargedTriangulationSerialReason::kNone);
    type.value(wire_symbols::kEmptyMesh.data(),
               geo::ChargedTriangulationSerialReason::kEmptyMesh);
    type.value(wire_symbols::kWorkerBudgetOne.data(),
               geo::ChargedTriangulationSerialReason::kWorkerBudgetOne);
    type.value(wire_symbols::kBelowFaceThreshold.data(),
               geo::ChargedTriangulationSerialReason::kBelowFaceThreshold);
    type.value(
        wire_symbols::kSharedWorkersUnavailableOrFewerThanTwoFaces.data(),
        geo::ChargedTriangulationSerialReason::
            kSharedWorkersUnavailableOrFewerThanTwoFaces);
  }
  if (!nb::type<ic::SurfaceStatus>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceStatus>(module, "OffGlbSurfaceStatus");
    type.value(wire_symbols::kSuccess.data(), ic::SurfaceStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               ic::SurfaceStatus::kInvalidConfiguration);
    type.value(wire_symbols::kInvalidStorage.data(),
               ic::SurfaceStatus::kInvalidStorage);
    type.value(wire_symbols::kUnsupported.data(),
               ic::SurfaceStatus::kUnsupported);
    type.value(wire_symbols::kLimitExceeded.data(),
               ic::SurfaceStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               ic::SurfaceStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(), ic::SurfaceStatus::kCanceled);
    type.value(wire_symbols::kDelegateFailure.data(),
               ic::SurfaceStatus::kDelegateFailure);
    type.value(wire_symbols::kVerificationFailed.data(),
               ic::SurfaceStatus::kVerificationFailed);
  }
  if (!nb::type<ic::SurfaceStage>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceStage>(module, "OffGlbSurfaceStage");
    type.value(wire_symbols::kConfiguration.data(),
               ic::SurfaceStage::kConfiguration);
    type.value(wire_symbols::kSource.data(), ic::SurfaceStage::kSource);
    type.value(wire_symbols::kCorrespondence.data(),
               ic::SurfaceStage::kCorrespondence);
    type.value(wire_symbols::kDisposition.data(),
               ic::SurfaceStage::kDisposition);
    type.value(wire_symbols::kCatalog.data(), ic::SurfaceStage::kCatalog);
    type.value(wire_symbols::kPlan.data(), ic::SurfaceStage::kPlan);
    type.value(wire_symbols::kNumeric.data(), ic::SurfaceStage::kNumeric);
    type.value(wire_symbols::kEncode.data(), ic::SurfaceStage::kEncode);
    type.value(wire_symbols::kCapture.data(), ic::SurfaceStage::kCapture);
    type.value(wire_symbols::kContainerRead.data(),
               ic::SurfaceStage::kContainerRead);
    type.value(wire_symbols::kAssetRead.data(), ic::SurfaceStage::kAssetRead);
    type.value(wire_symbols::kPrimitiveRead.data(),
               ic::SurfaceStage::kPrimitiveRead);
    type.value(wire_symbols::kCapsuleRead.data(),
               ic::SurfaceStage::kCapsuleRead);
    type.value(wire_symbols::kCompare.data(), ic::SurfaceStage::kCompare);
    type.value(wire_symbols::kComplete.data(), ic::SurfaceStage::kComplete);
  }
  if (!nb::type<ic::SurfaceCode>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceCode>(module, "OffGlbSurfaceCode");
    type.value(wire_symbols::kNone.data(), ic::SurfaceCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               ic::SurfaceCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               ic::SurfaceCode::kConfigurationExtent);
    type.value(wire_symbols::kUnknownEnum.data(),
               ic::SurfaceCode::kUnknownEnum);
    type.value(wire_symbols::kEncodingLimitsMismatch.data(),
               ic::SurfaceCode::kEncodingLimitsMismatch);
    type.value(wire_symbols::kInvalidBound.data(),
               ic::SurfaceCode::kInvalidBound);
    type.value(wire_symbols::kAllowanceTooSmall.data(),
               ic::SurfaceCode::kAllowanceTooSmall);
    type.value(wire_symbols::kForeignLedger.data(),
               ic::SurfaceCode::kForeignLedger);
    type.value(wire_symbols::kInvalidSourceStorage.data(),
               ic::SurfaceCode::kInvalidSourceStorage);
    type.value(wire_symbols::kSourceCount.data(),
               ic::SurfaceCode::kSourceCount);
    type.value(wire_symbols::kTriangleCount.data(),
               ic::SurfaceCode::kTriangleCount);
    type.value(wire_symbols::kSourceCorrespondence.data(),
               ic::SurfaceCode::kSourceCorrespondence);
    type.value(wire_symbols::kChannelCoverage.data(),
               ic::SurfaceCode::kChannelCoverage);
    type.value(wire_symbols::kChannelRole.data(),
               ic::SurfaceCode::kChannelRole);
    type.value(wire_symbols::kChannelShape.data(),
               ic::SurfaceCode::kChannelShape);
    type.value(wire_symbols::kChannelMetadata.data(),
               ic::SurfaceCode::kChannelMetadata);
    type.value(wire_symbols::kBareUnrepresented.data(),
               ic::SurfaceCode::kBareUnrepresented);
    type.value(wire_symbols::kLossNotAcknowledged.data(),
               ic::SurfaceCode::kLossNotAcknowledged);
    type.value(wire_symbols::kUvMap.data(), ic::SurfaceCode::kUvMap);
    type.value(wire_symbols::kUvReference.data(),
               ic::SurfaceCode::kUvReference);
    type.value(wire_symbols::kNormalAbsent.data(),
               ic::SurfaceCode::kNormalAbsent);
    type.value(wire_symbols::kNormalMissing.data(),
               ic::SurfaceCode::kNormalMissing);
    type.value(wire_symbols::kNormalNotConstant.data(),
               ic::SurfaceCode::kNormalNotConstant);
    type.value(wire_symbols::kNonfinite.data(), ic::SurfaceCode::kNonfinite);
    type.value(wire_symbols::kMaterialIds.data(),
               ic::SurfaceCode::kMaterialIds);
    type.value(wire_symbols::kMaterialBinding.data(),
               ic::SurfaceCode::kMaterialBinding);
    type.value(wire_symbols::kCatalogField.data(),
               ic::SurfaceCode::kCatalogField);
    type.value(wire_symbols::kCatalogExtension.data(),
               ic::SurfaceCode::kCatalogExtension);
    type.value(wire_symbols::kResourceBinding.data(),
               ic::SurfaceCode::kResourceBinding);
    type.value(wire_symbols::kResourceMedia.data(),
               ic::SurfaceCode::kResourceMedia);
    type.value(wire_symbols::kCountLimit.data(), ic::SurfaceCode::kCountLimit);
    type.value(wire_symbols::kDescriptorLimit.data(),
               ic::SurfaceCode::kDescriptorLimit);
    type.value(wire_symbols::kOutputLimit.data(),
               ic::SurfaceCode::kOutputLimit);
    type.value(wire_symbols::kMapLimit.data(), ic::SurfaceCode::kMapLimit);
    type.value(wire_symbols::kSizeOverflow.data(),
               ic::SurfaceCode::kSizeOverflow);
    type.value(wire_symbols::kNumericRange.data(),
               ic::SurfaceCode::kNumericRange);
    type.value(wire_symbols::kNonzeroUnderflow.data(),
               ic::SurfaceCode::kNonzeroUnderflow);
    type.value(wire_symbols::kExactness.data(), ic::SurfaceCode::kExactness);
    type.value(wire_symbols::kErrorBound.data(), ic::SurfaceCode::kErrorBound);
    type.value(wire_symbols::kNewCoincidence.data(),
               ic::SurfaceCode::kNewCoincidence);
    type.value(wire_symbols::kCollapsedEdge.data(),
               ic::SurfaceCode::kCollapsedEdge);
    type.value(wire_symbols::kCollapsedTriangle.data(),
               ic::SurfaceCode::kCollapsedTriangle);
    type.value(wire_symbols::kOrientation.data(),
               ic::SurfaceCode::kOrientation);
    type.value(wire_symbols::kRequestBudget.data(),
               ic::SurfaceCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               ic::SurfaceCode::kStopRequested);
    type.value(wire_symbols::kGeometryDelegate.data(),
               ic::SurfaceCode::kGeometryDelegate);
    type.value(wire_symbols::kStlDelegate.data(),
               ic::SurfaceCode::kStlDelegate);
    type.value(wire_symbols::kGlbDelegate.data(),
               ic::SurfaceCode::kGlbDelegate);
    type.value(wire_symbols::kGltfDelegate.data(),
               ic::SurfaceCode::kGltfDelegate);
    type.value(wire_symbols::kMemberInventory.data(),
               ic::SurfaceCode::kMemberInventory);
    type.value(wire_symbols::kMemberPath.data(), ic::SurfaceCode::kMemberPath);
    type.value(wire_symbols::kMemberDigest.data(),
               ic::SurfaceCode::kMemberDigest);
    type.value(wire_symbols::kJsonSyntax.data(), ic::SurfaceCode::kJsonSyntax);
    type.value(wire_symbols::kSchema.data(), ic::SurfaceCode::kSchema);
    type.value(wire_symbols::kBinarySlice.data(),
               ic::SurfaceCode::kBinarySlice);
    type.value(wire_symbols::kCapsuleSource.data(),
               ic::SurfaceCode::kCapsuleSource);
    type.value(wire_symbols::kWireShape.data(), ic::SurfaceCode::kWireShape);
    type.value(wire_symbols::kWireValue.data(), ic::SurfaceCode::kWireValue);
    type.value(wire_symbols::kWireMap.data(), ic::SurfaceCode::kWireMap);
    type.value(wire_symbols::kMaterialReference.data(),
               ic::SurfaceCode::kMaterialReference);
    type.value(wire_symbols::kResourceBytes.data(),
               ic::SurfaceCode::kResourceBytes);
    type.value(wire_symbols::kUnits.data(), ic::SurfaceCode::kUnits);
    type.value(wire_symbols::kNumericProfile.data(),
               ic::SurfaceCode::kNumericProfile);
  }
  if (!nb::type<ic::SurfaceSubject>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceSubject>(module, "OffGlbSurfaceSubject");
    type.value(wire_symbols::kNone.data(), ic::SurfaceSubject::kNone);
    type.value(wire_symbols::kVertex.data(), ic::SurfaceSubject::kVertex);
    type.value(wire_symbols::kFace.data(), ic::SurfaceSubject::kFace);
    type.value(wire_symbols::kCorner.data(), ic::SurfaceSubject::kCorner);
    type.value(wire_symbols::kAttribute.data(), ic::SurfaceSubject::kAttribute);
    type.value(wire_symbols::kScalar.data(), ic::SurfaceSubject::kScalar);
    type.value(wire_symbols::kMember.data(), ic::SurfaceSubject::kMember);
    type.value(wire_symbols::kMaterial.data(), ic::SurfaceSubject::kMaterial);
    type.value(wire_symbols::kTexture.data(), ic::SurfaceSubject::kTexture);
    type.value(wire_symbols::kImage.data(), ic::SurfaceSubject::kImage);
    type.value(wire_symbols::kSampler.data(), ic::SurfaceSubject::kSampler);
    type.value(wire_symbols::kPrimitive.data(), ic::SurfaceSubject::kPrimitive);
    type.value(wire_symbols::kUvSet.data(), ic::SurfaceSubject::kUvSet);
  }
  if (!nb::type<ic::SurfaceFilePublicationOutcome>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceFilePublicationOutcome>(
        module, "OffGlbSurfaceFilePublicationOutcome");
    type.value(wire_symbols::kFailed.data(),
               ic::SurfaceFilePublicationOutcome::kFailed);
    type.value(wire_symbols::kCanceled.data(),
               ic::SurfaceFilePublicationOutcome::kCanceled);
    type.value(wire_symbols::kPublished.data(),
               ic::SurfaceFilePublicationOutcome::kPublished);
  }
  if (!nb::type<ic::SurfaceFileStatus>().is_valid()) {
    auto type =
        nb::enum_<ic::SurfaceFileStatus>(module, "OffGlbSurfaceFileStatus");
    type.value(wire_symbols::kSuccess.data(), ic::SurfaceFileStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               ic::SurfaceFileStatus::kInvalidConfiguration);
    type.value(wire_symbols::kInvalidStorage.data(),
               ic::SurfaceFileStatus::kInvalidStorage);
    type.value(wire_symbols::kLimitExceeded.data(),
               ic::SurfaceFileStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               ic::SurfaceFileStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(),
               ic::SurfaceFileStatus::kCanceled);
    type.value(wire_symbols::kUnsupportedProfile.data(),
               ic::SurfaceFileStatus::kUnsupportedProfile);
    type.value(wire_symbols::kIoFailure.data(),
               ic::SurfaceFileStatus::kIoFailure);
    type.value(wire_symbols::kVerificationFailed.data(),
               ic::SurfaceFileStatus::kVerificationFailed);
    type.value(wire_symbols::kDestinationExists.data(),
               ic::SurfaceFileStatus::kDestinationExists);
    type.value(wire_symbols::kPublicationFailed.data(),
               ic::SurfaceFileStatus::kPublicationFailed);
    type.value(wire_symbols::kCallbackFailure.data(),
               ic::SurfaceFileStatus::kCallbackFailure);
  }
  if (!nb::type<ic::SurfaceFileStage>().is_valid()) {
    auto type =
        nb::enum_<ic::SurfaceFileStage>(module, "OffGlbSurfaceFileStage");
    type.value(wire_symbols::kConfiguration.data(),
               ic::SurfaceFileStage::kConfiguration);
    type.value(wire_symbols::kPathPlanning.data(),
               ic::SurfaceFileStage::kPathPlanning);
    type.value(wire_symbols::kStaging.data(), ic::SurfaceFileStage::kStaging);
    type.value(wire_symbols::kReread.data(), ic::SurfaceFileStage::kReread);
    type.value(wire_symbols::kInspection.data(),
               ic::SurfaceFileStage::kInspection);
    type.value(wire_symbols::kReceipt.data(), ic::SurfaceFileStage::kReceipt);
    type.value(wire_symbols::kInventory.data(),
               ic::SurfaceFileStage::kInventory);
    type.value(wire_symbols::kPublication.data(),
               ic::SurfaceFileStage::kPublication);
    type.value(wire_symbols::kComplete.data(), ic::SurfaceFileStage::kComplete);
  }
  if (!nb::type<ic::SurfaceFileCode>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceFileCode>(module, "OffGlbSurfaceFileCode");
    type.value(wire_symbols::kNone.data(), ic::SurfaceFileCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               ic::SurfaceFileCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               ic::SurfaceFileCode::kConfigurationExtent);
    type.value(wire_symbols::kUnknownEnum.data(),
               ic::SurfaceFileCode::kUnknownEnum);
    type.value(wire_symbols::kEncodingLimitsMismatch.data(),
               ic::SurfaceFileCode::kEncodingLimitsMismatch);
    type.value(wire_symbols::kAllowanceTooSmall.data(),
               ic::SurfaceFileCode::kAllowanceTooSmall);
    type.value(wire_symbols::kProviderConfiguration.data(),
               ic::SurfaceFileCode::kProviderConfiguration);
    type.value(wire_symbols::kDestinationPath.data(),
               ic::SurfaceFileCode::kDestinationPath);
    type.value(wire_symbols::kReceiptPath.data(),
               ic::SurfaceFileCode::kReceiptPath);
    type.value(wire_symbols::kPathCollision.data(),
               ic::SurfaceFileCode::kPathCollision);
    type.value(wire_symbols::kSourceContract.data(),
               ic::SurfaceFileCode::kSourceContract);
    type.value(wire_symbols::kCountLimit.data(),
               ic::SurfaceFileCode::kCountLimit);
    type.value(wire_symbols::kRequestBudget.data(),
               ic::SurfaceFileCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               ic::SurfaceFileCode::kStopRequested);
    type.value(wire_symbols::kUnsupportedProfile.data(),
               ic::SurfaceFileCode::kUnsupportedProfile);
    type.value(wire_symbols::kDestinationParent.data(),
               ic::SurfaceFileCode::kDestinationParent);
    type.value(wire_symbols::kStageCreate.data(),
               ic::SurfaceFileCode::kStageCreate);
    type.value(wire_symbols::kStageAttemptsExhausted.data(),
               ic::SurfaceFileCode::kStageAttemptsExhausted);
    type.value(wire_symbols::kFileOpen.data(), ic::SurfaceFileCode::kFileOpen);
    type.value(wire_symbols::kFileRead.data(), ic::SurfaceFileCode::kFileRead);
    type.value(wire_symbols::kFileWrite.data(),
               ic::SurfaceFileCode::kFileWrite);
    type.value(wire_symbols::kFileClose.data(),
               ic::SurfaceFileCode::kFileClose);
    type.value(wire_symbols::kEnumeration.data(),
               ic::SurfaceFileCode::kEnumeration);
    type.value(wire_symbols::kMemberInventory.data(),
               ic::SurfaceFileCode::kMemberInventory);
    type.value(wire_symbols::kFileChanged.data(),
               ic::SurfaceFileCode::kFileChanged);
    type.value(wire_symbols::kFileKind.data(), ic::SurfaceFileCode::kFileKind);
    type.value(wire_symbols::kInspection.data(),
               ic::SurfaceFileCode::kInspection);
    type.value(wire_symbols::kCallerReportEncoding.data(),
               ic::SurfaceFileCode::kCallerReportEncoding);
    type.value(wire_symbols::kReceiptSize.data(),
               ic::SurfaceFileCode::kReceiptSize);
    type.value(wire_symbols::kReceiptBytes.data(),
               ic::SurfaceFileCode::kReceiptBytes);
    type.value(wire_symbols::kProvider.data(), ic::SurfaceFileCode::kProvider);
    type.value(wire_symbols::kDestinationExists.data(),
               ic::SurfaceFileCode::kDestinationExists);
    type.value(wire_symbols::kCommitFailed.data(),
               ic::SurfaceFileCode::kCommitFailed);
    type.value(wire_symbols::kCommitUnsupported.data(),
               ic::SurfaceFileCode::kCommitUnsupported);
  }
  if (!nb::type<ic::SurfaceFileCleanupOutcome>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceFileCleanupOutcome>(
        module, "OffGlbSurfaceFileCleanupOutcome");
    type.value(wire_symbols::kNotRequired.data(),
               ic::SurfaceFileCleanupOutcome::kNotRequired);
    type.value(wire_symbols::kRemoved.data(),
               ic::SurfaceFileCleanupOutcome::kRemoved);
    type.value(wire_symbols::kFailed.data(),
               ic::SurfaceFileCleanupOutcome::kFailed);
  }
  if (!nb::type<ic::SurfaceFilePhaseOutcome>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceFilePhaseOutcome>(
        module, "OffGlbSurfaceFilePhaseOutcome");
    type.value(wire_symbols::kCompleted.data(),
               ic::SurfaceFilePhaseOutcome::kCompleted);
    type.value(wire_symbols::kFailed.data(),
               ic::SurfaceFilePhaseOutcome::kFailed);
    type.value(wire_symbols::kCanceled.data(),
               ic::SurfaceFilePhaseOutcome::kCanceled);
  }
  if (!nb::type<ic::SurfaceFileCommitOutcome>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceFileCommitOutcome>(
        module, "OffGlbSurfaceFileCommitOutcome");
    type.value(wire_symbols::kCommitted.data(),
               ic::SurfaceFileCommitOutcome::kCommitted);
    type.value(wire_symbols::kDestinationExists.data(),
               ic::SurfaceFileCommitOutcome::kDestinationExists);
    type.value(wire_symbols::kFailed.data(),
               ic::SurfaceFileCommitOutcome::kFailed);
    type.value(wire_symbols::kUnsupported.data(),
               ic::SurfaceFileCommitOutcome::kUnsupported);
  }
  if (!nb::type<ic::SurfaceDelegateKind>().is_valid()) {
    auto type =
        nb::enum_<ic::SurfaceDelegateKind>(module, "OffGlbSurfaceDelegateKind");
    type.value(wire_symbols::kDerivedMaterialization.data(),
               ic::SurfaceDelegateKind::kDerivedMaterialization);
    type.value(wire_symbols::kStlWrite.data(),
               ic::SurfaceDelegateKind::kStlWrite);
    type.value(wire_symbols::kGlbWrite.data(),
               ic::SurfaceDelegateKind::kGlbWrite);
    type.value(wire_symbols::kGlbRead.data(),
               ic::SurfaceDelegateKind::kGlbRead);
    type.value(wire_symbols::kGltfRead.data(),
               ic::SurfaceDelegateKind::kGltfRead);
    type.value(wire_symbols::kGltfExtract.data(),
               ic::SurfaceDelegateKind::kGltfExtract);
    type.value(wire_symbols::kStlRead.data(),
               ic::SurfaceDelegateKind::kStlRead);
    type.value(wire_symbols::kSourceMaterialFactors.data(),
               ic::SurfaceDelegateKind::kSourceMaterialFactors);
    type.value(wire_symbols::kWireMaterialFactors.data(),
               ic::SurfaceDelegateKind::kWireMaterialFactors);
  }
  if (!nb::type<ic::GlbStatus>().is_valid()) {
    auto type = nb::enum_<ic::GlbStatus>(module, "OffGlbGlbStatus");
    type.value(wire_symbols::kSuccess.data(), ic::GlbStatus::kSuccess);
    type.value(wire_symbols::kInvalidOptions.data(),
               ic::GlbStatus::kInvalidOptions);
    type.value(wire_symbols::kMalformed.data(), ic::GlbStatus::kMalformed);
    type.value(wire_symbols::kUnsupportedVersion.data(),
               ic::GlbStatus::kUnsupportedVersion);
    type.value(wire_symbols::kLimitExceeded.data(),
               ic::GlbStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               ic::GlbStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(), ic::GlbStatus::kCanceled);
  }
  if (!nb::type<ic::GltfStatus>().is_valid()) {
    auto type = nb::enum_<ic::GltfStatus>(module, "OffGlbGltfStatus");
    type.value(wire_symbols::kAccepted.data(), ic::GltfStatus::kAccepted);
    type.value(wire_symbols::kInvalidLimits.data(),
               ic::GltfStatus::kInvalidLimits);
    type.value(wire_symbols::kInvalidRequest.data(),
               ic::GltfStatus::kInvalidRequest);
    type.value(wire_symbols::kMalformed.data(), ic::GltfStatus::kMalformed);
    type.value(wire_symbols::kUnsupportedProfile.data(),
               ic::GltfStatus::kUnsupportedProfile);
    type.value(wire_symbols::kResourceBindingMismatch.data(),
               ic::GltfStatus::kResourceBindingMismatch);
    type.value(wire_symbols::kCanceled.data(), ic::GltfStatus::kCanceled);
    type.value(wire_symbols::kBudgetExceeded.data(),
               ic::GltfStatus::kBudgetExceeded);
    type.value(wire_symbols::kInvalidSelection.data(),
               ic::GltfStatus::kInvalidSelection);
    type.value(wire_symbols::kForeignOwner.data(),
               ic::GltfStatus::kForeignOwner);
  }
  if (!nb::type<ic::GltfDiagnosticCode>().is_valid()) {
    auto type =
        nb::enum_<ic::GltfDiagnosticCode>(module, "OffGlbGltfDiagnosticCode");
    type.value(wire_symbols::kNone.data(), ic::GltfDiagnosticCode::kNone);
    type.value(wire_symbols::kZeroLimit.data(),
               ic::GltfDiagnosticCode::kZeroLimit);
    type.value(wire_symbols::kLimitNotRepresentable.data(),
               ic::GltfDiagnosticCode::kLimitNotRepresentable);
    type.value(wire_symbols::kInvalidRequest.data(),
               ic::GltfDiagnosticCode::kInvalidRequest);
    type.value(wire_symbols::kInvalidBindingKind.data(),
               ic::GltfDiagnosticCode::kInvalidBindingKind);
    type.value(wire_symbols::kUnsupportedResourceRepresentation.data(),
               ic::GltfDiagnosticCode::kUnsupportedResourceRepresentation);
    type.value(wire_symbols::kEmbeddedUriSyntax.data(),
               ic::GltfDiagnosticCode::kEmbeddedUriSyntax);
    type.value(wire_symbols::kEmbeddedMediaMismatch.data(),
               ic::GltfDiagnosticCode::kEmbeddedMediaMismatch);
    type.value(wire_symbols::kLimitExceeded.data(),
               ic::GltfDiagnosticCode::kLimitExceeded);
    type.value(wire_symbols::kSizeOverflow.data(),
               ic::GltfDiagnosticCode::kSizeOverflow);
    type.value(wire_symbols::kJsonSyntax.data(),
               ic::GltfDiagnosticCode::kJsonSyntax);
    type.value(wire_symbols::kUnicodeScalar.data(),
               ic::GltfDiagnosticCode::kUnicodeScalar);
    type.value(wire_symbols::kDecodedDuplicateKey.data(),
               ic::GltfDiagnosticCode::kDecodedDuplicateKey);
    type.value(wire_symbols::kCoreFieldType.data(),
               ic::GltfDiagnosticCode::kCoreFieldType);
    type.value(wire_symbols::kCoreFieldRequired.data(),
               ic::GltfDiagnosticCode::kCoreFieldRequired);
    type.value(wire_symbols::kExactInteger.data(),
               ic::GltfDiagnosticCode::kExactInteger);
    type.value(wire_symbols::kCoreRange.data(),
               ic::GltfDiagnosticCode::kCoreRange);
    type.value(wire_symbols::kCoreArrayLength.data(),
               ic::GltfDiagnosticCode::kCoreArrayLength);
    type.value(wire_symbols::kCoreReference.data(),
               ic::GltfDiagnosticCode::kCoreReference);
    type.value(wire_symbols::kGraphRelationship.data(),
               ic::GltfDiagnosticCode::kGraphRelationship);
    type.value(wire_symbols::kBinaryLayout.data(),
               ic::GltfDiagnosticCode::kBinaryLayout);
    type.value(wire_symbols::kBinaryValue.data(),
               ic::GltfDiagnosticCode::kBinaryValue);
    type.value(wire_symbols::kIndexRole.data(),
               ic::GltfDiagnosticCode::kIndexRole);
    type.value(wire_symbols::kUnsupportedEnum.data(),
               ic::GltfDiagnosticCode::kUnsupportedEnum);
    type.value(wire_symbols::kUnsupportedNumberRepresentation.data(),
               ic::GltfDiagnosticCode::kUnsupportedNumberRepresentation);
    type.value(wire_symbols::kUnsupportedCoreString.data(),
               ic::GltfDiagnosticCode::kUnsupportedCoreString);
    type.value(wire_symbols::kRequiredExtension.data(),
               ic::GltfDiagnosticCode::kRequiredExtension);
    type.value(wire_symbols::kMissingResource.data(),
               ic::GltfDiagnosticCode::kMissingResource);
    type.value(wire_symbols::kDuplicateResource.data(),
               ic::GltfDiagnosticCode::kDuplicateResource);
    type.value(wire_symbols::kExtraResource.data(),
               ic::GltfDiagnosticCode::kExtraResource);
    type.value(wire_symbols::kResourceExtent.data(),
               ic::GltfDiagnosticCode::kResourceExtent);
    type.value(wire_symbols::kCanceled.data(),
               ic::GltfDiagnosticCode::kCanceled);
    type.value(wire_symbols::kPayloadBudget.data(),
               ic::GltfDiagnosticCode::kPayloadBudget);
    type.value(wire_symbols::kInvalidSelection.data(),
               ic::GltfDiagnosticCode::kInvalidSelection);
    type.value(wire_symbols::kForeignOwner.data(),
               ic::GltfDiagnosticCode::kForeignOwner);
  }
  if (!nb::type<ic::GltfResourceKind>().is_valid()) {
    auto type =
        nb::enum_<ic::GltfResourceKind>(module, "OffGlbGltfResourceKind");
    type.value(wire_symbols::kInvalid.data(), ic::GltfResourceKind::kInvalid);
    type.value(wire_symbols::kBuffer.data(), ic::GltfResourceKind::kBuffer);
    type.value(wire_symbols::kImage.data(), ic::GltfResourceKind::kImage);
  }
  if (!nb::type<ic::StlStatus>().is_valid()) {
    auto type = nb::enum_<ic::StlStatus>(module, "OffGlbStlStatus");
    type.value(wire_symbols::kSuccess.data(), ic::StlStatus::kSuccess);
    type.value(wire_symbols::kInvalidConfiguration.data(),
               ic::StlStatus::kInvalidConfiguration);
    type.value(wire_symbols::kMalformed.data(), ic::StlStatus::kMalformed);
    type.value(wire_symbols::kUnsupportedVariant.data(),
               ic::StlStatus::kUnsupportedVariant);
    type.value(wire_symbols::kUnsupportedNumericConversion.data(),
               ic::StlStatus::kUnsupportedNumericConversion);
    type.value(wire_symbols::kLimitExceeded.data(),
               ic::StlStatus::kLimitExceeded);
    type.value(wire_symbols::kBudgetExceeded.data(),
               ic::StlStatus::kBudgetExceeded);
    type.value(wire_symbols::kCanceled.data(), ic::StlStatus::kCanceled);
    type.value(wire_symbols::kInvalidStorage.data(),
               ic::StlStatus::kInvalidStorage);
    type.value(wire_symbols::kGeometryFailure.data(),
               ic::StlStatus::kGeometryFailure);
  }
  if (!nb::type<ic::StlStage>().is_valid()) {
    auto type = nb::enum_<ic::StlStage>(module, "OffGlbStlStage");
    type.value(wire_symbols::kConfiguration.data(),
               ic::StlStage::kConfiguration);
    type.value(wire_symbols::kScan.data(), ic::StlStage::kScan);
    type.value(wire_symbols::kDecode.data(), ic::StlStage::kDecode);
    type.value(wire_symbols::kMaterialization.data(),
               ic::StlStage::kMaterialization);
    type.value(wire_symbols::kInspection.data(), ic::StlStage::kInspection);
    type.value(wire_symbols::kValidation.data(), ic::StlStage::kValidation);
    type.value(wire_symbols::kEncode.data(), ic::StlStage::kEncode);
    type.value(wire_symbols::kPublication.data(), ic::StlStage::kPublication);
  }
  if (!nb::type<ic::StlDiagnosticCode>().is_valid()) {
    auto type =
        nb::enum_<ic::StlDiagnosticCode>(module, "OffGlbStlDiagnosticCode");
    type.value(wire_symbols::kNone.data(), ic::StlDiagnosticCode::kNone);
    type.value(wire_symbols::kRequiredFieldZero.data(),
               ic::StlDiagnosticCode::kRequiredFieldZero);
    type.value(wire_symbols::kConfigurationExtent.data(),
               ic::StlDiagnosticCode::kConfigurationExtent);
    type.value(wire_symbols::kEncoding.data(),
               ic::StlDiagnosticCode::kEncoding);
    type.value(wire_symbols::kSourceLimit.data(),
               ic::StlDiagnosticCode::kSourceLimit);
    type.value(wire_symbols::kFacetLimit.data(),
               ic::StlDiagnosticCode::kFacetLimit);
    type.value(wire_symbols::kNameLimit.data(),
               ic::StlDiagnosticCode::kNameLimit);
    type.value(wire_symbols::kLineLimit.data(),
               ic::StlDiagnosticCode::kLineLimit);
    type.value(wire_symbols::kTokenLimit.data(),
               ic::StlDiagnosticCode::kTokenLimit);
    type.value(wire_symbols::kOutputLimit.data(),
               ic::StlDiagnosticCode::kOutputLimit);
    type.value(wire_symbols::kSyntax.data(), ic::StlDiagnosticCode::kSyntax);
    type.value(wire_symbols::kNumericSyntax.data(),
               ic::StlDiagnosticCode::kNumericSyntax);
    type.value(wire_symbols::kNumericRange.data(),
               ic::StlDiagnosticCode::kNumericRange);
    type.value(wire_symbols::kNonfinite.data(),
               ic::StlDiagnosticCode::kNonfinite);
    type.value(wire_symbols::kBinaryExtent.data(),
               ic::StlDiagnosticCode::kBinaryExtent);
    type.value(wire_symbols::kTrailingBytes.data(),
               ic::StlDiagnosticCode::kTrailingBytes);
    type.value(wire_symbols::kAttributeWord.data(),
               ic::StlDiagnosticCode::kAttributeWord);
    type.value(wire_symbols::kRequestBudget.data(),
               ic::StlDiagnosticCode::kRequestBudget);
    type.value(wire_symbols::kStopRequested.data(),
               ic::StlDiagnosticCode::kStopRequested);
    type.value(wire_symbols::kGeometry.data(),
               ic::StlDiagnosticCode::kGeometry);
    type.value(wire_symbols::kStorageIssues.data(),
               ic::StlDiagnosticCode::kStorageIssues);
    type.value(wire_symbols::kNotTriangles.data(),
               ic::StlDiagnosticCode::kNotTriangles);
    type.value(wire_symbols::kNormalSelection.data(),
               ic::StlDiagnosticCode::kNormalSelection);
    type.value(wire_symbols::kUnsupportedAttribute.data(),
               ic::StlDiagnosticCode::kUnsupportedAttribute);
    type.value(wire_symbols::kMetadata.data(),
               ic::StlDiagnosticCode::kMetadata);
    type.value(wire_symbols::kExpansionAcknowledgment.data(),
               ic::StlDiagnosticCode::kExpansionAcknowledgment);
    type.value(wire_symbols::kNumericConversion.data(),
               ic::StlDiagnosticCode::kNumericConversion);
  }
  if (!nb::type<ic::StlEncoding>().is_valid()) {
    auto type = nb::enum_<ic::StlEncoding>(module, "OffGlbStlEncoding");
    type.value(wire_symbols::kInvalid.data(), ic::StlEncoding::kInvalid);
    type.value(wire_symbols::kAscii.data(), ic::StlEncoding::kAscii);
    type.value(wire_symbols::kBinary.data(), ic::StlEncoding::kBinary);
  }
  if (!nb::type<ic::StlMetadataPolicy>().is_valid()) {
    auto type =
        nb::enum_<ic::StlMetadataPolicy>(module, "OffGlbStlMetadataPolicy");
    type.value(wire_symbols::kExplicitWithoutSource.data(),
               ic::StlMetadataPolicy::kExplicitWithoutSource);
    type.value(wire_symbols::kRetainedSourceMetadata.data(),
               ic::StlMetadataPolicy::kRetainedSourceMetadata);
    type.value(wire_symbols::kReplacedSourceMetadata.data(),
               ic::StlMetadataPolicy::kReplacedSourceMetadata);
  }
  if (!nb::type<ic::SurfaceNumericSummary::Kind>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceNumericSummary::Kind>(
        module, "OffGlbSurfaceNumericSummary_Kind");
    type.value(wire_symbols::kPreparedFields.data(),
               ic::SurfaceNumericSummary::Kind::kPreparedFields);
    type.value(wire_symbols::kDecodedWire.data(),
               ic::SurfaceNumericSummary::Kind::kDecodedWire);
  }
  if (!nb::type<ic::SurfaceDelegateProfileStatus>().is_valid()) {
    auto type = nb::enum_<ic::SurfaceDelegateProfileStatus>(
        module, "OffGlbSurfaceDelegateProfileStatus");
    type.value(wire_symbols::kEstablishedBaseline.data(),
               ic::SurfaceDelegateProfileStatus::kEstablishedBaseline);
    type.value(wire_symbols::kUnsupportedRounding.data(),
               ic::SurfaceDelegateProfileStatus::kUnsupportedRounding);
    type.value(wire_symbols::kUnsupportedFlushState.data(),
               ic::SurfaceDelegateProfileStatus::kUnsupportedFlushState);
    type.value(wire_symbols::kUnqueryable.data(),
               ic::SurfaceDelegateProfileStatus::kUnqueryable);
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::OffFileReadResult>>(
        module, "OffFileReadObservation");
    type.def_prop_ro(observation_keys::kStatus,
                     [](const OffGlbObservation<ic::OffFileReadResult>& self) {
                       return Value(self.value.status);
                     });
    type.def_prop_ro(observation_keys::kDiagnostic,
                     [](const OffGlbObservation<ic::OffFileReadResult>& self) {
                       return Value(self.value.diagnostic);
                     });
    type.def_prop_ro(observation_keys::kExecution,
                     [](const OffGlbObservation<ic::OffFileReadResult>& self) {
                       return Value(self.value.execution);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::OffFileDiagnostic>>(
        module, "OffGlbOffFileDiagnostic");
    type.def_prop_ro(observation_keys::kStatus,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.status);
                     });
    type.def_prop_ro(observation_keys::kStage,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.stage);
                     });
    type.def_prop_ro(observation_keys::kCode,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.code);
                     });
    type.def_prop_ro(observation_keys::kFileIndex,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.file_index);
                     });
    type.def_prop_ro(observation_keys::kByteOffset,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.byte_offset);
                     });
    type.def_prop_ro(observation_keys::kSystemError,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.system_error);
                     });
    type.def_prop_ro(observation_keys::kOffStatus,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.off_status);
                     });
    type.def_prop_ro(observation_keys::kOffDiagnostic,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.off_diagnostic);
                     });
    type.def_prop_ro(observation_keys::kGeometryStatus,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.geometry_status);
                     });
    type.def_prop_ro(observation_keys::kGeometryDiagnostic,
                     [](const OffGlbObservation<ic::OffFileDiagnostic>& self) {
                       return Value(self.value.geometry_diagnostic);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::OffDiagnostic>>(
        module, "OffGlbOffDiagnostic");
    type.def_prop_ro(observation_keys::kStage,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.stage);
                     });
    type.def_prop_ro(observation_keys::kCode,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.code);
                     });
    type.def_prop_ro(observation_keys::kByteOffset,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.byte_offset);
                     });
    type.def_prop_ro(observation_keys::kVertex,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.vertex);
                     });
    type.def_prop_ro(observation_keys::kFace,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.face);
                     });
    type.def_prop_ro(observation_keys::kGeometryStatus,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.geometry_status);
                     });
    type.def_prop_ro(observation_keys::kGeometryDiagnostic,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.geometry_diagnostic);
                     });
    type.def_prop_ro(observation_keys::kStorageIssue,
                     [](const OffGlbObservation<ic::OffDiagnostic>& self) {
                       return Value(self.value.storage_issue);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<geo::MaterializationDiagnostic>>(
        module, "OffGlbGeometryMaterializationDiagnostic");
    type.def_prop_ro(
        observation_keys::kCode,
        [](const OffGlbObservation<geo::MaterializationDiagnostic>& self) {
          return Value(self.value.code);
        });
    type.def_prop_ro(
        observation_keys::kAttribute,
        [](const OffGlbObservation<geo::MaterializationDiagnostic>& self) {
          return Value(self.value.attribute);
        });
    type.def_prop_ro(
        observation_keys::kElement,
        [](const OffGlbObservation<geo::MaterializationDiagnostic>& self) {
          return Value(self.value.element);
        });
    type.def_prop_ro(
        observation_keys::kStorageIssue,
        [](const OffGlbObservation<geo::MaterializationDiagnostic>& self) {
          return Value(self.value.storage_issue);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<geo::StorageIssue>>(
        module, "OffGlbGeometryStorageIssue");
    type.def_prop_ro(observation_keys::kCode,
                     [](const OffGlbObservation<geo::StorageIssue>& self) {
                       return Value(self.value.code);
                     });
    type.def_prop_ro(observation_keys::kAttribute,
                     [](const OffGlbObservation<geo::StorageIssue>& self) {
                       return Value(self.value.attribute);
                     });
    type.def_prop_ro(observation_keys::kElement,
                     [](const OffGlbObservation<geo::StorageIssue>& self) {
                       return Value(self.value.element);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::OffFileExecution>>(
        module, "OffGlbOffFileExecution");
    type.def_prop_ro(observation_keys::kCapture,
                     [](const OffGlbObservation<ic::OffFileExecution>& self) {
                       return Value(self.value.capture);
                     });
    type.def_prop_ro(observation_keys::kCopying,
                     [](const OffGlbObservation<ic::OffFileExecution>& self) {
                       return Value(self.value.copying);
                     });
    type.def_prop_ro(observation_keys::kByteVerification,
                     [](const OffGlbObservation<ic::OffFileExecution>& self) {
                       return Value(self.value.byte_verification);
                     });
    type.def_prop_ro(observation_keys::kMeshVerification,
                     [](const OffGlbObservation<ic::OffFileExecution>& self) {
                       return Value(self.value.mesh_verification);
                     });
    type.def_prop_ro(observation_keys::kExpectedInspection,
                     [](const OffGlbObservation<ic::OffFileExecution>& self) {
                       return Value(self.value.expected_inspection);
                     });
    type.def_prop_ro(observation_keys::kOff,
                     [](const OffGlbObservation<ic::OffFileExecution>& self) {
                       return Value(self.value.off);
                     });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<geo::MaterializationExecutionStats>>(
            module, "OffGlbGeometryMaterializationExecutionStats");
    type.def_prop_ro(
        observation_keys::kPlannedJobs,
        [](const OffGlbObservation<geo::MaterializationExecutionStats>& self) {
          return Value(self.value.planned_jobs);
        });
    type.def_prop_ro(
        observation_keys::kLaunchedWorkers,
        [](const OffGlbObservation<geo::MaterializationExecutionStats>& self) {
          return Value(self.value.launched_workers);
        });
    type.def_prop_ro(
        observation_keys::kJoinedWorkers,
        [](const OffGlbObservation<geo::MaterializationExecutionStats>& self) {
          return Value(self.value.joined_workers);
        });
    type.def_prop_ro(
        observation_keys::kCallerJobs,
        [](const OffGlbObservation<geo::MaterializationExecutionStats>& self) {
          return Value(self.value.caller_jobs);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::OffExecution>>(
        module, "OffGlbOffExecution");
    type.def_prop_ro(observation_keys::kCodec,
                     [](const OffGlbObservation<ic::OffExecution>& self) {
                       return Value(self.value.codec);
                     });
    type.def_prop_ro(observation_keys::kMaterialization,
                     [](const OffGlbObservation<ic::OffExecution>& self) {
                       return Value(self.value.materialization);
                     });
    type.def_prop_ro(observation_keys::kInspection,
                     [](const OffGlbObservation<ic::OffExecution>& self) {
                       return Value(self.value.inspection);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<OffGlbSurfaceWorkflowResult>>(
        module, "OffGlbNativeObservation");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<OffGlbSurfaceWorkflowResult>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<OffGlbSurfaceWorkflowResult>& self) {
          return Value(self.value.diagnostic);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<OffGlbSurfaceDiagnostic>>(
        module, "OffGlbOffGlbSurfaceDiagnostic");
    type.def_prop_ro(
        observation_keys::kStage,
        [](const OffGlbObservation<OffGlbSurfaceDiagnostic>& self) {
          return Value(self.value.stage);
        });
    type.def_prop_ro(
        observation_keys::kCode,
        [](const OffGlbObservation<OffGlbSurfaceDiagnostic>& self) {
          return Value(self.value.code);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<OffDuplicateFileWorkflowResult>>(
        module, "OffWorkflowObservation");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<OffDuplicateFileWorkflowResult>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kOutcomes,
        [](const OffGlbObservation<OffDuplicateFileWorkflowResult>& self) {
          return Value(self.value.outcomes);
        });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<OffDuplicateFileWorkflowResult>& self) {
          return Value(self.value.diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kReportDiagnostic,
        [](const OffGlbObservation<OffDuplicateFileWorkflowResult>& self) {
          return Value(self.value.report_diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kExecution,
        [](const OffGlbObservation<OffDuplicateFileWorkflowResult>& self) {
          return Value(self.value.execution);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<OffWorkflowOutcomes>>(
        module, "OffGlbOffWorkflowOutcomes");
    type.def_prop_ro(observation_keys::kExecution,
                     [](const OffGlbObservation<OffWorkflowOutcomes>& self) {
                       return Value(self.value.execution);
                     });
    type.def_prop_ro(observation_keys::kCandidate,
                     [](const OffGlbObservation<OffWorkflowOutcomes>& self) {
                       return Value(self.value.candidate);
                     });
    type.def_prop_ro(observation_keys::kProfile,
                     [](const OffGlbObservation<OffWorkflowOutcomes>& self) {
                       return Value(self.value.profile);
                     });
    type.def_prop_ro(observation_keys::kPublication,
                     [](const OffGlbObservation<OffWorkflowOutcomes>& self) {
                       return Value(self.value.publication);
                     });
    type.def_prop_ro(observation_keys::kStage,
                     [](const OffGlbObservation<OffWorkflowOutcomes>& self) {
                       return Value(self.value.stage);
                     });
    type.def_prop_ro(observation_keys::kExit,
                     [](const OffGlbObservation<OffWorkflowOutcomes>& self) {
                       return Value(self.value.exit);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<OffWorkflowDiagnostic>>(
        module, "OffGlbOffWorkflowDiagnostic");
    type.def_prop_ro(observation_keys::kStage,
                     [](const OffGlbObservation<OffWorkflowDiagnostic>& self) {
                       return Value(self.value.stage);
                     });
    type.def_prop_ro(observation_keys::kCode,
                     [](const OffGlbObservation<OffWorkflowDiagnostic>& self) {
                       return Value(self.value.code);
                     });
    type.def_prop_ro(observation_keys::kElement,
                     [](const OffGlbObservation<OffWorkflowDiagnostic>& self) {
                       return Value(self.value.element);
                     });
    type.def_prop_ro(observation_keys::kDelegatedStatus,
                     [](const OffGlbObservation<OffWorkflowDiagnostic>& self) {
                       return Value(self.value.delegated_status);
                     });
    type.def_prop_ro(observation_keys::kDelegated,
                     [](const OffGlbObservation<OffWorkflowDiagnostic>& self) {
                       return Value(self.value.delegated);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<MaterializedDuplicateDiagnostic>>(
        module, "OffGlbMaterializedDuplicateDiagnostic");
    type.def_prop_ro(
        observation_keys::kStage,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.stage);
        });
    type.def_prop_ro(
        observation_keys::kCode,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.code);
        });
    type.def_prop_ro(
        observation_keys::kTarget,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.target);
        });
    type.def_prop_ro(
        observation_keys::kAttribute,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.attribute);
        });
    type.def_prop_ro(
        observation_keys::kElement,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.element);
        });
    type.def_prop_ro(
        observation_keys::kGeometryStatus,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.geometry_status);
        });
    type.def_prop_ro(
        observation_keys::kGeometryDiagnostic,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.geometry_diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kStorageIssue,
        [](const OffGlbObservation<MaterializedDuplicateDiagnostic>& self) {
          return Value(self.value.storage_issue);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<OffWorkflowExecution>>(
        module, "OffGlbOffWorkflowExecution");
    type.def_prop_ro(observation_keys::kRepair,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.repair);
                     });
    type.def_prop_ro(observation_keys::kWriting,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.writing);
                     });
    type.def_prop_ro(observation_keys::kReload,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.reload);
                     });
    type.def_prop_ro(observation_keys::kSourceProtection,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.source_protection);
                     });
    type.def_prop_ro(observation_keys::kRepairCorrespondence,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.repair_correspondence);
                     });
    type.def_prop_ro(observation_keys::kCanonicalPreservation,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.canonical_preservation);
                     });
    type.def_prop_ro(observation_keys::kReloadVerification,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.reload_verification);
                     });
    type.def_prop_ro(observation_keys::kSourceVerification,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.source_verification);
                     });
    type.def_prop_ro(observation_keys::kStagedCandidate,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.staged_candidate);
                     });
    type.def_prop_ro(observation_keys::kHashing,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.hashing);
                     });
    type.def_prop_ro(observation_keys::kReportCopy,
                     [](const OffGlbObservation<OffWorkflowExecution>& self) {
                       return Value(self.value.report_copy);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<MaterializedDuplicateExecution>>(
        module, "OffGlbMaterializedDuplicateExecution");
    type.def_prop_ro(
        observation_keys::kSourceInspection,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.source_inspection);
        });
    type.def_prop_ro(
        observation_keys::kEligibility,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.eligibility);
        });
    type.def_prop_ro(
        observation_keys::kComparison,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.comparison);
        });
    type.def_prop_ro(
        observation_keys::kSelection,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.selection);
        });
    type.def_prop_ro(
        observation_keys::kCorrespondence,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.correspondence);
        });
    type.def_prop_ro(
        observation_keys::kCandidateInspection,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.candidate_inspection);
        });
    type.def_prop_ro(
        observation_keys::kVerification,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.verification);
        });
    type.def_prop_ro(
        observation_keys::kDirectedVerification,
        [](const OffGlbObservation<MaterializedDuplicateExecution>& self) {
          return Value(self.value.directed_verification);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<geo::ChargedTriangulationResult>>(
        module, "ChargedTriangulationObservation");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kCounts,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.counts);
        });
    type.def_prop_ro(
        observation_keys::kSourceCapture,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.source_capture);
        });
    type.def_prop_ro(
        observation_keys::kSourceInspection,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.source_inspection);
        });
    type.def_prop_ro(
        observation_keys::kCore,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.core);
        });
    type.def_prop_ro(
        observation_keys::kOutputMaterialization,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.output_materialization);
        });
    type.def_prop_ro(
        observation_keys::kOutputInspection,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.output_inspection);
        });
    type.def_prop_ro(
        observation_keys::kAdaptationExecution,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.adaptation_execution);
        });
    type.def_prop_ro(
        observation_keys::kTransferExecution,
        [](const OffGlbObservation<geo::ChargedTriangulationResult>& self) {
          return Value(self.value.transfer_execution);
        });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<geo::ChargedTriangulationDiagnostic>>(
            module, "OffGlbGeometryChargedTriangulationDiagnostic");
    type.def_prop_ro(
        observation_keys::kStage,
        [](const OffGlbObservation<geo::ChargedTriangulationDiagnostic>& self) {
          return Value(self.value.stage);
        });
    type.def_prop_ro(
        observation_keys::kCode,
        [](const OffGlbObservation<geo::ChargedTriangulationDiagnostic>& self) {
          return Value(self.value.code);
        });
    type.def_prop_ro(
        observation_keys::kFace,
        [](const OffGlbObservation<geo::ChargedTriangulationDiagnostic>& self) {
          return Value(self.value.face);
        });
    type.def_prop_ro(
        observation_keys::kAttribute,
        [](const OffGlbObservation<geo::ChargedTriangulationDiagnostic>& self) {
          return Value(self.value.attribute);
        });
    type.def_prop_ro(
        observation_keys::kStorageIssue,
        [](const OffGlbObservation<geo::ChargedTriangulationDiagnostic>& self) {
          return Value(self.value.storage_issue);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<geo::ChargedTriangulationCounts>>(
        module, "OffGlbGeometryChargedTriangulationCounts");
    type.def_prop_ro(
        observation_keys::kSourceVertices,
        [](const OffGlbObservation<geo::ChargedTriangulationCounts>& self) {
          return Value(self.value.source_vertices);
        });
    type.def_prop_ro(
        observation_keys::kSourceFaces,
        [](const OffGlbObservation<geo::ChargedTriangulationCounts>& self) {
          return Value(self.value.source_faces);
        });
    type.def_prop_ro(
        observation_keys::kSourceCorners,
        [](const OffGlbObservation<geo::ChargedTriangulationCounts>& self) {
          return Value(self.value.source_corners);
        });
    type.def_prop_ro(
        observation_keys::kOutputVertices,
        [](const OffGlbObservation<geo::ChargedTriangulationCounts>& self) {
          return Value(self.value.output_vertices);
        });
    type.def_prop_ro(
        observation_keys::kOutputFaces,
        [](const OffGlbObservation<geo::ChargedTriangulationCounts>& self) {
          return Value(self.value.output_faces);
        });
    type.def_prop_ro(
        observation_keys::kOutputCorners,
        [](const OffGlbObservation<geo::ChargedTriangulationCounts>& self) {
          return Value(self.value.output_corners);
        });
  }
  {
    auto type = nb::class_<
        OffGlbObservation<geo::ChargedTriangulationMaterializationFacts>>(
        module, "OffGlbGeometryChargedTriangulationMaterializationFacts");
    type.def_prop_ro(observation_keys::kStatus,
                     [](const OffGlbObservation<
                         geo::ChargedTriangulationMaterializationFacts>& self) {
                       return Value(self.value.status);
                     });
    type.def_prop_ro(observation_keys::kDiagnostic,
                     [](const OffGlbObservation<
                         geo::ChargedTriangulationMaterializationFacts>& self) {
                       return Value(self.value.diagnostic);
                     });
    type.def_prop_ro(observation_keys::kExecution,
                     [](const OffGlbObservation<
                         geo::ChargedTriangulationMaterializationFacts>& self) {
                       return Value(self.value.execution);
                     });
    type.def_prop_ro(observation_keys::kNewRequestedBytesAtReturn,
                     [](const OffGlbObservation<
                         geo::ChargedTriangulationMaterializationFacts>& self) {
                       return Value(self.value.new_requested_bytes_at_return);
                     });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<geo::ChargedTriangulationInspectionFacts>>(
            module, "OffGlbGeometryChargedTriangulationInspectionFacts");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<geo::ChargedTriangulationInspectionFacts>&
               self) { return Value(self.value.status); });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<geo::ChargedTriangulationInspectionFacts>&
               self) { return Value(self.value.diagnostic); });
    type.def_prop_ro(
        observation_keys::kExecution,
        [](const OffGlbObservation<geo::ChargedTriangulationInspectionFacts>&
               self) { return Value(self.value.execution); });
    type.def_prop_ro(
        observation_keys::kStorageValid,
        [](const OffGlbObservation<geo::ChargedTriangulationInspectionFacts>&
               self) { return Value(self.value.storage_valid); });
    type.def_prop_ro(
        observation_keys::kNewRequestedBytesAtReturn,
        [](const OffGlbObservation<geo::ChargedTriangulationInspectionFacts>&
               self) {
          return Value(self.value.new_requested_bytes_at_return);
        });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<geo::ChargedTriangulationCoreFacts>>(
            module, "OffGlbGeometryChargedTriangulationCoreFacts");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<geo::ChargedTriangulationCoreFacts>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kWorkersUsed,
        [](const OffGlbObservation<geo::ChargedTriangulationCoreFacts>& self) {
          return Value(self.value.workers_used);
        });
    type.def_prop_ro(
        observation_keys::kSerialReason,
        [](const OffGlbObservation<geo::ChargedTriangulationCoreFacts>& self) {
          return Value(self.value.serial_reason);
        });
    type.def_prop_ro(
        observation_keys::kNewRequestedPeakBytes,
        [](const OffGlbObservation<geo::ChargedTriangulationCoreFacts>& self) {
          return Value(self.value.new_requested_peak_bytes);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceEncodeResult>>(
        module, "SurfaceEncodeObservation");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<ic::SurfaceEncodeResult>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<ic::SurfaceEncodeResult>& self) {
          return Value(self.value.diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kFacts,
        [](const OffGlbObservation<ic::SurfaceEncodeResult>& self) {
          return Value(self.value.facts);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceDiagnostic>>(
        module, "OffGlbSurfaceDiagnostic");
    type.def_prop_ro(observation_keys::kStage,
                     [](const OffGlbObservation<ic::SurfaceDiagnostic>& self) {
                       return Value(self.value.stage);
                     });
    type.def_prop_ro(observation_keys::kCode,
                     [](const OffGlbObservation<ic::SurfaceDiagnostic>& self) {
                       return Value(self.value.code);
                     });
    type.def_prop_ro(observation_keys::kSubject,
                     [](const OffGlbObservation<ic::SurfaceDiagnostic>& self) {
                       return Value(self.value.subject);
                     });
    type.def_prop_ro(observation_keys::kIndex,
                     [](const OffGlbObservation<ic::SurfaceDiagnostic>& self) {
                       return Value(self.value.index);
                     });
    type.def_prop_ro(observation_keys::kSecondaryIndex,
                     [](const OffGlbObservation<ic::SurfaceDiagnostic>& self) {
                       return Value(self.value.secondary_index);
                     });
    type.def_prop_ro(observation_keys::kComponent,
                     [](const OffGlbObservation<ic::SurfaceDiagnostic>& self) {
                       return Value(self.value.component);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceInspectionResult>>(
        module, "SurfaceInspectionObservation");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<ic::SurfaceInspectionResult>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<ic::SurfaceInspectionResult>& self) {
          return Value(self.value.diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kFacts,
        [](const OffGlbObservation<ic::SurfaceInspectionResult>& self) {
          return Value(self.value.facts);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceFilePublicationResult>>(
        module, "SurfacePublicationObservation");
    type.def_prop_ro(
        observation_keys::kOutcome,
        [](const OffGlbObservation<ic::SurfaceFilePublicationResult>& self) {
          return Value(self.value.outcome);
        });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<ic::SurfaceFilePublicationResult>& self) {
          return Value(self.value.diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kCleanup,
        [](const OffGlbObservation<ic::SurfaceFilePublicationResult>& self) {
          return Value(self.value.cleanup);
        });
    type.def_prop_ro(
        observation_keys::kFacts,
        [](const OffGlbObservation<ic::SurfaceFilePublicationResult>& self) {
          return Value(self.value.facts);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceFileDiagnostic>>(
        module, "OffGlbSurfaceFileDiagnostic");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kStage,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.stage);
        });
    type.def_prop_ro(
        observation_keys::kCode,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.code);
        });
    type.def_prop_ro(
        observation_keys::kMemberOrdinal,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.member_ordinal);
        });
    type.def_prop_ro(
        observation_keys::kByteOffset,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.byte_offset);
        });
    type.def_prop_ro(
        observation_keys::kSystemError,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.system_error);
        });
    type.def_prop_ro(
        observation_keys::kInspectionStatus,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.inspection_status);
        });
    type.def_prop_ro(
        observation_keys::kInspectionDiagnostic,
        [](const OffGlbObservation<ic::SurfaceFileDiagnostic>& self) {
          return Value(self.value.inspection_diagnostic);
        });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<ic::SurfaceFileCleanupObservation>>(
            module, "OffGlbSurfaceFileCleanupObservation");
    type.def_prop_ro(
        observation_keys::kOutcome,
        [](const OffGlbObservation<ic::SurfaceFileCleanupObservation>& self) {
          return Value(self.value.outcome);
        });
    type.def_prop_ro(
        observation_keys::kError,
        [](const OffGlbObservation<ic::SurfaceFileCleanupObservation>& self) {
          return Value(self.value.error);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceFileFacts>>(
        module, "OffGlbSurfaceFileFacts");
    type.def_prop_ro(observation_keys::kPayloadWrite,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.payload_write);
                     });
    type.def_prop_ro(observation_keys::kPayloadInventory,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.payload_inventory);
                     });
    type.def_prop_ro(observation_keys::kPayloadReread,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.payload_reread);
                     });
    type.def_prop_ro(observation_keys::kInspection,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.inspection);
                     });
    type.def_prop_ro(observation_keys::kProvider,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.provider);
                     });
    type.def_prop_ro(observation_keys::kReportImport,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.report_import);
                     });
    type.def_prop_ro(observation_keys::kReceiptWrite,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.receipt_write);
                     });
    type.def_prop_ro(observation_keys::kReceiptReread,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.receipt_reread);
                     });
    type.def_prop_ro(observation_keys::kFinalInventory,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.final_inventory);
                     });
    type.def_prop_ro(observation_keys::kFinalPayloadVerification,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.final_payload_verification);
                     });
    type.def_prop_ro(observation_keys::kFinalReceiptVerification,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.final_receipt_verification);
                     });
    type.def_prop_ro(observation_keys::kCommit,
                     [](const OffGlbObservation<ic::SurfaceFileFacts>& self) {
                       return Value(self.value.commit);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceFileIoObservation>>(
        module, "OffGlbSurfaceFileIoObservation");
    type.def_prop_ro(
        observation_keys::kOutcome,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.outcome);
        });
    type.def_prop_ro(
        observation_keys::kBytesRead,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.bytes_read);
        });
    type.def_prop_ro(
        observation_keys::kBytesWritten,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.bytes_written);
        });
    type.def_prop_ro(
        observation_keys::kBytesCopied,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.bytes_copied);
        });
    type.def_prop_ro(
        observation_keys::kBytesCompared,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.bytes_compared);
        });
    type.def_prop_ro(
        observation_keys::kBytesHashed,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.bytes_hashed);
        });
    type.def_prop_ro(
        observation_keys::kCompletedFiles,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.completed_files);
        });
    type.def_prop_ro(
        observation_keys::kClosedFiles,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.closed_files);
        });
    type.def_prop_ro(
        observation_keys::kExecution,
        [](const OffGlbObservation<ic::SurfaceFileIoObservation>& self) {
          return Value(self.value.execution);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceFileExecutionStats>>(
        module, "OffGlbSurfaceFileExecutionStats");
    type.def_prop_ro(
        observation_keys::kPlannedJobs,
        [](const OffGlbObservation<ic::SurfaceFileExecutionStats>& self) {
          return Value(self.value.planned_jobs);
        });
    type.def_prop_ro(
        observation_keys::kLaunchedWorkers,
        [](const OffGlbObservation<ic::SurfaceFileExecutionStats>& self) {
          return Value(self.value.launched_workers);
        });
    type.def_prop_ro(
        observation_keys::kCallerJobs,
        [](const OffGlbObservation<ic::SurfaceFileExecutionStats>& self) {
          return Value(self.value.caller_jobs);
        });
    type.def_prop_ro(
        observation_keys::kJoinedWorkers,
        [](const OffGlbObservation<ic::SurfaceFileExecutionStats>& self) {
          return Value(self.value.joined_workers);
        });
    type.def_prop_ro(
        observation_keys::kCompletedJobs,
        [](const OffGlbObservation<ic::SurfaceFileExecutionStats>& self) {
          return Value(self.value.completed_jobs);
        });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<ic::SurfaceFileInventoryObservation>>(
            module, "OffGlbSurfaceFileInventoryObservation");
    type.def_prop_ro(
        observation_keys::kOutcome,
        [](const OffGlbObservation<ic::SurfaceFileInventoryObservation>& self) {
          return Value(self.value.outcome);
        });
    type.def_prop_ro(
        observation_keys::kObservedFiles,
        [](const OffGlbObservation<ic::SurfaceFileInventoryObservation>& self) {
          return Value(self.value.observed_files);
        });
    type.def_prop_ro(
        observation_keys::kObservedDirectories,
        [](const OffGlbObservation<ic::SurfaceFileInventoryObservation>& self) {
          return Value(self.value.observed_directories);
        });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<ic::SurfaceFileInspectionObservation>>(
            module, "OffGlbSurfaceFileInspectionObservation");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<ic::SurfaceFileInspectionObservation>&
               self) { return Value(self.value.status); });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<ic::SurfaceFileInspectionObservation>&
               self) { return Value(self.value.diagnostic); });
    type.def_prop_ro(
        observation_keys::kFacts,
        [](const OffGlbObservation<ic::SurfaceFileInspectionObservation>&
               self) { return Value(self.value.facts); });
  }
  {
    auto type =
        nb::class_<OffGlbObservation<ic::SurfaceFileProviderObservation>>(
            module, "OffGlbSurfaceFileProviderObservation");
    type.def_prop_ro(
        observation_keys::kOutcome,
        [](const OffGlbObservation<ic::SurfaceFileProviderObservation>& self) {
          return Value(self.value.outcome);
        });
    type.def_prop_ro(
        observation_keys::kReturnedBytes,
        [](const OffGlbObservation<ic::SurfaceFileProviderObservation>& self) {
          return Value(self.value.returned_bytes);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceFileCommitObservation>>(
        module, "OffGlbSurfaceFileCommitObservation");
    type.def_prop_ro(
        observation_keys::kOutcome,
        [](const OffGlbObservation<ic::SurfaceFileCommitObservation>& self) {
          return Value(self.value.outcome);
        });
    type.def_prop_ro(
        observation_keys::kError,
        [](const OffGlbObservation<ic::SurfaceFileCommitObservation>& self) {
          return Value(self.value.error);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfacePrimitiveView>>(
        module, "OffGlbSurfacePrimitiveView");
    type.def_prop_ro(
        observation_keys::kOrdinal,
        [](const OffGlbObservation<ic::SurfacePrimitiveView>& self) {
          return Value(self.value.ordinal);
        });
    type.def_prop_ro(
        observation_keys::kFirstWireVertex,
        [](const OffGlbObservation<ic::SurfacePrimitiveView>& self) {
          return Value(self.value.first_wire_vertex);
        });
    type.def_prop_ro(
        observation_keys::kWireVertices,
        [](const OffGlbObservation<ic::SurfacePrimitiveView>& self) {
          return Value(self.value.wire_vertices);
        });
    type.def_prop_ro(
        observation_keys::kFirstWireTriangle,
        [](const OffGlbObservation<ic::SurfacePrimitiveView>& self) {
          return Value(self.value.first_wire_triangle);
        });
    type.def_prop_ro(
        observation_keys::kWireTriangles,
        [](const OffGlbObservation<ic::SurfacePrimitiveView>& self) {
          return Value(self.value.wire_triangles);
        });
    type.def_prop_ro(
        observation_keys::kMaterial,
        [](const OffGlbObservation<ic::SurfacePrimitiveView>& self) {
          return Value(self.value.material);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceMeshCounts>>(
        module, "OffGlbSurfaceMeshCounts");
    type.def_prop_ro(observation_keys::kVertices,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.vertices);
                     });
    type.def_prop_ro(observation_keys::kFaces,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.faces);
                     });
    type.def_prop_ro(observation_keys::kCorners,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.corners);
                     });
    type.def_prop_ro(observation_keys::kFaceOffsets,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.face_offsets);
                     });
    type.def_prop_ro(observation_keys::kAttributes,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.attributes);
                     });
    type.def_prop_ro(observation_keys::kAttributeScalars,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.attribute_scalars);
                     });
    type.def_prop_ro(observation_keys::kAttributeBytes,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.attribute_bytes);
                     });
    type.def_prop_ro(observation_keys::kAttributeOffsets,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.attribute_offsets);
                     });
    type.def_prop_ro(observation_keys::kPresenceBytes,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.presence_bytes);
                     });
    type.def_prop_ro(observation_keys::kMetadataEntries,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.metadata_entries);
                     });
    type.def_prop_ro(observation_keys::kDescriptorBytes,
                     [](const OffGlbObservation<ic::SurfaceMeshCounts>& self) {
                       return Value(self.value.descriptor_bytes);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceWireCounts>>(
        module, "OffGlbSurfaceWireCounts");
    type.def_prop_ro(observation_keys::kPrimitives,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.primitives);
                     });
    type.def_prop_ro(observation_keys::kVertices,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.vertices);
                     });
    type.def_prop_ro(observation_keys::kTriangles,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.triangles);
                     });
    type.def_prop_ro(observation_keys::kCorners,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.corners);
                     });
    type.def_prop_ro(observation_keys::kMapEntries,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.map_entries);
                     });
    type.def_prop_ro(observation_keys::kMaterials,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.materials);
                     });
    type.def_prop_ro(observation_keys::kTextures,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.textures);
                     });
    type.def_prop_ro(observation_keys::kImages,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.images);
                     });
    type.def_prop_ro(observation_keys::kSamplers,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.samplers);
                     });
    type.def_prop_ro(observation_keys::kMembers,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.members);
                     });
    type.def_prop_ro(observation_keys::kMemberBytes,
                     [](const OffGlbObservation<ic::SurfaceWireCounts>& self) {
                       return Value(self.value.member_bytes);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceDelegateView>>(
        module, "OffGlbSurfaceDelegateView");
    type.def_prop_ro(
        observation_keys::kStage,
        [](const OffGlbObservation<ic::SurfaceDelegateView>& self) {
          return Value(self.value.stage);
        });
    type.def_prop_ro(
        observation_keys::kKind,
        [](const OffGlbObservation<ic::SurfaceDelegateView>& self) {
          return Value(self.value.kind);
        });
    type.def_prop_ro(
        observation_keys::kCallOrdinal,
        [](const OffGlbObservation<ic::SurfaceDelegateView>& self) {
          return Value(self.value.call_ordinal);
        });
    type.def_prop_ro(
        observation_keys::kPrimitive,
        [](const OffGlbObservation<ic::SurfaceDelegateView>& self) {
          return Value(self.value.primitive);
        });
    type.def_prop_ro(
        observation_keys::kFacts,
        [](const OffGlbObservation<ic::SurfaceDelegateView>& self) {
          return Value(self.value.facts);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceGeometryFacts>>(
        module, "OffGlbSurfaceGeometryFacts");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<ic::SurfaceGeometryFacts>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kDiagnostic,
        [](const OffGlbObservation<ic::SurfaceGeometryFacts>& self) {
          return Value(self.value.diagnostic);
        });
    type.def_prop_ro(
        observation_keys::kExecution,
        [](const OffGlbObservation<ic::SurfaceGeometryFacts>& self) {
          return Value(self.value.execution);
        });
    type.def_prop_ro(
        observation_keys::kRequestedBytesAtReturn,
        [](const OffGlbObservation<ic::SurfaceGeometryFacts>& self) {
          return Value(self.value.requested_bytes_at_return);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceGlbFacts>>(
        module, "OffGlbSurfaceGlbFacts");
    type.def_prop_ro(observation_keys::kStatus,
                     [](const OffGlbObservation<ic::SurfaceGlbFacts>& self) {
                       return Value(self.value.status);
                     });
    type.def_prop_ro(observation_keys::kErrorByteOffset,
                     [](const OffGlbObservation<ic::SurfaceGlbFacts>& self) {
                       return Value(self.value.error_byte_offset);
                     });
    type.def_prop_ro(observation_keys::kWorkersUsed,
                     [](const OffGlbObservation<ic::SurfaceGlbFacts>& self) {
                       return Value(self.value.workers_used);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceGltfFacts>>(
        module, "OffGlbSurfaceGltfFacts");
    type.def_prop_ro(observation_keys::kDiagnostic,
                     [](const OffGlbObservation<ic::SurfaceGltfFacts>& self) {
                       return Value(self.value.diagnostic);
                     });
    type.def_prop_ro(observation_keys::kRequestedBytesAtReturn,
                     [](const OffGlbObservation<ic::SurfaceGltfFacts>& self) {
                       return Value(self.value.requested_bytes_at_return);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::GltfDiagnostic>>(
        module, "OffGlbGltfDiagnostic");
    type.def_prop_ro(observation_keys::kStatus,
                     [](const OffGlbObservation<ic::GltfDiagnostic>& self) {
                       return Value(self.value.status);
                     });
    type.def_prop_ro(observation_keys::kCode,
                     [](const OffGlbObservation<ic::GltfDiagnostic>& self) {
                       return Value(self.value.code);
                     });
    type.def_prop_ro(observation_keys::kJsonSpan,
                     [](const OffGlbObservation<ic::GltfDiagnostic>& self) {
                       return Value(self.value.json_span);
                     });
    type.def_prop_ro(observation_keys::kResourceKind,
                     [](const OffGlbObservation<ic::GltfDiagnostic>& self) {
                       return Value(self.value.resource_kind);
                     });
    type.def_prop_ro(observation_keys::kResourceIndex,
                     [](const OffGlbObservation<ic::GltfDiagnostic>& self) {
                       return Value(self.value.resource_index);
                     });
    type.def_prop_ro(observation_keys::kResourceSpan,
                     [](const OffGlbObservation<ic::GltfDiagnostic>& self) {
                       return Value(self.value.resource_span);
                     });
    type.def_prop_ro(observation_keys::kRuleId,
                     [](const OffGlbObservation<ic::GltfDiagnostic>& self) {
                       return Value(self.value.rule_id);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::GltfSourceSpan>>(
        module, "OffGlbGltfSourceSpan");
    type.def_prop_ro(observation_keys::kOffset,
                     [](const OffGlbObservation<ic::GltfSourceSpan>& self) {
                       return Value(self.value.offset);
                     });
    type.def_prop_ro(observation_keys::kLength,
                     [](const OffGlbObservation<ic::GltfSourceSpan>& self) {
                       return Value(self.value.length);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceStlFacts>>(
        module, "OffGlbSurfaceStlFacts");
    type.def_prop_ro(observation_keys::kStatus,
                     [](const OffGlbObservation<ic::SurfaceStlFacts>& self) {
                       return Value(self.value.status);
                     });
    type.def_prop_ro(observation_keys::kDiagnostic,
                     [](const OffGlbObservation<ic::SurfaceStlFacts>& self) {
                       return Value(self.value.diagnostic);
                     });
    type.def_prop_ro(observation_keys::kExecution,
                     [](const OffGlbObservation<ic::SurfaceStlFacts>& self) {
                       return Value(self.value.execution);
                     });
    type.def_prop_ro(observation_keys::kRequestedBytesAtReturn,
                     [](const OffGlbObservation<ic::SurfaceStlFacts>& self) {
                       return Value(self.value.requested_bytes_at_return);
                     });
    type.def_prop_ro(observation_keys::kWriteFacts,
                     [](const OffGlbObservation<ic::SurfaceStlFacts>& self) {
                       return Value(self.value.write_facts);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::StlDiagnostic>>(
        module, "OffGlbStlDiagnostic");
    type.def_prop_ro(observation_keys::kStage,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.stage);
                     });
    type.def_prop_ro(observation_keys::kCode,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.code);
                     });
    type.def_prop_ro(observation_keys::kByteOffset,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.byte_offset);
                     });
    type.def_prop_ro(observation_keys::kFacet,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.facet);
                     });
    type.def_prop_ro(observation_keys::kAttribute,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.attribute);
                     });
    type.def_prop_ro(observation_keys::kGeometryStatus,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.geometry_status);
                     });
    type.def_prop_ro(observation_keys::kGeometryDiagnostic,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.geometry_diagnostic);
                     });
    type.def_prop_ro(observation_keys::kStorageIssue,
                     [](const OffGlbObservation<ic::StlDiagnostic>& self) {
                       return Value(self.value.storage_issue);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::StlExecution>>(
        module, "OffGlbStlExecution");
    type.def_prop_ro(observation_keys::kCodec,
                     [](const OffGlbObservation<ic::StlExecution>& self) {
                       return Value(self.value.codec);
                     });
    type.def_prop_ro(observation_keys::kMaterialization,
                     [](const OffGlbObservation<ic::StlExecution>& self) {
                       return Value(self.value.materialization);
                     });
    type.def_prop_ro(observation_keys::kInspection,
                     [](const OffGlbObservation<ic::StlExecution>& self) {
                       return Value(self.value.inspection);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::StlRequestedBytes>>(
        module, "OffGlbStlRequestedBytes");
    type.def_prop_ro(observation_keys::kCodec,
                     [](const OffGlbObservation<ic::StlRequestedBytes>& self) {
                       return Value(self.value.codec);
                     });
    type.def_prop_ro(observation_keys::kMaterialization,
                     [](const OffGlbObservation<ic::StlRequestedBytes>& self) {
                       return Value(self.value.materialization);
                     });
    type.def_prop_ro(observation_keys::kInspection,
                     [](const OffGlbObservation<ic::StlRequestedBytes>& self) {
                       return Value(self.value.inspection);
                     });
    type.def_prop_ro(observation_keys::kTotal,
                     [](const OffGlbObservation<ic::StlRequestedBytes>& self) {
                       return Value(self.value.total);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::StlWriteFacts>>(
        module, "OffGlbStlWriteFacts");
    type.def_prop_ro(observation_keys::kEncoding,
                     [](const OffGlbObservation<ic::StlWriteFacts>& self) {
                       return Value(self.value.encoding);
                     });
    type.def_prop_ro(observation_keys::kFaceNormalAttribute,
                     [](const OffGlbObservation<ic::StlWriteFacts>& self) {
                       return Value(self.value.face_normal_attribute);
                     });
    type.def_prop_ro(observation_keys::kSourceVertexCount,
                     [](const OffGlbObservation<ic::StlWriteFacts>& self) {
                       return Value(self.value.source_vertex_count);
                     });
    type.def_prop_ro(observation_keys::kSourceFaceCount,
                     [](const OffGlbObservation<ic::StlWriteFacts>& self) {
                       return Value(self.value.source_face_count);
                     });
    type.def_prop_ro(observation_keys::kCornerExpansionAcknowledged,
                     [](const OffGlbObservation<ic::StlWriteFacts>& self) {
                       return Value(self.value.corner_expansion_acknowledged);
                     });
    type.def_prop_ro(observation_keys::kSourceEncoding,
                     [](const OffGlbObservation<ic::StlWriteFacts>& self) {
                       return Value(self.value.source_encoding);
                     });
    type.def_prop_ro(observation_keys::kMetadataPolicy,
                     [](const OffGlbObservation<ic::StlWriteFacts>& self) {
                       return Value(self.value.metadata_policy);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceCoreFactorFacts>>(
        module, "OffGlbSurfaceCoreFactorFacts");
    type.def_prop_ro(
        observation_keys::kMaterial,
        [](const OffGlbObservation<ic::SurfaceCoreFactorFacts>& self) {
          return Value(self.value.material);
        });
    type.def_prop_ro(
        observation_keys::kCoreF32,
        [](const OffGlbObservation<ic::SurfaceCoreFactorFacts>& self) {
          return Value(self.value.core_f32);
        });
    type.def_prop_ro(
        observation_keys::kAuthored,
        [](const OffGlbObservation<ic::SurfaceCoreFactorFacts>& self) {
          return Value(self.value.authored);
        });
    type.def_prop_ro(
        observation_keys::kObserved,
        [](const OffGlbObservation<ic::SurfaceCoreFactorFacts>& self) {
          return Value(self.value.observed);
        });
    type.def_prop_ro(
        observation_keys::kLexicalCalls,
        [](const OffGlbObservation<ic::SurfaceCoreFactorFacts>& self) {
          return Value(self.value.lexical_calls);
        });
    type.def_prop_ro(
        observation_keys::kComplete,
        [](const OffGlbObservation<ic::SurfaceCoreFactorFacts>& self) {
          return Value(self.value.complete);
        });
    type.def_prop_ro(
        observation_keys::kFailure,
        [](const OffGlbObservation<ic::SurfaceCoreFactorFacts>& self) {
          return Value(self.value.failure);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceNumericSummary>>(
        module, "OffGlbSurfaceNumericSummary");
    type.def_prop_ro(
        observation_keys::kKind,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.kind);
        });
    type.def_prop_ro(
        observation_keys::kComplete,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.complete);
        });
    type.def_prop_ro(
        observation_keys::kExaminedValues,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.examined_values);
        });
    type.def_prop_ro(
        observation_keys::kChangedValues,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.changed_values);
        });
    type.def_prop_ro(
        observation_keys::kMaxAbsoluteError,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.max_absolute_error);
        });
    type.def_prop_ro(
        observation_keys::kSourceAttribute,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.source_attribute);
        });
    type.def_prop_ro(
        observation_keys::kSourceRow,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.source_row);
        });
    type.def_prop_ro(
        observation_keys::kComponent,
        [](const OffGlbObservation<ic::SurfaceNumericSummary>& self) {
          return Value(self.value.component);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceDyadicError>>(
        module, "OffGlbSurfaceDyadicError");
    type.def_prop_ro(observation_keys::kMagnitude,
                     [](const OffGlbObservation<ic::SurfaceDyadicError>& self) {
                       return Value(self.value.magnitude);
                     });
    type.def_prop_ro(observation_keys::kUsedWords,
                     [](const OffGlbObservation<ic::SurfaceDyadicError>& self) {
                       return Value(self.value.used_words);
                     });
    type.def_prop_ro(observation_keys::kExponent2,
                     [](const OffGlbObservation<ic::SurfaceDyadicError>& self) {
                       return Value(self.value.exponent2);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceWorkCounts>>(
        module, "OffGlbSurfaceWorkCounts");
    type.def_prop_ro(observation_keys::kSourceScalarPairs,
                     [](const OffGlbObservation<ic::SurfaceWorkCounts>& self) {
                       return Value(self.value.source_scalar_pairs);
                     });
    type.def_prop_ro(observation_keys::kDescriptorBytePairs,
                     [](const OffGlbObservation<ic::SurfaceWorkCounts>& self) {
                       return Value(self.value.descriptor_byte_pairs);
                     });
    type.def_prop_ro(observation_keys::kCoincidenceComparisons,
                     [](const OffGlbObservation<ic::SurfaceWorkCounts>& self) {
                       return Value(self.value.coincidence_comparisons);
                     });
    type.def_prop_ro(observation_keys::kRelationshipSteps,
                     [](const OffGlbObservation<ic::SurfaceWorkCounts>& self) {
                       return Value(self.value.relationship_steps);
                     });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceDelegateProfileView>>(
        module, "OffGlbSurfaceDelegateProfileView");
    type.def_prop_ro(
        observation_keys::kStage,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileView>& self) {
          return Value(self.value.stage);
        });
    type.def_prop_ro(
        observation_keys::kKind,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileView>& self) {
          return Value(self.value.kind);
        });
    type.def_prop_ro(
        observation_keys::kCallOrdinal,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileView>& self) {
          return Value(self.value.call_ordinal);
        });
    type.def_prop_ro(
        observation_keys::kPrimitive,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileView>& self) {
          return Value(self.value.primitive);
        });
    type.def_prop_ro(
        observation_keys::kFactorComponent,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileView>& self) {
          return Value(self.value.factor_component);
        });
    type.def_prop_ro(
        observation_keys::kFacts,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileView>& self) {
          return Value(self.value.facts);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceDelegateProfileFacts>>(
        module, "OffGlbSurfaceDelegateProfileFacts");
    type.def_prop_ro(
        observation_keys::kStatus,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileFacts>& self) {
          return Value(self.value.status);
        });
    type.def_prop_ro(
        observation_keys::kNearestEven,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileFacts>& self) {
          return Value(self.value.nearest_even);
        });
    type.def_prop_ro(
        observation_keys::kFtzDisabled,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileFacts>& self) {
          return Value(self.value.ftz_disabled);
        });
    type.def_prop_ro(
        observation_keys::kDazDisabled,
        [](const OffGlbObservation<ic::SurfaceDelegateProfileFacts>& self) {
          return Value(self.value.daz_disabled);
        });
  }
  {
    auto type = nb::class_<OffGlbObservation<ic::SurfaceFacts>>(
        module, "SurfaceFactsObservation");
    type.def_prop_ro(observation_keys::kOriginalCounts,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.OriginalCounts());
                     });
    type.def_prop_ro(observation_keys::kTriangleCounts,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.TriangleCounts());
                     });
    type.def_prop_ro(observation_keys::kWireCounts,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.WireCounts());
                     });
    type.def_prop_ro(observation_keys::kDelegates,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.Delegates());
                     });
    type.def_prop_ro(observation_keys::kPositionNumeric,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.PositionNumeric());
                     });
    type.def_prop_ro(observation_keys::kUvNumeric,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.UvNumeric());
                     });
    type.def_prop_ro(observation_keys::kExecution,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.Execution());
                     });
    type.def_prop_ro(observation_keys::kExaminedWork,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.ExaminedWork());
                     });
    type.def_prop_ro(observation_keys::kNumericDelegateProfiles,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.NumericDelegateProfiles());
                     });
    type.def_prop_ro(observation_keys::kNewRequestedBytes,
                     [](const OffGlbObservation<ic::SurfaceFacts>& self) {
                       return Value(self.value.NewRequestedBytes());
                     });
  }
  nb::class_<OffGlbObservation<std::error_code>>(module,
                                                 "OffGlbSystemErrorCode")
      .def_prop_ro(observation_keys::kCategory,
                   [](const OffGlbObservation<std::error_code>& self) {
                     return nb::str(self.value.category().name());
                   })
      .def_prop_ro(observation_keys::kValue,
                   [](const OffGlbObservation<std::error_code>& self) {
                     return self.value.value();
                   });
  // Getter callables retain this interpreter-local heap type even when its
  // module attribute is removed. Their ordinary teardown releases the type.
  const nb::object buffer_type = RegisterBuffer(module);
  auto mesh = nb::class_<MeshView>(module, "OffGlbMeshView");
  mesh.def_prop_ro(
      observation_keys::kPositions, [buffer_type](const MeshView& self) {
        return Memory(buffer_type, self.owner, self.owner.View().positions);
      });
  mesh.def_prop_ro(
      observation_keys::kFaceOffsets, [buffer_type](const MeshView& self) {
        return Memory(buffer_type, self.owner, self.owner.View().face_offsets);
      });
  mesh.def_prop_ro(observation_keys::kCornerVertices,
                   [buffer_type](const MeshView& self) {
                     return Memory(buffer_type, self.owner,
                                   self.owner.View().corner_vertices);
                   });
  mesh.def_prop_ro(observation_keys::kVertexCount, [](const MeshView& self) {
    return self.owner.View().positions.size() / kPositionComponents;
  });
  mesh.def_prop_ro(observation_keys::kFaceCount, [](const MeshView& self) {
    return self.owner.View().face_offsets.size() - 1;
  });
  mesh.def_prop_ro(observation_keys::kCornerCount, [](const MeshView& self) {
    return self.owner.View().corner_vertices.size();
  });
  mesh.def_prop_ro(observation_keys::kAttributeCount, [](const MeshView& self) {
    return self.owner.View().attributes.size();
  });
  nb::class_<MeshInspectionView>(module, "OffGlbMeshInspectionView")
      .def_prop_ro(observation_keys::kSource,
                   [](const MeshInspectionView& self) {
                     return MeshView{self.owner.Source()};
                   })
      .def_prop_ro(observation_keys::kStorageValid,
                   [](const MeshInspectionView& self) {
                     return self.owner.IsStorageValid();
                   })
      .def_prop_ro(observation_keys::kIssues,
                   [](const MeshInspectionView& self) {
                     return Value(self.owner.Issues());
                   })
      .def_prop_ro(observation_keys::kNewRequestedBytes,
                   [](const MeshInspectionView& self) {
                     return self.owner.NewRequestedBytes();
                   });
  auto output = nb::class_<OutputView>(module, "OffGlbSurfaceOutputView");
  output.def_prop_ro(observation_keys::kEncoding, [](const OutputView& self) {
    return Value(self.owner.Encoding());
  });
  output.def_prop_ro(observation_keys::kRepresentation,
                     [](const OutputView& self) {
                       return Value(self.owner.Representation());
                     });
  output.def_prop_ro(observation_keys::kPrimitives, [](const OutputView& self) {
    return Value(self.owner.Primitives());
  });
  output.def_prop_ro(observation_keys::kFacts, [](const OutputView& self) {
    return Value(self.owner.Facts());
  });
  output.def_prop_ro(observation_keys::kNewRequestedBytes,
                     [](const OutputView& self) {
                       return Value(self.owner.NewRequestedBytes());
                     });
  output.def_prop_ro(observation_keys::kMembers, [](const OutputView& self) {
    return Members(self.owner);
  });
  output.def_prop_ro(observation_keys::kWireTriangleFaces,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.WireTriangleFaces());
                     });
  output.def_prop_ro(observation_keys::kWireTriangleSources,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.WireTriangleSources());
                     });
  output.def_prop_ro(observation_keys::kWireCornerCorners,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.WireCornerCorners());
                     });
  output.def_prop_ro(observation_keys::kWireCornerSources,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.WireCornerSources());
                     });
  output.def_prop_ro(observation_keys::kWireVertexSources,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.WireVertexSources());
                     });
  output.def_prop_ro(observation_keys::kSourceVertexOffsets,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.SourceVertexOffsets());
                     });
  output.def_prop_ro(observation_keys::kSourceVertexWireMembers,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.SourceVertexWireMembers());
                     });
  output.def_prop_ro(observation_keys::kSourceFaceOffsets,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.SourceFaceOffsets());
                     });
  output.def_prop_ro(observation_keys::kSourceFaceWireTriangles,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.SourceFaceWireTriangles());
                     });
  output.def_prop_ro(observation_keys::kSourceCornerOffsets,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.SourceCornerOffsets());
                     });
  output.def_prop_ro(observation_keys::kSourceCornerWireCorners,
                     [buffer_type](const OutputView& self) {
                       return Memory(buffer_type, self.owner,
                                     self.owner.SourceCornerWireCorners());
                     });
  nb::class_<InspectionView>(module, "OffGlbSurfaceInspectionView")
      .def_prop_ro(observation_keys::kExpected,
                   [](const InspectionView& self) {
                     return OutputView{self.owner.Expected()};
                   })
      .def_prop_ro(
          observation_keys::kCapturedMembers,
          [](const InspectionView& self) { return Members(self.owner); })
      .def_prop_ro(
          observation_keys::kFacts,
          [](const InspectionView& self) { return Value(self.owner.Facts()); })
      .def_prop_ro(observation_keys::kNewRequestedBytes,
                   [](const InspectionView& self) {
                     return self.owner.NewRequestedBytes();
                   });
  nb::class_<PublishedView>(module, "OffGlbPublishedBundleView")
      .def_prop_ro(observation_keys::kInspection,
                   [](const PublishedView& self) {
                     return InspectionView{self.owner.Inspection()};
                   })
      .def_prop_ro(
          observation_keys::kReceipt,
          [](const PublishedView& self) { return ReceiptView{self.owner}; })
      .def_prop_ro(observation_keys::kNewRequestedBytes,
                   [](const PublishedView& self) {
                     return self.owner.NewRequestedBytes();
                   });
  nb::class_<MemberView>(module, "OffGlbMemberView")
      .def_prop_ro(observation_keys::kKind,
                   [](const MemberView& self) { return self.member.kind; })
      .def_prop_ro(observation_keys::kPath,
                   [](const MemberView& self) {
                     return nb::str(self.member.path.data(),
                                    self.member.path.size());
                   })
      .def_prop_ro(observation_keys::kBytes,
                   [buffer_type](const MemberView& self) {
                     return std::visit(
                         [&](const auto& owner) {
                           return Memory(buffer_type, owner, self.member.bytes);
                         },
                         self.owner);
                   })
      .def_prop_ro(observation_keys::kSha256, [](const MemberView& self) {
        return Digest(self.member.sha256);
      });
  nb::class_<ReceiptView>(module, "OffGlbReceiptView")
      .def_prop_ro(observation_keys::kPath,
                   [](const ReceiptView& self) {
                     auto receipt = self.owner.Receipt();
                     return nb::str(receipt.path.data(), receipt.path.size());
                   })
      .def_prop_ro(observation_keys::kBytes,
                   [buffer_type](const ReceiptView& self) {
                     return Memory(buffer_type, self.owner,
                                   self.owner.Receipt().bytes);
                   })
      .def_prop_ro(observation_keys::kSha256, [](const ReceiptView& self) {
        return Digest(self.owner.Receipt().sha256);
      });
  nb::class_<OffGlbExecution>(module, "OffGlbExecution")
      .def(
          "__init__",
          [](OffGlbExecution* self, nb::handle name) {
            auto value = Text(name, kSerialOffGlbPreset.size());
            new (self) OffGlbExecution(value);
          },
          nb::kw_only(), nb::arg("preset") = kBalancedOffGlbPreset)
      .def_prop_ro(observation_keys::kPreset,
                   [](const OffGlbExecution& self) {
                     return nb::str(self.Name().c_str());
                   })
      .def_prop_ro(observation_keys::kResolvedPreset,
                   [](const OffGlbExecution& self) {
                     return PresetObservation(self.Preset());
                   })
      .def_prop_ro(observation_keys::kWorkerBudget,
                   [](const OffGlbExecution& self) {
                     return self.Context().WorkerBudget();
                   })
      .def_prop_ro(observation_keys::kActiveWorkers,
                   [](const OffGlbExecution& self) {
                     return self.Context().ActiveWorkers();
                   })
      .def_prop_ro(observation_keys::kActiveTrackedPayload,
                   [](const OffGlbExecution& self) {
                     return self.Context().ActiveTrackedPayload();
                   });
  nb::class_<PreparedOffGlbExport>(module, "PreparedOffGlbExport");
  auto result = nb::class_<OffGlbExportResult>(module, "OffGlbExportResult");
  result
      .def_prop_ro(
          observation_keys::kCapture,
          [](const OffGlbExportResult& self) { return Value(self.capture); })
      .def_prop_ro(
          observation_keys::kNative,
          [](const OffGlbExportResult& self) { return Value(self.native); })
      .def_prop_ro(observation_keys::kExit, &OffGlbExportResult::Exit)
      .def_prop_ro(observation_keys::kOffReportBytes,
                   [](const OffGlbExportResult& self) -> nb::object {
                     auto* off = self.Off();
                     if (!off) return nb::none();
                     auto bytes = off->ReportJson();
                     return bytes.empty() ? nb::none()
                                          : nb::object(nb::bytes(bytes.data(),
                                                                 bytes.size()));
                   })
      .def_prop_ro(observation_keys::kPublicationReceiptBytes,
                   [](const OffGlbExportResult& self) -> nb::object {
                     auto* f = self.Facts();
                     if (!f || !f->publication || !f->publication->bundle)
                       return nb::none();
                     auto bytes = f->publication->bundle->Receipt().bytes;
                     return nb::bytes(bytes.data(), bytes.size());
                   });
  result.def_prop_ro(observation_keys::kOff,
                     [](const OffGlbExportResult& self) {
                       auto* f = self.Facts();
                       return f ? Value(f->off) : nb::none();
                     });
  result.def_prop_ro(observation_keys::kTriangulation,
                     [](const OffGlbExportResult& self) {
                       auto* f = self.Facts();
                       return f ? Value(f->triangulation) : nb::none();
                     });
  result.def_prop_ro(observation_keys::kEncoding,
                     [](const OffGlbExportResult& self) {
                       auto* f = self.Facts();
                       return f ? Value(f->encoding) : nb::none();
                     });
  result.def_prop_ro(observation_keys::kInspection,
                     [](const OffGlbExportResult& self) {
                       auto* f = self.Facts();
                       return f ? Value(f->inspection) : nb::none();
                     });
  result.def_prop_ro(observation_keys::kPublication,
                     [](const OffGlbExportResult& self) {
                       auto* f = self.Facts();
                       return f ? Value(f->publication) : nb::none();
                     });
  result.def_prop_ro(
      observation_keys::kSourceMesh,
      [](const OffGlbExportResult& self) -> nb::object {
        if (!self.capture.asset) return nb::none();
        return nb::cast(MeshView{self.capture.asset->Document().Mesh()});
      });
  result.def_prop_ro(observation_keys::kCandidate,
                     [](const OffGlbExportResult& self) -> nb::object {
                       auto* repair = self.Repair();
                       return repair ? nb::cast(MeshView{repair->Candidate()})
                                     : nb::none();
                     });
  result.def_prop_ro(
      observation_keys::kCandidateInspection,
      [](const OffGlbExportResult& self) -> nb::object {
        auto* repair = self.Repair();
        return repair
                   ? nb::cast(MeshInspectionView{repair->CandidateInspection()})
                   : nb::none();
      });
  result.def_prop_ro(observation_keys::kTriangulatedMesh,
                     [](const OffGlbExportResult& self) -> nb::object {
                       auto* triangles = self.Triangles();
                       return triangles ? nb::cast(MeshView{triangles->Mesh()})
                                        : nb::none();
                     });
  result.def_prop_ro(
      observation_keys::kTriangulatedInspection,
      [](const OffGlbExportResult& self) -> nb::object {
        auto* triangles = self.Triangles();
        return triangles ? nb::cast(MeshInspectionView{triangles->Inspection()})
                         : nb::none();
      });
  result.def_prop_ro(observation_keys::kOutput,
                     [](const OffGlbExportResult& self) -> nb::object {
                       auto* f = self.Facts();
                       return f && f->encoding && f->encoding->output
                                  ? nb::cast(OutputView{*f->encoding->output})
                                  : nb::none();
                     });
  result.def_prop_ro(
      observation_keys::kProof,
      [](const OffGlbExportResult& self) -> nb::object {
        auto* f = self.Facts();
        return f && f->inspection && f->inspection->inspection
                   ? nb::cast(InspectionView{*f->inspection->inspection})
                   : nb::none();
      });
  result.def_prop_ro(
      observation_keys::kPublished,
      [](const OffGlbExportResult& self) -> nb::object {
        auto* f = self.Facts();
        return f && f->publication && f->publication->bundle
                   ? nb::cast(PublishedView{*f->publication->bundle})
                   : nb::none();
      });
  result.def_prop_ro(observation_keys::kRepairFaceMap,
                     [buffer_type](const OffGlbExportResult& self) {
                       auto* repair = self.Repair();
                       return repair ? Memory(buffer_type, *repair,
                                              repair->FaceMap())
                                     : nb::none();
                     });
  result.def_prop_ro(observation_keys::kRepairCornerMap,
                     [buffer_type](const OffGlbExportResult& self) {
                       auto* repair = self.Repair();
                       return repair ? Memory(buffer_type, *repair,
                                              repair->CornerMap())
                                     : nb::none();
                     });
  result.def_prop_ro(observation_keys::kRepairSourceFaces,
                     [buffer_type](const OffGlbExportResult& self) {
                       auto* repair = self.Repair();
                       return repair ? Memory(buffer_type, *repair,
                                              repair->SourceFaces())
                                     : nb::none();
                     });
  result.def_prop_ro(observation_keys::kRepairSourceCorners,
                     [buffer_type](const OffGlbExportResult& self) {
                       auto* repair = self.Repair();
                       return repair ? Memory(buffer_type, *repair,
                                              repair->SourceCorners())
                                     : nb::none();
                     });
  result.def_prop_ro(observation_keys::kTriangulationFaceSources,
                     [buffer_type](const OffGlbExportResult& self) {
                       auto* triangles = self.Triangles();
                       return triangles ? Memory(buffer_type, *triangles,
                                                 triangles->FaceSources())
                                        : nb::none();
                     });
  result.def_prop_ro(observation_keys::kTriangulationCornerSources,
                     [buffer_type](const OffGlbExportResult& self) {
                       auto* triangles = self.Triangles();
                       return triangles ? Memory(buffer_type, *triangles,
                                                 triangles->CornerSources())
                                        : nb::none();
                     });
  result.def_prop_ro(observation_keys::kTriangulationFaceOutputOffsets,
                     [buffer_type](const OffGlbExportResult& self) {
                       auto* triangles = self.Triangles();
                       return triangles ? Memory(buffer_type, *triangles,
                                                 triangles->FaceOutputOffsets())
                                        : nb::none();
                     });
  result.def_prop_ro(
      observation_keys::kRepairVertexIdentity,
      [](const OffGlbExportResult& self) {
        auto* off = self.Off();
        auto* repair = self.Repair();
        return off && off->Output() && repair
                   ? Identity(repair->Candidate().View().positions.size() /
                              kPositionComponents)
                   : nb::none();
      });
  result.def_prop_ro(
      observation_keys::kTriangulationVertexIdentity,
      [](const OffGlbExportResult& self) {
        auto* triangles = self.Triangles();
        return triangles ? Identity(triangles->Counts().output_vertices)
                         : nb::none();
      });
  module.def(
      "_prepare_off_glb_export", &Prepare, nb::arg("source"),
      nb::arg("destination"), nb::arg("targets"), nb::kw_only(),
      nb::arg("text"), nb::arg("edge_count"), nb::arg("coordinates"),
      nb::arg("precision"), nb::arg("max_position_error"),
      nb::arg("acknowledge_triangle_conversion"),
      nb::arg("acknowledge_corner_expansion"),
      nb::arg("acknowledge_unused_vertices"), nb::arg("input_id") = "source",
      nb::arg("receipt_path") = ic::kDefaultSurfacePublicationReceiptPath,
      nb::arg("preset") = kBalancedOffGlbPreset);
  module.def("_run_off_glb_export", &Run, nb::arg("prepared"), nb::kw_only(),
             nb::arg("execution") = nb::none(),
             nb::arg("cancellation") = nb::none());
  module.def("_observe_off_glb_export", &ObserveResult, nb::arg("result"));
}
}  // namespace meshvale::repair::python
