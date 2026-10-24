// SPDX-License-Identifier: Apache-2.0
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
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace fs = std::filesystem;
namespace repair = meshvale::repair;
constexpr std::uint64_t kCountLimit = 65536;
constexpr std::uint64_t kPhysicalBytes = 1024 * 1024;
constexpr std::uint64_t kRequestBytes = 8 * kPhysicalBytes;
constexpr std::uint64_t kContextBytes = 64 * kPhysicalBytes;
constexpr std::uint64_t kPathUnits = 4096;
constexpr std::uint64_t kNameBytes = 1024;
constexpr std::uint64_t kLineBytes = 4096;
constexpr std::uint64_t kTokenBytes = 128;
constexpr std::uint64_t kCommentBytes = 1024;
constexpr std::uint64_t kInputIdBytes = 128;
constexpr geo::MeshMaterializationWorkPolicy kGeometryWork{1, 256, 32, 32};
constexpr ic::OffWorkPolicy kCodecWork{32, 4, 1, kGeometryWork};
constexpr ic::OffFileWorkPolicy kFileWork{64, 1, 64, 32, 32};
constexpr std::string_view kReceiptName = "meshvale-report.json";
constexpr std::string_view kSource =
    "# indexed polygons with an unused signed-zero vertex\nOFF\n8 3 17\n"
    "-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 0\n99 -0 5\n"
    "4 0 1 2 3\n4 2 3 0 1\n3 4 5 6\n";
constexpr std::string_view kEmpty = "# actual empty capture\nOFF\n0 0 17\n";
constexpr std::array<std::uint64_t, 24> kPositionBits{0x8000000000000000,
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
constexpr std::array<geo::index_t, 4> kOffsets{0, 4, 8, 11};
constexpr std::array<geo::index_t, 11> kCorners{0, 1, 2, 3, 2, 3,
                                                0, 1, 4, 5, 6};

void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
ic::OffReadLimits ReadLimits() {
  return {kPhysicalBytes,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          kLineBytes,
          kTokenBytes,
          kCommentBytes,
          kRequestBytes,
          3 * kRequestBytes,
          {kCountLimit, kCountLimit, kCountLimit, kCountLimit, 1, 1, 1, 1, 1,
           kNameBytes, kRequestBytes},
          {kCountLimit, kRequestBytes}};
}
ic::OffWriteLimits WriteLimits() {
  return {kPhysicalBytes,    kCountLimit,
          kCountLimit,       kCountLimit,
          kCountLimit,       kLineBytes,
          kTokenBytes,       kRequestBytes,
          2 * kRequestBytes, {kCountLimit, kRequestBytes}};
}
ic::OffFileLimits FileLimits() {
  return {kPhysicalBytes, kPhysicalBytes, 2 * kPhysicalBytes, kNameBytes,
          kNameBytes,     kPathUnits,     kRequestBytes};
}
void CheckMesh(const geo::MaterializedMesh& mesh, bool empty,
               bool changed = false) {
  const auto view = mesh.View();
  Require(view.attributes.empty(), "No attributes may appear");
  if (empty) {
    Require(view.positions.empty() && view.corner_vertices.empty() &&
                view.face_offsets.size() == 1 && view.face_offsets[0] == 0,
            "Empty canonical storage must be genuine");
    return;
  }
  constexpr std::array<geo::index_t, 3> changed_offsets{0, 4, 7};
  constexpr std::array<geo::index_t, 7> changed_corners{0, 1, 2, 3, 4, 5, 6};
  const auto offsets = changed ? std::span<const geo::index_t>(changed_offsets)
                               : std::span<const geo::index_t>(kOffsets);
  const auto corners = changed ? std::span<const geo::index_t>(changed_corners)
                               : std::span<const geo::index_t>(kCorners);
  Require(view.positions.size() == kPositionBits.size() &&
              std::ranges::equal(view.face_offsets, offsets) &&
              std::ranges::equal(view.corner_vertices, corners),
          "Complete indexed polygons and unused rows must survive");
  for (std::size_t i = 0; i < kPositionBits.size(); ++i)
    Require(std::bit_cast<std::uint64_t>(view.positions[i]) == kPositionBits[i],
            "Every source coordinate bit must survive");
}
void WriteFile(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream file(path, std::ios::binary);
  Require(bool(file), "Cannot create demonstration input");
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  file.close();
  Require(bool(file), "Cannot finish demonstration input");
}
std::vector<std::byte> ReadFile(const fs::path& path) {
  const auto size = fs::file_size(path);
  Require(size <= kPhysicalBytes, "Unexpected demonstration output extent");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream file(path, std::ios::binary);
  Require(bool(file), "Cannot reopen physical output");
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(file.gcount() == static_cast<std::streamsize>(bytes.size()) &&
              file.peek() == std::char_traits<char>::eof(),
          "Actual physical bytes must have the exact expected extent");
  return bytes;
}
class Directory {
 public:
  Directory()
      : path_(fs::canonical(fs::temp_directory_path()) /
              ("meshvale-off-workflow-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    Require(fs::create_directory(path_), "Demonstration directory must be new");
  }
  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;
  ~Directory() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }
  const fs::path& Path() const { return path_; }

 private:
  fs::path path_;
};
repair::OffDuplicateFileWorkflowLimits WorkflowLimits() {
  const auto read = ReadLimits();
  return {kRequestBytes,
          kInputIdBytes,
          kPhysicalBytes,
          kPhysicalBytes,
          2 * kPhysicalBytes,
          kCountLimit,
          kCountLimit,
          kCountLimit,
          {kCountLimit,
           1,
           1,
           kCountLimit,
           4 * kRequestBytes,
           {kCountLimit, kRequestBytes},
           read.materialization},
          WriteLimits(),
          read,
          {FileLimits(), {kCountLimit, kRequestBytes}, read}};
}
repair::OffDuplicateFileWorkflowWorkPolicy WorkflowWork() {
  constexpr repair::OffByteWorkPolicy bytes{1, 256, 32, 32};
  return {kGeometryWork,
          kCodecWork,
          kCodecWork,
          bytes,
          bytes,
          bytes,
          {kFileWork, kGeometryWork, kCodecWork}};
}
void CheckMaps(const repair::MaterializedDuplicateRepair& facts, bool empty,
               bool changed) {
  CheckMesh(facts.Source(), empty);
  CheckMesh(facts.Candidate(), empty, changed);
  if (empty) {
    Require(facts.FaceMap().empty() && facts.CornerMap().empty() &&
                facts.SourceFaces().empty() && facts.SourceCorners().empty(),
            "Empty maps must come from the actual empty repair");
    return;
  }
  constexpr std::array<geo::index_t, 3> changed_faces{0, 0, 1};
  constexpr std::array<geo::index_t, 11> changed_corners{0, 1, 2, 3, 2, 3,
                                                         0, 1, 4, 5, 6};
  constexpr std::array<geo::index_t, 2> source_faces{0, 2};
  constexpr std::array<geo::index_t, 7> source_corners{0, 1, 2, 3, 8, 9, 10};
  if (changed) {
    Require(std::ranges::equal(facts.FaceMap(), changed_faces) &&
                std::ranges::equal(facts.CornerMap(), changed_corners) &&
                std::ranges::equal(facts.SourceFaces(), source_faces) &&
                std::ranges::equal(facts.SourceCorners(), source_corners),
            "All actual forward and inverse correspondence maps must agree");
  } else {
    for (const auto map : {facts.FaceMap(), facts.CornerMap(),
                           facts.SourceFaces(), facts.SourceCorners()})
      for (std::size_t i = 0; i < map.size(); ++i)
        Require(map[i] == i, "Unchanged correspondence is actual identity");
    Require(facts.FaceMap().size() == 3 && facts.SourceFaces().size() == 3 &&
                facts.CornerMap().size() == 11 &&
                facts.SourceCorners().size() == 11,
            "Unchanged maps retain every face and corner");
  }
}
void Demonstrate(const Directory& directory, bool empty,
                 const geo::ExecutionContext& context) {
  const auto entry = fs::path(u8"mesh-\u7f51\u683c.off");
  const auto source = directory.Path() / entry;
  WriteFile(source, Bytes(empty ? kEmpty : kSource));
  const ic::OffFileReadLimits reading{FileLimits(), ReadLimits()};
  const ic::OffFileReadWorkPolicy read_work{kFileWork, kCodecWork};
  auto capture = ic::ReadOffFileAsset(source, reading, read_work, context);
  Require(capture.asset.has_value(),
          "Real retained OFF file capture must succeed");
  CheckMesh(capture.asset->Document().Mesh(), empty);
  Require(fs::remove(source),
          "Captured source must outlive physical source removal");
  constexpr std::array<repair::DuplicateTarget, 1> targets{{{0, 1}}};
  for (const bool changed : {false, true}) {
    if (empty && changed) continue;
    for (const bool publish : {false, true}) {
      const auto destination =
          directory.Path() /
          (empty ? "empty" : (changed ? "changed" : "unchanged"));
      const repair::OffDuplicateFileWorkflowRequest request{
          *capture.asset,
          "installed-off-workflow",
          changed ? std::span<const repair::DuplicateTarget>(targets)
                  : std::span<const repair::DuplicateTarget>{},
          ic::OffTextPolicy::kCanonicalAscii,
          ic::OffEdgeCountPolicy::kUnknownZero,
          publish
              ? std::optional<repair::OffWorkflowPublication>{{destination,
                                                               kReceiptName}}
              : std::nullopt};
      const auto result = repair::RunOffDuplicateFileWorkflow(
          request, WorkflowLimits(), WorkflowWork());
      Require(
          result.status == repair::OffWorkflowStatus::kSuccess &&
              result.record && result.record->RepairFacts() &&
              result.record->Output() && result.record->Reload() &&
              result.record->InputSha256() &&
              !result.record->ReportJson().empty(),
          "Real repair, write, reload, preservation and report must complete");
      const auto& record = *result.record;
      CheckMaps(*record.RepairFacts(), empty, changed);
      CheckMesh(record.Reload()->Mesh(), empty, changed);
      Require(record.RepairFacts()->Outcome() ==
                  (changed ? repair::CandidateOutcome::accepted
                           : repair::CandidateOutcome::unchanged),
              "Empty targets use the existing engine's unchanged outcome");
      if (publish) {
        Require(record.Published() &&
                    record.PublishedReportPath() == kReceiptName &&
                    record.Published()->ReceiptBytes() &&
                    std::ranges::equal(*record.Published()->ReceiptBytes(),
                                       record.ReportJson()) &&
                    std::ranges::equal(
                        ReadFile(destination / fs::path(kReceiptName)),
                        record.ReportJson()) &&
                    std::ranges::equal(ReadFile(destination / entry),
                                       record.Output()->Bytes()),
                "Committed entry and native/imported/physical receipt bytes "
                "must agree");
        auto reopened = ic::ReadOffFileAsset(destination / entry, reading,
                                             read_work, context);
        Require(reopened.asset.has_value(),
                "Published entry must physically recapture");
        CheckMesh(reopened.asset->Document().Mesh(), empty, changed);
        std::size_t members = 0;
        for (const auto& member : fs::directory_iterator(destination)) {
          Require(member.is_regular_file() && !member.is_symlink() &&
                      (member.path().filename() == entry ||
                       member.path().filename() == fs::path(kReceiptName)),
                  "Complete bundle inventory must contain exactly the two "
                  "declared regular files");
          ++members;
        }
        Require(members == 2, "Published bundle has one entry and one receipt");
        const auto refused = repair::RunOffDuplicateFileWorkflow(
            request, WorkflowLimits(), WorkflowWork());
        Require(
            refused.publication && !refused.publication->bundle &&
                refused.publication->diagnostic.status ==
                    ic::OffFileStatus::kDestinationExists &&
                std::ranges::equal(ReadFile(destination / entry),
                                   record.Output()->Bytes()) &&
                std::ranges::equal(
                    ReadFile(destination / fs::path(kReceiptName)),
                    record.ReportJson()),
            "Existing destination refuses without replacing committed bytes");
      } else {
        Require(!record.Published() && !record.PublishedReportPath() &&
                    !fs::exists(destination),
                "Memory-only verification must not create a bundle");
      }
    }
  }
  Require(std::ranges::equal(capture.asset->EntryBytes(),
                             Bytes(empty ? kEmpty : kSource)) &&
              !fs::exists(source),
          "All workflows retain exact captured bytes after source removal");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "Unknown demonstration argument");
    Directory directory;
    geo::ExecutionContext context({2, 1, kContextBytes});
    Demonstrate(directory, true, context);
    if (unsupported) {
      const auto source = directory.Path() / "unsupported.off";
      WriteFile(source, Bytes(kSource));
      const auto refused =
          ic::ReadOffFileAsset(source, {FileLimits(), ReadLimits()},
                               {kFileWork, kCodecWork}, context);
      Require(
          !refused.asset &&
              refused.diagnostic.off_status ==
                  ic::OffStatus::kGeometryFailure &&
              refused.diagnostic.off_diagnostic &&
              refused.diagnostic.off_diagnostic->geometry_status ==
                  geo::MaterializationStatus::kUnsupportedAllocationProfile,
          "Nonempty capture retains the actual unsupported producer outcome");
    } else
      Demonstrate(directory, false, context);
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "All actual owners and workers release");
    if (unsupported)
      std::cout << "OFF repair: actual empty workflows, reports, publication, "
                   "no-overwrite and nonempty producer refusal checked\n";
    else
      std::cout
          << "OFF repair: changed/unchanged/empty, complete maps, retained "
             "source, verified reports, physical publication and "
             "no-overwrite checked\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
