# SPDX-License-Identifier: Apache-2.0
"""Build and exercise the isolated Windows CPython 3.13 return observer."""
import argparse
import hashlib
from importlib.metadata import version
import json
from pathlib import Path
import subprocess
import sys
import sysconfig


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--prefixes", required=True)
    args = parser.parse_args()
    assert sys.platform == "win32" and sys.version_info[:2] == (3, 13)
    assert not sysconfig.get_config_var("Py_GIL_DISABLED")
    assert version("nanobind") == "3.1.0"
    source = Path(__file__).resolve().parents[1]
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    commands = []
    files = {"python/gltf_bindings.cpp", "python/gltf_bindings.h", "python/gltf_presets.cpp",
             "python/gltf_presets.h", "CMakeLists.txt", "scripts/check-gltf-python-return.py"}
    for directory in ("src", "include", "cmake", "tests/python_native"):
        files.update(path.relative_to(source).as_posix() for path in (source / directory).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts)

    def hashes():
        return {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in sorted(files)}

    inputs = hashes()
    (output / "inputs.json").write_text(json.dumps(inputs, indent=2) + "\n", encoding="utf-8")

    def run(name, argv, timeout):
        with (output / (name + ".log")).open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(arg) for arg in argv], cwd=output,
                                    stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=timeout)
        commands.append({"name": name, "argv": [str(arg) for arg in argv],
                         "exit": result.returncode})
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
        if result.returncode:
            raise RuntimeError(name + " failed; inspect retained log")

    run("configure", ["cmake", "-G", "Ninja", "-S", source, "-B", build,
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
        "-DMESHVALE_REPAIR_PYTHON=ON", "-DMESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON",
        "-DMESHVALE_REPAIR_GLTF_WORKFLOW=ON", "-DMESHVALE_REPAIR_GLTF_FILE_WORKFLOW=ON",
        "-DMESHVALE_REPAIR_GLTF_BINDING_TESTS=ON", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_PREFIX_PATH=" + args.prefixes, "-DPython_EXECUTABLE=" + sys.executable], 600)
    run("build", ["cmake", "--build", build, "--target", "_repair_gltf_return_test",
                  "--config", "Release", "--parallel", "2", "--verbose"], 600)
    database = json.loads((build / "compile_commands.json").read_text())
    observed = [row for row in database if "CMakeFiles/_repair_gltf_return_test.dir/" in row["command"].replace("\\", "/")]
    production = [row for row in database if "CMakeFiles/_repair.dir/" in row["command"].replace("\\", "/")]
    assert len(observed) == 5 and len(production) == 3
    assert all("MESHVALE_REPAIR_GLTF_BINDING_TESTS=1" in row["command"] for row in observed)
    assert all("MESHVALE_REPAIR_GLTF_BINDING_TESTS" not in row["command"] for row in production)
    assert all(all(flag in row["command"] for flag in ("/W4", "/WX", "/fp:strict"))
               for row in observed + production)
    (output / "compile-commands.json").write_text(json.dumps(database, indent=2) + "\n", encoding="utf-8")
    module, = build.glob("_repair_gltf_return_test*.pyd")
    run("actual-return", [sys.executable, "-I", source / "tests/python_native/test_gltf_return.py",
                          module, output / "actual"], 120)
    assert inputs == hashes(), "Source changed during compiled observer qualification"
    (output / "receipt.json").write_text(json.dumps({
        "scope": "Isolated test variant; production compile commands configured but production module execution is separately qualified",
        "module_sha256": hashlib.sha256(module.read_bytes()).hexdigest(), "inputs": inputs,
        "native_entry": "Own Repair same-engine ForTest with original request observers; actual publisher unchanged",
        "nanobind": version("nanobind"), "python": sys.version,
        "commands": commands}, indent=2) + "\n", encoding="utf-8")
    print("Compiled glTF Python return observer passed")


if __name__ == "__main__":
    main()
