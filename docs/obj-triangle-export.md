# Explicit OBJ triangle export

| Field | Value |
|---|---|
| ID | REPAIR-OBJ-TRIANGLES-001 |
| Version | 0.1.2 |
| Status | Development implementation; package and hosted qualification pending |
| Owner | Optional conversion/publication composition in the OBJ workflow |

This opt-in profile composes the existing targeted repair, Geometry's
[native triangulation](https://github.com/Meshvale/meshvale-geometry/blob/48298b24be7c132816fb5580d22d58bde61558d0/docs/triangulation.md)
and the [OBJ bundle publisher](obj-workflow.md). One final destination contains
the verified triangular OBJ, original referenced resources and its report.
The default polygon-preserving workflow retains its existing profile and wire
defaults. This contract does not establish a supported package release.

## File interface and admission

`meshvale_repair.obj_workflow.repair_obj_file` adds keyword-only arguments:

| Argument | Default | Meaning |
|---|---|---|
| `output_topology` | `"preserve"` | Exact string `"preserve"` or `"triangles"` |
| `triangulation_options` | `None` | Geometry `TriangulationOptions`; None uses its defaults |
| `triangulation_execution` | `None` | Geometry `ExecutionContext`; None creates a context for this call |

`"preserve"` keeps authored face loops. Non-None triangulation options/execution
are refused in that mode, before import, rather than silently ignored. Triangle
mode validates genuine Geometry option/context types before import; options
retain their existing strict integer/range rules. Invalid options, topology,
tokens or callbacks are invocation errors before capture. No source conversion
occurs to decide whether an invocation is valid.

Triangle mode requires either no cancellation token or an `ObjCancellation`
from `meshvale_repair.obj_workflow`. This focused owner exposes `request_stop()`
and readonly `stop_requested`; it owns genuine Interchange and Geometry stop
sources. First stop request returns true, later requests false. A stopped owner
remains stopped. Default preserve mode accepts either the existing Interchange
Cancellation or ObjCancellation. A bare Interchange token is refused for triangle
mode, since it cannot stop Geometry work during native execution.

Existing targets and row-local declarations keep their repair meanings. A rejected
repair never proceeds to conversion. Empty targets explicitly request unchanged
repair content followed by the selected export profile. An already-triangular
candidate still undergoes the selected conversion/verification; actual identity
maps and preservation are reported when its native topology is unchanged. Empty
mesh conversion follows Geometry's empty result/ranges and the adapter's actual
ability to represent that asset; no fabricated nonempty output is returned.

`ObjWorkflowResult.source` remains the imported asset. `candidate` remains the
accepted or unchanged **pre-export Repair polygon asset**, including on a later
conversion/publication failure. `entry` identifies only a committed bundle.
The output snapshot and export maps describe the actual staged triangular reload.
No second candidate role or public temporary conversion object is introduced.

```python
from meshvale_geometry import ExecutionContext, TriangulationOptions
from meshvale_repair.obj_workflow import ObjCancellation, repair_obj_file

stop = ObjCancellation()
result = repair_obj_file(
    "sources/model.obj", "outputs/triangles", [(1, 3)],
    output_topology="triangles",
    triangulation_options=TriangulationOptions(max_corners_per_face=4096),
    triangulation_execution=ExecutionContext(worker_budget=2),
    cancellation=stop,
)
```

The destination is new and its parent must exist, as in the existing interface.

The original [installed walkthrough](../examples/python/obj_triangle_export.py)
retains sources, three material bindings, texture bytes, repaired polygons,
triangular bundles and their separate correspondence. With the documented
`assets` dependencies installed, run from this source checkout:

```sh
python examples/python/obj_triangle_export.py new-triangle-example --commands
```

Its parent is the current directory and must exist. The example also retains
actual nonplanar refusal, native resource-budget refusal and pre-cancellation
reports; none publishes a destination.

## Single command and ordered batch

The existing `obj` command adds `--output-topology {preserve,triangles}` and
optional decimal integer flags `--triangulation-max-corners`,
`--triangulation-minimum-parallel-faces`, `--triangulation-workers` and
`--triangulation-payload-bytes`. The latter four require triangle mode. They
resolve respectively to native `max_corners_per_face`, `minimum_parallel_faces`,
`worker_budget` and `tracked_payload_budget_bytes`. No supplied knob is ignored.

```sh
meshvale-repair obj sources/model.obj outputs/triangles --target 1:3 --output-topology triangles --triangulation-workers 2
```

An unchanged-copy request uses the existing explicit `--unchanged`, together with
the triangle choice when conversion is desired. Console and module commands use
the same file interface and report/exit rules.

Legacy `meshvale.repair.obj-jobs/1` retains its exact closed fields. Triangle
requests use `meshvale.repair.obj-jobs/2`, through the same `obj-batch` command
and `run_obj_jobs` interface. Top-level fields remain exactly `schema` and `jobs`.
Each v2 job admits the v1 fields plus optional `output_topology` and
`triangulation`. Topology defaults to preserve. `triangulation`, when present,
must be an object with only these optional fields:

| Field | Default | Native owner |
|---|---|---|
| `max_corners_per_face` | 4096 | TriangulationOptions |
| `minimum_parallel_faces` | 64 | TriangulationOptions |
| `worker_budget` | 0 | ExecutionContext; zero selects available native concurrency |
| `tracked_payload_budget_bytes` | 268435456 | ExecutionContext |

Booleans/floats/strings are not substitutes for integers. Existing native range
validation applies, including a corner cap from3 through4096. An empty
triangulation object uses the listed defaults; its presence still requires
triangle mode. A zero payload budget is a valid configuration that can refuse
work; it is not a successful unbounded conversion. A job receives its own context
with these budgets. Ordered jobs do not overlap execution. Library callers may
instead share a genuine context across file-interface calls for native admission.

All rows, including the last, options and resource/destination isolation are
validated before any asset capture. Supplied bare Interchange cancellation is
refused if any job requests triangles. With no supplied token, one coupled owner
serves a mixed triangle/preserve invocation. Legacy all-preserve calls retain
their existing token path. Empty batches perform no conversion. Continue,
fail-fast, named unattempted reasons, SIGINT restoration and report-delivery
precedence remain owned by the [command contract](cli.md).

## Numerical profile and supported data

The new profile is `obj-exact-duplicate-triangle-publication`, version `1`.
Native Geometry requires simple single-loop, exactly represented-binary64 planar
faces of nonzero area, within the selected corner cap. Concave polygons, both
windings and arbitrary plane orientation use that existing native profile.
There is no coordinate welding, tolerance projection, generated vertex, index fan
or fallback for unsupported geometry. Nonplanar/crossing/degenerate faces are
diagnosed. Native conversion acts on the accepted Repair candidate.

The existing OBJ channel subset and asset/resource rules remain unchanged.
Native conversion retains vertex rows and replicates face rows/transfers corner
rows through actual source maps, including scalar encodings, values, present
masks and bindings. Additional UV sets, skin and arbitrary custom channels do not
become exportable. Final floating serialization and binding equality use the
adapter's existing numerical/semantic policy. Dyadic examples do not establish
byte-exact round trips for arbitrary decimal coordinates.

## Actual state, maps and receipt

Input is the imported polygon snapshot; candidate is the completed Repair
polygon state; output is the actual verified triangular reload. Original
input→candidate `repair-*` maps remain unchanged. `export-*` maps point from
candidate to output. Validate native face/corner source arrays and source-face
ranges, then invert them deterministically. Every output face/corner must have
exactly one native source and every admitted source face/corner an output row.
Vertex maps are identity. Use identity when actual maps/counts permit it;
otherwise explicit rows use an integer for one destination and a list for
multiple destinations. Do not guess triangles from face size alone.

For the original clean triangle/quad/pentagon example, actual export-face rows
are `[0,[1,2],[3,4,5]]`; actual export-corner rows are
`[0,1,2,4,[5,6],7,[3,8],[12,15],[9,13],10,[11,14,16],17]`.
Duplicate repair first retains its own4→3 face/corner maps, followed by these
3→6 maps. Native diagnostic face indices name the **candidate** face domain,
not automatically original input indices after repair.

Required checks additionally include `export.triangulation` and
`export.correspondence`. The former reports actual native profile acceptance;
the latter independently checks the prepared triangle storage, source arrays,
ranges and supported channel transfer before publication. Existing
`export.verified-reload` is fulfilled only by the publisher's real staged reload.
Changed polygon topology is a `converted` geometry claim with its reason and
export maps. A channel can claim preservation under those maps only when its
actual transferred and serialized rows/bindings justify it; serialization
conversions retain explicit reasons. Authored polygons are never claimed to be
reconstructed from triangles.

Triangle reports use `meshvale.repair.obj` extension version `2`; default reports
retain version `1`. The input hash remains `meshvale.obj-snapshot/1` over the
owned imported snapshot. For every admitted triangle invocation,
`operation.options` retains `row_local_attributes` and adds exactly
`output_topology="triangles"` and `triangulation`. The latter has exactly
`max_corners_per_face`, `minimum_parallel_faces`, `worker_budget` and
`tracked_payload_budget_bytes`. Record validated options and genuine context
properties; a zero requested worker budget is resolved by context construction
before import. These are configuration values, not evidence of executed work.
Allocation failure during admission remains a typed exception in the file
interface and command exit2 before any asset capture.

The v2 OBJ extension retains `input_hash_convention`, `adapter_version`,
`report_path` and `artifacts`, and adds exactly this `triangulation` object:

```json
{"budget_scope":"native-conversion","native":null}
```

`native` remains null until a genuine Geometry result is returned. Import or
Repair failure, a stop before conversion and an exception without a returned
native result do not invent zero counters. When a result returns, `native` is an
object with exactly `status`, `workers_used`, `serial_reason` and
`peak_tracked_payload_bytes`, copied from that result. Native status keeps its
own `accepted`/`blocked`/`canceled` spelling. Actual returned zero metrics remain
valid on a blocked/canceled result. A later projection/publication failure does
not erase returned native facts. No second copy of full correspondence arrays
is required there; optional existing `report_delivery` retains its owner.

V2 batch reports have
`extensions["meshvale.repair.commands"]={"version":"2","request_schema":"meshvale.repair.obj-jobs/2"}`.
V1 keeps its existing version1 object. The common `meshvale.report/1` and
`meshvale.batch/1` envelopes are unchanged.

Prepare the converted immutable asset, then publish only that asset. The verified
content callback constructs, validates, serializes and preallocates the success
receipt/result before native no-replace commit. The bundle includes that receipt
and hashed physical artifacts. There is no intermediate polygon publication or
new fallible result allocation after commit. Existing external report-sink
failure can report execution failure while retaining a committed publication.

## Failure, cancellation and execution scope

| Outcome | Repair facts and publication | Exit |
|---|---|---|
| Invalid invocation/options/token | No asset capture | 2 in commands; typed exception in file interface |
| Input failure or rejected repair | Existing facts retained; conversion not reached | Existing2/1 rules |
| Native geometric/corner-cap refusal | Completed Repair candidate retained; conversion check failed, execution completed/complete, publication not attempted | 1 |
| Native payload admission/size overflow/invalid candidate, allocation or projection failure | Repair candidate retained; execution failed/export, publication not attempted before publisher entry | 2 |
| Cancellation before publication | Actual completed Repair facts retained; no converted partial output published | 130 |
| Export/receipt/readback failure | Repair candidate retained; actual failed publication/report stage | 2 |
| Verified native commit | Committed output/receipt remains a publication fact | Existing postcommit rules |

`conversion.payload_budget`, `conversion.size_overflow` and
`conversion.invalid_candidate` are execution failures. Geometric diagnostics
such as nonplanarity/self-intersection/repeated positions and selected face corner
limit are profile refusals. Native cancellation is cancellation. Unexpected
programming exceptions retain the existing propagation contract. Report checks
that were not reached remain skipped with reasons; no failure obtains a prepared
success receipt or output snapshot. A later conversion refusal does not relabel
an accepted Repair candidate as a rejected repair.

Conversion is part of the existing wire `export` stage; it does not add a new
stage enum. Before publisher entry, use these exact outcome tuples:

| Situation | Execution outcome/stage/reason | Profile | Publication outcome |
|---|---|---|---|
| Native geometric refusal | completed / complete / null | failed: export.triangulation performed/failed; later export checks skipped | not-attempted |
| Native resource/size/invalid candidate or conversion preparation failure | failed / export / nonempty actual reason | failed or incomplete from actual required coverage | not-attempted |
| Stop after completed Repair and before publisher entry | cancelled / export / nonempty actual reason | actual required coverage, with unreached checks skipped | not-attempted |

For every unpublished tuple, publication is requested, snapshot is null,
artifacts are empty, verified is false and reason is nonempty. No output snapshot
or export maps are attached before verified reload. Original Repair maps remain
when that stage completed. Cancellation during import/inspect/repair keeps its
actual existing stage; after publisher entry the existing export/report failed
or cancelled publication rules apply. Completed geometric refusal has a null
execution reason even though diagnostics, coverage and publication retain their
specific reasons. The profile is always derived from actual required checks.

One ObjCancellation synchronously requests both owned native stop sources;
native calls receive their genuine token types. Stage transitions recheck stop.
Geometry releases the GIL for native work, allowing another Python thread to
request cancellation; no Python worker callback or token-watcher thread is
introduced. Existing I/O phase callbacks retain only their I/O meanings.
A synchronous Python signal handler may run when native execution returns;
prompt cancellation during native work uses another caller thread requesting
the owned token. Commands recheck stop before publication and restore SIGINT
handling after the invocation.

Geometry's context bounds native conversion worker/payload reservations only.
It does not bound OBJ import, existing copy-producing Repair, Python buffer/map/
JSON preparation, publication, retained returned owners or process RSS. Map
projection and receipt packing are serial checked marshalling under the GIL;
allocation failure occurs before commit. No new application-wide memory cap or
parallel speed claim is made. Native returned per-call metrics are distinct from
context lifetime peaks. Joined workers/reservations must be released on every
accepted, refused, canceled or exceptional path.
`workers_used` counts created/joined native worker threads. Serial caller
fallback therefore reports zero, including a one-worker budget.
