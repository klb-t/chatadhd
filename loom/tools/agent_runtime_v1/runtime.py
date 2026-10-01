"""An injected planner/action loop using the existing durable local dispatch store.

Domain payloads and JSON schemas are retained. The caller supplies tools,
environment capabilities, planner identity and limits. No implicit fallback or
effect retry. This reference is Linux/local, not a production permission service.
"""
from copy import deepcopy
from dataclasses import dataclass
import fcntl
import json
from pathlib import Path
from typing import Callable

from jsonschema import Draft202012Validator

from ..coordination.leases import LeaseStore, digest, execute_once
from ..contracts.analysis_plan_ref import _write_new


class AgentError(ValueError):
    pass


def validate(schema, value):
    digest(value)  # Reject non-finite JSON and coercions before schema checking.
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(value)


@dataclass(frozen=True)
class Environment:
    id: str
    kind: str
    capabilities: tuple
    config: dict
    isolation: str

    def descriptor(self):
        return deepcopy({'id': self.id, 'kind': self.kind,
            'capabilities': list(self.capabilities), 'config': self.config,
            'isolation': self.isolation})


@dataclass(frozen=True)
class Tool:
    id: str
    revision: str
    input_schema: dict
    output_schema: dict
    required_capabilities: tuple
    effects: dict
    invoke: Callable
    metadata: dict

    def descriptor(self):
        return deepcopy({'id': self.id, 'revision': self.revision,
            'input_schema': self.input_schema, 'output_schema': self.output_schema,
            'required_capabilities': list(self.required_capabilities),
            'effects': self.effects, 'metadata': self.metadata})


ACTION_SCHEMA = {'type': 'object', 'additionalProperties': False,
    'required': ['kind', 'tool', 'arguments'], 'properties': {
        'kind': {'const': 'action'}, 'tool': {'type': 'string'},
        'arguments': {'type': 'object'}}}
FINAL_SCHEMA = {'type': 'object', 'additionalProperties': False,
    'required': ['kind', 'value'], 'properties': {
        'kind': {'const': 'final'}, 'value': {}}}
RETURN_SCHEMA = {'type': 'object', 'additionalProperties': False,
    'required': ['execution_status', 'payload'], 'properties': {
        'execution_status': {'enum': ['completed', 'tool_error', 'outcome_unknown']},
        'payload': {}}}


def observed(payload, status='completed'):
    return {'execution_status': status, 'payload': payload}


class AgentRuntime:
    def __init__(self, directory, *, tools, environments, source_commit):
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.store = LeaseStore(self.directory / 'actions.sqlite')
        self.tools = {t.id: t for t in tools}
        self.environments = {e.id: e for e in environments}
        if len(self.tools) != len(tools) or len(self.environments) != len(environments):
            raise AgentError('duplicate_identity')
        self.source_commit = source_commit

    def run(self, *, session_id, goal, environment_id, planner_id, planner,
            max_steps, lease_seconds, owner):
        if type(max_steps) is not int or max_steps < 1:
            raise AgentError('positive_step_limit_required')
        if type(lease_seconds) not in (int, float) or lease_seconds <= 0:
            raise AgentError('positive_lease_required')
        environment = self.environments.get(environment_id)
        if environment is None:
            return {'state': 'unavailable', 'reason': 'selected_environment_unavailable',
                    'environment_id': environment_id, 'dispatch_count': 0}
        folder = self.directory / digest(session_id)
        folder.mkdir(exist_ok=True)
        # Independent sessions may run concurrently. One session owns the local
        # planning journal; OS process death releases this lock automatically.
        with (folder / 'session.lock').open('a+b') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            catalog = [self.tools[key].descriptor() for key in sorted(self.tools)]
            specification = {'schema': 'ecosystem.agent_session/experimental-1',
                'session_id': session_id, 'goal': goal,
                'environment': environment.descriptor(), 'catalog': catalog,
                'planner_id': planner_id, 'max_steps': max_steps,
                'lease_seconds': lease_seconds, 'source_commit': self.source_commit}
            path = folder / 'session_first.json'
            if path.exists():
                if digest(json.loads(path.read_text())) != digest(specification):
                    raise AgentError('session_binding_changed')
            else:
                _write_new(path, specification)
            observations = []
            for step in range(max_steps):
                stepdir = folder / ('step-%04d' % step)
                stepdir.mkdir(exist_ok=True)
                response_path = stepdir / 'planner_first.json'
                if response_path.exists():
                    response = json.loads(response_path.read_text())
                else:
                    response = planner(deepcopy({'goal': goal, 'environment':
                        environment.descriptor(), 'catalog': catalog,
                        'observations': observations, 'step': step}))
                    _write_new(response_path, response)
                try:
                    if response.get('kind') == 'final':
                        validate(FINAL_SCHEMA, response)
                        return {'state': 'completed', 'value': response['value'],
                            'observations': observations, 'planner_id': planner_id}
                    validate(ACTION_SCHEMA, response)
                    tool = self.tools.get(response['tool'])
                    if tool is None:
                        raise AgentError('tool_unavailable')
                    missing = set(tool.required_capabilities) - set(environment.capabilities)
                    if missing:
                        raise AgentError('selected_environment_missing_capabilities:' + ','.join(sorted(missing)))
                    validate(tool.input_schema, response['arguments'])
                except (ValueError, AttributeError, TypeError) as exc:
                    return {'state': 'unavailable', 'reason': str(exc),
                            'observations': observations, 'dispatch': 'no_dispatch'}
                # jsonschema errors are caught separately: they are not ValueError.
                except Exception as exc:
                    from jsonschema.exceptions import ValidationError, SchemaError
                    if not isinstance(exc, (ValidationError, SchemaError)):
                        raise
                    return {'state': 'unavailable', 'reason': type(exc).__name__,
                            'observations': observations, 'dispatch': 'no_dispatch'}
                action_id = 'agent:' + digest([session_id, step])
                spec = {'session_sha256': digest(specification), 'step': step,
                    'tool': tool.descriptor(), 'environment': environment.descriptor(),
                    'arguments': deepcopy(response['arguments'])}
                self.store.register(action_id, source_commit=self.source_commit, spec=spec)

                def dispatch(bound, lease):
                    returned = tool.invoke(deepcopy(environment), deepcopy(bound['arguments']))
                    # Preserve the first finite JSON result before interpreting it.
                    _write_new(stepdir / 'tool_return_first.json', returned)
                    validate(RETURN_SCHEMA, returned)
                    if returned['execution_status'] == 'completed':
                        validate(tool.output_schema, returned['payload'])
                        if 'structured_output_schema' in tool.metadata:
                            validate(tool.metadata['structured_output_schema'],
                                     returned['payload']['structuredContent'])
                    return returned

                failure = None
                try:
                    execute_once(self.store, action_id, owner=owner,
                                 lease_seconds=lease_seconds, callback=dispatch)
                except Exception as exc:
                    failure = type(exc).__name__
                action = self.store.inspect(action_id)
                if action['state'] != 'succeeded':
                    return {'state': 'blocked', 'reason': action['state'],
                            'exception_type': failure, 'action_id': action_id,
                            'observations': observations, 'automatic_retry': False}
                completion = self.store.receipts(action_id)[-1]
                outcome = completion['detail']['outcome']
                observations.append({'step': step, 'action_id': action_id,
                    'tool': tool.id, 'environment_id': environment.id,
                    'result': deepcopy(outcome)})
                if outcome['execution_status'] == 'outcome_unknown':
                    return {'state': 'blocked', 'reason': 'tool_outcome_unknown',
                            'observations': observations, 'automatic_retry': False}
            return {'state': 'step_limit_reached', 'observations': observations,
                    'automatic_continuation': False}
