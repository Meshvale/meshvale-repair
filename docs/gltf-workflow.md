# Reported supplied-memory glTF repair workflow

| Field | Value |
|---|---|
| ID | REPAIR-GLTF-WORKFLOW-001 |
| Version | 0.1.4 |
| Status | Development native implementation; qualification pending |
| Owner | Repair orchestration, resolved policy, observations and report translation |
| Interface | [gltf_workflow.h](../include/meshvale/repair/gltf_workflow.h) |

This optional native module composes supplied JSON/resources through public
read, extraction, materialization, targeted duplicate repair, checked writing,
independent reload and reporting. Link `meshvale::gltf_workflow` to execute the
supplied-memory operation. Its Interchange workflow-support dependency must
provide the capped reader/extractor and independent output inspection below.
The ordinary reader/writer cohort alone does not satisfy that dependency.
[Filesystem publication](gltf-file-workflow.md), the [Python file API](gltf-python.md)
and [commands](gltf-commands.md) have separate interface owners.
Native implementation does not establish complete asset-workflow or release
qualification.

The additive [completed-facts retention interface](gltf-retained-facts.md)
provides an explicit opt-in policy for missing-report results. Public
qualification remains pending; default release and receipt meanings stay intact.

Geometry owns [report envelopes](https://github.com/Meshvale/meshvale-geometry/blob/main/docs/reports.md)
and [charged immutable storage](https://github.com/Meshvale/meshvale-geometry/blob/main/docs/materialization.md).
Repair's [charged duplicate contract](materialized_duplicates.md) owns target
equality, whole-request rejection, row-local declarations, independent candidate
verification and correspondence. Interchange owns
[read/extraction](https://github.com/Meshvale/meshvale-interchange/blob/main/docs/gltf-read.md),
[canonical transfer](https://github.com/Meshvale/meshvale-interchange/blob/main/docs/gltf-materialization.md)
and [checked topology writing](https://github.com/Meshvale/meshvale-interchange/blob/main/docs/gltf_topology_write.md).
This contract owns their workflow composition; it changes none of those rules.

The opt-in [direct-translation candidate](gltf-translation.md) is under
qualification. Its contract selects bounded direct node-translation
preservation during the same operation. Static processing remains the default.

## GWF-INPUT-001: Request and selected capability

`RunGltfDuplicateWorkflow` accepts supplied JSON bytes, explicit resource
bindings, an opaque report input identity, a primitive enumeration ordinal,
explicit extraction policy, targeted face pairs and row-local declarations.
The primitive ordinal selects `GltfAsset::Primitive()` after admitted reading;
it is neither an instance ordinal nor a mesh index. The workflow resolves and
retains the resulting strong handle. An absent primitive ordinal is invalid
configuration; the workflow never silently selects the first primitive.
Target rows name the canonical triangles
returned by extraction. The report records mesh/primitive indices, source mode,
accessor rows and original stream-corner provenance so those rows cannot be
mistaken for authored strip/fan corners or persistent editor identities.

Extraction is never chosen implicitly. Require-triangles or explicit strip/fan
conversion and raw or raw-and-normalized values follow Interchange's existing
profile. Both readiness states admitted by strong extraction remain eligible;
the workflow does not reject a preliminary binary-admission state in place of
performing extraction. The default static profile rejects selected animation;
the opt-in translation profile admits its defined subset. Unsupported selected
skin, morph, extension effects or other channel shapes fail through their owner. Unrelated
retained asset data remains subject to complete preservation verification.
Supporting such selected edits remains separate required work.

Positions and all currently admitted channel sets, encodings, normalized flags,
descriptors, sparse/interleaved provenance and raw values retain the transfer
contract. No normalization, implicit filling, pruning, numeric narrowing or
temporary dense Eigen conversion occurs in this module. The checked writer
keeps original vertex/channel accessors and resources; its appended UINT32
index stream does not constitute generic attribute editing. Shared primitive
instances stay shared. Identity selection retains exact original JSON;
changed-empty selection remains explicitly unsupported.

All request views stay readable and unmodified until synchronous return. Limits
precede traversal/copying. The input identity uses Geometry report/1's ASCII
opaque-token grammar and length bound, additionally bounded by
`max_input_id_bytes`; it is not a path. Malformed identity is an invocation
configuration failure. No operation chooses duplicate targets for the caller.
Row-local declaration names must be valid UTF-8 so the owned report can retain
their exact spelling. Invalid byte sequences are configuration failures with
`kReportStringEncoding`; they are never replaced or normalized. The existing
charged duplicate operation's standalone name admission is unchanged.

## GWF-STAGES-001: Ordered composition and results

The stable order is configuration, read, extract, materialize, repair, checked
write, reload, reload extraction, independent verification, report, complete.
Each stage uses the same caller-supplied ledger and the operation's fresh stop
token. The workflow composes the owning public operations, including the
fully capped Interchange routes, without a private cross-product helper.

Initial reading and reload use `ReadGltfAssetCapped`; initial and reload
extraction use `ExtractGltfPrimitiveCapped`. Interchange's
`GWS-READ-001` and `GWS-EXTRACT-001` own those complete explicit-request scopes.
The legacy reader and extractor retain their existing admission and runtime
thread-storage exclusions; their counters alone do not establish this stronger
workflow allowance. Both capped routes admit explicit native callable, worker,
handle and exception records through the relevant operation owner. Opaque OS/CRT
runtime internals retain the owning execution contract's explicit exclusions.

Repair supplies its actual `CandidateInspection()`, `SourceFaces()` and
`SourceCorners()` to checked writing. Input-to-candidate `FaceMap()` and
`CornerMap()` serve reporting and independent repair verification; they cannot
replace those inverse writer maps. All maps are checked against their owning
source and candidate. Nothing is accepted because the native operation merely
ran or returned a candidate.

Reload reads the emitted JSON through the public reader, with bindings only for
resources whose origin is caller binding. Decoded data URIs and buffer-view
image aliases receive no extra bindings. The same mesh/primitive table indices
identify the independently reloaded primitive; original handles are never
passed as output handles. Resolve the expected reload policy from independently
accepted Repair candidate/correspondence and the original strong extraction,
without a writer annotation or unavailable public document query. Verified
identity face/corner selection retains the original extraction policy and mode.
A verified changed ordered subset expects TRIANGLES and applies
`kRequireTriangles`, including when initial extraction explicitly converted a
strip/fan. Retain the caller's explicit values policy and the separate reload
limits, including raw values when normalized views were requested. The record's
`ReloadExtractionPolicy()` and report extension retain that applied request.
This is expected policy selection, not proof of actual output mode: public
extraction and the independent Interchange inspection must verify the actual
mode and identity match. Mismatch is a typed failure. Never retry with another
policy, silently fall through, or mask malformed/unsupported emitted output.

Expected failures retain their real stage, typed diagnostic and exact delegated
diagnostic. An inspection that succeeds with storage issues keeps its original
successful status and actual first ordered issue; no failure status is
synthesized for that delegate. Configuration/count/cap checks follow the
called owner's ordering; this wrapper evaluates configuration and the full
borrowed descriptor counts first, then executes stages in the order above.
Within verification, the wrapper checks source unchanged first, ordered JSON
bytes then caller bindings in request order and byte offset. It then delegates
to `InspectGltfTopologyOutput`, preserving its exact typed first diagnostic,
phase order and coverage rather than imposing a second checker order. The
support contract owns admission/identity/candidate validity followed by
source/candidate, correspondence, lexical, resources, reloaded primitive and
provenance. Parallel earlier-prefix finalization and cancellation priority
remain that owner's rules; Repair does not inspect hidden later phases to
replace its first finding. The wrapper projects those actual observations into
report predicates without conflating schema coverage with native phase order.

`GltfWorkflowStatus`, execution outcome, candidate outcome and profile outcome
are separate. A successful repair retains accepted/unchanged candidate facts
even if writing, reload, verification or reporting subsequently fails. A
rejected repair rejects the entire target request. No writer bundle is exposed
as `Output()` until all independent reload/preservation checks pass. Successful
memory completion returns the source, repair, checked output, independent
reload owners and owned report. Earlier expected failures may retain only
completed owners and observations. Outcome fields remain available even when
report admission fails; report allocation is not guaranteed under exhaustion.

Actual runtime, allocation and worker-launch exceptions propagate after
rollback and every started worker joins. There is no emergency uncharged report
or catch-all success/rejection conversion. Expected cancellation returns its
real completed-stage facts and no unverified output. No source is mutated and
no filesystem or published artifact is created on any path.

`GltfWorkflowTranslationFacts` holds the producer profile and six optional actual
returns: source, writing, reload, verification, publication reload and publication
verification. `GltfDuplicateWorkflowResult::translation` exposes these inline
even when no report owner survives. A record exposes `TranslationFacts()` as a
borrowed const reference, valid while that record remains alive and unassigned.
The memory operation performs the first four phases; its publication slots
remain empty. [The translation contract](gltf-translation.md) owns phase
nullability and failure meanings. Inline translation observations remain available with the default completed
Repair retention policy; they do not create a missing report or record.

Changed repair retains the existing explicit row-local declaration rule. For a
captured `TEXCOORD_0` vertex channel, the caller must declare that exact name
only when it knows the values contain no changed face/corner references. The
[native example](../examples/consumer/gltf_workflow.cpp) keeps its declaration
storage alive through the call. Attribute names and semantics are not inferred.

## GWF-VERIFY-001: Independent authority and public prerequisites

The native target requires these two additive public Interchange seams.
Their owning contracts, installed consumers, request proofs and applicable
profiles establish that dependency's qualification separately.

1. **Complete read/extraction operation accounting.** Initial/reload reading
   uses `ReadGltfAssetCapped` with `GltfReadOperationLimits`, not the legacy
   uncapped reader. Its nested `source` preserves existing read limits and its
   `max_new_requested_bytes` supplies the complete new-request cap. Reading
   needs an inclusive full fresh-request cap, including capture, control,
   projection, resource/decoded storage and scratch; `max_projection_bytes`
   alone is insufficient. Read and extraction need strong, allocation-free
   live operation counters attributable to their owners. Extraction already
   has a complete output-request limit, but does not expose its live counter.
   Failure cleanup and overlapping retained owners must remain observable.
   Ordinary current reader layouts/admission stay unchanged until that owner
   implements and qualifies an additive seam; no private allocator import or
   global tracked-payload delta substitutes for it.
   `GltfAsset::NewRequestedBytes()` returns an optional attributable counter;
   an absent legacy counter is a source-contract failure in this workflow,
   never zero. `GltfPrimitiveExtraction::NewRequestedBytes()` exposes its
   existing capped operation's live requests. The workflow retains counters
   separately from source owners shared by downstream stages.
2. **Owned topology-output inspection.** A public Interchange operation must
   provide `InspectGltfTopologyOutput` and the strong
   `GltfTopologyOutputInspection` result under its own physical/request limits
   and byte-work policy. The workflow retains that inspection as its authority.
   It compares original strong asset/materialization, actual repaired inspection
   and maps, checked output, and independently read/extracted output. It returns
   charged immutable typed findings and live request counts. It independently
   checks the selected primitive's exact row identity, directed loops, encoded
   channel bits/flags/sets/bounds/material binding and composed provenance;
   every retained resource byte and relationship; and exact original JSON
   lexemes outside the allowed primitive indices/mode and append-only table
   changes. Identity output compares entire original JSON. Unrelated/shared
   tables, opaque members, images, graph/instance identities and resource
   aliases remain unchanged. Read admission is not this preservation proof.
   No input JSON DOM, private JsonSnapshot import, writer success flag or copied
   writer plan is an independent oracle. An operation-owned lexical span index
   over retained OriginalJson is feasible; it must have its own cap, charged
   planning and source-topology agreement before comparing patch allowances.

Workflow source-protection independently compares caller JSON and explicit
resource bytes with the original admitted owner while those borrows are alive.
Source protection uses the verification byte-work policy while retaining its
own execution counters. The output inspection is bounded by initial/reload
physical limits, writer
vertex/channel/scalar/descriptor/map/JSON limits and the inspection's explicit
physical/request limits. It checks overflow and all aggregate extents before
allocation.
Source-to-candidate vertex rows remain shared and exact; input-to-candidate
face/corner maps and retained-source inverse maps must agree. A storage-valid
inspection alone cannot prove those relations or binary resource retention.

## GWF-BUDGET-001: Admission, lifetime and work

Every active limit and work field, including nested fields, is required and positive.
In direct-translation mode, the trailing `GltfDuplicateWorkflowLimits` fields
`report_publication_reload` and `report_publication_verification` supply positive,
host-representable report configuration. Static mode ignores their zero defaults.
They describe the two publication-phase caps in the six-object processing
configuration; the memory operation does not execute publication phases.
`max_new_requested_bytes` caps all concurrently live new requests from initial
read through the returned record, report and reloaded output. Each stage also
has its own cap. For a delegate with one complete operation cap, its effective
cap is the minimum of its configured cap and remaining aggregate capacity,
accounting for all still-live earlier owners and wrapper scratch.

Materialization is a multi-cap delegate: bridge, Geometry capture and Geometry
inspection can overlap. Its caller-configured positive caps are the three
suballowances. Check the sum of `max_bridge_new_requested_bytes`,
`materialization.max_new_requested_bytes` and
`inspection.max_new_requested_bytes` for host/uint64 overflow during
configuration. Immediately before the single adapter call, that sum must fit
the remaining workflow allowance; otherwise return wrapper `kBudgetExceeded`
with `kMaterializationAllowance` at materialize, without invoking the adapter.
Pass those three suballowances unchanged. Do not clamp each independently to
the same remainder, and do not infer peak admission from surviving counters
after return. This explicit conservative allowance can reject even if eventual
actual requests might have been smaller; callers choose smaller positive stage
caps to fit, without weakening any stage's real request admission.

Transfer's three owners and Repair's already aggregated delegate totals are
counted once. Wrapper growth is held stable while any delegate uses its assigned
allowance; concurrently running foreign operations are governed by the common
ledger, not included as new workflow allocations. Ledger deltas are never used
to guess this accounting.

Original requested block sizes, headers, control blocks, alignment padding,
descriptor/name/provenance/map arrays, reload binding tables, verification
plans, worker scratch, serialization lengths/escaping and report buffers are
admitted before their allocation. Old and new buffers coexist in charges until
old storage is freed. Captured sources, candidate, output and reload overlap
normally; budget checks cannot assume they have been destroyed. The returned
counter reports current surviving fresh requests, not cumulative traffic,
historical peak, allocator usable sizes or RSS. Standard runtime/thread/stack
allocations follow the linked execution owners' exclusions, explicitly
qualified when this module is implemented.

Source/configuration descriptor dependency planning and ordered JSON report
token planning may require serial prefixes; their reason is structural order,
not a general serial execution choice. Substantial disjoint byte comparison,
channel/map verification and admitted report copying use leased shared workers
when thresholds and slots allow, with cancellation checkpoints and caller
fallback. There is no unmanaged pool. Delegated phase stats retain their actual
owners; Interchange output-inspection phase counts remain separate from wrapper
source-protection/report-copy planned/launched/joined/caller counts.
Read/extraction work is not relabeled as wrapper work.
All launched workers join before charges, borrows or worker leases are released,
including partial launch and runtime failure.

Record copies share strong immutable owners and charges without allocation.
Borrowed references/spans require an alive, unreassigned visible record copy;
separate copies support concurrent reads. Caller context, input buffers, original
handles and other record copies may die after return. The same mutable record
variable requires external synchronization when reassigned. A later operation
uses a fresh token rather than a stopped token captured by a previous operation.

## GWF-REPORT-001: Native report/1 translation

The native boundary is an owned charged UTF-8 serialization of Geometry's
existing `meshvale.report/1`, validated against its installed schema and
reference consistency rules. It is not a new envelope or a stringly operation
interface. Core fields retain their owner. A serializer must stream typed
observations with checked lengths and exact integer formatting; it builds no
allocating input JSON DOM and has no dependency on the Python runtime. The
current Python producer is not evidence that this native serializer exists.

| Report field | Concrete workflow translation |
|---|---|
| `tool` | Actual compiled Repair package name/version; no placeholder version |
| `input` | Validated owned input identity; optional `sha256` is null unless actually computed under admission |
| `operation` | `kind=repair`, `name=remove-duplicate-faces`, `version=1`; resolved extraction policy, row-local declarations and ordered keep/remove relationships |
| `execution` | Actual completed/failed/cancelled state and schema stage, with a nonempty reason for failure/cancellation |
| `candidate` | Actual accepted/unchanged/rejected or not-attempted; acceptance is retained after a later failure |
| `profile` | `gltf-static-duplicate-preservation`, version `1`; required checks below and outcome derived from actual coverage |
| `snapshots` | Input/candidate canonical selected mesh counts only when those owners exist; not the entire scene or authored strip/fan stream |
| `maps` | Vertex identity and exact Repair input-to-candidate face/corner maps; unchanged uses identity for every domain |
| `preservation` | Actual named/set/domain channels, descriptors/raw bits and provenance; representation conversions state why, never claim unchanged wire encoding for appended indices |
| `publication` | `requested=false`, `outcome=not-requested`, null snapshot/reason, empty artifacts, `verified=false` |
| `extensions` | `meshvale.repair.gltf`, extension version `1`, with native stage, selected mesh/primitive indices, source mode/initial and resolved reload policy, composed source-corner provenance, inverse retain maps, changed-binding receipt, independently verified memory-output facts and actual phase counters |

The schema forbids an `output` snapshot for an unpublished result. This memory
workflow therefore keeps output counts/identity observations in the named
extension and scopes roundtrip coverage to the candidate; it does not emit core
candidate-to-output maps. `Output()` remains a real verified memory owner, not
a publication receipt. A memory checked-write/reload failure uses schema stage
`verify` with its exact native stage in the extension. It must not use schema
stage `export`, which requires a requested filesystem publication. Read/extract
failures map to `import`, transfer/storage to `inspect`, repair execution
failures to `repair`, post-candidate failures to `verify`, serialization to
`report`, and successful completion to `complete`. A finite rejected target
request has completed execution with core stage `complete`, as required by
report/1, and retains its exact native `repair` stage in the extension and typed
outcomes. This preserves rejected exit meaning 1. Null input/operation is reserved for the
schema's import/invocation-failure case; missing snapshot scopes use null
snapshot/mesh references rather than invented owners. Before a canonical input
snapshot exists, core `operation.targets` is empty and the extension retains
the unresolved requested pairs. Once it exists, core targets refer to its face
rows, including rejected out-of-range requests; ordered keep/remove relationships
remain explicit in operation options. No target references a missing snapshot.

Required coverage IDs are `source-storage`, `target-policy`,
`candidate-storage`, `repair-correspondence`, `canonical-preservation`,
`reload-selected-primitive`, `retained-asset-preservation`, `source-unchanged`.
Each maps to the identically named typed `GltfWorkflowCheck` predicate; the
extension distinguishes its native phase without redefining envelope meaning.
Actual completed observations produce performed plus explicit passed/failed
finding. Earlier blocking produces skipped with a real blocking reason;
unsupported capability stays unsupported; checker execution failure is failed.
All required references are present even if their scopes could not be created.
Any required performed failure makes the profile failed; otherwise any required
unperformed check makes it incomplete; otherwise it passes. Optional
`solid-validity` is unsupported with an explicit reason, never evidence of
manifoldness, closure or intersection freedom. It is not a required predicate.

An expected rejected edit has exit meaning 1, execution failure 2, cancellation
130, and complete successful memory repair 0. Cancellation takes precedence,
then execution failure, then rejected/incomplete, then success, using Geometry's
rules. A required predicate failure retains accepted candidate fact but prevents
successful workflow completion and output exposure. Finite rejected requests
do not become execution failures merely because no candidate was produced.
Diagnostics identify existing source rows where possible; invalid referenced
values belong to diagnostic data, never fictitious element references.

## GWF-DELIVERY-001: Qualification and remaining surfaces

### Native build and focused checks

Configure Repair with `MESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON` and
`MESHVALE_REPAIR_GLTF_WORKFLOW=ON`. The workflow option defaults OFF so existing
native/Python consumers retain their prior dependency cohort. Use installed
Geometry and Interchange prefixes through `CMAKE_PREFIX_PATH`; Interchange must
export `meshvale::gltf_workflow_support`. Merely using a producer whose package
version is `0.0.0` does not prove these development seams exist. Read the
[charged operation build selection](materialized_duplicates.md#dependency-build-and-test-selection)
and the Interchange support contract for their numerical/allocation profiles.

```sh
cmake -S . -B .local/gltf-build -DCMAKE_BUILD_TYPE=Release \
  -DMESHVALE_REPAIR_GLTF_WORKFLOW=ON \
  -DCMAKE_PREFIX_PATH="$GEOMETRY_PREFIX;$INTERCHANGE_PREFIX"
cmake --build .local/gltf-build --config Release
ctest --test-dir .local/gltf-build -C Release --output-on-failure
```

The focused `gltf_workflow` test exercises changed/no-op supplied-memory
composition, exact opaque JSON lexemes, shared instances, source/context
destruction, copied ownership, real joined report-copy work, target rejection,
accepted-then-write failure, report extent failure, materialization allowance
rejection/overflow, malformed read and initial cancellation. Additional fixtures
exercise declared UV/color channels, normalized integer views, external/data-URI/
buffer-view image aliases, changed/no-op strip and fan conversion, and exact
live-ledger accounting through copied-owner destruction. An optional
directory argument to its executable retains actual generated report JSON for
independent Geometry schema/reference validation. Keep generated evidence in
ignored local storage. These focused checks are a native development surface;
they do not replace the complete qualification inventory below.

The separate `gltf_workflow_requests` test exercises original wrapper-allocation
fault ordinals, partial source/report native launch, actual worker-entry
cancellation, serial/native/saturated agreement, overlapping report storage,
exact operation allowance and shared-ledger bounds, and concurrent workflows.
It checks unchanged caller storage and joining every started worker before
releasing its charged records. Its private per-call wrapper backends leave
delegated public operations under their owning allocation/worker contracts.

The separate [installed consumer](../examples/consumer/gltf_workflow.cpp) uses
only public installed headers/targets and retains its copied result after the
caller buffer and execution context are destroyed. Configure
`examples/consumer` with `MESHVALE_REPAIR_TEST_GLTF_WORKFLOW=ON` and the same
installed dependency prefixes, then build and run CTest. This opt-in check
requires a Repair producer built with the workflow option ON.

For a genuinely unsupported Geometry numerical producer, select
`MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=unsupported` for both native and
installed-consumer checks. The workflow test and installed consumer then run
`--unsupported-profile` with a valid nonempty static primitive and require the
actual delegated Geometry allocation-profile diagnostic, an owned failure
report, unchanged caller bytes and retained source ownership. This test option
does not change the producer's profile; running it against a qualified producer
fails its expectation. The complete successful workflow and request-fault tests
run only under the qualified selector.

Report/1's public native diagnostic receipt uses an owner discriminator and
the corresponding published enum ordinals under the extension's version `1`.
The typed result also retains the original delegate and its status directly.
The report includes original channels/accessor rows and inverse maps even on
later failure, with `memory_output_verified=false` until independent inspection
passes. Its core preservation claims cover the accepted canonical Repair edit;
appended UINT32 wire indices and exact scalar widening are separately described
as conversions in the extension. No unpublished core output snapshot is emitted.

The wrapper bounds borrowed resource descriptors by the combined buffer/image
table allowance (twice the initial reader's per-table row limit), checks all
descriptor host extents before traversal and admits its charged accounting
state before any delegated operation. Sequential descriptor and JSON-token
planning retain their required order. Source-byte comparison and report-byte
copying use the shared leased workers when thresholds and slots permit.
Two-pass report planning allocates no JSON DOM; the charged staging and output
buffers coexist and count toward the aggregate allowance. A bounded final stats
tail is emitted after real report-copy workers join. Actual report content must
fit `max_report_bytes`; retained buffer capacity remains part of live charges.
Stopping before report admission/serialization returns typed outcomes without
an emergency uncharged report.

Implementation must separately prove expected stage failures/rejections,
diagnostic precedence, all admitted channel encodings/sets and normalized/raw
views, strip/fan provenance, many-to-one duplicate maps, shared accessors and
instances, resource aliases, exact opaque number/string lexemes, unchanged
source bytes, no-op and changed-empty behavior, independent reload mismatch,
owner/context destruction, aggregate and stage cap boundaries, allocation and
partial-launch rollback, cancellation and substantial real joined work.
Report fixtures must pass the exact installed Geometry schema and independent
reference validator, including rejected/cancelled/accepted-then-failed states.
Installed public consumers must execute the complete memory workflow and retain
its output/report after source/context destruction. Applicable original-route
allocation and sanitizer/profile qualification remains required; a composition
probe or declarations alone establishes none of it.

The [retained-file interface](gltf-file-workflow.md) owns Interchange-backed
publication, source/destination separation, rollback and published-byte reload.
The [Python file API](gltf-python.md) and [single/batch commands](gltf-commands.md)
compose that orchestration with the same policy/report owners. The opt-in
[direct-translation candidate](gltf-translation.md) retains its separate
qualification gate. Broader skin/morph/animation edits, arbitrary channel
mutations, final package/release support and complete MVP obligations remain
future work. The [OBJ workflow](obj-workflow.md) owns its own publication behavior.
