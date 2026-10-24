# Changelog

## Unreleased

### Fixed

- OFF commands preserve logical source, destination and batch-base path spellings, including partial Windows anchors and trailing separators, so native path validation determines each attempted outcome.

- Reported workflows independently fail input/candidate storage coverage for nonfinite authored floating channel rows. Missing backing values remain permitted and unchanged.

### Added

- A [static shared-scene walkthrough](docs/gltf-scene-walkthrough.md) compares API, commands and batches on two transformed instances of one mesh plus unrelated geometry. Original red/blue PNG textures, materials, resource bytes and authored transforms remain intact. A separate installed C++ inspector checks published scene views; source and portable-package checks exercise the example. This demonstrates the existing shared-primitive operation and retains its development limits.

- Optional reported [OFF repair workflow](docs/off-quickstart.md): native file composition, `repair_off_file`, `off` and `off-batch` commands with explicit targets and text/edge-count policies. Canonical write/reload verification retains indexed polygons and unused vertex rows, records correspondence and raw-source/artifact hashes, and publishes a new two-member bundle without replacing an existing destination. Independent preset limits and ordered exact-receipt scheduling are exposed through the same prepared operation. Development qualification remains in progress.

- Development [`gltf` and `gltf-batch` commands](docs/gltf-commands.md) and reusable ordered scheduling over the same file operation. Whole-invocation admission precedes capture; native receipts remain byte-exact, absent receipts use actual retained facts with explicit incomplete coverage, and oversized truthful reports fail delivery without emitting a partial aggregate. The installed walkthrough covers JSON glTF and GLB console/module/API batches; platform qualification remains in progress.

- Python [`repair_gltf_file`](docs/gltf-python.md) for explicit targeted repair of one captured JSON glTF or GLB primitive, with owned execution/cancellation, lazy typed native observations, correspondence and exact bundled receipt bytes. The final Python result is prepared before capture; installed examples cover changed/no-op publication in both formats. Initial Windows x64 MSVC Release CPython 3.13 qualification is in progress; stable releases remain separate work.

- Optional native [`meshvale::gltf_file_workflow`](docs/gltf-file-workflow.md) for retained glTF file duplicate repair and verified same-format JSON glTF/GLB bundle publication. Its bundled report records retained-source and staged-artifact SHA-256 hashes, preserves actual diagnostics and shares the existing memory repair engine. GLB publication retains complete ordered BIN/opaque chunks and preserves the complete original wire for unchanged output. An installed example exercises both formats after source-file removal. Qualification remains profile-scoped.

- Optional native [`meshvale::gltf_workflow`](docs/gltf-workflow.md) for supplied-memory glTF duplicate repair, checked writing, independent reload/preservation inspection and owned report/1 output. It retains accepted candidate facts across later failures and uses explicit stage/aggregate request caps. The retained-file and Python interfaces compose this engine; commands and stable releases remain separate work.

- Development native `meshvale::materialized_duplicates` interface for direct immutable Geometry repair, shared execution accounting, joined workers, independently verified candidates and complete face/corner maps. Complete original-request/profile qualification remains pending; Python and asset commands do not expose this interface yet.

- Bounded CPython 3.13 Linux x86_64 / Windows x64 OBJ development cohort evaluation with repaired source/archive wheels, rebuilt public dependencies, installed preservation walkthrough and exact artifact/notice reports. CI artifacts do not establish a supported release matrix.

- Runnable textured OBJ walkthrough comparing the reusable API, single command and batch with independent reload, resource/correspondence/receipt checks and an explicit rejected request. Original mixed-polygon inputs and generated bundles remain available for inspection.

- Native targeted duplicate-face removal for indexed polygon meshes, with cyclic face/corner correspondence and whole-request rejection.
- Preservation of all supported UV sets and variable skin influence rows; unsupported semantics block edits unless explicitly declared row-local.
- CMake installation and a separate installed consumer.
- Optional Python targeted duplicate removal with canonical Geometry snapshots, owned candidates, readonly correspondence maps and explicit whole-request rejection. No CLI, adapter or release yet.
- Optional reported in-memory workflow with independent correspondence/attribute preservation predicates, visible topology limits, cooperative cancellation and versioned reports from actual native operations.
- Optional OBJ file workflow composing public import, the existing repair/profile and verified bundle publication, with actual artifact hashes and a report inside the published bundle. Cancellation/source protection, export failures and external report-delivery failures retain explicit outcomes.
- Installed OBJ single-asset/batch commands and reusable sequential scheduling, with whole-request validation, isolated destinations, real per-job reports, fail-fast accounting, cooperative interrupts and explicit aggregate delivery failure.

### Changed

- The default Python wheel enables the charged and glTF memory/file modules and builds against Geometry `48298b24be7c132816fb5580d22d58bde61558d0` (Python `0.0.1.dev134+g48298b24b`) and Interchange `5b9b695ba1fef166baa8c7cf9ad21a5630691f70` (Python `0.0.1.dev179+g5b9b695ba`). Rebuild all native producers together and retain their parser, Eigen and OBJ notices. Existing OBJ behavior and import boundaries are retained.

- Native builds enable the charged module by default and require Geometry `1a364acb0cba4fbf8cd9cccde5c04a1fa4428536`. Select `MESHVALE_REPAIR_MATERIALIZED_DUPLICATES=OFF` for legacy-only builds. Python packaging explicitly selects OFF and retains its existing exact dependency cohort.

- The OBJ asset workflow rebuilds Interchange `8d5823272b576052553608dbff26685ba56b4dea` (Python `0.0.1.dev62+g8d5823272`) against the same attribute-storage Geometry producer.

- Canonical numerical attribute access now uses Geometry's Eigen-owned `ScalarBuffer<T>` alternatives, with a compiled-interface configuration check. The matched Geometry producer is `ec38fabd783a578947d43856b0c7a7fd54ce8533` (Python `0.0.1.dev69+gec38fabd7`). Native variant callers must update typed access and rebuild against the matching Geometry producer; owned Python mesh records retain their existing schema and bits.

- The optional OBJ asset workflow now requires the matching Interchange `5ff43f07208f656a168b2a66d28da400e7b5c10f` candidate (Python `0.0.1.dev50+g5ff43f072`) built with the same owned-position Geometry producer.

- Native position access uses Geometry's owned PositionBuffer interface at `920be542502652b1d16c5f90414cec6495ff62b4` (Python `0.0.1.dev57+g920be5425`), preserving canonical mesh records and scalar bits. Linked distributions retain the producer's Eigen notices; older native snapshots are rejected during configuration.

- The optional binding compiles Geometry's installed record implementation through its per-extension factory and links the compiled native package, with exact matched producer requirements; mesh records and operation behavior are unchanged.

- Exact native/Python Geometry and optional Interchange dependencies now use the public source candidates recorded in the package and installation contracts.

- Native and Python dependency evaluation now pins Geometry's pooled-editing development candidate. Repair continues to use owned dense snapshots and its existing copy-producing operation; editor mutation and Python editor bindings are separate capabilities.
- C++ files now use the Google-based formatter with an exact-version check in development and CI; existing public API names remain unchanged.
- C++ callers use `meshvale/repair/duplicates.h` exclusively; the `duplicates.hpp` forwarding header is removed, requiring source callers to update that include path. Declarations remain in `.h` and operation implementation in `duplicates.cpp`; the independent canonical-header and installed-consumer checks retain unchanged behavior with language extensions disabled.
- Python and native evaluation now use the report-capable Geometry revision pinned in the package and operation contracts. The earlier exact-pinned Geometry development candidate is no longer the tested dependency for this branch.
- Asset evaluation pins the verified-supplement Interchange candidate and exercises real publication under all six product import orders; the primitive API and native library keep their original dependency boundary.
