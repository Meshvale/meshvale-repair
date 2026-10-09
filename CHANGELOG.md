# Changelog

## Unreleased

### Added

- Native targeted duplicate-face removal for indexed polygon meshes, with cyclic face/corner correspondence and whole-request rejection.
- Preservation of all supported UV sets and variable skin influence rows; unsupported semantics block edits unless explicitly declared row-local.
- CMake installation and a separate installed consumer.
- Optional Python targeted duplicate removal with canonical Geometry snapshots, owned candidates, readonly correspondence maps and explicit whole-request rejection. No CLI, adapter or release yet.
- Optional reported in-memory workflow with independent correspondence/attribute preservation predicates, visible topology limits, cooperative cancellation and versioned reports from actual native operations.
- Optional OBJ file workflow composing public import, the existing repair/profile and verified bundle publication, with actual artifact hashes and a report inside the published bundle. Cancellation/source protection, export failures and external report-delivery failures retain explicit outcomes.
- Installed OBJ single-asset/batch commands and reusable sequential scheduling, with whole-request validation, isolated destinations, real per-job reports, fail-fast accounting, cooperative interrupts and explicit aggregate delivery failure.

### Changed

- New C++ callers use `meshvale/repair/duplicates.h`; the existing `.hpp` include remains an installed forwarding header with unchanged declarations and behavior. Native and installed-consumer builds compile both spellings independently with language extensions disabled.
- Python and native evaluation now use the report-capable Geometry revision pinned in the package and operation contracts. The earlier exact-pinned Geometry development candidate is no longer the tested dependency for this branch.
- Asset evaluation pins the verified-supplement Interchange candidate and exercises real publication under all six product import orders; the primitive API and native library keep their original dependency boundary.
