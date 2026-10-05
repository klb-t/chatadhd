"""Offline exact request repetitions and supplied-source label receipt replay.

Selection, population, order, count and optional correlated bootstrap are caller
JSON data. No prompt compilation, provider I/O, retries, answer repair, corpus
creation or automatic recipe promotion is performed.
"""
from __future__ import annotations
import argparse
from copy import deepcopy
from decimal import Decimal, InvalidOperation
import hashlib
import itertools
from pathlib import Path
import random

from loom.tools.structure import graph_panel_live as panel
from loom.tools.structure import method_graph_export_v1 as graph
from loom.tools.structure import openrouter_runner as wire
from loom.tools.structure import research_programme_manifest as manifests

SCHEMA = 'loom.programme_repetition_policy/1'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def detached(value):
    return wire.parse_json(wire.canonical(value))


def _unique(values, reason):
    if not isinstance(values, list) or not values or any(not isinstance(x, str) or not x for x in values) or len(set(values)) != len(values):
        raise ValueError(reason)
    return values


def _policy(raw):
    p = wire.parse_json(raw)
    wire._keys(p, {'schema', 'programme_id', 'stage_id', 'repetitions', 'order_dimensions', 'seed',
                   'source_manifest_sha256', 'selection_sha256', 'selection_ids_pointer', 'arm_membership',
                   'arm_metadata_pointer', 'query_metadata_pointer', 'planned_query_ids', 'query_identity',
                   'prompt_pointers', 'scoring'})
    if p['schema'] != SCHEMA or type(p['repetitions']) is not int or p['repetitions'] < 1:
        raise ValueError('repetition_policy_invalid')
    manifests._identity(p['programme_id']); manifests._identity(p['stage_id'])
    if p['seed'] is not None or not isinstance(p['order_dimensions'], list) or sorted(p['order_dimensions']) != ['arm', 'query', 'repetition']:
        raise ValueError('repetition_order_invalid')
    for key in ('source_manifest_sha256', 'selection_sha256'):
        if not isinstance(p[key], str) or not manifests._SHA256.fullmatch(p[key]):
            raise ValueError('repetition_source_hash_required')
    _unique(p['planned_query_ids'], 'repetition_query_inventory_invalid')
    _unique(p['prompt_pointers'], 'repetition_prompt_pointers_invalid')
    wire._keys(p['query_identity'], {'json_string_pointer', 'id_pointer'})
    wire._keys(p['scoring'], {'gold_sha256', 'bootstrap', 'label_response_states'})
    states = _unique(p['scoring']['label_response_states'], 'repetition_label_response_states_invalid')
    if set(states) - {'completed', 'pending_billing'}:
        raise ValueError('repetition_ambiguous_label_state_unsupported')
    if not isinstance(p['scoring']['gold_sha256'], str) or not manifests._SHA256.fullmatch(p['scoring']['gold_sha256']):
        raise ValueError('repetition_gold_hash_required')
    return p


def _source(source_path, selection_raw, policy_raw):
    p = _policy(policy_raw); path = Path(source_path); raw = path.read_bytes()
    if sha(raw) != p['source_manifest_sha256'] or sha(selection_raw) != p['selection_sha256']:
        raise ValueError('repetition_source_snapshot_changed')
    source = manifests.read_manifest_bytes(raw)
    ops = manifests.load_operations(source, base_dir=path.parent)
    selected = _unique(graph.pointer(wire.parse_json(selection_raw), p['selection_ids_pointer']), 'repetition_selection_invalid')
    members = p['arm_membership']
    if not isinstance(members, list) or len(members) != len(selected):
        raise ValueError('repetition_membership_invalid')
    by_selection = {}; arms = []
    for row in members:
        wire._keys(row, {'selection_id', 'arm_id'})
        if row['selection_id'] in by_selection or not isinstance(row['arm_id'], str) or not row['arm_id']:
            raise ValueError('repetition_membership_invalid')
        by_selection[row['selection_id']] = row['arm_id']
    if set(by_selection) != set(selected):
        raise ValueError('repetition_selected_membership_mismatch')
    arms = _unique([by_selection[x] for x in selected], 'repetition_arm_inventory_invalid')
    index = {}
    for op in ops:
        meta = op.get('metadata', {})
        arm = graph.pointer(meta, p['arm_metadata_pointer'])
        if arm not in arms:
            continue
        query = graph.pointer(meta, p['query_metadata_pointer'])
        if query not in p['planned_query_ids'] or (arm, query) in index:
            raise ValueError('repetition_source_query_inventory_mismatch')
        text = graph.pointer(op['request_body'], p['query_identity']['json_string_pointer'])
        body_query = graph.pointer(wire.parse_json(text), p['query_identity']['id_pointer'])
        if body_query != query:
            raise ValueError('repetition_body_query_identity_mismatch')
        index[(arm, query)] = op
    if set(index) != set(itertools.product(arms, p['planned_query_ids'])):
        raise ValueError('repetition_source_query_inventory_mismatch')
    return p, source, index, arms


def _build(source_path, selection_raw, policy_raw):
    p, source, index, arms = _source(source_path, selection_raw, policy_raw)
    dimensions = {'arm': arms, 'query': p['planned_query_ids'], 'repetition': list(range(1, p['repetitions'] + 1))}
    operations, artifacts = [], {}
    for combination in itertools.product(*(dimensions[key] for key in p['order_dimensions'])):
        cell = dict(zip(p['order_dimensions'], combination)); arm, query, repetition = cell['arm'], cell['query'], cell['repetition']
        original = index[(arm, query)]; raw = original['request_bytes']; body = original['request_body']
        identity = [p['programme_id'], p['stage_id'], sha(policy_raw), sha(selection_raw), p['source_manifest_sha256'], original['operation_id'], repetition]
        new_id = 'repeat.' + wire.digest(identity)
        filename = 'requests/' + original['request_sha256'] + '.json'; artifacts[filename] = raw
        prompts = {ptr: wire.digest(graph.pointer(body, ptr)) for ptr in p['prompt_pointers']}
        metadata = deepcopy(original.get('metadata', {}))
        metadata['scientific_repetition'] = {'source_operation_id': original['operation_id'], 'source_programme_id': source['programme_id'],
            'source_stage_id': source['stage_id'], 'source_manifest_sha256': p['source_manifest_sha256'],
            'source_request_sha256': original['request_sha256'], 'source_prompt_sha256': prompts,
            'arm_id': arm, 'query_id': query, 'repetition_index': repetition,
            'qualified_query_id': 'rep.' + wire.digest([repetition, query]), 'retry_or_replacement': False}
        operation = {key: deepcopy(original[key]) for key in ('route_id', 'request_sha256', 'model_id', 'provider_id', 'units_upper_bounds')}
        operation.update(operation_id=new_id, request_file=filename, metadata=metadata)
        if 'minimum_reservation_usd' in original:
            operation['minimum_reservation_usd'] = deepcopy(original['minimum_reservation_usd'])
        operations.append(operation)
    metadata = deepcopy(source.get('metadata', {}))
    minimum_known = all('minimum_reservation_usd' in op for op in operations)
    minimum_sum = sum((manifests._quantity(op['minimum_reservation_usd']) for op in operations if 'minimum_reservation_usd' in op), Decimal(0))
    metadata['scientific_repetition'] = {'policy': detached(p), 'policy_raw_sha256': sha(policy_raw),
        'selection_raw_sha256': sha(selection_raw), 'source_manifest_sha256': p['source_manifest_sha256'],
        'source_operation_count': len(index), 'selected_arms': arms, 'planned_query_ids': p['planned_query_ids'],
        'repetitions': p['repetitions'], 'planned_operations': len(operations),
        'planned_operations_per_arm_per_repetition': len(p['planned_query_ids']),
        'minimum_reservation_sum_usd': format(minimum_sum, 'f') if minimum_known else None,
        'current_pair_prices_fx_and_budget_gate_required': True, 'order_dimensions': p['order_dimensions'],
        'seed': p['seed'], 'new_model_calls': 0, 'new_corpus_created': False, 'gold_loaded_by_preparation': False,
        'evidence_boundary': 'Scientific repeats of identical frozen request bytes; no retry, replacement or independent holdout.'}
    return {'schema': manifests.SCHEMA, 'programme_id': p['programme_id'], 'stage_id': p['stage_id'], 'operations': operations, 'metadata': metadata}, artifacts, p


def prepare(source_path, selection_raw, policy_raw, output_dir):
    """Materialize distinct planned operation IDs with exact original bytes."""
    manifest, artifacts, _ = _build(source_path, selection_raw, policy_raw)
    output = Path(output_dir)
    # Validate complete sources/config and all output collisions before mutation.
    manifest_raw = wire.canonical(manifest) + b'\n'
    for name, raw in {**artifacts, 'manifest.json': manifest_raw}.items():
        target = output / name
        if target.exists() and target.read_bytes() != raw:
            raise ValueError('repetition_output_already_exists')
    for name, raw in artifacts.items():
        manifests._write_exact(output / name, raw)
    manifests.validate_manifest(manifest, base_dir=output)
    manifests._write_exact(output / 'manifest.json', manifest_raw)
    return manifest


def verify(source_path, selection_raw, policy_raw, manifest_path):
    """Rebuild immutable inventory/metadata and reject changed body or recipe."""
    expected, _, _ = _build(source_path, selection_raw, policy_raw)
    actual = manifests.load_manifest(manifest_path)
    if wire.canonical(actual) != wire.canonical(expected):
        raise ValueError('repetition_manifest_source_binding_changed')
    return actual


def _money(value):
    if type(value) not in (int, float, str):
        raise ValueError('repetition_cost_invalid')
    try:
        result = Decimal(str(value))
    except InvalidOperation:
        raise ValueError('repetition_cost_invalid') from None
    if not result.is_finite() or result < 0:
        raise ValueError('repetition_cost_invalid')
    return result


def _inventory(rows, allowed, reason):
    if not isinstance(rows, list):
        raise ValueError(reason)
    result = {}
    for row in rows:
        if not isinstance(row, dict) or row.get('operation_id') not in allowed or row['operation_id'] in result:
            raise ValueError(reason)
        result[row['operation_id']] = row
    return result


def _bootstrap(gold, predictions, specification):
    if specification is None:
        return None
    wire._keys(specification, {'samples', 'seed', 'family_pointer', 'confidence'})
    if type(specification['samples']) is not int or specification['samples'] < 1 or type(specification['seed']) is not int:
        raise ValueError('repetition_bootstrap_invalid')
    confidence = specification['confidence']
    if type(confidence) not in (int, float) or not 0 < confidence < 1:
        raise ValueError('repetition_bootstrap_invalid')
    families = {}
    for group in gold:
        family = graph.pointer(group, specification['family_pointer'])
        if not isinstance(family, str) or not family:
            raise ValueError('repetition_bootstrap_family_invalid')
        families.setdefault(family, []).extend(group['judgments'])
    rng = random.Random(specification['seed']); keys = sorted(families); estimates = []
    actual = {row['query_id']: row for row in predictions}
    for _ in range(specification['samples']):
        sample = [query for _ in keys for query in families[rng.choice(keys)]]
        correct = sum(actual.get(q['query_id'], {}).get('state') == 'completed' and actual[q['query_id']].get('label') == q['label'] for q in sample)
        estimates.append(correct / len(sample))
    estimates.sort(); alpha = (1-confidence)/2
    lower = estimates[int(alpha * (len(estimates)-1))]; upper = estimates[int((1-alpha) * (len(estimates)-1))]
    return {'method': 'paired_repetition_family_cluster_bootstrap', 'samples': specification['samples'],
            'seed': specification['seed'], 'confidence': confidence, 'family_count': len(keys),
            'correctness_all_planned_interval': [lower, upper],
            'boundary': 'All repetitions of each sampled source family remain together; authored DEV dependence remains.'}


def score(source_path, selection_raw, policy_raw, manifest_path, bundle_raw, gold_raw, *, gold_cases_pointer=''):
    """Use unchanged pure Jev compiler/panel scorer on all planned first rows.

    Inputs are caller-supplied public normalized receipts. Exact public hashes and
    projections are bound; private HTTP/generation bytes are not read here. Credit
    replay status is an explicit input attestation, distinct from label agreement.
    The runtime JSON pointer selects a cases array after the entire raw gold
    container passes its existing hash binding; preparation does not use it.
    """
    manifest = verify(source_path, selection_raw, policy_raw, manifest_path); p = _policy(policy_raw)
    if sha(gold_raw) != p['scoring']['gold_sha256']:
        raise ValueError('repetition_gold_snapshot_changed')
    gold = graph.pointer(wire.parse_json(gold_raw), gold_cases_pointer)
    if not isinstance(gold, list) or not gold:
        raise ValueError('repetition_gold_invalid')
    gold_queries = [q['query_id'] for group in gold for q in group['judgments']]
    if len(gold_queries) != len(set(gold_queries)) or set(gold_queries) != set(p['planned_query_ids']):
        raise ValueError('repetition_gold_inventory_mismatch')
    bundle = wire.parse_json(bundle_raw); allowed = {op['operation_id'] for op in manifest['operations']}
    if (bundle.get('schema') != 'loom.programme_results/1' or bundle.get('programme_id') != manifest['programme_id']
        or bundle.get('stage_id') != manifest['stage_id'] or bundle.get('manifest_sha256') != sha(Path(manifest_path).read_bytes())
        or bundle.get('planned_operation_ids') != [op['operation_id'] for op in manifest['operations']]
        or bundle.get('planned_operations') != len(allowed)):
        raise ValueError('repetition_bundle_binding_changed')
    rows = _inventory(bundle.get('rows'), allowed, 'repetition_duplicate_or_unknown_first_row')
    responses = _inventory(bundle.get('responses'), allowed, 'repetition_duplicate_or_unknown_first_capture')
    requests = _inventory(bundle.get('requests'), allowed, 'repetition_request_inventory_invalid')
    if set(responses) - set(rows):
        raise ValueError('repetition_orphan_first_capture')
    if set(requests) != allowed or bundle.get('saved_row_count') != len(rows):
        raise ValueError('repetition_request_inventory_invalid')
    records = []; generation_ids = set(); first_response_hashes = set(); verified_generation_hashes = set()
    for op in manifest['operations']:
        ident = op['operation_id']; mapping = op['metadata']['scientific_repetition']; request = requests[ident]
        original_body = wire.parse_json(manifests.read_request(op, Path(manifest_path).parent))
        if request.get('request_sha256') != op['request_sha256'] or wire.canonical(request.get('body')) != wire.canonical(original_body):
            raise ValueError('repetition_public_request_changed')
        row = rows.get(ident); response = responses.get(ident); prediction = {'query_id': mapping['query_id'], 'state': 'unavailable'}
        label_error = 'missing_first_row'; actual_cost = None
        if row is not None:
            if row.get('request_sha256') != op['request_sha256'] or row.get('manifest_sha256') != bundle['manifest_sha256']:
                raise ValueError('repetition_first_row_binding_changed')
            if response is not None:
                response_hash = row.get('response_sha256')
                generation_hash = row.get('generation_sha256')
                if not isinstance(response_hash, str) or not manifests._SHA256.fullmatch(response_hash):
                    raise ValueError('repetition_first_capture_hash_invalid')
                if generation_hash is not None and (not isinstance(generation_hash, str) or not manifests._SHA256.fullmatch(generation_hash)):
                    raise ValueError('repetition_generation_capture_hash_invalid')
                if response.get('response_sha256') != response_hash or response.get('generation_sha256') != generation_hash:
                    raise ValueError('repetition_first_capture_binding_changed')
            label_error = 'first_attempt_unavailable'
            if response is not None and isinstance(response.get('projection'), dict):
                projected = response['projection']; generation = projected.get('id')
                if generation is not None:
                    if not isinstance(generation, str) or not generation or generation != row.get('generation_id') or generation in generation_ids:
                        raise ValueError('repetition_generation_identity_invalid')
                    generation_ids.add(generation)
                elif row.get('state') in {'completed', 'pending_billing'}:
                    raise ValueError('repetition_generation_identity_invalid')
                if row.get('state') in p['scoring']['label_response_states'] and row.get('http_status') == 200 and row.get('response_available') is True and row.get('response_ledger_bound') is True and row.get('exact_sent_request_capture_verified') is True:
                    if row['response_sha256'] in first_response_hashes:
                        raise ValueError('repetition_duplicate_successful_first_capture')
                    first_response_hashes.add(row['response_sha256'])
                    try:
                        if projected.get('model') != row.get('response_model') or row.get('requested_model') != op['model_id'] or row.get('requested_provider') != op['provider_id']:
                            raise ValueError('response_identity_mismatch')
                        reported = _money(projected.get('usage', {}).get('cost'))
                        if reported != _money(row.get('reported_cost_usd')):
                            raise ValueError('reported_cost_mismatch')
                        answers = projected.get('answers')
                        if not isinstance(answers, dict) or set(answers) != set(original_body.get('questions', {})):
                            raise ValueError('answer_inventory_mismatch')
                        probabilities = {}
                        for key, answer in answers.items():
                            if not isinstance(answer, dict) or answer.get('type') != 'noul' or type(answer.get('noul')) not in (int, float):
                                raise ValueError('invalid_noul_answer')
                            probabilities[key] = answer['noul']
                        prediction = panel.compile_jev_judgment({'probabilities': probabilities}, mapping['query_id'])
                        label_error = None
                    except (ValueError, KeyError, TypeError):
                        label_error = 'invalid_first_source_label_answer'
                if row.get('state') == 'completed' and row.get('billing_verified') is True and row.get('billing_replay_verified') is True and row.get('is_byok') is False:
                    if (row.get('billing_mode') is not None and row.get('billing_mode') != 'credits'
                            or projected.get('usage', {}).get('is_byok') is True):
                        raise ValueError('repetition_credit_attestation_contradiction')
                    receipt = response.get('generation_projection', {})
                    if (receipt.get('id') != generation or receipt.get('model') != row.get('observed_model')
                        or receipt.get('provider_name') != row.get('observed_provider') or receipt.get('is_byok') is not False
                        or not isinstance(row.get('generation_sha256'), str) or not manifests._SHA256.fullmatch(row['generation_sha256'])):
                        raise ValueError('repetition_credit_receipt_projection_mismatch')
                    if row['generation_sha256'] in verified_generation_hashes:
                        raise ValueError('repetition_duplicate_verified_generation_capture')
                    verified_generation_hashes.add(row['generation_sha256'])
                    actual_cost = _money(row.get('actual_cost_usd'))
                    if actual_cost != _money(receipt.get('total_cost')) or actual_cost != _money(row.get('reported_cost_usd')):
                        raise ValueError('repetition_credit_cost_mismatch')
        records.append({'operation_id': ident, **deepcopy(mapping), 'prediction': prediction, 'label_error': label_error,
            'source_first_row_state': None if row is None else row.get('state'),
            'response_sha256': None if row is None else row.get('response_sha256'),
            'generation_id': None if row is None else row.get('generation_id'),
            'generation_sha256': None if row is None else row.get('generation_sha256'),
            'actual_cost_usd': None if actual_cost is None else format(actual_cost, 'f'),
            'billing_complete': actual_cost is not None, 'native_grounding': None, 'semantic_adequacy': None})
    arms = manifest['metadata']['scientific_repetition']['selected_arms']; reports = []
    for arm in arms:
        arm_rows = [r for r in records if r['arm_id'] == arm]; per_repeat = []; pooled_gold = []; pooled_predictions = []
        for repetition in range(1, p['repetitions']+1):
            selected = [r for r in arm_rows if r['repetition_index'] == repetition]
            report = panel.score_judgments(gold, [r['prediction'] for r in selected])
            per_repeat.append({'repetition_index': repetition, 'source_label_report': report})
            qualification = {r['query_id']: r['qualified_query_id'] for r in selected}
            for group in gold:
                copy = deepcopy(group)
                for query in copy['judgments']:query['query_id'] = qualification[query['query_id']]
                pooled_gold.append(copy)
            for record in selected:
                pred = deepcopy(record['prediction']); pred['query_id'] = record['qualified_query_id']; pooled_predictions.append(pred)
        known = sum((Decimal(r['actual_cost_usd']) for r in arm_rows if r['actual_cost_usd'] is not None), Decimal(0))
        unknown = sum(r['actual_cost_usd'] is None for r in arm_rows)
        reports.append({'arm_id': arm, 'per_repetition': per_repeat, 'pooled_source_label_report': panel.score_judgments(pooled_gold, pooled_predictions),
            'cost': {'known_verified_usd': format(known, 'f'), 'unknown_planned_attempts': unknown, 'complete': unknown == 0,
                     'total_usd': format(known, 'f') if unknown == 0 else None},
            'correlated_family_bootstrap': _bootstrap(pooled_gold, pooled_predictions, p['scoring']['bootstrap']),
            'native_grounding': None, 'semantic_adequacy': None})
    return {'schema': 'loom.programme_repetition_score/1', 'programme_id': manifest['programme_id'], 'stage_id': manifest['stage_id'],
        'policy': detached(p), 'input_sha256': {'policy': sha(policy_raw), 'selection': sha(selection_raw), 'source_manifest': p['source_manifest_sha256'],
            'repeat_manifest': sha(Path(manifest_path).read_bytes()), 'normalized_bundle': sha(bundle_raw), 'gold': sha(gold_raw)},
        'gold_selection': {'cases_pointer': gold_cases_pointer, 'source_sha256': sha(gold_raw), 'selected_array_sha256': wire.digest(gold)},
        'code_sha256': {'repetition_adapter': sha(Path(__file__).read_bytes()), 'pure_judgment_compiler_and_scorer': sha(Path(panel.__file__).read_bytes()),
                       'manifest_adapter': sha(Path(manifests.__file__).read_bytes())},
        'planned_operations': len(allowed), 'received_first_rows': len(rows), 'records': records, 'arms': reports,
        'new_model_calls': 0, 'first_response_only': True, 'scientific_repetitions_are_not_retries': True,
        'native_grounding': None, 'semantic_adequacy': None, 'holdout_evaluation': False, 'automatic_promotion': False,
        'evidence_boundary': 'Exact supplied-source label agreement on reused authored DEV. Public normalized receipt attestations/projections are retained; this scorer does not replay private captures or establish native grounding, semantic adequacy, world truth or independent holdout performance.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__); commands = parser.add_subparsers(dest='command', required=True)
    for command in ('prepare', 'verify', 'score'):
        cli = commands.add_parser(command); cli.add_argument('--source-manifest', required=True, type=Path)
        cli.add_argument('--selection', required=True, type=Path); cli.add_argument('--policy', required=True, type=Path)
        if command == 'prepare':cli.add_argument('--output', required=True, type=Path)
        else:cli.add_argument('--manifest', required=True, type=Path)
        if command == 'score':
            cli.add_argument('--bundle', required=True, type=Path); cli.add_argument('--gold', required=True, type=Path); cli.add_argument('--output', required=True, type=Path)
            cli.add_argument('--gold-cases-pointer', default='')
    a = parser.parse_args(argv); arguments = (a.source_manifest, a.selection.read_bytes(), a.policy.read_bytes())
    if a.command == 'prepare':result = prepare(*arguments, a.output)
    elif a.command == 'verify':result = verify(*arguments, a.manifest)
    else:
        result = score(*arguments, a.manifest, a.bundle.read_bytes(), a.gold.read_bytes(), gold_cases_pointer=a.gold_cases_pointer)
        manifests._write_exact(a.output, wire.canonical(result)+b'\n')
    print(wire.canonical({'new_model_calls': 0, 'planned_operations': len(result['operations']) if 'operations' in result else result['planned_operations']}).decode())


if __name__ == '__main__':
    main()
