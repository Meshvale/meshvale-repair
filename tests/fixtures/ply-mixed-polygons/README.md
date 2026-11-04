# Original mixed-polygon PLY fixtures

These original fixtures share ten positions, a triangle, a quad and a concave
pentagon. They carry double XYZ positions and one unsigned face property,
`region_code`. Region values describe application regions, not topology IDs.
An explicit `face:region_code` declaration asserts that these particular values
are safe across face removal. No property name supplies that assertion by itself.

The twelve request cases in [literal-expectations.json](literal-expectations.json)
cover unchanged, exact/cyclic removal, reversed winding, distinct coincident
vertices, a retained unused vertex, missing/wrong/unused declarations, differing
region values, whole-request rejection and an invalid vertex index. The file
contains independent literal source words, output loops, all four maps and
expected region values. It is not derived from a Meshvale result.

Nine distinct assets are each authored in ASCII, little-endian binary and
big-endian binary form. Binary files are stored as `.ply.hex` text; decode their
hex bytes once into a new owned input file before processing. Container hashes
are distinct from the decoded wire hashes in the literal file. Fixture attributes
retain original bytes across Git checkouts. The invalid-index assets intentionally
contain vertex99 and must fail before editing; they are not valid mesh examples.

`expected.exit_code` names the shared-report/command outcome. Before a native
workflow starts, the Python API instead retains a typed capture result and null
native `exit_code`, `status` and `outcomes`. A canceled capture is projected to
command130; malformed capture to command2. The tests keep these domains distinct.

The [installed corpus test](../../ply-corpus/test_ply_corpus.py) uses a
[narrow physical oracle](../../ply-corpus/mixed_physical_oracle.py) for this exact
double/uint/uchar-list schema. It verifies actual output position bits, region
values, directed polygon indices and original vertex identity, including the
unused row. It does not replace the separate eight-scalar PLY fixture or implement
a general PLY reader. All inputs and actual outputs are retained for inspection.

Run the test with an installed qualified development build and an absent output
directory whose parent exists:

```sh
python -I tests/ply-corpus/test_ply_corpus.py --fixture-root tests/fixtures/ply-mixed-polygons --output-root "$OUTPUT" --native-version "$NATIVE_VERSION"
```

Supply the actual CMake native identity, independently of the wheel version.
The test installs nothing and rejects source-tree product imports. The
[walkthrough](../../../docs/ply-mixed-polygon-walkthrough.md) demonstrates a small
subset through the existing public API and commands. These files establish no
new PLY attribute semantics, STL repair/indexing, supported release or runtime floor.
