// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "gltf_deformation_staged_fixture.h"
#include "gltf_file_workflow_test_internal.h"
#include "materialized_duplicate_workers.h"

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace repair = meshvale::repair;
namespace test = repair::test_deformation_staged;
namespace base = repair::test_file_workflow;
namespace own = repair::materialized_duplicates_detail;
namespace fs = std::filesystem;
using test::Require;
enum class Action {
  kClean,
  kCorrupt,
  kCollision,
  kFileReportStop,
  kReportExtent
};
constexpr std::uint64_t kReportPartitionBytes = 256;
constexpr std::uint64_t kReportCheckpointBytes = 64;
constexpr std::uint64_t kReportJobs = 3;
constexpr std::size_t kFaultOffset = 7;
constexpr std::size_t kWorkerCapacity = 3;
constexpr std::uint64_t kPartialPlanUnits = 24;
constexpr std::uint64_t kPartialPlannedJobs = 48;

template <class T, class Equal>
bool SameOptional(const std::optional<T>& a, const std::optional<T>& b,
                  Equal equal) {
  return a.has_value() == b.has_value() && (!a || equal(*a, *b));
}
bool SameSample(const ic::GltfWeightSumSample& a,
                const ic::GltfWeightSumSample& b) {
  const auto fields = [](const auto& x) {
    return std::tie(x.mesh, x.primitive, x.vertex, x.set_count,
                    x.nonzero_weights, x.sum_bits, x.deviation_bits,
                    x.epsilon_bits, x.advisory, x.all_zero);
  };
  return fields(a) == fields(b);
}
bool SameRefusal(const ic::GltfQuaternionSourceRefusal& a,
                 const ic::GltfQuaternionSourceRefusal& b) {
  const auto fields = [](const auto& x) {
    return std::tie(x.role, x.encoding, x.node, x.animation, x.sampler, x.key,
                    x.accessor, x.row, x.reason);
  };
  return fields(a) == fields(b);
}
bool SameReturnedFacts(const ic::GltfDeformationObservation& a,
                       const ic::GltfDeformationObservation& b) {
  const auto counts = [](const auto& x) {
    return std::tie(x.node_rows, x.mesh_users, x.skin_rows, x.joint_references,
                    x.influence_sets, x.lane_checks, x.morph_targets,
                    x.morph_channels, x.animation_channels, x.distinct_samplers,
                    x.keys_checked, x.scalar_checks, x.sparse_writes,
                    x.relationship_steps, x.decoded_bytes_checked);
  };
  const auto quaternions = [](const auto& x, const auto& y) {
    return std::tie(x.policy, x.complete, x.node_values_checked,
                    x.animation_values_checked, x.tangent_rows_checked,
                    x.rows_admitted) ==
               std::tie(y.policy, y.complete, y.node_values_checked,
                        y.animation_values_checked, y.tangent_rows_checked,
                        y.rows_admitted) &&
           SameOptional(x.first_refusal, y.first_refusal, SameRefusal);
  };
  const auto weights = [](const auto& x, const auto& y) {
    return std::tie(x.scope, x.complete, x.floating_rows_expected,
                    x.rows_evaluated, x.advisory_rows, x.all_zero_rows) ==
               std::tie(y.scope, y.complete, y.floating_rows_expected,
                        y.rows_evaluated, y.advisory_rows, y.all_zero_rows) &&
           SameOptional(x.first_evaluated, y.first_evaluated, SameSample) &&
           SameOptional(x.first_advisory, y.first_advisory, SameSample);
  };
  return a.status == b.status && a.phase == b.phase &&
         a.complete == b.complete && counts(a.counts) == counts(b.counts) &&
         SameOptional(a.quaternions, b.quaternions, quaternions) &&
         SameOptional(a.weight_sums, b.weight_sums, weights);
}

struct Fixture {
  fs::path root;
  fs::path destination;
  fs::path entry;
  std::vector<std::byte> wire;
  const geo::ExecutionContext& context;
  std::optional<ic::GltfFileAsset> source;
  Fixture(const fs::path& fixtures, const fs::path& area, bool glb,
          const geo::ExecutionContext& execution)
      : root(area),
        destination(area / "published"),
        entry(area / "input" / (glb ? "source.glb" : "source.gltf")),
        wire(test::Wire(fixtures, "stage-rich", glb)),
        context(execution) {
    Require(fs::create_directory(root), "absent case directory");
    base::Write(entry, wire);
    base::Write(entry.parent_path() / test::kOpaquePath,
                base::Bytes(test::kOpaqueBytes));
    const std::array opaque{test::kOpaquePath};
    ic::GltfFileReadOptions options;
    options.extensions = ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory;
    options.opaque_source_files = opaque;
    options.read_request = {
        ic::GltfAdmissionProfile::kRetainedCoreDeformationV1, test::kPolicy};
    const auto work = base::Work(true).publication;
    auto captured = ic::ReadGltfFileAsset(
        entry, options,
        {base::FileLimits(),
         base::ReadLimits(),
         {base::kPhysicalBytes, base::kCount, base::kStageBytes}},
        {work.files, work.reload, work.container}, context);
    Require(captured.asset.has_value(),
            "genuine new-profile rich file capture");
    source = std::move(captured.asset);
  }
  void CheckSource() const {
    Require(base::Read(entry) == wire &&
                base::Text(base::Read(entry.parent_path() /
                                      test::kOpaquePath)) == test::kOpaqueBytes,
            "all source physical bytes protected");
    for (const auto& file : source->Files())
      Require(base::Read(entry.parent_path() / file.path) ==
                  std::vector<std::byte>(file.bytes.begin(), file.bytes.end()),
              "full retained source inventory matches physical bytes");
  }
};
struct Witness {
  struct Slot {
    own::NativeWorkerRecord wrapped;
    own::NativeWorkerRecord* original = nullptr;
    Witness* witness = nullptr;
  };
  const Fixture& fixture;
  Action action;
  std::stop_source stop;
  std::array<Slot, kWorkerCapacity> slots;
  std::size_t launched = 0, joined = 0, prepared = 0;
  std::atomic<std::size_t> entered{0}, exited{0};
  std::atomic<bool> entered_after_staged{true};
  std::optional<ic::GltfDeformationObservation> read, reload, verified;
  std::vector<std::byte> conditional;
  bool corrupted = false;
  static void Admitted(const ic::GltfFileVerifiedContent& content,
                       std::uint64_t, void* opaque) {
    auto& self = *static_cast<Witness*>(opaque);
    const auto* read =
        content.inspection.ReloadedAsset().DeformationReadFacts();
    const auto* reload =
        content.inspection.ReloadedExtraction().DeformationFacts();
    Require(read && reload && content.staged_deformation,
            "real borrowed staged returns exist before FILE hash/report");
    self.read = *read;
    self.reload = *reload;
    self.verified = *content.staged_deformation;
    Require(
        self.read->complete && self.reload->complete && self.verified->complete,
        "genuine stage completion precedes fallible file report");
  }
  static void Prepared(std::span<const std::byte> bytes, void* opaque) {
    auto& self = *static_cast<Witness*>(opaque);
    ++self.prepared;
    Require(!bytes.empty() && !fs::exists(self.fixture.destination),
            "real conditional report before actual commit");
    self.conditional.assign(bytes.begin(), bytes.end());
    if (self.action == Action::kCollision) {
      Require(fs::create_directory(self.fixture.destination),
              "caller occupies no-replace destination at real barrier");
      base::Write(self.fixture.destination / "sentinel.txt",
                  base::Bytes(test::kSentinel));
    } else if (self.action == Action::kCorrupt) {
      fs::path stage;
      for (const auto& entry : fs::directory_iterator(self.fixture.root)) {
        if (entry.path() == self.fixture.entry.parent_path()) continue;
        Require(stage.empty() && entry.is_directory(),
                "sole real native stage");
        stage = entry.path();
      }
      Require(!stage.empty(), "real exclusive stage exists");
      auto original = base::Read(stage / test::kOpaquePath);
      Require(base::Text(original) == test::kOpaqueBytes &&
                  original.size() > kFaultOffset,
              "literal opaque member physically exists before corruption");
      original[kFaultOffset] ^= std::byte{1};
      base::Write(stage / test::kOpaquePath, original);
      self.corrupted = true;
    }
  }
  static void Entry(own::NativeWorkerRecord& wrapped) noexcept {
    auto& slot = *static_cast<Slot*>(wrapped.state);
    auto& self = *slot.witness;
    ++self.entered;
    if (!self.read || !self.reload || !self.verified)
      self.entered_after_staged = false;
    self.stop.request_stop();
    slot.original->entry(*slot.original);
    ++self.exited;
  }
  static std::uintptr_t Launch(own::NativeWorkerRecord* record, void* opaque) {
    auto& self = *static_cast<Witness*>(opaque);
    Require(self.launched < self.slots.size(),
            "bounded actual FILE worker slots");
    auto& slot = self.slots[self.launched];
    slot.original = record;
    slot.witness = &self;
    slot.wrapped = {Entry, &slot, record->ordinal};
    const auto original = own::DefaultNativeWorkerBackend();
    auto handle = original.launch(&slot.wrapped, original.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<Witness*>(opaque);
    const auto original = own::DefaultNativeWorkerBackend();
    original.join(handle, original.state);
    ++self.joined;
  }
};

void FileCase(const fs::path& fixtures, const fs::path& root, bool glb,
              Action action, std::string_view label,
              const geo::ExecutionContext& context) {
  const auto before = context.ActiveTrackedPayload();
  {
    Fixture fixture(fixtures, root / std::string(label), glb, context);
    Witness witness{fixture, action};
    repair::gltf_file_workflow_detail::FileWorkflowTestOptions options;
    options.after_hash_admission = Witness::Admitted;
    options.prepared_report = Witness::Prepared;
    options.state = &witness;
    if (action == Action::kFileReportStop)
      options.report_worker_backend = {Witness::Launch, Witness::Join,
                                       &witness};
    const repair::GltfDuplicateFilePublication publication{
        fixture.destination,
        test::kReportPath,
        {},
        {test::kPolicy, test::kPolicy, test::kPolicy}};
    repair::GltfDuplicateFileWorkflowRequest request{
        *fixture.source,
        label,
        0,
        test::Extraction(test::kPolicy, true),
        base::kTargets,
        {test::kDeclarations},
        &publication};
    request.deformation = test::Policies();
    auto limits = test::Limits();
    if (action == Action::kReportExtent) limits.max_report_bytes = 1;
    auto work = base::Work(true);
    work.report = {1, kReportPartitionBytes, kReportCheckpointBytes,
                   kReportJobs};
    const auto result =
        repair::gltf_file_workflow_detail::RunGltfDuplicateFileWorkflowForTest(
            request, limits, work, context, witness.stop.get_token(), options);
    std::cout << label << " returned status=" << static_cast<int>(result.status)
              << " stage=" << static_cast<int>(result.diagnostic.stage)
              << " code=" << static_cast<int>(result.diagnostic.code)
              << " publication=" << result.publication.has_value()
              << " record=" << result.record.has_value() << std::endl;
    if (result.record && !result.record->ReportJson().empty())
      base::Write(fixture.root / "raw-returned-report.json",
                  result.record->ReportJson());
    Require(result.deformation.has_value(),
            "actual result inline deformation facts");
    test::CheckStaged(*result.deformation);
    Require(witness.read && witness.reload && witness.verified &&
                SameReturnedFacts(*result.deformation->publication_read,
                                  *witness.read) &&
                SameReturnedFacts(*result.deformation->publication_reload,
                                  *witness.reload) &&
                SameReturnedFacts(*result.deformation->publication_verification,
                                  *witness.verified),
            "all staged counts and exact quaternion/weight summaries survive "
            "the real file phase");
    Require(result.record && result.record->Memory() &&
                result.record->Memory()->Repair() &&
                result.outcomes.candidate == repair::CandidateOutcome::accepted,
            "completed memory/candidate/maps survive later file phase");
    Require(result.record->Memory()->Repair()->FaceMap().size() == 2,
            "both original duplicate faces retain correspondence");
    if (action == Action::kClean) {
      Require(result.status == repair::GltfWorkflowStatus::kSuccess &&
                  result.record->Published() &&
                  result.outcomes.profile ==
                      repair::GltfWorkflowProfileOutcome::kPassed,
              "clean new-profile positive reaches real commit");
      Require(base::Read(fixture.destination / test::kReportPath) ==
                  std::vector<std::byte>(result.record->ReportJson().begin(),
                                         result.record->ReportJson().end()),
              "actual published native bytes are authoritative");
    } else {
      Require(!result.record->Published() && result.publication &&
                  !result.publication->bundle,
              "later failures have no published owner");
      Require(
          result.publication->cleanup == ic::GltfFileCleanupOutcome::kRemoved,
          "real native stage removed after failure");
      Require(
          result.record->ReportJson().empty() ||
              !std::ranges::equal(result.record->ReportJson(),
                                  witness.conditional),
          "private conditional success receipt never exposed after failure");
      if (action == Action::kCorrupt) {
        Require(witness.corrupted && witness.prepared == 1 &&
                    result.outcomes.profile ==
                        repair::GltfWorkflowProfileOutcome::kFailed &&
                    result.publication->diagnostic.code ==
                        ic::GltfFileDiagnosticCode::kResourceBytes &&
                    result.publication->diagnostic.byte_offset == kFaultOffset,
                "actual late physical mismatch makes inherited profile failed");
      } else if (action == Action::kCollision) {
        Require(witness.prepared == 1 &&
                    result.outcomes.profile ==
                        repair::GltfWorkflowProfileOutcome::kPassed &&
                    result.outcomes.exit ==
                        repair::GltfWorkflowExitMeaning::kExecutionFailure &&
                    result.publication->diagnostic.code ==
                        ic::GltfFileDiagnosticCode::kDestinationExists &&
                    base::Text(base::Read(fixture.destination /
                                          "sentinel.txt")) == test::kSentinel,
                "late no-replace failure retains passed preservation and "
                "caller sentinel");
      } else if (action == Action::kFileReportStop) {
        Require(result.status == repair::GltfWorkflowStatus::kCanceled &&
                    result.diagnostic.stage ==
                        repair::GltfFileWorkflowStage::kReport &&
                    witness.entered_after_staged && witness.launched > 0 &&
                    witness.entered == witness.launched &&
                    witness.exited == witness.launched &&
                    witness.joined == witness.launched &&
                    result.record->ReportJson().empty() &&
                    witness.prepared == 0,
                "real FILE report worker stop after staged copies retains "
                "facts without receipt");
      } else {
        Require(result.status != repair::GltfWorkflowStatus::kSuccess &&
                    witness.prepared == 0 &&
                    result.diagnostic.stage ==
                        repair::GltfFileWorkflowStage::kReport,
                "bounded real FILE report extent failure after stage copies");
      }
      if (action != Action::kCollision)
        Require(!fs::exists(fixture.destination),
                "no final destination on failed publication");
    }
    fixture.CheckSource();
    if (!result.record->ReportJson().empty())
      base::Write(fixture.root / "returned-report.json",
                  result.record->ReportJson());
    Require(context.ActiveWorkers() == 0, "every started native worker joined");
    std::cout << label << " status=" << static_cast<int>(result.status)
              << " profile=" << static_cast<int>(result.outcomes.profile)
              << " file-report-workers=" << witness.entered << '/'
              << witness.joined
              << " report-bytes=" << result.record->ReportJson().size() << '\n';
  }
  Require(
      context.ActiveTrackedPayload() == before && context.ActiveWorkers() == 0,
      "case owners released on original execution ledger");
}

void Partial(const fs::path& fixtures, const fs::path& root,
             std::string_view name, const geo::ExecutionContext& context) {
  const auto wire = test::Wire(fixtures, name, false);
  auto policy = test::kPolicy;
  const auto serial =
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
  policy.work.parallel_min_scalars = policy.work.parallel_min_bytes = serial;
  if (name == "weight-partial") {
    policy.work = {1, 1, 1, 1, 1, 1, kPartialPlanUnits, 1};
  }
  repair::GltfDuplicateWorkflowRequest request{
      wire, {}, name, 0, test::Extraction(policy), {}, {}};
  request.deformation = test::Policies(policy);
  const auto result = repair::RunGltfDuplicateWorkflow(
      request, test::MemoryLimits(), base::Work(true).memory, context);
  std::cout << name << " returned status=" << static_cast<int>(result.status)
            << " stage=" << static_cast<int>(result.diagnostic.stage)
            << " record=" << result.record.has_value() << std::endl;
  if (result.record && !result.record->ReportJson().empty())
    base::Write(root / (std::string(name) + "-returned-report.json"),
                result.record->ReportJson());
  if (result.deformation && result.deformation->source &&
      result.deformation->source->weight_sums) {
    const auto& facts = *result.deformation->source;
    const auto& sums = *facts.weight_sums;
    std::cout << "raw weight expected=" << sums.floating_rows_expected
              << " evaluated=" << sums.rows_evaluated
              << " planned=" << facts.execution.planned_jobs
              << " launched=" << facts.execution.launched_workers
              << " joined=" << facts.execution.joined_workers << std::endl;
  }
  Require(
      result.status != repair::GltfWorkflowStatus::kSuccess &&
          result.deformation && !result.retained_repair &&
          (!result.record ||
           (!result.record->ExtractedPrimitive() && !result.record->Source() &&
            !result.record->Repair() && !result.record->Output())),
      "actual failed partial return has no successful extraction/repair owner");
  const auto& facts = *result.deformation;
  if (name == "node-partial") {
    Require(facts.source_read && !facts.source && !facts.materialization,
            "failed read called once, later roles null");
    Require(!result.record || !result.record->ReadAsset(),
            "failure report owner never substitutes for a failed source asset");
    const auto& q = facts.source_read->quaternions;
    Require(q && !q->complete && q->node_values_checked == 2 &&
                q->rows_admitted == 1 && q->first_refusal &&
                q->first_refusal->node == 1 &&
                q->first_refusal->reason ==
                    ic::GltfQuaternionSourceRefusalReason::kNonunit,
            "nonzero partial read quaternion with exact source node1 refusal");
  } else {
    Require(facts.source_read && facts.source && !facts.materialization &&
                !facts.writing,
            "failed extraction retains actual read and no later role");
    if (name == "payload-partial") {
      const auto& q = facts.source->quaternions;
      Require(q && !q->complete && q->animation_values_checked == 2 &&
                  q->rows_admitted == 1 && q->first_refusal &&
                  q->first_refusal->accessor == 3 && q->first_refusal->row == 1,
              "nonzero partial payload quaternion with exact accessor3 row1 "
              "refusal");
    } else {
      const auto& sums = facts.source->weight_sums;
      Require(sums && !sums->complete && sums->floating_rows_expected == 3 &&
                  sums->rows_evaluated == 2 && sums->all_zero_rows == 1 &&
                  sums->advisory_rows == 2 && sums->first_evaluated &&
                  sums->first_evaluated->vertex == 0 && sums->first_advisory &&
                  sums->first_advisory->vertex == 0 &&
                  sums->first_advisory->sum_bits == 0 &&
                  facts.source->location.vertex == 1 &&
                  facts.source->execution.planned_jobs == kPartialPlannedJobs &&
                  facts.source->execution.launched_workers > 0 &&
                  facts.source->execution.launched_workers ==
                      facts.source->execution.joined_workers,
              "actual three-row partition plan preserves noncontiguous "
              "floating2/3 facts");
    }
  }
  Require(context.ActiveWorkers() == 0, "failed partial workers joined");
  std::cout << name << " actual partial return PASS\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2 || argc == 3,
            "fixture directory and optional absent output root");
    const auto fixtures = fs::canonical(argv[1]);
    const auto root = argc == 3
                          ? fs::absolute(argv[2])
                          : fs::temp_directory_path() /
                                ("meshvale-deformation-staged-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()));
    Require(!fs::exists(root) && fs::is_directory(root.parent_path()) &&
                fs::create_directory(root),
            "fresh owned output root with existing parent");
    const geo::ExecutionContext context(
        {base::kWorkers, 1, base::kLedgerBytes});
    for (const bool glb : {false, true}) {
      const auto prefix = glb ? "glb-" : "json-";
      FileCase(fixtures, root, glb, Action::kClean,
               std::string(prefix) + "clean", context);
      FileCase(fixtures, root, glb, Action::kCorrupt,
               std::string(prefix) + "physical-false", context);
      FileCase(fixtures, root, glb, Action::kCollision,
               std::string(prefix) + "collision", context);
      FileCase(fixtures, root, glb, Action::kFileReportStop,
               std::string(prefix) + "file-report-stop", context);
      FileCase(fixtures, root, glb, Action::kReportExtent,
               std::string(prefix) + "file-report-extent", context);
    }
    for (const auto name :
         {"node-partial", "payload-partial", "weight-partial"}) {
      Partial(fixtures, root, name, context);
      Require(
          context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "partial owner and job cleanup on original ledger");
    }
    std::cout << "staged controls physical=10 partial=3 PASS workers=0 "
                 "payload=0 output="
              << root.generic_string() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "staged controls: " << error.what() << '\n';
    return 1;
  }
}
