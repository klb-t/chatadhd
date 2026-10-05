#!/usr/bin/env python3
"""Offline native consumer for method-graph producer artifacts, never a registry."""
from __future__ import annotations

import argparse
from copy import deepcopy
import ctypes
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile

if __package__ in (None, ''):
    sys.path.insert(0, str(Path(__file__).resolve().parents[4]))

from loom.tools.coordination.graph_store import NativeGraphStore
from loom.tools.structure.agentic_graph_v1 import packet as codec


class ArtifactError(ValueError):
    pass


def require(condition, code):
    if not condition:
        raise ArtifactError(code)


def same(left, right):
    return codec.safe.canonical(left) == codec.safe.canonical(right)


def protected_attrs(attrs):
    """Version content is immutable; descriptive annotations retain history."""
    return {key: attrs[key] for key in ('definition', 'definition_sha256', 'text', 'text_sha256')
            if key in attrs}


def packet_call(store, command):
    store.library.loom_packet.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    store.library.loom_packet.restype = ctypes.c_void_p
    result = store._take(store.library.loom_packet(
        store.context, json.dumps(command, ensure_ascii=False, allow_nan=False).encode()))
    if 'error' in result:
        raise ArtifactError('native_packet:' + result['error']['message'])
    return result


def validate_shape(contract, trace):
    try:
        from jsonschema import Draft202012Validator, FormatChecker, ValidationError
    except ImportError as error:
        raise ArtifactError('schema_validator_unavailable: install loom/tools/contracts/requirements.txt') from error
    schema = json.loads((Path(__file__).resolve().parents[1] / 'method-graph.schema.json').read_text())
    checker = FormatChecker()
    require('date-time' in checker.checkers, 'date_time_checker_unavailable')
    validator = Draft202012Validator(schema, format_checker=checker)
    try:
        validator.validate(contract)
        validator.validate(trace)
    except ValidationError as error:
        raise ArtifactError('method_graph_shape:' + error.json_path) from error


def validate_relations(contract, trace, packet, result_ids, definition_source_id, trace_source_id):
    validate_shape(contract, trace)
    bindings = contract['bindings']
    kinds = contract['vocabulary']['kinds']
    predicates = contract['vocabulary']['predicates']
    entities = {row['id']: row for row in packet['entities']}
    sources = {row['observation']['id']: row['observation'] for row in packet['sources']}
    observed_phase = 'requested_model' in contract['trace'] and 'actual_models' in trace
    require(len({bindings[role] for role in ('method_identity_id', 'method_version_id', 'run_id')}) == 3,
            'distinct_method_version_run')
    for role, id_ in bindings.items():
        if id_ is not None:
            require(id_ in entities, 'unknown_binding:' + role)
            kind_role = 'method' if role == 'method_identity_id' else role.removesuffix('_id')
            if role in {'method_identity_id', 'method_version_id', 'run_id', 'parameter_set_version_id',
                        'prompt_version_id', 'recipe_version_id', 'preset_version_id',
                        'combination_version_id', 'model_identity_id', 'compiler_transform_id'}:
                require(kind_role in kinds, 'missing_kind:' + kind_role)
            if kind_role in kinds:
                require(entities[id_]['kind'] == kinds[kind_role] and entities[id_]['status'] == 'active',
                        'binding_kind:' + role)
        if role in trace and not (observed_phase and role == 'model_identity_id'):
            require(trace[role] == id_, 'trace_binding:' + role)
    for role, binding in (('method', 'method_identity_id'), ('method_version', 'method_version_id'),
                          ('run', 'run_id')):
        require(entities[bindings[binding]]['kind'] == kinds[role], 'binding_kind:' + binding)
        require(trace[binding] == bindings[binding], 'trace_binding:' + binding)

    run = entities[bindings['run_id']]['attrs']
    if 'result_bindings' in run:
        require(same(run['result_bindings'], trace.get('result_bindings')), 'run_trace:result_bindings')
    for key, value in trace.items():
        if key != 'result_bindings':
            require(key in run and same(run[key], value), 'run_trace:' + key)
    # Measurements and projection state can change after a prepared attempt.
    phase_fields = {'measurements', 'measurement_scope', 'projection_status', 'projected_at',
                    'response_sha256', 'compilation_sha256', 'response_provenance', 'result_bindings'}
    if observed_phase:
        phase_fields.update(('model', 'model_identity_id', 'model_identity_basis'))
    for key, value in contract['trace'].items():
        if key not in phase_fields:
            require(key in trace and same(trace[key], value), 'prepared_trace:' + key)

    def edge(subject, role, object_):
        require(role in predicates, 'missing_predicate:' + role)
        matches = [claim for claim in packet['claims'] if claim['subject'] == subject
                   and claim['predicate'] == predicates[role] and claim['object'] == object_
                   and claim['value'] is None and claim['assessment']['status'] == 'active']
        require(bool(matches), 'missing_edge:' + role)

    edge(bindings['method_version_id'], 'version_of', bindings['method_identity_id'])
    edge(bindings['run_id'], 'requests_method_version', bindings['method_version_id'])

    hash_roles = {'method_version': ('method_version_id', None),
                  'parameter_set': ('parameter_set_version_id', 'parameter_set_sha256'),
                  'recipe': ('recipe_version_id', 'recipe_sha256'),
                  'preset': ('preset_version_id', 'preset_sha256'),
                  'combination': ('combination_version_id', 'combination_sha256')}
    for role, (binding, trace_hash) in hash_roles.items():
        id_ = bindings.get(binding)
        if id_ is None:
            continue
        attrs = entities[id_]['attrs']
        computed = codec.digest(attrs['definition'])
        require(attrs['definition_sha256'] == computed, 'definition_hash:' + role)
        require(contract['definition_hashes'].get(role) == computed, 'manifest_hash:' + role)
        if trace_hash:
            require(trace.get(trace_hash) == computed, 'trace_hash:' + role)

    parameter_id = bindings.get('parameter_set_version_id')
    require(parameter_id is not None, 'missing_parameter_set_binding')
    parameters = entities[parameter_id]['attrs']['definition']
    require(same(parameters['effective_parameters'], trace['effective_parameters']), 'effective_parameters')
    require(same(parameters.get('user_overrides'), trace.get('user_overrides')), 'user_overrides')
    method_definition = entities[bindings['method_version_id']]['attrs']['definition']
    require(method_definition.get('parameter_set_sha256') == trace['parameter_set_sha256'],
            'method_parameter_hash')
    edge(bindings['method_version_id'], 'uses_parameter_set', parameter_id)
    edge(bindings['run_id'], 'uses_parameter_set', parameter_id)

    recipe_id = bindings.get('recipe_version_id')
    if recipe_id is not None:
        recipe = entities[recipe_id]['attrs']['definition']
        require(same(recipe['parameters'], trace['effective_parameters']), 'recipe_parameters')
        require(method_definition.get('recipe_sha256') == trace['recipe_sha256'], 'method_recipe_hash')
        edge(bindings['method_version_id'], 'uses_recipe', recipe_id)
    prompt_id = bindings.get('prompt_version_id')
    if prompt_id is not None:
        prompt = entities[prompt_id]['attrs']
        computed = hashlib.sha256(prompt['text'].encode('utf-8')).hexdigest()
        require(prompt['text_sha256'] == computed and
                contract['definition_hashes'].get('prompt_bytes') == computed and
                trace.get('prompt_sha256') == computed, 'prompt_hash')
        if recipe_id is not None:
            require(recipe.get('prompt_sha256') == computed, 'recipe_prompt_hash')
            edge(recipe_id, 'uses_prompt', prompt_id)
    preset_id = bindings.get('preset_version_id')
    if preset_id is not None:
        if 'preset_sha256' in method_definition:
            require(method_definition['preset_sha256'] == trace['preset_sha256'], 'method_preset_hash')
        edge(bindings['method_version_id'], 'uses_preset', preset_id)
    combination_id = bindings.get('combination_version_id')
    immutable = {id_ for role, id_ in bindings.items()
                 if id_ is not None and role.endswith('_version_id')}

    def version(id_, role):
        require(isinstance(id_, str) and id_ in entities, 'unknown_combination_member')
        row = entities[id_]
        require(row['kind'] == kinds[role] and row['status'] == 'active', 'combination_member_kind')
        attrs = row['attrs']
        require(isinstance(attrs.get('definition'), dict) and
                attrs.get('definition_sha256') == codec.digest(attrs['definition']),
                'combination_member_hash')
        immutable.add(id_)
        return attrs['definition']

    def member_target(member):
        require(isinstance(member, dict), 'combination_member_object')
        combo = 'combination_version_id' in member
        require(combo != ('method_version_id' in member), 'combination_member_xor')
        if 'weight' in member:
            weight = member['weight']
            require(type(weight) in (int, float) and math.isfinite(weight), 'combination_member_weight')
        return member['combination_version_id' if combo else 'method_version_id'], combo

    if combination_id is not None:
        edge(bindings['run_id'], 'uses_combination', combination_id)
        # Ancestors reject cycles, while completed nodes permit shared DAGs and
        # ordered repeated members. No method names, depth caps or weight presets.
        pending, ancestors, completed = [(combination_id, False)], set(), set()
        while pending:
            id_, leaving = pending.pop()
            if leaving:
                ancestors.remove(id_)
                completed.add(id_)
                continue
            require(id_ not in ancestors, 'combination_cycle')
            if id_ in completed:
                continue
            definition = version(id_, 'combination_version')
            require(isinstance(definition.get('members'), list), 'combination_members')
            ancestors.add(id_)
            pending.append((id_, True))
            for member in reversed(definition['members']):
                child, combo = member_target(member)
                version(child, 'combination_version' if combo else 'method_version')
                edge(id_, 'includes_method', child)
                if combo:
                    pending.append((child, False))

        effective = trace.get('effective_combination_versions')
        if effective is not None:
            require(isinstance(effective, list) and bool(effective), 'effective_combinations')
            require(effective[-1]['id'] == combination_id, 'effective_combination_root')
            require(len({row['id'] for row in effective}) == len(effective), 'effective_combination_duplicate')
            for row in effective:
                require(row['id'] in completed, 'effective_combination_unreachable')
                require(row['kind'] == entities[row['id']]['kind'] and row['status'] == 'active',
                        'effective_combination_kind')
                require(same(protected_attrs(row['attrs']), protected_attrs(entities[row['id']]['attrs'])),
                        'effective_combination_capture')
            path = trace.get('selection_path')
            if path is not None:
                require(isinstance(path, list) and bool(path), 'selection_path')
                originals, original_ids = [], set()
                for position, step in enumerate(path):
                    id_, combo = member_target(step['member'])
                    require(id_ == step['id'], 'selection_path_member')
                    require(combo == (position + 1 < len(path)), 'selection_path_leaf')
                    definition = version(id_, 'combination_version' if combo else 'method_version')
                    require(step['definition_sha256'] == codec.digest(definition), 'selection_path_hash')
                    if combo:
                        require(id_ not in original_ids, 'selection_path_cycle')
                        original_ids.add(id_)
                        originals.append((position, step))
                        edge(id_, 'includes_method', path[position + 1]['id'])
                require(len(originals) == len(effective), 'effective_combination_path')
                child = bindings['method_version_id']
                for (position, original), replacement in zip(reversed(originals), effective):
                    definition = deepcopy(entities[original['id']]['attrs']['definition'])
                    # The following path step supplies its index within this
                    # original parent; only that consumed occurrence changes.
                    require(position + 1 < len(path), 'selection_path_leaf')
                    next_step = path[position + 1]
                    index = next_step['member_index']
                    require(type(index) is int and 0 <= index < len(definition['members']), 'selection_path_index')
                    require(same(definition['members'][index], next_step['member']), 'selection_path_occurrence')
                    role = 'combination_version_id' if 'combination_version_id' in next_step['member'] else 'method_version_id'
                    definition['members'][index][role] = child
                    require(same(definition, replacement['attrs']['definition']), 'effective_combination_rewrite')
                    child = replacement['id']

    for role, attrs in contract.get('definition_records', {}).items():
        require(role in bindings and bindings[role] is not None, 'captured_definition_binding:' + role)
        current = entities[bindings[role]]['attrs']
        require(same(protected_attrs(current), protected_attrs(attrs)) if role.endswith('_version_id')
                else same(current, attrs), 'captured_definition:' + role)
    require(definition_source_id in sources, 'unknown_definition_source')
    require(same(parse_json(sources[definition_source_id]['text']), contract), 'definition_capture')
    require(trace_source_id in sources, 'unknown_trace_source')

    require(isinstance(result_ids, list) and all(isinstance(id_, str) for id_ in result_ids), 'result_ids')
    require(len(set(result_ids)) == len(result_ids), 'duplicate_result_ids')
    results = trace.get('result_bindings')
    require(isinstance(results, list), 'missing_result_bindings')
    require(sorted(result_ids) == sorted(binding['result_entity_id'] for binding in results), 'result_set')
    actual_models, actual_origins = [], []
    for binding in results:
        result_id = binding['result_entity_id']
        require(result_id in entities, 'unknown_result')
        for role in ('run_id', 'method_version_id'):
            require(binding[role] == bindings[role], 'result_binding:' + role)
        edge(result_id, 'produced_in_run', bindings['run_id'])
        edge(result_id, 'produced_by_method_version', bindings['method_version_id'])
        compiler_id = binding.get('compiler_transform_id')
        require(compiler_id == bindings.get('compiler_transform_id'), 'result_compiler')
        if compiler_id is not None:
            edge(result_id, 'projected_by_compiler', compiler_id)
        origin = binding.get('model_origin')
        require(same(entities[result_id]['attrs'].get('model_origin'), origin), 'result_origin_capture')
        if origin is not None:
            require(origin['kind'] == 'model', 'result_model_origin')
            if origin.get('model') not in actual_models:
                actual_models.append(origin.get('model'))
            if not any(same(origin, known) for known in actual_origins):
                actual_origins.append(origin)
            origin_fields = [('recipe_sha256', 'recipe_sha256')]
            if not observed_phase:
                origin_fields.extend((('model', 'model'), ('response_sha256', 'response_sha256')))
            for key, trace_key in origin_fields:
                require(same(origin.get(key), trace.get(trace_key)), 'result_origin:' + key)

    if observed_phase:
        require(bool(actual_models), 'observed_models_without_origins')
        require(same(trace['actual_models'], actual_models), 'actual_models')
        require(same(trace.get('actual_model_origins'), actual_origins), 'actual_model_origins')
        actual = actual_models[0] if len(actual_models) == 1 else None
        require(same(trace.get('model'), actual) and same(trace.get('actual_model'), actual), 'actual_model')
        require(same(trace.get('model_origin'), actual_origins[0] if len(actual_origins) == 1 else None),
                'actual_model_origin')
        hashes = {origin.get('response_sha256') for origin in actual_origins}
        response = next(iter(hashes)) if len(hashes) == 1 and None not in hashes else None
        require(trace.get('response_sha256') == response and trace.get('response_hash_scope') == 'compiler_input',
                'actual_response_hash')
        prepared = contract['trace']
        require(same(prepared.get('model'), prepared['requested_model']), 'requested_prepared_model')
        require(same(trace.get('requested_model'), prepared['requested_model']), 'requested_model_changed')
        controls = [parameters['effective_parameters']]
        if recipe_id is not None:
            controls.append(recipe)
        for control in controls:
            if isinstance(control, dict) and isinstance(control.get('model'), str):
                require(control['model'] == prepared['requested_model'], 'requested_model_control')
        if trace.get('request_source_ref') is not None:
            source_id = trace['request_source_ref']
            require(source_id in sources, 'unknown_request_source')
            raw = sources[source_id]['text']
            request = parse_json(raw)
            require(trace.get('request_bytes_sha256') == hashlib.sha256(raw.encode('utf-8')).hexdigest(),
                    'request_bytes_hash')
            require(trace.get('request_sha256') == codec.digest(request), 'request_hash')
            if isinstance(request, dict) and isinstance(request.get('model'), str):
                require(request['model'] == prepared['requested_model'], 'requested_sent_model')
        requested_id = bindings.get('model_identity_id')
        if requested_id is not None:
            descriptor = entities[requested_id]['attrs']
            require('model' not in descriptor or same(descriptor['model'], prepared['requested_model']),
                    'requested_descriptor_model')
        actual_id = trace.get('actual_model_identity_id')
        if actual != prepared['requested_model']:
            if 'model_identity_id' in bindings:
                require(trace.get('requested_model_identity_id') == requested_id, 'requested_descriptor_lost')
            require(trace.get('model_identity_id') == actual_id, 'actual_descriptor_binding')
            require(actual_id != requested_id or actual_id is None, 'alias_descriptor_reused')
        elif actual_id is None:
            require(trace.get('model_identity_id') == requested_id, 'trace_binding:model_identity_id')
        if actual_id is not None:
            require(actual_id in entities and entities[actual_id]['kind'] == kinds['model_identity'] and
                    entities[actual_id]['status'] == 'active', 'actual_descriptor_missing')
            descriptor = entities[actual_id]['attrs']
            require('model' not in descriptor or same(descriptor['model'], actual), 'actual_descriptor_model')
            require(trace.get('model_identity_id') == actual_id, 'actual_descriptor_binding')
    require(same(parse_json(sources[trace_source_id]['text']), trace), 'trace_capture')
    return entities, immutable


def verify_artifact(artifact, library, data_directory, *, target='method-graph-artifact'):
    """Verify declared graph bindings and native persistence, not producer execution."""
    require(isinstance(artifact, dict), 'artifact_object_required')
    require(artifact.get('schema') == 'loom.method_graph_fixture/1', 'artifact_schema')
    contract, trace, packet = artifact['contract'], artifact['trace'], artifact['packet']
    with NativeGraphStore(library, data_directory) as store:
        validated = packet_call(store, {'operation': 'validate', 'packet': packet})
        require(same(validated, packet), 'native_packet_changed')
        entities, immutable = validate_relations(contract, trace, packet, artifact['result_entity_ids'],
                                      artifact['definition_capture_source_id'], artifact['trace_capture_source_id'])
        # A version's definition cannot be overwritten earlier in this packet's
        # reversible history while retaining the same version identity.
        for event in packet['history']:
            for change in event['changes']:
                id_ = change['record_id']
                if change['collection'] == 'entities' and id_ in immutable:
                    for state in ('before', 'after'):
                        if change[state] is not None:
                            require(same(protected_attrs(change[state]['attrs']), protected_attrs(entities[id_]['attrs'])),
                                    'version_overwritten:' + id_)
        selection = {name: [codec.record_id(name, row) for row in packet[name]]
                     for name in ('entities', 'claims', 'sources')}
        request = {'operation': 'accept', 'packet': packet, 'target': target, 'selection': selection,
                   'expected_rows': {name: {id_: None for id_ in ids} for name, ids in selection.items()},
                   'explicitly_accepted': True}
        accepted = store.execute(request)
        receipt = accepted['receipt']
        require(same(receipt['packet'], packet), 'accepted_packet_changed')
        require(receipt['acceptance_establishes_content_truth'] is False, 'acceptance_truth_scope')
    with NativeGraphStore(library, data_directory) as store:
        for operation in ('read', 'replay'):
            result = store.execute({'operation': operation, 'receipt_id': receipt['id']})
            require(same(result['receipt'], receipt) and result['row_drift']['matches'], 'restart_' + operation)
        retry = store.execute(request)
        require(retry['replayed'] is True and same(retry['receipt'], receipt), 'acceptance_retry_changed')
    summary = {'schema': 'loom.method_graph_artifact_verification/1', 'passed': True,
               'packet_id': packet['packet_id'], 'contract_sha256': codec.digest(contract),
               'trace_sha256': codec.digest(trace), 'receipt_id': receipt['id'],
               'receipt_sha256': receipt['receipt_sha256'], 'result_count': len(artifact['result_entity_ids']),
               'native_records': {name: len(packet[name]) for name in ('entities', 'claims', 'sources')},
               'producer_execution_verified': False, 'verifier_provider_calls': 0,
               'scope': 'declared graph consistency and actual native accept/restart/read/replay/retry'}
    return summary, receipt


def reference_artifact(fixture, library, data_directory, *, definition_source_id, trace_source_id):
    """Build the W4 reference offline; this is not evidence of registry execution."""
    with NativeGraphStore(library, data_directory) as store:
        base = packet_call(store, fixture['base_make_request'])
        compiled = packet_call(store, {'operation': 'compile_reply', 'packet': base, **fixture['reply_request']})
        reply = packet_call(store, {'operation': 'apply_compiled_reply', 'packet': base,
                                    'compilation': compiled, 'policy': fixture['apply_policy']})['packet']
        packet = packet_call(store, {'operation': 'apply', 'packet': reply,
                                     'diff': fixture['binding_diff'], 'policy': fixture['apply_policy']})['packet']
        for key, actual in (('base_packet_id', base['packet_id']), ('reply_packet_id', reply['packet_id']),
                            ('bound_packet_id', packet['packet_id']),
                            ('compilation_sha256', compiled['compilation_sha256']), ('node_ids', compiled['node_ids'])):
            require(same(fixture['expected'][key], actual), 'reference_golden:' + key)
    source = next(row['observation'] for row in packet['sources'] if row['observation']['id'] == trace_source_id)
    return {'schema': fixture['schema'], 'contract': deepcopy(fixture['contract']),
            'trace': parse_json(source['text']), 'packet': packet,
            'result_entity_ids': list(compiled['node_ids'].values()),
            'definition_capture_source_id': definition_source_id, 'trace_capture_source_id': trace_source_id,
            'producer_evidence': {'origin': 'W4 synthetic reference composition; no W3 registry execution'}}


def strict_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate_json_key')
        result[key] = value
    return result


def invalid_constant(value):
    raise ArtifactError('nonfinite_json:' + value)


def parse_json(raw):
    return json.loads(raw, object_pairs_hook=strict_pairs, parse_constant=invalid_constant)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--artifact', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path, required=True)
    args = parser.parse_args()
    raw = args.artifact.read_bytes()
    args.evidence_dir.mkdir(parents=True, exist_ok=True)
    if any(args.evidence_dir.iterdir()):
        parser.error('--evidence-dir must be empty; use a new directory to preserve the earlier receipt')
    (args.evidence_dir / 'input.json').write_bytes(raw)
    try:
        artifact = parse_json(raw.decode('utf-8'))
        with tempfile.TemporaryDirectory(prefix='loom-method-artifact-') as directory:
            summary, receipt = verify_artifact(artifact, args.library, directory)
        summary['input_sha256'] = hashlib.sha256(raw).hexdigest()
        with args.library.open('rb') as stream:
            summary['library_sha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
        summary['contract_file_sha256'] = hashlib.sha256(
            (Path(__file__).resolve().parents[1] / 'METHOD_GRAPH.md').read_bytes()).hexdigest()
        (args.evidence_dir / 'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
        (args.evidence_dir / 'verification.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(json.dumps(summary))
        return 0
    except (ArtifactError, ValueError, KeyError, TypeError, OSError) as error:
        result = {'schema': 'loom.method_graph_artifact_verification/1', 'passed': False,
                  'error': str(error), 'input_sha256': hashlib.sha256(raw).hexdigest()}
        (args.evidence_dir / 'error.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
