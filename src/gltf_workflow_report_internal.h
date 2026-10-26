// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_REPORT_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_REPORT_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

#include "gltf_workflow_internal.h"

namespace meshvale::repair::gltf_workflow_detail {
// Exact streaming extent/emission shared by the existing memory report and
// its file projection. No file-module header or allocator is required here.
class ReportSink {
 public:
  explicit ReportSink(std::span<std::byte> output = {},
                      std::stop_token stop = {});
  void Raw(std::string_view value);
  void String(std::string_view value);
  void Integer(std::uint64_t value);
  void SignedInteger(std::int64_t value);
  void Boolean(bool value);
  template <class T>
  void OptionalInteger(const std::optional<T>& value) {
    if (value)
      Integer(static_cast<std::uint64_t>(*value));
    else
      Raw("null");
  }
  template <class T>
  void Integers(std::span<const T> values) {
    Raw("[");
    bool comma = false;
    for (auto value : values) {
      if (comma) Raw(",");
      comma = true;
      Integer(value);
    }
    Raw("]");
  }
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  std::span<std::byte> output_;
  std::stop_token stop_;
  std::size_t size_ = 0;
};
enum class FileReportPublication {
  kNotRequested,
  kNotAttempted,
  kFailed,
  kCanceled,
  kPublished
};
enum class FileReportCoreStage { kMemory, kReport, kExport, kComplete };
enum class FileReportFormat { kJson, kGlb };
struct FileReportArtifact {
  std::string_view id, path, sha256;
};
struct FileReportSystemError {
  std::int64_t value;
  std::string_view category;
};
// Native enum ordinals are projected only at this serialization boundary.
// Actual delegate types remain on the public file result, outside this module.
struct FileReportPublisherDiagnostic {
  std::uint64_t status, stage, code;
  std::optional<std::uint64_t> file, byte, resource_kind, resource;
  std::optional<FileReportSystemError> system_error;
  std::optional<interchange::GltfDiagnostic> gltf;
  std::optional<interchange::GltfTopologyOutputInspectionStatus>
      inspection_status;
  std::optional<interchange::GltfTopologyOutputInspectionDiagnostic> inspection;
};
struct FileReportProjection {
  GltfWorkflowOutcomes outcomes;
  FileReportCoreStage core_stage = FileReportCoreStage::kMemory;
  bool requested = false;
  FileReportPublication publication = FileReportPublication::kNotRequested;
  FileReportFormat format = FileReportFormat::kJson;
  GltfWorkflowCoverage staged_primitive, physical_members;
  GltfWorkflowTranslationFacts translation;
  GltfWorkflowCoverage translation_coverage;
  const interchange::GltfTopologyOutputInspection* output = nullptr;
  std::string_view input_sha256, report_path;
  std::span<const FileReportArtifact> artifacts;
  GltfWorkflowStatus status = GltfWorkflowStatus::kSuccess;
  std::uint64_t native_stage = 0, native_code = 0;
  std::optional<FileReportPublisherDiagnostic> publisher;
  std::optional<std::uint64_t> cleanup;
  std::optional<FileReportSystemError> cleanup_error;
  std::uint64_t retained_files = 0;
  std::optional<std::uint64_t> retained_reader;
  bool container_counter_unavailable = false;
  // This sample precedes receipt buffers/jobs; the live getter remains the
  // authoritative current counter. Publisher final statistics are separate.
  std::uint64_t wrapper_pre_report_bytes = 0;
  GltfWorkflowRequestedBytes memory;
  GltfWorkflowByteExecutionStats hashing, report_copy;
};
// A null projection preserves the existing report bytes and dependency cohort.
void EmitWorkflowReport(ReportSink& sink, const WorkflowState& state,
                        const GltfDuplicateWorkflowRequest& request,
                        const FileReportProjection* file = nullptr);
void EmitFileWorkflowReportPrefix(ReportSink& sink, const WorkflowState& state,
                                  const GltfDuplicateWorkflowRequest& request,
                                  const FileReportProjection& file);
void EmitFileWorkflowReportTail(ReportSink& sink,
                                const FileReportProjection& file);
}  // namespace meshvale::repair::gltf_workflow_detail
#endif  // MESHVALE_REPAIR_SRC_GLTF_WORKFLOW_REPORT_INTERNAL_H_
