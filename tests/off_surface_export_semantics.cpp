// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_surface_export.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "off_surface_export_fixture.h"
namespace {
namespace f = meshvale::repair::test_off_surface;
namespace rep = meshvale::repair;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
using f::Require;
void Positive(std::string_view name, std::string_view source, bool changed,
              bool bounded = false) {
  f::Root root;
  const auto input = root.Path() / f::kEntry,
             destination = root.Path() / std::string(name);
  geo::ExecutionContext context({4, 1, f::kLedgerBytes});
  {
    f::Write(input, source);
    auto captured = f::Capture(input, context);
    Require(captured.asset.has_value(), "genuine OFF capture");
    const auto targets =
        changed ? std::span<const rep::DuplicateTarget>(f::kTargets)
                : std::span<const rep::DuplicateTarget>{};
    auto request = f::Request(*captured.asset, destination, targets);
    if (bounded)
      request.numeric = {
          ic::SurfaceNarrowing::kBoundedRne32, f::kShiftError, {}};
    const auto result =
        rep::RunOffGlbSurfaceWorkflow(request, f::Limits(), f::Work());
    f::Complete(result, destination, source, changed);
    const auto& output = *result.record->Facts().encoding->output;
    const auto numeric = output.Facts().PositionNumeric();
    Require(numeric && numeric->complete &&
                numeric->kind == ic::SurfaceNumericSummary::Kind::kDecodedWire,
            "completed actual decoded numeric observations");
    if (source == f::kMixed || source == f::kMixedShift) {
      Require(output.WireVertexSources().size() == (changed ? 18 : 24) &&
                  output.Original().Source().View().positions.size() == 42,
              "literal R14 and changed18/unchanged24 W rows");
      Require(output.SourceVertexOffsets().size() == 15 &&
                  output.SourceVertexOffsets()[12] ==
                      output.WireVertexSources().size() &&
                  output.SourceVertexOffsets()[13] ==
                      output.WireVertexSources().size() &&
                  output.SourceVertexOffsets()[14] ==
                      output.WireVertexSources().size(),
              "unused R12/R13 have empty CSR rows");
      Require(numeric->examined_values == 36 &&
                  numeric->changed_values == (bounded ? 1 : 0),
              "unique used source scalar counts, not wire occurrence counts");
      if (bounded)
        Require(numeric->max_absolute_error.used_words == 1 &&
                    numeric->max_absolute_error.magnitude[0] == 1 &&
                    numeric->max_absolute_error.exponent2 == -30,
                "exact observed maximum error is independently derived 2^-30");
      else
        Require(numeric->max_absolute_error.used_words == 0,
                "exact route has zero error");
    }
    if (source == f::kEmpty || source == f::kPoints)
      Require(numeric->examined_values == 0 && numeric->changed_values == 0,
              "unused/empty rows are retained without fabricated numeric "
              "examination");
  }
  f::Released(context);
}
void NumericRefusal(std::string_view source, ic::SurfaceCode code, double bound,
                    bool exact = false) {
  f::Root root;
  const auto input = root.Path() / f::kEntry,
             destination = root.Path() / "refused";
  geo::ExecutionContext context({4, 1, f::kLedgerBytes});
  {
    f::Write(input, source);
    auto captured = f::Capture(input, context);
    Require(captured.asset.has_value(),
            "numeric refusal source genuinely captured");
    const bool changed = source == f::kMixedShift;
    auto request =
        f::Request(*captured.asset, destination,
                   changed ? std::span<const rep::DuplicateTarget>(f::kTargets)
                           : std::span<const rep::DuplicateTarget>{});
    request.numeric = {exact ? ic::SurfaceNarrowing::kExact32
                             : ic::SurfaceNarrowing::kBoundedRne32,
                       bound,
                       {}};
    const auto result =
        rep::RunOffGlbSurfaceWorkflow(request, f::Limits(), f::Work());
    Require(result.status != rep::OffGlbSurfaceStatus::kSuccess &&
                result.diagnostic.stage == rep::OffGlbSurfaceStage::kEncoding &&
                result.record,
            "typed numerical refusal occurs after retained OFF/T");
    f::CheckOff(*result.record, source, changed);
    const auto& facts = result.record->Facts();
    Require(facts.triangulation && facts.triangulation->triangulation &&
                facts.encoding && facts.encoding->diagnostic.code == code &&
                !facts.encoding->output && !facts.inspection &&
                !facts.publication && !std::filesystem::exists(destination),
            "exact expected numerical cause; unreached V/P remain absent");
  }
  f::Released(context);
}
void Rejected(std::string_view source,
              std::span<const rep::DuplicateTarget> targets,
              rep::MaterializedDuplicateDiagnosticCode expected) {
  f::Root root;
  const auto input = root.Path() / f::kEntry,
             destination = root.Path() / "rejected";
  geo::ExecutionContext context({4, 1, f::kLedgerBytes});
  {
    f::Write(input, source);
    auto captured = f::Capture(input, context);
    Require(captured.asset.has_value(), "reject fixture capture");
    const auto result = rep::RunOffGlbSurfaceWorkflow(
        f::Request(*captured.asset, destination, targets), f::Limits(),
        f::Work());
    Require(
        result.status == rep::OffGlbSurfaceStatus::kRejected && result.record &&
            result.diagnostic.stage == rep::OffGlbSurfaceStage::kOffWorkflow,
        "actual whole-request Repair rejection");
    const auto& facts = result.record->Facts();
    Require(facts.off &&
                facts.off->status == rep::OffWorkflowStatus::kRejected &&
                facts.off->record && !facts.off->record->RepairFacts() &&
                !facts.triangulation && !facts.encoding && !facts.inspection &&
                !facts.publication,
            "rejection fabricates no candidate or later stages");
    const auto& diagnostic = std::get<rep::MaterializedDuplicateDiagnostic>(
        facts.off->diagnostic.delegated);
    Require(
        diagnostic.code == expected &&
            std::ranges::equal(result.record->Source().EntryBytes(),
                               f::Bytes(source)) &&
            !std::filesystem::exists(destination),
        "actual rejected diagnostic and complete unchanged capture survive");
  }
  f::Released(context);
}
void Nonplanar() {
  f::Root root;
  const auto input = root.Path() / f::kEntry,
             destination = root.Path() / "nonplanar";
  geo::ExecutionContext context({4, 1, f::kLedgerBytes});
  {
    f::Write(input, f::kNonplanar);
    auto captured = f::Capture(input, context);
    Require(captured.asset.has_value(), "nonplanar source capture");
    const auto result = rep::RunOffGlbSurfaceWorkflow(
        f::Request(*captured.asset, destination), f::Limits(), f::Work());
    Require(result.record && result.diagnostic.stage ==
                                 rep::OffGlbSurfaceStage::kTriangulation,
            "conversion fails after real accepted Repair");
    f::CheckOff(*result.record, f::kNonplanar, true);
    const auto& facts = result.record->Facts();
    Require(facts.triangulation &&
                facts.triangulation->status ==
                    geo::ChargedTriangulationStatus::kBlocked &&
                facts.triangulation->diagnostic.code ==
                    geo::ChargedTriangulationCode::kNonplanarFace &&
                facts.triangulation->diagnostic.face == 0 &&
                !facts.triangulation->triangulation && !facts.encoding &&
                !facts.inspection && !facts.publication,
            "typed nonplanar face zero and absent uninvoked later delegates");
  }
  f::Released(context);
}
void Unsupported() {
  f::Root root;
  geo::ExecutionContext context({4, 1, f::kLedgerBytes});
  const auto input = root.Path() / f::kEntry,
             destination = root.Path() / "empty";
  {
    f::Write(input, f::kMixed);
    const auto capture = f::Capture(input, context);
    Require(!capture.asset && capture.diagnostic.off_diagnostic &&
                capture.diagnostic.off_diagnostic->geometry_status ==
                    geo::MaterializationStatus::kUnsupportedAllocationProfile,
            "actual unsupported nonempty capture produces no fabricated source "
            "owner");
    f::Write(input, f::kEmpty);
    const auto empty = f::Capture(input, context);
    Require(empty.asset.has_value(),
            "actual empty capture in unsupported Geometry profile");
    const auto result = rep::RunOffGlbSurfaceWorkflow(
        f::Request(*empty.asset, destination, {}), f::Limits(), f::Work());
    Require(result.status == rep::OffGlbSurfaceStatus::kUnsupported &&
                result.record,
            "actual empty export must retain reached unsupported delegate");
    f::CheckOff(*result.record, f::kEmpty, false);
    const auto& facts = result.record->Facts();
    Require(
        facts.triangulation && facts.triangulation->triangulation &&
            facts.encoding && facts.encoding->output && facts.inspection &&
            facts.inspection->status == ic::SurfaceStatus::kUnsupported &&
            facts.inspection->diagnostic.code ==
                ic::SurfaceCode::kNumericProfile &&
            !facts.inspection->inspection && !facts.publication &&
            !std::filesystem::exists(destination),
        "empty unsupported parser creates neither fake proof nor publication");
  }
  f::Released(context);
}
static_assert(
    !std::is_default_constructible_v<rep::OffGlbSurfaceWorkflowRecord>);
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
    Require(argc == 1 || unsupported, "known semantic fixture arguments");
    f::CheckReader();
    if (unsupported) {
      Unsupported();
      return 0;
    }
    Positive("mixed-edit", f::kMixed, true);
    Positive("mixed-noop", f::kMixed, false);
    NumericRefusal(f::kMixedShift, ic::SurfaceCode::kExactness, 0, true);
    Positive("mixed-bounded", f::kMixedShift, true, true);
    NumericRefusal(f::kMixedShift, ic::SurfaceCode::kErrorBound,
                   std::nextafter(f::kShiftError, 0.0));
    Rejected(f::kReversed, f::kTargets,
             rep::MaterializedDuplicateDiagnosticCode::kNotEquivalent);
    Rejected(f::kDifferentIds, f::kTargets,
             rep::MaterializedDuplicateDiagnosticCode::kNotEquivalent);
    Positive("different-ids-noop", f::kDifferentIds, false);
    constexpr std::array<rep::DuplicateTarget, 2> invalid_last{
        {{0, 1}, {2, 4}}};
    Rejected(f::kMixed, invalid_last,
             rep::MaterializedDuplicateDiagnosticCode::kTargetRange);
    NumericRefusal(f::kNewCoincidence, ic::SurfaceCode::kNewCoincidence,
                   f::kShiftError);
    NumericRefusal(f::kCollapse, ic::SurfaceCode::kCollapsedTriangle,
                   f::kShiftError);
    NumericRefusal(f::kOrientation, ic::SurfaceCode::kOrientation, 0x1.8p-24);
    NumericRefusal(f::kRange, ic::SurfaceCode::kNumericRange, 0x1p128);
    NumericRefusal(f::kUnderflow, ic::SurfaceCode::kNonzeroUnderflow, 0x1p-150);
    Positive("exact-subnormal", f::kSubnormal, false);
    Nonplanar();
    Positive("true-empty", f::kEmpty, false);
    Positive("point-only", f::kPoints, false);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
