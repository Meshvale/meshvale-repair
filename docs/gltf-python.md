# Python glTF/GLB file workflow

| Field | Value |
|---|---|
| ID | REPAIR-GLTF-PYTHON-001 |
| Version | 0.1.1 |
| Status | Development interface; qualification in progress |
| Owner | Python path arguments, application execution presets and result observations |
| Native owner | [Retained-file workflow](gltf-file-workflow.md) |

This interface captures one file asset through Interchange and calls the existing
native retained-file Repair operation exactly once. Native capture, extraction,
targeted repair, preservation, reporting and publication retain their existing
owners. The interface handles the selected static primitive; native unsupported
skin, morph, animation and extension cases remain explicit outcomes.

## GLTF-PY-REQUEST-001: Explicit request

```python
repair_gltf_file(
    source, destination, targets, *,
    primitive_ordinal, triangles, values,
    input_id="source", resource_root=None, row_local_attributes=(),
    report_path="receipts/report.json", execution=None, cancellation=None,
    retain_failure_facts=False,
)
```

Import this function from `meshvale_repair.gltf_workflow`. Source, destination and
targets are required. An explicit `destination=None` requests no publication;
otherwise the native publisher requires a new directory with an existing parent.
Content selects JSON glTF or GLB; publication retains that format and relative
entry path. Empty targets explicitly request unchanged processing.

`primitive_ordinal` is a nonnegative uint64 ordinal in the captured primitive
enumeration, rather than a mesh or instance index. Triangle policy is required:
`require-triangles` or `convert-strip-or-fan`. Value policy is required: `raw` or
`raw-and-normalized`. Target pairs are `(keep, remove)` in the selected canonical
triangle view, including any explicitly requested strip/fan conversion. Row-local
declarations are `(domain, name)`, with domain `vertex`, `face` or `corner`; native
attribute policy remains authoritative. No primitive discovery, automatic target
selection, instance transformation or implicit channel normalization is added.

Validate and own all arguments before capture. Reject booleans as indices,
nonintegers, negative/overflow indices, malformed pairs, unknown policies and
malformed opaque ASCII input identifiers. Bound iterable consumption and native
request growth by the selected preset, with one lookahead to reject excess.
The declaration-name limit bounds their combined UTF-8 bytes, rather than each
name independently.
Never trust a custom length to authorize an unbounded allocation. Conversion
failures raise `TypeError`, `ValueError` or `OverflowError`; allocation failure
may raise `MemoryError`. These errors do not fabricate a native result.

Filesystem paths accept Unicode strings or `os.PathLike[str]`; reject bytes and
embedded NUL. Copy Windows paths through explicit-length wide conversion before
IO, preserving non-ASCII and non-BMP characters. Relative report paths remain
UTF-8 and native portable-path admission applies. `resource_root=None` uses the
entry's parent through the native default. Capture rejects opaque extensions by
default; this surface supplies no extension-file discovery or supplemental files.

`retain_failure_facts` must be an actual `bool`. The default `False` preserves
release of completed repair payload when no native memory record survives.
`True` selects the native [completed-facts retention policy](gltf-retained-facts.md).
It is per invocation, with no execution-session setting. Retained failures can
consume later session headroom until their last owning result is released.

## GLTF-PY-EXECUTION-001: Owned sessions and cancellation

`GltfExecution(preset="balanced-v1")` owns one native execution context and the
complete immutable application preset. `serial-reference-v1` uses the same caps
with one worker and caller-only thresholds. Resolved preset values are observable;
changing a detached observation never changes the session.

`execution=None` selects one Python module-owned default session for that
interpreter. Default-session calls, including concurrent calls and retained
results, share its 512 MiB declared native payload ledger and at most four native
workers. An explicit `GltfExecution()` creates an independent session. Native C++
does not cache a process-global Python object. This selects no additional
subinterpreter, free-threaded Python or cross-interpreter compatibility.

Capture and every workflow delegate use the same context. Results retain native
owners and their charges after the Python session wrapper dies. Separate capture,
parser, GLB container, memory-engine, wrapper and publisher request caps remain
separate observations; unavailable delegated totals remain unavailable. The
512 MiB ledger excludes Python objects, converted caller storage, filesystem and
runtime internals. It is not a process-memory or peak-memory ceiling.

Application presets have complete positive nested fields. Native actual remaining
admission checks the complete materialization allowance before delegation; it
does not clamp its three independently supplied request caps. A file satisfying
one physical limit can still fail another cap or exhaust the common ledger while
other owners survive. Preset capacities are ceilings, not guaranteed asset sizes.
Execution observations report actual admitted work; performance claims require
separate measurement.

`GltfCancellation.cancel()` returns the native stop source's actual request result;
its `cancelled` property reads the actual token state. Preparation copies that
owning source, or creates one when none is supplied. Reusing an object deliberately
shares cancellation across calls. The function releases the GIL for native
capture and the operation, joins all admitted work and keeps every request borrow
alive through return. It installs no Python callback or signal handler. A stop
arriving after commit cannot undo publication.

## GLTF-PY-RESULT-001: Actual immutable observations

The result is one immutable native-backed `GltfFileWorkflowResult`. Properties
are lazy and may allocate when requested. Native expected failures return actual
reached-stage facts; unexpected native exceptions propagate after owning joins
and best-effort cleanup, without asserting an invented cleanup outcome.

| Property | Observation |
|---|---|
| `workflow_started` | Whether the retained-file operation was invoked |
| `capture_status`, `capture_diagnostic`, `capture_execution` | Exact native capture observations |
| `status`, `outcomes`, `diagnostic`, `execution` | Exact native workflow observations; `None` when capture prevented the operation |
| `memory_outcomes` | Exact optional returned inner outcomes, even without a memory record; `None` if the inner operation never returned |
| `publication` | Actual optional publisher outcome, phase, diagnostic, cleanup/error and execution; absent when not invoked |
| `requested_bytes` | Actual immutable record counters when a record exists, preserving optional/unavailable values |
| `source_format`, `source_entry_path` | Successful capture's format and relative entry path; otherwise `None` |
| `published_entry_path`, `published_report_path` | Actual surviving published owner's relative paths; otherwise `None` |
| `report_json` | Exact nonempty native report bytes copied on access; otherwise `None` |
| `face_map`, `corner_map`, `source_faces`, `source_corners` | Immutable tuple copies of the reached native repair owner's actual maps; otherwise `None` |
| `input_snapshot_counts`, `candidate_snapshot_counts` | Optional immutable `(vertices, faces, corners)` tuples from the actual completed Repair owner; otherwise `None` |

Maps and counts use the ordinary memory Repair owner or the mutually exclusive
opt-in fallback. No pre-repair snapshot is inferred. `requested_bytes.retained_repair`
is the optional fallback source-Geometry/Repair live subset described by the
native retention contract; ordinary `memory` counters exclude it. Missing native
receipt bytes remain `None`. These observations create no native check coverage,
profile completion or publication receipt.

Diagnostic and execution observations preserve their public native field names,
enum domains, nested active variants and optionality. They do not flatten a
delegated diagnostic to a guessed message or reconstruct publication from a stop
token. System errors expose the actual category and integer value. No numeric
cross-version native ABI is promised.

Capture failure exposes its diagnostic and execution, with no workflow outcome
or report. It is not assigned a fabricated report or exit code. A future command
adapter must describe only those reached facts. Existing genuine native failure
reports are never replaced by an adapter. A successful publication's report bytes
remain the actual verified bundled receipt; no Python serialization or mandatory
reread follows commit.

## GLTF-PY-RETURN-001: Prepared result handoff

Allocate the final Python result and its inline native slots before capture.
Retain converted paths, target/declaration backing, context and stop ownership in
that same result through all native work. The internal execution call accepts
one borrowed handle and returns the already owned Python object through the
pinned binding runtime's pointer-transfer path. Require actual nothrow aggregate
and slot handoff traits; no automatic native-result wrapper conversion, callback,
postcall registration or report construction follows native commit.

This guarantee covers binding-owned native allocations and conversions. CPython
asynchronous exceptions, unrelated threads, interpreter runtime failure and OS
allocation retain their own behavior. Lazy property allocation occurs after the
result is returned and can fail without changing its retained native publication
facts. Qualification must exercise the actual compiled return path, pre-capture
result allocation refusal and lazy allocation failure separately.

The opt-in `MESHVALE_REPAIR_GLTF_BINDING_TESTS` target builds an isolated
Windows MSVC Release CPython 3.13 test module. It wraps saved CPython allocators
only in its own controlled test process, observes the shared aggregate
assignment and compiled object return, and restores allocators before reporting
test results or translating failures. Original native request observers cover
Repair's prepared-report boundary; they do not replace the real publisher or
claim that publisher internals cannot allocate. Real wrapper-allocation positive
controls and separate pre-instance/lazy-report refusals check observer sensitivity.
Additional controls cancel through the shared native stop source from an actual
joined report worker. They exercise JSON/GLB changed/unchanged default and opt-in
returns, exact retained counts/maps/byte attribution, lazy retained-property
allocation refusal and strict boolean admission. The test variant captures its
token before native work; production contains no fault behavior. These controls
preserve all original publication-return checks and require the public helper's
explicit inventories.
The module has no install rule and is absent from production wheels. With pinned
nanobind and a selected x64 compiler environment, run the public helper against
the same installed native prefixes used for the wheel:

```sh
python scripts/check-gltf-python-return.py --build .local/return-build --output .local/return-proof --prefixes "$NATIVE_PREFIXES"
```

Use a fresh output directory. The Windows portable candidate lane runs this gate
for both source and Git-free archive builds and retains its commands and results.

## GLTF-PY-BUILD-001: Qualification and package ownership

The existing `_repair` extension owns this interface; no glTF C++ asset, context
or Mesh object crosses product extension runtimes. Existing OBJ APIs retain the
canonical Geometry record seam. Build the compatible compiled native producers,
Python dependency versions and static dependency notices together.

Initial qualification targets ordinary GIL-enabled Windows x64 MSVC Release
CPython 3.13. Other numerical profiles must return their real native unsupported
outcomes unless separately qualified. Source/archive wheels, clean installed
JSON/GLB examples, Unicode paths, actual receipts/hashes/maps, failure/cancellation,
shared-session lifetime/GIL behavior, all OBJ/import-order regressions and actual
public checks remain required. A design contract or a native test is not an
installed Python capability or a supported software release.
