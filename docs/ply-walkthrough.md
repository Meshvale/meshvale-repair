# Repair one PLY file or an ordered batch

This development walkthrough uses the explicit duplicate-face operation. It
removes a face only when the supplied keep/remove pair passes the native checks.
It does not discover duplicates. The [workflow contract](ply-workflow.md) owns
preservation and resource guarantees; [report fields](ply-reports.md) describe
what was actually attempted and verified.

## Prepare the development build

Use the compatible native SDKs and ordinary Geometry Python wheel described in
[ENVIRONMENT.md](../ENVIRONMENT.md). Build Repair with
`MESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON` and `MESHVALE_REPAIR_PYTHON=ON`, and install
its declared `workflow` extra for report validation. PLY uses native Interchange;
it does not require the Interchange Python package. This is a development
interface; these instructions do not declare a supported wheel or platform floor.

Confirm that the interpreter uses your installed PLY-enabled build:

```sh
python -c "from meshvale_repair.ply_workflow import PlyExecution; print(PlyExecution().preset)"
```

The result should be `balanced-v1`. Module and console commands use that same
installed package. Use a new working directory for the following files.

## Create a small input

The two triangles below use the same vertices and cyclic corner order. Face
indices start at zero. This fixture has three vertices, two faces and six corners.

```python
from pathlib import Path

source = Path("triangles.ply")
with source.open("x", encoding="ascii", newline="\n") as stream:
    stream.write("""ply
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
""")
```

## Repair a single file

```sh
meshvale-repair ply triangles.ply repaired-ascii --encoding ascii --target 0:1
```

The equivalent module entry point is:

```sh
python -m meshvale_repair ply triangles.ply repaired-binary --encoding binary_little_endian --target 0:1
```

Each destination is a new bundle directory containing `triangles.ply` and
`meshvale-report.json`. Its returned stdout document is the genuine native
receipt, followed by a newline. The changed output has three vertices, one face
and three corners; all vertex rows remain. The destination must be new.

Output encoding is required: choose `ascii`, `binary_little_endian` or
`binary_big_endian`. `--unchanged` deliberately supplies an empty target list and
still performs canonical writing and verification. Canonical output preserves
admitted typed values and schema, while comments, whitespace and numeric spelling
may change.

For custom properties, explicitly declare each applicable safe row-local
property, for example `--row-local face:score` or `--row-local vertex:label`.
A declaration asserts that property values are safe across the changed identities;
its presence does not discover or prove that meaning. Omit declarations for this
position-and-topology-only fixture. Valid unused declarations are permitted.

## Use the same operation from Python

Choose a fresh destination distinct from the command examples:

```python
from pathlib import Path
from meshvale_repair.ply_workflow import repair_ply_file
from meshvale_reports import load, exit_code

result = repair_ply_file(
    Path("triangles.ply"), Path("repaired-api"), [(0, 1)],
    encoding="binary_big_endian",
)
assert result.exit_code == 0
assert result.input_snapshot_counts == (3, 2, 6)
assert result.candidate_snapshot_counts == (3, 1, 3)
assert result.face_map == (0, 0)
assert result.source_faces == (0,)
assert exit_code(load(result.report_json)) == 0
```

The owned result exposes actual capture/workflow/publication observations,
original and output bytes, ordered schema, encoding and four correspondence
arrays. Writer facts can survive a later reload failure; verified output bytes
remain unavailable until the required memory checks pass. Keep the result or an
owned schema view alive while inspecting it.

Pass `destination=None` for real memory-only writing/reload without publication.
Pass `row_local_attributes=[{"domain": "face", "name": "score"}]` for an explicit
custom-property assertion. `PlyExecution(preset="serial-reference-v1")` selects
the documented serial reference policy; the default module session uses
`balanced-v1`. Both come from the [complete preset table](ply-presets.md).

## Process an ordered batch

Save this as `jobs.json` beside `triangles.ply`. Relative paths are interpreted
from the request file's logical directory.

```json
{
  "schema": "meshvale.repair.ply-jobs/1",
  "jobs": [
    {"id": "changed", "input": "triangles.ply", "destination": "batch-changed",
     "targets": [[0, 1]], "encoding": "ascii"},
    {"id": "unchanged", "input": "triangles.ply", "destination": "batch-unchanged",
     "targets": [], "encoding": "binary_big_endian"}
  ]
}
```

```sh
meshvale-repair ply-batch jobs.json
```

The complete invocation is admitted before any asset capture, including all
source/output isolation and declaration checks. Jobs run once in order, and the
batch embeds the exact validated native receipt bytes. `--fail-fast` leaves later
jobs unattempted after the first nonzero result. A missing source is an attempted
failure; an invalid later job refuses the complete invocation before capture.

Exit codes are `0` for complete success, `1` for rejection or an incomplete/failed
profile, `2` for execution or delivery failure, and `130` for cancellation.
A stdout delivery failure does not undo a committed bundle. Inspect the actual
receipt and publication observations before deciding what to do next.
