#!/usr/bin/env python3
"""Data-configured stage 3/4 preparation; reuse pinned reply compiler, never call models."""
from __future__ import annotations

import argparse
import base64
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import types

try:
    from . import frontier_comparison_v1 as frontier
except ImportError:
    import frontier_comparison_v1 as frontier

ROOT = frontier.ROOT
CONFIG = ROOT / 'docs/research/model_research_2026-10-04/followup-frontier-reply/config.json'
SCHEMA = 'loom.frontier_reply_followup/1'


def git_bytes(binding):
    """Resolve explicit local immutable objects; no fetch or branch discovery."""
    commit = binding['commit']
    if (not isinstance(commit, str) or len(commit) not in (40, 64) or
            any(char not in '0123456789abcdef' for char in commit)):
        raise ValueError('followup_full_immutable_commit_required')
    try:
        raw = subprocess.check_output(['git', 'show', binding['commit'] + ':' + binding['path']],
            cwd=ROOT, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        raise ValueError('followup_pinned_git_source_unavailable') from None
    if hashlib.sha256(raw).hexdigest() != binding['sha256']:
        raise ValueError('followup_pinned_git_source_hash_drift')
    return raw


def source_module(binding):
    raw = git_bytes(binding)
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    module = types.ModuleType('pinned_reply_' + binding['sha256'])
    module.__package__ = 'loom.tools.structure.graph_reply_v1'
    exec(compile(raw, binding['commit'] + ':' + binding['path'], 'exec'), module.__dict__)
    return module


def load_config(path=CONFIG):
    value = frontier.read(path)
    if value.get('schema') != SCHEMA + '/config':
        raise ValueError('followup_configuration_schema_invalid')
    for binding in value['sources'].values():
        git_bytes(binding)
    return value


def source_receipt(config):
    native_paths = ('loom/src/packet/reply.cpp', 'loom/src/capi/capi_packet.cpp')
    return {'sources': deepcopy(config['sources']),
        'native_source_present_in_working_tree': all((ROOT / path).is_file() for path in native_paths),
        'native_api_execution_verified_here': False,
        'native_api': {'symbol': 'loom_packet', 'route': 'POST /api/packet',
            'operation': 'compile_reply', 'fields': ['packet', 'raw|raw_base64', 'host'],
            'host_required': ['request_id', 'turn_id', 'model']},
        'reference_compiler': 'exact pinned Python source reused without copying or modification',
        'model_calls': 0, 'canonical_store_written': False}


def configured_models(stage):
    models = frontier.historical_config(stage['models'])['models']
    for model in models:
        model['generation_parameters'].update(deepcopy(stage['generation_parameters']))
        if 'reasoning' in stage:
            model['reasoning'] = deepcopy(stage['reasoning'])
    return models


def render_template(template, bindings):
    if isinstance(template, dict):
        return {key: render_template(value, bindings) for key, value in template.items()}
    if isinstance(template, list):
        return [render_template(value, bindings) for value in template]
    if isinstance(template, str) and template.startswith('$'):
        path = template[1:].split('.'); value = bindings[path[0]]
        for component in path[1:]:
            value = value[component]
        return deepcopy(value)
    return deepcopy(template)


def prepare_stage3(config):
    stage = config['stage3']; cases, references = frontier.synthetic_cases()
    for case in cases:
        case['input'] = render_template(stage['user_template'], {'input': case['input']})
        case['input_sha256'] = frontier.digest(case['input'])
    # Complete settings come from the new data preset. Old v1 defaults and
    # frozen request/source hashes remain untouched.
    settings = {'schema': frontier.SCHEMA + '/config', 'models': configured_models(stage),
        'runtimes': deepcopy(stage['runtimes']), 'repetitions': stage['repetitions'],
        'recipes': deepcopy(stage['recipes']), 'selection': deepcopy(stage['selection']),
        'resource_limits': deepcopy(stage['resource_limits']), 'cost_assumptions': None,
        'live_authorized': False}
    manifest = frontier.prepare(cases, settings, references=references)
    responses = [frontier.scripted_response(row, references[row['case_id']]) for row in manifest['requests']]
    results = frontier.replay_bundle(manifest, responses, references)
    return {'manifest': manifest, 'responses': responses, 'references': references, 'results': results}


def reply_schema(prompt, arm):
    schema = prompt.reply_schema(arm['compiler_wire_schema'])
    schema['properties']['schema']['enum'] = [arm['wire_schema']]
    if arm['representation'] == 'text_plus_json':
        schema['properties']['text'] = {'type': 'string', 'description': 'Exact public answer, matching the composed annotation tree.'}
        schema['required'].append('text')
    elif arm['representation'] != 'native_graph':
        raise ValueError('followup_representation_adapter_unavailable')
    return schema


def _sources(config):
    prompt = source_module(config['sources']['reply_prompt'])
    compiler = source_module(config['sources']['reply_compiler'])
    cases = json.loads(git_bytes(config['sources']['reply_cases']))['cases']
    samples = config['scripted_references']
    if samples['sample_origin'] != 'authored-synthetic':
        # This tool never relabels a live or unknown captured response as scripted.
        raise ValueError('followup_scripted_source_origin_mismatch')
    return prompt, compiler, cases, samples['samples']


def prepare_stage4(config, *, score=True):
    stage = config['stage4']; prompt, compiler, cases, samples = _sources(config)
    by = {case['case_id']: case for case in cases}
    if len(by) != len(cases) or len(set(stage['selection'])) != len(stage['selection']):
        raise ValueError('followup_case_selection_identity_invalid')
    models = configured_models(stage); requests = []
    for case_id in stage['selection']:
        case = by[case_id]; packet = case['base_packet']
        frontier.codec.validate_packet(packet, resource_limits=stage['resource_limits'])
        for model in models:
            for runtime in stage['runtimes']:
                for repetition in range(stage['repetitions']):
                    common = render_template(stage['user_template'], {'packet': packet, 'query': case['user_query']})
                    pair_id = frontier.digest([case_id, model, runtime, repetition])
                    for arm in stage['arms']:
                        strict_schema = reply_schema(prompt, arm)
                        body = {'model': model['model'], 'stream': False,
                            'messages': [{'role': 'system', 'content': arm['recipe']},
                                         {'role': 'user', 'content': frontier.canonical(common).decode()}],
                            **deepcopy(model['generation_parameters'])}
                        mode = arm['format_mode']
                        if mode == 'json_schema':
                            body['response_format'] = {'type': 'json_schema', 'json_schema': {
                                'name': arm['schema_name'], 'strict': True, 'schema': strict_schema}}
                        elif mode == 'json_object':
                            body['response_format'] = {'type': 'json_object'}
                        elif mode != 'prompt':
                            raise ValueError('followup_output_format_adapter_unavailable')
                        body['provider'] = {'only': [model['provider']], 'require_parameters': True, 'allow_fallbacks': False}
                        body['usage'] = {'include': True}
                        if model.get('reasoning') is not None:
                            body['reasoning'] = deepcopy(model['reasoning'])
                        if body['model'] != model['model'] or body['messages'][1]['content'] != frontier.canonical(common).decode():
                            raise ValueError('followup_generation_overwrites_identity_or_input')
                        row = {'id': pair_id + ':' + arm['key'], 'pair_id': pair_id, 'case_id': case_id,
                            'arm': deepcopy(arm), 'model': deepcopy(model), 'runtime': deepcopy(runtime),
                            'repetition': repetition, 'common_input': common, 'base_packet': deepcopy(packet),
                            'common_input_sha256': frontier.digest(common),
                            'body': body, 'request_sent': False, 'provider_schema_measured': False,
                            'native_api_execution_verified': False}
                        # Reuse historical identity/rate accounting, but price the
                        # exact stage-4 body (including its strict schema bytes).
                        reservation_model = deepcopy(model)
                        reservation_model['generation_parameters'] = {key: deepcopy(body[key]) for key in body
                            if key not in ('model', 'messages', 'provider', 'stream', 'usage', 'reasoning')}
                        historical = frontier.historical_request_plan({'model': reservation_model,
                            'messages': body['messages'], 'runtime': runtime})
                        # The retained helper verifies identity and maximum
                        # rates. Its legacy envelope defaults are not used to
                        # price an optional prompt-only or streamed body.
                        reservation = historical['reservation']
                        prompt_allowance = len(frontier.canonical(body)) + 1024 + 32 * len(body['messages'])
                        reservation['prompt_token_allowance'] = prompt_allowance
                        reservation['historical_usd'] = str(Decimal(prompt_allowance) * Decimal(reservation['prompt_rate_per_token_usd']) +
                            Decimal(reservation['completion_token_allowance']) * Decimal(reservation['completion_rate_per_token_usd']))
                        reservation['exact_request_body_sha256'] = frontier.digest(body)
                        selected_parameters = set(body) - {'model', 'messages', 'provider', 'stream', 'usage'}
                        historical['capability']['unsupported_body_parameters'] = sorted(selected_parameters - set(model['historical_endpoint_evidence']['endpoint']['supported_parameters']))
                        historical['capability']['offline_capture_adapter_available'] = body['stream'] is False
                        row['historical_reservation'] = historical['reservation']
                        row['historical_capability'] = historical['capability']
                        row['historical_capability']['structured_output_feature_in_snapshot'] = (
                            mode != 'json_schema' or 'structured_outputs' in model['historical_endpoint_evidence']['endpoint']['supported_parameters'])
                        row['request_sha256'] = frontier.digest(row)
                        requests.append(row)
    manifest = {'schema': SCHEMA + '/stage4', 'requests': requests,
        'sources': deepcopy(config['sources']), 'config_sha256': frontier.digest(config),
        'dependencies': dependencies(), 'matched_inputs': True, 'paid_calls': 0,
        'cost_plan': historical_total(requests), 'model_semantic_quality': None}
    responses = [scripted_stage4(row, compiler, samples) for row in requests] if score else []
    results = score_stage4(manifest, responses, config) if score else None
    return {'manifest': manifest, 'responses': responses, 'results': results}


def dependencies():
    return {**frontier.dependencies(), str(Path(__file__).relative_to(ROOT)):
            hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}


def scripted_stage4(request, compiler, samples):
    sample = next(row for row in samples if row['case_id'] == request['case_id'])
    original = sample['raw_response'].encode(); wire = json.loads(original)
    root = next(node for node in wire['nodes'] if node['id'] == wire['response_id'])
    text = root['text']; wire['schema'] = request['arm']['compiler_wire_schema']
    if wire['schema'] == 'loom.graph_reply/2':
        for node in wire['nodes']:
            if node['children']:
                node['text'] = None
    wire['schema'] = request['arm']['wire_schema']
    if request['arm']['representation'] == 'text_plus_json':
        wire['text'] = text
    raw = frontier.canonical(wire)
    return {'schema': SCHEMA + '/stage4_response', 'request_sha256': request['request_sha256'],
        'sample_origin': 'scripted_transformation_of_separate_authored_synthetic_reference',
        'raw_capture': compiler.capture_response(raw), 'parent_raw_capture': compiler.capture_response(original),
        'provider_schema_measured': False, 'model_quality': None}


def _decode_capture(capture):
    raw = base64.b64decode(capture['raw_base64'], validate=True)
    if capture['sha256'] != hashlib.sha256(raw).hexdigest() or capture['byte_len'] != len(raw):
        raise ValueError('followup_first_capture_hash_drift')
    return raw


def score_stage4(manifest, responses, config):
    if manifest['dependencies'] != dependencies() or manifest['config_sha256'] != frontier.digest(config):
        raise ValueError('followup_instrument_or_configuration_drift')
    expected = prepare_stage4(config, score=False)['manifest']
    if manifest != expected:
        raise ValueError('followup_request_grid_configuration_drift')
    prompt, compiler, _, samples = _sources(config)
    by = {row['request_sha256']: row for row in responses}
    if len(by) != len(responses) or len(by) != len(manifest['requests']):
        raise ValueError('followup_response_inventory_mismatch')
    results = []; pairs = {}
    for row in manifest['requests']:
        if row['request_sha256'] != frontier.digest({key: value for key, value in row.items() if key != 'request_sha256'}):
            raise ValueError('followup_request_hash_drift')
        response = by[row['request_sha256']]; raw = _decode_capture(response['raw_capture'])
        claimed_sample_origin = response['sample_origin']
        source_transformation_verified = None
        sample_origin = claimed_sample_origin
        if claimed_sample_origin == 'scripted_transformation_of_separate_authored_synthetic_reference':
            parent = _decode_capture(response['parent_raw_capture'])
            source = next(sample for sample in config['scripted_references']['samples'] if sample['case_id'] == row['case_id'])
            if parent != source['raw_response'].encode('utf-8'):
                raise ValueError('followup_scripted_parent_capture_source_drift')
            expected_child = scripted_stage4(row, compiler, samples)
            source_transformation_verified = raw == _decode_capture(expected_child['raw_capture'])
            if source_transformation_verified:
                instrument_model = config['scripted_references']['compiler_model_identity']
            else:
                sample_origin = 'unverified_scripted_transformation_claim'
                instrument_model = config['scripted_references']['unverified_capture_compiler_model_identity']
        elif claimed_sample_origin == 'recorded_model_capture':
            instrument_model = response.get('captured_model')
            if not isinstance(instrument_model, str) or not instrument_model:
                raise ValueError('followup_recorded_model_identity_required')
        else:
            raise ValueError('followup_response_origin_adapter_unavailable')
        wire = json.loads(raw.decode('utf-8'), object_pairs_hook=_unique)
        if wire['schema'] != row['arm']['wire_schema']:
            raise ValueError('followup_wire_schema_mismatch')
        declared_text = None; transform = {'loss': [], 'augmentation': [], 'exact_original_bytes_retained': True}
        if row['arm']['representation'] == 'text_plus_json':
            declared_text = wire.pop('text'); wire['schema'] = row['arm']['compiler_wire_schema']
            transform['loss'] = [{'kind': 'duplicate_public_text_field_omitted_from_compiler_projection',
                                  'original_retained_in': 'raw_capture'}]
            transform['augmentation'] = [{'kind': 'explicit_wrapper_to_graph_reply_adapter',
                'compiler_input_is': 'derived_projection_not_original_model_wire'}]
            compiler_raw = frontier.canonical(wire)
        else:
            compiler_raw = raw
        packet = row['base_packet']; before = frontier.digest(packet)
        host = {'request_id': row['id'], 'turn_id': row['id'] + ':scripted-attempt1',
                'model': instrument_model, 'actor': 'reply-followup-offline-scorer',
                'recipe_sha256': hashlib.sha256(row['body']['messages'][0]['content'].encode()).hexdigest()}
        try:
            compiled = compiler.compile_reply(packet, compiler_raw, **host,
                resource_limits=config['stage4']['resource_limits'])
            if declared_text is not None and declared_text != compiled['response_text']:
                raise ValueError('followup_text_annotation_mismatch')
            preview = frontier.codec.preview_diff(packet, compiled['diff'])
            old_preserved = all(all(any(new == old for new in preview['candidate_packet'][name])
                                    for old in packet[name]) for name in frontier.codec.COLLECTIONS)
            result = {'mechanical_validity': True, 'response_text': compiled['response_text'],
                'response_text_sha256': hashlib.sha256(compiled['response_text'].encode()).hexdigest(),
                'node_count': len(wire['nodes']), 'link_count': len(wire['links']),
                'structural_claim_count': len(compiled['diff']['claims']['add']),
                'spans': compiled['spans'], 'existing_records_preserved': old_preserved,
                'compiler_capture_sha256': compiled['raw_capture']['sha256'],
                'compiler_input_is_original_response': compiler_raw == raw,
                'semantic_links_status': 'draft_not_validated_logical_claims'}
        except ValueError as exc:
            result = {'mechanical_validity': False, 'error': str(exc), 'first_capture_retained': True}
        if frontier.digest(packet) != before:
            raise AssertionError('followup_compiler_mutated_input')
        result.update(request_id=row['id'], arm=row['arm']['key'], raw_byte_len=len(raw),
            instrument_model=instrument_model, configured_model=row['model']['model'],
            captured_provider_identity_verified=False,
            transformation_report=transform, sample_origin=sample_origin,
            claimed_sample_origin=claimed_sample_origin,
            source_transformation_verified=source_transformation_verified,
            model_semantic_quality=None, provider_schema_measured=False, canonical_store_written=False)
        results.append(result); pairs.setdefault(row['pair_id'], []).append((row, result))
    pair_rows = []
    for pair_id, entries in pairs.items():
        if len(entries) != len(config['stage4']['arms']) or len({row['common_input_sha256'] for row, _ in entries}) != 1 or len({row['body']['messages'][1]['content'] for row, _ in entries}) != 1:
            raise ValueError('followup_matched_input_pair_drift')
        pair_rows.append({'pair_id': pair_id, 'arms': [result['arm'] for _, result in entries],
            'same_rendered_text': len({result.get('response_text_sha256') for _, result in entries}) == 1,
            'all_mechanically_valid': all(result['mechanical_validity'] for _, result in entries)})
    return {'schema': SCHEMA + '/stage4_results', 'requests': len(results), 'matched_pairs': len(pair_rows),
        'mechanically_valid': sum(row['mechanical_validity'] for row in results), 'rows': results,
        'pairs': pair_rows, 'network_calls': 0, 'paid_calls': 0, 'model_semantic_quality': None}


def _unique(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError('followup_duplicate_json_key')
        result[key] = value
    return result


def historical_total(requests):
    return {'status': 'stale_retained_quote_not_current_bill', 'calls': len(requests),
        'historical_reservation_usd': str(sum(Decimal(row['historical_reservation']['historical_usd']) for row in requests)),
        'billing_bound_guaranteed': False, 'current_price_quote': False}


def method_descriptors(config, stages):
    descriptors = []
    for stage_index, (stage_name, stage) in enumerate((name, config[name]) for name in ('stage3', 'stage4')):
        prepared = stages[stage_index]; requests = prepared['manifest']['requests']
        recipes = stage['recipes'] if stage_name == 'stage3' else {arm['method_id']: arm['recipe'] for arm in stage['arms']}
        for recipe_id, recipe in recipes.items():
            method_id = stage['method_ids'][recipe_id] if stage_name == 'stage3' else recipe_id
            components = deepcopy(stage['components'])
            if stage_name == 'stage4':
                components.extend(deepcopy(next(arm for arm in stage['arms'] if arm['method_id'] == method_id)['components']))
            base = {'method_id': method_id, 'version': stage['method_version'],
                'recipe_sha256': hashlib.sha256(recipe.encode()).hexdigest(),
                'recipe_material': recipe,
                'prompt_sha256': {'system': hashlib.sha256(recipe.encode()).hexdigest(),
                                 'user_template': frontier.digest(stage['user_template'])},
                'preset': {'id': stage['preset_id'], 'version': stage['preset_version'],
                           'sha256': frontier.digest(stage), 'values': deepcopy(stage)},
                'components': components, 'execution_kind': 'offline_preparation_and_scripted_replay',
                'metrics': {'model_semantic_quality': None, 'population': 'authored_synthetic_dev',
                            'evidence_class': 'scripted_mechanics'}, 'sources': deepcopy(config['sources'])}
            groups = {}
            for request in requests:
                match = request['track'] == recipe_id if stage_name == 'stage3' else request['arm']['method_id'] == method_id
                if not match:
                    continue
                model = request['model']
                parameters = {'model': model['model'], 'provider': model['provider'],
                    'model_key': model['key'], 'reasoning': deepcopy(model.get('reasoning')),
                    'generation_parameters': deepcopy(model['generation_parameters']),
                    'runtime': deepcopy(request['runtime']), 'resource_limits': deepcopy(stage['resource_limits']),
                    'repetitions': stage['repetitions']}
                if stage_name == 'stage4':
                    parameters.update(representation=request['arm']['representation'],
                        format_mode=request['arm']['format_mode'], wire_schema=request['arm']['wire_schema'],
                        compiler_wire_schema=request['arm']['compiler_wire_schema'])
                identity = frontier.digest(parameters)
                group = groups.setdefault(identity, {'parameters': parameters, 'requests': []})
                group['requests'].append({'request_id': request['id'], 'request_sha256': request['request_sha256'],
                                          'case_id': request['case_id']})
            for group in groups.values():
                descriptor = deepcopy(base); descriptor['parameters'] = group['parameters']
                descriptor['configured_method_id'] = method_id + ':' + frontier.digest(group['parameters'])
                descriptor['prepared_requests'] = group['requests']
                descriptor['historical_model_evidence'] = deepcopy(next(row['model']['historical_endpoint_evidence']
                    for row in requests if row['model']['key'] == group['parameters']['model_key']))
                descriptors.append(descriptor)
    return descriptors


def prepare_all(config, output):
    output = Path(output); output.mkdir(parents=True, exist_ok=False)
    stage3 = prepare_stage3(config); stage4 = prepare_stage4(config)
    for name, bundle in (('stage3', stage3), ('stage4', stage4)):
        for key, value in bundle.items():
            frontier.write(output / name / (key + '.json'), value)
    frontier.write(output / 'config.json', config)
    frontier.write(output / 'source_receipt.json', source_receipt(config))
    frontier.write(output / 'method_graph_descriptors.json', method_descriptors(config, (stage3, stage4)))
    summary = {'schema': SCHEMA + '/summary', 'stage3': {
        'requests': stage3['manifest']['request_count'], 'matched_pairs': stage3['results']['matched_pairs'],
        'historical_reservation_usd': stage3['manifest']['cost_plan']['historical_reservation_total_usd']},
        'stage4': {key: stage4['results'][key] for key in ('requests', 'matched_pairs', 'mechanically_valid')},
        'stage4_cost': stage4['manifest']['cost_plan'], 'campaign_budget': deepcopy(config['campaign_budget']),
        'euro_budget_fit_verified': False, 'paid_calls': 0, 'model_semantic_quality': None}
    frontier.write(output / 'summary.json', summary)
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=CONFIG)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    print(frontier.canonical(prepare_all(load_config(args.config), args.output)).decode())


if __name__ == '__main__':
    main()
