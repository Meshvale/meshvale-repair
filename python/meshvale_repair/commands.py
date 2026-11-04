# SPDX-License-Identifier: Apache-2.0
"""Validated file commands using the owning native workflows."""
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
    from .obj_workflow import ObjCancellation, _validate_obj_request
    from ._obj_export import resolve_export
    import meshvale_interchange as io
    if (type(request) is not dict or set(request) != {"schema", "jobs"} or
            request["schema"] not in ("meshvale.repair.obj-jobs/1", "meshvale.repair.obj-jobs/2") or type(request["jobs"]) is not list):
        raise InvocationError("Invalid batch request.")
    if token is not None and not isinstance(token, (io.Cancellation, ObjCancellation)):
        raise InvocationError("Invalid cancellation token.")
    if phase is not None and not callable(phase):
        raise InvocationError("Invalid phase callback.")
    required = {"id", "input", "destination", "targets"}
    optional = {"resource_root", "row_local_attributes", "report_path"}
    version_two = request["schema"] == "meshvale.repair.obj-jobs/2"
    if version_two:
        optional |= {"output_topology", "triangulation"}
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
            job = {"input": source, "destination": destination, "targets": targets,
                   "input_id": row["id"], "resource_root": root, "row_local_attributes": declarations, "report_path": receipt}
            if version_two:
                topology = row.get("output_topology", "preserve")
                options = execution = None
                if "triangulation" in row:
                    settings = row["triangulation"]
                    fields = {"max_corners_per_face", "minimum_parallel_faces", "worker_budget", "tracked_payload_budget_bytes"}
                    if (topology != "triangles" or type(settings) is not dict or set(settings) - fields or
                            any(type(value) is not int for value in settings.values())):
                        raise InvocationError("Invalid triangulation settings.")
                    from meshvale_geometry import ExecutionContext, TriangulationOptions
                    options = TriangulationOptions(**{name: value for name, value in settings.items()
                                                       if name in {"max_corners_per_face", "minimum_parallel_faces"}})
                    execution = ExecutionContext(**{name: value for name, value in settings.items()
                                                     if name in {"worker_budget", "tracked_payload_budget_bytes"}})
                resolved = resolve_export(topology, options, execution, token)
                if resolved is not None:
                    job.update(output_topology="triangles", triangulation_options=resolved.options,
                               triangulation_execution=resolved.execution)
            jobs.append(job)
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
    from .obj_workflow import ObjCancellation, repair_obj_file
    if type(fail_fast) is not bool:
        raise InvocationError("fail_fast must be boolean.")
    jobs = _jobs(request, Path.cwd() if base is None else base, cancellation, on_job_phase)
    if cancellation is None:
        token = ObjCancellation() if any(job.get("output_topology") == "triangles" for job in jobs) else io.Cancellation()
    else:
        token = cancellation
    batch = {"schema": "meshvale.batch/1", "tool": {"name": "meshvale-repair", "version": __version__},
        "mode": "fail-fast" if fail_fast else "continue",
        "execution": {"outcome": "completed", "stage": "complete", "reason": None},
        "jobs": [{"id": job["input_id"], "input": {"id": job["input_id"], "sha256": None},
                  "outcome": "not-attempted", "report": None, "reason": "Scheduling not reached."} for job in jobs],
        "exit_code": 0, "extensions": {"meshvale.repair.commands": {"version": "1"}}}
    if request["schema"] == "meshvale.repair.obj-jobs/2":
        batch["extensions"]["meshvale.repair.commands"] = {"version": "2", "request_schema": request["schema"]}
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
    parser = _Parser(prog="meshvale-repair", description="Verified targeted duplicate repair")
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
    single.add_argument("--output-topology", choices=("preserve", "triangles"), default="preserve")
    single.add_argument("--triangulation-max-corners")
    single.add_argument("--triangulation-minimum-parallel-faces")
    single.add_argument("--triangulation-workers")
    single.add_argument("--triangulation-payload-bytes")
    batch = commands.add_parser("obj-batch", help="Process a versioned JSON job request")
    batch.add_argument("request"); batch.add_argument("--fail-fast", action="store_true")
    gltf = commands.add_parser("gltf", help="Repair one explicit glTF primitive into a new bundle")
    gltf.add_argument("input"); gltf.add_argument("destination")
    gltf.add_argument("--primitive-ordinal", required=True)
    gltf.add_argument("--triangles", required=True,
                      choices=("require-triangles", "convert-strip-or-fan"))
    gltf.add_argument("--values", required=True, choices=("raw", "raw-and-normalized"))
    gltf.add_argument("--processing-profile", default="static-local-triangles-v1",
                      choices=("static-local-triangles-v1", "direct-translation-v1", "deformation-retained-v1"))
    selection = gltf.add_mutually_exclusive_group(required=True)
    selection.add_argument("--target", action="append", metavar="KEEP:REMOVE")
    selection.add_argument("--unchanged", action="store_true")
    gltf.add_argument("--input-id", default="source")
    gltf.add_argument("--resource-root")
    gltf.add_argument("--row-local", action="append", default=[], metavar="DOMAIN:NAME")
    gltf.add_argument("--report-path", default="receipts/report.json")
    gltf_batch = commands.add_parser("gltf-batch", help="Process a versioned explicit glTF job request")
    gltf_batch.add_argument("request")
    gltf_batch.add_argument("--fail-fast", action="store_true")
    off = commands.add_parser("off", help="Repair indexed OFF polygons into a verified new bundle")
    off.add_argument("input"); off.add_argument("destination")
    off.add_argument("--text", required=True, choices=("canonical-ascii",))
    off.add_argument("--edge-count", required=True, choices=("unknown-zero",))
    off_selection = off.add_mutually_exclusive_group(required=True)
    off_selection.add_argument("--target", action="append", metavar="KEEP:REMOVE")
    off_selection.add_argument("--unchanged", action="store_true")
    off.add_argument("--input-id", default="source")
    off.add_argument("--report-path", default="meshvale-report.json")
    off_batch = commands.add_parser("off-batch", help="Process a versioned indexed OFF job request")
    off_batch.add_argument("request")
    off_batch.add_argument("--fail-fast", action="store_true")
    ply = commands.add_parser("ply", help="Repair typed PLY polygons into a verified new bundle")
    ply.add_argument("input"); ply.add_argument("destination")
    ply.add_argument("--encoding", required=True,
                     choices=("ascii", "binary_little_endian", "binary_big_endian"))
    ply_selection = ply.add_mutually_exclusive_group(required=True)
    ply_selection.add_argument("--target", action="append", metavar="KEEP:REMOVE")
    ply_selection.add_argument("--unchanged", action="store_true")
    ply.add_argument("--row-local", action="append", default=[], metavar="DOMAIN:NAME")
    ply.add_argument("--input-id", default="source")
    ply.add_argument("--report-path", default="meshvale-report.json")
    ply_batch = commands.add_parser("ply-batch", help="Process a versioned typed PLY job request")
    ply_batch.add_argument("request")
    ply_batch.add_argument("--fail-fast", action="store_true")
    for command in (gltf, gltf_batch):
        command.add_argument("--preset",
                             choices=("balanced-v1", "serial-reference-v1",
                                      "balanced-direct-translation-v1", "serial-direct-translation-v1",
                                      "balanced-deformation-v1", "serial-deformation-v1"))
    for command in (off, off_batch, ply, ply_batch):
        command.add_argument("--preset", default="balanced-v1",
                             choices=("balanced-v1", "serial-reference-v1"))
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


def _quiet_shutdown_stdout(stdout):
    if stdout is sys.stdout:
        # A second failed shutdown flush would replace the chosen exit with120.
        try:
            descriptor = os.open(os.devnull, os.O_WRONLY)
            try:
                os.dup2(descriptor, stdout.fileno())
            finally:
                os.close(descriptor)
        except (OSError, ValueError, AttributeError):
            sys.stdout = None


def _deliver_gltf(result, stdout, stderr, cancellation=None):
    try:
        payload = result.report_json + b"\n"
        if stdout is sys.stdout and getattr(stdout, "buffer", None) is not None:
            target, data = stdout.buffer, payload
        else:
            target, data = stdout, payload.decode("utf-8")
        if target.write(data) != len(data):
            raise OSError("Incomplete report write")
        target.flush()
    except Exception:
        _diagnostic(stderr, "Report delivery failed; completed publication facts are retained.")
        _quiet_shutdown_stdout(stdout)
        return 130 if result.exit_code == 130 or cancellation is not None and cancellation.cancelled else 2
    return result.exit_code


@contextmanager
def _gltf_interrupt(token):
    previous = signal.signal(signal.SIGINT, lambda signum, frame: token.cancel())
    try:
        yield
    finally:
        signal.signal(signal.SIGINT, previous)


def _main_gltf(options, stdout, stderr):
    from .gltf_commands import (
        GltfCommandDeliveryError, _read_gltf_request, _run_gltf_single, run_gltf_jobs,
    )
    from .gltf_workflow import GltfCancellation, GltfExecution
    token = GltfCancellation()
    execution = None if options.preset is None else GltfExecution(preset=options.preset)
    try:
        with _gltf_interrupt(token):
            if options.command == "gltf":
                ordinal = options.primitive_ordinal
                if not ordinal.isascii() or not ordinal.isdecimal():
                    raise InvocationError("Invalid primitive ordinal.")
                try:
                    ordinal = int(ordinal)
                except ValueError as error:
                    raise InvocationError("Invalid primitive ordinal.") from error
                job = {"id": options.input_id, "input": options.input,
                       "destination": options.destination,
                       "targets": _pairs(options.target or [], True),
                       "primitive_ordinal": ordinal, "triangles": options.triangles,
                       "values": options.values, "resource_root": options.resource_root,
                       "row_local_attributes": _pairs(options.row_local),
                       "report_path": options.report_path}
                result = _run_gltf_single(job, base=Path.cwd(), execution=execution,
                                           cancellation=token, processing_profile=options.processing_profile)
            else:
                path = Path(options.request)
                if not path.is_absolute():
                    path = Path.cwd() / path
                request = _read_gltf_request(path)
                result = run_gltf_jobs(request, base=path.parent, fail_fast=options.fail_fast,
                                       execution=execution, cancellation=token)
            return _deliver_gltf(result, stdout, stderr)
    except GltfCommandDeliveryError as error:
        _diagnostic(stderr, "Report delivery failed; completed publication facts are retained.")
        return error.exit_code


def _main_off(options, stdout, stderr):
    from .off_commands import (
        OffCommandDeliveryError, _path as _off_path, _read_off_request,
        _run_off_single, run_off_jobs,
    )
    from .off_workflow import OffCancellation, OffExecution
    token = OffCancellation()
    execution = OffExecution(preset=options.preset)
    try:
        with _gltf_interrupt(token):
            if options.command == "off":
                job = {"id": options.input_id, "input": options.input,
                       "destination": options.destination,
                       "targets": _pairs(options.target or [], True),
                       "text": options.text, "edge_count": options.edge_count,
                       "report_path": options.report_path}
                result = _run_off_single(job, base=Path.cwd(), execution=execution,
                                         cancellation=token)
            else:
                path = _off_path(options.request, os.getcwd())
                request = _read_off_request(path)
                result = run_off_jobs(request, base=os.path.dirname(path), fail_fast=options.fail_fast,
                                      execution=execution, cancellation=token)
            return _deliver_gltf(result, stdout, stderr, token)
    except OffCommandDeliveryError as error:
        _diagnostic(stderr, "Report delivery failed; completed publication facts are retained.")
        return error.exit_code


def _main_ply(options, stdout, stderr):
    from .ply_commands import (
        PlyCommandDeliveryError, _path as _ply_path, _read_ply_request,
        _run_ply_single, run_ply_jobs,
    )
    from .ply_workflow import PlyCancellation, PlyExecution
    token = PlyCancellation()
    execution = PlyExecution(preset=options.preset)
    try:
        with _gltf_interrupt(token):
            if options.command == "ply":
                declarations = [dict(domain=domain, name=name)
                                for domain, name in _pairs(options.row_local)]
                job = {"id": options.input_id, "input": options.input,
                       "destination": options.destination,
                       "targets": _pairs(options.target or [], True),
                       "encoding": options.encoding,
                       "row_local_attributes": declarations,
                       "report_path": options.report_path}
                result = _run_ply_single(job, base=Path.cwd(), execution=execution,
                                        cancellation=token)
            else:
                path = _ply_path(options.request, os.getcwd())
                request = _read_ply_request(path)
                result = run_ply_jobs(request, base=os.path.dirname(path),
                                      fail_fast=options.fail_fast,
                                      execution=execution, cancellation=token)
            return _deliver_gltf(result, stdout, stderr, token)
    except PlyCommandDeliveryError as error:
        _diagnostic(stderr, "Report delivery failed; completed publication facts are retained.")
        return error.exit_code


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
        _quiet_shutdown_stdout(stdout)
    return exit_code(report)


def _main_obj(options, stdout, stderr):
    import meshvale_interchange as io
    from .obj_workflow import ObjCancellation, repair_obj_file
    if options.command == "obj":
        row = {"id": options.input_id, "input": options.input, "destination": options.destination,
               "targets": _pairs(options.target or [], True), "resource_root": options.resource_root,
               "row_local_attributes": _pairs(options.row_local), "report_path": options.report_path}
        settings = {}
        for name, value in (("max_corners_per_face", options.triangulation_max_corners),
                            ("minimum_parallel_faces", options.triangulation_minimum_parallel_faces),
                            ("worker_budget", options.triangulation_workers),
                            ("tracked_payload_budget_bytes", options.triangulation_payload_bytes)):
            if value is not None:
                if not value.isascii() or not value.isdecimal():
                    raise InvocationError("Invalid triangulation integer argument.")
                try:
                    settings[name] = int(value)
                except (ValueError, OverflowError) as error:
                    raise InvocationError("Invalid triangulation integer argument.") from error
        version_two = options.output_topology != "preserve" or bool(settings)
        if version_two:
            row["output_topology"] = options.output_topology
            if settings:
                row["triangulation"] = settings
        request = {"schema": "meshvale.repair.obj-jobs/2" if version_two else "meshvale.repair.obj-jobs/1", "jobs": [row]}
        directory = Path.cwd()
    else:
        path = Path(options.request)
        if not path.is_absolute():
            path = Path.cwd() / path
        request = _read_request(path)
        directory = path.parent
    token = ObjCancellation() if type(request) is dict and request.get("schema") == "meshvale.repair.obj-jobs/2" else io.Cancellation()
    with _interrupt(token):
        if options.command == "obj":
            job = _jobs(request, directory, token, None)[0]
            report = repair_obj_file(**job, cancellation=token).report
        else:
            report = run_obj_jobs(request, base=directory, fail_fast=options.fail_fast, cancellation=token)
        return _deliver(report, stdout, stderr)


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
        if options.command in {"gltf", "gltf-batch"}:
            return _main_gltf(options, stdout, stderr)
        if options.command in {"off", "off-batch"}:
            return _main_off(options, stdout, stderr)
        if options.command in {"ply", "ply-batch"}:
            return _main_ply(options, stdout, stderr)
        return _main_obj(options, stdout, stderr)
    except InvocationError:
        _diagnostic(stderr, "Invalid invocation; no assets processed.")
    except ImportError:
        if options.command in {"gltf", "gltf-batch", "off", "off-batch", "ply", "ply-batch"}:
            _diagnostic(stderr, "Report runtime unavailable; install the declared workflow extra and candidates.")
        else:
            _diagnostic(stderr, "Asset runtime unavailable; install the declared assets extra and candidates.")
    except Exception as error:
        _diagnostic(stderr, "Command execution failed: " + type(error).__name__ + "; no success report fabricated.")
    return 2
