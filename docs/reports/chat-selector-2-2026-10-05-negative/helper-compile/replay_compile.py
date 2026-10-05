#!/usr/bin/env python3
"""Reproduce the preserved test-only compile failure offline."""
import argparse, hashlib, json, pathlib, subprocess
p=argparse.ArgumentParser(); p.add_argument('--repo',required=True,type=pathlib.Path); p.add_argument('--output',required=True,type=pathlib.Path); a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent; repo=a.repo.resolve(); out=a.output.resolve(); out.mkdir(parents=True,exist_ok=False)
m=json.loads((root/'manifest.json').read_text()); verified={}
for name,digest in m['source_sha256'].items():
 f=root/'source/checkout'/name; actual=hashlib.sha256(f.read_bytes()).hexdigest()
 if actual!=digest: raise SystemExit('source differs: '+name)
 verified[name]=actual
for info in m['dependencies'].values():
 for name,digest in info['files'].items():
  if hashlib.sha256((root/'source'/name).read_bytes()).hexdigest()!=digest: raise SystemExit('dependency differs: '+name)
fixture=root/'source/checkout/loom/src/context/tests/runtime_preset_test.cpp'
command=['c++','-std=c++20','-O0','-g0','-Wall','-Wextra','-Werror','-DJSON_USE_IMPLICIT_CONVERSIONS=1','-DLOOM_RUNTIME_PRESET_TEST_MAIN=1','-I',str(root/'source/loom/include'),'-I',str(repo/'loom/include'),'-I',str(root/'source/checkout/loom/src'),'-I',str(root/'source/loom/src'),'-I',str(repo/'loom/src'),'-isystem',str(repo/'loom/third_party/nlohmann'),'-isystem',str(repo/'loom/third_party/doctest'),'-c',str(fixture),'-o',str(out/'fixture.o')]
r=subprocess.run(command,capture_output=True,text=True); log=r.stdout+r.stderr; (out/'compile.log').write_text(log)
matched=r.returncode!=0 and 'authoritative["hash"]' in log and 'operator==' in log
(out/'receipt.json').write_text(json.dumps({'schema':'loom.negative_fixture_replay/1','command':command,'verified_checkout_sources':verified,'exit_code':r.returncode,'expected_compile_failure':matched,'provider_calls':0,'paid_calls':0},indent=2)+'\n')
print('expected compile failure reproduced' if matched else 'unexpected result')
raise SystemExit(0 if matched else 1)
