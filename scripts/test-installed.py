# SPDX-License-Identifier: Apache-2.0
"""Install exact producer/consumer wheels and test outside the source directory."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import venv


def run(*args,cwd):
    subprocess.run([str(part) for part in args],cwd=cwd,check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry-wheel",required=True,type=Path)
    parser.add_argument("--repair-wheel",required=True,type=Path)
    parser.add_argument("--workflow",action="store_true",help="Install the declared validator extra and test reported operations")
    parser.add_argument("--interchange-wheel",type=Path,help="Also install and exercise the optional asset workflow")
    options = parser.parse_args(); root = Path(__file__).resolve().parents[1]
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
        if options.workflow:
            run(python,"-m","pip","install",str(options.repair_wheel.resolve())+"[workflow]",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/workflow","-v",cwd=directory)
            run(python,"-I",root/"examples/python/reported_duplicate.py",cwd=directory)
        if options.interchange_wheel:
            run(python,"-m","pip","install","--no-index",options.interchange_wheel.resolve(),cwd=directory)
            run(python,"-m","pip","install",str(options.repair_wheel.resolve())+"[assets]",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/assets","-v",cwd=directory)
            run(python,"-I",root/"examples/python/repair_obj_bundle.py",cwd=directory)
            run(python,"-I","-m","unittest","discover","-s",root/"tests/cli","-v",cwd=directory)
            run(python,"-I",root/"examples/python/obj_commands.py",cwd=directory)
            import itertools
            for order in itertools.permutations(("meshvale_geometry", "meshvale_interchange", "meshvale_repair")):
                code = "; ".join("import " + name for name in order)
                code += "; from meshvale_repair.obj_workflow import repair_obj_file; assert meshvale_interchange.Mesh is meshvale_geometry.Mesh"
                code += "; import runpy; runpy.run_path(" + repr(str(root/"examples/python/repair_obj_bundle.py")) + ", run_name='__main__')"
                code += "; print('Installed three-product import order passed: " + ",".join(order) + "')"
                run(python,"-I","-c",code,cwd=directory)
        run(python,"-m","pip","check",cwd=directory)
