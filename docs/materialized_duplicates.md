# Charged targeted duplicate-face repair

| Field | Value |
|---|---|
| ID | MESHVALE-MATERIALIZED-DUPLICATES-001 |
| Version | 0.1.1 |
| Status | Declaration-only proposal; runtime, installed target and qualification pending |
| Owner | Repair |

The proposed
[`materialized_duplicates.h`](../include/meshvale/repair/materialized_duplicates.h)
interface applies targeted exact duplicate removal directly to immutable
Geometry storage. The existing [duplicate contract](duplicates.md#request-and-equivalence)
owns equality and attribute eligibility. This document owns charged execution,
immutable result ownership and the new interface's failure ordering. It does not
change `remove_duplicate_faces` or claim a callable implementation.

## Input and operation

`RemoveMaterializedDuplicateFaces` accepts a `MaterializedMesh`, a borrowed span
of the existing `DuplicateTarget` records, borrowed row-local declarations,
explicit limits and the Geometry work policy. It reads `MeshReadView` directly
and delegates retained face construction to `SelectMeshFaces`. It does not
export a mutable `Mesh`, invoke the legacy operation or stage numerical payloads
in another representation.

The source owns the execution ledger. A prerequisite additive Geometry
`MaterializedMesh::ShareExecutionContext() noexcept` returns a strong context
copy without allocating or exposing a raw identity. The proposed operation uses
that context and a fresh per-call stop token; it takes no substitute caller
context. The original source context and earlier stop source may already have
been destroyed. Cancellation of an earlier capture does not cancel this call.
Geometry owns the [materialization and selection contract](https://github.com/Meshvale/meshvale-geometry/blob/main/docs/materialization.md)
and [execution lease contract](https://github.com/Meshvale/meshvale-geometry/blob/main/docs/execution.md).

Borrowed target/declaration records and declaration name bytes must remain
readable and unchanged until the synchronous call returns. Caller-owned ingress
storage is outside new-allocation accounting. A row-local declaration uses the
same domain/name identity as the existing attribute policy. Foreign declaration
domains, empty names and repeated domain/name declarations are invalid
configuration. A valid declaration that names no source channel has no effect.
Foreign domains in raw mesh storage instead remain Geometry storage defects.
After declaration count admission, a checked sum of all borrowed declaration
name lengths must not exceed the required inclusive
`max_declaration_name_bytes`, before any name content is traversed. This sum
reads fixed-width span/string-view descriptors; embedded zeros and repeated name
bytes count in full. An overflowing sum or an exceeded cap returns
`kLimitExceeded/kDeclarationNameByteLimit`.

Every source attribute participates under the existing contract, including
secondary sets, variable-length rows and vertex attributes. Authored floating
attribute rows must be finite even for an empty target request; missing rows'
backing values remain eligible for exact retention, including nonfinite bits.
Finiteness classification must not quiet signaling NaNs. Unknown semantics
require explicit row-local declarations only for a nonempty edit. Equality of
missing rows ignores their backing row lengths and payloads; independent retained
row verification preserves those lengths and every bit. Eligibility compares
signed zero numerically; retained output preserves the chosen representative's
bits. There is no normalization, tolerance or geometry generation.

## Limits and execution

Every field of `MaterializedDuplicateLimits`, its nested Geometry limits and
the work policy must be positive and fit the host extent. Count limits are
inclusive. Empty target/declaration spans remain valid under positive limits.
The nested inspection limits apply separately to complete source and candidate
inspection. Selection count limits retain their Geometry meanings.

`max_comparison_steps_per_target` admits a checked worst-case logical work bound
before comparisons launch. A vertex-index comparison, presence comparison,
row-length comparison or scalar comparison each contributes one step. Each
face-domain row contributes a presence step and, when authored, one row-length
step plus its representative scalar count. For each rotation, add the loop's
vertex count and the corresponding sum for every representative corner row.
Multiply that rotation bound by the loop length, then add the face-row bound
and one loop-length comparison step.
Missing representative rows contribute only presence steps. The representative
row scalar count bounds actual scalar comparisons even if the other row's length
differs; early exits can perform less work. This is a conservative algorithmic
bound, not an allocation estimate or measurement. Exceeding it is
`kLimitExceeded/kComparisonLimit` for that target before comparisons, not evidence
that the faces differ. Arithmetic is checked before overflow. Speculative later
rotation evaluation cannot change cap admission or the first matching rotation.

Substantial authored-value scans, directed cyclic comparisons, declaration-name
equality/duplicate checks and independent candidate/correspondence verification
partition readable work using the supplied
byte thresholds, partition size, cancellation block and job limit. A large
single target can partition its rotation candidates; the result still selects
the first matching rotation. Ordered fixed-width planning is a serial one-pass
O(record count) phase that assigns source ordinals and checked prefix ranges
needed for deterministic reductions and dependent map writes. It copies neither
numerical payloads nor name/semantic/metadata bytes. Count caps do not imply that
records are small; this phase checks cancellation in bounded record blocks.
Substantial name/semantic/metadata byte scanning, equality and descriptor-byte
verification use byte-aware workers and bounded checkpoints, rather than long
uninterruptible serial `string_view` comparisons. Numeric payload comparison
and verification are not reduced to a serial metadata-only substitute. Original
target order, source attribute order, row order and rotation order determine
outcomes, not scheduling.

Workers acquire slots through Geometry's shared ledger. Insufficient slots use
caller execution. Every actual launch joins before return, exception, slot
release or callable destruction, including partial launch and cancellation.
Execution fields report actual phase work, caller jobs and launched/joined
workers; planned jobs are not proof of a launch. No throughput claim follows
from partitioning. Meshvale's callable records, handles and scratch are charged;
opaque OS thread/CRT internals retain the execution contract's exclusions.

## Aggregate requested-block cap

`max_new_requested_bytes` bounds the maximum simultaneously live requested
blocks newly created by this repair, including Repair allocations and delegated
source inspection, selection and candidate inspection. It is a peak admission
cap, not cumulative allocated bytes, logical scalar bytes, RSS or process heap.
All still-live source allocations and concurrent foreign operations also count
against the common Geometry ledger, without being charged again to this cap.

Repair owns admission of its complete allocator requests before construction:
accounting seed, headers, alignment padding, rebound/control blocks, target
planning, descriptors, worker records, counters, scratch and correspondence.
Old/new replacement overlap stays charged. Canonical numerical requests remain
owned and planned by Geometry's compiled Eigen storage; Repair introduces no
replacement allocator, thread-local hook or guessed numeric payload reservation.

Delegated phases are synchronous barriers: Repair allocation growth is frozen
while a Geometry operation runs. Its effective `max_new_requested_bytes` is the
smaller of the caller's positive owning Geometry phase cap and the aggregate cap
remaining after live Repair requests and other retained delegated operations.
If no positive remainder remains, Repair returns `kBudgetExceeded/kRequestBudget`
without passing zero as a Geometry configuration. After return, every later
Repair admission includes the delegates' live `NewRequestedBytes()` values.
Repair retains those owners while measuring their charges; their storage cannot
disappear through caller handle destruction. No delegated call runs concurrently
with another delegated phase of this operation. Independent operations remain
concurrent through the shared ledger.

Each delegated operation is counted once: the selection and its copied candidate
share a counter, while candidate inspection has a separate counter. Derivation
uses checked subtraction and retained owners. No diagnostic/summary allocation
occurs during a delegated phase. The accounting seed and result ownership are
included in Repair's own contribution before delegation/publication.

`NewRequestedBytes()` returns a live split for Repair, source inspection,
selection and candidate inspection, plus their checked total. It excludes
original source charges and finished scratch. The retained source/selection may
share unchanged vertex blocks, which belong to their original allocation
operation and are not double charged. Destroy storage before releasing its
charge; final destruction and owner copies allocate nothing and ignore stop.

## Result, correspondence and verification

`kSuccess` contains a copy-only strong `MaterializedDuplicateRepair` owner.
`Outcome()` is the existing `accepted` or `unchanged` value. Empty targets yield
an unchanged selected candidate with identity input/output maps. The candidate
retains its own immutable face/corner storage and shared unchanged vertex part;
it need not duplicate source numerical owners to satisfy ownership. Rejected
requests contain no candidate, correspondence or partial inspection owner.

The owner exposes its original source, complete source and candidate inspections,
candidate and correspondence. `FaceMap()` and `CornerMap()` map every input
element to an output element, including removed copies. `SourceFaces()` and
`SourceCorners()` expose Geometry's output-to-retained-input correspondence.
Surviving rows preserve source order. Borrowed references/spans require the
visible owner to remain alive and unreassigned; owner copies share blocks and
charges without allocation and support independent concurrent reads. Copying
out a candidate retains that candidate but does not own Repair's maps.

Before publication, independent verification reads the actual candidate and all
maps. It checks storage, authored finiteness, unchanged vertex identities and
position bits, every retained loop, descriptor/name/semantic/set/metadata byte,
scalar type, optional dense/ragged/presence representation and complete retained
row bits. It separately checks every original face/corner through the many-to-one
maps using duplicate eligibility, including rotational alignment. Verification
uses source/candidate views rather than construction scratch as expected values.
Failure yields `kVerificationFailure/kPostcondition` with no published owner.
This establishes the existing narrow preservation conditions, not geometric
validity or edited asset writing.

## Failures and order

Validation order is required configuration; target/declaration count limits;
checked declaration-name byte admission; declaration shape; complete Geometry
source inspection; authored floating finiteness
in source attribute/row order; unknown semantic policy in attribute order for
nonempty edits; target shape in request order; removed representatives in request
order; exact comparisons in request order; selection; candidate inspection;
independent verification; final publication checkpoint. Within target shape,
range precedes self-target and repeated removal. One failure rejects the whole
request. A successful source inspection with issues is
`kInvalidSourceStorage/kStorageIssues`, preserving its actual Geometry `kSuccess`
and diagnostic together with the first typed `StorageIssue`.

Genuine delegated failures use `kGeometryFailure/kGeometry` and retain the exact
Geometry status and diagnostic; Repair does not relabel unsupported numerical
profiles, issue limits or Geometry budget rejection. Repair-owned count/work
limits, aggregate-cap/ledger rejection and cancellation use their own typed
statuses. Invalid targets, unsupported semantics, authored nonfinite values or
non-equivalent targets use `kRejected` with the first typed reason. Diagnostics
contain source/request ordinals, not borrowed or fabricated names.

Cancellation is checked before allocation and within bounded descriptor,
comparison and verification blocks, and after joins before publication. An
already discovered validation failure is preserved. If a parallel failure needs
an earlier source-order prefix to determine its first diagnostic, workers join
and only that necessary prefix is finalized without allocation before rejecting.
This does not resume mutation or publish success after cancellation. Actual
allocation, launch, synchronization and other runtime exceptions propagate
after rollback and joins, taking precedence over cooperative cancellation.

## Required qualification

The candidate needs actual native and installed public consumers against the
matching Geometry producer, native/ASan/UBSan/TSan gates, charged original-route
and fault observations, exact-cap/one-byte-under tests, foreign ledger contention,
partial launches, source-order cancellation and retained lifetime checks. Semantic
fixtures must independently cover cyclic/repeated loops, opposite winding,
missing backing payloads, signed zero, all seven canonical scalar types,
secondary channels, ragged rows and exact all-element maps. Substantial comparison
and verification cases must demonstrate positive actual launches and joined
counts as well as serial/parallel result equivalence. Qualified numerical
profiles and unsupported-profile behavior remain Geometry-owned. No such
qualification is established by this declaration-only draft.
