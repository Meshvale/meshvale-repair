# SPDX-License-Identifier: Apache-2.0
"""Independently check native file-workflow reports and physical hash inputs.

Run the semantic executable with --write-reports DIR first. This oracle uses
Python hashlib and Geometry's schema/reference/exit validator, never Repair's
native SHA, writer, serializer, or a generated-report patcher.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

BLOCK_BYTES = 65536
DOMAIN = b"meshvale.gltf-file-snapshot/1\0"
CONVENTION = "meshvale.gltf-file-snapshot/1"
ENTRY = "scene.gltf"
RECEIPT = "receipts/final.json"
STATIC = "receipts/static.bin"
STATIC_BYTES = b"literal static receipt input\n"
FILE_EXTENSION = "meshvale.repair.gltf.file"
MEMORY_PROFILE = ("gltf-static-duplicate-preservation", "1")
FILE_PROFILE = ("gltf-static-duplicate-publication", "1")
READBACK_CASE = "staged-byte-readback-failure"
READBACK_OUTCOMES = ("failed", "export", "accepted", 2, "failed", True)
# Independent literals from the pinned public enum wire projection. The opaque
# member is second in UTF-8 path order; byte 37 is deliberately modified only
# after the native conditional receipt has been prepared.
READBACK_PUBLISHER = {"status_ordinal": 10, "stage_ordinal": 9,
                      "code_ordinal": 25, "file_index": 1, "byte_offset": 37,
                      "resource_kind_ordinal": None, "resource_index": None,
                      "system_error": None, "gltf_diagnostic": None,
                      "inspection_status_ordinal": None, "inspection_diagnostic": None}
MEMORY_CHECKS = {
    "source-storage", "target-policy", "candidate-storage",
    "repair-correspondence", "canonical-preservation",
    "reload-selected-primitive", "retained-asset-preservation", "source-unchanged",
}
FILE_CHECKS = MEMORY_CHECKS | {"staged-selected-primitive", "staged-physical-members"}
# Core execution/stage, candidate, exit, publication and edit dimension are
# independent literals. Required profile is recomputed from actual coverage.
CASES = {
    "changed-published": ("completed", "complete", "accepted", 0, "published", True),
    "unchanged-published": ("completed", "complete", "unchanged", 0, "published", False),
    "changed-no-export": ("completed", "complete", "accepted", 0, "not-requested", True),
    "unchanged-no-export": ("completed", "complete", "unchanged", 0, "not-requested", False),
    "glb-no-export": ("completed", "complete", "accepted", 0, "not-requested", True),
    "rejected": ("completed", "complete", "rejected", 1, "not-attempted", None),
    "glb-unsupported-export": ("failed", "export", "accepted", 2, "failed", True),
    "destination-exists": ("failed", "export", "accepted", 2, "failed", True),
    "static-collision": ("failed", "export", "accepted", 2, "failed", True),
    "report-collision": ("failed", "export", "accepted", 2, "failed", True),
    "malformed-extraction": ("failed", "import", "not-attempted", 2, "not-requested", None),
    "hash-work-refused": ("failed", "report", "accepted", 2, "failed", True),
}


def require(value, message):
    if not value:
        raise ValueError(message)


def members(root):
    return sorted((p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()),
                  key=lambda name: name.encode("utf-8"))


def update_file(stream, path):
    with path.open("rb") as source:
        while block := source.read(BLOCK_BYTES):
            stream.update(block)


def file_hash(path):
    stream = hashlib.sha256()
    update_file(stream, path)
    return stream.hexdigest()


def frame(stream, data):
    stream.update(struct.pack("<Q", len(data)))
    stream.update(data)


def source_hash(root):
    names = members(root)
    with (root / ENTRY).open("rb") as entry:
        format_name = b"glb" if entry.read(4) == b"glTF" else b"gltf-json"
    stream = hashlib.sha256()
    stream.update(DOMAIN)
    frame(stream, format_name)
    stream.update(struct.pack("<Q", len(names)))
    work = len(DOMAIN) + 8 + len(format_name) + 8
    for name in names:
        relative = name.encode("utf-8")
        path = root / name
        size = path.stat().st_size
        frame(stream, relative)
        stream.update(struct.pack("<Q", size))
        update_file(stream, path)
        work += 8 + len(relative) + 8 + size
    return stream.hexdigest(), work, format_name.decode("ascii")


def check_meshes(report, changed):
    snapshots = {row["role"]: row for row in report["snapshots"]}
    if "input" in snapshots:
        meshes = snapshots["input"]["meshes"]
        require(len(meshes) == 1 and (meshes[0]["vertex_count"], meshes[0]["face_count"],
                meshes[0]["corner_count"]) == (3, 2, 6), "literal original snapshot counts")
    if changed is not None:
        require("candidate" in snapshots, "actual accepted/no-op candidate snapshot missing")
        mesh = snapshots["candidate"]["meshes"][0]
        require((mesh["vertex_count"], mesh["face_count"], mesh["corner_count"]) ==
                (3, 1 if changed else 2, 3 if changed else 6), "literal candidate counts")
        maps = {row["id"]: row for row in report["maps"]}
        if changed:
            require(maps["repair-faces"]["entries"] == [0, 0], "forward many-to-one face map")
            require(maps["repair-corners"]["entries"] == [0, 1, 2, 0, 1, 2],
                    "forward corner map must not become inverse writer selection")
        else:
            for domain in ("vertices", "faces", "corners"):
                require(maps["repair-" + domain]["kind"] == "identity", "no-op identity maps")
    if report["publication"]["outcome"] == "published":
        require("output" in snapshots, "actual committed output snapshot required")
        output = snapshots["output"]["meshes"][0]
        candidate = snapshots["candidate"]["meshes"][0]
        require((output["vertex_count"], output["face_count"], output["corner_count"]) ==
                (candidate["vertex_count"], candidate["face_count"], candidate["corner_count"]),
                "actual inspected candidate/output identity extents")
        for domain in ("vertices", "faces", "corners"):
            relation = next(row for row in report["maps"] if row["id"] == "export-" + domain)
            require(relation["kind"] == "identity" and relation["entries"] is None and
                    relation["from"]["snapshot"] == "candidate" and relation["to"]["snapshot"] == "output",
                    "actual candidate-to-output identity relationship")
    else:
        require("output" not in snapshots and not report["publication"]["artifacts"] and
                report["publication"]["snapshot"] is None and not report["publication"]["verified"],
                "unpublished report must not expose core output/artifacts")


def validate_case(root, name, model, schema_validator):
    expected = READBACK_OUTCOMES if name == READBACK_CASE else CASES[name]
    execution, stage, candidate, exit_code, publication, changed = expected
    case = root / name
    report_path = case / "report.json"
    require(report_path.is_file(), name + ": genuine admitted report required")
    raw = report_path.read_bytes()
    report = model.load(raw.decode("utf-8"))
    schema_validator.validate(report)
    require((report["execution"]["outcome"], report["execution"]["stage"],
             report["candidate"]["outcome"], model.exit_code(report),
             report["publication"]["outcome"]) ==
            (execution, stage, candidate, exit_code, publication), name + ": literal outcome/stage mismatch")
    require(model.profile_outcome(report) == report["profile"]["outcome"], "coverage-derived profile")
    requested = publication != "not-requested"
    require(report["publication"]["requested"] == requested, "requested export is independent from candidate")
    profile = (report["profile"]["name"], report["profile"]["version"])
    require(profile == (FILE_PROFILE if requested else MEMORY_PROFILE), "frozen owning profile identity")
    require(set(report["profile"]["required_checks"]) == (FILE_CHECKS if requested else MEMORY_CHECKS),
            "exact required checks; no required solid predicate")
    if publication in ("published", "not-requested") and execution == "completed":
        require(report["profile"]["outcome"] == "passed", "complete successful preservation profile")
        for row in report["coverage"]:
            if row["id"] in report["profile"]["required_checks"]:
                require(row["status"] == "performed" and row["finding"] == "passed", "real required predicate passed")
    solid = next(row for row in report["coverage"] if row["check"] == "solid-validity")
    require(solid["status"] == "unsupported" and solid["finding"] is None, "no manufactured solid check")
    check_meshes(report, changed)
    source_digest, framed_work, source_format = source_hash(case / "source")
    require(members(case / "source") == ["mesh.bin", "notes/original.bin", ENTRY, "texture.bin"],
            "literal full retained physical source inventory")
    require((case / "source/mesh.bin").read_bytes()[-8:] == bytes.fromhex("deadbeefcafebabe"), "unused buffer tail")
    require((case / "source/texture.bin").read_bytes() == bytes.fromhex("89504e470d0a1a0a") + bytes([0x5A]) * (4096 - 8), "full opaque image input")
    require((case / "source/notes/original.bin").read_bytes() == bytes([0xA7]) * 65536, "full explicit opaque input")
    extension = report["extensions"].get(FILE_EXTENSION)
    if requested:
        require(extension is not None and extension["version"] == "1", "owning file extension version")
        require(extension["source_format"] == source_format, "actual magic-derived source format")
        require(extension["memory_requested_bytes"]["read"] == 0 and
                extension["retained_source_requested_bytes"]["files"] > 0 and
                extension["retained_source_requested_bytes"]["capped_reader"] > 0,
                "retained/fresh owner scopes remain distinct")
        require(extension["retained_source_requested_bytes"]["container_counter"] ==
                ("unavailable" if source_format == "glb" else "not-applicable"), "no invented container count")
        require("capture" not in extension and "byte_verification" not in extension,
                "final publisher stats must not patch verified receipt")
        if extension["input_hash_convention"] is not None:
            require(extension["input_hash_convention"] == CONVENTION and report["input"]["sha256"] == source_digest,
                    "independent streamed source framing/hash")
    if name == READBACK_CASE:
        require(report["profile"]["outcome"] == "failed", "failed physical predicate dominates profile")
        coverage = {row["id"]: row for row in report["coverage"]}
        require((coverage["staged-physical-members"]["status"],
                 coverage["staged-physical-members"]["finding"]) == ("performed", "failed"),
                "actual final physical reverify failure is required performed/failed coverage")
        require((coverage["staged-selected-primitive"]["status"],
                 coverage["staged-selected-primitive"]["finding"]) == ("performed", "passed"),
                "real staged primitive inspection passed before subsequent physical-byte failure")
        require(extension["publisher_diagnostic"] == READBACK_PUBLISHER,
                "exact actual physical mismatch diagnostic and earliest byte offset")
        require(extension["cleanup_ordinal"] == 1 and extension["cleanup_error"] is None,
                "real staged cleanup removed, without cleanup error")
        require(extension["input_hash_convention"] == CONVENTION,
                "actual prepared source hash survives distinct admitted failure report")
        initial_paths = ["mesh.bin", "notes/original.bin", STATIC, ENTRY, "texture.bin"]
        require(extension["artifact_paths"] == [{"id": "artifact-" + str(i), "path": path}
                                              for i, path in enumerate(initial_paths)],
                "actual initial staged path observations exclude the conditional receipt self")
    if publication == "published":
        require((case / "bundle" / RECEIPT).read_bytes() == raw, "exact returned/committed success receipt bytes")
        names = members(case / "bundle")
        require(names == ["mesh.bin", "notes/original.bin", RECEIPT, STATIC, ENTRY, "texture.bin"],
                "actual complete committed bundle inventory")
        require((case / "bundle" / STATIC).read_bytes() == STATIC_BYTES, "literal static supplement")
        initial = [path for path in names if path != RECEIPT]
        expected_paths = [{"id": "artifact-" + str(i), "path": path} for i, path in enumerate(initial)]
        require(extension["artifact_paths"] == expected_paths, "literal sorted artifact path mapping excludes self")
        expected_artifacts = [{"id": row["id"], "sha256": file_hash(case / "bundle" / row["path"])}
                              for row in expected_paths]
        require(report["publication"]["artifacts"] == expected_artifacts, "streamed complete actual artifact digests")
        require(extension["input_hash_convention"] == CONVENTION, "published report contains actual source hash")
        for path in ("mesh.bin", "notes/original.bin", "texture.bin"):
            require(file_hash(case / "source" / path) == file_hash(case / "bundle" / path), "full source/resource protection")
        if not changed:
            require((case / "source" / ENTRY).read_bytes() == (case / "bundle" / ENTRY).read_bytes(),
                    "no-op entry preserves every original JSON byte")
        framed_work += sum((case / "bundle" / path).stat().st_size for path in initial)
    else:
        require(not (case / "bundle").exists(), "failed/no-export case has no fabricated exported tree")
    return {"case": name, "report_sha256": hashlib.sha256(raw).hexdigest(),
            "source_sha256": source_digest, "source_framed_and_artifact_bytes": framed_work}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", type=Path)
    parser.add_argument("--geometry-python", type=Path, required=True)
    parser.add_argument("--readback-failure", action="store_true",
                        help="validate the separate actual prepared-receipt physical readback failure")
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.geometry_python.resolve()))
    import meshvale_reports
    from jsonschema import Draft202012Validator
    manifest = json.loads((args.reports / "cases.json").read_text(encoding="utf-8"))
    require(manifest["version"] == 1 and sorted(manifest["cases"]) == ([READBACK_CASE] if args.readback_failure else sorted(CASES)), "exact actual report fixture inventory")
    validator = Draft202012Validator(meshvale_reports.schema_document())
    rows = [validate_case(args.reports, name, meshvale_reports, validator) for name in manifest["cases"]]
    receipt = {"scope": "Independent report/schema/reference/exit/physical SHA verification only",
               "validator_sha256": file_hash(Path(meshvale_reports.__file__)),
               "oracle": "Python hashlib streamed full physical bytes, little-endian framing",
               "reports": rows}
    if args.receipt:
        args.receipt.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"Independently verified {len(rows)} actual file-workflow reports and reopened physical hash inventories")


if __name__ == "__main__":
    main()
