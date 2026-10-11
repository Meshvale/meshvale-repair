// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {
namespace geo = meshvale::geometry;
namespace repair = meshvale::repair;
constexpr std::string_view kEmptySourceOption = "--empty-source";
constexpr std::uint64_t kCountLimit = 1024;
constexpr std::uint64_t kRequestBudgetBytes = 8 * 1024 * 1024;
constexpr std::uint64_t kComparisonStepLimit = 1024;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCancellationBlockScalars = 32;
constexpr std::uint64_t kMaximumJobs = 32;
constexpr std::size_t kWorkerBudget = 2;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool empty =
        argc == 2 && std::string_view(argv[1]) == kEmptySourceOption;
    Require(argc == 1 || empty, "Expected no option or --empty-source");
    const geo::MeshMaterializationLimits capture_limits{
        kCountLimit, kCountLimit, kCountLimit,        kCountLimit,
        kCountLimit, kCountLimit, kCountLimit,        kCountLimit,
        kCountLimit, kCountLimit, kRequestBudgetBytes};
    const geo::MeshMaterializationWorkPolicy work{
        kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
        kMaximumJobs};
    geo::ExecutionOptions options;
    options.worker_budget = kWorkerBudget;
    options.tracked_payload_budget_bytes = kRequestBudgetBytes;
    const geo::ExecutionContext context(options);
    const std::array<double, 12> positions{0, 0, 0, 1, 0, 0, 0, 1, 0, 9, 9, 9};
    const std::array<geo::index_t, 3> offsets{0, 3, 6};
    const std::array<geo::index_t, 6> corners{0, 1, 2, 2, 0, 1};
    const std::array<geo::index_t, 1> empty_offsets{0};
    geo::MeshMaterializationInput input;
    input.positions =
        empty ? std::span<const double>{} : std::span<const double>(positions);
    input.topology = empty ? geo::PolygonTopologyInput{empty_offsets, {}}
                           : geo::PolygonTopologyInput{offsets, corners};
    auto captured = geo::MaterializeMesh(input, capture_limits, work, context);
    Require(captured.status == geo::MaterializationStatus::kSuccess &&
                captured.mesh,
            "Public Geometry capture failed");
    const repair::MaterializedDuplicateLimits limits{
        kCountLimit,         kCountLimit,
        kCountLimit,         kComparisonStepLimit,
        kRequestBudgetBytes, {kCountLimit, kRequestBudgetBytes},
        capture_limits};
    const std::array<repair::DuplicateTarget, 1> targets{{{0, 1}}};
    auto result = repair::RemoveMaterializedDuplicateFaces(
        *captured.mesh,
        empty ? std::span<const repair::DuplicateTarget>{} : targets, {},
        limits, work);
    Require(result.status == repair::MaterializedDuplicateStatus::kSuccess &&
                result.repair,
            "Direct charged Repair failed");
    auto retained = *result.repair;
    const auto* shared_positions = captured.mesh->View().positions.data();
    captured.mesh.reset();
    result.repair.reset();
    const auto candidate = retained.Candidate().View();
    Require(
        retained.SourceInspection().IsStorageValid() &&
            retained.CandidateInspection().IsStorageValid() &&
            candidate.positions.data() == shared_positions &&
            context.ActiveWorkers() == 0,
        "Copied immutable owner lost source/candidate storage or worker joins");
    if (empty) {
      Require(retained.Outcome() == repair::CandidateOutcome::unchanged &&
                  retained.FaceMap().empty() && retained.CornerMap().empty() &&
                  candidate.face_offsets.size() == 1 &&
                  candidate.face_offsets[0] == 0,
              "Empty operation changed storage");
    } else {
      const std::array<geo::index_t, 6> expected_corners{0, 1, 2, 2, 0, 1};
      Require(retained.Outcome() == repair::CandidateOutcome::accepted &&
                  retained.FaceMap().size() == 2 &&
                  retained.FaceMap()[0] == 0 && retained.FaceMap()[1] == 0 &&
                  retained.CornerMap().size() == 6,
              "Many-to-one face/corner correspondence is incomplete");
      for (std::size_t i = 0; i < expected_corners.size(); ++i)
        Require(retained.CornerMap()[i] == expected_corners[i],
                "Cyclic corner correspondence is incorrect");
    }
    std::cout << "Installed direct charged Repair consumer passed\n";
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
