"""T5: offline UI-IR validation and transactional parameter propagation.

No renderer, graph mutation, model call, tool executor, filesystem watcher or UI
profile emulation. Couplings are data; only explicitly registered local, pure
transforms execute. See docs/contracts/WORKSPACE.md for precise event semantics.
"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
import threading
from typing import Any, Callable

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.exceptions import NoSuchResource
from validate import read_json

SCHEMA_PATH = Path(__file__).resolve().parents[3] / 'docs/contracts/workspace.schema.json'
EXAMPLE_PATH = SCHEMA_PATH.parent / 'examples/workspace_v1.json'
Transform = Callable[[Any, dict], Any]


class WorkspaceError(ValueError):
    """Stable code without potentially private parameter values."""
    def __init__(self, code: str):
        self.code = code
        super().__init__(code)


def encoded(value: Any) -> bytes:
    """Finite JSON only; type-aware equality (true differs from 1)."""
    def check(x):
        if x is None or type(x) in (str, int, bool):
            return
        if type(x) is float and math.isfinite(x):
            return
        if type(x) is list:
            for v in x:
                check(v)
            return
        if type(x) is dict and all(type(k) is str for k in x):
            for v in x.values():
                check(v)
            return
        raise WorkspaceError('not_finite_json')
    try:
        check(value)
        return json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True,
                          separators=(',', ':')).encode('utf-8')
    except (RecursionError, UnicodeError, OverflowError):
        raise WorkspaceError('not_finite_json') from None


def same(a: Any, b: Any) -> bool:
    return encoded(a) == encoded(b)


def endpoint(obj: dict) -> tuple[str, str]:
    if type(obj) is not dict or set(obj) != {'owner', 'parameter'} or any(
            type(obj[k]) is not str or not obj[k] for k in obj):
        raise WorkspaceError('endpoint_shape')
    return obj['owner'], obj['parameter']


def no_remote(uri: str):
    raise NoSuchResource(ref=uri)


def identity(value, args):
    if args:
        raise WorkspaceError('transform_arguments')
    return value


def affine(value, args):
    if set(args) - {'factor', 'offset'} or type(value) not in (int, float):
        raise WorkspaceError('transform_arguments')
    factor, offset = args.get('factor', 1), args.get('offset', 0)
    if type(factor) not in (int, float) or type(offset) not in (int, float):
        raise WorkspaceError('transform_arguments')
    return value * factor + offset


def lookup(value, args):
    if set(args) != {'pairs'} or type(args['pairs']) is not list:
        raise WorkspaceError('transform_arguments')
    matches, keys = [], set()
    for pair in args['pairs']:
        if type(pair) is not dict or set(pair) != {'from', 'to'}:
            raise WorkspaceError('transform_arguments')
        key = encoded(pair['from'])
        if key in keys:
            raise WorkspaceError('transform_ambiguous_lookup')
        keys.add(key)
        if same(pair['from'], value):
            matches.append(pair['to'])
    if not matches:
        raise WorkspaceError('transform_missing_lookup')
    return matches[0]


BUILTINS: dict[str, Transform] = {'identity': identity, 'affine': affine, 'lookup': lookup}


def _parameter_valid(definition: dict, value: Any) -> None:
    encoded(value)
    allowed = {'string': (str,), 'integer': (int,), 'number': (int, float),
               'boolean': (bool,), 'array': (list,), 'object': (dict,), 'null': (type(None),)}
    if value is None and definition['nullable']:
        return
    if type(value) not in allowed[definition['type']]:
        raise WorkspaceError('parameter_type')
    constraints = definition['constraints']
    for key in ('minimum', 'maximum'):
        if key in constraints:
            if type(value) not in (int, float):
                raise WorkspaceError('numeric_constraint_type')
            if (key == 'minimum' and value < constraints[key]) or (key == 'maximum' and value > constraints[key]):
                raise WorkspaceError('parameter_bounds')
    if 'enum' in constraints and not any(same(value, v) for v in constraints['enum']):
        raise WorkspaceError('parameter_enum')


class Workspace:
    """A serializable UI state, not a canonical graph. Calls are serialized by a lock.

    Each event assigns at most one value per endpoint. Equal competing proposals
    coalesce; unequal ones (including cycles) abort the entire transaction. Seeds
    participate in this rule. Order of bindings never selects a silent winner.
    """
    def __init__(self, document: dict, *, transforms: dict[str, Transform] | None = None,
                 schema_path: Path = SCHEMA_PATH):
        encoded(document)
        schema = read_json(schema_path)
        Draft202012Validator.check_schema(schema)
        registry = Registry(retrieve=no_remote).with_resource(schema['$id'], Resource.from_contents(schema))
        try:
            if next(Draft202012Validator(schema, registry=registry).iter_errors(document), None):
                raise WorkspaceError('workspace_schema')
        except RecursionError:
            raise WorkspaceError('workspace_too_deep') from None
        self._doc = deepcopy(document)
        self._lock = threading.RLock()
        self._transforms = dict(BUILTINS)
        if transforms:
            for name, fn in transforms.items():
                if not isinstance(name, str) or not name or name in BUILTINS or not callable(fn):
                    raise WorkspaceError('transform_registration')
                self._transforms[name] = fn
        self._nodes = {self._doc['id']: self._doc}
        self._parents: dict[str, str] = {}
        queue = deque([(self._doc['root'], self._doc['id'])])
        while queue:
            node, parent = queue.popleft()
            if node['id'] in self._nodes:
                raise WorkspaceError('duplicate_node_id')
            self._nodes[node['id']] = node
            self._parents[node['id']] = parent
            if node['kind'] == 'container':
                if node['container_kind'] == 'tabs' and 'active_tab' in node['layout']:
                    if node['layout']['active_tab'] not in {c['id'] for c in node['children']}:
                        raise WorkspaceError('active_tab_missing')
                queue.extend((child, node['id']) for child in node['children'])
        self._params = {}
        for owner, node in self._nodes.items():
            for name, definition in node['parameters'].items():
                if not name:
                    raise WorkspaceError('empty_parameter_name')
                _parameter_valid(definition, definition['value'])
                limits = definition['constraints']
                if 'minimum' in limits and 'maximum' in limits and limits['minimum'] > limits['maximum']:
                    raise WorkspaceError('invalid_parameter_bounds')
                self._params[owner, name] = definition
        self._bindings = {}
        self._outgoing = defaultdict(list)
        if '*' in self._doc['scopes']:
            raise WorkspaceError('wildcard_is_not_event_scope')
        for binding in self._doc['bindings']:
            if binding['id'] in self._bindings:
                raise WorkspaceError('duplicate_binding_id')
            source, target = endpoint(binding['source']), endpoint(binding['target'])
            if source not in self._params or target not in self._params:
                raise WorkspaceError('binding_endpoint_missing')
            if binding['scope'] not in self._doc['scopes'] + ['*']:
                raise WorkspaceError('binding_scope_missing')
            if binding['transform']['op'] not in self._transforms:
                raise WorkspaceError('transform_unavailable')
            self._bindings[binding['id']] = binding
            self._outgoing[source].append(binding)
        for edges in self._outgoing.values():
            edges.sort(key=lambda b: b['id'])
        self._frozen = set(self._doc['initial_frozen'])
        if not self._frozen <= set(self._nodes):
            raise WorkspaceError('frozen_node_missing')
        self._seen: dict[str, str] = {}
        self.revision = 0

    def _is_frozen(self, owner: str) -> bool:
        while True:
            if owner in self._frozen:
                return True
            if owner not in self._parents:
                return False
            owner = self._parents[owner]

    def _value(self, ep):
        if ep not in self._params:
            raise WorkspaceError('parameter_missing')
        return self._params[ep]['value']

    def value(self, owner: str, parameter: str):
        with self._lock:
            return deepcopy(self._value((owner, parameter)))

    def document(self) -> dict:
        """Save current values/layout/profiles/couplings; no external data copied.

        Event deduplication is session-local and intentionally not a replay ledger.
        A saved viewpoint must not be described as a durable execution checkpoint.
        """
        with self._lock:
            out = deepcopy(self._doc)
            out['initial_frozen'] = sorted(self._frozen)
            return out

    def profiles(self) -> dict:
        with self._lock:
            return deepcopy(self._doc['profiles'])

    def freeze(self, node_id: str, frozen: bool = True) -> None:
        with self._lock:
            if node_id not in self._nodes or type(frozen) is not bool:
                raise WorkspaceError('freeze_target')
            was = node_id in self._frozen
            if frozen:
                self._frozen.add(node_id)
            else:
                self._frozen.discard(node_id)
            if was != frozen:
                self.revision += 1

    def bind_enabled(self, binding_id: str, enabled: bool) -> None:
        with self._lock:
            if binding_id not in self._bindings or type(enabled) is not bool:
                raise WorkspaceError('binding_toggle')
            binding = self._bindings[binding_id]
            if binding['enabled'] != enabled:
                binding['enabled'] = enabled
                self.revision += 1

    def apply_preset(self, preset: dict, *, components: list[str]) -> None:
        """Explicit component selection only; no implicit model/tools/context switch."""
        with self._lock:
            encoded(preset)
            if type(preset) is not dict or set(preset) != {'id', 'components'} or not isinstance(preset['id'], str):
                raise WorkspaceError('preset_shape')
            if type(preset['components']) is not dict or type(components) is not list or not components:
                raise WorkspaceError('preset_selection')
            if not all(type(x) is str for x in components) or len(set(components)) != len(components):
                raise WorkspaceError('preset_selection')
            if not set(components) <= set(preset['components']) or not set(components) <= set(self._doc['profiles']):
                raise WorkspaceError('profile_missing')
            selected = {}
            for name in components:
                value = preset['components'][name]
                if type(value) is not dict or set(value) != {'id', 'version', 'config'} or not all(
                    type(value[k]) is str and value[k] for k in ('id', 'version')) or type(value['config']) is not dict:
                    raise WorkspaceError('profile_shape')
                selected[name] = deepcopy(value)
            if any(not same(self._doc['profiles'][name], value) for name, value in selected.items()):
                self._doc['profiles'].update(selected)
                self.revision += 1

    def capability(self, name: str) -> str:
        """A DECLARATION from the selected profile, never an executed capability test."""
        with self._lock:
            if type(name) is not str or not name:
                raise WorkspaceError('capability_name')
            catalog = self._doc['profiles'].get('tools', {}).get('config', {}).get('capabilities', {})
            status = catalog.get(name) if type(catalog) is dict else None
            return status if type(status) is str and status in {'native', 'equivalent', 'limited', 'unavailable'} else 'unavailable'

    def dispatch(self, event: dict) -> dict:
        """{id, scope, writes:[{owner, parameter, value}], expected_revision?}.

        Even a seed equal to its old value propagates: explicit resynchronization
        after reconnect/thaw uses a NEW event id. Frozen nodes block ingress,
        egress and direct editing (including descendants of frozen containers).
        """
        with self._lock:
            encoded(event)
            if type(event) is not dict or set(event) - {'id','scope','writes','expected_revision'} or not all(
                k in event for k in ('id','scope','writes')) or not isinstance(event['id'], str) or not event['id']:
                raise WorkspaceError('event_shape')
            if event['scope'] not in self._doc['scopes'] or type(event['writes']) is not list or not event['writes']:
                raise WorkspaceError('event_scope_or_writes')
            if 'expected_revision' in event and (type(event['expected_revision']) is not int or event['expected_revision'] < 0):
                raise WorkspaceError('revision_type')
            digest = hashlib.sha256(encoded(event)).hexdigest()
            if event['id'] in self._seen:
                if self._seen[event['id']] != digest:
                    raise WorkspaceError('event_id_collision')
                return {'event_id':event['id'],'status':'replayed','revision':self.revision,'changes':[], 'trace':[]}
            if event.get('expected_revision', self.revision) != self.revision:
                raise WorkspaceError('stale_revision')
            proposed, pending, trace = {}, deque(), []
            count = 0

            def offer(ep, value):
                nonlocal count
                count += 1
                if count > self._doc['limits']['max_proposals']:
                    raise WorkspaceError('proposal_budget_exceeded')
                if ep not in self._params:
                    raise WorkspaceError('parameter_missing')
                _parameter_valid(self._params[ep], value)
                if ep in proposed:
                    if not same(proposed[ep], value):
                        raise WorkspaceError('conflicting_proposals')
                    return
                proposed[ep] = deepcopy(value)
                pending.append(ep)

            for write in event['writes']:
                if type(write) is not dict or set(write) != {'owner','parameter','value'}:
                    raise WorkspaceError('write_shape')
                ep = endpoint({k:write[k] for k in ('owner','parameter')})
                if self._is_frozen(ep[0]):
                    raise WorkspaceError('direct_write_frozen')
                offer(ep, write['value'])
            while pending:
                source = pending.popleft()
                for binding in self._outgoing[source]:
                    target = endpoint(binding['target'])
                    reason = None
                    if not binding['enabled']:
                        reason = 'detached'
                    elif binding['scope'] not in (event['scope'], '*'):
                        reason = 'different_scope'
                    elif self._is_frozen(source[0]) or self._is_frozen(target[0]):
                        reason = 'frozen'
                    if reason:
                        trace.append({'binding':binding['id'],'status':reason})
                        continue
                    transform = binding['transform']
                    try:
                        value = self._transforms[transform['op']](deepcopy(proposed[source]), deepcopy(transform['args']))
                        offer(target, value)
                    except WorkspaceError:
                        raise
                    except Exception:
                        raise WorkspaceError('transform_failure') from None
                    trace.append({'binding':binding['id'],'status':'propagated'})
            changes = []
            for ep in sorted(proposed):
                if not same(self._value(ep), proposed[ep]):
                    changes.append({'owner':ep[0],'parameter':ep[1],
                                    'before':deepcopy(self._value(ep)),'after':deepcopy(proposed[ep])})
            # Commit only after complete successful propagation/validation.
            for change in changes:
                self._params[change['owner'],change['parameter']]['value'] = deepcopy(change['after'])
            if changes:
                self.revision += 1
            self._seen[event['id']] = digest
            return {'event_id':event['id'],'status':'committed','revision':self.revision,
                    'changes':changes,'trace':trace}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('document', nargs='?', type=Path, default=EXAMPLE_PATH)
    parser.add_argument('--events', type=Path, help='JSON array of local parameter events')
    parser.add_argument('--demo', action='store_true', help='Run deterministic fixture interaction, no UI or model')
    args = parser.parse_args(argv)
    try:
        ws = Workspace(read_json(args.document))
        events = read_json(args.events) if args.events else []
        if args.demo:
            events = [{'id':'demo-1','scope':'analysis','writes':[{'owner':'history','parameter':'selection','value':'e_archive_analyzer'}]},
                      {'id':'demo-2','scope':'analysis','writes':[{'owner':'history','parameter':'depth','value':3}]}]
        if type(events) is not list:
            raise WorkspaceError('event_list_required')
        results = [ws.dispatch(e) for e in events]
        print(json.dumps({'schema':'loom.workspace_demo/1','local_only':True,
                          'event_results':results,'workspace':ws.document()}, ensure_ascii=False,indent=2))
        return 0
    except (WorkspaceError,OSError,ValueError,RecursionError) as exc:
        print(json.dumps({'valid':False,'error':exc.code if isinstance(exc,WorkspaceError) else 'input_error'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
