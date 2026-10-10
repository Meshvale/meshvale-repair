# SPDX-License-Identifier: Apache-2.0
from array import array
from meshvale_geometry import Mesh
from meshvale_repair import remove_duplicate_faces

mesh = Mesh.from_record({"schema":"meshvale.mesh/1", "positions":array("d",[0,0,0, 1,0,0, 1,1,0, 0,1,0]),
    "face_offsets":array("Q",[0,4,8]),"corner_vertices":array("Q",[0,1,2,3, 2,3,0,1]),"attributes":[]})
result = remove_duplicate_faces(mesh,[(0,1)])
assert result.outcome == "accepted" and result.candidate.face_count == 1
assert list(result.face_map) == [0,0] and list(result.corner_map) == [0,1,2,3,2,3,0,1]
assert mesh.face_count == 2
print("Cyclic quad copy removed; source preserved; correspondence retained.")
