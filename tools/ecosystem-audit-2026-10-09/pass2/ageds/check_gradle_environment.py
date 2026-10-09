#!/usr/bin/env python3
"""Bounded Gradle bootstrap/configuration retry using the current invocation proxy.
Does not modify repo/toolchain pins, run compilation, use emulator, or write checkout.
"""
import argparse,hashlib,io,json,os,pathlib,signal,subprocess,tarfile,tempfile,time,urllib.parse,urllib.request
p=argparse.ArgumentParser();p.add_argument('--checkout',required=True);p.add_argument('--sha',required=True);p.add_argument('--jdk',required=True);p.add_argument('--sdk',required=True);p.add_argument('--scratch',required=True);p.add_argument('--output',required=True);p.add_argument('--timeout',type=int,default=300);a=p.parse_args()
repo=pathlib.Path(a.checkout).resolve();assert subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()==a.sha
out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
if any(out.iterdir()):raise SystemExit('Use a fresh output directory, do not overwrite historical receipt')
work=pathlib.Path(a.scratch).resolve();work.mkdir(parents=True,exist_ok=True);snapshot=work/'source';snapshot.mkdir()
raw=subprocess.check_output(['git','-C',str(repo),'archive',a.sha]);tarfile.open(fileobj=io.BytesIO(raw)).extractall(snapshot,filter='data')
env=dict(os.environ);proxy=urllib.parse.urlsplit(urllib.request.getproxies().get('https',''));env['JAVA_HOME']=str(pathlib.Path(a.jdk).resolve());env['ANDROID_HOME']=str(pathlib.Path(a.sdk).resolve());env['ANDROID_SDK_ROOT']=env['ANDROID_HOME'];env['GRADLE_USER_HOME']=str(work/'gradle-home');env['PATH']=env['JAVA_HOME']+'/bin:'+env.get('PATH','')
env['GRADLE_OPTS']=''
if proxy.hostname:
 env['GRADLE_OPTS']=f'-Dhttp.proxyHost={proxy.hostname} -Dhttp.proxyPort={proxy.port or 80} -Dhttps.proxyHost={proxy.hostname} -Dhttps.proxyPort={proxy.port or 80} -Dhttp.nonProxyHosts=localhost|127.*'
receipt={'sha':a.sha,'scope':'Wrapper bootstrap and project task discovery only; no compilation or emulator','started_epoch':time.time(),'current_proxy_configured':bool(proxy.hostname),'old_GRADLE_OPTS_replaced':True,'jdk_used':pathlib.Path(a.jdk).name,'sdk_platforms':[x.name for x in (pathlib.Path(a.sdk)/'platforms').glob('*')],'required_by_AGEDS':'JDK21 / SDK37 / Gradle9.7.0 / Kotlin2.4.20 / AGP9.4; no pin substitution','checks':[]}
start=time.monotonic()
for name,args in [('wrapper',['bash','./gradlew','--version']),('project_configuration',['bash','./gradlew','--no-daemon','--max-workers=1','-Dorg.gradle.jvmargs=-Xmx512m','tasks','--all'])]:
 remain=min(a.timeout,300)-(time.monotonic()-start)
 if remain<=1:receipt['checks'].append({'name':name,'status':'NOT_RUN_TOTAL_BUDGET'});break
 log=out/(name+'.log')
 with log.open('wb') as f:
  proc=subprocess.Popen(args,cwd=snapshot,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  timeout=False
  try:code=proc.wait(timeout=remain)
  except subprocess.TimeoutExpired:
   timeout=True;os.killpg(proc.pid,signal.SIGTERM)
   try:code=proc.wait(timeout=5)
   except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);code=proc.wait()
 receipt['checks'].append({'name':name,'exit_code':code,'timed_out':timeout,'log':log.name,'sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
 if code!=0:break
receipt['elapsed_seconds']=round(time.monotonic()-start,2);receipt['product_tests_executed']=0;receipt['device_tests_executed']=0;receipt['pins_unchanged']=True
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt));raise SystemExit(0 if all(x.get('exit_code')==0 for x in receipt['checks']) else 1)
