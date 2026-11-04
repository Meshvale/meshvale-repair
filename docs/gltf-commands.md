# glTF single-file and batch commands

| Field | Value |
|---|---|
| ID | REPAIR-GLTF-COMMANDS-001 |
| Version | 0.3.0 |
| Status | Development commands; retained-deformation integration and qualification pending; no supported release |
| Owner | Command admission, ordered scheduling, receipt framing and delivery |

These commands use the same [prepared file operation](gltf-python.md), native
policies, execution presets and verified publisher as the Python interface.
Install the declared `workflow` extra for Geometry's report reader/validator;
the glTF path does not require the Interchange Python extension. There is no
package-index release or supported compatibility matrix yet.

With the [matched development wheels](python.md) installed, run the
[self-contained walkthrough](../examples/python/gltf_walkthrough.py) from outside
the source tree:

```sh
python -I examples/python/gltf_walkthrough.py --commands
```

Use the example's absolute path when running elsewhere. It creates small JSON
glTF and embedded-BIN GLB inputs, runs single console/module commands and ordered
API/CLI batches, and checks physical receipts, resource hashes and exact
correspondence. The default mode demonstrates the file API. The selected
qualified profile is Windows x64 MSVC Release; `--unsupported-profile` checks
explicit native refusal on other currently unqualified numerical profiles.

The implemented opt-in [direct-translation development profile](gltf-translation.md)
preserves its bounded direct node-translation subset during the same operation.
Static processing remains the default. The profile establishes no supported
release or runtime floor.

## GLC-INVOCATION-001: Explicit operation

```sh
meshvale-repair gltf source/scene.glb output/new --primitive-ordinal 0 --triangles require-triangles --values raw --target 0:1
meshvale-repair gltf source/scene.gltf output/copy --primitive-ordinal 0 --triangles require-triangles --values raw --unchanged
meshvale-repair gltf-batch jobs.json --preset balanced-v1
```

`python -m meshvale_repair` uses the same front end. A single invocation requires
INPUT and DESTINATION, an explicit primitive ordinal, triangle/value policies and
either repeated `--target KEEP:REMOVE` or `--unchanged`. They retain the Python
interface's canonical-triangle meanings. Destination is required for commands;
the Python operation separately permits explicit `None` for no export. Content
selects JSON/GLB and publication retains the format.

Single options `--input-id` (default `source`), `--resource-root`, repeated
`--row-local DOMAIN:NAME` and `--report-path` (default `receipts/report.json`)
mirror the file interface. A single command also accepts `--processing-profile`
with default `static-local-triangles-v1` or opt-in `direct-translation-v1`.
The selected `deformation-retained-v1` path additionally requires the matching
deformation-capable native producer. Its Python admission/delivery code is under
qualification; complete native/Python/installed execution remains pending.
Both commands accept the original presets `balanced-v1` and
`serial-reference-v1`, plus `balanced-direct-translation-v1` and
`serial-direct-translation-v1`. The new `balanced-deformation-v1` and
`serial-deformation-v1` accept all three processing profiles. Original presets
remain static-only; translation presets accept static/translation. Preset
omission selects the matching balanced policy: batches select by schema even
when every row is static or the list is empty (version1 original, version2
translation, version3 deformation). Single commands select by explicit profile.
Each invocation creates one shared context/token for its rows. Incompatible
explicit old presets fail before filesystem work. Batch also accepts `--fail-fast`. No automatic primitive
discovery, target selection, custom native limits, phase callbacks or parallel
job scheduler is added. Help/version do not import optional report/asset modules.

## GLC-ADMISSION-001: Closed, bounded request

The existing static batch UTF-8 JSON has exactly `schema` and `jobs`, with schema
`meshvale.repair.gltf-jobs/1`. Each job requires `id`, `input`, `destination`,
`targets`, `primitive_ordinal`, `triangles`, `values`; optional fields are
`resource_root`, `row_local_attributes`, `report_path`. IDs are unique opaque
native report identifiers. Paths are nonempty strings; resource root may be null.
Targets and row declarations are arrays of two-element arrays. Empty targets are
intentional unchanged processing. Reject duplicate JSON keys, nonfinite numbers,
unknown/missing fields, booleans as indices, invalid policy/encoding and bounds
before capture. The native preparation path remains the conversion authority.

Closed schema `meshvale.repair.gltf-jobs/2` keeps those keys and row fields,
requires `processing_profile` on every row, and permits mixed static/translation
jobs. Version 1 remains closed and rejects that added field. The
[translation command contract](gltf-translation.md#gtran-command-001-single-command-and-closed-batch-version-2)
owns the complete version-2 example and profile/preset rules.

Closed schema `meshvale.repair.gltf-jobs/3` requires the same explicit fields as
version2 and permits all three profiles. It adds no per-row preset, session,
admission profile or numeric limits. Version1 still rejects a profile field and
version2 still rejects deformation. The [mixed request fixture](../tests/fixtures/gltf-deformation-jobs3.json)
shows version3 shape; it is input data, not a native execution result. The full
[retained-deformation contract](gltf-deformation.md#rgdef-jobs-001-closed-jobs3-and-session-admission)
owns compatibility, required source authority and the remaining qualification.
An explicit session must admit every actual row; an empty request is compatible
with any declared session. Without a session, version3 still creates one fresh
deformation-balanced command context, including for empty/all-legacy requests.

```json
{"schema":"meshvale.repair.gltf-jobs/1","jobs":[
  {"id":"part-a","input":"source/a.glb","destination":"output/a","targets":[[0,1]],"primitive_ordinal":0,"triangles":"require-triangles","values":"raw"},
  {"id":"part-b","input":"source/b.gltf","destination":"output/b","targets":[],"primitive_ordinal":0,"triangles":"require-triangles","values":"raw"}
]}
```

Admission ceilings are inclusive: 16 jobs, 8 MiB request bytes, 1,000,000 target
pairs in total, 16 MiB per detached report and 8 MiB outer-only batch framing.
The CLI reads at most 8 MiB plus one refusal lookahead before decoding. Parsed API
requests must be ordinary dictionaries/lists/scalars and also fit 8 MiB canonical
UTF-8 JSON with compact separators, `ensure_ascii=False` and no nonfinite values.
Validate shape/count limits before that bounded encoding. This is a separate
API-object admission rule; whitespace in a CLI file still counts toward its raw
byte limit. Native per-job declaration/name/path/target ceilings also apply.

Before any capture, validate all rows and path isolation, allocate every final
prepared result, and admit `16 MiB * job_count` of arithmetic report allowance
(at most 256 MiB), plus the separate 8 MiB framing allowance. These are wire-byte
ceilings, not preallocated storage, Python heap limits, native payload totals or
RSS guarantees. Report validation and lazy parsing can temporarily expand JSON.
The actual command also preflights its declared report reader and JSON-schema
runtime before capture. Help/version retain their optional-import independence.

CLI paths resolve from cwd; batch paths resolve from the request file's parent.
The API's base defaults to cwd. Use canonical existing ancestors for comparison,
while retaining logical source/resource arguments for native capture. Reject
equal/nested destinations, or any destination overlapping any job's source entry
or effective resource root (default source parent). Missing files/resources,
existing destinations and native unsupported profiles remain actual per-job
outcomes. Metadata comparison does not read asset contents or override native
link traversal rules. The file owner's concurrent-change and alias limits apply.

## GLC-RESULT-001: One operation, truthful detached bytes

`run_gltf_jobs(request, *, base=None, fail_fast=False, execution=None,
cancellation=None)` in `meshvale_repair.gltf_commands` returns an immutable
`GltfCommandResult` with `report_json: bytes`, `exit_code: int`, and a lazy `report`
property returning a fresh validated parsed value. It retains no native job owner
or cached parsed tree. Invalid invocation raises the front end's `InvocationError`
before processing. A typed `GltfCommandDeliveryError` carries exit 2 or 130 when
no complete truthful result can be constructed. Unexpected internal/native
exceptions propagate to embedding callers; the CLI emits a path-free diagnostic
and exit 2, without inventing a report.

One new `GltfExecution` and `GltfCancellation` span an invocation unless explicit
owning objects are supplied. Both must have the actual interface types;
`fail_fast` must be a bool. Prepare every final result through the same private
facade used by `repair_gltf_file`, then run each once in request order with
`retain_failure_facts=True`. Detach and validate its exact report bytes or actual
fallback observations; release its native owner before scheduling the next job.
Unscheduled prepared results own only converted arguments and are released too.

Nonempty native report bytes always win, including genuine failure reports.
Validate them without changing any bytes or publication facts. A single stdout
document is that exact byte slice plus newline. A batch frames each validated
single-document slice literally inside its attempted row; it does not reserialize
native receipts. Input identities/hashes and exits come from the validated actual
reports. Framing is command-owned; bundle receipts remain the native originals.

When no native bytes exist, the command produces an explicitly identified
Repair command fallback under Geometry's unchanged `meshvale.report/1` contract.
Known caller IDs and requested operation options remain; unavailable hashes are
null. Preserve actual candidate/execution/diagnostic observations. Accepted or
unchanged candidates require real retained source/candidate counts and complete
vertex/face/corner correspondence. Vertex identity follows Geometry's selection
contract; all other maps come from actual Repair. Before completed repair, no
snapshot/map is invented. No file table counts or reread reconstructs a mesh.

The fallback extension `meshvale.repair.gltf.commands` version `1` identifies its
command origin and missing native receipt, and projects whitelisted actual typed
capture/file/inner outcomes, diagnostics, execution and publication observations.
Enum domains/members and optionality stay explicit. System errors carry category
and value without machine paths/messages. A required skipped
`native-receipt-validation` check with null finding and a blocked-by-absence reason
derives an incomplete command profile. It does not pretend that native checks
failed or never ran. Preservation claims and publication artifacts stay empty;
successful publication requires its genuine native receipt.

## GLC-SCHEDULING-001: Cancellation, delivery and exits

Continue mode attempts all admitted jobs despite nonzero results. Fail-fast stops
after the first nonzero result, preserving its opaque ID in unscheduled reasons.
Attempted batch rows always contain complete reports. Unattempted rows carry the
known ID, null hash, null report and explicit reason. Uncancelled empty batches
complete with exit 0; an already cancelled token yields 130 at the same final
observation as every other batch. Use Geometry's actual batch/1 consistency and
exit rules.

The CLI temporarily handles SIGINT by requesting the shared native token and
restores the caller's handler. It raises no asynchronous exception across native
commit. Pre/between-job cancellation stops scheduling; actual completed job facts
stay unchanged. The final scheduling token observation is the batch completion
boundary. A later interrupt cannot relabel a completed batch. Single-job exit
comes from its actual report; a late token cannot erase committed success.

Each detached report must fit 16 MiB, including an honest fallback. Completed
maps may legitimately exceed that allowance. If a truthful attempted row cannot
be produced within it, stop scheduling and release remaining owners. Return no
aggregate; raise delivery failure with exit 130 for actually observed cancellation,
otherwise 2. Never relabel an attempted job, truncate correspondence, relax v1 or
emit a misleading partial aggregate. There is no emergency OOM report promise.
Previously committed bundles and their receipts remain true.

Stdout contains one UTF-8 JSON document and newline, with no progress text. Short
write/flush failure produces a path-free stderr diagnostic and exit 2, preserving
actual cancellation's 130 precedence. Never retry partial stdout or mutate native
receipts to describe delivery failure. Physical partial writes are possible; they
are not claimed as valid JSON. Prevent a second shutdown flush from replacing the
chosen process exit. Shell redirection is caller-owned, not atomic report storage.

Exit meanings/precedence are 130 cancellation, 2 execution/delivery failure,
1 rejection or failed/incomplete required profile, 0 successful processing, as
owned by Geometry's [report contract](https://github.com/Meshvale/meshvale-geometry/blob/48298b24be7c132816fb5580d22d58bde61558d0/docs/reports.md).
Native ledger limits, Python allocation and report-wire ceilings remain distinct.

## RGDEF-COMMAND-PROJECTION-001: Retained deformation selection

The [retained deformation contract](gltf-deformation.md) owns the opt-in profile's
single/batch preparation, source/read authority, policies and called facts. Its
complete native/runtime implementation and qualification remain pending; current
command source/component checks do not establish availability. Existing static/
direct-translation clauses, defaults and unchanged-request wire keep their meaning.
There is no implicit source upgrade or widening of a legacy schema/preset.

For deformation and absent native receipt bytes, the existing incomplete command
receipt profile remains `gltf-command-native-receipt/1`. The command extension
`meshvale.repair.gltf.commands` has string version`3`, retains the existing exact
keys and adds `processing_profile` and `processing`. Processing is the fresh
readonly binding's 15-key `deformation_facts` value, including all11 actual nullable
producer returns. The operation options receive the binding's seven-field
`deformation_options` configuration view; configured limits are never derived from
clipped telemetry. Missing/incoherent views cause delivery failure, not inferred
facts. The fixed top-level metadata must agree between those readonly views.

An unreached phase stays null. Numeric masks/refusals, quaternion words and partial
weight summaries remain as returned, without recomputation or normalization.
Actual nonempty native bytes always win and are forwarded exactly. Publication
without its genuine receipt remains a delivery error; fallback never establishes
verified publication or borrowed native preservation checks. Batch framing stays
`meshvale.batch/1`, and all previous wire ceilings, order, cancellation/fail-fast
precedence and final-owner release boundaries remain unchanged. Commands create
fresh invocation contexts; they do not borrow the direct API's module default
context or allocate another context per row.
