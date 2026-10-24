# Reported OFF repair through files, API and commands

| Field | Value |
|---|---|
| ID | REPAIR-OFF-FILE-001 |
| Version | 0.1.0 |
| Status | Development implementation; qualification in progress |
| Interface | `meshvale/repair/off_workflow.h` and `meshvale_repair.off_workflow` |
| Target | Optional `meshvale::off_file_workflow` |

This workflow applies the existing [charged duplicate-face operation](materialized_duplicates.md)
to one retained indexed OFF file, verifies its canonical output and optionally
publishes a new directory containing that OFF file and its report. The same
prepared operation serves the reusable Python API, single command and ordered
batch. No target discovery, welding, triangulation or additional repair algorithm
is selected. This document owns the development contract; the
[quickstart](off-quickstart.md) provides build instructions and an executable
walkthrough. Supported releases remain separate work.

## OFW-DEPENDENCY-001: Independently selectable native module

The selected option `MESHVALE_REPAIR_OFF_FILE_WORKFLOW` initially defaults OFF.
It requires `meshvale::materialized_duplicates` and Interchange's optional
`meshvale::off_files`/`meshvale::off` from a compatible installed producer with
the OFF-FILES-001 contract. Geometry owns execution and `meshvale.report/1`.
All native producers/consumers must match compiler, configuration, CRT and
instrumentation. This target does not require the optional glTF workflows,
Interchange's Python extension, a new worker pool or another format SDK.
The header/target are installed only when the implementation is enabled.

## OFW-REQUEST-001: Exact source and one engine invocation

`RunOffDuplicateFileWorkflow(request, limits, work, stop)` borrows a strong
`OffFileAsset`, ASCII opaque-token `input_id`, ordered `DuplicateTarget` span,
explicit `OffTextPolicy::kCanonicalAscii` and
`OffEdgeCountPolicy::kUnknownZero`, and optional publication
`{destination, receipt_basename}`. Publication requires exactly one portable
flat receipt basename distinct from the source entry. Null publication means
no filesystem export. Every borrow remains valid through return and joins.

Before work validate enums, all configuration/count/name/extents, and the report
ID grammar `^[A-Za-z0-9][A-Za-z0-9_.:-]*$` within `max_input_id_bytes` (preset128,
schema maximum256). Never rewrite an ID. Source capture is a separately typed
Interchange result. Derive execution from the source document's actual strong
mesh; no substitute ledger is accepted. OFF admits no attributes, so this
workflow has no row-local declaration option.

Allocate final record state before calling `RemoveMaterializedDuplicateFaces`
exactly once on the actual source mesh. Empty targets request the engine's real
unchanged operation, including for nonempty input. Immediately retain a completed
repair owner through allocation-free, nonthrowing handoff. Preserve actual engine
status, diagnostics and phase statistics even without an owner. Never replay
after write, verification, report or cancellation failure.

For accepted/unchanged candidates, call `WriteOff` on the real candidate with
the original document as source provenance. Independently `ReadOff` the emitted
memory bytes. Compare exact counts, every vertex bit including unused/signed-zero
rows, complete offsets, polygon starts/winding/directed indices, emitted edge
zero and actual writer facts. Independently check all actual correspondence
maps against retained rows; use real `SourceFaces`/`SourceCorners`, without a
new duplicate selection. Publication invokes `PublishOffBundle` once with the
source, actual candidate and output. Its real staged reload must also pass
application correspondence checks before receipt preparation.

## OFW-OWNERSHIP-001: Retained facts survive expected later failure

The typed result identifies workflow status/stage/primary diagnostic, actual
execution, candidate, profile, publication and exit outcomes, optional immutable
record, actual publisher result and wrapper execution. The record exposes
`Source()`, optional actual `RepairFacts()`, optional verified memory output and
reload, optional `ReportJson()` bytes, optional published owner/report basename,
and attributed live requests.

The preallocated record retains completed Repair facts through every later
expected failure, including report cancellation/extent/budget refusal. No
retention opt-in is needed. Missing report bytes do not imply no completed repair
or no retained memory. Expose verified output only after its required checks.
State-init failure before the engine has no record or repair. Native reports may
be unavailable after secondary failure; preserve primary/actual outcomes without
promising an emergency report. Unexpected allocation/native exceptions propagate
after cleanup/joins. Strong copies allocate nothing and preserve original backend
lifetime; views require a live unreassigned owner. Lazy Python observations may
allocate after native return.

## OFW-PRESERVATION-001: Concrete report predicates

Use unchanged `meshvale.report/1`, the actual compiled tool version, operation
`remove-duplicate-faces/1`, resolved ordered targets and explicit text/edge
policies. The profile `off-indexed-duplicate-preservation/1` requires:

| Check | Version1 predicate |
|---|---|
| `source-storage` | Actual source storage inspection passes |
| `target-policy` | Actual engine eligibility/target validation passes |
| `candidate-storage` | Actual candidate storage inspection passes |
| `repair-correspondence` | Full actual source/candidate maps and retained rows agree |
| `canonical-preservation` | All preserved position bits, offsets and directed loops agree |
| `reload-candidate` | Actual emitted-memory reload agrees with the candidate and write facts |
| `source-unchanged` | Captured immutable original bytes equal a separately admitted pre-engine baseline |

Publication profile `off-indexed-duplicate-publication/1` additionally requires
`staged-candidate` (actual staged reload/correspondence) and
`staged-physical-members` (every exact member byte and complete inventory).
These are preservation predicates. Planarity, manifoldness, closure,
self-intersection and solid validity are not implied. Optional `solid-validity`
is explicitly unsupported.

Performed checks have actual passed/failed findings. Earlier blocking makes a
required check skipped with a reason; unavailable capability is unsupported;
checker execution failure is failed. A performed required failure dominates;
otherwise any unperformed required check makes the profile incomplete. Do not
silently remove checks. Truly empty input may pass genuinely performed vacuous
comparisons only after actual read/engine/write/reload. Empty targets on nonempty
data still require actual producer capability. Unsupported profiles retain the
real failing delegate status/stage, without a portable numerical fallback.

Input/candidate snapshots use actual canonical vertex/face/corner counts. An
accepted/unchanged result includes input-to-candidate vertex identity and complete
actual face/corner maps; unchanged maps are identity. Extension
`meshvale.repair.off` version1 carries actual inverse `SourceFaces` and
`SourceCorners`. Only true publication adds core output snapshot,
candidate-to-output identity maps and verified artifact SHA-256. Memory-only
success does real writing/reload, but has no exported output snapshot/artifacts.

`input.sha256` hashes complete captured entry bytes, without canonical text or
bundle framing. After successful memory verification and source-baseline
comparison, compute it once under the hash cap before preparing memory-only
success or invoking publication. Publication reuses that digest; its provider
adds the actual staged output-entry digest. Count each raw stream once.
Earlier failures may have null input hash if hashing never completed; later
failures retain a completed digest. Hash refusal/cancellation keeps its actual
stage/diagnostic, without a guessed digest or source reread. Complete success
requires the genuinely computed source hash.

The extension records source portable name/profile/declared edge count, actual
write facts and phase diagnostics/statistics. Raw source bytes remain exact.
Canonical output explicitly drops comments, whitespace and numeric spelling and
emits unknown edge zero, even for unchanged repair. Units remain unknown.
Preservation claims state that rewrite and valid map scopes, not lexical identity.
`source-unchanged` does not promise external physical source-path state after
capture. Publication never rereads that source path.

## OFW-COMMIT-001: Receipt prepared before publication

The report is the only dynamic bundle member. After actual staged-entry
verification, bounded hashing and report generation prepare conditional success,
output snapshot/maps and final owner metadata before commit. Provider state and
backing outlive publisher return/import joins/cleanup and obey Repair wrapper
caps. Expected provider failure latches its real wrapper stage. Internal
unexpected exceptions are retained and rethrown after publisher cleanup rather
than flattened into an external callback failure.

Interchange imports and exactly verifies the receipt before one native
no-replace commit. The report excludes its own hash. Only actual published
outcome exposes conditional success bytes/path. No precommit callback can promise
publication, and no postcommit allocation, callback, report patch or mandatory
reload is allowed. Final publisher statistics are separate typed facts.

Precommit failure/cancellation exposes no finalized artifacts/output snapshot.
Commit failure may retain a passed preservation profile and accepted candidate
with failed execution. Secondary report failure cannot erase primary outcomes.
Later stdout failure cannot change a true committed bundle/receipt. Core report
stages are the actual reached import/inspect/repair/verify/export/report/complete;
memory writing/reload uses verify. A finite rejected target is completed/rejected
with complete core stage and its actual native repair stage retained.
Exit precedence is130 cancellation,2 execution/delivery failure,1 rejection or
incomplete/failed profile,0 complete success.

## OFW-RESOURCES-001: Complete explicit scopes

The [preset contract](off-presets.md) and [complete field table](off-presets.json)
own application choices. Native callers supply all positive inclusive fields;
validate nested fields and checked sums before work. Do not clamp separate
delegate subcaps independently to one remainder.

`OffDuplicateFileWorkflowLimits` contains
`max_wrapper_new_requested_bytes`, `max_input_id_bytes`,
`max_source_protection_bytes`, `max_report_bytes`, `max_hash_input_bytes`,
`max_verification_vertices`, `max_verification_faces`,
`max_verification_corners`, and nested `repair: MaterializedDuplicateLimits`,
`writing: OffWriteLimits`, `reload: OffReadLimits`,
`publication: OffFilePublicationLimits`.

`OffDuplicateFileWorkflowWorkPolicy` contains
`repair: MeshMaterializationWorkPolicy`, `writing/reload: OffWorkPolicy`,
`verification/hashing/report: OffByteWorkPolicy`,
`publication: OffFilePublicationWorkPolicy`. `OffByteWorkPolicy` has
`parallel_min_bytes`, `partition_bytes`, `cancellation_block_bytes`, `max_jobs`.

Charge wrapper baseline/hash/report/verification plans, escaped text, scratch,
workers and overlapping old/new buffers. Capture, engine, writing/reload and
publication retain their own attributed scopes on the common ledger; do not
double-count source mesh through Repair or infer an end-to-end total/peak/RSS.
Report every actual phase's jobs/launches/joins/caller work separately; serial
codec counts stay zero. Substantial supported work uses shared leased workers,
with bounded scalar/byte/relocation stops and caller fallback. Join every worker
on every exit; perform pre-stop and final handoff checks.

## OFW-ENTRYPOINTS-001: API and ordered commands

The selected API is
`repair_off_file(source, destination, targets, *, text, edge_count, input_id='source', report_path='meshvale-report.json', execution=None, cancellation=None)`
in `meshvale_repair.off_workflow`. `destination=None` means real memory-only
verification. `OffExecution(preset='balanced-v1')` and `OffCancellation` own
existing Geometry session/stop machinery. `execution=None` uses one module-owned
default OFF session per interpreter; concurrent/retained results share its512MiB
ledger and4-worker cap. An explicit session is independent. No free-threaded,
subinterpreter or cross-interpreter guarantee is selected.

One private prepare/run seam owns all arguments/final result slots before
capture and serves API/commands. Native work releases the GIL; actual typed
capture/workflow/publisher outcomes, maps/counters and optional bytes remain
inspectable. Commands create one invocation session and token; supplied
execution/cancellation must be actual OFF types and `fail_fast` a real bool.

Development command forms:

```sh
meshvale-repair off source.off output/new --text canonical-ascii --edge-count unknown-zero --target 0:1
meshvale-repair off source.off output/copy --text canonical-ascii --edge-count unknown-zero --unchanged
meshvale-repair off-batch jobs.json --preset balanced-v1
```

Module and console entry points share the front end. Either repeated
`--target KEEP:REMOVE` or `--unchanged` is required. Commands require a
destination; the API separately permits `None`. Single `--input-id` defaults
`source`; flat `--report-path` defaults `meshvale-report.json`. Both commands
accept the two documented presets; batch also accepts `--fail-fast`.

Batch JSON has exactly `schema: 'meshvale.repair.off-jobs/1'` and `jobs`.
Each closed row requires `id`, `input`, `destination`, `targets`, `text`,
`edge_count`, with optional `report_path`. Targets are arrays of two-element
integer arrays; an empty array intentionally means unchanged. Reject duplicate
keys, nonfinite JSON numbers, unknown/missing fields, booleans as indices and
invalid scalar/policy kinds. No resource/primitive/row-local fields are admitted.

Admit the entire invocation before any asset contents: unique ASCII IDs, native
derived counts/name/path bounds, receipt collision and source/output isolation.
API paths default to cwd; request-file paths use its logical parent. Retain
logical native paths; canonical copies serve comparisons only. Compare every
destination to every source entry and every destination, including resolved
existing ancestors/aliases. Reject equality or ancestor/descendant overlap with
source entries and equal/nested outputs. Allow `a.off` to `out/new` under one cwd;
do not exclude whole source-parent directories. Resolving a comparison path must
not turn an unsupported native capture alias into an accepted input. Missing
files, existing destinations and native path/numerical refusals remain attempted
outcomes, rather than request-shape rejection.

Wire ceilings are16 jobs,8MiB request,1million total target pairs,16MiB per
detached report and8MiB outer framing. Reject excessive character counts before
copy/UTF-8 encoding, then exact encoded/native extents and canonical request
extent. Arithmetic per-job allowance is no allocation/RSS promise. Preflight
declared report/jsonschema dependencies during invocation; help/version stay
lazy. Prepare once, run once in order, detach/validate exact native bytes and
release each native owner before the next job.

Immutable `OffCommandResult` exposes bytes/exit with a fresh lazy parsed report.
Batch frames literal native report slices without reserialization. Nonempty
native bytes always win: validate them or raise delivery failure; never replace
invalid bytes with a fallback. An actual published result with absent, invalid
or undeliverable genuine receipt raises typed delivery failure2 (130 if actually
canceled at delivery/scheduling) and yields no aggregate. Preserve the true
committed bundle/receipt and typed facts; never relabel publication for a fallback.

Only an unpublished missing-receipt result may use command-owned profile
`off-command-receipt/1` under unchanged report/1, projecting actual outcomes.
Completed accepted/unchanged counts/maps remain mandatory; before repair invent
no snapshots/maps. Required `native-receipt-validation` is skipped by absence,
hence incomplete. Claims/artifacts stay empty and publication observations remain
typed extension facts. If a truthful row cannot fit16MiB, raise delivery error
with no aggregate, preserving earlier receipts and truncating nothing.

Continue/fail-fast, unattempted reasons, SIGINT token/restored handler, actual
single exit, final batch cancellation including an empty batch, UTF-8 stdout
plus newline and short-write/flush/shutdown handling follow the
[existing command delivery contract](gltf-commands.md). No asynchronous exception
may cross native commit.

## OFW-ACCEPTANCE-001: Shared actual corpus

API, console/module and ordered batch must use the same independently specified
corpus: cyclic duplicate quads, an unrelated triangle, unused signed-zero
vertices, all four exact maps, reverse/distinct-ID refusals, real unchanged and
empty operations, malformed/unsupported inputs, captured-source hashes, Unicode,
whole-invocation rejection before capture, fail-fast, cancellation, occupied
ledger, allocation faults, report/delivery failures and existing destinations.
Required qualification includes native, relocated packages, Git-free source
archives, sanitizers, actual public platform logs and a self-contained runnable
walkthrough for implemented behavior. This contract supplies no completed-run
or release claim.
