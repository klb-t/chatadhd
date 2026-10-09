#!/usr/bin/env python3
"""Real profile serializer -> NativeGraphStore -> reopen -> real web consumer.
No product implementation is copied. FAIL and BLOCKED are separate; exit 2
means the complete vertical acceptance remains blocked despite component PASS.
"""
import argparse, hashlib, json, pathlib, socket, subprocess, sys, tempfile
p=argparse.ArgumentParser(description=__doc__)
for key in ['repo','sha','typescript','native-lib','native-source','native-sha','output']:p.add_argument('--'+key,required=True)
p.add_argument('--node',default='node');a=p.parse_args()
repo=pathlib.Path(a.repo).resolve();out=pathlib.Path(a.output).resolve();native=pathlib.Path(a.native_source).resolve()
def git(root,*args):return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
assert git(repo,'rev-parse','HEAD')==a.sha,'checkout SHA mismatch'
assert git(native,'rev-parse','HEAD')==a.native_sha,'native source SHA mismatch'
assert not out.exists(),'use a fresh evidence directory';out.mkdir(parents=True)
scope=['loom/web/src/profiles/runtime.ts','loom/web/src/profiles/graph.ts','loom/web/src/profiles/capability-graph.ts','loom/web/src/profiles/loom-adapter.ts','loom/web/src/api/loom-http.ts','loom/web/src/api/operations.ts','loom/web/src/api/types.ts','loom/tools/coordination/graph_store.py','loom/tools/structure/agentic_graph_v1/packet.py']
assert not git(repo,'status','--porcelain','--',*scope),'audited source is dirty'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
binary_scope=['loom/src/capi/capi_graph_packet_store.cpp','loom/src/knowledge/graph_packet_store.cpp']
# Resolve actual graph-store units rather than asserting unobserved filenames.
binary_scope=[x for x in git(repo,'ls-files','loom/src').splitlines() if 'graph_packet' in x]
assert binary_scope,'graph packet store source set missing'
for x in binary_scope:assert (repo/x).read_bytes()==(native/x).read_bytes(),f'native source differs: {x}'
def node(mode):
 r=subprocess.run([a.node,str(pathlib.Path(__file__).with_name('profile_probe.mjs')),str(repo),str(pathlib.Path(a.typescript).resolve()),str(out),mode],capture_output=True,text=True,timeout=60)
 (out/f'node-{mode}.log').write_text(r.stdout+r.stderr)
 if r.returncode not in [0,1]:raise RuntimeError(f'node {mode} setup failed: see log')
 if not (out/f'web-{mode}.json').exists():raise RuntimeError(f'node {mode} did not emit receipt: see log')
 return r.returncode
prepare=node('prepare');sys.path.insert(0,str(repo));sys.dont_write_bytecode=True
def blocked(*args,**kwargs):raise RuntimeError('audit_external_transport_blocked')
socket.socket.connect=blocked;socket.create_connection=blocked
from loom.tools.coordination.graph_store import NativeGraphStore
request=json.loads((out/'acceptance-request.json').read_text());native_cases=[]
second_request=json.loads((out/'acceptance-request-revision2.json').read_text())
with tempfile.TemporaryDirectory(prefix='audit-resource-web-') as d:
 with NativeGraphStore(a.native_lib,d) as store:
  result=store.execute(request);receipt_id=result['receipt']['id']
  second_result=store.execute(second_request);second_id=second_result['receipt']['id']
 with NativeGraphStore(a.native_lib,d) as store:
  read=store.read(receipt_id);replay=store.replay(receipt_id)
  second_read=store.read(second_id)
  ok=read['row_drift']['matches'] and read['receipt']['packet']==request['packet'] and replay['receipt']==result['receipt'] and second_read['receipt']['packet']==second_request['packet'] and second_read['row_drift']['matches']
  native_cases.append({'id':'WEB-NATIVE-01','phase':'acceptance','status':'PASS' if ok else 'FAIL','actual_native_write_close_reopen_read_replay':True})
  (out/'native-read.json').write_text(json.dumps(read,ensure_ascii=False,indent=2)+'\n')
  (out/'native-read-revision2.json').write_text(json.dumps(second_read,ensure_ascii=False,indent=2)+'\n')
restored=node('restore');cases=native_cases
for name in ['prepare','restore']:cases+=json.loads((out/f'web-{name}.json').read_text())['cases']
summary={key:sum(c['status']==key for c in cases) for key in ['PASS','FAIL','BLOCKED']}
receipt={'schema':'klbt.audit.resource-web/1','repo':'klb-t/chatadhd','sha':a.sha,'native_build_source_sha':a.native_sha,'native_library_sha256':sha(pathlib.Path(a.native_lib)),'native_graph_packet_units_equal':[{'path':x,'sha256':sha(repo/x)} for x in binary_scope], 'source_files':[{'path':x,'sha256':sha(repo/x)} for x in scope], 'cases':cases,'summary':summary,'external_model_calls':0,'blocked_oracles':'WEB-03/07/11 are manually identified missing consumer contracts; these placeholders do not discover a future B API automatically.', 'transport_limit':'Node and Python guards do not sandbox native C++; NativeGraphStore invokes only local graph packet operations with workers disabled. No model operation is invoked.', 'boundary':'Existing graph packet store native library from declared SHA; selected graph-packet implementation source checked equal. Not a full B2 rebuild or a mounted browser test.'}
(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n');print(json.dumps(summary))
sys.exit(1 if summary['FAIL'] else 2 if summary['BLOCKED'] else 0)
