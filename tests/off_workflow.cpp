// SPDX-License-Identifier: Apache-2.0
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
#include <type_traits>
#include <variant>
#include <vector>

#include "off_workflow_fixture.h"

namespace {
namespace rep = meshvale::repair;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace f = rep::test_off_workflow;
namespace fs = std::filesystem;
using f::Require;
constexpr std::string_view kSourceSha =
    "9276b5149a05dd440dc63e1ef9be7c9fb6831e33040833eae4c2712cdb68edab";
constexpr std::string_view kEmptySha =
    "f521b7c4af6b1ad7e20401b136222304f8f5894f7517c778d5d4085403e952c1";
struct Reports {
  fs::path root;
  std::vector<std::string> cases;
  void Save(std::string_view name,
            const rep::OffDuplicateFileWorkflowResult& result,
            std::string_view source, const fs::path* destination = nullptr) {
    if (root.empty()) return;
    const auto directory = root / name;
    fs::create_directories(directory);
    f::Write(directory / "source.off", source);
    if (result.record && !result.record->ReportJson().empty())
      f::Write(directory / "report.json", result.record->ReportJson());
    if (destination && result.record && result.record->Published()) {
      fs::create_directories(directory / "bundle");
      for (const auto& entry : fs::directory_iterator(*destination))
        f::Write(directory / "bundle" / entry.path().filename(),
                 f::Read(entry.path()));
    }
    cases.emplace_back(name);
  }
  void Finish() const {
    if (root.empty()) return;
    fs::create_directories(root);
    std::ofstream stream(root / "cases.json", std::ios::binary);
    stream << "{\"version\":1,\"cases\":[";
    for (std::size_t i = 0; i < cases.size(); ++i) {
      if (i) stream << ',';
      stream << '"' << cases[i] << '"';
    }
    stream << "]}\n";
    stream.close();
    Require(stream.good(), "independent report manifest closes");
  }
};
void Coverage(const rep::OffDuplicateFileWorkflowRecord& record,
              bool published) {
  constexpr std::array required{rep::OffWorkflowCheck::kSourceStorage,
                                rep::OffWorkflowCheck::kTargetPolicy,
                                rep::OffWorkflowCheck::kCandidateStorage,
                                rep::OffWorkflowCheck::kRepairCorrespondence,
                                rep::OffWorkflowCheck::kCanonicalPreservation,
                                rep::OffWorkflowCheck::kReloadCandidate,
                                rep::OffWorkflowCheck::kSourceUnchanged,
                                rep::OffWorkflowCheck::kStagedCandidate,
                                rep::OffWorkflowCheck::kStagedPhysicalMembers};
  Require(record.Coverage().size() == 10,
          "complete ten named coverage observations");
  std::size_t performed = 0;
  for (std::size_t i = 0; i < required.size(); ++i) {
    const auto found = std::ranges::find(record.Coverage(), required[i],
                                         &rep::OffWorkflowCoverage::check);
    Require(found != record.Coverage().end(),
            "required predicate never disappears");
    if (i < 7 || published) {
      Require(found->state == rep::OffWorkflowCoverageState::kPerformed &&
                  found->passed == true &&
                  found->reason == rep::OffWorkflowDiagnosticCode::kNone,
              "real required preservation predicate passed");
      ++performed;
    } else
      Require(found->state == rep::OffWorkflowCoverageState::kSkipped &&
                  !found->passed,
              "memory-only staged predicates not claimed performed");
  }
  Require(performed == (published ? 9 : 7),
          "literal7 memory or9 publication checks");
  const auto solid = std::ranges::find(record.Coverage(),
                                       rep::OffWorkflowCheck::kSolidValidity,
                                       &rep::OffWorkflowCoverage::check);
  Require(
      solid != record.Coverage().end() &&
          solid->state == rep::OffWorkflowCoverageState::kUnsupported &&
          !solid->passed &&
          solid->reason == rep::OffWorkflowDiagnosticCode::kSolidUnavailable,
      "solid validity remains explicitly unavailable");
}
void Complete(const rep::OffDuplicateFileWorkflowResult& result, bool changed,
              bool empty, bool published, const fs::path& destination,
              std::string_view source) {
  Require(
      result.status == rep::OffWorkflowStatus::kSuccess && result.record &&
          result.outcomes.execution ==
              rep::OffWorkflowExecutionOutcome::kCompleted &&
          result.outcomes.candidate ==
              (changed ? rep::CandidateOutcome::accepted
                       : rep::CandidateOutcome::unchanged) &&
          result.outcomes.profile == rep::OffWorkflowProfileOutcome::kPassed &&
          result.outcomes.publication ==
              (published ? rep::OffWorkflowPublicationOutcome::kPublished
                         : rep::OffWorkflowPublicationOutcome::kNotRequested) &&
          result.outcomes.stage == rep::OffWorkflowStage::kComplete &&
          result.outcomes.exit == rep::OffWorkflowExitMeaning::kSuccess &&
          !result.report_diagnostic,
      "actual complete execution/candidate/profile/publication/exit tuple");
  const auto& record = *result.record;
  Require(record.RepairFacts() && record.Output() && record.Reload(),
          "actual completed owner, verified write and reload retained");
  f::CheckRepair(*record.RepairFacts(), changed, empty);
  f::CheckMesh(record.Reload()->Mesh(), changed, empty);
  Require(std::ranges::equal(record.Source().EntryBytes(), f::Bytes(source)) &&
              std::ranges::equal(record.Source().Document().OriginalBytes(),
                                 f::Bytes(source)) &&
              record.Source().Document().DeclaredEdgeCount() == 17 &&
              record.Reload()->DeclaredEdgeCount() == 0,
          "capture bytes never rewritten; canonical serialization declares "
          "edge zero");
  Require(record.Output()->Facts().source_declared_edge_count == 17 &&
              record.Output()->Facts().correspondence ==
                  ic::OffCorrespondence::kIdentity &&
              record.Output()->Facts().vertex_count == (empty ? 0 : 8) &&
              record.Output()->Facts().face_count ==
                  (empty ? 0 : (changed ? 2 : 3)) &&
              record.Output()->Facts().corner_count ==
                  (empty ? 0 : (changed ? 7 : 11)),
          "actual writer facts bind candidate not source counts");
  Require(
      !std::ranges::equal(record.Output()->Bytes(), f::Bytes(source)),
      "unchanged canonical candidate still honestly rewrites lexical source");
  Require(record.InputSha256() == (empty ? kEmptySha : kSourceSha),
          "raw complete source SHA matches independently computed literal");
  const auto report = f::Text(record.ReportJson());
  Require(
      report.starts_with("{\"schema\":\"meshvale.report/1\"") &&
          report.find("\"operation\":") != std::string_view::npos &&
          report.find("\"meshvale.repair.off\":{\"version\":1") !=
              std::string_view::npos,
      "real native report uses unchanged public core and typed OFF extension");
  Coverage(record, published);
  f::Joined(result.execution);
  if (published) {
    Require(result.publication && result.publication->bundle &&
                record.Published() &&
                record.PublishedReportPath() == f::kReceipt,
            "true publication alone exposes physical receipt path");
    const auto portable_name = record.Published()->EntryPath();
    const auto entry =
        fs::path(std::u8string(portable_name.begin(), portable_name.end()));
    Require(
        f::Read(destination / entry) ==
                std::vector<std::byte>(record.Output()->Bytes().begin(),
                                       record.Output()->Bytes().end()) &&
            f::Read(destination / f::kReceipt) ==
                std::vector<std::byte>(record.ReportJson().begin(),
                                       record.ReportJson().end()) &&
            record.Published()->ReceiptBytes() &&
            std::ranges::equal(*record.Published()->ReceiptBytes(),
                               record.ReportJson()),
        "native returned, imported and physical report bytes are identical");
    std::size_t count = 0;
    for (const auto& item : fs::directory_iterator(destination)) {
      Require(item.is_regular_file() && !item.is_symlink(),
              "flat regular bundle members");
      ++count;
      Require(item.path().filename() == entry ||
                  item.path().filename() == fs::path(f::kReceipt),
              "no unexpected bundle member");
    }
    Require(count == 2, "whole physical inventory exactly entry plus report");
    f::CheckMesh(record.Published()->Document().Mesh(), changed, empty);
  } else
    Require(!result.publication && !record.Published() &&
                !record.PublishedReportPath() &&
                report.find("\"artifacts\":[]") != std::string_view::npos &&
                report.find("\"role\":\"output\"") == std::string_view::npos &&
                !fs::exists(destination),
            "memory verification does not invent exported output or artifact");
}
void Corpus(bool unsupported, Reports& reports) {
  for (bool empty : {false, true}) {
    if (unsupported && !empty) continue;
    for (bool serial : {false, true})
      for (bool published : {false, true})
        for (bool changed : {false, true}) {
          if (empty && changed) continue;
          f::Root root;
          const auto input = root.Path() / fs::path(u8"\u6e90-\U0001f600.off");
          const auto destination =
              root.Path() / fs::path(u8"\u5019\u9009-\U0001f600");
          constexpr std::array<unsigned char, 12> kFilenameUtf8{
              0xe6, 0xba, 0x90, 0x2d, 0xf0, 0x9f,
              0x98, 0x80, 0x2e, 0x6f, 0x66, 0x66};
          Require(
              std::ranges::equal(input.filename().u8string(), kFilenameUtf8),
              "actual Unicode filename has literal Chinese/emoji UTF8 "
              "independent of compiler source codepage");
          f::Write(input, empty ? f::kEmpty : f::kSource);
          geo::ExecutionContext context({4, 1, f::kLedger});
          {
            auto captured = f::Capture(input, context, serial);
            std::string filename_bytes;
            for (const auto byte : input.filename().u8string())
              filename_bytes +=
                  std::to_string(static_cast<unsigned>(byte)) + ",";
            Require(
                captured.asset.has_value(),
                "actual selected capture succeeds: status=" +
                    std::to_string(static_cast<int>(captured.status)) +
                    " stage=" +
                    std::to_string(
                        static_cast<int>(captured.diagnostic.stage)) +
                    " code=" +
                    std::to_string(static_cast<int>(captured.diagnostic.code)) +
                    " filename_utf8=" + filename_bytes);
            fs::remove(input);
            const auto request = f::Request(*captured.asset, changed,
                                            published ? &destination : nullptr);
            const auto result = rep::RunOffDuplicateFileWorkflow(
                request, f::Limits(), f::Work(serial));
            Complete(result, changed, empty, published, destination,
                     empty ? f::kEmpty : f::kSource);
            const std::string name =
                std::string(empty ? "empty"
                                  : (changed ? "changed" : "unchanged")) +
                (published ? "-published" : "-memory") +
                (serial ? "-serial" : "-parallel");
            reports.Save(name, result, empty ? f::kEmpty : f::kSource,
                         published ? &destination : nullptr);
          }
          f::Released(context);
        }
  }
}
void PointOnly() {
  f::Root root;
  const auto input = root.Path() / "point.off";
  f::Write(input, f::kPoint);
  geo::ExecutionContext context({4, 1, f::kLedger});
  {
    auto capture = f::Capture(input, context);
    Require(capture.asset.has_value(), "nonempty point-only actual capture");
    const auto result = rep::RunOffDuplicateFileWorkflow(
        f::Request(*capture.asset, false), f::Limits(), f::Work());
    Require(result.status == rep::OffWorkflowStatus::kSuccess &&
                result.record && result.record->RepairFacts(),
            "real point-only engine, writer and reload complete");
    const auto& repair = *result.record->RepairFacts();
    Require(repair.Outcome() == rep::CandidateOutcome::unchanged &&
                repair.FaceMap().empty() && repair.CornerMap().empty() &&
                repair.SourceFaces().empty() &&
                repair.SourceCorners().empty() &&
                result.record->Reload()->Mesh().View().positions.size() == 3 &&
                std::bit_cast<std::uint64_t>(
                    result.record->Reload()->Mesh().View().positions[0]) ==
                    0x8000000000000000,
            "point-only is nonempty and preserves signedzero without synthetic "
            "faces");
    Coverage(*result.record, false);
  }
  f::Released(context);
}
void Rejections(Reports& reports) {
  const std::array cases{
      std::string(f::kSource),
      std::string("OFF\n8 3 17\n-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 "
                  "0\n99 -0 5\n4 0 1 2 3\n4 0 3 2 1\n3 4 5 6\n"),
      std::string("OFF\n9 3 17\n-0 0 0\n1 0 0\n1 1 0\n0 1 0\n2 0 0\n3 0 0\n2 1 "
                  "0\n99 -0 5\n-0 0 0\n4 0 1 2 3\n4 8 1 2 3\n3 4 5 6\n")};
  for (std::size_t i = 0; i < cases.size(); ++i) {
    f::Root root;
    const auto input = root.Path() / f::kEntry;
    f::Write(input, cases[i]);
    geo::ExecutionContext context({4, 1, f::kLedger});
    {
      auto capture = f::Capture(input, context);
      Require(capture.asset.has_value(), "real rejection source capture");
      auto request = f::Request(*capture.asset, true);
      constexpr std::array<rep::DuplicateTarget, 1> outside{{{0, 99}}};
      if (i == 0) request.targets = outside;
      const auto result =
          rep::RunOffDuplicateFileWorkflow(request, f::Limits(), f::Work());
      Require(
          result.status == rep::OffWorkflowStatus::kRejected && result.record &&
              !result.record->RepairFacts() && !result.record->Output() &&
              !result.record->Reload() && !result.record->InputSha256() &&
              result.outcomes.execution ==
                  rep::OffWorkflowExecutionOutcome::kCompleted &&
              result.outcomes.candidate == rep::CandidateOutcome::rejected &&
              result.outcomes.exit ==
                  rep::OffWorkflowExitMeaning::kRejectedOrIncomplete &&
              result.outcomes.stage == rep::OffWorkflowStage::kComplete,
          "finite actual rejection is completed without candidate "
          "owner/hash/output");
      const auto diagnostic = std::get<rep::MaterializedDuplicateDiagnostic>(
          result.diagnostic.delegated);
      Require(
          diagnostic.code ==
              (i == 0
                   ? rep::MaterializedDuplicateDiagnosticCode::kTargetRange
                   : rep::MaterializedDuplicateDiagnosticCode::kNotEquivalent),
          "range, reversed winding and equal-coordinate distinct IDs retain "
          "actual diagnostic");
      reports.Save("rejected-" + std::to_string(i), result, cases[i]);
      f::Joined(result.execution);
    }
    f::Released(context);
  }
}
void Lifetime(bool unsupported) {
  f::Root root;
  const auto input = root.Path() / f::kEntry;
  f::Write(input, unsupported ? f::kEmpty : f::kSource);
  std::optional<ic::OffFileAsset> source;
  {
    geo::ExecutionContext original({4, 1, f::kLedger});
    auto captured = f::Capture(input, original);
    Require(captured.asset.has_value(), "lifetime capture");
    source.emplace(*captured.asset);
  }
  fs::remove(input);
  std::optional<rep::OffDuplicateFileWorkflowRecord> held;
  {
    auto result = rep::RunOffDuplicateFileWorkflow(
        f::Request(*source, !unsupported), f::Limits(), f::Work());
    Require(result.record && result.status == rep::OffWorkflowStatus::kSuccess,
            "workflow uses source after original context destruction");
    held.emplace(*result.record);
  }
  source.reset();
  auto final_context = held->Source().Document().Mesh().ShareExecutionContext();
  f::CheckRepair(*held->RepairFacts(), !unsupported, unsupported);
  std::optional<rep::OffDuplicateFileWorkflowRecord> copy{*held};
  held.reset();
  Require(!copy->ReportJson().empty() && copy->InputSha256(),
          "copy retains bytes/maps/hash after source and result destruction");
  const auto retained = final_context.ActiveTrackedPayload();
  {
    auto second = *copy;
    Require(final_context.ActiveTrackedPayload() == retained &&
                !second.ReportJson().empty(),
            "record sharing copy has no fresh tracked request");
  }
  copy.reset();
  f::Released(final_context);
}
void UnsupportedCapture() {
  f::Root root;
  const auto input = root.Path() / f::kEntry;
  f::Write(input, f::kSource);
  geo::ExecutionContext context({4, 1, f::kLedger});
  const auto captured = f::Capture(input, context);
  Require(
      !captured.asset && captured.status == ic::OffFileStatus::kOffFailure &&
          captured.diagnostic.off_status == ic::OffStatus::kGeometryFailure &&
          captured.diagnostic.off_diagnostic &&
          captured.diagnostic.off_diagnostic->geometry_status ==
              geo::MaterializationStatus::kUnsupportedAllocationProfile,
      "nonempty actual numerical producer refusal never becomes a portable "
      "fake source");
  f::Released(context);
}
static_assert(
    std::is_nothrow_copy_constructible_v<rep::OffDuplicateFileWorkflowRecord>);
static_assert(
    std::is_nothrow_copy_assignable_v<rep::OffDuplicateFileWorkflowRecord>);
static_assert(
    std::is_nothrow_destructible_v<rep::OffDuplicateFileWorkflowRecord>);
}  // namespace
int main(int argc, char** argv) {
  try {
    bool unsupported = false;
    Reports reports;
    for (int i = 1; i < argc; ++i) {
      const std::string_view argument = argv[i];
      if (argument == "--unsupported-profile")
        unsupported = true;
      else if (argument == "--write-reports" && i + 1 < argc)
        reports.root = fs::path(argv[++i]);
      else
        throw std::runtime_error("unknown OFF workflow fixture argument");
    }
    if (unsupported) UnsupportedCapture();
    Corpus(unsupported, reports);
    Lifetime(unsupported);
    if (!unsupported) {
      PointOnly();
      Rejections(reports);
    }
    reports.Finish();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
