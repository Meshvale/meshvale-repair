# SPDX-License-Identifier: Apache-2.0
"""Bounded PLY command admission and truthful detached report delivery."""
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path

from . import __version__
from . import ply_workflow as _workflow
from .commands import InvocationError

_REQUEST_SCHEMA = "meshvale.repair.ply-jobs/1"
_REPORT_SCHEMA = "meshvale.report/1"
_BATCH_SCHEMA = "meshvale.batch/1"
_EXTENSION = "meshvale.repair.ply.command"
_PROFILE = "ply-command-receipt"
_MAX_JOBS = 16
_MAX_REQUEST_BYTES = 8 * 1024 * 1024
_MAX_TARGET_PAIRS = 1_000_000
_MAX_REPORT_BYTES = 16 * 1024 * 1024
_MAX_REPORT_ALLOWANCE = _MAX_JOBS * _MAX_REPORT_BYTES
_MAX_FRAMING_BYTES = 8 * 1024 * 1024
_MAX_INDEX = (1 << 64) - 1
_MAX_PATH_BYTES = 4096
_DEFAULT_REPORT_PATH = "meshvale-report.json"
_RECEIPT_CHECK = "native-receipt-validation"
_ABSENT_RECEIPT = "Native receipt validation was blocked because no native receipt bytes are available."
_PROHIBITED_MEMBER_CHARACTERS = frozenset(':<>"|?*/\\')
_ASCII_FOLD = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
_WINDOWS_DEVICE_STEMS = frozenset(("con", "prn", "aux", "nul")) | frozenset(
    prefix + str(number) for prefix in ("com", "lpt") for number in range(1, 10))
_JSON = json.JSONEncoder(ensure_ascii=False, separators=(",", ":"), allow_nan=False)
_REQUIRED = frozenset(("id", "input", "destination", "targets", "encoding"))
_OPTIONAL = frozenset(("report_path", "row_local_attributes"))
_ENCODINGS = ("ascii", "binary_little_endian", "binary_big_endian")
_DOMAINS = ("vertex", "face", "corner")
_STAGES = {
    "kConfiguration": "import", "kSourceProtection": "inspect",
    "kRepair": "repair", "kWrite": "verify", "kReload": "verify",
    "kVerify": "verify", "kHash": "report", "kReport": "report",
    "kPublication": "export", "kComplete": "complete",
}
_EXECUTIONS = {"kCompleted": "completed", "kFailed": "failed", "kCanceled": "cancelled"}
_PROFILES = {"kPassed": "passed", "kFailed": "failed", "kIncomplete": "incomplete"}
_PUBLICATIONS = {
    "kNotRequested": "not-requested", "kNotAttempted": "not-attempted",
    "kFailed": "failed", "kCanceled": "cancelled", "kPublished": "published",
}

class PlyCommandDeliveryError(RuntimeError):
    """No complete truthful command document can be delivered within its budget."""

    def __init__(self, exit_code=2):
        if type(exit_code) is not int or exit_code not in (2, 130):
            raise ValueError("Delivery failure exit must be 2 or 130.")
        super().__init__("A complete PLY command report could not be delivered.")
        self.exit_code = exit_code


@dataclass(frozen=True, slots=True)
class PlyCommandResult:
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
    declaration_count: int
    declaration_bytes: int

    @classmethod
    def from_session(cls, session):
        preset = _workflow.PlyExecution.resolved_preset.__get__(session, _workflow.PlyExecution)
        workflow = preset["workflow_limits"]
        repair = workflow["repair"]
        return cls(preset["capture_limits"]["files"]["max_native_path_code_units"],
                   workflow["publication"]["files"]["max_receipt_name_bytes"],
                   workflow["max_input_id_bytes"], repair["max_targets"],
                   repair["max_row_local_declarations"], repair["max_declaration_name_bytes"])


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
    return InvocationError("Invalid PLY invocation; no assets processed.")


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


def _declarations(values, limits):
    if type(values) is not list or len(values) > limits.declaration_count:
        raise _invocation_error()
    declarations, seen, name_bytes = [], set(), 0
    for value in values:
        if type(value) is not dict or len(value) != 2 or set(value) != {"domain", "name"}:
            raise _invocation_error()
        domain = _choice(value["domain"], _DOMAINS)
        name = _text(value["name"], max_chars=limits.declaration_bytes - name_bytes,
                     max_bytes=limits.declaration_bytes - name_bytes)
        if (not name.isascii() or not (name[0].isalpha() or name[0] == "_") or
                any(not (char.isalnum() or char == "_") for char in name)):
            raise _invocation_error()
        pair = domain, name
        if pair in seen:
            raise _invocation_error()
        seen.add(pair)
        name_bytes += len(name)
        declarations.append({"domain": domain, "name": name})
    return declarations


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
        encoding = _choice(row["encoding"], _ENCODINGS)
        declarations = _declarations(row.get("row_local_attributes", []), limits)
        receipt = _report_path(row.get("report_path", _DEFAULT_REPORT_PATH), limits.report_path_bytes)
        if receipt.translate(_ASCII_FOLD) == os.path.basename(row["input"]).translate(_ASCII_FOLD):
            raise _invocation_error()
        pairs = []
        for pair in row["targets"]:
            if type(pair) is not list or len(pair) != 2:
                raise _invocation_error()
            pairs.append([_index(pair[0]), _index(pair[1])])
        rows.append({**row, "targets": pairs, "encoding": encoding,
                     "row_local_attributes": declarations})
    _encoded({"schema": _REQUEST_SCHEMA, "jobs": rows}, _MAX_REQUEST_BYTES, _invocation_error)
    return rows


def _path(value, base):
    # Keep native IO spelling, including partial Windows anchors and trailing
    # separators/dots. Path objects normalize some of these into accepted paths.
    return value if Path(value).anchor else os.path.join(base, value)


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
        if execution is not None and not isinstance(execution, _workflow.PlyExecution):
            raise _invocation_error()
        if cancellation is not None and not isinstance(cancellation, _workflow.PlyCancellation):
            raise _invocation_error()
        session = _workflow.PlyExecution() if execution is None else execution
        token = _workflow.PlyCancellation() if cancellation is None else cancellation
        limits = _AdmissionLimits.from_session(session)
        rows = _shape(request, limits)
        _preflight_reports()
        base_text = os.fspath(Path.cwd() if base is None else base)
        _text(base_text, max_chars=min(limits.path_units - 1, _MAX_PATH_BYTES), max_bytes=_MAX_PATH_BYTES)
        directory = base_text
        if not Path(directory).anchor:
            directory = os.path.join(os.getcwd(), directory)
        for row in rows:
            source = _path(row["input"], directory)
            destination = _path(row["destination"], directory)
            arguments = {"source": source, "destination": destination, "targets": row["targets"],
                         "input_id": row["id"], "encoding": row["encoding"],
                         "row_local_attributes": row["row_local_attributes"],
                         "report_path": row.get("report_path", _DEFAULT_REPORT_PATH),
                         "execution": session, "cancellation": token}
            jobs.append(_Job(row, arguments))
        destinations = [Path(job.arguments["destination"]).resolve() for job in jobs]
        protected = [Path(job.arguments["source"]).resolve() for job in jobs]
        for i, destination in enumerate(destinations):
            if any(_overlap(destination, previous) for previous in destinations[:i]):
                raise _invocation_error()
            if any(_overlap(destination, source) for source in protected):
                raise _invocation_error()
        if len(jobs) * _MAX_REPORT_BYTES > _MAX_REPORT_ALLOWANCE:
            raise _invocation_error()
        for job in jobs:
            job.prepared = _workflow._prepare_ply_request(**job.arguments)
        return jobs, token
    except BaseException as error:
        for job in jobs:
            job.prepared = None
            job.arguments.clear()
        if isinstance(error, InvocationError):
            raise
        if isinstance(error, (TypeError, ValueError, OverflowError, OSError)):
            raise _invocation_error() from None
        # Internal/native exceptions keep their actual type after owned
        # prepared slots have been released; they are not caller shape errors.
        raise


def _member(value, domain):
    if type(value).__name__ != domain:
        raise ValueError("Unexpected native enum domain.")
    name = value.name
    if type(name) is not str or getattr(type(value), name, None) != value:
        raise ValueError("Unnamed native enum observation.")
    return name


def _uint(value):
    if type(value) is not int or not 0 <= value <= _MAX_INDEX:
        raise ValueError("Invalid unsigned native observation.")
    return value


def _ordinal(value, domain):
    if value is None:
        return None
    _member(value, domain)
    return _uint(value.value)


def _required_ordinal(value, domain):
    if value is None:
        raise ValueError("Required native enum observation is unavailable.")
    return _ordinal(value, domain)


def _optional_uint(value):
    return None if value is None else _uint(value)


def _object(value, name, fields):
    if value is None:
        return None
    if type(value).__name__ != name:
        raise ValueError("Unexpected native diagnostic type.")
    return {key: convert(getattr(value, key)) for key, convert in fields}


def _storage_issue(value):
    return _object(value, "PlyStorageIssue", (
        ("code", lambda item: _required_ordinal(item, "StorageIssueCode")),
        ("attribute", _optional_uint), ("element", _optional_uint)))


def _geometry_diagnostic(value):
    return _object(value, "PlyMaterializationDiagnostic", (
        ("code", lambda item: _required_ordinal(item, "MaterializationDiagnosticCode")),
        ("attribute", _optional_uint), ("element", _optional_uint),
        ("storage_issue", _storage_issue)))


def _ply_diagnostic(value):
    return _object(value, "PlyDiagnostic", (
        ("stage", lambda item: _required_ordinal(item, "PlyStage")),
        ("code", lambda item: _required_ordinal(item, "PlyDiagnosticCode")),
        ("byte_offset", _optional_uint), ("element", _optional_uint),
        ("property", _optional_uint), ("row", _optional_uint),
        ("item", _optional_uint), ("attribute", _optional_uint),
        ("geometry_status", lambda item: _ordinal(item, "MaterializationStatus")),
        ("geometry_diagnostic", _geometry_diagnostic), ("storage_issue", _storage_issue)))


def _system_error(value):
    if value is None:
        return None
    if (type(value).__name__ != "PlySystemErrorCode" or type(value.category) is not str or
            type(value.value) is not int):
        raise ValueError("Unexpected native system error.")
    return {"category": value.category, "value": value.value}


def _file_diagnostic(value):
    return _object(value, "PlyFileDiagnostic", (
        ("status", lambda item: _required_ordinal(item, "PlyFileStatus")),
        ("stage", lambda item: _required_ordinal(item, "PlyFileStage")),
        ("code", lambda item: _required_ordinal(item, "PlyFileDiagnosticCode")),
        ("file_index", _optional_uint), ("byte_offset", _optional_uint),
        ("system_error", _system_error),
        ("ply_status", lambda item: _ordinal(item, "PlyStatus")),
        ("ply_diagnostic", _ply_diagnostic),
        ("geometry_status", lambda item: _ordinal(item, "MaterializationStatus")),
        ("geometry_diagnostic", _geometry_diagnostic)))


def _published(result):
    try:
        if result.workflow_started and _member(
                result.outcomes.publication, "PlyWorkflowPublicationOutcome") == "kPublished":
            return True
        publication = result.publication
        return publication is not None and _member(
            publication.outcome, "PlyFilePublicationOutcome") == "kPublished"
    finally:
        result = None


def _canceled(result):
    try:
        if result.workflow_started:
            return _member(result.outcomes.execution, "PlyWorkflowExecutionOutcome") == "kCanceled"
        return _member(result.capture_status, "PlyFileStatus") == "kCanceled"
    finally:
        result = None


def _failure_exit(result):
    try:
        return 130 if _canceled(result) else 2
    finally:
        result = None


def _core_outcomes(result):
    try:
        if not result.workflow_started:
            return ("cancelled" if _canceled(result) else "failed"), "import", "not-attempted", "not-attempted"
        outcomes = result.outcomes
        execution = _EXECUTIONS[_member(outcomes.execution, "PlyWorkflowExecutionOutcome")]
        candidate = "not-attempted" if outcomes.candidate is None else _member(outcomes.candidate, "CandidateOutcome")
        publication = _PUBLICATIONS[_member(outcomes.publication, "PlyWorkflowPublicationOutcome")]
        stage = _STAGES[_member(outcomes.stage, "PlyWorkflowStage")]
        if execution == "completed":
            stage = "complete"
        elif stage == "complete":
            raise ValueError("Failed native execution has no failure stage.")
        return execution, stage, candidate, publication
    finally:
        result = None


def _counts(value):
    if value is None:
        return None
    if type(value) is not tuple or len(value) != 3:
        raise ValueError("Missing actual native counts.")
    return dict(zip(("vertices", "faces", "corners"), map(_uint, value)))


def _indices(value):
    if value is None:
        return None
    if type(value) is not tuple:
        raise ValueError("Missing actual native correspondence.")
    return [_uint(item) for item in value]


def _command_observations(result):
    try:
        started = result.workflow_started
        if type(started) is not bool or type(result.memory_output_verified) is not bool:
            raise ValueError("Invalid native availability observation.")
        execution, _, candidate, publication = _core_outcomes(result)
        native_exit = result.exit_code
        if native_exit is not None and (type(native_exit) is not int or native_exit not in (0, 1, 2, 130)):
            raise ValueError("Invalid native exit observation.")
        return {
            "capture_status": _ordinal(result.capture_status, "PlyFileStatus"),
            "capture_diagnostic": _file_diagnostic(result.capture_diagnostic),
            "workflow_status": _required_ordinal(result.status, "PlyWorkflowStatus") if started else None,
            "workflow_stage": _required_ordinal(result.outcomes.stage, "PlyWorkflowStage") if started else None,
            "execution_outcome": execution if started else None,
            "candidate_outcome": candidate if started else None,
            "profile_outcome": _PROFILES[_member(result.outcomes.profile, "PlyWorkflowProfileOutcome")] if started else None,
            "publication_outcome": publication if started else None,
            "exit_code": native_exit,
            "source_counts": _counts(result.input_snapshot_counts),
            "candidate_counts": _counts(result.candidate_snapshot_counts),
            "face_map": _indices(result.face_map), "corner_map": _indices(result.corner_map),
            "source_faces": _indices(result.source_faces), "source_corners": _indices(result.source_corners),
            "memory_output_verified": result.memory_output_verified, "published": False,
        }
    finally:
        result = None


def _completed_maps(report, observations, job, candidate):
    before, after = observations["source_counts"], observations["candidate_counts"]
    names = ("face_map", "corner_map", "source_faces", "source_corners")
    if before is None or after is None or any(observations[name] is None for name in names):
        raise ValueError("Completed repair requires genuine counts and all four arrays.")
    if before["vertices"] != after["vertices"]:
        raise ValueError("Native vertex identity is unavailable.")
    sizes = (before["faces"], before["corners"], after["faces"], after["corners"])
    bounds = (after["faces"], after["corners"], before["faces"], before["corners"])
    for name, size, bound in zip(names, sizes, bounds):
        values = observations[name]
        if len(values) != size or any(value >= bound for value in values):
            raise ValueError("Incomplete actual native correspondence.")
    for forward, inverse in (("face_map", "source_faces"), ("corner_map", "source_corners")):
        if any(observations[forward][old] != new for new, old in enumerate(observations[inverse])):
            raise ValueError("Inconsistent actual inverse correspondence.")
    if candidate == "unchanged" and (before != after or any(
            any(value != index for index, value in enumerate(observations[name])) for name in names)):
        raise ValueError("Actual unchanged correspondence is not identity.")
    for role, counts in (("input", before), ("candidate", after)):
        report["snapshots"].append({"id": role, "role": role, "meshes": [{"id": "surface",
            "vertex_count": counts["vertices"], "face_count": counts["faces"], "corner_count": counts["corners"]}]})
    report["candidate"]["snapshot"] = "candidate"
    report["operation"]["targets"] = [{"snapshot": "input", "mesh": "surface", "domain": "face", "index": pair[1]} for pair in job["targets"]]
    for domain, name in (("vertex", None), ("face", "face_map"), ("corner", "corner_map")):
        identity = domain == "vertex" or candidate == "unchanged"
        report["maps"].append({"id": "repair-" + ("vertices" if domain == "vertex" else "faces" if domain == "face" else "corners"),
            "domain": domain, "from": {"snapshot": "input", "mesh": "surface"},
            "to": {"snapshot": "candidate", "mesh": "surface"},
            "kind": "identity" if identity else "explicit", "entries": None if identity else observations[name]})


def _project_ply_fallback(result, job):
    """Only unpublished absence may project the selected seventeen-field wire."""
    try:
        if _published(result):
            raise PlyCommandDeliveryError(_failure_exit(result))
        execution, stage, candidate, publication = _core_outcomes(result)
        observations = _command_observations(result)
        scope = {"snapshot": None, "mesh": None, "subject": "command receipt validation"}
        options = {"encoding": job["encoding"], "rewrite": "canonical",
                   "targets": [{"keep": pair[0], "remove": pair[1]} for pair in job["targets"]],
                   "row_local_attributes": job["row_local_attributes"]}
        report = {"schema": _REPORT_SCHEMA, "tool": {"name": "meshvale-repair", "version": __version__},
            "input": {"id": job["id"], "sha256": result.input_sha256},
            "operation": {"kind": "repair", "name": "remove-duplicate-faces", "version": "1", "options": options, "targets": []},
            "execution": {"outcome": execution, "stage": stage,
                "reason": None if execution == "completed" else "Native execution did not complete; actual observations are in the command extension."},
            "candidate": {"outcome": candidate, "snapshot": None},
            "profile": {"name": _PROFILE, "version": "1", "required_checks": [_RECEIPT_CHECK], "outcome": "incomplete"},
            "snapshots": [], "maps": [], "preservation": [],
            "diagnostics": [{"id": "native-receipt-absent", "code": "ply.command.native-receipt-unavailable", "severity": "warning",
                "message": _ABSENT_RECEIPT, "scope": scope, "element": None, "data": {"origin": "command"}}],
            "coverage": [{"id": _RECEIPT_CHECK, "check": _RECEIPT_CHECK, "version": "1", "scope": scope,
                "status": "skipped", "finding": None, "reason": _ABSENT_RECEIPT, "diagnostics": ["native-receipt-absent"]}],
            "publication": {"requested": True, "outcome": publication, "snapshot": None, "artifacts": [], "verified": False,
                "reason": "No verified publication completed; actual observations are in the command extension."},
            "extensions": {_EXTENSION: {"version": 1, "observations": observations}}}
        if candidate in ("accepted", "unchanged"):
            _completed_maps(report, observations, job, candidate)
        return report
    finally:
        result = None


def _native_receipt(result):
    """Read the genuine receipt; a narrow byte-delivery fault seam for tests."""
    try:
        return result.report_json
    finally:
        result = None


def _detach(result, job):
    failure = 2
    try:
        from meshvale_reports import ReportError, exit_code, load
        try:
            failure = _failure_exit(result)
            published = _published(result)
            payload = _native_receipt(result)
            if payload is None:
                if published:
                    raise PlyCommandDeliveryError(failure)
                payload = _encoded(_project_ply_fallback(result, job), _MAX_REPORT_BYTES,
                                   lambda: PlyCommandDeliveryError(failure))
            if type(payload) is not bytes or not payload or len(payload) > _MAX_REPORT_BYTES:
                raise PlyCommandDeliveryError(failure)
            parsed = load(payload)
            if (parsed["schema"] != _REPORT_SCHEMA or parsed["input"] is None or
                    parsed["input"]["id"] != job["id"] or
                    published and parsed["publication"]["outcome"] != "published"):
                raise PlyCommandDeliveryError(failure)
            return payload, parsed["input"], exit_code(parsed)
        except (MemoryError, ReportError, UnicodeError, ValueError, TypeError, KeyError,
                AttributeError, OverflowError, PlyCommandDeliveryError) as error:
            error.__traceback__ = None
            raise PlyCommandDeliveryError(failure) from None
    finally:
        result = None


def _attempt(job):
    prepared, job.prepared = job.prepared, None
    result = None
    try:
        result = _workflow._run_ply_request(prepared)
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
            raise PlyCommandDeliveryError(failure)
        parts.append(value)

    def field(value):
        add(_encoded(value, _MAX_FRAMING_BYTES, lambda: PlyCommandDeliveryError(failure)))

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
        add(b',"extensions":'); field({})
        add(b'}')
        payload = b"".join(parts)
        # Full schema/semantic validation is temporary; the result stores only
        # bytes. This may expand JSON and is not a Python heap/RSS promise.
        checked = load(payload)
        if checked["schema"] != _BATCH_SCHEMA or checked["exit_code"] != code:
            raise PlyCommandDeliveryError(failure)
        return PlyCommandResult(payload, code)
    except MemoryError:
        raise PlyCommandDeliveryError(failure) from None


def _cancelled(token):
    return _workflow.PlyCancellation.cancelled.__get__(token, _workflow.PlyCancellation)


def run_ply_jobs(request, *, base=None, fail_fast=False, execution=None, cancellation=None):
    """Admit the complete invocation, then run jobs once in order and detach v1."""
    if type(fail_fast) is not bool:
        raise _invocation_error()
    jobs, token = _admit(request, base, execution, cancellation)
    rows, codes, stopped = [], [], None
    try:
        for job in jobs:
            if _cancelled(token):
                stopped = "Batch cancellation before scheduling."
            if stopped is not None:
                job.prepared = None
                rows.append({"id": job.request["id"], "input": {"id": job.request["id"], "sha256": None},
                             "outcome": "not-attempted", "report": None, "reason": stopped})
                continue
            try:
                payload, identity, code = _attempt(job)
            except PlyCommandDeliveryError as error:
                if 130 in codes or _cancelled(token):
                    raise PlyCommandDeliveryError(130) from None
                raise error
            rows.append({"id": job.request["id"], "input": identity, "outcome": "attempted", "report": payload, "reason": None})
            codes.append(code)
            if fail_fast and code:
                stopped = "Fail-fast after job " + job.request["id"] + "."
        # The single observation fixes completion; later interrupts cannot
        # rewrite native receipts or this batch's outcome.
        canceled = _cancelled(token)
        return _frame_batch(rows, "fail-fast" if fail_fast else "continue", canceled, codes)
    finally:
        for job in jobs:
            job.prepared = None


def _run_ply_single(job, *, base=None, execution=None, cancellation=None):
    jobs, token = _admit({"schema": _REQUEST_SCHEMA, "jobs": [job]}, base, execution, cancellation)
    try:
        payload, _, code = _attempt(jobs[0])
        return PlyCommandResult(payload, code)
    except PlyCommandDeliveryError:
        if _cancelled(token):
            raise PlyCommandDeliveryError(130) from None
        raise
    finally:
        jobs[0].prepared = None


def _read_ply_request(path):
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

    def finite(text):
        value = float(text)
        if not math.isfinite(value):
            raise _invocation_error()
        return value

    try:
        with open(path, "rb") as stream:
            raw = stream.read(_MAX_REQUEST_BYTES + 1)
        if len(raw) > _MAX_REQUEST_BYTES:
            raise _invocation_error()
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs, parse_constant=constant, parse_float=finite)
    except (OSError, UnicodeError, ValueError, TypeError, RecursionError):
        raise _invocation_error() from None


__all__ = ["PlyCommandResult", "PlyCommandDeliveryError", "run_ply_jobs"]
