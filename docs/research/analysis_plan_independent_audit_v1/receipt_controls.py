"""New receipt-layer controls; never alter first original or owner files."""
from pathlib import Path
from copy import deepcopy
from unittest.mock import patch
import hashlib, json
from loom.tools.contracts import analysis_plan_ref as ref
from loom.tools.contracts.test_analysis_plan_ref import plan, result

HERE=Path(__file__).resolve().parent/'receipt_controls_first'
HERE.mkdir(exist_ok=False)
def save(name,value):
    with (HERE/name).open('x') as f:json.dump(value,f,indent=2,sort_keys=True);f.write('\n')
save('SOURCE.json',{'source_sha256':hashlib.sha256(Path(ref.__file__).read_bytes()).hexdigest()})
def execute(p,ledger,callback):return ref.execute_variant(p,0,ledger,{('future:analysis','local:scripted'):callback},capabilities=('local',))
outcomes=[]
for kind in ('completion_state','completion_provenance','result_and_completion_recomputed','first_return_output','missing_first_return','missing_reservation'):
    p=plan(); p['resource_limits']['money_usd']['limit']='1'
    ledger=ref.ResourceLedger(HERE/kind,p['resource_limits'],budget_id='shared')
    execute(p,ledger,lambda *_:result())
    folder=next(ledger.directory.glob('attempt-*')); completion=json.loads((folder/'completion.json').read_bytes())
    if kind=='completion_state':completion['state']='uncertain'
    elif kind=='completion_provenance':completion['measurement_provenance']['money_usd']['status']='unknown'
    elif kind=='result_and_completion_recomputed':
        response=json.loads((folder/'result.json').read_bytes());response['measurements']['money_usd']='0'
        (folder/'result.json').write_text(json.dumps(response));completion['measurements']['money_usd']='0';completion['result_sha256']=ref.digest(response)
    elif kind=='first_return_output':
        response=json.loads((folder/'returned_first.json').read_bytes());response['output']={'different':'candidate'}
        (folder/'returned_first.json').write_text(json.dumps(response))
    elif kind=='missing_first_return':(folder/'returned_first.json').unlink()
    elif kind=='missing_reservation':(folder/'reservation.json').unlink()
    if kind.startswith('completion') or kind=='result_and_completion_recomputed':(folder/'completion.json').write_text(json.dumps(completion))
    response={'case':kind,'usage_rejected':False,'lookup_rejected':False,'new_branch_rejected':False,'new_callback_count':0}
    for op in ('usage','lookup','new_branch'):
        calls=[]
        try:
            if op=='usage':ledger.usage()
            elif op=='lookup':ledger.lookup(folder.name[8:])
            else:
                another=deepcopy(p);another['id']='after-'+kind
                execute(another,ledger,lambda *_:(calls.append(True) or result()))
        except ref.PlanError as exc:response[op+'_rejected']=True;response[op+'_reason']=exc.code
        response['new_callback_count']+=len(calls)
    outcomes.append(response)
save('FIRST_RECEIPT_RESULTS.json',outcomes)
assert all(row['usage_rejected'] and row['lookup_rejected'] and row['new_branch_rejected'] and row['new_callback_count']==0 for row in outcomes)

p=plan(); ledger=ref.ResourceLedger(HERE/'result_write_failure',p['resource_limits'],budget_id='shared')
original=ref._write_new; calls=[]
def fail_result(path,value):
    if Path(path).name=='result.json':raise OSError('scripted result storage failure')
    return original(path,value)
with patch.object(ref,'_write_new',fail_result):value=execute(p,ledger,lambda *_:(calls.append(True) or result({'first':'survives result write failure'})))
again=execute(p,ledger,lambda *_:(calls.append(True) or result()))
folder=next(ledger.directory.glob('attempt-*'))
save('FIRST_RESULT_WRITE_FAILURE.json',{'state':value['results']['one']['state'],'retry_state':again['results']['one']['state'],
    'callback_count':len(calls),'first_return_saved':(folder/'returned_first.json').exists(),'usage':ledger.usage()})
print('Six receipt corruptions fail closed; result-write failure preserves first return')
