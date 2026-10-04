import hashlib,json,os,pathlib,subprocess,time
repo=pathlib.Path('/workspace/scratch/943af489e102/chatadhd')
out=pathlib.Path('/workspace/scratch/943af489e102/bench/screenshot-focused-2026-10-04')
out.mkdir(parents=True,exist_ok=True)
old=pathlib.Path('/dev/shm/chatadhd-thread5/baseline-build')
libs=[old/'libloom_core.a',old/'libloom_sqlite3_amalgamation.a',old/'libloom_miniz.a']
sources=[repo/'loom/tests/main.cpp',repo/'loom/tests/test_import_screenshot.cpp',repo/'loom/src/import/importer_core.cpp',repo/'loom/src/import/importer_text.cpp']
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  while b:=f.read(1<<20):h.update(b)
 return h.hexdigest()
receipt={'description':'Supplementary derivative only: historical core archive plus new test; green additionally overrides current importer_core/importer_text translation units. Not full mixed-main build or CTest proof.','inputs_before':{str(p):sha(p) for p in sources+libs},'commands':[]}
flags=['c++','-std=c++20','-O0','-DNDEBUG','-Wall','-Wextra','-Wpedantic','-Wshadow','-Wnon-virtual-dtor','-Wold-style-cast','-Wcast-align','-Woverloaded-virtual','-Wnull-dereference','-Wimplicit-fallthrough','-Wno-unused-parameter','-Werror','-DJSON_USE_IMPLICIT_CONVERSIONS=1','-DLOOM_HAVE_OPENSSL=1','-DLOOM_VENDORED_SQLITE=1','-Iloom/include','-Iloom/src','-Iloom/tests','-Iloom/third_party/nlohmann','-Iloom/third_party/sqlite','-Iloom/third_party/doctest']
def run(label,args,env=None):
 print(label,flush=True)
 start=time.monotonic()
 with (out/(label+'.stdout')).open('wb') as stdout, (out/(label+'.stderr')).open('wb') as stderr:
  result=subprocess.run(args,cwd=repo,env=env,stdout=stdout,stderr=stderr)
 item={'label':label,'args':[str(x) for x in args],'exit_code':result.returncode,'seconds':time.monotonic()-start}
 receipt['commands'].append(item)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(label,'exit',result.returncode,'seconds',round(item['seconds'],3),flush=True)
 return result.returncode
objs=[]
for source in sources:
 obj=out/(source.stem+'.o');objs.append(obj)
 if run('compile-'+source.stem,flags+['-c',str(source),'-o',str(obj)]):raise SystemExit(1)
linklibs=[str(x) for x in libs]+['-lssl','-lcrypto','-ldl','-lm','-pthread']
red=out/'screenshot-red';green=out/'screenshot-green'
if run('link-red',['c++',str(objs[0]),str(objs[1])]+linklibs+['-o',str(red)]):raise SystemExit(1)
if run('link-green',['c++']+[str(x) for x in objs]+linklibs+['-o',str(green)]):raise SystemExit(1)
env=os.environ.copy();env.pop('PYTHONPATH',None);env.pop('TMPDIR',None)
run('run-red',[str(red),'--test-suite=import_screenshot','--no-intro=true'],env)
rc=run('run-green',[str(green),'--test-suite=import_screenshot','--no-intro=true'],env)
receipt['inputs_after']={str(p):sha(p) for p in sources+libs}
receipt['source_stable']=receipt['inputs_before']==receipt['inputs_after']
receipt['binaries']={str(p):sha(p) for p in [red,green]}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'out':str(out),'green_exit':rc,'source_stable':receipt['source_stable']}),flush=True)
raise SystemExit(rc)
