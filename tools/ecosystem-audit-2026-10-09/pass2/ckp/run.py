#!/usr/bin/env python3
"""Compile actual CKP consumers from --repo/--sha; no Android or external traffic.
Whole files stay unmodified. Sliced methods are extracted byte-for-byte into host
shells; infrastructure and effects only are test doubles. See README.md.
"""
import argparse, hashlib, json, os, re, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
BASE='app/src/main/java/com/example/'
FULL=['core/ai/AiTasks.kt','core/discovery/ProviderCatalog.kt','core/discovery/Capabilities.kt','core/discovery/Discovery.kt','core/matrix/Model.kt','core/matrix/Planner.kt','core/matrix/Transforms.kt','core/io/Command.kt','core/matrix/NetLines.kt','engine/NetListener.kt']

def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--deps',type=Path,required=True);p.add_argument('--workdir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--mode',choices=['reproduction','acceptance','all'],default='all');a=p.parse_args()
 sha=subprocess.check_output(['git','-C',str(a.repo),'rev-parse',a.sha+'^{commit}'],text=True).strip();a.workdir.mkdir(parents=True,exist_ok=True);a.out.parent.mkdir(parents=True,exist_ok=True)
 sources=a.workdir/'sources';sources.mkdir(exist_ok=True);manifest=[]
 def source(rel):return subprocess.check_output(['git','-C',str(a.repo),'show',sha+':'+BASE+rel],text=True)
 def record(rel,text,start,end,kind):manifest.append(dict(path=BASE+rel,line_start=start,line_end=end,kind=kind,sha256=hashlib.sha256(text.encode()).hexdigest()))
 def method(rel,name):
  s=source(rel);m=re.search(r'^    (?:private |internal )?(?:suspend )?fun '+re.escape(name)+r'\([^\n]*[\s\S]*?^    }',s,re.M)
  if not m:raise RuntimeError('Method seam missing: '+rel+':'+name)
  text=m.group();record(rel,text,s[:m.start()].count('\n')+1,s[:m.end()].count('\n')+1,'unmodified_method_slice');return text
 for rel in FULL:
  s=source(rel);record(rel,s,1,len(s.splitlines()),'unmodified_whole_source');(sources/Path(rel).name).write_text(s)
 for file in (ROOT/'stubs').glob('*.kt'):(sources/file.name).write_bytes(file.read_bytes())
 ai=method('ime/CustomKeyboardIme.kt','runAiTask')
 editor=method('ime/EditorController.kt','replaceSelectionOrAll')
 conv=method('io/ConvertRunner.kt','convert')
 text=source('io/ConvertRunner.kt');names=re.search(r'^    private fun names\([\s\S]*?^        }',text,re.M).group();start=text.index(names);record('io/ConvertRunner.kt',names,text[:start].count('\n')+1,text[:start+len(names)].count('\n')+1,'unmodified_method_slice')
 shell=(ROOT/'HostShells.kt.in').read_text().replace('/*ACTUAL_RUN_AI_TASK*/',ai).replace('/*ACTUAL_REPLACE*/',editor).replace('/*ACTUAL_CONVERT*/',conv).replace('/*ACTUAL_NAMES*/',names)
 (sources/'HostShells.kt').write_text(shell);(sources/'AuditMain.kt').write_bytes((ROOT/'AuditMain.kt').read_bytes())
 (a.out.parent/'source-manifest.json').write_text(json.dumps(dict(repo='klb-t/Custom-Keyboard-Pro',sha=sha,sources=manifest),indent=2))
 jars=sorted(a.deps.resolve().glob('*.jar'));cp=os.pathsep.join(map(str,jars));jar=a.workdir/'audit.jar'
 compile_cmd=['java','-Xmx768m','-cp',cp,'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-classpath',cp,*map(str,sorted(sources.glob('*.kt'))),'-d',str(jar)]
 c=subprocess.run(compile_cmd,text=True,capture_output=True);(a.out.parent/'compile.log').write_text(c.stdout+c.stderr)
 if c.returncode: print(c.stderr[-12000:]);return 2
 r=subprocess.run(['java','-cp',str(jar)+os.pathsep+cp,'AuditMainKt',a.mode],text=True,capture_output=True);(a.out.parent/'runtime-stderr.log').write_text(r.stderr)
 try:cases=[json.loads(l) for l in r.stdout.splitlines() if l.startswith('{')]
 except Exception:print(r.stdout);raise
 receipt=dict(repo='klb-t/Custom-Keyboard-Pro',sha=sha,mode=a.mode,level='JVM host; exact method slices and whole source files; NOT Android/device/E2E',transport='SecurityManager blocks all connect; AiClient replaced only at suspended transport boundary',compiler='Kotlin 2.1.20',exit_code=r.returncode,cases=cases,summary={k:sum(x['status']==k for x in cases) for k in ['PASS','FAIL']})
 a.out.write_text(json.dumps(receipt,ensure_ascii=False,indent=2));print(json.dumps(receipt['summary']));return r.returncode
if __name__=='__main__':sys.exit(main())
