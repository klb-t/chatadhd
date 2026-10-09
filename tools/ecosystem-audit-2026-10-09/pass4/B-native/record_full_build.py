#!/usr/bin/env python3
"""Execute current configured CMake native build, record verifiable source/command provenance.
Configure a separate fresh build directory against the requested checkout first.
No CTest, external services or CI. This attests a completed compiler/build invocation;
it does not claim all behavioral gates or sanitizers.
"""
import argparse,hashlib,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=pathlib.Path);p.add_argument('--sha',required=True);p.add_argument('--build-dir',required=True,type=pathlib.Path);p.add_argument('--out',required=True,type=pathlib.Path);p.add_argument('--jobs',type=int,default=1);a=p.parse_args();repo=a.repo.resolve();b=a.build_dir.resolve();a.out.parent.mkdir(parents=True,exist_ok=True)
def h(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def state():
 assert subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()==a.sha
 subprocess.run(['git','-C',str(repo),'diff','--exit-code',a.sha,'--','loom/src','loom/include','loom/data','loom/third_party'],check=True)
 files=subprocess.check_output(['git','-C',str(repo),'ls-files','loom/src','loom/include','loom/data','loom/third_party','loom/CMakeLists.txt'],text=True).splitlines()
 return {x:h(repo/x) for x in files if (repo/x).is_file()}
cache=(b/'CMakeCache.txt').read_text();home=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_HOME_DIRECTORY:'));assert pathlib.Path(home).resolve()==repo/'loom','CMake source tree mismatch'
commands=json.loads((b/'compile_commands.json').read_text());native=[x for x in commands if '/loom/src/' in x['file']];assert native,'No native compile commands'
assert all(pathlib.Path(x['file']).resolve().is_relative_to(repo/'loom/src') for x in native),'compile_commands source checkout mismatch'
before=state();command=['cmake','--build',str(b),'--target','loom_core','--parallel',str(a.jobs)]
a.out.with_suffix('.intent.json').write_text(json.dumps({'sha':a.sha,'checkout':str(repo),'build_dir':str(b),'command':command,'source_hashes':before},indent=2)+'\n')
r=subprocess.run(command,capture_output=True,text=True);a.out.with_suffix('.log').write_text(r.stdout+r.stderr);after=state();assert before==after,'Source changed during build'
receipt={'sha':a.sha,'checkout':str(repo),'build_dir':str(b),'build_command':command,'build_exit':r.returncode,'source_hashes':after,'compile_commands_sha256':h(b/'compile_commands.json'),'archive_sha256':h(b/'libloom_core.a') if r.returncode==0 else None,'scope':'Actual CMake native core build invocation, not CTest/behavior/sanitizer attestation'}
a.out.write_text(json.dumps(receipt,indent=2)+'\n');raise SystemExit(r.returncode)
