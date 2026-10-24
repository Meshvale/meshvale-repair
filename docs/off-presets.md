# OFF execution presets

| Field | Value |
|---|---|
| ID | REPAIR-OFF-PRESETS-001 |
| Version | 0.1.0 |
| Status | Selected development values; new file/workflow implementation pending |

The [complete JSON table](off-presets.json) owns every one of183 native leaf
values for `balanced-v1` and `serial-reference-v1` used by the
[OFF workflow](off-workflow.md). Native callers continue to supply explicit
limits/work policies. No implicit glTF resource, primitive, container or attribute
configuration belongs to this interface.

`OffPreset` contains `execution: ExecutionOptions`,
`capture_limits: OffFileReadLimits`, `capture_work: OffFileReadWorkPolicy`,
`workflow_limits: OffDuplicateFileWorkflowLimits` and
`workflow_work: OffDuplicateFileWorkflowWorkPolicy`. The workflow contract owns
its wrapper declarations; Interchange OFF-FILES-001 owns file aggregates and
OFF-IO-001 owns codec declarations. Geometry/Repair own their existing delegates.
All fields are positive, with checked host64 representation/products/sums.

Balanced uses4 workers and512MiB common tracked payload. Entry/source/output
ceilings are64MiB; receipt16MiB; two-member total80MiB; vertices/faces1million;
corners3million; corners/face65536; face offsets1000001. A16MiB line,256-byte
token and1MiB comment have independent meanings. Input IDs are128 ASCII bytes
within schema256; portable entry/receipt names1024 UTF-8 bytes; native paths4096
code units including terminator. Front-end caller paths additionally cap4096
UTF-8 bytes before IO; actual filesystem limits can be lower.

OFF creates/adopts no attributes. Geometry's required positive attribute,
scalar/offset/presence/metadata/descriptor limits are1 for empty corresponding
inputs. Repair's declaration count/name caps are also1 with an empty declaration
span. These nonzero placeholders do not admit dropping an unexpected channel.

Each read allows128MiB codec +64MiB materialization +8MiB inspection =200MiB
configured aggregate. Writing allows256MiB codec +8MiB inspection =264MiB.
Pass each nested cap unchanged after checked sum admission. Repair has96MiB
aggregate,64MiB selection and8MiB per inspection under actual live accounting.
File-module fresh requests have a separate128MiB cap. Initial, memory and staged
reads remain separate operations on the same live ledger.

Wrapper fresh requests cap128MiB, source baseline64MiB, report16MiB, and raw
source-plus-output hash input128MiB. A64MiB baseline and two16MiB report buffers
alone can coexist as96MiB; every additional request still needs actual admission.
Hash raw source and output once each, without source framing or a self-report
hash. No count/byte maximum promises another cap or the common512MiB ledger has
remaining capacity. These are request scopes, not process peak/RSS guarantees.

Byte threshold/partition are256KiB, byte cancellation64KiB and IO block64KiB.
OFF token/record cancellation intervals are256; Geometry scalar interval8192;
max_jobs64. Report threshold/partition64KiB and cancellation16KiB. Serial codec
worker statistics stay zero. A single SHA digest is serial; do not invent
parallel hash jobs.

Serial-reference changes only24 worker/threshold leaves: worker budget/max_jobs1
and parallel thresholds, including `minimum_parallel_vertices`, to signed64 max.
It requires a64-bit host. Caps, cancellation intervals and algorithms match
balanced; it is a caller-only comparison preset, not a separate implementation.
Actual allocation/cancellation/boundary and platform qualification remains
required before a supported preset claim.
