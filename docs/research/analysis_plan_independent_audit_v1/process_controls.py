"""Actual process concurrency and process-exit durability, with zero API calls."""
from pathlib import Path
from copy import deepcopy
import hashlib, json, multiprocessing, os, subprocess, sys

from loom.tools.contracts import analysis_plan_ref as ref
from loom.tools.contracts.test_analysis_plan_ref import plan, result

HERE = Path(__file__).resolve().parent
def save(name, value):
    with (HERE/name).open('x') as f: json.dump(value,f,indent=2,sort_keys=True); f.write('\n')

save('PROCESS_CONTROL_SOURCE.json', {'analysis_plan_ref_sha256':hashlib.sha256(Path(ref.__file__).read_bytes()).hexdigest()})
p = plan(); p['resource_limits']['money_usd']['limit']='2'
directory=HERE/'process_concurrency_ledger'
ref.ResourceLedger(directory,p['resource_limits'],budget_id='shared')
ctx=multiprocessing.get_context('fork'); barrier=ctx.Barrier(4); entered=ctx.Value('i',0); release=ctx.Event(); queue=ctx.Queue()
def worker(index):
    q=deepcopy(p); q['id']='process-'+str(index)
    try:
        ledger=ref.ResourceLedger(directory,p['resource_limits'],budget_id='shared')
        def callback(*_):
            with entered.get_lock():
                entered.value+=1
                if entered.value==2: release.set()
            if not release.wait(5): raise RuntimeError('process probe timed out')
            return result()
        barrier.wait(timeout=5)
        value=ref.execute_variant(q,0,ledger,{('future:analysis','local:scripted'):callback},capabilities=('local',))
        queue.put({'index':index,'state':value['results']['one']['state']})
    except Exception as exc:queue.put({'index':index,'error':type(exc).__name__})
workers=[ctx.Process(target=worker,args=(i,)) for i in range(4)]
for worker in workers:worker.start()
for worker in workers:worker.join(15)
responses=[queue.get(timeout=2) for _ in workers]
save('FIRST_PROCESS_CONCURRENT_DISTINCT.json',{'entered_callbacks':entered.value,'workers_exit_codes':[w.exitcode for w in workers],
    'responses':sorted(responses,key=lambda r:r['index']), 'usage':ref.ResourceLedger(directory,p['resource_limits'],budget_id='shared').usage()})

crash=HERE/'process_exit_ledger'
program='''import os,sys
from pathlib import Path
from loom.tools.contracts import analysis_plan_ref as ref
from loom.tools.contracts.test_analysis_plan_ref import plan
p=plan(); ledger=ref.ResourceLedger(Path(sys.argv[1]),p['resource_limits'],budget_id='shared')
ref.execute_variant(p,0,ledger,{('future:analysis','local:scripted'):lambda *_:os._exit(43)},capabilities=('local',))
'''
process=subprocess.run([sys.executable,'-B','-c',program,str(crash)],capture_output=True,text=True)
p=plan(); ledger=ref.ResourceLedger(crash,p['resource_limits'],budget_id='shared'); calls=[]
value=ref.execute_variant(p,0,ledger,{('future:analysis','local:scripted'):lambda *_:(calls.append(True) or result())},capabilities=('local',))
save('FIRST_PROCESS_EXIT.json',{'child_exit_code':process.returncode,'stderr':process.stderr,'retry_callback_count':len(calls),
    'reopened_state':value['results']['one']['state'],'usage':ledger.usage(),
    'attempt_files':sorted(f.name for f in next(crash.glob('attempt-*')).iterdir())})

dimensions=[]
for dimension in ref.DIMENSIONS:
    p=plan(); p['resource_limits'][dimension]['limit']='0.5'
    ledger=ref.ResourceLedger(HERE/('dimension_'+dimension),p['resource_limits'],budget_id='shared');calls=[]
    value=ref.execute_variant(p,0,ledger,{('future:analysis','local:scripted'):lambda *_:(calls.append(True) or result())},capabilities=('local',))
    dimensions.append({'dimension':dimension,'callback_count':len(calls),'reason':value['results']['one']['reason']})
save('FIRST_DIMENSION_ADMISSION.json',dimensions)
print('Process/dimension controls preserved')
