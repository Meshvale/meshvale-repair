# Deformation format-preservation fixtures

The native `gltf_deformation_format_preservation` regression reads these literal
assets through public Interchange and Repair APIs. The test owns independent
component-word, layout, triangle-expansion and correspondence expectations.

| Fixtures | Discriminator |
| --- | --- |
| `indexed-strip-edit`, `indexed-fan-edit` | Three expanded triangles, one cyclic duplicate removed, opposite winding retained; edited output uses triangles. |
| `nonindexed-strip-identity`, `nonindexed-fan-identity` | Four vertices, two triangles; identity preserves the original primitive mode and absence of indices. |
| `influences-unorm16` | Three influence sets with normalized unsigned 16-bit weights, including zero lanes. |
| `influences-cross-width` | Normalized unsigned 8-bit and 16-bit weights in one primitive. |
| `influences-mixed` | Floating and normalized integer weight sets, including authored signed zero. |
| `morph-encodings` | UV and color base/delta encodings described below. |
| `opaque-tail-alias` | Edited GLB with ordered JSON/BIN/A/B/A chunks, distinct repeated-type payloads, BIN tail/padding and two resource identities for one physical file. |

The influence and morph assets have JSON and GLB representations. Lowercase
`.glb.hex` files decode to real GLB wire bytes only inside the bounded test helper.
`shared.bin.hex` similarly becomes the real external file `shared.bin` in the
exclusive temporary input tree. Its two authored URI spellings are `shared.bin`
and `%73hared.bin`; both retain the full six physical bytes with a four-byte
logical buffer bound. The embedded BIN has 110 logical bytes, an unreferenced
nonzero tail and two zero alignment-padding bytes.

The morph table contains five UV pairs (all VEC2): UNORM8/FLOAT,
UNORM16/UNORM16, FLOAT/UNORM8, FLOAT/SNORM8 and FLOAT/SNORM16. Its six color pairs
are UNORM8 VEC3/UNORM16 VEC4, UNORM16 VEC4/UNORM8 VEC3,
FLOAT VEC3/SNORM8 VEC4, UNORM8 VEC4/FLOAT VEC4,
UNORM16 VEC3/SNORM16 VEC3 and FLOAT VEC4/UNORM16 VEC3.
Signed and floating morph deltas include negative values. Dense vertex data with
components smaller than four bytes has explicit aligned four- or eight-byte
strides. Every original scalar word remains an independent test expectation.

These inputs follow the retained-core contract's selected
[glTF specification revision](https://github.com/KhronosGroup/glTF/blob/1c84b58ba3338be4731ea546fa4138dcbb93d42d/specification/2.0/Specification.adoc),
including its effective morph component types, base-presence and VEC3/VEC4 color
rules. They do not request a quantization extension or normalize authored values.
The finite test limits are native test policies, not application presets.

The regression exercises eight supplied-memory workflows and nine physical
publications, checks each producer's preservation predicates and independent
reload, compares staged report bytes, and verifies original-ledger release and
cleanup. This fixture set does not establish a release, ABI or platform matrix.

## Numerical test selection

Both portable native deformation test targets are built in materialization-qualified
builds. The applicability test and nine format-preservation positive runtime cases
share registration only for the named Release configuration after a fresh
compile-only check of the retained deformation
numerical profile. This check uses the actual generator/configuration and
standard/CRT headers: MSVC full version 195136248, STL 145/update 202604,
CRT-header 14.51.36244.0, dynamic CRT, iterator-debug 0, x64, strict C++20 and the
required binary32/binary64 representations. Other configurations/toolchains
retain their existing tests; an unregistered positive is not an unsupported
runtime pass. The native inventory checker validates the derived selection.

Use matching Interchange/Geometry producer SDKs and consumer compiler, headers,
runtime and configuration. The compile check does not attest an installed SDK
binary or actual thread state. An unexpected refusal on an eligible run fails
the unchanged assertions; no runtime fallback enables or suppresses the test.

An eligible single-config Release build includes both positives in ordinary
`ctest` as well as `ctest -C Release`. Multi-config builds register it only for
Release; select `ctest -C Release` there. Debug never gains the positive from a
stale configuration-list cache entry. These selections do not replace existing
unsupported-profile checks or qualify a producer binary.
