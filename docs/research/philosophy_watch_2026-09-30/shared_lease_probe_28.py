"""Author-written offline lease counterexamples; never a paid client."""
from copy import deepcopy
from datetime import datetime,timezone
import hashlib,importlib.util,json,pathlib,sys,tempfile,zipfile
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.contracts import analysis_plan_ref as core
from loom.tools.structure.shared_runtime_lease_v1 import adapter as producer

def enc(v):return (json.dumps(v,sort_keys=True,ensure_ascii=False,indent=2,allow_nan=False)+'\n').encode()
def sha(raw):return hashlib.sha256(raw).hexdigest()
def new(path,v):
    with path.open('xb') as f:f.write(enc(v))
def main():
    source=HERE/'shared_lease_audited_source_28.py'
    with source.open('xb') as f:f.write(pathlib.Path(producer.__file__).read_bytes())
    spec=importlib.util.spec_from_file_location('loom.tools.structure.shared_runtime_lease_v1.watch_snapshot_28',source);lease=importlib.util.module_from_spec(spec);spec.loader.exec_module(lease)
    cases=['interpret_throws_control','interpret_returns_set','interpret_returns_nan','interpret_returns_cycle','usage_instrument_throws','unknown_fee','estimated_zero','gate_revoked_at_start','configurable_two_million_fee']
    pins=[source,pathlib.Path(core.__file__),core.SCHEMA_PATH,HERE/'PROTOCOL_28_SHARED_LEASE_SEMANTICS.md',pathlib.Path(__file__)]
    new(HERE/'SHARED_LEASE_FREEZE_28.json',{'known_at':datetime.now(timezone.utc).isoformat(),'files_sha256':{str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in pins},'cases':cases,'actual_paid_requests':0,'actual_paid_cost_usd':'0','hypothesis_previously_suggested_by':'recipe_experiments independent reviewer; no outcomes supplied'})
    reports=[];inventory=[]
    with zipfile.ZipFile(HERE/'shared_lease_first_scripted_evidence_28.zip','x',compression=zipfile.ZIP_DEFLATED) as archive:
      for ident in cases:
        with tempfile.TemporaryDirectory(prefix='lease-watch-',dir='/var/tmp') as temp:
          try:
            limit='1000000000' if ident=='configurable_two_million_fee' else '0.05'
            limits={d:{'limit':None,'accounting':'cumulative','measurement_policy':{s:'use_declared_amount' if s in core.KNOWN_STATUSES else 'max_reservation_amount' for s in core.MEASUREMENT_STATUSES}} for d in core.DIMENSIONS};limits['money_usd']['limit']=limit
            ledger=core.ResourceLedger(pathlib.Path(temp)/'ledger',limits,budget_id='authored-offline-only-'+ident)
            sources=[{'id':'source','ref':'authored:raw-preserved','provenance':{'known_at':None,'kind':'raw_source'},'binding':{'mode':'pinned_snapshot','snapshot_id':'authored-local-v1'}}]
            semantic={'id':'other.domain/source-review','version':'22','config':{'identity_semantics':'source_local_label','automatic_acceptance':True}}
            gates=[];dispatches=[];raw='{"źródło":"Żółć 🧪","reported_fee_fixture":true}'.encode()
            def gate(binding):
                gates.append(deepcopy(binding));return {'allowed':not(ident=='gate_revoked_at_start' and len(gates)>1),'cancelled':False,'permission_version':str(len(gates)),'source_ref':'authored-current-grant'}
            authority=lease.Authority(ledger,parent_attempt_id='a'*64,parent_reservation=lease.zero_reservation(),sources=sources,semantic_contract=semantic,gate=gate)
            reserve=lease.zero_reservation();reserve.update(money_usd='2000000' if ident=='configurable_two_million_fee' else '0.02',calls='1')
            def executor(request,context):dispatches.append(deepcopy(context));return raw
            fee='2000000' if ident=='configurable_two_million_fee' else None if ident=='unknown_fee' else '0' if ident=='estimated_zero' else '0.06'
            status='unknown' if fee is None else 'estimated' if ident=='estimated_zero' else 'provider_reported'
            usage={'measurements':{'money_usd':fee,'calls':'1'},'measurement_provenance':{'money_usd':{'status':status,'source_ref':'authored-report-not-provider-invoice'},'calls':{'status':'instrument_measured','source_ref':'authored-executor-counter'}}}
            def usage_extractor(bytes_value,request):
                assert bytes_value==raw
                folder=next(authority.directory.glob('step-*'))
                assert (folder/'raw_first.bin').read_bytes()==raw and json.loads((folder/'raw_receipt.json').read_text())['sha256']==sha(raw)
                if ident=='usage_instrument_throws':raise ValueError('authored unreadable billing')
                return deepcopy(usage)
            def interpret(bytes_value,request):
                assert bytes_value==raw
                if ident=='interpret_throws_control':raise ValueError('authored semantic rejection')
                if ident=='interpret_returns_set':return {'semantic_value':{'not','json'}}
                if ident=='interpret_returns_nan':return {'semantic_value':float('nan')}
                if ident=='interpret_returns_cycle':
                    v={};v['cycle']=v;return v
                return {'candidate':'authored only'}
            arguments=dict(permission={'execute':True,'source_ref':'authored-explicit-step-grant'},instrument={'id':'authored-local-executor-and-fee/1'},interpret=interpret)
            first=authority.run_step('first',{'kind':'authored-source-review'},reserve,executor,usage_extractor,**arguments)
            second_reserve=lease.zero_reservation();second=authority.run_step('next',{'kind':'authored-independent-step'},second_reserve,executor,usage_extractor,permission={'execute':True},instrument={'id':'authored-local/1'}) if ident.startswith('interpret_') or ident=='usage_instrument_throws' else None
            folder=authority.directory/('step-'+first['logical_key']);files={p.name:sha(p.read_bytes()) for p in folder.iterdir() if p.is_file()}
            money=ledger.usage()['money_usd'];report={'case_id':ident,'state':first['state'],'first_core_state':first.get('core_receipt',{}).get('state'),'policy_usage_money_usd':money,'overrun_dimensions':first.get('overrun_dimensions'),'raw_retained_exact':first.get('raw')==raw if dispatches else not(folder/'raw_first.bin').exists(),'raw_sha256':sha(raw),'first_usage_saved_exact':json.loads((folder/'usage_first.json').read_text())==usage if (folder/'usage_first.json').exists() else False,'dispatch_count':len(dispatches),'gate_checks':len(gates),'second_state':None if second is None else second['state'],'second_reason':None if second is None else second.get('reason'),'source_context_preserved':all(c['lease_binding']['sources']==sources and c['lease_binding']['semantic_contract']==semantic for c in dispatches),'source_known_at_remains_null':all(c['lease_binding']['sources'][0]['provenance']['known_at'] is None for c in dispatches),'source_binding_verification':first.get('core_receipt',{}).get('source_binding_verification'),'measurements_status':first.get('core_receipt',{}).get('result',{}).get('measurement_provenance',{}).get('money_usd',{}).get('status'),'first_step_artifacts_sha256':files}
            if ident.startswith('interpret_'):
                report['criterion_pass']=report['state']=='semantic_rejected' and core.quantity(money)==core.quantity('0.06') and report['raw_retained_exact'] and report['first_usage_saved_exact'] and len(dispatches)==1 and second['state']=='unavailable'
            elif ident in ('usage_instrument_throws','unknown_fee','estimated_zero'):
                report['criterion_pass']=core.quantity(money)==core.quantity('0.02') and report['raw_retained_exact'] and (ident!='estimated_zero' or report['measurements_status']=='estimated')
            elif ident=='gate_revoked_at_start':report['criterion_pass']=first['state']=='revoked_before_dispatch' and not dispatches and core.quantity(money)==0
            else:report['criterion_pass']=first['state']=='completed' and core.quantity(money)==core.quantity('2000000') and report['first_usage_saved_exact']
            reports.append(report)
          except Exception as exc:reports.append({'case_id':ident,'harness_or_mechanism_exception_class':type(exc).__name__,'exception_text':str(exc),'criterion_pass':False})
          finally:
            for path in sorted(pathlib.Path(temp).rglob('*')):
                if path.is_file():
                    blob=path.read_bytes();name=ident+'/'+str(path.relative_to(temp));archive.writestr(name,blob);inventory.append({'path':name,'bytes':len(blob),'sha256':sha(blob)})
      archive.writestr('inventory.json',enc(inventory))
    results={'schema':'loom.philosophy.shared_lease_raw_audit/1','known_at':datetime.now(timezone.utc).isoformat(),'source_sha256':sha(source.read_bytes()),'cases':reports,'case_count':len(reports),'criteria_passed':sum(r['criterion_pass'] for r in reports),'artifact_count':len(inventory),'actual_paid_requests':0,'actual_paid_cost_usd':'0','canonical_graph_writes':0,'new_validation_or_old_holdout_read':False,'usage_kind':'synthetic_policy_accounting_not_actual_billing','decision':'investigate_preserved_failures' if any(not r['criterion_pass'] for r in reports) else 'keep_mechanism'}
    new(HERE/'SHARED_LEASE_FIRST_RESULTS_28.json',results);print(json.dumps(results,ensure_ascii=False))
if __name__=='__main__':main()
