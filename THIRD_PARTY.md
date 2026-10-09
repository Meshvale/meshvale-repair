# Dependency inventory

| Dependency | Tested input | Role / notices |
|---|---|---|
| Meshvale Geometry | Native revision `36d36cba938ab660ad25c71c0afbb0d3f5963ff3`; Python `0.0.1.dev11+g36d36cba9` | Installed C++20 headers / owned record protocol; separately installed canonical Python Mesh and report envelope; Apache-2.0 [retained attribution](licenses/meshvale-geometry-notice.txt) |
| jsonschema | `4.26.0`, through Geometry's `reports` extra | Optional workflow reference validation; MIT, separately installed with its own COPYING and dependencies; not vendored or linked into Repair's extension |
| nanobind | `3.1.0` | Optional extension runtime linked statically; BSD-3-Clause [notice](licenses/nanobind.txt) |
| tsl::robin_map | Bundled with nanobind 3.1.0 | MIT [notice](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling, not wheel runtime dependencies |

Original Repair code remains Apache-2.0. Python wheels include actual dependency notices; native-only builds do not incorporate nanobind. Candidate wheels are platform/Python-ABI specific and depend on the platform C++ runtime. These inputs are an evaluated development combination, not an arbitrary compatible-version range or a released package set.
