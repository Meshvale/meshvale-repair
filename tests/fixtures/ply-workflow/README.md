# Native PLY workflow report fixtures

The [report checker](../../../scripts/check-ply-workflow-reports.py) checks the
native fixture's actual retained owners, reports and published files against
these literal expectations. Python3.10+ and its standard library suffice.
Optional JSON Schema validation additionally needs `jsonschema` and an explicit
Geometry report schema path; that structural validation is supplementary.
The product [workflow](../../../docs/ply-workflow.md) and
[report wire contract](../../../docs/ply-reports.md) own behavior and report fields.
This test tool is not a general PLY reader, native operation or release gate.

## Reproduce the qualified fixture checks

Build the existing `meshvale_ply_workflow_tests` target with the PLY workflow
and qualified test profile enabled as described by the workflow contract.
Run that test executable with its existing `--write-reports` option and a new
absolute directory. It creates and retains the input snapshots, native
observations, returned report copies and physical bundles. Select the full
executable path for the chosen configuration: a Visual Studio Release build
usually places `meshvale_ply_workflow_tests.exe` under `build/Release`, whereas
a single-configuration build usually places it directly under the build root.
Running an ordinary CTest without the option instead
creates its own retained directory; use the actual directory from that run.

The following variables denote caller-selected absolute paths. The fixture
directory must not exist before native execution; the case and check receipt
paths must be new files in existing directories. `NATIVE_CACHE` is the
`CMakeCache.txt` of the build that actually produced the fixture executable.
It supplies native project name/version, not a Python package version.
`TEST_EXECUTABLE` selects that actual executable, independently of the cache path.
Both output files must be outside every supplied published bundle; admission
resolves comparison copies to also reject directory aliases into those bundles.

```sh
"$TEST_EXECUTABLE" --write-reports "$FIXTURE_DIRECTORY"
python -I scripts/check-ply-workflow-reports.py make-cases \
  --fixture-root "$FIXTURE_DIRECTORY" --native-cache "$NATIVE_CACHE" \
  --output "$CASE_FILE"
python -I scripts/check-ply-workflow-reports.py check \
  --cases "$CASE_FILE" --receipt "$CHECK_RECEIPT" --require-chains18
```

`make-cases` writes fixed request/outcome/reachability expectations for this
fixture. It never reads reports or sidecars to infer expected outcomes and
executes no native operation. It requires the expected retained files to exist.
Its stdout records the supplied native cache hash and project identity. Supply
the matching actual build cache; a different wheel version is not a substitute.
`check` writes a product-owned `meshvale.repair.ply-native-checks/1` receipt,
returns0 on pass and2 on a recorded check failure. That receipt retains exact
input hashes, completed case results and any failure. Output is new-only within
a caller-owned directory; this is not a transactional publication primitive.

The fixed33 cases comprise18 nonempty publication chains (three source encodings
by changed/empty-target-unchanged by three output encodings), nine true-empty
publication chains and six controls below. The separate native lifetime and late-reload
refusal calls have no observation sidecars and are deliberately not fabricated
into this manifest.

| Control | Fixed expected reachability/outcome |
|---|---|
| memory-unchanged | Real engine/write/reload/hash; unchanged, passed, no publication |
| unused-declaration | Real changed repair with one structurally valid unused declaration; no publication |
| missing-declaration | Engine rejection; no completed candidate/write/reload/output/hash |
| duplicate-declaration | Invalid request before work; no report or record |
| invalid-declaration | Invalid request before work; no report or record |
| later-write-limit | Actual writer refusal after completed changed Repair facts; no verified output/hash |

The source fixture has eight vertex rows, three faces and eleven corners.
Changing removes the cyclic duplicate face, retains the unrelated triangle and
all eight vertex rows, and produces two faces/seven corners. Literal properties
include all eight scalar types, signed zero, integer extrema and ragged rows.
Empty retains the same two-element/fourteen-property schema and ten zero-row
attributes, with singleton zero offsets for lists. It is not attribute-free.
The checker independently compares all four actual engine maps and separate
vertex identity, original/current schemas, narrow wire types, bit words and
all retained typed values. This fixed corpus does not cover arbitrary reversed,
different-ID, point-only or stress inputs without additional literal authorities.

## Case interface

A case file uses schema `meshvale.repair.ply-native-cases/1`, with exactly these
root keys: `schema`, `literal_oracles`, `literal_oracles_sha256`,
`empty_source_oracles`, `empty_source_oracles_sha256`, `cases`.
The two oracle paths and all case paths must be explicit absolute paths.
The oracle SHA fields are the pinned CRLF-to-LF container hashes described below.
No directory discovery or relative path resolution is performed by `check`.

Every case has exactly `id`, `kind`, `source_encoding`, `encoding`, `source_file`,
`native_file`, `report_file`, `memory_output_file`, `bundle_dir`, `entry_name`,
`receipt_name`, `input_id`, `targets`, `declarations`, `expected`.
`kind` is changed/unchanged/empty/rejected/early-failure. Encodings are exactly
`ascii`, `binary_little_endian`, `binary_big_endian`. `source_file` names retained
captured bytes even after original-path removal. Unavailable report/output/bundle
paths are null; receipt basename may be null without publication. Targets are
ordered `{keep,remove}` unsigned pairs. Declarations are ordered `{domain,name}`
objects; raw invalid declaration controls are admitted only for their expected
native InvalidRequest with no report. They are not valid operation requests.

`expected` has exactly `native_status`, `native_stage`, `report_native_stage`,
`execution`, `core_stage`, `candidate`, `profile`, `publication`, `exit`,
`input_snapshot`, `repair_completed`, `repair_called`, `writing_called`,
`reload_called`, `write_facts_available`, `memory_output_verified`,
`input_hash_available`, `report_is_first`, `tool`. Native status is its public
ordinal; stages/outcomes are their documented wire strings; exit is0/1/2/130.
The nine availability/call fields from input_snapshot through report_is_first
are booleans. `tool` is the explicit actual native `{name,version}`.

Final typed `native_stage` and sampled `report_native_stage` are distinct.
Memory success has final complete but sampled report; missing report has null
sampled stage. A committed conditional publication receipt has complete.
No normalization/patching of actual report bytes is performed.

## Actual native observation interface

`native.json` comes from real public result/record owners, not expected arrays
or values recovered from ReportJson. It has exactly the following members:

- `status`, `stage`, `execution_outcome`, `candidate_outcome`, `profile_outcome`,
  `publication_outcome`, `exit_code`, `repair_completed`,
  `memory_output_verified`, `input_sha256`: actual result/outcome/availability.
  Candidate remains null before any real outcome. A completed owner is different
  from an engine rejection.
- `face_map`, `corner_map`, `source_faces`, `source_corners`: complete actual
  engine arrays, null without completed Repair facts. Identity arrays are retained,
  including empty arrays for actual empty input.
- `source_mesh`, `candidate_mesh`, `reload_mesh`, `published_mesh`: each actual
  available mesh has `counts` (vertices/faces/corners), `positions_f64_hex`,
  `face_offsets`, `corner_vertices`, `attributes`. Unavailable owners are null.
  Each attribute has name/domain/components/semantic/set_index/present/metadata,
  canonical_type/data_kind/values/offsets, in the literal file's exact shape.
  Float values are actual fixed-width hexadecimal bit words, not decimal casts.
- `source_schema`, `reload_schema`, `published_schema`: ordered element arrays
  with kind/original_count/properties. Properties have name/kind/value_type/
  count_type/binding/attribute, using the report contract's wire spellings.
  Schema observations are read from the actual corresponding documents.
- `source_encoding`, `reload_encoding`, `published_encoding`: actual document
  encodings, null when the corresponding reload/published owner is absent.
- `execution`: final actual workflow phase counters, plus `publication` projected
  from the separate returned publisher result, or null. This does not invent a
  publication member on the workflow execution struct.
- `diagnostic`, `publisher`: actual primary workflow diagnostic and returned
  publisher outcome/diagnostic/cleanup/error projection, or null publisher when
  not returned. Separate writer/reloader diagnostic getters are not invented.

The checker also reads bounded physical format headers and verifies memory entry,
committed entry, returned receipt copy and physical receipt bytes/inventory.
Uncalled/unreached work must be absent/zero even when no report exists. Serial
codec observations are zero. Retained prior success and delegated failure facts
must agree. Final published publisher/execution facts are nonnull; conditional
pre-return receipt publisher/owned-byte/execution fields remain null. First
serialization checks the real pre-copy statistics without demanding that final
report-copy work was absent. Byte scopes are not summed into a peak/RSS claim.

## Fixture metadata container portability

Only these two expectation JSON files use CRLF-to-LF container hashing. The
operation replaces literal CRLF byte pairs with LF; it performs no JSON
reformatting, semantic normalization, Unicode normalization or other whitespace
conversion. Their parsed expectation values stay identical. The check receipt
records both raw `sha256`/`bytes` and `canonical_sha256`/`canonical_bytes` for them.
A real expected value change fails the pinned canonical identity.

Physical PLY/source/output/report/receipt/sidecar bytes are not normalized by
this rule. Their input hashes remain exact, and actual receipt/readback equality
remains byte-exact. The rule supplies only metadata-container portability when
Git checks out the fixture JSON using platform line endings.

Readers cap cases128, JSON16MiB, physical stream64MiB and JSON depth64. JSON
rejects duplicate keys, bool-as-integer fields and nonfinite/overflow numbers.
Directory inventory stops at the third member. These are checker bounds, not
new product defaults. Optional `--shared-schema`/`--require-shared-validation`
checks only structural Geometry JSON Schema; it is not full shared report
semantic validation. No generic PLY parser, external physical source-state,
worker/budget stress, supported platform, runtime floor or release claim follows.

## Installed Python API and command checks

Build/install the opt-in Python extension and its declared `workflow` extra
using [ENVIRONMENT.md](../../../ENVIRONMENT.md#ply-python-build-and-installed-checks).
The [walkthrough](../../../docs/ply-walkthrough.md) introduces the same API and
single/batch routes. This integration uses the public literal expectations
above and a retained native `--write-reports` tree; it does not modify native
fixtures or infer expected maps/schema/counts from their observed reports.

Use an isolated installed interpreter, outside any source-tree import fallback.
All five arguments below are explicit inputs. `NATIVE_VERSION` is the actual
native CMake project version of the installed extension, not its wheel version.
`PYTHON_TEST_OUTPUT` must be absent, have an existing parent and be outside the
fixture tree after comparison-path resolution. No recursive cleanup is done.

```sh
"$PYTHON" -I "$REPAIR_SOURCE/tests/ply_workflow_python.py" \
  --fixture-root "$FIXTURE_DIRECTORY" --output-root "$PYTHON_TEST_OUTPUT" \
  --literal-oracles "$REPAIR_SOURCE/tests/fixtures/ply-workflow/literal-predicates.json" \
  --presets "$REPAIR_SOURCE/docs/ply-presets.json" --native-version "$NATIVE_VERSION"
```

The test checks 215 leaves for each preset and the 24 scheduling-only differences;
18 balanced API cells and two serial parity cells; the same 18 cells through
each module/console single route and through each ordered batch route. Batch
requests split into two nine-row invocations per route to stay within the
16-job cap. Each batch must contain the literal physical native receipt bytes,
then pass shared loading, identity and semantic comparisons.

Additional bounded controls exercise real missing/unused declarations,
empty-target behavior, a schema view surviving its result, ledger release,
whole-invocation bad-later-row admission, empty-batch cancellation, actual
missing-file attempts, continue/fail-fast scheduling and four post-publication
receipt-byte delivery faults. The fault injection changes only the receipt
getter after a real committed result; it does not fabricate native outcomes.
Missing/invalid delivery gives 2/130 without undoing a bundle. A genuinely
unpublished missing-source result instead exercises the closed command projection with 17 observations and
integer version 1. There is no Interchange Python dependency.

The test independently checks literal source digests, four maps, vertex identity,
counts and original/current wire types/order/schema. Physical entry comparisons
against the same retained native matrix cell are explicitly byte parity, not an
independent scalar-value decoder. Existing native checker bit/value checks
remain separate. It rehashes fixtures and expectations after the run, retaining
all own outputs and an event journal. Only a completed journal establishes
reached scope: the current Windows x64 MSVC Release / ordinary CPython 3.13
development run records 102 attempts, 97 bundles and 109 events. This is not a
full unsupported-profile, concurrency/fault/stress or platform qualification,
an independently executed PLY-only Python build, or a released-wheel claim.
