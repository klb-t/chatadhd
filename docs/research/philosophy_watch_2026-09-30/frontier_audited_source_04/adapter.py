"""Configurable, offline-prepared frontier instruments over existing graph scorers.

No API call, archive discovery, validation access, graph store, or graph promotion
occurs here. Public endpoint snapshots define each instrument's identity. Product
choices live in presets; the bounded transport is an explicitly invoked sibling.
"""
from __future__ import annotations
import argparse
from copy import deepcopy
from decimal import Decimal, InvalidOperation
import hashlib
from pathlib import Path
import types

from .. import graph_panel_live as panel, graph_panel_score_run as integrity
from .. import graph_free_extraction as free, openrouter_runner as safe

HERE = panel.ROOT / 'docs/research/frontier_panel_v1'
PRESETS = HERE / 'presets.json'
RECIPES = HERE / 'recipes.json'
TRACKS = ('assisted_extraction', 'free_source_extraction', 'supplied_edge_judgment', 'graph_packet')
VERSION = 'loom.frontier_panel/1'


class IdentityIntegrityError(RuntimeError):
    pass


def read(path):
    return safe.parse_json(Path(path).read_bytes())


def money(value):
    """Finite decimal accounting; global budgets can exceed a billion dollars."""
    if type(value) not in (int, float, str) or isinstance(value, bool) or len(str(value)) > 80:
        raise safe.RunnerError('invalid_money')
    try:
        out = Decimal(str(value))
    except InvalidOperation:
        raise safe.RunnerError('invalid_money') from None
    if not out.is_finite() or out < 0 or out.as_tuple().exponent < -18:
        raise safe.RunnerError('invalid_money')
    return out


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def public_identity(model_config, *, base=HERE, catalog=None):
    """Accept only aliases observed in this exact model/provider endpoint snapshot."""
    path = (Path(base) / model_config['endpoint_file']).resolve()
    if Path(base).resolve() not in path.parents or sha(path) != model_config['endpoint_sha256']:
        raise IdentityIntegrityError('endpoint_snapshot_hash_or_path_drift')
    snapshot = read(path)
    if snapshot.get('data', {}).get('id') != model_config['model']:
        raise IdentityIntegrityError('snapshot_model_mismatch')
    endpoints = [e for e in snapshot['data'].get('endpoints', [])
                 if e.get('tag') == model_config['provider'] and e.get('status') == 0]
    if len(endpoints) != 1:
        raise IdentityIntegrityError('one_observed_active_provider_required')
    endpoint = endpoints[0]
    aliases = {model_config['model'], endpoint.get('model_id')}
    if ' | ' in endpoint.get('name', ''):
        aliases.add(endpoint['name'].split(' | ', 1)[1])
    aliases.discard(None)
    providers = {model_config['provider'], endpoint.get('provider_name')}
    providers.discard(None)
    return {'snapshot': snapshot, 'endpoint': endpoint, 'model_aliases': sorted(aliases),
            'provider_aliases': sorted(providers), 'catalog_model': catalog}


def load_presets(path=PRESETS):
    path = Path(path); presets = read(path)
    if presets.get('schema') != 'loom.frontier_panel_presets/1':
        raise ValueError('invalid_frontier_preset_schema')
    catalog_path = (path.parent / presets['catalog_file']).resolve()
    if path.parent.resolve() not in catalog_path.parents or sha(catalog_path) != presets['retrieved_catalog_sha256']:
        raise IdentityIntegrityError('catalog_snapshot_drift')
    catalog = read(catalog_path); model_by = {m['id']: m for m in catalog['data']}
    configs = {}; identities = {}
    for config in presets['models']:
        key = config['key']
        if key in configs or config['model'] not in model_by:
            raise IdentityIntegrityError('duplicate_or_unobserved_model')
        configs[key] = config
        identities[key] = public_identity(config, base=path.parent, catalog=model_by[config['model']])
    return presets, configs, identities


def validate_reasoning(config, identity):
    selected = config.get('reasoning')
    info = identity['catalog_model'].get('reasoning', {})
    if selected is None:
        # Explicit omission is a valid user preset; record the provider default.
        return
    if not isinstance(selected, dict) or set(selected) - {'enabled', 'effort', 'max_tokens', 'exclude'}:
        raise ValueError('invalid_reasoning_configuration')
    if 'enabled' in selected and type(selected['enabled']) is not bool:
        raise ValueError('invalid_reasoning_enabled_flag')
    if 'exclude' in selected and type(selected['exclude']) is not bool:
        raise ValueError('invalid_reasoning_exclude_flag')
    disabled = selected.get('enabled') is False or selected.get('effort') == 'none'
    if disabled and info.get('mandatory') is True:
        raise ValueError('observed_model_requires_reasoning')
    if 'effort' in selected:
        supported = info.get('supported_efforts')
        if supported is not None and selected['effort'] not in supported:
            raise ValueError('unobserved_reasoning_effort')
    if 'max_tokens' in selected:
        if 'effort' in selected or type(selected['max_tokens']) is not int or selected['max_tokens'] < 1:
            raise ValueError('invalid_reasoning_token_configuration')
        if info.get('supports_max_tokens') is not True:
            raise ValueError('catalog_does_not_advertise_precise_reasoning_budget')
    if selected and 'reasoning' not in identity['endpoint'].get('supported_parameters', []):
        raise ValueError('endpoint_does_not_support_reasoning_parameter')


def rate_config(identity):
    pricing = identity['endpoint']['pricing']
    tiers = [pricing, *pricing.get('overrides', [])]
    prompt = max(money(tier.get('prompt', pricing['prompt'])) for tier in tiers)
    completion = max(money(tier.get('completion', pricing['completion'])) for tier in tiers)
    # Text-only bodies expose no image/audio/search/cache-control fields. Preserve
    # all public prices; reserve against possible cache-write premiums nonetheless.
    reserve_prompt = max([prompt] + [money(tier[k]) for tier in tiers for k in
                         ('input_cache_write', 'input_cache_write_1h') if k in tier])
    internal = money(pricing.get('internal_reasoning', '0'))
    if internal > completion:
        raise ValueError('internal_reasoning_rate_needs_explicit_reservation_policy')
    known = {'prompt', 'completion', 'image', 'audio', 'input_audio_cache', 'web_search',
             'input_cache_read', 'input_cache_write', 'input_cache_write_1h',
             'internal_reasoning', 'discount', 'overrides'}
    for key, value in pricing.items():
        if key not in known and money(value) != 0:
            raise ValueError('unaccounted_public_charge_requires_policy')
    return {'caps': {'prompt': str(prompt * 1000000), 'completion': str(completion * 1000000)},
            'reserve_prompt_per_token': reserve_prompt, 'completion_per_token': completion,
            'full_public_prices': deepcopy(pricing)}


def reservation(body, config, identity):
    rates = rate_config(identity)
    allowance = len(safe.canonical(body)) + 1024 + 32 * len(body['messages'])
    multiplier = money(config.get('completion_reservation_multiplier', '1'))
    if multiplier < 1:
        raise ValueError('completion_reservation_multiplier_below_one')
    completion_tokens = Decimal(body['max_tokens']) * multiplier
    result = Decimal(allowance) * rates['reserve_prompt_per_token'] + completion_tokens * rates['completion_per_token']
    return {'reservation_usd': str(result), 'input_token_allowance': allowance,
            'completion_token_allowance': str(completion_tokens),
            'billing_bound_guaranteed': False}


def load_recipes(path=RECIPES):
    value = read(path)
    if value.get('schema') != 'loom.frontier_recipe_set/1':
        raise ValueError('invalid_recipe_set_schema')
    for track, recipes in value['tracks'].items():
        if track not in TRACKS:
            raise ValueError('unknown_recipe_track')
        for name, recipe in recipes.items():
            if (not isinstance(recipe['system'], str) or
                    recipe['system_sha256'] != hashlib.sha256(recipe['system'].encode()).hexdigest()):
                raise ValueError('recipe_system_hash_drift')
    return value


def body_for(payload, track, recipe_name, config, identity, recipes):
    if track not in TRACKS:
        raise ValueError('unknown_track')
    recipe = recipes['tracks'][track][recipe_name]
    if (config['model'] != identity['snapshot']['data']['id'] or
            config['provider'] != identity['endpoint']['tag']):
        raise IdentityIntegrityError('configured_model_provider_not_public_identity')
    validate_reasoning(config, identity)
    supported = set(identity['endpoint'].get('supported_parameters', []))
    output_max = config['max_tokens'][track]
    if type(output_max) is not int or output_max < 1 or output_max > identity['endpoint']['max_completion_tokens']:
        raise ValueError('output_tokens_exceed_observed_endpoint_capability')
    if 'max_tokens' not in supported or 'response_format' not in supported:
        raise ValueError('endpoint_missing_requested_parameters')
    body = {'model': config['model'], 'messages': [{'role': 'system', 'content': recipe['system']},
            {'role': 'user', 'content': safe.canonical(payload).decode()}],
            'stream': False, 'max_tokens': output_max,
            'response_format': {'type': 'json_object'}, 'usage': {'include': True},
            'provider': {'only': [config['provider']], 'allow_fallbacks': False,
                         'require_parameters': True, 'max_price': rate_config(identity)['caps']}}
    if config.get('reasoning') is not None:
        body['reasoning'] = deepcopy(config['reasoning'])
    for key, value in config.get('generation_parameters', {}).items():
        if key not in ('temperature', 'top_p', 'seed') or key not in supported:
            raise ValueError('unsupported_generation_parameter')
        body[key] = value
    return body


def dependencies():
    paths = [Path(__file__), Path(__file__).with_name('bounded_runner.py'),
             Path(panel.__file__), Path(free.__file__), Path(integrity.__file__), Path(safe.__file__)]
    return {str(p.relative_to(panel.ROOT)): sha(p) for p in paths}


def selected_cases(presets, selection):
    cases = panel.load_dev_inputs(); ids = presets['selection_presets'][selection]
    if ids == 'all':
        return cases
    if not isinstance(ids, list) or len(ids) != len(set(ids)):
        raise ValueError('invalid_selection_preset')
    by = {c['id']: c for c in cases}
    if any(i not in by for i in ids):
        raise ValueError('selection_contains_unknown_dev_case')
    return [by[i] for i in ids]


def dev_rows(cases, track, recipe, config, identity, recipes):
    rows = []
    for case in cases:
        pairs = [(q['id'], panel.query_payload(case, q)) for q in case['judgment_queries']
                 if q['scope'] == 'explicit_source'] if track == 'supplied_edge_judgment' else [(
                 case['id'], free.source_payload(case) if track == 'free_source_extraction' else panel.extraction_payload(case))]
        for ident, payload in pairs:
            body = body_for(payload, track, recipe, config, identity, recipes)
            rows.append({'id': ident, 'body': body, 'reservation_usd': reservation(body, config, identity)['reservation_usd'],
                         'metadata': {'case_id': case['id'], 'language': case['language'], 'track': track,
                                      'input_hash': safe.digest(payload)}})
    return rows


def _private_base_planner(limits):
    scope = dict(vars(safe)); scope['_money'] = money
    for key, name in (('max_input_bytes', 'MAX_INPUT_BYTES'), ('max_body_bytes', 'MAX_BODY_BYTES'),
                      ('max_response_bytes', 'MAX_RESPONSE_BYTES')):
        scope[name] = limits[key]
    # Helpers close over the private scope, without mutating the frozen module.
    for name, value in vars(safe).items():
        if isinstance(value, types.FunctionType) and value.__module__ == safe.__name__:
            scope[name] = types.FunctionType(value.__code__, scope, value.__name__, value.__defaults__, value.__closure__)
    scope['_money'] = money
    return scope['plan_manifest']


def plan_manifest(manifest):
    metadata = manifest.get('metadata', {})
    if metadata.get('schema') != VERSION or metadata.get('split') not in ('dev', 'owner_archive'):
        raise ValueError('frontier_manifest_metadata_required')
    limits = metadata['execution']['limits']
    for key in ('max_input_bytes', 'max_body_bytes', 'max_response_bytes', 'timeout_seconds'):
        if type(limits[key]) is not int or limits[key] < 1:
            raise ValueError('invalid_configured_resource_limit')
    presets, configs, identities = load_presets()
    recipes = load_recipes()
    if (metadata.get('presets_sha256') != sha(PRESETS) or metadata.get('recipes_sha256') != sha(RECIPES) or
            metadata.get('dependencies') != dependencies()):
        raise IdentityIntegrityError('frozen_instrument_dependency_drift')
    key = metadata['model_key']; config = metadata['model_config']; identity = identities[key]
    # Configuration is fully frozen in the manifest, and can differ from presets.
    if (config['model'] != configs[key]['model'] or config['provider'] != configs[key]['provider'] or
            config['endpoint_sha256'] != configs[key]['endpoint_sha256'] or
            config['endpoint_file'] != configs[key]['endpoint_file']):
        raise IdentityIntegrityError('frozen_public_identity_drift')
    validate_reasoning(config, identity)
    if (metadata.get('public_model_aliases') != identity['model_aliases'] or
            metadata.get('public_provider_aliases') != identity['provider_aliases'] or
            metadata.get('complete_public_prices') != identity['endpoint']['pricing']):
        raise IdentityIntegrityError('manifest_public_identity_or_pricing_claim_drift')
    if metadata['split'] == 'dev' and metadata.get('fixture_manifest_sha256') != sha(panel.FIXTURE / 'manifest.json'):
        raise IdentityIntegrityError('dev_fixture_manifest_drift')
    cap = money(metadata['execution']['batch_reservation_cap_usd'])
    budget = money(metadata['execution']['budget_usd'])
    if money(manifest['budget_usd']) != budget or budget <= 0 or cap <= 0:
        raise ValueError('configured_budget_mismatch')
    if len(manifest['requests']) > metadata['execution']['max_requests_per_batch']:
        raise ValueError('configured_request_batch_limit')
    expected_rates = rate_config(identity)
    evidence = [{'model': config['model'], 'provider': config['provider'],
                 'pricing': {k: identity['endpoint']['pricing'][k] for k in ('prompt', 'completion')},
                 'source_url': safe.API_ROOT + '/models/' + config['model'] + '/endpoints',
                 'retrieved_at': config['retrieved_at']}]
    if manifest['pricing_evidence'] != evidence:
        raise IdentityIntegrityError('observed_endpoint_pricing_drift')
    all_plan_rows = []; seen = set(); total = Decimal(0)
    for offset in range(0, len(manifest['requests']), 256):
        batch = deepcopy(manifest); batch['requests'] = deepcopy(manifest['requests'][offset:offset + 256]); batch['max_requests'] = len(batch['requests'])
        for row in batch['requests']:
            original = next(r for r in manifest['requests'] if r['id'] == row['id'])
            if row['id'] in seen:
                raise ValueError('duplicate_request_id')
            seen.add(row['id'])
            body = original['body']
            if (body['model'] != config['model'] or body['provider']['only'] != [config['provider']] or
                    body['provider']['max_price'] != expected_rates['caps'] or
                    body.get('reasoning') != config.get('reasoning')):
                raise IdentityIntegrityError('request_model_or_reasoning_configuration_drift')
            if body['max_tokens'] > identity['endpoint']['max_completion_tokens']:
                raise ValueError('output_tokens_exceed_observed_endpoint_capability')
            required = reservation(body, config, identity)
            if money(original['reservation_usd']) < money(required['reservation_usd']):
                raise ValueError('frontier_reservation_below_full_allowance')
            # Structural proof uses original guard unchanged; new, independently
            # validated reasoning and endpoint token capability are projected out.
            row['body']['reasoning'] = {'enabled': False}
            row['body']['max_tokens'] = min(row['body']['max_tokens'], 16384)
        base = _private_base_planner(limits)(batch)
        for projected, original in zip(base['requests'], manifest['requests'][offset:offset + 256]):
            amount = money(original['reservation_usd']); total += amount
            projected.update(request_hash=safe.digest(original['body']),
                             **reservation(original['body'], config, identity))
            projected['reservation_usd'] = str(amount)
            all_plan_rows.append(projected)
    if not all_plan_rows or total > budget or total > cap:
        raise ValueError('configured_reservations_exceeded')
    if manifest['max_requests'] != len(manifest['requests']):
        raise ValueError('planned_request_count_mismatch')
    return {'schema': 'loom.openrouter_plan/1', 'runner_version': VERSION,
            'manifest_hash': safe.digest(manifest), 'experiment_id': manifest['experiment_id'],
            'budget_usd': str(budget), 'total_reservation_usd': str(total),
            'request_count': len(all_plan_rows), 'requests': all_plan_rows,
            'billing_bound_guaranteed': False, 'no_graph_promotion': True}


def manifest_for(rows, config, model_key, track, recipe, selection, execution, batch_index):
    presets, configs, identities = load_presets(); identity = identities[model_key]
    model_slug = model_key.replace('_', '-')
    experiment = f'frontier-{model_slug}-{track.replace("_", "-")}-{recipe}-{selection}-b{batch_index:03d}'
    value = {'schema': safe.MANIFEST_SCHEMA, 'experiment_id': experiment,
             'budget_usd': str(money(execution['budget_usd'])), 'max_requests': len(rows), 'requests': rows,
             'pricing_evidence': [{'model': config['model'], 'provider': config['provider'],
                 'pricing': {k: identity['endpoint']['pricing'][k] for k in ('prompt', 'completion')},
                 'source_url': safe.API_ROOT + '/models/' + config['model'] + '/endpoints',
                 'retrieved_at': config['retrieved_at']}],
             'metadata': {'schema': VERSION, 'split': 'dev', 'model_key': model_key,
                 'model_config': deepcopy(config), 'track': track, 'recipe': recipe,
                 'selection': selection, 'execution': deepcopy(execution),
                 'presets_sha256': sha(PRESETS), 'recipes_sha256': sha(RECIPES),
                 'dependencies': dependencies(), 'fixture_manifest_sha256': sha(panel.FIXTURE / 'manifest.json'),
                 'public_model_aliases': identity['model_aliases'], 'public_provider_aliases': identity['provider_aliases'],
                 'complete_public_prices': identity['endpoint']['pricing'], 'batch_index': batch_index,
                 'live_policy': presets['live_policy'], 'canonical_graph_write': False}}
    plan_manifest(value)
    return value


def prepare_dev(output, *, model_key, track, recipe='baseline', selection='balanced8', config_override=None, execution_override=None):
    if track == 'graph_packet':
        raise ValueError('use_packet_builder_for_graph_packet_input')
    presets, configs, identities = load_presets(); recipes = load_recipes()
    config = deepcopy(configs[model_key]); config.update(deepcopy(config_override or {}))
    execution = deepcopy(presets['default_execution']); execution.update(deepcopy(execution_override or {}))
    cases = selected_cases(presets, selection)
    rows = dev_rows(cases, track, recipe, config, identities[model_key], recipes)
    cap = money(execution['batch_reservation_cap_usd']); size = execution['max_requests_per_batch']
    batches = []; current = []; total = Decimal(0)
    for row in rows:
        cost = money(row['reservation_usd'])
        if cost > cap:
            raise ValueError('individual_request_exceeds_selected_batch_reservation_preset')
        if current and (len(current) >= size or total + cost > cap):
            batches.append(current); current = []; total = Decimal(0)
        current.append(row); total += cost
    if current: batches.append(current)
    target = Path(output); target.mkdir(parents=True, exist_ok=False)
    summaries = []
    for n, batch in enumerate(batches, 1):
        folder = target / f'batch{n:03d}' / 'prepared'; folder.mkdir(parents=True)
        manifest = manifest_for(batch, config, model_key, track, recipe, selection, execution, n)
        panel.write_new(folder / 'manifest.json', manifest)
        summaries.append({'batch': n, 'manifest': str(folder.relative_to(target) / 'manifest.json'),
                          'manifest_sha256': sha(folder / 'manifest.json'),
                          'request_ids': [r['id'] for r in batch],
                          'reservation_usd': plan_manifest(manifest)['total_reservation_usd']})
    summary = {'schema': VERSION, 'prepared_only': True, 'paid_calls': 0, 'split': 'dev',
               'model_key': model_key, 'track': track, 'recipe': recipe, 'selection': selection,
               'case_ids': [c['id'] for c in cases], 'planned_requests': len(rows), 'batches': summaries,
               'total_reservation_usd': str(sum((money(b['reservation_usd']) for b in summaries), Decimal(0))),
               'batch_cap_is_not_global_budget_reset': True, 'live_policy': presets['live_policy']}
    panel.write_new(target / 'batch_index.json', summary)
    return summary


def response_content(raw, attempt, identity):
    value = safe.parse_json(raw); usage = value.get('usage') if isinstance(value, dict) else None
    if not isinstance(usage, dict) or usage.get('is_byok') is not False or 'cost' not in usage:
        raise integrity.BillingIntegrityError('frontier_response_billing_metadata_missing_or_invalid')
    integrity.billing_consistent(usage['cost'], attempt)
    if value.get('model') not in identity['model_aliases'] or value.get('provider') not in identity['provider_aliases']:
        raise IdentityIntegrityError('frontier_response_model_provider_unobserved')
    parsed = safe._response_result(raw)
    if parsed['state'] != 'completed':
        raise ValueError('frontier_response_not_complete')
    return value['choices'][0]['message']['content']


def replay(manifest_path, run_dir):
    manifest = read(manifest_path); plan = plan_manifest(manifest); metadata = manifest['metadata']
    if metadata['split'] != 'dev' or metadata['track'] == 'graph_packet':
        raise ValueError('research_scorer_is_dev_only')
    directory = Path(run_dir); ledger = read(directory / 'ledger.json')
    attempts = safe._validate_ledger(ledger, plan, directory)
    integrity.audit_billing_artifacts(attempts, directory)
    presets, configs, identities = load_presets(); identity = identities[metadata['model_key']]
    if (metadata['public_model_aliases'] != identity['model_aliases'] or
            metadata['public_provider_aliases'] != identity['provider_aliases']):
        raise IdentityIntegrityError('manifest_aliases_not_exact_public_observations')
    cases = selected_cases(presets, metadata['selection']); by = {c['id']: c for c in cases}
    expected = {r['id']: r for r in dev_rows(cases, metadata['track'], metadata['recipe'],
                metadata['model_config'], identity, load_recipes())}
    attempt_by = {a['id']: a for a in attempts}; output = []
    for request in manifest['requests']:
        ident = request['id']
        if ident not in expected or request != expected[ident]:
            raise IdentityIntegrityError('frozen_frontier_source_prefix_or_recipe_drift')
        attempt = attempt_by.get(ident, {})
        key = 'query_id' if metadata['track'] == 'supplied_edge_judgment' else 'case_id'
        compiled = {key: ident, 'state': 'unavailable', 'reason': 'not_attempted'}
        if attempt.get('state') == 'completed' and attempt.get('http_status') == 200:
            raw = (directory / attempt['response_file']).read_bytes()
            content = response_content(raw, attempt, identity)
            try:
                if metadata['track'] == 'supplied_edge_judgment':
                    compiled = panel.compile_gpt_judgment(content, ident)
                elif metadata['track'] == 'assisted_extraction':
                    compiled = panel.compile_extraction(content, by[ident])
                else:
                    compiled = free.compile_free(content, by[ident])
            except (KeyError, TypeError, ValueError, IndexError):
                compiled = {key: ident, 'state': 'unavailable', 'reason': 'semantic_output_rejected'}
        elif attempt:
            compiled.update(reason='attempt_not_complete', attempt_state=attempt.get('state'))
        output.append(compiled)
    summary = {'schema': VERSION, 'model': metadata['model_config']['model'],
               'provider': metadata['model_config']['provider'], 'track': metadata['track'],
               'recipe': metadata['recipe'], 'selection': metadata['selection'],
               'planned_requests': len(manifest['requests']), 'attempted_requests': len(attempts),
               'compiled_complete': sum(r['state'] == 'completed' for r in output),
               'reported_cost_usd': str(sum((money(a['reported_cost_usd']) for a in attempts if 'reported_cost_usd' in a), Decimal(0))),
               'missing_cost_attempts': sum('reported_cost_usd' not in a for a in attempts),
               'manifest_sha256': sha(manifest_path), 'ledger_sha256': sha(directory / 'ledger.json'),
               'no_graph_promotion': True, 'split': 'dev'}
    return output, summary, cases


def score(manifest_path, run_dir, output_dir):
    compiled, summary, cases = replay(manifest_path, run_dir)
    gold_by = {g['id']: g for g in panel.load_dev_gold()}
    selected = {r.get('query_id', r.get('case_id')) for r in compiled}
    if summary['track'] == 'supplied_edge_judgment':
        golds = []
        for case in cases:
            g = deepcopy(gold_by[case['id']]); g['judgments'] = [q for q in g['judgments'] if q['query_id'] in selected]
            if g['judgments']: golds.append(g)
        report = panel.score_judgments(golds, compiled)
    else:
        cases = [c for c in cases if c['id'] in selected]; golds = [gold_by[c['id']] for c in cases]
        report = free.score_free(cases, golds, compiled) if summary['track'] == 'free_source_extraction' else panel.score_extraction(cases, golds, compiled)
    target = Path(output_dir); target.mkdir(parents=True, exist_ok=False)
    panel.write_new(target / 'compiled_first.json', compiled)
    panel.write_new(target / 'execution_summary.json', summary)
    panel.write_new(target / 'score_first.json', report)
    return summary, report


def packet_body(packet, *, model_key, recipe='complete_graph', config_override=None):
    """The single external packet codec owns graph validation; no second store."""
    from ..agentic_graph_v1 import packet as codec
    # This integration deliberately calls the codec's advertised validator, rather
    # than projecting rich claims into our narrow research edge representation.
    validated = codec.validate_packet(deepcopy(packet))
    payload = deepcopy(packet) if validated is None else validated
    presets, configs, identities = load_presets(); config = deepcopy(configs[model_key]); config.update(config_override or {})
    return body_for(payload, 'graph_packet', recipe, config, identities[model_key], load_recipes())


def packet_response_preview(raw, attempt, packet, request, *, model_key, instrument_actor):
    """Preserve the model proposal, then bind measured instrument provenance.

    Only the diff's instrument origin/proposal-availability timestamp is derived.
    Native records and source known_at values remain untouched. Preview is a
    same-graph projection and never invokes auto acceptance or writes a store.
    """
    from ..agentic_graph_v1 import packet as codec
    presets, configs, identities = load_presets(); identity = identities[model_key]
    if (request['body']['model'] != configs[model_key]['model'] or
            request['body']['provider']['only'] != [configs[model_key]['provider']]):
        raise IdentityIntegrityError('packet_request_identity_mismatch')
    raw_hash = hashlib.sha256(raw).hexdigest()
    if attempt.get('response_sha256') != raw_hash or attempt.get('state') != 'completed' or attempt.get('http_status') != 200:
        raise IdentityIntegrityError('packet_response_attempt_hash_or_state_mismatch')
    content = response_content(raw, attempt, identity)
    original = safe.parse_json(content); codec.validate_diff(original, packet)
    available_at = attempt.get('finished_at')
    if not isinstance(available_at, str):
        raise IdentityIntegrityError('measured_response_availability_time_required')
    safe._timestamp(available_at)
    actual = safe.parse_json(raw)
    bound = deepcopy(original)
    bound['origin'] = {'kind': 'model', 'actor': instrument_actor, 'model': actual['model'],
        'recipe_sha256': hashlib.sha256(request['body']['messages'][0]['content'].encode()).hexdigest(),
        'response_sha256': raw_hash}
    bound['known_at'] = available_at
    codec.validate_diff(bound, packet)
    return {'schema': 'loom.frontier_packet_response/1', 'raw_response_sha256': raw_hash,
            'raw_proposed_diff': original, 'raw_proposed_diff_sha256': safe.digest(original),
            'instrument_bound_diff': bound, 'instrument_bound_diff_sha256': safe.digest(bound),
            'binding_changes': ['diff.origin', 'diff.known_at'],
            'model_response_known_at': available_at,
            'native_record_mutations_from_binding': 0,
            'preview': codec.preview_diff(packet, bound),
            'canonical_store_written': False, 'accepted': False,
            'acceptance_policy_owner': 'agentic_graph_v1.packet.apply_diff'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__); commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare-dev'); prep.add_argument('--output', required=True, type=Path)
    prep.add_argument('--model', required=True); prep.add_argument('--track', choices=TRACKS[:-1], required=True)
    prep.add_argument('--recipe', default='baseline'); prep.add_argument('--selection', default='balanced8')
    scoring = commands.add_parser('score'); scoring.add_argument('manifest', type=Path); scoring.add_argument('run_dir', type=Path); scoring.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == 'prepare-dev':
        result = prepare_dev(args.output, model_key=args.model, track=args.track, recipe=args.recipe, selection=args.selection)
    else:
        summary, report = score(args.manifest, args.run_dir, args.output); result = {'summary': summary, 'report': report}
    print(safe.canonical(result).decode())


if __name__ == '__main__':
    main()
