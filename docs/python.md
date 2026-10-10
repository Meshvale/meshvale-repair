# Python targeted duplicate removal

| Field | Value |
|---|---|
| ID | REPAIR-PYTHON-001 |
| Version | 0.1.0 |
| Status | Development interface; not released |
| Owner | Python operation shape, ownership, errors and installation |

`meshvale_repair.remove_duplicate_faces(mesh, targets, *, row_local_attributes=())` accepts the canonical `meshvale_geometry.Mesh` snapshot, an iterable of `(keep, remove)` input face-index pairs, and optionally an iterable of `(domain, name)` custom row-local declarations. Domains are `vertex`, `face`, `corner`; declarations retain the meaning in the [native operation contract](duplicates.md). No automatic duplicate detection is added. Pair indices must be Python integers in uint64 range; booleans, floats, negative or oversized values are rejected without truncation. A representable out-of-range face index is instead a native `rejected` result.

The frozen `DuplicateResult` has `outcome` (`accepted`, `unchanged`, `rejected`), nullable `candidate`, a tuple of diagnostic dictionaries (`code`, `subject`, nullable `element`), and readonly uint64 memoryviews `face_map`/`corner_map`. Candidate meshes are canonical Geometry snapshots. Their ownership, row order and buffer protocol follow [Geometry's Python contract](https://github.com/Meshvale/meshvale-geometry/blob/a27cae686ea4f4c6dabeebfc984f0265602e3b1d/docs/python.md). Diagnostics are detached copies and may be edited without changing meshes/maps. An empty request uses the native unchanged-copy/identity-map behavior. A rejected result has no candidate and empty maps, preserving the entire input.

The wrapper copies Geometry buffers to an owned native input and returns an owned candidate through the explicit record seam. Each direction makes two numeric payload copies (Geometry export + native import, then native export + Geometry import), in addition to the operation's own candidate construction. This interface trades copying for lifetime isolation; it makes no zero-copy or performance claim. No Mesh C++ object or mutable borrowed vector crosses extension runtimes. The native operation releases the GIL only after copying input/request data, and regains it before constructing Python output. Deleting sources/results, releasing exported views, later operations and concurrent operations on immutable snapshots cannot invalidate surviving candidates/maps. Native semantics, preconditions and preservation limits remain owned by [the duplicate contract](duplicates.md); this binding adds no manifold or complete-repair guarantee.

Wrong Python shapes/types raise `TypeError`/`ValueError`/`OverflowError`, with no partially returned candidate; allocation errors raise `MemoryError`. Mesh defects, invalid representable targets, mismatched face/attribute records and unsupported semantics use the native rejection diagnostics. This primitive has no incremental mutation, file/scene operation, cancellation or CLI. The separate optional [reported workflow](workflow.md) owns application verification, cancellation checkpoints and reports.

## Build and consumption

The distribution is `meshvale-repair`; the import is `meshvale_repair`. Optional builds require ordinary GIL-enabled CPython 3.10+, C++20, CMake 3.24+, the pinned nanobind/scikit-build-core/version provider in [pyproject.toml](../pyproject.toml), and installed Geometry native headers from public revision `a27cae686ea4f4c6dabeebfc984f0265602e3b1d`. Python runtime dependency is exactly `meshvale-geometry==0.0.1.dev35+ga27cae686`, the tested development candidate from that same revision. Neither candidate is on a package index; build/install its wheel first. Geometry native snapshots still use version `0.0.0`. Native-only Repair builds do not need Python bindings.

Supply your chosen installed Geometry prefix; no private or sibling source dependency is built automatically:

```sh
python -m pip wheel . --no-deps --wheel-dir .local/wheels --config-settings=cmake.define.CMAKE_PREFIX_PATH="$GEOMETRY_PREFIX"
python -m pip install --no-index --find-links "$CANDIDATE_WHEELS" meshvale-repair
python -m unittest discover -s tests/python -v
python examples/python/remove_duplicate.py
```

Candidate wheels contain the Python package/native extension and required notices, with the repair library and nanobind runtime linked statically; ABI/platform-specific wheels and dynamic platform C++ runtime dependencies remain explicit build constraints. Source archives select public product inputs, include generated source version metadata, and rebuild without Git. Exact `vX.Y.Z` tags own released versions; development versions derive from full Git history. Archive checks and installed tests run outside source directories with two separate wheels, and both native-extension import orders are evaluated. Hosted CPython 3.10/3.14 Windows/Linux/macOS checks are evaluation evidence rather than a full release matrix. Free-threaded/stable-ABI wheels and arbitrary newer Geometry versions are not supported by this experiment.

```python
from meshvale_repair import remove_duplicate_faces

result = remove_duplicate_faces(mesh, [(0, 1)])
if result.outcome == "accepted":
    print(result.candidate.face_count, list(result.face_map))
else:
    print(result.outcome, result.diagnostics)
```

See the [runnable installed example](../examples/python/remove_duplicate.py), [original Python tests](../tests/python/test_duplicates.py), [dependency inventory](../THIRD_PARTY.md) and [changelog](../CHANGELOG.md). Real format/asset integration and CLI/report policy remain separate work.

## Portable OBJ development cohort

The [candidate workflow](../.github/workflows/portable-candidates.yml) evaluates ordinary GIL-enabled CPython 3.13 on Linux x86_64 with a manylinux_2_28 floor and Windows x64 on `windows-2022`. Artifacts are development candidates retained for 14 days; they do not establish a supported release matrix. A clean Windows 11 consumer floor, macOS and additional interpreter/architecture combinations remain unevaluated by this workflow.

Each lane rebuilds public Geometry `a27cae686ea4f4c6dabeebfc984f0265602e3b1d` / Python `0.0.1.dev35+ga27cae686` / native `0.0.0` and Interchange `84e7912cba74e6c78410f42fee75856332940c94` / Python `0.0.1.dev37+g84e7912cb` from full-history public source. It invokes that pinned Interchange source's preparation to retain its double-precision static tinyobjloader manifest, then builds Repair independently from source and its Git-free source archive. Upstream CI downloads are not build dependencies. The workflow pins cibuildwheel `4.2.0`, product and isolated-backend CMake `4.3.1`, the manylinux image digest and action revisions; retained reports record compiler, dependency tools, final metadata, symbols, hashes and notices.

Windows candidates require the [official x64 Microsoft Visual C++ v14 runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) at least as recent as the recorded build toolset, including `msvcp140.dll` and `msvcp140_atomic_wait.dll`. These files remain external prerequisites. Package checks reject unexpected bundled binaries; no binary is manually retagged.

Clean installed checks exercise the existing Repair and producer test suites, all six product import orders with canonical Mesh identity, independent native record consumers and the [textured API/single/batch walkthrough](quickstart.md). The walkthrough retains independently reloaded bundles, source/resource bytes, correspondence, receipts and a rejected request that publishes nothing. The report reader is also checked with all native product imports blocked, and `pip check` verifies installed dependencies.

To inspect a matching cohort, download one lane's artifact from its passing workflow run. Select a **new** `CANDIDATE_WHEELS` directory containing one Repair wheel plus that lane's Geometry and Interchange wheels from `upstream/`; avoid mixing source/archive alternatives or runs. Create a fresh environment matching the candidate's interpreter and platform, satisfy the recorded platform runtime prerequisite, then install:

```sh
python -m pip install jsonschema==4.26.0
python -m pip install --no-index --find-links "$CANDIDATE_WHEELS" "meshvale-repair[assets]"
python -m pip check
python -I "$REPAIR_SOURCE/examples/python/obj_walkthrough.py" "$WALKTHROUGH_ROOT"
```

Use this candidate's Repair source checkout for `REPAIR_SOURCE` and a new output directory for `WALKTHROUGH_ROOT`, as described in the [quickstart](quickstart.md#run-and-inspect). Building from public source remains available through the installation instructions above.
