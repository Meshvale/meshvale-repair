# Retaining completed repair facts after a missing glTF receipt

| Field | Value |
|---|---|
| ID | REPAIR-GLTF-FACTS-001 |
| Version | 0.2.0 |
| Status | Development implementation; public qualification pending |
| Owner | Repair workflow result ownership and live requested-byte attribution |

This additive interface lets a caller retain an actually completed duplicate
repair when a later expected failure prevents a memory-workflow record. It uses
the existing [memory workflow](gltf-workflow.md),
[retained-file workflow](gltf-file-workflow.md) and
[immutable repair owner](materialized_duplicates.md). It adds no report generator,
publication path, processing retry or accepted-candidate inference.

## GRF-RETENTION-001: Explicit invocation policy

Append `GltfReportFailureRetention report_failure_retention` to both workflow
requests, defaulting to `kRelease`. The other enumerator is
`kRetainCompletedRepair`. Validate unknown enumerators as invalid configuration
before work. The file operation forwards the policy once to its existing memory
operation. No execution-session setting changes the default.

`GltfDuplicateWorkflowResult::retained_repair` is an optional
`MaterializedDuplicateRepair`. It is present only for opt-in calls with no
ordinary memory record and an actual completed Repair owner. A rejected repair
or a failure before completed repair does not create one. A single nonthrowing
transfer covers both early stopped return and the common expected-failure tail.
Unexpected exceptions preserve joining, cleanup and propagation.

The file operation copies that owner into its already allocated state before its
temporary memory result dies. `GltfDuplicateFileWorkflowRecord::RepairFacts()`
returns a pointer to the normal memory record's Repair owner, otherwise the
retained fallback, otherwise null. The two owner paths are mutually exclusive.
`MemoryOutcomes()` exposes the already retained optional actual inner outcomes,
including when `Memory()` is absent. It is absent when the inner operation never
returned. It does not infer inner stages from an outer diagnostic.

All returned pointers/views borrow an alive, unreassigned record. Separate
record copies support concurrent reads; assignment of one copy requires external
synchronization. Strong copies share payload without allocation and survive
source asset, request/options and execution-session destruction. Injected
allocator callback state remains borrowed and must survive the final matching
free. Production delegates retain their documented stateless backends.

## GRF-BYTES-001: Retained subset and shared ledger

Expose `GltfRetainedRepairRequestedBytes` with `source_geometry`, `repair` and
`total`. Their values are exactly:

```text
source_geometry = repair.Source().NewRequestedBytes()
repair = repair.NewRequestedBytes()
total = source_geometry + repair.total
```

The file record's requested-byte observation adds an optional `retained_repair`
field for this fallback. It is absent with an ordinary memory record or no
fallback. Existing `memory` fields still count only surviving memory-record
owners. Zero memory-record attribution does not mean zero retained payload.
The sum follows checked admitted-size reasoning; it does not relax any cap.

These are current live attributed requests, including the source Geometry
operation and Repair's selection, maps and two inspections. Candidate operation
bytes are already in selection and must not be added again. Released Interchange
bridge, extraction, read, writer/reload and workflow report owners contribute
nothing to this subset. Retained original file source and wrapper charges remain
separate. There is no end-to-end total, peak, cumulative-traffic or RSS guarantee.
Shared copies and overlapping result counters are not additive process totals.

Retained payload keeps its original execution ledger and allocation backend.
Several failed results can consume later operation headroom. Dropping the last
owning copy releases its charges; dropping only the execution session does not.
Larger charged workflow state remains subject to its original request admission.
Caller result slots and Python prepared-object storage retain their original
exclusion scopes; no new charge is claimed for stack storage or Python objects.

## GRF-RECEIPT-001: Unchanged result meanings

Status, actual outcomes, diagnostics, profile and receipt availability retain
their owning workflow meanings. Retaining facts does not create a workflow
record, promote any check to complete, or populate `ReportJson()`. Available
native bytes remain authoritative and unchanged. No partial success receipt is
published. A caller may later use real retained counts/maps in its own explicitly
identified report; it must not claim a missing native check or receipt existed.

Valid completed meshes expose vertex, face and corner counts from
`positions.size()/3`, `face_offsets.size()-1` and `corner_vertices.size()`.
Face/corner maps and inverse selection maps come from the retained Repair owner.
Source and candidate views retain their Geometry contracts. Counts alone do not
satisfy Geometry's accepted-candidate report correspondence requirements.

Transfer is shared-copy, allocation-free and nonthrowing. Lazy language-binding
observations can allocate after return. There is no emergency report guarantee
under Python or native allocation failure.

## Qualification requirements

Keep default-mode late-cancellation/no-record/zero-fresh-payload controls intact.
Require opt-in changed/unchanged faults at real report cancellation, extent and
admission refusal, early stop and common expected-failure exits. Preserve original
outcomes and missing reports; verify exact real counts/maps, pre-repair absence
and ordinary/fallback exclusivity. Test original backend lifetime, multiple
copies/results, exact ledger delta, later refusal and last-copy release. Observe
the real handoff under request refusal and assert nothrow transfer/destruction.
Requalify enlarged storage boundaries, unexpected exception joins, installed
consumers and the compiled Python return observer before acceptance.

## RGDEF-RETENTION-PROJECTION-001: Retained deformation selection

The [retained deformation contract](gltf-deformation.md) selects the opt-in
deformation-retained-v1 extension for called-failure and completed-Repair retention.
Development implementation is present; full workflow and dependency-cohort
qualification and release availability remain pending. Existing static/direct-translation clauses, defaults and
unchanged-request wire keep their current meaning. The new contract owns the
additional source/read/processing authority, policies, roles and called facts.
There is no implicit source upgrade or widening of a legacy schema/preset.
