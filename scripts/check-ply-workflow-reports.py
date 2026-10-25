# SPDX-License-Identifier: Apache-2.0
"""Read-only, bounded oracle for supplied native PLY workflow observations.

No producer, delegate, package installation, or binary execution occurs here.
See tests/fixtures/ply-workflow/README.md for the case and observation interfaces.
"""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys


MAX_CASES = 128
MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_ENTRY_BYTES = 64 * 1024 * 1024
MAX_JSON_DEPTH = 64
HASH_BLOCK_BYTES = 65536
U64_MAX = (1 << 64) - 1
LITERAL_SHA256 = "aa59d36ef3f46f485ed3b2cd02c7f4882e995dce4b3e21e0f1433714ea4bf100"
EMPTY_LITERAL_SHA256 = "6b78a62a9f50c0ae424bf8c51bf4b72f2089b8ce380a24ab8a3a173eedc39029"
MAX_FORMAT_LINE_BYTES = 128
PUBLISHED_MEMBER_COUNT = 2
ENCODINGS = ("ascii", "binary_little_endian", "binary_big_endian")
STAGES = ("configuration", "source-protection", "repair", "write", "reload",
          "verify", "hash", "report", "publication", "complete")
CORE_STAGES = ("import", "inspect", "repair", "verify", "export", "report", "complete")
CHECKS = ("source-storage", "target-policy", "candidate-storage",
          "repair-correspondence", "canonical-preservation", "reload-candidate",
          "source-unchanged", "typed-properties", "wire-schema")
STAGED_CHECKS = ("staged-candidate", "staged-physical-members")
STATS_KEYS = ("planned_jobs", "launched_workers", "joined_workers", "caller_jobs")
CODEC_KEYS = ("codec", "materialization", "inspection")
REPAIR_STATS = ("source_inspection", "eligibility", "comparison", "selection",
                "correspondence", "candidate_inspection", "verification", "directed_verification")
PHASES = ("repair", "writing", "reload", "source_protection", "repair_correspondence",
          "canonical_preservation", "typed_properties", "wire_schema", "reload_verification",
          "source_verification", "staged_candidate", "hashing", "report_copy", "publication")
EXT_KEYS = ("version", "native_status", "native_stage", "native_diagnostic", "source",
            "policies", "memory_output_verified", "source_faces", "source_corners",
            "write_facts", "phase_diagnostics", "publisher", "artifact_paths",
            "hash_bytes_processed", "requested_bytes_before_report_copy", "phase_execution")
MESH_KEYS = ("counts", "positions_f64_hex", "face_offsets", "corner_vertices", "attributes")
MAP_KEYS = ("face_map", "corner_map", "source_faces", "source_corners")
CASE_KEYS = ("id", "kind", "source_encoding", "encoding", "source_file", "native_file",
             "report_file", "memory_output_file", "bundle_dir", "entry_name", "receipt_name", "input_id",
             "targets", "declarations", "expected")
EXPECTED_KEYS = ("native_status", "native_stage", "report_native_stage", "execution", "core_stage", "candidate",
                 "profile", "publication", "exit", "input_snapshot", "repair_completed",
                 "repair_called", "writing_called", "reload_called", "write_facts_available",
                 "memory_output_verified", "input_hash_available", "report_is_first", "tool")
EXPECTED_BOOL_KEYS = ("input_snapshot", "repair_completed", "repair_called", "writing_called",
                      "reload_called", "write_facts_available", "memory_output_verified",
                      "input_hash_available", "report_is_first")
NATIVE_KEYS = ("status", "stage", "execution_outcome", "candidate_outcome", "profile_outcome",
               "publication_outcome", "exit_code", "repair_completed", "memory_output_verified",
               "input_sha256", "face_map", "corner_map", "source_faces", "source_corners",
               "source_mesh", "candidate_mesh", "reload_mesh", "published_mesh",
               "reload_schema", "published_schema", "execution", "diagnostic", "publisher",
               "source_schema", "source_encoding", "reload_encoding", "published_encoding")
INPUTS = {}


class Rejected(ValueError):
    pass


def require(condition, where):
    if not condition:
        raise Rejected(where)


def closed(value, keys, where):
    require(type(value) is dict and set(value) == set(keys), where + ": closed keys")
    return value


def uint(value, where, maximum=U64_MAX):
    require(type(value) is int and 0 <= value <= maximum, where + ": unsigned integer")


def boolean(value, where):
    require(type(value) is bool, where + ": boolean")


def string(value, where):
    require(type(value) is str, where + ": string")


def one_of(value, choices, where):
    require(type(value) is str and value in choices, where + ": enum string")


def nullable(value, validator, where):
    if value is not None:
        validator(value, where)


def exact(actual, expected, where):
    require(type(actual) is type(expected), where + ": exact type")
    if type(expected) is dict:
        closed(actual, expected, where)
        for key in expected:
            exact(actual[key], expected[key], where + "." + key)
    elif type(expected) is list:
        require(len(actual) == len(expected), where + ": extent")
        for i, item in enumerate(expected):
            exact(actual[i], item, f"{where}[{i}]")
    else:
        require(actual == expected, where + ": literal equality")


def array(value, validator, where):
    require(type(value) is list, where + ": array")
    for i, item in enumerate(value):
        validator(item, f"{where}[{i}]")


def pairs(items):
    result = {}
    for key, value in items:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def depth(value, level=0):
    require(level <= MAX_JSON_DEPTH, "JSON nesting bound")
    if type(value) is dict:
        for item in value.values():
            depth(item, level + 1)
    elif type(value) is list:
        for item in value:
            depth(item, level + 1)
    elif type(value) is float:
        require(math.isfinite(value), "nonfinite decoded JSON number")


def finite_float(text):
    value = float(text)
    require(math.isfinite(value), "nonfinite or overflow JSON number: " + text)
    return value


def absolute(value):
    string(value, "path")
    p = Path(value)
    require(p.is_absolute(), "explicit absolute path required: " + value)
    return p


def read_bytes(path, cap):
    p = absolute(str(path))
    with p.open("rb") as stream:
        data = stream.read(cap + 1)
    require(len(data) <= cap, "reader byte bound: " + str(p))
    INPUTS[str(p)] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    return data


def read_json(path, *, fixture=False):
    raw = read_bytes(path, MAX_JSON_BYTES)
    if fixture:
        canonical = raw.replace(b"\r\n", b"\n")
        INPUTS[str(absolute(str(path)))].update(
            canonical_sha256=hashlib.sha256(canonical).hexdigest(),
            canonical_bytes=len(canonical))
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                       parse_float=finite_float,
                       parse_constant=lambda value: (_ for _ in ()).throw(Rejected("nonfinite JSON: " + value)))
    depth(value)
    return value


def fixture_json(path, expected_sha256):
    value = read_json(path, fixture=True)
    exact(INPUTS[str(absolute(str(path)))]["canonical_sha256"], expected_sha256,
          "fixture CRLF-to-LF container identity")
    return value


def digest(path):
    p = absolute(str(path))
    h = hashlib.sha256()
    total = 0
    with p.open("rb") as stream:
        while block := stream.read(HASH_BLOCK_BYTES):
            total += len(block)
            require(total <= MAX_ENTRY_BYTES, "physical entry reader bound: " + str(p))
            h.update(block)
    value = h.hexdigest()
    INPUTS[str(p)] = {"sha256": value, "bytes": total}
    return value


def physical_encoding(path):
    p = absolute(str(path))
    with p.open("rb") as stream:
        marker = stream.readline(MAX_FORMAT_LINE_BYTES + 1)
        declaration = stream.readline(MAX_FORMAT_LINE_BYTES + 1)
    require(len(marker) <= MAX_FORMAT_LINE_BYTES and len(declaration) <= MAX_FORMAT_LINE_BYTES,
            "bounded physical format prefix")
    require(marker in (b"ply\n", b"ply\r\n"), "physical PLY marker")
    for encoding in ENCODINGS:
        prefix = ("format " + encoding + " 1.0").encode("ascii")
        if declaration in (prefix + b"\n", prefix + b"\r\n"):
            return encoding
    raise Rejected("actual physical PLY format declaration")


def physical_members(root, names):
    found = set()
    with os.scandir(root) as entries:
        for ordinal, entry in enumerate(entries):
            require(ordinal < PUBLISHED_MEMBER_COUNT, "extra physical bundle member")
            require(entry.is_file(follow_symlinks=False), "regular flat bundle member")
            found.add(entry.name)
    exact(found, set(names), "exact physical two-member inventory")


def stats(value, where):
    closed(value, STATS_KEYS, where)
    for key, item in value.items():
        uint(item, where + "." + key)
    require(value["launched_workers"] == value["joined_workers"], where + ": all workers joined")


def codec_stats(value, where):
    closed(value, CODEC_KEYS, where)
    for key, item in value.items():
        stats(item, where + "." + key)
    exact(value["codec"], dict.fromkeys(STATS_KEYS, 0), where + ": actual serial codec counters")


def file_execution(value, where):
    closed(value, ("capture", "copying", "byte_verification", "mesh_verification", "expected_inspection", "ply"), where)
    for key, item in value.items():
        (codec_stats if key == "ply" else stats)(item, where + "." + key)


def phase_execution(value, where):
    closed(value, PHASES, where)
    closed(value["repair"], REPAIR_STATS, where + ".repair")
    for key, item in value["repair"].items():
        stats(item, where + ".repair." + key)
    for key, item in value.items():
        if key == "repair":
            continue
        if key in ("writing", "reload"):
            codec_stats(item, where + "." + key)
        elif key == "publication":
            nullable(item, file_execution, where + ".publication")
        else:
            stats(item, where + "." + key)
    require(value["hashing"]["launched_workers"] == 0, where + ": serial SHA stream")


def zero_counters(value, where):
    if type(value) is dict and set(value) == set(STATS_KEYS):
        exact(value, dict.fromkeys(STATS_KEYS, 0), where + ": unreached counters")
    else:
        require(type(value) is dict, where + ": counter group")
        for key, item in value.items():
            zero_counters(item, where + "." + key)


def native_reachability(native, expected):
    actual = native["execution"]
    for key, flag in (("repair", "repair_called"), ("writing", "writing_called"), ("reload", "reload_called")):
        if not expected[flag]:
            zero_counters(actual[key], "actual uncalled " + key)
    if not expected["reload_called"]:
        for key in ("repair_correspondence", "canonical_preservation", "typed_properties", "wire_schema", "reload_verification", "source_verification", "staged_candidate", "hashing"):
            zero_counters(actual[key], "actual unreached " + key)
    if expected["publication"] == "not-requested":
        require(native["publisher"] is None and actual["publication"] is None,
                "actual memory-only publisher never called")


def phase_consistency(ext, native, expected):
    phases = ext["phase_diagnostics"]
    actual = native["execution"]
    for key, flag in (("repair", "repair_called"), ("writing", "writing_called"), ("reload", "reload_called")):
        if not expected[flag]:
            require(phases[key] is None, "uncalled phase diagnostic absent: " + key)
            zero_counters(ext["phase_execution"][key], "report uncalled " + key)
    successful = {
        "repair": native["repair_completed"] or expected["writing_called"],
        "writing": expected["write_facts_available"] or expected["reload_called"],
        "reload": native["memory_output_verified"] or (expected["reload_called"] and native["diagnostic"]["stage"] in (5, 6, 7, 8, 9)),
    }
    for key, succeeded in successful.items():
        if succeeded:
            require(phases[key] is not None, "successful actual phase missing: " + key)
            exact(phases[key]["status"], 0, "actual prior phase succeeded: " + key)
            exact(phases[key]["diagnostic"]["code"], 0, "actual prior success diagnostic: " + key)
    primary = native["diagnostic"]["delegated"]
    if primary is not None:
        key = {2: "repair", 3: "writing", 4: "reload"}.get(native["diagnostic"]["stage"])
        if key is not None:
            exact(primary["kind"], "repair" if key == "repair" else "ply", "primary delegate kind")
            exact(phases[key], {"status": primary["status"], "diagnostic": primary["diagnostic"]}, "actual primary delegated phase")
    if not expected["reload_called"]:
        for key in ("repair_correspondence", "canonical_preservation", "typed_properties", "wire_schema", "reload_verification", "source_verification", "staged_candidate", "hashing"):
            zero_counters(ext["phase_execution"][key], "report unreached " + key)


def byte_object(value, keys, where):
    closed(value, keys, where)
    for key, item in value.items():
        uint(item, where + "." + key)


def codec_bytes(value, where):
    byte_object(value, (*CODEC_KEYS, "total"), where)


def requested_bytes(value, where):
    closed(value, ("wrapper", "source_files", "source_ply", "repair", "writing", "reload", "publication_files", "publication_ply"), where)
    uint(value["wrapper"], where + ".wrapper")
    uint(value["source_files"], where + ".source_files")
    for key in ("source_ply", "writing", "reload"):
        codec_bytes(value[key], where + "." + key)
    byte_object(value["repair"], ("repair", "source_inspection", "selection", "candidate_inspection", "total"), where + ".repair")
    nullable(value["publication_files"], uint, where + ".publication_files")
    nullable(value["publication_ply"], codec_bytes, where + ".publication_ply")


def storage_issue(value, where):
    closed(value, ("code", "attribute", "element"), where)
    uint(value["code"], where + ".code", 22)
    for key in ("attribute", "element"):
        nullable(value[key], uint, where + "." + key)


def geometry_diagnostic(value, where):
    closed(value, ("code", "attribute", "element", "storage_issue"), where)
    uint(value["code"], where + ".code", 14)
    for key in ("attribute", "element"):
        nullable(value[key], uint, where + "." + key)
    nullable(value["storage_issue"], storage_issue, where + ".storage_issue")


def delegate_diagnostic(value, where, kind):
    repair = ("stage", "code", "target", "attribute", "element", "geometry_status", "geometry_diagnostic", "storage_issue")
    ply = ("stage", "code", "byte_offset", "element", "property", "row", "item", "attribute", "geometry_status", "geometry_diagnostic", "storage_issue")
    closed(value, repair if kind == "repair" else ply, where)
    uint(value["stage"], where + ".stage", 9 if kind == "repair" else 7)
    uint(value["code"], where + ".code", 21 if kind == "repair" else 39)
    for key, item in value.items():
        if key in ("stage", "code"):
            continue
        validator = geometry_diagnostic if key == "geometry_diagnostic" else storage_issue if key == "storage_issue" else uint
        nullable(item, validator, where + "." + key)
    if value["geometry_status"] is not None:
        uint(value["geometry_status"], where + ".geometry_status", 8)


def delegate(value, where, kind):
    closed(value, ("status", "diagnostic"), where)
    uint(value["status"], where + ".status", 8 if kind == "repair" else 10)
    delegate_diagnostic(value["diagnostic"], where + ".diagnostic", kind)


def workflow_diagnostic(value, where):
    closed(value, ("stage", "code", "element", "delegated"), where)
    uint(value["stage"], where + ".stage", 9)
    uint(value["code"], where + ".code", 25)
    nullable(value["element"], uint, where + ".element")
    if value["delegated"] is not None:
        d = closed(value["delegated"], ("kind", "status", "diagnostic"), where + ".delegated")
        one_of(d["kind"], ("repair", "ply"), where + ".kind")
        delegate({"status": d["status"], "diagnostic": d["diagnostic"]}, where + ".delegated", d["kind"])


def system_error(value, where):
    closed(value, ("category", "value"), where)
    string(value["category"], where + ".category")
    require(type(value["value"]) is int, where + ": signed error integer")


def file_diagnostic(value, where):
    closed(value, ("status", "stage", "code", "file_index", "byte_offset", "system_error", "ply_status", "ply_diagnostic", "geometry_status", "geometry_diagnostic"), where)
    for key in ("status", "stage", "code"):
        uint(value[key], where + "." + key, {"status": 15, "stage": 11, "code": 28}[key])
    for key in ("file_index", "byte_offset", "ply_status", "geometry_status"):
        nullable(value[key], uint, where + "." + key)
    if value["ply_status"] is not None:
        uint(value["ply_status"], where + ".ply_status", 10)
    if value["geometry_status"] is not None:
        uint(value["geometry_status"], where + ".geometry_status", 8)
    nullable(value["system_error"], system_error, where + ".system_error")
    nullable(value["geometry_diagnostic"], geometry_diagnostic, where + ".geometry_diagnostic")
    if value["ply_diagnostic"] is not None:
        delegate_diagnostic(value["ply_diagnostic"], where + ".ply_diagnostic", "ply")


def publisher(value, where):
    closed(value, ("outcome", "diagnostic", "cleanup", "cleanup_error"), where)
    uint(value["outcome"], where + ".outcome", 2)
    uint(value["cleanup"], where + ".cleanup", 2)
    file_diagnostic(value["diagnostic"], where + ".diagnostic")
    nullable(value["cleanup_error"], system_error, where + ".cleanup_error")


def published_final(native):
    require(native["publisher"] is not None, "actual final published publisher cannot be absent")
    exact(native["publisher"]["outcome"], 2, "actual native published outcome ordinal")
    exact(native["publisher"]["diagnostic"]["status"], 0, "actual published native success status")
    exact(native["publisher"]["diagnostic"]["stage"], 11, "actual published native complete stage")
    exact(native["publisher"]["diagnostic"]["code"], 0, "actual published native success diagnostic")
    require(native["execution"]["publication"] is not None, "actual final publication execution cannot be absent")


def declaration(value, where):
    closed(value, ("domain", "name"), where)
    one_of(value["domain"], ("vertex", "face", "corner"), where + ".domain")
    require(type(value["name"]) is str and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value["name"]), where + ": ASCII identifier")


def target(value, where):
    closed(value, ("keep", "remove"), where)
    uint(value["keep"], where + ".keep")
    uint(value["remove"], where + ".remove")


def portable_name(value, where):
    string(value, where)
    require(value not in ("", ".", "..") and "/" not in value and "\\" not in value and ":" not in value, where + ": flat portable name")


def counts(value, where):
    byte_object(value, ("vertices", "faces", "corners"), where)


def schema(value, where):
    require(type(value) is list and len(value) in (1, 2), where + ": element array")
    for i, e in enumerate(value):
        closed(e, ("kind", "original_count", "properties"), where)
        one_of(e["kind"], ("vertex", "face"), where)
        uint(e["original_count"], where)
        require(type(e["properties"]) is list, where + ": property array")
        for p in e["properties"]:
            closed(p, ("name", "kind", "value_type", "count_type", "binding", "attribute"), where)
            string(p["name"], where)
            one_of(p["kind"], ("scalar", "list"), where)
            one_of(p["value_type"], ("char", "uchar", "short", "ushort", "int", "uint", "float", "double"), where)
            one_of(p["binding"], ("x", "y", "z", "vertex-indices", "attribute"), where)
            if p["kind"] == "scalar":
                require(p["count_type"] is None, where + ": scalar count null")
            else:
                one_of(p["count_type"], ("char", "uchar", "short", "ushort", "int", "uint"), where)
            if p["binding"] == "attribute":
                uint(p["attribute"], where)
            else:
                require(p["attribute"] is None, where + ": core ordinal null")


def mesh_literal(literal, kind):
    value = literal["changed" if kind == "changed" else "unchanged"]
    result = {k: value[k] for k in MESH_KEYS if k != "positions_f64_hex"}
    result["positions_f64_hex"] = literal["positions_f64_hex"]
    if kind == "empty":
        result = {"counts": {"vertices": 0, "faces": 0, "corners": 0}, "positions_f64_hex": [], "face_offsets": [0], "corner_vertices": [], "attributes": []}
        for original in value["attributes"]:
            a = dict(original)
            a["values"] = []
            a["offsets"] = None if original["offsets"] is None else [0]
            result["attributes"].append(a)
    return result


def projected_schema(literal, current_counts):
    result = []
    for element in literal["original_schema"]:
        e = dict(element)
        e["original_count"] = current_counts["vertices" if e["kind"] == "vertex" else "faces"]
        result.append(e)
    return result


def core_maps(report, source_counts, candidate_counts, expected_maps, published):
    require(type(report["maps"]) is list, "core maps array")
    seen = set()
    for m in report["maps"]:
        closed(m, ("id", "from", "to", "domain", "kind", "entries"), "core map")
        string(m["id"], "map id")
        require(m["id"] not in seen, "duplicate map id")
        seen.add(m["id"])
        closed(m["from"], ("snapshot", "mesh"), "map from")
        closed(m["to"], ("snapshot", "mesh"), "map to")
    def check_map(start, finish, domain, expected):
        candidates = [m for m in report["maps"] if m["from"] == {"snapshot": start, "mesh": "surface"} and m["to"] == {"snapshot": finish, "mesh": "surface"} and m["domain"] == domain]
        require(len(candidates) == 1, f"one {start}->{finish}/{domain} map")
        m = candidates[0]
        if m["kind"] == "identity":
            require(m["entries"] is None, "identity entries:null")
            actual = list(range(len(expected)))
        else:
            require(m["kind"] == "explicit" and type(m["entries"]) is list, "explicit map rows")
            actual = []
            for row in m["entries"]:
                if type(row) is list:
                    require(len(row) == 1, "this repair is single-destination")
                    row = row[0]
                uint(row, "map entry")
                actual.append(row)
        exact(actual, expected, f"{start}->{finish}/{domain}")
    check_map("input", "candidate", "vertex", list(range(source_counts["vertices"])))
    check_map("input", "candidate", "face", expected_maps["face_map"])
    check_map("input", "candidate", "corner", expected_maps["corner_map"])
    if published:
        for domain, key in (("vertex", "vertices"), ("face", "faces"), ("corner", "corners")):
            check_map("candidate", "output", domain, list(range(candidate_counts[key])))
    require(len(report["maps"]) == (6 if published else 3), "exact selected map set")


def validate_case(case, literal, empty_literal, shared):
    closed(case, CASE_KEYS, "case")
    string(case["id"], "case id")
    one_of(case["kind"], ("changed", "unchanged", "empty", "rejected", "early-failure"), "case kind")
    for key in ("source_encoding", "encoding"):
        one_of(case[key], ENCODINGS, key)
    portable_name(case["entry_name"], "entry_name")
    if case["receipt_name"] is not None:
        portable_name(case["receipt_name"], "receipt_name")
    array(case["targets"], target, "targets")
    e = closed(case["expected"], EXPECTED_KEYS, "case expected")
    if case["report_file"] is None and e["native_status"] == 2:
        require(type(case["declarations"]) is list, "raw rejected declaration array")
        for d in case["declarations"]:
            closed(d, ("domain", "name"), "raw rejected declaration")
            string(d["domain"], "raw declaration domain spelling")
            string(d["name"], "raw declaration name")
    else:
        array(case["declarations"], declaration, "declarations")
        require(len({(d["domain"], d["name"]) for d in case["declarations"]}) == len(case["declarations"]), "unique declarations")
    uint(e["native_status"], "expected status", 8)
    one_of(e["native_stage"], STAGES, "expected stage")
    if case["report_file"] is None:
        require(e["report_native_stage"] is None, "unavailable report has no sampled stage")
    else:
        one_of(e["report_native_stage"], STAGES, "expected sampled report stage")
    one_of(e["execution"], ("completed", "failed", "cancelled"), "expected execution")
    one_of(e["core_stage"], CORE_STAGES, "expected core stage")
    one_of(e["candidate"], ("accepted", "unchanged", "rejected", "not-attempted"), "expected candidate")
    one_of(e["profile"], ("passed", "failed", "incomplete"), "expected profile")
    one_of(e["publication"], ("published", "failed", "cancelled", "not-attempted", "not-requested"), "expected publication")
    require(type(e["exit"]) is int and e["exit"] in (0, 1, 2, 130), "expected exit")
    for key in EXPECTED_BOOL_KEYS:
        boolean(e[key], "expected." + key)
    closed(e["tool"], ("name", "version"), "expected actual configured tool")
    for key in ("name", "version"):
        require(type(e["tool"][key]) is str and bool(e["tool"][key]), "explicit configured tool " + key)
    expected_exit = (130 if e["execution"] == "cancelled" else 2 if e["execution"] == "failed"
                     else 1 if e["candidate"] in ("rejected", "not-attempted") or e["profile"] != "passed" else 0)
    exact(e["exit"], expected_exit, "contract exit precedence")
    require(e["repair_completed"] == (e["candidate"] in ("accepted", "unchanged")), "actual completed candidate availability")
    require(not e["repair_completed"] or e["repair_called"], "completed engine was called")
    require(not e["write_facts_available"] or e["writing_called"], "actual writer output needs called writer")
    require(not e["reload_called"] or e["write_facts_available"], "actual reload needs writer output")
    require(not e["memory_output_verified"] or (e["repair_completed"] and e["writing_called"] and e["reload_called"]), "verified owners imply reached delegates")
    require(not e["input_hash_available"] or e["memory_output_verified"], "source hash follows verified memory")
    if e["publication"] == "published":
        require(e["native_status"] == 0 and e["native_stage"] == "complete" and e["core_stage"] == "complete"
                and e["execution"] == "completed" and e["profile"] == "passed", "true published success tuple")
        exact(e["report_native_stage"], "complete", "conditional receipt completion stage")
    native = closed(read_json(absolute(case["native_file"])), NATIVE_KEYS, "actual native sidecar")
    for key, ek in (("status", "native_status"), ("stage", "native_stage"), ("execution_outcome", "execution"), ("candidate_outcome", "candidate"), ("profile_outcome", "profile"), ("publication_outcome", "publication"), ("exit_code", "exit"), ("repair_completed", "repair_completed"), ("memory_output_verified", "memory_output_verified")):
        expected = None if key == "candidate_outcome" and e[ek] == "not-attempted" else e[ek]
        exact(native[key], expected, "native." + key)
    phase_execution(native["execution"], "actual native execution")
    workflow_diagnostic(native["diagnostic"], "actual primary diagnostic")
    nullable(native["publisher"], publisher, "actual publisher")
    native_reachability(native, e)
    source_hash = digest(absolute(case["source_file"]))
    expected_source_hash = (empty_literal["encodings"][case["source_encoding"]]["sha256"]
                            if case["kind"] == "empty" else literal["source_hashes"][case["source_encoding"]])
    exact(source_hash, expected_source_hash, "pinned actual raw source bytes for every supported kind")
    exact(physical_encoding(case["source_file"]), case["source_encoding"], "actual source format declaration")
    exact(native["source_encoding"], case["source_encoding"], "actual source Document.Encoding")
    if e["input_hash_available"]:
        exact(native["input_sha256"], source_hash, "actual native source digest")
    else:
        require(native["input_sha256"] is None, "no invented source digest")
    source_mesh = mesh_literal(literal, "empty" if case["kind"] == "empty" else "unchanged")
    exact(native["source_mesh"], source_mesh, "actual source mesh literal")
    exact(native["source_schema"], projected_schema(literal, source_mesh["counts"]), "actual source Document.Schema")
    maps = {"face_map": [], "corner_map": [], "source_faces": [], "source_corners": []}
    candidate = mesh_literal(literal, case["kind"])
    if e["repair_completed"]:
        if case["kind"] != "empty":
            m = literal["changed" if case["kind"] == "changed" else "unchanged"]["maps"]
            maps = dict(zip(MAP_KEYS, (m["FaceMap"], m["CornerMap"], m["SourceFaces"], m["SourceCorners"])))
        exact(native["candidate_mesh"], candidate, "actual candidate mesh literal")
        for key in MAP_KEYS:
            exact(native[key], maps[key], "actual engine." + key)
    else:
        for key in (*MAP_KEYS, "candidate_mesh"):
            require(native[key] is None, "no invented completed owner: " + key)
    current_schema = projected_schema(literal, candidate["counts"])
    for key, available in (("reload", e["memory_output_verified"]), ("published", e["publication"] == "published")):
        if available:
            exact(native[key + "_mesh"], candidate, "actual " + key + " literal")
            exact(native[key + "_schema"], current_schema, "actual " + key + " ordered schema")
            exact(native[key + "_encoding"], case["encoding"], "actual " + key + " Document.Encoding")
        else:
            require(native[key + "_mesh"] is None and native[key + "_schema"] is None, "unavailable " + key)
            require(native[key + "_encoding"] is None, "unavailable actual " + key + " encoding")
    output_hash = None
    if e["memory_output_verified"]:
        output_hash = digest(absolute(case["memory_output_file"]))
        exact(physical_encoding(case["memory_output_file"]), case["encoding"], "actual memory output format declaration")
    else:
        require(case["memory_output_file"] is None, "no exposed unverified output bytes")
    if case["report_file"] is None:
        require(e["publication"] != "published", "published receipt cannot be absent")
        require(case["bundle_dir"] is None, "unpublished case has no bundle")
        return {"id": case["id"], "native_receipt": "absent", "shared_validation": "not performed"}
    report_path = absolute(case["report_file"])
    report = read_json(report_path)
    require(type(case["input_id"]) is str and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.:-]{0,255}", case["input_id"]), "reported ID grammar/extent")
    closed(report, ("schema", "tool", "input", "operation", "execution", "candidate", "profile", "snapshots", "diagnostics", "coverage", "maps", "preservation", "publication", "extensions"), "report/1")
    exact(report["schema"], "meshvale.report/1", "report schema")
    if shared is not None:
        shared.validate(report)
    closed(report["tool"], ("name", "version"), "tool")
    exact(report["tool"], e["tool"], "actual independently supplied configured tool identity")
    closed(report["input"], ("id", "sha256"), "input")
    exact(report["input"], {"id": case["input_id"], "sha256": source_hash if e["input_hash_available"] else None}, "input identity/raw digest")
    op = closed(report["operation"], ("kind", "name", "version", "options", "targets"), "operation")
    exact((op["kind"], op["name"], op["version"]), ("repair", "remove-duplicate-faces", "1"), "operation identity")
    exact(op["options"], {"encoding": case["encoding"], "rewrite": "canonical", "targets": case["targets"], "row_local_attributes": case["declarations"]}, "closed options")
    exact(op["targets"], ([{"snapshot": "input", "mesh": "surface", "domain": "face", "index": t["remove"]} for t in case["targets"]] if e["input_snapshot"] else []), "available core target references")
    closed(report["execution"], ("outcome", "stage", "reason"), "execution")
    exact(report["execution"]["outcome"], e["execution"], "execution outcome")
    exact(report["execution"]["stage"], e["core_stage"], "execution core stage")
    closed(report["candidate"], ("outcome", "snapshot"), "candidate")
    exact(report["candidate"], {"outcome": e["candidate"], "snapshot": "candidate" if e["repair_completed"] else None}, "candidate observation")
    published = e["publication"] == "published"
    publication_requested = e["publication"] != "not-requested"
    prof = closed(report["profile"], ("name", "version", "required_checks", "outcome"), "profile")
    exact(prof, {"name": "ply-typed-duplicate-publication" if publication_requested else "ply-typed-duplicate-preservation", "version": "1", "required_checks": list(CHECKS + (STAGED_CHECKS if publication_requested else ())), "outcome": e["profile"]}, "selected profile")
    seen = {}
    for item in report["coverage"]:
        closed(item, ("id", "check", "version", "scope", "status", "finding", "reason", "diagnostics"), "coverage")
        require(item["id"] not in seen, "unique coverage id")
        seen[item["id"]] = item
        one_of(item["status"], ("performed", "unsupported", "skipped", "failed"), "coverage status")
        require(item["finding"] in (None, "passed", "failed"), "coverage finding")
        if item["status"] == "performed":
            require(item["finding"] in ("passed", "failed"), "performed finding")
        else:
            require(item["finding"] is None and type(item["reason"]) is str and bool(item["reason"]), "unperformed reason")
    for check in prof["required_checks"]:
        require(check in seen and seen[check]["check"] == check, "required check retained: " + check)
        if e["profile"] == "passed":
            require(seen[check]["status"] == "performed" and seen[check]["finding"] == "passed", "actual passed required check: " + check)
    required = [seen[check] for check in prof["required_checks"]]
    derived_profile = ("failed" if any(c["status"] == "performed" and c["finding"] == "failed" for c in required)
                       else "incomplete" if any(c["status"] != "performed" for c in required) else "passed")
    exact(prof["outcome"], derived_profile, "required-check profile derivation")
    if "solid-validity" in seen:
        require(seen["solid-validity"]["status"] == "unsupported", "solid capability unsupported")
    snapshots = {}
    for snap in report["snapshots"]:
        closed(snap, ("id", "role", "meshes"), "snapshot")
        require(snap["id"] not in snapshots, "unique snapshot")
        expected_count = source_mesh["counts"] if snap["role"] == "input" else candidate["counts"]
        exact(snap["meshes"], [{"id": "surface", "vertex_count": expected_count["vertices"], "face_count": expected_count["faces"], "corner_count": expected_count["corners"]}], "snapshot actual counts")
        exact(snap["id"], snap["role"], "selected snapshot name/role")
        snapshots[snap["id"]] = snap
    expected_snaps = ({"input"} if e["input_snapshot"] else set()) | ({"candidate"} if e["repair_completed"] else set()) | ({"output"} if published else set())
    require(set(snapshots) == expected_snaps, "only actual reached snapshots")
    if e["repair_completed"]:
        require(e["input_snapshot"], "completed repair has actual input snapshot")
        core_maps(report, source_mesh["counts"], candidate["counts"], maps, published)
    else:
        exact(report["maps"], [], "no invented maps")
    closed(report["extensions"], ("meshvale.repair.ply",), "native extension namespace")
    ext = closed(report["extensions"]["meshvale.repair.ply"], EXT_KEYS, "native extension")
    exact(ext["version"], 1, "integer extension version")
    exact(ext["native_status"], e["native_status"], "native status ordinal")
    exact(ext["native_stage"], e["report_native_stage"], "sampled native report stage")
    workflow_diagnostic(ext["native_diagnostic"], "native diagnostic")
    original_schema = projected_schema(literal, source_mesh["counts"])
    schema(ext["source"]["schema"], "source schema")
    exact(ext["source"], {"portable_name": case["entry_name"], "encoding": case["source_encoding"], "schema": original_schema}, "source original schema/provenance")
    exact(ext["policies"], {"encoding": case["encoding"], "rewrite": "canonical", "units": "unknown", "row_local_attributes": case["declarations"]}, "explicit policies")
    exact(ext["memory_output_verified"], e["memory_output_verified"], "verified memory availability")
    for key in ("source_faces", "source_corners"):
        exact(ext[key], maps[key] if e["repair_completed"] else None, "actual inverse " + key)
    if e["write_facts_available"]:
        exact(ext["write_facts"], {"source_encoding": case["source_encoding"], "source_vertex_count": source_mesh["counts"]["vertices"], "source_face_count": source_mesh["counts"]["faces"], "encoding": case["encoding"], "rewrite_policy": "canonical", "vertex_count": candidate["counts"]["vertices"], "face_count": candidate["counts"]["faces"], "corner_count": candidate["counts"]["corners"], "property_count": 14, "correspondence": "identity"}, "genuine current writer facts/original source counts")
    else:
        require(ext["write_facts"] is None, "writer facts unavailable")
    phase = closed(ext["phase_diagnostics"], ("repair", "writing", "reload"), "phase diagnostics")
    for key, called in (("repair", e["repair_called"]), ("writing", e["writing_called"]), ("reload", e["reload_called"])):
        if called:
            delegate(phase[key], "phase." + key, "repair" if key == "repair" else "ply")
            if e["native_status"] == 0 or e["memory_output_verified"]:
                exact(phase[key]["status"], 0, "actual successful called phase")
                exact(phase[key]["diagnostic"]["code"], 0, "actual success diagnostic retained")
        else:
            require(phase[key] is None, "uncalled delegate remains null")
    nullable(ext["publisher"], publisher, "publisher")
    requested_bytes(ext["requested_bytes_before_report_copy"], "pre-copy requested bytes")
    phase_execution(ext["phase_execution"], "report phase execution")
    phase_consistency(ext, native, e)
    for key in PHASES:
        if key not in ("report_copy", "publication"):
            exact(ext["phase_execution"][key], native["execution"][key], "actual phase statistics." + key)
    if e["report_is_first"]:
        exact(ext["phase_execution"]["report_copy"], dict.fromkeys(STATS_KEYS, 0), "first report pre-copy statistics")
    if published:
        published_final(native)
        require(ext["publisher"] is None and ext["phase_execution"]["publication"] is None, "publisher has not returned while preparing conditional receipt")
        require(ext["requested_bytes_before_report_copy"]["publication_files"] is None and ext["requested_bytes_before_report_copy"]["publication_ply"] is None, "conditional publisher byte availability")
        exact(ext["native_diagnostic"], {"stage": 9, "code": 0, "element": None, "delegated": None}, "conditional completed diagnostic")
    elif not publication_requested:
        require(ext["publisher"] is None and ext["phase_execution"]["publication"] is None, "memory-only publisher never called")
    if not published:
        exact(ext["native_diagnostic"], native["diagnostic"], "actual primary failure/success diagnostic")
        exact(ext["publisher"], native["publisher"], "publisher availability/actual returned facts")
        exact(ext["phase_execution"]["publication"], native["execution"]["publication"], "actual returned publication execution")
        for key in ("publication_files", "publication_ply"):
            require((ext["requested_bytes_before_report_copy"][key] is None) == (native["publisher"] is None), "publisher request snapshot availability")
    uint(ext["hash_bytes_processed"], "actual hash traffic")
    raw_source_count = INPUTS[str(absolute(case["source_file"]))]["bytes"]
    if e["native_status"] == 0:
        require(e["input_hash_available"] and e["memory_output_verified"], "success needs verified memory and source hash")
    pub = closed(report["publication"], ("requested", "outcome", "snapshot", "artifacts", "verified", "reason"), "publication")
    exact(pub["requested"], publication_requested, "publication requested")
    exact(pub["outcome"], e["publication"], "true publication outcome")
    if published:
        root = absolute(case["bundle_dir"])
        require(case["entry_name"] != case["receipt_name"], "distinct physical entry and receipt")
        physical_members(root, (case["entry_name"], case["receipt_name"]))
        exact(read_bytes(root / case["receipt_name"], MAX_JSON_BYTES), read_bytes(report_path, MAX_JSON_BYTES), "physical receipt exact returned bytes")
        physical_hash = digest(root / case["entry_name"])
        exact(physical_encoding(root / case["entry_name"]), case["encoding"], "actual published physical format declaration")
        exact(physical_hash, output_hash, "physical entry equals actual retained memory output hash")
        exact(ext["artifact_paths"], {"entry": case["entry_name"], "receipt": case["receipt_name"]}, "committed portable paths")
        exact(pub["snapshot"], "output", "published output snapshot")
        exact(pub["verified"], True, "published verified")
        require(type(pub["artifacts"]) is list and len(pub["artifacts"]) == 1, "one entry artifact, no report self-hash")
        closed(pub["artifacts"][0], ("id", "sha256"), "artifact")
        exact(pub["artifacts"][0]["sha256"], physical_hash, "actual output SHA256")
        exact(ext["hash_bytes_processed"], raw_source_count + INPUTS[str(root / case["entry_name"])]["bytes"], "each successful raw hash stream once")
    else:
        require(case["bundle_dir"] is None, "no finalized bundle path before publication")
        exact(ext["artifact_paths"], {}, "no finalized artifact paths")
        require(pub["snapshot"] is None and pub["artifacts"] == [] and pub["verified"] is False, "unpublished has no finalized artifact/output")
        if e["native_status"] == 0:
            exact(ext["hash_bytes_processed"], raw_source_count, "memory success hashes source once")
    return {"id": case["id"], "native_receipt": "validated", "shared_validation": "performed" if shared else "not performed", "published": published}


def require_output_isolated(output, cases):
    # Resolve comparison copies only; retained paths and their raw bytes stay intact.
    resolved_output = output.resolve()
    for case in cases:
        require(type(case) is dict, "case object required for output isolation")
        bundle = case.get("bundle_dir")
        if bundle is not None:
            resolved_bundle = absolute(bundle).resolve()
            require(not resolved_output.is_relative_to(resolved_bundle),
                    "output must be outside every supplied published bundle")


def check_reports(args):
    output = absolute(args.receipt)
    require(not output.exists() and output.parent.is_dir(), "fresh receipt in an existing directory required")
    manifest = closed(read_json(absolute(args.cases)), ("schema", "literal_oracles", "literal_oracles_sha256", "empty_source_oracles", "empty_source_oracles_sha256", "cases"), "manifest")
    require(type(manifest["cases"]) is list and 0 < len(manifest["cases"]) <= MAX_CASES, "bounded nonempty case list")
    require_output_isolated(output, manifest["cases"])
    result = {"schema": "meshvale.repair.ply-native-checks/1", "scope": "Read-only Meshvale Repair PLY fixture checks; no producer or binary execution", "passed": False, "cases": [], "inputs": INPUTS, "helper_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    try:
        exact(manifest["schema"], "meshvale.repair.ply-native-cases/1", "manifest schema")
        literal = fixture_json(absolute(manifest["literal_oracles"]), LITERAL_SHA256)
        exact(manifest["literal_oracles_sha256"], LITERAL_SHA256, "literal fixture identity")
        empty_literal = fixture_json(absolute(manifest["empty_source_oracles"]), EMPTY_LITERAL_SHA256)
        exact(manifest["empty_source_oracles_sha256"], EMPTY_LITERAL_SHA256, "empty fixture input")
        require(type(manifest["cases"]) is list and 0 < len(manifest["cases"]) <= MAX_CASES, "bounded nonempty case list")
        shared = None
        if args.shared_schema:
            shared_schema = read_json(absolute(args.shared_schema))
            try:
                import jsonschema
                shared = jsonschema.Draft202012Validator(shared_schema)
            except ImportError as exc:
                result["shared_validation_unavailable"] = str(exc)
        require(not args.require_shared_validation or shared is not None, "required supplementary shared schema validator unavailable")
        seen = set()
        matrix = set()
        for case in manifest["cases"]:
            require(type(case) is dict and type(case.get("id")) is str and case["id"] not in seen, "unique string case id")
            seen.add(case["id"])
            result["cases"].append(validate_case(case, literal, empty_literal, shared))
            if case["kind"] in ("changed", "unchanged") and case["expected"]["publication"] == "published":
                key = (case["source_encoding"], case["kind"], case["encoding"])
                require(key not in matrix, "duplicate physical18 chain")
                matrix.add(key)
        if args.require_chains18:
            exact(matrix, {(a, b, c) for a in ENCODINGS for b in ("changed", "unchanged") for c in ENCODINGS}, "complete actual physical18 matrix")
        result["physical_matrix_count"] = len(matrix)
        result["native_receipts_validated"] = sum(c["native_receipt"] == "validated" for c in result["cases"])
        result["native_receipts_absent"] = sum(c["native_receipt"] == "absent" for c in result["cases"])
        result["passed"] = True
    except Exception as exc:
        result["error"] = {"type": type(exc).__name__, "message": str(exc)}
    result["created_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    with output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=True)
        stream.write("\n")
    print(json.dumps({"passed": result["passed"], "cases_validated": len(result["cases"]), "receipt": str(output)}, ensure_ascii=True))
    return 0 if result["passed"] else 2


def native_tool_from_cache(path):
    text = read_bytes(absolute(path), MAX_JSON_BYTES).decode("utf-8")
    selected = {}
    for line in text.splitlines():
        match = re.fullmatch(r"(CMAKE_PROJECT_NAME|CMAKE_PROJECT_VERSION):[^=]+=(.*)", line)
        if match:
            require(match[1] not in selected, "duplicate native project cache field")
            selected[match[1]] = match[2]
    exact(selected.get("CMAKE_PROJECT_NAME"), "MeshvaleRepair", "native project cache name")
    version = selected.get("CMAKE_PROJECT_VERSION")
    require(type(version) is str and re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version),
            "explicit native three-component project version required")
    return {"name": "meshvale-repair", "version": version}


def fixture_cases(root, tool, literal):
    # Fixed requests/expectations for tests/ply_workflow.cpp, never result-derived.
    declarations = literal["explicit_measurement_declarations"]
    target_pair = [{"keep": 0, "remove": 1}]
    unused = {"domain": "corner", "name": "unused_measurement"}
    flags = ("input_snapshot", "repair_completed", "repair_called", "writing_called",
             "reload_called", "write_facts_available", "memory_output_verified",
             "input_hash_available", "report_is_first")
    def success(changed, published):
        return dict(native_status=0, native_stage="complete",
                    report_native_stage="complete" if published else "report",
                    execution="completed", core_stage="complete",
                    candidate="accepted" if changed else "unchanged", profile="passed",
                    publication="published" if published else "not-requested", exit=0,
                    tool=tool, **dict.fromkeys(flags, True))
    def row(name, kind, source_encoding, encoding, directory, targets, declarations,
            expected, published=False, report=True, memory=True):
        return dict(id=name, kind=kind, source_encoding=source_encoding, encoding=encoding,
                    source_file=str(directory / "captured.ply"),
                    native_file=str(directory / "native.json"),
                    report_file=str(directory / "report.json") if report else None,
                    memory_output_file=str(directory / "memory-output.ply") if memory else None,
                    bundle_dir=str(directory / "bundle") if published else None,
                    entry_name="source.ply", receipt_name="meshvale-report.json" if published else None,
                    input_id="literal-ply-workflow", targets=targets,
                    declarations=declarations, expected=expected)
    cases = []
    for source_encoding in ENCODINGS:
        for changed in (False, True):
            for encoding in ENCODINGS:
                name = source_encoding + ("-changed-" if changed else "-unchanged-") + encoding
                cases.append(row(name, "changed" if changed else "unchanged", source_encoding,
                                 encoding, root / name, target_pair if changed else [],
                                 declarations if changed else [], success(changed, True), True))
    for source_encoding in ENCODINGS:
        for encoding in ENCODINGS:
            name = "empty-" + source_encoding + "-" + encoding
            cases.append(row(name, "empty", source_encoding, encoding, root / name,
                             [], [], success(False, True), True))
    controls = root / "controls"
    cases.append(row("memory-unchanged", "unchanged", "ascii", "ascii",
                     controls / "memory-unchanged", [], [], success(False, False)))
    cases.append(row("unused-declaration", "changed", "ascii", "ascii",
                     controls / "unused-declaration", target_pair, declarations + [unused],
                     success(True, False)))
    expected = dict(native_status=6, native_stage="complete", report_native_stage="complete",
                    execution="completed", core_stage="complete", candidate="rejected",
                    profile="failed", publication="not-requested", exit=1, tool=tool,
                    **dict.fromkeys(flags, False))
    expected.update(input_snapshot=True, repair_called=True, report_is_first=True)
    cases.append(row("missing-declaration", "rejected", "ascii", "ascii",
                     controls / "missing-declaration", target_pair, [], expected, memory=False))
    expected = dict(native_status=2, native_stage="configuration", report_native_stage=None,
                    execution="failed", core_stage="import", candidate="not-attempted",
                    profile="incomplete", publication="not-requested", exit=2, tool=tool,
                    **dict.fromkeys(flags, False))
    cases.append(row("duplicate-declaration", "early-failure", "ascii", "ascii",
                     controls / "duplicate-declaration", [], declarations + [unused, unused],
                     dict(expected), report=False, memory=False))
    cases.append(row("invalid-declaration", "early-failure", "ascii", "ascii",
                     controls / "invalid-declaration", [], [{"domain": "vertex", "name": "bad-name"}],
                     dict(expected), report=False, memory=False))
    expected = dict(native_status=3, native_stage="write", report_native_stage="write",
                    execution="failed", core_stage="verify", candidate="accepted",
                    profile="incomplete", publication="not-requested", exit=2, tool=tool,
                    **dict.fromkeys(flags, False))
    expected.update(input_snapshot=True, repair_completed=True, repair_called=True,
                    writing_called=True, report_is_first=True)
    cases.append(row("later-write-limit", "changed", "ascii", "ascii",
                     controls / "later-write-limit", target_pair, declarations, expected, memory=False))
    return cases


def make_cases(args):
    root = absolute(args.fixture_root)
    output = absolute(args.output)
    require(root.is_dir(), "existing retained native fixture root required")
    require(not output.exists() and output.parent.is_dir(), "new case file in an existing directory required")
    fixture_root = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "ply-workflow"
    literal_path = fixture_root / "literal-predicates.json"
    empty_path = fixture_root / "empty-source-oracles.json"
    literal = fixture_json(literal_path, LITERAL_SHA256)
    fixture_json(empty_path, EMPTY_LITERAL_SHA256)
    tool = native_tool_from_cache(args.native_cache)
    cases = fixture_cases(root, tool, literal)
    require_output_isolated(output, cases)
    for case in cases:
        for key in ("source_file", "native_file", "report_file", "memory_output_file"):
            if case[key] is not None:
                require(absolute(case[key]).is_file(), "missing retained file: " + case[key])
        if case["bundle_dir"] is not None:
            for name in (case["entry_name"], case["receipt_name"]):
                require((absolute(case["bundle_dir"]) / name).is_file(), "missing published fixture member")
    manifest = dict(schema="meshvale.repair.ply-native-cases/1",
                    literal_oracles=str(literal_path), literal_oracles_sha256=LITERAL_SHA256,
                    empty_source_oracles=str(empty_path), empty_source_oracles_sha256=EMPTY_LITERAL_SHA256,
                    cases=cases)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2, ensure_ascii=True)
        stream.write("\n")
    print(json.dumps({"case_manifest": str(output), "cases": len(cases), "native_tool": tool,
                      "native_cache": str(absolute(args.native_cache)),
                      "native_cache_sha256": INPUTS[str(absolute(args.native_cache))]["sha256"]}, ensure_ascii=True))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("make-cases", help="write fixed expectations for a retained native fixture run")
    build.add_argument("--fixture-root", required=True)
    build.add_argument("--native-cache", required=True)
    build.add_argument("--output", required=True)
    check = commands.add_parser("check", help="check supplied native observations and physical files")
    check.add_argument("--cases", required=True)
    check.add_argument("--receipt", required=True)
    check.add_argument("--require-chains18", action="store_true")
    check.add_argument("--shared-schema")
    check.add_argument("--require-shared-validation", action="store_true")
    args = parser.parse_args()
    return make_cases(args) if args.command == "make-cases" else check_reports(args)


if __name__ == "__main__":
    sys.exit(main())
