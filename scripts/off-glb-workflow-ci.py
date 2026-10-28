# SPDX-License-Identifier: Apache-2.0
"""Candidate OFF to annotated GLB gate: Windows MSVC Release, ordinary x64 CP313.

Populate .local/og/{g,i,gp,v} with the exact public revisions below. Native
Geometry and Python Geometry are separate producers. This focused candidate
builds all six Repair native modules, checks Surface ON/OFF inventories, builds
the production source package, relocates the SDK and tests installed wheels.
Only the Surface ON native test build selects the matching Interchange source
for composed publication fault tests; production and installed consumers do not.
No Interchange Python wheel is selected. This is not a portable wheel, clean
runtime-floor, sanitizer, unsupported-platform or release qualification.
"""
import argparse
from email.parser import BytesParser
import importlib.util
import os
from pathlib import Path
import platform
import shutil
import struct
import sys
import sysconfig
import tarfile
import tomllib
import zipfile


GEOMETRY_NATIVE_REVISION = "5dd210b19d05bf651b853b8fdac398c7feceeba9"
INTERCHANGE_NATIVE_REVISION = "3bd6f0d6c1189410689e6b1dcddb5d31eac4f1fe"
GEOMETRY_PYTHON_REVISION = "48298b24be7c132816fb5580d22d58bde61558d0"
GEOMETRY_PYTHON_VERSION = "0.0.1.dev134+g48298b24b"
VCPKG_REVISION = "cb5a41b03cde4086d554b3f3e2eac39206b16eda"


def load_helper(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def one_file(directory, pattern):
    files = list(directory.glob(pattern))
    if len(files) != 1:
        raise RuntimeError(f"Expected one fresh {pattern} in {directory}, found {files}")
    return files[0]


def installed_inventory(runner, checker, build, surface):
    # The standard consumer has no PLY test; native PLY stays ON. Its separate
    # standalone consumer is not covered by this OFF/Surface candidate.
    cache = checker.cache_entries(build / "CMakeCache.txt")
    if cache.get(checker.PUBLICATION_SOURCE_CACHE_KEY, ("PATH", ""))[1]:
        raise RuntimeError("Installed consumers must not select private Interchange test source")
    checker.validate_cache(cache,
        "qualified", "Release", installed=True, workflow=True, file_workflow=True,
        off_file_workflow=True, off_surface_export=surface)
    checker.validate_compiler(build, "MSVC", "")
    names = {"installed_repair_consumer", "installed_materialized_duplicates",
             "installed_gltf_workflow", "installed_gltf_file_workflow", "installed_off_workflow"}
    if surface:
        names.add("installed_off_surface_export")
    actual = runner.inventory(build, build.name, len(names))
    if {test["name"] for test in actual} != names:
        raise RuntimeError(f"Actual standard installed consumer inventory differs: {actual}")
    runner.test(build, True)


def qualify(root):
    if (platform.system() != "Windows" or platform.machine().lower() not in ("amd64", "x86_64") or
            sys.implementation.name != "cpython" or sys.version_info[:2] != (3, 13) or
            struct.calcsize("P") != 8 or sysconfig.get_config_var("Py_GIL_DISABLED") or
            (hasattr(sys, "_is_gil_enabled") and not sys._is_gil_enabled())):
        raise RuntimeError("This candidate requires Windows x64 ordinary GIL-enabled CPython 3.13")
    common = load_helper("meshvale_off_glb_common", root / "scripts/gltf-workflow-ci.py")
    checker = load_helper("meshvale_off_glb_inventory", root / "scripts/check-native-tests.py")
    work = root / ".local/og"
    runner = common.Runner(root, work, common.PROFILES["msvc-release"])
    result = {"gate_passed": False, "profile": "msvc-release-cp313",
              "completed": [], "scope": "OFF/Surface development candidate; all six native modules ON",
              "interchange_python_selected": False, "installed_ply_consumer_covered": False,
              "dedicated_source_coupled_test_selected": False,
              "portable_wheel_qualified": False, "runtime_floor_qualified": False}
    try:
        sources = {}
        for name, revision in (("g", GEOMETRY_NATIVE_REVISION), ("i", INTERCHANGE_NATIVE_REVISION),
                               ("gp", GEOMETRY_PYTHON_REVISION), ("v", VCPKG_REVISION)):
            source = work / name
            actual = runner.run("git", "-C", source, "rev-parse", "HEAD").strip()
            if actual != revision or runner.run("git", "-C", source, "status", "--porcelain").strip():
                raise RuntimeError(f"{name}: dependency is not the exact clean public producer")
            sources[name] = actual
        if runner.run("git", "status", "--porcelain").strip():
            raise RuntimeError("Candidate source must be a clean, committed checkout")
        sources["repair"] = runner.run("git", "rev-parse", "HEAD").strip()
        project = tomllib.loads((root / "pyproject.toml").read_text(encoding="utf-8"))["project"]
        if project["dependencies"] != [f"meshvale-geometry=={GEOMETRY_PYTHON_VERSION}"]:
            raise RuntimeError("Repair's Python producer pin differs from this candidate")
        common.write_json(runner.evidence / "inputs.json", {"sources": sources,
            "python": sys.version, "python_geometry_version": GEOMETRY_PYTHON_VERSION,
            "native_geometry_is_separate_from_python_geometry": True,
            "interchange_python_selected": False, "configuration": vars(runner.profile)})
        geometry, interchange, python_geometry, vcpkg = (work / name for name in ("g", "i", "gp", "v"))
        for tool in ("cmake", "ctest", "ninja", "cl"):
            if shutil.which(tool) is None:
                raise RuntimeError(f"Required tool missing: {tool}")
        runner.run("cmake", "--version")
        runner.run("cmd", "/d", "/c", str(vcpkg / "bootstrap-vcpkg.bat"), "-disableMetrics")
        triplet = "x64-windows-static-md"
        dependencies, prefix = work / "d", work / "p"
        runner.configure(geometry, work / "gb", "-DBUILD_TESTING=OFF",
            "-DMESHVALE_GEOMETRY_PYTHON=OFF", "-DCMAKE_DISABLE_FIND_PACKAGE_Eigen3=ON")
        runner.build(work / "gb")
        runner.install(work / "gb", prefix)
        # The Interchange manifest owns its parser dependency. Retain the exact
        # compiler database for that build, using the standard MD triplet.
        overlay = runner.evidence / "triplets"
        overlay.mkdir()
        base = vcpkg / "triplets" / f"{triplet}.cmake"
        if not base.is_file():
            raise RuntimeError("Pinned vcpkg standard Windows MD triplet is missing")
        compiler = Path(shutil.which("cl")).resolve().as_posix()
        (overlay / f"{triplet}.cmake").write_text(
            f'include("{base.as_posix()}")\n'
            'list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n'
            f'  "-DCMAKE_CXX_COMPILER={compiler}" "-DCMAKE_C_COMPILER={compiler}")\n'
            'list(APPEND VCPKG_ENV_PASSTHROUGH CC CXX)\n', encoding="utf-8")
        runner.configure(interchange, work / "ib", "-DBUILD_TESTING=OFF",
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
            f"-DVCPKG_TARGET_TRIPLET={triplet}", f"-DVCPKG_OVERLAY_TRIPLETS={overlay}",
            f"-DVCPKG_INSTALLED_DIR={dependencies}", "-DMESHVALE_INTERCHANGE_PYTHON=OFF",
            "-DMESHVALE_INTERCHANGE_USD=OFF", "-DMESHVALE_INTERCHANGE_FBX=OFF",
            "-DMESHVALE_INTERCHANGE_GLTF_TOPOLOGY_WRITE=ON",
            "-DMESHVALE_INTERCHANGE_GLTF_WORKFLOW_SUPPORT=ON",
            "-DMESHVALE_INTERCHANGE_GLTF_FILES=ON", "-DMESHVALE_INTERCHANGE_OFF_FILES=ON",
            "-DMESHVALE_INTERCHANGE_PLY_FILES=ON", "-DMESHVALE_INTERCHANGE_SURFACE_OUTPUT=ON",
            "-DMESHVALE_INTERCHANGE_SURFACE_FILES=ON")
        runner.build(work / "ib")
        runner.compilation(vcpkg / "buildtrees/tinyobjloader" / f"{triplet}-rel/compile_commands.json")
        runner.install(work / "ib", prefix)
        tiny = dependencies / triplet
        native_options = ["-DMESHVALE_REPAIR_PYTHON=OFF", "-DMESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON",
            "-DMESHVALE_REPAIR_GLTF_WORKFLOW=ON", "-DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW=ON",
            "-DMESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON", "-DMESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON",
            "-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=qualified"]
        for surface, directory, count in ((True, "rb", 18), (False, "ob", 14)):
            build = work / directory
            runner.configure(root, build, f"-DCMAKE_PREFIX_PATH={prefix};{tiny}", "-DBUILD_TESTING=ON",
                *native_options, f"-DMESHVALE_REPAIR_OFF_SURFACE_EXPORT={'ON' if surface else 'OFF'}",
                f"-DMESHVALE_REPAIR_INTERCHANGE_TEST_SOURCE={interchange if surface else ''}")
            runner.build(build)
            runner.inventory(build, directory, count)
            runner.check_inventory(root, build, "--gltf-workflow", "ON", "--gltf-file-workflow", "ON",
                "--off-file-workflow", "ON", "--ply-file-workflow", "ON",
                "--off-surface-export", "ON" if surface else "OFF")
            runner.test(build, True)
            if surface:
                result["dedicated_source_coupled_test_selected"] = True
            result["completed"].append(f"native-surface-{'on' if surface else 'off'}-{count}")
        dist = work / "dist"
        runner.run(sys.executable, "-m", "build", "--sdist", "--no-isolation", "--outdir", dist)
        archive = one_file(dist, "*.tar.gz")
        runner.run(sys.executable, root / "scripts/check-package.py", archive)
        unpacked = work / "s"
        unpacked.mkdir()
        with tarfile.open(archive, "r:gz") as source_archive:
            source_archive.extractall(unpacked, filter="data")
        source = one_file(unpacked, "*")
        if not source.is_dir() or (source / ".git").exists():
            raise RuntimeError("Source package must contain one Git-free product directory")
        runner.configure(source, work / "pb", "-DBUILD_TESTING=OFF", *native_options,
            "-DMESHVALE_REPAIR_INTERCHANGE_TEST_SOURCE=",
            "-DMESHVALE_REPAIR_OFF_SURFACE_EXPORT=ON", f"-DCMAKE_PREFIX_PATH={prefix};{tiny}")
        runner.build(work / "pb")
        runner.inventory(work / "pb", "production", 0)
        runner.install(work / "pb", prefix)
        off_prefix = work / "off-prefix"
        runner.install(work / "ob", off_prefix)
        targets = (off_prefix / "lib/cmake/MeshvaleRepair/MeshvaleRepairTargets.cmake").read_text(encoding="utf-8")
        if ((off_prefix / "include/meshvale/repair/off_surface_export.h").exists() or
                "meshvale::repair_off_surface_export" in targets):
            raise RuntimeError("Surface OFF installation exported the optional header or target")
        inventory = [{"path": path.relative_to(prefix).as_posix(), "sha256": common.sha256(path)}
                     for path in sorted(prefix.rglob("*")) if path.is_file()]
        if not (prefix / "include/meshvale/repair/off_surface_export.h").is_file() or any(
                "test_internal" in item["path"] or item["path"].startswith("include/src/") for item in inventory):
            raise RuntimeError("Production installed headers differ from the public feature profile")
        common.write_json(runner.evidence / "package.json", {
            "source_archive_sha256": common.sha256(archive), "installed_files": inventory})
        relocated = work / "q"
        if relocated.exists():
            raise RuntimeError("SDK relocation destination must be absent")
        prefix.rename(relocated)
        for surface, directory, sdk in ((True, "cb", relocated), (False, "ocb", off_prefix)):
            runner.configure(source / "examples/consumer", work / directory,
                f"-DCMAKE_PREFIX_PATH={sdk};{relocated};{tiny}",
                "-DMESHVALE_REPAIR_TEST_GLTF_WORKFLOW=ON", "-DMESHVALE_REPAIR_TEST_GLTF_FILE_WORKFLOW=ON",
                "-DMESHVALE_REPAIR_TEST_OFF_FILE_WORKFLOW=ON",
                f"-DMESHVALE_REPAIR_TEST_OFF_SURFACE_EXPORT={'ON' if surface else 'OFF'}",
                "-DMESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE=qualified")
            runner.build(work / directory)
            installed_inventory(runner, checker, work / directory, surface)
        result["completed"].append("source-package-production-zero-and-relocated-consumers-six-five")
        runner.environment["CMAKE_PREFIX_PATH"] = os.pathsep.join((str(relocated), str(tiny)))
        wheels = work / "python-wheels"
        runner.run(sys.executable, "-m", "build", "--wheel", "--no-isolation",
            "--outdir", wheels / "geometry", "-Ccmake.define.CMAKE_BUILD_TYPE=Release",
            "-Ccmake.define.CMAKE_CXX_FLAGS=/EHsc", python_geometry)
        geometry_wheel = one_file(wheels / "geometry", "*.whl")
        with zipfile.ZipFile(geometry_wheel) as archive:
            metadata_files = [name for name in archive.namelist() if name.endswith(".dist-info/METADATA")]
            if len(metadata_files) != 1:
                raise RuntimeError("Geometry wheel must contain one metadata document")
            metadata = BytesParser().parsebytes(archive.read(metadata_files[0]))
        if metadata["Name"] != "meshvale-geometry" or metadata["Version"] != GEOMETRY_PYTHON_VERSION:
            raise RuntimeError("Built Geometry Python metadata differs from the exact owned-record pin")
        runner.run(sys.executable, python_geometry / "scripts/check-package.py", geometry_wheel)
        # Build the affected extension from the checked source package, using
        # the same all-six-ON module selection as the native candidate.
        configuration = ["-Ccmake.define.CMAKE_BUILD_TYPE=Release", "-Ccmake.define.CMAKE_CXX_FLAGS=/EHsc",
            "-Ccmake.define.MESHVALE_REPAIR_INTERCHANGE_TEST_SOURCE=",
            "-Ccmake.define.MESHVALE_REPAIR_PYTHON=ON", "-Ccmake.define.BUILD_TESTING=OFF"]
        configuration.extend("-Ccmake.define." + option[2:] for option in native_options
                             if not option.startswith("-DMESHVALE_REPAIR_PYTHON="))
        runner.run(sys.executable, "-m", "build", "--wheel", "--no-isolation", "--outdir", wheels / "repair",
            *configuration, "-Ccmake.define.MESHVALE_REPAIR_OFF_SURFACE_EXPORT=ON", source)
        repair_wheel = one_file(wheels / "repair", "*.whl")
        runner.run(sys.executable, source / "scripts/check-package.py", repair_wheel)
        runner.run(sys.executable, source / "scripts/test-installed.py", "--geometry-wheel", geometry_wheel,
            "--repair-wheel", repair_wheel, "--off", "qualified", "--off-glb", timeout=1800)
        common.write_json(runner.evidence / "python-wheels.json", {
            "geometry_python_version": GEOMETRY_PYTHON_VERSION,
            "geometry": {"path": str(geometry_wheel), "sha256": common.sha256(geometry_wheel)},
            "repair": {"path": str(repair_wheel), "sha256": common.sha256(repair_wheel)},
            "interchange_python_selected": False, "source_archive_build": True,
            "off_glb_api_tests": 15, "off_glb_command_tests": 10, "skips_allowed": False})
        result["completed"].append("installed-g134-repair-off-glb-api15-command10-and-legacy-off")
        result.update(gate_passed=True, native_surface_on_tests=18, native_surface_off_tests=14,
                      standard_installed_surface_on_tests=6, standard_installed_surface_off_tests=5,
                      production_tests=0)
    except BaseException as error:
        result["error"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        common.write_json(runner.evidence / "result.json", result)


if __name__ == "__main__":
    argparse.ArgumentParser(description=__doc__).parse_args()
    qualify(Path(__file__).resolve().parents[1])
