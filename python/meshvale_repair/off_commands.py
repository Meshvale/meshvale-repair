# SPDX-License-Identifier: Apache-2.0
"""Bounded admission and detached OFF receipts over the prepared file operation."""
from dataclasses import dataclass
import json
import os
from pathlib import Path

from . import __version__
from . import off_workflow as _workflow
from .commands import InvocationError


_REQUEST_SCHEMA = "meshvale.repair.off-jobs/1"
_REPORT_SCHEMA = "meshvale.report/1"
_BATCH_SCHEMA = "meshvale.batch/1"
_EXTENSION = "meshvale.repair.off.commands"
_MAX_JOBS = 16
_MAX_REQUEST_BYTES = 8 * 1024 * 1024
_MAX_TARGET_PAIRS = 1_000_000
_MAX_REPORT_BYTES = 16 * 1024 * 1024
_MAX_REPORT_ALLOWANCE = _MAX_JOBS * _MAX_REPORT_BYTES
_MAX_FRAMING_BYTES = 8 * 1024 * 1024
_MAX_INDEX = (1 << 64) - 1
_PROFILE = "off-command-receipt"
_RECEIPT_CHECK = "native-receipt-validation"
_ABSENT_RECEIPT = "Native receipt validation was blocked because no native receipt bytes are available."
_DEFAULT_REPORT_PATH = "meshvale-report.json"
_PROHIBITED_MEMBER_CHARACTERS = frozenset(':<>"|?*/\\')
_ASCII_FOLD = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
_WINDOWS_DEVICE_STEMS = frozenset(("con", "prn", "aux", "nul")) | frozenset(
    prefix + str(number) for prefix in ("com", "lpt") for number in range(1, 10))
_JSON = json.JSONEncoder(ensure_ascii=False, separators=(",", ":"), allow_nan=False)
_MAX_PATH_BYTES = 4096
_REQUIRED = frozenset(("id", "input", "destination", "targets", "text", "edge_count"))
_OPTIONAL = frozenset(("report_path",))
_TEXT_POLICIES = ("canonical-ascii",)
_EDGE_POLICIES = ("unknown-zero",)

# Only closed native observation fields enter detached fallback documents.
# Native paths, arbitrary object dictionaries and runtime owners never do.
_ENUM_DOMAINS = frozenset(('CandidateOutcome',
 'MaterializedDuplicateDiagnosticCode',
 'MaterializedDuplicateStage',
 'MaterializedDuplicateStatus',
 'MaterializationDiagnosticCode',
 'MaterializationStatus',
 'StorageIssueCode',
 'OffWorkflowStage',
 'OffWorkflowStatus',
 'OffWorkflowDiagnosticCode',
 'OffWorkflowExecutionOutcome',
 'OffWorkflowProfileOutcome',
 'OffWorkflowPublicationOutcome',
 'OffWorkflowExitMeaning',
 'OffWorkflowCheck',
 'OffWorkflowCoverageState',
 'OffProfile',
 'OffStatus',
 'OffStage',
 'OffDiagnosticCode',
 'OffTextPolicy',
 'OffEdgeCountPolicy',
 'OffCorrespondence',
 'OffFileStatus',
 'OffFileStage',
 'OffFileDiagnosticCode',
 'OffFilePublicationOutcome',
 'OffFileCleanupOutcome'))
_OBSERVATION_FIELDS = {'OffWorkflowDiagnostic': ('stage', 'code', 'element', 'delegated_status', 'delegated'),
 'OffWorkflowOutcomes': ('execution', 'candidate', 'profile', 'publication', 'stage', 'exit'),
 'OffWorkflowCoverage': ('check', 'state', 'passed', 'reason'),
 'OffWorkflowExecution': ('repair',
                          'writing',
                          'reload',
                          'source_protection',
                          'repair_correspondence',
                          'canonical_preservation',
                          'reload_verification',
                          'source_verification',
                          'staged_candidate',
                          'hashing',
                          'report_copy'),
 'OffWorkflowRequestedBytes': ('wrapper',
                               'source_files',
                               'source_off',
                               'repair',
                               'writing',
                               'reload',
                               'publication_files',
                               'publication_off'),
 'OffFileDiagnostic': ('status',
                       'stage',
                       'code',
                       'file_index',
                       'byte_offset',
                       'system_error',
                       'off_status',
                       'off_diagnostic',
                       'geometry_status',
                       'geometry_diagnostic'),
 'OffFileExecution': ('capture',
                      'copying',
                      'byte_verification',
                      'mesh_verification',
                      'expected_inspection',
                      'off'),
 'OffFilePublicationObservation': ('outcome',
                                   'phase',
                                   'diagnostic',
                                   'cleanup',
                                   'cleanup_error',
                                   'execution'),
 'OffDiagnostic': ('stage',
                   'code',
                   'byte_offset',
                   'vertex',
                   'face',
                   'geometry_status',
                   'geometry_diagnostic',
                   'storage_issue'),
 'OffCodecExecution': ('codec', 'materialization', 'inspection'),
 'OffRequestedBytes': ('codec', 'materialization', 'inspection', 'total'),
 'OffWriteFacts': ('profile',
                   'vertex_count',
                   'face_count',
                   'corner_count',
                   'text_policy',
                   'edge_count_policy',
                   'emitted_edge_count',
                   'correspondence',
                   'source_declared_edge_count'),
 'OffMaterializedDuplicateDiagnostic': ('stage',
                                        'code',
                                        'target',
                                        'attribute',
                                        'element',
                                        'geometry_status',
                                        'geometry_diagnostic',
                                        'storage_issue'),
 'OffMaterializedDuplicateExecution': ('source_inspection',
                                       'eligibility',
                                       'comparison',
                                       'selection',
                                       'correspondence',
                                       'candidate_inspection',
                                       'verification',
                                       'directed_verification'),
 'OffMaterializedDuplicateRequestedBytes': ('repair',
                                            'source_inspection',
                                            'selection',
                                            'candidate_inspection',
                                            'total'),
 'OffMaterializationDiagnostic': ('code', 'attribute', 'element', 'storage_issue'),
 'OffStorageIssue': ('code', 'attribute', 'element'),
 'OffMaterializationExecutionStats': ('planned_jobs',
                                      'launched_workers',
                                      'joined_workers',
                                      'caller_jobs'),
 'OffCapturedRequestedBytes': ('files', 'off'),
 'OffSystemErrorCode': ('category', 'value')}
_STAGES = {
    "kConfiguration": "import", "kSourceProtection": "inspect",
    "kRepair": "repair", "kWrite": "verify", "kReload": "verify",
    "kVerify": "verify", "kHash": "report", "kReport": "report",
    "kPublication": "export", "kComplete": "complete",
}
_EXECUTIONS = {"kCompleted": "completed", "kFailed": "failed", "kCanceled": "cancelled"}
_PUBLICATIONS = {
    "kNotRequested": "not-requested", "kNotAttempted": "not-attempted",
    "kFailed": "failed", "kCanceled": "cancelled", "kPublished": "published",
}


class OffCommandDeliveryError(RuntimeError):
    """No complete truthful command document can be delivered within its budget."""

    def __init__(self, exit_code=2):
        if type(exit_code) is not int or exit_code not in (2, 130):
            raise ValueError("Delivery failure exit must be 2 or 130.")
        super().__init__("A complete OFF command report could not be delivered.")
        self.exit_code = exit_code


@dataclass(frozen=True, slots=True)
class OffCommandResult:
    """Exact detached wire bytes and their validated exit; no native owner/cache."""

    report_json: bytes
    exit_code: int

    @property
    def report(self):
        from meshvale_reports import load
        return load(self.report_json)


@dataclass(slots=True)
class _Job:
    request: dict
    arguments: dict
    prepared: object = None


@dataclass(frozen=True, slots=True)
class _AdmissionLimits:
    path_units: int
    report_path_bytes: int
    input_id_bytes: int
    target_pairs: int

    @classmethod
    def from_session(cls, session):
        # Read the owning descriptor, not an instance-supplied projection.
        preset = _workflow.OffExecution.resolved_preset.__get__(session, _workflow.OffExecution)
        workflow = preset["workflow_limits"]
        return cls(preset["capture_limits"]["files"]["max_native_path_code_units"],
                   workflow["publication"]["files"]["max_receipt_name_bytes"],
                   workflow["max_input_id_bytes"], workflow["repair"]["max_targets"])


def _encoded(value, limit, failure):
    parts, count = [], 0
    for text in _JSON.iterencode(value):
        part = text.encode("utf-8")
        count += len(part)
        if count > limit:
            raise failure()
        parts.append(part)
    return b"".join(parts)


def _invocation_error():
    return InvocationError("Invalid OFF invocation; no assets processed.")


def _text_bytes(value, *, max_chars, max_bytes=None):
    if type(value) is not str or not value or len(value) > max_chars:
        raise _invocation_error()
    if "\0" in value:
        raise _invocation_error()
    size = len(value.encode("utf-8"))
    if max_bytes is not None and size > max_bytes:
        raise _invocation_error()
    return size


def _text(value, *, max_chars, max_bytes=None):
    _text_bytes(value, max_chars=max_chars, max_bytes=max_bytes)
    return value


def _choice(value, choices):
    _text(value, max_chars=max(map(len, choices)))
    if value not in choices:
        raise _invocation_error()
    return value


def _index(value):
    if type(value) is not int or not 0 <= value <= _MAX_INDEX:
        raise _invocation_error()
    return value


def _report_path(value, limit):
    """Admit a portable bundle member, not a URI or a host filesystem path."""
    _text(value, max_chars=limit, max_bytes=limit)
    if any(ord(char) < 0x20 or 0x7f <= ord(char) < 0xa0 or
           char in _PROHIBITED_MEMBER_CHARACTERS for char in value):
        raise _invocation_error()
    stem = value.split(".", 1)[0].translate(_ASCII_FOLD)
    if (value in (".", "..") or value[-1] in (".", " ") or
            stem in _WINDOWS_DEVICE_STEMS):
        raise _invocation_error()
    return value


def _identity(value, limit):
    _text(value, max_chars=limit, max_bytes=limit)
    if not value.isascii() or not value[0].isalnum() or any(
            not (char.isalnum() or char in "_.:-") for char in value):
        raise _invocation_error()
    return value


def _shape(request, limits):
    if (type(request) is not dict or len(request) != 2 or set(request) != {"schema", "jobs"} or
            type(request["schema"]) is not str or request["schema"] != _REQUEST_SCHEMA or
            type(request["jobs"]) is not list or len(request["jobs"]) > _MAX_JOBS):
        raise _invocation_error()
    rows, identities, targets = [], set(), 0
    for row in request["jobs"]:
        if (type(row) is not dict or not len(_REQUIRED) <= len(row) <= len(_REQUIRED) + len(_OPTIONAL) or
                not _REQUIRED <= set(row) or set(row) - _REQUIRED - _OPTIONAL):
            raise _invocation_error()
        if type(row["targets"]) is not list or len(row["targets"]) > limits.target_pairs:
            raise _invocation_error()
        targets += len(row["targets"])
        if targets > _MAX_TARGET_PAIRS:
            raise _invocation_error()
        identity = _identity(row["id"], limits.input_id_bytes)
        if identity in identities:
            raise _invocation_error()
        identities.add(identity)
        for name in ("input", "destination"):
            _text(row[name], max_chars=min(limits.path_units - 1, _MAX_PATH_BYTES),
                  max_bytes=_MAX_PATH_BYTES)
        _choice(row["text"], _TEXT_POLICIES)
        _choice(row["edge_count"], _EDGE_POLICIES)
        receipt = _report_path(row.get("report_path", _DEFAULT_REPORT_PATH), limits.report_path_bytes)
        # Check portable name collisions before native capture. The logical
        # path itself still reaches native preparation and the actual reader.
        basename = Path(row["input"]).name
        if receipt.translate(_ASCII_FOLD) == basename.translate(_ASCII_FOLD):
            raise _invocation_error()
        pairs = []
        for pair in row["targets"]:
            if type(pair) is not list or len(pair) != 2:
                raise _invocation_error()
            pairs.append([_index(pair[0]), _index(pair[1])])
        rows.append({**row, "targets": pairs})
    _encoded({"schema": _REQUEST_SCHEMA, "jobs": rows}, _MAX_REQUEST_BYTES, _invocation_error)
    return rows


def _path(value, base):
    path = Path(value)
    return path if path.is_absolute() else base / path


def _overlap(first, second):
    return first == second or first in second.parents or second in first.parents


def _preflight_reports():
    # Invocation-only imports preserve the front end's lazy help/version path.
    # Resolve both the owning reader and its declared validator before capture.
    from meshvale_reports import exit_code, load, schema_document, validate
    from jsonschema import Draft202012Validator
    if not all(map(callable, (exit_code, load, schema_document, validate))):
        raise ImportError("The report reader is unavailable.")
    Draft202012Validator.check_schema(schema_document())


def _admit(request, base, execution, cancellation):
    jobs = []
    try:
        if execution is not None and not isinstance(execution, _workflow.OffExecution):
            raise _invocation_error()
        if cancellation is not None and not isinstance(cancellation, _workflow.OffCancellation):
            raise _invocation_error()
        session = _workflow.OffExecution() if execution is None else execution
        token = _workflow.OffCancellation() if cancellation is None else cancellation
        limits = _AdmissionLimits.from_session(session)
        rows = _shape(request, limits)
        _preflight_reports()
        base_text = os.fspath(Path.cwd() if base is None else base)
        _text(base_text, max_chars=min(limits.path_units - 1, _MAX_PATH_BYTES), max_bytes=_MAX_PATH_BYTES)
        directory = Path(base_text)
        if not directory.is_absolute():
            directory = Path.cwd() / directory
        for row in rows:
            source = _path(row["input"], directory)
            destination = _path(row["destination"], directory)
            arguments = {"source": source, "destination": destination, "targets": row["targets"],
                         "input_id": row["id"], "text": row["text"], "edge_count": row["edge_count"],
                         "report_path": row.get("report_path", _DEFAULT_REPORT_PATH),
                         "execution": session, "cancellation": token}
            jobs.append(_Job(row, arguments))
        destinations = [job.arguments["destination"].resolve() for job in jobs]
        protected = [job.arguments["source"].resolve() for job in jobs]
        for i, destination in enumerate(destinations):
            if any(_overlap(destination, previous) for previous in destinations[:i]):
                raise _invocation_error()
            if any(_overlap(destination, source) for source in protected):
                raise _invocation_error()
        if len(jobs) * _MAX_REPORT_BYTES > _MAX_REPORT_ALLOWANCE:
            raise _invocation_error()
        for job in jobs:
            job.prepared = _workflow._prepare_off_request(**job.arguments)
        return jobs, token
    except (TypeError, ValueError, OverflowError, OSError, RuntimeError) as error:
        for job in jobs:
            job.prepared = None
        if isinstance(error, InvocationError):
            raise
        raise _invocation_error() from None


def _member(value, domain):
    if type(value).__name__ != domain or domain not in _ENUM_DOMAINS:
        raise ValueError("Unexpected native enum domain.")
    name = value.name
    if type(name) is not str or getattr(type(value), name, None) != value:
        raise ValueError("Unnamed native enum observation.")
    return name


def _observe(value):
    if value is None or type(value) in (bool, int, str):
        return value
    name = type(value).__name__
    if name in _ENUM_DOMAINS:
        return {"domain": name, "member": _member(value, name)}
    fields = _OBSERVATION_FIELDS.get(name)
    if fields is None:
        raise ValueError("Unsupported native observation type.")
    return {"type": name, **{field: _observe(getattr(value, field)) for field in fields}}


def _canceled(result):
    if result.workflow_started:
        return _member(result.outcomes.execution, "OffWorkflowExecutionOutcome") == "kCanceled"
    return _member(result.capture_status, "OffFileStatus") == "kCanceled"


def _failure_exit(result):
    return 130 if _canceled(result) else 2


def _core_outcomes(result):
    if not result.workflow_started:
        return ("cancelled" if _canceled(result) else "failed"), "import", "not-attempted", "not-attempted"
    outcomes = result.outcomes
    execution = _EXECUTIONS[_member(outcomes.execution, "OffWorkflowExecutionOutcome")]
    candidate = "not-attempted" if outcomes.candidate is None else _member(outcomes.candidate, "CandidateOutcome")
    publication = _PUBLICATIONS[_member(outcomes.publication, "OffWorkflowPublicationOutcome")]
    stage = _STAGES[_member(outcomes.stage, "OffWorkflowStage")]
    if execution == "completed":
        stage = "complete"
    elif stage == "complete":
        raise ValueError("Failed native execution has no failure stage.")
    return execution, stage, candidate, publication


def _project_off_fallback(result, job):
    """Detach actual no-receipt observations, including a real test-module result."""
    try:
        execution, stage, candidate, publication = _core_outcomes(result)
        if publication == "published" or result.publication is not None and _member(
                result.publication.outcome, "OffFilePublicationOutcome") == "kPublished":
            raise OffCommandDeliveryError(_failure_exit(result))
        options = {"text": job["text"], "edge_count": job["edge_count"],
                   "targets": [{"keep": pair[0], "remove": pair[1]} for pair in job["targets"]]}
        scope = {"snapshot": None, "mesh": None, "subject": "command receipt validation"}
        report = {"schema": _REPORT_SCHEMA, "tool": {"name": "meshvale-repair", "version": __version__},
                  "input": {"id": job["id"], "sha256": result.input_sha256},
                  "operation": {"kind": "repair", "name": "remove-duplicate-faces", "version": "1", "options": options, "targets": []},
                  "execution": {"outcome": execution, "stage": stage,
                                "reason": None if execution == "completed" else "Native execution did not complete; exact reached observations are in the command extension."},
                  "candidate": {"outcome": candidate, "snapshot": None},
                  "profile": {"name": _PROFILE, "version": "1", "required_checks": [_RECEIPT_CHECK], "outcome": "incomplete"},
                  "snapshots": [], "maps": [], "preservation": [],
                  "diagnostics": [{"id": "native-receipt-absent", "code": "off.command.native-receipt-unavailable", "severity": "warning",
                                   "message": _ABSENT_RECEIPT, "scope": scope, "element": None,
                                   "data": {"origin": "command", "workflow_started": result.workflow_started}}],
                  "coverage": [{"id": _RECEIPT_CHECK, "check": _RECEIPT_CHECK, "version": "1", "scope": scope,
                                "status": "skipped", "finding": None, "reason": _ABSENT_RECEIPT, "diagnostics": ["native-receipt-absent"]}],
                  "publication": {"requested": True, "outcome": publication, "snapshot": None, "artifacts": [], "verified": False,
                                  "reason": "No verified publication was completed; exact reached observations are in the command extension."},
                  "extensions": {}}
        if candidate in ("accepted", "unchanged"):
            before, after = result.input_snapshot_counts, result.candidate_snapshot_counts
            if (type(before) is not tuple or type(after) is not tuple or len(before) != 3 or len(after) != 3 or
                    any(type(value) is not int or not 0 <= value <= _MAX_INDEX for value in before + after)):
                raise OffCommandDeliveryError(_failure_exit(result))
            for role, counts in (("input", before), ("candidate", after)):
                report["snapshots"].append({"id": role, "role": role, "meshes": [{"id": "surface",
                    "vertex_count": counts[0], "face_count": counts[1], "corner_count": counts[2]}]})
            report["candidate"]["snapshot"] = "candidate"
            report["operation"]["targets"] = [{"snapshot": "input", "mesh": "surface", "domain": "face", "index": pair[1]} for pair in job["targets"]]
            for domain, entries in (("vertex", None), ("face", result.face_map), ("corner", result.corner_map)):
                if domain != "vertex" and type(entries) is not tuple:
                    raise OffCommandDeliveryError(_failure_exit(result))
                report["maps"].append({"id": "repair-" + ("vertices" if domain == "vertex" else "faces" if domain == "face" else "corners"), "domain": domain,
                    "from": {"snapshot": "input", "mesh": "surface"}, "to": {"snapshot": "candidate", "mesh": "surface"},
                    "kind": "identity" if domain == "vertex" or candidate == "unchanged" else "explicit",
                    "entries": None if domain == "vertex" or candidate == "unchanged" else list(entries)})
        report["extensions"][_EXTENSION] = {
            "version": "1", "origin": "command-workflow-fallback" if result.workflow_started else "command-capture-fallback",
            "native_report_available": False, "workflow_started": result.workflow_started,
            "capture": {"status": _observe(result.capture_status), "diagnostic": _observe(result.capture_diagnostic),
                        "execution": _observe(result.capture_execution), "requested_bytes": _observe(result.capture_requested_bytes)},
            "workflow": None if not result.workflow_started else {"status": _observe(result.status), "outcomes": _observe(result.outcomes),
                "diagnostic": _observe(result.diagnostic), "report_diagnostic": _observe(result.report_diagnostic),
                "execution": _observe(result.execution), "coverage": None if result.coverage is None else [_observe(item) for item in result.coverage]},
            "publication": _observe(result.publication), "requested_bytes": _observe(result.requested_bytes),
            "source_profile": _observe(result.source_profile), "write_facts": _observe(result.write_facts),
            "source_faces": None if result.source_faces is None else list(result.source_faces),
            "source_corners": None if result.source_corners is None else list(result.source_corners)}
        return report
    finally:
        # A handled delivery exception must not pin a native owner in traceback
        # frames after the scheduler clears its prepared/result slots.
        result = None


def _native_receipt(result):
    """Read the genuine receipt; a narrow byte-delivery fault seam for tests."""
    try:
        return result.report_json
    finally:
        result = None


def _detach(result, job):
    from meshvale_reports import ReportError, exit_code, load
    try:
        failure = _failure_exit(result)
        try:
            payload = _native_receipt(result)
            if payload is None:
                payload = _encoded(_project_off_fallback(result, job), _MAX_REPORT_BYTES,
                                   lambda: OffCommandDeliveryError(failure))
            if type(payload) is not bytes or not payload or len(payload) > _MAX_REPORT_BYTES:
                raise OffCommandDeliveryError(failure)
            parsed = load(payload)
            if parsed["schema"] != _REPORT_SCHEMA or parsed["input"] is None or parsed["input"]["id"] != job["id"]:
                raise OffCommandDeliveryError(failure)
            identity, code = parsed["input"], exit_code(parsed)
            return payload, identity, code
        except (MemoryError, ReportError, UnicodeError, ValueError, TypeError, KeyError, OverflowError) as error:
            # A failing byte getter may have borrowed the native owner. Its
            # traceback must not retain that owner after this typed failure.
            error.__traceback__ = None
            raise OffCommandDeliveryError(failure) from None
    finally:
        result = None


def _attempt(job):
    prepared, job.prepared = job.prepared, None
    result = None
    try:
        result = _workflow._run_off_request(prepared)
        return _detach(result, job.request)
    finally:
        result = prepared = None


def _precedence(codes):
    return next((value for value in (130, 2, 1) if value in codes), 0)


def _frame_batch(rows, mode, canceled, codes):
    from meshvale_reports import load
    parts, framing = [], 0
    failure = 130 if canceled or 130 in codes else 2

    def add(value):
        nonlocal framing
        framing += len(value)
        if framing > _MAX_FRAMING_BYTES:
            raise OffCommandDeliveryError(failure)
        parts.append(value)

    def field(value):
        add(_encoded(value, _MAX_FRAMING_BYTES, lambda: OffCommandDeliveryError(failure)))

    execution = {"outcome": "cancelled" if canceled else "completed", "stage": "repair" if canceled else "complete",
                 "reason": "Batch cancellation observed at the scheduling boundary." if canceled else None}
    code = _precedence(codes + ([130] if canceled else []))
    try:
        add(b'{"schema":"meshvale.batch/1","tool":')
        field({"name": "meshvale-repair", "version": __version__})
        add(b',"mode":'); field(mode)
        add(b',"execution":'); field(execution)
        add(b',"jobs":[')
        for index, row in enumerate(rows):
            if index:
                add(b',')
            add(b'{"id":'); field(row["id"])
            add(b',"input":'); field(row["input"])
            add(b',"outcome":'); field(row["outcome"])
            add(b',"report":')
            if row["report"] is None:
                add(b'null')
            else:
                # This slice is the actual validated native/fallback document.
                # It is the only part excluded from outer framing attribution.
                parts.append(row["report"])
            add(b',"reason":'); field(row["reason"])
            add(b'}')
        add(b'],"exit_code":'); field(code)
        add(b',"extensions":'); field({_EXTENSION: {"version": "1", "origin": "command-batch-framing"}})
        add(b'}')
        payload = b"".join(parts)
        # Full schema/semantic validation is temporary; the result stores only
        # bytes. This may expand JSON and is not a Python heap/RSS promise.
        checked = load(payload)
        if checked["schema"] != _BATCH_SCHEMA or checked["exit_code"] != code:
            raise OffCommandDeliveryError(failure)
        return OffCommandResult(payload, code)
    except MemoryError:
        raise OffCommandDeliveryError(failure) from None


def run_off_jobs(request, *, base=None, fail_fast=False, execution=None, cancellation=None):
    """Admit the complete invocation, then run jobs once in order and detach v1."""
    if type(fail_fast) is not bool:
        raise _invocation_error()
    jobs, token = _admit(request, base, execution, cancellation)
    rows, codes, stopped = [], [], None
    try:
        for job in jobs:
            if token.cancelled:
                stopped = "Batch cancellation before scheduling."
            if stopped is not None:
                job.prepared = None
                rows.append({"id": job.request["id"], "input": {"id": job.request["id"], "sha256": None},
                             "outcome": "not-attempted", "report": None, "reason": stopped})
                continue
            try:
                payload, identity, code = _attempt(job)
            except OffCommandDeliveryError as error:
                if 130 in codes or token.cancelled:
                    raise OffCommandDeliveryError(130) from None
                raise error
            rows.append({"id": job.request["id"], "input": identity, "outcome": "attempted", "report": payload, "reason": None})
            codes.append(code)
            if fail_fast and code:
                stopped = "Fail-fast after job " + job.request["id"] + "."
        # The single observation fixes completion; later interrupts cannot
        # rewrite native receipts or this batch's outcome.
        canceled = token.cancelled
        return _frame_batch(rows, "fail-fast" if fail_fast else "continue", canceled, codes)
    finally:
        for job in jobs:
            job.prepared = None


def _run_off_single(job, *, base=None, execution=None, cancellation=None):
    jobs, token = _admit({"schema": _REQUEST_SCHEMA, "jobs": [job]}, base, execution, cancellation)
    try:
        payload, _, code = _attempt(jobs[0])
        return OffCommandResult(payload, code)
    except OffCommandDeliveryError:
        if token.cancelled:
            raise OffCommandDeliveryError(130) from None
        raise
    finally:
        jobs[0].prepared = None


def _read_off_request(path):
    """Read bounded raw UTF-8 JSON; semantic admission still occurs as a whole."""
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise _invocation_error()
            value[key] = item
        return value

    def constant(_):
        raise _invocation_error()

    try:
        with Path(path).open("rb") as stream:
            raw = stream.read(_MAX_REQUEST_BYTES + 1)
        if len(raw) > _MAX_REQUEST_BYTES:
            raise _invocation_error()
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs, parse_constant=constant)
    except (OSError, UnicodeError, ValueError, TypeError, RecursionError):
        raise _invocation_error() from None


__all__ = ["OffCommandResult", "OffCommandDeliveryError", "run_off_jobs"]
