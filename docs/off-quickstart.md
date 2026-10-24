# Targeted repair of indexed OFF polygons

This development workflow removes explicitly selected duplicate faces, keeps
unrelated polygons and unused vertex rows, and publishes a new OFF file with a
verified report. An empty target list requests a real unchanged operation.
Canonical output rewrites text and emits an unknown edge count of zero. It does
not preserve comments, whitespace or original numeric spelling.

The [workflow contract](off-workflow.md) owns exact guarantees and limits.
Native and Python qualification is in progress; these development candidates
are not a supported package-index release.

## Build the optional interface

Use the public Geometry producer
[`48298b2`](https://github.com/Meshvale/meshvale-geometry/tree/48298b24be7c132816fb5580d22d58bde61558d0)
and Interchange producer
[`b577263`](https://github.com/Meshvale/meshvale-interchange/tree/b577263107403e65643fdcfc1b8af3faa5c9fff4).
Follow Interchange's
[OFF file build instructions](https://github.com/Meshvale/meshvale-interchange/blob/b577263107403e65643fdcfc1b8af3faa5c9fff4/docs/off-files.md)
to install `meshvale::off_files`. Match the compiler, configuration, runtime
library and instrumentation across the native packages. Geometry's ordinary
Python wheel must be `0.0.1.dev134+g48298b24b`; retain full Git history when
building this version from source.

Set `NATIVE_PREFIXES` to the semicolon-separated installed Geometry, Interchange
and tinyobjloader prefixes. From this Repair checkout, build an OFF Python wheel
with the optional glTF modules disabled:

```sh
python -m pip wheel . --no-deps --wheel-dir .local/off-wheels \
  --config-settings=cmake.define.CMAKE_PREFIX_PATH="$NATIVE_PREFIXES" \
  --config-settings=cmake.define.MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON \
  --config-settings=cmake.define.MESHVALE_REPAIR_GLTF_WORKFLOW=OFF \
  --config-settings=cmake.define.MESHVALE_REPAIR_GLTF_FILE_WORKFLOW=OFF
```

The build uses the pinned dependencies in [pyproject.toml](../pyproject.toml).
Set `GEOMETRY_WHEEL` and `REPAIR_WHEEL` to the exact local wheel paths, then
install both candidates and the report-validation extra:

```sh
python -m pip install "$GEOMETRY_WHEEL" "${REPAIR_WHEEL}[workflow]"
```

Interchange's Python package is not needed by this OFF path. Native-only callers
enable `MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON` and link
`meshvale::off_file_workflow`; see the
[installed C++ example](../examples/consumer/off_workflow.cpp).

## Run the API and commands

The [self-contained example](../examples/python/off_walkthrough.py) creates
original cyclic duplicate quads, an unrelated triangle and signed-zero vertex
data. It checks maps, raw-source hashes, canonical output and physical receipts:

```sh
python -I examples/python/off_walkthrough.py
python -I examples/python/off_walkthrough.py --commands
```

For an existing file, supply its known duplicate pair explicitly. Face indices
refer to the captured input. The output parent must exist and the destination
directory must be new:

```sh
meshvale-repair off source.off output/new --text canonical-ascii --edge-count unknown-zero --target 0:1
meshvale-repair off source.off output/copy --text canonical-ascii --edge-count unknown-zero --unchanged
```

The reusable call is
`repair_off_file(source, destination, [(0, 1)], text="canonical-ascii", edge_count="unknown-zero")`
from `meshvale_repair.off_workflow`. Pass `destination=None` for memory-only
writing, reload and verification. Inspect returned typed outcomes before using
optional report bytes; an unsupported numerical producer returns its actual
capture refusal for nonempty geometry. The example's `--unsupported-profile`
mode verifies that refusal and runs genuine empty operations.

## Run an ordered batch

Save this request as `jobs.json`; relative paths use that file's logical parent:

```json
{"schema":"meshvale.repair.off-jobs/1","jobs":[
  {"id":"changed","input":"source.off","destination":"output/batch-changed","targets":[[0,1]],"text":"canonical-ascii","edge_count":"unknown-zero"},
  {"id":"copy","input":"source.off","destination":"output/batch-copy","targets":[],"text":"canonical-ascii","edge_count":"unknown-zero"}
]}
```

```sh
meshvale-repair off-batch jobs.json --preset balanced-v1
```

All rows are admitted before capture. Jobs run in order; `--fail-fast` stops
after the first nonzero job exit. Each successful destination contains its OFF
entry and `meshvale-report.json`. Stdout contains one validated report or batch
document; exit codes are 0 for success, 1 for rejection/incomplete coverage,
2 for execution/report-delivery failure, and 130 for observed cancellation.
The [preset table](off-presets.md) states the independent count, byte, worker
and shared-budget limits.
