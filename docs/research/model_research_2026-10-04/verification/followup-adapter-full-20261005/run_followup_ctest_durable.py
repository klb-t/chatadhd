"""Full unchanged CTest with flushed stdout evidence; no environment dump."""
import hashlib,json,os,subprocess,sys,time
from pathlib import Path
BASE=Path('/workspace/scratch/407fe6f6ff42/native-logs')
OUT=BASE/'followup-adapter-final'
subprocess.run([sys.executable,str(BASE/'audit_followup_adapter.py'),'start'],check=True)
env=os.environ.copy()
env.pop('PYTHONPATH',None);env.pop('TMPDIR',None)
env['LOOM_CANDIDATE_GRAPH_NATIVE_TOOL']='/workspace/scratch/407fe6f6ff42/build/loom_candidate_graph_native_tool'
cmd=['/root/.local/bin/ctest','--test-dir','/workspace/scratch/407fe6f6ff42/build','-j2','--output-on-failure',
     '--test-output-size-passed','10485760','--test-output-size-failed','10485760',
     '--output-junit',str(OUT/'ctest.junit.xml')]
proc=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
lines=[];last=time.monotonic()
with (OUT/'ctest.raw.log').open('wb') as stream:
    for line in proc.stdout:
        lines.append(line);stream.write(line);stream.flush();os.fsync(stream.fileno())
        now=time.monotonic()
        if now-last>=25:
            print(line.decode(errors='replace').rstrip(),flush=True);last=now
    status=proc.wait()
raw=b''.join(lines)
assert (OUT/'ctest.raw.log').read_bytes()==raw
(OUT/'stdout-capture-proof.json').write_text(json.dumps({'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),
 'method':'Captured complete subprocess stdout/stderr stream; every line flushed+fsynced; final bytes equality verified',
 'ctest_exit_code':status,'starts':raw[:100].decode(errors='replace'),'final_summary':raw[-180:].decode(errors='replace')},indent=2)+'\n')
print(raw[-220:].decode(errors='replace'),flush=True)
audit=subprocess.run([sys.executable,str(BASE/'audit_followup_adapter.py'),'finish','--exit-code',str(status)])
sys.exit(status or audit.returncode)
