#!/usr/bin/env python3
"""Offline matched-input graph completion / pattern discovery preparation.

No transport, credential loading, store mutation or corpus discovery occurs here.
The reference corpus and scripted responses are author-owned DEV mechanics.
Model quality is deliberately not inferred from a successful scripted replay.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import itertools
import json
from pathlib import Path

try:
    from .agentic_graph_v1 import packet as codec
except ImportError:
    from agentic_graph_v1 import packet as codec

SCHEMA = 'loom.frontier_comparison/1'
ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / 'docs/research/model_research_2026-10-04/frontier'
TRACKS = ('graph_completion', 'pattern_discovery')
RECORDED = {'kind': 'recorded', 'actor': 'author-owned-dev-fixture', 'model': None,
            'recipe_sha256': None, 'response_sha256': None}
SYSTEMS = {
    'graph_completion': (
        'Return a loom.graph_packet_diff/1 for the supplied packet. Identify missing '
        'relations, alternatives and open questions. Preserve source bytes and all '
        'qualifiers. Source-backed extraction and model knowledge are different evidence '
        'origins. Inference needs an explicit derivation and expected property. Source '
        'support checks only establish what was stated, not whether it is true. Do not '
        'treat recurring structure as observed evidence for an unstated relation. Return '
        'JSON with diff and transformation_report; declare every loss or augmentation.'),
    'pattern_discovery': (
        'Return JSON with patterns and transformation_report. Each pattern has id, '
        'nodes (id, kind, optional label), edges (source, target, predicate, qualifiers), '
        'occurrences (node_map and claim_ids in template edge order), confidence, '
        'expected_property and alternatives. A structural witness must preserve '
        'direction, kinds, multiplicity and exact qualifiers. Declare label abstraction '
        'as loss; do not erase scope, polarity or version implicitly. Keep source '
        'occurrences separate; source group independence is caller-declared. Recurrence '
        'is a candidate pattern, not universal validity and not permission to fill a gap. '
        'Unwitnessed hypotheses belong in transformation_report.augmentation, not '
        'verified occurrences. Do not mutate the supplied packet.')}


def canonical(value):
    codec.validate_json_resources(value)
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode('utf-8')


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def dependencies():
    return {str(Path(path).resolve().relative_to(ROOT)): hashlib.sha256(Path(path).read_bytes()).hexdigest()
            for path in (__file__, codec.__file__, codec.safe.__file__)}


def read(path):
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate_json_key')
            result[key] = value
        return result
    value = json.loads(Path(path).read_bytes(), object_pairs_hook=unique_pairs)
    codec.validate_json_resources(value)
    return value


def write(path, value):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(canonical(value) + b'\n')


def _entity(ident, kind, label):
    return {'id': ident, 'kind': kind, 'canonical_key': ident, 'label': label,
            'labels': {'en': label}, 'aliases': [], 'parent': '', 'first_seen': '',
            'last_seen': '', 'evidence_class': 'observed', 'origin': 'archive',
            'confidence': 1, 'status': 'active', 'attrs': {}}


def _source(ident, text):
    text_sha = hashlib.sha256(text.encode()).hexdigest()
    locator = {'source': 'sha256:' + text_sha, 'member': 'synthetic-dev.json',
               'json_pointer': '/' + ident, 'byte_start': 0,
               'byte_len': len(text.encode()), 'time_start': None,
               'time_end': None, 'line': None}
    return {'observation': {'id': ident, 'unit': ident, 'kind': 'utterance',
            'text': text, 'locator': locator, 'lang': 'en', 'date': '2026-10-04',
            'ordinal': 0, 'artifact_type': 'synthetic_conversation',
            'speaker': 'synthetic-author', 'attrs': {}}, 'known_at': None,
            'text_sha256': text_sha}


def _claim(ident, left, right, predicate, source, *, polarity='positive', scope='dev'):
    observation = source['observation']
    return {'id': ident, 'subject': left, 'predicate': predicate, 'object': right,
            'value': None, 'qualifiers': {'valid_from': '', 'valid_to': '',
            'version': '', 'branch': '', 'scope': scope, 'lang': 'en',
            'extra': {'polarity': polarity}}, 'assessment': {
            'basis': {'support': [{'observation': observation['id'],
            'locator': deepcopy(observation['locator']), 'quote': observation['text'],
            'extractor': 'author-owned-dev@1', 'quality': 1}], 'derivation': None},
            'evidence_class': 'observed', 'origin': 'archive', 'confidence': 1,
            'premises': {'claims': [], 'principles': [], 'assumptions': []},
            'counter': {'observations': [], 'claims': []}, 'status': 'active',
            'consequences': {'claims': [], 'predictions': [], 'checks': []},
            'open': {'slots': [], 'questions': ['Content truth is unverified.'],
            'fill_query': None}, 'expected_property': None, 'check_state': 'n/a',
            'alternatives': []}}


def synthetic_cases():
    """Six explicit edge cases; no archived or sealed corpus is read."""
    cases = []; references = {}
    for ident in ('repeat', 'polarity', 'direction', 'shared_source', 'scope', 'unicode'):
        names = ('żaba', 'lampa', 'wynik') if ident == 'unicode' else ('trigger', 'action', 'result')
        entities = [_entity(side + str(i), kind, side + '-' + name)
                    for side in ('left', 'right')
                    for i, (kind, name) in enumerate(zip(('condition', 'action', 'result'), names))]
        sources = []; claims = []
        for side in ('left', 'right'):
            reverse = side == 'right' and ident == 'direction'
            polarity = 'negative' if side == 'right' and ident == 'polarity' else 'positive'
            scope = 'another-dev-scope' if side == 'right' and ident == 'scope' else 'dev'
            first_left, first_right = (names[1], names[0]) if reverse else (names[0], names[1])
            first = _source(side + '-first', f'{side}: {first_left} permits {first_right}; polarity={polarity}; scope={scope}.')
            second = _source(side + '-second', f'{side}: {names[1]} produces {names[2]}; scope=dev.')
            sources.extend([first, second])
            pair = (side + '1', side + '0') if reverse else (side + '0', side + '1')
            claims.extend([_claim(side + '-first-claim', *pair, 'permits', first,
                                  polarity=polarity, scope=scope),
                           _claim(side + '-second-claim', side + '1', side + '2', 'produces', second)])
        missing = claims.pop()  # One explicitly stated relation is missing in the input graph.
        packet = codec.make_packet(entities=entities, claims=claims, sources=sources,
            task={'goal': 'Inspect source-backed omissions and recurring structure.'},
            origin=RECORDED)
        groups = {'left-first': 'left-source', 'right-first':
                  'left-source' if ident == 'shared_source' else 'right-source'}
        common = {'packet': packet, 'source_groups': groups,
                  'scope': 'author_owned_synthetic_dev', 'source_completeness': 'fixture_only'}
        case = {'id': ident, 'input': common, 'input_sha256': digest(common)}
        repeats = ident in ('repeat', 'shared_source', 'unicode')
        references[ident] = {'completion_claims': [missing], 'pattern_claim_sets':
            [['left-first-claim', 'right-first-claim']] if repeats else [],
            'split': 'author_owned_synthetic_dev', 'independent_model_quality': False}
        cases.append(case)
    return cases, references


def default_config():
    return {'schema': SCHEMA + '/config', 'models': [
        {'key': name, 'model': 'OWNER_SELECT_' + name.upper(), 'provider': None,
         'reasoning': None, 'generation_parameters': {'max_output_tokens': 4096}}
        for name in ('family_a', 'family_b', 'family_c')],
        'runtimes': [{'key': 'single_call', 'kind': 'single_call',
                      'agents': 1, 'parameters': {}}], 'repetitions': 1,
        'recipes': deepcopy(SYSTEMS),
        'resource_limits': None, 'cost_assumptions': None,
        'live_authorized': False, 'selection': None}


def historical_config(keys=('gpt61_sol', 'sonnet55', 'gemini31_pro')):
    """Retained 2026-09-30 snapshots, not current identity/pricing verification."""
    base = ROOT / 'docs/research/frontier_panel_v1'
    preset_path = base / 'presets.json'; presets = read(preset_path)
    configs = {model['key']: model for model in presets['models']}
    result = default_config(); result['models'] = []
    for key in keys:
        old = configs[key]; endpoint_path = (base / old['endpoint_file']).resolve()
        if base.resolve() not in endpoint_path.parents:
            raise ValueError('comparison_endpoint_snapshot_path_escape')
        snapshot_hash = hashlib.sha256(endpoint_path.read_bytes()).hexdigest()
        if snapshot_hash != old['endpoint_sha256']:
            raise ValueError('comparison_historical_endpoint_snapshot_drift')
        snapshot = read(endpoint_path)
        endpoints = [item for item in snapshot['data']['endpoints'] if item['tag'] == old['provider']]
        if snapshot['data']['id'] != old['model'] or len(endpoints) != 1:
            raise ValueError('comparison_historical_endpoint_identity_mismatch')
        endpoint = endpoints[0]
        result['models'].append({'key': key, 'model': old['model'], 'provider': old['provider'],
            'reasoning': deepcopy(old.get('reasoning')),
            'generation_parameters': {'max_tokens': old['max_tokens']['graph_packet']},
            'completion_reservation_multiplier': old['completion_reservation_multiplier'],
            'historical_endpoint_evidence': {
                'snapshot_file': str(endpoint_path.relative_to(ROOT)),
                'snapshot_sha256': snapshot_hash,
                'preset_file': str(preset_path.relative_to(ROOT)),
                'preset_sha256': hashlib.sha256(preset_path.read_bytes()).hexdigest(),
                'source_model_key': key,
                'retrieved_at': old['retrieved_at'], 'status': 'stale_retained_snapshot',
                'source_url': 'https://openrouter.ai/api/v1/models/' + old['model'] + '/endpoints',
                'endpoint': deepcopy(endpoint)}})
    result['cost_assumptions'] = None
    return result


def historical_request_plan(request):
    """Conservative token-accounting surrogate; never a guaranteed billing bound."""
    model = request['model']; evidence = model['historical_endpoint_evidence']
    preset_path = (ROOT / evidence['preset_file']).resolve()
    if ROOT not in preset_path.parents or hashlib.sha256(preset_path.read_bytes()).hexdigest() != evidence['preset_sha256']:
        raise ValueError('comparison_historical_preset_evidence_drift')
    presets = read(preset_path)
    original = next((row for row in presets['models'] if row['key'] == evidence['source_model_key']), None)
    if (original is None or original['model'] != model['model'] or original['provider'] != model['provider'] or
        original['endpoint_sha256'] != evidence['snapshot_sha256'] or
        original['retrieved_at'] != evidence['retrieved_at'] or
        (preset_path.parent / original['endpoint_file']).resolve() != (ROOT / evidence['snapshot_file']).resolve()):
        raise ValueError('comparison_historical_preset_identity_or_date_drift')
    path = (ROOT / evidence['snapshot_file']).resolve()
    if ROOT not in path.parents or hashlib.sha256(path.read_bytes()).hexdigest() != evidence['snapshot_sha256']:
        raise ValueError('comparison_historical_evidence_drift')
    snapshot = read(path)
    endpoints = [e for e in snapshot['data']['endpoints'] if e['tag'] == model['provider']]
    if (snapshot['data']['id'] != model['model'] or len(endpoints) != 1 or
            endpoints[0] != evidence['endpoint']):
        raise ValueError('comparison_historical_evidence_projection_drift')
    endpoint = endpoints[0]; pricing = endpoint['pricing']
    tiers = [pricing, *pricing.get('overrides', [])]
    prompt_rate = max(Decimal(str(tier.get(name, pricing.get(name, '0'))))
        for tier in tiers for name in ('prompt', 'input_cache_write', 'input_cache_write_1h'))
    completion_rate = max(Decimal(str(tier.get(name, pricing.get(name, '0'))))
        for tier in tiers for name in ('completion', 'internal_reasoning'))
    body = {'model': model['model'], 'messages': deepcopy(request['messages']),
        'provider': {'only': [model['provider']], 'allow_fallbacks': False, 'require_parameters': True},
        'stream': False, 'response_format': {'type': 'json_object'}, 'usage': {'include': True},
        **deepcopy(model['generation_parameters'])}
    if (body['model'] != model['model'] or body['messages'] != request['messages'] or
        body['provider'] != {'only': [model['provider']], 'allow_fallbacks': False, 'require_parameters': True}):
        raise ValueError('comparison_generation_overwrites_execution_identity_or_source')
    if model.get('reasoning') is not None:
        body['reasoning'] = deepcopy(model['reasoning'])
    token_limit = body.get('max_tokens', body.get('max_output_tokens'))
    if type(token_limit) is not int or token_limit < 1:
        raise ValueError('comparison_historical_output_allowance_required')
    multiplier = Decimal(str(model.get('completion_reservation_multiplier', '1')))
    if not multiplier.is_finite() or multiplier < 1:
        raise ValueError('comparison_completion_reservation_multiplier_invalid')
    completion = Decimal(token_limit) * multiplier
    prompt = len(canonical(body)) + 1024 + 32 * len(body['messages'])
    supported = set(endpoint['supported_parameters'])
    selected = {'response_format', *model['generation_parameters']}
    if model.get('reasoning') is not None:
        selected.add('reasoning')
    missing = sorted(selected - supported)
    capability = {'status': 'stale_snapshot_only', 'unsupported_body_parameters': missing,
        'output_within_historical_capability': token_limit <= endpoint['max_completion_tokens'],
        'prompt_surrogate_within_historical_context': prompt + token_limit <= endpoint['context_length'],
        'runtime_adapter_available': request['runtime']['kind'] == 'single_call',
        'current_endpoint_capability_verified': False, 'provider_account_verified': False}
    return {'body': body, 'capability': capability, 'reservation': {
        'historical_usd': str(Decimal(prompt) * prompt_rate + completion * completion_rate),
        'prompt_token_allowance': prompt, 'completion_token_allowance': str(completion),
        'prompt_rate_per_token_usd': str(prompt_rate),
        'completion_rate_per_token_usd': str(completion_rate),
        'policy': 'UTF8_request_bytes_plus_framing; maximum_snapshot_tier_and_cache_write_rates; configured_completion_multiplier',
        'billing_bound_guaranteed': False, 'fresh_quote_required_before_execution': True,
        'agent_continuation_and_tool_calls_included': False,
        'reasoning_handling': 'completion_allowance_times_configured_multiplier_not_observed_usage'}}


def validate_config(config):
    if config.get('schema') != SCHEMA + '/config':
        raise ValueError('comparison_configuration_schema_invalid')
    codec.validate_json_resources(config, config.get('resource_limits'))
    if type(config.get('repetitions')) is not int or config['repetitions'] < 1:
        raise ValueError('comparison_repetitions_positive_integer_required')
    for collection in ('models', 'runtimes'):
        rows = config.get(collection)
        if not isinstance(rows, list) or not rows:
            raise ValueError('comparison_configuration_rows_required')
        keys = [row.get('key') for row in rows]
        if any(not isinstance(key, str) or not key for key in keys) or len(keys) != len(set(keys)):
            raise ValueError('comparison_configuration_keys_unique_required')
    for row in config['models']:
        if not isinstance(row.get('model'), str) or not row['model']:
            raise ValueError('comparison_model_identity_required')
    # Values are open caller data. Provider compatibility must be checked by a
    # future live executor, not inferred from the ability to serialize a request.
    return config


def prepare(cases, config, *, references=None):
    validate_config(config)
    if not isinstance(cases, list) or not cases:
        raise ValueError('comparison_cases_required')
    by = {}
    for case in cases:
        if case['id'] in by or case['input_sha256'] != digest(case['input']):
            raise ValueError('comparison_case_identity_or_hash_drift')
        codec.validate_packet(case['input']['packet'], resource_limits=config.get('resource_limits'))
        by[case['id']] = case
    selected = list(by) if config.get('selection') is None else config['selection']
    if not isinstance(selected, list) or len(set(selected)) != len(selected) or any(x not in by for x in selected):
        raise ValueError('comparison_selection_invalid')
    requests = []
    for case_id, model, runtime, repetition in itertools.product(selected,
            config['models'], config['runtimes'], range(config['repetitions'])):
        case = by[case_id]
        pair_id = digest([case_id, model, runtime, repetition])
        for track in TRACKS:
            value = {'id': pair_id + ':' + track, 'pair_id': pair_id,
                'case_id': case_id, 'track': track, 'repetition': repetition,
                'reference_sha256': None if references is None else digest(references[case_id]),
                'input_sha256': case['input_sha256'], 'input': deepcopy(case['input']),
                'model': deepcopy(model), 'runtime': deepcopy(runtime),
                'messages': [{'role': 'system', 'content': config.get('recipes', SYSTEMS)[track]},
                             {'role': 'user', 'content': canonical(case['input']).decode()}],
                'execution': {'mode': 'prepared_only', 'authorization': 'not_granted_by_preparation',
                              'identity_and_current_capability_verified': False}}
            if model.get('historical_endpoint_evidence'):
                value['historical_openrouter_plan'] = historical_request_plan(value)
            value['request_sha256'] = digest(value)
            requests.append(value)
    cost_plan = estimate_cost(len(requests), config.get('cost_assumptions'))
    historical = [x for x in requests if 'historical_openrouter_plan' in x]
    if historical:
        by_model = {}
        for request in historical:
            reservation = request['historical_openrouter_plan']['reservation']
            row = by_model.setdefault(request['model']['key'], {'calls': 0, 'historical_reservation_usd': Decimal(0)})
            row['calls'] += 1; row['historical_reservation_usd'] += Decimal(reservation['historical_usd'])
        for row in by_model.values():
            row['historical_reservation_usd'] = str(row['historical_reservation_usd'])
        cost_plan = {'status': 'stale_retained_snapshot_reservations_not_current_quote',
            'calls': len(requests), 'historically_priced_calls': len(historical),
            'by_model': by_model, 'historical_reservation_total_usd': str(sum(
                Decimal(x['historical_openrouter_plan']['reservation']['historical_usd']) for x in historical)),
            'billing_bound_guaranteed': False, 'current_provider_price_claim': False}
    return {'schema': SCHEMA, 'config': deepcopy(config), 'requests': requests,
            'dependencies': dependencies(),
            'references_sha256': None if references is None else digest(references),
            'network_calls': 0, 'paid_calls': 0, 'canonical_store_written': False,
            'references_in_request': False, 'request_count': len(requests),
            'cost_plan': cost_plan}


def estimate_cost(calls, assumptions):
    if assumptions is None:
        return {'status': 'unknown_without_token_and_rate_assumptions', 'calls': calls,
                'estimated_usd': None, 'current_provider_price_claim': False}
    numbers = {key: Decimal(str(assumptions[key])) for key in
               ('input_tokens_per_call', 'output_tokens_per_call',
                'usd_per_million_input', 'usd_per_million_output')}
    if any(not value.is_finite() or value < 0 for value in numbers.values()):
        raise ValueError('comparison_cost_assumptions_invalid')
    overhead = Decimal(str(assumptions.get('non_token_usd_per_call', '0')))
    if not overhead.is_finite() or overhead < 0:
        raise ValueError('comparison_non_token_cost_invalid')
    cost = Decimal(calls) * ((numbers['input_tokens_per_call'] * numbers['usd_per_million_input'] +
            numbers['output_tokens_per_call'] * numbers['usd_per_million_output']) / Decimal(1000000) + overhead)
    return {'status': 'illustrative_assumptions_not_current_quote', 'calls': calls,
            'assumptions': deepcopy(assumptions), 'estimated_usd': str(cost),
            'current_provider_price_claim': False,
            'exclusions': ['retries', 'provider-specific reasoning billing', 'tools', 'cache writes',
                           'taxes', 'unconfigured agent continuation calls']}


def _pattern_response(packet, reference):
    if not reference['pattern_claim_sets']:
        return {'patterns': [], 'transformation_report': {'loss': [], 'augmentation': []}}
    claims = {c['id']: c for c in packet['claims']}
    pattern = {'id': 'permits-pattern', 'nodes': [{'id': 'condition', 'kind': 'condition'},
        {'id': 'action', 'kind': 'action'}], 'edges': [{'source': 'condition', 'target': 'action',
        'predicate': 'permits', 'qualifiers': deepcopy(claims['left-first-claim']['qualifiers'])}],
        'occurrences': [{'node_map': {'condition': claims[ident]['subject'],
                                      'action': claims[ident]['object']}, 'claim_ids': [ident]}
                        for ident in reference['pattern_claim_sets'][0]], 'confidence': None,
        'expected_property': 'Same typed relation recurs in these supplied records.',
        'alternatives': ['No general validity beyond these occurrences.']}
    return {'patterns': [pattern], 'transformation_report': {'loss': [
        {'kind': 'label_abstraction', 'fields': ['label', 'canonical_key', 'aliases', 'labels'],
         'reason': 'Compare entity kinds while retaining original records and exact edge qualifiers.'}],
         'augmentation': []}}


def scripted_response(request, reference):
    """Author-defined perfect replay; never advertised as generated model output."""
    packet = request['input']['packet']
    if request['track'] == 'graph_completion':
        origin = {'kind': 'system', 'actor': 'scripted-comparison-dev', 'model': None,
                  'recipe_sha256': hashlib.sha256(request['messages'][0]['content'].encode()).hexdigest(),
                  'response_sha256': None}
        diff = codec.empty_diff(packet, proposal_id=request['id'], origin=origin)
        diff['claims']['add'] = deepcopy(reference['completion_claims'])
        content = {'diff': diff, 'transformation_report': {'loss': [], 'augmentation': [
            {'kind': 'source_backed_extraction', 'collection': 'claims', 'action': 'add',
             'id': c['id'], 'content_truth': 'unverified'} for c in diff['claims']['add']]}}
    else:
        content = _pattern_response(packet, reference)
    return {'schema': SCHEMA + '/response', 'request_sha256': request['request_sha256'],
            'input_sha256': request['input_sha256'], 'response_origin': 'scripted',
            'content': content, 'content_sha256': digest(content), 'usage': None,
            'provider_identity_verified': False}


def _verify_patterns(packet, content, source_groups):
    entities = {x['id']: x for x in packet['entities']}
    claims = {x['id']: x for x in packet['claims']}
    claim_sets = []; occurrences = 0; support_groups = []
    patterns = content['patterns']
    if not isinstance(patterns, list):
        raise ValueError('comparison_patterns_array_required')
    for pattern in patterns:
        nodes = {node['id']: node for node in pattern['nodes']}
        if len(nodes) != len(pattern['nodes']) or not nodes or not pattern['edges']:
            raise ValueError('comparison_pattern_identity_invalid')
        for edge in pattern['edges']:
            if edge['source'] not in nodes or edge['target'] not in nodes:
                raise ValueError('comparison_pattern_endpoint_invalid')
        seen = set(); ids = []; groups = set()
        for occurrence in pattern['occurrences']:
            mapping = occurrence['node_map']; edge_ids = occurrence['claim_ids']
            if (set(mapping) != set(nodes) or len(set(mapping.values())) != len(mapping) or
                    len(edge_ids) != len(pattern['edges']) or len(set(edge_ids)) != len(edge_ids)):
                raise ValueError('comparison_pattern_witness_noninjective_or_incomplete')
            key = digest(occurrence)
            if key in seen:
                raise ValueError('comparison_duplicate_occurrence')
            seen.add(key)
            for ident, target in mapping.items():
                entity = entities.get(target)
                if set(nodes[ident]) - {'id', 'kind', 'label'}:
                    raise ValueError('comparison_pattern_node_semantics_unrepresented')
                if (entity is None or entity['kind'] != nodes[ident]['kind'] or
                    ('label' in nodes[ident] and entity['label'] != nodes[ident]['label'])):
                    raise ValueError('comparison_pattern_node_witness_mismatch')
            for edge, claim_id in zip(pattern['edges'], edge_ids):
                claim = claims.get(claim_id)
                if (claim is None or claim['subject'] != mapping[edge['source']] or
                    claim['object'] != mapping[edge['target']] or claim['predicate'] != edge['predicate'] or
                    claim['qualifiers'] != edge['qualifiers']):
                    raise ValueError('comparison_pattern_edge_witness_mismatch')
                for support in claim['assessment']['basis']['support']:
                    group = source_groups.get(support['observation'])
                    if group is not None:
                        groups.add(group)
            occurrences += 1; ids.extend(edge_ids)
        claim_sets.append(sorted(ids)); support_groups.append(len(groups))
    return sorted(claim_sets), occurrences, support_groups


def _declared_labels(content):
    """A label-free pattern is an explicit projection, not identity semantics."""
    abstract = any('label' not in node for pattern in content['patterns'] for node in pattern['nodes'])
    report = content['transformation_report']
    if abstract and not any(isinstance(row, dict) and row.get('kind') == 'label_abstraction'
                            for row in report['loss']):
        raise ValueError('comparison_pattern_label_abstraction_undeclared')


def _validate_edit_declarations(changes, report, diff):
    actual = {(row['collection'], row['action'], row['record_id']) for row in changes}
    if diff['task'] is not None:
        actual.add(('task', 'update', 'task'))
    declared = {'loss': set(), 'augmentation': set()}
    for channel in declared:
        for row in report[channel]:
            if not isinstance(row, dict):
                raise ValueError('comparison_transformation_entry_object_required')
            if 'action' in row:
                key = (row.get('collection'), row['action'], row.get('id'))
                if key not in actual:
                    raise ValueError('comparison_transformation_declares_nonexistent_edit')
                declared[channel].add(key)
    for key in actual:
        required = ('augmentation',) if key[1] == 'add' else ('loss',) if key[1] == 'remove' else ('loss', 'augmentation')
        if any(key not in declared[channel] for channel in required):
            raise ValueError('comparison_actual_edit_loss_or_augmentation_undeclared')


def _target_agreement(packet, candidate, reference):
    target = {name: {codec.record_id(name, item): item for item in packet[name]}
              for name in codec.COLLECTIONS}
    for item in reference['completion_claims']:
        target['claims'][item['id']] = item
    candidate_records = {name: {codec.record_id(name, item): item for item in candidate[name]}
                         for name in codec.COLLECTIONS}
    return target == candidate_records and candidate['task'] == packet['task']


def score_response(request, response, reference):
    if (request.get('request_sha256') != digest({k: v for k, v in request.items() if k != 'request_sha256'}) or
        request['input_sha256'] != digest(request['input']) or
        request['messages'][1]['content'] != canonical(request['input']).decode()):
        raise ValueError('comparison_request_hash_or_input_drift')
    if request.get('reference_sha256') is not None and request['reference_sha256'] != digest(reference):
        raise ValueError('comparison_case_reference_drift')
    if (response.get('schema') != SCHEMA + '/response' or
        response.get('request_sha256') != request['request_sha256'] or
        response.get('input_sha256') != request['input_sha256'] or
        response.get('content_sha256') != digest(response['content'])):
        raise ValueError('comparison_response_binding_drift')
    if response.get('response_origin') not in ('scripted', 'recorded'):
        raise ValueError('comparison_response_origin_required')
    content = response['content']; report = content['transformation_report']
    if (not isinstance(report, dict) or not isinstance(report.get('loss'), list) or
            not isinstance(report.get('augmentation'), list)):
        raise ValueError('comparison_transformation_report_required')
    packet = request['input']['packet']; before = digest(packet)
    result = {'request_id': request['id'], 'track': request['track'],
              'response_origin': response['response_origin'], 'mechanical_validity': True,
              'model_semantic_quality': None, 'model_semantic_quality_status':
              'unmeasured_scripted_or_author_owned_reference', 'reference_split': reference['split'],
              'canonical_store_written': False, 'transformation_report': deepcopy(report)}
    result['reference_hash_verified'] = request.get('reference_sha256') == digest(reference)
    if request['track'] == 'graph_completion':
        original_diff = content['diff']; diff = deepcopy(original_diff)
        if response['response_origin'] == 'recorded':
            captured_model = response.get('captured_model')
            if not isinstance(captured_model, str) or not captured_model:
                raise ValueError('comparison_recorded_model_identity_required')
            # Raw proposal is retained in the response. Bind measured instrument
            # origin beside the native claim assessment, never rewrite evidence.
            diff['origin'] = {'kind': 'model', 'actor': 'saved-response-comparison',
                'model': captured_model,
                'recipe_sha256': hashlib.sha256(request['messages'][0]['content'].encode()).hexdigest(),
                'response_sha256': response['content_sha256']}
        preview = codec.preview_diff(packet, diff)
        _validate_edit_declarations(preview['changes'], report, diff)
        candidate = preview['candidate_packet']
        proposed = {digest(x) for x in content['diff']['claims']['add']}
        preserved = all(all(any(codec.record_id(name, current) == codec.record_id(name, before_record)
            and current == before_record for current in candidate[name]) for before_record in packet[name])
            for name in codec.COLLECTIONS) and candidate['task'] == packet['task']
        result.update(reference_agreement=_target_agreement(packet, candidate, reference),
            reference_agreement_scope='entire_candidate_record_content_and_task_not_only_added_claims',
            added_claims=len(proposed), existing_records_preserved=preserved,
            source_bytes_preserved=candidate['sources'] == packet['sources'],
            history_events=len(candidate['history']) - len(packet['history']),
            instrument_origin=deepcopy(diff['origin']),
            raw_proposed_origin=deepcopy(original_diff['origin']),
            captured_provider_identity_verified=False,
            native_assessments_changed_by_binding=False)
    else:
        _declared_labels(content)
        claim_sets, occurrences, groups = _verify_patterns(packet, content, request['input']['source_groups'])
        expected = sorted(sorted(x) for x in reference['pattern_claim_sets'])
        result.update(reference_agreement=expected == claim_sets, pattern_count=len(claim_sets),
            witnessed_occurrences=occurrences, declared_independent_source_groups=groups,
            source_group_independence_verified=False, source_bytes_preserved=True,
            mutations_applied=0)
    if digest(packet) != before:
        raise AssertionError('comparison_scorer_mutated_input')
    return result


def replay_bundle(manifest, responses, references):
    if manifest.get('dependencies') != dependencies():
        raise ValueError('comparison_frozen_dependency_drift')
    if manifest.get('references_sha256') is not None and manifest['references_sha256'] != digest(references):
        raise ValueError('comparison_reference_bundle_drift')
    if len(responses) != len(manifest['requests']):
        raise ValueError('comparison_response_inventory_mismatch')
    by = {x['request_sha256']: x for x in responses}
    if len(by) != len(responses):
        raise ValueError('comparison_duplicate_response')
    rows = []; pairs = {}
    for request in manifest['requests']:
        pairs.setdefault(request['pair_id'], []).append(request)
        response = by[request['request_sha256']]
        rows.append(score_response(request, response, references[request['case_id']]))
    for values in pairs.values():
        if (len(values) != 2 or {x['track'] for x in values} != set(TRACKS) or
            len({x['input_sha256'] for x in values}) != 1 or
            len({x['messages'][1]['content'] for x in values}) != 1):
            raise ValueError('comparison_matched_pair_drift')
    return {'schema': SCHEMA + '/results', 'requests': len(rows), 'matched_pairs': len(pairs),
            'mechanically_valid': sum(x['mechanical_validity'] for x in rows),
            'scripted_reference_agreement': sum(x['reference_agreement'] for x in rows
                                                 if x['response_origin'] == 'scripted'),
            'model_semantic_quality': None, 'network_calls': 0, 'paid_calls': 0,
            'canonical_store_written': False, 'rows': rows}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prepare_cmd = commands.add_parser('prepare')
    prepare_cmd.add_argument('--output', type=Path, default=HERE / 'prepared')
    prepare_cmd.add_argument('--config', type=Path)
    prepare_cmd.add_argument('--historical-primary', action='store_true',
                             help='Retained 2026-09-30 snapshot preset; no current-price claim.')
    prepare_cmd.add_argument('--cases', type=Path, help='Public or owner-approved supplied inputs; never auto-discovered.')
    replay_cmd = commands.add_parser('replay')
    replay_cmd.add_argument('manifest', type=Path); replay_cmd.add_argument('responses', type=Path)
    replay_cmd.add_argument('references', type=Path); replay_cmd.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == 'prepare':
        if args.config is not None and args.historical_primary:
            parser.error('--config and --historical-primary choose separate configuration sources')
        config = (historical_config() if args.historical_primary else default_config()) if args.config is None else read(args.config)
        if args.cases is None:
            cases, references = synthetic_cases()
        else:
            cases, references = read(args.cases), None
        manifest = prepare(cases, config, references=references)
        args.output.mkdir(parents=True, exist_ok=False)
        write(args.output / 'config.json', config); write(args.output / 'inputs.json', cases)
        write(args.output / 'manifest.json', manifest)
        result = {'request_count': manifest['request_count'], 'cost_plan': manifest['cost_plan']}
        if references is not None:
            responses = [scripted_response(r, references[r['case_id']]) for r in manifest['requests']]
            write(args.output / 'references.synthetic.json', references)
            write(args.output / 'responses.scripted.json', responses)
            results = replay_bundle(manifest, responses, references)
            write(args.output / 'results.scripted.json', results)
            result.update(matched_pairs=results['matched_pairs'], mechanically_valid=results['mechanically_valid'])
    else:
        result = replay_bundle(read(args.manifest), read(args.responses), read(args.references))
        write(args.output, result)
    print(canonical(result).decode())


if __name__ == '__main__':
    main()
