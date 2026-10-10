# Dependency inventory

| Dependency | Tested input | Role / notices |
|---|---|---|
| Meshvale Geometry | Native revision `5ad9642911d36c9f81f242a3012ac0e77df919e5`; Python `0.0.1.dev55+g5ad964291` | Compiled C++20 library / owned per-extension record protocol; separately installed canonical Python Mesh and report envelope; Apache-2.0 [retained attribution](licenses/meshvale-geometry-notice.txt) |
| Meshvale Interchange | Revision `041acf9e201ae0ae5cf2afd0aeb1e798e229300e`; Python `0.0.1.dev48+g041acf9e2` | Optional `assets` extra's separately installed OBJ/MTL adapter and verified publisher; Apache-2.0 [dependency notices](https://github.com/Meshvale/meshvale-interchange/blob/041acf9e201ae0ae5cf2afd0aeb1e798e229300e/THIRD_PARTY.md); its parser is not linked or vendored into Repair |
| jsonschema | `4.26.0`, through Geometry's `reports` extra | Optional workflow reference validation; MIT, separately installed with its own COPYING and dependencies; not vendored or linked into Repair's extension |
| Eigen | `3.4.1`, through the exact compiled Geometry producer | Private unmodified Core implementation; `EIGEN_MPL2_ONLY`, internal threading disabled, fast-math disabled; [MPL-2.0](licenses/eigen-mpl2.txt), [Apache-2.0](licenses/eigen-apache.txt) and [included upstream notices](licenses/eigen-notices.txt); [source](https://gitlab.com/libeigen/eigen/-/tree/3.4.1). No Eigen types or headers are required by installed consumers |
| nanobind | `3.1.0` | Optional extension runtime linked statically; BSD-3-Clause [notice](licenses/nanobind.txt) |
| tsl::robin_map | Bundled with nanobind 3.1.0 | MIT [notice](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling, not wheel runtime dependencies |

Original Repair code remains Apache-2.0. Python wheels include actual dependency notices; native-only builds do not incorporate nanobind. Candidate wheels are platform/Python-ABI specific and depend on the platform C++ runtime. These inputs are an evaluated development combination, not an arbitrary compatible-version range or a released package set.
