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

namespace {
namespace geo = meshvale::geometry;
namespace repair = meshvale::repair;
constexpr std::uint64_t kCountLimit = 16;
constexpr std::uint64_t kRequestBudgetBytes = 1024 * 1024;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kPartitionBytes = 256;
constexpr std::uint64_t kCancellationBlockScalars = 16;
constexpr std::uint64_t kMaximumJobs = 16;
constexpr std::size_t kWorkerBudget = 2;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
geo::MeshMaterializationLimits Limits() {
  return {kCountLimit, kCountLimit, kCountLimit,        kCountLimit,
          kCountLimit, kCountLimit, kCountLimit,        kCountLimit,
          kCountLimit, kCountLimit, kRequestBudgetBytes};
}
}  // namespace
int main() {
  try {
    geo::ExecutionOptions options;
    options.worker_budget = kWorkerBudget;
    options.tracked_payload_budget_bytes = kRequestBudgetBytes;
    const geo::ExecutionContext context(options);
    const geo::MeshMaterializationWorkPolicy work{
        kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
        kMaximumJobs};
    const std::array<geo::index_t, 1> offsets{0};
    geo::MeshMaterializationInput input;
    input.positions = std::span<const double>{};
    input.topology = geo::PolygonTopologyInput{offsets, {}};
    const auto captured = geo::MaterializeMesh(input, Limits(), work, context);
    Require(captured.status == geo::MaterializationStatus::kSuccess &&
                captured.mesh,
            "Empty structural source must remain available on every profile");
    const auto baseline = context.ActiveTrackedPayload();
    const repair::MaterializedDuplicateLimits limits{
        kCountLimit, kCountLimit,         kCountLimit,
        kCountLimit, kRequestBudgetBytes, {kCountLimit, kRequestBudgetBytes},
        Limits()};
    {
      const auto result = repair::RemoveMaterializedDuplicateFaces(
          *captured.mesh, {}, {}, limits, work);
      Require(
          result.status == repair::MaterializedDuplicateStatus::kSuccess &&
              result.repair &&
              result.repair->Outcome() == repair::CandidateOutcome::unchanged &&
              result.repair->FaceMap().empty() &&
              result.repair->CornerMap().empty() &&
              result.repair->SourceFaces().empty() &&
              result.repair->SourceCorners().empty() &&
              result.repair->SourceInspection().IsStorageValid() &&
              result.repair->CandidateInspection().IsStorageValid(),
          "Empty direct operation must publish unchanged structural storage");
      const auto view = result.repair->Candidate().View();
      Require(view.positions.empty() && view.corner_vertices.empty() &&
                  view.attributes.empty() && view.face_offsets.size() == 1 &&
                  view.face_offsets[0] == 0,
              "Empty selected candidate has incorrect structural shape");
    }
    const std::array<repair::DuplicateTarget, 1> targets{{{0, 1}}};
    const auto rejected = repair::RemoveMaterializedDuplicateFaces(
        *captured.mesh, targets, {}, limits, work);
    Require(rejected.status == repair::MaterializedDuplicateStatus::kRejected &&
                !rejected.repair &&
                rejected.diagnostic.code ==
                    repair::MaterializedDuplicateDiagnosticCode::kTargetRange &&
                rejected.diagnostic.target == 0 &&
                context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "Empty range rejection must preserve source charges and publish "
            "nothing");
    std::cout << "Empty charged Repair and range rejection passed\n";
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
