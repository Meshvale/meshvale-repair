# Dependency inventory

| Dependency | Tested input | Role / notices |
|---|---|---|
| Meshvale Geometry | Native revision `a27cae686ea4f4c6dabeebfc984f0265602e3b1d`; Python `0.0.1.dev35+ga27cae686` | Installed C++20 headers / owned record protocol; separately installed canonical Python Mesh and report envelope; Apache-2.0 [retained attribution](licenses/meshvale-geometry-notice.txt) |
| Meshvale Interchange | Revision `84e7912cba74e6c78410f42fee75856332940c94`; Python `0.0.1.dev37+g84e7912cb` | Optional `assets` extra's separately installed OBJ/MTL adapter and verified publisher; Apache-2.0 [dependency notices](https://github.com/Meshvale/meshvale-interchange/blob/84e7912cba74e6c78410f42fee75856332940c94/THIRD_PARTY.md); its parser is not linked or vendored into Repair |
| jsonschema | `4.26.0`, through Geometry's `reports` extra | Optional workflow reference validation; MIT, separately installed with its own COPYING and dependencies; not vendored or linked into Repair's extension |
| nanobind | `3.1.0` | Optional extension runtime linked statically; BSD-3-Clause [notice](licenses/nanobind.txt) |
| tsl::robin_map | Bundled with nanobind 3.1.0 | MIT [notice](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling, not wheel runtime dependencies |

Original Repair code remains Apache-2.0. Python wheels include actual dependency notices; native-only builds do not incorporate nanobind. Candidate wheels are platform/Python-ABI specific and depend on the platform C++ runtime. These inputs are an evaluated development combination, not an arbitrary compatible-version range or a released package set.
