# SPDX-License-Identifier: Apache-2.0
"""Qualify optional glTF and OFF workflows against pinned public dependencies.

Run from a clean public checkout after populating .local/gw/{g,i,v} with the
Geometry, Interchange, and vcpkg revisions below. Windows needs an x64 MSVC
developer environment; Linux needs Clang 18.1.3 or GNU 13. Python requires the
pinned build and jsonschema packages selected by the dedicated hosted workflow.
Failures remain failures, including sanitizer process startup failures.
"""
import argparse
from dataclasses import dataclass
from email.parser import BytesParser
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shlex
import shutil
import subprocess
import sys
import sysconfig
import struct
import tarfile
import zipfile


GEOMETRY_REVISION = "48298b24be7c132816fb5580d22d58bde61558d0"
GEOMETRY_PYTHON_VERSION = "0.0.1.dev134+g48298b24b"
INTERCHANGE_REVISION = "88acdba9046352b84d3dea90648e3d271629f511"
INTERCHANGE_FILE_REVISION = "88acdba9046352b84d3dea90648e3d271629f511"
INTERCHANGE_OFF_FILE_REVISION = "b577263107403e65643fdcfc1b8af3faa5c9fff4"
INTERCHANGE_PLY_FILE_REVISION = "33f483d2f5bed45dd2c3273957c08c404fc03bee"
VCPKG_REVISION = "cb5a41b03cde4086d554b3f3e2eac39206b16eda"
BUILD_WORKERS = 2
TEST_TIMEOUT_SECONDS = 120
FILE_TEST_TIMEOUT_SECONDS = 600
FILE_SUITE_TIMEOUT_SECONDS = 1800
FILE_REPORT_CASE_COUNT = 14
OFF_REPORT_CASE_COUNT = 15
OFF_UNSUPPORTED_REPORT_CASE_COUNT = 4


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


def qualify_off_python(runner, geometry, source, relocated, dependencies, triplet):
    """Build real wheels and run installed OFF tests on the selected producer."""
    profile = runner.profile
    runner.environment["CMAKE_PREFIX_PATH"] = os.pathsep.join((str(relocated), str(dependencies / triplet)))
    output = runner.work / "python-wheels"
    configuration = [f"-Ccmake.define.CMAKE_BUILD_TYPE={profile.configuration}",
                     f"-Ccmake.define.CMAKE_CXX_FLAGS={profile.flags}"]
    runner.run(sys.executable, "-m", "build", "--wheel", "--no-isolation",
               "--outdir", output / "geometry", *configuration, geometry)
    geometry_wheels = list((output / "geometry").glob("*.whl"))
    if len(geometry_wheels) != 1:
        raise RuntimeError("Expected one fresh Geometry wheel")
    required = re.findall(r'"meshvale-geometry==([^"\s]+)"', (source / "pyproject.toml").read_text(encoding="utf-8"))
    if required != [GEOMETRY_PYTHON_VERSION]:
        raise RuntimeError("Repair's declared Geometry dependency differs from the selected Python producer")
    with zipfile.ZipFile(geometry_wheels[0]) as archive:
        names = [name for name in archive.namelist() if name.endswith(".dist-info/METADATA")]
        if len(names) != 1:
            raise RuntimeError("Expected one Geometry wheel metadata document")
        metadata = BytesParser().parsebytes(archive.read(names[0]))
    if metadata["Name"] != "meshvale-geometry" or metadata["Version"] != GEOMETRY_PYTHON_VERSION:
        raise RuntimeError("Geometry wheel metadata differs from Repair's exact dependency; use full pinned history")
    runner.run(sys.executable, geometry / "scripts/check-package.py", geometry_wheels[0])
    runner.run(sys.executable, "-m", "build", "--wheel", "--no-isolation",
               "--outdir", output / "repair", *configuration,
               "-Ccmake.define.MESHVALE_REPAIR_GLTF_WORKFLOW=OFF",
               "-Ccmake.define.MESHVALE_REPAIR_GLTF_FILE_WORKFLOW=OFF",
               "-Ccmake.define.MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON", source)
    repair_wheels = list((output / "repair").glob("*.whl"))
    if len(repair_wheels) != 1:
        raise RuntimeError("Expected one fresh OFF Repair wheel")
    runner.run(sys.executable, source / "scripts/check-package.py", repair_wheels[0])
    runner.run(sys.executable, source / "scripts/test-installed.py",
               "--geometry-wheel", geometry_wheels[0], "--repair-wheel", repair_wheels[0],
               "--off", profile.materialization, timeout=FILE_SUITE_TIMEOUT_SECONDS)
    write_json(runner.evidence / "off-python-wheels.json", {
        "profile": profile.materialization,
        "geometry_wheel": {"path": str(geometry_wheels[0]), "sha256": sha256(geometry_wheels[0])},
        "repair_wheel": {"path": str(repair_wheels[0]), "sha256": sha256(repair_wheels[0])},
        "source_archive_build": True, "interchange_python_required": False,
        "scope": "Current-run installed development wheels; portable redistribution remains a separate gate"})



def ply_cache(build, key):
    values = re.findall(r"^" + re.escape(key) + r":[^=]+=([^\r\n]*)$",
                        (build / "CMakeCache.txt").read_text(encoding="utf8"), re.MULTILINE)
    if len(values) != 1:
        raise RuntimeError(f"Expected one {key} cache value in {build}")
    return values[0]


def ply_signature(root):
    return [{"path": path.relative_to(root).as_posix(), "sha256": sha256(path)}
            for path in sorted(root.rglob("*")) if path.is_file()]


def ply_sdk_version(prefix, package):
    # The pinned Geometry producer predates project(VERSION). Its installed
    # ConfigVersion file owns the SDK version consumed by find_package.
    path = prefix / "lib/cmake" / package / (package + "ConfigVersion.cmake")
    values = re.findall(r'^set\(PACKAGE_VERSION "([^"\r\n]+)"\)\s*$',
                        path.read_text(encoding="utf8"), re.MULTILINE)
    if len(values) != 1 or values[0] != "0.0.0":
        raise RuntimeError("Installed SDK version differs from the selected development cohort: " + package)
    return {"version": values[0], "path": str(path), "sha256": sha256(path)}



def ply_object_target(entry, tokens):
    """Select only the object output; source/include/PDB text is not a target."""
    outputs = [entry["output"]] if entry.get("output") else []
    for index, token in enumerate(tokens):
        if token == "-o" or token.lower() in ("/fo", "-fo"):
            if index + 1 == len(tokens):
                raise RuntimeError("Missing compiler object-output argument")
            outputs.append(tokens[index + 1])
        elif token[:3].lower() in ("/fo", "-fo"):
            outputs.append(token[3:])
    if not outputs:
        raise RuntimeError("Missing actual CMake object-output identity")
    targets = set()
    for output in outputs:
        matches = re.findall(r"(?:^|/)CMakeFiles/([^/]+)\.dir/", output.replace("\\", "/"))
        if len(matches) != 1:
            raise RuntimeError("Missing or ambiguous CMake target in object output")
        targets.add(matches[0])
    if len(targets) != 1:
        raise RuntimeError("Conflicting actual object-output targets")
    return targets.pop()


def ply_fp_selection(entry, tokens, product, gnu=False):
    target = ply_object_target(entry, tokens)
    # Exact targets from the selected products' owning CMake declarations.
    strict_targets = {
        "geometry": {"meshvale_geometry", "meshvale_geometry_materialization",
                     "meshvale_geometry_attribute_numerics", "meshvale_geometry_triangulation"},
        "interchange": {"meshvale_interchange_ply", "meshvale_interchange_ply_files",
                        "meshvale_ply_tests", "meshvale_ply_request_tests", "meshvale_ply_files_tests",
                        "meshvale_ply_files_request_tests", "meshvale_ply_files_consumer_tests"},
        "repair": {"meshvale_repair_materialized_duplicates", "meshvale_repair_ply_file_workflow",
                   "meshvale_materialized_duplicate_request_tests", "meshvale_materialized_duplicate_tests",
                   "meshvale_materialized_duplicate_semantic_tests", "meshvale_ply_workflow_tests",
                   "meshvale_ply_workflow_request_tests",
                   "meshvale_materialized_duplicate_unsupported_tests",
                   "meshvale_ply_workflow_unsupported_tests", "meshvale_ply_workflow_unsupported_consumer",
                   "meshvale_repair_gltf_workflow", "meshvale_repair_gltf_file_workflow",
                   "meshvale_repair_off_file_workflow", "_repair"}}
    if product == "repair" and target not in strict_targets[product] | {
            "meshvale_repair", "meshvale_repair_header_checks", "meshvale_duplicate_tests",
            "meshvale_repair_record", "nanobind-static"}:
        raise RuntimeError("Unclassified Repair object target: " + target)
    required = target in strict_targets[product]
    gnu_flags = (("-fno-fast-math",) if product == "geometry" else
                 ("-fno-fast-math", "-ffp-contract=off"))
    present = (all(flag in tokens for flag in gnu_flags) if gnu else
               any(flag in tokens for flag in ("/fp:strict", "-fp:strict")))
    if required and not present:
        raise RuntimeError("Required per-target strict FP missing: " + target + ": " + entry["file"])
    return target, required


def ply_compilation(runner, build, product, wheel=False):
    database = build / "compile_commands.json"
    runner.compilation(database, warnings=not wheel)
    commands = json.loads(database.read_text(encoding="utf8"))
    compiler = Path(shutil.which(runner.profile.cxx)).resolve()
    rows = []
    for entry in commands:
        tokens = entry.get("arguments") or shlex.split(entry["command"], posix=False)
        tokens = [token.strip('"') for token in tokens]
        third_party = "nanobind" in Path(entry["file"]).as_posix().lower()
        if not any(flag in tokens for flag in ("/MD", "-MD")):
            raise RuntimeError("PLY cohort requires dynamic release CRT per TU: " + entry["file"])
        if not third_party and not any(flag in tokens for flag in ("/std:c++20", "-std:c++20")):
            raise RuntimeError("First-party C++20 selection missing: " + entry["file"])
        if not third_party and "/WX" not in tokens and "-WX" not in tokens:
            raise RuntimeError("First-party warnings-as-errors missing: " + entry["file"])
        target, strict = ply_fp_selection(entry, tokens, product)
        rows.append({"file": entry["file"], "target": target, "third_party": third_party,
                     "strict_fp_required": strict, "strict_fp_present": any(x in tokens for x in ("/fp:strict", "-fp:strict")), "tokens": tokens})
    if ply_cache(build, "CMAKE_MSVC_RUNTIME_LIBRARY") != "MultiThreadedDLL":
        raise RuntimeError("Configured runtime differs from the selected dynamic release CRT")
    package_dirs = {}
    for key in ("MeshvaleGeometry_DIR", "MeshvaleInterchange_DIR", "tinyobjloader_DIR"):
        text = (build / "CMakeCache.txt").read_text(encoding="utf8")
        if re.search(r"^" + key + r":", text, re.MULTILINE):
            directory = Path(ply_cache(build, key)).resolve()
            allowed = (runner.work / "p", runner.work / "d/x64-windows-static-md")
            if not any(directory.is_relative_to(prefix.resolve()) for prefix in allowed):
                raise RuntimeError("Package discovery outside this run's SDK inputs: " + key)
            package_dirs[key] = str(directory)
    write_json(runner.evidence / (build.name + "-toolchain.json"), {
        "database_sha256": sha256(database), "compiler": str(compiler),
        "compiler_sha256": sha256(compiler), "cache_sha256": sha256(build / "CMakeCache.txt"),
        "generator": ply_cache(build, "CMAKE_GENERATOR"), "package_dirs": package_dirs, "translation_units": rows})


def ply_sdist(runner, source, label):
    dist = runner.work / "dist" / label
    runner.run(sys.executable, "-m", "build", "--sdist", "--no-isolation", "--outdir", dist, source)
    archives = list(dist.glob("*.tar.gz"))
    if len(archives) != 1:
        raise RuntimeError("Expected one fresh source package for " + label)
    runner.run(sys.executable, source / "scripts/check-package.py", archives[0])
    destination = runner.work / (label + "-source")
    destination.mkdir()
    with tarfile.open(archives[0], "r:gz") as archive:
        archive.extractall(destination, filter="data")
    roots = list(destination.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        raise RuntimeError("Source package must have one owning root")
    metadata = BytesParser().parsebytes((roots[0] / "PKG-INFO").read_bytes())
    if metadata["Name"] != "meshvale-" + label or not metadata["Version"]:
        raise RuntimeError("Source package Name/FULL missing or mismatched")
    write_json(runner.evidence / (label + "-sdist.json"), {
        "path": str(archives[0]), "sha256": sha256(archives[0]), "name": metadata["Name"],
        "full_version": metadata["Version"], "files": ply_signature(roots[0]),
        "scope": "Genuine configured-backend sdist; no node-only Git-archive Python route"})
    return roots[0], metadata["Version"]


def ply_wheel(runner, source, label, version, prefixes):
    build = runner.work / (label + "-wheel-build")
    output = runner.work / "python-wheels" / label
    ninja = Path(shutil.which("ninja")).resolve().as_posix()
    # Child-owned selection: never pair a Visual Studio generator with Ninja's program.
    runner.environment.update(CMAKE_GENERATOR="Ninja", CMAKE_PREFIX_PATH=prefixes)
    options = [f"-Cbuild-dir={build}", "-Ccmake.define.CMAKE_BUILD_TYPE=Release",
               "-Ccmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON",
               f"-Ccmake.define.CMAKE_MAKE_PROGRAM={ninja}",
               f"-Ccmake.define.CMAKE_CXX_COMPILER={Path(shutil.which('cl')).resolve().as_posix()}",
               "-Ccmake.define.CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
               "-Ccmake.define.CMAKE_CXX_FLAGS=/EHsc", "-Ccmake.define.BUILD_TESTING=OFF"]
    if label == "geometry":
        options += ["-Ccmake.define.CMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON"]
    if label == "repair":
        options += ["-Ccmake.define.MESHVALE_REPAIR_PYTHON=ON",
                    "-Ccmake.define.MESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON",
                    "-Ccmake.define.MESHVALE_REPAIR_GLTF_WORKFLOW=ON",
                    "-Ccmake.define.MESHVALE_REPAIR_GLTF_FILE_WORKFLOW=ON",
                    "-Ccmake.define.MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON",
                    "-Ccmake.define.MESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON"]
    runner.run(sys.executable, "-m", "build", "--wheel", "--no-isolation", "--outdir", output, *options, source)
    wheels = list(output.glob("*.whl"))
    if len(wheels) != 1:
        raise RuntimeError("Expected one fresh " + label + " wheel")
    with zipfile.ZipFile(wheels[0]) as archive:
        names = [name for name in archive.namelist() if name.endswith(".dist-info/METADATA")]
        if len(names) != 1:
            raise RuntimeError("Expected one wheel metadata document")
        metadata = BytesParser().parsebytes(archive.read(names[0]))
    if metadata["Name"] != "meshvale-" + label or metadata["Version"] != version:
        raise RuntimeError("Wheel identity differs from genuine source package FULL")
    selected = (["MESHVALE_REPAIR_PYTHON", "MESHVALE_REPAIR_MATERIALIZED_DUPLICATES",
                 "MESHVALE_REPAIR_GLTF_WORKFLOW", "MESHVALE_REPAIR_GLTF_FILE_WORKFLOW",
                 "MESHVALE_REPAIR_OFF_FILE_WORKFLOW", "MESHVALE_REPAIR_PLY_FILE_WORKFLOW"]
                if label == "repair" else ["MESHVALE_GEOMETRY_PYTHON"])
    if any(ply_cache(build, key).upper() not in ("ON", "TRUE", "1") for key in selected):
        raise RuntimeError("Wheel did not enable every selected native module")
    if ply_cache(build, "BUILD_TESTING").upper() not in ("OFF", "FALSE", "0"):
        raise RuntimeError("Production wheel unexpectedly enabled native test fixtures")
    if ply_cache(build, "CMAKE_GENERATOR") != "Ninja":
        raise RuntimeError("Wheel did not use selected Ninja generator")
    ply_compilation(runner, build, label, wheel=True)
    runner.run(sys.executable, source / "scripts/check-package.py", wheels[0])
    write_json(runner.evidence / (label + "-wheel.json"), {
        "path": str(wheels[0]), "sha256": sha256(wheels[0]), "name": metadata["Name"],
        "version": version, "build_directory": str(build), "actual_wheel_tu_database": True})
    return wheels[0]


def validate_ply_resource_observations(runner, fixture, validator, reports, literal):
    """Read real results separately from a caught allocator exception witness."""
    budget_name = "shared-budget-at-report"
    allocation_name = "allocation-failure-at-report"
    budget = fixture / budget_name
    allocation = fixture / allocation_name
    control_names = ("baseline", "pressure", "released-with-foreign")
    if {p.name for p in budget.iterdir()} != set(control_names) | {"source.ply", "budget-witness.json"}:
        raise RuntimeError("Budget resource fixture inventory differs")
    if {p.name for p in allocation.iterdir()} != {"source.ply", "captured.ply", "exception-witness.json"}:
        raise RuntimeError("Allocator exception must not fabricate native/report/publication files")

    def witness(path):
        if path.stat().st_size > 16 * 1024:
            raise RuntimeError("Bounded resource witness exceeded")
        return json.loads(path.read_text(encoding="utf8"))

    b = witness(budget / "budget-witness.json")
    budget_keys = {"schema", "workflow_calls", "source_baseline_bytes", "root_budget_bytes",
                   "baseline_report_request_bytes", "wrapper_cap_bytes", "wrapper_live_bytes_at_report",
                   "root_live_bytes_before_pressure", "shared_pressure_bytes", "foreign_pressure_bytes",
                   "root_available_after_pressure", "shares_root", "foreign_shares_root",
                   "pressure_backend_report_requests", "root_bytes_after_pressure_release",
                   "root_bytes_after_all_owners", "foreign_bytes_after_all_owners", "report_bytes",
                   "report_diagnostic_stage", "report_diagnostic_code"}
    if set(b) != budget_keys or b["schema"] != "meshvale.repair.ply-budget-witness/1":
        raise RuntimeError("Closed budget witness differs")
    for key in budget_keys - {"schema", "shares_root", "foreign_shares_root"}:
        if type(b[key]) is not int or b[key] < 0:
            raise RuntimeError("Actual budget witness integer expected: " + key)
    if (b["workflow_calls"] != 3 or b["source_baseline_bytes"] <= 0 or
            b["shared_pressure_bytes"] <= 0 or b["shared_pressure_bytes"] != b["foreign_pressure_bytes"] or
            b["shares_root"] is not True or b["foreign_shares_root"] is not False or
            b["root_available_after_pressure"] >= b["baseline_report_request_bytes"] or
            b["root_live_bytes_before_pressure"] + b["shared_pressure_bytes"] +
            b["root_available_after_pressure"] != b["root_budget_bytes"] or
            b["wrapper_live_bytes_at_report"] + 2 * b["baseline_report_request_bytes"] > b["wrapper_cap_bytes"] or
            b["pressure_backend_report_requests"] != 0 or b["report_bytes"] != 0 or
            b["report_diagnostic_stage"] != 7 or b["report_diagnostic_code"] != 8 or
            b["root_bytes_after_all_owners"] != 0 or b["foreign_bytes_after_all_owners"] != 0):
        raise RuntimeError("Real shared pressure, local headroom or release observations differ")
    maps = literal["changed"]["maps"]
    fields = dict(zip(("face_map", "corner_map", "source_faces", "source_corners"),
                      ("FaceMap", "CornerMap", "SourceFaces", "SourceCorners")))
    actual_source = (budget / "source.ply").read_bytes()
    source_sha = hashlib.sha256(actual_source).hexdigest()
    if source_sha != literal["source_hashes"]["ascii"]:
        raise RuntimeError("Resource source differs from the literal original bytes")
    final_reports, native_rows = [], []
    for name in control_names:
        directory = budget / name
        native_path = directory / "native.json"
        native = json.loads(native_path.read_text(encoding="utf8"))
        if ((directory / "captured.ply").read_bytes() != actual_source or
                any(native[key] != maps[value] for key, value in fields.items()) or
                native["candidate_outcome"] != "accepted" or native["repair_completed"] is not True or
                native["memory_output_verified"] is not True or native["publisher"] is not None or
                native["publication_outcome"] != "not-requested" or
                native["input_sha256"] != source_sha or native["reload_encoding"] != "binary_big_endian"):
            raise RuntimeError("Real resource record lost completed facts or source provenance: " + name)
        report_path = directory / "report.json"
        if name == "pressure":
            if (native["status"] != 4 or native["stage"] != "report" or native["exit_code"] != 2 or
                    native["diagnostic"]["stage"] != 7 or native["diagnostic"]["code"] != 8 or
                    report_path.exists()):
                raise RuntimeError("Shared refusal must retain actual budget/report/no-JSON outcome")
        else:
            if native["status"] != 0 or native["stage"] != "complete" or native["exit_code"] != 0:
                raise RuntimeError("Unchanged-cap positive control did not genuinely succeed")
            raw = report_path.read_bytes()
            report = reports.load(raw.decode("utf8"))
            validator.validate(report)
            ext = report["extensions"]["meshvale.repair.ply"]
            if (reports.exit_code(report) != 0 or report["candidate"]["outcome"] != "accepted" or
                    report["publication"]["outcome"] != "not-requested" or report["publication"]["artifacts"] or
                    ext["native_stage"] != "report" or ext["native_status"] != 0 or
                    ext["publisher"] is not None or ext["memory_output_verified"] is not True or
                    report["input"]["sha256"] != source_sha or
                    ext["source_faces"] != maps["SourceFaces"] or ext["source_corners"] != maps["SourceCorners"]):
                raise RuntimeError("Real memory-control receipt or sampled report-stage facts differ")
            final_reports.append({"case": budget_name + "/" + name,
                                  "sha256": hashlib.sha256(raw).hexdigest(), "exit_code": 0})
        native_rows.append({"case": budget_name + "/" + name, "sha256": sha256(native_path)})

    e = witness(allocation / "exception-witness.json")
    exception_keys = {"schema", "exception", "native_result_returned", "exception_observed",
                      "workflow_calls", "after_repair_calls", "failed_requests", "failed_request_bytes",
                      "failed_request_alignment", "actual_wrapper_allocations", "active_backend_bytes_after_unwind",
                      "source_counts", "candidate_counts", "face_map", "corner_map", "source_faces",
                      "source_corners", "root_bytes_after_all_owners"}
    if (set(e) != exception_keys or e["schema"] != "meshvale.repair.ply-allocation-witness/1" or
            e["exception"] != "std.bad_alloc" or e["native_result_returned"] is not False or
            e["exception_observed"] is not True or e["workflow_calls"] != 1 or e["after_repair_calls"] != 1 or
            e["failed_requests"] != 1 or e["failed_request_bytes"] <= 0 or e["failed_request_alignment"] <= 0 or
            e["active_backend_bytes_after_unwind"] != 0 or e["root_bytes_after_all_owners"] != 0 or
            e["source_counts"] != {"vertices": 8, "faces": 3, "corners": 11} or
            e["candidate_counts"] != {"vertices": 8, "faces": 2, "corners": 7} or
            any(e[key] != maps[value] for key, value in fields.items()) or
            (allocation / "source.ply").read_bytes() != actual_source or
            (allocation / "captured.ply").read_bytes() != actual_source):
        raise RuntimeError("Actual allocator exception/retained-owner witness differs")
    return final_reports, native_rows, [{"case": allocation_name,
                                        "sha256": sha256(allocation / "exception-witness.json")}]


def validate_ply_request_reports(runner, geometry, fixture):
    """Validate seven real request cases separately from the closed33 corpus."""
    sys.path.insert(0, str(geometry / "python"))
    import meshvale_reports
    from jsonschema import Draft202012Validator
    validator = Draft202012Validator(meshvale_reports.schema_document())
    rejections = {"face-property-difference", "opposite-winding", "distinct-coincident-ids"}
    publication_failures = {"existing-destination", "staged-entry-corruption"}
    expected = publication_failures | rejections | {"interior-stop", "point-only"}
    if {path.name for path in fixture.iterdir()} != expected | {"shared-budget-at-report", "allocation-failure-at-report"}:
        raise RuntimeError("PLY request fixture inventory differs from the nine selected groups")
    literal = json.loads((runner.root / "tests/fixtures/ply-workflow/literal-predicates.json").read_text(encoding="utf8"))
    maps = literal["changed"]["maps"]
    map_fields = ("face_map", "corner_map", "source_faces", "source_corners")
    observed = []
    for name in sorted(expected):
        directory = fixture / name
        native = json.loads((directory / "native.json").read_text(encoding="utf8"))
        if name in rejections:
            if (any(native[key] is not None for key in map_fields) or
                    native["repair_completed"] is not False or
                    any(native[key] is not None for key in
                        ("candidate_mesh", "reload_mesh", "published_mesh", "input_sha256")) or
                    native["status"] != 6 or native["stage"] != "complete" or native["exit_code"] != 1 or
                    native["publisher"] is not None or native["memory_output_verified"] is not False):
                raise RuntimeError("Comparison refusal fabricated later facts or accepted empty maps: " + name)
            delegated = native["diagnostic"]["delegated"]
            if (native["diagnostic"]["stage"] != 2 or native["diagnostic"]["code"] != 10 or
                    delegated["kind"] != "repair" or delegated["status"] != 6 or
                    delegated["diagnostic"]["stage"] != 4 or delegated["diagnostic"]["code"] != 19 or
                    delegated["diagnostic"]["target"] != 0):
                raise RuntimeError("Expected actual target-zero comparison/NotEquivalent: " + name)
            counts = {"vertices": 4, "faces": 2, "corners": 6} if name == "distinct-coincident-ids" else {
                "vertices": 8, "faces": 3, "corners": 11}
            if native["source_mesh"]["counts"] != counts or (directory / "bundle").exists():
                raise RuntimeError("Refusal lost its actual source or created a publication: " + name)
        elif name == "point-only":
            if (any(native[key] != [] for key in map_fields) or native["repair_completed"] is not True or
                    native["status"] != 0 or native["stage"] != "complete" or native["exit_code"] != 0 or
                    native["memory_output_verified"] is not True or native["publisher"] is None or
                    native["source_mesh"]["counts"] != {"vertices": 8, "faces": 0, "corners": 0} or
                    any(native[key] != native["source_mesh"] for key in
                        ("candidate_mesh", "reload_mesh", "published_mesh")) or
                    native["source_encoding"] != "ascii" or
                    native["reload_encoding"] != "binary_big_endian" or
                    native["published_encoding"] != "binary_big_endian" or
                    native["reload_schema"] != native["source_schema"] or
                    native["published_schema"] != native["source_schema"]):
                raise RuntimeError("Point-only rows/schema/maps or actual big-endian publication differ")
        elif (native["face_map"] != maps["FaceMap"] or native["corner_map"] != maps["CornerMap"] or
                native["source_faces"] != maps["SourceFaces"] or native["source_corners"] != maps["SourceCorners"]):
            raise RuntimeError("Request sidecar lacks the four literal completed Repair maps: " + name)
        if name == "interior-stop":
            if (native["status"] != 5 or native["stage"] != "verify" or native["exit_code"] != 130 or
                    native["publisher"] is not None or native["memory_output_verified"] is not False):
                raise RuntimeError("Interior stop did not retain its typed canceled/verify/no-publisher facts")
            # FailureReport may return without JSON under a requested stop.
            continue
        raw = (directory / "report.json").read_bytes()
        report = meshvale_reports.load(raw.decode("utf8"))
        validator.validate(report)
        ext = report["extensions"]["meshvale.repair.ply"]
        if (ext["native_status"] != native["status"] or
                (name != "point-only" and ext["publisher"] != native["publisher"]) or
                ext["source_faces"] != native["source_faces"] or ext["source_corners"] != native["source_corners"] or
                ext["memory_output_verified"] != native["memory_output_verified"]):
            raise RuntimeError("Final receipt disagrees with actual typed request observations: " + name)
        if name in publication_failures:
            if (meshvale_reports.exit_code(report) != 2 or report["execution"]["outcome"] != "failed" or
                    report["execution"]["stage"] != "export" or report["candidate"]["outcome"] != "accepted" or
                    report["publication"]["outcome"] != "failed" or report["publication"]["artifacts"] or
                    ext["native_stage"] != "publication" or ext["memory_output_verified"] is not True):
                raise RuntimeError("Final failure receipt disagrees with its real publication outcome: " + name)
        elif name in rejections:
            if (meshvale_reports.exit_code(report) != 1 or report["execution"]["outcome"] != "completed" or
                    report["execution"]["stage"] != "complete" or report["candidate"]["outcome"] != "rejected" or
                    report["candidate"]["snapshot"] is not None or report["publication"]["outcome"] != "not-attempted" or
                    report["publication"]["artifacts"] or report["maps"] or report["input"]["sha256"] is not None or
                    ext["native_stage"] != "complete" or ext["write_facts"] is not None or
                    ext["native_diagnostic"] != native["diagnostic"] or
                    any(ext["phase_diagnostics"][key] is not None for key in ("writing", "reload"))):
                raise RuntimeError("Rejected receipt claims an unreached candidate/write/hash/publication: " + name)
        else:
            bundle = directory / "bundle"
            captured = (directory / "captured.ply").read_bytes()
            entry = (bundle / "source.ply").read_bytes()
            artifacts = report["publication"]["artifacts"]
            facts = ext["write_facts"]
            # Committed receipt bytes were prepared before the publisher returned;
            # its actual final typed result is in native.json, not fabricated in JSON.
            if (ext["publisher"] is not None or
                    meshvale_reports.exit_code(report) != 0 or report["execution"]["outcome"] != "completed" or
                    report["candidate"]["outcome"] != "unchanged" or report["publication"]["outcome"] != "published" or
                    {path.name for path in bundle.iterdir()} != {"source.ply", "meshvale-report.json"} or
                    (bundle / "meshvale-report.json").read_bytes() != raw or
                    report["input"]["sha256"] != hashlib.sha256(captured).hexdigest() or
                    native["input_sha256"] != report["input"]["sha256"] or len(artifacts) != 1 or
                    artifacts[0]["sha256"] != hashlib.sha256(entry).hexdigest() or
                    not entry.startswith(b"ply\nformat binary_big_endian 1.0\n") or
                    facts["source_vertex_count"] != 8 or facts["source_face_count"] != 0 or
                    facts["vertex_count"] != 8 or facts["face_count"] != 0 or facts["corner_count"] != 0 or
                    facts["property_count"] != 11 or report["operation"]["options"]["targets"] or
                    report["operation"]["options"]["row_local_attributes"]):
                raise RuntimeError("Actual point-only binary bytes, hashes, receipt or unchanged writer facts differ")
        observed.append({"case": name, "sha256": hashlib.sha256(raw).hexdigest(),
                         "exit_code": meshvale_reports.exit_code(report)})
    resource_reports, resource_native, exceptions = validate_ply_resource_observations(
        runner, fixture, validator, meshvale_reports, literal)
    observed.extend(resource_reports)
    if len(observed) != 8:
        raise RuntimeError("Expected exactly eight available final request reports")
    retained = runner.evidence / "ply-request-observations"
    source_signature = ply_signature(fixture)
    shutil.copytree(fixture, retained)
    if source_signature != ply_signature(retained):
        raise RuntimeError("Retained request artifact bytes changed during copy")
    write_json(runner.evidence / "ply-request-checks.json", {
        "schema": "meshvale.repair.ply-request-checks/1", "passed": True,
        "actual_cases": 9, "actual_workflow_calls": 11, "final_reports": observed, "native_sidecars": [
            {"case": name, "sha256": sha256(fixture / name / "native.json")} for name in sorted(expected)] + resource_native,
        "exception_witnesses": exceptions,
        "retained_raw_files": source_signature,
        "scope": "Nine logical request groups, eleven actual workflows, ten real native sidecars, eight final reports and one separate allocator exception witness; canceled/budget JSON absent; closed33 unchanged"})

def qualify_ply(root, profile_name):
    """One bounded qualified Windows route, independent of the older cohorts."""
    if (profile_name != "msvc-release" or platform.system() != "Windows" or
            sys.version_info[:2] != (3, 13) or sys.implementation.name != "cpython" or
            struct.calcsize("P") != 8 or sysconfig.get_config_var("Py_GIL_DISABLED")):
        raise RuntimeError("PLY CI requires Windows x64 ordinary CPython3.13 msvc-release")
    work = root / ".local/gw"
    runner = Runner(root, work, PROFILES[profile_name])
    result = {"gate_passed": False, "profile": profile_name, "completed": [],
              "runtime_floor_qualified": False, "interchange_python_required": False}
    try:
        geometry, interchange, vcpkg = (work / name for name in ("g", "i", "v"))
        inputs = {"profile": vars(runner.profile), "sources": {}, "event": {
            key: os.environ.get(key) for key in ("GITHUB_EVENT_NAME", "GITHUB_SHA", "GITHUB_REF",
                                                "GITHUB_HEAD_REF", "GITHUB_BASE_REF")}}
        for name, source, expected in (("geometry", geometry, GEOMETRY_REVISION),
                                      ("interchange", interchange, INTERCHANGE_PLY_FILE_REVISION),
                                      ("vcpkg", vcpkg, VCPKG_REVISION), ("repair", root, None)):
            head = runner.run("git", "-C", source, "rev-parse", "HEAD").strip()
            if ((expected is not None and head != expected) or
                    runner.run("git", "-C", source, "status", "--porcelain").strip()):
                raise RuntimeError(name + ": expected clean exact owning checkout")
            inputs["sources"][name] = {"head": head,
                "tree": runner.run("git", "-C", source, "rev-parse", "HEAD^{tree}").strip(),
                "parents": runner.run("git", "-C", source, "show", "-s", "--format=%P", "HEAD").strip().split()}
        actual_head = inputs["sources"]["repair"]["head"]
        if os.environ.get("GITHUB_SHA") and os.environ["GITHUB_SHA"] != actual_head:
            raise RuntimeError("Actual checkout differs from workflow event SHA")
        event_path = os.environ.get("GITHUB_EVENT_PATH")
        if event_path:
            event = json.loads(Path(event_path).read_text(encoding="utf8"))
            pull = event.get("pull_request")
            if pull:
                inputs["event"]["pull_request"] = {"number": event["number"], "head": pull["head"]["sha"],
                    "base": pull["base"]["sha"], "merge_commit_sha": pull.get("merge_commit_sha")}
        write_json(runner.evidence / "inputs.json", inputs)
        for tool in ("cmake", "ctest", "ninja", "cl"):
            if shutil.which(tool) is None:
                raise RuntimeError("Required tool missing: " + tool)
        runner.run("cmake", "--version")
        runner.run("ninja", "--version")
        runner.environment["CMAKE_GENERATOR"] = "Ninja"
        runner.run("cmd", "/d", "/c", str(vcpkg / "bootstrap-vcpkg.bat"), "-disableMetrics")
        prefix, dependencies, triplet = work / "p", work / "d", "x64-windows-static-md"
        gb, ib, rb = (work / name for name in ("gb", "ib", "rb"))
        runtime = "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
        runner.configure(geometry, gb, runtime, "-DBUILD_TESTING=ON", "-DMESHVALE_GEOMETRY_PYTHON=OFF",
                         "-DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON", "-DMESHVALE_GEOMETRY_ALLOCATION_PROBE=ON",
                         "-DMESHVALE_GEOMETRY_MATERIALIZATION_TEST_PROFILE=qualified")
        runner.build(gb)
        ply_compilation(runner, gb, "geometry")
        runner.inventory(gb, "geometry")
        runner.check_inventory(geometry, gb, "--allocation-probe", "enabled")
        runner.test(gb)
        runner.install(gb, prefix)
        overlay = runner.evidence / "triplets"
        overlay.mkdir()
        compiler = Path(shutil.which("cl")).resolve().as_posix()
        (overlay / (triplet + ".cmake")).write_text(
            f'include("{(vcpkg / "triplets" / (triplet + ".cmake")).as_posix()}")\n'
            'list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n'
            f'  "-DCMAKE_CXX_COMPILER={compiler}" "-DCMAKE_C_COMPILER={compiler}")\n'
            'list(APPEND VCPKG_ENV_PASSTHROUGH CC CXX)\n', encoding="utf8")
        runner.configure(interchange, ib, runtime, "-DBUILD_TESTING=ON",
            f"-DCMAKE_PREFIX_PATH={prefix}", f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
            f"-DVCPKG_TARGET_TRIPLET={triplet}", f"-DVCPKG_OVERLAY_TRIPLETS={overlay}",
            f"-DVCPKG_INSTALLED_DIR={dependencies}", "-DMESHVALE_INTERCHANGE_PYTHON=OFF",
            "-DMESHVALE_INTERCHANGE_USD=OFF", "-DMESHVALE_INTERCHANGE_FBX=OFF",
            "-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=ON", "-DMESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT=ON",
            "-DMESHVALE_INTERCHANGE_GLTF_FILES=ON", "-DMESHVALE_INTERCHANGE_OFF_FILES=ON",
            "-DMESHVALE_INTERCHANGE_PLY_FILES=ON", "-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE=qualified")
        runner.build(ib)
        ply_compilation(runner, ib, "interchange")
        runner.compilation(vcpkg / "buildtrees/tinyobjloader" / (triplet + "-rel") / "compile_commands.json")
        runner.inventory(ib, "interchange")
        runner.check_inventory(interchange, ib, "--topology-write", "ON", "--workflow-support", "ON",
                               "--gltf-files", "ON", "--off-files", "ON", "--ply-files", "ON")
        runner.test(ib, True)
        runner.install(ib, prefix)
        sdk_versions = {package: ply_sdk_version(prefix, package)
                        for package in ("MeshvaleGeometry", "MeshvaleInterchange")}
        write_json(runner.evidence / "sdk-inputs.json", {"combined_geometry_interchange": ply_signature(prefix),
                   "tiny_prefix": ply_signature(dependencies / triplet), "pins": inputs["sources"],
                   "native_versions": sdk_versions,
                   "runtime": "MultiThreadedDLL", "scope": "Fresh same-run SDK inputs; no relocation/runtime-floor claim"})
        result["completed"].append("fresh-input-sdks")
        runner.configure(root, rb, runtime, f"-DCMAKE_PREFIX_PATH={prefix};{dependencies / triplet}",
            "-DBUILD_TESTING=ON", "-DMESHVALE_REPAIR_PYTHON=OFF", "-DMESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON",
            "-DMESHVALE_REPAIR_GLTF_WORKFLOW=OFF", "-DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW=OFF",
            "-DMESHVALE_REPAIR_OFF_FILE_WORKFLOW=OFF", "-DMESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON",
            "-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=qualified")
        runner.build(rb)
        ply_compilation(runner, rb, "repair")
        runner.inventory(rb, "repair", 6)
        runner.check_inventory(root, rb, "--gltf-workflow", "OFF", "--gltf-file-workflow", "OFF",
                               "--off-file-workflow", "OFF", "--ply-file-workflow", "ON")
        try:
            runner.run("ctest", "--test-dir", rb, "-C", "Release", "--verbose", "--output-on-failure",
                       "--timeout", FILE_TEST_TIMEOUT_SECONDS, "--output-junit", rb / "Testing/ply-junit.xml",
                       timeout=FILE_SUITE_TIMEOUT_SECONDS)
        finally:
            log = rb / "Testing/Temporary/LastTest.log"
            if log.is_file():
                shutil.copyfile(log, runner.evidence / "rb-actual-LastTest.log")
        fixtures = list(rb.glob("ply-workflow-fixture-*"))
        if len(fixtures) != 1 or not fixtures[0].is_dir():
            raise RuntimeError("Need exactly one retained native fixture epoch from the fresh CTest run")
        fixture = fixtures[0]
        native_version = ply_cache(rb, "CMAKE_PROJECT_VERSION")
        if native_version != "0.0.0":
            raise RuntimeError("Native baseline differs from selected untagged development source")
        cases = runner.evidence / "ply-cases.json"
        checks = runner.evidence / "ply-checks.json"
        checker = root / "scripts/check-ply-workflow-reports.py"
        runner.run(sys.executable, checker, "make-cases", "--fixture-root", fixture,
                   "--native-cache", rb / "CMakeCache.txt", "--output", cases)
        runner.run(sys.executable, checker, "check", "--cases", cases, "--receipt", checks,
                   "--require-chains18", "--shared-schema", geometry / "python/meshvale_reports/report-v1.schema.json",
                   "--require-shared-validation")
        check = json.loads(checks.read_text(encoding="utf8"))
        if not check["passed"] or len(check["cases"]) != 33:
            raise RuntimeError("Public closed native checker did not accept all33 cases")
        request_roots = list(rb.glob("ply-workflow-requests-*"))
        if len(request_roots) != 1 or not request_roots[0].is_dir():
            raise RuntimeError("Need one separate actual request fixture epoch")
        validate_ply_request_reports(runner, geometry, request_roots[0])
        result["completed"].append("native-six-public33-and-request2")
        geometry_source, geometry_version = ply_sdist(runner, geometry, "geometry")
        if geometry_version != GEOMETRY_PYTHON_VERSION:
            raise RuntimeError("Fresh Geometry backend version differs from exact required pin")
        repair_source, repair_version = ply_sdist(runner, root, "repair")
        prefixes = os.pathsep.join((str(prefix), str(dependencies / triplet)))
        geometry_wheel = ply_wheel(runner, geometry_source, "geometry", geometry_version, prefixes)
        repair_wheel = ply_wheel(runner, repair_source, "repair", repair_version, prefixes)
        result["completed"].append("genuine-sdist-wheels")
        temporary = Path(os.environ["RUNNER_TEMP"]).resolve(strict=True)
        installed_root = temporary / ("meshvale-ply-" + actual_head)
        write_json(runner.evidence / "installed-location.json", {"path": str(installed_root),
                   "scope": "Fresh retained interpreter/cwd outside checkout"})
        runner.run(sys.executable, root / "scripts/test-installed.py", "--geometry-wheel", geometry_wheel,
                   "--repair-wheel", repair_wheel, "--ply", "--ply-fixture-root", fixture,
                   "--ply-native-version", native_version, "--ply-cases", cases,
                   "--ply-output-root", installed_root, timeout=FILE_SUITE_TIMEOUT_SECONDS)
        installed = json.loads((installed_root / "qualification.json").read_text(encoding="utf8"))
        if installed["passed"] is not True:
            raise RuntimeError("Installed PLY integration did not pass")
        result.update(gate_passed=True, native_tests=6, native_cases=33, shared_reports=31,
                      native_request_cases=9, native_request_workflow_calls=11, native_request_sidecars=10,
                      native_request_reports=8, native_request_exception_witnesses=1,
                      journal_counts=installed["journal_counts"], bundles=installed["bundles"])
        result["completed"].append("installed-ply-and-full31")
    except BaseException as error:
        result["error"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        write_json(runner.evidence / "result.json", result)


def ply_gnu_compilation(runner, build, product, prefixes):
    """Audit the genuine GNU build, without imposing the MSVC CRT contract."""
    database = build / "compile_commands.json"
    runner.compilation(database, warnings=True)
    if ply_cache(build, "CMAKE_GENERATOR") != "Ninja":
        raise RuntimeError("GNU PLY requires the explicit Ninja generator")
    records = list((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if len(records) != 1:
        raise RuntimeError("Need one actual GNU compiler identity record")
    compiler_record = records[0].read_text(encoding="utf8")
    if (not re.search(r'set\(CMAKE_CXX_COMPILER_ID "GNU"\)', compiler_record) or
            not re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "13\.[^"]+"\)', compiler_record)):
        raise RuntimeError("Actual compiler identity differs from GNU 13")
    if not re.search(r'set\(CMAKE_CXX_SIZEOF_DATA_PTR "8"\)', compiler_record):
        raise RuntimeError("Actual GNU target must have 64-bit pointers")
    home = Path(ply_cache(build, "CMAKE_HOME_DIRECTORY")).resolve(strict=True)
    rows = []
    for entry in json.loads(database.read_text(encoding="utf8")):
        tokens = entry.get("arguments") or shlex.split(entry["command"])
        if "-std=c++20" not in tokens:
            raise RuntimeError("GNU first-party C++20 selection missing: " + entry["file"])
        include_dirs = []
        for index, token in enumerate(tokens):
            if token in ("-I", "-isystem"):
                if index + 1 == len(tokens):
                    raise RuntimeError("Missing include-directory argument")
                include_dirs.append(tokens[index + 1])
            elif token.startswith("-I"):
                include_dirs.append(token[2:])
        for value in include_dirs:
            directory = Path(value)
            if not directory.is_absolute():
                directory = Path(entry["directory"]) / directory
            if not any(directory.resolve().is_relative_to(prefix.resolve()) for prefix in [home, build, *prefixes]):
                raise RuntimeError("GNU include fallback escaped source/build/SDK inputs: " + value)
        target, strict = ply_fp_selection(entry, tokens, product, gnu=True)
        rows.append({"file": entry["file"], "target": target, "strict_fp_required": strict,
                     "tokens": tokens})
    package_dirs = {}
    cache = (build / "CMakeCache.txt").read_text(encoding="utf8")
    for key in ("MeshvaleRepair_DIR", "MeshvaleGeometry_DIR", "MeshvaleInterchange_DIR", "tinyobjloader_DIR"):
        if re.search(r"^" + key + r":", cache, re.MULTILINE):
            directory = Path(ply_cache(build, key)).resolve(strict=True)
            if not any(directory.is_relative_to(prefix.resolve(strict=True)) for prefix in prefixes):
                raise RuntimeError("GNU package discovery escaped the selected prefixes: " + key)
            package_dirs[key] = str(directory)
    commands = runner.run("ninja", "-C", build, "-t", "commands")
    libraries = {}
    for line in commands.splitlines():
        for token in shlex.split(line):
            path = Path(token)
            if path.is_absolute() and path.suffix in (".a", ".so"):
                if not path.is_file() or not any(path.resolve().is_relative_to(prefix.resolve()) for prefix in [build, *prefixes]):
                    raise RuntimeError("GNU static/shared library escaped selected prefixes: " + token)
                libraries[str(path)] = sha256(path)
    write_json(runner.evidence / (build.name + "-gnu-toolchain.json"), {
        "database_sha256": sha256(database), "cache_sha256": sha256(build / "CMakeCache.txt"),
        "compiler_record_sha256": sha256(records[0]), "compiler_sha256": sha256(Path(shutil.which(runner.profile.cxx)).resolve()),
        "package_dirs": package_dirs, "library_inputs": libraries, "translation_units": rows,
        "scope": "Exact object targets and actual GNU compiler/link inputs; no archive object-extraction claim"})


def validate_ply_unsupported(runner, geometry, build, label):
    """Read one true empty receipt separately from a capture-only refusal."""
    roots = list(build.glob("ply-workflow-unsupported-*"))
    if len(roots) != 1 or not roots[0].is_dir():
        raise RuntimeError("Need exactly one fresh unsupported PLY fixture root")
    fixture = roots[0]
    if {p.name for p in fixture.iterdir()} != {"nonempty.ply", "captured-empty.ply", "bundle", "observations.json"}:
        raise RuntimeError("Unsupported PLY fixture inventory differs")
    bundle = fixture / "bundle"
    if {p.name for p in bundle.iterdir()} != {"empty.ply", "meshvale-report.json"}:
        raise RuntimeError("Empty publication must contain exactly entry and receipt")
    members = list(fixture.rglob("*"))
    if len(members) != 6 or any(p.is_symlink() or (p != bundle and not p.is_file()) for p in members):
        raise RuntimeError("Unexpected non-regular fixture member")
    observation = json.loads((fixture / "observations.json").read_text(encoding="utf8"))
    # Existing public enum ordinals, not substitutes for a native result:
    # I file kPlyFailure/kPlyRead/kDelegatedPly, PLY kGeometryFailure, G kUnsupportedAllocationProfile.
    expected = {"capture_status": 9, "capture_stage": 3, "capture_code": 17,
                "ply_status": 10, "geometry_status": 4, "source_encoding": 1,
                "reload_encoding": 1, "published_encoding": 1, "exit_code": 0,
                "active_workers_with_retained_record": 0}
    if (set(observation) != set(expected) | {"schema", "input_sha256"} or
            observation["schema"] != "meshvale.repair.ply-unsupported/1" or
            any(type(observation[key]) is not int or observation[key] != value for key, value in expected.items())):
        raise RuntimeError("Actual capture-only/empty native observations differ")
    sys.path.insert(0, str(geometry / "python"))
    import meshvale_reports
    from jsonschema import Draft202012Validator
    path = bundle / "meshvale-report.json"
    raw = path.read_bytes()
    report = meshvale_reports.load(raw.decode("utf8"))
    Draft202012Validator(meshvale_reports.schema_document()).validate(report)
    extension = report["extensions"]["meshvale.repair.ply"]
    input_sha = sha256(fixture / "captured-empty.ply")
    output_sha = sha256(bundle / "empty.ply")
    if (meshvale_reports.exit_code(report) != 0 or meshvale_reports.profile_outcome(report) != "passed" or
            report["execution"]["outcome"] != "completed" or report["execution"]["stage"] != "complete" or
            report["candidate"]["outcome"] != "unchanged" or report["publication"]["outcome"] != "published" or
            report["publication"]["verified"] is not True or report["input"]["sha256"] != input_sha or
            observation["input_sha256"] != input_sha or
            report["publication"]["artifacts"] != [{"id": "entry", "sha256": output_sha}] or
            report["operation"]["options"] != {"encoding": "ascii", "rewrite": "canonical", "targets": [], "row_local_attributes": []} or
            extension["native_status"] != 0 or extension["native_stage"] != "complete" or
            extension["memory_output_verified"] is not True or
            extension["source_faces"] != [] or extension["source_corners"] != [] or
            extension["publisher"] is not None or
            extension["artifact_paths"] != {"entry": "empty.ply", "receipt": "meshvale-report.json"}):
        raise RuntimeError("Actual empty receipt/final physical bytes differ from the selected contract")
    facts = extension["write_facts"]
    if (facts["encoding"] != "ascii" or facts["source_encoding"] != "ascii" or
            facts["source_vertex_count"] != 0 or facts["source_face_count"] != 0 or
            facts["vertex_count"] != 0 or facts["face_count"] != 0 or facts["corner_count"] != 0 or
            facts["property_count"] != 4 or any(row["kind"] != "identity" or row["entries"] is not None for row in report["maps"]) or
            len(report["maps"]) != 6):
        raise RuntimeError("Empty writer/map provenance differs")
    for snapshot in report["snapshots"]:
        if any(any(mesh[key] != 0 for key in ("vertex_count", "face_count", "corner_count")) for mesh in snapshot["meshes"]):
            raise RuntimeError("Empty snapshots contain numerical rows")
    if ply_cache(build, "CMAKE_PROJECT_NAME") == "MeshvaleRepair" and report["tool"]["version"] != ply_cache(build, "CMAKE_PROJECT_VERSION"):
        raise RuntimeError("Native receipt tool version differs from the actual build")
    signature = ply_signature(fixture)
    retained = runner.evidence / (label + "-unsupported-fixtures")
    shutil.copytree(fixture, retained)
    if signature != ply_signature(retained):
        raise RuntimeError("Retained unsupported fixture copy changed raw bytes")
    write_json(runner.evidence / (label + "-unsupported-checks.json"), {
        "passed": True, "native_workflow_calls": 1, "capture_only_refusals": 1,
        "reports": [{"sha256": hashlib.sha256(raw).hexdigest(), "input_sha256": input_sha,
                     "output_sha256": output_sha, "exit_code": meshvale_reports.exit_code(report)}],
        "validator_sha256": sha256(Path(meshvale_reports.__file__)), "retained_files": signature,
        "scope": "One genuine empty publication and one earlier nonempty capture refusal; no nonempty Repair result"})


def qualify_ply_unsupported(root, profile_name):
    """Native-only GNU producer/installed/archive boundary, with no Python wheel route."""
    if profile_name != "gnu-unsupported" or platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
        raise RuntimeError("Native unsupported PLY lane requires Linux x64 GNU 13")
    work = root / ".local/gw"
    runner = Runner(root, work, PROFILES[profile_name])
    result = {"gate_passed": False, "profile": profile_name, "completed": [],
              "python_qualified": False, "runtime_floor_qualified": False}
    try:
        geometry, interchange, vcpkg = (work / name for name in ("g", "i", "v"))
        inputs = {"profile": vars(runner.profile), "sources": {}}
        for name, source, revision in (("geometry", geometry, GEOMETRY_REVISION),
                ("interchange", interchange, INTERCHANGE_PLY_FILE_REVISION),
                ("vcpkg", vcpkg, VCPKG_REVISION), ("repair", root, None)):
            head = runner.run("git", "-C", source, "rev-parse", "HEAD").strip()
            if (revision and head != revision) or runner.run("git", "-C", source, "status", "--porcelain").strip():
                raise RuntimeError("Expected exact clean source: " + name)
            inputs["sources"][name] = {"head": head,
                "tree": runner.run("git", "-C", source, "rev-parse", "HEAD^{tree}").strip(),
                "parents": runner.run("git", "-C", source, "show", "-s", "--format=%P", "HEAD").strip().split()}
        head = inputs["sources"]["repair"]["head"]
        if os.environ.get("GITHUB_SHA") and os.environ["GITHUB_SHA"] != head:
            raise RuntimeError("Actual checkout differs from workflow event SHA")
        event_path = os.environ.get("GITHUB_EVENT_PATH")
        if event_path:
            event = json.loads(Path(event_path).read_text(encoding="utf8"))
            pull = event.get("pull_request")
            if pull:
                inputs["pull_request"] = {"number": event["number"], "head": pull["head"]["sha"],
                                          "base": pull["base"]["sha"], "merge_commit_sha": pull.get("merge_commit_sha")}
        write_json(runner.evidence / "inputs.json", inputs)
        for tool in ("cmake", "ctest", "ninja", runner.profile.cxx, runner.profile.cc):
            if shutil.which(tool) is None:
                raise RuntimeError("Required GNU tool missing: " + tool)
        if runner.run(runner.profile.cxx, "-dumpfullversion").strip().split(".")[0] != "13":
            raise RuntimeError("Selected GNU compiler must actually be version 13")
        runner.run("cmake", "--version")
        runner.run("ninja", "--version")
        runner.environment["CMAKE_GENERATOR"] = "Ninja"
        runner.run("bash", vcpkg / "bootstrap-vcpkg.sh", "-disableMetrics")
        prefix, dependencies, triplet = work / "p", work / "d", "x64-linux"
        gb, ib, rb = (work / name for name in ("gb", "ib", "rb"))
        runner.configure(geometry, gb, "-DBUILD_TESTING=ON", "-DMESHVALE_GEOMETRY_PYTHON=OFF",
            "-DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON", "-DMESHVALE_GEOMETRY_ALLOCATION_PROBE=ON",
            "-DMESHVALE_GEOMETRY_MATERIALIZATION_TEST_PROFILE=unsupported")
        runner.build(gb)
        ply_gnu_compilation(runner, gb, "geometry", [])
        runner.inventory(gb, "geometry")
        runner.check_inventory(geometry, gb, "--allocation-probe", "enabled")
        runner.run("ctest", "--test-dir", gb, "-C", "Release", "-R", "^unsupported_materialization_profile$",
                   "--verbose", "--output-on-failure", "--output-junit", gb / "Testing/unsupported-junit.xml")
        runner.install(gb, prefix)
        overlay = runner.evidence / "triplets"
        overlay.mkdir()
        (overlay / (triplet + ".cmake")).write_text(
            f'include("{(vcpkg / "triplets" / (triplet + ".cmake")).as_posix()}")\n'
            'list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n'
            f'  "-DCMAKE_CXX_COMPILER={Path(shutil.which(runner.profile.cxx)).resolve().as_posix()}"\n'
            f'  "-DCMAKE_C_COMPILER={Path(shutil.which(runner.profile.cc)).resolve().as_posix()}")\n'
            'list(APPEND VCPKG_ENV_PASSTHROUGH CC CXX)\n', encoding="utf8")
        runner.configure(interchange, ib, "-DBUILD_TESTING=ON", f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake", f"-DVCPKG_TARGET_TRIPLET={triplet}",
            f"-DVCPKG_OVERLAY_TRIPLETS={overlay}", f"-DVCPKG_INSTALLED_DIR={dependencies}",
            "-DMESHVALE_INTERCHANGE_PYTHON=OFF", "-DMESHVALE_INTERCHANGE_USD=OFF", "-DMESHVALE_INTERCHANGE_FBX=OFF",
            "-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=OFF", "-DMESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT=OFF",
            "-DMESHVALE_INTERCHANGE_GLTF_FILES=OFF", "-DMESHVALE_INTERCHANGE_OFF_FILES=OFF",
            "-DMESHVALE_INTERCHANGE_PLY_FILES=ON", "-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE=unsupported")
        runner.build(ib)
        ply_gnu_compilation(runner, ib, "interchange", [prefix, dependencies / triplet])
        runner.compilation(vcpkg / "buildtrees/tinyobjloader" / (triplet + "-rel") / "compile_commands.json")
        runner.inventory(ib, "interchange")
        runner.check_inventory(interchange, ib, "--topology-write", "OFF", "--workflow-support", "OFF",
                               "--gltf-files", "OFF", "--off-files", "OFF", "--ply-files", "ON")
        runner.run("ctest", "--test-dir", ib, "-C", "Release", "-R", "^ply_files_(semantics|requests|consumer)$",
            "--verbose", "--output-on-failure", "--timeout", FILE_TEST_TIMEOUT_SECONDS,
            "--output-junit", ib / "Testing/ply-unsupported-junit.xml", timeout=FILE_SUITE_TIMEOUT_SECONDS)
        runner.install(ib, prefix)
        write_json(runner.evidence / "sdk-inputs.json", {
            "sources": inputs["sources"], "native_versions": {name: ply_sdk_version(prefix, name)
            for name in ("MeshvaleGeometry", "MeshvaleInterchange")},
            "combined_geometry_interchange": ply_signature(prefix), "tiny_prefix": ply_signature(dependencies / triplet),
            "scope": "Fresh GNU SDKs; G one and I three selected tests, not their complete registered inventories"})
        result["completed"].append("genuine-gnu-producers")
        options = ["-DBUILD_TESTING=ON", "-DMESHVALE_REPAIR_PYTHON=OFF",
            "-DMESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON", "-DMESHVALE_REPAIR_GLTF_WORKFLOW=OFF",
            "-DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW=OFF", "-DMESHVALE_REPAIR_OFF_FILE_WORKFLOW=OFF",
            "-DMESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON", "-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=unsupported"]
        runner.configure(root, rb, *options, f"-DCMAKE_PREFIX_PATH={prefix};{dependencies / triplet}")
        runner.build(rb)
        ply_gnu_compilation(runner, rb, "repair", [prefix, dependencies / triplet])
        runner.inventory(rb, "repair", 4)
        runner.check_inventory(root, rb, "--gltf-workflow", "OFF", "--gltf-file-workflow", "OFF",
                               "--off-file-workflow", "OFF", "--ply-file-workflow", "ON")
        runner.test(rb, True)
        validate_ply_unsupported(runner, geometry, rb, "native")
        runner.install(rb, prefix)
        write_json(runner.evidence / "repair-sdk.json", {
            "version": ply_sdk_version(prefix, "MeshvaleRepair"), "installed_files": ply_signature(prefix)})
        relocated, tiny = work / "q", work / "tiny-relocated"
        shutil.copytree(prefix, relocated)
        shutil.copytree(dependencies / triplet, tiny)
        if ply_signature(prefix) != ply_signature(relocated) or ply_signature(dependencies / triplet) != ply_signature(tiny):
            raise RuntimeError("Relocation changed installed SDK bytes")
        consumer = work / "consumer-source"
        shutil.copytree(root / "examples/ply-workflow-consumer", consumer)
        consumer_signature = ply_signature(root / "examples/ply-workflow-consumer")
        if consumer_signature != ply_signature(consumer):
            raise RuntimeError("Copied standalone consumer source changed raw bytes")
        write_json(runner.evidence / "consumer-source.json", {"files": consumer_signature,
            "source": str(consumer), "installed_prefix_files": ply_signature(relocated),
            "relocated_tiny_files": ply_signature(tiny)})
        cb = work / "cb"
        runner.configure(consumer, cb, f"-DCMAKE_PREFIX_PATH={relocated};{tiny}")
        runner.build(cb)
        ply_gnu_compilation(runner, cb, "repair", [relocated, tiny])
        names = [test["name"] for test in runner.inventory(cb, "installed", 1)]
        if names != ["installed_ply_workflow_unsupported"]:
            raise RuntimeError("Standalone installed inventory differs")
        runner.test(cb, True)
        validate_ply_unsupported(runner, geometry, cb, "installed")
        result["completed"].append("native-four-and-relocated-installed-one")
        dist = work / "dist"
        dist.mkdir(exist_ok=False)
        archive_path = dist / "repair-native.tar"
        runner.run("git", "-C", root, "archive", "--format=tar", "--output", archive_path, head)
        archived = work / "archive-source"
        archived.mkdir()
        members = []
        with tarfile.open(archive_path, "r:") as archive:
            tracked = set(runner.run("git", "-C", root, "ls-tree", "-r", "--name-only", head).splitlines())
            for member in archive.getmembers():
                path = PurePosixPath(member.name)
                if path.is_absolute() or ".." in path.parts or not (member.isfile() or member.isdir()):
                    raise RuntimeError("Invalid source archive member: " + member.name)
                if member.isfile():
                    raw = archive.extractfile(member).read()
                    original = subprocess.check_output(["git", "-C", str(root), "show", head + ":" + member.name], env=runner.environment)
                    source_text = path.suffix in {".cpp", ".h", ".py", ".md", ".cmake", ".txt", ".toml", ".yml", ".yaml", ".json"} or member.name in {".gitattributes", ".gitignore", "LICENSE", "NOTICE"}
                    eol_only = source_text and raw.replace(b"\r\n", b"\n") == original.replace(b"\r\n", b"\n")
                    if member.name != ".git_archival.txt" and raw != original and not eol_only:
                        raise RuntimeError("Git archive source bytes differ beyond source EOL: " + member.name)
                    members.append({"path": member.name, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
                                    "git_blob_sha256": hashlib.sha256(original).hexdigest(),
                                    "bridge": "expanded-marker" if member.name == ".git_archival.txt" else "raw" if raw == original else "source-eol-only"})
            if {member["path"] for member in members} != tracked:
                raise RuntimeError("Git archive file inventory differs from this exact commit")
            archive.extractall(archived, filter="data")
        marker = (archived / ".git_archival.txt").read_text(encoding="utf8")
        if ("$Format:" in marker or "node: " + head not in marker or (archived / ".git").exists()):
            raise RuntimeError("Archive must have genuine expanded node metadata and no Git directory")
        write_json(runner.evidence / "archive.json", {"head": head, "sha256": sha256(archive_path),
            "members": members, "marker": marker, "scope": "Genuine reviewed-commit native archive; no Python backend route"})
        ab = work / "ab"
        runner.configure(archived, ab, *options, f"-DCMAKE_PREFIX_PATH={relocated};{tiny}")
        runner.build(ab)
        ply_gnu_compilation(runner, ab, "repair", [relocated, tiny])
        runner.inventory(ab, "archive", 4)
        runner.check_inventory(archived, ab, "--gltf-workflow", "OFF", "--gltf-file-workflow", "OFF",
                               "--off-file-workflow", "OFF", "--ply-file-workflow", "ON")
        runner.test(ab, True)
        if ply_cache(ab, "CMAKE_HOME_DIRECTORY") != str(archived) or ply_cache(ab, "CMAKE_PROJECT_VERSION") != "0.0.0":
            raise RuntimeError("Native Git-free archive source/version identity differs")
        validate_ply_unsupported(runner, geometry, ab, "archive")
        result.update(gate_passed=True, native_tests=4, installed_tests=1, archive_native_tests=4,
                      capture_only_refusals=3, actual_empty_workflows=3, final_reports=3)
        result["completed"].append("genuine-archive-native-four")
    except BaseException as error:
        result["error"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        write_json(runner.evidence / "result.json", result)

def qualify(root, profile_name, file_workflow=False, off_file_workflow=False, off_python=False):
    profile = PROFILES[profile_name]
    if platform.system() != profile.system:
        raise RuntimeError(f"{profile_name} requires {profile.system}")
    work = root / ".local" / "gw"
    runner = Runner(root, work, profile)
    geometry, interchange, vcpkg = (work / name for name in ("g", "i", "v"))
    inputs = {"profile": profile_name, "configuration": vars(profile),
              "file_workflow": file_workflow, "off_file_workflow": off_file_workflow, "off_python": off_python,
              "sources": {}}
    for name, source, revision in (("geometry", geometry, GEOMETRY_REVISION),
                                    ("interchange", interchange,
                                     INTERCHANGE_OFF_FILE_REVISION if off_file_workflow else
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
                     f"-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE={'OFF' if off_file_workflow else 'ON'}",
                     f"-DMESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT={'OFF' if off_file_workflow else 'ON'}",
                     f"-DMESHVALE_INTERCHANGE_GLTF_FILES={'ON' if file_workflow else 'OFF'}",
                     *(["-DMESHVALE_INTERCHANGE_OFF_FILES=ON"] if off_file_workflow else []),
                     f"-DMESHVALE_INTERCHANGE_MATERIALIZATION_TEST_PROFILE={profile.materialization}")
    runner.build(ib)
    # Audit the parser's actual compiler too; sanitizer triplets expose databases.
    port_suffix = "dbg" if profile.configuration == "Debug" else "rel"
    parser_database = vcpkg / "buildtrees" / "tinyobjloader" / f"{triplet}-{port_suffix}" / "compile_commands.json"
    runner.compilation(parser_database)
    runner.inventory(ib, "interchange")
    runner.check_inventory(interchange, ib, "--topology-write", "OFF" if off_file_workflow else "ON",
                           "--workflow-support", "OFF" if off_file_workflow else "ON")
    runner.test(ib, file_workflow or off_file_workflow)
    runner.install(ib, prefix)
    package_prefix = f"{prefix};{dependencies}/{triplet}"
    repair_options = [f"-DCMAKE_PREFIX_PATH={package_prefix}", "-DMESHVALE_REPAIR_PYTHON=OFF",
                      "-DMESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON",
                      f"-DMESHVALE_REPAIR_GLTF_WORKFLOW={'OFF' if off_file_workflow else 'ON'}",
                      f"-DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW={'ON' if file_workflow else 'OFF'}",
                      *(["-DMESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON"] if off_file_workflow else []),
                      f"-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE={profile.materialization}"]
    runner.configure(root, rb, "-DBUILD_TESTING=ON", *repair_options)
    runner.build(rb)
    qualified = profile.materialization == "qualified"
    native_count = ((7 if qualified else 6) if off_file_workflow else
                    (10 if qualified else 6) if file_workflow else (6 if qualified else 4))
    tests = runner.inventory(rb, "repair", native_count)
    runner.check_inventory(root, rb, "--gltf-workflow", "OFF" if off_file_workflow else "ON", "--gltf-file-workflow",
                           "ON" if file_workflow else "OFF",
                           *(["--off-file-workflow", "ON"] if off_file_workflow else []))
    runner.test(rb, file_workflow or off_file_workflow)
    if off_file_workflow:
        reports = runner.evidence / "off-reports"
        test = next(test for test in tests if test["name"] == "off_workflow_semantics")
        runner.run(*test["command"], "--write-reports", reports, timeout=FILE_TEST_TIMEOUT_SECONDS)
        receipt = runner.evidence / "off-report-validation.json"
        runner.run(sys.executable, root / "scripts/check-off-workflow-reports.py", reports,
                   "--geometry-python", geometry / "python", "--receipt", receipt,
                   *([] if qualified else ["--unsupported-profile"]))
        expected = OFF_REPORT_CASE_COUNT if qualified else OFF_UNSUPPORTED_REPORT_CASE_COUNT
        if len(json.loads(receipt.read_text(encoding="utf-8"))["reports"]) != expected:
            raise RuntimeError("OFF report oracle did not validate its exact fixture count")
    if qualified and not off_file_workflow:
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
    if (prefix / "include/meshvale/repair/gltf_workflow.h").is_file() == off_file_workflow:
        raise RuntimeError("Installed glTF workflow header differs from selected module")
    if (prefix / "include/meshvale/repair/gltf_file_workflow.h").is_file() != file_workflow:
        raise RuntimeError("Installed file workflow header differs from selected module")
    if (prefix / "include/meshvale/repair/off_workflow.h").is_file() != off_file_workflow:
        raise RuntimeError("Installed OFF workflow header differs from selected module")
    write_json(runner.evidence / "package.json", {"source_archive_sha256": sha256(archives[0]),
                                                "installed_files": inventory})
    relocated = work / "q"
    if relocated.exists() or not prefix.resolve().is_relative_to(work.resolve()):
        raise RuntimeError("Install relocation requires a fresh workspace-local destination")
    prefix.rename(relocated)
    runner.configure(source / "examples/consumer", cb,
                     f"-DCMAKE_PREFIX_PATH={relocated};{dependencies}/{triplet}",
                     f"-DMESHVALE_REPAIR_TEST_GLTF_WORKFLOW={'OFF' if off_file_workflow else 'ON'}",
                     f"-DMESHVALE_REPAIR_TEST_GLTF_FILE_WORKFLOW={'ON' if file_workflow else 'OFF'}",
                     *(["-DMESHVALE_REPAIR_TEST_OFF_FILE_WORKFLOW=ON"] if off_file_workflow else []),
                     f"-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE={profile.materialization}")
    runner.build(cb)
    installed_count = 4 if file_workflow else 3
    runner.inventory(cb, "installed", installed_count)
    runner.check_inventory(root, rb, "--gltf-workflow", "OFF" if off_file_workflow else "ON", "--gltf-file-workflow",
                           "ON" if file_workflow else "OFF", "--installed-build-dir", cb,
                           *(["--off-file-workflow", "ON"] if off_file_workflow else []))
    runner.test(cb, file_workflow or off_file_workflow)
    if off_python:
        qualify_off_python(runner, geometry, source, relocated, dependencies, triplet)
    write_json(runner.evidence / "result.json", {"gate_passed": True, "profile": profile_name,
               "materialization_profile": profile.materialization,
               "native_tests": native_count, "installed_tests": installed_count,
               "reports_validated": 12 if qualified and not off_file_workflow else 0,
               "file_reports_validated": FILE_REPORT_CASE_COUNT if qualified and file_workflow else 0,
               "file_readback_reports_validated": 1 if qualified and file_workflow else 0,
               "off_reports_validated": (OFF_REPORT_CASE_COUNT if qualified else OFF_UNSUPPORTED_REPORT_CASE_COUNT) if off_file_workflow else 0,
               "file_workflow": file_workflow, "off_file_workflow": off_file_workflow,
               "off_python": off_python, "production_tests": 0})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=PROFILES)
    parser.add_argument("--file-workflow", action="store_true",
                        help="Qualify retained file publication against its pinned Interchange producer")
    parser.add_argument("--off-file-workflow", action="store_true",
                        help="Qualify reported OFF repair with optional glTF modules disabled")
    parser.add_argument("--off-python", action="store_true",
                        help="Also build and exercise OFF wheels in the selected development profiles")
    parser.add_argument("--ply-file-workflow", action="store_true",
                        help="Qualify Windows native/Python PLY, or the native-only GNU unsupported producer")
    parser.add_argument("--validate-reports", type=Path,
                        help="Validate existing fixture JSON without configuring native builds")
    parser.add_argument("--geometry-source", type=Path)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.validate_reports:
        if args.profile or args.file_workflow or args.off_file_workflow or args.off_python or args.ply_file_workflow or args.geometry_source is None or args.receipt is None:
            parser.error("Report-only validation requires --geometry-source and --receipt, without --profile")
        validate_reports(args.geometry_source.resolve(), args.validate_reports.resolve(), args.receipt.resolve())
    elif args.profile:
        if args.ply_file_workflow:
            if args.profile not in ("msvc-release", "gnu-unsupported") or args.file_workflow or args.off_file_workflow or args.off_python:
                parser.error("PLY requires msvc-release or native-only gnu-unsupported without another mode")
            if args.profile == "gnu-unsupported":
                qualify_ply_unsupported(root, args.profile)
            else:
                qualify_ply(root, args.profile)
            return
        if args.file_workflow and args.off_file_workflow:
            parser.error("Select one file-workflow qualification cohort")
        if args.off_python and (not args.off_file_workflow or args.profile not in ("msvc-release", "gnu-unsupported")):
            parser.error("OFF Python qualification requires the OFF workflow and msvc-release or gnu-unsupported")
        qualify(root, args.profile, args.file_workflow, args.off_file_workflow, args.off_python)
    else:
        parser.error("Select --profile or --validate-reports")


if __name__ == "__main__":
    main()
