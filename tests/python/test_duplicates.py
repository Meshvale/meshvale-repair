# SPDX-License-Identifier: Apache-2.0
from array import array
from concurrent.futures import ThreadPoolExecutor
from dataclasses import FrozenInstanceError
import gc
import importlib.metadata
import unittest
from meshvale_geometry import Mesh
from meshvale_repair import remove_duplicate_faces, __version__


def attribute(domain,name,semantic,code,values,**options):
    kinds = {"f":"float32","d":"float64","i":"int32","B":"uint8","H":"uint16","I":"uint32","Q":"uint64"}
    return {"domain":domain,"name":name,"semantic":semantic,"set_index":None,"components":1,
            "scalar_type":kinds[code],"values":array(code,values),"offsets":None,"present":None,"metadata":{},**options}


def fixture():
    positions = [0,0,0, 1,0,0, 0,1,0, 2,0,0, 2,1,0, 4,0,0, 6,0,0, 6,2,0, 5,1,0, 4,2,0]
    uv = [0,0, 1,0, 0,1, .25,0, 1,0, 1,1, .25,1, 0,0, 1,0, 1,1, .5,.5, 0,1]
    uv.extend(uv[10:14]+uv[6:10])
    return {"schema":"meshvale.mesh/1","positions":array("d",positions),
        "face_offsets":array("Q",[0,3,7,12,16]),
        "corner_vertices":array("Q",[0,1,2, 1,3,4,2, 5,6,7,8,9, 4,2,1,3]),
        "attributes":[attribute("corner","uv0","texcoord","d",uv,components=2,set_index=0),
            attribute("corner","uv1","texcoord","f",uv,components=2,set_index=1),
            attribute("corner","normal","normal","d",[0,0,1]*16,components=3),
            attribute("face","material","material_index","i",[0,1,2,1]),
            attribute("face","tag","label","B",[10,11,12,11]),
            attribute("vertex","joints","joint_indices","I",[0,1,0,1,2,3,4],offsets=array("Q",[0,0,2,7,7,7,7,7,7,7,7])),
            attribute("vertex","weights","joint_weights","f",[.25,.75,.1,.2,.3,.15,.25],offsets=array("Q",[0,0,2,7,7,7,7,7,7,7,7])),
            attribute("vertex","id","identity","Q",[2**53+1+i for i in range(10)],metadata={"origin":"作者"})]}


def fingerprint(mesh):
    record = mesh.to_record()
    return (record["positions"].tobytes(),record["face_offsets"].tobytes(),record["corner_vertices"].tobytes(),
            tuple((row["domain"],row["name"],row["semantic"],row["set_index"],row["components"],row["scalar_type"],
                   row["values"].tobytes(),None if row["offsets"] is None else row["offsets"].tobytes(),
                   None if row["present"] is None else row["present"].tobytes(),dict(row["metadata"])) for row in record["attributes"]))


class DuplicateTests(unittest.TestCase):
    def test_installed_version_and_cyclic_mixed_polygon_preservation(self):
        self.assertEqual(__version__,importlib.metadata.version("meshvale-repair"))
        mesh = Mesh.from_record(fixture()); before = fingerprint(mesh)
        result = remove_duplicate_faces(mesh,[(1,3)])
        self.assertEqual(result.outcome,"accepted"); self.assertIsInstance(result.candidate,Mesh)
        self.assertEqual(result.diagnostics,()); self.assertEqual(list(result.face_map),[0,1,2,1])
        self.assertEqual(list(result.corner_map),list(range(12))+[5,6,3,4])
        self.assertEqual(fingerprint(mesh),before)
        out = result.candidate.to_record(); original = mesh.to_record()
        self.assertEqual(list(out["face_offsets"]),[0,3,7,12])
        self.assertEqual(list(out["corner_vertices"]),list(original["corner_vertices"][:12]))
        for old,new in zip(original["attributes"],out["attributes"]):
            if old["domain"] == "vertex":
                self.assertEqual(old["values"].tobytes(),new["values"].tobytes())
                if old["offsets"] is not None: self.assertEqual(list(old["offsets"]),list(new["offsets"]))
            elif old["domain"] == "face": self.assertEqual(list(new["values"]),list(old["values"][:3]))
            else: self.assertEqual(list(new["values"]),list(old["values"][:12*old["components"]]))
        self.assertEqual(list(out["attributes"][0]["values"][2:4]),[1,0])
        self.assertEqual(list(out["attributes"][0]["values"][6:8]),[.25,0])

    def test_unchanged_identity_copy_and_surviving_maps(self):
        mesh = Mesh.from_record(fixture()); expected = fingerprint(mesh)
        result = remove_duplicate_faces(mesh,iter(()))
        self.assertEqual(result.outcome,"unchanged"); self.assertIsNot(result.candidate,mesh)
        self.assertEqual(fingerprint(result.candidate),expected)
        self.assertEqual(list(result.face_map),list(range(4)))
        self.assertEqual(list(result.corner_map),list(range(16)))
        self.assertTrue(result.face_map.readonly); self.assertTrue(result.corner_map.readonly)
        with self.assertRaises(TypeError): result.face_map[0] = 99
        with self.assertRaises(FrozenInstanceError): result.outcome = "accepted"
        candidate,face_map = result.candidate,result.face_map
        view = mesh.to_record()["positions"]; view.release()
        del mesh,result; gc.collect()
        self.assertEqual(fingerprint(candidate),expected); self.assertEqual(list(face_map),[0,1,2,3])

    def assert_rejected(self,source,targets,code):
        mesh = Mesh.from_record(source); before = fingerprint(mesh)
        result = remove_duplicate_faces(mesh,targets)
        self.assertEqual(result.outcome,"rejected"); self.assertIsNone(result.candidate)
        self.assertEqual(list(result.face_map),[]); self.assertEqual(list(result.corner_map),[])
        self.assertIn(code,{item["code"] for item in result.diagnostics})
        self.assertEqual(fingerprint(mesh),before)
        return result

    def test_attribute_winding_and_whole_request_rejections(self):
        for channel,offset,value in [(1,24,.5),(2,36,1),(3,3,0),(4,3,99)]:
            source = fixture(); source["attributes"][channel]["values"][offset] = value
            with self.subTest(channel=channel): self.assert_rejected(source,[(1,3)],"repair.not_equivalent")
        source = fixture(); source["corner_vertices"][12:] = array("Q",[1,2,4,3])
        self.assert_rejected(source,[(1,3)],"repair.not_equivalent")
        self.assert_rejected(fixture(),[(1,3),(0,2)],"repair.not_equivalent")
        self.assert_rejected(fixture(),[(1,3),(3,1)],"repair.removed_representative")
        self.assert_rejected(fixture(),[(1,3),(1,3)],"repair.repeated_target")
        self.assert_rejected(fixture(),[(1,1)],"repair.self_target")
        self.assert_rejected(fixture(),[(1,2**64-1)],"repair.target_range")

    def test_unknown_row_local_declaration_and_nonmanifold_input(self):
        source = fixture(); source["attributes"].append(attribute("face","custom","vendor_tag","H",[0,1,2,1]))
        self.assert_rejected(source,[(1,3)],"repair.unsupported_attribute")
        mesh = Mesh.from_record(source)
        result = remove_duplicate_faces(mesh,[(1,3)],row_local_attributes=[("face","custom")])
        self.assertEqual(result.outcome,"accepted")
        self.assertEqual(list(result.candidate.to_record()["attributes"][-1]["values"]),[0,1,2])
        self.assertIn("topology.edge_nonmanifold",{d["code"] for d in mesh.inspect_topology()["diagnostics"]})
        self.assertNotIn("topology.edge_nonmanifold",{d["code"] for d in result.candidate.inspect_topology()["diagnostics"]})

    def test_malformed_storage_missingness_and_ragged_rows(self):
        source = fixture(); source["corner_vertices"][0] = 99
        self.assert_rejected(source,[(1,3)],"mesh.vertex_range")
        source = fixture(); source["attributes"][0]["present"] = array("B",[1]*12+[0]*4)
        self.assert_rejected(source,[(1,3)],"repair.not_equivalent")
        source = fixture(); channel = source["attributes"][1]
        channel["offsets"] = array("Q",range(0,33,2)); channel["present"] = array("B",[1]*16)
        result = remove_duplicate_faces(Mesh.from_record(source),[(1,3)])
        self.assertEqual(result.outcome,"accepted")
        self.assertEqual(list(result.candidate.to_record()["attributes"][1]["offsets"]),list(range(0,25,2)))

    def test_strict_request_representation_and_concurrent_calls(self):
        mesh = Mesh.from_record(fixture())
        for targets in [[(True,3)],[(1.0,3)],[(1,-1)],[(1,2**64)],[(1,3,4)],[None]]:
            with self.subTest(targets=targets),self.assertRaises((TypeError,ValueError,OverflowError)):
                remove_duplicate_faces(mesh,targets)
        for keys in [[("edge","a")],[("face",False)],[("face",)], [None]]:
            with self.assertRaises((TypeError,ValueError)): remove_duplicate_faces(mesh,[(1,3)],row_local_attributes=keys)
        with self.assertRaises(TypeError): remove_duplicate_faces(mesh.to_record(),[(1,3)])
        with ThreadPoolExecutor(max_workers=3) as workers:
            results = list(workers.map(lambda _: remove_duplicate_faces(mesh,[(1,3)]),range(12)))
        self.assertTrue(all(result.outcome == "accepted" for result in results))
        self.assertEqual(mesh.face_count,4)


if __name__ == "__main__":
    unittest.main()
