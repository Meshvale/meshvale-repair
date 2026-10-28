# OFF repair to annotated GLB

| Field | Value |
|---|---|
| ID | REPAIR-OFF-SURFACE-001 |
| Version | 0.2.0 |
| Status | Draft contract; implementation and dependency selection pending |

This proposed opt-in workflow repairs explicit duplicate OFF faces once, verifies
the polygon candidate, and publishes a new annotated GLB directory. It composes
the existing [OFF workflow](off-workflow.md), [charged repair](materialized_duplicates.md),
Geometry charged triangulation and Interchange Surface encoding, independent
inspection and file publication. It selects no installed API yet. The names and
requirements below are the interface to implement after independent review and
acceptance of the required producer main. Existing OFF/OBJ/PLY/glTF APIs, command
names, defaults and producer pins remain unchanged.

The proposed native option is `MESHVALE_REPAIR_OFF_SURFACE_EXPORT=ON` (default
OFF). Native composition requires matching Geometry/Interchange development
SDKs exporting `meshvale::charged_triangulation`, `meshvale::surface_output`
and `meshvale::surface_files`, alongside the OFF dependencies. Equal development
version strings alone do not prove feature or ABI compatibility. The later
implementation gate must select source identities and actual installed feature
checks. Native SDK identity is separate from Python owned-record dependencies;
this draft selects no change to existing Python package pins, requires no
Interchange Python extension, and does not claim native features are exposed by
an older Geometry Python wheel. The new opt-in binding and its affected wheel
must be tested deliberately with their chosen native and Python dependencies.

## OSG-REQUEST-001: One explicit route

The only output is `SurfaceEncoding::kGlb` with
`SurfaceRepresentation::kAnnotatedBundleV1`. The ordinary member is `surface.glb`;
`surface.json` and `surface.bin` retain the annotated representation. The
publisher adds its separate receipt, default `publication.json`. A destination
directory is required and must not exist. There is no overwrite, memory-only
export switch, format detection, alternate encoding or silent fallback.

OFF provides positions and indexed polygon loops, not normals, UVs, materials,
units or a coordinate basis. Require an explicit
`SurfaceCoordinates::kDeclaredMetersRightHandedYUp` declaration: the supplied
values already have that meaning. Unknown units/basis or a needed transform are
refusals, not an invitation to infer, rescale or change basis. Use empty channel,
UV, material and catalog-UV spans and a null catalog. Emit no generated normals,
materials, image resources or additional authored scene semantics. Allow the
producer-required GLB geometry buffer and minimal static scene/node/mesh
structure. Genuine empty output retains its required empty default scene and
JSON-only GLB without fabricated geometry. Direct OFF-to-STL is outside this contract;
the current attribute-free STL route retains its genuine `NormalAbsent` refusal,
including empty meshes.

The caller selects `SurfaceNarrowing::kExact32` or `kBoundedRne32`. Exact32
requires a finite nonnegative zero error bound; bounded-RNE32 requires an explicit
finite nonnegative position L-infinity bound. UV bounds are an empty span. The
existing finite/range/nonzero-underflow/exactness/error/new-coincidence/
collapse/orientation checks remain authoritative. No epsilon, welding,
normalization, host floating conversion or alternate numeric fallback is added.
Triangle conversion and corner expansion require explicit true acknowledgments,
even for empty output. Unused rows require explicit acknowledgment if any source
vertex has no wire row. The convenience interfaces require this acknowledgment
explicitly before capture as well, so preparation never guesses mesh contents.

`text = kCanonicalAscii` and `edge_count = kUnknownZero` explicitly acknowledge
the existing OFF memory verification rewrite. The workflow does not publish that
OFF output. Input IDs retain the existing ASCII report grammar and limit.

## OSG-NATIVE-001: Native interface and retained observations

Proposed owning header: `meshvale/repair/off_surface_export.h`; target:
`meshvale::repair_off_surface_export`, default OFF, requiring the existing OFF
workflow and the reviewed Interchange Surface output/files targets. This is a
future build interface, not a dependency-version selection.

```cpp
struct OffGlbSurfaceWorkflowRequest {
  const interchange::OffFileAsset& source;
  std::string_view input_id;
  std::span<const DuplicateTarget> targets;
  const std::filesystem::path& destination;
  interchange::OffTextPolicy text;
  interchange::OffEdgeCountPolicy edge_count;
  interchange::SurfaceCoordinates coordinates;
  interchange::SurfaceNumericPolicy numeric;
  bool acknowledge_triangle_conversion;
  bool acknowledge_corner_expansion;
  bool acknowledge_unused_vertices;
  geometry::TriangulationOptions triangulation_options;
  std::string_view receipt_path =
      interchange::kDefaultSurfacePublicationReceiptPath;
};
struct OffGlbSurfaceWorkflowLimits {
  std::uint64_t max_wrapper_new_requested_bytes = 0;
  OffDuplicateFileWorkflowLimits off;
  geometry::ChargedTriangulationLimits triangulation;
  interchange::SurfaceEncodeLimits encoding;
  interchange::SurfaceInspectLimits inspection;
  interchange::SurfaceFilePublicationLimits publication;
};
struct OffGlbSurfaceWorkflowWorkPolicy {
  OffDuplicateFileWorkflowWorkPolicy off;
  geometry::MeshMaterializationWorkPolicy triangulation;
  interchange::SurfaceWorkPolicy encoding;
  interchange::SurfaceWorkPolicy inspection;
  interchange::SurfaceFilePublicationWorkPolicy publication;
};
enum class OffGlbSurfaceStage {
  kConfiguration, kOffWorkflow, kTriangulation, kEncoding,
  kInspection, kPublication, kComplete
};
enum class OffGlbSurfaceStatus {
  kSuccess, kInvalidConfiguration, kLimitExceeded, kBudgetExceeded,
  kCanceled, kRejected, kUnsupported, kDelegatedFailure
};
enum class OffGlbSurfaceCode {
  kNone, kRequiredFieldZero, kConfigurationExtent, kPolicy,
  kCoordinates, kNumericPolicy, kProjectionAcknowledgment, kInputIdentity,
  kReceiptPath, kRequestBudget, kStopRequested, kOffWorkflow,
  kTriangulation, kEncoding, kInspection, kPublication
};
struct OffGlbSurfaceDiagnostic {
  OffGlbSurfaceStage stage = OffGlbSurfaceStage::kConfiguration;
  OffGlbSurfaceCode code = OffGlbSurfaceCode::kNone;
};
struct OffGlbSurfaceFacts {
  std::optional<OffDuplicateFileWorkflowResult> off;
  std::optional<geometry::ChargedTriangulationResult> triangulation;
  std::optional<interchange::SurfaceEncodeResult> encoding;
  std::optional<interchange::SurfaceInspectionResult> inspection;
  std::optional<interchange::SurfaceFilePublicationResult> publication;
};
class OffGlbSurfaceWorkflowRecord {
 public:
  OffGlbSurfaceWorkflowRecord(const OffGlbSurfaceWorkflowRecord&) noexcept;
  OffGlbSurfaceWorkflowRecord& operator=(
      const OffGlbSurfaceWorkflowRecord&) noexcept;
  ~OffGlbSurfaceWorkflowRecord() noexcept;
  const interchange::OffFileAsset& Source() const noexcept;
  const OffGlbSurfaceFacts& Facts() const noexcept;
  std::uint64_t NewRequestedBytes() const noexcept;  // wrapper-only live scope
  // Private construction from a strong, charged immutable state only.
};
struct OffGlbSurfaceWorkflowResult {
  OffGlbSurfaceStatus status = OffGlbSurfaceStatus::kInvalidConfiguration;
  OffGlbSurfaceDiagnostic diagnostic;
  std::optional<OffGlbSurfaceWorkflowRecord> record;
};
OffGlbSurfaceWorkflowResult RunOffGlbSurfaceWorkflow(
    const OffGlbSurfaceWorkflowRequest&, const OffGlbSurfaceWorkflowLimits&,
    const OffGlbSurfaceWorkflowWorkPolicy&, std::stop_token stop = {});
```

The declaration outline specifies the complete visible fields, enumerators and
methods, not an implemented header or an enum wire-ordinal promise. The owner has
no public default constructor, caller-supplied proof constructor or move-empty
state. Views borrow a live unreassigned copy. Copies allocate nothing and keep
the original allocator/backend state alive until its last actual free. Distinct
copies permit concurrent reads; assignment/destruction cannot race access to the
same visible handle or its borrowed views.

Before the OFF call allocate final wrapper state on the source's actual ledger
and retain the captured source. After this succeeds, every ordinary expected
return exposes a record, including a stopped or rejected operation. A
configuration refusal before state construction has no record. A missing
optional delegate means **not invoked**. A present delegate is its exact actual
result, including partial facts and absent owners; do not turn a failed call
into an absent call. Delegate diagnostics remain in their own typed domains.
An already stopped, otherwise valid call may return Canceled before state
construction with no record; cancellation after construction retains the record.

## OSG-ORDER-001: Admission, one repair and strong lifetime

Validate in this order before stop, allocation, source traversal or stage
creation: all required active limit/work leaves and checked host extents;
delegated allowance sums; the fixed OFF/GLB/annotated policies and triangulation
option extents; coordinate selector; numeric mode/bound/empty UV bounds;
projection acknowledgments; input-ID grammar; portable receipt path. For ties
within a nested object retain that owner's first-invalid order. A known selector
outside this route is a policy refusal. Do not drop a zero or unknown field from
validation because a later stage might not run. Receipt path admission uses the
publisher's portable ASCII segment, alias/prefix/device/dot/trailing rules;
destination native Unicode admission stays with the file publisher.

After valid configuration, honor stop; derive the ledger from
`request.source.Document().Mesh().ShareExecutionContext()`. Do not create a
replacement context or merely compare budget numbers. Call
`RunOffDuplicateFileWorkflow` exactly once with the actual source/targets and
`publication = std::nullopt`, passing `limits.off`/`work.off` unchanged. Its
canonical OFF write/reload, maps, captured-source protection and report remain
real operations. Store its returned result immediately by nonthrowing handoff,
before any subsequent fallible work. Do not retry repair after any later failure.

Continue only when the OFF result succeeds, has its retained record, passed
memory verification/profile and actual `RepairFacts()` whose outcome is
`accepted` or `unchanged`. Otherwise retain the exact OFF status/outcomes and
do not invoke triangulation. Empty targets still invoke the real unchanged
operation; true empty input follows actual capability results, not fabricated
vacuous delegates. A rejected repair remains rejected, not export-unsupported.

Let **O** be the captured OFF mesh and **R** its accepted polygon candidate.
Call retained `TriangulateCharged(R, ...)`, then `EncodeSurface` with
`original = RepairFacts()->CandidateInspection()` and its actual accepted charged
triangulation. All handles share the same genuine ledger. The capsule's
`original` is **R**, not O. The OFF record/report retains O, O→R face/corner maps
and inverse retained rows; Surface retains R→triangles→wire maps and their CSR
inverses. Preserve these separate scopes. Do not relabel a Repair map as an
O→wire map or rerun triangulation to obtain a preferred ear order.

Require actual successful E with output before creating supplied-member views
over its real `Members()`. Call `InspectSurfaceOutput` once on these actual bytes.
Require its genuine successful proof before `PublishSurfaceBundle`. The latter
performs its own distinct physical reread/fresh inspection and closed inventory.
The first V facts and the publisher's fresh V facts stay separately reached and
separately charged. Staged inspection is not replaced by the memory proof.
Prepare every outer owner/result slot before publication. Store P's actual
return by allocation-free, nonthrowing handoff; perform no mandatory native
serialization, callback, hash, owner allocation or reread after commit. Retain
the necessary genuine owner/backend and inert stop-state lifetimes so local
temporary release cannot become an unplanned final deallocation on that path.
Caller-requested Python observation/framing after native return is separate
delivery and can fail without rewriting publication.

Native calls are synchronous; borrowed source, paths, options, spans, tokens and
provider backing stay readable/unassigned through return and all joins. Workers
use the shared executor/leases and bounded work policies; no uncharged Python
worker pool, independent executor, global cache or hidden default allocation is
permitted. Every launched worker joins on all paths. Overlapping retained OFF,
Repair, triangulation, E, V and P owners continue to occupy the root ledger.
Wrapper storage, control blocks, member-view plans, strings and temporary
handoff records obey `max_wrapper_new_requested_bytes`. An allocation-budget
denial is BudgetExceeded without guessing operation-versus-root attribution.
Copying aliases adds no retained charge; do not sum alias getters as a peak/RSS.

Unexpected allocation, engine, native/thread or programming exceptions propagate
after joins and identity-safe staged cleanup; no emergency result/report is
promised. Only exceptions from an actual external receipt callback receive the
publisher's documented callback treatment. The internal provider performs no
Repair, encoding, report regeneration, Python callback or new ledger creation.

## OSG-REPORT-001: Repair facts survive export failure

Store each actual delegate result before subsequent fallible work. For every
later expected triangulation/encode/inspect/publish refusal, cancellation or
delivery failure, the retained OFF result, accepted candidate, maps and original
report remain available. A report refusal in the OFF operation is an actual OFF
failure and blocks export; retained Repair facts do not override it. Missing
report bytes do not erase completed Repair facts. A wrapper exception may prevent
a return value; caller-owned captured inputs remain their own surviving owners.

The original `meshvale.report/1` bytes are never patched or reserialized. Their
OFF publication remains `kNotRequested`; successful memory verification has no
OFF exported-output snapshot/artifact. Do not rewrite its candidate or passed
OFF profile because GLB export later fails. GLB conversion does not claim the
original input was already triangular, that ordinary GLB preserves polygons,
that a mesh is solid/manifold, or that rendering/shading/units were inferred.

The publisher's synchronous provider returns exactly the successful OFF
`ReportJson()` span as uninterpreted valid UTF-8 caller text. Its state strongly
retains the OFF record and backing through import, joins and cleanup. It must
not supply a guessed receipt: Interchange generates the neutral native receipt
and owns its reserved payload fields/hashes. The receipt records only reached
precommit payload facts; no self-hash, receipt-own-write prediction, commit or
cleanup prediction is inserted. The published owner/typed P facts expose actual
receipt verification, final group, commit and cleanup. No application callback,
allocation, report patch or mandatory reread follows native commit. A failed
publication exposes no published owner or conditional success receipt bytes.

Overall status is Success only for actual `kPublished` with a genuine bundle;
its diagnostic is Complete/None. A failed delegated call maps wrapper stage to
the call made and code to its matching delegate name. Preserve actual canceled,
budget and limit failures as those wrapper statuses; Repair rejection is
Rejected; genuine unavailable/numeric-profile capability is Unsupported; other
typed delegate failures are DelegatedFailure. Configuration errors use the
ordered wrapper codes above. Cleanup never replaces the primary diagnostic.

Apply one exit merge rule everywhere: 130 dominates 2, which dominates 1,
which dominates 0. Actual native/capture cancellation contributes 130; an
invocation SIGINT observed at an interpreter boundary also contributes 130
without changing a completed native outcome. Expected framing/stdout failure
contributes 2. A canceled operation plus delivery failure therefore exits 130,
not 2. Rejected/blocked/unsupported results contribute 1 under the exact table
below; actual published success contributes 0. Unattempted rows contribute
nothing. A committed bundle, P facts and receipt bytes never change because of
later cancellation observation or delivery failure.

## OSG-PRESETS-001: Complete finite application configuration

[off-surface-export-presets.json](off-surface-export-presets.json) owns the two
fully expanded proposed application configurations `balanced-v1` and
`serial-reference-v1`. They are development contract values, not measured
capacity, a performance promise or a selected runtime floor. Native callers
still supply explicit structures. Python preparation copies a named preset once
and rejects unknown names/overrides. No automatic cap growth or serial retry is
allowed after refusal.

The JSON paths `capture_limits`/`capture_work` name `OffFileReadLimits` and
`OffFileReadWorkPolicy`; `limits`/`work` name the native aggregates above.
`execution` initializes the existing shared Geometry execution session for
capture; an explicitly supplied compatible execution session remains its actual
ledger with actual pressure. `triangulation_options` supplies the request's
options. All active integer leaves are positive and inclusive. Canonical
inactive branches are **explicit zero**, not missing: encoding `work.gltf` and
`work.stl`; inspection/fresh-V `extraction.direct_translation`,
`work.gltf.direct_translation` and `work.stl`. No STL delegate variant exists.
These exceptions follow Surface admission and do not waive positive active caps.
The existing OFF memory workflow still validates its positive unused OFF
publication limits/work; pass them unchanged although no OFF publication runs.

Checked containment, without double-counting nested totals:

```text
GLB E allowance >= E own cap + E container request cap
GLB V allowance >= V own cap + V container cap + read request cap + extraction cap
P allowance >= P file own cap + its DISTINCT fresh-V allowance
```

Each selected preset uses E allowance 384 MiB, V allowance 640 MiB and P allowance
896 MiB. Initial V and physical V use equal-valued configurations but distinct
operations/owners. OFF delegate sums and their limits remain as in the complete
expanded table. The common 2 GiB root budget is not a reservation or a promise
that every count/byte maximum fits simultaneously. No constituent cap exempts
another scope or existing same-ledger occupancy. Attribute-related count caps
remain positive even though OFF has zero actual attributes. Required map,
comparison, JSON-depth/integer and resource caps apply even where actual counts
are zero. Triangulation supports only its existing exact single-loop profile,
at most 4096 corners/face; the OFF parser's larger face cap does not bypass that
actual conversion refusal.

Both presets require checked 64-bit host representation before native allocation.
Serial-reference changes worker budget/max_jobs to one and actual parallel
thresholds to signed-64 maximum, including minimum parallel faces/vertices;
caps, cancellation blocks, partition sizes, algorithms and expected facts match.
`max_jobs` bounds one actual executor invocation/group, not the sum of all phase
invocations. Final P execution views are nonadditive aliases where the publisher
uses one prepared final group. A sequential codec/SHA stream has no invented
worker launches. Preset names do not imply that jobs actually parallelized.

## OSG-PYTHON-001: Prepare once, execute once

Proposed opt-in module `meshvale_repair.off_surface_export` exposes:

```python
prepare_off_glb_export(source, destination, targets, *, text, edge_count,
    coordinates, precision, max_position_error,
    acknowledge_triangle_conversion, acknowledge_corner_expansion,
    acknowledge_unused_vertices, input_id="source",
    receipt_path="publication.json", preset="balanced-v1") -> PreparedOffGlbExport
run_off_glb_export(prepared, *, execution=None, cancellation=None) -> OffGlbExportResult
repair_off_to_glb(source, destination, targets, *, text, edge_count,
    coordinates, precision, max_position_error,
    acknowledge_triangle_conversion, acknowledge_corner_expansion,
    acknowledge_unused_vertices, input_id="source",
    receipt_path="publication.json", preset="balanced-v1",
    execution=None, cancellation=None) -> OffGlbExportResult
```

It also exports `OffGlbExecution(*, preset="balanced-v1")`, with readonly
`preset`, `resolved_preset`, `worker_budget`, `active_workers` and
`active_tracked_payload`, plus the existing `OffCancellation` type (`cancel()`
and readonly `cancelled`). An explicit execution must be an `OffGlbExecution`
whose preset name/configuration matches the prepared operation; otherwise
admission fails before capture. With `execution=None`, create one session from
that operation's selected preset before capture and retain its ledger through
all returned owners. This does not change `OffExecution` or its legacy presets.
Batch default sessions are per attempted asset, without an aggregate cross-asset
memory-budget promise.

`repair_off_to_glb` is exactly prepare then run. Preparation owns converted
paths/targets/options/configuration and the final result holder before reading
source contents; it performs no capture, native operation, directory creation or
publication. `source`/`destination` accept `str` or `os.PathLike[str]`, not bytes,
NUL or arbitrary coercion; measure bounded UTF-8 before native path conversion.
Targets are a bounded finite iterable of two-element exact uint64 index pairs;
bool is never an integer. Required acknowledgments are actual bool and true.
`text` is `canonical-ascii`, `edge_count` is `unknown-zero`, `coordinates` is
`meters-right-handed-y-up`, and precision is `exact32` or `bounded-rne32`.
`max_position_error` is a finite nonnegative binary64 value (bool refused), zero
for exact32. No default interpretation is supplied for these required keywords.
Reject wrong configuration before source capture, including an invalid last
target. Preparation checks count/range/shape, not geometric target validity.

All three reusable functions and `single` apply the existing OFF isolation
profile before capture, not just batch. Snapshot the preparation cwd and join it
to a relative logical path, preserving that native logical spelling for IO.
Use weakly canonical/resolved comparison copies only to reject equal or
ancestor/descendant source-entry/destination paths, including resolved existing
ancestors and aliases. Keep `a.off` to `out/new` valid; do not exclude an entire
source parent. Never replace the logical IO argument with its resolved copy:
an unsupported native alias must still reach the genuine capture refusal.
If comparison resolution reports a native path/IO error, preserve the logical
path for the actual attempted native diagnosis instead of guessing equality or
success. Missing sources and existing destinations remain attempted outcomes.
Batch additionally compares every destination to every source and destination.
Captured-byte protection is not a guarantee against later hostile source-path
mutation; native no-replace and identity-safe cleanup are not universal
filesystem alias/race guarantees.

Run releases the GIL for capture and the complete synchronous native workflow.
It calls the existing OFF capture once with the prepared capture caps/work and
the selected execution/cancellation. A capture failure exposes its actual
`OffFileReadResult`, no native workflow and no fabricated OFF owner. On success
call the native workflow once. Prepared declarations are reusable with fresh
result storage and fresh native operations; reuse never means reusing a prior
captured mesh/repair or silently overwriting a destination. Concurrent runs need
separate prepared visible handles or external synchronization and share actual
execution pressure. No Python callback runs inside the GIL-free native region.

`OffGlbExportResult` is readonly and retains: `capture` (actual typed result),
`native` (optional native result), `exit` (native/capture meaning, before stdout
delivery), plus lazy observations. `off_report_bytes` returns an owned Python bytes
copy of the exact retained OFF report or None; `publication_receipt_bytes`
returns exact committed receipt bytes or None. `candidate`, maps, triangulation,
output/proof and published owner are readonly views/copies backed by their real
owners and may outlive caller buffers, prepared objects and execution handles.
Lazy Python allocations are post-return delivery, not native P accounting.
Unexpected internal exceptions propagate; binding code must not manufacture a
success result. Another Python/native thread may call the same
`OffCancellation.cancel()` while the GIL is released, requesting cooperative
stop at actual native poll points; all joins/cleanup precede return. Python
SIGINT handlers instead run at interpreter boundaries. A synchronous GIL-free
call does not promise prompt SIGINT interruption and may commit before the
handler runs. Reusable API/scheduler calls leave process handlers alone; a
caller-owned handler/KeyboardInterrupt may propagate at that boundary without
a promised return value. Store actual native results in the preallocated holder
before reacquiring the GIL; do not relabel a completed commit as canceled.

Console/module `main` requires the interpreter main thread. It saves the old
SIGINT handler, installs a non-raising Python recording handler, and restores
the old handler in `finally` on every exit, including admission/unexpected
exceptions. When Python actually invokes the handler, it latches invocation
interruption and requests the shared token. It stops later jobs and contributes
130, while completed calls keep their actual facts. This is boundary-delayed
handling, not an async-signal-safe bridge, polling thread or Python worker pool.
Embedded non-main-thread callers use the reusable scheduler/token and their own
signal policy; command main refuses that context before capture. No Python
handler/callback runs inside the GIL-free native region.

## OSG-COMMAND-001: Single and whole-batch admission

Proposed entry points are console `meshvale-repair-off-glb` and
`python -m meshvale_repair.off_glb_commands`, both calling the same `main(argv)`.
Subcommands are `single` and `batch`; no existing command changes meaning.
Single accepts required `--input`, `--destination`, `--targets` (JSON pair array),
`--text canonical-ascii`, `--edge-count unknown-zero`,
`--coordinates meters-right-handed-y-up`, `--precision`,
`--max-position-error`, and flags `--ack-triangle-conversion`,
`--ack-corner-expansion`, `--ack-unused-vertices`; optional `--id`,
`--receipt-path` and `--preset` match API defaults. Bounded request-file input is
used for batch; JSON permits no NaN/Infinity, duplicate keys or unknown fields.

Batch schema `meshvale.repair.off-glb-jobs/1` is the closed object
`{"schema":...,"jobs":[...]}`. Each row has exactly required keys
`id,input,destination,targets,text,edge_count,coordinates,precision,
max_position_error,acknowledge_triangle_conversion,
acknowledge_corner_expansion,acknowledge_unused_vertices` and optional
`receipt_path,preset`. IDs are unique ASCII report tokens. Decode and prepare
**every** row before any capture: enforce wire/job/target bounds, exact key/type
checks, all policies/caps, checked framing/report reservations, source/destination
separation and pairwise destination-domain isolation. Paths resolve against one
explicit `base` (scheduler default: invocation cwd; console request-file default:
its logical parent); preserve logical IO arguments and reject known alias/containment overlaps
and source/destination collisions under the existing command path profile.
Admission is not a universal filesystem-race guarantee; native no-replace and
identity-safe cleanup remain authoritative. Invalid last row means zero jobs.

Reusable scheduler:
`run_off_glb_jobs(request, *, base=None, fail_fast=False, cancellation=None)
-> OffGlbBatchResult`. After admission, allocate one stable holder, fixed
job/result slots and delivery observation before any capture. Retain every
attempted `OffGlbExportResult` there by nonthrowing handoff before later jobs or
framing. No callback/sink or caller race to copy a result is required.
The readonly public shape is:

* `mode`: `continue` or `fail-fast`.
* `jobs`: ordered tuple of readonly `OffGlbJobResult`, with exactly
  `id,attempted,result,reason`. Result is the actual retained owner or None;
  reason is None or `{kind: fail-fast|canceled, id: triggering-id-or-None}`.
* `delivery`: readonly `OffGlbDeliveryObservation`, exactly `status,code`.
  Status is `not-attempted`, `prepared`, `framing-failed`, `written` or
  `write-failed`. Code is `none`, `observation-limit`, `report-limit`,
  `receipt-limit`, `frame-limit`, `invalid-observation` or `stdout-io`.
* `stdout_bytes`: bounded immutable prepared bytes, or None after expected
  framing refusal. The getter shares the already prepared bytes.
* `exit`: merge attempted results, actual invocation interruption and delivery
  contributions using the fixed 130/2/1/0 rule.

Expected validation/cap/framing refusal returns this same holder with delivery
`framing-failed`, its bounded code, stdout_bytes=None and every attempted owner
still readable. It neither drops accepted OFF facts nor fabricates success JSON.
Unexpected MemoryError/internal exceptions remain exceptions, with no emergency
aggregate promised; initial holder-allocation failure is before capture.
The single command uses this same holder/framer with one prepared job.
Jobs run sequentially in declaration order using the prepared operation, without
an arbitrary per-job callback or external report sink. Continue mode attempts all
jobs despite ordinary nonzero results. Fail-fast stops after the first nonzero
exit; each later row is unattempted with the triggering opaque ID/reason and null
capture/native/report/receipt fields. Actual cancellation stops further jobs and
records their unattempted cancellation reason. Unattempted slots have result=None
and are not fake executed failures. Console-internal stdout delivery updates
only the preallocated delivery slot and process exit after actual IO. The
reusable scheduler does no stdout write and returns prepared or framing-failed;
a caller writing stdout_bytes owns that external delivery observation. No
holder certifies a write it did not observe. Unexpected exceptions propagate;
console contributes2 without manufacturing aggregate JSON, still merging any
already observed interruption130. Published bundles remain published.
Help/version captures nothing; handler restoration rules still apply.

### Closed root outcome and nullability table

For a returned attempted API result, `capture` is actual. `native` is None
exactly when no native workflow was invoked. Success without the required owner
is an unexpected producer invariant failure, not an ordinary success row.
Root wire `status,stage,code` are domain-qualified symbolic strings. Before a
native call they are `capture:<actual OffFileStatus>`,
`capture:<actual OffFileStage>` and `capture:<actual OffFileDiagnosticCode>`.
After a native call they are `native:<actual OffGlbSurfaceStatus>`,
`native:<actual OffGlbSurfaceStage>` and `native:<actual OffGlbSurfaceCode>`.

| Row | attempted / root exit | Root triple | Nullability and aggregate contribution |
|---|---|---|---|
| Fail-fast unattempted | false / null | `not-attempted`, null, null | capture/OFF/T/E/V/P and both report strings null; reason names trigger; no contribution |
| Cancellation unattempted | false / null | `not-attempted`, null, null | same nulls, canceled reason; row contributes nothing; actual invocation cancellation contributes130 |
| Capture `kCanceled` | true /130 | Actual capture-qualified triple | Actual capture only, every later field/report null; contributes130 |
| Capture `kUnsupportedProfile` | true /1 | Actual capture-qualified triple | Actual capture only, every later field/report null; contributes1 |
| Any other non-success capture status | true /2 | Actual capture-qualified triple; preserve nested cause in diagnostic | Actual capture only, no fake native/Repair call; contributes2 |
| Native configuration refusal or valid pre-state stop | true /2 or130 respectively | Actual native triple | Capture real; native present, possibly no record; uninvoked delegates/report null |
| Native canceled | true /130 | Actual native triple | All reached results/owners/report retained; later uninvoked fields null |
| Native `kRejected` or `kUnsupported` | true /1 | Actual native triple | Actual reached facts retained; no published owner/receipt |
| Native delegated failure with actual charged `kBlocked`, or actual OFF `outcomes.exit == kRejectedOrIncomplete` | true /1 | Actual native triple | Actual reached facts retained; later uninvoked calls null |
| All remaining native non-success statuses | true /2 | Actual native triple | Retain actual reached results/cleanup; invent no publication |
| Native published success | true /0 | `native:kSuccess`, `native:kComplete`, `native:kNone` | All calls reached; committed owner/receipt real; contributes0 |

Consider native-failure rows in order: cancellation dominates every other
category. A delegate field appears iff that call occurred, not iff it succeeded.
OFF report appears iff actually available; receipt appears only with a real
committed owner. Requesting a token after native success cannot change its
triple; a separately observed invocation interruption contributes130. Invalid
invocation raises InvocationError before any capture (console2), with no job
document. With no attempted rows, the aggregate is 0 unless an actual invocation
interruption or delivery failure contributes a nonzero code. Unattempted rows
never manufacture execution.

Single detached output schema is `meshvale.repair.off-glb-result/1`, distinct from
Geometry `meshvale.report/1`. It has exactly `schema,id,attempted,exit,status,
stage,code,capture,off,triangulation,encoding,inspection,publication,delivery,
off_report_utf8,publication_receipt_utf8,reason`. Missing/unreached observations
are null, not zero successes. `capture` projects actual OFF capture status,
diagnostic and execution; `off` projects status/outcomes/diagnostic/
report_diagnostic/execution; `triangulation` projects every non-owner field of
`ChargedTriangulationResult`; E/V project status/diagnostic/facts; P projects
outcome/diagnostic/cleanup/facts. These are closed projections of the named
public records, not arbitrary object dictionaries or owner serialization.
Enums use their exact declared symbolic enumerator names in their typed record;
uint64/index values are canonical unsigned decimal strings, bool is bool, absent
optionals null, ordered arrays remain ordered, and fixed integer error/digest
words use fixed-width lower-case hex. No machine paths/raw exception strings enter
observations. `off_report_utf8` and committed `publication_receipt_utf8` are JSON
strings whose UTF-8 encoded decoded values reproduce the exact original bytes;
do not parse-and-reserialize those reports. No payload bytes are printed.
`delivery` in these detached bytes is exactly `not-attempted`: it describes
stdout delivery, which occurs later. A prepared document cannot certify its own
successful stdout write. API/native exit and observation fields remain their
actual pre-delivery values; the command's actual process exit additionally
includes any later delivery failure. Do not patch or reprint the document after
writing it to predict successful delivery.

Batch output schema `meshvale.repair.off-glb-batch/1` has exactly
`schema,mode,exit,jobs`; jobs are the ordered single result objects. Mode is
`continue` or `fail-fast`; aggregate exit applies 130,2,1,0 precedence across
attempted job/delivery exits. Validate the old OFF report with its existing
validator if bytes exist, and native publication receipt with its declared
schema; validation cannot create missing facts or change a committed outcome.
Framing is bounded separately from native storage. Reserve worst-case JSON
escaping (six bytes per source byte plus checked quotes/structure), with a cap
for native observation arrays, before capture; check actual emitted length too.
On stdout/write failure retain native owners, set write-failed/stdout-io and
contribute2 using the same merge rule: cancellation/interruption plus delivery
failure still exits130. Emit no emergency document and alter no committed
receipt. Prepared bytes keep delivery=not-attempted; the actual holder/process
observation is separate. Native outcomes and all attempted owners remain
available through the returned stable holder.

## OSG-OBSERVATIONS-001: Exact Python getters and map scopes

All getters below are readonly. An observation is an immutable value record,
with the exact named native public fields and nested diagnostic/enum/count/
execution domains; owner fields are excluded and exposed as strong views below.
No arbitrary Python dictionary, new accepted flag or caller-created proof is
admitted. Tuples preserve native order. Exact observation field sets are:

* `OffFileReadObservation`: `status,diagnostic,execution`.
* `OffGlbNativeObservation`: `status,diagnostic`.
* `OffWorkflowObservation`: `status,outcomes,diagnostic,report_diagnostic,execution`.
* `ChargedTriangulationObservation`: every non-owner field of the declared
  `ChargedTriangulationResult`, including its actual optional facts and counts.
* `SurfaceEncodeObservation` and `SurfaceInspectionObservation`:
  `status,diagnostic,facts`; facts is `SurfaceFactsObservation` or None.
* `SurfacePublicationObservation`: `outcome,diagnostic,cleanup,facts`, preserving
  separately reached physical-V facts from the actual P result.
* `SurfaceFactsObservation`: `original_counts,triangle_counts,wire_counts,
  delegates,position_numeric,uv_numeric,execution,examined_work,
  numeric_delegate_profiles,new_requested_bytes`, matching the native getters.
  Optional facts remain None until reached; delegate/profile tuples retain
  actual invocation identities and their complete declared fact fields.

| Result getter | Type / availability | Ownership |
|---|---|---|
| `capture` | `OffFileReadObservation`, every returned attempted API result | Immutable actual value copy |
| `native` | `OffGlbNativeObservation` or None iff workflow not invoked | Value copy, including actual no-record refusal |
| `off` | `OffWorkflowObservation` or None iff OFF workflow not invoked | Actual result value copy |
| `triangulation` | `ChargedTriangulationObservation` or None iff call not invoked | Value copy, including partial facts |
| `encoding` | `SurfaceEncodeObservation` or None iff E not invoked | Value copy with retained actual facts |
| `inspection` | `SurfaceInspectionObservation` or None iff memory V not invoked | Value copy; not physical V |
| `publication` | `SurfacePublicationObservation` or None iff P not invoked | Value copy including cleanup and fresh-V facts |
| `exit` | int in {0,1,2,130} from the root table, before command delivery/SIGINT additions | Exact value |
| `source_mesh` | `OffGlbMeshView` or None iff no captured asset | Strong view of O |
| `candidate` | `OffGlbMeshView` or None iff no actual RepairFacts owner | Strong view of R, including after later failure |
| `candidate_inspection` | `OffGlbMeshInspectionView` or None under the same Repair-owner condition | Strong view of genuine R inspection |
| `triangulated_mesh` | `OffGlbMeshView` or None iff no genuine charged owner | Strong view of T |
| `triangulated_inspection` | `OffGlbMeshInspectionView` or None under the same charged-owner condition | Strong view of genuine T inspection |
| `output` | `OffGlbSurfaceOutputView` or None iff E returned no output | Strong E view, not a V/P proof |
| `proof` | `OffGlbSurfaceInspectionView` or None iff memory V returned no proof | Strong view of actual memory V |
| `published` | `OffGlbPublishedBundleView` or None iff P returned no committed bundle | Strong P view, with distinct physical V |
| `off_report_bytes` | bytes or None iff actual OFF report unavailable | Fresh owned copy of exact retained bytes |
| `publication_receipt_bytes` | bytes or None iff no committed bundle | Fresh owned copy of exact receipt bytes |

An `OffGlbMeshView` has exactly `positions,face_offsets,corner_vertices,
vertex_count,face_count,corner_count,attribute_count`. Positions are a flat
readonly native binary64 memoryview (format `d`, length3V); offsets/corners are
readonly native uint64 memoryviews (format `Q`). Counts are exact Python ints;
attribute_count is actual zero in this route. `OffGlbMeshInspectionView` has
exactly `source,storage_valid,issues,new_requested_bytes`: source is its strong
mesh view, issues an ordered tuple of immutable actual Geometry `StorageIssue`
records with their declared fields, and validity/bytes are actual getters.

The following result properties are readonly memoryview format `Q`, or None
until their genuine owner exists. A reached empty map is a zero-length view,
not None. Every view independently retains its real native backing.

| Result map getter | Direction / meaning |
|---|---|
| `repair_face_map` | Every O face → R face, including removed duplicates |
| `repair_corner_map` | Every O corner → R corner |
| `repair_source_faces` | Every R face → retained O face |
| `repair_source_corners` | Every R corner → retained O corner |
| `triangulation_face_sources` | Every T triangle → R polygon face |
| `triangulation_corner_sources` | Every T corner → R corner |
| `triangulation_face_output_offsets` | R-face CSR offsets into ordered T triangles |

`repair_vertex_identity` is range(V) or None, only after actual OFF memory
verification established unchanged O/R vertex rows. It is a compact Python
value representing verified identity, not an invented engine map array.
`triangulation_vertex_identity` is range(V) or None after accepted charged
transfer established unchanged R/T vertex rows. Neither infers wire identity
from coincident positions. The four Repair map views require actual RepairFacts,
not complete later OFF memory/report success; each later property keeps its own
actual availability condition.

`OffGlbSurfaceOutputView` has exactly `encoding,representation,members,
primitives,facts,new_requested_bytes` plus the map properties below. Members are
an ordered tuple of `OffGlbMemberView`, whose exact fields are `kind,path,bytes,
sha256`: path is a str copy, bytes a readonly memoryview format `B`, digest an
owned32-byte bytes copy. Primitives are immutable copies of every declared
`SurfacePrimitiveView` field; facts is a strong `SurfaceFactsObservation`.
All the following properties are readonly uint64 memoryviews retaining E.

| Output-view map property | Direction / meaning |
|---|---|
| `wire_triangle_faces` | W triangle → R face (`Triangles().FaceSources()` lookup) |
| `wire_triangle_sources` | W triangle → T triangle |
| `wire_corner_corners` | W corner → R corner (`Triangles().CornerSources()` lookup) |
| `wire_corner_sources` | W corner → T corner |
| `wire_vertex_sources` | W vertex → R vertex |
| `source_vertex_offsets`, `source_vertex_wire_members` | R-vertex CSR → W vertices |
| `source_face_offsets`, `source_face_wire_triangles` | R-face CSR → W triangles |
| `source_corner_offsets`, `source_corner_wire_corners` | R-corner CSR → W corners |

`OffGlbSurfaceInspectionView` has exactly `expected,captured_members,facts,
new_requested_bytes`: expected is a strong E view and captured_members an
ordered tuple of actual V member views. `OffGlbPublishedBundleView` has exactly
`inspection,receipt,new_requested_bytes`; inspection is its distinct physical-V
view. Receipt has exactly `path` (str copy), `bytes` (readonly memoryview format
`B`) and `sha256` (owned32-byte copy). It retains no destination path content.

All observation integers are exact Python ints, enums keep typed domains,
optionals are None, collections are the tuples/views above. Every strong view
and memoryview keeps its native backing alive after caller buffers, prepared
objects, execution handles and the original result are destroyed. No writable
buffer export is provided. Metadata/path, observation tuple, digest and report
copies may allocate during Python observation delivery, bounded by frontend
caps rather than native P accounting. Expected command framing refusal returns
the stable holder; unexpected observation MemoryError/programming exceptions
propagate without modifying native owners already retained by the caller.

## OSG-ACCEPT-001: Required evidence before availability claims

Implementation requires reviewed independent literal O→R maps and
order-independent R→triangle/triangle→wire coverage, exact physical GLB/capsule
oracles and independent receipt validation. Do not assert one ear order, infer
identity from equal positions or replace genuine producers with fake owners.
Cover changed, unchanged, empty/empty-target, unused/signed-zero rows, source and
session destruction, valid/no-catalog/no-normal output, actual unsupported
profiles, Exact32 failure, bounded rounding and bound-minus-one, range/underflow,
coincidence/collapse/orientation/triangulation refusals and genuine map scopes.

Test configuration precedence before stop/capture; budget occupancy and cap
boundaries; actual joined worker cancellation/partial launch/allocation failure;
I/O/close/provider/receipt/inventory substitution, destination races and typed
cleanup; every later expected failure retaining the OFF facts/report; postcommit
delivery failure preserving publication; invalid-last-row zero-capture admission;
API/module/console/batch parity, continuation/fail-fast and a self-contained
walkthrough. Separate native, installed, archive, relocated, Python/package,
declared platform/hosted and actual-main qualification. Old green OFF/base or
Surface tests do not establish this composition. No supported release, ABI/runtime
floor, full scene reconstruction, STL preparation or broader MVP claim follows.
