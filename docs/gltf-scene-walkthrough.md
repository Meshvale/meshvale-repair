# A shared mesh in a static scene

| Field | Value |
|---|---|
| ID | REPAIR-GLTF-SCENE-EXAMPLE-001 |
| Version | 0.1.2 |
| Status | Development example implemented; final integration/platform qualification pending |
| Owner | Static shared-instance walkthrough and its literal preservation oracle |

This example applies the existing [file operation](gltf-python.md) and
[single/batch commands](gltf-commands.md) to one explicitly selected primitive.
It demonstrates the native workflow's existing shared-primitive preservation;
it adds no instance-local edit, mesh clone, discovery, transform evaluation or
scene-wide processing API. Dynamic skin, morph and animation processing retains
the native operation's existing admission limits. There is no stable release.

## Original scene and expected result

One scene has roots `[0,1,2]` and four retained nodes. Node0 uses mesh0 with
translation `[2,0,0]`, rotation `[0,0,0,1]` and scale `[2,1,1]`. Node1 also uses
mesh0 with the authored column-major matrix
`[1,0,0,0,0,1,0,0,0,0,1,0,-3,4,0,1]`. Node2 uses mesh1 with translation
`[0,0,5]`. Node3 is an unused named node with no mesh. These exactly
representable values are retained as authored; the operation never evaluates
or bakes them.

Each mesh has one primitive. Primitive ordinal0 has three vertices and two
identical triangles, and binds material0. Primitive ordinal1 is unrelated,
uses distinct position rows and one triangle, and binds material1. The two
materials have distinct named definitions and preserved texture/image resource
bindings. Generated original resource bytes, including unused/trailing bytes,
have independent literal expectations. The GLB variant includes embedded BIN
padding and an opaque chunk; JSON glTF and GLB retain their original format.

Both primitives carry `POSITION` and raw named `TEXCOORD_0` vertex rows. The
request explicitly declares `("vertex","TEXCOORD_0")` row-local, using
`--row-local vertex:TEXCOORD_0` in single commands and
`"row_local_attributes":[["vertex","TEXCOORD_0"]]` in batch jobs. This uses the
existing [attribute policy](duplicates.md); it does not infer semantics or ignore
channel equality. The undeclared changed request is a separate actual refusal
control and must publish nothing. UV rows remain unchanged in accepted output.

Request `(keep=0,remove=1)` on primitive ordinal0. Its two triangles become one
with vertex identity, face map `[0,0]` and corner map `[0,1,2,0,1,2]`.
Both retained instances still refer to the same mesh0; instance count stays
three in total, including the unrelated mesh1 instance. Primitive ordinal1,
all vertex/channel rows, node/scene identities, authored transform presence and
bits, material definitions/bindings and resources remain unchanged. The explicit
unchanged request retains the complete original entry/container bytes.

## Entry points and independent inspection

The installed Python walkthrough creates this same original scene in JSON
glTF and GLB and runs changed/unchanged requests through the file API,
console/module commands and API/CLI batches. It compares real receipts,
literal batch slices, correspondence, physical artifact hashes and original
source bytes. A mixed batch demonstrates an actual missing-resource failure,
continuation and fail-fast without inventing successful publication.

## Run the walkthrough

Follow the [build recipe](quickstart.md) and install the declared workflow extra
for the [glTF/GLB Python interface](gltf-python.md). Its initial qualified cohort
is Windows x64 MSVC Release CPython3.13. Unsupported numerical profiles retain
their actual native refusals.
From this checkout, choose a new output directory to retain the original scene,
published results, requests and reports:

```sh
python -I examples/python/gltf_scene_walkthrough.py --output .local/shared-scene
```

For independent native inspection, set `NATIVE_PREFIXES` to the compatible
installed Interchange, Geometry and parser prefixes used for the wheel, then
build the separate consumer:

```sh
cmake -S examples/scene-consumer -B .local/scene-inspector -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$NATIVE_PREFIXES"
cmake --build .local/scene-inspector --config Release
```

Set `SCENE_INSPECTOR` to the built executable, including its configuration
subdirectory and `.exe` suffix where applicable. Run again with a different new
output directory:

```sh
python -I examples/python/gltf_scene_walkthrough.py --scene-inspector "$SCENE_INSPECTOR" --output .local/inspected-scene
```

The installed-test helper's `--gltf qualified|unsupported` selection also runs
the scene tests and walkthrough. Its optional `MESHVALE_GLTF_SCENE_INSPECTOR`
environment value selects the independent executable; repository Python CI
builds it against the installed native dependencies.

## What native inspection checks

The separate installed C++ scene consumer accepts
`SOURCE_ENTRY OUTPUT_ENTRY changed|unchanged`. It captures both actual assets
through the installed Interchange file interface and inspects native node,
scene, primitive and instance views. It asserts the literal fixture identities
and before/after preservation independently of the workflow's own checks.
The Python walkthrough accepts optional `--scene-inspector PATH` to run this
consumer for each successful output. Without it, the walkthrough does not claim
that this independent native consumer executed. JSON lexical-table checks
remain separate from native scene-view inspection. The two original texture
resources are valid one-pixel red and blue PNGs. The workflow retains them as
opaque bytes; image decoding and rendered appearance are outside its checks.

Qualified-profile success and actual unsupported numerical refusal are separate
executions. An unsupported producer must return its genuine native result and
publish nothing; it is not called successful scene repair. Source/package
identities, exact producer cohort and actual execution determine acceptance.
The shared [native file contract](gltf-file-workflow.md) continues to own
concurrent-change, resource, publication and preservation limits.
