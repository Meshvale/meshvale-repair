# Reported duplicate repair

| Field | Value |
|---|---|
| ID | REPAIR-WORKFLOW-001 |
| Version | 0.1.2 |
| Status | Development in-memory interface; not released |
| Owner | Repair profile, verification and actual operation reports |

`meshvale_repair.workflow.repair_duplicate_mesh(mesh, targets, *, input_id="source", row_local_attributes=(), cancelled=None)` runs the existing [native duplicate operation](duplicates.md) on one immutable Geometry mesh. It returns a frozen `WorkflowResult` with nullable accepted `candidate`, detached JSON `report` and derived `exit_code`. Input remains usable on every outcome. This interface processes mesh snapshots. The separate [OBJ asset workflow](obj-workflow.md) composes import and verified publication around it; [OBJ commands](cli.md) consume that file interface.

Targets and custom row-local declarations follow the [Python operation contract](python.md). Resolve iterables once. Wrong types/shapes and invalid integer representations raise the same invocation exceptions before execution; representable out-of-range targets produce a rejected operation report. `input_id` is an opaque report-schema identifier, never a path. Input SHA-256 is null: this interface does not claim a file or canonical mesh hash. The report records both representative and removal indices, their pair relationship and resolved declarations.

## Required profile

Profile `exact-duplicate-preservation`, version `1`, requires:

- Input and candidate storage inspection have no diagnostics, including nonfinite authored data.
- Native maps describe exactly the requested removal, retained source face order and directed cyclic correspondence. Vertices are unchanged; removed corners map into their explicit representative face. No different surface patch, vertex connection or polygon subdivision is introduced.
- Positions and retained loops are byte-exact. Every channel retains metadata, type, semantic, set, width, dense/ragged representation, presence and retained row bytes. Merged authored rows compare numerically exactly under correspondence; two missing rows need not have equal backing bytes. Signed floating zero follows native equivalence. Vertex channels, including arbitrarily many UV/skin sets and variable influence rows, are unchanged.
- The input record remains byte-exact after processing.

These predicates are independently evaluated against the native candidate and maps; native acceptance alone does not pass the profile. Candidate verification failure rejects the workflow candidate while retaining a diagnostic snapshot and failed predicates. A native rejection has no candidate; candidate predicates are skipped and the profile is incomplete (or failed if an evaluated required predicate failed). An empty request still checks an independently owned unchanged copy.

Topology inspection runs on input and any native candidate. Its diagnostics and scoped native coverage are retained under `meshvale.repair` extension version `1`. A separate optional `topology.observed` predicate means an incidence snapshot was constructed; it does not mean manifoldness, closedness or consistent winding. Existing defects remain visible warnings and do not fail this preservation profile. Native unsupported geometric/solid checks are explicit optional unsupported coverage; they are never reported as passed. No general new-defect comparison, tolerance-based geometry predicate or solid-validity guarantee is implemented here.

## Outcomes and cancellation

Reports use Geometry's [version-1 envelope](https://github.com/Meshvale/meshvale-geometry/blob/36d36cba938ab660ad25c71c0afbb0d3f5963ff3/docs/reports.md). Coverage findings come from the predicates above. Accepted/unchanged reports include actual vertex/face/corner maps and per-channel preservation claims. Publication is `not-requested`, with no output snapshot/artifact claim. A passed in-memory operation is not a published file workflow.

Malformed storage fails execution at `inspect` before the native operation. Expected allocation failure during inspection, repair or verification becomes failed execution at its actual stage, with candidate predicates skipped if unavailable. Allocation failure while constructing/validating the report itself propagates because a report cannot be reliably produced without memory. Programming errors and exceptions from a caller's cancellation callback propagate rather than becoming successful results. Cancellation is cooperative: the optional zero-argument callback must return a bool; it is checked before inspection, before the native call, after the call and after verification. The synchronous native call is not interrupted mid-operation. Cancellation before final acceptance returns no accepted candidate and exit 130; source is retained. There are no borrowed mutable views.

Every returned report is checked by the public structural/semantic validator. Exit meanings follow that owner: 0 successful in-memory operation, 1 rejection/profile failure, 2 execution failure, 130 cancellation. Mutating a returned report changes that caller-owned dictionary only; the immutable candidate and stored exit code do not change. Validate again after intentionally modifying a report.

## Install and exercise

The optional `workflow` extra enables reference report validation. It uses the exact report-capable Geometry development dependency in [pyproject.toml](../pyproject.toml); build its public candidate first. Native-only builds and the original Python operation do not require the validator extra. There is no package-index release yet.

```sh
python -m pip install 'meshvale-repair[workflow]'
python -m unittest discover -s tests/workflow -v
python examples/python/reported_duplicate.py
```

The [original workflow tests](../tests/workflow/test_workflow.py) check reports against real operations, source protection, multiple UV sets/ragged skin data, non-manifold and malformed input, cancellation and faulty-candidate detection. File/resource and installed cross-adapter proofs belong to the separate [OBJ asset workflow](obj-workflow.md).
