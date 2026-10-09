#!/usr/bin/env python3
"""Run exact repository Android test gate on a disposable pinned-source snapshot.
Requires bootstrapped toolchain. Without --audit-tests, runs existing tests unchanged.
With --audit-tests adds only external audit test source and runs that class; failures
are product acceptance failures and return nonzero. No wrapper/invariants weakened.
"""
import argparse,subprocess,tarfile,io,os,json,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--workdir',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--audit-tests',action='store_true');a=p.parse_args()
sha=subprocess.check_output(['git','-C',str(a.repo),'rev-parse',a.sha+'^{commit}'],text=True).strip();a.workdir.mkdir(parents=True,exist_ok=True);a.out.parent.mkdir(parents=True,exist_ok=True)
snapshot=a.workdir/'checkout';snapshot.mkdir(exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.repo),'archive',sha]))) as t:t.extractall(snapshot,filter='data')
args=['bash','tools/local-android-build.sh',':app:compileDebugKotlin',':app:testDebugUnitTest','-Dorg.gradle.jvmargs=-Xmx2g -Dfile.encoding=UTF-8','--max-workers=1']
if a.audit_tests:args+=['--init-script',str(Path(__file__).with_name('android-tests.gradle').resolve()),'-Daudit.testSource='+str(Path(__file__).with_name('android-tests').resolve()),'--tests','com.example.audit.EcosystemAuditConsumerTest']
env=os.environ.copy();env['IO_TOOLCHAIN_ROOT']=str(a.toolchain.resolve())
log=a.out.with_suffix('.log')
with log.open('w') as f:r=subprocess.run(args,cwd=snapshot,env=env,stdout=f,stderr=subprocess.STDOUT)
xmls=list(snapshot.glob('app/build/test-results/testDebugUnitTest/TEST-*.xml'));tests=[]
for file in xmls:
 for case in ET.parse(file).getroot().iter('testcase'):
  tests.append(dict(classname=case.get('classname'),name=case.get('name'),status='FAIL' if case.find('failure') is not None or case.find('error') is not None else ('SKIP' if case.find('skipped') is not None else 'PASS')))
a.out.write_text(json.dumps(dict(repo='klb-t/Custom-Keyboard-Pro',sha=sha,kind='Android JVM/Robolectric, not hardware/device',audit_tests=a.audit_tests,exit_code=r.returncode,tests=tests,counts={s:sum(t['status']==s for t in tests) for s in ['PASS','FAIL','SKIP']}),indent=2));raise SystemExit(r.returncode)
