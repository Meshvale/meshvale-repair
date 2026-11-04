# Keep an inspectable PLY repair example

Run the [installed Python example](../examples/python/ply_walkthrough.py) to
repair the same small ASCII asset through the API, module and console commands,
and ordered batches. It keeps inputs, output bundles and reports in a new
directory so you can inspect them after the process exits.

This is explicit duplicate-face removal: you supply a keep/remove pair. The
operation does not find duplicates, weld vertices, fix winding, triangulate or
establish solid validity. The [PLY workflow contract](ply-workflow.md) owns the
preservation rules and [PLY report contract](ply-reports.md) owns actual report
fields. This example checks position/topology-only ASCII; it does not qualify
other schemas, encodings, runtimes or platforms.

## Use an installed development build

Follow [PLY Python build and installed checks](../ENVIRONMENT.md#ply-python-build-and-installed-checks)
for compatible native SDKs, the declared Geometry Python wheel and a Repair
wheel built with `MESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON`. Install its `workflow`
extra for shared report validation. PLY uses native Interchange and does not
require the optional Interchange Python package. There is no package-index wheel
or supported runtime floor established by this walkthrough.

`REPAIR_SOURCE` below is the public checkout or source archive containing the
example. `OUTPUT` is an absent directory with an existing parent; keep it outside
the source tree. Use the ordinary interpreter into which the wheels were
installed. The example requires a profile that actually performs these native
operations; an unsupported outcome fails its checks rather than being counted
as a successful repair. Shell variable notation may need adapting for your host.

```sh
python -m pip check
python -I "$REPAIR_SOURCE/examples/python/ply_walkthrough.py" "$OUTPUT"
```

The script finds `meshvale-repair` in this interpreter's scripts directory; an
explicit `--console` path must belong to that same directory. It checks installed
module origins and writes actual interpreter/package identities to `runtime.json`.
It installs nothing and refuses to reuse an existing output directory.

## What the inputs mean

The original fixture from this guide has three vertices and two cyclically
identical directed triangles, indexed from zero:

```text
ply
format ascii 1.0
element vertex 3
property double x
property double y
property double z
element face 2
property list uchar uint vertex_indices
end_header
0 0 0
1 0 0
0 1 0
3 0 1 2
3 1 2 0
```

All five cases run through every route:

| Case | Explicit request | Expected result |
|---|---|---|
| `changed` | Keep face0, remove face1 | Accepted;3 vertices,1 face,3 corners |
| `unchanged` | Empty targets on the two-face input | Unchanged;3 vertices,2 faces,6 corners |
| `clean` | Empty targets on a genuinely one-face input | Unchanged;3 vertices,1 face,3 corners |
| `reverse` | Keep face0, remove reversed loop`0 2 1` | Rejected; no destination |
| `invalid-target` | Keep face0, remove nonexistent face99 | Rejected; no destination |

The changed case retains face0 and corners0,1,2. Its forward face map is
`[0,0]` and corner map is `[0,1,2,1,2,0]`; inverse source faces/corners are
`[0]` and `[0,1,2]`. Every vertex remains. Empty targets deliberately request no
edit, even on the still-duplicated input. A reverse-winding face is not accepted
as the requested directed duplicate.

## Inspect the results

There are30 actual workflow attempts: five API calls, five module single calls,
five console single calls, and five jobs in each API/module/console batch. Single
success exits0 and refusal exits1. Each batch uses continue mode, attempts all
five jobs in order and exits1 because two jobs are rejected. No fail-fast or
skip hides the refused cases.

Each successful destination contains exactly the source entry basename and
`meshvale-report.json`. The script checks exact receipt equality, SHA-256 of the
physical entry, all four edit maps, report snapshot counts and forward/export
map scopes. Its small independent ASCII decoder checks ordered double XYZ /
uchar-count uint-index schema, every position bit and exact directed loops.
It does not call a product reader to manufacture expected output. Canonical
serialization may change whitespace, comments and numeric spelling even for
an unchanged candidate; lexical equality is not the preservation promise.

Inspect `api-*.json`, single-command `.stdout`/`.stderr`/`.process.json`, batch
requests and aggregate streams. The original batch document retains refused
native reports; only successful jobs have bundled receipts. `results.json`
records each observed case/route and unchanged source hashes. The script compares
semantic results across routes rather than requiring allocation/worker statistics
to match. It requires zero active session workers and tracked payload after
releasing API results and batches, and compares original physical input bytes
after all operations.

Output encoding is explicit `ascii` here. The workflow also defines
`binary_little_endian` and `binary_big_endian`; this ASCII example does not test
them. Custom properties require caller-owned safety declarations, for example
`--row-local face:score`; spelling alone does not prove row-local semantics.
No such declaration is needed for these XYZ/topology-only inputs. Units remain
unknown. Broader fault/platform checks and supported releases remain separate.
