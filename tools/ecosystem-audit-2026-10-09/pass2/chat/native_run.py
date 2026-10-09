#!/usr/bin/env python3
"""Compile independent driver against the supplied checkout's actual core."""
import argparse,hashlib,json,pathlib,subprocess,sys,tempfile,sqlite3
def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=pathlib.Path);p.add_argument('--sha',required=True);p.add_argument('--build-dir',required=True,type=pathlib.Path);p.add_argument('--output',required=True,type=pathlib.Path);p.add_argument('--phase',choices=['both','reproduction','acceptance'],default='both');a=p.parse_args()
 a.repo=a.repo.resolve();a.build_dir=a.build_dir.resolve();a.output=a.output.resolve();a.output.parent.mkdir(parents=True,exist_ok=True)
 sha=subprocess.check_output(['git','-C',str(a.repo),'rev-parse','HEAD'],text=True).strip()
 if sha!=a.sha:raise SystemExit('checkout SHA mismatch')
 sources=['loom/src','loom/include','loom/data','loom/third_party','loom/CMakeLists.txt']
 subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',sha,'--',*sources],check=True,stdout=subprocess.DEVNULL)
 cache=(a.build_dir/'CMakeCache.txt').read_text(); source=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_HOME_DIRECTORY:'))
 build_repo=pathlib.Path(source).parent
 subprocess.run(['git','-C',str(build_repo),'diff','--exit-code',sha,'--',*sources],check=True,stdout=subprocess.DEVNULL)
 cmake=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_COMMAND:'))
 freshness=subprocess.run([cmake,'--build',str(a.build_dir),'--target','loom_core','--parallel','1'],capture_output=True,text=True,timeout=300)
 if freshness.returncode: a.output.with_suffix('.freshness.log').write_text(freshness.stdout+freshness.stderr);raise SystemExit('core freshness build failed')
 here=pathlib.Path(__file__).resolve().parent
 with tempfile.TemporaryDirectory(prefix='audit-chat-native-') as tmp:
  exe=pathlib.Path(tmp)/'driver'
  command=['c++','-std=c++20','-O0','-g',*[f'-I{a.repo}/loom/{x}' for x in ['include','third_party/nlohmann','third_party/sqlite']],str(here/'native_consumers.cpp'),*[str(a.build_dir/x) for x in ['libloom_core.a','libloom_sqlite3_amalgamation.a','libloom_miniz.a']],'-lssl','-lcrypto','-pthread','-ldl','-lm','-Wl,--no-keep-memory','-o',str(exe)]
  c=subprocess.run(command,capture_output=True,text=True,timeout=180)
  if c.returncode: a.output.with_suffix('.compile.log').write_text(c.stdout+c.stderr);raise SystemExit('compile failed; see log')
  run=subprocess.run([str(exe),str(pathlib.Path(tmp)/'data')],capture_output=True,text=True,timeout=60)
  obs=json.loads(run.stdout)
  if run.returncode or 'fixture_error' in obs:raise SystemExit(json.dumps(obs))
  with sqlite3.connect(pathlib.Path(tmp)/'data'/'usage-policy.sqlite') as db: obs['chat']['all_usage_operations']=db.execute('SELECT COUNT(*) FROM usage_operations').fetchone()[0]
 rows=[]
 def check(id,phase,ok):
  if a.phase in ('both',phase):rows.append({'id':id,'phase':phase,'status':'PASS' if ok else 'FAIL'})
 chat=obs['chat'];links=obs['graph']['links'];ordinary_sent=chat['send_ok'] and chat['transport_calls']==1
 check('CH-004.scoped-vs-ordinary','reproduction',ordinary_sent and not chat['scoped_calls_authorized'] and chat['body_contains_profile_marker'])
 if a.phase in ('both','acceptance'): rows.append({'id':'CH-004.bound-policy','phase':'acceptance','status':'BLOCKED_MISSING_CONTRACT','reason':'ordinary chat has no user/revision/category binding contract; synthetic memory metadata is not an existing enforced policy API'})
 check('CH-005.ordinary-ledger','reproduction',ordinary_sent and chat['policy_preview_status']=='requires_confirmation' and chat['all_usage_operations']==0)
 if a.phase in ('both','acceptance'): rows.append({'id':'CH-005.bound-admission','phase':'acceptance','status':'BLOCKED_MISSING_CONTRACT','reason':'ordinary chat has no operation/estimate/confirmation binding. Requires matching authorized receipt before transport; absence of a binding is not simulated budget=0.'})
 check('CH-006.unreviewed-relation','reproduction',len(links)==1 and links[0]['metadata']=={})
 check('CH-006.provenance-minimum','acceptance',not links or all(x['metadata'].get('admission') and x['metadata'].get('method_ref') for x in links))
 if a.phase in ('both','acceptance'): rows.append({'id':'CH-006.candidate-ask','phase':'acceptance','status':'BLOCKED_MISSING_CONTRACT','reason':'legacy ingestion has no candidate/ask selection contract; provenance-minimum is necessary only, never proof of valid references or admission'})
 check('CH-M4.actual-config-consumer','acceptance',all(x['ok'] and x['actual_temperature']==x['requested_temperature'] and x['actual_max_tokens']==x['requested_max_tokens'] for x in obs['config_consumption']))
 out={'schema':'klbt.audit.receipt/2','repo':'klb-t/chatadhd','sha':sha,'suite':'chat-native-consumers','scope':'actual native Runtime, OnboardingStore, MemoryEngine, ChatEngine, UsagePolicy, GraphEngine and SQLite. No browser. Profile-field identity copied to synthetic memory with explicit metadata; ordinary memory ignores that binding. No claim of an existing enforced memory policy API.','external_network':'existing ScriptedTransport injected before Runtime construction; all workers off','source_hashes':{x:hashlib.sha256((here/x).read_bytes()).hexdigest() for x in ['native_run.py','native_consumers.cpp']},'library_sha256':hashlib.sha256((a.build_dir/'libloom_core.a').read_bytes()).hexdigest(),'build_source':str(build_repo),'compile_command':command,'cases':rows,'observations':obs}
 out['counts']={s:sum(x['status']==s for x in rows) for s in ['PASS','FAIL','BLOCKED_MISSING_CONTRACT']};a.output.write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out['counts']));return int(out['counts']['FAIL']>0 or out['counts']['BLOCKED_MISSING_CONTRACT']>0)
if __name__=='__main__':sys.exit(main())
