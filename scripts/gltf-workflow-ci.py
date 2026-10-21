# SPDX-License-Identifier: Apache-2.0
"""Qualify the optional native glTF workflow against pinned public dependencies.

Run from a clean public checkout after populating .local/gw/{g,i,v} with the
Geometry, Interchange, and vcpkg revisions below. Windows needs an x64 MSVC
developer environment; Linux needs Clang 18.1.3 or GNU 13. Python requires the
pinned build and jsonschema packages selected by the dedicated hosted workflow.
Failures remain failures, including sanitizer process startup failures.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile


GEOMETRY_REVISION = "48298b24be7c132816fb5580d22d58bde61558d0"
INTERCHANGE_REVISION = "916c45f742feadbf9bbb608af3576c8d12db045d"
INTERCHANGE_FILE_REVISION = "b8e81599d6f08c3b55060c75e76d6b7e75144561"
VCPKG_REVISION = "cb5a41b03cde4086d554b3f3e2eac39206b16eda"
BUILD_WORKERS = 2
TEST_TIMEOUT_SECONDS = 120
FILE_TEST_TIMEOUT_SECONDS = 600
FILE_SUITE_TIMEOUT_SECONDS = 1800
FILE_REPORT_CASE_COUNT = 14


@dataclass(frozen=True)
class Profile:
    system: str
    compiler_id: str
    cxx: str
    cc: str
    configuration: str
    materialization: str = "qualified"
    version: str = ""
    flags: str = ""
    link_flags: str = ""
    sanitizer: str = ""
    allocation_probe: str = "enabled"


PROFILES = {
    "msvc-release": Profile("Windows", "MSVC", "cl", "cl", "Release", flags="/EHsc"),
    "msvc-asan": Profile("Windows", "MSVC", "cl", "cl", "Release",
                         flags="/EHsc /fsanitize=address /Zi",
                         link_flags="/INCREMENTAL:NO", sanitizer="asan"),
    "clang-debug": Profile("Linux", "Clang", "clang++-18", "clang-18", "Debug",
                           version="18.1.3"),
    "clang-asan-ubsan": Profile("Linux", "Clang", "clang++-18", "clang-18", "Debug",
                                version="18.1.3", sanitizer="asan-ubsan",
                                flags="-fsanitize=address,undefined -fno-omit-frame-pointer",
                                link_flags="-fsanitize=address,undefined"),
    "clang-tsan": Profile("Linux", "Clang", "clang++-18", "clang-18", "Debug",
                          version="18.1.3", sanitizer="tsan", allocation_probe="disabled",
                          flags="-fsanitize=thread -O1 -g -fno-omit-frame-pointer",
                          link_flags="-fsanitize=thread"),
    "gnu-unsupported": Profile("Linux", "GNU", "g++-13", "gcc-13", "Release",
                               materialization="unsupported"),
    "appleclang-unsupported": Profile("Darwin", "AppleClang", "clang++", "clang",
                                      "Release", materialization="unsupported"),
}

# Independent literals describing these fixture outcomes, never imported from Repair.
# Values: execution, core stage, candidate, required profile, exit, native stage, output.
REPORT_EXPECTATIONS = {
    "accepted.json": ("completed", "complete", "accepted", "passed", 0, "complete", True),
    "unchanged.json": ("completed", "complete", "unchanged", "passed", 0, "complete", True),
    "rejected.json": ("completed", "complete", "rejected", "failed", 1, "repair", False),
    "accepted-then-failed.json": ("failed", "verify", "accepted", "incomplete", 2, "write", False),
    "materialization-allowance.json": ("failed", "inspect", "not-attempted", "incomplete", 2,
                                      "materialize", False),
    "malformed.json": ("failed", "import", "not-attempted", "incomplete", 2, "read", False),
    "undeclared-channels.json": ("completed", "complete", "rejected", "incomplete", 1,
                                 "repair", False),
    "channels-and-aliases.json": ("completed", "complete", "accepted", "passed", 0,
                                  "complete", True),
    "strip-unchanged.json": ("completed", "complete", "unchanged", "passed", 0, "complete", True),
    "strip-changed.json": ("completed", "complete", "accepted", "passed", 0, "complete", True),
    "fan-unchanged.json": ("completed", "complete", "unchanged", "passed", 0, "complete", True),
    "fan-changed.json": ("completed", "complete", "accepted", "passed", 0, "complete", True),
}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_reports(geometry, reports, receipt):
    # This public dependency API validates schema, references, coverage and exit
    # precedence independently of the native producer's serializer.
    sys.path.insert(0, str(geometry / "python"))
    import meshvale_reports
    from jsonschema import Draft202012Validator
    actual = {path.name for path in reports.iterdir()}
    if actual != REPORT_EXPECTATIONS.keys():
        raise RuntimeError(f"Report inventory mismatch: {sorted(actual)}")
    validator = Draft202012Validator(meshvale_reports.schema_document())
    outcomes = []
    for name, expected in REPORT_EXPECTATIONS.items():
        path = reports / name
        report = meshvale_reports.load(path.read_text(encoding="utf-8"))
        validator.validate(report)
        extension = report["extensions"]["meshvale.repair.gltf"]
        observed = (report["execution"]["outcome"], report["execution"]["stage"],
                    report["candidate"]["outcome"], meshvale_reports.profile_outcome(report),
                    meshvale_reports.exit_code(report), extension["native_stage"],
                    extension["memory_output_verified"])
        if observed != expected:
            raise RuntimeError(f"{name}: expected {expected}, observed {observed}")
        publication = report["publication"]
        if (report["schema"] != "meshvale.report/1" or
                publication != {"requested": False, "outcome": "not-requested",
                                "snapshot": None, "artifacts": [], "verified": False,
                                "reason": None}):
            raise RuntimeError(f"{name}: supplied-memory scope differs")
        if any(snapshot["role"] == "output" for snapshot in report["snapshots"]):
            raise RuntimeError(f"{name}: published output snapshot was claimed")
        copy_stats = extension["phase_execution"]["report_copy"]
        if (copy_stats["launched_workers"] == 0 or
                copy_stats["launched_workers"] != copy_stats["joined_workers"]):
            raise RuntimeError(f"{name}: native report workers were not fully joined")
        outcomes.append({"fixture": name, "sha256": sha256(path), "observed": observed})
    write_json(receipt, {"validator": "Geometry public meshvale_reports + Draft202012Validator",
                         "geometry_revision": GEOMETRY_REVISION,
                         "validator_sha256": sha256(Path(meshvale_reports.__file__)),
                         "reports": outcomes})
    print(f"Independently validated {len(outcomes)} supplied-memory reports", flush=True)


class Runner:
    def __init__(self, root, work, profile):
        self.root, self.work, self.profile = root, work, profile
        self.evidence = work / "e"
        self.evidence.mkdir(exist_ok=False)
        self.environment = os.environ.copy()
        self.number = 0
        self.environment.update({"CC": profile.cc, "CXX": profile.cxx,
                                 "CFLAGS": "", "CXXFLAGS": "", "LDFLAGS": "",
                                 "CMAKE_BUILD_PARALLEL_LEVEL": str(BUILD_WORKERS),
                                 "VCPKG_ROOT": str(work / "v"), "VCPKG_BINARY_SOURCES": "clear",
                                 "VCPKG_DISABLE_METRICS": "1", "VCPKG_MAX_CONCURRENCY": "2",
                                 "MESHVALE_SANITIZER_PROFILE": profile.sanitizer,
                                 "MESHVALE_SANITIZER_COMPILE_FLAGS": profile.flags,
                                 "MESHVALE_SANITIZER_LINK_FLAGS": profile.link_flags})
        for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS"):
            self.environment.pop(key, None)
        if profile.sanitizer == "asan":
            self.environment["ASAN_OPTIONS"] = "alloc_dealloc_mismatch=1"
        elif profile.sanitizer == "asan-ubsan":
            self.environment.update({"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                                     "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
        elif profile.sanitizer == "tsan":
            self.environment["TSAN_OPTIONS"] = "halt_on_error=1"

    def run(self, *command, timeout=1800):
        command = list(map(str, command))
        self.number += 1
        stem = self.evidence / f"command-{self.number:03}"
        print(shlex.join(command), flush=True)
        record = {"command": command}
        try:
            result = subprocess.run(command, cwd=self.root, env=self.environment,
                                    capture_output=True, text=True, errors="replace", timeout=timeout)
            stem.with_suffix(".stdout").write_text(result.stdout, encoding="utf-8")
            stem.with_suffix(".stderr").write_text(result.stderr, encoding="utf-8")
            record["returncode"] = result.returncode
            print(result.stdout, end="", flush=True)
            print(result.stderr, end="", flush=True)
        except subprocess.TimeoutExpired as error:
            for suffix, output in ((".stdout", error.stdout), (".stderr", error.stderr)):
                stem.with_suffix(suffix).write_bytes(
                    output if isinstance(output, bytes) else (output or "").encode())
            record["timeout_seconds"] = timeout
            raise
        finally:
            with (self.evidence / "commands.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(record) + "\n")
        result.check_returncode()
        return result.stdout

    def configure(self, source, build, *options):
        p = self.profile
        self.run("cmake", "-G", "Ninja", "-S", source, "-B", build,
                 f"-DCMAKE_BUILD_TYPE={p.configuration}", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                 f"-DCMAKE_CXX_COMPILER={p.cxx}", f"-DCMAKE_C_COMPILER={p.cc}",
                 f"-DCMAKE_CXX_FLAGS={p.flags}", f"-DCMAKE_C_FLAGS={p.flags}",
                 f"-DCMAKE_EXE_LINKER_FLAGS={p.link_flags}",
                 f"-DCMAKE_SHARED_LINKER_FLAGS={p.link_flags}", *options)

    def build(self, build):
        self.run("cmake", "--build", build, "--parallel", BUILD_WORKERS, "--verbose")
        self.compilation(build / "compile_commands.json", warnings=True)

    def compilation(self, database, warnings=False):
        commands = json.loads(database.read_text(encoding="utf-8"))
        if not commands:
            raise RuntimeError(f"Empty compilation database: {database}")
        expected = Path(shutil.which(self.profile.cxx)).resolve()
        required = shlex.split(self.profile.flags)
        if warnings:
            required.append("/WX" if self.profile.compiler_id == "MSVC" else "-Werror")
        # The Windows tinyobj triplet supplies its sanitizer flags but does not
        # impose Repair's C++ exception selection on the third-party parser.
        if not warnings and self.profile.compiler_id == "MSVC":
            required = [flag for flag in required if flag != "/EHsc"]
        for entry in commands:
            tokens = entry.get("arguments") or shlex.split(
                entry["command"], posix=self.profile.system != "Windows")
            tokens = [token.strip('"') for token in tokens]
            actual = shutil.which(tokens[0])
            if actual is None or Path(actual).resolve() != expected:
                raise RuntimeError(f"Compiler mismatch: {database}: {tokens[0]}")
            if not all(flag in tokens for flag in required):
                raise RuntimeError(f"Missing flags in {entry['file']}: {required}")
        write_json(self.evidence / (database.parent.name + "-compile-commands.json"), commands)

    def inventory(self, build, name, count=None):
        text = self.run("ctest", "--test-dir", build, "-C", self.profile.configuration,
                        "--show-only=json-v1")
        (self.evidence / f"{name}-inventory.json").write_text(text, encoding="utf-8")
        tests = json.loads(text)["tests"]
        if count is not None and len(tests) != count:
            raise RuntimeError(f"{name}: expected {count} tests, observed {len(tests)}")
        return tests

    def test(self, build, file_workflow=False):
        actual_log = Path(build) / "Testing" / "Temporary" / "LastTest.log"
        try:
            self.run("ctest", "--test-dir", build, "-C", self.profile.configuration,
                     "--verbose", "--output-on-failure", "--timeout",
                     FILE_TEST_TIMEOUT_SECONDS if file_workflow else TEST_TIMEOUT_SECONDS,
                     timeout=FILE_SUITE_TIMEOUT_SECONDS if file_workflow else 900)
        finally:
            if actual_log.is_file():
                shutil.copyfile(actual_log, self.evidence / (Path(build).name + "-actual-LastTest.log"))
        if not actual_log.is_file():
            raise RuntimeError("Executed CTest did not retain its actual test log")

    def install(self, build, prefix):
        self.run("cmake", "--install", build, "--config", self.profile.configuration,
                 "--prefix", prefix)

    def check_inventory(self, source, build, *options):
        p = self.profile
        version = ["--compiler-version", p.version] if p.version else []
        self.run(sys.executable, source / "scripts/check-native-tests.py", "--build-dir", build,
                 "--configuration", p.configuration, "--compiler-id", p.compiler_id,
                 "--materialization-profile", p.materialization, *version, *options)

    def expose_windows_asan(self, build):
        records = list((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
        if len(records) != 1:
            raise RuntimeError("Expected one compiler identity record")
        match = re.search(r'set\(CMAKE_CXX_COMPILER "([^"\n]+)"\)', records[0].read_text())
        if not match:
            raise RuntimeError("Configured compiler path is missing")
        directory = Path(match.group(1)).parent
        runtime = directory / "clang_rt.asan_dynamic-x86_64.dll"
        if not runtime.is_file():
            raise RuntimeError("Configured compiler's x64 ASan runtime is missing")
        self.environment["PATH"] = str(directory) + os.pathsep + self.environment["PATH"]
        write_json(self.evidence / "windows-asan-runtime.json",
                   {"path": str(runtime), "sha256": sha256(runtime),
                    "compiler_record_sha256": sha256(records[0])})


def qualify(root, profile_name, file_workflow=False):
    profile = PROFILES[profile_name]
    if platform.system() != profile.system:
        raise RuntimeError(f"{profile_name} requires {profile.system}")
    work = root / ".local" / "gw"
    runner = Runner(root, work, profile)
    geometry, interchange, vcpkg = (work / name for name in ("g", "i", "v"))
    inputs = {"profile": profile_name, "configuration": vars(profile),
              "file_workflow": file_workflow, "sources": {}}
    for name, source, revision in (("geometry", geometry, GEOMETRY_REVISION),
                                    ("interchange", interchange,
                                     INTERCHANGE_FILE_REVISION if file_workflow else INTERCHANGE_REVISION),
                                    ("vcpkg", vcpkg, VCPKG_REVISION)):
        actual = runner.run("git", "-C", source, "rev-parse", "HEAD").strip()
        if actual != revision or runner.run("git", "-C", source, "status", "--porcelain").strip():
            raise RuntimeError(f"{name}: dependency checkout differs from pinned clean revision")
        inputs["sources"][name] = actual
    inputs["sources"]["repair"] = runner.run("git", "rev-parse", "HEAD").strip()
    write_json(runner.evidence / "inputs.json", inputs)
    for tool in ("cmake", "ctest", "ninja", profile.cxx):
        if shutil.which(tool) is None:
            raise RuntimeError(f"Required tool missing: {tool}")
    runner.run("cmake", "--version")
    if profile.compiler_id == "GNU":
        if runner.run(profile.cxx, "-dumpfullversion").strip().split(".")[0] != "13":
            raise RuntimeError("GNU unsupported route requires GNU 13")
    if profile.system == "Windows":
        runner.run("cmd", "/d", "/c", str(vcpkg / "bootstrap-vcpkg.bat"), "-disableMetrics")
        triplet = "x64-windows-static-md" + ("-asan" if profile.sanitizer else "")
    else:
        runner.run("bash", vcpkg / "bootstrap-vcpkg.sh", "-disableMetrics")
        if profile.system == "Darwin":
            triplet = "arm64-osx" if platform.machine() == "arm64" else "x64-osx"
        else:
            triplet = "x64-linux" + ("-" + profile.sanitizer if profile.sanitizer else "")
    prefix, dependencies = work / "p", work / "d"
    gb, ib, rb, pb, cb = (work / name for name in ("gb", "ib", "rb", "pb", "cb"))
    runner.configure(geometry, gb, "-DBUILD_TESTING=ON", "-DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON",
                     f"-DMESHVALE_GEOMETRY_ALLOCATION_PROBE={'ON' if profile.allocation_probe == 'enabled' else 'OFF'}",
                     f"-DMESHVALE_GEOMETRY_MATERIALIZATION_TEST_PROFILE={profile.materialization}")
    runner.build(gb)
    if profile.sanitizer == "asan":
        runner.expose_windows_asan(gb)
    runner.inventory(gb, "geometry")
    runner.check_inventory(geometry, gb, "--allocation-probe", profile.allocation_probe)
    runner.test(gb)
    runner.install(gb, prefix)
    # Retain the owning sanitizer triplets, adding explicit compiler selection
    # and compilation databases for the parser on every profile, including the
    # non-sanitized GNU/AppleClang unsupported routes.
    base_triplet = interchange / "cmake/sanitizer-triplets" / f"{triplet}.cmake"
    if not profile.sanitizer:
        candidates = [vcpkg / "triplets" / f"{triplet}.cmake",
                      vcpkg / "triplets/community" / f"{triplet}.cmake"]
        base_triplet = next((path for path in candidates if path.is_file()), None)
    if base_triplet is None or not base_triplet.is_file():
        raise RuntimeError(f"Owning vcpkg triplet missing: {triplet}")
    overlay = runner.evidence / "triplets"
    overlay.mkdir()
    cxx_path = Path(shutil.which(profile.cxx)).resolve().as_posix()
    cc_path = Path(shutil.which(profile.cc)).resolve().as_posix()
    (overlay / f"{triplet}.cmake").write_text(
        f'include("{base_triplet.as_posix()}")\n'
        'list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n'
        f'  "-DCMAKE_CXX_COMPILER={cxx_path}" "-DCMAKE_C_COMPILER={cc_path}")\n'
        'list(APPEND VCPKG_ENV_PASSTHROUGH CC CXX)\n', encoding="utf-8")
    toolchain = [f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
                 f"-DVCPKG_TARGET_TRIPLET={triplet}",
                 f"-DVCPKG_OVERLAY_TRIPLETS={overlay}",
                 f"-DVCPKG_INSTALLED_DIR={dependencies}"]
    runner.configure(interchange, ib, *toolchain, f"-DCMAKE_PREFIX_PATH={prefix}",
                     "-DBUILD_TESTING=ON", "-DMESHVALE_INTERCHANGE_PYTHON=OFF",
                     "-DMESHVALE_INTERCHANGE_USD=OFF", "-DMESHVALE_INTERCHANGE_FBX=OFF",
                     "-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=ON",
                     "-DMESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT=ON",
                     f"-DMESHVALE_INTERCHANGE_GLTF_FILES={'ON' if file_workflow else 'OFF'}",
                     f"-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE={profile.materialization}")
    runner.build(ib)
    # Audit the parser's actual compiler too; sanitizer triplets expose databases.
    port_suffix = "dbg" if profile.configuration == "Debug" else "rel"
    parser_database = vcpkg / "buildtrees" / "tinyobjloader" / f"{triplet}-{port_suffix}" / "compile_commands.json"
    runner.compilation(parser_database)
    runner.inventory(ib, "interchange")
    runner.check_inventory(interchange, ib, "--topology-write", "ON", "--workflow-support", "ON")
    runner.test(ib, file_workflow)
    runner.install(ib, prefix)
    package_prefix = f"{prefix};{dependencies}/{triplet}"
    repair_options = [f"-DCMAKE_PREFIX_PATH={package_prefix}", "-DMESHVALE_REPAIR_PYTHON=OFF",
                      "-DMESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON",
                      "-DMESHVALE_REPAIR_GLTF_WORKFLOW=ON",
                      f"-DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW={'ON' if file_workflow else 'OFF'}",
                      f"-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE={profile.materialization}"]
    runner.configure(root, rb, "-DBUILD_TESTING=ON", *repair_options)
    runner.build(rb)
    qualified = profile.materialization == "qualified"
    native_count = (10 if qualified else 6) if file_workflow else (6 if qualified else 4)
    tests = runner.inventory(rb, "repair", native_count)
    runner.check_inventory(root, rb, "--gltf-workflow", "ON", "--gltf-file-workflow",
                           "ON" if file_workflow else "OFF")
    runner.test(rb, file_workflow)
    if qualified:
        reports = runner.evidence / "reports"
        test = next(test for test in tests if test["name"] == "gltf_workflow")
        runner.run(*test["command"], reports, timeout=TEST_TIMEOUT_SECONDS)
        validate_reports(geometry, reports, runner.evidence / "report-validation.json")
        if file_workflow:
            file_reports = runner.evidence / "file-reports"
            file_test = next(test for test in tests if test["name"] == "gltf_file_workflow_semantics")
            runner.run(*file_test["command"], "--write-reports", file_reports,
                       timeout=FILE_TEST_TIMEOUT_SECONDS)
            file_receipt = runner.evidence / "file-report-validation.json"
            runner.run(sys.executable, root / "scripts/check-gltf-file-reports.py", file_reports,
                       "--geometry-python", geometry / "python", "--receipt", file_receipt)
            validated = json.loads(file_receipt.read_text(encoding="utf-8"))["reports"]
            if len(validated) != FILE_REPORT_CASE_COUNT:
                raise RuntimeError("File report oracle did not validate its exact fixture count")
            readback_reports = runner.evidence / "file-readback-report"
            request_test = next(test for test in tests if test["name"] == "gltf_file_workflow_requests")
            runner.run(*request_test["command"], "--write-readback-report", readback_reports,
                       timeout=FILE_TEST_TIMEOUT_SECONDS)
            readback_receipt = runner.evidence / "file-readback-validation.json"
            runner.run(sys.executable, root / "scripts/check-gltf-file-reports.py", readback_reports,
                       "--readback-failure", "--geometry-python", geometry / "python",
                       "--receipt", readback_receipt)
            validated_readback = json.loads(readback_receipt.read_text(encoding="utf-8"))["reports"]
            if len(validated_readback) != 1:
                raise RuntimeError("Readback oracle did not validate its one actual failure report")
    dist = work / "dist"
    runner.run(sys.executable, "-m", "build", "--sdist", "--outdir", dist)
    archives = list(dist.glob("*.tar.gz"))
    if len(archives) != 1:
        raise RuntimeError("Expected exactly one fresh Repair source package")
    runner.run(sys.executable, root / "scripts/check-package.py", archives[0])
    package_source = work / "s"
    package_source.mkdir()
    with tarfile.open(archives[0], "r:gz") as archive:
        archive.extractall(package_source, filter="data")
    sources = list(package_source.iterdir())
    if len(sources) != 1 or not sources[0].is_dir():
        raise RuntimeError("Source archive must have one top-level directory")
    source = sources[0]
    runner.configure(source, pb, "-DBUILD_TESTING=OFF", *repair_options)
    runner.build(pb)
    if runner.inventory(pb, "production", 0):
        raise RuntimeError("Production source-package build registered tests")
    runner.install(pb, prefix)
    inventory = [{"path": str(path.relative_to(prefix)).replace(os.sep, "/"),
                  "sha256": sha256(path)} for path in sorted(prefix.rglob("*")) if path.is_file()]
    if any("test_internal" in item["path"] or item["path"].startswith("include/src/") for item in inventory):
        raise RuntimeError("Private workflow test seams leaked into installed headers")
    if not (prefix / "include/meshvale/repair/gltf_workflow.h").is_file():
        raise RuntimeError("Installed workflow public header is absent")
    if (prefix / "include/meshvale/repair/gltf_file_workflow.h").is_file() != file_workflow:
        raise RuntimeError("Installed file workflow header differs from selected module")
    write_json(runner.evidence / "package.json", {"source_archive_sha256": sha256(archives[0]),
                                                "installed_files": inventory})
    relocated = work / "q"
    if relocated.exists() or not prefix.resolve().is_relative_to(work.resolve()):
        raise RuntimeError("Install relocation requires a fresh workspace-local destination")
    prefix.rename(relocated)
    runner.configure(source / "examples/consumer", cb,
                     f"-DCMAKE_PREFIX_PATH={relocated};{dependencies}/{triplet}",
                     "-DMESHVALE_REPAIR_TEST_GLTF_WORKFLOW=ON",
                     f"-DMESHVALE_REPAIR_TEST_GLTF_FILE_WORKFLOW={'ON' if file_workflow else 'OFF'}",
                     f"-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE={profile.materialization}")
    runner.build(cb)
    installed_count = 4 if file_workflow else 3
    runner.inventory(cb, "installed", installed_count)
    runner.check_inventory(root, rb, "--gltf-workflow", "ON", "--gltf-file-workflow",
                           "ON" if file_workflow else "OFF", "--installed-build-dir", cb)
    runner.test(cb, file_workflow)
    write_json(runner.evidence / "result.json", {"gate_passed": True, "profile": profile_name,
               "materialization_profile": profile.materialization,
               "native_tests": native_count, "installed_tests": installed_count,
               "reports_validated": 12 if qualified else 0,
               "file_reports_validated": FILE_REPORT_CASE_COUNT if qualified and file_workflow else 0,
               "file_readback_reports_validated": 1 if qualified and file_workflow else 0,
               "file_workflow": file_workflow, "production_tests": 0})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=PROFILES)
    parser.add_argument("--file-workflow", action="store_true",
                        help="Qualify retained file publication against its pinned Interchange producer")
    parser.add_argument("--validate-reports", type=Path,
                        help="Validate existing fixture JSON without configuring native builds")
    parser.add_argument("--geometry-source", type=Path)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.validate_reports:
        if args.profile or args.file_workflow or args.geometry_source is None or args.receipt is None:
            parser.error("Report-only validation requires --geometry-source and --receipt, without --profile")
        validate_reports(args.geometry_source.resolve(), args.validate_reports.resolve(), args.receipt.resolve())
    elif args.profile:
        qualify(root, args.profile, args.file_workflow)
    else:
        parser.error("Select --profile or --validate-reports")


if __name__ == "__main__":
    main()
