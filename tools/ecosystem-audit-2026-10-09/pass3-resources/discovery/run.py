#!/usr/bin/env python3
"""Compile/run actual pinned native discovery/method/provider boundary consumers."""
import argparse,gzip,hashlib,json,os,subprocess,tempfile,zipfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True)
p.add_argument('--build-dir',type=Path,required=True)
p.add_argument('--research-repo',type=Path,required=True);p.add_argument('--research-sha',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();build=a.build_dir.resolve();research=a.research_repo.resolve();here=Path(__file__).resolve().parent
sha=lambda data:hashlib.sha256(data).hexdigest()
for path,expected in [(repo,a.sha),(research,a.research_sha)]:
 actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
 if actual!=expected:raise SystemExit('checkout_sha_mismatch')
source=here/'probe.cpp';artifact=research/'docs/research/thread7_real_2026-10-09/continuation_01/handoff-artifact-v2.json.gz'
obj=json.loads(gzip.decompress(artifact.read_bytes()));contract=obj['contract'];packet=obj['packet']
profile={'vocabulary':contract['vocabulary'],**{k:packet[k] for k in ['entities','claims','sources']},'selection':{'members':[{'method_version_id':contract['bindings']['method_version_id']}],'parameter_layers':['method','member','selection','user']}}
a.output.parent.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='audit-discovery-') as tmp:
 d=Path(tmp);fixture=d/'research-profile.json';fixture.write_text(json.dumps({'profile':profile,'packet':packet}))
 alpha=[{'speaker':'user','body':'synthetic alternative alpha'}];beta=[{'speaker':'user','body':'synthetic alternative beta'}]
 first={'title':'synthetic ambiguous source','alpha':alpha,'beta':beta};second={'title':'synthetic ambiguous source','beta':beta,'alpha':alpha}
 for name,document in [('first.zip',first),('second.zip',second)]:
  with zipfile.ZipFile(d/name,'w') as archive:archive.writestr('unknown-provider.json',json.dumps(document))
 binary=d/'probe';cmd=['g++','-std=c++20','-O0','-g0','-pthread','-I'+str(repo/'loom/include'),'-I'+str(repo/'loom/src'),'-I'+str(repo/'loom/third_party/nlohmann'),'-I'+str(repo/'loom/third_party/cpp-httplib'),str(source),str(build/'libloom_core.a'),str(build/'libloom_miniz.a'),str(build/'libloom_sqlite3_amalgamation.a'),'-lssl','-lcrypto','-pthread','-ldl','-lm','-o',str(binary)]
 compiled=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 a.output.with_suffix('.compile.log').write_text(compiled.stdout)
 if compiled.returncode:raise SystemExit('native_probe_compile_failed')
 run=subprocess.run([str(binary),str(fixture),str(d/'first.zip'),str(d/'second.zip')],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 a.output.with_suffix('.runtime.log').write_text(run.stderr)
 try:result=json.loads(run.stdout)
 except json.JSONDecodeError:raise SystemExit('native_receipt_not_json')
receipt={'schema':'klb.audit.discovery-native/1','repo':'klb-t/chatadhd','native_sha':a.sha,'research_sha':a.research_sha,'native_archive_sha256':sha((build/'libloom_core.a').read_bytes()),'runner_sha256':sha(Path(__file__).read_bytes()),'probe_sha256':sha(source.read_bytes()),'research_artifact_sha256':sha(artifact.read_bytes()),'fixture_projection_sha256':sha(fixture.read_bytes()) if fixture.exists() else sha(json.dumps({'profile':profile,'packet':packet}).encode()),'transport':'actual ScriptedTransport injected at Runtime creation; it has no socket backend; workers disabled','private_inputs_read':False,'paid_calls':0,'compile_exit':compiled.returncode,'run_exit':run.returncode,**result}
receipt['counts']={s:sum(x['acceptance']==s for x in result['tests']) for s in ['PASS','FAIL','BLOCKED']}
a.output.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'output':str(a.output),'counts':receipt['counts'],'real_network_calls':0}))
raise SystemExit(run.returncode)
