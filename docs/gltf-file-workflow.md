# Native retained-file glTF workflow

This development C++20 interface composes an immutable Interchange file capture,
the targeted duplicate-face operation and verified same-format JSON glTF or GLB
bundle publication.
The additive [completed-facts retention interface](gltf-retained-facts.md)
provides explicit opt-in ownership after a missing memory receipt. Public
qualification remains pending; ordinary receipt semantics remain those below.
It has no stable release yet. Enable `MESHVALE_REPAIR_GLTF_FILE_WORKFLOW=ON` and
link `meshvale::gltf_file_workflow`; the supplied-memory workflow option must also
be ON. Install Geometry and Interchange with the public `meshvale::gltf_files`
and `meshvale::gltf_workflow_support` targets, then supply their prefixes through
`CMAKE_PREFIX_PATH`. The existing Python/OBJ and default native cohorts remain
separate development interfaces.

The dependency baseline is Geometry
[`48298b24be7c132816fb5580d22d58bde61558d0`](https://github.com/Meshvale/meshvale-geometry/tree/48298b24be7c132816fb5580d22d58bde61558d0)
and Interchange
[`1b1cf0583cd227c0845441c04cb720d231e9ec08`](https://github.com/Meshvale/meshvale-interchange/tree/1b1cf0583cd227c0845441c04cb720d231e9ec08).
Follow their public
[Geometry build instructions](https://github.com/Meshvale/meshvale-geometry/blob/48298b24be7c132816fb5580d22d58bde61558d0/ENVIRONMENT.md)
and [Interchange file-module instructions](https://github.com/Meshvale/meshvale-interchange/blob/1b1cf0583cd227c0845441c04cb720d231e9ec08/docs/gltf-files.md)
to install matching compiler/configuration/runtime packages. Enable Interchange
files, topology writing and workflow support. Use fresh build directories and
match sanitizer flags across all producers and the consumer.

With `GEOMETRY_PREFIX`, `INTERCHANGE_PREFIX` and `REPAIR_PREFIX` set to your
installed/new output directories, the following CMake commands configure the
qualified numerical profile, run the native suite and install Repair:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$GEOMETRY_PREFIX;$INTERCHANGE_PREFIX" \
  -DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=qualified \
  -DMESHVALE_REPAIR_GLTF_WORKFLOW=ON \
  -DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$REPAIR_PREFIX"
cmake -S examples/consumer -B consumer -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$REPAIR_PREFIX;$GEOMETRY_PREFIX;$INTERCHANGE_PREFIX" \
  -DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=qualified \
  -DMESHVALE_REPAIR_TEST_GLTF_WORKFLOW=ON \
  -DMESHVALE_REPAIR_TEST_GLTF_FILE_WORKFLOW=ON
cmake --build consumer --config Release
ctest --test-dir consumer -C Release --output-on-failure
```

Use `unsupported` instead of `qualified` in both configurations when the actual
Geometry numerical producer refuses the required allocation profile. That route
checks a genuine nonempty materialization refusal; it cannot substitute for a
successful publication test. Test selection does not change producer capability.
For Git-free production builds, use `BUILD_TESTING=OFF`, then test the installed
consumer. See the [installed example](../examples/consumer/gltf_file_workflow.cpp)
and the [file-workflow development gate](../.github/workflows/gltf-file-workflow.yml)
for the public recipe. Configured gates do not establish supported releases.

`RunGltfDuplicateFileWorkflow` takes a strong `GltfFileAsset`, an opaque input
identity, one primitive ordinal and explicitly selected duplicate targets. It
adopts the exact canonical asset owner. It does not reread the source filesystem
or parse an equal replacement asset. Failed initial capture remains an
Interchange result. The [supplied-memory contract](gltf-workflow.md) owns the
static extraction subset, exact duplicate comparison, row-local attribute policy
and forward face/corner correspondence.

Configuration, positive nested limits, checked extents and borrowed descriptor
counts are validated first. The retained asset must share the caller's execution
ledger and expose its capped-reader counter. Those checks precede initial
cancellation and every fresh request. Copies of the same context share a ledger;
an unrelated context with equal numerical limits does not.

The operation independently copies canonical source JSON and caller-binding
resource bytes into charged baseline storage, then uses the existing repair
engine and source protection comparisons. It extracts, materializes, repairs,
writes, reloads and independently inspects the actual output. Acceptance remains
visible if a later hash, report or publication stage fails. Rejected targets never
publish a partial edit. No publication request returns the genuine memory
suboperation report, with publication `not-requested` and no output scope.

For requested publication, the destination is a new directory with an existing
parent. Supplemental files and the report path are borrowed through return.
Interchange owns portable path admission, collisions, staged capture/inspection,
full byte/inventory verification, cleanup and native no-replace finalization.
JSON glTF and GLB publish in the captured source format. Changed GLB output
replaces its JSON chunk while retaining complete ordered BIN and opaque chunks,
including admitted logical unused bytes and alignment padding. Unchanged output
retains the complete original entry bytes. The owning publisher verifies actual
staged bytes before finalization; no format conversion is implied.

The conditional success report is prepared after actual staged verification.
Repair admits all source framing and initial staged artifact hash bytes before
starting hash jobs. Independent artifact SHA-256 streams hash complete physical
members, including entry/resource tails and static supplements. The sole dynamic
report excludes its own hash. Source identity uses
`meshvale.gltf-file-snapshot/1`: its ASCII marker and NUL, length-framed format,
file count, then each UTF-8-sorted path and complete captured byte span. Lengths
and count are unsigned 64-bit little endian. SHA-256 is an integrity identifier,
not authentication or a FIPS validation claim.

The report is a real supplemental bundle member. The publisher imports and
verifies those exact prepared bytes before commit. Only an actual `kPublished`
result exposes the conditional success report and `PublishedReportPath`. No
postcommit allocation, serialization, hash patch, callback or mandatory reread
establishes success. Final publisher statistics stay on the typed publication
result, separate from immutable report bytes.

Requested exports use Geometry `meshvale.report/1` with preservation profile
`gltf-static-duplicate-publication/1`: the memory profile's eight required checks
plus `staged-selected-primitive` and `staged-physical-members`. Failed required
findings dominate; otherwise unperformed/unsupported checks make the profile
incomplete. A commit IO failure may retain a passed preservation profile with
failed execution. A successful report has the actual output snapshot, three
candidate-to-output identity maps and hashes of initial verified artifacts.
Failure reports expose no conditional output or published artifact claim.

Primary inline diagnostics, candidate and actual publication/cleanup facts survive
a secondary failure to prepare a failure report. Report bytes may be absent, even
when a retained record exists; no emergency allocation or separate delivery
diagnostic is promised. Unexpected allocation/native exceptions propagate after
all joins and the publisher's best-effort rollback. They do not claim a normal
typed cleanup outcome.

Limits describe distinct scopes. The fresh memory aggregate includes complete
concurrently live baseline, scratch, delegate and memory-report requests; its
fresh `read` is zero for retained input. The unchanged supplied-memory API still
charges its real read. The three materialization allowances are checked as one
sum and passed unchanged. A separate fresh wrapper cap includes hash metadata,
paths, views, report buffers and worker records. Publication keeps its own caps.
Retained file/reader charges stay on their original scopes and shared ledger.
Container and composite publication totals are unavailable; there is no combined
file-workflow total, peak or RSS guarantee. The report's wrapper byte sample is
explicitly taken before receipt buffers/jobs; the record getter is the live
counter.

All borrowed input/options/context/backend observer state remains unchanged and
alive through synchronous return and every worker join. The copy-only record
retains source, memory and any actual published owners after caller destruction.
Original allocator backend state must outlive the last retained block free.
Concurrent immutable reads through separate copies are supported. The separate
[Python path interface](gltf-python.md) owns capture convenience, application
presets and Python observations. Single/batch glTF commands remain later work.
The separately selected [direct-translation contract](gltf-translation.md)
defines a future explicit animation-preserving profile. Its implementation and
qualification are pending; the static/default behavior documented here is unchanged.
