// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_OFF_WORKFLOW_FIXTURE_H_
#define MESHVALE_REPAIR_TESTS_OFF_WORKFLOW_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_workflow.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace meshvale::repair::test_off_workflow {
namespace geo = geometry;
namespace ic = interchange;
namespace fs = std::filesystem;
inline constexpr std::uint64_t kMiB = 1024 * 1024;
inline constexpr std::uint64_t kLedger = 256 * kMiB;
inline constexpr std::uint64_t kCount = 65536;
inline constexpr std::string_view kSource =
    "# Original indexed polygons; retain these exact bytes.\nOFF\n8 3 17\n"
    "-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    "4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n";
inline constexpr std::string_view kEmpty = "# real empty source\nOFF\n0 0 17\n";
inline constexpr std::string_view kPoint = "OFF\n1 0 17\n-0 2 3\n";
inline constexpr std::string_view kEntry = "source.off";
inline constexpr std::string_view kReceipt = "report.json";
inline constexpr std::array<std::uint64_t, 24> kPositionBits{
    0x8000000000000000,
    0,
    0,
    0x3ff0000000000000,
    0,
    0,
    0x3ff0000000000000,
    0x3ff0000000000000,
    0,
    0,
    0x3ff0000000000000,
    0,
    0x4000000000000000,
    0,
    0,
    0x4008000000000000,
    0,
    0,
    0x4000000000000000,
    0x3ff0000000000000,
    0,
    0x4058c00000000000,
    0x8000000000000000,
    0x4014000000000000};
inline constexpr std::array<geo::index_t, 4> kOffsets{0, 4, 8, 11};
inline constexpr std::array<geo::index_t, 11> kCorners{0, 1, 2, 3, 2, 3,
                                                       0, 1, 4, 5, 6};
inline constexpr std::array<geo::index_t, 3> kChangedOffsets{0, 4, 7};
inline constexpr std::array<geo::index_t, 7> kChangedCorners{0, 1, 2, 3,
                                                             4, 5, 6};
inline constexpr std::array<geo::index_t, 3> kFaceMap{0, 0, 1};
inline constexpr std::array<geo::index_t, 2> kSourceFaces{0, 2};
inline constexpr std::array<geo::index_t, 7> kSourceCorners{0, 1, 2, 3,
                                                            8, 9, 10};
inline constexpr std::array<DuplicateTarget, 1> kTargets{{{0, 1}}};
inline void Require(
    bool value, std::string_view message,
    std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
inline std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
inline std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
inline geo::MeshMaterializationLimits MeshLimits() {
  return {kCount, kCount, kCount, kCount, 8,       kCount,
          kCount, kCount, 4096,   4096,   8 * kMiB};
}
inline ic::OffReadLimits ReadLimits() {
  return {kMiB, kCount, kCount,   kCount,    kCount,       512,
          128,  256,    8 * kMiB, 24 * kMiB, MeshLimits(), {kCount, 8 * kMiB}};
}
inline ic::OffWriteLimits WriteLimits() {
  return {kMiB, kCount, kCount,   kCount,    kCount,
          512,  128,    8 * kMiB, 16 * kMiB, {kCount, 8 * kMiB}};
}
inline ic::OffFileLimits FileLimits() {
  return {kMiB, kMiB, 2 * kMiB, 256, 256, 4096, 64 * kMiB};
}
inline std::uint64_t Threshold(bool serial) {
  return serial ? static_cast<std::uint64_t>(
                      std::numeric_limits<std::ptrdiff_t>::max())
                : 1;
}
inline geo::MeshMaterializationWorkPolicy MeshWork(bool serial = false) {
  return {Threshold(serial), 64, 8, 64};
}
inline ic::OffWorkPolicy CodecWork(bool serial = false) {
  return {16, 4, 1, MeshWork(serial)};
}
inline ic::OffFileWorkPolicy FileWork(bool serial = false) {
  return {64, Threshold(serial), 128, 16, 64};
}
inline OffDuplicateFileWorkflowLimits Limits() {
  return {64 * kMiB,
          128,
          kMiB,
          kMiB,
          2 * kMiB,
          kCount,
          kCount,
          kCount,
          {kCount, 1, 1, kCount, 32 * kMiB, {kCount, 8 * kMiB}, MeshLimits()},
          WriteLimits(),
          ReadLimits(),
          {FileLimits(), {kCount, 8 * kMiB}, ReadLimits()}};
}
inline OffDuplicateFileWorkflowWorkPolicy Work(bool serial = false) {
  const OffByteWorkPolicy bytes{Threshold(serial), 128, 16, 64};
  return {MeshWork(serial),
          CodecWork(serial),
          CodecWork(serial),
          bytes,
          bytes,
          bytes,
          {FileWork(serial), MeshWork(serial), CodecWork(serial)}};
}
class Root {
 public:
  Root() {
    static std::atomic<std::uint64_t> sequence{0};
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
      const auto tick =
          std::chrono::steady_clock::now().time_since_epoch().count();
      const auto path = fs::temp_directory_path() /
                        ("meshvale-off-workflow-" + std::to_string(tick) + "-" +
                         std::to_string(sequence.fetch_add(1)));
      std::error_code error;
      if (fs::create_directory(path, error)) {
        path_ = fs::canonical(path);
        return;
      }
    }
    Require(false, "exclusive owned temporary root");
  }
  Root(const Root&) = delete;
  Root& operator=(const Root&) = delete;
  ~Root() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
};
inline void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  Require(stream.good(), "fixture opens physical file");
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(stream.good(), "fixture full bytes close");
}
inline void Write(const fs::path& path, std::string_view text) {
  Write(path, Bytes(text));
}
inline std::vector<std::byte> Read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  Require(stream.good(), "independent physical read opens");
  const auto count = stream.tellg();
  Require(count >= 0 && count <= static_cast<std::streamoff>(4 * kMiB),
          "bounded physical read");
  std::vector<std::byte> bytes(static_cast<std::size_t>(count));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  Require(stream.good() || (bytes.empty() && stream.eof()),
          "exact physical bytes");
  return bytes;
}
inline ic::OffFileReadResult Capture(const fs::path& input,
                                     const geo::ExecutionContext& context,
                                     bool serial = false) {
  return ic::ReadOffFileAsset(input, {FileLimits(), ReadLimits()},
                              {FileWork(serial), CodecWork(serial)}, context);
}
inline void CheckMesh(const geo::MaterializedMesh& mesh, bool changed,
                      bool empty = false) {
  const auto view = mesh.View();
  Require(view.attributes.empty() &&
              view.positions.size() == (empty ? 0 : kPositionBits.size()),
          "literal position count and no dropped/adopted channels");
  for (std::size_t i = 0; i < view.positions.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) == kPositionBits[i],
            "all position bits including unused signed zeros");
  constexpr std::array<geo::index_t, 1> empty_offsets{0};
  const auto offsets =
      empty ? std::span<const geo::index_t>(empty_offsets)
            : (changed ? std::span<const geo::index_t>(kChangedOffsets)
                       : std::span<const geo::index_t>(kOffsets));
  const auto corners =
      empty ? std::span<const geo::index_t>{}
            : (changed ? std::span<const geo::index_t>(kChangedCorners)
                       : std::span<const geo::index_t>(kCorners));
  Require(std::ranges::equal(view.face_offsets, offsets) &&
              std::ranges::equal(view.corner_vertices, corners),
          "literal complete directed polygon loops and offsets");
}
inline void CheckRepair(const MaterializedDuplicateRepair& repair, bool changed,
                        bool empty = false) {
  CheckMesh(repair.Source(), false, empty);
  CheckMesh(repair.Candidate(), changed, empty);
  Require(repair.Outcome() == (changed ? CandidateOutcome::accepted
                                       : CandidateOutcome::unchanged),
          "actual existing engine outcome");
  if (changed) {
    Require(std::ranges::equal(repair.FaceMap(), kFaceMap) &&
                std::ranges::equal(repair.CornerMap(), kCorners) &&
                std::ranges::equal(repair.SourceFaces(), kSourceFaces) &&
                std::ranges::equal(repair.SourceCorners(), kSourceCorners),
            "all four literal cyclic correspondence maps");
  } else {
    for (const auto values : {repair.FaceMap(), repair.CornerMap(),
                              repair.SourceFaces(), repair.SourceCorners()})
      for (std::size_t i = 0; i < values.size(); ++i)
        Require(values[i] == i, "unchanged correspondence identity");
    Require(repair.FaceMap().size() == (empty ? 0 : 3) &&
                repair.CornerMap().size() == (empty ? 0 : 11) &&
                repair.SourceFaces().size() == (empty ? 0 : 3) &&
                repair.SourceCorners().size() == (empty ? 0 : 11),
            "unchanged maps complete, including all four real empty maps");
  }
}
inline void Joined(const geo::MaterializationExecutionStats& stats) {
  Require(stats.launched_workers == stats.joined_workers,
          "all real launched workers joined");
}
inline void Joined(const OffWorkflowExecution& execution) {
  for (const auto* stats : {&execution.repair.source_inspection,
                            &execution.repair.eligibility,
                            &execution.repair.comparison,
                            &execution.repair.selection,
                            &execution.repair.correspondence,
                            &execution.repair.candidate_inspection,
                            &execution.repair.verification,
                            &execution.repair.directed_verification,
                            &execution.writing.codec,
                            &execution.writing.materialization,
                            &execution.writing.inspection,
                            &execution.reload.codec,
                            &execution.reload.materialization,
                            &execution.reload.inspection,
                            &execution.source_protection,
                            &execution.repair_correspondence,
                            &execution.canonical_preservation,
                            &execution.reload_verification,
                            &execution.source_verification,
                            &execution.staged_candidate,
                            &execution.hashing,
                            &execution.report_copy})
    Joined(*stats);
  Require(execution.hashing.launched_workers == 0 &&
              execution.hashing.joined_workers == 0,
          "actual raw SHA streams are serial");
}
inline void Released(const geo::ExecutionContext& context) {
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "last owner frees full ledger and workers");
}
inline OffDuplicateFileWorkflowRequest Request(
    const ic::OffFileAsset& asset, bool changed,
    const fs::path* destination = nullptr,
    std::string_view id = "literal-off-workflow") {
  return {asset,
          id,
          changed ? std::span<const DuplicateTarget>(kTargets)
                  : std::span<const DuplicateTarget>{},
          ic::OffTextPolicy::kCanonicalAscii,
          ic::OffEdgeCountPolicy::kUnknownZero,
          destination
              ? std::optional<OffWorkflowPublication>{{*destination, kReceipt}}
              : std::nullopt};
}
}  // namespace meshvale::repair::test_off_workflow
#endif  // MESHVALE_REPAIR_TESTS_OFF_WORKFLOW_FIXTURE_H_
