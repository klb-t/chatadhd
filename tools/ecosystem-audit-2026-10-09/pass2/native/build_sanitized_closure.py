#!/usr/bin/env python3
"""Build only the real native source closure linked by the audit transport.
The regular link map determines the closure; duplicate archive member basenames
are conservatively expanded to every matching source. No unsanitized core object
is reused. Requires an already-configured ASan+UBSan CMake build.
"""
import argparse, hashlib, json, pathlib, re, shutil, signal, subprocess, time
p=argparse.ArgumentParser();p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--sanitizer-build',type=pathlib.Path,required=True);p.add_argument('--regular-link-map',type=pathlib.Path,required=True);p.add_argument('--output-build',type=pathlib.Path,required=True);p.add_argument('--jobs',type=int,default=2);p.add_argument('--timeout',type=int,default=540);a=p.parse_args()
a.repo=a.repo.resolve();a.sanitizer_build=a.sanitizer_build.resolve();a.output_build=a.output_build.resolve();a.output_build.mkdir(parents=True,exist_ok=True)
subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',a.sha,'--','loom/src','loom/include','loom/data','loom/third_party'],check=True,stdout=subprocess.PIPE)
cache=(a.sanitizer_build/'CMakeCache.txt').read_text()
for flag in ('CMAKE_C_FLAGS:STRING=','CMAKE_CXX_FLAGS:STRING='):
 line=next(x for x in cache.splitlines() if x.startswith(flag));assert '-fsanitize=address,undefined' in line,flag+' must instrument actual sources'
ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:'))
header=a.regular_link_map.read_text().split('Discarded input sections')[0];names=set(re.findall(r'libloom_core\.a\(([^)]+)\)',header))
assert names,'No core closure in linker map'
commands=json.loads((a.sanitizer_build/'compile_commands.json').read_text());selected=[]
for c in commands:
 source=pathlib.Path(c['file'])
 if source.name+'.o' in names:
  match=re.search(r' -o ([^ ]+) -c ',c['command']);assert match,'Unsupported command format'
  selected.append({'source':str(source.relative_to(a.repo)),'object':match[1],'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'compile_command':c['command']})
assert names <= {pathlib.Path(x['source']).name+'.o' for x in selected}
cmd=[ninja,'-j',str(a.jobs),*[x['object'] for x in selected],'loom_sqlite3_amalgamation','loom_miniz']
start=time.monotonic();log=a.output_build/'build.log'
with log.open('w') as out:
 process=subprocess.Popen(cmd,cwd=a.sanitizer_build,stdout=out,stderr=subprocess.STDOUT)
 try:rc=process.wait(timeout=a.timeout)
 except subprocess.TimeoutExpired:
  process.send_signal(signal.SIGINT);rc=process.wait(timeout=60)
manifest={'schema':'ecosystem-audit.sanitized-closure/1','repo':'klb-t/chatadhd','sha':a.sha,'link_map_sha256':hashlib.sha256(a.regular_link_map.read_bytes()).hexdigest(),'scope':'instrumented selected actual loom_core source closure, vendored SQLite/miniz, and separately instrumented audit transport; not all core modules or full unit suite','selection':'all source objects matching linked archive-member basenames (conservative for duplicate store/profile/schema/evidence names)','source_files':selected,'member_names':sorted(names),'command':cmd,'seconds':time.monotonic()-start,'returncode':rc}
manifest['vendored_c_sources']=[{'source':str(pathlib.Path(c['file']).relative_to(a.repo)),'source_sha256':hashlib.sha256(pathlib.Path(c['file']).read_bytes()).hexdigest(),'compile_command':c['command']} for c in commands if pathlib.Path(c['file']).name in ('sqlite3.c','miniz.c')]
if rc==0:
 objects=[a.sanitizer_build/x['object'] for x in selected]
 for x in objects:
  assert x.open('rb').read(4)==b'\x7fELF','Corrupt object '+str(x)
 archive=a.output_build/'libloom_core.a'
 if archive.exists():archive.unlink()
 subprocess.run(['ar','qc',str(archive),*[str(x) for x in objects]],check=True);subprocess.run(['ranlib',str(archive)],check=True)
 for name in ('libloom_sqlite3_amalgamation.a','libloom_miniz.a','CMakeCache.txt','compile_commands.json'):shutil.copyfile(a.sanitizer_build/name,a.output_build/name)
 manifest['libraries_sha256']={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in a.output_build.glob('*.a')}
(a.output_build/'closure-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'source_files':len(selected),'returncode':rc,'seconds':manifest['seconds']}))
raise SystemExit(rc)
