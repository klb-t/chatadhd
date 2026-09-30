"""Offline executable runtime adapters: local, client tools, hosted Responses agents.

The transport callback is injected for scripted/replay experiments; this module
has no credential loader or network implementation. Hosted API availability is
not established by a successful mocked run. Runtime kinds can be registered.
"""
from __future__ import annotations
from copy import deepcopy
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
from pathlib import Path

from . import adapter as a
from .. import openrouter_runner as safe

BUILDERS = {}
PARSERS = {}


def register_runtime(kind, builder, parser):
    if not isinstance(kind, str) or not kind or not callable(builder) or not callable(parser):
        raise ValueError('runtime_registration_invalid')
    BUILDERS[kind] = builder; PARSERS[kind] = parser


def _base(packet, config):
    from ..agentic_graph_v1 import packet as codec
    codec.validate_packet(packet, resource_limits=config.get('packet_resource_limits'))
    if config.get('schema') != 'loom.analysis_runtime/1':
        raise ValueError('runtime_configuration_schema_invalid')
    a.money(config['budget_usd']); a.money(config['reservation_per_call_usd'])
    if (a.money(config['budget_usd']) <= 0 or a.money(config['reservation_per_call_usd']) < 0):
        raise ValueError('runtime_budget_configuration_invalid')
    maximum = config.get('max_steps')
    if maximum is not None and (type(maximum) is not int or maximum < 1):
        raise ValueError('runtime_step_configuration_invalid')
    return {'schema': 'loom.analysis_runtime_request/1', 'runtime_kind': config['kind'],
            'config': deepcopy(config), 'packet_sha256': packet['packet_id'],
            'account_availability_verified': False, 'scripted_or_replay_transport_only': True,
            'semantic_contract': deepcopy(config.get('semantic_contract')),
            'resource_scope': deepcopy(config.get('resource_scope')),
            'tool_capability_is_not_execution_permission': True}


def _local(packet, config):
    value = _base(packet, config)
    value.update(method='LOCAL', endpoint=None, headers={}, body=deepcopy(packet))
    return value


def _client(packet, config):
    value = _base(packet, config)
    body = a.packet_body(packet, model_key=config['model_key'], recipe=config['recipe'],
                         config_override=config.get('model_config_override'))
    if config.get('tools'):
        presets, models, identities = a.load_presets()
        if 'tools' not in identities[config['model_key']]['endpoint']['supported_parameters']:
            raise ValueError('observed_endpoint_tool_capability_unavailable')
        body['tools'] = deepcopy(config['tools'])
        for tool in body['tools']:
            if tool.get('type') != 'function' or not isinstance(tool.get('function'), dict):
                raise ValueError('client_tool_definition_invalid')
            fn=tool['function']
            if not isinstance(fn.get('name'),str) or not fn['name'] or not isinstance(fn.get('parameters'),dict):
                raise ValueError('client_tool_definition_invalid')
        body['tool_choice'] = deepcopy(config.get('tool_choice','auto'))
        body['parallel_tool_calls'] = config.get('parallel_tool_calls', True)
        if type(body['parallel_tool_calls']) is not bool:
            raise ValueError('parallel_tool_configuration_invalid')
    value.update(method='POST', endpoint='https://openrouter.ai/api/v1/chat/completions',headers={},body=body)
    return value


def _managed(packet, config):
    value = _base(packet, config)
    recipes = a.load_recipes(); system=recipes['tracks']['graph_packet'][config['recipe']]['system']
    body={'model':config['model'], 'input':[{'role':'developer','content':system},
            {'role':'user','content':safe.canonical(packet).decode()}],
          'max_output_tokens':config['max_output_tokens'], 'store':config.get('store',False)}
    if type(body['max_output_tokens']) is not int or body['max_output_tokens']<1 or type(body['store']) is not bool:
        raise ValueError('managed_output_or_store_configuration_invalid')
    if config.get('reasoning') is not None:
        body['reasoning']=deepcopy(config['reasoning'])
    if config.get('tools'):
        body['tools']=deepcopy(config['tools'])
    headers={}
    if config['kind']=='managed_swarm':
        if config.get('subagent_models'):
            raise ValueError('hosted_responses_subagents_share_model_use_loom_for_heterogeneous_models')
        concurrency=config.get('max_concurrent_subagents',3)
        if type(concurrency) is not int or concurrency<1:
            raise ValueError('managed_subagent_concurrency_invalid')
        if 'summary' in body.get('reasoning',{}) or 'max_tool_calls' in config:
            raise ValueError('documented_multi_agent_parameter_not_supported')
        body['multi_agent']={'enabled':True,'max_concurrent_subagents':concurrency}
        headers['OpenAI-Beta']='responses_multi_agent=v1'
    value.update(method='POST',endpoint='https://api.openai.com/v1/responses',headers=headers,body=body,
                 documented_api='Responses API hosted tooling and optional beta multi-agent',
                 persistent_agents_api_session=False)
    return value


def prepare_runtime(packet, config):
    kind=config.get('kind')
    if kind not in BUILDERS:
        raise ValueError('runtime_kind_not_registered')
    result=BUILDERS[kind](deepcopy(packet),deepcopy(config))
    result['request_sha256']=safe.digest({k:v for k,v in result.items() if k!='request_sha256'})
    return result


def _billing(value):
    usage=value.get('usage') if isinstance(value,dict) else None
    cost=usage.get('cost') if isinstance(usage,dict) else None
    return {'reported_cost_usd':None if cost is None else str(a.money(cost)),
            'reported_usage':deepcopy(usage),'cost_status':'unknown' if cost is None else 'provider_reported'}


def _parse_local(raw, request):
    value=safe.parse_json(raw)
    return {'state':'completed','final_content':safe.canonical(value).decode(),'pending_tools':[],
            'continuation_items':[], 'reported_cost_usd':'0','reported_usage':None,'cost_status':'local_no_model_call'}


def _parse_client(raw, request):
    value=safe.parse_json(raw); config=request['config']
    presets, models, identities=a.load_presets(); identity=identities[config['model_key']]
    if value.get('model') not in identity['model_aliases'] or value.get('provider') not in identity['provider_aliases']:
        raise a.IdentityIntegrityError('runtime_model_provider_identity_unobserved')
    billing=_billing(value)
    if not isinstance(value.get('usage'),dict) or value['usage'].get('is_byok') is not False:
        raise ValueError('runtime_billing_mode_unknown_or_byok')
    if value.get('error') is not None or len(value.get('choices',[]))!=1:
        return dict(billing,state='rejected',reason='provider_error_or_choice_shape',pending_tools=[],continuation_items=[])
    choice=value['choices'][0]; message=choice.get('message',{})
    if message.get('refusal') or choice.get('finish_reason') not in ('stop','tool_calls'):
        return dict(billing,state='rejected',reason='refusal_or_incomplete',pending_tools=[],continuation_items=[])
    pending=[]
    for call in message.get('tool_calls',[]):
        if call.get('type')!='function' or not isinstance(call.get('id'),str):
            raise ValueError('client_tool_call_shape_invalid')
        args=safe.parse_json(call['function']['arguments'])
        if not isinstance(args,dict): raise ValueError('client_tool_arguments_not_object')
        pending.append({'call_id':call['id'],'name':call['function']['name'],'arguments':args,'original':deepcopy(call)})
    if pending and choice['finish_reason']!='tool_calls':
        raise ValueError('client_tool_finish_reason_mismatch')
    return dict(billing,state='awaiting_tools' if pending else 'completed',
                final_content=None if pending else message.get('content'),pending_tools=pending,
                continuation_items=[deepcopy(message)])


def _parse_managed(raw, request):
    value=safe.parse_json(raw); billing=_billing(value)
    if value.get('model')!=request['config']['model']:
        raise a.IdentityIntegrityError('managed_response_model_configuration_mismatch')
    if value.get('status') not in ('completed','in_progress') or value.get('error') is not None:
        return dict(billing,state='rejected',reason='managed_response_incomplete',pending_tools=[],continuation_items=[])
    pending=[]; text=[]; items=value.get('output',[])
    if not isinstance(items,list): raise ValueError('managed_output_array_invalid')
    for item in items:
        if item.get('type')=='function_call':
            args=safe.parse_json(item['arguments'])
            if not isinstance(args,dict): raise ValueError('managed_function_arguments_not_object')
            pending.append({'call_id':item['call_id'],'name':item['name'],'arguments':args,'original':deepcopy(item)})
        elif item.get('type')=='message':
            if request['runtime_kind']=='managed_swarm' and (item.get('agent',{}).get('agent_name')!='/root' or item.get('phase')!='final_answer'):
                continue
            for part in item.get('content',[]):
                if part.get('type')=='output_text': text.append(part['text'])
        # Hosted multi_agent_call items are provider orchestration, never client
        # tool invocations. Preserve them for continuation/replay unchanged.
    if value.get('status') != 'completed' and not pending:
        return dict(billing,state='rejected',reason='managed_response_still_in_progress',pending_tools=[],continuation_items=deepcopy(items))
    return dict(billing,state='awaiting_tools' if pending else 'completed',
                final_content=None if pending else ''.join(text),pending_tools=pending,
                continuation_items=deepcopy(items))


for _kind,_builder,_parser in [('local',_local,_parse_local),('client_loop',_client,_parse_client),
                              ('managed_agent',_managed,_parse_managed),('managed_swarm',_managed,_parse_managed)]:
    register_runtime(_kind,_builder,_parser)


def parse_runtime_response(raw, request):
    return PARSERS[request['runtime_kind']](raw,request)


def continue_with_tools(request, parsed, results):
    if {p['call_id'] for p in parsed['pending_tools']}!={r['call_id'] for r in results}:
        raise ValueError('tool_result_inventory_mismatch')
    out=deepcopy(request)
    if request['runtime_kind']=='client_loop':
        out['body']['messages'].extend(deepcopy(parsed['continuation_items']))
        out['body']['messages'].extend({'role':'tool','tool_call_id':r['call_id'],
                'content':safe.canonical(r['output']).decode()} for r in results)
    else:
        out['body']['input'].extend(deepcopy(parsed['continuation_items']))
        out['body']['input'].extend({'type':'function_call_output','call_id':r['call_id'],
                'output':safe.canonical(r['output']).decode()} for r in results)
    out['request_sha256']=safe.digest({k:v for k,v in out.items() if k!='request_sha256'})
    return out


def _persist(path,value):
    safe._atomic(Path(path),safe.canonical(value)+b'\n')


def execute_scripted(request, output_dir, call, *, tool_handlers=None):
    """Run an injected script once per stage, with durable first artifacts.

    Unknown billing consumes its full declared reservation; an ambiguous callback
    stops. Existing artifact directories never restart, so replay is a distinct
    read-only action. This function does not imply network execution support.
    """
    directory=Path(output_dir);directory.mkdir(parents=True,exist_ok=False)
    current=deepcopy(request); handlers=tool_handlers or {}; attempts=[]; retained=Decimal(0)
    config=request['config']; budget=a.money(config['budget_usd']); reservation=a.money(config['reservation_per_call_usd'])
    ledger={'schema':'loom.scripted_runtime_ledger/1','request_sha256':request['request_sha256'],
            'attempts':attempts,'scripted':True,'network_calls':0,'canonical_store_written':False}
    _persist(directory/'initial_request.json',request);_persist(directory/'ledger.json',ledger)
    step=0
    while config.get('max_steps') is None or step<config['max_steps']:
        if retained+reservation>budget:
            ledger['stopped_reason']='declared_reservation_budget_exhausted';break
        step+=1;ident=f'call{step:04d}';row={'id':ident,'state':'started','request_sha256':safe.digest(current),
            'reservation_usd':str(reservation),'started_at':datetime.now(timezone.utc).isoformat()}
        attempts.append(row);_persist(directory/(ident+'.request.json'),current);_persist(directory/'ledger.json',ledger)
        try:
            raw=call(deepcopy(current))
            if not isinstance(raw,bytes): raise ValueError('scripted_callback_must_return_bytes')
            safe._atomic(directory/(ident+'.response.bin'),raw)
            row.update(response_file=ident+'.response.bin',response_sha256=hashlib.sha256(raw).hexdigest())
            initial_billing = {'reported_cost_usd':'0','reported_usage':None,'cost_status':'local_no_model_call'} if current['runtime_kind']=='local' else _billing(safe.parse_json(raw))
            row.update(initial_billing)
            initial_charge = reservation if initial_billing['reported_cost_usd'] is None else a.money(initial_billing['reported_cost_usd'])
            row['retained_budget_charge_usd'] = str(initial_charge)
            _persist(directory/'ledger.json',ledger)
            parsed=parse_runtime_response(raw,current)
            row.update(state=parsed['state'],reported_cost_usd=parsed['reported_cost_usd'],cost_status=parsed['cost_status'],
                       finished_at=datetime.now(timezone.utc).isoformat())
            cost=reservation if parsed['reported_cost_usd'] is None else a.money(parsed['reported_cost_usd'])
            retained+=cost;row['retained_budget_charge_usd']=str(cost)
            _persist(directory/(ident+'.parsed.json'),parsed);_persist(directory/'ledger.json',ledger)
            if cost>reservation or retained>budget:
                ledger['stopped_reason']='reported_cost_exceeded_declaration';break
            if parsed['state']!='awaiting_tools':
                ledger['final_content']=parsed.get('final_content');ledger['stopped_reason']=parsed['state'];break
            results=[]
            declared=(current['body'].get('tools') or [])
            allowed={t.get('function',{}).get('name') if current['runtime_kind']=='client_loop' else t.get('name') for t in declared}
            ids=[p['call_id'] for p in parsed['pending_tools']]
            permissions = config.get('tool_permissions', {})
            if len(ids)!=len(set(ids)) or any(p['name'] not in handlers or p['name'] not in allowed for p in parsed['pending_tools']):
                raise ValueError('tool_not_registered_declared_or_unique')
            if any(not isinstance(permissions.get(p['name']), dict) or permissions[p['name']].get('execute') is not True for p in parsed['pending_tools']):
                raise ValueError('tool_capability_has_no_execution_permission')
            for pending in parsed['pending_tools']:
                context = {'permission': deepcopy(permissions[pending['name']]),
                           'semantic_contract': deepcopy(config.get('semantic_contract')),
                           'resource_scope': deepcopy(config.get('resource_scope')),
                           'packet_sha256': request['packet_sha256'],
                           'executor_binding': pending['name']}
                result=handlers[pending['name']](deepcopy(pending['arguments']), context)
                results.append({'call_id':pending['call_id'],'name':pending['name'],'output':result,'execution_context':context})
            _persist(directory/(ident+'.tool_results.json'),results)
            current=continue_with_tools(current,parsed,results)
        except Exception as exc:
            if row['state']=='started': row['state']='rejected' if 'response_file' in row else 'uncertain'
            row['failure_class']=type(exc).__name__
            row.setdefault('retained_budget_charge_usd',str(reservation));retained=max(retained,sum((a.money(r['retained_budget_charge_usd']) for r in attempts),Decimal(0)))
            ledger['stopped_reason']='stage_failure_no_retry';break
    else:
        ledger['stopped_reason']='configured_step_count_reached'
    ledger['retained_budget_charge_usd']=str(retained);_persist(directory/'ledger.json',ledger)
    return ledger


def replay_scripted(output_dir):
    directory=Path(output_dir);ledger=a.read(directory/'ledger.json');initial=a.read(directory/'initial_request.json')
    if ledger['request_sha256']!=initial['request_sha256']:
        raise a.IdentityIntegrityError('scripted_initial_request_drift')
    outputs=[]
    for attempt in ledger['attempts']:
        request=a.read(directory/(attempt['id']+'.request.json'))
        if safe.digest(request)!=attempt['request_sha256']:
            raise a.IdentityIntegrityError('scripted_attempt_request_drift')
        if 'response_file' not in attempt:
            outputs.append({'state':attempt['state']});continue
        raw=(directory/attempt['response_file']).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=attempt['response_sha256']:
            raise a.IdentityIntegrityError('scripted_first_response_hash_drift')
        try: parsed=parse_runtime_response(raw,request)
        except (ValueError,KeyError,TypeError,a.IdentityIntegrityError):
            if attempt.get('failure_class') is None: raise
            outputs.append({'state':'replay_rejected','failure_class':attempt['failure_class']});continue
        if parsed['reported_cost_usd']!=attempt.get('reported_cost_usd'):
            raise a.IdentityIntegrityError('scripted_replay_cost_drift')
        outputs.append(parsed)
    return {'ledger':ledger,'outputs':outputs,'network_calls':0,'replay_only':True}
