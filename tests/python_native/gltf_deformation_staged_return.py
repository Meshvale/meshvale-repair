# SPDX-License-Identifier: Apache-2.0
"""Exercise real staged/partial returns in the isolated native test variant.

This manual qualification is separate from the normal installed-module lane.
It loads the selected pure Python receiver source in its own package namespace
and delegates each prepared request to the real test module exactly once.
"""
import argparse
from copy import deepcopy
import gc
import hashlib
import importlib
import importlib.metadata
import importlib.util
import json
from pathlib import Path
import sys
import types


PROFILE = "deformation-retained-v1"
PHASES = ("capture_read", "source_read", "source", "materialization", "writing",
          "reload_read", "reload", "verification", "publication_read",
          "publication_reload", "publication_verification")
DECLARATIONS = (("vertex", "gltf/0/0/base/7:5f4d4154524958"),
                ("vertex", "gltf/0/0/base/5:5f41e4b8ad"),
                ("vertex", "gltf/0/0/morph/0/7:5f4d4154524958"))


def selected_receiver(source, module):
    # The synthetic namespace identifies this as a test-variant receiver. No
    # normal meshvale_repair._repair module alias or installed-module claim.
    name = "_meshvale_staged_receiver"
    package = types.ModuleType(name)
    package.__path__ = [str(source / "python/meshvale_repair")]
    package.__version__ = importlib.metadata.version("meshvale-repair")
    package._repair = module
    sys.modules[name] = package
    return importlib.import_module(name + ".gltf_commands")


def override_exception_control(module, output):
    session = module.GltfExecution()
    missing = output / "exception-source-never-opened.gltf"
    prepared = module._prepare_gltf_file(
        missing, None, [], primitive_ordinal=0, triangles="require-triangles",
        values="raw", execution=session,
    )
    module._staged_work_mode("weight-row-partitions")
    try:
        module._run_gltf_file(prepared)
    except ValueError as error:
        assert "owned deformation invocation" in str(error)
    else:
        raise AssertionError("test-only work override accepted a static invocation")
    stats = module._staged_work_stats()
    assert stats["override_consumed"] and session.active_workers == session.active_tracked_payload == 0
    assert not missing.exists()
    del prepared
    gc.collect()
    return {"scope": "Pre-native test-hook refusal, not an actual native partial return",
            "override_consumed": True, "native_capture_attempted": False,
            "next_normal_control": "json-file-report-clean-before"}


def execute(module, receiver, fixtures, output, name, glb, override=None,
            publish=False, clean=False):
    label = ("glb-" if glb else "json-") + name
    area = output / label
    area.mkdir()
    fixture_name = "stage-rich" if name.startswith("file-report") or clean else name
    wire_path = fixtures / (fixture_name + (".glb.hex" if glb else ".gltf"))
    wire = bytes.fromhex(wire_path.read_text()) if glb else wire_path.read_bytes()
    entry = area / ("source.glb" if glb else "source.gltf")
    entry.write_bytes(wire)
    destination = area / "published" if publish else None
    rich = fixture_name == "stage-rich"
    targets = [(0, 1)] if rich else []
    declarations = DECLARATIONS if rich else ()
    preset = "serial-deformation-v1" if fixture_name in ("node-partial", "payload-partial") else "balanced-deformation-v1"
    session = module.GltfExecution(preset=preset)
    prepared = module._prepare_gltf_file(
        entry, destination, targets, primitive_ordinal=0,
        triangles="require-triangles", values="raw-and-normalized",
        processing_profile=PROFILE, input_id=label,
        row_local_attributes=declarations, execution=session,
    )
    requested = {"id": label, "primitive_ordinal": 0,
                 "triangles": "require-triangles", "values": "raw-and-normalized",
                 "processing_profile": PROFILE, "targets": targets,
                 "row_local_attributes": declarations}
    calls = 0
    actual = []
    original = receiver._workflow._run_gltf_request

    def delegate(value):
        nonlocal calls
        calls += 1
        assert value is prepared
        if override:
            module._staged_work_mode(override)
        returned = module._run_gltf_file(value)
        actual.append(returned)
        return returned

    receiver._workflow._run_gltf_request = delegate
    try:
        payload, identity, code = receiver._attempt(types.SimpleNamespace(
            prepared=prepared, request=requested))
    finally:
        receiver._workflow._run_gltf_request = original
        module._observer_restore()
    assert calls == 1 and len(actual) == 1 and actual[0] is prepared
    result = actual.pop()
    facts = result.deformation_facts
    options = result.deformation_options
    stats = module._staged_work_stats()
    (area / "raw-native-return.json").write_text(json.dumps({"status": result.status.name if result.status is not None else None, "facts": facts, "options": options, "test_work": stats}, indent=2) + "\n", encoding="utf-8")
    assert stats["override_consumed"]
    assert len(facts) == 15 and facts["profile"] == PROFILE
    for phase in PHASES:
        if facts[phase] is not None:
            assert len(facts[phase]) == 14
            assert facts[phase]["execution"]["launched_workers"] == facts[phase]["execution"]["joined_workers"]
    assert facts["source_read"] is None
    report = json.loads(payload)
    assert identity["id"] == label
    if result.report_json is not None:
        assert payload == result.report_json
        assert report["extensions"]["meshvale.repair.gltf"]["processing"] == facts
        origin = "exact-native-bytes"
    else:
        extension = report["extensions"]["meshvale.repair.gltf.commands"]
        assert extension["version"] == "3"
        assert extension["processing"] == facts
        assert all(report["operation"]["options"][key] == value
                   for key, value in options.items())
        assert report["profile"]["outcome"] == "incomplete"
        origin = "actual-return-command-v3-fallback"
    if fixture_name == "node-partial":
        observation = facts["capture_read"]
        assert not result.workflow_started and facts["source"] is None
        q = observation["quaternions"]
        assert len(q) == 7 and len(q["first_refusal"]) == 9
        assert not q["complete"] and q["node_values_checked"] == 2 and q["rows_admitted"] == 1
        assert q["first_refusal"]["node"] == 1 and q["first_refusal"]["reason"] == "nonunit"
    elif fixture_name == "payload-partial":
        observation = facts["source"]
        q = observation["quaternions"]
        assert len(q) == 7 and len(q["first_refusal"]) == 9
        assert not q["complete"] and q["animation_values_checked"] == 2 and q["rows_admitted"] == 1
        assert (q["first_refusal"]["accessor"], q["first_refusal"]["row"]) == (3, 1)
        assert facts["materialization"] is None
    elif fixture_name == "weight-partial":
        observation = facts["source"]
        sums = observation["weight_sums"]
        assert len(sums) == 8 and len(sums["first_evaluated"]) == len(sums["first_advisory"]) == 10
        assert not sums["complete"]
        assert (sums["floating_rows_expected"], sums["rows_evaluated"], sums["all_zero_rows"], sums["advisory_rows"]) == (3, 2, 1, 2)
        assert (sums["first_evaluated"]["vertex"], sums["first_advisory"]["vertex"]) == (0, 0)
        assert sums["first_advisory"]["sum_bits"] == 0
        assert observation["location"]["vertex"] == 1
        assert stats["weight_work_applied"]
        assert options["deformation_work"]["source"] == observation["work"]
        assert observation["work"]["partition_scalars"] == 1 and observation["work"]["max_jobs"] == 24
        assert observation["execution"]["planned_jobs"] == 48
        assert observation["execution"]["launched_workers"] > 0
        assert facts["materialization"] is None
    elif override == "file-report-worker":
        for phase in ("publication_read", "publication_reload", "publication_verification"):
            assert facts[phase] is not None and facts[phase]["complete"]
        assert stats["file_report_work_applied"] and stats["staged_seen"] and stats["entered_after_staged"]
        assert stats["launched"] > 0 and stats["entered"] == stats["launched"] == stats["joined"]
        assert stats["stop_accepted"]
        assert result.status == module.GltfWorkflowStatus.kCanceled
        assert result.diagnostic.stage == module.GltfFileWorkflowStage.kReport
        assert result.report_json is None and result.face_map is not None
        assert not destination.exists()
    else:
        assert clean and result.status == module.GltfWorkflowStatus.kSuccess
        assert not stats["weight_work_applied"] and not stats["file_report_work_applied"]
        assert (destination / result.published_report_path).read_bytes() == payload
        assert code == 0
    assert entry.read_bytes() == wire and session.active_workers == 0
    detached = deepcopy(facts)
    facts["capture_read"] = None
    assert result.deformation_facts == detached
    try:
        result.deformation_facts = {}
    except AttributeError:
        pass
    else:
        raise AssertionError("readonly native facts property accepted assignment")
    row = {"case": label, "variant_only": True, "calls": calls,
           "status": result.status.name if result.status is not None else None, "delivery": origin, "code": code,
           "facts": detached, "options": options, "test_work": stats,
           "payload_sha256": hashlib.sha256(payload).hexdigest()}
    (area / "returned-report.json").write_bytes(payload)
    del result, prepared
    gc.collect()
    assert session.active_workers == session.active_tracked_payload == 0
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(exist_ok=False)
    spec = importlib.util.spec_from_file_location("_repair_gltf_return_test", args.module)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    receiver = selected_receiver(args.source.resolve(), module)
    fixtures = args.source / "tests/fixtures/gltf-deformation-staged"
    exception_control = override_exception_control(module, args.output)
    rows = []
    for glb in (False, True):
        rows.append(execute(module, receiver, fixtures, args.output, "file-report-clean-before", glb, publish=True, clean=True))
        for name in ("node-partial", "payload-partial", "weight-partial"):
            rows.append(execute(module, receiver, fixtures, args.output, name, glb,
                                "weight-row-partitions" if name == "weight-partial" else None))
        rows.append(execute(module, receiver, fixtures, args.output, "file-report-stop", glb,
                            "file-report-worker", publish=True))
        # No override call: the next normal invocation must use its own preset.
        rows.append(execute(module, receiver, fixtures, args.output, "file-report-clean-after", glb, publish=True, clean=True))
    (args.output / "receipt.json").write_text(json.dumps({
        "scope": "Isolated test variant, real native once-only returns and selected pure Python receiver; no normal module qualification",
        "module": str(args.module.resolve()), "module_sha256": hashlib.sha256(args.module.read_bytes()).hexdigest(),
        "pre_native_override_exception": exception_control,
        "cases": rows}, indent=2) + "\n", encoding="utf-8")
    print("Staged/partial variant: 12 actual JSON/GLB native returns and exact native/v3 fallback deliveries PASS")


if __name__ == "__main__":
    main()
