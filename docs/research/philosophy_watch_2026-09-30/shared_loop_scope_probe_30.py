from datetime import datetime,timezone
import hashlib,importlib.util,json,pathlib,sys,tempfile,zipfile
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.contracts import analysis_plan_ref as core
from loom.tools.structure.shared_runtime_lease_v1 import runtime_loop as producer

def enc(v):return (json.dumps(v,sort_keys=True,ensure_ascii=False,indent=2,allow_nan=False)+'\n').encode()
def sha(raw):return hashlib.sha256(raw).hexdigest()
def new(path,v):
    with path.open('xb') as f:f.write(enc(v))
def load(path,name):
    spec=importlib.util.spec_from_file_location('loom.tools.structure.shared_runtime_lease_v1.'+name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def main():
    source=HERE/'shared_loop_audited_source_30.py'
    with source.open('xb') as f:f.write(pathlib.Path(producer.__file__).read_bytes())
    adapter=HERE/'shared_lease_audited_source_28.py'
    new(HERE/'SHARED_LOOP_SCOPE_FREEZE_30.json',{'known_at':datetime.now(timezone.utc).isoformat(),'files_sha256':{str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in [source,adapter,pathlib.Path(__file__),HERE/'PROTOCOL_30_SHARED_LOOP_SCOPE.md',pathlib.Path(core.__file__),pathlib.Path(producer.frozen_runtime.__file__),pathlib.Path(producer.safe.__file__)]},'actual_paid_requests':0})
    lease=load(adapter,'scope_adapter_30');loop=load(source,'scope_loop_30');inventory=[];report={}
    with zipfile.ZipFile(HERE/'shared_loop_scope_evidence_30.zip','x',compression=zipfile.ZIP_DEFLATED) as archive:
      with tempfile.TemporaryDirectory(prefix='loop-scope-watch-',dir='/var/tmp') as temp:
        try:
          limits={d:{'limit':None,'accounting':'cumulative','measurement_policy':{s:'use_declared_amount' if s in core.KNOWN_STATUSES else 'max_reservation_amount' for s in core.MEASUREMENT_STATUSES}} for d in core.DIMENSIONS};limits['money_usd']['limit']='0.05'
          ledger=core.ResourceLedger(pathlib.Path(temp)/'ledger',limits,budget_id='authored-loop-only')
          authority=lease.Authority(ledger,parent_attempt_id='a'*64,parent_reservation=lease.zero_reservation(),sources=[{'id':'source','ref':'authored:source','provenance':{'known_at':None},'binding':{'mode':'pinned_snapshot','snapshot_id':'authored-frozen'}}],semantic_contract={'id':'other.domain/graph','version':'1','config':{}},gate=lambda _: {'allowed':True,'cancelled':False,'permission_version':'1','source_ref':'authored-current-grant'})
          reserve=lease.zero_reservation();reserve.update(money_usd='0.02',calls='1');raw=b'{"usage":{"cost":"0.06"},"scripted":true}';calls=[]
          def call(value):calls.append(value);return raw
          request={'config':{'max_steps':1,'budget_usd':'1'},'body':{},'runtime_kind':'client_loop','packet_sha256':'b'*64}
          result=loop.execute_loop(request,authority,call,namespace='authored',model_reservation=reserve,model_permission={'execute':True},model_instrument={'id':'authored-dispatch/1'})
          new(pathlib.Path(temp)/'loop_receipt.json',result)
          report={'loop_receipt':result,'dispatch_count':len(calls),'raw_preserved':next(authority.directory.glob('step-*/raw_first.bin')).read_bytes()==raw,'criterion_pass':core.quantity(result['scope_retained_allowance_usd'])==core.quantity('0.06') and core.quantity(result['shared_usage']['money_usd'])==core.quantity('0.06') and len(calls)==1}
        finally:
          for path in sorted(pathlib.Path(temp).rglob('*')):
            if path.is_file():blob=path.read_bytes();name=str(path.relative_to(temp));archive.writestr(name,blob);inventory.append({'path':name,'bytes':len(blob),'sha256':sha(blob)})
      archive.writestr('inventory.json',enc(inventory))
    report.update(known_at=datetime.now(timezone.utc).isoformat(),source_sha256=sha(source.read_bytes()),adapter_sha256=sha(adapter.read_bytes()),artifact_count=len(inventory),actual_paid_requests=0,actual_paid_cost_usd='0',usage_kind='synthetic_policy_accounting_not_actual_invoice')
    new(HERE/'SHARED_LOOP_SCOPE_FIRST_RESULTS_30.json',report);print(json.dumps({k:v for k,v in report.items() if k!='loop_receipt'}|{'stopped_reason':result['stopped_reason'],'scope_retained_allowance_usd':result['scope_retained_allowance_usd'],'shared_money_usd':result['shared_usage']['money_usd']}))
if __name__=='__main__':main()
