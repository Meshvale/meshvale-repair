# SPDX-License-Identifier: Apache-2.0
"""Inspect Repair wheel/source archives against the product publication list."""
import argparse
from pathlib import Path, PurePosixPath
import tarfile
import zipfile


def inspect(path):
    if path.suffix == ".whl":
        with zipfile.ZipFile(path) as archive:
            members = archive.infolist()
            names = [member.filename for member in members if not member.is_dir()]
            notice_contents = {name: archive.read(name) for name in names if ".dist-info/licenses/" in name}
        for member in members:
            name = member.filename
            parts = PurePosixPath(name).parts
            assert not PurePosixPath(name).is_absolute() and ".." not in parts, name
            assert parts[0] == "meshvale_repair" or parts[0].endswith(".dist-info"), name
            if member.is_dir():
                # Repair tools emit structural directories within existing namespaces.
                assert len(parts) == 1 or parts[0].endswith(".dist-info"), name
                continue
            if parts[0] == "meshvale_repair":
                assert len(parts) == 2 and (parts[1] in {"__init__.py","__main__.py","_version.py","workflow.py","obj_workflow.py","commands.py"} or
                    (parts[1].startswith("_repair.") and parts[1].endswith((".pyd",".so")))), name
            else:
                assert parts[1:] in [("METADATA",), ("WHEEL",), ("RECORD",), ("entry_points.txt",),
                    ("licenses", "LICENSE"), ("licenses", "NOTICE"),
                    ("licenses", "licenses", "nanobind.txt"),
                    ("licenses", "licenses", "robin-map.txt"),
                    ("licenses", "licenses", "eigen-mpl2.txt"),
                    ("licenses", "licenses", "eigen-apache.txt"),
                    ("licenses", "licenses", "eigen-notices.txt"),
                    ("licenses", "licenses", "meshvale-geometry-notice.txt")], name
        for license in ["eigen-mpl2.txt","eigen-apache.txt","eigen-notices.txt","LICENSE","NOTICE","nanobind.txt","robin-map.txt","meshvale-geometry-notice.txt"]:
            matching = [name for name in notice_contents if name.endswith("/" + license)]
            assert len(matching) == 1, (license, matching)
            reference = Path(__file__).resolve().parents[1] / (license if license in {"LICENSE", "NOTICE"} else "licenses/" + license)
            assert notice_contents[matching[0]].decode().replace("\r\n", "\n") == reference.read_text(), license
    else:
        with tarfile.open(path,"r:gz") as archive:
            members = archive.getmembers()
        names = []
        roots = {".clang-format","CMakeLists.txt","pyproject.toml","README.md","AGENTS.md","ENVIRONMENT.md",
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
        for required in [".clang-format","scripts/check-cpp-format.py","include/meshvale/repair/duplicates.h",
                         "tests/header_canonical.cpp","examples/consumer/main.cpp",
                         "python/meshvale_repair/_version.py","python/bindings.cpp","tests/python/test_duplicates.py","src/duplicates.cpp",
                         "python/meshvale_repair/workflow.py","tests/workflow/test_workflow.py","docs/workflow.md","examples/python/reported_duplicate.py",
                         "python/meshvale_repair/obj_workflow.py","tests/assets/test_obj_workflow.py","docs/obj-workflow.md","examples/python/repair_obj_bundle.py",
                         "python/meshvale_repair/commands.py","python/meshvale_repair/__main__.py","docs/cli.md","tests/cli/test_cli.py","examples/python/obj_commands.py",
                         "scripts/portable-preview.py","examples/python/obj_walkthrough.py","docs/quickstart.md"]:
            assert required in names,required
        for license in ["eigen-mpl2.txt", "eigen-apache.txt", "eigen-notices.txt", "meshvale-geometry-notice.txt"]:
            assert "licenses/" + license in names, license
        assert not any(name.endswith(".hpp") for name in names)
    print(f"Package content check passed: {path.name}; {len(names)} files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives",nargs="+",type=Path)
    for path in parser.parse_args().archives: inspect(path)
