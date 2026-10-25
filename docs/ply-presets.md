# PLY execution presets

| Field | Value |
|---|---|
| ID | REPAIR-PLY-PRESETS-001 |
| Version | 0.1.0 |
| Status | Selected development values; implementation and qualification pending |

The [complete JSON table](ply-presets.json) selects every one of215 native leaf
values for `balanced-v1` and `serial-reference-v1` in the
[PLY workflow](ply-workflow.md). Native callers still provide explicit limits
and work policies. This document and its table select application values; they
do not establish an implemented preset resolver or accepted platform.

The selected `PlyPreset` has five roots: `execution: ExecutionOptions`,
`capture_limits: PlyFileReadLimits`, `capture_work: PlyFileReadWorkPolicy`,
`workflow_limits: PlyDuplicateFileWorkflowLimits` and
`workflow_work: PlyDuplicateFileWorkflowWorkPolicy`. Their leaf counts are
3/38/12/116/46. The workflow owns its wrapper declarations; Interchange's
PLY-FILES-001 owns file aggregates and PLY-IO-001 owns codec declarations.
Geometry and Repair own their existing delegates. Every numeric leaf is positive
and checked for host64 representation, extents, byte products and aggregate sums.
The caller-path UTF-8 cap is4096 bytes before IO, separate from these215 leaves;
actual filesystem limits may be lower.

## Payload, schema and attribute scopes

Balanced selects4 workers and512MiB common tracked payload. Source, entry and
encoded output caps are64MiB; receipt16MiB; planned entry-plus-receipt80MiB.
File-module fresh requests have their own128MiB cap. Portable entry/receipt names
cap1024 UTF-8 bytes; native paths4096 code units including their terminator.
Input IDs cap128 ASCII bytes within the report schema's256-byte bound.

Each codec configuration caps vertices/faces at1000000, corners at3000000,
corners/face at65536, header bytes at1MiB, header lines at4096, line bytes at16MiB
and token bytes at256. Read comments cap1MiB. Property names cap128 bytes and
aggregate schema names1MiB. Properties cap1024, including XYZ and the structural
vertex-index list. List rows cap4000000, counting structural/custom list count
prefixes even when empty. Items/list row cap65536. Custom property values cap
16000000; positions, structural indices and count prefixes are not custom values.

Every materialization/selection scope caps face offsets at1000001, attributes
at1024, attribute scalars at16000000, attribute offsets at4001024 and descriptor
bytes at1MiB. Its fresh requests cap128MiB. Presence bytes and metadata entries
remain required-positive1: the admitted PLY binding has no presence mask, set,
semantic or metadata fields. These values cannot authorize dropping a channel.
Unlike an attribute-free format, the active attribute count/value/offset/descriptor
and declaration caps are not empty-input placeholders.

Repair allows1000000 targets,1024 declarations and1MiB aggregate declaration-name
bytes. Front-end declaration names and domain/name pairs are structurally
admitted and owned before capture; native callers keep borrowed storage alive
through return and joins. Valid unused declarations are allowed. Exact matching
where used and nonempty-target semantic eligibility follow the existing engine;
empty targets skip that eligibility path without an invented check result. Its comparison cap is16000000 logical steps per target. All inspection
scopes allow1024 issues and8MiB fresh requests.

## Independent allocation scopes

Each PLY read configures128MiB codec +128MiB materialization +8MiB inspection,
with264MiB aggregate. Initial capture, memory reload and staged reload are
separate operations on the same live ledger. Writing configures256MiB codec
+8MiB inspection, with264MiB aggregate. Repair configures192MiB aggregate and
128MiB selection; its required inspection caps remain independent.

Wrapper fresh requests cap128MiB, retained original-byte protection64MiB and
report bytes16MiB. Hash input caps128MiB: source and output raw bytes are each
hashed once. A64MiB source baseline and two16MiB report buffers alone can coexist
as96MiB; other live requests still need actual admission. Captured entry bytes
alias the codec document rather than adding a permanent duplicate source.

Pass nested limits unchanged after checked aggregate admission. A scoped maximum
does not promise that every configured maximum can be reached simultaneously.
Retained source/candidate/output/reload/report owners share the common ledger;
occupied requests can cause a genuine refusal or admitted caller fallback.
Do not sum shared owners twice, infer process RSS or report configured budgets
as observed usage. Keep file, codec, materialization, inspection, engine and
wrapper attributed observations distinct.

## Work policies and serial reference

Balanced file IO/cancellation blocks are65536 bytes, ordinary parallel and
partition thresholds262144 bytes, scalar checkpoints8192, codec value/record
checkpoints256 and maximum jobs64. Report parallel threshold is65536 bytes.
The JSON table owns every exact occurrence. Typed comparisons keep scalar and
offset boundaries intact and check cancellation while walking values/schema.

Serial reference selects1 worker, sets minimum parallel thresholds to
9223372036854775807 and every maximum-job field to1. It retains the same payload,
schema, attribute, declaration and cancellation caps; it changes scheduling,
not preservation policy or admitted wire types. A configured parallel policy is
not evidence that workers ran. Preserve actual launches/joins/caller work and
the codec's actual serial observations.

Any future value or field change requires updating the complete table, resolver
and corresponding admission/qualification checks together. Presets select no
additional encoding, property interpretation, repair algorithm, runtime floor,
supported release or package publication.
