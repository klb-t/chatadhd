#!/usr/bin/env python3
"""Run the existing Android JVM gate from an immutable archive, using caller-supplied tools."""
import argparse
import collections
import io
import json
import os
import pathlib
import subprocess
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['repo','out','gradle','jdk','sdk']:p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--sha',required=True)
    p.add_argument('--timeout',type=int,default=300)
    p.add_argument('--heap',choices=['1g','2g'],default='1g')
    p.add_argument('--audit-room',action='store_true',help='Attach external independent Room tests; product sources remain unchanged')
    p.add_argument('--phase',choices=['all','reproduction','acceptance'],default='all')
    args=p.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    # A new source directory prevents files deleted on a later SHA from surviving
    # a previous extraction. Dependency/build caches can still be reused.
    src=pathlib.Path(tempfile.mkdtemp(prefix='source-',dir=out))
    sha=subprocess.check_output(['git','-C',str(args.repo),'rev-parse',args.sha+'^{commit}'],text=True).strip()
    raw=subprocess.check_output(['git','-C',str(args.repo),'archive',sha])
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:archive.extractall(src,filter='data')
    env=os.environ.copy();env['JAVA_HOME']=str(args.jdk.resolve());env['ANDROID_HOME']=str(args.sdk.resolve());env['ANDROID_SDK_ROOT']=str(args.sdk.resolve())
    # Match the environment's current authorized proxy, not a stale inherited host.
    proxy=urllib.request.getproxies().get('https')
    if proxy:
        proxy=urllib.parse.urlsplit(proxy)
        if proxy.username or proxy.password:raise RuntimeError('Credentialed proxy requires a separate secret-safe adapter')
        env['GRADLE_OPTS']=f'-Dhttp.proxyHost={proxy.hostname} -Dhttp.proxyPort={proxy.port} -Dhttps.proxyHost={proxy.hostname} -Dhttps.proxyPort={proxy.port}'
    cmd=[str(args.gradle.resolve()),'--no-daemon','--gradle-user-home',str(out/'gradle-home'),'--console=plain','--max-workers=1',f'-Dorg.gradle.jvmargs=-Xmx{args.heap} -Dfile.encoding=UTF-8','testDebugUnitTest','--info']
    if args.audit_room:
        base=pathlib.Path(__file__).resolve().parent
        pattern='audit.lem.room.RoomPersistenceAuditTest'
        if args.phase!='all':pattern += '.A_*' if args.phase=='reproduction' else '.B_*'
        cmd += ['-I',str(base/'room-audit.init.gradle'),'-Dlem.audit.testSource='+str(base/'room'),'--tests',pattern,'--no-configuration-cache']
    start=time.time()
    with (out/'gradle.log').open('w') as log:
        try:
            result=subprocess.run(cmd,cwd=src,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout)
            code=result.returncode;status='PASS' if code==0 else 'FAILED_OR_BLOCKED'
        except subprocess.TimeoutExpired:code=None;status='BLOCKED_TIMEOUT'
    tests=[]
    for report in sorted((src/'app/build/test-results/testDebugUnitTest').glob('TEST-*.xml')):
        for test in ET.fromstring(report.read_text()).findall('testcase'):
            name=test.get('name');phase='A' if name.startswith('A_') else 'B' if name.startswith('B_') else 'existing'
            tests.append({'name':name,'phase':phase,'status':'FAIL' if test.find('failure') is not None or test.find('error') is not None else 'SKIP' if test.find('skipped') is not None else 'PASS'})
    if code and tests:status='SELECTED_TEST_ASSERTIONS_FAILED'
    receipt={'schema':'klbt.audit.lem-gradle/1','sha':sha,'command':cmd,'source_directory':str(src),'status':status,'exit_code':code,'elapsed_seconds':round(time.time()-start,2),'scope':'Independent external Room host tests' if args.audit_room else 'Existing testDebugUnitTest task, not Android device/Room migration unless explicitly present in those tests','tests':tests,'counts':{phase:dict(collections.Counter(t['status'] for t in tests if t['phase']==phase)) for phase in ['A','B','existing']}}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))
    return 0 if code==0 and tests else 1 if tests else 2

if __name__=='__main__':raise SystemExit(main())
