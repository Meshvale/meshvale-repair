# SPDX-License-Identifier: Apache-2.0
"""Repair a real temporary OBJ and publish its verified receipt."""
from pathlib import Path
import tempfile
from meshvale_reports import load
from meshvale_repair.obj_workflow import repair_obj_file

with tempfile.TemporaryDirectory() as scratch:
    root = Path(scratch)
    source = root / "original.obj"
    source.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 2 3 1\n", encoding="ascii")
    result = repair_obj_file(source, root / "bundle", [(0, 1)], input_id="example-source")
    assert result.exit_code == 0 and result.candidate.document.mesh.face_count == 1
    receipt = load((root / "bundle/meshvale-report.json").read_text(encoding="ascii"))
    assert receipt == result.report and receipt["publication"]["verified"]
    print("Installed OBJ repair, verified bundle and receipt passed")
