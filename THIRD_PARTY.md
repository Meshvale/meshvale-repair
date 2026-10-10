# Dependency inventory

| Dependency | Tested input | Role / notices |
|---|---|---|
| Meshvale Geometry | Native revision `519eaf2f4eda24c39eaa3ace7f213e5571239fe2`; Python `0.0.1.dev23+g519eaf2f4` | Installed C++20 headers / owned record protocol; separately installed canonical Python Mesh and report envelope; Apache-2.0 [retained attribution](licenses/meshvale-geometry-notice.txt) |
| Meshvale Interchange | Revision `abcef4a72d67d550d3a7f0a0559dbe686c963898`; Python `0.0.1.dev24+gabcef4a72` | Optional `assets` extra's separately installed OBJ/MTL adapter and verified publisher; Apache-2.0 [dependency notices](https://github.com/Meshvale/meshvale-interchange/blob/abcef4a72d67d550d3a7f0a0559dbe686c963898/THIRD_PARTY.md); its parser is not linked or vendored into Repair |
| jsonschema | `4.26.0`, through Geometry's `reports` extra | Optional workflow reference validation; MIT, separately installed with its own COPYING and dependencies; not vendored or linked into Repair's extension |
| nanobind | `3.1.0` | Optional extension runtime linked statically; BSD-3-Clause [notice](licenses/nanobind.txt) |
| tsl::robin_map | Bundled with nanobind 3.1.0 | MIT [notice](licenses/robin-map.txt) |
| scikit-build-core / setuptools-scm | `1.1.1` / `10.3.4` | Python build/version tooling, not wheel runtime dependencies |

Original Repair code remains Apache-2.0. Python wheels include actual dependency notices; native-only builds do not incorporate nanobind. Candidate wheels are platform/Python-ABI specific and depend on the platform C++ runtime. These inputs are an evaluated development combination, not an arbitrary compatible-version range or a released package set.
