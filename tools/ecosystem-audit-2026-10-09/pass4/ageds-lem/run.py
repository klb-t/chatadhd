#!/usr/bin/env python3
"""PASS4 actual-consumer host probes; A reproduction is not B acceptance."""
import argparse,collections,hashlib,json,pathlib,subprocess,sys
BASE=pathlib.Path(__file__).resolve().parent
OLD=BASE.parents[1]/'pass2'/'lem'
LEM=['api/GeminiApiService.kt','api/ResearchAgent.kt','api/ApiKeyStore.kt','research/ExperimentRunner.kt','viewmodel/ResearchViewModel.kt','data/repository/ResearchRepository.kt','data/local/AppDatabase.kt','data/local/ConfigDao.kt','data/local/ExperimentDao.kt','data/local/LedgerDao.kt','data/model/ExperimentConfig.kt','data/model/ExperimentResult.kt','data/model/LedgerEntry.kt']
AGEDS=['core/src/commonMain/kotlin/dev/klbt/ageds/core/Models.kt','core/src/commonMain/kotlin/dev/klbt/ageds/core/CitationSelection.kt','androidApp/src/main/java/dev/klbt/ageds/CitationDisplayProjection.kt','androidApp/src/main/java/dev/klbt/ageds/RangePlaybackController.kt']
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--suite',choices=['lem','ageds-kotlin','ageds-js'],required=True);p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--compiler-dir',type=pathlib.Path);p.add_argument('--deps-dir',type=pathlib.Path);p.add_argument('--phase',choices=['all','reproduction','acceptance'],default='all');a=p.parse_args()
 a.out=a.out.resolve()
 if a.out.exists() and any(a.out.iterdir()):p.error("--out must be a new or empty directory; historical receipts are immutable")
 a.out.mkdir(parents=True,exist_ok=True)
 sha=subprocess.check_output(['git','-C',str(a.repo),'rev-parse',a.sha+'^{commit}'],text=True).strip()
 paths=['app/src/main/java/com/example/'+x for x in LEM] if a.suite=='lem' else AGEDS if a.suite=='ageds-kotlin' else ['server/app/static/citations.mjs','server/app/static/range-player.mjs']
 manifest=[];src=a.out/'source';src.mkdir(exist_ok=True);sources=[]
 for path in paths:
  data=subprocess.check_output(['git','-C',str(a.repo),'show',sha+':'+path]);dest=src/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data);sources.append(dest);manifest.append({'path':path,'sha256':hashlib.sha256(data).hexdigest()})
 receipt={'schema':'klbt.audit.pass4.host/1','suite':a.suite,'sha':sha,'sources':manifest}
 if a.suite=='ageds-js':cmd=['node',str(BASE/'ageds.mjs'),str(src)]
 else:
  if not a.compiler_dir or not a.deps_dir:p.error('--compiler-dir and --deps-dir required for Kotlin')
  compiler=sorted(a.compiler_dir.resolve().glob('*.jar'));deps=sorted(a.deps_dir.resolve().glob('*.jar'))+list(a.compiler_dir.resolve().glob('kotlin-stdlib-*.jar'))+list(a.compiler_dir.resolve().glob('annotations-*.jar'))
  if a.suite=='ageds-kotlin':deps=[x for x in deps if x.name.startswith(('kotlinx-serialization-core','kotlinx-serialization-json','kotlin-stdlib','annotations-'))]
  cp=':'.join(map(str,deps));extra=sorted((OLD/'fixtures').glob('*.kt')) if a.suite=='lem' else []
  sources+=extra+[BASE/('LemHarness.kt' if a.suite=='lem' else 'AgedsHarness.kt')]
  compilecmd=['java','-cp',':'.join(map(str,compiler)),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-jvm-target','17','-classpath',cp,*map(str,sources),'-d',str(a.out/'host.jar')]
  c=subprocess.run(compilecmd,text=True,capture_output=True,timeout=120);(a.out/'compile.log').write_text(c.stdout+c.stderr);receipt['compile_exit_code']=c.returncode;receipt['dependencies']=[{'name':x.name,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in deps];receipt['compiler']=[{'name':x.name,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in compiler]
  if c.returncode:receipt['status']='BLOCKED_COMPILE';(a.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(c.stderr);return 2
  cmd=['java','-Djava.security.manager=allow','-cp',str(a.out/'host.jar')+':'+cp,'audit.lem.LemHarnessKt' if a.suite=='lem' else 'audit.ageds.AgedsHarnessKt']
 r=subprocess.run(cmd,text=True,capture_output=True,timeout=60);receipt['run_exit_code']=r.returncode;(a.out/'run.stderr.log').write_text(r.stderr)
 if r.returncode:receipt['status']='BLOCKED_RUN';(a.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(r.stderr);return 2
 result=json.loads(r.stdout);(a.out/'results.json').write_text(json.dumps(result,indent=2)+'\n');receipt['counts']={p:dict(collections.Counter(t['status'] for t in result['tests'] if t['phase']==p)) for p in ['A','B','contract','control']};receipt['status']='EXECUTED';(a.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt['counts']))
 wanted={'A'} if a.phase=='reproduction' else {'B','contract','control'} if a.phase=='acceptance' else {'A','B','contract','control'}
 return int(any(t['phase'] in wanted and t['status']!='PASS' for t in result['tests']))
if __name__=='__main__':sys.exit(main())
