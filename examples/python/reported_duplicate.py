# SPDX-License-Identifier: Apache-2.0
"""Run a real in-memory duplicate edit and print its checked report."""
from array import array
import json
from meshvale_geometry import Mesh
from meshvale_repair.workflow import repair_duplicate_mesh

mesh = Mesh.from_record({"schema": "meshvale.mesh/1", "positions": array("d", [0,0,0, 1,0,0, 0,1,0]),
                        "face_offsets": array("Q", [0,3,6]), "corner_vertices": array("Q", [0,1,2, 1,2,0]), "attributes": []})
result = repair_duplicate_mesh(mesh, [(0, 1)], input_id="example-source")
assert result.exit_code == 0 and result.candidate.face_count == 1 and mesh.face_count == 2
print(json.dumps(result.report, indent=2, ensure_ascii=False))
