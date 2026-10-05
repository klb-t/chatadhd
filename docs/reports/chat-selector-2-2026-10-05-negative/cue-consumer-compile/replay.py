#!/usr/bin/env python3
import argparse,hashlib,json,pathlib,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent;repo=a.repo.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
old=json.loads((root/'command.json').read_text());fixture=root/'failed-fixture.cpp'
if hashlib.sha256(fixture.read_bytes()).hexdigest()!=old['source_sha256']:raise SystemExit('failing fixture changed')
context=out/'source/context';(context/'tests').mkdir(parents=True);source=context/'tests/goal_cues_configuration.cpp';shutil.copy2(fixture,source);shutil.copy2(root/'source/context_execution.h',context/'context_execution.h')
cmd=[]
for item in old['argv']:
 if item.endswith('/fixture.o'):cmd.append(str(out/'fixture.o'))
 elif item.endswith('/goal_cues_configuration.cpp'):cmd.append(str(source))
 elif '/tmp/w3-selector-2-baseline/loom/' in item:cmd.append(item.replace('/tmp/w3-selector-2-baseline/loom/',str(repo/'loom')+'/'))
 else:cmd.append(item)
r=subprocess.run(cmd,capture_output=True,text=True);text=r.stdout+r.stderr;(out/'compile.log').write_text(text);matched=r.returncode!=0 and 'fallback_goal_type' in text and 'operator==' in text
(out/'receipt.json').write_text(json.dumps({'schema':'loom.negative_fixture_replay/1','command':cmd,'source_sha256':old['source_sha256'],'private_header_sha256':hashlib.sha256((root/'source/context_execution.h').read_bytes()).hexdigest(),'exit_code':r.returncode,'expected_compile_failure':matched,'provider_calls':0,'paid_calls':0},indent=2)+'\n');print('expected compile failure reproduced' if matched else 'unexpected result');raise SystemExit(0 if matched else 1)
