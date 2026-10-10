# SPDX-License-Identifier: Apache-2.0
"""Consume the installed scheduler, module and console using real OBJ inputs."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from meshvale_reports import load
from meshvale_repair.commands import run_obj_jobs

with tempfile.TemporaryDirectory() as scratch:
    root = Path(scratch)
    (root / "sources").mkdir(); (root / "outputs").mkdir()
    (root / "sources/model.obj").write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 2 3 1\n", encoding="ascii")
    request = {"schema": "meshvale.repair.obj-jobs/1", "jobs": [
        {"id": "example-part", "input": "sources/model.obj", "destination": "outputs/repaired", "targets": [[0, 1]]}]}
    batch = run_obj_jobs(request, base=root)
    receipt = load((root / "outputs/repaired/meshvale-report.json").read_text(encoding="ascii"))
    assert batch["exit_code"] == 0 and batch["jobs"][0]["report"] == receipt
    assert receipt["publication"]["outcome"] == "published"
    module = subprocess.run([sys.executable, "-I", "-m", "meshvale_repair", "obj", "sources/model.obj",
                             "outputs/module", "--target", "0:1", "--input-id", "module-part"],
                            cwd=root, capture_output=True, text=True, check=False, timeout=60)
    assert module.returncode == 0, module.stderr
    assert load(module.stdout) == load((root / "outputs/module/meshvale-report.json").read_text(encoding="ascii"))
    request["jobs"][0].update(id="console-part", destination="outputs/console")
    (root / "jobs.json").write_text(json.dumps(request), encoding="utf-8")
    console = Path(sys.executable).parent / ("meshvale-repair.exe" if os.name == "nt" else "meshvale-repair")
    command = subprocess.run([str(console), "obj-batch", "jobs.json"], cwd=root, capture_output=True,
                             text=True, check=False, timeout=60)
    assert command.returncode == 0, command.stderr
    batch = load(command.stdout)
    assert batch["jobs"][0]["report"] == load((root / "outputs/console/meshvale-report.json").read_text(encoding="ascii"))
    print("Installed OBJ scheduler, module/console commands and independent receipts passed")
