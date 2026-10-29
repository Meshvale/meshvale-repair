#!/usr/bin/env python3
"""Select an attested deformation test lane, never infer one from runtime success."""
from __future__ import annotations

import argparse
import importlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gltf_deformation_test_support import (
    SelectionError, UNSUPPORTED_STDOUT, artifact, capture, checked_schema,
    choose_domain, load, require, resolve, save, sha, validated_cohort,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    options = parser.parse_args()
    require(not options.result.exists(), "result must be a new path")
    result = {"schema": "meshvale.gltf-deformation-python-domain-result/1",
        "manifest_sha256": None, "origin": None, "domain": "selection-error",
        "outcome": "selection-error", "reason": "missing-manifest", "exit_code": 2,
        "counts": {key: 0 for key in ("positive_selected", "positive_run", "positive_passed", "positive_skipped",
            "sdk_numeric_control_attempts", "sdk_numeric_controls_credited", "range_controls_credited", "failures")},
        "loaded_module_sha256": None, "sdk_library_manifest_sha256": None, "artifacts": []}
    roots = None
    try:
        require(options.manifest.is_file(), "missing-manifest")
        result["manifest_sha256"] = sha(options.manifest)
        manifest = validated_cohort(options.manifest)
        result["origin"] = manifest["origin"]
        roots = manifest["roots"]
        require(options.result.resolve().is_relative_to(Path(roots["evidence"]).resolve()),
                "result must be inside the declared evidence root")
        # No asset work occurs before this derivation. The test outcome cannot
        # change the selected domain, including an unexpected eligible refusal.
        domain = choose_domain(manifest)
        normal = importlib.import_module("meshvale_repair._repair")
        actual = Path(normal.__file__).resolve(strict=True)
        expected = resolve(manifest["module"]["installed_member"], roots)
        require(actual == expected and normal.__name__ == "meshvale_repair._repair" and
                normal.__spec__.name == "meshvale_repair._repair" and
                Path(normal.__spec__.origin).resolve() == expected and actual.is_relative_to(Path(sys.prefix).resolve()) and
                sha(actual) == manifest["module"]["post_link"]["sha256"], "module-identity-mismatch")
        result["domain"] = domain
        result["loaded_module_sha256"] = sha(actual)
        result["sdk_library_manifest_sha256"] = manifest["sdk"]["installed_headers"]["sha256"]
        if domain == "positive":
            log = options.result.with_suffix(".unittest.txt")
            # Reserve before discovery or execution; another result suffix may
            # derive the same log path. Never truncate an earlier raw record.
            with log.open("x", encoding="utf-8") as stream:
                inventory = load(resolve(manifest["positive_inventory"], roots))
                directory = Path(roots["receiver_source"]) / "tests/gltf-deformation-python"
                sys.path.insert(0, str(directory))
                suite = unittest.defaultTestLoader.discover(str(directory), pattern="test_*.py")
                def methods(node):
                    for child in node:
                        if isinstance(child, unittest.TestSuite):
                            yield from methods(child)
                        else:
                            yield child.id()
                ids = list(methods(suite))
                require(len(ids) == 10 and len(set(ids)) == 10 and set(ids) ==
                        {row["method"] for row in inventory["methods"]}, "unexpected-test-inventory")
                result["counts"]["positive_selected"] = 10
                tested = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
            result["artifacts"].append(artifact(log, roots, "evidence"))
            def owning_id(test):
                return getattr(test, "test_case", test).id()
            failed_methods = {owning_id(test) for test, _ in tested.failures + tested.errors}
            skipped_methods = {owning_id(test) for test, _ in tested.skipped}
            result["counts"].update(positive_run=tested.testsRun,
                positive_passed=tested.testsRun - len(failed_methods | skipped_methods),
                positive_skipped=len(tested.skipped), failures=len(tested.failures) + len(tested.errors))
            passed = tested.wasSuccessful() and tested.testsRun == 10 and not tested.skipped
            result.update(outcome="passed" if passed else "execution-failure",
                          reason="strict-positive-complete" if passed else "unexpected-runtime-refusal", exit_code=0 if passed else 1)
        else:
            control = manifest["unsupported_control"]
            executable = resolve(control["executable"], roots)
            fixture = Path(roots["producer_source"]) / control["fixtures_relative"]
            result["counts"]["sdk_numeric_control_attempts"] = 1
            record, tested = capture([executable, fixture], executable.parent,
                                     options.result.with_suffix(".control"), roots,
                                     inputs=[executable])
            result["artifacts"].append(artifact(options.result.with_suffix(".control") / "process.json", roots))
            passed = tested.returncode == 0 and tested.stderr == b"" and \
                tested.stdout.decode("utf-8").replace("\r\n", "\n") == UNSUPPORTED_STDOUT
            result["counts"].update(sdk_numeric_controls_credited=int(passed),
                                    range_controls_credited=int(passed), failures=int(not passed))
            result.update(outcome="passed" if passed else "execution-failure",
                          reason="compiled-unsupported-observed" if passed else "unsupported-control-mismatch", exit_code=0 if passed else 1)
        # Repeat the pure deep validation, not domain selection or runtime work.
        # Referenced JSON can stay unchanged while its source/header members drift.
        require(sha(options.manifest) == result["manifest_sha256"] and
                validated_cohort(options.manifest) == manifest, "cohort changed during execution")
        require(sha(actual) == result["loaded_module_sha256"], "module-identity-mismatch")
    except (SelectionError, OSError, ValueError, KeyError, ImportError, TypeError, IndexError) as error:
        print(str(error), file=sys.stderr)
        known = {"missing-manifest", "invalid-generator", "unprobed-guard", "supported-but-not-qualified",
                 "module-identity-mismatch", "unexpected-test-inventory"}
        reason = str(error) if str(error) in known else "unknown-or-mismatched-cohort"
        if result["domain"] != "selection-error":
            result.update(outcome="execution-failure", reason="execution-error", exit_code=1)
            result["counts"]["failures"] += 1
        else:
            result.update(reason=reason)
    checked_schema(result, "result")
    save(options.result, result)
    print(result["domain"] + ": " + result["outcome"] + " (" + result["reason"] + ")")
    return result["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
