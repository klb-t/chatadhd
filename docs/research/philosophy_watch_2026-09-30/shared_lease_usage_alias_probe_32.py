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
    source=HERE/'shared_lease_usage_alias_source_32.py'
    with source.open('xb') as f:f.write(pathlib.Path(producer.__file__).read_bytes())
    new(HERE/'SHARED_LEASE_USAGE_ALIAS_FREEZE_32.json',{'known_at':datetime.now(timezone.utc).isoformat(),'files_sha256':{str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in [source,pathlib.Path(__file__),HERE/'PROTOCOL_32_SHARED_LEASE_USAGE_ALIAS.md',pathlib.Path(core.__file__),core.SCHEMA_PATH]},'baseline_source':'corrected adapter; identical callback-alias fixture and explicit external-parent lineage','actual_paid_requests':0})
    spec=importlib.util.spec_from_file_location('loom.tools.structure.shared_runtime_lease_v1.watch_alias_32',source);lease=importlib.util.module_from_spec(spec);spec.loader.exec_module(lease)
    inventory=[];report={}
    with zipfile.ZipFile(HERE/'shared_lease_usage_alias_evidence_32.zip','x',compression=zipfile.ZIP_DEFLATED) as archive:
      with tempfile.TemporaryDirectory(prefix='lease-alias-watch-',dir='/var/tmp') as temp:
        try:
          limits={d:{'limit':None,'accounting':'cumulative','measurement_policy':{s:'use_declared_amount' if s in core.KNOWN_STATUSES else 'max_reservation_amount' for s in core.MEASUREMENT_STATUSES}} for d in core.DIMENSIONS};limits['money_usd']['limit']='0.05'
          ledger=core.ResourceLedger(pathlib.Path(temp)/'ledger',limits,budget_id='authored-alias-only')
          authority=lease.Authority(ledger,parent_attempt_id='a'*64,parent_reservation=lease.zero_reservation(),parent_lineage='caller_declared_external',sources=[{'id':'source','ref':'authored:source','provenance':{'known_at':None},'binding':{'mode':'pinned_snapshot','snapshot_id':'authored-frozen'}}],semantic_contract={'id':'other.domain/graph','version':'1','config':{}},gate=lambda _: {'allowed':True,'cancelled':False,'permission_version':'1','source_ref':'authored-current-grant'})
          reserve=lease.zero_reservation();reserve.update(money_usd='0.02',calls='1');raw=b'{"scripted":true}';calls=[]
          usage={'measurements':{'money_usd':'0.06','calls':'1'},'measurement_provenance':{'money_usd':{'status':'provider_reported','source_ref':'authored-billing-fixture'},'calls':{'status':'instrument_measured','source_ref':'authored-local-counter'}}}
          original_usage=deepcopy(usage)
          def executor(*_):calls.append(1);return raw
          def interpret(*_):usage['measurements']['money_usd']='0';return {'semantic':'finite'}
          try:
            result=authority.run_step('step',{'kind':'authored'},reserve,executor,lambda *_:usage,interpret=interpret,permission={'execute':True},instrument={'id':'authored-billing/1'});report['returned_state']=result['state']
          except Exception as exc:report.update(receipt_exception_class=type(exc).__name__,receipt_exception_code=str(exc))
          folder=next(authority.directory.glob('step-*'));saved=json.loads((folder/'usage_first.json').read_text());report.update(saved_usage=saved,saved_usage_is_original=saved==original_usage,mutated_callback_object=usage,policy_usage_money_usd=ledger.usage()['money_usd'],raw_preserved=(folder/'raw_first.bin').read_bytes()==raw,dispatch_count=len(calls))
          report['criterion_pass']=core.quantity(report['policy_usage_money_usd'])==core.quantity('0.06') and report['saved_usage_is_original'] and report['raw_preserved']
        finally:
          for path in sorted(pathlib.Path(temp).rglob('*')):
            if path.is_file():blob=path.read_bytes();name=str(path.relative_to(temp));archive.writestr(name,blob);inventory.append({'path':name,'bytes':len(blob),'sha256':sha(blob)})
      archive.writestr('inventory.json',enc(inventory))
    report.update(known_at=datetime.now(timezone.utc).isoformat(),source_sha256=sha(source.read_bytes()),artifact_count=len(inventory),actual_paid_requests=0,actual_paid_cost_usd='0',usage_kind='synthetic_policy_accounting_not_actual_invoice')
    new(HERE/'SHARED_LEASE_USAGE_ALIAS_FIRST_RESULTS_32.json',report);print(json.dumps(report))
if __name__=='__main__':main()
