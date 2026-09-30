"""Final independent rerun at the owner's exact three-file candidate pin."""
from pathlib import Path
from copy import deepcopy
import hashlib,json,os,subprocess,sys
from loom.tools.contracts import analysis_plan_ref as ref
from loom.tools.contracts.test_analysis_plan_ref import plan,result

PINS={'loom/tools/contracts/analysis_plan_ref.py':'2dfc9c3c67fb7135477b91e5599bbc30c96e006ab301b55b4a1d0f142f442372',
 'docs/contracts/analysis_plan.schema.json':'3f70b5db565c191e72af4200a6348c342e7210dfac4a96c2d217da78725e3776',
 'loom/tools/contracts/test_analysis_plan_ref.py':'a5e4fa15178e0dd0f39f28515137aefc849b6eeff60a7d4182654a980cc10f60'}
HERE=Path(__file__).resolve().parent
ROOT=Path(ref.__file__).resolve().parents[3]
DEST=HERE/'repair3';DEST.mkdir(exist_ok=False)
def save(name,value):
 with (DEST/name).open('x') as f:json.dump(value,f,indent=2,sort_keys=True);f.write('\n')
def hashes():return {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in PINS}
before=hashes();save('ORCHESTRATION_SOURCE_BEFORE.json',before);assert before==PINS
env=dict(os.environ);env['TMPDIR']='/var/tmp';env['PYTHONPATH']='/workspace/scratch/34e008d7a951/research-recovery/contract-deps:'+str(ROOT)
commands=[]
for name in ('probe.py','process_controls.py','receipt_controls.py','arithmetic_controls.py'):
 code=(HERE/name).read_text().replace('ROOT = HERE.parents[2]','ROOT = Path(ref.__file__).resolve().parents[3]')
 path=DEST/name
 with path.open('x') as f:f.write(code)
 r=subprocess.run([sys.executable,'-B',str(path)],cwd=ROOT,env=env,capture_output=True,text=True)
 with (DEST/(name+'.FIRST.log')).open('x') as f:f.write(r.stdout+'\n'+r.stderr)
 commands.append({'script':name,'exit_code':r.returncode});assert r.returncode==0,r.stderr
test=subprocess.run([sys.executable,'-B','-m','unittest','loom.tools.contracts.test_analysis_plan_ref','-v'],cwd=ROOT,env=env,capture_output=True,text=True)
with (DEST/'targeted32_FIRST.log').open('x') as f:f.write(test.stdout+'\n'+test.stderr)
assert test.returncode==0,test.stderr

p=plan();p['variant_axes']={'prompt':{'values':['A','B']}};p['selection']={'mode':'indices','indices':[0]};p['methods'][0]['variant_axes']=['prompt']
ledger=ref.ResourceLedger(DEST/'selected_scope_ledger',p['resource_limits'],budget_id='shared');calls=[];loads=[];rejected=None
try:
 ref.execute_variant(p,1,ledger,{('future:analysis','local:scripted'):lambda *_:(calls.append(True) or result())},
   capabilities=('local',),packet_loader=lambda *_:(loads.append(True) or {}))
except ref.PlanError as e:rejected=e.code
save('SELECTION_REPAIR.json',{'rejected':rejected,'callback_count':len(calls),'loader_count':len(loads),
 'attempt_count':len(list(ledger.directory.glob('attempt-*'))),'coordinate_inspection':ref.variant_at(p,1)})
assert rejected=='variant_not_selected' and not calls and not loads and not list(ledger.directory.glob('attempt-*'))

p=plan();ledger=ref.ResourceLedger(DEST/'packet_receipt_ledger',p['resource_limits'],budget_id='shared')
packet={'raw_turns':[{'id':'turn','text':'exact source bytes','known_at':'2026-09-30T00:00:00Z'}]};original=deepcopy(packet)
observations=[]
def callback(context,loaded):
 folder=next(ledger.directory.glob('attempt-*'));persisted=json.loads((folder/'loaded_packet_first.json').read_bytes())
 observations.append({'persisted_before_callback':persisted['packet']==original,
  'hash_matches_context':ref.digest(persisted)==context['loaded_packet_sha256']})
 loaded['raw_turns'].clear();return result()
value=ref.execute_variant(p,0,ledger,{('future:analysis','local:scripted'):callback},capabilities=('local',),packet_loader=lambda *_:packet)
folder=next(ledger.directory.glob('attempt-*'));saved=json.loads((folder/'loaded_packet_first.json').read_bytes());saved['packet']['raw_turns'][0]['text']='changed'
(folder/'loaded_packet_first.json').write_text(json.dumps(saved));rejections=[]
for operation in ('usage','lookup'):
 try:ledger.usage() if operation=='usage' else ledger.lookup(folder.name[8:]);rejections.append(None)
 except ref.PlanError as e:rejections.append(e.code)
save('PACKET_RECEIPT_REPAIR.json',{'observations':observations,'original_packet_unchanged':packet==original,
 'source_binding_verification':value['results']['one']['source_binding_verification'],'tamper_rejections':rejections})
assert packet==original and all(all(v.values()) for v in observations) and all(rejections)
after=hashes();save('ORCHESTRATION_SOURCE_AFTER.json',after);assert after==before
save('FINAL_RESULT.json',{'schema':'loom.analysis_plan_final_independent_review/1','source_pins':PINS,
 'source_bytes_unchanged_during_review':True,'independent_scripts':commands,'targeted_tests':32,'targeted_test_exit_code':test.returncode,
 'selection_repair_passed':True,'packet_snapshot_and_receipt_passed':True,'all_prior_first_artifacts_unchanged':True,
 'no_api_calls':True,'validation_access':False,'approved_scope':'callback-only reference; no nested resource enforcement or raw source-binding verification claimed'})
print('Final candidate independently verified at exact source/schema/test pins')
