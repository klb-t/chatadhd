"""Optional scripted per-leaf admission over the unchanged AnalysisPlan ledger.

No credential loader, network transport, graph write or executor sandbox. Raw
bytes, permissions and accounting belong to explicit registered instruments.
"""
from copy import deepcopy
from datetime import datetime,timezone
import hashlib
from pathlib import Path

from ...contracts import analysis_plan_ref as core


class LeaseError(ValueError):pass


def _id(value):
    if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value):
        raise LeaseError('parent_attempt_identity_invalid')
    return value


def _quantities(values):
    if not isinstance(values,dict) or set(values)!=set(core.DIMENSIONS):
        raise LeaseError('all_resource_reservations_required')
    for value in values.values():core.quantity(value)
    return deepcopy(values)


def zero_reservation():return {d:'0' for d in core.DIMENSIONS}


def parent_identity(context):
    return core.digest({'plan_sha256':context['plan_sha256'],'method_id':context['method']['id'],
        'axes':context['axes'],'dependency_attempts':{d:context['dependencies'][d]['attempt_id'] for d in sorted(context['method']['depends_on'])}})


class Authority:
    def __init__(self,ledger,*,parent_attempt_id,parent_reservation,sources,semantic_contract,gate,profile='leaf_only'):
        self.ledger=ledger;self.parent_id=_id(parent_attempt_id)
        declared=_quantities(parent_reservation)
        if profile!='leaf_only' or any(core.quantity(v)!=0 for v in declared.values()):
            raise LeaseError('parent_envelope_or_reservation_not_supported')
        if not callable(gate):raise LeaseError('live_execution_gate_required')
        self.sources=deepcopy(sources);self.semantic_contract=deepcopy(semantic_contract);self.gate=gate
        self.directory=ledger.directory/'shared-runtime-lease-v1';self.directory.mkdir(exist_ok=True)
        parent_folder=ledger.directory/('attempt-'+self.parent_id)
        if parent_folder.exists():
            actual=core._read(parent_folder/'reservation.json')
            if actual['reservation']!=declared:raise LeaseError('parent_reservation_binding_drift')
        self.receipts=[]

    def _decision(self,binding):
        decision=self.gate(deepcopy(binding))
        if (not isinstance(decision,dict) or type(decision.get('allowed')) is not bool or
            type(decision.get('cancelled')) is not bool or not isinstance(decision.get('permission_version'),str) or
            not decision['permission_version'] or not isinstance(decision.get('source_ref'),str) or not decision['source_ref']):
            raise LeaseError('execution_gate_receipt_invalid')
        core.encoded(decision)
        return decision

    def _plan(self,binding):
        method={'id':'leaf','method':'shared-runtime:leaf','runtime':{'id':'scripted:injected','config':{}},
            'depends_on':[],'source_refs':[s['id'] for s in self.sources],'variant_axes':[],
            'required_capabilities':list(binding['required_capabilities']),'scope':{'profile':'leaf_only'},'roles':[],
            'tools':[],'reasoning':{},'acceptance_policy':{'mode':'no_graph_write'},'evaluation_policy':{'preserve_first':True},
            'reservation':deepcopy(binding['reservation']),'config':{'lease_binding':deepcopy(binding)}}
        plan={'schema':'loom.analysis_plan/1','id':'leaf:'+binding['logical_key'],
            'resource_budget_ref':self.ledger.budget_id,'semantic_contract':deepcopy(self.semantic_contract),
            'sources':deepcopy(self.sources),'variant_axes':{},'selection':{'mode':'all'},'methods':[method],
            'resource_limits':deepcopy(self.ledger.limits),'presets':{},'extensions':{'accounting_owner':'leaf'}}
        core.validate_plan(plan);return plan

    def _unreconciled_started(self):
        # All adapter instances beneath this ledger share this journal. Its
        # records are dispatch evidence, not a second resource usage store.
        for folder in sorted(self.directory.glob('step-*')):
            if (folder/'dispatch_started.json').exists():
                binding=core._read(folder/'binding.json');receipt=self.ledger.lookup(binding['attempt_id'])
                if receipt is None or receipt['state']!='completed':return binding['logical_key']
        return None

    def run_step(self,logical_step_id,request,reservation,executor,usage_extractor,*,interpret=None,
                 required_capabilities=(),capabilities=(),permission=None,instrument=None):
        if not isinstance(logical_step_id,str) or not logical_step_id:raise LeaseError('logical_step_identity_invalid')
        if not callable(executor) or not callable(usage_extractor):raise LeaseError('registered_executor_and_usage_instrument_required')
        if not isinstance(permission,dict) or permission.get('execute') is not True:
            raise LeaseError('explicit_execution_permission_required')
        if not isinstance(instrument,dict) or not isinstance(instrument.get('id'),str) or not instrument['id']:
            raise LeaseError('instrument_identity_required')
        required=list(required_capabilities)
        if len(set(required))!=len(required) or any(not isinstance(v,str) or not v for v in required):raise LeaseError('capability_declaration_invalid')
        logical_key=core.digest({'budget_id':self.ledger.budget_id,'parent_attempt_id':self.parent_id,'logical_step_id':logical_step_id})
        definition={'budget_id':self.ledger.budget_id,'parent_attempt_id':self.parent_id,'logical_step_id':logical_step_id,
            'logical_key':logical_key,'request':deepcopy(request),'request_sha256':core.digest(request),
            'reservation':_quantities(reservation),'permission':deepcopy(permission),'instrument':deepcopy(instrument),
            'required_capabilities':required,'sources':deepcopy(self.sources),'semantic_contract':deepcopy(self.semantic_contract)}
        core.encoded(definition)
        plan=self._plan(definition)
        attempt=core.digest({'plan_sha256':core.plan_identity(plan),'method_id':'leaf','axes':{},'dependency_attempts':{}})
        binding=definition|{'attempt_id':attempt};folder=self.directory/('step-'+logical_key)
        with self.ledger.locked():
            folder.mkdir(exist_ok=True)
            if (folder/'binding.json').exists():
                if core._read(folder/'binding.json')!=binding:raise LeaseError('logical_step_binding_drift_no_retry')
            else:core._write_new(folder/'binding.json',binding)
        existing=self.ledger.lookup(attempt)
        if existing is not None:
            return self._receipt(binding,folder,existing,'replay')
        uncertain=self._unreconciled_started()
        if uncertain is not None:return {'state':'unavailable','reason':'unreconciled_started_step','blocked_by_logical_key':uncertain,'logical_key':logical_key,'dispatch':'no_dispatch'}
        decision=self._decision(binding)
        if decision['cancelled'] or not decision['allowed']:
            return {'state':'unavailable','reason':'cancelled_before_admission' if decision['cancelled'] else 'revoked_before_admission',
                'logical_key':logical_key,'dispatch':'no_dispatch','gate_receipt':decision}
        def callback(context,packet):
            with self.ledger.locked():
                start_decision=self._decision(binding)
                if set(required)-set(capabilities):
                    start_decision=dict(start_decision,allowed=False,missing_capabilities=sorted(set(required)-set(capabilities)))
                if start_decision['cancelled'] or not start_decision['allowed']:
                    value={'state':'cancelled_before_dispatch' if start_decision['cancelled'] else 'revoked_before_dispatch',
                           'gate_receipt':start_decision,'dispatch':'no_dispatch'}
                    core._write_new(folder/'no_dispatch.json',value)
                    return {'output':value,'measurements':{'money_usd':'0','calls':'0'},
                        'measurement_provenance':{d:{'status':'instrument_measured','source_ref':'durable-no-dispatch-receipt','scope':'no_provider_dispatch_or_fee'} for d in ('money_usd','calls')}}
                core._write_new(folder/'dispatch_started.json',{'binding_sha256':core.digest(binding),
                    'request_sha256':binding['request_sha256'],'gate_receipt':start_decision,
                    'started_at':datetime.now(timezone.utc).isoformat(),'accounting_owner':'leaf'})
            raw=executor(deepcopy(request),{'lease_binding':deepcopy(binding),'gate_receipt':start_decision})
            if not isinstance(raw,bytes):raise LeaseError('executor_must_return_exact_bytes')
            with (folder/'raw_first.bin').open('xb') as stream:
                stream.write(raw);stream.flush()
                import os
                os.fsync(stream.fileno())
            raw_sha=hashlib.sha256(raw).hexdigest()
            core._write_new(folder/'raw_receipt.json',{'sha256':raw_sha,'bytes':len(raw),'received_at':datetime.now(timezone.utc).isoformat()})
            usage=usage_extractor(raw,deepcopy(request))
            if not isinstance(usage,dict) or set(usage)!={'measurements','measurement_provenance'}:raise LeaseError('usage_instrument_contract_invalid')
            core._write_new(folder/'usage_first.json',usage)
            output={'state':'completed','raw_sha256':raw_sha,'raw_bytes':len(raw),'accounting_owner':'leaf'}
            if interpret is not None:
                try:output['value']=interpret(raw,deepcopy(request))
                except Exception as exc:output.update(state='semantic_rejected',failure_class=type(exc).__name__)
            return {'output':output,**usage}
        result=core.execute_variant(plan,0,self.ledger,{('shared-runtime:leaf','scripted:injected'):callback},capabilities=capabilities)
        return self._receipt(binding,folder,result['results']['leaf'],'first')

    def _receipt(self,binding,folder,core_receipt,mode):
        raw=None;raw_receipt=None
        if (folder/'raw_first.bin').exists():
            raw=(folder/'raw_first.bin').read_bytes()
            if (folder/'raw_receipt.json').exists():
                raw_receipt=core._read(folder/'raw_receipt.json')
                if hashlib.sha256(raw).hexdigest()!=raw_receipt['sha256'] or len(raw)!=raw_receipt['bytes']:raise LeaseError('first_raw_hash_drift')
            elif core_receipt['state']=='completed':raise LeaseError('completed_raw_receipt_missing')
        if (folder/'dispatch_started.json').exists():
            start=core._read(folder/'dispatch_started.json')
            if start['binding_sha256']!=core.digest(binding) or start['request_sha256']!=binding['request_sha256']:raise LeaseError('dispatch_binding_drift')
        if core_receipt['state']=='completed':
            output=core_receipt['result']['output']
            if output.get('dispatch')!='no_dispatch':
                if raw is None or raw_receipt is None or output.get('raw_sha256')!=raw_receipt['sha256'] or core._read(folder/'usage_first.json')!={k:core_receipt['result'][k] for k in ('measurements','measurement_provenance')}:
                    raise LeaseError('completed_leaf_evidence_drift')
        usage=self.ledger.usage()
        overrun=[d for d,v in usage.items() if self.ledger.limits[d]['limit'] is not None and core.quantity(v)>core.quantity(self.ledger.limits[d]['limit'])]
        receipt={'state':core_receipt['state'],'logical_key':binding['logical_key'],'attempt_id':binding['attempt_id'],
            'parent_attempt_id':self.parent_id,'request_sha256':binding['request_sha256'],'core_receipt':deepcopy(core_receipt),
            'raw_receipt':raw_receipt,'raw':raw,'mode':mode,'resource_usage':usage,'overrun_dimensions':overrun,'accounting_owner':'leaf'}
        if core_receipt['state']=='completed':receipt['state']=core_receipt['result']['output'].get('state','completed')
        self.receipts.append({k:v for k,v in receipt.items() if k!='raw'})
        return receipt

    def orchestration_result(self,output):
        memo={'output':deepcopy(output),'leaf_receipt_history':deepcopy(self.receipts),
              'leaf_receipt_ids':sorted({r['attempt_id'] for r in self.receipts}),'accounting_owner':False,
              'aggregate_usage_is_memo_only':True}
        return {'output':memo,'measurements':{'money_usd':'0','calls':'0'},
            'measurement_provenance':{d:{'status':'instrument_measured','source_ref':'orchestration-no-direct-fee-or-dispatch',
                'scope':'orchestration_only_leaf_expenses_accounted_separately'} for d in ('money_usd','calls')}}


def orchestration_callback(ledger,gate,run):
    """A registered parent returns domain output; adapter owns its zero-fee memo."""
    def callback(context,packet):
        authority=Authority(ledger,parent_attempt_id=parent_identity(context),parent_reservation=context['method']['reservation'],
            sources=context['sources'],semantic_contract=context['semantic_contract'],gate=gate)
        return authority.orchestration_result(run(authority,deepcopy(context),packet))
    return callback
