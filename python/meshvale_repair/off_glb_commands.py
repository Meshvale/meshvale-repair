# SPDX-License-Identifier: Apache-2.0
"""Whole-invocation admission and retained OFF to annotated GLB commands."""
import argparse
from dataclasses import dataclass
import json
import math
import os
import re
import signal
import sys
import threading
from types import MappingProxyType

from . import off_surface_export as _workflow
from ._version import __version__
from .commands import InvocationError


_REQUEST_SCHEMA = "meshvale.repair.off-glb-jobs/1"
_RESULT_SCHEMA = "meshvale.repair.off-glb-result/1"
_BATCH_SCHEMA = "meshvale.repair.off-glb-batch/1"
_MAX_JOBS = 4
_MAX_REQUEST_BYTES = 8388608
_MAX_TARGETS = 1000000
_MAX_REPORT_BYTES = 4194304
_MAX_RECEIPT_BYTES = 33554432
_MAX_OBSERVATION_BYTES = 16777216
_MAX_RESULT_BYTES = 268435456
_MAX_FRAMING_BYTES = 8388608
_MAX_BATCH_BYTES = 1082130432
_MAX_RESERVED_REPORT_BYTES = 16777216
_U64_MAX = (1 << 64) - 1
_JSON = json.JSONEncoder(ensure_ascii=False, allow_nan=False, separators=(",", ":"))
_REQUIRED = frozenset(("id", "input", "destination", "targets", "text", "edge_count",
    "coordinates", "precision", "max_position_error", "acknowledge_triangle_conversion",
    "acknowledge_corner_expansion", "acknowledge_unused_vertices"))
_OPTIONAL = frozenset(("receipt_path", "preset"))
_OBSERVATIONS = ("capture", "off", "triangulation", "encoding", "inspection", "publication")
_OBSERVER_FIELDS = frozenset(("status", "stage", "code", *_OBSERVATIONS))
_DELIVERY_NONE = MappingProxyType({"status": "not-attempted", "code": "none"})


def _invalid():
    return InvocationError("Invalid OFF to GLB invocation; no assets processed.")


class _FramingFailure(ValueError):
    def __init__(self, code):
        self.code = code


@dataclass(frozen=True, slots=True)
class OffGlbDeliveryObservation:
    """Observed delivery only; prepared bytes do not certify stdout IO."""
    status: str = "not-attempted"
    code: str = "none"


@dataclass(frozen=True, slots=True)
class OffGlbJobResult:
    id: str
    attempted: bool = False
    result: object = None
    reason: object = None


@dataclass(frozen=True, slots=True)
class OffGlbBatchResult:
    """Stable holder of every actual attempted native result and bounded bytes."""
    mode: str
    jobs: tuple
    delivery: OffGlbDeliveryObservation
    stdout_bytes: bytes | None = None
    exit: int = 0


@dataclass(frozen=True, slots=True)
class _Job:
    id: str
    source: str
    destination: str
    prepared: object


def _merge(*codes):
    return next((code for code in (130, 2, 1) if code in codes), 0)


def _set_delivery(holder, status, code):
    object.__setattr__(holder.delivery, "status", status)
    object.__setattr__(holder.delivery, "code", code)


def _encode(value, limit, code):
    parts, size = [], 0
    try:
        for chunk in _JSON.iterencode(value):
            chunk = chunk.encode("utf-8")
            size += len(chunk)
            if size > limit:
                raise _FramingFailure(code)
            parts.append(chunk)
    except _FramingFailure:
        raise
    except (ValueError, TypeError, UnicodeError, OverflowError, RecursionError):
        raise _FramingFailure("invalid-observation") from None
    return b"".join(parts)


def _pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("Duplicate JSON key")
        result[key] = value
    return result


def _constant(_):
    raise ValueError("Non-finite JSON constant")


def _decode(raw):
    return json.loads(raw, object_pairs_hook=_pairs, parse_constant=_constant)


def _identity(value):
    if (type(value) is not str or not 1 <= len(value) <= 128 or
            re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.:-]*", value) is None):
        raise _invalid()
    return value


def _shape(request):
    if (type(request) is not dict or set(request) != {"schema", "jobs"} or
            request["schema"] != _REQUEST_SCHEMA or type(request["schema"]) is not str or
            type(request["jobs"]) is not list or len(request["jobs"]) > _MAX_JOBS):
        raise _invalid()
    rows, identities, count = [], set(), 0
    for row in request["jobs"]:
        if (type(row) is not dict or not _REQUIRED <= set(row) or
                set(row) - _REQUIRED - _OPTIONAL):
            raise _invalid()
        identity = _identity(row["id"])
        if identity in identities:
            raise _invalid()
        identities.add(identity)
        if type(row["targets"]) is not list:
            raise _invalid()
        count += len(row["targets"])
        if count > _MAX_TARGETS:
            raise _invalid()
        targets = []
        for pair in row["targets"]:
            if (type(pair) is not list or len(pair) != 2 or any(
                    type(index) is not int or not 0 <= index <= _U64_MAX for index in pair)):
                raise _invalid()
            targets.append((pair[0], pair[1]))
        for name in ("input", "destination", "text", "edge_count", "coordinates",
                     "precision", "receipt_path", "preset"):
            if name in row and type(row[name]) is not str:
                raise _invalid()
        if (row["text"] != "canonical-ascii" or row["edge_count"] != "unknown-zero" or
                row["coordinates"] != "meters-right-handed-y-up" or
                row["precision"] not in ("exact32", "bounded-rne32") or
                row.get("preset", "balanced-v1") not in ("balanced-v1", "serial-reference-v1")):
            raise _invalid()
        error = row["max_position_error"]
        if (type(error) not in (int, float) or not math.isfinite(error) or error < 0 or
                row["precision"] == "exact32" and error != 0):
            raise _invalid()
        for name in ("acknowledge_triangle_conversion", "acknowledge_corner_expansion",
                     "acknowledge_unused_vertices"):
            if type(row[name]) is not bool or not row[name]:
                raise _invalid()
        rows.append({**row, "targets": tuple(targets)})
    # The public scheduler takes a Python document too; enforce its wire ceiling.
    try:
        _encode(request, _MAX_REQUEST_BYTES, "frame-limit")
    except _FramingFailure:
        raise _invalid() from None
    return rows


def _preflight_report_reader():
    from meshvale_reports import ReportError, load, schema_document, validate
    from jsonschema import Draft202012Validator
    from ._off_glb_schema import SCHEMA
    if not all(map(callable, (load, schema_document, validate))):
        raise ImportError("The OFF report reader is unavailable")
    Draft202012Validator.check_schema(schema_document())
    Draft202012Validator.check_schema(SCHEMA)
    return load, ReportError, Draft202012Validator(SCHEMA)


def _admit(request, base, cancellation):
    try:
        rows = _shape(request)
        native = _workflow._native()
        if cancellation is not None and not isinstance(cancellation, native.OffCancellation):
            raise _invalid()
        token = native.OffCancellation() if cancellation is None else cancellation
        directory = os.getcwd() if base is None else _workflow._logical_path(base, os.getcwd())
        arguments, sources, destinations = [], [], []
        for row in rows:
            source = _workflow._logical_path(row["input"], directory)
            destination = _workflow._logical_path(row["destination"], directory)
            sources.append(_workflow._comparison_path(source))
            destinations.append(_workflow._comparison_path(destination))
            options = {key: value for key, value in row.items() if key not in ("id", "input", "destination")}
            arguments.append((row["id"], source, destination, options))
        for index, destination in enumerate(destinations):
            if (any(_workflow._overlap(destination, prior) for prior in destinations[:index]) or
                    any(_workflow._overlap(destination, source) for source in sources)):
                raise _invalid()
        count = len(rows)
        # Reserve escaped reports and receipts plus already encoded observations.
        per_job = 6 * (_MAX_REPORT_BYTES + _MAX_RECEIPT_BYTES) + _MAX_OBSERVATION_BYTES + _MAX_FRAMING_BYTES
        if (count * _MAX_REPORT_BYTES > _MAX_RESERVED_REPORT_BYTES or
                per_job > _MAX_RESULT_BYTES or
                count * _MAX_RESULT_BYTES + _MAX_FRAMING_BYTES > _MAX_BATCH_BYTES):
            raise _invalid()
        reader = _preflight_report_reader()
        jobs = tuple(_Job(identity, source, destination,
            _workflow.prepare_off_glb_export(source, destination, input_id=identity, **options))
            for identity, source, destination, options in arguments)
        return jobs, token, reader
    except (TypeError, ValueError, OverflowError, UnicodeError, RecursionError):
        raise _invalid() from None


def _closed(value, fields):
    if type(value) is not dict or set(value) != set(fields):
        raise _FramingFailure("invalid-observation")


def _validate_receipt(value, report, validator):
    # The declared producer schema owns the shape; validation cannot certify
    # publication or create a proof. The retained native owner owns that fact.
    if not validator.is_valid(value):
        raise _FramingFailure("invalid-observation")
    caller = value["caller_report"]
    if (value["encoding"] != "glb" or value["representation"] != "annotated-bundle-v1" or
            caller is None or report is None):
        raise _FramingFailure("invalid-observation")
    try:
        if caller["text"].encode("utf-8") != report:
            raise _FramingFailure("invalid-observation")
    except UnicodeError:
        raise _FramingFailure("invalid-observation") from None


def _report_text(raw, limit, code):
    if raw is None:
        return None
    if type(raw) is not bytes:
        raise _FramingFailure("invalid-observation")
    if len(raw) > limit:
        raise _FramingFailure(code)
    try:
        return raw.decode("utf-8")
    except UnicodeError:
        raise _FramingFailure("invalid-observation") from None


def _detach(job, reader):
    result = job.result
    if not job.attempted:
        observation = dict.fromkeys(_OBSERVATIONS)
        observation.update(status="not-attempted", stage=None, code=None)
        code, report_text, receipt_text = None, None, None
    else:
        native = _workflow._native()
        try:
            observation = native._observe_off_glb_export(result)
        except native.OffGlbObservationLimitError:
            raise _FramingFailure("observation-limit") from None
        _closed(observation, _OBSERVER_FIELDS)
        # The native observer is a closed typed projection, never __dict__.
        for field in ("status", "stage", "code"):
            if (type(observation[field]) is not str or re.fullmatch(
                    r"(?:capture|native):k[A-Za-z0-9]+", observation[field]) is None):
                raise _FramingFailure("invalid-observation")
        if observation["capture"] is None:
            raise _FramingFailure("invalid-observation")
        _encode(observation, _MAX_OBSERVATION_BYTES, "observation-limit")
        code = result.exit
        if type(code) is not int or code not in (0, 1, 2, 130):
            raise _FramingFailure("invalid-observation")
        report = result.off_report_bytes
        report_text = _report_text(report, _MAX_REPORT_BYTES, "report-limit")
        if report is not None:
            load, report_error, _ = reader
            try:
                checked = load(report)
            except report_error:
                raise _FramingFailure("invalid-observation") from None
            if (checked.get("schema") != "meshvale.report/1" or
                    type(checked.get("input")) is not dict or checked["input"].get("id") != job.id):
                raise _FramingFailure("invalid-observation")
        receipt_text = _report_text(result.publication_receipt_bytes, _MAX_RECEIPT_BYTES, "receipt-limit")
        if receipt_text is not None:
            try:
                receipt = _decode(receipt_text)
            except (ValueError, TypeError, UnicodeError, OverflowError, RecursionError):
                raise _FramingFailure("invalid-observation") from None
            _validate_receipt(receipt, report, reader[2])
        if code == 0 and (receipt_text is None or observation["status"] != "native:kSuccess"):
            raise RuntimeError("Successful OFF to GLB result has no publication receipt")
    return {"schema": _RESULT_SCHEMA, "id": job.id, "attempted": job.attempted,
            "exit": code, **observation, "delivery": dict(_DELIVERY_NONE),
            "off_report_utf8": report_text, "publication_receipt_utf8": receipt_text,
            "reason": None if job.reason is None else dict(job.reason)}


def _frame(holder, reader, single):
    parts, framing, total = [], 0, 0

    def append(raw, outer=False):
        nonlocal framing, total
        total += len(raw)
        if outer:
            framing += len(raw)
        if total > _MAX_BATCH_BYTES or framing > _MAX_FRAMING_BYTES:
            raise _FramingFailure("frame-limit")
        parts.append(raw)

    try:
        if not single:
            prefix = _encode({"schema": _BATCH_SCHEMA, "mode": holder.mode,
                              "exit": holder.exit}, _MAX_FRAMING_BYTES, "frame-limit")
            append(prefix[:-1] + b',"jobs":[', True)
        for index, job in enumerate(holder.jobs):
            row = _detach(job, reader)
            if index:
                append(b",", True)
            raw = _encode(row, _MAX_RESULT_BYTES, "frame-limit")
            skeleton = {key: value for key, value in row.items()}
            for key in (*_OBSERVATIONS, "off_report_utf8", "publication_receipt_utf8"):
                skeleton[key] = None
            framing += len(_encode(skeleton, _MAX_FRAMING_BYTES, "frame-limit"))
            append(raw)
        if not single:
            append(b"]}", True)
        if framing > _MAX_FRAMING_BYTES:
            raise _FramingFailure("frame-limit")
        object.__setattr__(holder, "stdout_bytes", b"".join(parts))
        _set_delivery(holder, "prepared", "none")
    except _FramingFailure as error:
        _set_delivery(holder, "framing-failed", error.code)
        object.__setattr__(holder, "exit", _merge(holder.exit, 2))


def _run(jobs, token, reader, fail_fast, single=False):
    # All fixed slots, the delivery observation and the stable outer holder exist
    # before the first source capture. Only these private handoffs mutate them.
    holder = OffGlbBatchResult("fail-fast" if fail_fast else "continue",
        tuple(OffGlbJobResult(job.id) for job in jobs), OffGlbDeliveryObservation())
    stopped = None
    for prepared, slot in zip(jobs, holder.jobs):
        if token.cancelled:
            stopped = MappingProxyType({"kind": "canceled", "id": None if stopped is None else stopped["id"]})
            object.__setattr__(holder, "exit", _merge(holder.exit, 130))
        if stopped is not None:
            object.__setattr__(slot, "reason", stopped)
            continue
        result = _workflow.run_off_glb_export(prepared.prepared, cancellation=token)
        object.__setattr__(slot, "result", result)
        object.__setattr__(slot, "attempted", True)
        code = result.exit
        if type(code) is not int or code not in (0, 1, 2, 130):
            raise RuntimeError("Invalid OFF to GLB native exit meaning")
        object.__setattr__(holder, "exit", _merge(holder.exit, code))
        if code == 130 or token.cancelled:
            stopped = MappingProxyType({"kind": "canceled", "id": slot.id})
            object.__setattr__(holder, "exit", _merge(holder.exit, 130))
        elif fail_fast and code:
            stopped = MappingProxyType({"kind": "fail-fast", "id": slot.id})
    if token.cancelled:
        object.__setattr__(holder, "exit", _merge(holder.exit, 130))
    _frame(holder, reader, single)
    return holder


def run_off_glb_jobs(request, *, base=None, fail_fast=False, cancellation=None):
    """Admit all rows, run in order, and retain actual owners before framing.

    This scheduler never installs signal handlers or writes stdout.
    """
    if type(fail_fast) is not bool:
        raise _invalid()
    jobs, token, reader = _admit(request, base, cancellation)
    return _run(jobs, token, reader, fail_fast)


def _read_request(path):
    try:
        with open(path, "rb") as stream:
            raw = stream.read(_MAX_REQUEST_BYTES + 1)
        if len(raw) > _MAX_REQUEST_BYTES:
            raise _invalid()
        return _decode(raw.decode("utf-8"))
    except (OSError, UnicodeError, ValueError, TypeError, RecursionError):
        raise _invalid() from None


def _parser():
    parser = argparse.ArgumentParser(prog="meshvale-repair-off-glb")
    parser.add_argument("--version", action="version", version=__version__)
    commands = parser.add_subparsers(dest="command", required=True)
    single = commands.add_parser("single", help="repair OFF and publish annotated GLB")
    for name in ("input", "destination", "targets", "text", "edge-count", "coordinates",
                 "precision", "max-position-error"):
        single.add_argument("--" + name, required=True)
    for name in ("triangle-conversion", "corner-expansion", "unused-vertices"):
        single.add_argument("--ack-" + name, action="store_true", required=True)
    single.add_argument("--id", default="source")
    single.add_argument("--receipt-path", default="publication.json")
    single.add_argument("--preset", default="balanced-v1")
    batch = commands.add_parser("batch", help="run a bounded JSON request in declaration order")
    batch.add_argument("--request", required=True)
    batch.add_argument("--base")
    batch.add_argument("--fail-fast", action="store_true")
    return parser


def _write(holder):
    if holder.stdout_bytes is None:
        return
    try:
        stream = sys.stdout.buffer
        # A short write is an observed failure, never a certified full document.
        count = stream.write(holder.stdout_bytes)
        if count != len(holder.stdout_bytes):
            raise OSError("Incomplete stdout write")
        stream.flush()
        _set_delivery(holder, "written", "none")
    except (OSError, ValueError, AttributeError):
        _set_delivery(holder, "write-failed", "stdout-io")
        object.__setattr__(holder, "exit", _merge(holder.exit, 2))


def main(argv=None):
    """Console/module entry point with boundary-delayed SIGINT recording."""
    if threading.current_thread() is not threading.main_thread():
        return 2
    interrupted, token, holder = False, None, None

    def record_interrupt(_signum, _frame):
        nonlocal interrupted
        interrupted = True
        if token is not None:
            token.cancel()

    previous = signal.getsignal(signal.SIGINT)
    signal.signal(signal.SIGINT, record_interrupt)
    try:
        args = _parser().parse_args(argv)
        if args.command == "single":
            try:
                if len(args.targets.encode("utf-8")) > _MAX_REQUEST_BYTES:
                    raise _invalid()
                targets = _decode(args.targets)
                error = float(args.max_position_error)
            except (ValueError, TypeError, OverflowError, RecursionError):
                raise _invalid() from None
            row = {"id": args.id, "input": args.input, "destination": args.destination,
                   "targets": targets, "text": args.text, "edge_count": args.edge_count,
                   "coordinates": args.coordinates, "precision": args.precision,
                   "max_position_error": error,
                   "acknowledge_triangle_conversion": args.ack_triangle_conversion,
                   "acknowledge_corner_expansion": args.ack_corner_expansion,
                   "acknowledge_unused_vertices": args.ack_unused_vertices,
                   "receipt_path": args.receipt_path, "preset": args.preset}
            request, base, fail_fast = {"schema": _REQUEST_SCHEMA, "jobs": [row]}, None, False
        else:
            request = _read_request(args.request)
            logical = _workflow._logical_path(args.request, os.getcwd())
            base = os.path.dirname(logical) if args.base is None else args.base
            fail_fast = args.fail_fast
        jobs, token, reader = _admit(request, base, None)
        if interrupted:
            token.cancel()
        holder = _run(jobs, token, reader, fail_fast, single=args.command == "single")
        if interrupted:
            object.__setattr__(holder, "exit", _merge(holder.exit, 130))
        _write(holder)
        return _merge(holder.exit, 130 if interrupted else 0)
    except SystemExit as error:
        return _merge(int(error.code or 0), 130 if interrupted else 0)
    except Exception:
        # No exception text, guessed native facts or emergency document enters stdout.
        return _merge(2, 130 if interrupted else 0)
    finally:
        signal.signal(signal.SIGINT, previous)


__all__ = ["OffGlbBatchResult", "OffGlbJobResult", "OffGlbDeliveryObservation",
           "run_off_glb_jobs", "main"]


if __name__ == "__main__":
    raise SystemExit(main())
