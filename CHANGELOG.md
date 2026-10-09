# Changelog

## Unreleased

### Added

- Native targeted duplicate-face removal for indexed polygon meshes, with cyclic face/corner correspondence and whole-request rejection.
- Preservation of all supported UV sets and variable skin influence rows; unsupported semantics block edits unless explicitly declared row-local.
- CMake installation and a separate installed consumer.
- Optional Python targeted duplicate removal with canonical Geometry snapshots, owned candidates, readonly correspondence maps and explicit whole-request rejection. No CLI, adapter or release yet.
