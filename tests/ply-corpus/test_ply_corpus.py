# SPDX-License-Identifier: Apache-2.0
"""Keep original mixed-polygon PLY outcomes from installed APIs and commands."""
import argparse
from collections import Counter
import gc
import hashlib
import importlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import sysconfig


verify_mixed_ply = runpy.run_path(str(Path(__file__).with_name("mixed_physical_oracle.py")))["verify_mixed_ply"]
ENCODINGS = ("ascii", "binary_little_endian", "binary_big_endian")
SHORT = dict(zip(ENCODINGS, ("a", "le", "be")))
LITERAL_SHA256 = "b321d133ede4533b28a9158412770bcd08f17319a23c7154af667caa0cf0bdb2"
REQUIRED_CHECKS = ("source-storage", "target-policy", "candidate-storage", "repair-correspondence",
                   "canonical-preservation", "reload-candidate", "source-unchanged", "typed-properties",
                   "wire-schema", "staged-candidate", "staged-physical-members")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read(path, limit=128 * 1024):
    with Path(path).open("rb") as stream:
        raw = stream.read(limit + 1)
    require(len(raw) <= limit, "bounded file read")
    return raw


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def inventory(root):
    result = {}
    for path in sorted(root.rglob("*")):
        require(not path.is_symlink(), "ordinary retained files only")
        if path.is_file():
            result[path.relative_to(root).as_posix()] = sha(read(path))
    return result


def write_json(path, data):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2, ensure_ascii=True, allow_nan=False)
        stream.write("\n")


class Corpus:
    def __init__(self, args):
        require(sys.flags.isolated, "run with an isolated installed interpreter (-I)")
        self.args = args
        self.fixture = args.fixture_root.resolve(strict=True)
        self.root = args.output_root.resolve()
        require(not self.root.exists() and self.root.parent.is_dir(), "fresh output with existing parent")
        require(not self.root.is_relative_to(self.fixture) and not self.fixture.is_relative_to(self.root),
                "output and fixture do not overlap")
        require(not self.root.is_relative_to(Path(__file__).resolve().parent), "output outside tests")
        self.console = (args.console or Path(sysconfig.get_path("scripts")) / (
            "meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")).resolve(strict=True)
        require(self.console.parent == Path(sysconfig.get_path("scripts")).resolve(), "console belongs to interpreter")
        require(sha(read(self.fixture / "literal-expectations.json")) == LITERAL_SHA256,
                "original literal expectation identity")
        self.literal = json.loads(read(self.fixture / "literal-expectations.json"))
        require(self.literal["schema"] == "meshvale.test.ply-mixed-polygons/1", "literal schema")
        self.cases = {case["id"]: case for case in self.literal["cases"]}
        require(len(self.cases) == 12 and len(self.literal["assets"]) == 27, "closed case/data inventory")
        self.protected = inventory(self.fixture)
        self.wires = {}
        for asset in self.literal["assets"]:
            raw = read(self.fixture / asset["file"], 4096)
            raw = raw if asset["encoding"] == "ascii" else bytes.fromhex(raw.decode("ascii"))
            require(len(raw) == asset["bytes"] and sha(raw) == asset["sha256"], "original authored wire identity")
            self.wires[(asset["variant"], asset["encoding"])] = raw
        for case in self.cases.values():
            for encoding in ENCODINGS:
                verify_mixed_ply(self.wires[(case["wire_variant"], encoding)], case, encoding, source=True)
        self.origins = {}
        for name in ("meshvale_repair", "meshvale_repair._repair", "meshvale_repair.ply_workflow",
                     "meshvale_repair.ply_commands", "meshvale_geometry", "meshvale_reports"):
            module = importlib.import_module(name)
            path = Path(module.__file__).resolve()
            require(path.is_relative_to(Path(sys.prefix).resolve()), "installed import: " + name)
            self.origins[name] = {"path": str(path), "sha256": sha(read(path, 256 * 1024 * 1024))}
        self.workflow = importlib.import_module("meshvale_repair.ply_workflow")
        self.reports = importlib.import_module("meshvale_reports")
        self.runtime = {"python": sys.version, "executable": sys.executable,
                        "meshvale-repair": version("meshvale-repair"), "meshvale-geometry": version("meshvale-geometry"),
                        "console": str(self.console), "console_sha256": sha(read(self.console))}
        self.root.mkdir()
        (self.root / "inputs").mkdir()
        (self.root / "bundles").mkdir()
        self.sources = {}
        for (variant, encoding), raw in self.wires.items():
            path = self.root / "inputs" / (variant + "-" + SHORT[encoding] + ".ply")
            path.write_bytes(raw)
            self.sources[(variant, encoding)] = path
        self.events, self.physical, self.processes = [], [], []

    def released(self, context):
        gc.collect()
        require(context.active_workers == 0 and context.active_tracked_payload == 0,
                "all returned owners and workers released")

    def job(self, route, case, source_encoding, output_encoding, suffix=""):
        identity = route + "-" + case["id"] + "-" + SHORT[source_encoding] + "-" + SHORT[output_encoding] + suffix
        return {"id": identity, "input": str(self.sources[(case["wire_variant"], source_encoding)]),
                "destination": str(self.root / "bundles" / identity), "targets": case["targets"],
                "encoding": output_encoding, "row_local_attributes": case["row_local_attributes"]}

    def check_report(self, report, job, case):
        expected = case["expected"]
        self.reports.validate(report)
        require(self.reports.exit_code(report) == expected["exit_code"], "literal shared report outcome")
        require(report["input"]["id"] == job["id"], "report identity")
        require(report["candidate"]["outcome"] == expected["candidate_outcome"], "literal candidate decision")
        if expected["exit_code"]:
            require(report["publication"]["outcome"] != "published" and not report["publication"]["artifacts"],
                    "refusal has no published artifacts")
            return
        require(report["tool"] == {"name": "meshvale-repair", "version": self.args.native_version}, "actual native identity")
        require(report["input"]["sha256"] == sha(read(job["input"])), "actual input digest")
        require(report["operation"]["options"] == {"encoding": job["encoding"], "rewrite": "canonical",
                "targets": [{"keep": pair[0], "remove": pair[1]} for pair in job["targets"]],
                "row_local_attributes": job["row_local_attributes"]}, "exact caller policies")
        require(report["publication"]["outcome"] == "published" and report["publication"]["verified"] is True,
                "actual verified publication")
        ext = report["extensions"]["meshvale.repair.ply"]
        require(ext["memory_output_verified"] is True and ext["source_faces"] == expected["maps"]["SourceFaces"] and
                ext["source_corners"] == expected["maps"]["SourceCorners"], "actual inverse correspondence")
        for domain, entries in [("vertex", expected["retained_vertex_rows"]),
                                ("face", expected["maps"]["FaceMap"]), ("corner", expected["maps"]["CornerMap"])]:
            maps = [item for item in report["maps"] if item["domain"] == domain and
                    item["from"] == {"snapshot": "input", "mesh": "surface"} and
                    item["to"] == {"snapshot": "candidate", "mesh": "surface"}]
            require(len(maps) == 1, "one forward correspondence per domain")
            mapping = maps[0]
            actual = list(range(len(entries))) if mapping["kind"] == "identity" else [
                value[0] if type(value) is list and len(value) == 1 else value for value in mapping["entries"]]
            require(actual == entries, "literal forward correspondence " + domain)
            exported = [item for item in report["maps"] if item["domain"] == domain and
                        item["from"] == {"snapshot": "candidate", "mesh": "surface"} and
                        item["to"] == {"snapshot": "output", "mesh": "surface"}]
            require(len(exported) == 1 and exported[0]["kind"] == "identity" and exported[0]["entries"] is None,
                    "explicit output identity correspondence " + domain)
        require(len(report["maps"]) == 6, "exact three repair and three output maps")
        require(report["profile"]["outcome"] == "passed", "scoped profile passed")
        require(report["profile"]["required_checks"] == list(REQUIRED_CHECKS), "owning PLY publication profile")
        required = set(REQUIRED_CHECKS)
        coverage = {item["id"]: item for item in report["coverage"]}
        require(required and all(coverage[key]["status"] == "performed" and coverage[key]["finding"] == "passed"
                                 for key in required), "required checks actually performed")

    def bundle(self, job, case, report, payload=None):
        destination = Path(job["destination"])
        if case["expected"]["exit_code"]:
            require(not destination.exists(), "no bundle on refused input or request")
            return
        name = Path(job["input"]).name
        require(sorted(path.name for path in destination.iterdir()) == sorted([name, "meshvale-report.json"]),
                "exact physical bundle inventory")
        raw = read(destination / name, 4096)
        facts = verify_mixed_ply(raw, case, job["encoding"])
        receipt = read(destination / "meshvale-report.json")
        require(self.reports.load(receipt) == report, "physical receipt and delivered semantic report")
        if payload is not None:
            require(receipt == payload, "physical receipt exact delivered bytes")
        require(len(report["publication"]["artifacts"]) == 1 and
                report["publication"]["artifacts"][0]["sha256"] == sha(raw), "real physical entry digest")
        self.physical.append({"id": job["id"], "entry": str(destination / name), "sha256": sha(raw), **facts})

    def api(self, job, case, context, *, route="api", cancellation=None):
        result = self.workflow.repair_ply_file(job["input"], job["destination"], job["targets"],
            encoding=job["encoding"], row_local_attributes=job["row_local_attributes"], input_id=job["id"],
            execution=context, cancellation=cancellation)
        code = 130 if cancellation is not None else case["expected"]["exit_code"]
        self.events.append({"route": route, "id": job["id"], "expected_exit": code, "exit": result.exit_code,
                            "workflow_started": result.workflow_started})
        capture_only = cancellation is not None or case["id"] == "malformed-index"
        require(result.exit_code is None if capture_only else result.exit_code == code,
                "native API exit availability/outcome " + job["id"])
        if capture_only:
            require(result.status is None and result.outcomes is None, "no invented workflow status/outcomes before capture")
        if cancellation is not None:
            require(not result.workflow_started and result.capture_status.name == "kCanceled" and
                    result.capture_diagnostic is not None and result.output_bytes is None and
                    not Path(job["destination"]).exists(), "genuine canceled capture without publication")
        elif code:
            require(result.output_bytes is None and not Path(job["destination"]).exists(), "refusal no partial output")
            if case["id"] == "malformed-index":
                require(not result.workflow_started and result.capture_status.name != "kSuccess" and
                        result.capture_diagnostic is not None, "malformed source fails before workflow")
            else:
                require(result.workflow_started and result.status.name == "kRejected" and
                        result.outcomes.execution.name == "kCompleted" and
                        result.outcomes.candidate.name == "rejected" and
                        result.outcomes.publication.name == "kNotAttempted", "actual native whole-request rejection")
                require(result.report_json, "reached native rejection retains its real report")
                self.check_report(self.reports.load(result.report_json), job, case)
        else:
            expected = case["expected"]
            require(result.workflow_started and result.status.name == "kSuccess" and result.memory_output_verified,
                    "genuine native completed workflow")
            require(result.input_snapshot_counts == (len(case["source_literal"]["positions_f64_hex"]) // 3,
                    len(case["source_literal"]["face_offsets"]) - 1, len(case["source_literal"]["corner_vertices"])), "literal source counts")
            require(result.candidate_snapshot_counts == tuple(expected["counts"][key] for key in ("vertices", "faces", "corners")), "literal candidate counts")
            for prop, key in [("face_map", "FaceMap"), ("corner_map", "CornerMap"), ("source_faces", "SourceFaces"), ("source_corners", "SourceCorners")]:
                require(list(getattr(result, prop)) == expected["maps"][key], "literal native " + prop)
            require(result.original_bytes == read(job["input"]), "retained original bytes")
            verify_mixed_ply(result.output_bytes, case, job["encoding"])
            payload = result.report_json
            report = self.reports.load(payload)
            self.check_report(report, job, case)
            self.bundle(job, case, report, payload)
        del result
        self.released(context)

    def process(self, route, arguments, name, expected_exit):
        command = ([sys.executable, "-I", "-m", "meshvale_repair"] if route == "module" else [str(self.console)]) + arguments
        result = subprocess.run(command, cwd=self.root, capture_output=True, timeout=90, check=False)
        (self.root / (name + ".stdout")).write_bytes(result.stdout)
        (self.root / (name + ".stderr")).write_bytes(result.stderr)
        self.processes.append({"id": name, "command": command, "exit": result.returncode, "expected_exit": expected_exit})
        require(result.returncode == expected_exit, "literal process outcome " + name)
        return result.stdout

    def single(self, route, job, case):
        arguments = ["ply", job["input"], job["destination"], "--encoding", job["encoding"], "--input-id", job["id"]]
        for keep, remove in job["targets"]:
            arguments += ["--target", str(keep) + ":" + str(remove)]
        if not job["targets"]:
            arguments.append("--unchanged")
        for declaration in job["row_local_attributes"]:
            arguments += ["--row-local", declaration["domain"] + ":" + declaration["name"]]
        code = case["expected"]["exit_code"]
        payload = self.process(route, arguments, job["id"], code)
        report = self.reports.load(payload)
        self.events.append({"route": route + "-single", "id": job["id"], "exit": code})
        self.check_report(report, job, case)
        self.bundle(job, case, report, payload[:-1] if payload.endswith(b"\n") else payload)

    def batch(self, route, jobs, cases, name, *, fail_fast=False):
        request = self.root / (name + ".json")
        write_json(request, {"schema": "meshvale.repair.ply-jobs/1", "jobs": jobs})
        arguments = ["ply-batch", str(request)] + (["--fail-fast"] if fail_fast else [])
        code = max(case["expected"]["exit_code"] for case in cases)
        payload = self.process(route, arguments, name, code)
        batch = self.reports.load(payload)
        require(batch["exit_code"] == self.reports.exit_code(batch) == code, "literal batch outcome")
        require([row["id"] for row in batch["jobs"]] == [job["id"] for job in jobs], "ordered batch identities")
        stopped = False
        for row, job, case in zip(batch["jobs"], jobs, cases):
            require(row["outcome"] == ("not-attempted" if stopped else "attempted"), "literal continuation policy")
            if stopped:
                require(row["report"] is None and not Path(job["destination"]).exists(), "no later capture/publication")
                continue
            self.events.append({"route": route + "-batch", "id": job["id"], "exit": case["expected"]["exit_code"]})
            self.check_report(row["report"], job, case)
            self.bundle(job, case, row["report"])
            if case["expected"]["exit_code"] == 0:
                receipt = read(Path(job["destination"]) / "meshvale-report.json")
                require(payload.count(receipt) == 1, "actual receipt bytes occur once in ordered batch")
            if fail_fast and case["expected"]["exit_code"]:
                stopped = True

    def run(self):
        passed, error = False, None
        try:
            context = self.workflow.PlyExecution()
            exact = self.cases["exact"]
            token = self.workflow.PlyCancellation()
            require(token.cancel(), "genuine stop source")
            self.api(self.job("prestop", exact, "ascii", "ascii"), exact, context, route="prestop", cancellation=token)
            cells = [(case, source, destination) for case in self.cases.values() for source in ENCODINGS
                     for destination in (ENCODINGS if case["expected"]["exit_code"] == 0 else ("ascii",))]
            require(len(cells) == 66, "finite main matrix")
            for case, source, destination in cells:
                self.api(self.job("api", case, source, destination), case, context)
            serial = self.workflow.PlyExecution(preset="serial-reference-v1")
            for name in ("exact", "cyclic"):
                case = self.cases[name]
                self.api(self.job("serial", case, "ascii", "binary_big_endian"), case, serial, route="serial")
            for route in ("module", "console"):
                for case, source, destination in cells:
                    self.single(route, self.job(route + "-single", case, source, destination), case)
                for start in range(0, len(cells), 16):
                    group = cells[start:start + 16]
                    jobs = [self.job(route + "-batch", case, source, destination) for case, source, destination in group]
                    self.batch(route, jobs, [cell[0] for cell in group], route + "-batch-" + str(start // 16))
                for fail_fast in (False, True):
                    cases = [self.cases[name] for name in ("clean", "exact", "malformed-index", "clean")]
                    prefix = route + "-mixed-" + str(fail_fast)
                    jobs = [self.job(prefix, case, "ascii", "ascii", "-" + str(index)) for index, case in enumerate(cases)]
                    self.batch(route, jobs, cases, prefix, fail_fast=fail_fast)
            require(len(self.events) == 347 and len(self.physical) == 237, "complete reached attempts/publications")
            passed = True
        except Exception as exc:
            error = {"type": type(exc).__name__, "message": str(exc)}
        finally:
            unchanged = inventory(self.fixture) == self.protected and all(read(path) == self.wires[key] for key, path in self.sources.items())
            passed = passed and unchanged
            summary = {"schema": "meshvale.test.ply-mixed-results/1", "passed": passed, "error": error,
                       "inputs_unchanged": unchanged, "runtime": self.runtime, "origins": self.origins,
                       "events": self.events, "event_routes": dict(Counter(event["route"] for event in self.events)),
                       "physical_publications": self.physical, "processes": self.processes}
            write_json(self.root / "corpus-results.json", summary)
            print(json.dumps({"passed": passed, "error": error, "attempts": len(self.events), "publications": len(self.physical)}))
        return 0 if passed else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-root", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--native-version", required=True)
    parser.add_argument("--console", type=Path)
    raise SystemExit(Corpus(parser.parse_args()).run())
