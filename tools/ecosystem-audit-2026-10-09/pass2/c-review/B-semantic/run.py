#!/usr/bin/env python3
"""Compile the independent C++ caller against a pinned existing libloom_core.

No product source is generated/edited; no paid model is called. The executable
uses Runtime with actual ScriptedTransport and start_workers=false.
"""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True)
p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--scratch',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=a.repo.resolve();build=a.build_dir.resolve();scratch=a.scratch.resolve();scratch.mkdir(parents=True,exist_ok=True)
sha=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
if sha!=a.sha:raise SystemExit('checkout_sha_mismatch')
source=Path(__file__).with_name('probe.cpp').resolve();binary=scratch/'probe'
cmd=['g++','-std=c++20','-O0','-I'+str(repo/'loom/include'),'-I'+str(repo/'loom/third_party/nlohmann'),
     '-I'+str(repo/'loom/third_party/sqlite'),str(source),str(build/'libloom_core.a'),
     str(build/'libloom_sqlite3_amalgamation.a'),str(build/'libloom_miniz.a'),'-lssl','-lcrypto','-ldl','-lm','-pthread','-o',str(binary)]
compiled=subprocess.run(cmd,capture_output=True,text=True)
(scratch/'compile.log').write_text(compiled.stdout+compiled.stderr)
if compiled.returncode:raise SystemExit('audit_harness_compile_failed; see '+str(scratch/'compile.log'))
with tempfile.TemporaryDirectory(prefix='data-',dir=scratch) as d:
    executed=subprocess.run([str(binary),d],capture_output=True,text=True,timeout=90)
(scratch/'runtime.log').write_text(executed.stderr)
result=json.loads(executed.stdout)
result.update(schema='klb.audit.semantic-consumer/1',sha=sha,repo='klb-t/chatadhd',
    source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    core_library_sha256=hashlib.sha256((build/'libloom_core.a').read_bytes()).hexdigest(),
    compile_exit=compiled.returncode,run_exit=executed.returncode,
    source_binding='Pinned checkout verified; build provenance supplied by native build receipt, library hash recorded.')
result['counts']={s:sum(c['acceptance']==s for c in result['cases']) for s in ['PASS','FAIL']}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'sha':sha,'counts':result['counts'],'output':str(a.output),'setup_error':result.get('setup_error')}))
raise SystemExit(2 if executed.returncode else 1 if result['counts']['FAIL'] else 0)
