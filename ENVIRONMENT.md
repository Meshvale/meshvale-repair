# Local development environment

This file owns the boundary between portable configuration and machine settings.

Copy [environment.example.json](environment.example.json) to `.local/environment.json` and fill only the fields needed for your task. Keep local paths, credentials, source assets, and raw logs in ignored storage. The template describes configuration inputs; no automatic loader or native build integration is implemented yet.

`workspace_root` is the optional local checkout/workspace location. `vcpkg_root` is an optional existing vcpkg installation. Compiler, generator, triplet, and corpus fields remain unset until used. Relative build paths resolve from this repository. Native code uses C++20, CMake 3.24+ and an installed `MeshvaleGeometry` development package. No vcpkg third-party dependency is required by this initial operation.

Follow [the operation contract](docs/duplicates.md#dependency-and-installation) for the tested geometry revision and native build/install/consumer commands. Supply installed prefixes explicitly through `CMAKE_PREFIX_PATH`; this template does not change the build environment automatically. Native tests treat warnings as errors. CI evaluates Windows, Linux and macOS runners; those checks do not establish a Python wheel or stable ABI matrix.

The current checks need Git and Python 3.10 or newer, with no third-party Python packages:

Optional Python builds use the pinned build/runtime dependencies in [pyproject.toml](pyproject.toml), an installed native Geometry prefix and ordinary GIL-enabled CPython. See [Python packaging and ownership requirements](docs/python.md). Keep candidate wheel directories, virtual environments and raw logs under ignored `.local/`; native-only builds and the checks below do not need these Python dependencies.

```sh
python scripts/check-portability.py
python scripts/check-docs.py
```

Public files use repository-relative links, public URLs, tool names, and symbolic environment values. Run these checks before committing or publishing. The portability check scans working files and staged content for machine paths and private local files; it does not scan all secrets or determine whether a document should be public. Review public content and package contents separately.
