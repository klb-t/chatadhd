#!/usr/bin/env python3
"""PASS4 actual CKP capture consumers + verbatim delayed-paste methods.
No product write; transport replaced at suspend boundary; all sockets blocked.
"""
import argparse,hashlib,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
BASE="app/src/main/java/com/example/"
def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--deps',type=Path,required=True);p.add_argument('--workdir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--phase',choices=['all','reproduction','acceptance','contract'],default='all');a=p.parse_args()
 sha=subprocess.check_output(['git','-C',str(a.repo),'rev-parse',a.sha+'^{commit}'],text=True).strip()
 a.workdir.mkdir(parents=True,exist_ok=True);a.out.parent.mkdir(parents=True,exist_ok=True)
 sources=a.workdir/'sources';sources.mkdir(exist_ok=True);manifest=[]
 def src(rel):return subprocess.check_output(['git','-C',str(a.repo),'show',sha+':'+BASE+rel],text=True)
 def record(rel,text,start,end,kind):manifest.append(dict(path=BASE+rel,line_start=start,line_end=end,kind=kind,sha256=hashlib.sha256(text.encode()).hexdigest()))
 for rel in ['core/capture/ConversationCapture.kt','core/capture/CaptureSession.kt','core/capture/TreeObservation.kt']:
  s=src(rel);record(rel,s,1,len(s.splitlines()),'unmodified_whole_source');(sources/Path(rel).name).write_text(s)
 s=src('ime/CustomKeyboardIme.kt');shell=(ROOT/'PasteHost.kt.in').read_text()
 for name in ['pasteOrConvert','pasteNativeClip','refreshBusyLamp']:
  m=re.search(r'^    private fun '+name+r'\([^\n]*[\s\S]*?^    }',s,re.M)
  if not m:raise RuntimeError('Real consumer seam absent: '+name)
  t=m.group();record('ime/CustomKeyboardIme.kt',t,s[:m.start()].count('\n')+1,s[:m.end()].count('\n')+1,'verbatim_method_in_platform_shell');shell=shell.replace('/*'+name+'*/',t)
 s=src('ime/EditorController.kt')
 m=re.search(r'^    fun commitText\([^\n]*[\s\S]*?^    }',s,re.M)
 if not m:raise RuntimeError('Real editor seam absent: commitText')
 t=m.group();record('ime/EditorController.kt',t,s[:m.start()].count('\n')+1,s[:m.end()].count('\n')+1,'verbatim_method_in_platform_shell');shell=shell.replace('/*commitText*/',t)
 (sources/'PasteHost.kt').write_text(shell);(sources/'Probe.kt').write_bytes((ROOT/'Probe.kt').read_bytes())
 jars=sorted(a.deps.resolve().glob('*.jar'));cp=os.pathsep.join(map(str,jars));jar=a.workdir/'audit.jar'
 command=['java','-Xmx512m','-cp',cp,'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-classpath',cp,*map(str,sorted(sources.glob('*.kt'))),'-d',str(jar)]
 result=subprocess.run(command,capture_output=True,text=True);(a.out.parent/'compile.log').write_text(result.stdout+result.stderr)
 if result.returncode:return 2
 result=subprocess.run(['java','-cp',str(jar.resolve())+os.pathsep+cp,'ProbeKt',a.phase],capture_output=True,text=True)
 (a.out.parent/'runtime-stderr.log').write_text(result.stderr)
 cases=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
 receipt=dict(repo='klb-t/Custom-Keyboard-Pro',sha=sha,phase=a.phase,level='Kotlin/JVM host: whole real capture sources and verbatim paste consumer methods; platform/transport doubles; no Android/device E2E',source_manifest=manifest,dependencies=[dict(name=j.name,sha256=hashlib.sha256(j.read_bytes()).hexdigest()) for j in jars],transport='SecurityManager rejects all connect/listen/accept; paste conversion suspended recording double',exit_code=result.returncode,cases=cases,counts={s:sum(c['status']==s for c in cases) for s in ['PASS','FAIL']})
 a.out.write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n');print(json.dumps(receipt['counts']));return result.returncode
if __name__=='__main__':sys.exit(main())
