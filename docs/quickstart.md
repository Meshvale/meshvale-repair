# Try a textured OBJ repair

Remove an explicitly selected duplicate quad from an original four-face OBJ while
keeping a triangle, quad and concave pentagon, a corner UV seam, material bindings,
texture bytes and intentional open boundaries. The walkthrough runs the reusable
Python API, single command and batch command on the **same input and target
`1:3`**: keep source face 1, remove its cyclically rotated copy at source face 3.
It then reloads the three outputs independently and checks their correspondence,
receipts, artifact hashes and unchanged sources. Selecting `0:1` instead asks to
match a triangle with a quad; that request is rejected and publishes nothing.

## Install the development candidates

These packages are development candidates built from public source, with no
package-index release. Use ordinary GIL-enabled CPython 3.10 or newer, a C++20
compiler and CMake 3.24 or newer. Wheels must match the interpreter, operating
system and architecture used to run the example.

Geometry and Interchange must match Repair's exact requirements in
[pyproject.toml](../pyproject.toml):

| Product | Public source revision | Python candidate |
|---|---|---|
| Geometry | `5ad9642911d36c9f81f242a3012ac0e77df919e5` | `0.0.1.dev55+g5ad964291` |
| Interchange | `041acf9e201ae0ae5cf2afd0aeb1e798e229300e` | `0.0.1.dev48+g041acf9e2` |

Choose absolute paths for `GEOMETRY_SOURCE`, `INTERCHANGE_SOURCE`, this
`REPAIR_SOURCE` checkout, `GEOMETRY_PREFIX`, `TINYOBJLOADER_PREFIX`,
`CANDIDATE_WHEELS` and `VENV`. Keep build directories outside tracked source.
The following commands use POSIX shell variables; PowerShell callers use
`$env:NAME` for environment variables and activate with
`& "$env:VENV/Scripts/Activate.ps1"`.

Clone Geometry and Interchange with full history so development versions can be
derived, then check out the exact revisions:

```sh
git clone https://github.com/Meshvale/meshvale-geometry.git "$GEOMETRY_SOURCE"
git -C "$GEOMETRY_SOURCE" checkout 5ad9642911d36c9f81f242a3012ac0e77df919e5
git clone https://github.com/Meshvale/meshvale-interchange.git "$INTERCHANGE_SOURCE"
git -C "$INTERCHANGE_SOURCE" checkout 041acf9e201ae0ae5cf2afd0aeb1e798e229300e
python -m venv "$VENV"
. "$VENV/bin/activate"
python -m pip install jsonschema==4.26.0
```

Build and install Geometry's native development package in Release mode:

```sh
cmake -S "$GEOMETRY_SOURCE" -B "$GEOMETRY_SOURCE/.local/native" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$GEOMETRY_SOURCE/.local/native" --config Release
cmake --install "$GEOMETRY_SOURCE/.local/native" --config Release --prefix "$GEOMETRY_PREFIX"
```

Interchange also needs **double-precision tinyobjloader `2.0.0rc13`**. Follow its
[public dependency setup](https://github.com/Meshvale/meshvale-interchange/blob/041acf9e201ae0ae5cf2afd0aeb1e798e229300e/ENVIRONMENT.md)
using the pinned vcpkg manifest, and set `TINYOBJLOADER_PREFIX` to the resulting
installed target-triplet prefix. Retain the same compiler/runtime and Release
configuration. The [dependency inventory](https://github.com/Meshvale/meshvale-interchange/blob/041acf9e201ae0ae5cf2afd0aeb1e798e229300e/THIRD_PARTY.md)
owns the parser revision and notices; the
[binding build contract](https://github.com/Meshvale/meshvale-interchange/blob/041acf9e201ae0ae5cf2afd0aeb1e798e229300e/docs/python.md#build-installation-and-example)
owns its package requirements.

Build the three local wheels, then install Repair with its asset dependencies
from that directory. Python build tools come from the pinned build requirements
in each public source checkout; the Meshvale runtime packages come from your
local wheels:

```sh
python -m pip wheel "$GEOMETRY_SOURCE" --no-deps --wheel-dir "$CANDIDATE_WHEELS"
python -m pip wheel "$INTERCHANGE_SOURCE" --no-deps --wheel-dir "$CANDIDATE_WHEELS" --config-settings=cmake.define.CMAKE_PREFIX_PATH="$GEOMETRY_PREFIX;$TINYOBJLOADER_PREFIX"
python -m pip wheel "$REPAIR_SOURCE" --no-deps --wheel-dir "$CANDIDATE_WHEELS" --config-settings=cmake.define.CMAKE_PREFIX_PATH="$GEOMETRY_PREFIX"
python -m pip install --no-index --find-links "$CANDIDATE_WHEELS" "meshvale-repair[assets]"
python -m pip check
```

Use a fresh wheel directory containing one intended candidate of each product.
For the narrower primitive interface and additional packaging details, see
[Python installation](python.md#build-and-consumption).

## Run and inspect

Choose `WALKTHROUGH_ROOT` as a **new directory with an existing parent**, outside
the source checkouts. The example refuses existing paths and keeps its output:

```sh
python -I "$REPAIR_SOURCE/examples/python/obj_walkthrough.py" "$WALKTHROUGH_ROOT"
```

The final message confirms four faces became three. Inspect:

| Path under the chosen directory | Contents |
|---|---|
| `sources/` | Original OBJ, MTL and a small valid PPM checker texture |
| `outputs/api/`, `outputs/single/`, `outputs/batch/` | Matching repaired bundles, each with `meshvale-report.json` |
| `api-report.json`, `single-report.json`, `batch-report.json` | Returned API, single-command and aggregate batch evidence |
| `jobs.json` | Runnable batch request with explicit targets |
| `rejected-report.json` | Rejected triangle/quad request; `outputs/rejected/` is absent |

Open `model.obj` in a viewer supporting OBJ/MTL and PPM textures, or inspect the
plain-text polygon loops and reports directly. Texture rendering depends on the
viewer; the adapter verifies resource bytes rather than decoding images.

The reusable call demonstrated by the example is:

```python
from meshvale_repair.obj_workflow import repair_obj_file

result = repair_obj_file(source_obj, new_bundle, [(1, 3)], input_id="mixed-panel")
print(result.exit_code, result.report["publication"]["outcome"])
```

To try the commands yourself after the example, run them from the chosen
directory and use fresh destinations:

```sh
cd "$WALKTHROUGH_ROOT"
meshvale-repair obj sources/model.obj outputs/manual --target 1:3 --input-id mixed-panel
meshvale-repair obj sources/model.obj outputs/manual-rejected --target 0:1 --input-id mixed-panel
```

The successful command returns exit 0 and prints its JSON report. The rejected
command returns exit 1, prints its rejected report and creates no output bundle.
For batch, change the destination in `jobs.json` to `outputs/manual-batch` before
running `meshvale-repair obj-batch jobs.json`. Reusing `outputs/batch` would
correctly fail because it already exists. See [the command contract](cli.md) for
request fields and exit meanings.

This workflow performs selected exact duplicate-face removal. It does not select
duplicates automatically, close holes or offer general mesh repair. OBJ/MTL is
the implemented file adapter; glTF/GLB support remains forthcoming.
