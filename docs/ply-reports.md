# PLY report wire contract

| Field | Value |
|---|---|
| ID | REPAIR-PLY-REPORT-001 |
| Version | 0.1.0 |
| Status | Selected development wire profile; implementation and qualification pending |

This document owns the PLY-specific options and extensions inside unchanged
`meshvale.report/1` for the [file workflow](ply-workflow.md). Objects below have
exactly the listed required keys unless an alternative is explicitly defined.
`U` is an exact JSON integer in0..18446744073709551615; `U?` means U or null.
Arrays preserve order. Null means unavailable/not reached, not zero or success.
No nonfinite number, duplicate key or machine path is emitted.

## PLY-REPORT-OPTIONS-001: Operation and checks

Core operation is `kind="repair"`, `name="remove-duplicate-faces"`,
`version="1"`. Its `options` is exactly:

| Key | Type/value |
|---|---|
| `encoding` | `ascii`, `binary_little_endian` or `binary_big_endian` string |
| `rewrite` | String `canonical` |
| `targets` | Array of objects `{keep: U, remove: U}` in caller order |
| `row_local_attributes` | Array of `{domain: "vertex"/"face"/"corner", name: string}` in caller order |

Names obey wrapper identifier/count/byte admission. Options record supplied
declarations, including valid unused names; their presence never proves that
semantic eligibility ran. Empty targets deliberately use the unchanged engine
path without that evaluation. `options.targets` always retains every raw caller
pair. Core `operation.targets` is empty unless the report actually emits the
matching input snapshot and its surface mesh. With that real snapshot it contains
one `{snapshot:"input", mesh:"surface", domain:"face", index:remove}` per request,
including actual requested indices later rejected as out of range. This is a
reference-availability rule, not a new target range rejection. A missing-file,
pre-capture-stop or other no-snapshot projection invents neither snapshots nor
core target references.

Memory profile name is `ply-typed-duplicate-preservation`, version`1`. Its ordered
required check IDs are `source-storage`, `target-policy`, `candidate-storage`,
`repair-correspondence`, `canonical-preservation`, `reload-candidate`,
`source-unchanged`, `typed-properties`, `wire-schema`. Publication profile name
is `ply-typed-duplicate-publication`, version`1`, adding `staged-candidate` and
`staged-physical-members` in that order. Required findings/coverage and core
outcomes retain the shared report rules. An empty-target policy check can pass
the actual no-edit policy; it cannot assert an executed attribute eligibility
check. `solid-validity` remains optional and unsupported.

## PLY-REPORT-NATIVE-001: Native extension

Native report bytes contain `extensions["meshvale.repair.ply"]` with:

| Key | Exact type/meaning |
|---|---|
| `version` | Integer1 |
| `native_status` | `PlyWorkflowStatus` unsigned ordinal |
| `native_stage` | One stage string from the table below |
| `native_diagnostic` | Workflow diagnostic object defined below |
| `source` | `{portable_name:string, encoding:Encoding, schema:[Element]}` |
| `policies` | `{encoding:Encoding, rewrite:"canonical", units:"unknown", row_local_attributes:[Declaration]}`; declaration objects equal the options above |
| `memory_output_verified` | Boolean; true only after all required memory comparisons and source-baseline verification |
| `source_faces` | U array from actual completed Repair owner, otherwise null |
| `source_corners` | U array from actual completed Repair owner, otherwise null |
| `write_facts` | WriteFacts object once an actual writer output exists, otherwise null |
| `phase_diagnostics` | `{repair:RepairPhase?, writing:PlyPhase?, reload:PlyPhase?}` |
| `publisher` | Publisher object after the actual publisher returns, otherwise null |
| `artifact_paths` | Empty object before/without publication; after true publication exactly `{entry:string, receipt:string}` portable relative basenames |
| `hash_bytes_processed` | U actual processed raw-stream bytes, including a real partial hash operation |
| `requested_bytes_before_report_copy` | RequestedBytes snapshot defined below |
| `phase_execution` | PhaseExecution snapshot defined below |

`Encoding` is one of the three option strings. `Element` is exactly
`{kind:"vertex"/"face", original_count:U, properties:[Property]}`.
`Property` is exactly `{name:string, kind:"scalar"/"list", value_type:Type,
count_type:IntegerType?, binding:Binding, attribute:U?}`. `Type` is one of
`char/uchar/short/ushort/int/uint/float/double`; `IntegerType` excludes float/double.
`Binding` is `x/y/z/vertex-indices/attribute`. Count type is null for scalar
properties; attribute ordinal is null for core bindings and actual for custom
properties. This is source schema provenance, not the rewritten element counts.

`WriteFacts` has exactly `source_encoding`, `source_vertex_count`,
`source_face_count`, `encoding`, `rewrite_policy`, `vertex_count`, `face_count`,
`corner_count`, `property_count`, `correspondence`. Counts are U. Encoding fields
use Encoding strings; rewrite policy is `canonical`; correspondence is `identity`
at the supplied-mesh-to-serialization boundary. Facts are actual writer facts
even when later verification fails; `memory_output_verified` remains false then.
They do not replace the four edit arrays or claim lexical/comment preservation.

Workflow status ordinals0..8 are Success, InvalidConfiguration, InvalidRequest,
LimitExceeded, BudgetExceeded, Canceled, Rejected, DelegatedFailure,
VerificationFailure. Stage ordinals0..9 and their `native_stage` strings are
Configuration/configuration, SourceProtection/source-protection, Repair/repair,
Write/write, Reload/reload, Verify/verify, Hash/hash, Report/report,
Publication/publication, Complete/complete. C++ enumerators have the `k` prefix.
Workflow diagnostic code ordinals start at0 in this order: None,
RequiredFieldZero, ConfigurationExtent, InputIdentity, Policy, NameUnsupported,
NameCollision, CountLimit, RequestBudget, StopRequested, Repair, Write, Reload,
Correspondence, CanonicalPreservation, ReloadMismatch, SourceChanged, HashExtent,
ReportExtent, ReportStringEncoding, Publication, Blocked, SolidUnavailable,
Declaration, TypedProperties, WireSchema. Do not renumber published ordinals.

Workflow diagnostic is exactly `{stage:U, code:U, element:U?, delegated:Phase?}`.
`Phase` is `{kind:"repair"/"ply", status:U, diagnostic:RepairDiagnostic/PlyDiagnostic}`.
RepairPhase and PlyPhase omit `kind` and use their named diagnostic type.
Null phase means the delegate was not called; successful called phases retain
their actual success status and diagnostic rather than becoming null.

Delegate status/stage/code numbers are unsigned ordinals from the owning public
Repair materialized-duplicate, Interchange PLY and Geometry materialization
headers: the [Repair declarations](../include/meshvale/repair/materialized_duplicates.h),
[Interchange PLY declarations](https://github.com/Meshvale/meshvale-interchange/blob/aa9c551c01f248a09c96ee5f2476f0feb4997495/include/meshvale/interchange/ply.h)
and [Geometry declarations](https://github.com/Meshvale/meshvale-geometry/blob/26c8b33e6d0b5bcebdef89ccc46f74889e0bdab6/include/meshvale/geometry/materialization.h).
They are native diagnostics, never shared report outcomes or CLI exits.
Append-only enum growth does not reinterpret an existing ordinal. The PLY file
contract separately freezes publisher enum ordinals. Full typed native results
remain available independently of these JSON observations.

All diagnostic objects have exactly these keys; optional native members emit
null when absent:

| Object | Keys/types |
|---|---|
| `StorageIssue` | `code:U, attribute:U?, element:U?` |
| `GeometryDiagnostic` | `code:U, attribute:U?, element:U?, storage_issue:StorageIssue?` |
| `RepairDiagnostic` | `stage:U, code:U, target:U?, attribute:U?, element:U?, geometry_status:U?, geometry_diagnostic:GeometryDiagnostic?, storage_issue:StorageIssue?` |
| `PlyDiagnostic` | `stage:U, code:U, byte_offset:U?, element:U?, property:U?, row:U?, item:U?, attribute:U?, geometry_status:U?, geometry_diagnostic:GeometryDiagnostic?, storage_issue:StorageIssue?` |
| `FileDiagnostic` | `status:U, stage:U, code:U, file_index:U?, byte_offset:U?, system_error:SystemError?, ply_status:U?, ply_diagnostic:PlyDiagnostic?, geometry_status:U?, geometry_diagnostic:GeometryDiagnostic?` |
| `SystemError` | `category:string, value:signed integer` from the actual native error category/value |
| `Publisher` | `outcome:U, diagnostic:FileDiagnostic, cleanup:U, cleanup_error:SystemError?` |

## PLY-REPORT-ACCOUNTING-001: Exact observation snapshots

`Stats` has exactly `planned_jobs`, `launched_workers`, `joined_workers`,
`caller_jobs`, each U. `CodecStats` has exactly `codec`, `materialization`,
`inspection`, each Stats. `CodecBytes` has exactly `codec`, `materialization`,
`inspection`, `total`, each U. `RepairBytes` has exactly `repair`,
`source_inspection`, `selection`, `candidate_inspection`, `total`, each U.

RequestedBytes has exactly `wrapper:U`, `source_files:U`,
`source_ply:CodecBytes`, `repair:RepairBytes`, `writing:CodecBytes`,
`reload:CodecBytes`, `publication_files:U?`, `publication_ply:CodecBytes?`.
Publisher-owned fields remain null until it returns. These are attributed live
requests at report preparation before its own final copy/import, not budgets,
cumulative traffic, RSS or an inferred cross-owner sum.

PhaseExecution has exactly `repair`, `writing`, `reload`, `source_protection`,
`repair_correspondence`, `canonical_preservation`, `typed_properties`,
`wire_schema`, `reload_verification`, `source_verification`, `staged_candidate`,
`hashing`, `report_copy`, `publication`. Repair is an object with exactly
`source_inspection`, `eligibility`, `comparison`, `selection`, `correspondence`,
`candidate_inspection`, `verification`, `directed_verification`, each Stats.
Writing/reload are CodecStats; publication is FileExecution or null. All other
members are Stats. FileExecution has exactly `capture`, `copying`,
`byte_verification`, `mesh_verification`, `expected_inspection` as Stats and `ply`
as CodecStats. No worker count is inferred from preset configuration.

Unreached phases retain actual zero-initialized counters; zero alone does not
assert execution or success. Delegate diagnostics/coverage establish reachability.
Report-copy counters describe the snapshot before copying that report, not its
later completed copy. Conditional success bytes are prepared before native commit
and exposed only after a true published outcome. Their native status is Success,
stage is complete and workflow diagnostic is `{stage:9,code:0,element:null,
delegated:null}`, conditioned on that actual commit. Consequently their `publisher`,
publication-owned requested bytes and publication execution are null: the
publisher has not returned. Final typed result facts remain separate. Never patch
that receipt or allocate a mandatory report after commit.

## PLY-REPORT-COMMAND-001: Unpublished missing-receipt projection

Only an actually unpublished result with absent native receipt may use profile
`ply-command-receipt`, version`1`, under unchanged report/1. It uses extension
`meshvale.repair.ply.command`, not a fabricated native extension. This object has
exactly `version:1` and `observations:CommandObservations`.

CommandObservations has exactly `capture_status:U?`,
`capture_diagnostic:FileDiagnostic?`, `workflow_status:U?`, `workflow_stage:U?`,
`execution_outcome:Execution?`, `candidate_outcome:Candidate?`,
`profile_outcome:Profile?`, `publication_outcome:Publication?`, `exit_code:U?`,
`source_counts:Counts?`, `candidate_counts:Counts?`, `face_map:[U]?`,
`corner_map:[U]?`, `source_faces:[U]?`, `source_corners:[U]?`,
`memory_output_verified:boolean?`, `published:boolean` (must be false).
Counts is exactly `{vertices:U, faces:U, corners:U}`. Execution is
`completed/failed/cancelled`; Candidate is `accepted/unchanged/rejected/not-attempted`;
Profile is `passed/failed/incomplete`; Publication is
`not-requested/not-attempted/failed/cancelled/published`. Strings follow the shared
report spellings; publisher diagnostic enums remain numeric native facts.
Exit is one of0/1/2/130. Publication outcome cannot be `published` in this
projection. Null represents an unavailable observation, not a default.

Project only actual retained binding observations. Completed accepted/unchanged
repair requires genuine counts and four engine arrays; core vertex identity is
a separate preservation fact. Before repair do not invent snapshots or maps.
Required `native-receipt-validation` is skipped by absence and makes the fallback
profile incomplete. Claims/artifacts stay empty. Present native bytes must be
validated and used, never replaced by this projection. A genuinely published
result with missing/invalid/undeliverable receipt is delivery failure with no
aggregate; preserve its true committed outcome and files.
