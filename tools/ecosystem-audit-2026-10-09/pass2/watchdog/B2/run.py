#!/usr/bin/env python3
"""Rebuild the complete six-TU B2 header/data dependency delta; reuse unchanged B core.

Requires an independently built baseline core and its Ninja dependency log.
Does not invoke CMake, full builds, network, product edits or author's tests.
"""
import argparse, hashlib, json, pathlib, shlex, subprocess, sys

def digest(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--base-repo',type=pathlib.Path,required=True);p.add_argument('--base-sha',required=True);p.add_argument('--base-build',type=pathlib.Path,required=True);p.add_argument('--scratch',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--reuse-objects',type=pathlib.Path);a=p.parse_args()
 for k in ['repo','base_repo','base_build','scratch','out']:setattr(a,k,getattr(a,k).resolve())
 a.scratch.mkdir(parents=True,exist_ok=True);a.out.parent.mkdir(parents=True,exist_ok=True)
 def git(repo,*args):return subprocess.check_output(['git','-C',str(repo),*args],text=True).strip()
 for repo,sha in [(a.repo,a.sha),(a.base_repo,a.base_sha)]:
  assert git(repo,'rev-parse','HEAD')==sha,'SHA mismatch'
  subprocess.run(['git','-C',str(repo),'diff','--exit-code',sha,'--','loom/src','loom/include','loom/data','loom/third_party','loom/CMakeLists.txt'],check=True,stdout=subprocess.DEVNULL)
 changed=git(a.repo,'diff','--name-only',a.base_sha,a.sha,'--','loom/src','loom/include','loom/data','loom/third_party','loom/CMakeLists.txt').splitlines()
 expected={'loom/src/onboarding/README.md','loom/src/onboarding/graph.cpp','loom/src/onboarding/builtin.inc','loom/include/loom/onboarding_layers.h','loom/data/onboarding/ui.pack','loom/data/profiles/user.pack'}
 assert set(changed)==expected,('Unexpected delta; update complete dependency analysis',changed)
 cache=(a.base_build/'CMakeCache.txt').read_text().splitlines();ninja=next(x.split('=',1)[1] for x in cache if x.startswith('CMAKE_MAKE_PROGRAM:'))
 deps=subprocess.check_output([ninja,'-t','deps'],cwd=a.base_build,text=True)
 targets=[x.splitlines()[0].split(': #deps')[0] for x in deps.split('\n\n') if 'loom_core.dir/' in x and ('/onboarding_layers.h' in x or '/onboarding/builtin.inc' in x)]
 assert len(targets)==6,('unexpected affected-TU closure',targets)
 compile_db=json.loads((a.base_build/'compile_commands.json').read_text());commands=[];objects=[];tu_hashes={}
 def run(cmd,name,timeout=180):
  commands.append(cmd);r=subprocess.run(cmd,capture_output=True,text=True,timeout=timeout)
  (a.scratch/(name+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(f'{name} failed: {r.stderr[-2500:]}')
  return r.stdout
 reused=json.loads(a.reuse_objects.read_text()) if a.reuse_objects else None
 if reused:assert reused['sha']==a.sha and reused['base_sha']==a.base_sha
 object_manifest=[]
 for target in targets:
  row=next(x for x in compile_db if target in x['command']);cmd=shlex.split(row['command']);src=pathlib.Path(row['file']).relative_to(a.base_repo);out=a.scratch/(str(src).replace('/','_')+'.o')
  cmd=[x.replace(str(a.base_repo),str(a.repo)) for x in cmd];cmd[cmd.index('-o')+1]=str(out);cmd=['-g0' if x=='-g' else x for x in cmd]
  source_hash=digest(a.repo/src)
  if reused:
   item=next(x for x in reused['objects'] if x['source']==str(src));assert item['source_sha256']==source_hash and digest(item['path'])==item['object_sha256'];out=pathlib.Path(item['path']);commands.append({'reused_verified_object':str(out),'original_compile_command':item['command']})
  else:run(cmd,out.stem)
  objects.append(str(out));tu_hashes[str(src)]=source_hash;object_manifest.append({'source':str(src),'source_sha256':source_hash,'path':str(out),'object_sha256':digest(out),'command':item['command'] if reused else cmd})
 manifest={'sha':a.sha,'base_sha':a.base_sha,'objects':object_manifest};(a.scratch/'object-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 here=pathlib.Path(__file__).resolve().parent;inc=[f'-I{a.repo}/loom/{x}' for x in ['include','third_party/nlohmann','third_party/sqlite']]
 libs=[str(a.base_build/x) for x in ['libloom_core.a','libloom_sqlite3_amalgamation.a','libloom_miniz.a']]
 common=['c++','-std=c++20','-O0','-g0',*inc,str(here/'consumer.cpp')]
 tail=['-lssl','-lcrypto','-pthread','-ldl','-lm','-Wl,--no-keep-memory']
 old=a.scratch/'baseline';new=a.scratch/'B2';linkmap=a.scratch/'B2-link.map'
 run([*common,*libs,*tail,'-o',str(old)],'link-baseline')
 run([*common,*objects,*libs,*tail,'-Wl,-Map='+str(linkmap),'-o',str(new)],'link-B2')
 # Selected archive members at the start of the map must exclude replaced objects.
 selected=linkmap.read_text().split('Allocating common symbols')[0].split('Discarded input sections')[0]
 for basename in ['chat_engine.cpp.o','context_engine.cpp.o','layers.cpp.o','graph.cpp.o','runtime_adapter.cpp.o']:
  assert f'libloom_core.a({basename})' not in selected,('old replaced member extracted',basename)
 # Archive basenames collide: kb/store.cpp and onboarding/store.cpp are both
 # store.cpp.o. The needed KnowledgeStore member must not be mistaken for the
 # replaced OnboardingStore TU. Linker-selected reference names disambiguate it.
 selected_lines=selected.splitlines()
 store_refs=[selected_lines[i+1] for i,x in enumerate(selected_lines[:-1]) if x.endswith('libloom_core.a(store.cpp.o)')]
 assert store_refs and all('(loom::kb::' in x for x in store_refs),('unexpected store.cpp member',store_refs)
 database=a.scratch/'upgrade.sqlite'
 if database.exists():raise RuntimeError('Use a new scratch directory; historical fixture is not overwritten')
 seed=json.loads(run([str(old),'seed',str(database)],'seed-native-baseline',60));result=json.loads(run([str(new),'verify',str(database)],'run-native-B2',60))
 cases=seed['cases']+result['cases'];counts={s:sum(c['status']==s for c in cases) for s in ['PASS','FAIL']}
 receipt={'schema':'klbt.audit.receipt/2','repo':'klb-t/chatadhd','sha':a.sha,'base_sha':a.base_sha,'suite':'B2-presentation-consumers','assembly':'All six affected translation units from Ninja header/generated-include dependency closure compiled from B2 and linked before unchanged B core. Archive selection map verifies no replaced old objects were extracted. This is a focused assembly, not a full B2 build or CTest run.','baseline_library_precondition':'baseline core independently built at base_sha; source equality and artifact hashes recorded; existing parent native build receipt remains provenance for that original build','changed_product_files':changed,'affected_targets':targets,'translation_unit_hashes':tu_hashes,'data_header_hashes':{x:digest(a.repo/x) for x in changed if not x.endswith('README.md')},'libraries':{str(pathlib.Path(x).name):digest(x) for x in libs},'commands':commands,'driver_sha256':digest(here/'consumer.cpp'),'runner_sha256':digest(here/'run.py'),'linkmap_sha256':digest(linkmap),'cases':cases,'counts':counts,'network':'No transport or runtime worker instantiated; tests only local real OnboardingStore, DefaultLayers, project_graph and SQLite/KnowledgeStore.'}
 a.out.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(counts));return int(counts['FAIL']>0)
if __name__=='__main__':sys.exit(main())
