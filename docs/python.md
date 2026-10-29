# Python targeted duplicate removal

| Field | Value |
|---|---|
| ID | REPAIR-PYTHON-001 |
| Version | 0.1.3 |
| Status | Development interface; not released |
| Owner | Python operation shape, ownership, errors and installation |

`meshvale_repair.remove_duplicate_faces(mesh, targets, *, row_local_attributes=())` accepts the canonical `meshvale_geometry.Mesh` snapshot, an iterable of `(keep, remove)` input face-index pairs, and optionally an iterable of `(domain, name)` custom row-local declarations. Domains are `vertex`, `face`, `corner`; declarations retain the meaning in the [native operation contract](duplicates.md). No automatic duplicate detection is added. Pair indices must be Python integers in uint64 range; booleans, floats, negative or oversized values are rejected without truncation. A representable out-of-range face index is instead a native `rejected` result.

The frozen `DuplicateResult` has `outcome` (`accepted`, `unchanged`, `rejected`), nullable `candidate`, a tuple of diagnostic dictionaries (`code`, `subject`, nullable `element`), and readonly uint64 memoryviews `face_map`/`corner_map`. Candidate meshes are canonical Geometry snapshots. Their ownership, row order and buffer protocol follow [Geometry's Python contract](https://github.com/Meshvale/meshvale-geometry/blob/ec38fabd783a578947d43856b0c7a7fd54ce8533/docs/python.md). Diagnostics are detached copies and may be edited without changing meshes/maps. An empty request uses the native unchanged-copy/identity-map behavior. A rejected result has no candidate and empty maps, preserving the entire input.

The wrapper copies Geometry buffers to an owned native input and returns an owned candidate through the explicit record seam. Each direction makes two numeric payload copies (Geometry export + native import, then native export + Geometry import), in addition to the operation's own candidate construction. This interface trades copying for lifetime isolation; it makes no zero-copy or performance claim. No Mesh C++ object or mutable borrowed vector crosses extension runtimes. The native operation releases the GIL only after copying input/request data, and regains it before constructing Python output. Deleting sources/results, releasing exported views, later operations and concurrent operations on immutable snapshots cannot invalidate surviving candidates/maps. Native semantics, preconditions and preservation limits remain owned by [the duplicate contract](duplicates.md); this binding adds no manifold or complete-repair guarantee.

Wrong Python shapes/types raise `TypeError`/`ValueError`/`OverflowError`, with no partially returned candidate; allocation errors raise `MemoryError`. Mesh defects, invalid representable targets, mismatched face/attribute records and unsupported semantics use the native rejection diagnostics. This primitive has no incremental mutation, file/scene operation, cancellation or CLI. The separate optional [reported workflow](workflow.md) owns application verification, cancellation checkpoints and reports.

## Build and consumption

The distribution is `meshvale-repair`; the import is `meshvale_repair`. Optional builds require ordinary GIL-enabled CPython 3.10+, C++20, CMake 3.24+, and the pinned nanobind/scikit-build-core/version provider in [pyproject.toml](../pyproject.toml). The native SDK cohort for the default/glTF-coenabled wheel is [specified by the retained-file interface](gltf-file-workflow.md): Geometry `48298b24be7c132816fb5580d22d58bde61558d0` and Interchange `88acdba9046352b84d3dea90648e3d271629f511`. Separately build the Python dependencies from public source: Geometry `0.0.1.dev134+g48298b24b` from that same Geometry revision and, for the assets extra, Interchange `0.0.1.dev179+g5b9b695ba` from `5b9b695ba1fef166baa8c7cf9ad21a5630691f70`. The native Interchange SDK and optional Interchange Python wheel therefore use separate selected source trees. These development candidates are not on a package index; build their wheels first. Native snapshots still use version `0.0.0`.

The default wheel enables the charged, glTF memory and retained-file native modules.
A wheel that also enables OFF/PLY must satisfy their additional native targets
using the same compatible SDK cohort; their Python paths do not require the
Interchange Python extension. Supply compatible installed Geometry, Interchange and tinyobjloader prefixes. Interchange must enable `MESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE`, `MESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT` and `MESHVALE_INTERCHANGE_GLTF_FILES`; its public preparation helper owns the static double-precision tinyobjloader dependency. Native-only Repair builds do not need Python bindings. The existing mesh API uses the installed `meshvale_geometry_add_python_record` factory with its `meshvale_repair` nanobind domain. This compiles Geometry's installed record implementation into a per-extension target linked to `meshvale::geometry`; no Python ABI archive is shared between products. The glTF file API instead calls the linked native producers within this extension, as specified in [its own contract](gltf-python.md).

Supply a CMake prefix list containing those installed native producers; no private or sibling source dependency is built automatically. Use semicolons between entries, including in the quoted shell variable below:

```sh
python -m pip wheel . --no-deps --wheel-dir .local/wheels --config-settings=cmake.define.CMAKE_PREFIX_PATH="$NATIVE_PREFIXES"
python -m pip install --no-index --find-links "$CANDIDATE_WHEELS" meshvale-repair
python -m unittest discover -s tests/python -v
python examples/python/remove_duplicate.py
```

Candidate wheels contain the Python package/native extension and required notices, with the native Repair/producer libraries and nanobind runtime linked statically; ABI/platform-specific wheels and dynamic platform C++ runtime dependencies remain explicit build constraints. Source archives select public product inputs, include generated source version metadata, and rebuild without Git. Exact `vX.Y.Z` tags own released versions; development versions derive from full Git history. Archive checks and installed tests run outside source directories with a matched three-product cohort, and all six import orders are evaluated. Hosted CPython 3.10/3.14 Windows/Linux/macOS checks are development evaluation rather than a full release matrix. Free-threaded/stable-ABI wheels and arbitrary newer producer versions remain unsupported.

```python
from meshvale_repair import remove_duplicate_faces

result = remove_duplicate_faces(mesh, [(0, 1)])
if result.outcome == "accepted":
    print(result.candidate.face_count, list(result.face_map))
else:
    print(result.outcome, result.diagnostics)
```

See the [runnable installed example](../examples/python/remove_duplicate.py), [original Python tests](../tests/python/test_duplicates.py), [dependency inventory](../THIRD_PARTY.md) and [changelog](../CHANGELOG.md). Real format/asset integration and CLI/report policy remain separate work.

## OFF to annotated GLB

The implemented opt-in `meshvale_repair.off_surface_export` module follows
[REPAIR-OFF-SURFACE-001 v0.3.0](off-surface-export.md). It repairs explicit OFF
duplicate targets, verifies the polygon candidate, and publishes `surface.glb`,
`surface.json`, `surface.bin` and the separate `publication.json` receipt into a
new destination directory, which must not already exist. Coordinates must already
be meters in a right-handed Y-up basis. No units, basis, normals or materials are
inferred.

Enable both `MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON` and
`MESHVALE_REPAIR_OFF_SURFACE_EXPORT=ON` when building the wheel; surface export
defaults OFF and its Python bindings are compiled only when enabled. In addition
to the existing OFF/default-wheel requirements, the installed native SDKs must
export Geometry's `meshvale::charged_triangulation` and Interchange's
`meshvale::surface_output` and `meshvale::surface_files` targets. Supply compatible
SDK prefixes; the older default SDK revisions above do not establish these
features. Native SDK selection remains separate from the unchanged Geometry
`0.0.1.dev134+g48298b24b` and optional Interchange `0.0.1.dev179+g5b9b695ba` Python
dependency pins. This module requires no Interchange Python extension. Commands
use the report dependencies supplied by the `workflow` or `assets` extra.

```sh
python -m pip wheel . --no-deps --wheel-dir .local/wheels \
  --config-settings=cmake.define.CMAKE_PREFIX_PATH="$NATIVE_PREFIXES" \
  --config-settings=cmake.define.MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON \
  --config-settings=cmake.define.MESHVALE_REPAIR_OFF_SURFACE_EXPORT=ON
```

Prepare once without capturing the source, then run its fresh native operation:

```python
from meshvale_repair.off_surface_export import prepare_off_glb_export, run_off_glb_export

prepared = prepare_off_glb_export(
    "input.off", "output-glb", [(0, 1)],
    text="canonical-ascii", edge_count="unknown-zero",
    coordinates="meters-right-handed-y-up", precision="exact32",
    max_position_error=0.0,
    acknowledge_triangle_conversion=True,
    acknowledge_corner_expansion=True,
    acknowledge_unused_vertices=True,
)
result = run_off_glb_export(prepared)
print(result.exit)
```

`repair_off_to_glb` accepts the same declarations and performs prepare then run.
`bounded-rne32` instead requires an explicit finite nonnegative position error
bound. `OffGlbExecution` selects `balanced-v1` or `serial-reference-v1` without
automatic cap growth; `OffCancellation` requests cooperative stop. Readonly
results retain actual capture/native observations, candidate/export views and
maps. `off_report_bytes` and `publication_receipt_bytes` expose exact available
bytes. Export failure does not rewrite the accepted polygon candidate or its
OFF report. The [contract](off-surface-export.md#osg-observations-001-exact-python-getters-and-map-scopes)
owns each getter's availability and map scope.

The installed console command and `python -m meshvale_repair.off_glb_commands`
share `single` and `batch` subcommands:

```sh
meshvale-repair-off-glb single --input input.off --destination output-glb \
  --targets '[[0,1]]' --text canonical-ascii --edge-count unknown-zero \
  --coordinates meters-right-handed-y-up --precision exact32 --max-position-error 0 \
  --ack-triangle-conversion --ack-corner-expansion --ack-unused-vertices
meshvale-repair-off-glb batch --request jobs.json --fail-fast
```

`jobs.json` uses the closed [off-glb-jobs/1 request](off-surface-export.md#osg-command-001-single-and-whole-batch-admission).
Every row is admitted before any source capture; relative paths use the logical
request-file parent unless `--base` is supplied. The reusable
`run_off_glb_jobs` in `meshvale_repair.off_glb_commands` leaves process signal
handlers and stdout alone and returns a readonly holder of all attempted results
and bounded prepared bytes. Detached stdout documents embed the original OFF
report and native publication receipt as exact UTF-8 strings; stdout failure
cannot undo a committed directory. Console SIGINT handling is delayed until a
Python interpreter boundary and does not promise prompt native interruption.

Recorded Windows x64 MSVC Release checks with ordinary CPython 3.13 cover the
native and installed SDK interfaces, the Python API, single/batch commands and
legacy behavior. Source-archive and relocated installed-SDK checks have also
passed, including fresh default-branch CI. GNU 13 checks cover native
unsupported-profile outcomes and relocated installed consumers. The
[development workflow](../.github/workflows/off-glb-workflow.yml) owns these
configured profiles; they do not establish a supported release, published wheel
or runtime floor.

## Portable development cohort

The [candidate workflow](../.github/workflows/portable-candidates.yml) evaluates ordinary GIL-enabled CPython 3.13 on Linux x86_64 with a manylinux_2_28 floor and Windows x64 on `windows-2022`. Artifacts are development candidates retained for 14 days; they do not establish a supported release matrix. A clean Windows 11 consumer floor, macOS and additional interpreter/architecture combinations remain unevaluated by this workflow.

Each configured lane rebuilds the exact Geometry and optional Interchange Python revisions above from full-history public source. The optional Interchange Python producer's preparation supplies the Geometry/parser dependencies and double-precision static tinyobjloader manifest. The helper then separately checks out the native Interchange SDK revision and builds its native glTF interfaces using those inputs. Repair then builds independently from source and its Git-free source archive with that native cohort. This configured route does not by itself establish successful final artifact qualification. Upstream CI downloads are not build dependencies. The workflow pins cibuildwheel `4.2.0`, product and isolated-backend CMake `4.3.1`, the manylinux image digest and action revisions; retained reports record compiler, dependency tools, final metadata, symbols, hashes and notices.

Windows candidates require the [official x64 Microsoft Visual C++ v14 runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) at least as recent as the recorded build toolset, including `msvcp140.dll` and `msvcp140_atomic_wait.dll`. These files remain external prerequisites. Package checks reject unexpected bundled binaries; no binary is manually retagged.

Clean installed checks exercise the existing Repair and producer test suites, all six product import orders with canonical Mesh identity, independent native record consumers and the [textured API/single/batch walkthrough](quickstart.md). The walkthrough retains independently reloaded bundles, source/resource bytes, correspondence, receipts and a rejected request that publishes nothing. The report reader is also checked with all native product imports blocked, and `pip check` verifies installed dependencies. The dedicated [glTF suite](../tests/gltf-python/test_gltf_file_workflow.py) uses explicit numerical profiles: qualified Windows MSVC Release checks actual JSON/GLB publication; the Linux portable lane checks actual unsupported native outcomes. Neither profile silently skips the other. The [glTF walkthrough](../examples/python/gltf_walkthrough.py) follows that same profile selection.

The [glTF commands](gltf-commands.md) also run through the installed `workflow`
extra before the optional Interchange Python package is installed. Their separate
suite checks admission, ordered jobs, exact receipts, fallback facts and delivery.
The walkthrough's `--commands` mode compares console/module commands and API/CLI
batches. In the Windows portable lane, a separate compiled observer child checks
real report-worker cancellation against the installed command code; it retains
its original native compile proof and never loads the production extension in
that test child. Production-wheel tests remain separate.

To inspect a matching cohort, download one lane's artifact from its passing workflow run. Select a **new** `CANDIDATE_WHEELS` directory containing one Repair wheel plus that lane's Geometry and Interchange wheels from `upstream/`; avoid mixing source/archive alternatives or runs. Create a fresh environment matching the candidate's interpreter and platform, satisfy the recorded platform runtime prerequisite, then install:

```sh
python -m pip install jsonschema==4.26.0
python -m pip install --no-index --find-links "$CANDIDATE_WHEELS" "meshvale-repair[assets]"
python -m pip check
python -I "$REPAIR_SOURCE/examples/python/obj_walkthrough.py" "$WALKTHROUGH_ROOT"
```

Use this candidate's Repair source checkout for `REPAIR_SOURCE` and a new output directory for `WALKTHROUGH_ROOT`, as described in the [quickstart](quickstart.md#run-and-inspect). Building from public source remains available through the installation instructions above.
