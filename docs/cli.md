# OBJ commands and batch scheduling

| Field | Value |
|---|---|
| ID | REPAIR-CLI-001 |
| Version | 0.1.0 |
| Status | Implemented development commands; installed desktop proofs; not released |
| Owner | Invocation, sequential scheduling, cancellation and command report delivery |

Install the optional `assets` extra with the exact public candidates in [pyproject.toml](../pyproject.toml). `meshvale-repair` and `python -m meshvale_repair` select the same front end. Native-only and primitive Python consumption remain independent of those optional dependencies. Commands use [the existing file workflow](obj-workflow.md); they do not implement a second repair algorithm, preservation profile or exporter. There is no package-index release yet.

## Single asset

```sh
meshvale-repair obj sources/part/model.obj outputs/part --target 1:3 --input-id part
meshvale-repair obj sources/part/model.obj outputs/copy --unchanged
```

At least one `--target KEEP:REMOVE` or explicit `--unchanged` is required; they are mutually exclusive. Indices are decimal uint64 source-face rows. Repeat targets for one atomic request. Options `--input-id` (default `source`), `--resource-root`, repeated `--row-local DOMAIN:NAME`, and `--report-path` mirror the file interface. Paths on the command line resolve from the current directory. Destination must be a new directory with an existing parent; the command creates no guessed parent tree. Empty targets explicitly request verified unchanged-copy publication.

The command writes one validated `meshvale.report/1` JSON document plus newline to stdout and uses its derived exit meaning. Each successfully published bundle contains its independent verified success receipt. Wrong invocation produces exit 2, a path-free diagnostic on stderr and no report or processing. Expected per-asset failures/rejections/cancellation come from the real workflow. `--help` and `--version` require no optional asset runtime.

## Batch request

```sh
meshvale-repair obj-batch jobs.json
meshvale-repair obj-batch jobs.json --fail-fast
```

The UTF-8 JSON request has closed fields and schema `meshvale.repair.obj-jobs/1`. Duplicate object keys and nonfinite numbers are refused. Top-level fields are exactly `schema` and `jobs`. Each job requires `id`, `input`, `destination`, `targets`; optional fields are `resource_root`, `row_local_attributes`, `report_path`. IDs are unique opaque report identifiers. Paths are nonempty strings; optional `resource_root` may be null. Targets and row-local declarations are arrays of pairs using the file interface's representation. An explicit empty target array requests an unchanged copy. Unknown fields or missing fields fail the whole invocation. Relative input/destination/resource-root paths resolve against the request file's containing directory; the receipt path stays bundle-relative. Empty batches complete with exit 0.

```json
{
  "schema": "meshvale.repair.obj-jobs/1",
  "jobs": [
    {"id": "part-a", "input": "sources/a/model.obj", "destination": "outputs/a", "targets": [[1, 3]]},
    {"id": "part-b", "input": "sources/b/model.obj", "destination": "outputs/b", "targets": []}
  ]
}
```

All invocation representations and path conflicts are checked before the first job, including UTF-8 representability of paths and native declaration strings. Shared pure validation also serves the file interface. Missing files/resources, unsupported imports and existing destinations remain actual per-job processing failures, not guessed invocation results. Canonical comparison paths resolve existing ancestors/symlinks; actual import arguments retain their logical entry names and authored resource-reference bases. Equal/nested destinations and destinations overlapping any effective input/resource root are invocation errors. The effective root defaults to the source parent. This conservative restriction also applies to the single command. Repeated sources with isolated destinations are allowed. It avoids creating or overwriting another job's source/resource domain; it does not promise protection against concurrent filesystem changes, hard-link aliases to nonexistent outputs or hostile mutation.

Jobs run sequentially in request order using `repair_obj_file`, without an external per-job report sink. `run_obj_jobs(request, *, base=None, fail_fast=False, cancellation=None, on_job_phase=None)` exposes the same scheduler for embedding; `base` defaults to the current directory, the token is Interchange's `Cancellation`, and optional `on_job_phase(job_id, phase)` forwards the actual publisher's phase callback rules. It returns a detached validated `meshvale.batch/1` dictionary. Wrong invocation raises `InvocationError` before processing. Unexpected internal programming exceptions propagate; command callers return exit 2 without fabricating a job report when trustworthy aggregate reporting is impossible. Already published receipts retain their evidence.

Default `continue` attempts every job despite nonzero per-asset results. Explicit `fail-fast` stops after the first nonzero result, retaining a reason naming the triggering opaque ID for every unattempted job. Attempted rows preserve the returned single report and copy its actual input identity/hash. Unattempted rows use their opaque input ID, null hash, no report and an explicit reason. No resolved machine path or raw exception message enters command-produced reports/diagnostics.

## Cancellation, delivery and exits

Commands temporarily install a SIGINT handler that requests the shared cooperative token and restore the caller's handler afterward. The same token reaches import, mesh checkpoints and publication. It does not raise an asynchronous `KeyboardInterrupt` across native commit. Cancellation before/between jobs stops scheduling with explicit reasons. An attempted cancelled job retains its returned report. A cancellation observed after a current publication preserves that publication while stopping later jobs. The completion boundary is the scheduler's final token observation: a later interrupt cannot relabel a completed job/batch or erase output. Process termination retains the publisher's stated limits.

Batch stdout is one validated aggregate JSON document, with no progress mixed into stdout. Aggregate delivery follows scheduling; writing/flush failure returns exit 2, records report-stage failure in the in-process report, and retains all published job facts. Single-report delivery uses the same postpublication semantics as the file interface. Cancellation retains exit 130 precedence over accompanying delivery failure. A broken stdout stream is not recursively retried; stderr receives a path-free diagnostic if available. Partial stdout writes are possible on stream failure; successful bundles still contain their own receipts. Shell redirection belongs to the caller and is not an atomic report-file facility.

Exit meanings/precedence belong to Geometry's [report owner](https://github.com/Meshvale/meshvale-geometry/blob/d1b0ec5ddf37d6ff648ddd57fe8c7caefbd78832/docs/reports.md#batch-and-exit-rules): 130 cancellation, 2 execution/delivery failure, 1 rejection or failed/incomplete required profile, 0 successful processing. Completed fail-fast execution may have unattempted jobs but retains the failed job's exit. This interface offers no parallel scheduler, automatic duplicate selection, other format support or released compatibility matrix.
