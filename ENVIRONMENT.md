# Local development environment

This file owns the boundary between portable configuration and machine settings.

Copy [environment.example.json](environment.example.json) to `.local/environment.json` and fill only the fields needed for your task. Keep local paths, credentials, source assets, and raw logs in ignored storage. The template describes configuration inputs; no automatic loader or native build integration is implemented yet.

`workspace_root` is the optional local checkout/workspace location. `vcpkg_root` is an optional existing vcpkg installation. Compiler, generator, triplet, and corpus fields remain unset until used. Relative build paths resolve from this repository. Native code uses C++20, CMake 3.24+ and an installed `MeshvaleGeometry` development package. No vcpkg third-party dependency is required by this initial operation.

Follow [the charged native build instructions](docs/materialized_duplicates.md#dependency-build-and-test-selection) for the default-ON module, its Geometry producer and explicit test profile. The [legacy operation instructions](docs/duplicates.md#dependency-and-installation) select the module OFF for the earlier Geometry producer. Supply installed prefixes explicitly through `CMAKE_PREFIX_PATH`; this template does not change the build environment automatically. Native tests treat warnings as errors. CI evaluates Windows, Linux and macOS runners; configured checks do not establish a supported release matrix.

The opt-in [native glTF workflow](docs/gltf-workflow.md) additionally requires an
installed Interchange workflow-support target. The dedicated
[qualification workflow](.github/workflows/gltf-workflow.yml) pins public
dependency revisions and matches compiler/sanitizer flags across their builds.
[Its helper](scripts/gltf-workflow-ci.py) checks native inventories, actual report
fixtures, a production source-package build and relocated installed consumers.
Run its declared profile from a fresh checkout with the dependency paths and
tooling described in the helper. Configured jobs remain development checks;
their presence does not establish a supported release matrix.

The opt-in [retained-file workflow](docs/gltf-file-workflow.md) additionally
requires `MESHVALE_REPAIR_GLTF_FILE_WORKFLOW=ON` and the memory workflow enabled.
Use the Interchange producer pinned by the [file qualification workflow](.github/workflows/gltf-file-workflow.yml).
Configuration checks the compiled asset ledger query and borrowed verified-file
provider, in addition to the installed `meshvale::gltf_files` target. The
[same helper](scripts/gltf-workflow-ci.py) selects this cohort with
`--file-workflow`. Its configured native inventories contain ten tests in
qualified profiles and six in unsupported profiles, with four installed
consumers. Qualified profiles also independently validate the report from an
actual staged-byte readback failure after receipt preparation. The semantic
oracle validates fourteen reports, including changed/no-op GLB publication with
embedded BIN, opaque chunks and external resources. The file header
and target are installed only when enabled. Actual
execution, physical artifact hash checks and shared report-schema validation
are required before a qualification claim; configured jobs alone do not establish
acceptance.

Documentation and portability checks need Git and Python 3.10 or newer, with no third-party Python packages:

Optional Python builds use the pinned build/runtime dependencies in [pyproject.toml](pyproject.toml) and ordinary GIL-enabled CPython. The default wheel also enables the charged, memory and retained-file modules: supply compatible installed Geometry, Interchange and tinyobjloader prefixes. The native Interchange producer must enable its glTF writer, workflow support and file interfaces. See [Python packaging and ownership requirements](docs/python.md) and the [glTF Python contract](docs/gltf-python.md). Native-only legacy builds remain selectable separately. Keep candidate wheel directories, virtual environments and raw logs under ignored `.local/`; native-only builds and the checks below do not need these Python dependencies.

The optional [reported OFF workflow](docs/off-workflow.md) requires
`MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON`, charged Repair and an installed
Interchange `meshvale::off_files` target. Its
[dedicated qualification workflow](.github/workflows/off-workflow.yml) pins the
public Geometry and Interchange producers. The existing helper selects this
independent cohort with `--off-file-workflow`: optional glTF modules are disabled,
qualified native inventories contain seven tests, unsupported inventories six,
and both use three installed consumers. The report oracle checks 15 qualified
reports or four genuine empty reports on unsupported producers. Two selected
development profiles also build wheels and run the installed OFF API/commands
with `--off-python`; those checks are separate from portable redistribution.
The OFF Python module is opt-in at wheel build time and requires the declared
workflow extra for command report validation, without Interchange's Python
extension. Configured jobs alone do not establish qualification or a release.

```sh
python scripts/check-portability.py
python scripts/check-docs.py
```

For C++ changes, use the pinned `clang-format==23.1.3` in an isolated development
environment, then run `python scripts/check-cpp-format.py`. The checker selects
Git-tracked and non-ignored untracked `.h`, `.hpp` and `.cpp` project files and
uses `--dry-run --Werror` with the repository's Google-based configuration. Supply
`--formatter "$CLANG_FORMAT"` when the formatter is outside your command path.
CI installs this exact formatter version; the source distribution retains its
checker and configuration. This verifies formatting, not API naming or semantic
compliance. Existing public names remain unchanged pending a separate
compatibility migration.

## Windows sanitizer diagnostics

The Windows ASan workflow retains verbose producer build records and replays
the three affected Geometry tests through [the bounded process controller](scripts/diagnose-windows-startup.py)
before the complete CTest gate. Its 20-second diagnostic deadline records the
configured command, owned child process, output, executable/symbol/runtime
identities and timeout cleanup. A replay failure remains fatal; these extra
observations never replace the required test inventory or full test execution.
Before those tests, a Windows ASan setup step requires the configured compiler's
release sanitizer runtime and adds its directory to the following steps' PATH.
The retained receipts record the runtime path/hash and verify that directory
membership after the next step starts. They do not dump the full environment or
establish that this resolves a particular hosted failure.
The controller preserves the resulting PATH, supplies an explicit environment
allowlist, and suppresses interactive Windows failure dialogs only while
launching its diagnostic child. It restores the controller's previous policy.
It captures module/thread metadata, without claiming stack or wait-chain
diagnosis. These records do not establish the cause of a blank test timeout.

The opt-in [native PLY file workflow](docs/ply-workflow.md) requires
`MESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON`, charged Repair and an installed
Interchange `meshvale::ply_files` target from its documented compatible producer.
Supply compatible installed prefixes through `CMAKE_PREFIX_PATH` as above.
The target and public header are installed only when enabled. A native-only
configuration can keep the glTF and OFF workflows disabled:

```sh
cmake -S . -B build-ply -DCMAKE_BUILD_TYPE=Release -DMESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON -DBUILD_TESTING=ON -DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=qualified
cmake --build build-ply --config Release
ctest --test-dir build-ply -C Release --output-on-failure
```

Select `qualified` only with a matching qualified Geometry producer. Current
Windows MSVC development checks exercise the native typed PLY matrix, actual
reports, relocated installed consumers and a Git-free source archive. The opt-in
Python API, module/console single commands and ordered batches are implemented
and have bounded installed development checks below. Complete fault,
unsupported-profile and cross-platform qualification remains pending. Disabled
PLY builds keep that optional target/header absent.

## PLY Python build and installed checks

Use compatible installed Geometry and Interchange native SDKs from the
[PLY dependency contract](docs/ply-workflow.md#pfw-dependency-001-independently-selectable-native-module),
including Interchange's `meshvale::ply_files` target. Compiler, configuration,
CRT, architecture and instrumentation must match the extension. Matching native
version 0.0.0 alone is insufficient. Supply Geometry, Interchange and the static
double-precision tinyobjloader prefixes as a quoted semicolon-separated
`NATIVE_PREFIXES`, following [the existing Python build contract](docs/python.md#build-and-consumption).

The exact declared Geometry Python wheel is
`meshvale-geometry==0.0.1.dev134+g48298b24b`; its `reports` extra provides the
shared report reader/validation dependencies. Repair's `workflow` extra declares
that same dependency. Build these development wheels from their public source
and provide their declared dependencies locally; these commands presume no
package-index availability. PLY does not require the Interchange Python wheel
or Repair's `assets` extra.

The recipe below uses symbolic absolute paths and a fresh ordinary
GIL-enabled Python environment (`PYTHON`). `BUILD_WHEELS` and
`CANDIDATE_WHEELS` contain the pinned build tools and runtime dependencies;
`REPAIR_WHEEL_DIRECTORY` is a fresh output directory and `REPAIR_WHEEL` selects
its exact built wheel filename. CMake, the compiler and compatible native SDKs
must already be available. Adjust shell notation for your host.

```sh
"$PYTHON" -m pip install --no-index --find-links "$BUILD_WHEELS" \
  "scikit-build-core==1.1.1" "nanobind==3.1.0" "setuptools-scm==10.3.4"
"$PYTHON" -m pip install --no-index --find-links "$CANDIDATE_WHEELS" \
  "meshvale-geometry[reports]==0.0.1.dev134+g48298b24b"
"$PYTHON" -m pip wheel . --no-deps --no-build-isolation \
  --wheel-dir "$REPAIR_WHEEL_DIRECTORY" \
  --config-settings=cmake.define.CMAKE_PREFIX_PATH="$NATIVE_PREFIXES" \
  --config-settings=cmake.define.MESHVALE_REPAIR_PYTHON=ON \
  --config-settings=cmake.define.MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON \
  --config-settings=cmake.define.MESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON
"$PYTHON" -m pip install --no-index --find-links "$CANDIDATE_WHEELS" "$REPAIR_WHEEL[workflow]"
```

This explicitly selects the co-enabled development configuration exercised by
the current extension build: the existing default glTF modules plus optional
OFF and PLY. Its Interchange SDK must therefore also provide the default glTF
and selected OFF targets. OFF is not a PLY prerequisite, and its flag above
does not change the default. A Python build with all other file workflows
disabled has not been independently executed in this qualification. The native
PLY-only configuration above is a separate result.

Run the [walkthrough](docs/ply-walkthrough.md) from a new working directory.
For the wider installed API/module/console/batch matrix, first retain native
fixtures using the [fixture guide](tests/fixtures/ply-workflow/README.md).
`REPAIR_SOURCE` denotes the checkout/archive containing the test and public
expectations. `NATIVE_VERSION` is the actual `CMAKE_PROJECT_VERSION` in the
`CMakeCache.txt` for the build that compiled the installed extension, with
`CMAKE_PROJECT_NAME=MeshvaleRepair`; do not substitute its Python distribution
version. Choose an absent `PYTHON_TEST_OUTPUT` with an existing parent outside
the retained fixture tree. The test refuses resolved source/output overlap and
source-tree imports before creating its outputs.

```sh
"$PYTHON" -I "$REPAIR_SOURCE/tests/ply_workflow_python.py" \
  --fixture-root "$FIXTURE_DIRECTORY" --output-root "$PYTHON_TEST_OUTPUT" \
  --literal-oracles "$REPAIR_SOURCE/tests/fixtures/ply-workflow/literal-predicates.json" \
  --presets "$REPAIR_SOURCE/docs/ply-presets.json" --native-version "$NATIVE_VERSION"
```

The matching installed console is found beside that interpreter by default;
`--console` may explicitly select its entrypoint in the same scripts directory.
The test keeps streams, bundles and an `integration-results.json` with actual
reached events and unchanged-input checks. Its planned successful scope is 102
native attempts (100 workflow calls and two capture-only missing-file failures)
and 97 two-member bundles. The current ordinary Windows x64 MSVC Release /
CPython 3.13 development execution reached that scope. This does not establish a
clean runtime floor, package-index wheel, supported release, complete fault
coverage or additional platform/interpreter qualification.

Public files use repository-relative links, public URLs, tool names, and symbolic environment values. Run these checks before committing or publishing. The portability check scans working files and staged content for machine paths and private local files; it does not scan all secrets or determine whether a document should be public. Review public content and package contents separately.
