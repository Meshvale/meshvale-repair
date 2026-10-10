# SPDX-License-Identifier: Apache-2.0
"""Inspect Repair wheel/source archives against the product publication list."""
import argparse
from pathlib import Path, PurePosixPath
import tarfile
import zipfile


def inspect(path):
    if path.suffix == ".whl":
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
        for name in names:
            parts = PurePosixPath(name).parts
            assert not PurePosixPath(name).is_absolute() and ".." not in parts, name
            assert parts[0] == "meshvale_repair" or parts[0].endswith(".dist-info"), name
            if parts[0] == "meshvale_repair":
                assert len(parts) == 2 and (parts[1] in {"__init__.py","_version.py","workflow.py"} or
                    (parts[1].startswith("_repair.") and parts[1].endswith((".pyd",".so")))), name
        for license in ["LICENSE","NOTICE","nanobind.txt","robin-map.txt","meshvale-geometry-notice.txt"]:
            assert any(name.endswith("/"+license) and ".dist-info/licenses/" in name for name in names), license
    else:
        with tarfile.open(path,"r:gz") as archive:
            members = archive.getmembers()
        names = []
        roots = {"CMakeLists.txt","pyproject.toml","README.md","AGENTS.md","ENVIRONMENT.md",
                 "environment.example.json","LICENSE","NOTICE","THIRD_PARTY.md","CHANGELOG.md","PKG-INFO"}
        directories = {"cmake","include","src","python","docs","examples","tests","licenses","scripts"}
        for member in members:
            assert member.isfile(),member.name
            parts = PurePosixPath(member.name).parts
            assert not PurePosixPath(member.name).is_absolute() and ".." not in parts and len(parts)>=2,member.name
            name = "/".join(parts[1:]); names.append(name)
            assert name in roots or parts[1] in directories,name
            assert not any(part in {".local",".scratch","__pycache__","references","build",".github"} for part in parts),name
            assert not name.endswith((".pyc",".pyd",".so",".obj",".log")),name
        for required in ["python/meshvale_repair/_version.py","python/bindings.cpp","tests/python/test_duplicates.py","src/duplicates.cpp",
                         "python/meshvale_repair/workflow.py","tests/workflow/test_workflow.py","docs/workflow.md","examples/python/reported_duplicate.py"]:
            assert required in names,required
    print(f"Package content check passed: {path.name}; {len(names)} files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives",nargs="+",type=Path)
    for path in parser.parse_args().archives: inspect(path)
