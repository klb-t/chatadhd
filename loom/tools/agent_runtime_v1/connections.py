"""Offline connection contracts. No secret loading, HTTP call or provisioning.

OpenRouter supplies model decisions; GCP supplies an execution target. Neither
is the ecosystem agent itself. Production dispatch/accounting remain separate.
"""
from copy import deepcopy
import json
import re

from .runtime import AgentError, validate
from ..coordination.leases import digest


def openrouter_request(context, *, model, parameters, secret_ref='api_key'):
    """Compile an initial/stateless planning request, retaining tool schemas.

    A live conversation adapter must additionally round-trip assistant/tool-call
    messages and opaque reasoning fields. This compiler does not call a model.
    """
    if type(model) is not str or not model.strip():
        raise AgentError('explicit_model_required')
    if type(secret_ref) is not str or not secret_ref.strip():
        raise AgentError('secret_reference_required')
    if {'model', 'messages', 'tools', 'stream'} & set(parameters):
        raise AgentError('reserved_request_fields')
    names, tools = {}, []
    for tool in context['catalog']:
        name = 'tool_' + digest(tool['id'])[:24]
        if name in names:
            raise AgentError('tool_name_collision')
        names[name] = tool['id']
        tools.append({'type': 'function', 'function': {
            'name': name, 'description': json.dumps({'id': tool['id'],
                'revision': tool['revision'], 'effects': tool['effects']}, ensure_ascii=False),
            'parameters': deepcopy(tool['input_schema'])}})
    payload = {'model': model, 'stream': False,
        'messages': [{'role': 'user', 'content': json.dumps({'goal': context['goal'],
            'environment': context['environment'], 'observations': context['observations']},
            ensure_ascii=False, allow_nan=False)}], 'tools': tools, **deepcopy(parameters)}
    digest(payload)
    return {'endpoint': 'https://openrouter.ai/api/v1/chat/completions',
        'payload': payload, 'tool_names': names,
        'secret_ref': secret_ref, 'network_dispatched': False,
        'transformation': {'preserved': ['input_schema', 'effects', 'domain_payload', 'units'],
            'added': ['protocol_function_alias'], 'lost': [], 'reversible_aliases': True}}


def openrouter_decisions(raw_response, compiled):
    """Parse one nonstreaming choice; retain all requested calls and raw data.

    The caller must preserve raw bytes before calling this function and perform
    permission, budget and environment admission before dispatching any action.
    """
    response = json.loads(raw_response)
    digest(response)
    choices = response.get('choices', [])
    if len(choices) != 1:
        raise AgentError('one_provider_choice_required')
    message = choices[0]['message']
    calls = message.get('tool_calls') or []
    actions = []
    seen = set()
    for call in calls:
        if call['type'] != 'function' or call['id'] in seen:
            raise AgentError('unsupported_or_duplicate_tool_call')
        seen.add(call['id'])
        name = call['function']['name']
        if name not in compiled['tool_names']:
            raise AgentError('provider_requested_undeclared_tool')
        arguments = json.loads(call['function']['arguments'])
        declaration = next(t['function'] for t in compiled['payload']['tools']
                           if t['function']['name'] == name)
        validate(declaration['parameters'], arguments)
        actions.append({'kind': 'action', 'tool': compiled['tool_names'][name],
                        'arguments': arguments, 'provider_tool_call_id': call['id']})
    return {'actions': actions, 'final_content': message.get('content') if not calls else None,
        'assistant_message': deepcopy(message), 'provider_response': response,
        'response_sha256': digest(response), 'dispatch': 'none',
        'parallel_execution_implied': False, 'raw_response_must_be_retained': True}


def gcp_target(*, project, zone, instance, connection, credential_ref):
    """Describe a user-selected existing machine without substituting another.

    Availability, IAM and isolation remain unverified until a remote probe.
    Explicit references identify credentials; credentials are never included.
    """
    for label, value in (('project', project), ('zone', zone), ('instance', instance)):
        if type(value) is not str or not re.fullmatch(r'[a-z0-9][a-z0-9:._-]*', value):
            raise AgentError('invalid_gcp_' + label)
    if connection not in ('iap_ssh', 'ssh', 'application_endpoint'):
        raise AgentError('unsupported_connection_method')
    if type(credential_ref) is not str or not credential_ref.strip():
        raise AgentError('credential_reference_required')
    return {'id': 'gcp:' + project + '/' + zone + '/' + instance,
        'kind': 'gcp.compute_instance', 'resource':
            'projects/' + project + '/zones/' + zone + '/instances/' + instance,
        'connection': connection, 'credential_ref': credential_ref,
        'capabilities': [], 'availability': 'unverified',
        'isolation': 'unverified_until_remote_probe', 'fallback': 'none',
        'provisioning_dispatched': False, 'workload_identity': 'attached_service_account_or_impersonation'}
