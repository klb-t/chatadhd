#!/usr/bin/env python3
"""Execute actual Settings/codec/profile classes emitted by the pinned Android build.
All product/config/build inputs in --checkout must equal --repo/--sha. This is a
JVM codec check, not encrypted storage, Android UI, or process restart.
"""
import argparse,json,subprocess,os,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--checkout',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True);p.add_argument('--deps',type=Path,required=True);p.add_argument('--workdir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
sha=subprocess.check_output(['git','-C',str(a.repo),'rev-parse',a.sha+'^{commit}'],text=True).strip();inputs=[]
for path in subprocess.check_output(['git','-C',str(a.repo),'ls-tree','-r','--name-only',sha],text=True).splitlines():
 if path.startswith('app/src/main/') or path.endswith(('.gradle.kts','.toml','.properties')):
  wanted=subprocess.check_output(['git','-C',str(a.repo),'show',sha+':'+path]);actual=(a.checkout/path).read_bytes()
  if actual!=wanted:raise RuntimeError('Build input differs from pinned SHA: '+path)
  inputs.append(dict(path=path,sha256=hashlib.sha256(wanted).hexdigest()))
classes=a.checkout/'app/build/intermediates/built_in_kotlinc/debug/compileDebugKotlin/classes'
if not (classes/'com/example/core/config/SettingsStore.class').exists():raise RuntimeError('Run compileDebugKotlin first with exact toolchain')
a.workdir.mkdir(parents=True,exist_ok=True);a.out.parent.mkdir(parents=True,exist_ok=True);jdk=a.toolchain/'jdk-17.0.20.1+1/bin';cp=os.pathsep.join([str(classes.resolve()),*(str(j.resolve()) for j in a.deps.glob('*.jar')),str((a.toolchain/'sdk/platforms/android-36.1/android.jar').resolve())]);source=Path(__file__).with_name('CompiledSettingsProbe.java');r=subprocess.run([str(jdk/'javac'),'-cp',cp,'-d',str(a.workdir),str(source)],capture_output=True,text=True)
(a.out.parent/'compiled-settings-javac.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit(r.returncode)
r=subprocess.run([str(jdk/'java'),'-cp',str(a.workdir.resolve())+os.pathsep+cp,'CompiledSettingsProbe'],capture_output=True,text=True);(a.out.parent/'compiled-settings-stderr.log').write_text(r.stderr);cases=[json.loads(l) for l in r.stdout.splitlines() if l.startswith('{')]
receipt=dict(repo='klb-t/Custom-Keyboard-Pro',sha=sha,level='JVM calls to actual Android-build Kotlin bytecode; no consumer stubs; codec not encrypted persistence or restart',build_inputs_verified=len(inputs),classes={p.relative_to(classes).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (classes/'com/example/core/config').glob('*.class')},cases=cases,exit_code=r.returncode,counts={s:sum(c['status']==s for c in cases) for s in ['PASS','FAIL']})
a.out.write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt['counts']));raise SystemExit(r.returncode)
