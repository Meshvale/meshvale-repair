# SPDX-License-Identifier: Apache-2.0
"""Bounded admission and detached glTF receipts over the prepared file operation."""
from dataclasses import dataclass
import json
import os
from pathlib import Path

from . import __version__
from . import gltf_workflow as _workflow
from .commands import InvocationError


_REQUEST_SCHEMA = "meshvale.repair.gltf-jobs/1"
_REQUEST_SCHEMA_V2 = "meshvale.repair.gltf-jobs/2"
_STATIC_PROFILE = "static-local-triangles-v1"
_TRANSLATION_PROFILE = "direct-translation-v1"
_PROCESSING_PROFILES = (_STATIC_PROFILE, _TRANSLATION_PROFILE)
_TRANSLATION_PRESETS = ("balanced-direct-translation-v1", "serial-direct-translation-v1")
_REPORT_SCHEMA = "meshvale.report/1"
_BATCH_SCHEMA = "meshvale.batch/1"
_EXTENSION = "meshvale.repair.gltf.commands"
_MAX_JOBS = 16
_MAX_REQUEST_BYTES = 8 * 1024 * 1024
_MAX_TARGET_PAIRS = 1_000_000
_MAX_REPORT_BYTES = 16 * 1024 * 1024
_MAX_REPORT_ALLOWANCE = _MAX_JOBS * _MAX_REPORT_BYTES
_MAX_FRAMING_BYTES = 8 * 1024 * 1024
_MAX_INDEX = (1 << 64) - 1
_PROFILE = "gltf-command-native-receipt"
_RECEIPT_CHECK = "native-receipt-validation"
_ABSENT_RECEIPT = "Native receipt validation was blocked because no native receipt bytes are available."
_DEFAULT_REPORT_PATH = "receipts/report.json"
_PROHIBITED_MEMBER_CHARACTERS = frozenset(':<>"|?*\\')
_ASCII_FOLD = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
_WINDOWS_DEVICE_STEMS = frozenset(("con", "prn", "aux", "nul")) | frozenset(
    prefix + str(number) for prefix in ("com", "lpt") for number in range(1, 10))
_JSON = json.JSONEncoder(ensure_ascii=False, separators=(",", ":"), allow_nan=False)
_REQUIRED = frozenset(("id", "input", "destination", "targets", "primitive_ordinal", "triangles", "values"))
_OPTIONAL = frozenset(("resource_root", "row_local_attributes", "report_path"))
_TRIANGLE_POLICIES = ("require-triangles", "convert-strip-or-fan")
_VALUE_POLICIES = ("raw", "raw-and-normalized")
_ATTRIBUTE_DOMAINS = ("vertex", "face", "corner")

# This is a detached value seam, including the isolated compiled test variant.
# Only these named native domains and public fields may enter an extension;
# arbitrary object dictionaries, messages, paths and runtime owners never do.
_ENUM_DOMAINS = frozenset((
    "CandidateOutcome", "GltfContainerCounterAvailability", "GltfFileWorkflowDiagnosticCode",
    "GltfFileWorkflowPublicationOutcome", "GltfFileWorkflowStage", "GltfWorkflowDiagnosticCode",
    "GltfWorkflowExecutionOutcome", "GltfWorkflowExitMeaning", "GltfWorkflowProfileOutcome",
    "GltfWorkflowStage", "GltfWorkflowStatus", "MaterializedDuplicateDiagnosticCode",
    "MaterializedDuplicateStage", "MaterializedDuplicateStatus", "MaterializationDiagnosticCode",
    "MaterializationStatus", "StorageIssueCode", "GlbStatus", "GltfDiagnosticCode",
    "GltfFileCleanupOutcome", "GltfFileDiagnosticCode", "GltfFileFormat",
    "GltfFilePublicationOutcome", "GltfFilePublicationPhase", "GltfFileStage", "GltfFileStatus",
    "GltfMaterializationDiagnosticCode", "GltfMaterializationStage", "GltfMaterializationStatus",
    "GltfResourceKind", "GltfStatus", "GltfTopologyOutputInspectionDiagnosticCode",
    "GltfTopologyOutputInspectionStage", "GltfTopologyOutputInspectionStatus",
    "GltfTopologyWriteDiagnosticCode", "GltfTopologyWriteStage", "GltfTopologyWriteStatus",
))
_OBSERVATION_FIELDS = {
    "GltfFileWorkflowByteExecution": ("hashing", "report_copy"),
    "GltfFileWorkflowDiagnostic": ("stage", "code", "memory", "publisher"),
    "GltfFileWorkflowOutcomes": ("execution", "candidate", "profile", "publication", "stage", "exit"),
    "GltfFileWorkflowRequestedBytes": ("retained_source", "wrapper", "memory", "publication_files", "retained_repair"),
    "GltfRetainedRepairRequestedBytes": ("source_geometry", "repair", "total"),
    "MaterializedDuplicateRequestedBytes": ("repair", "source_inspection", "selection", "candidate_inspection", "total"),
    "GltfWorkflowOutcomes": ("execution", "candidate", "profile", "stage", "exit"),
    "GltfRetainedFileSourceRequestedBytes": ("files", "capped_reader", "container"),
    "GltfWorkflowByteExecutionStats": ("planned_jobs", "launched_workers", "joined_workers", "caller_jobs"),
    "GltfWorkflowDiagnostic": ("stage", "code", "attribute", "element", "resource_kind", "resource_index", "json_span", "delegated", "delegated_status"),
    "GltfWorkflowRequestedBytes": ("workflow", "read", "extraction", "materialization", "repair", "writing", "reload", "reload_extraction", "verification", "total"),
    "MaterializedDuplicateDiagnostic": ("stage", "code", "target", "attribute", "element", "geometry_status", "geometry_diagnostic", "storage_issue"),
    "MaterializationDiagnostic": ("code", "attribute", "element", "storage_issue"),
    "StorageIssue": ("code", "attribute", "element"),
    "GltfDiagnostic": ("status", "code", "json_span", "resource_kind", "resource_index", "resource_span", "rule_id"),
    "GltfFileDiagnostic": ("status", "stage", "code", "file_index", "byte_offset", "resource_kind", "resource_index", "system_error", "gltf_diagnostic", "container_status", "container_byte_offset", "inspection_status", "inspection_diagnostic"),
    "GltfFileExecution": ("capture", "byte_verification"),
    "GltfFileExecutionStats": ("planned_jobs", "launched_workers", "joined_workers", "caller_jobs"),
    "GltfMaterializationDiagnostic": ("stage", "code", "source_channel", "geometry_status", "geometry_diagnostic", "storage_issue"),
    "GltfMaterializationRequestedBytes": ("bridge", "materialization", "inspection"),
    "GltfSourceSpan": ("offset", "length"),
    "GltfTopologyOutputInspectionDiagnostic": ("stage", "code", "attribute", "element", "resource", "json_span", "storage_issue"),
    "GltfTopologyWriteDiagnostic": ("stage", "code", "attribute", "element", "json_span", "storage_issue"),
    "SystemErrorCode": ("category", "value"),
    "GltfFilePublicationObservation": ("outcome", "phase", "diagnostic", "cleanup", "cleanup_error", "execution"),
}
_MEMORY_STAGES = {
    "kConfiguration": "import", "kRead": "import", "kExtract": "import",
    "kMaterialize": "inspect", "kRepair": "repair", "kWrite": "export",
    "kReload": "verify", "kReloadExtract": "verify", "kVerify": "verify",
    "kReport": "report", "kComplete": "complete",
}
_FILE_STAGES = {
    "kConfiguration": "import", "kSource": "import", "kHash": "report",
    "kReport": "report", "kPublication": "export", "kComplete": "complete",
}
_EXECUTIONS = {"kCompleted": "completed", "kFailed": "failed", "kCanceled": "cancelled"}
_PUBLICATIONS = {
    "kNotRequested": "not-requested", "kNotAttempted": "not-attempted",
    "kFailed": "failed", "kCanceled": "cancelled", "kPublished": "published",
}


class GltfCommandDeliveryError(RuntimeError):
    """No complete truthful command document can be delivered within its budget."""

    def __init__(self, exit_code=2):
        if type(exit_code) is not int or exit_code not in (2, 130):
            raise ValueError("Delivery failure exit must be 2 or 130.")
        super().__init__("A complete glTF command report could not be delivered.")
        self.exit_code = exit_code


@dataclass(frozen=True, slots=True)
class GltfCommandResult:
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
    declarations: int
    declaration_name_bytes: int

    @classmethod
    def from_session(cls, session):
        # These guards precede Python copies/encoding; final conversion still
        # belongs to the native preparation path using this very same preset.
        preset = _workflow.GltfExecution.resolved_preset.__get__(session, _workflow.GltfExecution)
        workflow = preset["workflow_limits"]
        memory = workflow["memory"]
        repair = memory["repair"]
        return cls(preset["capture_limits"]["files"]["max_native_path_code_units"],
                   workflow["max_report_path_bytes"], memory["max_input_id_bytes"],
                   repair["max_targets"], repair["max_row_local_declarations"],
                   repair["max_declaration_name_bytes"])


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
    return InvocationError("Invalid glTF invocation; no assets processed.")


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
    for component in value.split("/"):
        stem = component.split(".", 1)[0].translate(_ASCII_FOLD)
        if (not component or component in (".", "..") or component.endswith((".", " ")) or
                stem in _WINDOWS_DEVICE_STEMS):
            raise _invocation_error()
    return value


def _shape(request, limits):
    if (type(request) is not dict or len(request) != 2 or set(request) != {"schema", "jobs"} or
            type(request["schema"]) is not str or request["schema"] not in (_REQUEST_SCHEMA, _REQUEST_SCHEMA_V2) or
            type(request["jobs"]) is not list or len(request["jobs"]) > _MAX_JOBS):
        raise _invocation_error()
    required = _REQUIRED | {"processing_profile"} if request["schema"] == _REQUEST_SCHEMA_V2 else _REQUIRED
    rows, identities, targets = [], set(), 0
    for row in request["jobs"]:
        if (type(row) is not dict or not len(required) <= len(row) <= len(required) + len(_OPTIONAL) or
                not required <= set(row) or set(row) - required - _OPTIONAL):
            raise _invocation_error()
        copied = {name: row[name] for name in row}
        # Refuse oversized ordinary arrays before iterating or copying them.
        if type(copied["targets"]) is not list or len(copied["targets"]) > limits.target_pairs:
            raise _invocation_error()
        targets += len(copied["targets"])
        if targets > _MAX_TARGET_PAIRS:
            raise _invocation_error()
        if "row_local_attributes" in copied and (
                type(copied["row_local_attributes"]) is not list or
                len(copied["row_local_attributes"]) > limits.declarations):
            raise _invocation_error()
        _text(copied["id"], max_chars=limits.input_id_bytes, max_bytes=limits.input_id_bytes)
        for name in ("input", "destination"):
            _text(copied[name], max_chars=limits.path_units - 1)
        _choice(copied["triangles"], _TRIANGLE_POLICIES)
        _choice(copied["values"], _VALUE_POLICIES)
        if request["schema"] == _REQUEST_SCHEMA_V2:
            _choice(copied["processing_profile"], _PROCESSING_PROFILES)
        if copied["id"] in identities:
            raise _invocation_error()
        identities.add(copied["id"])
        _index(copied["primitive_ordinal"])
        pairs = []
        for pair in copied["targets"]:
            if type(pair) is not list or len(pair) != 2:
                raise _invocation_error()
            pairs.append([_index(pair[0]), _index(pair[1])])
        copied["targets"] = pairs
        if "row_local_attributes" in copied:
            declarations = []
            remaining_name_bytes = limits.declaration_name_bytes
            for pair in copied["row_local_attributes"]:
                if type(pair) is not list or len(pair) != 2:
                    raise _invocation_error()
                domain = _choice(pair[0], _ATTRIBUTE_DOMAINS)
                remaining_name_bytes -= _text_bytes(pair[1], max_chars=remaining_name_bytes,
                                                    max_bytes=remaining_name_bytes)
                declarations.append([domain, pair[1]])
            copied["row_local_attributes"] = declarations
        if copied.get("resource_root") is not None:
            _text(copied["resource_root"], max_chars=limits.path_units - 1)
        if "report_path" in copied:
            _report_path(copied["report_path"], limits.report_path_bytes)
        rows.append(copied)
    # Shape/count admission precedes the separate API canonical-byte admission.
    _encoded({"schema": request["schema"], "jobs": rows}, _MAX_REQUEST_BYTES, _invocation_error)
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
        if execution is not None and not isinstance(execution, _workflow.GltfExecution):
            raise _invocation_error()
        if cancellation is not None and not isinstance(cancellation, _workflow.GltfCancellation):
            raise _invocation_error()
        if execution is None:
            preset = ("balanced-direct-translation-v1"
                      if type(request) is dict and request.get("schema") == _REQUEST_SCHEMA_V2
                      else "balanced-v1")
            session = _workflow.GltfExecution(preset=preset)
        else:
            session = execution
        token = _workflow.GltfCancellation() if cancellation is None else cancellation
        limits = _AdmissionLimits.from_session(session)
        rows = _shape(request, limits)
        if any(row.get("processing_profile", _STATIC_PROFILE) == _TRANSLATION_PROFILE for row in rows):
            preset = _workflow.GltfExecution.preset.__get__(session, _workflow.GltfExecution)
            if preset not in _TRANSLATION_PRESETS:
                raise _invocation_error()
        _preflight_reports()
        base_text = os.fspath(Path.cwd() if base is None else base)
        _text(base_text, max_chars=limits.path_units - 1)
        directory = Path(base_text)
        if not directory.is_absolute():
            directory = Path.cwd() / directory
        for row in rows:
            source = _path(row["input"], directory)
            destination = _path(row["destination"], directory)
            resource = None if row.get("resource_root") is None else _path(row["resource_root"], directory)
            arguments = {"source": source, "destination": destination, "targets": row["targets"],
                         "input_id": row["id"], "primitive_ordinal": row["primitive_ordinal"],
                         "triangles": row["triangles"], "values": row["values"],
                         "processing_profile": row.get("processing_profile", _STATIC_PROFILE),
                         "resource_root": resource, "row_local_attributes": row.get("row_local_attributes", []),
                         "report_path": row.get("report_path", _DEFAULT_REPORT_PATH),
                         "execution": session, "cancellation": token, "retain_failure_facts": True}
            jobs.append(_Job(row, arguments))
        destinations = [job.arguments["destination"].resolve() for job in jobs]
        protected = []
        for job in jobs:
            source = job.arguments["source"]
            protected.extend((source.resolve(), (job.arguments["resource_root"] or source.parent).resolve()))
        for i, destination in enumerate(destinations):
            if any(_overlap(destination, previous) for previous in destinations[:i]):
                raise _invocation_error()
            if any(_overlap(destination, source) for source in protected):
                raise _invocation_error()
        if len(jobs) * _MAX_REPORT_BYTES > _MAX_REPORT_ALLOWANCE:
            raise _invocation_error()
        for job in jobs:
            job.prepared = _workflow._prepare_gltf_request(**job.arguments)
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
        return _member(result.outcomes.execution, "GltfWorkflowExecutionOutcome") == "kCanceled"
    return _member(result.capture_status, "GltfFileStatus") == "kCanceled"


def _failure_exit(result):
    return 130 if _canceled(result) else 2


def _core_outcomes(result):
    if not result.workflow_started:
        return ("cancelled" if _canceled(result) else "failed"), "import", "not-attempted", "not-attempted"
    outcomes = result.outcomes
    execution = _EXECUTIONS[_member(outcomes.execution, "GltfWorkflowExecutionOutcome")]
    candidate = "not-attempted" if outcomes.candidate is None else _member(outcomes.candidate, "CandidateOutcome")
    publication = _PUBLICATIONS[_member(outcomes.publication, "GltfFileWorkflowPublicationOutcome")]
    stage = _member(outcomes.stage, "GltfFileWorkflowStage")
    if stage == "kMemory":
        inner = result.memory_outcomes
        if inner is None:
            raise ValueError("Reached memory operation has no returned outcomes.")
        stage = _MEMORY_STAGES[_member(inner.stage, "GltfWorkflowStage")]
    else:
        stage = _FILE_STAGES[stage]
    if execution == "completed":
        stage = "complete"
    elif stage == "complete":
        raise ValueError("Failed native execution has no failure stage.")
    return execution, stage, candidate, publication


def _project_gltf_fallback(result, job):
    """Detach actual no-receipt observations, including a real test-module result."""
    try:
        execution, stage, candidate, publication = _core_outcomes(result)
        if publication == "published" or result.publication is not None and _member(
                result.publication.outcome, "GltfFilePublicationOutcome") == "kPublished":
            raise GltfCommandDeliveryError(_failure_exit(result))
        options = {"primitive_ordinal": job["primitive_ordinal"], "triangles": job["triangles"],
                   "values": job["values"], "target_pairs": [list(pair) for pair in job["targets"]],
                   "row_local_attributes": [list(pair) for pair in job.get("row_local_attributes", [])]}
        scope = {"snapshot": None, "mesh": None, "subject": "command receipt validation"}
        report = {"schema": _REPORT_SCHEMA, "tool": {"name": "meshvale-repair", "version": __version__},
                  "input": {"id": job["id"], "sha256": None},
                  "operation": {"kind": "repair", "name": "remove-duplicate-faces", "version": "1", "options": options, "targets": []},
                  "execution": {"outcome": execution, "stage": stage,
                                "reason": None if execution == "completed" else "Native execution did not complete; exact reached observations are in the command extension."},
                  "candidate": {"outcome": candidate, "snapshot": None},
                  "profile": {"name": _PROFILE, "version": "1", "required_checks": [_RECEIPT_CHECK], "outcome": "incomplete"},
                  "snapshots": [], "maps": [], "preservation": [],
                  "diagnostics": [{"id": "native-receipt-absent", "code": "gltf.command.native-receipt-unavailable", "severity": "warning",
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
                raise GltfCommandDeliveryError(_failure_exit(result))
            for role, counts in (("input", before), ("candidate", after)):
                report["snapshots"].append({"id": role, "role": role, "meshes": [{"id": "selected",
                    "vertex_count": counts[0], "face_count": counts[1], "corner_count": counts[2]}]})
            report["candidate"]["snapshot"] = "candidate"
            report["operation"]["targets"] = [{"snapshot": "input", "mesh": "selected", "domain": "face", "index": pair[1]} for pair in job["targets"]]
            for domain, entries in (("vertex", None), ("face", result.face_map), ("corner", result.corner_map)):
                if domain != "vertex" and type(entries) is not tuple:
                    raise GltfCommandDeliveryError(_failure_exit(result))
                report["maps"].append({"id": "repair-" + domain, "domain": domain,
                    "from": {"snapshot": "input", "mesh": "selected"}, "to": {"snapshot": "candidate", "mesh": "selected"},
                    "kind": "identity" if domain == "vertex" or candidate == "unchanged" else "explicit",
                    "entries": None if domain == "vertex" or candidate == "unchanged" else list(entries)})
        report["extensions"][_EXTENSION] = {
            "version": "1", "origin": "command-workflow-fallback" if result.workflow_started else "command-capture-fallback",
            "native_report_available": False, "workflow_started": result.workflow_started,
            "capture": {"status": _observe(result.capture_status), "diagnostic": _observe(result.capture_diagnostic), "execution": _observe(result.capture_execution)},
            "workflow": None if not result.workflow_started else {"status": _observe(result.status), "outcomes": _observe(result.outcomes),
                "diagnostic": _observe(result.diagnostic), "execution": _observe(result.execution)},
            "memory_outcomes": _observe(result.memory_outcomes), "publication": _observe(result.publication),
            "requested_bytes": _observe(result.requested_bytes)}
        profile = result.processing_profile
        if type(profile) is not str or profile != job.get("processing_profile", _STATIC_PROFILE):
            raise GltfCommandDeliveryError(_failure_exit(result))
        if profile == _TRANSLATION_PROFILE:
            extension = report["extensions"][_EXTENSION]
            extension["version"] = "2"
            extension["processing_profile"] = profile
            extension["processing"] = result.translation_facts
        return report
    finally:
        # A handled delivery exception must not pin a native owner in traceback
        # frames after the scheduler clears its prepared/result slots.
        result = None


def _detach(result, job):
    from meshvale_reports import ReportError, exit_code, load
    try:
        failure = _failure_exit(result)
        try:
            payload = result.report_json
            if payload is None:
                payload = _encoded(_project_gltf_fallback(result, job), _MAX_REPORT_BYTES,
                                   lambda: GltfCommandDeliveryError(failure))
            if type(payload) is not bytes or not payload or len(payload) > _MAX_REPORT_BYTES:
                raise GltfCommandDeliveryError(failure)
            parsed = load(payload)
            if parsed["schema"] != _REPORT_SCHEMA or parsed["input"] is None or parsed["input"]["id"] != job["id"]:
                raise GltfCommandDeliveryError(failure)
            identity, code = parsed["input"], exit_code(parsed)
            return payload, identity, code
        except (MemoryError, ReportError, UnicodeError):
            raise GltfCommandDeliveryError(failure) from None
    finally:
        result = None


def _attempt(job):
    prepared, job.prepared = job.prepared, None
    result = None
    try:
        result = _workflow._run_gltf_request(prepared)
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
            raise GltfCommandDeliveryError(failure)
        parts.append(value)

    def field(value):
        add(_encoded(value, _MAX_FRAMING_BYTES, lambda: GltfCommandDeliveryError(failure)))

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
            raise GltfCommandDeliveryError(failure)
        return GltfCommandResult(payload, code)
    except MemoryError:
        raise GltfCommandDeliveryError(failure) from None


def run_gltf_jobs(request, *, base=None, fail_fast=False, execution=None, cancellation=None):
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
            except GltfCommandDeliveryError as error:
                if 130 in codes or token.cancelled:
                    raise GltfCommandDeliveryError(130) from None
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


def _run_gltf_single(job, *, base=None, execution=None, cancellation=None,
                     processing_profile=_STATIC_PROFILE):
    _choice(processing_profile, _PROCESSING_PROFILES)
    schema = _REQUEST_SCHEMA
    if processing_profile == _TRANSLATION_PROFILE:
        schema = _REQUEST_SCHEMA_V2
        job = {**job, "processing_profile": processing_profile}
    jobs, _ = _admit({"schema": schema, "jobs": [job]}, base, execution, cancellation)
    try:
        payload, _, code = _attempt(jobs[0])
        return GltfCommandResult(payload, code)
    finally:
        jobs[0].prepared = None


def _read_gltf_request(path):
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


__all__ = ["GltfCommandResult", "GltfCommandDeliveryError", "run_gltf_jobs"]
