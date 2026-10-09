# meshvale-repair

Configurable inspection and repair of imperfect polygon meshes.

**Status:** Development C++20 library and optional Python package with targeted exact duplicate-face removal, reported in-memory verification and verified OBJ bundle publication. No stable release or CLI/batch yet.

## Available native operation

Remove explicitly selected duplicate triangles, quads or n-gons while retaining representative loops, all supported UV sets, and unchanged variable joint/weight rows. Results carry face/corner correspondence; a failing target rejects the whole request. Unknown attribute semantics block edits unless explicitly declared row-local. Non-manifold raw input can remain representable and is not implicitly split or welded.

Read the [operation contract and install instructions](docs/duplicates.md) for exact equality, attribute policy, rejection and preservation limits. The [installed consumer example](examples/consumer/main.cpp) demonstrates the public C++ interface.

The [Python interface](docs/python.md) accepts immutable Geometry snapshots and returns an owned candidate, rejection diagnostics and readonly correspondence maps. It uses the same native operation and preserves whole-request rejection. See the [installed Python example](examples/python/remove_duplicate.py).

The optional [reported workflow](docs/workflow.md) independently verifies actual candidate correspondence and every channel, retains scoped topology diagnostics, and returns a versioned report. Its preservation profile accepts intentional open surfaces and retains unrelated defects as visible diagnostics. See the [real reported-operation example](examples/python/reported_duplicate.py); in-memory acceptance is separate from file publication.

The optional [OBJ asset workflow](docs/obj-workflow.md) imports OBJ/MTL and referenced resources through Interchange, applies that same repair/profile and publishes a verified new bundle with its report. Polygon loops, materials and opaque resources follow the adapter's explicit subset; unsupported export channels fail explicitly. A rejected or cancelled publication leaves no repaired bundle. See the [installed file example](examples/python/repair_obj_bundle.py). OBJ support does not establish glTF/GLB or broader format coverage.

## Planned capabilities

- Conservative repair operations with stated preconditions.
- Polygon face capabilities and preservation reporting per operation.
- Verification of edits and explicit failed or skipped outcomes.
- Reusable interfaces and single-asset or batch command-line workflows.

Supported formats, operation guarantees, and platform compatibility will be documented and tested with each implementation and release.

## Development

Read [ENVIRONMENT.md](ENVIRONMENT.md) for configuration and checks. CMake installs `meshvale::repair` with an installed `MeshvaleGeometry` dependency; source-tree sibling paths and private access are not required. [AGENTS.md](AGENTS.md) provides focused instructions. Consumer-facing changes are recorded in [CHANGELOG.md](CHANGELOG.md).

## Contributing

Use this repository's issues for reproducible problems and feature requests. Follow the public [contribution guide](https://github.com/Meshvale/.github/blob/main/CONTRIBUTING.md) and include how your change was validated. Share only assets you have permission to redistribute.

## License

Original material is licensed under [Apache-2.0](LICENSE). See [NOTICE](NOTICE) for attribution; third-party material retains its own terms.
