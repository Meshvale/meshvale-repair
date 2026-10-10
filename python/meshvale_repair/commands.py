# SPDX-License-Identifier: Apache-2.0
"""Validated OBJ commands and a sequential scheduler using the file workflow."""
import argparse
from contextlib import contextmanager
from copy import deepcopy
import json
import os
from pathlib import Path
import signal
import sys
from . import __version__


class InvocationError(ValueError):
    """The complete invocation was refused before asset processing."""


def _path(value, base):
    if type(value) is not str or not value or "\0" in value:
        raise InvocationError("Invalid path representation.")
    value.encode("utf-8")
    path = Path(value)
    return base / path if not path.is_absolute() else path


def _overlap(first, second):
    return first == second or first in second.parents or second in first.parents


def _jobs(request, base, token, phase):
    from .obj_workflow import _validate_obj_request
    import meshvale_interchange as io
    if (type(request) is not dict or set(request) != {"schema", "jobs"} or
            request["schema"] != "meshvale.repair.obj-jobs/1" or type(request["jobs"]) is not list):
        raise InvocationError("Invalid batch request.")
    if token is not None and not isinstance(token, io.Cancellation):
        raise InvocationError("Invalid cancellation token.")
    if phase is not None and not callable(phase):
        raise InvocationError("Invalid phase callback.")
    required = {"id", "input", "destination", "targets"}
    optional = {"resource_root", "row_local_attributes", "report_path"}
    jobs, identities = [], set()
    try:
        directory = Path(os.fsdecode(os.fspath(base))).resolve()
        for row in request["jobs"]:
            if type(row) is not dict or not required <= set(row) or set(row) - required - optional:
                raise InvocationError("Invalid job fields.")
            if type(row["id"]) is not str or row["id"] in identities:
                raise InvocationError("Invalid or duplicate job identity.")
            if type(row["targets"]) is not list or type(row.get("row_local_attributes", [])) is not list:
                raise InvocationError("Request pairs must be arrays.")
            if type(row.get("report_path", "meshvale-report.json")) is not str:
                raise InvocationError("Invalid receipt path representation.")
            source = _path(row["input"], directory)
            destination = _path(row["destination"], directory)
            root = None if row.get("resource_root") is None else _path(row["resource_root"], directory)
            targets, declarations, receipt = _validate_obj_request(source, destination, row["targets"],
                input_id=row["id"], resource_root=root, row_local_attributes=row.get("row_local_attributes", []),
                cancellation=token, report_path=row.get("report_path", "meshvale-report.json"))
            jobs.append({"input": source, "destination": destination, "targets": targets,
                "input_id": row["id"], "resource_root": root, "row_local_attributes": declarations, "report_path": receipt})
            identities.add(row["id"])
        destinations = [job["destination"].resolve() for job in jobs]
        roots = [(job["resource_root"] or job["input"].parent).resolve() for job in jobs]
        for i, destination in enumerate(destinations):
            for other in destinations[:i]:
                if _overlap(destination, other):
                    raise InvocationError("Output destinations overlap.")
            for root in roots:
                if _overlap(destination, root):
                    raise InvocationError("Output and source/resource domains overlap.")
    except (TypeError, ValueError, OverflowError, OSError, RuntimeError) as error:
        raise InvocationError("Invalid invocation or path isolation.") from error
    return jobs


def _batch_exit(batch):
    from meshvale_reports import exit_code
    codes = [exit_code(row["report"]) for row in batch["jobs"] if row["outcome"] == "attempted"]
    if batch["execution"]["outcome"] != "completed":
        codes.append(130 if batch["execution"]["outcome"] == "cancelled" else 2)
    batch["exit_code"] = next((code for code in (130, 2, 1) if code in codes), 0)
    return exit_code(batch)


def run_obj_jobs(request, *, base=None, fail_fast=False, cancellation=None, on_job_phase=None):
    """Validate all requests, process isolated jobs in order and return a real batch report."""
    import meshvale_interchange as io
    from meshvale_reports import exit_code
    from .obj_workflow import repair_obj_file
    if type(fail_fast) is not bool:
        raise InvocationError("fail_fast must be boolean.")
    jobs = _jobs(request, Path.cwd() if base is None else base, cancellation, on_job_phase)
    token = io.Cancellation() if cancellation is None else cancellation
    batch = {"schema": "meshvale.batch/1", "tool": {"name": "meshvale-repair", "version": __version__},
        "mode": "fail-fast" if fail_fast else "continue",
        "execution": {"outcome": "completed", "stage": "complete", "reason": None},
        "jobs": [{"id": job["input_id"], "input": {"id": job["input_id"], "sha256": None},
                  "outcome": "not-attempted", "report": None, "reason": "Scheduling not reached."} for job in jobs],
        "exit_code": 0, "extensions": {"meshvale.repair.commands": {"version": "1"}}}
    stop_reason = None
    for job, row in zip(jobs, batch["jobs"]):
        if token.stop_requested:
            stop_reason = "Batch cancellation before scheduling."
        if stop_reason is not None:
            row["reason"] = stop_reason
            continue
        phase = None if on_job_phase is None else lambda value, identity=job["input_id"]: on_job_phase(identity, value)
        result = repair_obj_file(**job, cancellation=token, on_phase=phase)
        row.update(input=deepcopy(result.report["input"]), outcome="attempted", report=result.report, reason=None)
        if fail_fast and exit_code(result.report):
            stop_reason = "Fail-fast after job " + job["input_id"] + "."
    # This observation is the scheduling-completion boundary, including the empty batch.
    if token.stop_requested:
        batch["execution"] = {"outcome": "cancelled", "stage": "repair", "reason": "Batch cancellation observed."}
    _batch_exit(batch)
    return batch


class _Parser(argparse.ArgumentParser):
    def error(self, message):
        raise InvocationError("Invalid command invocation.")


def _parser():
    parser = _Parser(prog="meshvale-repair", description="Verified targeted OBJ duplicate repair")
    parser.add_argument("--version", action="version", version=__version__)
    commands = parser.add_subparsers(dest="command", required=True, parser_class=_Parser)
    single = commands.add_parser("obj", help="Repair one OBJ into a verified new bundle")
    single.add_argument("input"); single.add_argument("destination")
    operation = single.add_mutually_exclusive_group(required=True)
    operation.add_argument("--target", action="append", metavar="KEEP:REMOVE")
    operation.add_argument("--unchanged", action="store_true")
    single.add_argument("--input-id", default="source")
    single.add_argument("--resource-root")
    single.add_argument("--row-local", action="append", default=[], metavar="DOMAIN:NAME")
    single.add_argument("--report-path", default="meshvale-report.json")
    batch = commands.add_parser("obj-batch", help="Process a versioned JSON job request")
    batch.add_argument("request"); batch.add_argument("--fail-fast", action="store_true")
    return parser


def _pairs(values, indices=False):
    result = []
    for value in values:
        parts = value.split(":", 1)
        if len(parts) != 2 or any(not part for part in parts):
            raise InvocationError("Invalid pair argument.")
        if indices:
            if any(not part.isascii() or not part.isdecimal() for part in parts):
                raise InvocationError("Invalid face index argument.")
            parts = [int(part) for part in parts]
        result.append(parts)
    return result


def _read_request(path):
    def pairs(rows):
        result = {}
        for key, value in rows:
            if key in result:
                raise InvocationError("Duplicate request key.")
            result[key] = value
        return result
    def constant(value):
        raise InvocationError("Nonfinite request number.")
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs, parse_constant=constant)
    except (OSError, ValueError) as error:
        raise InvocationError("Cannot read a valid request.") from error


@contextmanager
def _interrupt(token):
    previous = signal.signal(signal.SIGINT, lambda signum, frame: token.request_stop())
    try:
        yield
    finally:
        signal.signal(signal.SIGINT, previous)


def _diagnostic(stream, message):
    try:
        stream.write(message + "\n"); stream.flush()
    except Exception:
        pass


def _deliver(report, stdout, stderr):
    from meshvale_reports import exit_code, validate
    validate(report)
    try:
        payload = json.dumps(report, ensure_ascii=True, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n"
        written = stdout.write(payload)
        if written is not None and written != len(payload):
            raise OSError("Incomplete report write")
        stdout.flush()
    except Exception as error:
        reason = "Report delivery failed: " + type(error).__name__
        if report["schema"] == "meshvale.batch/1":
            report["extensions"]["meshvale.repair.commands"]["report_delivery"] = {"outcome": "failed", "reason": reason}
            if report["execution"]["outcome"] != "cancelled":
                report["execution"] = {"outcome": "failed", "stage": "report", "reason": reason}
            _batch_exit(report)
        else:
            report["extensions"]["meshvale.repair.obj"]["report_delivery"] = {"outcome": "failed", "reason": reason}
            if report["execution"]["outcome"] != "cancelled":
                report["execution"] = {"outcome": "failed", "stage": "report", "reason": reason}
        _diagnostic(stderr, "Report delivery failed; completed publication facts are retained.")
        if stdout is sys.stdout:
            # A second flush during interpreter shutdown would override exit 2/130 with 120.
            try:
                descriptor = os.open(os.devnull, os.O_WRONLY)
                try:
                    os.dup2(descriptor, stdout.fileno())
                finally:
                    os.close(descriptor)
            except (OSError, ValueError, AttributeError):
                sys.stdout = None
    return exit_code(report)


def main(argv=None, *, stdout=None, stderr=None):
    """Command entry point; return its derived process exit without unsafe path diagnostics."""
    stdout, stderr = sys.stdout if stdout is None else stdout, sys.stderr if stderr is None else stderr
    try:
        options = _parser().parse_args(argv)
    except SystemExit as error:
        return error.code
    except InvocationError:
        _diagnostic(stderr, "Invalid invocation; no assets processed.")
        return 2
    try:
        import meshvale_interchange as io
        from .obj_workflow import repair_obj_file
        token = io.Cancellation()
        with _interrupt(token):
            if options.command == "obj":
                request = {"schema": "meshvale.repair.obj-jobs/1", "jobs": [{"id": options.input_id,
                    "input": options.input, "destination": options.destination, "targets": _pairs(options.target or [], True),
                    "resource_root": options.resource_root, "row_local_attributes": _pairs(options.row_local), "report_path": options.report_path}]}
                job = _jobs(request, Path.cwd(), token, None)[0]
                report = repair_obj_file(**job, cancellation=token).report
            else:
                path = Path(options.request)
                if not path.is_absolute():
                    path = Path.cwd() / path
                request = _read_request(path)
                report = run_obj_jobs(request, base=path.parent, fail_fast=options.fail_fast, cancellation=token)
            return _deliver(report, stdout, stderr)
    except InvocationError:
        _diagnostic(stderr, "Invalid invocation; no assets processed.")
    except ImportError:
        _diagnostic(stderr, "Asset runtime unavailable; install the declared assets extra and candidates.")
    except Exception as error:
        _diagnostic(stderr, "Command execution failed: " + type(error).__name__ + "; no success report fabricated.")
    return 2
