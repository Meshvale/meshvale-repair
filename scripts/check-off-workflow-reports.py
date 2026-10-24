# SPDX-License-Identifier: Apache-2.0
"""Check actual OFF workflow reports against literal maps and physical bytes.

Run the native semantic fixture with --write-reports DIR first. The selected
Geometry report reader and jsonschema validate unchanged report/1 documents;
Python hashlib independently hashes complete captured and published bytes.
"""
from pathlib import Path
import copy
import hashlib
import json
import struct
import sys

import argparse
from jsonschema import Draft202012Validator, ValidationError

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('reports', type=Path)
parser.add_argument('--geometry-python', type=Path, required=True)
parser.add_argument('--unsupported-profile', action='store_true')
parser.add_argument('--receipt', type=Path)
args = parser.parse_args()
reports = args.reports.resolve()
sys.path.insert(0, str(args.geometry_python.resolve()))
import meshvale_reports as model
validator = Draft202012Validator(model.schema_document())

digest=lambda b:hashlib.sha256(b).hexdigest()
checks=['source-storage','target-policy','candidate-storage','repair-correspondence',
 'canonical-preservation','reload-candidate','source-unchanged','staged-candidate','staged-physical-members']
position_bits=[0x8000000000000000,0,0,0x3ff0000000000000,0,0,0x3ff0000000000000,0x3ff0000000000000,0,
 0,0x3ff0000000000000,0,0x4000000000000000,0,0,0x4008000000000000,0,0,0x4000000000000000,
 0x3ff0000000000000,0,0x4058c00000000000,0x8000000000000000,0x4014000000000000]
face_map=[0,0,1];corner_map=[0,1,2,3,2,3,0,1,4,5,6]
expected={}
for kind in ['changed','unchanged','empty']:
    for publication in ['memory','published']:
        for policy in ['serial','parallel']:expected[f'{kind}-{publication}-{policy}']=(kind,publication=='published')
for i in range(3):expected['rejected-'+str(i)]=('rejected',False)
if args.unsupported_profile:
    expected = {name: row for name, row in expected.items() if row[0] == 'empty'}
manifest = json.loads((reports/'cases.json').read_text(encoding='utf-8'))
assert manifest['version'] == 1 and sorted(manifest['cases']) == sorted(expected)
def stats(value):
    if isinstance(value,dict):
        if 'launched_workers' in value:assert value['launched_workers']==value['joined_workers']
        for item in value.values():stats(item)
    elif isinstance(value,list):
        for item in value:stats(item)
def verify(case,report,kind,published):
    model.validate(report)
    validator.validate(report)
    assert model.profile_outcome(report)==report['profile']['outcome']
    source=(case/'source.off').read_bytes()
    extension=report['extensions']['meshvale.repair.off']
    rejected=kind=='rejected';changed=kind=='changed';empty=kind=='empty'
    assert extension['version']==1
    assert report['execution']=={'outcome':'completed','stage':'complete','reason':None}
    assert report['candidate']['outcome']==('rejected' if rejected else ('accepted' if changed else 'unchanged'))
    assert model.exit_code(report)==(1 if rejected else 0)
    assert report['profile']['name']==('off-indexed-duplicate-publication' if published else 'off-indexed-duplicate-preservation')
    assert report['profile']['version']=='1' and report['profile']['required_checks']==checks[:9 if published else 7]
    assert report['operation']['name']=='remove-duplicate-faces' and report['operation']['version']=='1'
    wanted_targets=[] if kind in ['empty','unchanged'] else [{'keep':0,'remove':99 if case.name=='rejected-0' else 1}]
    assert report['operation']['options']=={'text':'canonical-ascii','edge_count':'unknown-zero','targets':wanted_targets}
    coverage={c['check']:c for c in report['coverage']}
    assert set(coverage)==set(checks+['solid-validity'])
    assert coverage['solid-validity']['status']=='unsupported' and coverage['solid-validity']['finding'] is None
    assert extension['policies']=={'text':'canonical-ascii','edge_count':'unknown-zero','units':'unknown'}
    assert extension['source']['declared_edge_count']==17 and extension['source']['profile']=='ascii-records'
    meshes={s['role']:s['meshes'][0] for s in report['snapshots']}
    input_counts=(0,0,0) if empty else (9 if case.name=='rejected-2' else 8,3,11)
    assert tuple(meshes['input'][k+'_count'] for k in ['vertex','face','corner'])==input_counts
    if rejected:
        assert set(meshes)=={'input'} and report['maps']==[] and report['preservation']==[]
        assert extension['source_faces'] is None and extension['source_corners'] is None and not extension['memory_output_verified']
        assert extension['write_facts'] is None and report['input']['sha256'] is None
        assert extension['hash_bytes_processed']==0 and report['publication']['artifacts']==[]
    else:
        candidate_counts=(0,0,0) if empty else (8,2,7) if changed else (8,3,11)
        assert tuple(meshes['candidate'][k+'_count'] for k in ['vertex','face','corner'])==candidate_counts
        assert set(meshes)==({'input','candidate','output'} if published else {'input','candidate'})
        assert report['input']['sha256']==digest(source)
        assert extension['memory_output_verified']
        assert [(row['feature']['name'],row['outcome']) for row in report['preservation']]==[
          ('vertex-position-bits','preserved'),('retained-directed-polygon-loops','preserved'),('off-lexical-and-edge-count','converted')]
        assert extension['source_faces']==([] if empty else [0,2] if changed else [0,1,2])
        assert extension['source_corners']==([] if empty else [0,1,2,3,8,9,10] if changed else list(range(11)))
        maps={m['id']:m for m in report['maps']}
        assert set(maps)==set(['repair-'+x for x in ['vertices','faces','corners']]+(['export-'+x for x in ['vertices','faces','corners']] if published else []))
        for name,row in maps.items():
            assert row['from']['mesh']==row['to']['mesh']=='surface'
            if changed and name in ['repair-faces','repair-corners']:
                assert row['kind']=='explicit' and row['entries']==(face_map if name=='repair-faces' else corner_map)
            else:assert row['kind']=='identity' and row['entries'] is None
        for check in checks[:9 if published else 7]:
            assert coverage[check]['status']=='performed' and coverage[check]['finding']=='passed'
        if not published:
            for check in checks[7:]:assert coverage[check]['status']=='skipped' and coverage[check]['finding'] is None
        assert extension['hash_bytes_processed']==len(source)+(len(next((case/'bundle').glob('*.off')).read_bytes()) if published else 0)
        hashing=extension['phase_execution']['hashing']
        assert hashing['launched_workers']==hashing['joined_workers']==0
        assert hashing['planned_jobs']==hashing['caller_jobs']==(2 if published else 1)
        if published:
            entry=extension['artifact_paths']['entry']
            assert entry=='\u6e90-\U0001f600.off'
            assert {p.name for p in (case/'bundle').iterdir()}=={entry,'report.json'}
            physical=(case/'bundle'/entry).read_bytes()
            assert (case/'bundle'/'report.json').read_bytes()==(case/'report.json').read_bytes()
            assert report['publication']['artifacts']==[{'id':'entry','sha256':digest(physical)}]
            assert physical!=source
            lines=physical.decode('ascii').splitlines();assert lines[0]=='OFF'
            vertices,faces,edge=map(int,lines[1].split());assert edge==0 and (vertices,faces)==candidate_counts[:2]
            actual=[struct.unpack('<Q',struct.pack('<d',float(word)))[0] for line in lines[2:2+vertices] for word in line.split()]
            assert actual==([] if empty else position_bits)
            loops=[list(map(int,line.split())) for line in lines[2+vertices:]]
            assert all(row[0]==len(row)-1 for row in loops)
            assert [row[1:] for row in loops]==([] if empty else [[0,1,2,3],[4,5,6]] if changed else [[0,1,2,3],[2,3,0,1],[4,5,6]])
            assert extension['requested_bytes_before_report_copy']['publication_files'] is None
            assert extension['requested_bytes_before_report_copy']['publication_off'] is None
        else:assert report['publication']['artifacts']==[] and not (case/'bundle').exists()
    stats(extension['phase_execution'])
rows=[]
for name,(kind,published) in expected.items():
    case=reports/name;raw=(case/'report.json').read_bytes();report=model.load(raw.decode('utf-8'))
    verify(case,report,kind,published)
    rows.append({'case':name,'report_sha256':digest(raw),'source_sha256':digest((case/'source.off').read_bytes()),'exit':model.exit_code(report)})
case=reports/('empty-published-serial' if args.unsupported_profile else 'changed-published-serial');original=model.load((case/'report.json').read_bytes().decode('utf-8'))
controls=[]
for name in (['schema','profile','source-hash','artifact-hash'] if args.unsupported_profile else ['schema','map','profile','source-hash','artifact-hash']):
    changed=copy.deepcopy(original)
    if name=='schema':changed['unexpected']=1
    if name=='map':next(row for row in changed['maps'] if row['id']=='repair-faces')['entries'][0]=99
    if name=='profile':changed['profile']['outcome']='incomplete'
    if name=='source-hash':changed['input']['sha256']='0'*64
    if name=='artifact-hash':changed['publication']['artifacts'][0]['sha256']='0'*64
    refused=False
    try:verify(case,changed,'empty' if args.unsupported_profile else 'changed',True)
    except (AssertionError,model.ReportError,ValidationError):refused=True
    assert refused;controls.append(name)
receipt={'scope':'Selected unchanged report/1 reader plus explicit JSON schema, references/exits, literal OFF/maps/raw SHA and physical inventories; reader path/hash identifies source or installed producer, no inferred package qualification',
 'reader':{'path':model.__file__,'sha256':digest(Path(model.__file__).read_bytes())},
 'schema_sha256':digest((Path(model.__file__).parent/'report-v1.schema.json').read_bytes()),
 'reports':rows,'negative_detached_controls':controls,
 'native_reports_unmodified':True,'oracle_sha256':digest(Path(__file__).read_bytes())}
if args.receipt:
    args.receipt.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print('verified',len(rows),'native reports and',len(controls),'detached negative controls')
if args.receipt: print('receipt',digest(args.receipt.read_bytes()))
