# SPDX-License-Identifier: Apache-2.0
"""Install exact producer/consumer wheels and test outside the source directory."""
import argparse
import hashlib
import json
import shutil
import sysconfig
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import venv


def run(*args,cwd,env=None):
    subprocess.run([str(part) for part in args],cwd=cwd,env=env,check=True)


def off_glb_suite(python, source, count, directory):
    # A missing test directory, unsupported producer, or skipped drive test must
    # fail this qualified Windows candidate rather than produce a green gate.
    code = """import pathlib, sys, unittest
import meshvale_geometry, meshvale_repair, meshvale_reports
assert all(pathlib.Path(module.__file__).resolve().is_relative_to(pathlib.Path(sys.prefix).resolve())
           for module in (meshvale_geometry, meshvale_repair, meshvale_reports))
suite = unittest.defaultTestLoader.discover(sys.argv[1])
expected = int(sys.argv[2])
assert suite.countTestCases() == expected, suite.countTestCases()
result = unittest.TextTestRunner(verbosity=2).run(suite)
assert result.testsRun == expected and not result.skipped, (result.testsRun, result.skipped)
raise SystemExit(0 if result.wasSuccessful() else 1)
"""
    run(python, "-I", "-c", code, source, count, cwd=directory)



def ply_journal_counts(events):
    attempts = sum(event["route"] in ("api", "api-control", "module-batch-job", "console-batch-job",
                   "injected-byte-read-fault") or
                   (event["route"] in ("module", "console") and
                    event["id"].startswith(event["route"] + "-single-")) or
                   (event["route"] == "scheduler-job" and event["observed"] == "attempted")
                   for event in events)
    capture_only = sum(event["route"] == "scheduler-job" and event.get("observed") == "attempted"
                       and event["id"].startswith("missing-") for event in events)
    return {"events": len(events), "native_attempts": attempts,
            "workflow_attempts": attempts - capture_only, "capture_only_attempts": capture_only}


def ply_installed(options, root):
    """Retain this qualified integration in a fresh owned directory, outside source."""
    output = options.ply_output_root.resolve()
    fixture = options.ply_fixture_root.resolve(strict=True)
    if (sys.platform != "win32" or sys.version_info[:2] != (3, 13) or
            sys.implementation.name != "cpython" or sysconfig.get_config_var("Py_GIL_DISABLED")):
        raise ValueError("PLY qualification currently selects Windows ordinary CPython3.13 only")
    if (output.exists() or output.is_symlink() or not output.parent.is_dir() or
            output.is_relative_to(root) or output.is_relative_to(fixture) or fixture.is_relative_to(output)):
        raise ValueError("PLY output requires a new directory outside checkout and native fixtures")
    output.mkdir()
    inputs = output / "inputs"
    inputs.mkdir()
    copied = {}
    for relative in ("tests/ply_workflow_python.py", "docs/ply-presets.json",
                     "tests/fixtures/ply-workflow/literal-predicates.json"):
        destination = inputs / Path(relative).name
        shutil.copyfile(root / relative, destination)
        copied[relative] = hashlib.sha256(destination.read_bytes()).hexdigest()
    environment = output / "env"
    venv.EnvBuilder(with_pip=True).create(environment)
    python = environment / "Scripts/python.exe"
    console = environment / "Scripts/meshvale-repair.exe"
    run(python, "-m", "pip", "install", "--no-index", options.geometry_wheel.resolve(),
        options.repair_wheel.resolve(), cwd=output)
    run(python, "-m", "pip", "install", str(options.repair_wheel.resolve()) + "[workflow]", cwd=output)
    # This is an installed ownership capsule, not a clean-machine runtime-floor test.
    capsule = """import importlib.metadata as m, importlib.util, json, pathlib, struct, sys, sysconfig
import meshvale_geometry, meshvale_repair, meshvale_reports
assert sys.implementation.name == 'cpython' and sys.version_info[:2] == (3,13)
assert struct.calcsize('P') == 8 and not sysconfig.get_config_var('Py_GIL_DISABLED')
assert not hasattr(sys, '_is_gil_enabled') or sys._is_gil_enabled()
assert m.version('meshvale-geometry') == '0.0.1.dev134+g48298b24b'
assert m.version('jsonschema') == '4.26.0'
assert importlib.util.find_spec('meshvale_interchange') is None
origins = {x.__name__: x.__file__ for x in (meshvale_geometry,meshvale_repair,meshvale_reports)}
assert all(pathlib.Path(x).resolve().is_relative_to(pathlib.Path(sys.prefix).resolve()) for x in origins.values())
packages = {}
for d in m.distributions():
    files = {}
    for item in d.files or ():
        if '.dist-info/' in item.as_posix() and pathlib.Path(item).name in ('METADATA','WHEEL','RECORD','direct_url.json'):
            files[str(item)] = d.locate_file(item).read_text(encoding='utf8')
    packages[d.metadata['Name']] = {'version':d.version,'metadata_files':files}
pathlib.Path(sys.argv[1]).write_text(json.dumps({'executable':sys.executable,'prefix':sys.prefix,'base_prefix':sys.base_prefix,'version':sys.version,'ordinary_gil':True,'origins':origins,'packages':packages},indent=2)+chr(10),encoding='utf8')
"""
    run(python, "-I", "-c", capsule, output / "runtime.json", cwd=output)
    shared = """import hashlib, importlib.metadata as m, json, pathlib, sys
import meshvale_reports
from collections import Counter
from jsonschema import Draft202012Validator
assert m.version('meshvale-geometry') == '0.0.1.dev134+g48298b24b'
validator=Draft202012Validator(meshvale_reports.schema_document())
rows=[]
for case in json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf8'))['cases']:
    if case['report_file'] is None: continue
    raw=pathlib.Path(case['report_file']).read_bytes()
    report=meshvale_reports.load(raw.decode('utf8'))
    validator.validate(report)
    code=meshvale_reports.exit_code(report)
    assert code == case['expected']['exit']
    rows.append({'id':case['id'],'path':case['report_file'],'sha256':hashlib.sha256(raw).hexdigest(),'exit_code':code})
assert len(rows)==31 and Counter(x['exit_code'] for x in rows)=={0:29,1:1,2:1}
pathlib.Path(sys.argv[2]).write_text(json.dumps({'passed':True,'validator_version':m.version('meshvale-geometry'),'validator_sha256':hashlib.sha256(pathlib.Path(meshvale_reports.__file__).read_bytes()).hexdigest(),'reports':rows},indent=2)+chr(10),encoding='utf8')
"""
    run(python, "-I", "-c", shared, options.ply_cases.resolve(), output / "shared-reports.json", cwd=output)
    run(python, "-I", inputs / "ply_workflow_python.py", "--fixture-root", fixture,
        "--output-root", output / "results", "--literal-oracles", inputs / "literal-predicates.json",
        "--presets", inputs / "ply-presets.json", "--native-version", options.ply_native_version,
        "--console", console, cwd=output)
    summary = json.loads((output / "results/integration-results.json").read_text(encoding="utf8"))
    counts = ply_journal_counts(summary["events"])
    bundles = list((output / "results").rglob("meshvale-report.json"))
    if (summary["passed"] is not True or summary["inputs_unchanged"] is not True or
            counts != dict(events=109, native_attempts=102, workflow_attempts=100, capture_only_attempts=2) or
            len(bundles) != 97):
        raise RuntimeError("Installed PLY journal/bundle inventory differs from the qualified test")
    run(python, "-m", "pip", "check", cwd=output)
    (output / "qualification.json").write_text(json.dumps({
        "passed": True, "scope": "Windows ordinary CPython3.13 development PLY integration",
        "journal_counts": counts, "bundles": len(bundles), "input_sha256": copied,
        "counts_source": "complete passing integration journal; native attempts include capture-only failures",
        "runtime_floor_qualified": False, "interchange_python_installed": False}, indent=2) + "\n", encoding="utf8")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry-wheel",required=True,type=Path)
    parser.add_argument("--repair-wheel",required=True,type=Path)
    parser.add_argument("--workflow",action="store_true",help="Install the declared validator extra and test reported operations")
    parser.add_argument("--gltf", choices=["qualified", "unsupported"],
                        help="Exercise the installed glTF API with an explicit numerical profile")
    parser.add_argument("--off", choices=["qualified", "unsupported"],
                        help="Exercise the installed OFF API and commands with an explicit numerical profile")
    parser.add_argument("--off-glb", action="store_true",
                        help="Exercise all 15 API and 10 command tests on Windows ordinary x64 CPython 3.13")
    parser.add_argument("--interchange-wheel",type=Path,help="Also install and exercise the optional asset workflow")
    parser.add_argument("--ply", action="store_true", help="Run the qualified Windows installed PLY integration")
    parser.add_argument("--ply-fixture-root", type=Path)
    parser.add_argument("--ply-output-root", type=Path, help="New retained directory outside the checkout")
    parser.add_argument("--ply-cases", type=Path, help="Actual public make-cases manifest for the same native run")
    parser.add_argument("--ply-native-version", help="Actual producing native CMake version, not wheel version")
    options = parser.parse_args(); root = Path(__file__).resolve().parents[1]
    if options.off_glb and (options.off != "qualified" or options.ply or
            sys.platform != "win32" or sys.version_info[:2] != (3, 13) or
            sys.implementation.name != "cpython" or sysconfig.get_config_var("Py_GIL_DISABLED") or
            sys.maxsize <= 2**32 or (hasattr(sys, "_is_gil_enabled") and not sys._is_gil_enabled())):
        parser.error("--off-glb requires --off qualified and Windows ordinary x64 CPython 3.13, without PLY mode")
    ply_inputs = (options.ply_fixture_root, options.ply_output_root, options.ply_native_version, options.ply_cases)
    if options.ply:
        if not all(ply_inputs) or options.workflow or options.gltf or options.off or options.interchange_wheel:
            parser.error("PLY requires its four explicit inputs, without another installed mode")
        raise SystemExit(ply_installed(options, root))
    if any(ply_inputs):
        parser.error("PLY inputs require --ply")
    with tempfile.TemporaryDirectory(prefix="meshvale-repair-installed-") as scratch:
        directory = Path(scratch); environment = directory/"env"
        venv.EnvBuilder(with_pip=True).create(environment)
        python = environment/("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
        run(python,"-m","pip","install","--no-index",options.geometry_wheel.resolve(),options.repair_wheel.resolve(),cwd=directory)
        run(python,"-I","-m","unittest","discover","-s",root/"tests/python","-v",cwd=directory)
        run(python,"-I",root/"examples/python/remove_duplicate.py",cwd=directory)
        run(python,"-I","-m","meshvale_repair","--help",cwd=directory)
        run(python,"-I","-m","meshvale_repair","--version",cwd=directory)
        console = environment/("Scripts/meshvale-repair.exe" if sys.platform == "win32" else "bin/meshvale-repair")
        run(console,"--help",cwd=directory)
        for order in ["geometry-first","repair-first"]:
            code = ("import meshvale_geometry; import meshvale_repair" if order == "geometry-first" else
                    "import meshvale_repair; import meshvale_geometry")
            code += "; assert meshvale_repair.Mesh is meshvale_geometry.Mesh; assert meshvale_repair.remove_duplicate_faces(meshvale_geometry.Mesh(), []).outcome == 'unchanged'; print('Installed import order passed: "+order+"')"
            run(python,"-I","-c",code,cwd=directory)
        if options.workflow or options.gltf or options.off:
            run(python,"-m","pip","install",str(options.repair_wheel.resolve())+"[workflow]",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/workflow","-v",cwd=directory)
            run(python,"-I",root/"examples/python/reported_duplicate.py",cwd=directory)
        if options.gltf:
            gltf_environment = os.environ.copy()
            gltf_environment["MESHVALE_GLTF_PYTHON_PROFILE"] = options.gltf
            run(python,"-I","-m","unittest","discover","-s",root/"tests/gltf-python","-v",
                cwd=directory,env=gltf_environment)
            profile_arguments = ["--unsupported-profile"] if options.gltf == "unsupported" else []
            run(python,"-I",root/"examples/python/gltf_walkthrough.py",*profile_arguments,
                cwd=directory,env=gltf_environment)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/gltf-cli","-v",
                cwd=directory,env=gltf_environment)
            run(python,"-I",root/"examples/python/gltf_walkthrough.py","--commands",*profile_arguments,
                cwd=directory,env=gltf_environment)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/gltf-scene","-v",
                cwd=directory,env=gltf_environment)
            inspector = gltf_environment.get("MESHVALE_GLTF_SCENE_INSPECTOR")
            inspector_arguments = ["--scene-inspector", inspector] if inspector else []
            run(python,"-I",root/"examples/python/gltf_scene_walkthrough.py",*profile_arguments,*inspector_arguments,
                cwd=directory,env=gltf_environment)
        if options.off:
            off_environment = os.environ.copy()
            off_environment["MESHVALE_OFF_PYTHON_PROFILE"] = options.off
            run(python,"-I","-m","unittest","discover","-s",root/"tests/off-python","-v",
                cwd=directory,env=off_environment)
            profile_arguments = ["--unsupported-profile"] if options.off == "unsupported" else []
            run(python,"-I",root/"examples/python/off_walkthrough.py",*profile_arguments,
                cwd=directory,env=off_environment)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/off-cli","-v",
                cwd=directory,env=off_environment)
            run(python,"-I",root/"examples/python/off_walkthrough.py","--commands",*profile_arguments,
                cwd=directory,env=off_environment)
        if options.off_glb:
            off_glb_suite(python, root / "tests/off-glb-python", 15, directory)
            off_glb_suite(python, root / "tests/off-glb-cli", 10, directory)
        if options.interchange_wheel:
            run(python,"-m","pip","install","--no-index",options.interchange_wheel.resolve(),cwd=directory)
            run(python,"-m","pip","install",str(options.repair_wheel.resolve())+"[assets]",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/assets","-v",cwd=directory)
            run(python,"-I",root/"examples/python/repair_obj_bundle.py",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/cli","-v",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/obj-corpus","-v",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/obj-export","-v",cwd=directory)
            run(python,"-I",root/"examples/python/obj_triangle_export.py",directory/"triangle-walkthrough","--commands",cwd=directory)
            run(python,"-I",root/"examples/python/obj_commands.py",cwd=directory)
            import itertools
            for order in itertools.permutations(("meshvale_geometry", "meshvale_interchange", "meshvale_repair")):
                code = "; ".join("import " + name for name in order)
                code += "; from meshvale_repair.obj_workflow import repair_obj_file; assert meshvale_interchange.Mesh is meshvale_geometry.Mesh"
                code += "; import runpy; runpy.run_path(" + repr(str(root/"examples/python/repair_obj_bundle.py")) + ", run_name='__main__')"
                code += "; print('Installed three-product import order passed: " + ",".join(order) + "')"
                run(python,"-I","-c",code,cwd=directory)
        run(python,"-m","pip","check",cwd=directory)
