# Targeted exact duplicate-face removal

Contract version: **0.1.2**, development API. The C++ declarations in [duplicates.h](../include/meshvale/repair/duplicates.h) own interface shapes. This document owns operation behavior.

## Header interface

Use `meshvale/repair/duplicates.h`, the self-contained C++20 interface with a
path-derived include guard. The former `duplicates.hpp` forwarding header has
been removed; existing source callers must update their include path. Public
types, functions and operation behavior retain their contracts. Non-template
operation implementation is compiled from `src/duplicates.cpp`.

Native tests compile the canonical header independently and repeatedly. The
separate installed consumer calls the real operation through the installed `.h`
interface and target; CMake builds use C++20 with extensions disabled.

## Request and equivalence

`remove_duplicate_faces` takes a const geometry mesh and explicit `(keep, remove)` face pairs. Indices refer to input storage. Every removed face must be distinct, no representative may also be removed, and both faces must exist. Chains and self-pairs are rejected. A valid empty request returns an owned unchanged copy and identity maps.

Faces match only when their directed vertex-index loops agree up to cyclic rotation and every aligned authored corner row and face row agrees numerically exactly. All named sets participate, including secondary UV maps. Missing and authored rows differ; backing values of two missing rows do not participate in equivalence. Ragged row lengths and values participate when authored. Opposite winding and coincident coordinates on different vertex indices do not imply equivalence. Repeated vertex loops use the first rotation that matches both indices and attributes.

There is no tolerance. Signed floating zero compares equal for duplicate eligibility. Authored floating payloads must be finite. Retained storage, including signed zero and missing-row backing values, is copied without numerical changes. The operation does not validate normal lengths, UV ranges, skin bindings, weight sums or joint/weight pairing.

## Attribute policy

The initial understood semantics are `texcoord`, `normal`, `color`, `material_index`, `label`, `identity`, `joint_indices` and `joint_weights`. These designate row-local values or references to unchanged asset/vertex data. The caller must use these labels truthfully. Unknown semantics block a nonempty edit in any domain, since their data may refer to changed face/corner identities.

For a custom channel, the caller can explicitly declare its `(domain, name)` row-local in `DuplicateOptions`. This declaration means values have no references to face/corner indices that need remapping. It does not disable equality checks. Internal provenance belongs in the returned correspondence, not a fabricated semantic channel. Asset-level material definitions and skin associations remain the caller's responsibility.

There is no single-UV or four-influence limit. Vertex channels are copied intact, including multiple joint/weight sets and variable-length rows. Every face/corner channel is selected by retained source rows, preserving type, name, semantic, set index, width, metadata, presence and dense/ragged representation.

## Candidate and correspondence

The result is `accepted`, `unchanged` or `rejected`. Accepted and unchanged results contain an independently owned mesh. Rejected results contain no candidate or maps, with diagnostic codes instead. Input is never modified. Allocation failures propagate as C++ exceptions; no in-place edits occur before them.

The entire request is transactional: one invalid target rejects all pairs. Retained faces preserve source order. Positions and vertex identities are unchanged; unused vertices remain. Face/corner arrays are compacted only to remove selected copies. Every source face maps to its output face; removed faces map to the surviving representative. Every source corner maps to its retained or rotationally aligned output corner. These many-to-one maps retain all input element identities.

Before acceptance, checks inspect candidate storage, authored floating values, correspondence and preservation of every retained row/loop independently of the construction loop. There is no hidden welding, hole filling, winding change, triangulation, normal generation, weight normalization or scene edit. Open surfaces and non-planar polygons can remain open/non-planar. This operation establishes its narrow preservation conditions, not manifoldness, self-intersection freedom, outward orientation or solid validity. Edge/boundary inspection, file adapters, serialized reports, CLI/batch and export completion are not implemented by this native API.

## Dependency and installation

Use C++20, CMake 3.24+ and an installed `MeshvaleGeometry` package. The evaluated development dependency is public geometry revision `920be542502652b1d16c5f90414cec6495ff62b4`; native packages identify untagged snapshots as `0.0.0`, which is not a compatibility promise. CI installs that exact revision before building this repository. The native CMake build does not download dependencies or require private sources. The optional [Python interface](python.md) owns its separate package/buffer requirements; native operation behavior above is unchanged.

Install geometry into a chosen prefix, then supply that absolute prefix through `CMAKE_PREFIX_PATH`:

```sh
cmake -S . -B .local/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$GEOMETRY_PREFIX"
cmake --build .local/build --config Release
ctest --test-dir .local/build -C Release --output-on-failure
cmake --install .local/build --config Release --prefix "$REPAIR_PREFIX"
cmake -S examples/consumer -B .local/consumer -DCMAKE_PREFIX_PATH="$REPAIR_PREFIX;$GEOMETRY_PREFIX"
cmake --build .local/consumer --config Release
ctest --test-dir .local/consumer -C Release --output-on-failure
```

The installed target is `meshvale::repair`, discovered with `find_package(MeshvaleRepair CONFIG REQUIRED)`. Its geometry dependency is transitive. Exact clean `vMAJOR.MINOR.PATCH` Git tags determine release versions; no release has been published yet.

The documented Geometry producer provides the compiled owned `PositionBuffer` interface. The build checks that seam because native development version `0.0.0` alone does not identify a compatible source interface, and verifies retained notices against the installed producer. Geometry keeps Eigen private: consuming this installed package requires neither Eigen headers nor a source checkout. Native installs and wheels include the producer's retained Eigen license and attribution files.
