# Preserve direct translation animation during glTF repair

| Field | Value |
|---|---|
| ID | REPAIR-GLTF-TRANSLATION-001 |
| Version | 0.1.0 |
| Status | Implemented development profile; no supported release |
| Owner | Explicit workflow selection, application presets, API/commands and report projection |
| Producer | Interchange GLTF-DIRECT-TRANSLATION-001 |

This contract selects an opt-in extension to the existing targeted duplicate-face
workflow. It preserves directly authored node-translation tracks while editing
one shared mesh primitive. It does not evaluate, bake, resample, normalize, blend,
retarget or render animation. The native/Python/command interfaces below are
an implemented development profile. The required
[implementation evidence](#gtran-verify-001-required-implementation-evidence) has
passed source/archive, installed/relocated-consumer and applicable CI/sanitizer
development checks for the documented producer cohort. Use the
[matching native SDK cohort](gltf-file-workflow.md). Supported releases and runtime
floor qualification remain open.

The current static profile and its default, API behavior, report extension and
closed batch schema remain unchanged. Skin/morph processing, affected
rotation/scale and animated ancestors are outside this first animation profile.
The existing operation still owns duplicate equivalence, whole-request rejection
and every Geometry correspondence map. This is a same-format glTF/GLB workflow,
not a converter or another repair algorithm.

## GTRAN-DEPENDENCY-001: Producer authority and ownership

The [Interchange producer contract](https://github.com/Meshvale/meshvale-interchange/blob/main/docs/gltf-translation.md)
owns exact affected-user/ancestor membership, sampler/type/time/bounds/layout
admission, immutable source-bound proof, independent writing/reload inspection,
actual observation units and phase-specific native diagnostics. The existing
[read](https://github.com/Meshvale/meshvale-interchange/blob/main/docs/gltf-read.md),
[transfer](https://github.com/Meshvale/meshvale-interchange/blob/main/docs/gltf-materialization.md)
and [file publication](https://github.com/Meshvale/meshvale-interchange/blob/main/docs/gltf-files.md)
owners continue to apply. Opaque retention alone is not admitted animation
processing; caller flags cannot manufacture the producer's proof.

Use a producer that actually implements this contract and its profile, successful
owner getters and failure observation slots. Rebuild the compatible native SDKs
and Repair consumer together. Equal native package version `0.0.0` does not prove
feature or ABI compatibility. No Interchange Python package is required. This
selection declares no supported platform, runtime floor, package-index wheel or
stable release.

Native memory/file requests carry the selected trailing
`GltfExtractionRequest.processing_profile`. The existing
[reported memory operation](gltf-workflow.md) and
[retained-file operation](gltf-file-workflow.md) remain the only stage engines.
Materialization retains the strong source extraction; animation is not copied
into Geometry attributes. Checked writing, memory reload and physical staged
reload propagate the actual profile and independently validate their own source
owners. Falling back to a static extraction after selecting animation is a
profile/proof failure, not successful preservation.

## GTRAN-SELECTION-001: Explicit profile and unchanged defaults

| Selection | Native processing value | Python/single-command value |
|---|---|---|
| Existing default | `kStaticLocalTrianglesV1 = 0` | `static-local-triangles-v1` |
| New explicit profile | `kDirectTranslationTrianglesV1 = 1` | `direct-translation-v1` |

The new producer profile examines every authored user of the selected mesh,
including inactive users, and its strict ancestors. Its initial admitted tracks
are direct translation with dense FLOAT SCALAR times and FLOAT VEC3 values,
LINEAR or STEP interpolation, with the producer's exact finite/time/bounds rules.
An affected unsupported user cannot be bypassed by choosing another scene or
clip. Unrelated data retains the existing producer contract; no blanket claim
of whole-asset animation validation or playback is made. A static primitive can
use the new profile after a real inventory scan with zero affected tracks.

On every completed result, including capture failure, `processing_profile` is
the actual selected Python `str`. Its read-only lazy `translation_facts` property
is `None` for static processing. In new mode it returns a fresh detached Python
`dict` with exactly the seven keys of the closed `processing` object in
[GTRAN-REPORT-001](#gtran-report-001-closed-new-mode-observations). Its observation
and nested diagnostic/work/execution fields use that section's plain Python
counterparts: `int`, `bool`, `str`, `None` and nested `dict`, with integer enum
ordinals. It does not return enum wrappers or a parsed report.

The `profile` value records actual selection. Each of the six observation slots
is `None` until that helper was called; capture/preworkflow failure has all six
slots `None`, rather than a null whole new-mode object or synthetic zero-work
facts. Reached observations copy only already retained typed facts. Access never
starts validation. Mutating returned dictionaries cannot change the result,
native state or receipt. A later Python allocation failure cannot change a
completed native operation or published receipt. An escaping operation exception
provides no fabricated normal result.

## GTRAN-PRESETS-001: Positive configuration and real shared bounds

The machine-readable [selected preset table](gltf-translation-presets.json) has
exactly `status` and the two new preset-name objects below. `status` is descriptive
text, not an execution limit. Each preset has all 303 numeric leaves; no field is
implicitly clamped, injected from a fixture or replaced by a zero default.

| Preset | Scheduling |
|---|---|
| `balanced-direct-translation-v1` | Existing four-worker, 512 MiB shared context configuration plus explicit translation policy |
| `serial-direct-translation-v1` | Same limits, with 40 explicit scheduling differences and the existing ordinary 64-bit restriction |

The existing `balanced-v1` and `serial-reference-v1` remain exactly their old
219-leaf shapes and values. An explicit old preset cannot run the new profile;
refuse the mismatch before capture. New presets can run static requests without
animation validation. Old static request/report wire shapes remain unchanged.

Six positive ten-field translation limit objects are projected into source
extraction, writer revalidation, memory reload extraction, memory inspection,
staged extraction and staged inspection. Four work fields apply at each actual
call. The preset JSON owns their exact values and seven explicit changes to
existing caps. These are limits, not reservations, measured memory fit or a
promise that all independent maxima can succeed simultaneously.

The producer intersects a validator's configured ceiling with the actual owning
remaining allowance before its first allocation. Ordinary operation growth is
frozen during transient validation. Retained extraction-proof bytes are measured,
frozen and subtracted before ordinary extraction allocations; its composite
getter counts the two disjoint live owners once. Existing memory aggregate
accounting consumes that actual composite. Validator sampled live bytes are not
added again to whole-operation totals. Backend, stop and worker ownership remain
actual. The file publisher's 288 MiB file-only cap stays unchanged; delegated
operations have separate caps on the same ledger, with no invented publisher
aggregate or process-memory claim.

## GTRAN-API-001: File API and exact context owners

The selected extension adds one strict keyword to the existing
[`repair_gltf_file`](gltf-python.md) interface:

```python
repair_gltf_file(
    source, destination, targets, *,
    primitive_ordinal, triangles, values,
    processing_profile="static-local-triangles-v1",
    input_id="source", resource_root=None, row_local_attributes=(),
    report_path="receipts/report.json", execution=None, cancellation=None,
    retain_failure_facts=False,
)
```

Existing target, path, value/triangle, declaration, cancellation
and retention rules keep their owners. Empty targets explicitly request unchanged
processing. No target discovery, automatic profile widening or transform baking
is added. Argument/preset/profile mismatches fail structurally before capture.

Direct file-API calls with `execution=None`, whether static or new-profile, share
one module-owned native context: one four-worker lease pool and 512 MiB payload
ledger, including concurrent calls and retained results. Immutable profile policy
handles select the appropriate old/new balanced preset over that same context.
They are not two independent default contexts. An explicitly supplied
`GltfExecution` remains its actual independent context and immutable preset.

Commands have a different existing boundary. With execution/cancellation omitted,
each single or batch invocation creates one fresh context and cancellation source;
all rows in that invocation share them. Preserve that behavior for old static
single/batch-1 and select it for new single/batch-2. They do not use the direct
API's module-default context. Explicit objects are reused exactly, including any
intentional sharing chosen by an embedding caller. The command passes its own
session explicitly to each prepared file operation.

## GTRAN-COMMAND-001: Single command and closed batch version 2

The selected single command adds `--processing-profile` with the two selection
strings above. Absence retains the current static path. Preset omission selects
the corresponding balanced policy in a fresh command-invocation context; an
incompatible explicit preset fails before capture. Console and module entry
points use the same parser and prepared operation. The existing
[command owner](gltf-commands.md) retains path isolation, admission ceilings,
report-reader preflight, ordered scheduling, fail-fast and delivery rules.

Select the profile explicitly with the single command:

```sh
meshvale-repair gltf source/scene.glb output/new --primitive-ordinal 0 --triangles require-triangles --values raw --target 0:1 --processing-profile direct-translation-v1
```

Batch `meshvale.repair.gltf-jobs/1` stays closed and rejects a
`processing_profile` field as unknown. New schema `meshvale.repair.gltf-jobs/2`
has exactly top-level `schema` and `jobs`. Every row has all old required/optional
keys plus REQUIRED `processing_profile`; no new top-level default/policy key.
Every row explicitly selects static or new mode. The full invocation is admitted
before any capture, including every row/session match and prepared result owner.
A malformed later row performs zero asset work.

```json
{"schema":"meshvale.repair.gltf-jobs/2","jobs":[
  {"id":"moving","input":"source/a.glb","destination":"output/a","targets":[[0,1]],"primitive_ordinal":0,"triangles":"require-triangles","values":"raw","processing_profile":"direct-translation-v1"},
  {"id":"static","input":"source/b.gltf","destination":"output/b","targets":[],"primitive_ordinal":0,"triangles":"require-triangles","values":"raw","processing_profile":"static-local-triangles-v1"}
]}
```

An omitted batch-2 execution creates one fresh new-balanced invocation context;
explicit new serial execution likewise spans the invocation. An old explicit
execution is allowed only when every row is static, checked before IO. Existing
16-job, 8 MiB request, one-million target-pair, 16 MiB detached-report and 8 MiB
framing ceilings stay unchanged. No schema auto-upgrade, per-row executor or
silently discarded field is permitted. Output batch framing retains its existing
report owner and raw native receipt forwarding.

## GTRAN-REPORT-001: Closed new-mode observations

New-mode core `profile.name` is
`gltf-direct-translation-duplicate-preservation` for memory or
`gltf-direct-translation-duplicate-publication` for a file workflow, with string
`profile.version` equal to `"1"`. These are preservation-profile versions, not
product release numbers. Existing static profile names, versions and
report bytes keep their current contracts.

For new mode only, the `meshvale.repair.gltf` extension has string `version: "2"`.
It retains every existing key/type/meaning and adds exactly `processing`, a closed
object with `profile`, `source`, `writing`, `reload`, `verification`,
`publication_reload` and `publication_verification`. `profile` is exactly
`direct-translation-v1`; the six other slots are null or an actual observation.
`operation.options` adds exactly `processing_profile` and
`direct_translation_limits`; the latter has exactly those six slot names and
actual resolved ten-field configured objects. Ordered target/declaration options
remain unchanged; core target references require a real matching input snapshot.

Each observation has exactly these fields:

| Field | Wire type and meaning |
|---|---|
| `status`, `diagnostic` | Actual internal GltfStatus ordinal 0-9 and the full matching GltfDiagnostic |
| `complete` | Bool, true only after the complete admitted validation/comparison succeeds |
| `node_rows`, `mesh_users`, `scanned_channels`, `affected_channels`, `distinct_samplers`, `keys_checked`, `decoded_bytes_checked`, `relationship_steps` | uint64 actual examined/admitted work under the producer's exact units; not successful-item or contiguous-prefix counts |
| `new_requested_bytes` | uint64 actual fresh live validator requests sampled after joins, before proof transfer/release; not peak, traffic or final operation bytes |
| `effective_request_cap` | uint64 real derived allowance applied before allocation; configured options remain unchanged |
| `work` | Exactly `parallel_min_keys`, `partition_keys`, `cancellation_block_keys`, `max_jobs`, actual resolved uint64 values |
| `execution` | Exactly `planned_jobs`, `launched_workers`, `joined_workers`, `caller_jobs`, actual uint64 scheduler facts after joins |

`diagnostic` has exactly `status`, `code`, `json_span`, `resource_kind`,
`resource_index`, `resource_span`, `rule_id`. Status/code use the unchanged public
numeric enums; spans are null or `{offset,length}` uint64 objects; resource kind
is null or the existing ordinal 0-2; resource index is null/uint64; rule ID is
uint32. Bool cannot substitute for integer. Missing actual locations stay null.
Its status equals observation status. Accepted uses the actual accepted/none
diagnostic. The internal result and its outer operation adapter remain distinct.

Null means the validator was not called, including an earlier phase, invalid
configuration/owner or a zero remaining allowance. An accounting-seed failure
before helper entry does not manufacture a zero-work observation. A called failure
retains actual facts immediately, before fallible report projection. Distinct
samplers/clips sharing accessor bytes still represent distinct canonical work;
channels sharing one sampler do not duplicate its key work. Failed keys and
performed predecessor rereads count under the producer's exact rules. Interrupted
parallel counts may be noncontiguous and schedule-dependent; deterministic first
semantic diagnostics are not fixed failure telemetry.

Append native translation check value 9 after optional solid-validity value 8,
without renumbering. Static required memory checks remain 0-7 with their old typed
coverage/report shapes; new mode requires 0-7 and 9, never optional 8. New memory
reports append one `translation-preservation` row. A requested file report projects
the conjunction of actual memory and its two staged predicates into that same row;
it does not emit a duplicate ID or overwrite the separately retained memory facts.
The new row uses the exact existing native coverage states:

| Actual condition at the translation predicate | Native coverage state / passed |
|---|---|
| All required memory predicates, plus staged predicates when requested, complete true | `kPerformed` / true |
| An actual malformed-data or binding/identity/comparison predicate evaluates false | `kPerformed` / false |
| Profile exclusion or extraction logical-limit-as-unsupported | `kUnsupported` / null |
| A called request/physical-budget/execution limit prevents evaluation, including writer/inspector logical-count failure | `kFailed` / null |
| Actual stop or an earlier different phase prevents evaluation | `kBlocked` / null |

Performed true/false uses coverage reason `kNone`; nonperformed checks use the
existing `kBlockedByEarlierStage` reason. The actual detailed owning diagnostic
remains in its retained record. A required performed false makes the preservation
profile failed; otherwise a nonperformed check makes it incomplete, and only all
required true passes. Thus `kFailed` coverage with null passed is incomplete,
without inventing a false predicate. Other existing failures may still dominate.
Use the actual input snapshot/mesh scope when present, otherwise the existing
allowed null snapshot/mesh scope with the translation-preservation subject. Do
not invent an animation mesh or snapshot. Existing maps remain actual
input/candidate face/corner/inverse maps and vertex identity; animation creates
no Geometry domain or map.

## GTRAN-FAILURE-001: Typed outcomes, stages and immutable receipts

The producer owns the exact internal-to-extraction/writer/inspection/file table.
Repair keeps the actual outer status/diagnostic and the called observation,
rather than translating only its numeric status. Existing memory adapters apply:

| Actual delegate failure | Repair status |
|---|---|
| Extraction canceled / payload budget / unsupported | `kCanceled` / `kBudgetExceeded` / `kUnsupported` |
| Other extraction failure | `kDelegatedFailure` |
| Writer canceled / payload budget / unsupported profile or selection | `kCanceled` / `kBudgetExceeded` / `kUnsupported` |
| Other writer failure, including a writer limit | `kDelegatedFailure` |
| Inspector canceled / payload budget / unsupported selection | `kCanceled` / `kBudgetExceeded` / `kUnsupported` |
| Other inspector failure | `kVerificationFailure` |

Initial extraction retains native `kExtract/kExtract`; writing `kWrite/kWrite`;
reload extraction `kReloadExtract/kReloadExtract`; inspection
`kVerify/kReloadMismatch`. Physical delegates keep actual file
`kExtraction/kDelegatedExtraction` or `kInspection/kDelegatedInspection`; Repair
retains them under file `kPublication/kPublication`. The retained publisher's
actual file status maps to Repair's file status as follows:

| Actual Interchange file status | Repair file status |
|---|---|
| `kCanceled` | `kCanceled` |
| `kBudgetExceeded` | `kBudgetExceeded` |
| `kLimitExceeded` | `kLimitExceeded` |
| `kUnsupportedProfile` | `kUnsupported` |
| `kInvalidConfiguration` | `kInvalidConfiguration` |
| `kSourceContractFailure`, `kForeignBudget` | `kSourceContractFailure` |
| `kPreservationMismatch` | `kVerificationFailure` |
| Every other failed file status | `kDelegatedFailure` |

The [native file owner](gltf-file-workflow.md) retains its actual publisher outcome,
cleanup and nested diagnostics. Earlier lexical/resource/report failures keep
their actual earlier stage and leave a new validator slot null if it did not run.

Every failed operation status, including `kUnsupported`, has failed execution and
exit 2. Actual cancellation is 130. Completed finite target rejection/incompleteness
keeps exit 1; completed passing work is 0. Python/CLI structural argument errors
fail before capture. Capture failure has actual capture diagnostics with no
Repair record, accepted candidate or invented target snapshot; command fallback
is failed/import/2 or cancelled/import/130. Escaping backend/runtime exceptions
join started workers and keep existing cleanup behavior without manufacturing a
normal returned result, observation or cleanup tuple.

Core report stages retain their owner: memory extract failures are `import`;
write/reload/reload-extract/verify are `verify`; actual file publication is
`export`, report preparation is `report`. Completed target rejection is
`complete`. Actual candidate/repair/maps survive later failure where retained.
Memory verification is not physical publication. A later commit failure can keep
passed preservation while execution/publication fail and expose no committed
output claim.

Successful extraction/writer/inspection owners expose actual
`DirectTranslationFacts()`; failed operation results retain called observations
without successful owners. File publisher inline slots retain actual extraction
and inspection returns. Its successful inspected-content owner exposes staged
facts before the borrowed report provider prepares the conditional receipt.
Only reached facts can appear in that immutable precommit report. Final publisher
outcomes/counters may be later typed observations and are not patched into it.
No postcommit allocation/callback/JSON patch establishes success. A stop or
exception does not promise a failure report. New-mode command fallback uses
`meshvale.repair.gltf.commands` string version `"2"`. It retains all existing
keys/types/meanings and adds exactly `processing_profile` (the actual selected
string) and `processing` (the same closed seven-key object above). The new-mode
`processing` object is not null: its six observation slots are nullable and remain
null when not called, including capture failure. It never claims an artifact or
substitutes default JSON for native facts.
Existing static fallback and batch framing keep their owners and shapes. Native
nonempty receipt bytes are forwarded byte-for-byte.

## GTRAN-VERIFY-001: Required implementation evidence

Use changed and unchanged JSON glTF/GLB with two directly animated users of one
mesh, unrelated geometry/material/resources, literal times/value bytes and every
actual repair map. Verify installed API, module, console, batch and native
consumers; remove source bytes after capture and retain results after caller
owners die. Independent output decoding must compare exact authored animation
references/TRS/resources and tiny dyadic values, not just geometry counts or the
producer's own report. No rendered-playback or performance result is implied.

Include malformed time/type/count/bounds/layout, valid unsupported affected
profiles and hidden shared users, foreign/stale proof, readable output
corruptions, real request/shared-ledger pressure, joined workers and interior
cancellation. Check API mixed-default shared admission separately from command
invocation isolation. Reuse existing publisher/scheduler failure boundaries with
focused cases. Old static API/command golden behavior and co-enabled modules must
remain actual. Source/package/installed/relocated and applicable sanitizer/CI
qualification on compatible producers is required before implemented/support
claims; this document alone establishes none of it.
