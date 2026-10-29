#!/usr/bin/env python3
"""Capture actual compiler, SDK, wheel and loaded-module test provenance.

Run with the existing configured toolchain environment. Missing records are fatal;
neither host labels nor successful asset execution attest a numerical profile.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tarfile
import zipfile
from types import SimpleNamespace

# -I removes the script directory as well as the working directory. Load only
# this owning test helper; product imports remain installed-only in a child.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gltf_deformation_test_support import (
    ABI_SOURCE, DATA, SelectionError, artifact, cache_records, capture, checked_schema,
    clone_compile, compile_entry, compile_object, generator, load, manifest_files, predicate_text,
    require, resolve, save, sha, verify_dependencies, verify_file_manifest, verify_refs,
    capture_dependencies,
    remap_refs, validated_producer,
    validated_receiver,
)

ROOT = Path(__file__).resolve().parents[1]


def copied(source, target, roots):
    require(not target.exists(), "existing evidence copy")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    require(sha(source) == sha(target), "copy changed")
    return artifact(target, roots, "evidence")


def success(process, label):
    require(process.returncode == 0, label + " failed; raw process preserved")


def probe_compile(build, entry, argv, native_source, output, roots, configuration):
    output.mkdir(parents=True, exist_ok=False)
    cache = cache_records(build / "CMakeCache.txt")
    compiler = Path(argv[0]).resolve(strict=True)
    compiler_version = capture([compiler, "/?"] if compiler.name.lower() == "cl.exe" else
                               [compiler, "--version"], build, output / "compiler-version", roots)
    success(compiler_version[1], "compiler identity")
    compiler_record = save(output / "compiler.json", {"path": str(compiler), "sha256": sha(compiler),
         "version_process": artifact(output / "compiler-version/process.json", roots)})
    predicate = predicate_text(native_source)
    predicate_file = output / "predicate.cpp.inc"
    predicate_file.write_text(predicate, encoding="utf-8")
    dependencies = capture_dependencies(build, entry, output, roots)
    command_record = save(output / "production-command.json", entry)
    production_object = copied(compile_object(entry), output / "production-object.bin", roots)
    probe_records = {}
    for label, assertion in (("common", ""), ("expected-true", "kMeshvaleDomainGuard"),
                              ("expected-false", "!kMeshvaleDomainGuard")):
        directory = output / label
        directory.mkdir()
        source = directory / "probe.cpp"
        message = "MESHVALE_DOMAIN_" + label.replace("-", "_").upper()
        source.write_text(predicate + ("\nstatic_assert(" + assertion + ', "' + message + '");\n'
                                      if assertion else "\n") + "int meshvale_domain_probe() { return 0; }\n",
                          encoding="utf-8")
        obj = directory / ("probe.obj" if compiler.name.lower() == "cl.exe" else "probe.o")
        command = clone_compile(argv, entry["file"], obj, source)
        _, process = capture(command, build, directory / "compile", roots, inputs=[source, compiler], outputs=[obj])
        library = directory / ("probe.lib" if compiler.name.lower() == "cl.exe" else "probe.a")
        if process.returncode == 0:
            librarian = cache.get("CMAKE_AR", (None, ""))[1]
            require(Path(librarian).is_file(), "missing actual librarian")
            archive_argv = ([librarian, "/nologo", "/OUT:" + str(library), str(obj)]
                            if compiler.name.lower() == "cl.exe" else [librarian, "qc", str(library), str(obj)])
            _, archived = capture(archive_argv, directory, directory / "archive", roots,
                                  inputs=[obj], outputs=[library])
            success(archived, "probe static library")
        else:
            diagnostics = (process.stdout + process.stderr).decode("utf-8", errors="replace")
            require(label != "common" and message in diagnostics,
                    "probe has no named predicate assertion; unprobed")
            error_lines = [line for line in diagnostics.splitlines() if
                           re.search(r"\berror(?: [A-Z]+\d+)?:", line)]
            require(error_lines and all(message in line for line in error_lines),
                    "probe has unrelated compilation errors; unprobed")
        record = {"schema": "meshvale.gltf-deformation-compile-probe/1", "assertion": label,
            "source": artifact(source, roots), "command": artifact(directory / "compile/process.json", roots),
            "stdout": artifact(directory / "compile/stdout", roots), "stderr": artifact(directory / "compile/stderr", roots),
            "output": artifact(library, roots) if library.is_file() else None,
            "returncode": process.returncode, "selected_configuration": configuration,
            "compiler_record": artifact(compiler_record, roots), "dependency_headers": artifact(dependencies, roots)}
        checked_schema(record, "compile-probe")
        probe_records[label] = record
    require(probe_records["common"]["returncode"] == 0, "common compile failed")
    true_pass = probe_records["expected-true"]["returncode"] == 0
    false_pass = probe_records["expected-false"]["returncode"] == 0
    require(true_pass != false_pass, "ambiguous predicate pair")

    # Compile-time preprocessing emits ABI macro values, never queries FP state.
    abi_source = output / "abi.cpp"
    abi_source.write_text(predicate + ABI_SOURCE, encoding="utf-8")
    _, preprocessed = capture(clone_compile(argv, entry["file"], output / "unused", abi_source, preprocess=True),
                             build, output / "abi-preprocess", roots, inputs=[abi_source, compiler])
    success(preprocessed, "ABI preprocessing")
    lines = preprocessed.stdout.decode("utf-8").splitlines()
    metadata = {}
    for line in lines:
        match = re.fullmatch(r"MV_DOMAIN_(ARCH|STANDARD|RUNTIME|ITERATOR|STDLIB)\s+(.*)", line.strip())
        if match:
            require(match[1] not in metadata, "duplicate ABI field")
            parts = re.findall(r'"([^"\\]*)"', match[2])
            require(parts, "invalid ABI field")
            metadata[match[1]] = "".join(parts)
    require(set(metadata) == {"ARCH", "STANDARD", "RUNTIME", "ITERATOR", "STDLIB"} and
            metadata["ARCH"] != "unknown" and metadata["STDLIB"] != "unknown", "unknown ABI")
    standard = int(metadata["STANDARD"].rstrip("LlUu"))
    compiler_record = save(output / "compiler-sealed.json", {
        "path": str(compiler), "sha256": sha(compiler),
        "version_process": artifact(output / "compiler-version/process.json", roots),
        "abi_source": artifact(abi_source, roots),
        "abi_process": artifact(output / "abi-preprocess/process.json", roots),
        "configure_inputs": copied(build / "gltf-domain-configure-inputs.json", output / "configure-inputs.json", roots),
        "configure_hook": copied(ROOT / "cmake/GltfDeformationTestProfile.cmake", output / "configure-hook.cmake", roots)})
    for label, probe in probe_records.items():
        probe["compiler_record"] = artifact(compiler_record, roots)
        save(output / label / "probe.json", probe)
    name, kind, configs = generator(build / "CMakeCache.txt", configuration)
    observation = load(build / "gltf-domain-generator.json")
    require(set(observation) == {"schema", "generator", "kind", "build_type", "configuration_types"} and
            observation["schema"] == "meshvale.gltf-deformation-generator/1" and
            observation["generator"] == name and observation["kind"] == kind,
            "missing/mismatched actual configure generator observation")
    if kind == "single":
        require(observation["build_type"] == configuration, "actual configure build type mismatch")
    else:
        require(observation["configuration_types"].split(";") == configs, "actual configure members mismatch")
    return {"generator": {"name": name, "kind": kind,
                "kind_observation": copied(build / "gltf-domain-generator.json", output / "generator.json", roots),
                "cache": copied(build / "CMakeCache.txt", output / "CMakeCache.txt", roots),
                "configurations": configs, "selected_configuration": configuration,
                "compiler_record": artifact(compiler_record, roots)},
        "guard": "true" if true_pass else "false", "guard_definition_source": artifact(native_source, roots),
        "predicate_source": artifact(predicate_file, roots), "production_compile_command": artifact(command_record, roots),
        "production_object": production_object, "dependency_headers": artifact(dependencies, roots),
        "common_probe": artifact(output / "common/probe.json", roots),
        "expected_true_probe": artifact(output / "expected-true/probe.json", roots),
        "expected_false_probe": artifact(output / "expected-false/probe.json", roots),
        "abi": {"architecture": metadata["ARCH"], "effective_standard": standard,
                "runtime": metadata["RUNTIME"], "iterator_debug":
                None if metadata["ITERATOR"] == "none" else int(metadata["ITERATOR"]),
                "stdlib_abi": metadata["STDLIB"]}}


def extract_member(build, sdk, entry, output, roots):
    obj = compile_object(entry)
    cache = cache_records(build / "CMakeCache.txt")
    librarian = cache["CMAKE_AR"][1]
    candidates = [path for path in (sdk / "lib").iterdir() if path.name in
                  {"meshvale_interchange.lib", "libmeshvale_interchange.a"}]
    require(len(candidates) == 1, "missing/ambiguous installed core archive")
    library = candidates[0]
    msvc = Path(librarian).name.lower() == "lib.exe"
    _, listed = capture([librarian, "/nologo", "/list", library] if msvc else [librarian, "t", library],
                        build, output / "archive-list", roots, inputs=[library])
    success(listed, "archive member inventory")
    members = [line.strip() for line in listed.stdout.decode("utf-8").splitlines()
               if "gltf_numerical_profile.cpp." in line]
    require(len(members) == 1, "missing/ambiguous numerical archive member")
    extracted = output / "numerical-member.bin"
    if msvc:
        _, process = capture([librarian, "/nologo", "/EXTRACT:" + members[0], "/OUT:" + str(extracted), library],
                             output, output / "archive-extract", roots, inputs=[library], outputs=[extracted])
    else:
        _, process = capture([librarian, "p", library, members[0]], output,
                             output / "archive-extract", roots, inputs=[library])
        if process.returncode == 0:
            extracted.write_bytes(process.stdout)
    success(process, "numerical archive member extraction")
    require(sha(extracted) == sha(obj), "installed archive differs from compiled numerical object")
    record = {"archive": artifact(library, roots), "member": members[0],
              "extracted": artifact(extracted, roots), "production_object_sha256": sha(obj),
              "list_process": artifact(output / "archive-list/process.json", roots),
              "extract_process": artifact(output / "archive-extract/process.json", roots)}
    return save(output / "archive-membership.json", record)


def producer(options):
    source, build, sdk, output = [Path(getattr(options, key)).resolve() for key in ("source", "build", "sdk", "output")]
    require(not output.exists(), "producer output must be fresh")
    output.mkdir(parents=True)
    cache = cache_records(build / "CMakeCache.txt")
    geometry = Path(cache["MeshvaleGeometry_DIR"][1]).resolve().parents[2]
    entry, argv, name, _ = compile_entry(build, "gltf_numerical_profile.cpp", options.configuration)
    require(name in {"Ninja", "Ninja Multi-Config"}, "provenance backend requires Ninja; unknown is selection error")
    require(Path(entry["file"]).resolve() == source / "src/gltf_numerical_profile.cpp", "wrong production source")
    roots = {"evidence": str(output), "producer_source": str(source), "sdk": str(sdk),
             "geometry_sdk": str(geometry), "toolchain": str(Path(argv[0]).resolve().parent)}
    sources = {"interchange_manifest": manifest_files(source, output / "interchange-source.json", roots),
               "geometry_manifest": manifest_files(geometry, output / "geometry-sdk.json", roots)}
    installed = manifest_files(sdk, output / "installed-sdk.json", roots)
    compiled = probe_compile(build, entry, argv, source / "src/gltf_numerical_profile.cpp",
                             output / "compile", roots, options.configuration)
    membership = extract_member(build, sdk, entry, output, roots)
    libraries = [artifact(path, roots, "sdk") for path in sorted((sdk / "lib").iterdir())
                 if path.suffix in {".lib", ".a", ".so", ".dylib"}]
    require(libraries, "no installed SDK libraries")
    header_rows = load(resolve(installed, roots))["files"]
    require(any(row["path"] == "include/meshvale/interchange/gltf.h" for row in header_rows), "missing SDK header")
    bridge = []
    for row in header_rows:
        if row["path"].startswith("include/"):
            original = source / row["path"]
            require(original.is_file() and sha(original) == row["sha256"], "installed/source header mismatch")
            bridge.append({"source": artifact(original, roots), "installed": artifact(sdk / row["path"], roots)})
    save(output / "installed-source-bridge.json", bridge)
    control_build = output / "read-control-build"
    configure = ["cmake", "-S", str(ROOT / "tests/python_native/deformation-read"), "-B", str(control_build),
                 "-G", name, "-DCMAKE_CXX_COMPILER=" + argv[0],
                 "-DCMAKE_PREFIX_PATH=" + str(sdk) + ";" + str(geometry),
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_PROJECT_INCLUDE=" + str(ROOT / "cmake/GltfDeformationTestProfile.cmake"),
                 "-DMESHVALE_DEFORMATION_READ_SOURCE=" + str(source / "tests/gltf_deformation_read.cpp")]
    standards = {202002: 20, 202100: 23, 202302: 23, 202400: 26}
    require(compiled["abi"]["effective_standard"] in standards, "unrecognized actual C++ standard")
    configure.append("-DCMAKE_CXX_STANDARD=" + str(standards[compiled["abi"]["effective_standard"]]))
    configure.append("-DCMAKE_CXX_STANDARD_REQUIRED=ON")
    if name == "Ninja":
        configure.append("-DCMAKE_BUILD_TYPE=" + options.configuration)
    else:
        configure.append("-DCMAKE_CONFIGURATION_TYPES=" + options.configuration)
    for key in ("CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_" + options.configuration.upper(), "CMAKE_MSVC_RUNTIME_LIBRARY"):
        if key in cache:
            configure.append("-D" + key + "=" + cache[key][1])
    # Installed exports retain their real declared package dependencies. Reuse
    # the producing package locations rather than substitute another install.
    for key in ("tinyobjloader_DIR",):
        if key in cache:
            configure.append("-D" + key + "=" + cache[key][1])
    _, configured = capture(configure, output, output / "read-configure", roots)
    success(configured, "linked read-control configure")
    executable = control_build / (options.configuration if name == "Ninja Multi-Config" else "") / (
        "meshvale_deformation_domain_read.exe" if os.name == "nt" else "meshvale_deformation_domain_read")
    _, built = capture(["cmake", "--build", control_build, "--config", options.configuration, "--verbose"],
                       output, output / "read-build", roots, outputs=[executable])
    success(built, "linked read-control build")
    control_entry, _, _, _ = compile_entry(control_build, "gltf_deformation_read.cpp", options.configuration)
    deps = capture_dependencies(control_build, control_entry, output / "control", roots)
    # Resolved archive paths must appear in the actual captured verbose link.
    link_text = built.stdout.decode("utf-8", errors="replace").replace("\\", "/").lower()
    used = [ref for ref in libraries if str(resolve(ref, roots)).replace("\\", "/").lower() in link_text]
    require(any(Path(ref["path"]).name in {"meshvale_interchange.lib", "libmeshvale_interchange.a"} for ref in used),
            "linked control lacks resolved installed core archive")
    save(output / "control-link-inputs.json", used)
    fixture = manifest_files(source / "tests/fixtures/gltf-deformation/matrix-weights",
                             output / "control-fixtures.json", roots)
    record = {"schema": "meshvale.gltf-deformation-python-producer/1", "roots": roots, "sources": sources,
              "producer": compiled, "sdk": {"libraries": libraries, "installed_headers": installed,
                  "exports": installed, "archive_object_membership": artifact(membership, roots),
                  "installed_to_source_bridge": artifact(output / "installed-source-bridge.json", roots)},
              "unsupported_control": {"source": artifact(source / "tests/gltf_deformation_read.cpp", roots),
                  "executable": artifact(executable, roots), "build_process": artifact(output / "read-build/process.json", roots),
                  "resolved_link_inputs": artifact(output / "control-link-inputs.json", roots),
                  "actual_header_dependencies": artifact(deps, roots), "fixture_manifest": fixture,
                  "fixtures_relative": "tests/fixtures/gltf-deformation/matrix-weights"}}
    checked_schema(record, "producer")
    verify_refs(record, roots)
    for ref in (sources["interchange_manifest"], sources["geometry_manifest"], installed):
        verify_file_manifest(resolve(ref, roots))
    verify_dependencies(resolve(compiled["dependency_headers"], roots))
    verify_dependencies(deps)
    save(output / "producer.json", record)
    return output / "producer.json"


def resolved_module_links(build, output, libraries, roots):
    """Capture the actual Ninja link recipe and its resolved archive inputs.

    Build stdout independently retains execution; this recipe alone is never
    accepted as proof that the link happened.
    """
    records = cache_records(build / "CMakeCache.txt")
    ninja = records["CMAKE_MAKE_PROGRAM"][1]
    modules = [path for path in build.rglob("*") if path.is_file() and
               path.name.startswith("_repair.") and path.suffix in {".pyd", ".so"}]
    require(len(modules) == 1, "missing/ambiguous normal native module")
    module = modules[0]
    _, commands = capture([ninja, "-t", "commands", module.relative_to(build).as_posix()],
                          build, output / "link-recipe", roots)
    success(commands, "actual module link recipe")
    lines = [line for line in commands.stdout.decode("utf-8").splitlines() if
             module.name.lower() in line.lower() and
             ("link.exe" in line.lower() or " -shared " in line)]
    require(len(lines) == 1, "missing/ambiguous link command")
    actual_stdout = (output / "wheel-build/stdout").read_text(encoding="utf-8", errors="replace")
    # Ninja verbose prints this exact recipe when executing it. Do not accept
    # a no-work build, cache recipe or a different module's successful link.
    require(lines[0] in actual_stdout, "module link recipe was not actually executed")
    lowered = lines[0].replace("\\", "/").lower()
    used = [ref for ref in libraries if str(resolve(ref, roots)).replace("\\", "/").lower() in lowered]
    require(any(Path(ref["path"]).name in {"meshvale_interchange.lib", "libmeshvale_interchange.a"} for ref in used),
            "normal module did not link attested core SDK library")
    # Every explicitly linked own Interchange archive must be the attested one;
    # no library-name lookup or unrecorded alternate prefix is accepted.
    all_sdk = {str(resolve(ref, roots)).replace("\\", "/").lower() for ref in libraries}
    for match in re.finditer(r'(?i)(?:"([^"]*meshvale_interchange[^" ]*\.(?:lib|a))"|([^ ]*meshvale_interchange[^ ]*\.(?:lib|a)))', lines[0]):
        item = (match[1] or match[2]).replace("\\", "/").lower()
        require(item in all_sdk, "unresolved or substituted Interchange link archive")
    save(output / "resolved-link-inputs.json", used)
    return module, used


def extract_regular_sdist(package, destination):
    """Extract only validated source files/directories, including on CPython 3.10."""
    destination = Path(destination).resolve(strict=True)
    members = package.getmembers()
    require(members and all(member.isfile() or member.isdir() for member in members),
            "non-regular sdist member")
    targets = []
    for member in members:
        relative = PurePosixPath(member.name)
        require(not relative.is_absolute() and ".." not in relative.parts, "invalid sdist member")
        target = (destination / member.name).resolve()
        require(target.is_relative_to(destination), "sdist member escapes extraction root")
        targets.append((member, target))
    for member, target in targets:
        if member.isdir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            source = package.extractfile(member)
            require(source is not None, "missing regular sdist payload")
            with source, target.open("xb") as output:
                shutil.copyfileobj(source, output)


def wheel(options):
    producer_path = Path(options.producer).resolve(strict=True)
    source = Path(options.source).resolve(strict=True)
    output = Path(options.output).resolve()
    require(not output.exists(), "wheel output must be fresh")
    output.mkdir(parents=True)
    produced = validated_producer(producer_path)
    # Each phase retains its original records. The common evidence root merely
    # gives the final manifest canonical references to both immutable subtrees.
    roots = dict(produced["roots"])
    roots["evidence"] = os.path.commonpath([roots["evidence"], str(output)])
    roots["receiver_source"] = str(source)
    input_manifest = manifest_files(source, output / "original-repair-source.json", roots)
    sdist, member_manifest = None, None
    if options.origin == "sdist":
        _, made = capture([sys.executable, "-I", "-m", "build", "--sdist", "--no-isolation",
                           "--outdir", output / "sdist", source], output, output / "sdist-build", roots)
        success(made, "genuine backend sdist")
        archives = list((output / "sdist").glob("*.tar.gz"))
        require(len(archives) == 1, "missing/ambiguous genuine sdist")
        sdist = artifact(archives[0], roots)
        extracted = output / "sdist-source"
        extracted.mkdir()
        with tarfile.open(archives[0]) as package:
            extract_regular_sdist(package, extracted)
        top = list(extracted.iterdir())
        require(len(top) == 1 and top[0].is_dir() and not (top[0] / ".git").exists(), "sdist is not Git-free")
        source = top[0]
        roots["receiver_source"] = str(source)
        member_manifest = manifest_files(source, output / "sdist-members.json", roots)
    original_rows = load(resolve(input_manifest, roots))["files"]
    bridge = []
    # Every shipped native/binding/pure/test/schema input must be identical.
    # Backend generated SCM/version metadata is recorded explicitly, not forced.
    generated = {"python/meshvale_repair/_version.py", "PKG-INFO"}
    for row in original_rows:
        name = row["path"]
        needed = name.startswith(("include/", "src/", "python/", "tests/", "scripts/", "cmake/")) or name in {"CMakeLists.txt", "pyproject.toml"}
        if needed and name not in generated:
            member = source / name
            require(member.is_file() and sha(member) == row["sha256"], "source/sdist required input mismatch: " + name)
            bridge.append({"path": name, "sha256": row["sha256"], "lf_sha256": row["lf_sha256"]})
    save(output / "raw-lf-bridge.json", {"original_manifest": input_manifest,
         "required": bridge, "generated_version_paths": sorted(generated)})
    build = output / "normal-build"
    command = [sys.executable, "-I", "-m", "build", "--wheel", "--no-isolation", "--outdir", output / "wheelhouse",
               "-Cbuild-dir=" + str(build), "-Cbuild.verbose=true",
               "-Ccmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON", "-Ccmake.define.BUILD_TESTING=OFF",
               "-Ccmake.define.CMAKE_PROJECT_INCLUDE=" + str(ROOT / "cmake/GltfDeformationTestProfile.cmake")]
    for setting in options.config_setting:
        require(not any(key in setting for key in ("BUILD_TESTING", "build-dir", "CMAKE_PROJECT_INCLUDE", "CMAKE_EXPORT_COMPILE_COMMANDS")),
                "caller cannot replace provenance settings")
        command.append("-C" + setting)
    command.append(source)
    _, built = capture(command, output, output / "wheel-build", roots)
    success(built, "normal wheel build")
    cache = cache_records(build / "CMakeCache.txt")
    require(cache.get("BUILD_TESTING", (None, None)) == ("BOOL", "OFF"), "wheel is not ordinary BUILD_TESTING=OFF")
    configuration = cache.get("CMAKE_BUILD_TYPE", (None, ""))[1]
    if not configuration:
        # A multi-config backend must have an explicit selected build type;
        # a cache membership list alone cannot supply the executed config.
        selected = [value.split("=", 1)[1] for value in options.config_setting if value.startswith("cmake.build-type=")]
        require(len(selected) == 1 and selected[0], "receiver actual selected configuration is missing")
        configuration = selected[0]
    require(configuration, "receiver actual selected configuration is missing")
    entry, argv, _, _ = compile_entry(build, "gltf_bindings.cpp", configuration)
    require(not any("MESHVALE_REPAIR_GLTF_BINDING_TESTS" in item for item in argv), "isolated test variant is not normal module")
    compiled = probe_compile(build, entry, argv,
        resolve(produced["producer"]["guard_definition_source"], produced["roots"]),
        output / "compile", roots, configuration)
    libraries = remap_refs(produced["sdk"]["libraries"], produced["roots"], roots)
    module, _ = resolved_module_links(build, output, libraries, roots)
    dependencies = verify_dependencies(resolve(compiled["dependency_headers"], roots))
    sdk = Path(roots["sdk"]).resolve()
    source_include = Path(roots["producer_source"]) / "include"
    sdk_headers = {str(path.resolve()): sha(path) for path in (sdk / "include").rglob("*.h")}
    actual_headers = [row for row in dependencies["files"] if "/meshvale/interchange/" in row["path"].replace("\\", "/")]
    require(actual_headers and all(row["path"] in sdk_headers and row["sha256"] == sdk_headers[row["path"]] for row in actual_headers),
            "module used unrecorded/substituted Interchange header")
    require(not any(Path(row["path"]).is_relative_to(source_include) for row in dependencies["files"]), "module used source SDK include")
    wheels = list((output / "wheelhouse").glob("*.whl"))
    require(len(wheels) == 1, "missing/ambiguous normal wheel")
    with zipfile.ZipFile(wheels[0]) as package:
        members = [name for name in package.namelist() if name.startswith("meshvale_repair/_repair.") and name.endswith((".pyd", ".so"))]
        require(len(members) == 1, "missing/ambiguous wheel native member")
        native = output / "wheel-native.bin"
        native.write_bytes(package.read(members[0]))
    require(sha(native) == sha(module), "wheel native file differs from actual linked module; transform evidence required")
    save(output / "normal-defines.json", {"compile_entry": artifact(output / "compile/production-command.json", roots),
         "argv": argv, "test_variant": False})
    record = {"schema": "meshvale.gltf-deformation-python-receiver/1", "origin": options.origin,
         "roots": roots, "producer_record": artifact(producer_path, roots),
         "sources": {"repair_manifest": manifest_files(source, output / "repair-source.json", roots),
             "interchange_manifest": remap_refs(produced["sources"]["interchange_manifest"], produced["roots"], roots),
             "geometry_manifest": remap_refs(produced["sources"]["geometry_manifest"], produced["roots"], roots),
             "sdist": sdist, "sdist_member_manifest": member_manifest,
             "raw_lf_bridge": artifact(output / "raw-lf-bridge.json", roots)},
         "receiver": compiled, "module_build": {"name": "meshvale_repair._repair",
             "build_record": artifact(output / "wheel-build/process.json", roots),
             "normal_defines": artifact(output / "normal-defines.json", roots), "post_link": artifact(module, roots),
             "actual_link_process": artifact(output / "wheel-build/process.json", roots),
             "resolved_link_inputs": artifact(output / "resolved-link-inputs.json", roots),
             "actual_header_dependencies": compiled["dependency_headers"], "wheel": artifact(wheels[0], roots),
             "wheel_member_name": members[0], "pre_repair_member": artifact(native, roots),
             "post_repair_member": artifact(native, roots), "repair_transform": None},
         "positive_inventory": copied(DATA / "positive-inventory.json", output / "positive-inventory.json", roots)}
    checked_schema(record, "receiver")
    verify_refs(record, roots)
    validated_producer(producer_path)
    save(output / "receiver.json", record)
    return output / "receiver.json"


LOADER_SOURCE = r'''
import hashlib, importlib, importlib.metadata, json, pathlib, sys, sysconfig
member = sys.argv[1]
expected = (pathlib.Path(sysconfig.get_path("platlib")) / member).resolve(strict=True)
before = hashlib.sha256(expected.read_bytes()).hexdigest()
module = importlib.import_module("meshvale_repair._repair")
actual = pathlib.Path(module.__file__).resolve(strict=True)
after = hashlib.sha256(actual.read_bytes()).hexdigest()
distribution = importlib.metadata.distribution("meshvale-repair")
metadata = {}
for name in ("RECORD", "METADATA", "WHEEL"):
    candidates = [row for row in distribution.files if str(row).endswith(".dist-info/" + name)]
    if len(candidates) != 1: raise ValueError("ambiguous installed metadata")
    path = pathlib.Path(distribution.locate_file(candidates[0])).resolve(strict=True)
    metadata[name] = {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
print(json.dumps({"module_name": module.__name__, "spec_name": module.__spec__.name,
    "module_file": str(actual), "spec_origin": str(pathlib.Path(module.__spec__.origin).resolve()),
    "expected_file": str(expected), "sys_prefix": str(pathlib.Path(sys.prefix).resolve()),
    "before": before, "after": after, "python": str(pathlib.Path(sys.executable).resolve()),
    "python_version": sys.version, "metadata": metadata}))
'''


def installed(options):
    receiver_path = Path(options.receiver).resolve(strict=True)
    received, produced = validated_receiver(receiver_path)
    old_roots = received["roots"]
    wheel_path = Path(options.wheel).resolve(strict=True)
    require(wheel_path == resolve(received["module_build"]["wheel"], old_roots), "installed wheel not built origin")
    output = Path(options.output).resolve()
    require(not output.exists(), "installed output must be fresh")
    output.mkdir(parents=True)
    roots = dict(old_roots)
    roots["evidence"] = os.path.commonpath([roots["evidence"], str(output)])
    loader_source = output / "loader.py"
    loader_source.write_text(LOADER_SOURCE, encoding="utf-8")
    _, process = capture([options.python, "-I", "-B", loader_source, received["module_build"]["wheel_member_name"]],
                         output, output / "load", roots, inputs=[loader_source, wheel_path])
    success(process, "fresh isolated normal-module load")
    from gltf_deformation_test_support import unique_pairs
    observed = json.loads(process.stdout.decode("utf-8"), object_pairs_hook=unique_pairs)
    require(set(observed) == {"module_name", "spec_name", "module_file", "spec_origin", "expected_file", "sys_prefix",
                             "before", "after", "python", "python_version", "metadata"}, "invalid loader output")
    require(observed["module_name"] == observed["spec_name"] == "meshvale_repair._repair" and
            observed["module_file"] == observed["spec_origin"] == observed["expected_file"] and
            observed["before"] == observed["after"] == received["module_build"]["post_link"]["sha256"],
            "module-identity-mismatch")
    roots["installed"] = observed["sys_prefix"]
    native = Path(observed["module_file"])
    require(native.is_relative_to(Path(roots["installed"])) and
            Path(observed["python"]).resolve() == Path(options.python).resolve(), "module-identity-mismatch")
    metadata = {}
    with zipfile.ZipFile(wheel_path) as package:
        for name, item in observed["metadata"].items():
            require(set(item) == {"path", "sha256"}, "invalid metadata observation")
            member = [row for row in package.namelist() if row.endswith(".dist-info/" + name)]
            require(len(member) == 1, "ambiguous wheel metadata")
            actual = Path(item["path"]).resolve(strict=True)
            require(actual.is_relative_to(Path(roots["installed"])) and sha(actual) == item["sha256"], "stale installed metadata")
            if name != "RECORD":
                import hashlib
                require(hashlib.sha256(package.read(member[0])).hexdigest() == sha(actual), "installed metadata differs from wheel")
            metadata[name] = artifact(actual, roots, "installed")
    metadata_record = {"files": metadata, "loader_process": artifact(output / "load/process.json", roots),
                       "loader_source": artifact(loader_source, roots), "wheel": remap_refs(received["module_build"]["wheel"], old_roots, roots)}
    save(output / "installed-metadata.json", metadata_record)
    native_ref = artifact(native, roots, "installed")
    loader = {"schema": "meshvale.gltf-deformation-test-loader/1", "module_name": observed["module_name"],
              "spec_name": observed["spec_name"], "module_file": str(native), "spec_origin": observed["spec_origin"],
              "sys_prefix": roots["installed"], "before_import": native_ref, "after_import": native_ref,
              "wheel_member": remap_refs(received["module_build"]["post_repair_member"], old_roots, roots),
              "python_executable": artifact(options.python, roots, "installed"), "python_version": observed["python_version"]}
    checked_schema(loader, "loader")
    save(output / "loader.json", loader)
    module = remap_refs(received["module_build"], old_roots, roots)
    module.update(installed_member=native_ref, installed_record_metadata_wheel=artifact(output / "installed-metadata.json", roots),
                  loader_observation=artifact(output / "loader.json", roots))
    record = {"schema": "meshvale.gltf-deformation-python-domain/1", "kind": "installed-cohort", "origin": received["origin"],
              "roots": roots, "producer_record": remap_refs(received["producer_record"], old_roots, roots),
              "receiver_record": artifact(receiver_path, roots), "sources": remap_refs(received["sources"], old_roots, roots),
              "producer": remap_refs(produced["producer"], produced["roots"], roots),
              "receiver": remap_refs(received["receiver"], old_roots, roots),
              "sdk": remap_refs(produced["sdk"], produced["roots"], roots), "module": module,
              "unsupported_control": remap_refs(produced["unsupported_control"], produced["roots"], roots),
              "positive_inventory": remap_refs(received["positive_inventory"], old_roots, roots)}
    checked_schema(record, "manifest")
    verify_refs(record, roots)
    validated_receiver(receiver_path)
    save(output / "installed.json", record)
    return output / "installed.json"


def qualify(options):
    """Run two explicit emitter-owned origins, distinct from portable artifacts."""
    output = Path(options.output).resolve()
    require(not output.exists(), "qualification output must be fresh")
    output.mkdir(parents=True)
    producer_path = Path(options.producer).resolve(strict=True)
    produced = validated_producer(producer_path)
    roots = dict(produced["roots"])
    roots["evidence"] = os.path.commonpath([roots["evidence"], str(output),
                                           str(Path(options.geometry_wheel).resolve().parent)])
    summaries = []
    for origin in ("source", "sdist"):
        phase = output / origin
        phase.mkdir()
        receiver_path = wheel(SimpleNamespace(origin=origin, source=options.source,
            producer=producer_path, output=phase / "wheel", config_setting=options.config_setting))
        received = load(receiver_path)
        built_wheel = resolve(received["module_build"]["wheel"], received["roots"])
        environment = phase / "runtime"
        _, made = capture([sys.executable, "-I", "-m", "venv", "--copies", environment], phase, phase / "make-runtime", roots)
        success(made, "new qualification runtime")
        python = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        _, added = capture([python, "-I", "-m", "pip", "install",
            str(Path(options.geometry_wheel).resolve()) + "[reports]", str(built_wheel) + "[workflow]"],
            phase, phase / "install-runtime", roots, inputs=[built_wheel, Path(options.geometry_wheel).resolve()])
        success(added, "exact qualification wheel installation")
        manifest = installed(SimpleNamespace(receiver=receiver_path, python=python,
            wheel=built_wheel, output=phase / "installed"))
        result = phase / "result.json"
        _, tested = capture([python, "-I", ROOT / "scripts/check-gltf-deformation-python.py",
                             "--manifest", manifest, "--result", result], phase,
                             phase / "selected-tests", roots, inputs=[manifest], outputs=[result])
        success(tested, "selected deformation domain")
        actual = checked_schema(load(result), "result")
        require(actual["outcome"] == "passed" and actual["origin"] == origin, "qualification result mismatch")
        summaries.append({"origin": origin, "wheel": str(built_wheel), "wheel_sha256": sha(built_wheel),
            "manifest": str(manifest), "manifest_sha256": sha(manifest), "result": str(result),
            "result_sha256": sha(result), "domain": actual["domain"], "counts": actual["counts"]})
    save(output / "qualification.json", {"schema": "meshvale.gltf-deformation-emitter-cohorts/1",
         "artifact_scope": "Separate emitter-built source and genuine-sdist artifacts; no cibuildwheel artifact credit",
         "cohorts": summaries})
    return output / "qualification.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="phase", required=True)
    command = commands.add_parser("producer")
    for name in ("source", "build", "sdk", "configuration", "output"):
        command.add_argument("--" + name, required=True)
    command = commands.add_parser("wheel")
    command.add_argument("--origin", choices=("source", "sdist"), required=True)
    for name in ("source", "producer", "output"):
        command.add_argument("--" + name, required=True)
    command.add_argument("--config-setting", action="append", default=[])
    command = commands.add_parser("installed")
    for name in ("receiver", "python", "wheel", "output"):
        command.add_argument("--" + name, required=True)
    command = commands.add_parser("qualify")
    for name in ("source", "producer", "geometry-wheel", "output"):
        command.add_argument("--" + name, required=True)
    command.add_argument("--config-setting", action="append", default=[])
    options = parser.parse_args()
    try:
        result = {"producer": producer, "wheel": wheel, "installed": installed, "qualify": qualify}[options.phase](options)
        print(result)
        return 0
    except (SelectionError, OSError, KeyError, ValueError, TypeError, IndexError) as error:
        print("selection-error: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
