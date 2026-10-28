// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/off_surface_export.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
#include <variant>

#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"
#include "off_surface_export_fixture.h"
#include "off_surface_export_internal.h"
#include "off_workflow_test_internal.h"
namespace {
namespace rep = meshvale::repair;
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace f = rep::test_off_surface;
namespace detail = rep::off_surface_export_detail;
namespace allocation = rep::materialized_duplicates_detail;
using f::Require;
using Stage = rep::OffGlbSurfaceStage;
using Status = rep::OffGlbSurfaceStatus;
using Code = rep::OffGlbSurfaceCode;
struct Marker {};
struct Backend {
  struct Block {
    void* pointer = nullptr;
    std::size_t bytes = 0;
  };
  std::array<Block, 512> blocks{};
  allocation::AllocationBackend original =
      allocation::DefaultAllocationBackend();
  std::mutex mutex;
  std::size_t calls = 0, live = 0, fail_at = 0;
  bool frozen = false, bad_free = false;
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<Backend*>(opaque);
    std::lock_guard guard(self.mutex);
    ++self.calls;
    if (self.frozen || self.calls == self.fail_at) throw std::bad_alloc{};
    auto* pointer =
        self.original.allocate(bytes, alignment, self.original.state);
    for (auto& block : self.blocks)
      if (!block.pointer) {
        block = {pointer, bytes};
        self.live += bytes;
        return pointer;
      }
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
    throw std::bad_alloc{};
  }
  static void Free(void* pointer, std::size_t bytes, std::size_t alignment,
                   void* opaque) noexcept {
    auto& self = *static_cast<Backend*>(opaque);
    std::lock_guard guard(self.mutex);
    bool found = false;
    for (auto& block : self.blocks)
      if (block.pointer == pointer) {
        if (block.bytes != bytes) self.bad_free = true;
        self.live -= block.bytes;
        block = {};
        found = true;
        break;
      }
    if (!found) self.bad_free = true;
    self.original.deallocate(pointer, bytes, alignment, self.original.state);
  }
  allocation::AllocationBackend Options() { return {Allocate, Free, this}; }
  void Empty() const {
    Require(live == 0 && !bad_free,
            "original allocation backend receives exact last-owner frees");
  }
};
struct Fixture {
  f::Root root;
  geo::ExecutionContext context{{4, 1, f::kLedgerBytes}};
  std::filesystem::path input = root.Path() / f::kEntry,
                        destination = root.Path() / "destination";
  ic::OffFileReadResult capture;
  explicit Fixture(bool unsupported = false) {
    f::Write(input, unsupported ? f::kEmpty : f::kMixed);
    capture = f::Capture(input, context);
    Require(capture.asset.has_value(), "genuine captured request fixture");
  }
  rep::OffGlbSurfaceWorkflowRequest Request(bool unsupported = false) const {
    return f::Request(*capture.asset, destination,
                      unsupported
                          ? std::span<const rep::DuplicateTarget>{}
                          : std::span<const rep::DuplicateTarget>(f::kTargets));
  }
};
struct Probe {
  Backend backend;
  std::stop_source stop;
  Stage target = Stage::kComplete;
  const geo::ExecutionContext* context = nullptr;
  geo::PayloadLease held;
  bool occupy = false, freeze = false;
  std::array<std::size_t, 7> reached{};
  static void Checkpoint(Stage stage, void* opaque) {
    auto& self = *static_cast<Probe*>(opaque);
    ++self.reached[static_cast<std::size_t>(stage)];
    if (stage != self.target) return;
    if (self.occupy) {
      Require(self.context != nullptr, "occupied ledger fixture context");
      auto lease = geo::TryReservePayload(
          *self.context, static_cast<std::size_t>(f::kLedgerBytes) -
                             self.context->ActiveTrackedPayload());
      Require(lease.status == geo::ReservationStatus::kAccepted,
              "genuine currently remaining payload held");
      self.held = std::move(lease.lease);
    } else if (self.freeze)
      self.backend.frozen = true;
    else
      self.stop.request_stop();
  }
  detail::WorkflowTestOptions Options() {
    return {backend.Options(), Checkpoint, this, nullptr};
  }
};
using ZeroMutation = void (*)(rep::OffGlbSurfaceWorkflowLimits&,
                              rep::OffGlbSurfaceWorkflowWorkPolicy&);
constexpr std::array<ZeroMutation, 359> kZeroMutations{
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.max_wrapper_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_wrapper_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_input_id_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_source_protection_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_report_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_hash_input_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_verification_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_verification_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.max_verification_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.max_targets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.max_row_local_declarations = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.max_declaration_name_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.max_comparison_steps_per_target = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.inspection.max_issues = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.repair.selection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_output_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_corners_per_face = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_line_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_codec_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.max_total_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.inspection.max_issues = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.writing.inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_source_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_corners_per_face = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_line_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_comment_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_codec_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.max_total_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.materialization.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.inspection.max_issues = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.reload.inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.files.max_entry_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.files.max_receipt_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.files.max_total_file_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.files.max_entry_name_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.files.max_receipt_name_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.files.max_native_path_code_units = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.files.max_file_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.expected_inspection.max_issues = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.expected_inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_source_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_corners_per_face = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_line_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_comment_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_codec_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.max_total_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.materialization.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.inspection.max_issues = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.off.publication.reload.inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source_inspection.max_issues = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.source_inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output_inspection.max_issues = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.output_inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.max_map_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.max_core_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.triangulation.max_bridge_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_attribute_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_wire_primitives = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_wire_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_wire_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_map_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_members = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_member_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_total_member_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_resources = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_binary_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_capsule_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_capsule_binary_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_material_bindings = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_numeric_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_json_nodes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_json_depth = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_numeric_integer_bits = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_coincidence_comparisons = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.counts.max_relationship_steps = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.encoding.max_total_requested_allowance = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceEncodeLimits>(limits.encoding.delegates)
          .container.max_container_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceEncodeLimits>(limits.encoding.delegates)
          .container.max_chunks = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceEncodeLimits>(limits.encoding.delegates)
          .container.max_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_attribute_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_wire_primitives = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_wire_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_wire_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_map_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_members = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_member_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_total_member_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_resources = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_binary_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_capsule_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_capsule_binary_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_material_bindings = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_numeric_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_json_nodes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_json_depth = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_numeric_integer_bits = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_coincidence_comparisons = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.counts.max_relationship_steps = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.inspection.max_total_requested_allowance = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .container.max_container_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .container.max_chunks = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .container.max_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_owned_source_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_document_nodes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_document_depth = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_decoded_string_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_numeric_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_decimal_coefficient_digits = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_decimal_exponent_digits = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_table_rows = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_graph_edges = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_instances = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_accessor_elements = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.source.max_projection_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .read.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .extraction.max_components = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .extraction.max_triangle_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(limits.inspection.delegates)
          .extraction.max_output_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_payload_members = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_payload_member_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_total_payload_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_receipt_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_caller_report_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_path_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_total_path_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_native_path_code_units = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_inventory_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_stage_name_attempts = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.files.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_faces = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_face_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_attributes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_attribute_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_attribute_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_attribute_offsets = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_presence_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_metadata_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_descriptor_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_wire_primitives = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_wire_vertices = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_wire_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_map_entries = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_members = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_member_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_total_member_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_resources = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_binary_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_capsule_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_capsule_binary_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_material_bindings = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_numeric_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_json_nodes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_json_depth = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_numeric_integer_bits = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_coincidence_comparisons = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.counts.max_relationship_steps = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.inspection.max_total_requested_allowance = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .container.max_container_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .container.max_chunks = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .container.max_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_json_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_owned_source_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_document_nodes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_document_depth = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_decoded_string_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_numeric_token_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_decimal_coefficient_digits = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_decimal_exponent_digits = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_table_rows = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_graph_edges = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_instances = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_accessor_elements = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.source.max_projection_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .read.max_new_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .extraction.max_components = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .extraction.max_triangle_corners = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      std::get<ic::GltfSurfaceInspectLimits>(
          limits.publication.inspection.delegates)
          .extraction.max_output_requested_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(work);
      limits.publication.max_total_requested_allowance = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.repair.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.repair.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.repair.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.repair.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.writing.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.writing.cancellation_block_tokens = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.writing.cancellation_block_records = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.writing.geometry.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.writing.geometry.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.writing.geometry.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.writing.geometry.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.reload.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.reload.cancellation_block_tokens = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.reload.cancellation_block_records = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.reload.geometry.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.reload.geometry.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.reload.geometry.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.reload.geometry.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.verification.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.verification.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.verification.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.verification.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.hashing.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.hashing.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.hashing.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.hashing.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.report.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.report.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.report.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.report.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.files.io_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.files.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.files.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.files.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.files.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.expected_inspection.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.expected_inspection.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.expected_inspection.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.expected_inspection.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.reload.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.reload.cancellation_block_tokens = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.reload.cancellation_block_records = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.reload.geometry.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.reload.geometry.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.reload.geometry.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.off.publication.reload.geometry.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.triangulation.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.triangulation.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.triangulation.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.triangulation.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.encoding.surface.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.encoding.surface.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.encoding.surface.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.encoding.surface.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.encoding.container.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.encoding.container.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.surface.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.surface.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.surface.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.surface.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.container.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.container.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.gltf.parallel_min_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.gltf.resource_partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.gltf.parallel_min_channel_components = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.inspection.gltf.channel_partition_elements = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.files.io_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.files.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.files.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.files.cancellation_block_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.files.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.surface.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.surface.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.surface.cancellation_block_scalars = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.surface.max_jobs = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.container.parallel_min_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.container.partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.gltf.parallel_min_resource_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.gltf.resource_partition_bytes = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.gltf.parallel_min_channel_components = 0;
    },
    +[](rep::OffGlbSurfaceWorkflowLimits& limits,
        rep::OffGlbSurfaceWorkflowWorkPolicy& work) {
      static_cast<void>(limits);
      work.publication.inspection.gltf.channel_partition_elements = 0;
    },
};
void Configuration(bool unsupported) {
  Fixture fixture(unsupported);
  const auto baseline = fixture.context.ActiveTrackedPayload();
  for (const auto mutate : kZeroMutations) {
    Probe probe;
    probe.stop.request_stop();
    auto limits = f::Limits();
    auto work = f::Work();
    mutate(limits, work);
    const auto result = detail::RunOffGlbSurfaceWorkflowForTest(
        fixture.Request(unsupported), limits, work, probe.stop.get_token(),
        probe.Options());
    Require(result.status == Status::kInvalidConfiguration &&
                result.diagnostic.stage == Stage::kConfiguration &&
                result.diagnostic.code == Code::kRequiredFieldZero &&
                !result.record && probe.backend.calls == 0,
            "every active positive leaf is admitted before stop or allocation, "
            "including unreached nested branches");
    Require(fixture.context.ActiveTrackedPayload() == baseline &&
                !std::filesystem::exists(fixture.destination),
            "configuration refusal has no work or destination");
  }
  for (unsigned kind = 0; kind < 12; ++kind) {
    Probe probe;
    probe.stop.request_stop();
    auto request = fixture.Request(unsupported);
    auto limits = f::Limits();
    auto work = f::Work();
    Code expected = Code::kPolicy;
    switch (kind) {
      case 0:
        limits.max_wrapper_new_requested_bytes =
            std::numeric_limits<std::uint64_t>::max();
        expected = Code::kConfigurationExtent;
        break;
      case 1:
        limits.encoding.max_total_requested_allowance = 1;
        expected = Code::kConfigurationExtent;
        break;
      case 2:
        request.text = ic::OffTextPolicy::kInvalid;
        break;
      case 3:
        request.triangulation_options.max_corners_per_face = 2;
        break;
      case 4:
        request.coordinates = ic::SurfaceCoordinates::kStlUnitsUnspecified;
        expected = Code::kCoordinates;
        break;
      case 5:
        request.numeric.positions = ic::SurfaceNarrowing::kExact64;
        expected = Code::kNumericPolicy;
        break;
      case 6:
        request.numeric.max_position_linf_error = f::kShiftError;
        expected = Code::kNumericPolicy;
        break;
      case 7:
        request.numeric.max_position_linf_error =
            std::numeric_limits<double>::quiet_NaN();
        expected = Code::kNumericPolicy;
        break;
      case 8:
        request.acknowledge_triangle_conversion = false;
        expected = Code::kProjectionAcknowledgment;
        break;
      case 9:
        request.acknowledge_corner_expansion = false;
        expected = Code::kProjectionAcknowledgment;
        break;
      case 10:
        request.input_id = "has space";
        expected = Code::kInputIdentity;
        break;
      case 11:
        request.receipt_path = "SURFACE.GLB";
        expected = Code::kReceiptPath;
        break;
    }
    const auto result = detail::RunOffGlbSurfaceWorkflowForTest(
        request, limits, work, probe.stop.get_token(), probe.Options());
    Require(result.status == Status::kInvalidConfiguration &&
                result.diagnostic.code == expected && !result.record &&
                probe.backend.calls == 0 &&
                fixture.context.ActiveTrackedPayload() == baseline,
            "ordered typed preflight refusal before stopped work");
  }
  for (const std::string_view path :
       {"../receipt.json", "a/../receipt.json", "CON.json", "a\\b", "/absolute",
        "a.", "surface.bin/receipt.json", "surface.json",
        "surface.glb/receipt.json", "a//b", "a%20b"}) {
    auto request = fixture.Request(unsupported);
    request.receipt_path = path;
    const auto result =
        rep::RunOffGlbSurfaceWorkflow(request, f::Limits(), f::Work());
    Require(result.status == Status::kInvalidConfiguration &&
                result.diagnostic.code == Code::kReceiptPath && !result.record,
            "portable receipt alias/prefix/device grammar before work");
  }
  std::stop_source stopped;
  stopped.request_stop();
  const auto canceled =
      rep::RunOffGlbSurfaceWorkflow(fixture.Request(unsupported), f::Limits(),
                                    f::Work(), stopped.get_token());
  Require(canceled.status == Status::kCanceled &&
              canceled.diagnostic.code == Code::kStopRequested &&
              !canceled.record &&
              fixture.context.ActiveTrackedPayload() == baseline,
          "otherwise valid already-stopped call has no state or delegate");
}
void LaterFailures() {
  for (unsigned kind = 0; kind < 7; ++kind) {
    Fixture fixture;
    const auto baseline = fixture.context.ActiveTrackedPayload();
    {
      auto limits = f::Limits();
      auto request = fixture.Request();
      if (kind == 0) limits.triangulation.max_map_entries = 27;
      if (kind == 1) limits.encoding.counts.max_wire_vertices = 17;
      if (kind == 2) limits.inspection.counts.max_members = 2;
      if (kind == 3) {
        std::filesystem::create_directory(fixture.destination);
        f::Write(fixture.destination / "sentinel", "foreign destination");
      }
      if (kind == 4) limits.publication.files.max_caller_report_bytes = 1;
      if (kind == 5) limits.publication.files.max_receipt_bytes = 1;
      if (kind == 6) request.acknowledge_unused_vertices = false;
      const auto result =
          rep::RunOffGlbSurfaceWorkflow(request, limits, f::Work());
      Require(result.status != Status::kSuccess && result.record,
              "actual later failure retains genuine wrapper");
      f::CheckOff(*result.record, f::kMixed, true);
      const auto& facts = result.record->Facts();
      if (kind == 0)
        Require(facts.triangulation &&
                    facts.triangulation->diagnostic.code ==
                        geo::ChargedTriangulationCode::kMapEntryLimit &&
                    !facts.triangulation->triangulation && !facts.encoding,
                "literal sum map cap27 refuses required28 before E");
      if (kind == 1)
        Require(
            facts.encoding &&
                facts.encoding->status == ic::SurfaceStatus::kLimitExceeded &&
                !facts.encoding->output && !facts.inspection,
            "literal W18 exceeds cap17 with OFF and T preserved");
      if (kind == 2)
        Require(
            facts.encoding && facts.encoding->output && facts.inspection &&
                facts.inspection->status == ic::SurfaceStatus::kLimitExceeded &&
                !facts.inspection->inspection && !facts.publication,
            "actual V failure preserves E and OFF report");
      if (kind == 6)
        Require(facts.encoding &&
                    facts.encoding->status == ic::SurfaceStatus::kUnsupported &&
                    facts.encoding->diagnostic.code ==
                        ic::SurfaceCode::kLossNotAcknowledged &&
                    !facts.encoding->output && !facts.inspection &&
                    !facts.publication,
                "conditional unused-row projection acknowledgment remains a "
                "genuine E refusal");
      if (kind >= 3 && kind < 6) {
        Require(facts.inspection && facts.inspection->inspection &&
                    facts.publication && !facts.publication->bundle &&
                    facts.publication->outcome ==
                        ic::SurfaceFilePublicationOutcome::kFailed,
                "actual physical publisher failed after genuine E/V with no "
                "fabricated bundle");
        if (kind == 3)
          Require(facts.publication->diagnostic.status ==
                          ic::SurfaceFileStatus::kDestinationExists &&
                      f::Text(f::Read(fixture.destination / "sentinel")) ==
                          "foreign destination",
                  "existing directory remains byte-for-byte intact");
        else {
          Require(facts.publication->diagnostic.status ==
                          ic::SurfaceFileStatus::kLimitExceeded &&
                      facts.publication->diagnostic.stage ==
                          ic::SurfaceFileStage::kReceipt &&
                      facts.publication->diagnostic.code ==
                          (kind == 4 ? ic::SurfaceFileCode::kCountLimit
                                     : ic::SurfaceFileCode::kReceiptSize),
                  "actual late caller-report/receipt capacity refusal");
          Require(facts.publication->facts.inspection &&
                      facts.publication->facts.inspection->status ==
                          ic::SurfaceStatus::kSuccess &&
                      facts.publication->facts.payload_reread &&
                      facts.publication->facts.provider &&
                      !facts.publication->facts.commit &&
                      facts.publication->cleanup.outcome ==
                          ic::SurfaceFileCleanupOutcome::kRemoved,
                  "physical staged reread/fresh proof precede capacity "
                  "failure; stage is removed without commit");
        }
      }
      if (kind != 3)
        Require(!std::filesystem::exists(fixture.destination),
                "later expected failure exposes no destination");
    }
    Require(fixture.context.ActiveTrackedPayload() == baseline &&
                fixture.context.ActiveWorkers() == 0,
            "later delegate owners release after result destruction");
  }
  Fixture fixture;
  auto limits = f::Limits();
  limits.triangulation.max_map_entries = 28;
  const auto sufficient =
      rep::RunOffGlbSurfaceWorkflow(fixture.Request(), limits, f::Work());
  f::Complete(sufficient, fixture.destination, f::kMixed, true);
}
void BudgetAndAllocation(bool unsupported) {
  Fixture fixture(unsupported);
  const auto baseline = fixture.context.ActiveTrackedPayload();
  {
    auto lease = geo::TryReservePayload(
        fixture.context, static_cast<std::size_t>(f::kLedgerBytes) - baseline);
    Require(lease.status == geo::ReservationStatus::kAccepted,
            "genuine externally occupied shared payload budget");
    const auto result = rep::RunOffGlbSurfaceWorkflow(
        fixture.Request(unsupported), f::Limits(), f::Work());
    Require(result.status == Status::kBudgetExceeded && !result.record,
            "actual full ledger denies wrapper without detached replacement "
            "context");
  }
  for (std::size_t fail_at : {std::size_t{1}, std::size_t{2}}) {
    Probe probe;
    probe.backend.fail_at = fail_at;
    bool propagated = false;
    try {
      static_cast<void>(detail::RunOffGlbSurfaceWorkflowForTest(
          fixture.Request(unsupported), f::Limits(), f::Work(), {},
          probe.Options()));
    } catch (const std::bad_alloc&) {
      propagated = true;
    }
    Require(propagated && probe.backend.calls == fail_at,
            "genuine original backend allocation failure propagates");
    probe.backend.Empty();
    Require(
        fixture.context.ActiveTrackedPayload() == baseline &&
            fixture.context.ActiveWorkers() == 0,
        "unexpected allocation failure releases all started owners/workers");
  }
  if (unsupported) return;
  for (const auto stage : {Stage::kOffWorkflow, Stage::kTriangulation,
                           Stage::kEncoding, Stage::kInspection}) {
    Probe probe;
    probe.target = stage;
    probe.occupy = true;
    probe.context = &fixture.context;
    {
      const auto result = detail::RunOffGlbSurfaceWorkflowForTest(
          fixture.Request(), f::Limits(), f::Work(), {}, probe.Options());
      Require(
          result.status == Status::kBudgetExceeded && result.record &&
              probe.reached[static_cast<std::size_t>(stage)] == 1,
          "real occupied ledger denies next delegate after once-reached stage");
      f::CheckOff(*result.record, f::kMixed, true);
      Require(!std::filesystem::exists(fixture.destination),
              "held budget failure creates no published output");
    }
    probe.held.Reset();
    probe.backend.Empty();
    Require(fixture.context.ActiveTrackedPayload() == baseline &&
                fixture.context.ActiveWorkers() == 0,
            "overlapping actual lease and stage owners release exactly");
  }
}
void CooperativeStops(bool unsupported) {
  if (unsupported) return;
  Fixture fixture;
  const auto baseline = fixture.context.ActiveTrackedPayload();
  for (const auto stage : {Stage::kOffWorkflow, Stage::kTriangulation,
                           Stage::kEncoding, Stage::kInspection}) {
    Probe probe;
    probe.target = stage;
    {
      const auto result = detail::RunOffGlbSurfaceWorkflowForTest(
          fixture.Request(), f::Limits(), f::Work(), probe.stop.get_token(),
          probe.Options());
      Require(result.status == Status::kCanceled && result.record &&
                  probe.reached[static_cast<std::size_t>(stage)] == 1,
              "cooperative stop after a genuine retained delegate");
      f::CheckOff(*result.record, f::kMixed, true);
      Require(!std::filesystem::exists(fixture.destination),
              "cooperative cancellation has no committed destination");
      for (std::size_t later = static_cast<std::size_t>(stage) + 1;
           later < probe.reached.size(); ++later)
        Require(probe.reached[later] == 0,
                "stop prevents subsequent completed stage callbacks");
    }
    probe.backend.Empty();
    Require(fixture.context.ActiveTrackedPayload() == baseline &&
                fixture.context.ActiveWorkers() == 0,
            "canceled genuine owners and worker leases released");
  }
}
struct Workers {
  allocation::NativeWorkerBackend original =
      allocation::DefaultNativeWorkerBackend();
  std::stop_source* stop = nullptr;
  std::size_t attempts = 0, launched = 0, joined = 0, fail_at = 0;
  static std::uintptr_t Launch(allocation::NativeWorkerRecord* record,
                               void* opaque) {
    auto& self = *static_cast<Workers*>(opaque);
    ++self.attempts;
    if (self.attempts == self.fail_at) throw Marker{};
    const auto handle = self.original.launch(record, self.original.state);
    ++self.launched;
    if (self.stop) self.stop->request_stop();
    return handle;
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<Workers*>(opaque);
    self.original.join(handle, self.original.state);
    ++self.joined;
  }
  allocation::NativeWorkerBackend Options() { return {Launch, Join, this}; }
};
void NativeWorkers(bool unsupported) {
  Fixture fixture(unsupported);
  const auto baseline = fixture.context.ActiveTrackedPayload();
  for (const bool partial_failure : {false, true}) {
    Workers workers;
    std::stop_source stop;
    if (partial_failure)
      workers.fail_at = 2;
    else
      workers.stop = &stop;
    rep::off_workflow_detail::WorkflowTestOptions off;
    off.source_worker_backend = workers.Options();
    auto options = detail::WorkflowTestOptions{};
    options.off = &off;
    auto work = f::Work();
    work.off.verification = {1, 1, 1, 64};
    bool propagated = false;
    try {
      const auto result = detail::RunOffGlbSurfaceWorkflowForTest(
          fixture.Request(unsupported), f::Limits(), work, stop.get_token(),
          options);
      Require(!partial_failure && result.status == Status::kCanceled &&
                  result.record && result.record->Facts().off &&
                  !result.record->Facts().triangulation,
              "stop during actual source worker phase retains canceled OFF "
              "delegate");
    } catch (const Marker&) {
      propagated = true;
    }
    Require(propagated == partial_failure && workers.launched > 0 &&
                workers.launched == workers.joined &&
                (!partial_failure || workers.launched == 1),
            "every genuinely launched native worker joins after cancellation "
            "or partial-launch exception");
    Require(fixture.context.ActiveTrackedPayload() == baseline &&
                fixture.context.ActiveWorkers() == 0 &&
                !std::filesystem::exists(fixture.destination),
            "real worker failure releases shared leases and no output");
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == f::kUnsupportedArgument;
    Require(argc == 1 || unsupported, "known request fixture arguments");
    Configuration(unsupported);
    BudgetAndAllocation(unsupported);
    CooperativeStops(unsupported);
    NativeWorkers(unsupported);
    if (!unsupported) LaterFailures();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
