"""Offline public first-response Jev validation summaries and method graph claims.

The frozen scorer remains the instrument. This adapter binds its already scored
bytes to the exact public inputs, retains every planned slot and exports through
the existing method/profile graph machinery. No provider, private ledger, repair,
new inference or threshold selection is performed.
"""
from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
from decimal import Decimal
import gzip
import math
from pathlib import Path

from . import openrouter_runner as wire
from . import programme_results_v1 as results


class ValidationResultsError(ValueError):
    pass


LABELS = ('supported', 'refuted', 'unknown')


def _bound(raw, expected, reason):
    if not isinstance(expected, str) or len(expected) != 64 or results.sha(raw) != expected:
        raise ValidationResultsError(reason)
    return wire.parse_json(raw)


def _inventory(rows, reason):
    if not isinstance(rows, list):
        raise ValidationResultsError(reason)
    by_id = {}
    for row in rows:
        ident = row.get('operation_id') if isinstance(row, dict) else None
        if not isinstance(ident, str) or not ident or ident in by_id:
            raise ValidationResultsError(reason)
        by_id[ident] = row
    return by_id


def _protocol_valid(record, row, response, questions, policy):
    if (row is None or response is None or row.get('state') not in policy['label_response_states']
            or row.get('http_status') != policy['successful_http_status']
            or any(row.get(k) is not v for k, v in policy['label_required_flags'].items())):
        return False
    projection = response.get('projection')
    answers = projection.get('answers') if isinstance(projection, dict) else None
    if not isinstance(answers, dict) or set(answers) != set(questions):
        return False
    return all(isinstance(a, dict) and a.get('type') == policy['answer_type']
               and type(a.get('noul')) in (int, float) and math.isfinite(a['noul'])
               and 0 <= a['noul'] <= 1 for a in answers.values())


def summarize(score_raw, bundle_raw, manifest_raw, config_raw, *, observed_on,
              score_sha256, bundle_sha256, manifest_sha256, config_sha256,
              known_family_history=None):
    """Validate bindings, then aggregate all DATA-defined first-operation slots."""
    results.graph.date_text(observed_on)
    score = _bound(score_raw, score_sha256, 'score_hash_changed')
    bundle = _bound(bundle_raw, bundle_sha256, 'bundle_hash_changed')
    manifest = _bound(manifest_raw, manifest_sha256, 'manifest_hash_changed')
    config = _bound(config_raw, config_sha256, 'config_hash_changed')
    expected_inputs = {'config': config_sha256, 'paid_manifest': manifest_sha256,
                       'normalized_bundle': bundle_sha256,
                       'gold_raw_container': config['gold']['raw_sha256'],
                       'source_inputs': config['source_inputs']['raw_sha256']}
    if (score.get('schema') != config['output_schema'] or score.get('input_sha256') != expected_inputs
            or config.get('binding_status') != 'bound'
            or config['paid_manifest']['raw_sha256'] != manifest_sha256
            or score.get('gold_selection') != config['gold']
            or score.get('instrument') != config['instrument']
            or score.get('population') != config['population']):
        raise ValidationResultsError('score_input_binding_changed')
    if (score.get('first_response_only') is not True or score.get('replacement_responses_used') is not False
            or score.get('holdout_evaluation') is not False):
        raise ValidationResultsError('score_evidence_boundary_changed')
    for field in config['paid_manifest']['identity_fields']:
        if any(v.get(field) != config['paid_manifest'][field] for v in (manifest, bundle, score)):
            raise ValidationResultsError('programme_identity_changed')
    operations = _inventory(manifest.get('operations'), 'operation_inventory_invalid')
    mapping = _inventory(config.get('operation_mapping'), 'mapping_inventory_invalid')
    records = _inventory(score.get('records'), 'score_record_inventory_invalid')
    requests = _inventory(bundle.get('requests'), 'request_inventory_invalid')
    rows = _inventory(bundle.get('rows'), 'first_row_inventory_invalid')
    responses = _inventory(bundle.get('responses'), 'first_response_inventory_invalid')
    if (set(operations) != set(mapping) or set(records) != set(operations) or set(requests) != set(operations)
            or set(rows) - set(operations) or set(responses) - set(rows)
            or bundle.get('manifest_sha256') != manifest_sha256
            or bundle.get('planned_operation_ids') != list(operations)
            or bundle.get('planned_operations') != len(operations)
            or score.get('planned_operations') != len(operations)
            or bundle.get('saved_row_count') != len(rows)
            or score.get('received_first_rows') != len(rows)):
        raise ValidationResultsError('planned_population_binding_changed')
    arms = {a['arm_id']: a['authored_planned_queries'] for a in config['arms']}
    if Counter(m['arm_id'] for m in mapping.values()) != Counter(arms):
        raise ValidationResultsError('planned_arm_denominator_changed')
    targets = {}
    for arm, count in arms.items():
        reports = [g for g in score['groups'] if g['dimensions'] == {'arm_id': arm}]
        if len(reports) != 1 or reports[0]['quality']['availability']['query_count'] != count:
            raise ValidationResultsError('scorer_arm_denominator_changed')
        failures = {f['query_id']: f for f in reports[0]['failures']}
        for record in records.values():
            if record['arm_id'] != arm:
                continue
            query = record['query_id']; prediction = record['prediction']
            target = failures[query]['gold'] if query in failures else prediction.get('label')
            if target not in LABELS or query in targets and targets[query] != target:
                raise ValidationResultsError('scorer_gold_labels_inconsistent')
            targets[query] = target
    evaluated = []
    request_indexes = {r['operation_id']: i for i, r in enumerate(bundle['requests'])}
    response_indexes = {r['operation_id']: i for i, r in enumerate(bundle['responses'])}
    for ident, operation in operations.items():
        record, mapped, request = records[ident], mapping[ident], requests[ident]
        if any(record.get(k) != v for k, v in mapped.items()):
            raise ValidationResultsError('score_mapping_changed')
        for field, pointer in config['manifest_metadata_bindings'].items():
            if results.graph.pointer(operation, pointer) != mapped[field]:
                raise ValidationResultsError('manifest_mapping_changed')
        if (request.get('request_sha256') != operation['request_sha256']
                or results.sha(wire.canonical(request.get('body'))) != mapped.get('body_sha256', operation['request_sha256'])
                or any(request.get(k) != operation.get(k) for k in config['normalized_request_identity_fields'])):
            raise ValidationResultsError('public_request_binding_changed')
        row, response = rows.get(ident), responses.get(ident)
        projection = None if row is None else {k: row.get(k) for k in config['retained_first_row_fields']}
        if (record.get('first_row_projection') != projection
                or record.get('source_first_row_present') is not (row is not None)
                or record.get('source_first_capture_present') is not (response is not None)):
            raise ValidationResultsError('score_first_projection_changed')
        if response is not None and (response.get('response_sha256') != row.get('response_sha256')
                                      or response.get('generation_sha256') != row.get('generation_sha256')):
            raise ValidationResultsError('first_capture_binding_changed')
        cost = record.get('actual_cost_usd')
        if cost is not None:
            receipt = response.get('generation_projection', {}) if response else {}
            if (row is None or row.get('billing_verified') is not True or row.get('billing_replay_verified') is not True
                    or row.get('is_byok') is not False or receipt.get('is_byok') is not False
                    or Decimal(str(cost)) != Decimal(str(row.get('actual_cost_usd')))
                    or Decimal(str(cost)) != Decimal(str(receipt.get('total_cost')))):
                raise ValidationResultsError('score_verified_cost_changed')
        elif row is not None and row.get('actual_cost_usd') is not None:
            raise ValidationResultsError('unverified_normalized_cost_claim')
        prediction = record['prediction']; available = prediction.get('state') == 'completed' and prediction.get('label') in LABELS
        protocol = _protocol_valid(record, row, response, request['body']['questions'], config['response_policy'])
        if available and not protocol:
            raise ValidationResultsError('available_label_without_valid_protocol')
        correct = available and prediction['label'] == targets[record['query_id']]
        error = ('correct' if correct else 'source_label_mismatch' if available else
                 'conflicting_label' if prediction.get('label') == 'conflicting' else
                 'protocol_invalid' if record.get('label_error') == 'invalid_first_source_label_answer' else
                 record.get('label_error') or 'unavailable')
        evaluated.append({**deepcopy(record), 'gold_label': targets[record['query_id']],
                          'protocol_valid': protocol, 'label_available': available, 'correct': correct,
                          'error_class': error, 'public_evidence': {
                              'request': {'path': 'REQUESTS.json', 'pointer': '/rows/' + str(request_indexes[ident])},
                              'first_response': None if ident not in response_indexes else {
                                  'path': 'RESPONSES.json', 'pointer': '/rows/' + str(response_indexes[ident])}}})
    def cohort(selected):
        denominator = len(selected); correct = sum(r['correct'] for r in selected)
        available = sum(r['label_available'] for r in selected); protocol = sum(r['protocol_valid'] for r in selected)
        costs = [Decimal(r['actual_cost_usd']) for r in selected if r['actual_cost_usd'] is not None]
        known = sum(costs, Decimal(0)); unknown = denominator - len(costs)
        measured = any(r['source_first_row_present'] for r in selected)
        model_answers = any(r['source_first_capture_present'] and r['response_probabilities'] is not None for r in selected)
        return {'planned_queries': denominator, 'planned_families': len({r['family_id'] for r in selected}),
                'correct': correct, 'accuracy_all_planned': correct / denominator if measured else None,
                'available': available, 'unavailable': denominator - available, 'coverage': available / denominator,
                'protocol_valid': protocol, 'protocol_validity': protocol / denominator,
                'error_classes': dict(Counter(r['error_class'] for r in selected)),
                'known_verified_cost_usd': format(known, 'f'), 'unknown_cost_operations': unknown,
                'actual_total_cost_usd': format(known, 'f') if not unknown else None,
                'first_attempt_outcomes_measured': measured,
                'actual_model_answer_labels_measured': model_answers}
    reports = {}
    for arm in arms:
        selected = [r for r in evaluated if r['arm_id'] == arm]
        reports[arm] = {**cohort(selected), 'confusion': next(g['confusion'] for g in score['groups'] if g['dimensions'] == {'arm_id': arm}),
                        'by_language': {language: cohort([r for r in selected if r['language'] == language])
                                        for language in sorted({r['language'] for r in selected})}}
    paired = []
    arm_ids = list(arms)
    for baseline_index, baseline in enumerate(arm_ids):
        for candidate in arm_ids[baseline_index + 1:]:
            a = {r['query_id']: r for r in evaluated if r['arm_id'] == baseline}
            b = {r['query_id']: r for r in evaluated if r['arm_id'] == candidate}
            if set(a) != set(b):
                raise ValidationResultsError('paired_query_population_changed')
            pairs = []
            for query in a:
                left, right = a[query], b[query]
                label = lambda r: r['prediction'].get('label') if r['label_available'] else 'unavailable'
                pairs.append({'query_id': query, 'baseline_operation_id': left['operation_id'],
                              'candidate_operation_id': right['operation_id'], 'gold': left['gold_label'],
                              'baseline': label(left), 'candidate': label(right),
                              'disagreement': label(left) != label(right),
                              'correction': left['label_available'] and right['label_available'] and not left['correct'] and right['correct'],
                              'regression': left['label_available'] and right['label_available'] and left['correct'] and not right['correct'],
                              'availability_gained': not left['label_available'] and right['label_available'],
                              'availability_lost': left['label_available'] and not right['label_available']})
            paired.append({'baseline': baseline, 'candidate': candidate, 'planned_pairs': len(pairs),
                           'counts': {key: sum(p[key] for p in pairs) for key in
                                      ('disagreement', 'correction', 'regression', 'availability_gained', 'availability_lost')},
                           'pairs': pairs})
    summary = {'schema': 'loom.jev_validation_results/1', 'observed_on': observed_on,
               'input_sha256': {**expected_inputs, 'frozen_score': score_sha256}, 'population': deepcopy(score['population']),
               'reports': reports, 'paired_changes': paired, 'records': evaluated,
               'known_family_history': None if known_family_history is None else {
                   'supplied_data': deepcopy(known_family_history),
                   'supplied_data_sha256': results.sha(wire.canonical(known_family_history)),
                   'used_as_new_data': False},
               'new_model_calls': 0, 'heldout_evaluation': False, 'automatic_promotion': False,
               'evidence_boundary': score['evidence_boundary'],
               'cost_boundary': 'Generation-verified actual cost only; unknown or unattempted operations stay null. Overlapping language/family views are not added.'}
    return summary, bundle


def export(score_raw, bundle_raw, manifest_raw, config_raw, output, **bindings):
    summary, bundle = summarize(score_raw, bundle_raw, manifest_raw, config_raw, **bindings)
    by_operation = {r['operation_id']: r for r in summary['records']}
    def evaluator(planned, rows):
        selected = [by_operation[r['operation_id']] for r in rows]
        denominator = len(planned)
        correct = sum(r['correct'] for r in selected); available = sum(r['label_available'] for r in selected)
        protocol = sum(r['protocol_valid'] for r in selected)
        measured = any(r['source_first_row_present'] for r in selected)
        note = ('Primary frozen source-commitment success over all planned authored new DATA queries, including failed first attempts. '
                'A measured zero may be entirely unavailable attempts and does not establish intrinsic semantic quality. '
                'An entirely unattempted cohort remains null. No world truth, sealed holdout or automatic promotion.')
        metrics = {'accuracy_all_queries': results.shared_metric(correct / denominator if measured else None,
                    numerator=correct, denominator=denominator, unit='fraction', note=note),
                   'semantic_coverage': results.shared_metric(available / denominator, numerator=available,
                    denominator=denominator, unit='fraction', note='Valid ternary labels; conflicting probabilities are unavailable.'),
                   'protocol_validity': results.shared_metric(protocol / denominator, numerator=protocol,
                    denominator=denominator, unit='fraction', note='Valid two-probability first answers; conflicting labels remain protocol valid.')}
        for language in sorted({by_operation[r['operation_id']]['language'] for r in planned}):
            language_denominator = sum(by_operation[r['operation_id']]['language'] == language for r in planned)
            language_rows = [r for r in selected if r['language'] == language]
            correct = sum(r['correct'] for r in language_rows)
            language_measured = any(r['source_first_row_present'] for r in language_rows)
            metrics['accuracy_' + language] = results.shared_metric(correct / language_denominator if language_measured else None,
                numerator=correct, denominator=language_denominator, unit='fraction', note=note)
        return metrics
    copied = deepcopy(bundle); copied['scoring'] = {'frozen_score': wire.parse_json(score_raw), 'validation': summary}
    receipt = results.export_results(copied, output, observed_on=bindings['observed_on'], evaluator=evaluator)
    output = Path(output)
    for name, raw in (('FROZEN_SCORE_INPUT.json', score_raw), ('FROZEN_NORMALIZED_INPUT.json', bundle_raw),
                      ('FROZEN_MANIFEST_INPUT.json', manifest_raw), ('FROZEN_CONFIGURATION_INPUT.json', config_raw)):
        with (output / name).open('xb') as file:
            file.write(raw)
    results.write(output / 'VALIDATION.json', summary)
    # The reusable programme exporter treats unfamiliar metrics as resources.
    # Declare the new protocol metric's mechanism axis in its existing DATA
    # configuration, and rebuild the graph without changing frozen scoring.
    graph_config = wire.parse_json((output / 'configuration.json').read_bytes())
    for run in graph_config['runs']:
        run['population']['dependent_observations'] = (
            'New authored source families shared across matched recipes; query rows are family-dependent. '
            'First evaluation of this population; author-visible, not external blind holdout.')
        for metric in run['metrics']:
            if metric['id'] == 'protocol_validity':
                metric['axis'] = 'mechanism'
    (output / 'configuration.json').write_bytes(results.graph.model_profiles.encoded(graph_config))
    packet = results.graph.export(graph_config, root=results.graph.ROOT)
    packet_raw = results.graph.codec.encode_packet(packet)
    with (output / 'packet.json.gz').open('wb') as file:
        with gzip.GzipFile(filename='', fileobj=file, mode='wb', mtime=0) as zipped:
            zipped.write(packet_raw)
    receipt.update(packet_id=packet['packet_id'], packet_sha256=results.sha(packet_raw),
                   entities=len(packet['entities']), claims=len(packet['claims']), sources=len(packet['sources']),
                   validation_adapter_sha256=results.sha(Path(__file__).read_bytes()),
                   frozen_input_sha256={key: bindings[key] for key in
                                        ('score_sha256', 'bundle_sha256', 'manifest_sha256', 'config_sha256')},
                   output_sha256={p.name: results.sha(p.read_bytes()) for p in sorted(output.iterdir())
                                  if p.is_file() and p.name != 'VERIFICATION.json'})
    (output / 'VERIFICATION.json').write_bytes(results.graph.model_profiles.encoded(receipt))
    return receipt, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('score', 'bundle', 'manifest', 'config', 'output', 'observed-on',
                 'score-sha256', 'bundle-sha256', 'manifest-sha256', 'config-sha256'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--history')
    parser.add_argument('--history-sha256')
    args = parser.parse_args()
    history = None
    if args.history:
        history = _bound(Path(args.history).read_bytes(), args.history_sha256, 'historical_context_hash_changed')
    receipt, _ = export(*(Path(getattr(args, name)).read_bytes() for name in ('score', 'bundle', 'manifest', 'config')),
                        args.output, observed_on=args.observed_on, score_sha256=args.score_sha256,
                        bundle_sha256=args.bundle_sha256, manifest_sha256=args.manifest_sha256,
                        config_sha256=args.config_sha256, known_family_history=history)
    print(wire.canonical({'packet_id': receipt['packet_id'], 'new_model_calls': 0}).decode())


if __name__ == '__main__':
    main()
