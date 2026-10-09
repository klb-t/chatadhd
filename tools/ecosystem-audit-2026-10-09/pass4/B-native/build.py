#!/usr/bin/env python3
"""Selective native build against a pinned checkout; reuses verified earlier base archive.
Not a full CMake build. Preserves commands, all changed/dependent object hashes,
archive/shared-library hashes and actual linker selected-member closure.
"""
import argparse,hashlib,json,pathlib,shlex,subprocess
p=argparse.ArgumentParser();p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--base-build',type=pathlib.Path,required=True);p.add_argument('--prior-manifest',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
a.repo=a.repo.resolve();a.base_build=a.base_build.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
def h(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
assert subprocess.check_output(['git','-C',str(a.repo),'rev-parse','HEAD'],text=True).strip()==a.sha
subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',a.sha,'--','loom/src','loom/include','loom/data'],check=True)
prior=json.loads(a.prior_manifest.read_text());objects=prior['objects'][:]
# Refuse a future checkout whose native changes are outside this demonstrated
# selective dependency closure. A broader product fix needs a fresh full build
# or an independently expanded/reviewed closure, never stale archive success.
known_cpp={x['source'] for x in objects}|{'loom/src/catalog/query.cpp','loom/src/runtime.cpp','loom/src/model/runtime_profile.cpp'}
known_other={'loom/include/loom/catalog.h','loom/include/loom/onboarding_layers.h','loom/src/model/runtime_profiles_embedded.inc','loom/src/onboarding/builtin.inc','loom/src/onboarding/README.md'}
changed=subprocess.check_output(['git','-C',str(a.repo),'diff','--name-only',prior['base_sha'],a.sha,'--','loom/src','loom/include'],text=True).splitlines()
dependency_pins={'loom/include/loom/catalog.h': '13d440fb7f726c219800c1f98a01dd8b5ce4acb4038a07938dd95cdb2a561c05', 'loom/include/loom/onboarding_layers.h': '8c954a5b47ab0d9c156966284c2f4e23298b14e7586ae7ce88b3b165ba5cdff0', 'loom/src/onboarding/builtin.inc': '2f2f23e990f7a03b98231586a620002bc83ca20eea7cc228164121db51745b35'}
for path,digest in dependency_pins.items():
 if h(a.repo/path)!=digest:raise SystemExit('BLOCKED changed dependent header/embedding: '+path+'; full build required')
uncovered=set(changed)-known_cpp-known_other
if uncovered:raise SystemExit('BLOCKED selective dependency closure; full build required for: '+', '.join(sorted(uncovered)))

for x in objects:
 assert h(a.repo/x['source'])==x['source_sha256'];assert h(x['path'])==x['object_sha256']
commands=json.loads((a.base_build/'compile_commands.json').read_text())
for rel in ['loom/src/catalog/query.cpp','loom/src/runtime.cpp','loom/src/model/runtime_profile.cpp']:
 row=next(c for c in commands if c['file'].endswith('/'+rel));base_repo=row['file'][:-len(rel)-1]
 command=[x.replace(base_repo,str(a.repo)) for x in shlex.split(row['command'])]
 command=['-g0' if x=='-g' or x.startswith('-g1') else x for x in command]
 dest=a.out/(rel.replace('/','_')+'.o');command[command.index('-o')+1]=str(dest)
 result=subprocess.run(command,capture_output=True,text=True);(a.out/(dest.name+'.log')).write_text(result.stdout+result.stderr)
 if result.returncode:raise SystemExit('compile failed: '+rel)
 objects.append({'source':rel,'source_sha256':h(a.repo/rel),'path':str(dest),'object_sha256':h(dest),'command':command})
core=a.base_build/'libloom_core.a';libs=[str(core),str(a.base_build/'libloom_sqlite3_amalgamation.a'),str(a.base_build/'libloom_miniz.a'),'-lssl','-lcrypto','-pthread','-ldl','-lm']
exports=[line.split()[-1] for line in subprocess.check_output(['nm','-D','--defined-only',str(a.base_build/'libloom.so')],text=True).splitlines() if line.split()[-1].startswith('loom_')]
shared=a.out/'libloom.so';mapfile=a.out/'libloom.map'
command=['c++','-shared','-Wl,--no-keep-memory','-Wl,--strip-debug','-o',str(shared)]+['-Wl,-u,'+x for x in exports]+[x['path'] for x in objects]+libs+['-Wl,-Map='+str(mapfile)]
r=subprocess.run(command,capture_output=True,text=True);(a.out/'link.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit('shared link failed')
closure=mapfile.read_text().split('Discarded input sections')[0];(a.out/'link-closure.txt').write_text(closure);mapfile.unlink()
# Every replaced member except duplicate basename store.cpp must be absent.
for x in objects:
 name=pathlib.Path(x['source']).name+'.o'
 if name!='store.cpp.o':assert 'libloom_core.a('+name+')' not in closure, name
manifest={'sha':a.sha,'base_sha':prior['base_sha'],'base_archive':str(core),'base_archive_sha256':h(core),'objects':objects,'shared_library':str(shared),'shared_library_sha256':h(shared),'shared_link_command':command,'generated_embedding_sha256':h(a.repo/'loom/src/model/runtime_profiles_embedded.inc'),'closure_sha256':h(a.out/'link-closure.txt'),'scope':'three B4 native translation units + six verified B2 objects before unchanged B base archive; full CMake/CTest not claimed'}
(a.out/'object-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'shared':str(shared),'sha256':h(shared),'objects':len(objects)}))
