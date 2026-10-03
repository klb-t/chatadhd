"""Scripted model/tool loop: each dispatched executor is its own shared leaf."""
from copy import deepcopy
import hashlib

from ...contracts import analysis_plan_ref as core
from ..frontier_panel_v1 import runtime as frozen_runtime
from .. import openrouter_runner as safe
from .adapter import LeaseError


def response_usage(raw,request):
    value=safe.parse_json(raw);usage=value.get('usage') if isinstance(value,dict) else None
    cost=usage.get('cost') if isinstance(usage,dict) else None
    measurements={'money_usd':None if cost is None else str(core.quantity(cost)),'calls':'1'}
    provenance={'money_usd':{'status':'unknown' if cost is None else 'provider_reported',
        'source_ref':'raw-response:sha256:'+hashlib.sha256(raw).hexdigest(),'scope':'reported_usage_not_independently_verified_invoice'},
        'calls':{'status':'instrument_measured','source_ref':'registered-executor-return','scope':'one_declared_dispatch_not_hidden_executor_side_effects'}}
    if isinstance(usage,dict):
        for dimension,key in (('input_tokens','prompt_tokens'),('output_tokens','completion_tokens')):
            if usage.get(key) is not None:
                measurements[dimension]=str(core.quantity(usage[key]));provenance[dimension]={'status':'provider_reported','source_ref':'raw-response:sha256:'+hashlib.sha256(raw).hexdigest()}
    return {'measurements':measurements,'measurement_provenance':provenance}


def execute_loop(request,authority,call,*,namespace,model_reservation,model_permission,model_instrument,
                 tool_specs=None,capabilities=(),usage_extractor=response_usage):
    """No network transport: callbacks must represent one declared leaf dispatch.

    Each tool spec supplies executor, usage_extractor, reservation, permission and
    instrument. Domain payload fields never imply billing. Runtime step/local
    budget settings are additional scope controls, not a shared-budget reset.
    """
    if not isinstance(namespace,str) or not namespace:raise LeaseError('loop_namespace_required')
    current=deepcopy(request);tool_specs=tool_specs or {};results=[];scope_charge=core.quantity('0')
    maximum=request['config'].get('max_steps');local_limit=core.quantity(request['config']['budget_usd'])
    def charge(receipt,reservation):
        nonlocal scope_charge
        if receipt['core_receipt']['state']!='completed':return
        response=receipt['core_receipt']['result'];amount=response['measurements'].get('money_usd')
        if amount is None:retained=core.quantity(reservation['money_usd'])
        else:
            status=response['measurement_provenance']['money_usd']['status'];policy=authority.ledger.limits['money_usd']['measurement_policy'][status]
            q=core.quantity(amount);reserved=core.quantity(reservation['money_usd'])
            retained=reserved if policy=='retain_reservation' else max(q,reserved) if policy=='max_reservation_amount' else q
        scope_charge=core.exact_sum(scope_charge,retained)
    ordinal=0;stopped='configured_step_count_reached';final=None
    while maximum is None or ordinal<maximum:
        if core.exact_sum(scope_charge,core.quantity(model_reservation['money_usd']))>local_limit:
            stopped='local_scope_allowance_exhausted';break
        ordinal+=1
        receipt=authority.run_step(f'{namespace}/model/{ordinal:04d}',current,model_reservation,
            lambda value,context:call(value),usage_extractor,interpret=frozen_runtime.parse_runtime_response,
            permission=model_permission,instrument=model_instrument,capabilities=capabilities)
        results.append({k:v for k,v in receipt.items() if k!='raw'})
        if 'core_receipt' not in receipt or receipt['state']!='completed':stopped=receipt['state'];break
        charge(receipt,model_reservation)
        if receipt['overrun_dimensions']:stopped='reported_overrun';break
        parsed=receipt['core_receipt']['result']['output']['value']
        if parsed['state']!='awaiting_tools':final=parsed.get('final_content');stopped=parsed['state'];break
        pending=parsed['pending_tools'];ids=[p['call_id'] for p in pending]
        declared=current['body'].get('tools',[])
        names={t.get('function',{}).get('name') if current['runtime_kind']=='client_loop' else t.get('name') for t in declared}
        if len(ids)!=len(set(ids)) or any(p['name'] not in names or p['name'] not in tool_specs for p in pending):
            stopped='tool_not_declared_registered_or_unique';break
        if any(current['config'].get('tool_permissions',{}).get(p['name'],{}).get('execute') is not True for p in pending):
            stopped='tool_execution_permission_missing';break
        tool_results=[]
        for p in pending:
            spec=tool_specs[p['name']]
            if core.exact_sum(scope_charge,core.quantity(spec['reservation']['money_usd']))>local_limit:
                stopped='local_scope_allowance_exhausted';break
            tool_request={'kind':'tool','call_id':p['call_id'],'name':p['name'],'arguments':deepcopy(p['arguments']),
                'model_leaf_attempt_id':receipt['attempt_id'],'packet_sha256':request['packet_sha256'],
                'semantic_contract':deepcopy(request.get('semantic_contract')),'resource_scope':deepcopy(request.get('resource_scope')),
                'runtime_permission':deepcopy(current['config']['tool_permissions'][p['name']])}
            tool_receipt=authority.run_step(f'{namespace}/model/{ordinal:04d}/tool/{p["call_id"]}',tool_request,
                spec['reservation'],spec['executor'],spec['usage_extractor'],interpret=lambda raw,value:safe.parse_json(raw),
                permission=spec['permission'],instrument=spec['instrument'],
                required_capabilities=spec.get('required_capabilities',()),capabilities=capabilities)
            results.append({k:v for k,v in tool_receipt.items() if k!='raw'})
            if 'core_receipt' not in tool_receipt or tool_receipt['state']!='completed':stopped=tool_receipt['state'];break
            charge(tool_receipt,spec['reservation'])
            if tool_receipt['overrun_dimensions']:stopped='reported_overrun';break
            tool_results.append({'call_id':p['call_id'],'name':p['name'],'output':tool_receipt['core_receipt']['result']['output']['value']})
        if len(tool_results)!=len(pending):break
        current=frozen_runtime.continue_with_tools(current,parsed,tool_results)
    return {'schema':'loom.shared_runtime_loop_receipt/1','results':results,'stopped_reason':stopped,'final_content':final,
        'scope_retained_allowance_usd':str(scope_charge),'shared_usage':authority.ledger.usage(),
        'scope_budget_is_additional_not_shared_reset':True,'accounting_owner':False,'network_transport_provided':False,
        'canonical_graph_written':False}
