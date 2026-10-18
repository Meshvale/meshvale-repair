// SPDX-License-Identifier: Apache-2.0
#include <meshvale/repair/gltf_file_workflow.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>

#include "gltf_file_workflow_internal.h"
#include "gltf_file_workflow_test_internal.h"
#include "gltf_workflow_internal.h"
#include "gltf_workflow_report_internal.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::gltf_file_workflow_detail {
Artifact::Artifact(const allocation::AllocationContext& allocation)
    : path(allocation::Allocator<char>(allocation)) {}
State::State(const allocation::AllocationContext& allocator,
             const interchange::GltfFileAsset& input)
    : allocation(allocator),
      source(input),
      report_path(allocation::Allocator<char>(allocator)),
      report(allocation::Allocator<std::byte>(allocator)),
      artifacts(allocation::Allocator<Artifact>(allocator)),
      artifact_views(
          allocation::Allocator<gltf_workflow_detail::FileReportArtifact>(
              allocator)) {}
GltfFileWorkflowRequestedBytes State::Bytes() const noexcept {
  GltfFileWorkflowRequestedBytes bytes;
  bytes.retained_source = {
      source.NewRequestedBytes(), source.Asset().NewRequestedBytes(),
      source.Format() == interchange::GltfFileFormat::kGlb
          ? GltfContainerCounterAvailability::kUnavailable
          : GltfContainerCounterAvailability::kNotApplicable};
  bytes.wrapper = allocation.OperationActiveBytes();
  if (memory) bytes.memory = memory->NewRequestedBytes();
  if (published) bytes.publication_files = published->NewRequestedBytes();
  return bytes;
}
namespace {
namespace memory = gltf_workflow_detail;
namespace hash = gltf_file_detail;
namespace ic = interchange;
using Stage = GltfFileWorkflowStage;
using Code = GltfFileWorkflowDiagnosticCode;
using Status = GltfWorkflowStatus;
using Publication = GltfFileWorkflowPublicationOutcome;
using CoverageState = GltfWorkflowCoverageState;
struct Failure {
  Status status;
  Stage stage;
  Code code;
};
// The public publisher translates callback exceptions after its real rollback.
// This private signal never escapes; the original owning fact is retained.
struct ProviderFailure {};
[[noreturn]] void Fail(Status status, Stage stage, Code code) {
  throw Failure{status, stage, code};
}
void Positive(std::uint64_t value) {
  if (!value)
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kRequiredFieldZero);
  if (value > std::numeric_limits<std::size_t>::max())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
}
template <class T>
void ByteWork(const T& work) {
  Positive(work.parallel_min_bytes);
  Positive(work.partition_bytes);
  Positive(work.cancellation_block_bytes);
  Positive(work.max_jobs);
}
GltfDuplicateWorkflowRequest MemoryRequest(
    const GltfDuplicateFileWorkflowRequest& request) {
  return {request.source.Asset().OriginalJson(),
          {},
          request.input_id,
          request.primitive_ordinal,
          request.extraction,
          request.targets,
          request.repair};
}
GltfDuplicateWorkflowLimits MemoryLimits(
    const GltfDuplicateFileMemoryLimits& limits) {
  return {limits.max_new_requested_bytes,
          limits.max_input_id_bytes,
          limits.max_reload_bindings,
          limits.max_report_bytes,
          {},
          limits.extraction,
          limits.materialization,
          limits.repair,
          limits.writing,
          limits.reload,
          limits.reload_extraction,
          limits.verification};
}
memory::RetainedSourceInitialization Initialization(
    const GltfDuplicateFileWorkflowRequest& request,
    const GltfDuplicateFileWorkflowLimits& limits) {
  return {request.source.Asset(), limits.memory.max_source_protection_bytes,
          limits.memory.max_source_binding_records};
}
void Validate(const GltfDuplicateFileWorkflowRequest& request,
              const GltfDuplicateFileWorkflowLimits& limits,
              const GltfDuplicateFileWorkflowWorkPolicy& work) {
  for (auto value :
       {limits.max_wrapper_new_requested_bytes, limits.max_source_files,
        limits.max_supplemental_files, limits.max_report_path_bytes,
        limits.max_report_bytes, limits.max_hash_input_bytes})
    Positive(value);
  ByteWork(work.hashing);
  ByteWork(work.report);
  const auto& files = limits.publication.files;
  for (auto value :
       {files.max_files, files.max_resource_bindings, files.max_file_bytes,
        files.max_total_file_bytes, files.max_path_bytes,
        files.max_total_path_bytes, files.max_native_path_code_units,
        files.max_new_requested_bytes})
    Positive(value);
  const auto& file_work = work.publication.files;
  Positive(file_work.io_block_bytes);
  ByteWork(file_work);
  for (auto value : {limits.publication.container.max_container_bytes,
                     limits.publication.container.max_chunks,
                     limits.publication.container.max_requested_bytes,
                     work.publication.container.parallel_min_bytes,
                     work.publication.container.partition_bytes})
    Positive(value);
  const auto host = std::numeric_limits<std::size_t>::max();
  if (request.source.Files().size() > limits.max_source_files ||
      request.source.Files().size() > host / sizeof(ic::GltfFileView))
    Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
  if (request.publication &&
      (request.publication->supplemental_files.size() >
           limits.max_supplemental_files ||
       request.publication->supplemental_files.size() >
           host / sizeof(ic::GltfFileView) ||
       request.publication->report_path.size() > limits.max_report_path_bytes))
    Fail(Status::kLimitExceeded, Stage::kConfiguration, Code::kCountLimit);
  if (request.publication && request.publication->report_path.empty())
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  if (request.publication &&
      !memory::IsWorkflowReportUtf8(request.publication->report_path))
    Fail(Status::kInvalidConfiguration, Stage::kConfiguration,
         Code::kConfigurationExtent);
  auto engine_limits = MemoryLimits(limits.memory);
  const auto init = Initialization(request, limits);
  if (auto failure = memory::ValidateRetainedWorkflow(
          init, MemoryRequest(request), engine_limits, work.memory))
    Fail(failure->status, Stage::kConfiguration,
         failure->diagnostic.code ==
                 GltfWorkflowDiagnosticCode::kRequiredFieldZero
             ? Code::kRequiredFieldZero
         : failure->diagnostic.code == GltfWorkflowDiagnosticCode::kCountLimit
             ? Code::kCountLimit
             : Code::kConfigurationExtent);
  // Reuse the owning memory validation for common public delegate types. This
  // admits positive publication settings even when export is not requested.
  engine_limits.reload = limits.publication.reload;
  engine_limits.extraction = limits.publication.extraction;
  engine_limits.verification = limits.publication.inspection;
  auto engine_work = work.memory;
  engine_work.interchange = work.publication.reload;
  engine_work.verification = work.publication.inspection;
  if (auto failure = memory::ValidateRetainedWorkflow(
          init, MemoryRequest(request), engine_limits, engine_work))
    Fail(failure->status, Stage::kConfiguration,
         failure->diagnostic.code ==
                 GltfWorkflowDiagnosticCode::kRequiredFieldZero
             ? Code::kRequiredFieldZero
         : failure->diagnostic.code == GltfWorkflowDiagnosticCode::kCountLimit
             ? Code::kCountLimit
             : Code::kConfigurationExtent);
}
Status Admission(const allocation::AdmissionFailure& failure) {
  switch (failure.Code()) {
    case allocation::AdmissionCode::kCanceled:
      return Status::kCanceled;
    case allocation::AdmissionCode::kSizeLimit:
      return Status::kLimitExceeded;
    case allocation::AdmissionCode::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case allocation::AdmissionCode::kInvalidObject:
      return Status::kSourceContractFailure;
  }
  std::terminate();
}
void Latch(State& state, const Failure& failure) {
  state.status = failure.status;
  state.diagnostic.stage = failure.stage;
  state.diagnostic.code = failure.code;
  state.outcomes.stage = failure.stage;
}
void Compute(State& state, bool requested) {
  auto& outcomes = state.outcomes;
  bool failed = false, incomplete = false;
  if (state.memory) {
    outcomes.candidate = state.memory->Outcomes().candidate;
    const auto coverage = state.memory->Coverage();
    for (std::size_t i = 0; i < memory::kRequiredCheckCount; ++i) {
      failed |= coverage[i].passed == false;
      incomplete |= coverage[i].state != CoverageState::kPerformed;
    }
  } else if (state.memory_outcomes) {
    outcomes.candidate = state.memory_outcomes->candidate;
    failed =
        state.memory_outcomes->profile == GltfWorkflowProfileOutcome::kFailed;
    incomplete = state.memory_outcomes->profile ==
                 GltfWorkflowProfileOutcome::kIncomplete;
  } else
    incomplete = true;
  if (requested)
    for (const auto* check :
         {&state.staged_primitive, &state.physical_members}) {
      failed |= check->passed == false;
      incomplete |= check->state != CoverageState::kPerformed;
    }
  outcomes.profile = failed       ? GltfWorkflowProfileOutcome::kFailed
                     : incomplete ? GltfWorkflowProfileOutcome::kIncomplete
                                  : GltfWorkflowProfileOutcome::kPassed;
  outcomes.execution =
      state.status == Status::kCanceled
          ? GltfWorkflowExecutionOutcome::kCanceled
      : (state.status == Status::kSuccess || state.status == Status::kRejected)
          ? GltfWorkflowExecutionOutcome::kCompleted
          : GltfWorkflowExecutionOutcome::kFailed;
  outcomes.exit = state.status == Status::kCanceled
                      ? GltfWorkflowExitMeaning::kCanceled
                  : outcomes.execution == GltfWorkflowExecutionOutcome::kFailed
                      ? GltfWorkflowExitMeaning::kExecutionFailure
                  : (state.status == Status::kRejected ||
                     outcomes.profile != GltfWorkflowProfileOutcome::kPassed)
                      ? GltfWorkflowExitMeaning::kRejectedOrIncomplete
                      : GltfWorkflowExitMeaning::kSuccess;
}
memory::FileReportProjection Projection(
    const State& state, const ic::GltfTopologyOutputInspection* output,
    const ic::GltfFilePublicationResult* publication) {
  memory::FileReportProjection file;
  file.outcomes = {state.outcomes.execution, state.outcomes.candidate,
                   state.outcomes.profile, GltfWorkflowStage::kComplete,
                   state.outcomes.exit};
  file.core_stage = state.outcomes.stage == Stage::kMemory
                        ? memory::FileReportCoreStage::kMemory
                    : state.outcomes.stage == Stage::kPublication
                        ? memory::FileReportCoreStage::kExport
                    : state.outcomes.stage == Stage::kComplete
                        ? memory::FileReportCoreStage::kComplete
                        : memory::FileReportCoreStage::kReport;
  file.requested = state.outcomes.publication != Publication::kNotRequested;
  file.publication =
      static_cast<memory::FileReportPublication>(state.outcomes.publication);
  file.format = state.source.Format() == ic::GltfFileFormat::kGltfJson
                    ? memory::FileReportFormat::kJson
                    : memory::FileReportFormat::kGlb;
  file.staged_primitive = state.staged_primitive;
  file.physical_members = state.physical_members;
  file.output = output;
  if (state.input_hash_available)
    file.input_sha256 = {state.input_hash.data(), state.input_hash.size()};
  file.report_path = {state.report_path.data(), state.report_path.size()};
  file.artifacts = state.artifact_views;
  file.status = state.status;
  file.native_stage = static_cast<std::uint64_t>(state.outcomes.stage);
  file.native_code = static_cast<std::uint64_t>(state.diagnostic.code);
  const auto bytes = state.Bytes();
  file.retained_files = bytes.retained_source.files;
  file.retained_reader = bytes.retained_source.capped_reader;
  file.container_counter_unavailable =
      bytes.retained_source.container ==
      GltfContainerCounterAvailability::kUnavailable;
  file.memory = bytes.memory;
  if (publication) {
    const auto& d = publication->diagnostic;
    memory::FileReportPublisherDiagnostic p{
        static_cast<std::uint64_t>(d.status),
        static_cast<std::uint64_t>(d.stage),
        static_cast<std::uint64_t>(d.code),
        d.file_index,
        d.byte_offset,
        {},
        d.resource_index,
        {},
        d.gltf_diagnostic,
        d.inspection_status,
        d.inspection_diagnostic};
    if (d.resource_kind)
      p.resource_kind = static_cast<std::uint64_t>(*d.resource_kind);
    if (d.system_error)
      p.system_error = memory::FileReportSystemError{
          d.system_error->value(), d.system_error->category().name()};
    file.publisher = p;
    file.cleanup = static_cast<std::uint64_t>(publication->cleanup);
    if (publication->cleanup_error)
      file.cleanup_error = memory::FileReportSystemError{
          publication->cleanup_error->value(),
          publication->cleanup_error->category().name()};
  }
  return file;
}
constexpr std::string_view kHashDomain = "meshvale.gltf-file-snapshot/1";
constexpr std::string_view kJsonFormat = "gltf-json", kGlbFormat = "glb";
constexpr std::size_t kLengthFrameBytes = sizeof(std::uint64_t);
constexpr unsigned kBitsPerByte = 8;
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
std::uint64_t SourceStreamBytes(const State& state) {
  auto size = std::uint64_t{0};
  const auto add = [&](std::uint64_t bytes) {
    if (!hash::CanAppendSha256Bytes(size, bytes))
      Fail(Status::kLimitExceeded, Stage::kHash, Code::kHashInputExtent);
    size += bytes;
  };
  add(kHashDomain.size() + 1);
  add(kLengthFrameBytes);
  add(state.source.Format() == ic::GltfFileFormat::kGltfJson
          ? kJsonFormat.size()
          : kGlbFormat.size());
  add(kLengthFrameBytes);
  for (const auto& file : state.source.Files()) {
    add(kLengthFrameBytes);
    add(file.path.size());
    add(kLengthFrameBytes);
    add(file.bytes.size());
  }
  return size;
}
void Update(hash::Sha256& digest, std::span<const std::byte> bytes,
            std::size_t block, std::stop_token stop) {
  while (!bytes.empty()) {
    if (stop.stop_requested())
      throw allocation::AdmissionFailure(allocation::AdmissionCode::kCanceled);
    const auto count = std::min(block, bytes.size());
    if (!digest.Update(bytes.first(count)))
      Fail(Status::kLimitExceeded, Stage::kHash, Code::kHashInputExtent);
    bytes = bytes.subspan(count);
  }
}
void Length(hash::Sha256& digest, std::uint64_t length, std::size_t block,
            std::stop_token stop) {
  constexpr std::uint64_t kLengthByteMask = 0xff;
  std::array<std::byte, kLengthFrameBytes> frame{};
  for (std::size_t i = 0; i < frame.size(); ++i) {
    frame[i] = static_cast<std::byte>(length & kLengthByteMask);
    length >>= kBitsPerByte;
  }
  Update(digest, frame, block, stop);
}
void SourceHash(State& state, std::size_t block, std::stop_token stop) {
  hash::Sha256 digest;
  Update(digest, Bytes(kHashDomain), block, stop);
  constexpr std::array<std::byte, 1> kDomainTerminator{};
  Update(digest, kDomainTerminator, block, stop);
  const auto format = state.source.Format() == ic::GltfFileFormat::kGltfJson
                          ? kJsonFormat
                          : kGlbFormat;
  Length(digest, format.size(), block, stop);
  Update(digest, Bytes(format), block, stop);
  Length(digest, state.source.Files().size(), block, stop);
  for (const auto& file : state.source.Files()) {
    Length(digest, file.path.size(), block, stop);
    Update(digest, Bytes(file.path), block, stop);
    Length(digest, file.bytes.size(), block, stop);
    Update(digest, file.bytes, block, stop);
  }
  state.input_hash = hash::LowercaseSha256Hex(digest.Digest());
  state.input_hash_available = true;
}
struct HashPlan {
  std::span<const ic::GltfFileView> input;
  std::span<Artifact> output;
  std::size_t block;
  std::stop_token stop;
};
void ArtifactHash(void* opaque, std::size_t begin, std::size_t end) {
  auto& plan = *static_cast<HashPlan*>(opaque);
  for (auto i = begin; i < end; ++i) {
    hash::Sha256 digest;
    Update(digest, plan.input[i].bytes, plan.block, plan.stop);
    plan.output[i].sha256 = hash::LowercaseSha256Hex(digest.Digest());
  }
}
void HashFiles(State& state, const ic::GltfFileVerifiedContent& content,
               const GltfDuplicateFileWorkflowLimits& limits,
               const GltfDuplicateFileWorkflowWorkPolicy& work,
               const geometry::ExecutionContext& context, std::stop_token stop,
               const FileWorkflowTestOptions& options) {
  state.outcomes.stage = Stage::kHash;
  auto total = SourceStreamBytes(state);
  std::uint64_t artifact_bytes = 0;
  for (const auto& file : content.files) {
    if (file.bytes.size() > hash::kSha256MaxInputBytes ||
        file.bytes.size() >
            std::numeric_limits<std::uint64_t>::max() - artifact_bytes)
      Fail(Status::kLimitExceeded, Stage::kHash, Code::kHashInputExtent);
    artifact_bytes += file.bytes.size();
  }
  if (total > limits.max_hash_input_bytes ||
      artifact_bytes > limits.max_hash_input_bytes - total)
    Fail(Status::kLimitExceeded, Stage::kHash, Code::kHashInputExtent);
  total += artifact_bytes;
  if (content.files.size() > limits.publication.files.max_files ||
      content.files.size() >
          std::numeric_limits<std::size_t>::max() / sizeof(Artifact) ||
      content.files.size() > std::numeric_limits<std::size_t>::max() /
                                 sizeof(memory::FileReportArtifact))
    Fail(Status::kLimitExceeded, Stage::kHash, Code::kCountLimit);
  if (options.after_hash_admission)
    options.after_hash_admission(content, total, options.state);
  // A source stream is one ordered SHA chain; independent physical artifact
  // streams below use native shared workers. No worker starts before admission.
  state.allocation.Checkpoint();
  ++state.execution.hashing.planned_jobs;
  ++state.execution.hashing.caller_jobs;
  SourceHash(state,
             static_cast<std::size_t>(work.hashing.cancellation_block_bytes),
             stop);
  state.artifacts.reserve(content.files.size());
  state.artifact_views.reserve(content.files.size());
  constexpr std::string_view kArtifactPrefix = "artifact-";
  for (std::size_t i = 0; i < content.files.size(); ++i) {
    auto& artifact = state.artifacts.emplace_back(state.allocation);
    std::copy(kArtifactPrefix.begin(), kArtifactPrefix.end(),
              artifact.id.begin());
    const auto number =
        std::to_chars(artifact.id.data() + kArtifactPrefix.size(),
                      artifact.id.data() + artifact.id.size(), i);
    artifact.id_size =
        static_cast<std::size_t>(number.ptr - artifact.id.data());
    artifact.path.assign(content.files[i].path.begin(),
                         content.files[i].path.end());
  }
  HashPlan plan{content.files, state.artifacts,
                static_cast<std::size_t>(work.hashing.cancellation_block_bytes),
                stop};
  // Whole-file chaining is indivisible. Mean byte cost (rounded up) makes the
  // shared scheduler byte-aware without pretending one stream is parallel.
  const auto count = content.files.size();
  const auto cost =
      count ? artifact_bytes / count + (artifact_bytes % count != 0) : 1;
  const geometry::MeshMaterializationWorkPolicy policy{
      work.hashing.parallel_min_bytes, work.hashing.partition_bytes,
      work.hashing.cancellation_block_bytes, work.hashing.max_jobs};
  geometry::MaterializationExecutionStats stats;
  const auto account = [&] {
    state.execution.hashing.planned_jobs += stats.planned_jobs;
    state.execution.hashing.launched_workers += stats.launched_workers;
    state.execution.hashing.joined_workers += stats.joined_workers;
    state.execution.hashing.caller_jobs += stats.caller_jobs;
  };
  try {
    allocation::RunRange(
        context, state.allocation, policy, count,
        static_cast<std::size_t>(std::max<std::uint64_t>(1, cost)),
        ArtifactHash, &plan, stop, stats, options.hash_worker_backend, &stats);
  } catch (...) {
    account();
    throw;
  }
  account();
  for (const auto& artifact : state.artifacts)
    state.artifact_views.push_back(
        {{artifact.id.data(), artifact.id_size},
         {artifact.path.data(), artifact.path.size()},
         {artifact.sha256.data(), artifact.sha256.size()}});
}
struct ProviderState {
  State& state;
  const GltfDuplicateWorkflowRequest& memory_request;
  const GltfDuplicateFileWorkflowLimits& limits;
  const GltfDuplicateFileWorkflowWorkPolicy& work;
  const geometry::ExecutionContext& context;
  std::stop_token stop;
  const FileWorkflowTestOptions& options;
  std::exception_ptr unexpected;
  bool own_failure = false;
};
std::span<const ic::GltfFileView> Provide(
    const ic::GltfFileVerifiedContent& content, void* opaque) {
  auto& provider = *static_cast<ProviderState*>(opaque);
  auto& state = provider.state;
  state.staged_primitive.state = CoverageState::kPerformed;
  state.staged_primitive.passed = true;
  state.physical_members.state = CoverageState::kPerformed;
  state.physical_members.passed = true;
  try {
    HashFiles(state, content, provider.limits, provider.work, provider.context,
              provider.stop, provider.options);
    state.outcomes.stage = Stage::kReport;
    // Conditional only: public exposure still depends on actual native commit.
    state.status = Status::kSuccess;
    Compute(state, true);
    auto conditional = Projection(state, &content.inspection, nullptr);
    conditional.publication = memory::FileReportPublication::kPublished;
    conditional.core_stage = memory::FileReportCoreStage::kComplete;
    conditional.native_stage = static_cast<std::uint64_t>(Stage::kComplete);
    PrepareFileReport(state, provider.memory_request, conditional,
                      provider.limits, provider.work, provider.context,
                      provider.stop, provider.options);
    state.conditional_report = true;
    state.provided_files[0] = {
        {state.report_path.data(), state.report_path.size()}, state.report};
    if (provider.options.prepared_report)
      provider.options.prepared_report(state.report, provider.options.state);
    return state.provided_files;
  } catch (const Failure& failure) {
    Latch(state, failure);
    provider.own_failure = true;
  } catch (const memory::ReportExtentFailure&) {
    Latch(state,
          {Status::kLimitExceeded, Stage::kReport, Code::kReportPreparation});
    provider.own_failure = true;
  } catch (const allocation::AdmissionFailure& failure) {
    Latch(state, {Admission(failure), state.outcomes.stage,
                  failure.Code() == allocation::AdmissionCode::kCanceled
                      ? Code::kStopRequested
                      : Code::kRequestBudget});
    provider.own_failure = true;
  } catch (...) {
    provider.unexpected = std::current_exception();
  }
  state.outcomes.publication = Publication::kNotAttempted;
  // Actual cancellation checkpoint remains owned by the publisher. Returning
  // an empty span permits it to observe the real stop instead of
  // CallbackFailure.
  if (provider.own_failure && state.status == Status::kCanceled &&
      provider.stop.stop_requested())
    return {};
  throw ProviderFailure{};
}
Status PublisherStatus(ic::GltfFileStatus status) {
  switch (status) {
    case ic::GltfFileStatus::kSuccess:
      return Status::kSuccess;
    case ic::GltfFileStatus::kCanceled:
      return Status::kCanceled;
    case ic::GltfFileStatus::kBudgetExceeded:
      return Status::kBudgetExceeded;
    case ic::GltfFileStatus::kLimitExceeded:
      return Status::kLimitExceeded;
    case ic::GltfFileStatus::kUnsupportedProfile:
      return Status::kUnsupported;
    case ic::GltfFileStatus::kSourceContractFailure:
    case ic::GltfFileStatus::kForeignBudget:
      return Status::kSourceContractFailure;
    case ic::GltfFileStatus::kPreservationMismatch:
      return Status::kVerificationFailure;
    case ic::GltfFileStatus::kInvalidConfiguration:
      return Status::kInvalidConfiguration;
    default:
      return Status::kDelegatedFailure;
  }
}
void PublicationFailureChecks(State& state,
                              const ic::GltfFileDiagnostic& diagnostic) {
  if (diagnostic.code == ic::GltfFileDiagnosticCode::kStagedInventory ||
      diagnostic.code == ic::GltfFileDiagnosticCode::kEntryBytes ||
      diagnostic.code == ic::GltfFileDiagnosticCode::kResourceBytes) {
    state.physical_members.state = CoverageState::kPerformed;
    state.physical_members.passed = false;
  } else if (diagnostic.code ==
             ic::GltfFileDiagnosticCode::kDelegatedInspection) {
    state.staged_primitive.state = CoverageState::kPerformed;
    state.staged_primitive.passed = false;
  }
}
void FailureReport(State& state, const GltfDuplicateWorkflowRequest& request,
                   const GltfDuplicateFileWorkflowLimits& limits,
                   const GltfDuplicateFileWorkflowWorkPolicy& work,
                   const geometry::ExecutionContext& context,
                   std::stop_token stop, const FileWorkflowTestOptions& options,
                   const ic::GltfFilePublicationResult* publication) {
  state.report_available = false;
  state.conditional_report = false;
  if (!state.memory || stop.stop_requested()) return;
  state.failure_report_attempted = true;
  try {
    PrepareFileReport(state, request, Projection(state, nullptr, publication),
                      limits, work, context, stop, options);
    state.report_available = true;
    state.failure_report_delivered = true;
  } catch (const Failure&) {
  } catch (const memory::ReportExtentFailure&) {
  } catch (const allocation::AdmissionFailure&) {
  }
}
}  // namespace
GltfDuplicateFileWorkflowResult RunGltfDuplicateFileWorkflowForTest(
    const GltfDuplicateFileWorkflowRequest& request,
    const GltfDuplicateFileWorkflowLimits& limits,
    const GltfDuplicateFileWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const FileWorkflowTestOptions& options) {
  GltfDuplicateFileWorkflowResult result;
  result.outcomes.publication = request.publication
                                    ? Publication::kNotAttempted
                                    : Publication::kNotRequested;
  std::shared_ptr<State> state;
  try {
    Validate(request, limits, work);
    if (!request.source.Asset().SharesExecutionBudget(context))
      Fail(Status::kSourceContractFailure, Stage::kSource, Code::kSourceLedger);
    if (!request.source.Asset().NewRequestedBytes())
      Fail(Status::kSourceContractFailure, Stage::kSource,
           Code::kSourceAccounting);
    if (stop.stop_requested())
      Fail(Status::kCanceled, Stage::kSource, Code::kStopRequested);
    const auto allocation =
        allocation::AllocationContext(context, stop, options.allocation_backend)
            .WithRequestLimit(static_cast<std::size_t>(
                limits.max_wrapper_new_requested_bytes));
    state = std::allocate_shared<State>(
        allocation::Allocator<State>(allocation), allocation, request.source);
    state->outcomes.publication = result.outcomes.publication;
    if (request.publication)
      state->report_path.assign(request.publication->report_path.begin(),
                                request.publication->report_path.end());
    state->outcomes.stage = Stage::kMemory;
    const auto memory_request = MemoryRequest(request);
    const auto memory_result = memory::RunRetainedGltfDuplicateWorkflow(
        Initialization(request, limits), memory_request,
        MemoryLimits(limits.memory), work.memory, context, stop,
        options.memory);
    state->memory = memory_result.record;
    state->memory_outcomes = memory_result.outcomes;
    state->status = memory_result.status;
    state->outcomes.candidate = memory_result.outcomes.candidate;
    state->diagnostic.memory = memory_result.diagnostic;
    if (memory_result.status != Status::kSuccess) {
      state->diagnostic.stage = Stage::kMemory;
      state->diagnostic.code = Code::kMemory;
      Compute(*state, request.publication != nullptr);
      if (request.publication)
        FailureReport(*state, memory_request, limits, work, context, stop,
                      options, nullptr);
      else
        state->report_available = state->memory.has_value();
    } else if (!request.publication) {
      state->outcomes.stage = Stage::kComplete;
      Compute(*state, false);
      state->report_available = true;
    } else {
      ProviderState provider{*state,  memory_request, limits,  work,
                             context, stop,           options, {}};
      const ic::GltfFilePublicationOptions publication_options{
          request.publication->supplemental_files,
          {},
          {},
          {Provide, &provider}};
      state->outcomes.stage = Stage::kPublication;
      result.publication = ic::PublishGltfTopologyBundle(
          {state->source, *state->memory->OutputInspection()},
          request.publication->destination, publication_options,
          limits.publication, work.publication, stop);
      // Real publisher has completed all workers and attempted its actual
      // cleanup before an owning exception resumes propagation.
      if (provider.unexpected) std::rethrow_exception(provider.unexpected);
      if (result.publication->outcome ==
          ic::GltfFilePublicationOutcome::kPublished) {
        state->published = result.publication->bundle;
        state->outcomes.publication = Publication::kPublished;
        state->outcomes.stage = Stage::kComplete;
        state->status = Status::kSuccess;
        state->report_available = true;
        // All prepared facts/bytes already finalized; no fallible work here.
      } else {
        state->outcomes.publication =
            result.publication->outcome ==
                    ic::GltfFilePublicationOutcome::kCanceled
                ? Publication::kCanceled
                : Publication::kFailed;
        state->diagnostic.publisher = result.publication->diagnostic;
        if (!provider.own_failure) {
          state->status =
              PublisherStatus(result.publication->diagnostic.status);
          state->outcomes.stage = result.publication->diagnostic.stage ==
                                          ic::GltfFileStage::kSupplemental
                                      ? Stage::kReport
                                      : Stage::kPublication;
          state->diagnostic.stage = state->outcomes.stage;
          state->diagnostic.code = Code::kPublication;
        }
        PublicationFailureChecks(*state, result.publication->diagnostic);
        Compute(*state, true);
        FailureReport(*state, memory_request, limits, work, context, stop,
                      options, &*result.publication);
      }
    }
  } catch (const Failure& failure) {
    result.status = failure.status;
    result.diagnostic.stage = failure.stage;
    result.diagnostic.code = failure.code;
    result.outcomes.stage = failure.stage;
    if (state) {
      Latch(*state, failure);
      Compute(*state, request.publication != nullptr);
    }
  } catch (const allocation::AdmissionFailure& failure) {
    const auto status = Admission(failure);
    const auto stage = state ? state->outcomes.stage : Stage::kSource;
    const auto code = status == Status::kCanceled ? Code::kStopRequested
                                                  : Code::kRequestBudget;
    result.status = status;
    result.diagnostic.stage = stage;
    result.diagnostic.code = code;
    result.outcomes.stage = stage;
    if (state) {
      Latch(*state, {status, stage, code});
      Compute(*state, request.publication != nullptr);
    }
  }
  if (state) {
    result.status = state->status;
    result.outcomes = state->outcomes;
    result.diagnostic = state->diagnostic;
    result.execution = state->execution;
    result.record = Access::Make(state);
  } else if (result.status == Status::kCanceled) {
    result.outcomes.execution = GltfWorkflowExecutionOutcome::kCanceled;
    result.outcomes.exit = GltfWorkflowExitMeaning::kCanceled;
  }
  return result;
}
}  // namespace meshvale::repair::gltf_file_workflow_detail

namespace meshvale::repair {
GltfDuplicateFileWorkflowRecord::GltfDuplicateFileWorkflowRecord(
    std::shared_ptr<const gltf_file_workflow_detail::State> state) noexcept
    : state_(std::move(state)) {}
GltfDuplicateFileWorkflowRecord::GltfDuplicateFileWorkflowRecord(
    const GltfDuplicateFileWorkflowRecord&) noexcept = default;
GltfDuplicateFileWorkflowRecord& GltfDuplicateFileWorkflowRecord::operator=(
    const GltfDuplicateFileWorkflowRecord&) noexcept = default;
GltfDuplicateFileWorkflowRecord::~GltfDuplicateFileWorkflowRecord() noexcept =
    default;
const interchange::GltfFileAsset& GltfDuplicateFileWorkflowRecord::Source()
    const noexcept {
  return state_->source;
}
const std::optional<GltfDuplicateWorkflowRecord>&
GltfDuplicateFileWorkflowRecord::Memory() const noexcept {
  return state_->memory;
}
const std::optional<interchange::GltfPublishedBundle>&
GltfDuplicateFileWorkflowRecord::Published() const noexcept {
  return state_->published;
}
GltfFileWorkflowOutcomes GltfDuplicateFileWorkflowRecord::Outcomes()
    const noexcept {
  return state_->outcomes;
}
std::span<const std::byte> GltfDuplicateFileWorkflowRecord::ReportJson()
    const noexcept {
  if (!state_->report_available) return {};
  if (state_->outcomes.publication ==
      GltfFileWorkflowPublicationOutcome::kNotRequested)
    return state_->memory ? state_->memory->ReportJson()
                          : std::span<const std::byte>{};
  return state_->report;
}
std::optional<std::string_view>
GltfDuplicateFileWorkflowRecord::PublishedReportPath() const noexcept {
  if (state_->outcomes.publication !=
      GltfFileWorkflowPublicationOutcome::kPublished)
    return {};
  return std::string_view(state_->report_path.data(),
                          state_->report_path.size());
}
GltfFileWorkflowRequestedBytes
GltfDuplicateFileWorkflowRecord::NewRequestedBytes() const noexcept {
  return state_->Bytes();
}
GltfFileWorkflowByteExecution GltfDuplicateFileWorkflowRecord::Execution()
    const noexcept {
  return state_->execution;
}
GltfDuplicateFileWorkflowResult RunGltfDuplicateFileWorkflow(
    const GltfDuplicateFileWorkflowRequest& request,
    const GltfDuplicateFileWorkflowLimits& limits,
    const GltfDuplicateFileWorkflowWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop) {
  return gltf_file_workflow_detail::RunGltfDuplicateFileWorkflowForTest(
      request, limits, work, context, stop, {});
}
}  // namespace meshvale::repair
