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
                assert len(parts) == 2 and (parts[1] in {"__init__.py","__main__.py","_version.py","workflow.py","obj_workflow.py","_obj_export.py","commands.py","gltf_workflow.py","gltf_commands.py","off_workflow.py","off_commands.py","ply_workflow.py","ply_commands.py"} or
                    (parts[1].startswith("_repair.") and parts[1].endswith((".pyd",".so")))), name
            else:
                assert parts[1:] in [("METADATA",), ("WHEEL",), ("RECORD",), ("entry_points.txt",),
                    ("licenses", "LICENSE"), ("licenses", "NOTICE"),
                    ("licenses", "licenses", "nanobind.txt"),
                    ("licenses", "licenses", "robin-map.txt"),
                    ("licenses", "licenses", "eigen-mpl2.txt"),
                    ("licenses", "licenses", "eigen-apache.txt"),
                    ("licenses", "licenses", "eigen-notices.txt"),
                    ("licenses", "licenses", "meshvale-geometry-notice.txt"),
                    ("licenses", "licenses", "meshvale-interchange-notice.txt"),
                    ("licenses", "licenses", "cgltf.txt"),
                    ("licenses", "licenses", "jsoncons.txt"),
                    ("licenses", "licenses", "tinyobjloader.txt")], name
        for license in ["eigen-mpl2.txt","eigen-apache.txt","eigen-notices.txt","LICENSE","NOTICE","nanobind.txt","robin-map.txt","meshvale-geometry-notice.txt","meshvale-interchange-notice.txt","cgltf.txt","jsoncons.txt","tinyobjloader.txt"]:
            matching = [name for name in notice_contents if name.endswith("/" + license)]
            assert len(matching) == 1, (license, matching)
            reference = Path(__file__).resolve().parents[1] / (license if license in {"LICENSE", "NOTICE"} else "licenses/" + license)
            assert notice_contents[matching[0]].decode().replace("\r\n", "\n") == reference.read_text(), license
    else:
        with tarfile.open(path,"r:gz") as archive:
            members = archive.getmembers()
        names = []
        roots = {".clang-format",".gitattributes",".git_archival.txt","CMakeLists.txt","pyproject.toml","README.md","AGENTS.md","ENVIRONMENT.md",
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
        for required in [".clang-format", ".gitattributes", ".git_archival.txt",
                         "cmake/SourceVersion.cmake", "docs/versioning.md", "scripts/check-source-version.py",
                         "scripts/check-cpp-format.py","include/meshvale/repair/duplicates.h",
                         "scripts/check-native-tests.py", "scripts/diagnose-windows-startup.py",
                         "scripts/check-gltf-python-return.py",
                         "tests/python_native/gltf_binding_module.cpp",
                         "tests/python_native/gltf_binding_observer.h",
                         "tests/python_native/gltf_binding_observer.cpp",
                         "tests/python_native/gltf_binding_allocation.h",
                         "tests/python_native/gltf_binding_allocation.cpp",
                         "tests/python_native/test_gltf_return.py",
                         "include/meshvale/repair/materialized_duplicates.h",
                         "include/meshvale/repair/off_workflow.h", "cmake/OffWorkflow.cmake",
                         "src/off_workflow.cpp", "src/off_workflow_internal.h",
                         "src/off_workflow_report.cpp", "src/off_workflow_test_internal.h",
                         "tests/off_workflow.cpp", "tests/off_workflow_requests.cpp",
                         "tests/off_workflow_fixture.h", "tests/header_off_workflow.cpp",
                         "examples/consumer/off_workflow.cpp", "docs/off-workflow.md",
                         "examples/python/off_walkthrough.py", "docs/off-quickstart.md",
                         "docs/off-presets.md", "docs/off-presets.json",
                         "python/off_presets.h", "python/off_presets.cpp",
                         "python/off_bindings.h", "python/off_bindings.cpp",
                         "python/meshvale_repair/off_workflow.py",
                         "python/meshvale_repair/off_commands.py",
                         "tests/off-python/off_test_fixture.py",
                         "tests/off-python/test_off_workflow.py",
                         "tests/off-cli/off_command_fixture.py",
                         "tests/off-cli/test_off_commands.py",
                         "scripts/check-off-workflow-reports.py",
                         "cmake/PlyWorkflow.cmake", "include/meshvale/repair/ply_workflow.h",
                         "src/ply_workflow.cpp", "src/ply_workflow_internal.h",
                         "src/ply_workflow_report.cpp", "src/ply_workflow_test_internal.h",
                         "docs/ply-workflow.md", "docs/ply-reports.md",
                         "docs/ply-presets.md", "docs/ply-presets.json",
                         "tests/ply_interchange_fixture.h", "tests/ply_workflow.cpp",
                         "tests/ply_workflow_observations.h", "tests/header_ply_workflow.cpp",
                         "tests/header_ply_workflow_observations.cpp", "scripts/check-ply-workflow-reports.py",
                         "tests/fixtures/ply-workflow/README.md", "tests/fixtures/ply-workflow/literal-predicates.json",
                         "tests/fixtures/ply-workflow/empty-source-oracles.json", "python/ply_bindings.h",
                         "python/ply_bindings.cpp", "python/ply_presets.h",
                         "python/ply_presets.cpp", "python/meshvale_repair/ply_workflow.py",
                         "python/meshvale_repair/ply_commands.py", "tests/ply_workflow_python.py",
                         "docs/ply-walkthrough.md",
                         "examples/ply-workflow-consumer/main.cpp",
                         "examples/ply-workflow-consumer/CMakeLists.txt",
                         "include/meshvale/repair/gltf_workflow.h", "docs/gltf-workflow.md",
                         "cmake/GltfWorkflow.cmake", "src/gltf_workflow.cpp",
                         "cmake/GltfFileWorkflow.cmake",
                         "include/meshvale/repair/gltf_file_workflow.h",
                         "docs/gltf-file-workflow.md",
                         "docs/gltf-retained-facts.md",
                         "src/gltf_file_workflow.cpp", "src/gltf_file_workflow_internal.h",
                         "src/gltf_file_workflow_report.cpp",
                         "src/gltf_file_sha256_internal.h", "src/gltf_file_sha256_internal.cpp",
                         "tests/header_gltf_file_workflow.cpp", "tests/gltf_file_sha256.cpp",
                         "tests/gltf_file_sha256_allocation.cpp", "tests/gltf_file_sha256_allocation.h",
                         "tests/hash_vectors.h", "tests/hash_prefix_answers.h",
                         "tests/gltf_file_workflow.cpp", "tests/gltf_file_workflow_requests.cpp",
                         "tests/gltf_retained_facts.cpp",
                         "tests/gltf_file_workflow_fixture.h", "scripts/check-gltf-file-reports.py",
                         "examples/consumer/gltf_file_workflow.cpp",
                         "src/gltf_workflow_internal.h", "src/gltf_workflow_report.cpp",
                         "src/gltf_workflow_report_internal.h", "src/gltf_file_workflow_test_internal.h",
                         "tests/gltf_workflow.cpp", "tests/gltf_workflow_requests.cpp",
                         "src/gltf_workflow_test_internal.h", "tests/header_gltf_workflow.cpp",
                         "scripts/gltf-workflow-ci.py",
                         "examples/consumer/gltf_workflow.cpp",
                         "src/materialized_duplicates.cpp", "src/materialized_duplicate_allocation.cpp",
                         "src/materialized_duplicate_allocation.h", "src/materialized_duplicate_workers.cpp",
                         "src/materialized_duplicate_workers.h", "src/materialized_duplicate_test_internal.h",
                         "tests/header_materialized_duplicates.cpp", "tests/materialized_duplicate_requests.cpp",
                         "tests/materialized_duplicates.cpp", "tests/materialized_duplicates_unsupported.cpp",
                         "tests/materialized_duplicate_semantics/fixture_support.h",
                         "tests/materialized_duplicate_semantics/fixture_support.cpp",
                         "tests/materialized_duplicate_semantics/equality.cpp",
                         "tests/materialized_duplicate_semantics/attributes.cpp",
                         "tests/materialized_duplicate_semantics/correspondence.cpp",
                         "tests/materialized_duplicate_semantics/lifetimes.cpp",
                         "tests/materialized_duplicate_semantics/main.cpp",
                         "docs/materialized_duplicates.md", "examples/consumer/materialized_duplicates.cpp",
                         "tests/header_canonical.cpp","examples/consumer/main.cpp",
                         "python/meshvale_repair/_version.py","python/bindings.cpp","tests/python/test_duplicates.py","src/duplicates.cpp",
                         "python/gltf_bindings.h", "python/gltf_bindings.cpp",
                         "python/gltf_presets.h", "python/gltf_presets.cpp",
                         "python/meshvale_repair/gltf_workflow.py", "docs/gltf-python.md",
                         "tests/gltf-python/test_gltf_file_workflow.py", "examples/python/gltf_walkthrough.py",
                         "tests/gltf-python/test_retained_facts.py",
                         "python/meshvale_repair/gltf_commands.py", "docs/gltf-commands.md",
                         "tests/gltf-cli/test_gltf_commands.py",
                         "tests/gltf-cli/_gltf_command_observer.py", "tests/gltf-cli/gltf_command_fixture.py",
                         "docs/gltf-scene-walkthrough.md", "examples/python/gltf_scene_walkthrough.py",
                         "examples/scene-consumer/CMakeLists.txt", "examples/scene-consumer/main.cpp",
                         "tests/gltf-scene/test_gltf_scene.py",
                         "python/meshvale_repair/workflow.py","tests/workflow/test_workflow.py","docs/workflow.md","examples/python/reported_duplicate.py",
                         "python/meshvale_repair/obj_workflow.py","tests/assets/test_obj_workflow.py","docs/obj-workflow.md","examples/python/repair_obj_bundle.py",
                         "python/meshvale_repair/commands.py","python/meshvale_repair/__main__.py","docs/cli.md","tests/cli/test_cli.py","examples/python/obj_commands.py",
                         "tests/obj-corpus/test_obj_corpus.py",
                         "python/meshvale_repair/_obj_export.py", "docs/obj-triangle-export.md",
                         "tests/obj-export/test_preparation.py", "tests/obj-export/test_obj_export.py",
                         "examples/python/obj_triangle_export.py",
                         "scripts/portable-preview.py","examples/python/obj_walkthrough.py","docs/quickstart.md"]:
            assert required in names,required
        for license in ["eigen-mpl2.txt", "eigen-apache.txt", "eigen-notices.txt", "meshvale-geometry-notice.txt", "meshvale-interchange-notice.txt", "cgltf.txt", "jsoncons.txt", "tinyobjloader.txt"]:
            assert "licenses/" + license in names, license
        assert not any(name.endswith(".hpp") for name in names)
    print(f"Package content check passed: {path.name}; {len(names)} files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives",nargs="+",type=Path)
    for path in parser.parse_args().archives: inspect(path)
