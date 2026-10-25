# SPDX-License-Identifier: Apache-2.0
"""Exercise an installed PLY API and commands against retained native fixtures.

Run with an isolated interpreter (-I). Inputs are read-only. The output root
must be absent, have an existing parent, and not overlap the fixture tree. This
qualified-profile test keeps every created bundle and command stream. It does
not install dependencies or establish a platform/support floor.

Literal maps, counts and wire schema are independent expectations. Comparing
serialized entry bytes with the matching native fixture is a parity check,
not an independent scalar-value decoder. No native result is substituted.
"""

import argparse
from contextlib import contextmanager
import copy
import gc
import hashlib
import importlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import sysconfig


ENCODINGS = ("ascii", "binary_little_endian", "binary_big_endian")
ENCODING_ENUMS = dict(zip(ENCODINGS, ("kAscii", "kBinaryLittleEndian", "kBinaryBigEndian")))
TYPE_WORDS = {"kChar": "char", "kUchar": "uchar", "kShort": "short",
              "kUshort": "ushort", "kInt": "int", "kUint": "uint",
              "kFloat": "float", "kDouble": "double"}
BINDING_WORDS = {"kX": "x", "kY": "y", "kZ": "z",
                 "kVertexIndices": "vertex-indices", "kAttribute": "attribute"}
MAP_PROPERTIES = {"FaceMap": "face_map", "CornerMap": "corner_map",
                  "SourceFaces": "source_faces", "SourceCorners": "source_corners"}
OBSERVATION_KEYS = frozenset((
    "capture_status", "capture_diagnostic", "workflow_status", "workflow_stage",
    "execution_outcome", "candidate_outcome", "profile_outcome", "publication_outcome",
    "exit_code", "source_counts", "candidate_counts", "face_map", "corner_map",
    "source_faces", "source_corners", "memory_output_verified", "published"))
MAX_INPUT_BYTES = 64 * 1024 * 1024
MAX_FIXTURE_FILES = 1024
REQUEST_SCHEMA = "meshvale.repair.ply-jobs/1"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read(path, cap=MAX_INPUT_BYTES):
    with path.open("rb") as stream:
        value = stream.read(cap + 1)
    require(len(value) <= cap, "bounded input: " + str(path))
    return value


def sha(value):
    return hashlib.sha256(value).hexdigest()


def write_json(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=True, indent=2, allow_nan=False)
        stream.write("\n")


def file_inventory(root):
    result = {}
    for directory, directories, files in os.walk(root, followlinks=False):
        for name in directories + files:
            require(not (Path(directory) / name).is_symlink(), "ordinary fixture entries only")
        for name in files:
            path = Path(directory) / name
            require(len(result) < MAX_FIXTURE_FILES, "bounded fixture inventory")
            result[path.relative_to(root).as_posix()] = sha(read(path))
    return result


def absolute(text):
    path = Path(text)
    require(path.is_absolute(), "explicit absolute path required: " + text)
    return path


def encoding(value):
    lines = value.split(b"\n", 2)
    require(len(lines) == 3 and lines[0].rstrip(b"\r") == b"ply", "physical PLY header")
    line = lines[1].rstrip(b"\r")
    found = [word for word in ENCODINGS if line == ("format " + word + " 1.0").encode("ascii")]
    require(len(found) == 1, "physical encoding declaration")
    return found[0]


def schema(views):
    require(views is not None, "actual typed schema available")
    return [{"kind": {"kVertex": "vertex", "kFace": "face"}[element.kind.name],
             "original_count": element.original_count,
             "properties": [{"name": prop.name,
                             "kind": {"kScalar": "scalar", "kList": "list"}[prop.kind.name],
                             "value_type": TYPE_WORDS[prop.value_type.name],
                             "count_type": None if prop.count_type is None else TYPE_WORDS[prop.count_type.name],
                             "binding": BINDING_WORDS[prop.binding.name],
                             "attribute": prop.attribute} for prop in element.properties]}
            for element in views]


def leaves(value, prefix=""):
    if type(value) is dict:
        return {key: leaf for name, item in value.items()
                for key, leaf in leaves(item, prefix + "." + name).items()}
    require(type(value) is int and value > 0, "positive exact integer preset leaf")
    return {prefix: value}


@contextmanager
def replacement(owner, name, value):
    """Narrow test seams without a mock retaining native arguments/results."""
    previous = getattr(owner, name)
    setattr(owner, name, value)
    try:
        yield
    finally:
        setattr(owner, name, previous)


class Integration:
    def __init__(self, args):
        self.args = args
        self.fixture = absolute(args.fixture_root)
        self.root = absolute(args.output_root)
        self.literal_path = absolute(args.literal_oracles)
        self.presets_path = absolute(args.presets)
        self.console = absolute(args.console) if args.console else Path(sys.executable).parent / (
            "meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
        require(sys.flags.isolated, "invoke this installed test with Python -I")
        require(self.fixture.is_dir() and self.root.parent.is_dir(), "existing input/output parent directories")
        source, output = self.fixture.resolve(strict=True), self.root.resolve(strict=False)
        require(not output.is_relative_to(source) and not source.is_relative_to(output),
                "output root must not overlap the native fixture tree")
        require(not self.root.exists() and not self.root.is_symlink(), "fresh absent output root")
        require(self.literal_path.is_file() and self.presets_path.is_file() and self.console.is_file(),
                "explicit oracle/preset files and installed console required")
        require(self.console.parent.resolve() == Path(sysconfig.get_path("scripts")).resolve(),
                "console entrypoint must belong to this interpreter's installed scripts directory")
        require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.native_version),
                "actual CMake native three-component version required")
        self.literal = json.loads(read(self.literal_path).decode("utf-8"))
        self.presets = json.loads(read(self.presets_path).decode("utf-8"))
        self.protected = file_inventory(self.fixture)
        self.input_hashes = {str(path): sha(read(path)) for path in (self.literal_path, self.presets_path)}
        self.cells = [(src, changed, dst) for src in ENCODINGS for changed in (False, True) for dst in ENCODINGS]
        # Preflight every native matrix file before creating any output.
        for src, changed, dst in self.cells:
            directory = self.fixture / self.cell_name(src, changed, dst)
            require(sha(read(directory / "captured.ply")) == self.literal["source_hashes"][src],
                    "literal source hash before any execution")
            require(encoding(read(directory / "bundle" / "source.ply")) == dst,
                    "retained native publication encoding")
        self.origins = {}
        for name in ("meshvale_repair", "meshvale_geometry", "meshvale_reports",
                     "meshvale_repair._repair", "meshvale_repair.ply_workflow",
                     "meshvale_repair.ply_commands"):
            module = importlib.import_module(name)
            origin = Path(module.__file__).resolve(strict=True)
            sites = [Path(sysconfig.get_path(key)).resolve() for key in ("purelib", "platlib")]
            require(any(origin.is_relative_to(site) for site in sites), "installed import only: " + name)
            self.origins[name] = {"path": str(origin), "sha256": sha(read(origin, 256 * 1024 * 1024))}
        self.workflow = importlib.import_module("meshvale_repair.ply_workflow")
        self.commands = importlib.import_module("meshvale_repair.ply_commands")
        self.reports = importlib.import_module("meshvale_reports")
        self.invocation_error = importlib.import_module("meshvale_repair.commands").InvocationError
        self.runtime = {"python": sys.version, "executable": sys.executable,
                        "meshvale-repair": version("meshvale-repair"),
                        "meshvale-geometry": version("meshvale-geometry"),
                        "console": str(self.console), "console_sha256": sha(read(self.console))}
        # All path, input and installed-origin guards above precede this mkdir.
        self.root.mkdir()
        self.events = []
        self.sources = {}
        inputs = self.root / "inputs"
        inputs.mkdir()
        for src in ENCODINGS:
            path = inputs / (src + ".ply")
            path.write_bytes(read(self.fixture / self.cell_name(src, True, "ascii") / "captured.ply"))
            self.sources[src] = path

    @staticmethod
    def cell_name(src, changed, dst):
        return src + ("-changed-" if changed else "-unchanged-") + dst

    def destination(self, name):
        return self.root / name

    def job(self, name, src="ascii", changed=True, dst="ascii"):
        return {"id": name, "input": str(self.sources[src]), "destination": str(self.destination(name)),
                "targets": self.target_pairs() if changed else [], "encoding": dst,
                "row_local_attributes": copy.deepcopy(self.literal["explicit_measurement_declarations"] if changed else [])}

    def target_pairs(self):
        return [[pair["keep"], pair["remove"]] for pair in self.literal["targets"]]

    @staticmethod
    def released(context):
        gc.collect()
        require(context.active_workers == 0 and context.active_tracked_payload == 0,
                "all actual workers joined and tracked owners released")

    def expected_schema(self, changed):
        value = copy.deepcopy(self.literal["original_schema"])
        for element in value:
            element["original_count"] = self.literal["changed" if changed else "unchanged"]["counts"][
                "vertices" if element["kind"] == "vertex" else "faces"]
        return value

    def report(self, payload, job, src, changed, published=True):
        result = self.reports.load(payload)
        require(self.reports.exit_code(result) == 0, "shared report exit semantics")
        require(result["tool"] == {"name": "meshvale-repair", "version": self.args.native_version},
                "native version is the supplied actual CMake identity, not the wheel version")
        require(result["input"] == {"id": job["id"], "sha256": self.literal["source_hashes"][src]},
                "real raw input identity")
        require(result["operation"]["options"] == {"encoding": job["encoding"], "rewrite": "canonical",
                "targets": [{"keep": keep, "remove": remove} for keep, remove in job["targets"]],
                "row_local_attributes": job["row_local_attributes"]},
                "exact explicit caller policies")
        expected = self.literal["changed" if changed else "unchanged"]
        ext = result["extensions"]["meshvale.repair.ply"]
        require(type(ext["version"]) is int and ext["version"] == 1, "native extension integer version")
        require(ext["source"]["schema"] == self.literal["original_schema"] and
                ext["source"]["encoding"] == src and
                ext["source"]["portable_name"] == Path(job["input"]).name, "original wire width/order/name provenance")
        require(ext["source_faces"] == expected["maps"]["SourceFaces"] and
                ext["source_corners"] == expected["maps"]["SourceCorners"], "literal inverse maps")
        require(ext["memory_output_verified"] is True, "actual memory verification")
        require(result["candidate"]["outcome"] == ("accepted" if changed else "unchanged"), "edit decision")
        require(result["publication"]["outcome"] == ("published" if published else "not-requested"),
                "true publication outcome")
        for domain, expected_rows in (("vertex", list(range(self.literal["source_counts"]["vertices"]))),
                                      ("face", expected["maps"]["FaceMap"]),
                                      ("corner", expected["maps"]["CornerMap"])):
            selected = [item for item in result["maps"] if item["domain"] == domain and
                        item["from"] == {"snapshot": "input", "mesh": "surface"} and
                        item["to"] == {"snapshot": "candidate", "mesh": "surface"}]
            require(len(selected) == 1, "one actual repair map per domain")
            mapping = selected[0]
            actual = list(range(len(expected_rows))) if mapping["kind"] == "identity" else [
                row[0] if type(row) is list and len(row) == 1 else row for row in mapping["entries"]]
            require(actual == expected_rows, "independent literal forward map: " + domain)
        require(len(result["maps"]) == (6 if published else 3), "exact selected map set")
        if published:
            for domain in ("vertex", "face", "corner"):
                selected = [item for item in result["maps"] if item["domain"] == domain and
                            item["from"] == {"snapshot": "candidate", "mesh": "surface"} and
                            item["to"] == {"snapshot": "output", "mesh": "surface"}]
                require(len(selected) == 1 and selected[0]["kind"] == "identity" and
                        selected[0]["entries"] is None, "actual export identity: " + domain)
        required = self.literal["publication_required_checks" if published else "memory_required_checks"]
        require(result["profile"]["required_checks"] == required, "ordered required coverage")
        if published:
            require(ext["native_stage"] == "complete" and ext["publisher"] is None and
                    ext["requested_bytes_before_report_copy"]["publication_files"] is None and
                    ext["requested_bytes_before_report_copy"]["publication_ply"] is None and
                    ext["phase_execution"]["publication"] is None, "conditional pre-return publication snapshot")
        for phase in ("repair", "writing", "reload"):
            require(ext["phase_diagnostics"][phase] is not None and
                    ext["phase_diagnostics"][phase]["status"] == 0, "actual successful called phase: " + phase)
        return result

    def bundle(self, job, src, changed, payload):
        destination = Path(job["destination"])
        name = Path(job["input"]).name
        require(sorted(path.name for path in destination.iterdir()) == sorted((name, "meshvale-report.json")),
                "complete flat physical publication inventory")
        entry = read(destination / name)
        require(encoding(entry) == job["encoding"], "independent physical encoding header")
        require(read(destination / "meshvale-report.json") == payload, "physical receipt equals delivered native bytes")
        # This is serialization parity, not a new scalar decoder or oracle.
        reference = self.fixture / self.cell_name(src, changed, job["encoding"]) / "bundle" / "source.ply"
        require(entry == read(reference), "same-matrix native entry byte parity")
        report = self.report(payload, job, src, changed)
        require(report["extensions"]["meshvale.repair.ply"]["artifact_paths"] ==
                {"entry": name, "receipt": "meshvale-report.json"}, "portable committed paths")
        artifacts = report["publication"]["artifacts"]
        require(len(artifacts) == 1 and artifacts[0]["sha256"] == sha(entry), "physical entry digest")

    def api(self, job, src, changed, context):
        result = self.workflow.repair_ply_file(job["input"], job["destination"], job["targets"],
                    encoding=job["encoding"], row_local_attributes=job["row_local_attributes"],
                    input_id=job["id"], execution=context)
        self.events.append({"route": "api", "id": job["id"], "native_exit": result.exit_code})
        require(result.workflow_started and result.status.name == "kSuccess" and result.exit_code == 0 and
                result.memory_output_verified, "genuine native completed workflow")
        expected = self.literal["changed" if changed else "unchanged"]
        require(result.input_snapshot_counts == tuple(self.literal["source_counts"][key] for key in ("vertices", "faces", "corners")) and
                result.candidate_snapshot_counts == tuple(expected["counts"][key] for key in ("vertices", "faces", "corners")),
                "literal retained counts")
        for key, prop in MAP_PROPERTIES.items():
            require(list(getattr(result, prop)) == expected["maps"][key], "literal native " + key)
        require(result.original_bytes == read(Path(job["input"])) and
                result.input_sha256 == self.literal["source_hashes"][src], "owned original bytes and real digest")
        require(schema(result.source_schema) == self.literal["original_schema"], "typed original schema")
        require(schema(result.reload_schema) == self.expected_schema(changed) and
                schema(result.published_schema) == self.expected_schema(changed), "actual reloaded current schema")
        require(result.source_encoding.name == ENCODING_ENUMS[src] and
                result.reload_encoding.name == ENCODING_ENUMS[job["encoding"]] and
                result.published_encoding.name == ENCODING_ENUMS[job["encoding"]], "three genuine Encoding getters")
        facts = result.write_facts
        require(facts.source_vertex_count == 8 and facts.source_face_count == 3 and
                facts.vertex_count == expected["counts"]["vertices"] and
                facts.face_count == expected["counts"]["faces"] and
                facts.corner_count == expected["counts"]["corners"] and facts.property_count == 14 and
                facts.encoding.name == ENCODING_ENUMS[job["encoding"]] and
                facts.source_encoding.name == ENCODING_ENUMS[src] and facts.rewrite_policy.name == "kCanonical" and
                facts.correspondence.name == "kIdentity", "retained actual writer facts")
        payload = result.report_json
        require(result.output_bytes == read(Path(job["destination"]) / Path(job["input"]).name),
                "actual memory bytes equal physical publication")
        require(result.publication.outcome.name == "kPublished", "final typed commit facts")
        self.bundle(job, src, changed, payload)
        del facts, result
        self.released(context)

    def presets_and_matrix(self):
        resolved = {}
        for preset in ("balanced-v1", "serial-reference-v1"):
            context = self.workflow.PlyExecution(preset=preset)
            require(context.preset == preset and context.resolved_preset == self.presets[preset], "215 native preset leaves")
            resolved[preset] = leaves(context.resolved_preset)
            require(len(resolved[preset]) == 215, "selected complete native leaf count")
            detached = context.resolved_preset
            detached["execution"]["worker_budget"] = 999
            require(context.resolved_preset == self.presets[preset], "resolved preset is a fresh detached value")
            cells = self.cells if preset == "balanced-v1" else [("ascii", True, "binary_big_endian"),
                                                                  ("binary_big_endian", False, "ascii")]
            for src, changed, dst in cells:
                self.api(self.job(preset + "-" + self.cell_name(src, changed, dst), src, changed, dst),
                         src, changed, context)
        require(sum(resolved["balanced-v1"][key] != resolved["serial-reference-v1"][key]
                    for key in resolved["balanced-v1"]) == 24, "exact scheduling-only preset differences")

    def process(self, route, arguments, label):
        command = ([sys.executable, "-I", "-m", "meshvale_repair"] if route == "module" else [str(self.console)])
        completed = subprocess.run(command + arguments, cwd=self.root, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, timeout=90, check=False)
        (self.root / (label + ".stdout")).write_bytes(completed.stdout)
        (self.root / (label + ".stderr")).write_bytes(completed.stderr)
        self.events.append({"route": route, "id": label, "returncode": completed.returncode})
        require(completed.returncode == 0, "actual installed " + route + " command: " + label)
        return completed.stdout

    def command_matrix(self):
        for route in ("module", "console"):
            for src, changed, dst in self.cells:
                job = self.job(route + "-single-" + self.cell_name(src, changed, dst), src, changed, dst)
                arguments = ["ply", job["input"], job["destination"], "--encoding", dst, "--input-id", job["id"]]
                arguments += ["--target", "0:1"] if changed else ["--unchanged"]
                for declaration in job["row_local_attributes"]:
                    arguments += ["--row-local", declaration["domain"] + ":" + declaration["name"]]
                stdout = self.process(route, arguments, job["id"])
                require(stdout.endswith(b"\n"), "single command report framing")
                self.bundle(job, src, changed, stdout[:-1])
            # Each genuine batch stays within the public 16-job admission cap.
            for group, cells in enumerate((self.cells[:9], self.cells[9:])):
                jobs = [self.job(route + "-batch-" + self.cell_name(src, changed, dst), src, changed, dst)
                        for src, changed, dst in cells]
                request = self.root / (route + "-batch-" + str(group) + ".json")
                write_json(request, {"schema": REQUEST_SCHEMA, "jobs": jobs})
                stdout = self.process(route, ["ply-batch", str(request)], route + "-batch-" + str(group))
                batch = self.reports.load(stdout)
                require(batch["exit_code"] == self.reports.exit_code(batch) == 0 and
                        [row["id"] for row in batch["jobs"]] == [job["id"] for job in jobs], "ordered batch delivery")
                for row, job, (src, changed, _) in zip(batch["jobs"], jobs, cells):
                    require(row["outcome"] == "attempted", "each native batch job actually attempted")
                    self.events.append({"route": route + "-batch-job", "id": job["id"], "observed": "attempted"})
                    physical = read(Path(job["destination"]) / "meshvale-report.json")
                    require(physical in stdout, "batch transports the literal physical native receipt bytes")
                    require(self.reports.load(physical) == row["report"], "batch embeds the actual native report")
                    self.bundle(job, src, changed, physical)

    def declarations_and_lifetime(self):
        context = self.workflow.PlyExecution()
        source = str(self.sources["ascii"])
        declarations = copy.deepcopy(self.literal["explicit_measurement_declarations"])
        unused = declarations + [{"domain": "corner", "name": "unused_measurement"}]
        result = self.workflow.repair_ply_file(source, None, self.target_pairs(), encoding="ascii",
                                             row_local_attributes=unused, execution=context)
        self.events.append({"route": "api-control", "id": "valid-unused-corner", "native_exit": result.exit_code})
        require(result.exit_code == 0 and list(result.face_map) == self.literal["changed"]["maps"]["FaceMap"],
                "valid unused corner declaration permits the actual edit")
        prop = result.source_schema[0].properties[0]
        require(prop.name == self.literal["original_schema"][0]["properties"][0]["name"], "actual lazy schema property")
        del result
        gc.collect()
        require(prop.name == "sample_i8" and context.active_workers == 0 and context.active_tracked_payload > 0,
                "schema property retains its actual source document after workflow result release")
        try:
            prop.name = "rewritten"
        except AttributeError:
            pass
        else:
            raise AssertionError("schema property must be read-only")
        del prop
        self.released(context)
        for targets, supplied, expected in ((self.target_pairs(), [], 1), ([], [], 0)):
            result = self.workflow.repair_ply_file(source, None, targets, encoding="ascii",
                                                 row_local_attributes=supplied, execution=context)
            self.events.append({"route": "api-control", "id": "declarations-" + str(expected), "native_exit": result.exit_code})
            require(result.exit_code == expected, "real engine missing-declaration versus empty-target path")
            if targets:
                require(result.outcomes.candidate.name == "rejected" and result.output_bytes is None,
                        "whole request rejected without output")
            else:
                require(list(result.face_map) == self.literal["unchanged"]["maps"]["FaceMap"], "real unchanged maps")
            del result
            self.released(context)
        for bad in (declarations + [declarations[0]], [{"domain": "corner", "name": "invalid-name"}]):
            try:
                self.workflow.repair_ply_file(source, None, [], encoding="ascii", row_local_attributes=bad, execution=context)
            except (TypeError, ValueError):
                pass
            else:
                raise AssertionError("duplicate/invalid declarations must fail admission")
            self.released(context)

    def admission_and_scheduler(self):
        context = self.workflow.PlyExecution()
        valid = self.job("admission-first")
        bad = self.job("admission-bad-later")
        bad["row_local_attributes"] = [{"domain": "face", "name": "bad-name"}]
        calls = []
        run = self.workflow._run_ply_request
        def witness(prepared):
            calls.append(1)
            (self.root / "unexpected-admission-run.txt").write_text("native run reached\n", encoding="utf-8")
            return run(prepared)
        with replacement(self.workflow, "_run_ply_request", witness):
            try:
                self.commands.run_ply_jobs({"schema": REQUEST_SCHEMA, "jobs": [valid, bad]}, execution=context)
            except self.invocation_error:
                pass
            else:
                raise AssertionError("bad later row must refuse the complete invocation")
        require(not calls and not Path(valid["destination"]).exists(), "admission before any real capture/run side effect")
        self.released(context)
        token = self.workflow.PlyCancellation()
        require(token.cancel() and token.cancelled, "genuine cancellation source")
        canceled = self.commands.run_ply_jobs({"schema": REQUEST_SCHEMA, "jobs": []}, execution=context, cancellation=token)
        require(canceled.exit_code == 130 and self.reports.load(canceled.report_json)["jobs"] == [],
                "empty batch cancellation boundary without native jobs")
        del canceled
        self.released(context)
        for fail_fast in (False, True):
            missing = self.job("missing-" + str(fail_fast))
            missing["input"] = str(self.root / "inputs" / "absent.ply")
            next_job = self.job("after-missing-" + str(fail_fast))
            result = self.commands.run_ply_jobs({"schema": REQUEST_SCHEMA, "jobs": [missing, next_job]},
                                              fail_fast=fail_fast, execution=context)
            batch = self.reports.load(result.report_json)
            self.events.append({"route": "scheduler", "id": "fail-fast-" + str(fail_fast), "exit": result.exit_code})
            require(result.exit_code == 2 and batch["jobs"][0]["outcome"] == "attempted", "real missing-file attempt")
            fallback = batch["jobs"][0]["report"]
            ext = fallback["extensions"]["meshvale.repair.ply.command"]
            require(type(ext["version"]) is int and ext["version"] == 1 and set(ext) == {"version", "observations"},
                    "command projection integer version and exact object")
            obs = ext["observations"]
            require(set(obs) == OBSERVATION_KEYS and obs["published"] is False and
                    type(obs["capture_status"]) is int and obs["capture_status"] != 0 and
                    obs["capture_diagnostic"] is not None, "exact17 actual missing-file observations")
            require(all(obs[key] is None for key in ("source_counts", "candidate_counts", "face_map", "corner_map",
                    "source_faces", "source_corners")) and fallback["maps"] == [] and
                    fallback["publication"]["artifacts"] == [], "no invented missing-source maps/artifacts")
            require(not Path(missing["destination"]).exists(), "missing source cannot publish")
            require(batch["jobs"][1]["outcome"] == ("not-attempted" if fail_fast else "attempted"), "ordered stop policy")
            for row in batch["jobs"]:
                self.events.append({"route": "scheduler-job", "id": row["id"], "observed": row["outcome"]})
            if fail_fast:
                require(batch["jobs"][1]["report"] is None and not Path(next_job["destination"]).exists(), "no later publication")
            else:
                self.bundle(next_job, "ascii", True, read(Path(next_job["destination"]) / "meshvale-report.json"))
            del result
            self.released(context)

    def delivery_faults(self):
        # Injection changes only reading receipt bytes after genuine publication.
        for invalid in (False, True):
            for cancel in (False, True):
                context = self.workflow.PlyExecution()
                token = self.workflow.PlyCancellation()
                job = self.job("wire-fault-" + str(invalid) + "-" + str(cancel))
                observed = []
                def getter(result):
                    require(result.publication.outcome.name == "kPublished" and result.exit_code == 0,
                            "fault seam entered only after actual successful native commit")
                    observed.append({"native_exit": result.exit_code, "receipt": result.report_json})
                    if cancel:
                        token.cancel()
                    return b"{" if invalid else None
                with replacement(self.commands, "_native_receipt", getter):
                    try:
                        self.commands._run_ply_single(job, execution=context, cancellation=token)
                    except self.commands.PlyCommandDeliveryError as error:
                        require(error.exit_code == (130 if cancel else 2), "typed delivery failure with no aggregate")
                    else:
                        raise AssertionError("published missing/invalid receipt must fail delivery")
                require(len(observed) == 1, "one actual native result at the receipt read boundary")
                self.bundle(job, "ascii", True, observed[0]["receipt"])
                self.events.append({"route": "injected-byte-read-fault", "id": job["id"],
                                    "native_exit": 0, "delivery_exit": 130 if cancel else 2})
                self.released(context)

    def run(self):
        passed, error = False, None
        try:
            self.presets_and_matrix()
            self.command_matrix()
            self.declarations_and_lifetime()
            self.admission_and_scheduler()
            self.delivery_faults()
            passed = True
        except Exception as exc:
            error = {"type": type(exc).__name__, "message": str(exc)}
        finally:
            unchanged = (file_inventory(self.fixture) == self.protected and all(
                sha(read(Path(path))) == digest for path, digest in self.input_hashes.items()) and all(
                sha(read(path)) == self.literal["source_hashes"][src] for src, path in self.sources.items()))
            passed = passed and unchanged
            summary = {"schema": "meshvale.repair.ply-python-tests/1", "passed": passed,
                       "inputs_unchanged": unchanged, "error": error, "origins": self.origins,
                       "runtime": self.runtime, "input_sha256": self.input_hashes, "events": self.events,
                       "planned_scopes": {"balanced_api_matrix": 18, "serial_api_parity": 2,
                                  "single_cells_per_command_route": 18, "batch_cells_per_command_route": 18,
                                  "entry_bytes": "same-matrix native parity; independent literal maps/counts/schema",
                                  "native_results_substituted": False}}
            write_json(self.root / "integration-results.json", summary)
            print(json.dumps({"passed": passed, "events": len(self.events), "error": error}, ensure_ascii=True))
        return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-root", required=True, help="Retained native --write-reports root (read-only)")
    parser.add_argument("--output-root", required=True, help="Absent owned output directory with existing parent")
    parser.add_argument("--literal-oracles", required=True, help="Public literal-predicates.json")
    parser.add_argument("--presets", required=True, help="Public docs/ply-presets.json")
    parser.add_argument("--native-version", required=True, help="Actual CMake project version of this installed build")
    parser.add_argument("--console", help="Matching installed meshvale-repair executable (default: beside Python)")
    return Integration(parser.parse_args()).run()


if __name__ == "__main__":
    raise SystemExit(main())
