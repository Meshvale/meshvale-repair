"""Fail-closed, test-only provenance support for deformation qualification.

This module does not query production capabilities or select tests from runtime
success. Evidence is generated locally and is not a signature or a release claim.
"""
from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import tarfile
import zipfile
from datetime import datetime, timezone
from email.parser import BytesParser

DATA = Path(__file__).resolve().parent / "gltf-deformation-domain"
ROOT_NAMES = {"evidence", "producer_source", "receiver_source", "sdk",
              "geometry_sdk", "toolchain", "installed"}
UNSUPPORTED_STDOUT = (
    "exact read range PASS; compiled-unsupported refusal PASS; "
    "matrix/quaternion read cohort NOT EXERCISED\n"
    "deformation read controls PASS; payload=0 workers=0\n"
)


class SelectionError(ValueError):
    """The available evidence cannot select either execution domain."""


def require(condition, message):
    if not condition:
        raise SelectionError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def load(path):
    def invalid(value):
        raise SelectionError("non-finite JSON constant: " + value)
    return json.loads(Path(path).read_text(encoding="utf-8"),
                      object_pairs_hook=unique_pairs, parse_constant=invalid)


def save(path, value):
    path = Path(path)
    require(not path.exists(), "refusing to overwrite evidence: " + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return path


def validate(value, schema, root=None, location="$"):
    """Validate the bounded JSONSchema vocabulary of the seven shipped schemas.

    Unknown schema vocabulary is an error, rather than an ignored constraint.
    Integer checks intentionally exclude bool, unlike Python isinstance(int).
    """
    root = root or schema
    known = {"$schema", "$id", "$defs", "$ref", "type", "const", "enum",
             "anyOf", "properties", "required", "additionalProperties", "items",
             "minItems", "minimum", "maximum", "pattern", "minLength"}
    require(not set(schema) - known, "unknown schema constraint at " + location)
    if "$ref" in schema:
        ref = schema["$ref"]
        require(ref.startswith("#/$defs/") and ref.count("/") == 2, "external schema reference")
        return validate(value, root["$defs"][ref.rsplit("/", 1)[1]], root, location)
    if "anyOf" in schema:
        matches = 0
        for choice in schema["anyOf"]:
            try:
                validate(value, choice, root, location)
                matches += 1
            except SelectionError:
                pass
        require(matches > 0, "no allowed type at " + location)
    if "const" in schema:
        require(type(value) is type(schema["const"]) and value == schema["const"],
                "wrong constant at " + location)
    if "enum" in schema:
        require(any(type(value) is type(item) and value == item for item in schema["enum"]),
                "unknown enum at " + location)
    kind = schema.get("type")
    types = {"object": dict, "array": list, "string": str, "integer": int, "null": type(None)}
    if kind:
        require(kind in types and type(value) is types[kind], "wrong type at " + location)
    if kind == "object":
        require(set(schema["required"]) <= value.keys(), "missing field at " + location)
        require(schema["additionalProperties"] is False and
                not value.keys() - schema["properties"].keys(), "unknown field at " + location)
        for key, item in value.items():
            validate(item, schema["properties"][key], root, location + "." + key)
    elif kind == "array":
        require(len(value) >= schema.get("minItems", 0), "short array at " + location)
        for index, item in enumerate(value):
            validate(item, schema["items"], root, location + "[" + str(index) + "]")
    elif kind == "integer":
        require(schema.get("minimum", value) <= value <= schema.get("maximum", value),
                "integer outside bounds at " + location)
    elif kind == "string":
        require(len(value) >= schema.get("minLength", 0), "short string at " + location)
        if "pattern" in schema:
            require(re.search(schema["pattern"], value) is not None, "invalid string at " + location)


def checked_schema(value, name):
    validate(value, load(DATA / (name + ".schema.json")))
    return value


def artifact(path, roots, preferred=None):
    path = Path(path).resolve(strict=True)
    names = [preferred] if preferred else sorted(roots, key=lambda key: len(roots[key]), reverse=True)
    for name in names:
        try:
            relative = path.relative_to(Path(roots[name]).resolve(strict=True)).as_posix()
        except ValueError:
            continue
        require(re.fullmatch(r"[A-Za-z0-9._+/-]+", relative) is not None,
                "artifact needs a portable evidence filename: " + relative)
        return {"root": name, "path": relative, "bytes": path.stat().st_size, "sha256": sha(path)}
    raise SelectionError("artifact outside declared roots: " + str(path))


def resolve(ref, roots):
    require(type(ref) is dict and set(ref) == {"root", "path", "bytes", "sha256"},
            "invalid artifact record")
    require(ref["root"] in ROOT_NAMES and ref["root"] in roots, "unknown artifact root")
    require(Path(roots[ref["root"]]).is_absolute(), "artifact root must be absolute")
    relative = PurePosixPath(ref["path"])
    require(not relative.is_absolute() and relative.as_posix() == ref["path"] and
            all(part not in (".", "..", "") for part in ref["path"].split("/")) and
            re.fullmatch(r"[A-Za-z0-9._+/-]+", ref["path"]) is not None, "invalid artifact path")
    root = Path(roots[ref["root"]]).resolve(strict=True)
    path = (root / relative).resolve(strict=True)
    require(path.is_relative_to(root) and path.is_file(), "artifact escapes its root")
    require(type(ref["bytes"]) is int and path.stat().st_size == ref["bytes"] and
            sha(path) == ref["sha256"], "stale artifact: " + str(path))
    return path


def verify_refs(value, roots):
    if type(value) is dict:
        if set(value) == {"root", "path", "bytes", "sha256"}:
            resolve(value, roots)
        else:
            for child in value.values():
                verify_refs(child, roots)
    elif type(value) is list:
        for child in value:
            verify_refs(child, roots)


def manifest_files(root, output, roots):
    root = Path(root).resolve(strict=True)
    rows = []
    excluded = {".git", ".local", ".scratch", "__pycache__", "build"}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if path.is_file() and not (set(relative.parts) & excluded) and path.suffix != ".pyc":
            data = path.read_bytes()
            rows.append({"path": relative.as_posix(), "bytes": len(data),
                         "sha256": hashlib.sha256(data).hexdigest(),
                         "lf_sha256": hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()})
    require(rows, "empty source/SDK manifest")
    save(output, {"root": str(root), "files": rows})
    return artifact(output, roots, "evidence")


def verify_file_manifest(path):
    data = load(path)
    require(set(data) == {"root", "files"} and type(data["files"]) is list,
            "invalid file manifest")
    root = Path(data["root"]).resolve(strict=True)
    require(len({row["path"] for row in data["files"]}) == len(data["files"]), "duplicate manifest member")
    for row in data["files"]:
        require(set(row) == {"path", "bytes", "sha256", "lf_sha256"}, "invalid file member")
        require(type(row["bytes"]) is int and row["bytes"] >= 0, "invalid file member size")
        relative = PurePosixPath(row["path"])
        require(not relative.is_absolute() and ".." not in relative.parts, "invalid file member path")
        member = (root / relative).resolve(strict=True)
        require(member.is_relative_to(root) and member.is_file(), "file member escapes root")
        data_bytes = member.read_bytes()
        require(len(data_bytes) == row["bytes"] and hashlib.sha256(data_bytes).hexdigest() == row["sha256"] and
                hashlib.sha256(data_bytes.replace(b"\r\n", b"\n")).hexdigest() == row["lf_sha256"],
                "stale file member: " + str(member))
    return data


def capture(argv, cwd, output, roots, *, inputs=(), outputs=(), env=None):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    before = [artifact(path, roots) for path in inputs]
    started = datetime.now(timezone.utc).isoformat()
    try:
        process = subprocess.run([str(item) for item in argv], cwd=cwd, env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    except OSError as error:
        (output / "launch-error.txt").write_text(repr(error), encoding="utf-8")
        save(output / "launch.json", {"argv": [str(item) for item in argv], "cwd": str(cwd),
             "started_utc": started, "error": repr(error)})
        raise SelectionError("process did not start; preserved launch record") from error
    (output / "stdout").write_bytes(process.stdout)
    (output / "stderr").write_bytes(process.stderr)
    record = {"schema": "meshvale.gltf-deformation-test-process/1", "argv": [str(item) for item in argv],
              "cwd": str(Path(cwd).resolve()), "started_utc": started,
              "ended_utc": datetime.now(timezone.utc).isoformat(), "returncode": process.returncode,
              "stdout": artifact(output / "stdout", roots), "stderr": artifact(output / "stderr", roots),
              "inputs": before, "outputs": [artifact(path, roots) for path in outputs if Path(path).is_file()]}
    checked_schema(record, "process")
    save(output / "process.json", record)
    for ref in before:
        resolve(ref, roots)
    return record, process


def cache_records(path):
    result = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([^#/][^:=]*):([^=]+)=(.*)", line)
        if match:
            name, kind, value = match.groups()
            require(name not in result, "duplicate CMake cache field: " + name)
            result[name] = (kind, value)
    return result


def generator(cache, selected):
    records = cache_records(cache)
    require(records.get("CMAKE_GENERATOR", (None,))[0] == "INTERNAL", "untyped generator")
    name = records["CMAKE_GENERATOR"][1]
    if name in {"Ninja", "Unix Makefiles", "NMake Makefiles", "NMake Makefiles JOM", "MinGW Makefiles", "MSYS Makefiles"}:
        require(records.get("CMAKE_BUILD_TYPE", (None,))[0] == "STRING", "missing actual build type")
        require(records["CMAKE_BUILD_TYPE"][1] == selected and selected, "configuration mismatch")
        return name, "single", [selected]
    if name in {"Ninja Multi-Config", "Xcode"} or re.fullmatch(r"Visual Studio \d+ \d{4}", name):
        kind, value = records.get("CMAKE_CONFIGURATION_TYPES", (None, ""))
        require(kind in {"STRING", "UNINITIALIZED"}, "invalid configuration-list type")
        values = value.split(";")
        require(all(values) and len(values) == len(set(values)) and selected in values,
                "invalid multi-configuration membership")
        return name, "multi", values
    raise SelectionError("unknown generator: " + name)


def split_command(command):
    if os.name != "nt":
        return shlex.split(command)
    import ctypes
    from ctypes import wintypes
    count = ctypes.c_int()
    function = ctypes.windll.shell32.CommandLineToArgvW
    function.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    function.restype = ctypes.POINTER(wintypes.LPWSTR)
    pointer = function(command, ctypes.byref(count))
    require(bool(pointer), "cannot parse native command")
    try:
        return [pointer[index] for index in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(pointer)


def compile_entry(build, filename, configuration):
    rows = load(Path(build) / "compile_commands.json")
    matching = [row for row in rows if Path(row["file"]).name == filename]
    name, kind, _ = generator(Path(build) / "CMakeCache.txt", configuration)
    if kind == "multi":
        matching = [row for row in matching if configuration.lower() in
                    tuple(part.lower() for part in Path(row.get("output", "")).parts) or
                    ('CMAKE_INTDIR=\\"' + configuration + '\\"') in row.get("command", "")]
    require(len(matching) == 1, "missing or ambiguous actual compile command: " + filename)
    entry = matching[0]
    argv = entry.get("arguments") or split_command(entry["command"])
    require(argv and Path(entry["directory"]).resolve() == Path(build).resolve(), "compile directory mismatch")
    return entry, argv, name, kind


def same_file(left, right):
    return Path(left).resolve(strict=True) == Path(right).resolve(strict=True)


def choose_domain(manifest):
    producer, receiver = manifest["producer"], manifest["receiver"]
    require(producer["guard"] in {"true", "false"} and receiver["guard"] in {"true", "false"},
            "unprobed-guard")
    require(producer["abi"] == receiver["abi"], "unknown-or-mismatched-cohort")
    if producer["guard"] == "false":
        return "compiled-unsupported"
    require(receiver["guard"] == "true", "unknown-or-mismatched-cohort")
    require(producer["generator"]["selected_configuration"] == "Release" and
            receiver["generator"]["selected_configuration"] == "Release", "supported-but-not-qualified")
    return "positive"


def clone_compile(argv, original_source, output, source, *, preprocess=False):
    """Replace only source/output/dependency destinations; preserve policy flags."""
    msvc = Path(argv[0]).name.lower() in {"cl", "cl.exe"}
    result, index = [argv[0]], 1
    while index < len(argv):
        item = argv[index]
        require(not item.startswith("@"), "response compile command requires captured expansion")
        if item in ("-o", "-MF", "-MT", "-MQ", "/Fo", "/Fd", "/sourceDependencies"):
            index += 2
            continue
        if item.lower().startswith(("/fo", "/fd")) or item in ("-c", "/c", "-MD", "-MMD") and not msvc:
            index += 1
            continue
        if msvc and item.lower() in ("/c", "-c", "/fs"):
            index += 1
            continue
        if item == original_source or Path(item).as_posix() == Path(original_source).as_posix():
            index += 1
            continue
        result.append(item)
        index += 1
    if msvc:
        result += (["/EP"] if preprocess else ["/c", "/Fo" + str(output)]) + [str(source)]
    else:
        result += (["-E", "-P"] if preprocess else ["-c", "-o", str(output)]) + [str(source)]
    return result


def predicate_text(native_source):
    text = Path(native_source).read_text(encoding="utf-8")
    guards = []
    lines = iter(text.splitlines())
    for line in lines:
        if line.startswith("#if defined(_MSC_FULL_VER)"):
            guard = [line]
            while guard[-1].endswith("\\"):
                guard.append(next(lines))
            guards.append("\n".join(guard))
    require(len(guards) == 3 and len(set(guards)) == 1, "production guard needs explicit review")
    representation = re.search(r"if constexpr \((std::numeric_limits<float>::is_iec559.*?)\) \{", text, re.S)
    require(representation is not None, "missing production representation predicate")
    expression = representation.group(1)
    require(expression.count("&&") == 5, "representation predicate shape changed")
    return ("#include <cstdint>\n#include <limits>\n"
            "#if defined(_MSC_VER) && defined(_M_X64)\n"
            "#include <crtversion.h>\n#include <yvals.h>\n#endif\n" +
            guards[0] + "\nconstexpr bool kMeshvaleDomainGuard = (" + expression +
            ");\n#else\nconstexpr bool kMeshvaleDomainGuard = false;\n#endif\n")


def compile_object(entry):
    """Resolve the CDB object against its recorded compile working directory."""
    directory = Path(entry["directory"])
    require(directory.is_absolute() and directory.is_dir(), "invalid compile directory")
    obj = Path(entry["output"])
    if not obj.is_absolute():
        obj = directory / obj
    obj = obj.resolve(strict=True)
    require(obj.is_file(), "missing production object")
    return obj


def dependency_records(build, raw_paths, observations):
    """Retain every spelling; coalesce only identical canonical file identities."""
    canonical = {}
    for raw in raw_paths:
        path = Path(raw)
        if not path.is_absolute():
            path = Path(build) / path
        require(path.is_file(), "missing actual dependency")
        row = {"path": str(path.resolve(strict=True)), "bytes": path.stat().st_size,
               "sha256": sha(path)}
        observations.append({"raw": raw, **row})
        previous = canonical.get(row["path"])
        require(previous is None or previous == row, "conflicting dependency alias observation")
        canonical[row["path"]] = row
    require(canonical, "empty dependency record")
    return list(canonical.values())


def capture_dependencies(build, entry, output, roots):
    records = cache_records(Path(build) / "CMakeCache.txt")
    ninja = records.get("CMAKE_MAKE_PROGRAM", (None, ""))[1]
    require(Path(ninja).name.lower() in {"ninja", "ninja.exe"},
            "actual dependency capture currently requires Ninja")
    obj = compile_object(entry)
    relative = obj.relative_to(Path(build)).as_posix()
    _, process = capture([ninja, "-t", "deps", relative], build, output / "ninja-deps", roots)
    require(process.returncode == 0, "Ninja dependency query failed")
    lines = process.stdout.decode("utf-8").splitlines()
    require(lines and "VALID" in lines[0] and not any("STALE" in row for row in lines),
            "missing/stale actual Ninja dependency record")
    raw_paths = [line.strip() for line in lines[1:] if line.startswith("    ")]
    observations = []
    dependencies = []
    complete = False
    try:
        dependencies = dependency_records(build, raw_paths, observations)
        complete = True
    finally:
        # Raw Ninja stdout remains authoritative, including an incomplete or
        # conflicting collection. This sidecar preserves each observed alias.
        save(output / "dependency-observations.json", {
            "complete": complete, "raw_count": len(raw_paths),
            "observed_count": len(observations),
            "canonical_count": len({row["path"] for row in observations}),
            "observations": observations})
    configure_source = Path(build) / "gltf-domain-configure-inputs.json"
    require(configure_source.is_file(), "missing before-build input record")
    configure_copy = output / "configure-inputs-copy.json"
    require(not configure_copy.exists(), "existing configure input copy")
    configure_copy.write_bytes(configure_source.read_bytes())
    save(output / "dependencies.json", {"object": str(obj.resolve()), "object_sha256": sha(obj),
         "files": dependencies, "configure_inputs": artifact(configure_copy, roots)})
    return output / "dependencies.json"


def verify_dependencies(path):
    record = load(path)
    require(set(record) == {"object", "object_sha256", "files", "configure_inputs"}, "invalid dependency manifest")
    require(sha(record["object"]) == record["object_sha256"], "stale actual object")
    require(record["files"], "empty dependency manifest")
    for row in record["files"]:
        require(set(row) == {"path", "bytes", "sha256"}, "invalid dependency entry")
        source = Path(row["path"])
        require(source.stat().st_size == row["bytes"] and sha(source) == row["sha256"],
                "stale actual header: " + str(source))
    return record


def remap_refs(value, old_roots, new_roots):
    if type(value) is dict:
        if set(value) == {"root", "path", "bytes", "sha256"}:
            return artifact(resolve(value, old_roots), new_roots, value["root"])
        return {key: remap_refs(child, old_roots, new_roots) for key, child in value.items()}
    if type(value) is list:
        return [remap_refs(child, old_roots, new_roots) for child in value]
    return value


def same_refs(left, left_roots, right, right_roots):
    """Compare artifact identity independently of each phase's evidence root."""
    def normalized(value, roots):
        if type(value) is dict:
            if set(value) == {"root", "path", "bytes", "sha256"}:
                return (str(resolve(value, roots)), value["bytes"], value["sha256"])
            return {key: normalized(child, roots) for key, child in value.items()}
        if type(value) is list:
            return [normalized(child, roots) for child in value]
        return value
    require(normalized(left, left_roots) == normalized(right, right_roots), "phase identity mismatch")


def validated_process(ref, roots):
    record = checked_schema(load(resolve(ref, roots)), "process")
    verify_refs(record, roots)
    return record


def validated_compile(record, roots):
    verify_refs(record, roots)
    current = generator(resolve(record["generator"]["cache"], roots),
                        record["generator"]["selected_configuration"])
    require(current == (record["generator"]["name"], record["generator"]["kind"],
                        record["generator"]["configurations"]), "invalid-generator")
    observed = load(resolve(record["generator"]["kind_observation"], roots))
    require(set(observed) == {"schema", "generator", "kind", "build_type", "configuration_types"} and
            observed["schema"] == "meshvale.gltf-deformation-generator/1" and
            observed["generator"] == current[0] and observed["kind"] == current[1], "invalid-generator")
    if current[1] == "single":
        require(observed["build_type"] == record["generator"]["selected_configuration"], "invalid-generator")
    else:
        require(observed["configuration_types"].split(";") == current[2], "invalid-generator")
    compiler = load(resolve(record["generator"]["compiler_record"], roots))
    require(set(compiler) == {"path", "sha256", "version_process", "abi_source", "abi_process", "configure_inputs", "configure_hook"} and
            sha(compiler["path"]) == compiler["sha256"], "compiler identity mismatch")
    validated_process(compiler["version_process"], roots)
    dependency = verify_dependencies(resolve(record["dependency_headers"], roots))
    require(dependency["object_sha256"] == record["production_object"]["sha256"], "object dependency mismatch")
    command = load(resolve(record["production_compile_command"], roots))
    argv = command.get("arguments") or split_command(command["command"])
    require(same_file(argv[0], compiler["path"]), "actual compiler mismatch")
    configured = load(resolve(compiler["configure_inputs"], roots))
    require(set(configured) == {"schema", "timing", "files"} and
            configured["schema"] == "meshvale.gltf-deformation-configure-inputs/1" and
            configured["timing"] == "end-of-configure-before-build", "missing configure-time source identity")
    require(sha(resolve(compiler["configure_hook"], roots)) == sha(DATA.parent.parent / "cmake/GltfDeformationTestProfile.cmake"),
            "configure hook changed")
    before = {}
    for row in configured["files"]:
        require(set(row) == {"path", "bytes", "sha256"}, "invalid configure input")
        source = Path(row["path"]).resolve(strict=True)
        require(str(source) not in before and source.stat().st_size == row["bytes"] and sha(source) == row["sha256"],
                "source/header changed after configure")
        before[str(source)] = row["sha256"]
    actual_source = Path(command["file"]).resolve(strict=True)
    require(str(actual_source) in before, "source was not captured before build")
    actual_hook = Path(cache_records(resolve(record["generator"]["cache"], roots))["CMAKE_PROJECT_INCLUDE"][1]).resolve(strict=True)
    require(before.get(str(actual_hook)) == compiler["configure_hook"]["sha256"],
            "actual configure hook differs from attested hook")
    for header in dependency["files"]:
        path = Path(header["path"]).resolve()
        if path.is_relative_to(Path(roots["producer_source"]).resolve()) or path.is_relative_to(Path(roots["sdk"]).resolve()) or \
                ("receiver_source" in roots and path.is_relative_to(Path(roots["receiver_source"]).resolve())):
            require(str(path) in before and before[str(path)] == header["sha256"], "own input has no configure-before-build identity")
    # Rebuild the predicate from the actual production source, never trust guard.
    require(resolve(record["predicate_source"], roots).read_text(encoding="utf-8") ==
            predicate_text_for_validation(resolve(record["guard_definition_source"], roots)), "predicate/source mismatch")
    verify_refs(compiler, roots)
    abi_source = resolve(compiler["abi_source"], roots)
    require(abi_source.read_text(encoding="utf-8") ==
            resolve(record["predicate_source"], roots).read_text(encoding="utf-8") + ABI_SOURCE,
            "ABI source differs from actual predicate")
    abi_process = validated_process(compiler["abi_process"], roots)
    require(abi_process["returncode"] == 0 and abi_process["argv"] ==
            clone_compile(argv, command["file"], abi_source.parent / "unused", abi_source, preprocess=True),
            "ABI was not preprocessed with actual production flags")
    metadata = {}
    for line in resolve(abi_process["stdout"], roots).read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"MV_DOMAIN_(ARCH|STANDARD|RUNTIME|ITERATOR|STDLIB)\s+(.*)", line.strip())
        if match:
            require(match[1] not in metadata, "duplicate ABI field")
            fields = re.findall(r'"([^"\\]*)"', match[2])
            require(fields, "invalid ABI metadata")
            metadata[match[1]] = "".join(fields)
    require(set(metadata) == {"ARCH", "STANDARD", "RUNTIME", "ITERATOR", "STDLIB"}, "missing ABI metadata")
    observed_abi = {"architecture": metadata["ARCH"], "effective_standard": int(metadata["STANDARD"].rstrip("LlUu")),
        "runtime": metadata["RUNTIME"], "iterator_debug": None if metadata["ITERATOR"] == "none" else int(metadata["ITERATOR"]),
        "stdlib_abi": metadata["STDLIB"]}
    require(record["abi"] == observed_abi and observed_abi["architecture"] != "unknown" and
            observed_abi["stdlib_abi"] != "unknown", "ABI fields contradict actual preprocessing")
    probes = []
    for key, label in (("common_probe", "common"), ("expected_true_probe", "expected-true"),
                       ("expected_false_probe", "expected-false")):
        require(record[key] is not None, "unprobed-guard")
        probe = checked_schema(load(resolve(record[key], roots)), "compile-probe")
        verify_refs(probe, roots)
        require(probe["assertion"] == label and probe["selected_configuration"] ==
                record["generator"]["selected_configuration"], "probe identity mismatch")
        same_refs(probe["compiler_record"], roots, record["generator"]["compiler_record"], roots)
        process = validated_process(probe["command"], roots)
        require(process["returncode"] == probe["returncode"], "probe result mismatch")
        same_refs(process["stdout"], roots, probe["stdout"], roots)
        same_refs(process["stderr"], roots, probe["stderr"], roots)
        require(same_file(process["argv"][0], compiler["path"]), "probe compiler mismatch")
        source = resolve(probe["source"], roots)
        require(str(source) in process["argv"], "probe source not compiled")
        expected = clone_compile(argv, command["file"],
                                 source.parent / ("probe.obj" if Path(argv[0]).name.lower() == "cl.exe" else "probe.o"), source)
        require(process["argv"] == expected, "probe changed production policy flags")
        prefix = resolve(record["predicate_source"], roots).read_text(encoding="utf-8")
        assertion = "" if label == "common" else ("kMeshvaleDomainGuard" if label == "expected-true" else "!kMeshvaleDomainGuard")
        message = "MESHVALE_DOMAIN_" + label.replace("-", "_").upper()
        expected_source = prefix + ("\nstatic_assert(" + assertion + ', "' + message + '");\n' if assertion else "\n") + "int meshvale_domain_probe() { return static_cast<int>(kMeshvaleDomainGuard); }\n"
        require(source.read_text(encoding="utf-8") == expected_source, "probe source changed")
        if process["returncode"] == 0:
            require(probe["output"] is not None, "missing actual probe library")
            archive_process = validated_process(artifact(source.parent / "archive/process.json", roots), roots)
            require(archive_process["returncode"] == 0 and any(
                row["sha256"] == probe["output"]["sha256"] for row in archive_process["outputs"]),
                "missing actual probe archive operation")
        else:
            require(probe["output"] is None and label != "common", "common probe failed")
            diagnostic = (resolve(probe["stdout"], roots).read_bytes() +
                          resolve(probe["stderr"], roots).read_bytes()).decode("utf-8", errors="replace")
            errors = [line for line in diagnostic.splitlines() if re.search(r"\berror(?: [A-Z]+\d+)?:", line)]
            require(errors and all(message in line for line in errors), "unrelated probe failure")
        probes.append(probe)
    require(probes[0]["returncode"] == 0 and (probes[1]["returncode"] == 0) !=
            (probes[2]["returncode"] == 0), "unprobed-guard")
    derived = "true" if probes[1]["returncode"] == 0 else "false"
    require(record["guard"] == derived, "guard field contradicts actual compile")
    return record


def predicate_text_for_validation(path):
    # Kept as a named boundary so reader and emitter share one source parser.
    return predicate_text(path)


def validated_producer(path):
    producer = checked_schema(load(path), "producer")
    roots = producer["roots"]
    verify_refs(producer, roots)
    validated_compile(producer["producer"], roots)
    for ref in producer["sources"].values():
        verify_file_manifest(resolve(ref, roots))
    verify_file_manifest(resolve(producer["sdk"]["installed_headers"], roots))
    membership = load(resolve(producer["sdk"]["archive_object_membership"], roots))
    verify_refs(membership, roots)
    require(membership["extracted"]["sha256"] == membership["production_object_sha256"] ==
            producer["producer"]["production_object"]["sha256"], "archive/object mismatch")
    archive_process = validated_process(membership["extract_process"], roots)
    require(archive_process["returncode"] == 0 and str(resolve(membership["archive"], roots)) in
            archive_process["argv"], "missing actual archive extraction")
    require(any(row["sha256"] == membership["archive"]["sha256"] for row in producer["sdk"]["libraries"]),
            "numerical archive is not attested SDK library")
    for row in load(resolve(producer["sdk"]["installed_to_source_bridge"], roots)):
        require(set(row) == {"source", "installed"}, "invalid SDK source bridge")
        resolve(row["source"], roots)
        resolve(row["installed"], roots)
        require(row["source"]["sha256"] == row["installed"]["sha256"], "SDK header/source mismatch")
    control = producer["unsupported_control"]
    control_dependencies = verify_dependencies(resolve(control["actual_header_dependencies"], roots))
    before_control = load(resolve(control_dependencies["configure_inputs"], roots))
    control_source = resolve(control["source"], roots)
    require(any(Path(row["path"]).resolve() == control_source and row["sha256"] == sha(control_source)
                for row in before_control["files"]), "control source lacks precompile identity")
    verify_file_manifest(resolve(control["fixture_manifest"], roots))
    process = validated_process(control["build_process"], roots)
    require(process["returncode"] == 0 and any(row["sha256"] == control["executable"]["sha256"]
            for row in process["outputs"]), "missing control executable build")
    linked = load(resolve(control["resolved_link_inputs"], roots))
    require(linked and all(ref in producer["sdk"]["libraries"] for ref in linked) and any(
        Path(ref["path"]).name in {"meshvale_interchange.lib", "libmeshvale_interchange.a"} for ref in linked),
        "control linked SDK mismatch")
    log = resolve(process["stdout"], roots).read_text(encoding="utf-8", errors="replace").replace("\\", "/").lower()
    require(all(str(resolve(ref, roots)).replace("\\", "/").lower() in log for ref in linked),
            "control SDK inputs absent from actual link log")
    return producer


def distribution_identity(payload):
    fields = BytesParser().parsebytes(payload, headersonly=True)
    names, versions = fields.get_all("Name", []), fields.get_all("Version", [])
    require(len(names) == len(versions) == 1, "ambiguous package identity")
    require(all(type(value) is str and value.isascii() for value in (names[0], versions[0])),
            "non-ASCII or non-string package identity")
    name, full = names[0].strip(" \t"), versions[0].strip(" \t")
    require(re.sub(r"[-_.]+", "-", name).lower() == "meshvale-repair", "wrong package identity")
    # The owning source-version contract admits three release components with
    # optional dev/local data; this is not a general Python version parser.
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)(?:\.dev([0-9]+))?(?:\+([A-Za-z0-9]+(?:[._-][A-Za-z0-9]+)*))?", full, re.I)
    require(match is not None, "unsupported generated FULL profile")
    version_tuple = tuple(int(match[index]) for index in (1, 2, 3))
    if match[4] is not None:
        version_tuple += ("dev" + str(int(match[4])),)
    if match[5] is not None:
        local = ".".join(str(int(part)) if part.isdecimal() else part.lower()
                         for part in re.split(r"[._-]", match[5]))
        version_tuple += (local,)
    return full, version_tuple


def generated_version_identity(payload, full, version_tuple):
    names = {"__version__", "version", "__version_tuple__", "version_tuple",
             "__commit_id__", "commit_id"}
    values = {}
    try:
        tree = ast.parse(payload)
        for node in tree.body:
            if isinstance(node, ast.Assign):
                for target in node.targets:
                    if isinstance(target, ast.Name) and target.id in names:
                        require(target.id not in values, "duplicate generated version assignment")
                        values[target.id] = ast.literal_eval(node.value)
    except (SyntaxError, ValueError, TypeError) as error:
        raise SelectionError("invalid generated version AST") from error
    require(values.keys() == names, "missing generated version identity")
    require(type(values["version"]) is str and values["version"] == values["__version__"] == full,
            "generated FULL mismatch")
    require(all(type(values[name]) is tuple and
                all(type(value) in {int, str} for value in values[name])
                for name in ("version_tuple", "__version_tuple__")) and
            values["version_tuple"] == values["__version_tuple__"] == version_tuple,
            "generated version tuple mismatch")
    commit = values["commit_id"]
    require((commit is None or type(commit) is str and commit) and
            commit == values["__commit_id__"], "invalid generated commit identity")
    # Closed observed vcs-versioning 2.6.0 .py template, used by the selected
    # build. Check each complete module independently; equal unsupported input
    # and output shapes must not authorize executable writes. AST comparison
    # permits formatting/comments and only these already validated literal slots.
    expected = ast.parse("""from __future__ import annotations
__all__ = ["__version__", "__version_tuple__", "version", "version_tuple", "__commit_id__", "commit_id"]
version: str
__version__: str
__version_tuple__: tuple[int | str, ...]
version_tuple: tuple[int | str, ...]
commit_id: str | None
__commit_id__: str | None
__version__ = version = {full!r}
__version_tuple__ = version_tuple = {version_tuple!r}
__commit_id__ = commit_id = {commit!r}
""".format(full=full, version_tuple=version_tuple, commit=commit))
    require(ast.dump(tree, include_attributes=False) == ast.dump(expected, include_attributes=False),
            "unsupported generated version module shape")
    return commit


def verify_sdist_regeneration(archive, pristine_manifest, built_manifest, wheel,
                              native_member, native_sha256):
    pristine = verify_file_manifest(pristine_manifest)
    built = verify_file_manifest(built_manifest)
    pristine_root, built_root = Path(pristine["root"]).resolve(), Path(built["root"]).resolve()
    require(pristine_root != built_root and not pristine_root.is_relative_to(built_root) and
            not built_root.is_relative_to(pristine_root), "SDIST input witness is mutable build source")
    before = {row["path"]: row for row in pristine["files"]}
    after = {row["path"]: row for row in built["files"]}
    require(before.keys() == after.keys(), "SDIST build member inventory changed")
    generated = "python/meshvale_repair/_version.py"
    require(generated in before and "PKG-INFO" in before, "missing SDIST version inputs")
    require(all(row == after[name] for name, row in before.items() if name != generated),
            "ordinary SDIST member changed during wheel build")
    archived = {}
    top_names = set()
    with tarfile.open(archive) as package:
        for member in package.getmembers():
            relative = PurePosixPath(member.name)
            require(not relative.is_absolute() and ".." not in relative.parts and
                    (member.isfile() or member.isdir()), "invalid bound SDIST member")
            if member.isdir():
                continue
            require(len(relative.parts) >= 2, "SDIST member lacks source root")
            top_names.add(relative.parts[0])
            name = "/".join(relative.parts[1:])
            require(name in before and name not in archived, "unexpected bound SDIST member")
            payload = package.extractfile(member).read()
            archived[name] = {"path": name, "bytes": len(payload),
                              "sha256": hashlib.sha256(payload).hexdigest(),
                              "lf_sha256": hashlib.sha256(payload.replace(b"\r\n", b"\n")).hexdigest()}
    require(len(top_names) == 1 and archived == before, "SDIST archive/input witness mismatch")
    full, version_tuple = distribution_identity((pristine_root / "PKG-INFO").read_bytes())
    input_bytes, output_bytes = (pristine_root / generated).read_bytes(), (built_root / generated).read_bytes()
    input_commit = generated_version_identity(input_bytes, full, version_tuple)
    output_commit = generated_version_identity(output_bytes, full, version_tuple)
    require(output_commit is None or output_commit == input_commit, "regenerated commit identity changed")
    with zipfile.ZipFile(wheel) as package:
        metadata = [name for name in package.namelist() if name.endswith(".dist-info/METADATA")]
        require(len(metadata) == 1 and distribution_identity(package.read(metadata[0])) == (full, version_tuple),
                "SDIST/wheel FULL mismatch")
        require(package.read("meshvale_repair/_version.py") == output_bytes,
                "wheel generated version differs from actual build output")
        require(native_member.startswith("meshvale_repair/_repair.") and native_member.endswith((".pyd", ".so")) and
                hashlib.sha256(package.read(native_member)).hexdigest() == native_sha256,
                "regenerated wheel/native mismatch")
    return {"full": full, "version_tuple": list(version_tuple),
            "input_version": before[generated], "output_version": after[generated],
            "input_commit": input_commit, "output_commit": output_commit,
            "archive_members": len(before), "unchanged_members": len(before) - 1}


def validated_receiver(path):
    receiver = checked_schema(load(path), "receiver")
    roots = receiver["roots"]
    verify_refs(receiver, roots)
    producer_path = resolve(receiver["producer_record"], roots)
    producer = validated_producer(producer_path)
    validated_compile(receiver["receiver"], roots)
    require(Path(roots["sdk"]).resolve() == Path(producer["roots"]["sdk"]).resolve() and
            Path(roots["producer_source"]).resolve() == Path(producer["roots"]["producer_source"]).resolve(),
            "receiver substituted producer roots")
    for name in ("interchange_manifest", "geometry_manifest"):
        same_refs(receiver["sources"][name], roots, producer["sources"][name], producer["roots"])
    for name in ("repair_manifest", "interchange_manifest", "geometry_manifest"):
        verify_file_manifest(resolve(receiver["sources"][name], roots))
    require((receiver["origin"] == "sdist") == (receiver["sources"]["sdist"] is not None) and
            (receiver["origin"] == "sdist") == (receiver["sources"]["sdist_member_manifest"] is not None),
            "source origin mismatch")
    if receiver["origin"] == "sdist":
        verify_file_manifest(resolve(receiver["sources"]["sdist_member_manifest"], roots))
        require(not (Path(roots["receiver_source"]) / ".git").exists(), "sdist has Git state")
        module = receiver["module_build"]
        verify_sdist_regeneration(resolve(receiver["sources"]["sdist"], roots),
            resolve(receiver["sources"]["sdist_member_manifest"], roots),
            resolve(receiver["sources"]["repair_manifest"], roots),
            resolve(module["wheel"], roots), module["wheel_member_name"], module["post_link"]["sha256"])
    bridge = load(resolve(receiver["sources"]["raw_lf_bridge"], roots))
    require(set(bridge) == {"original_manifest", "required", "generated_version_paths"}, "invalid source bridge")
    verify_refs(bridge, roots)
    original = load(resolve(bridge["original_manifest"], roots))
    original_rows = {row["path"]: row for row in original["files"]}
    actual_rows = {row["path"]: row for row in verify_file_manifest(resolve(receiver["sources"]["repair_manifest"], roots))["files"]}
    needed = {name for name in original_rows if (name.startswith(("include/", "src/", "python/", "tests/", "scripts/", "cmake/")) or
              name in {"CMakeLists.txt", "pyproject.toml"}) and name not in bridge["generated_version_paths"]}
    require(len(bridge["required"]) == len(needed) and {row["path"] for row in bridge["required"]} == needed,
            "incomplete source bridge")
    require(bridge["generated_version_paths"] == ["PKG-INFO", "python/meshvale_repair/_version.py"], "unreviewed source exception")
    for row in bridge["required"]:
        require(row["path"] in actual_rows and row["sha256"] == original_rows[row["path"]]["sha256"] == actual_rows[row["path"]]["sha256"] and
                row["lf_sha256"] == original_rows[row["path"]]["lf_sha256"] == actual_rows[row["path"]]["lf_sha256"], "source bridge mismatch")
    module = receiver["module_build"]
    process = validated_process(module["actual_link_process"], roots)
    require(process["returncode"] == 0, "normal module build failed")
    same_refs(module["actual_link_process"], roots, module["build_record"], roots)
    defines = load(resolve(module["normal_defines"], roots))
    require(set(defines) == {"compile_entry", "argv", "test_variant"} and defines["test_variant"] is False,
            "invalid normal module definitions")
    same_refs(defines["compile_entry"], roots, receiver["receiver"]["production_compile_command"], roots)
    command = load(resolve(defines["compile_entry"], roots))
    require(defines["argv"] == (command.get("arguments") or split_command(command["command"])) and
            not any("MESHVALE_REPAIR_GLTF_BINDING_TESTS" in arg for arg in defines["argv"]), "test variant compile")
    libraries = remap_refs(producer["sdk"]["libraries"], producer["roots"], roots)
    linked = load(resolve(module["resolved_link_inputs"], roots))
    require(linked and all(ref in libraries for ref in linked) and any(
        Path(ref["path"]).name in {"meshvale_interchange.lib", "libmeshvale_interchange.a"} for ref in linked), "module SDK mismatch")
    actual = resolve(process["stdout"], roots).read_text(encoding="utf-8", errors="replace").replace("\\", "/").lower()
    require(all(str(resolve(ref, roots)).replace("\\", "/").lower() in actual for ref in linked),
            "SDK not in actual normal link process")
    require(resolve(module["post_link"], roots).name.lower() in actual, "normal module not in actual link output")
    dependencies = verify_dependencies(resolve(module["actual_header_dependencies"], roots))
    sdk = Path(roots["sdk"]).resolve()
    headers = [row for row in dependencies["files"] if "/meshvale/interchange/" in row["path"].replace("\\", "/")]
    require(headers and all(Path(row["path"]).resolve().is_relative_to(sdk / "include") for row in headers),
            "substituted module SDK headers")
    require(module["repair_transform"] is None, "repair transform requires separately implemented process verification")
    require(module["post_link"]["sha256"] == module["pre_repair_member"]["sha256"] == module["post_repair_member"]["sha256"],
            "wheel member differs from actual linked module")
    import zipfile
    with zipfile.ZipFile(resolve(module["wheel"], roots)) as package:
        member = module["wheel_member_name"]
        require(member.startswith("meshvale_repair/_repair.") and member.endswith((".pyd", ".so")) and
                hashlib.sha256(package.read(member)).hexdigest() == module["post_link"]["sha256"], "wheel/module mismatch")
    inventory = load(resolve(receiver["positive_inventory"], roots))
    require(inventory == load(DATA / "positive-inventory.json"), "unexpected-test-inventory")
    for row in inventory["methods"]:
        require(sha(Path(roots["receiver_source"]) / row["new_path"]) == row["source_sha256"], "strict test changed")
    return receiver, producer


def validated_cohort(path):
    manifest = checked_schema(load(path), "manifest")
    roots = manifest["roots"]
    verify_refs(manifest, roots)
    receiver, producer = validated_receiver(resolve(manifest["receiver_record"], roots))
    for name in receiver["roots"]:
        if name != "evidence":
            require(Path(roots[name]).resolve() == Path(receiver["roots"][name]).resolve(), "cohort root substitution")
    same_refs(manifest["producer_record"], roots, receiver["producer_record"], receiver["roots"])
    require(manifest["origin"] == receiver["origin"], "origin mismatch")
    for name in ("producer", "sdk", "unsupported_control"):
        same_refs(manifest[name], roots, producer[name], producer["roots"])
    for name in ("receiver", "sources", "positive_inventory"):
        same_refs(manifest[name], roots, receiver[name], receiver["roots"])
    for name, value in receiver["module_build"].items():
        same_refs(manifest["module"][name], roots, value, receiver["roots"])
    loader = checked_schema(load(resolve(manifest["module"]["loader_observation"], roots)), "loader")
    verify_refs(loader, roots)
    native = resolve(manifest["module"]["installed_member"], roots)
    require(native.is_relative_to(Path(roots["installed"]).resolve()) and
            loader["module_file"] == str(native) and loader["spec_origin"] == str(native) and
            native.is_relative_to(Path(loader["sys_prefix"]).resolve()), "module-identity-mismatch")
    require(all(ref["sha256"] == manifest["module"]["post_link"]["sha256"] for ref in
                (loader["before_import"], loader["after_import"], loader["wheel_member"], manifest["module"]["installed_member"])),
            "module-identity-mismatch")
    require(sha(native) == manifest["module"]["post_link"]["sha256"], "module-identity-mismatch")
    return manifest


ABI_SOURCE = r'''
#define MV_STRING_INNER(x) #x
#define MV_STRING(x) MV_STRING_INNER(x)
#if defined(_M_X64) || defined(__x86_64__)
MV_DOMAIN_ARCH "x64"
#elif defined(_M_ARM64) || defined(__aarch64__)
MV_DOMAIN_ARCH "arm64"
#else
MV_DOMAIN_ARCH "unknown"
#endif
#ifdef _MSVC_LANG
MV_DOMAIN_STANDARD MV_STRING(_MSVC_LANG)
#else
MV_DOMAIN_STANDARD MV_STRING(__cplusplus)
#endif
#if defined(_DLL) && defined(_MT) && !defined(_DEBUG)
MV_DOMAIN_RUNTIME "MD"
#elif defined(_MT) && !defined(_DEBUG)
MV_DOMAIN_RUNTIME "MT"
#elif defined(_DLL) && defined(_DEBUG)
MV_DOMAIN_RUNTIME "MDd"
#elif defined(_MT) && defined(_DEBUG)
MV_DOMAIN_RUNTIME "MTd"
#else
MV_DOMAIN_RUNTIME "posix"
#endif
#ifdef _ITERATOR_DEBUG_LEVEL
MV_DOMAIN_ITERATOR MV_STRING(_ITERATOR_DEBUG_LEVEL)
#else
MV_DOMAIN_ITERATOR "none"
#endif
#ifdef _MSVC_STL_VERSION
MV_DOMAIN_STDLIB "msvc:" MV_STRING(_MSVC_STL_VERSION) ":" MV_STRING(_MSVC_STL_UPDATE)
#elif defined(__GLIBCXX__)
MV_DOMAIN_STDLIB "libstdc++:" MV_STRING(__GLIBCXX__) ":" MV_STRING(_GLIBCXX_USE_CXX11_ABI) ":gxx:" MV_STRING(__GXX_ABI_VERSION) ":debug:" MV_STRING(_GLIBCXX_DEBUG) ":assertions:" MV_STRING(_GLIBCXX_ASSERTIONS)
#elif defined(_LIBCPP_VERSION)
MV_DOMAIN_STDLIB "libc++:" MV_STRING(_LIBCPP_VERSION) ":" MV_STRING(_LIBCPP_ABI_VERSION) ":hardening:" MV_STRING(_LIBCPP_HARDENING_MODE)
#else
MV_DOMAIN_STDLIB "unknown"
#endif
'''
