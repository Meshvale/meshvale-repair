# SPDX-License-Identifier: Apache-2.0
"""Build and verify the bounded CPython 3.13 OBJ development cohort."""
import argparse
from email.parser import Parser
import hashlib
import importlib.util
from importlib.metadata import version
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import zipfile

GEOMETRY_REVISION = "ec38fabd783a578947d43856b0c7a7fd54ce8533"
GEOMETRY_VERSION = "0.0.1.dev69+gec38fabd7"
INTERCHANGE_REVISION = "8d5823272b576052553608dbff26685ba56b4dea"
INTERCHANGE_VERSION = "0.0.1.dev62+g8d5823272"
PACKAGE = Path(__file__).resolve().parents[1]


def run(*args, cwd=None):
    subprocess.run([str(arg) for arg in args], check=True, cwd=cwd)


def dependency_root():
    return Path(os.environ["MESHVALE_DEPS"]).resolve()


def interchange():
    path = dependency_root() / "interchange/scripts/portable-preview.py"
    specification = importlib.util.spec_from_file_location("interchange_preview", path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    assert module.GEOMETRY_REVISION == GEOMETRY_REVISION
    assert module.GEOMETRY_VERSION == GEOMETRY_VERSION
    return module


def prepare():
    root = dependency_root()
    root.mkdir(parents=True, exist_ok=True)
    source = root / "interchange"
    if not source.exists():
        run("git", "clone", "--filter=blob:none", "--no-checkout",
            "https://github.com/Meshvale/meshvale-interchange.git", source)
    run("git", "checkout", "--detach", INTERCHANGE_REVISION, cwd=source)
    assert subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip() == INTERCHANGE_REVISION
    assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=source)
    producer = interchange()
    producer.prepare()
    assert (root / "geometry/NOTICE").read_text().strip() == (PACKAGE / "licenses/meshvale-geometry-notice.txt").read_text().strip()
    for notice in ["eigen-mpl2.txt", "eigen-apache.txt", "eigen-notices.txt"]:
        installed = root / "prefix/share/MeshvaleGeometry/licenses" / notice
        assert installed.read_text() == (PACKAGE / "licenses" / notice).read_text(), notice
    run(sys.executable, "-m", "pip", "wheel", source, "--no-deps", "--verbose",
        "--config-settings=cmake.version===4.3.1", "--wheel-dir", root / "interchange-raw")
    wheel, = (root / "interchange-raw").glob("meshvale_interchange-*.whl")
    assert wheel.name.startswith("meshvale_interchange-" + INTERCHANGE_VERSION + "-cp313-cp313-"), wheel.name
    output = Path(os.environ["MESHVALE_PREVIEW_DIR"])
    producer.repair(wheel, root / "interchange-wheels", output / "interchange-dependencies.txt")
    for path in (root / "interchange-wheels").glob("*.whl"):
        run(sys.executable, source / "scripts/check-package.py", path)
        shutil.copy2(path, output / "upstream" / path.name)
    (output / "cohort-inputs.json").write_text(json.dumps({
        "geometry_revision": GEOMETRY_REVISION, "geometry_version": GEOMETRY_VERSION,
        "geometry_native_version": "0.0.0", "interchange_revision": INTERCHANGE_REVISION,
        "interchange_version": INTERCHANGE_VERSION}, indent=2) + "\n", encoding="utf-8")


def before_test():
    interchange().before_test()
    run(sys.executable, "-m", "pip", "install", "--no-index", "--find-links",
        dependency_root() / "interchange-wheels", "meshvale-interchange==" + INTERCHANGE_VERSION)


def test():
    run(sys.executable, "-m", "pip", "install", "--no-index", "--find-links", dependency_root() / "wheels",
        "--find-links", dependency_root() / "interchange-wheels", "meshvale-repair[assets]==" + version("meshvale-repair"))
    producer = interchange()
    producer.test()
    for directory in ["tests/python", "tests/workflow", "tests/assets", "tests/cli"]:
        run(sys.executable, "-I", "-m", "unittest", "discover", "-s", PACKAGE / directory, "-v")
    for example in ["remove_duplicate.py", "reported_duplicate.py", "repair_obj_bundle.py", "obj_commands.py"]:
        run(sys.executable, "-I", PACKAGE / "examples/python" / example)
    products = ("meshvale_geometry", "meshvale_interchange", "meshvale_repair")
    for order in itertools.permutations(products):
        code = "; ".join("import " + name for name in order)
        code += "; assert meshvale_repair.Mesh is meshvale_interchange.Mesh is meshvale_geometry.Mesh"
        code += "; assert meshvale_geometry.__version__ == " + repr(GEOMETRY_VERSION)
        code += "; assert meshvale_interchange.__version__ == " + repr(INTERCHANGE_VERSION)
        code += "; import runpy; runpy.run_path(" + repr(str(PACKAGE / "examples/python/repair_obj_bundle.py")) + ", run_name='__main__')"
        code += "; print('Installed three-product import order passed: " + ",".join(order) + "')"
        run(sys.executable, "-I", "-c", code)
    walkthrough = Path(os.environ["MESHVALE_PREVIEW_DIR"]) / "walkthrough"
    run(sys.executable, "-I", PACKAGE / "examples/python/obj_walkthrough.py", walkthrough)
    lazy = """import importlib.abc, sys
class BlockNative(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] in ('meshvale_geometry', 'meshvale_interchange', 'meshvale_repair'):
            raise AssertionError('Report reader imported native product: ' + fullname)
sys.meta_path.insert(0, BlockNative())
from meshvale_reports import load, validate
from pathlib import Path
report = load(Path(sys.argv[1]).read_text())
validate(report)
assert report['publication']['outcome'] == 'published'
assert not any(name.split('.')[0] in ('meshvale_geometry', 'meshvale_interchange', 'meshvale_repair') for name in sys.modules)
print('Installed reports-only reader passed with native product imports blocked')
"""
    run(sys.executable, "-I", "-c", lazy, walkthrough / "api-report.json")
    run(sys.executable, "-m", "pip", "check")


def manifest(output):
    root = dependency_root()
    artifacts, versions = [], set()
    owners = {"meshvale-geometry": root / "geometry", "meshvale-interchange": root / "interchange", "meshvale-repair": PACKAGE}
    for path in sorted(output.rglob("*")):
        if path.suffix != ".whl" and not path.name.endswith(".tar.gz"):
            continue
        item = {"path": path.relative_to(output).as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        if path.suffix == ".whl":
            with zipfile.ZipFile(path) as archive:
                names = archive.namelist()
                item["package_metadata"] = archive.read(next(name for name in names if name.endswith(".dist-info/METADATA"))).decode()
                metadata = Parser().parsestr(item["package_metadata"])
                owner = owners[metadata["Name"]]
                run(sys.executable, owner / "scripts/check-package.py", path)
                item["wheel_metadata"] = archive.read(next(name for name in names if name.endswith(".dist-info/WHEEL"))).decode()
                item["native_files"] = [name for name in names if name.endswith((".so", ".pyd", ".dll")) or ".so." in name]
                item["license_sha256"] = {}
                for name in names:
                    if ".dist-info/licenses/" not in name or name.endswith("/"):
                        continue
                    filename = Path(name).name
                    reference = owner / filename if filename in {"LICENSE", "NOTICE"} else owner / "licenses" / filename
                    content = archive.read(name)
                    assert content.decode().strip().replace("\r\n", "\n") == reference.read_text().strip(), name
                    item["license_sha256"][name] = hashlib.sha256(content).hexdigest()
            assert "-cp313-cp313-" in path.name, path.name
            assert "manylinux_2_28_x86_64" in path.name if sys.platform != "win32" else path.name.endswith("-win_amd64.whl"), path.name
            if metadata["Name"] == "meshvale-geometry":
                assert metadata["Version"] == GEOMETRY_VERSION
            elif metadata["Name"] == "meshvale-interchange":
                assert metadata["Version"] == INTERCHANGE_VERSION
                assert metadata.get_all("Requires-Dist") == ["meshvale-geometry==" + GEOMETRY_VERSION]
            else:
                versions.add(metadata["Version"])
                expected = ["meshvale-geometry==" + GEOMETRY_VERSION,
                    'meshvale-geometry[reports]==' + GEOMETRY_VERSION + '; extra == "workflow"',
                    'meshvale-geometry[reports]==' + GEOMETRY_VERSION + '; extra == "assets"',
                    'meshvale-interchange==' + INTERCHANGE_VERSION + '; extra == "assets"']
                assert metadata.get_all("Requires-Dist") == expected, metadata.get_all("Requires-Dist")
        else:
            run(sys.executable, PACKAGE / "scripts/check-package.py", path)
            with tarfile.open(path) as archive:
                info = next(member for member in archive.getmembers() if member.name.endswith("/PKG-INFO"))
                versions.add(Parser().parsestr(archive.extractfile(info).read().decode())["Version"])
        artifacts.append(item)
    assert len(artifacts) == 7 and len(versions) == 1, (artifacts, versions)
    data = {"candidate_only": True, "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        "package_version": next(iter(versions)), "geometry_revision": GEOMETRY_REVISION, "geometry_version": GEOMETRY_VERSION,
        "interchange_revision": INTERCHANGE_REVISION, "interchange_version": INTERCHANGE_VERSION,
        "cibuildwheel": "4.2.0", "selected_build": os.environ["CIBW_BUILD"], "artifacts": artifacts}
    (output / "manifest.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["prepare", "before-test", "test", "repair", "manifest"])
    parser.add_argument("--wheel", type=Path)
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--output", type=Path)
    options = parser.parse_args()
    if options.mode == "repair":
        interchange().repair(options.wheel, options.destination, Path(os.environ["MESHVALE_PREVIEW_DIR"]) / "repair-dependencies.txt")
    elif options.mode == "manifest":
        manifest(options.output)
    else:
        {"prepare": prepare, "before-test": before_test, "test": test}[options.mode]()
