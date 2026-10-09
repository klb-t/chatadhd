#!/usr/bin/env python3
"""Build a minimal C4 final-packet boundary against an attested native archive.

Supports the PASS4 selective object manifest or an ordinary --build-dir. All
objects are linked before the unchanged archive; no product checkout is edited.
"""
import argparse,gzip,hashlib,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True)
p.add_argument('--research-repo',type=Path,required=True);p.add_argument('--research-sha',required=True)
p.add_argument('--object-manifest',type=Path);p.add_argument('--build-dir',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve();research=a.research_repo.resolve();source=Path(__file__).with_name('registry_probe.cpp')
sha=lambda raw:hashlib.sha256(raw).hexdigest()
for checkout,expected in [(repo,a.sha),(research,a.research_sha)]:
 if subprocess.check_output(['git','-C',str(checkout),'rev-parse','HEAD'],text=True).strip()!=expected:raise SystemExit('checkout_sha_mismatch')
objects=[];attestation={}
if a.object_manifest:
 manifest=json.loads(a.object_manifest.read_bytes())
 if manifest['sha']!=a.sha:raise SystemExit('manifest_sha_mismatch')
 archive=Path(manifest['base_archive']);build=archive.parent
 if sha(archive.read_bytes())!=manifest['base_archive_sha256']:raise SystemExit('base_archive_hash_mismatch')
 for item in manifest['objects']:
  path=Path(item['path'])
  if sha(path.read_bytes())!=item['object_sha256'] or sha((repo/item['source']).read_bytes())!=item['source_sha256']:raise SystemExit('object_source_hash_mismatch')
  objects.append(str(path))
 attestation={'manifest_sha256':sha(a.object_manifest.read_bytes()),'object_count':len(objects),'source_closure_sha256':manifest.get('closure_sha256')}
elif a.build_dir:build=a.build_dir.resolve();archive=build/'libloom_core.a'
else:raise SystemExit('native_build_or_manifest_required')
artifact_path=research/'docs/research/thread7_real_2026-10-09/continuation_01/handoff-final-artifact-v3.json.gz'
artifact=json.loads(gzip.decompress(artifact_path.read_bytes()));packet=artifact['packet'];contract=artifact['contract']
profile={'vocabulary':contract['vocabulary'],**{k:packet[k] for k in ['entities','claims','sources']},'selection':{'members':[{'method_version_id':contract['bindings']['method_version_id']}],'parameter_layers':['method','member','selection','user']}}
a.output.parent.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='audit-c4-registry-',dir='/var/tmp') as td:
 d=Path(td);fixture=d/'input.json';fixture.write_text(json.dumps({'profile':profile,'packet':packet}));binary=d/'probe'
 command=['g++','-std=c++20','-O0','-g0','-pthread','-I'+str(repo/'loom/include'),'-I'+str(repo/'loom/src'),'-I'+str(repo/'loom/third_party/nlohmann'),'-I'+str(repo/'loom/third_party/cpp-httplib'),str(source),*objects,str(archive),str(build/'libloom_miniz.a'),str(build/'libloom_sqlite3_amalgamation.a'),'-lssl','-lcrypto','-pthread','-ldl','-lm','-o',str(binary)]
 compile_result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 a.output.with_suffix('.compile.log').write_text(compile_result.stdout)
 if compile_result.returncode:raise SystemExit('registry_probe_compile_failed')
 run=subprocess.run([str(binary),str(fixture)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 a.output.with_suffix('.runtime.log').write_text(run.stderr)
 results=json.loads(run.stdout)
receipt={'schema':'klb.audit.pass4.c-registry/1','native_sha':a.sha,'research_sha':a.research_sha,
 'artifact_sha256':sha(artifact_path.read_bytes()),'probe_sha256':sha(source.read_bytes()),'runner_sha256':sha(Path(__file__).read_bytes()),
 'base_archive_sha256':sha(archive.read_bytes()),'attestation':attestation,'compile_exit':compile_result.returncode,'run_exit':run.returncode,
 'transport':'ScriptedTransport with zero expected replies; workers disabled; no socket backend',**results}
receipt['counts']={s:sum(t['status']==s for t in results['tests']) for s in ['PASS','FAIL','BLOCKED']}
a.output.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'output':str(a.output),'counts':receipt['counts']}));raise SystemExit(run.returncode)
