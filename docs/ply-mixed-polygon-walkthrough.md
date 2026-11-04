# Preserve mixed PLY polygons and region values

The original [ASCII exact-copy fixture](../tests/fixtures/ply-mixed-polygons/exact.ascii.ply)
contains ten vertices, a triangle, a quad, a concave pentagon and one duplicate
quad. The operation removes that explicitly selected duplicate, retaining the
three original polygon loops. It does not triangulate, weld vertices, repair
winding or establish solid validity.

Use the installed development build described in the
[PLY guide](ply-walkthrough.md#use-an-installed-development-build). This example
needs the opt-in PLY workflow and its `workflow` report extra. It does not need
Interchange's Python extension or establish a supported wheel/runtime floor.

Each face has an authored unsigned `region_code`:101 for the triangle,202 for
the quad and its copy,303 for the pentagon. These application labels contain no
topology references. Explicitly declaring `face:region_code` is appropriate for
this original data. Do not infer safety for an unknown property with the same
name, or declare a property containing face/corner IDs safe across removal.

From the Repair checkout or source archive, choose a new destination with an
existing parent:

```sh
python -m meshvale_repair ply tests/fixtures/ply-mixed-polygons/exact.ascii.ply "$OUTPUT" --encoding ascii --target 1:3 --row-local face:region_code
```

The equivalent console command starts with `meshvale-repair ply`. The destination
contains the verified PLY entry and `meshvale-report.json`. The report records
accepted4→3 faces, forward face map `[0,1,2,1]` and original source faces
`[0,1,2]`. All ten vertex rows remain, and the face region values are101,202,303.
The reusable API invokes the same native workflow:

```python
from meshvale_repair.ply_workflow import repair_ply_file

result = repair_ply_file(
    "tests/fixtures/ply-mixed-polygons/exact.ascii.ply",
    "output/new-ply-bundle",
    [(1, 3)],
    encoding="ascii",
    row_local_attributes=[{"domain": "face", "name": "region_code"}],
)
assert result.workflow_started and result.exit_code == 0
assert tuple(result.face_map) == (0, 1, 2, 1)
```

Use a different absent destination for each invocation. Publication never
overwrites an existing directory. The
[cyclic fixture](../tests/fixtures/ply-mixed-polygons/cyclic.ascii.ply) removes the
same directed quad with explicit corner correspondence. The
[differing-region fixture](../tests/fixtures/ply-mixed-polygons/label-different.ascii.ply)
instead has404 on the duplicate: the same target/declaration request returns
exit1 and publishes no bundle. Equal positions and loops do not authorize
dropping authored region differences.

The [retained tests](../tests/fixtures/ply-mixed-polygons/README.md) cover all
three encodings, API/module/console single and batch routes, explicit refusals
and independently decoded physical outputs. The
[workflow contract](ply-workflow.md) owns behavior; these focused examples do
not add UVs, materials, normals, skin data or generic conversion to PLY.
