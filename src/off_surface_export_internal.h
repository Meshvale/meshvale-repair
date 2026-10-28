// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_OFF_SURFACE_EXPORT_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_OFF_SURFACE_EXPORT_INTERNAL_H_

#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <meshvale/repair/off_surface_export.h>

#include <filesystem>
#include <stop_token>

#include "materialized_duplicate_allocation.h"
#include "off_workflow_test_internal.h"

namespace meshvale::repair::off_surface_export_detail {
// Test-only adapter entrance to the genuine producer publisher. The adapter
// forwards these unchanged arguments plus borrowed producer fault options and
// returns the actual result unchanged. It must not fabricate owners/results or
// perform fallible work or invoke callbacks after a successful publisher
// return. This entrance is before P; it never runs inside P's atomic final
// window.
using PublicationInvoker = interchange::SurfaceFilePublicationResult (*)(
    const interchange::SurfaceOutputInspection&, const std::filesystem::path&,
    const interchange::SurfaceFilePublicationOptions&,
    const interchange::SurfaceFilePublicationLimits&,
    const interchange::SurfaceFilePublicationWorkPolicy&, std::stop_token,
    void* state);
// Private fault seam. State/backends outlive every returned owner. Checkpoints
// follow immediate retention of OFF/T/E/V and never run after publication.
struct WorkflowTestOptions {
  materialized_duplicates_detail::AllocationBackend allocation_backend =
      materialized_duplicates_detail::DefaultAllocationBackend();
  void (*checkpoint)(OffGlbSurfaceStage, void*) = nullptr;
  void* state = nullptr;
  const off_workflow_detail::WorkflowTestOptions* off = nullptr;
  // Null preserves the direct public publisher call. Adapter state remains
  // readable through synchronous return/all joins; retained backend state must
  // additionally outlive the last corresponding returned owner.
  PublicationInvoker publication_invoker = nullptr;
  void* publication_state = nullptr;
};
[[nodiscard]] OffGlbSurfaceWorkflowResult RunOffGlbSurfaceWorkflowForTest(
    const OffGlbSurfaceWorkflowRequest&, const OffGlbSurfaceWorkflowLimits&,
    const OffGlbSurfaceWorkflowWorkPolicy&, std::stop_token,
    const WorkflowTestOptions&);
}  // namespace meshvale::repair::off_surface_export_detail
#endif  // MESHVALE_REPAIR_SRC_OFF_SURFACE_EXPORT_INTERNAL_H_
