// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/ply_workflow.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace rep = meshvale::repair;
namespace fs = std::filesystem;

constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::size_t kObservationFileBytes = 2 * kMiB;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kDescriptorBytes = 8192;
constexpr std::uint64_t kRequestBytes = 8 * kMiB;
constexpr std::uint64_t kWrapperBytes = 64 * kMiB;
constexpr std::uint64_t kRepairBytes = 32 * kMiB;
constexpr std::uint64_t kFileBytes = 64 * kMiB;
constexpr std::uint64_t kLedgerBytes = 256 * kMiB;
constexpr std::uint64_t kHeaderLineBytes = 4096;
constexpr std::uint64_t kTokenBytes = 128;
constexpr std::uint64_t kCommentBytes = 256;
constexpr std::uint64_t kNameBytes = 256;
constexpr std::uint64_t kNativePathUnits = 4096;
constexpr std::uint64_t kInputIdBytes = 128;
constexpr std::uint64_t kIoBytes = 64;
constexpr std::uint64_t kParallelMinBytes = 1;
constexpr std::uint64_t kPartitionBytes = 128;
constexpr std::uint64_t kCancellationBytes = 16;
constexpr std::uint64_t kMaxJobs = 64;
constexpr std::uint64_t kCancellationValues = 4;
constexpr std::uint64_t kGeometryCancellationScalars = 8;
constexpr std::uint32_t kWorkerBudget = 4;
constexpr std::uint32_t kMinimumParallelVertices = 1;
constexpr std::size_t kCoordinates = 3;
constexpr std::size_t kElements = 2;
constexpr std::size_t kProperties = 4;
constexpr std::size_t kSha256HexBytes = 64;
constexpr std::size_t kRequiredChecks = 11;
constexpr std::string_view kInputId = "empty-unsupported-ply";
constexpr std::string_view kEntryName = "empty.ply";
constexpr std::string_view kCapturedName = "captured-empty.ply";
constexpr std::string_view kNonemptyName = "nonempty.ply";
constexpr std::string_view kBundleName = "bundle";
constexpr std::string_view kReceiptName = "meshvale-report.json";
constexpr std::string_view kObservationName = "observations.json";
constexpr std::string_view kRootPrefix = "ply-workflow-unsupported-";
constexpr std::string_view kOutputArgument = "--output-root";
constexpr std::string_view kEmpty =
    "ply\nformat ascii 1.0\nelement vertex 0\nproperty float x\n"
    "property float y\nproperty float z\nelement face 0\n"
    "property list uchar int vertex_indices\nend_header\n";
constexpr std::string_view kNonempty =
    "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n"
    "property float y\nproperty float z\nelement face 1\n"
    "property list uchar int vertex_indices\nend_header\n"
    "0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";

void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
// These are bounded example limits, matching the existing native fixture's
// capacities. They are not a new preset or supported-platform policy.
geo::MeshMaterializationLimits MeshLimits() {
  return {kCountLimit, kCountLimit,      kCountLimit,  kCountLimit,
          kCountLimit, kCountLimit,      kCountLimit,  kCountLimit,
          kCountLimit, kDescriptorBytes, kRequestBytes};
}
ic::PlyReadLimits ReadLimits() {
  return {kMiB,          kMiB,
          kCountLimit,   kHeaderLineBytes,
          kTokenBytes,   kCommentBytes,
          kTokenBytes,   kDescriptorBytes,
          kCountLimit,   kCountLimit,
          kCountLimit,   kCountLimit,
          kCountLimit,   kCountLimit,
          kCountLimit,   kCountLimit,
          kRequestBytes, 3 * kRequestBytes,
          MeshLimits(),  {kCountLimit, kRequestBytes}};
}
ic::PlyWriteLimits WriteLimits() {
  return {kMiB,
          kCountLimit,
          kHeaderLineBytes,
          kTokenBytes,
          kTokenBytes,
          kDescriptorBytes,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kRequestBytes,
          2 * kRequestBytes,
          kMiB,
          {kCountLimit, kRequestBytes}};
}
ic::PlyWorkPolicy CodecWork() {
  return {
      kCancellationBytes,
      kCancellationValues,
      1,
      {kParallelMinBytes, kIoBytes, kGeometryCancellationScalars, kMaxJobs}};
}
ic::PlyFileLimits FileLimits() {
  return {kMiB,      kMiB, 2 * kMiB, kNameBytes, kNameBytes, kNativePathUnits,
          kFileBytes};
}
ic::PlyFileWorkPolicy FileWork() {
  return {kIoBytes, kParallelMinBytes, kPartitionBytes, kCancellationBytes,
          kMaxJobs};
}
rep::PlyDuplicateFileWorkflowLimits Limits() {
  return {kWrapperBytes,
          kInputIdBytes,
          kMiB,
          kMiB,
          2 * kMiB,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          {kCountLimit,
           kCountLimit,
           kDescriptorBytes,
           kCountLimit,
           kRepairBytes,
           {kCountLimit, kRequestBytes},
           MeshLimits()},
          WriteLimits(),
          ReadLimits(),
          {FileLimits(), {kCountLimit, kRequestBytes}, ReadLimits()}};
}
rep::PlyDuplicateFileWorkflowWorkPolicy Work() {
  const rep::PlyByteWorkPolicy bytes{kParallelMinBytes, kPartitionBytes,
                                     kCancellationBytes, kMaxJobs};
  return {CodecWork().geometry,
          CodecWork(),
          CodecWork(),
          bytes,
          bytes,
          bytes,
          {FileWork(), CodecWork().geometry, CodecWork()}};
}
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  Require(!fs::exists(path) && bytes.size() <= kObservationFileBytes,
          "Write only a bounded new file in the owned directory");
  std::ofstream stream(path, std::ios::binary);
  Require(stream.good(), "Open owned output");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(stream.good(), "Write and close complete bytes");
}
std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  Require(stream.good(), "Open physical observation");
  const auto count = stream.tellg();
  Require(
      count >= 0 && count <= static_cast<std::streamoff>(kObservationFileBytes),
      "Physical observation is bounded");
  std::vector<std::byte> bytes(static_cast<std::size_t>(count));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(stream.good(), "Read complete physical bytes");
  stream.close();
  Require(stream.good(), "Close physical observation");
  return bytes;
}
void Joined(const geo::MaterializationExecutionStats& value) {
  Require(value.launched_workers == value.joined_workers,
          "Every actual worker joined");
}
void Joined(const ic::PlyExecution& value) {
  Joined(value.codec);
  Joined(value.materialization);
  Joined(value.inspection);
}
void Joined(const ic::PlyFileExecution& value) {
  for (const auto& stats :
       {value.capture, value.copying, value.byte_verification,
        value.mesh_verification, value.expected_inspection})
    Joined(stats);
  Joined(value.ply);
}
void Joined(const rep::PlyWorkflowExecution& value) {
  const auto& repair = value.repair;
  for (const auto& stats :
       {repair.source_inspection, repair.eligibility, repair.comparison,
        repair.selection, repair.correspondence, repair.candidate_inspection,
        repair.verification, repair.directed_verification,
        value.source_protection, value.repair_correspondence,
        value.canonical_preservation, value.typed_properties, value.wire_schema,
        value.reload_verification, value.source_verification,
        value.staged_candidate, value.hashing, value.report_copy})
    Joined(stats);
  Joined(value.writing);
  Joined(value.reload);
}
void EmptyMesh(const geo::MaterializedMesh& mesh) {
  const auto view = mesh.View();
  Require(view.positions.empty() && view.corner_vertices.empty() &&
              view.face_offsets.size() == 1 && view.face_offsets.front() == 0 &&
              view.attributes.empty(),
          "True empty mesh has no numerical rows or attributes");
}
void EmptyDocument(const ic::PlyDocument& document) {
  EmptyMesh(document.Mesh());
  Require(document.Encoding() == ic::PlyEncoding::kAscii,
          "Actual document encoding is explicit ASCII");
  const auto schema = document.Schema();
  Require(schema.size() == kElements &&
              schema[0].kind == ic::PlyElementKind::kVertex &&
              schema[1].kind == ic::PlyElementKind::kFace &&
              schema[0].original_count == 0 && schema[1].original_count == 0 &&
              schema[0].properties.size() == kCoordinates &&
              schema[1].properties.size() == 1,
          "Original empty counts and ordered wire schema remain present");
  constexpr std::array<std::string_view, kCoordinates> kNames{"x", "y", "z"};
  constexpr std::array kBindings{ic::PlyBinding::kX, ic::PlyBinding::kY,
                                 ic::PlyBinding::kZ};
  for (std::size_t i = 0; i < kCoordinates; ++i) {
    const auto& property = schema[0].properties[i];
    Require(property.name == kNames[i] &&
                property.kind == ic::PlyPropertyKind::kScalar &&
                property.value_type == ic::PlyScalarType::kFloat &&
                !property.count_type && !property.attribute &&
                property.binding == kBindings[i],
            "Coordinates preserve scalar width, order and bindings");
  }
  const auto& indices = schema[1].properties.front();
  constexpr std::string_view kIndices = "vertex_indices";
  Require(indices.name == kIndices &&
              indices.kind == ic::PlyPropertyKind::kList &&
              indices.value_type == ic::PlyScalarType::kInt &&
              indices.count_type == ic::PlyScalarType::kUchar &&
              indices.binding == ic::PlyBinding::kVertexIndices &&
              !indices.attribute,
          "Empty face-list declaration preserves both original wire widths");
}
void CheckRecord(const rep::PlyDuplicateFileWorkflowRecord& record,
                 const fs::path& bundle) {
  const auto* repair = record.RepairFacts();
  Require(repair && repair->Outcome() == rep::CandidateOutcome::unchanged &&
              repair->FaceMap().empty() && repair->CornerMap().empty() &&
              repair->SourceFaces().empty() && repair->SourceCorners().empty(),
          "Real unchanged Repair retains all four complete empty maps");
  EmptyMesh(repair->Source());
  EmptyMesh(repair->Candidate());
  EmptyDocument(record.Source().Document());
  Require(std::ranges::equal(record.Source().EntryBytes(), Bytes(kEmpty)),
          "Captured original bytes outlive the deleted source file");
  Require(record.Output() && record.Reload() && record.Published() &&
              record.WriteFacts(),
          "Actual verified writer, reload and published owners are retained");
  EmptyDocument(*record.Reload());
  EmptyDocument(record.Published()->Document());
  const auto& facts = *record.WriteFacts();
  Require(facts.source_encoding == ic::PlyEncoding::kAscii &&
              facts.encoding == ic::PlyEncoding::kAscii &&
              facts.rewrite_policy == ic::PlyRewritePolicy::kCanonical &&
              facts.source_vertex_count == 0 && facts.source_face_count == 0 &&
              facts.vertex_count == 0 && facts.face_count == 0 &&
              facts.corner_count == 0 && facts.property_count == kProperties &&
              facts.correspondence == ic::PlyCorrespondence::kIdentity,
          "Actual writer facts distinguish empty data from absent schema");
  Require(record.InputSha256() &&
              record.InputSha256()->size() == kSha256HexBytes &&
              std::ranges::all_of(*record.InputSha256(),
                                  [](char ch) {
                                    return (ch >= '0' && ch <= '9') ||
                                           (ch >= 'a' && ch <= 'f');
                                  }),
          "Actual source SHA is a complete lowercase SHA256");
  Require(
      std::ranges::equal(Read(bundle / kEntryName), record.Output()->Bytes()) &&
          std::ranges::equal(Read(bundle / kReceiptName), record.ReportJson()),
      "Physical output and receipt equal the genuine native bytes");
  std::set<fs::path> names;
  for (const auto& entry : fs::directory_iterator(bundle)) {
    Require(entry.is_regular_file() && !entry.is_symlink(),
            "Committed bundle members are regular files");
    names.insert(entry.path().filename());
  }
  Require(names == std::set<fs::path>{kEntryName, kReceiptName},
          "Exactly one entry and one real report are published");
  const auto coverage = record.Coverage();
  Require(coverage.size() > kRequiredChecks, "Required coverage is present");
  for (std::size_t i = 0; i < kRequiredChecks; ++i)
    Require(
        coverage[i].check == static_cast<rep::PlyWorkflowCheck>(i) &&
            coverage[i].state == rep::PlyWorkflowCoverageState::kPerformed &&
            coverage[i].passed == true,
        "All actual required memory and publication predicates passed");
  Joined(record.Execution());
}
ic::PlyFileReadResult Capture(const fs::path& path,
                              const geo::ExecutionContext& context) {
  return ic::ReadPlyFileAsset(path, {FileLimits(), ReadLimits()},
                              {FileWork(), CodecWork()}, context);
}
void Observe(const fs::path& root, const ic::PlyFileReadResult& refused,
             const rep::PlyDuplicateFileWorkflowRecord& record,
             const geo::ExecutionContext& ledger) {
  // Fixed ASCII keys and a checked hexadecimal getter require no JSON escaping.
  constexpr std::string_view kSchemaKey = "schema";
  constexpr std::string_view kSchema = "meshvale.repair.ply-unsupported/1";
  constexpr std::string_view kCaptureStatus = "capture_status";
  constexpr std::string_view kCaptureStage = "capture_stage";
  constexpr std::string_view kCaptureCode = "capture_code";
  constexpr std::string_view kPlyStatus = "ply_status";
  constexpr std::string_view kGeometryStatus = "geometry_status";
  constexpr std::string_view kInputSha = "input_sha256";
  constexpr std::string_view kSourceEncoding = "source_encoding";
  constexpr std::string_view kReloadEncoding = "reload_encoding";
  constexpr std::string_view kPublishedEncoding = "published_encoding";
  constexpr std::string_view kExitCode = "exit_code";
  constexpr std::string_view kWorkers = "active_workers_with_retained_record";
  const auto path = root / kObservationName;
  Require(!fs::exists(path), "Fresh observation file");
  std::ofstream output(path, std::ios::binary);
  Require(output.good(), "Open UTF-8 ASCII observation");
  output << "{\n\"" << kSchemaKey << "\":\"" << kSchema << "\",\n";
  const auto number = [&](std::string_view key, auto value) {
    output << '"' << key << "\":" << static_cast<std::uint64_t>(value) << ",\n";
  };
  number(kCaptureStatus, refused.status);
  number(kCaptureStage, refused.diagnostic.stage);
  number(kCaptureCode, refused.diagnostic.code);
  number(kPlyStatus, *refused.diagnostic.ply_status);
  number(kGeometryStatus, *refused.diagnostic.ply_diagnostic->geometry_status);
  number(kSourceEncoding, record.Source().Document().Encoding());
  number(kReloadEncoding, record.Reload()->Encoding());
  number(kPublishedEncoding, record.Published()->Document().Encoding());
  number(kExitCode, record.Outcomes().exit);
  number(kWorkers, ledger.ActiveWorkers());
  output << '"' << kInputSha << "\":\"" << *record.InputSha256() << "\"\n}\n";
  output.close();
  Require(output.good(), "Close complete observation");
}
void Run(const fs::path& root) {
  const auto input = root / kEntryName;
  const auto bundle = root / kBundleName;
  const auto nonempty = root / kNonemptyName;
  Write(nonempty, Bytes(kNonempty));
  Write(input, Bytes(kEmpty));
  Write(root / kCapturedName, Bytes(kEmpty));
  std::optional<rep::PlyDuplicateFileWorkflowRecord> retained;
  std::optional<geo::ExecutionContext> ledger;
  {
    geo::ExecutionContext context(
        {kWorkerBudget, kMinimumParallelVertices, kLedgerBytes});
    const auto refused = Capture(nonempty, context);
    Require(
        !refused.asset && refused.status == ic::PlyFileStatus::kPlyFailure &&
            refused.diagnostic.stage == ic::PlyFileStage::kPlyRead &&
            refused.diagnostic.code ==
                ic::PlyFileDiagnosticCode::kDelegatedPly &&
            refused.diagnostic.ply_status == ic::PlyStatus::kGeometryFailure &&
            refused.diagnostic.ply_diagnostic &&
            refused.diagnostic.ply_diagnostic->stage ==
                ic::PlyStage::kMaterialization &&
            refused.diagnostic.ply_diagnostic->code ==
                ic::PlyDiagnosticCode::kGeometry &&
            refused.diagnostic.ply_diagnostic->geometry_status ==
                geo::MaterializationStatus::kUnsupportedAllocationProfile &&
            refused.diagnostic.ply_diagnostic->geometry_diagnostic &&
            refused.diagnostic.ply_diagnostic->geometry_diagnostic->code ==
                geo::MaterializationDiagnosticCode::kAllocationProfile,
        "Real nonempty capture refuses before any Repair invocation");
    Joined(refused.execution);
    Require(std::ranges::equal(Read(nonempty), Bytes(kNonempty)) &&
                context.ActiveTrackedPayload() == 0 &&
                context.ActiveWorkers() == 0,
            "Refused capture leaves original source and ledger unchanged");
    const auto captured = Capture(input, context);
    Require(captured.status == ic::PlyFileStatus::kSuccess && captured.asset,
            "Real empty capture succeeds without numerical rows");
    EmptyDocument(captured.asset->Document());
    Joined(captured.execution);
    ledger = captured.asset->Document().Mesh().ShareExecutionContext();
    Require(geo::SharesExecutionBudget(*ledger, context),
            "The source keeps the actual capture ledger");
    Require(fs::remove(input), "Remove only the captured owned input file");
    const rep::PlyDuplicateFileWorkflowRequest request{
        *captured.asset,
        kInputId,
        {},
        {ic::PlyEncoding::kAscii, ic::PlyRewritePolicy::kCanonical},
        {},
        rep::PlyWorkflowPublication{bundle, kReceiptName}};
    const auto result =
        rep::RunPlyDuplicateFileWorkflow(request, Limits(), Work());
    Require(result.status == rep::PlyWorkflowStatus::kSuccess &&
                result.record &&
                result.outcomes.execution ==
                    rep::PlyWorkflowExecutionOutcome::kCompleted &&
                result.outcomes.candidate == rep::CandidateOutcome::unchanged &&
                result.outcomes.publication ==
                    rep::PlyWorkflowPublicationOutcome::kPublished &&
                result.outcomes.stage == rep::PlyWorkflowStage::kComplete &&
                result.outcomes.exit == rep::PlyWorkflowExitMeaning::kSuccess &&
                result.publication &&
                result.publication->outcome ==
                    ic::PlyFilePublicationOutcome::kPublished,
            "Real empty Repair and canonical publication complete");
    Joined(result.execution);
    Joined(result.publication->execution);
    retained = result.record;
    CheckRecord(*retained, bundle);
    Observe(root, refused, *retained, *ledger);
  }
  // The original context and asset have died. All views now borrow this record.
  CheckRecord(*retained, bundle);
  Require(!fs::exists(input) && ledger->ActiveWorkers() == 0,
          "No source reread and no worker survives synchronous return");
  retained.reset();
  Require(ledger->ActiveTrackedPayload() == 0 && ledger->ActiveWorkers() == 0,
          "The final owner releases every real charge and worker slot");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Require(argc == 1 ||
                (argc == 3 && std::string_view(argv[1]) == kOutputArgument),
            "Use --output-root NEW_DIRECTORY, or a fresh default directory");
    const auto parent = fs::canonical(fs::current_path());
    const auto root =
        argc == 3 ? fs::absolute(fs::path(argv[2]))
                  : parent / (std::string(kRootPrefix) +
                              std::to_string(std::chrono::steady_clock::now()
                                                 .time_since_epoch()
                                                 .count()));
    Require(!fs::exists(root) && fs::is_directory(root.parent_path()),
            "The output parent exists and output root must be absent");
    Require(fs::create_directory(root),
            "Create one fresh owned output directory");
    Run(root);
    std::cout << "PLY unsupported producer: one real capture-only refusal; one "
                 "empty workflow and two-member publication; owners and ledger "
                 "verified\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
