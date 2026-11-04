# Deformation-preserving glTF targeted repair

| Field | Value |
|---|---|
| ID | REPAIR-GLTF-DEFORMATION-001 |
| Version | 0.2.0 |
| Status | Development implementation present; full qualification and release availability pending |
| Owner | Repair composition, options, called results, Python/commands and report projection |

This contract extends the existing explicit selected-primitive duplicate operation
through native memory/file APIs, Python, single commands and ordered batches.
It uses Interchange's deformation-retained-v1 authority and existing Geometry
inspection/selection. It adds no second Repair algorithm, automatic target choice,
weld, transform bake, vertex compaction or general conversion pipeline.

Quaternion source-squared admission is selected and immutable for this profile.
The development implementation resolves the complete positive preset tables
below. Full workflow and dependency-cohort qualification remain pending. No
command example here establishes availability, runtime support or a compatible
binary cohort. Old static/translation calls, schemas1/2, presets and wire remain unchanged.

## RGDEF-REQUEST-001: Native and Python selection

Native requests retain their existing extraction profile selector, target list,
Repair options, source/canonical owners and default kRelease failure retention.
Append explicit deformation phase policies; source extraction uses the policy in
the extraction request. Other memory phases have source_read,materialization,
writing,reload_read,reload,verification policies. Optional file publication supplies
read/extraction/verification policies for its staged calls. Every applicable
policy is explicit/positive and stays under the real existing parent allowance.
Capture uses its own new-read request before the file engine is invoked.

The Python call signature remains:

    repair_gltf_file(source, destination, targets, *,
        primitive_ordinal, triangles, values,
        processing_profile="static-local-triangles-v1",
        input_id="source", resource_root=None, row_local_attributes=(),
        report_path="receipts/report.json", execution=None, cancellation=None,
        retain_failure_facts=False)

The new profile string is deformation-retained-v1. It selects only the matching
retained-core-deformation-v1 read interpretation at the fixed normative revision.
Legacy source authority never upgrades. Old preset/profile combinations fail
before capture. Destination=None still means no export in the Python API; commands
continue to require it. Empty targets explicitly request identity.

Direct execution=None calls share the existing interpreter module-owned512 MiB
ledger/four-worker pool across old/new balanced policy handles, concurrent calls
and retained results. Explicit sessions retain their actual independent context.
GltfExecution() still defaults to balanced-v1. New preset names are
balanced-deformation-v1 and serial-deformation-v1; both admit all three profiles.
Their full resolved positive tables are linked below; no old default or source
profile is silently changed.

## RGDEF-RESULT-001: Called facts without a successful record

Add readonly deformation_facts: None for old profiles; otherwise a fresh detached
15-key dictionary containing profile,read_admission_profile,normative_revision,
quaternion_policy and
these11 nullable actual producer returns:

capture_read,source_read,source,materialization,writing,reload_read,reload,
verification,publication_read,publication_reload,publication_verification.

Null means its helper did not return, including earlier/configuration/seed failure.
A called expected failure retains the actual fixed inline observation immediately
after joins, even without a successful source proof or ordinary workflow record.
A retained input does not invent a source_read call. Staged calls own independent
read/extraction authority. Successful owners' getters and inline result copies
agree; lazy property access performs no validation or weight recomputation.

Each nonnull observation has exactly status,diagnostic,phase,complete,location,
counts,predicates,new_requested_bytes,effective_request_cap,work,execution,numeric,
weight_sums,quaternions. Status is actual Interchange ordinal0..9; diagnostic preserves full
matching native fields and code0..36, including mismatch35/numerical36. Counts and
scheduler facts are actual uint64 work, not fabricated contiguous progress.
Numeric's seven fields and refusal/mask snapshots preserve actual-thread facts.
Weight summaries retain performed sum/deviation/epsilon words and advisories,
including partial/noncontiguous completion and later failure, in bounded inline
storage. Two samples do not replace full raw source/canonical data.

The immutable quaternion_policy is gltf-quaternion-source-squared-v1, with no
caller override. Read/proof/reload authority binds it only after actual source
admission; selection text is not authority. Node summaries come from capture_read,
source_read,reload_read,publication_read; animation summaries from source,reload,
publication_reload. Materialization/writing/verification stages compare authority
and raw roles without fabricating repeated unit checks.

quaternions is required null or exactly {policy,complete,node_values_checked,
animation_values_checked,tangent_rows_checked,rows_admitted,first_refusal}.
The source refusal is null or exactly {role,encoding,node,animation,sampler,key,
accessor,row,reason}; location fields are null/uint64 and domains are the closed
Interchange policy values. Host refusal stays in numeric, not source defects.
Facts are copied after actual return/joins before fallible report/owner transfer
and survive later failure. Node original decimals remain separate from projected
words; q/-q, signed zero, normalized flags/aliases and tangent data never change.

Existing actual capture/memory/publication diagnostics, candidate/maps/counts,
requested bytes and native report availability remain separate properties.
Normal Repair and explicitly retained completed-Repair fallback owners remain
mutually exclusive. Default release does not erase fixed called observations.
A lazy Python allocation failure cannot alter completed native publication.
Unexpected native exceptions propagate after joins without a manufactured result.

The readonly deformation_options property is None for old profiles, otherwise
exactly processing_profile,read_admission_profile,normative_revision,
numerical_profile,quaternion_policy,deformation_limits,deformation_work. It reads
the actual immutable selected configuration retained by the prepared result before
capture, not clipped limits reconstructed from telemetry. Applicable configured
policies remain available even if a later helper never ran; inapplicable slots
are null. This detached view may allocate lazily without changing native outcomes.
Successful source assets expose their actual DeformationReadPolicy() and proofs
their configured Policy(); both are immutable borrows, not new admission inputs.

## RGDEF-JOBS-001: Closed jobs/3 and session admission

Top-level keys are exactly schema and jobs, with schema
meshvale.repair.gltf-jobs/3 and an ordinary0..16-job list. Each row requires
id,input,destination,targets,primitive_ordinal,triangles,values,processing_profile.
Optional keys remain resource_root,row_local_attributes,report_path. No row-level
session, read profile, raw limits or preset override is accepted.

IDs are unique nonempty bounded strings. Paths are nonempty; resource_root may be
null/omitted (source parent), report_path defaults receipts/report.json and cannot
be null. Targets/declarations are ordinary two-element lists; indices are ordinary
nonnegative bounded integers, never bool. Triangles is require-triangles or
convert-strip-or-fan; values is raw or raw-and-normalized. Profile is exactly static,
direct-translation or deformation-retained as spelled above. Unknown/missing keys,
duplicate JSON keys, nonfinite numbers, unsupported value types and mismatches
reject the whole invocation before any capture, including an invalid last row.

| Surface/schema | Omitted policy | Explicit old family | Translation family | Deformation family |
|---|---|---|---|---|
| Single direct API | Matching balanced handle on one module context | static only | static/translation | all3 |
| Single module/console | Fresh context, matching balanced family; omitted profile static | static only | static/translation | all3 |
| jobs/1 | Fresh old-balanced context, including empty | yes | yes | yes |
| jobs/2 | Fresh translation-balanced context, even all-static/empty | all rows static | yes | yes |
| jobs/3 | Fresh deformation-balanced context, even all-legacy/empty | all rows static | no deformation rows | yes |

Families include their balanced and serial names. Jobs1 stays implicit-static and
rejects a processing_profile key. Jobs2 requires it but rejects deformation.
Jobs3 requires it for every row. Empty explicit-session batches satisfy row
compatibility vacuously; omitted policy still follows schema. No per-row context
or schema/source upgrade occurs. run_gltf_jobs is a command facade even in Python:
execution=None creates a fresh invocation context/token, unlike the direct API.

Keep existing inclusive ceilings:16 jobs,8 MiB CLI/canonical parsed-API request,
one-million total target pairs,16 MiB detached report/job,8 MiB outer framing.
Preflight every shape/profile/session/path-isolation rule, report runtime and final
prepared result/argument ownership before asset work. Existing alias/concurrency/
missing-resource semantics retain their native owner. One scheduler invokes the
same prepared API once per attempted row in request order, releases native owners
after detachment, and preserves existing fail-fast/cancellation/exit precedence.
Empty uncancelled batches return0; actual cancellation returns130 at the established
final observation. Wire ceilings are not Python heap, payload or RSS guarantees.

## RGDEF-REPORT-001: Exact version3 projection

Geometry report/1 and batch/1 envelopes stay unchanged. Native deformation receipts
use extensions["meshvale.repair.gltf"] string version"3": existing version1 native
keys/meanings plus exactly processing. Processing is the same15-key object above,
with profile deformation-retained-v1, read_admission_profile
retained-core-deformation-v1 and normative_revision exactly {commit,blob} for the
selected Khronos revision. All11 slots are present null/object; none is omitted.

operation.options retains old fields and adds exactly processing_profile,
read_admission_profile,normative_revision,numerical_profile,deformation_limits,
deformation_work,quaternion_policy. numerical_profile is gltf-deformation-rne-gradual-v1. Limits and
work each have all11 slot keys, null only where not applicable to that operation;
otherwise actual resolved18-limit/8-work objects. Configured values remain unchanged;
actual clipped allowances belong to returned observations.

Memory profile is gltf-deformation-duplicate-preservation/1; requested-file profile
is gltf-deformation-duplicate-publication/1. Required native memory checks are0..7
plus10..16, file checks0..7 plus10..17. Optional solid8 and translation9 are not
new required checks. New IDs in native order10..17 are:

gltf.deformation.source-admission, gltf.deformation.influences,
gltf.deformation.bindings, gltf.deformation.morphs,
gltf.deformation.affecting-animation, gltf.deformation.canonical-association,
gltf.deformation.output-reload, gltf.deformation.staged-reload.

Requested files additionally require BOTH inherited string IDs
staged-selected-primitive and staged-physical-members through their existing
FileCoverage owner. These are not enums8/9 or substitutes for deformation checks.
Any actual required false finding fails the profile, even when every deformation
check passed. Otherwise an unperformed required check makes it incomplete; only
all required true passes. Actual commit IO failure can retain passed preservation;
a failed required physical predicate cannot. Publication remains its separate real
typed/core outcome, with no new postcommit preservation check.

| Condition | Native coverage/passed | Wire status/finding |
|---|---|---|
| Required predicates evaluated true | Performed/true | performed/passed |
| Actual malformed/identity/comparison false | Performed/false | performed/failed |
| Numerical/profile exclusion or extraction logical cap unsupported | Unsupported/null | unsupported/null |
| Called request/budget/execution limit; writer/inspector count limit | Failed/null | failed/null |
| Stop or earlier different phase | Blocked/null | skipped/null |

Scopes use real canonical snapshots/meshes or permitted null references; animation
creates no Geometry map. Existing maps/counts remain real Repair results.
Memory read/extract failures use import, transfer inspect, Repair repair, memory
postcandidate write/reload/verification verify, report refusal report and physical
publisher failure export. Completed finite rejection remains complete/exit1,
execution failure2, actual cancellation130, passing completion0.

## RGDEF-PREDICATES-001: Intrinsic producer ownership

Generic reads and extractions have no source/reload/staged role argument. Their
observations retain the actual `source_admission` predicate; they never infer an
output or staged comparison from configuration or call order. Repair combines
those admission facts with the actual owning comparison result:

| Required check | Exact conjunction of actual returned predicates |
|---|---|
| gltf.deformation.output-reload | reload_read.source_admission AND reload.source_admission AND verification.output_reload |
| gltf.deformation.staged-reload | publication_read.source_admission AND publication_reload.source_admission AND publication_verification.staged_reload |

The memory inspector owns the source/candidate/reloaded comparison. The private
publication verification operation owns `publication_verification`: its own
returned observation covers genuine staged capture, expected source/candidate
inspection and physical verification. It establishes `staged_reload` only after
those actual checks, before the existing provider/commit boundary. A generic
inspector observation is never renamed or copied as a staged observation.

`GltfFileVerifiedContent::staged_deformation` borrows that actual publication
observation only through the synchronous verified callback/provider return. It
defaults to null for legacy or uncalled paths. Repair copies the fixed value into
its owned inline facts before fallible report work and never retains the borrowed
pointer. The publisher supplies it only after genuine staged semantic and physical
verification, before the provider and native commit; the final publication result
retains the same actual operation's observation. No postcommit receipt patch is
permitted. A new-profile provider missing this required return refuses before
commit with a source-contract failure; a null value is never substituted by
configuration, generic comparison facts or a successful empty observation.

All influence/binding/morph/animation/association conjunctions remain unchanged.
Required true results need every applicable actual return. An actual false
dominates missing evidence; otherwise missing read, extraction or comparison is
blocked, numerical/profile exclusion is unsupported, and called allocation/work
failure is failed. Both inherited physical file checks remain independently
required. Observation shapes, profile/API values and old profiles are unchanged.

This table corrects the earlier role-specific predicate expectation for generic
read/extraction calls. It selects no new role option or capability and establishes
no implementation, qualification or availability claim.

## RGDEF-FALLBACK-001: Missing native bytes are not success

Nonempty native bytes always win and are forwarded exactly, including failures.
Without them the command-owned extension is meshvale.repair.gltf.commands string
version"3", retaining its existing exact keys plus processing_profile,processing.
Its origin remains command-capture-fallback or command-workflow-fallback.
processing is nonnull even if all11 producer slots are null. Actual numeric/read
capture refusal can remain while the workflow never started.

The outer fallback profile remains gltf-command-native-receipt/1 with required
native-receipt-validation skipped/null and profile incomplete. It does not borrow
the native deformation file profile or infer its checks failed/ran. Actual input
ID and unavailable hashes, candidate/diagnostics, whitelisted capture/workflow/
publication facts and resolved options remain; absent means unavailable, not0.
Preservation/artifacts stay empty and publication verified=false. A published
outcome without its genuine receipt is a delivery error, not fallback success.
Accepted/unchanged fallback needs actual retained counts and complete maps; missing
facts cause delivery failure, never reconstruction from files or receipts.

All mandatory native report/provider work stays before no-replace commit.
No emergency receipt, postcommit serialization/patch/callback or obligatory reread
establishes success. Existing prepared Python return and explicit runtime/lazy
delivery-failure boundaries remain. A stop after commit cannot undo publication.

## RGDEF-QUALIFY-001: Implementation and compatibility

The existing operation, ownership, stage/publisher and Geometry rich transfer seams
are reused. Full JSON/GLB changed/identity behavior, all influence sets/zeros,
canonical ragged0/2/5 separately from dense glTF, every shared/inactive binding,
morph role/default and affecting animation, real accounting/joins and retained
later failures are required. The complete preset tables are implemented; matching
native/installed/relocated/Python cohort and runtime qualification remain pending.
This contract establishes no supported release or broader conversion/corpus claim.


<a id="rgdef-presets-001-complete-proposed-policy-tables"></a>
## RGDEF-PRESETS-001: Resolved policy tables

The [balanced table](presets/balanced-deformation-v1.json) and
[serial table](presets/serial-deformation-v1.json) each define all589 positive
resolved values: 302 unchanged generic values, one preset-family declaration-name
allowance and 286 deformation leaves.
They are passive contract tables, not an additional job/configuration input format.
Each table identifies contract REPAIR-GLTF-DEFORMATION-001/0.2.0. Before the first
qualified publication, this revision retains the two new v1 preset identifiers.
Each new family permits an inclusive aggregate of 4096 logical declaration-name
bytes, including generated source-qualified names. The 128-declaration count and
all other bounds remain. This family allowance applies to all three processing
profiles selected with either new preset. It is finite capacity, not a measured
cost, a normative minimum or a guarantee that all maxima fit together. All four
existing presets retain their exact values and meanings, including their
256-byte aggregate allowance. Both new tables
share536870912 tracked-payload bytes; balanced leases at most4 workers and serial1.
Their capacities/partitions/checkpoints match; serial uses parallel minima
9223372036854775807 and max_jobs1. Balanced deformation work uses scalar minimum/
partition16384, byte minimum/partition262144, scalar checkpoint256, byte checkpoint
16384, reference checkpoint256 and max_jobs64, independently of the4 shared leases.

For the new profile only, scalar scheduling means S=C+I+R and byte scheduling B as
defined by the Interchange deformation contract; observed scalar_checks and
authored inventory retain their meanings. Conservative loop planning,16-inner-
helper checkpoints and actual original-context joins are required. The implementation uses these work units and tables. Their configured values
are not measurements or a supported-runtime qualification claim.

Source policy is only request.extraction.deformation. Applicable configured policy
slots are retained before capture; null is reserved for structurally inapplicable
slots. Called observations/effective caps never reconstruct configuration. Each
new request must fit its configured validator remainder, COMPLETE owning-stage
remainder, fresh-workflow remainder where applicable and original shared ledger.
The same physical block is charged once; an independent reload/copy is distinct.
Owner counters/configured maxima are not summed as a usage proof or reservation.

The validator subcaps in bytes are read4194304, payload16777216, materialization
2097152, writing4194304 and verification8388608 at their11 respective roles. The
materialization complete triple remains bridge4194304 + Geometry capture33554432 +
inspection4194304 =41943040; the new2097152 subcap is INSIDE the bridge. Pass the
original triple unchanged after checked admission, with no fourth allowance or
term clamping. Fresh memory184549376 and wrapper41943040 remain distinct existing
scopes under the same ledger. Publication keeps its existing independent complete
file/read/extraction/inspection/container parents, without a new aggregate cap.

Actual backing/control/worker/summary/copy and report replacement allocations must
remain charged throughout overlap. Logical helper widths, field counts and the
33554688 logical bytes of two16MiB reports plus the existing256-byte tail prove no
native size, stack placement or40MiB allocation fit. Fixed facts and full typed
raw/canonical data remain distinct; no per-row advisory vector, dropped role,
mode downgrade or synthetic fallback proof makes an input fit. Honest cap/budget
refusal stays observable. Native sizes, all parent request accounting, checkpoint
coverage, independent workers and full report/lazy failure paths need actual
implementation qualification. No source/ABI/numerical/runtime support is implied.


Weight-sum observations belong only to actual payload producer roles source,
reload and publication_reload. All read, transfer, writer and comparison-only
roles return weight_sums=null, including a successfully called empty node/unit
reader. A real complete-zero payload summary is different from an unperformed
weight fold. Actual payload partial/later-failure summaries remain retained.


Full command fallback examples select the actual balanced-deformation-v1
retained-file-publication configuration: source_read limits/work are structurally
null; applicable uncalled roles retain their configured preset values. Capture
validator configuration is4194304 under the complete25165824 read parent. An
effective4194304 example additionally requires observed parent/shared remainders
at entry to be at least that much; otherwise report the actual smaller clipped
allowance. Configured/read-only options are not reconstructed from telemetry.
Read-only options retain the selected applicable configuration even after an
earlier failure. Requested-byte counts and numerical/source failures retain their
actual producer values; configuration cannot manufacture or recompute those facts.
