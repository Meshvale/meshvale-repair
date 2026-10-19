# Dependency inventory

| Dependency | Tested input | Role / notices |
|---|---|---|
| Meshvale Geometry | Native revision `48298b24be7c132816fb5580d22d58bde61558d0`; Python `0.0.1.dev134+g48298b24b` | Compiled C++20 library / owned per-extension record protocol; separately installed canonical Python Mesh and report envelope; Apache-2.0 [retained attribution](licenses/meshvale-geometry-notice.txt) |
| Meshvale Interchange | Revision `5b9b695ba1fef166baa8c7cf9ad21a5630691f70`; Python `0.0.1.dev179+g5b9b695ba` | Compiled glTF capture/publication dependency of the optional native workflow and its Python binding; the `assets` extra separately installs its OBJ/MTL Python adapter; Apache-2.0 [retained attribution](licenses/meshvale-interchange-notice.txt) |
| cgltf | `1.15`, commit `360db1a95480fe102ae9c69b27c5d101167ff5ba`, through Interchange | Private bounded glTF reader implementation; [MIT terms and copyright](licenses/cgltf.txt) |
| jsoncons | `1.10.0`, commit `9e57e421285422d7aeeb5aa7dc839f6a19121ee6`, through Interchange | Private lossless SAX/number handling; [Boost Software License 1.0 and copyright](licenses/jsoncons.txt) |
| tinyobjloader | `2.0.0rc13`, commit `2945a967c5303b2c8c14174117c45f3302591150`, through Interchange | Double-precision static dependency in the compiled producer closure; [MIT terms and copyright](licenses/tinyobjloader.txt) |
| jsonschema | `4.26.0`, through Geometry's `reports` extra | Optional workflow reference validation; MIT, separately installed with its own COPYING and dependencies; not vendored or linked into Repair's extension |
| Eigen | `3.4.1`, through the exact compiled Geometry producer | Private unmodified Core implementation; `EIGEN_MPL2_ONLY`, internal threading disabled, fast-math disabled; [MPL-2.0](licenses/eigen-mpl2.txt), [Apache-2.0](licenses/eigen-apache.txt) and [included upstream notices](licenses/eigen-notices.txt); [source](https://gitlab.com/libeigen/eigen/-/tree/3.4.1). No Eigen types or headers are required by installed consumers |
| nanobind | `3.1.0` | Optional extension runtime linked statically; BSD-3-Clause [notice](licenses/nanobind.txt) |
| tsl::robin_map | Bundled with nanobind 3.1.0 | MIT [notice](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling, not wheel runtime dependencies |

Original Repair code remains Apache-2.0. Python wheels include actual dependency notices; native-only builds do not incorporate nanobind. Candidate wheels are platform/Python-ABI specific and depend on the platform C++ runtime. These inputs are an evaluated development combination, not an arbitrary compatible-version range or a released package set.
