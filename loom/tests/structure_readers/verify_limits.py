"""Faults in NEW synthetic installation copies; never mutate the accepted stage."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import time


def child(library, data):
    Native=runpy.run_path(str(Path(__file__).with_name('verify_native.py')))['Native']
    native=Native(library,data)
    try:
        result=native.invoke('client.blob_text',{'hash':native.upload(b'a;b;c\n'),'source_key':'synthetic:limits','name':'synthetic.csv','format':'csv'})
        assert 'error' in result, result
        print(json.dumps({'error_code':result['error']['code'],'diagnostic':result['error']['message']}))
    finally:native.close()


def run(stage, work):
    work.mkdir(mode=0o700)
    results={}
    for case in ['missing_helper','changed_helper','missing_interpreter','input_limit','output_limit','wall_timeout','cpu_limit','failed_exit']:
        root=work/case;package=root/'package';(package/'lib').mkdir(parents=True)
        os.link((stage/'lib/libloom.so').resolve(),package/'lib/libloom.so')
        helpers=package/'share/loom/structure_readers';shutil.copytree(stage/'share/loom/structure_readers',helpers)
        config=json.loads((helpers/'runtime.json').read_text());profile=json.loads((helpers/'profile.json').read_text())
        worker=helpers/'worker.py';csv=helpers/'csv/reader.py'
        if case=='missing_helper':worker.unlink()
        elif case=='changed_helper':worker.write_text(worker.read_text()+'\n# synthetic changed pin\n')
        elif case=='missing_interpreter':config['python']=str(root/'absent-python')
        elif case=='input_limit':profile['limits']['max_input_bytes']=1
        elif case=='output_limit':profile['max_output_bytes']=16
        elif case=='wall_timeout':
            profile['timeout_ms']=30
            worker.write_text('import time\ntime.sleep(10)\n'+worker.read_text())
            config['files']['worker.py']=hashlib.sha256(worker.read_bytes()).hexdigest()
        elif case=='cpu_limit':
            profile['limits']['cpu_seconds']=1
            csv.write_text('while True: pass\n')
            config['files']['csv/reader.py']=hashlib.sha256(csv.read_bytes()).hexdigest()
        elif case=='failed_exit':
            worker.write_text("print('{}')\nraise SystemExit(2)\n")
            config['files']['worker.py']=hashlib.sha256(worker.read_bytes()).hexdigest()
        (helpers/'profile.json').write_text(json.dumps(profile))
        config['files']['profile.json']=hashlib.sha256((helpers/'profile.json').read_bytes()).hexdigest()
        (helpers/'runtime.json').write_text(json.dumps(config))
        data=root/'data';data.mkdir()
        started=time.monotonic()
        command=['/usr/bin/setpriv','--no-new-privs','--landlock-access','fs']
        for rule in ['execute,read-file,read-dir:/usr','read-file,write-file:/dev/null',
                     'read-file,read-dir:'+str(Path(__file__).resolve().parent),
                     'execute,read-file,read-dir:'+str(package),
                     'execute,write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,make-sym,refer,truncate:'+str(data)]:
            command+=['--landlock-rule','path-beneath:'+rule]
        command+=[sys.executable,'-I','-B','-S',str(Path(__file__).resolve()),'--child','--stage',str(package),'--work',str(data)]
        completed=subprocess.run(command,env={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','LOOM_LOG_ENABLED':'0','TMPDIR':str(data)},capture_output=True,text=True,timeout=15)
        assert completed.returncode==0,(case,completed.stderr)
        result=json.loads(completed.stdout);result['seconds']=round(time.monotonic()-started,3)
        assert result['seconds']<10,(case,result)
        if case=='changed_helper':assert result['diagnostic']=='host_reader_package_changed'
        if case=='input_limit':assert result['diagnostic']=='host_reader_input_limit'
        if case=='wall_timeout':assert result['error_code']=='timeout'
        results[case]=result
    print(json.dumps({'refused':len(results),'cases':results},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--stage',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--child',action='store_true');a=p.parse_args()
    if a.child:child((a.stage/'lib/libloom.so').resolve(),a.work.resolve())
    else:run(a.stage.resolve(),a.work.resolve())
