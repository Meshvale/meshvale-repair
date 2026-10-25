# meshvale-repair

Configurable inspection and repair of imperfect polygon meshes.

**Status:** Development C++20 library and optional Python package with targeted exact duplicate-face removal, reported verification, verified OBJ bundles and commands, and a glTF/GLB file API under qualification. No stable release yet.

The selected [source-version profile](docs/versioning.md) defines build identity
for tagged and Git-free sources. Its extended archive implementation is pending.

Start with the [textured OBJ walkthrough](docs/quickstart.md): run the same repair through the Python API, single command and batch, then inspect preserved polygon loops, UV seams, resource bytes and bundled reports.

## Available native operation

Remove explicitly selected duplicate triangles, quads or n-gons while retaining representative loops, all supported UV sets, and unchanged variable joint/weight rows. Results carry face/corner correspondence; a failing target rejects the whole request. Unknown attribute semantics block edits unless explicitly declared row-local. Non-manifold raw input can remain representable and is not implicitly split or welded.

Read the [operation contract and install instructions](docs/duplicates.md) for exact equality, attribute policy, rejection and preservation limits. The [installed consumer example](examples/consumer/main.cpp) demonstrates the public C++ interface.

The development [charged native interface](docs/materialized_duplicates.md) applies the same targeted operation directly to immutable Geometry owners, shares their execution budget, and returns an immutable candidate with complete correspondence. Its default-ON native target requires the documented Geometry producer. The [installed example](examples/consumer/materialized_duplicates.cpp) exercises that interface. The glTF file API composes this operation; the existing Python mesh and OBJ interfaces retain their original copy-producing operation.

The optional [supplied-memory glTF workflow](docs/gltf-workflow.md) composes public read, extraction, charged repair, checked writing, independent reload/preservation inspection and owned report/1 output. Its [installed native example](examples/consumer/gltf_workflow.cpp) retains verified output and correspondence after the caller buffer and execution context are destroyed. Initial Windows native development checks cover this seam; complete qualification remains pending. Filesystem publication and the Python path API compose the separate retained-file interface below. A Python supplied-memory API remains forthcoming.

The development [glTF single and batch commands](docs/gltf-commands.md) use the same file operation for explicit JSON glTF or GLB targets. They prepare all jobs before capture, run in order and preserve exact native receipt bytes. The [installed walkthrough](examples/python/gltf_walkthrough.py) adds `--commands` to compare console, module and batch behavior, including unchanged copies and resource hashes. Final platform qualification remains pending.

The [static shared-scene walkthrough](docs/gltf-scene-walkthrough.md) creates two
transformed instances of one mesh and an unrelated mesh. It repairs the shared
primitive through the API, commands and batches while checking transforms,
materials, textures and correspondence. An optional installed C++ inspector
independently reloads the published scene.

The optional [retained-file glTF workflow](docs/gltf-file-workflow.md) uses an
already captured file asset and the same repair engine. It can publish a new
JSON glTF or GLB bundle with a report that records SHA-256 hashes of the retained
source and staged artifacts. Publication verifies the report and all staged
bytes before committing the destination. The [installed native example](examples/consumer/gltf_file_workflow.cpp)
demonstrates capture, source-file removal, changed/no-op repair, publication and
reopening in both formats. GLB retains complete ordered BIN and opaque chunks;
unchanged output preserves the original wire bytes. This development interface
is under qualification.

The [Python glTF/GLB file API](docs/gltf-python.md) accepts an explicit primitive,
triangle/value policies and duplicate targets. It owns an execution session,
releases the GIL for native work, and retains actual diagnostics, maps and the
verified bundled receipt. The [installed walkthrough](examples/python/gltf_walkthrough.py)
exercises changed and unchanged JSON glTF and GLB files. Initial qualification
targets Windows x64 MSVC Release CPython 3.13; other profiles expose their actual
native unsupported outcomes.

The [Python interface](docs/python.md) accepts immutable Geometry snapshots and returns an owned candidate, rejection diagnostics and readonly correspondence maps. It uses the same native operation and preserves whole-request rejection. See the [installed Python example](examples/python/remove_duplicate.py).

The optional [OFF workflow](docs/off-quickstart.md) applies the same targeted
repair to indexed polygons, verifies canonical write/reload and publishes a new
OFF entry with its hashed receipt. Its native interface, opt-in Python path API,
single command and ordered batch share actual outcomes and correspondence maps.
The walkthrough checks cyclic quads, unrelated faces and unused signed-zero
vertices. Qualification remains in progress.

The optional [reported workflow](docs/workflow.md) independently verifies actual candidate correspondence and every channel, retains scoped topology diagnostics, and returns a versioned report. Its preservation profile accepts intentional open surfaces and retains unrelated defects as visible diagnostics. See the [real reported-operation example](examples/python/reported_duplicate.py); in-memory acceptance is separate from file publication.

The optional [OBJ asset workflow](docs/obj-workflow.md) imports OBJ/MTL and referenced resources through Interchange, applies that same repair/profile and publishes a verified new bundle with its report. Polygon loops, materials and opaque resources follow the adapter's explicit subset; unsupported export channels fail explicitly. A rejected or cancelled publication leaves no repaired bundle. See the [installed file example](examples/python/repair_obj_bundle.py). OBJ support does not establish glTF/GLB or broader format coverage.

The [OBJ commands](docs/cli.md) use that same workflow for one asset or an ordered batch. They validate the entire invocation before processing, isolate source/resource and destination domains, retain every actual job outcome, and support explicit fail-fast and cooperative cancellation. Reports go to stdout; published bundles retain their own verified receipts. The [installed command example](examples/python/obj_commands.py) runs both entry points.

The opt-in [OBJ triangle export](docs/obj-triangle-export.md) composes existing
native triangulation with that file workflow. Reports retain the accepted Repair
polygon state and map it to the actual verified triangular output. The
[runnable example](examples/python/obj_triangle_export.py) checks three materials,
UV seams, source/resource bytes and API/console/batch agreement, with explicit
refusal/cancellation outcomes. Package and hosted qualification remain in progress.

## Planned capabilities

- Conservative repair operations with stated preconditions.
- Polygon face capabilities and preservation reporting per operation.
- Verification of edits and explicit failed or skipped outcomes.
- Additional operations and formats through reusable interfaces and command workflows.

Supported formats, operation guarantees, and platform compatibility will be documented and tested with each implementation and release.

## Development

Read [ENVIRONMENT.md](ENVIRONMENT.md) for configuration and checks. CMake installs `meshvale::repair` with an installed `MeshvaleGeometry` dependency; source-tree sibling paths and private access are not required. [AGENTS.md](AGENTS.md) provides focused instructions. Consumer-facing changes are recorded in [CHANGELOG.md](CHANGELOG.md).

## Contributing

Use this repository's issues for reproducible problems and feature requests. Follow the public [contribution guide](https://github.com/Meshvale/.github/blob/main/CONTRIBUTING.md) and include how your change was validated. Share only assets you have permission to redistribute.

## License

Original material is licensed under [Apache-2.0](LICENSE). See [NOTICE](NOTICE) for attribution; third-party material retains its own terms.
