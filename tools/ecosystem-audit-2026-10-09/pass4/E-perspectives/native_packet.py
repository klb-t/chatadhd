#!/usr/bin/env python3
"""Run actual D packet roundtrip against a declared native binary for E input."""
import argparse,hashlib,json,pathlib,socket,subprocess,sys,tempfile
p=argparse.ArgumentParser()
for name in ['d-repo','d-sha','packet','native-source','native-sha','library','out']:p.add_argument('--'+name,required=True)
a=p.parse_args();repo=pathlib.Path(a.d_repo).resolve();native=pathlib.Path(a.native_source).resolve();out=pathlib.Path(a.out).resolve();assert not out.exists()
for r,s in [(repo,a.d_sha),(native,a.native_sha)]:assert subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip()==s
sys.dont_write_bytecode=True;sys.path.insert(0,str(repo))
from loom.tools.resource_graph.native import roundtrip
socket.socket.connect=lambda *x,**kw:(_ for _ in ()).throw(RuntimeError('audit network denied'))
socket.create_connection=socket.socket.connect
packet=json.loads(pathlib.Path(a.packet).read_text())
with tempfile.TemporaryDirectory(prefix='audit-d-native-e-') as d:result=roundtrip(packet,a.library,d)
result.update({'schema':'klbt.audit.d-native-e/1','d_sha':a.d_sha,'native_sha':a.native_sha,'native_library_sha256':hashlib.sha256(pathlib.Path(a.library).read_bytes()).hexdigest(),'input_packet_sha256':hashlib.sha256(pathlib.Path(a.packet).read_bytes()).hexdigest(),'network_calls':0,'limit':'Node/Python guards are not a native network sandbox. Only local NativeGraphStore methods called, workers disabled.'})
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['native_executed','reopened','packet_equal','selected_counts']}))
