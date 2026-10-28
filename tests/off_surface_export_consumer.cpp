// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/off_surface_export.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "off_surface_export_fixture.h"
namespace {
namespace f = meshvale::repair::test_off_surface;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace rep = meshvale::repair;
using f::Require;
void Retained(bool unsupported) {
  f::Root root;
  const auto input = root.Path() / f::kEntry,
             destination = root.Path() / "published";
  std::optional<rep::OffGlbSurfaceWorkflowRecord> retained;
  std::vector<std::byte> off_report, receipt;
  {
    geo::ExecutionContext caller_context({4, 1, f::kLedgerBytes});
    f::Write(input, unsupported ? f::kEmpty : f::kMixed);
    auto capture = f::Capture(input, caller_context);
    Require(capture.asset.has_value(),
            "installed public OFF capture returns genuine source");
    Require(std::filesystem::remove(input),
            "captured source file is removed before composed export");
    const auto result = rep::RunOffGlbSurfaceWorkflow(
        f::Request(*capture.asset, destination,
                   unsupported
                       ? std::span<const rep::DuplicateTarget>{}
                       : std::span<const rep::DuplicateTarget>(f::kTargets)),
        f::Limits(), f::Work());
    if (unsupported && !result.record)
      f::LogUnsupportedFacts(result, "consumer empty input missing record");
    Require(result.record.has_value(),
            "installed workflow returns retained reached facts");
    if (unsupported) {
      const bool expected_unsupported =
          result.status == rep::OffGlbSurfaceStatus::kUnsupported &&
          result.record->Facts().inspection &&
          result.record->Facts().inspection->status ==
              ic::SurfaceStatus::kUnsupported &&
          result.record->Facts().inspection->diagnostic.code ==
              ic::SurfaceCode::kNumericProfile &&
          !result.record->Facts().inspection->inspection &&
          !result.record->Facts().publication;
      if (!expected_unsupported)
        f::LogUnsupportedFacts(result, "consumer empty input");
      Require(expected_unsupported,
              "installed unsupported parser keeps genuine earlier stages "
              "without fake proof or commit");
    } else
      f::Complete(result, destination, f::kMixed, true);
    retained = *result.record;
    const auto report = retained->Facts().off->record->ReportJson();
    off_report.assign(report.begin(), report.end());
    if (!unsupported) {
      const auto bytes = retained->Facts().publication->bundle->Receipt().bytes;
      receipt.assign(bytes.begin(), bytes.end());
    }
  }
  // Caller capture, source file, result and original execution handle are gone.
  f::CheckOff(*retained, unsupported ? f::kEmpty : f::kMixed, !unsupported);
  Require(!std::filesystem::exists(input) &&
              std::ranges::equal(retained->Facts().off->record->ReportJson(),
                                 off_report),
          "strong record keeps source, candidate, maps and exact report after "
          "caller/session destruction");
  auto ledger = retained->Source().Document().Mesh().ShareExecutionContext();
  const auto live = ledger.ActiveTrackedPayload();
  Require(live > 0 && ledger.ActiveWorkers() == 0,
          "only immutable actual owners survive; every worker joined");
  {
    auto copy = *retained;
    Require(ledger.ActiveTrackedPayload() == live &&
                copy.NewRequestedBytes() == retained->NewRequestedBytes(),
            "copy sharing adds no charge to genuine retained scopes");
    retained.reset();
    f::CheckOff(copy, unsupported ? f::kEmpty : f::kMixed, !unsupported);
    if (!unsupported) {
      Require(std::ranges::equal(
                  copy.Facts().publication->bundle->Receipt().bytes, receipt),
              "last copy preserves actual receipt after original destruction");
      f::Physical(destination, copy);
      const auto before = f::Read(destination / f::kGlbPath),
                 before_receipt = f::Read(destination / f::kReceiptPath);
      const auto refused = rep::RunOffGlbSurfaceWorkflow(
          f::Request(copy.Source(), destination), f::Limits(), f::Work());
      Require(refused.status != rep::OffGlbSurfaceStatus::kSuccess &&
                  refused.record && refused.record->Facts().publication &&
                  refused.record->Facts().publication->diagnostic.status ==
                      ic::SurfaceFileStatus::kDestinationExists,
              "installed rerun genuinely refuses existing destination");
      f::CheckOff(*refused.record, f::kMixed, true);
      Require(f::Read(destination / f::kGlbPath) == before &&
                  f::Read(destination / f::kReceiptPath) == before_receipt,
              "installed no-replace refusal preserves original payload and "
              "receipt");
    }
  }
  f::Released(ledger);
}
static_assert(
    std::is_nothrow_copy_constructible_v<rep::OffGlbSurfaceWorkflowRecord>);
static_assert(
    std::is_nothrow_copy_assignable_v<rep::OffGlbSurfaceWorkflowRecord>);
static_assert(std::is_nothrow_destructible_v<rep::OffGlbSurfaceWorkflowRecord>);
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == f::kUnsupportedArgument;
    Require(argc == 1 || unsupported, "known installed consumer argument");
    f::CheckReader();
    Retained(unsupported);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
