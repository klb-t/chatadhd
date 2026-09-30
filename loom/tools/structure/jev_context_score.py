#!/usr/bin/env python3
"""Offline scoring of frozen Jev context first responses; development labels only."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

try:
    from . import jev_context_pilot as context
except ImportError:
    import jev_context_pilot as context

jev = context.jev
KINDS = ('membership', 'selected_claim')
VERSION = 'loom.jev_context_score/1'
# Existing authored corpus freeze, pinned before live inference. Hashing raw bytes
# does not decode or interpret the validation-label rows in the shared JSONL.
EXPECTED_GOLD_SHA256 = '2682a8e23346ae32ba34d69a74b7c350b89aceb743226c42e76186ecc4cdabf5'


def ratio(a, b):
    return a / b if b else None


def counts(rows):
    available = [r for r in rows if r['probability'] is not None]
    tp = sum(r['probability'] >= .5 and r['label'] == 1 for r in available)
    fp = sum(r['probability'] >= .5 and r['label'] == 0 for r in available)
    tn = sum(r['probability'] < .5 and r['label'] == 0 for r in available)
    fn = sum(r['label'] == 1 and (r['probability'] is None or r['probability'] < .5) for r in rows)
    p, r = ratio(tp, tp + fp), ratio(tp, tp + fn)
    f1 = None if p is None or r is None else (2 * p * r / (p + r) if p + r else 0.)
    return {'tp': tp, 'fp': fp, 'tn': tn, 'fn': fn, 'precision': p, 'recall': r, 'f1': f1,
            'unavailable_positive': sum(x['label'] == 1 and x['probability'] is None for x in rows),
            'unavailable_negative': sum(x['label'] == 0 and x['probability'] is None for x in rows)}


def bit_metrics(rows):
    available = [r for r in rows if r['probability'] is not None]
    selective = [r for r in available if r['probability'] <= .2 or r['probability'] >= .8]
    all_counts, valid_counts = counts(rows), counts(available)
    correct = valid_counts['tp'] + valid_counts['tn']
    negatives = sum(r['label'] == 0 for r in rows)
    return {'planned': len(rows), 'available': len(available), 'missing': len(rows) - len(available),
            'coverage': ratio(len(available), len(rows)), 'negative_count': negatives,
            'negative_prevalence': ratio(negatives, len(rows)),
            'always_false_accuracy': ratio(negatives, len(rows)),
            'all_query_counts': all_counts, 'valid_output_counts': valid_counts,
            'accuracy_all_queries': ratio(correct, len(rows)),
            'accuracy_valid_outputs': ratio(correct, len(available)),
            'brier_valid_outputs': ratio(sum((r['probability'] - r['label']) ** 2 for r in available), len(available)),
            'selective': {'thresholds': [.2, .8], 'retained': len(selective),
                          'coverage_planned': ratio(len(selective), len(rows)),
                          'coverage_available': ratio(len(selective), len(available)),
                          'errors': sum((r['probability'] >= .5) != r['label'] for r in selective),
                          'error_rate': ratio(sum((r['probability'] >= .5) != r['label'] for r in selective), len(selective)),
                          'counts': counts(selective)}}


def exact_metrics(queries, kind=None):
    valid = [q for q in queries if q['valid']]
    def correct(q):
        return q['valid'] and (q['sets'][kind]['exact'] if kind else all(q['sets'][k]['exact'] for k in KINDS))
    n = sum(correct(q) for q in queries)
    return {'planned': len(queries), 'valid': len(valid), 'correct': n,
            'accuracy_all_queries': ratio(n, len(queries)),
            'accuracy_valid_outputs': ratio(n, len(valid))}


def summary(queries, rows):
    with_claims = [q for q in queries if q['sets']['selected_claim']['candidate_ids']]
    return {'queries': len(queries), 'valid_queries': sum(q['valid'] for q in queries),
            'query_coverage': ratio(sum(q['valid'] for q in queries), len(queries)),
            'bits': {kind: bit_metrics([r for r in rows if r['kind'] == kind]) for kind in KINDS},
            'exact_sets': {kind: exact_metrics(queries, kind) for kind in KINDS},
            'joint_exact_set': exact_metrics(queries),
            'claim_candidate_subset': exact_metrics(with_claims, 'selected_claim'),
            'no_claim_candidates': len(queries) - len(with_claims)}


def development_gold(fixture, expected_case_ids=None):
    """Discard non-development rows before decoding/parsing their label bodies.

    The shared JSONL's leading case_id must be explicit. Only selected rows are
    parsed; validation bodies are never interpreted, returned or printed.
    """
    fixture = Path(fixture)
    split = jev.read_json(fixture / 'split.json')
    assignments = {r['case_id']: r for r in split['cases'] if r['split'] == 'development'}
    if expected_case_ids is not None and set(assignments) != set(expected_case_ids):
        raise ValueError('development_split_inventory_changed_before_label_read')
    result = {}
    with (fixture / 'gold.jsonl').open('rb') as handle:
        for line in handle:
            match = re.match(rb'\s*\{\s*"case_id"\s*:\s*"([A-Za-z0-9_-]+)"\s*,', line)
            if not match:
                raise ValueError('gold_requires_leading_case_id_for_split_isolation')
            identifier = match.group(1).decode('ascii')
            if identifier not in assignments:
                continue
            if identifier in result:
                raise ValueError('duplicate_development_gold_case')
            row = jev.safe.parse_json(line)
            if row['case_id'] != identifier:
                raise ValueError('gold_identity_mismatch')
            labels = {label['message_id']: label for label in row['labels']}
            if len(labels) != len(row['labels']):
                raise ValueError('duplicate_development_message')
            result[identifier] = {'labels': labels, 'family': assignments[identifier]['family'],
                                  'language': assignments[identifier]['language']}
    if set(result) != set(assignments):
        raise ValueError('development_gold_inventory_mismatch')
    return result


def verified_bundle(prepared, manifest):
    directory = Path(prepared)
    offline = jev.read_json(directory / 'offline_manifest.json')
    inputs = jev.read_json(directory / 'inputs.json')
    mapping = jev.read_json(directory / 'question_map.json')
    exports = jev.read_json(directory / 'causal_exports.json')
    if (offline.get('schema') != 'loom.jev_context_offline_manifest/1' or
            offline.get('projection') != context.VERSION or offline.get('split') != 'development' or
            offline.get('requests') != 48 or len(inputs) != 48 or len(mapping) != 48 or len(exports) != 48):
        raise ValueError('offline_inventory_invalid')
    for field, value in (('inputs_sha256', inputs), ('question_map_sha256', mapping), ('causal_exports_sha256', exports)):
        if offline.get(field) != context.digest(value):
            raise ValueError('offline_artifact_hash_mismatch')
    expected_prompt = context.digest({'instruction': context.INSTRUCTION, 'topic': context.TOPIC, 'claim': context.CLAIM})
    if offline.get('prompt_sha256') != expected_prompt:
        raise ValueError('offline_prompt_hash_mismatch')
    for name, digest in offline['code_sha256'].items():
        if '/' in name or Path(name).name != name:
            raise ValueError('invalid_code_hash_path')
        if hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() != digest:
            raise ValueError('offline_code_changed')
    jev.validate_manifest(manifest)
    if (len(manifest['requests']) != 48 or manifest['inputs_hash'] != context.digest(inputs)):
        raise ValueError('live_manifest_input_mismatch')
    ids = set()
    for source, row, audit, request in zip(exports, inputs, mapping, manifest['requests']):
        rebuilt_row, rebuilt_audit = context.from_export(source)
        if rebuilt_row != row or rebuilt_audit != audit or row['case_id'] in ids:
            raise ValueError('projection_or_map_mismatch')
        ids.add(row['case_id'])
        body = {'model': jev.MODEL, 'provider': context.PROVIDER, 'state': row['state'], 'questions': row['questions']}
        if (request['id'] != row['case_id'] or request['language'] != row['language'] or
                request['body'] != body or request['request_hash'] != audit['body_sha256']):
            raise ValueError('live_request_mapping_mismatch')
    return offline, mapping


def evaluate(mapping, gold, results):
    """Pure semantic scoring; results already integrity-checked by score()."""
    rows, queries = [], []
    for audit in mapping:
        case = gold[audit['case_id']]
        label = case['labels'][audit['message_id']]
        expected = {'membership': {x['topic_id'] for x in label['memberships']},
                    'selected_claim': set(label['selected_claim_ids'])}
        outcome = results.get(audit['request_id'], {'valid': False, 'status': 'not_attempted', 'probabilities': {}})
        probabilities = outcome['probabilities'] if outcome['valid'] else {}
        if outcome['valid'] and (set(probabilities) != set(audit['questions']) or any(
                type(p) not in (int, float) or not 0 <= p <= 1 for p in probabilities.values())):
            raise ValueError('invalid_verified_probabilities')
        query = {k: audit[k] for k in ('request_id', 'case_id', 'message_id', 'language')}
        query.update(family=case['family'], valid=outcome['valid'], status=outcome['status'],
                     response_sha256=outcome.get('response_sha256'), error=outcome.get('error'), sets={})
        if case['language'] != audit['language']:
            raise ValueError('label_language_mismatch')
        for kind in KINDS:
            candidates = {q['id'] for q in audit['questions'].values() if q['kind'] == kind}
            if not expected[kind] <= candidates:
                raise ValueError('gold_outside_candidate_universe')
            predicted = {q['id'] for name, q in audit['questions'].items()
                         if q['kind'] == kind and probabilities.get(name, -1) >= .5}
            query['sets'][kind] = {
                'candidate_ids': sorted(candidates), 'expected_ids': sorted(expected[kind]),
                'predicted_ids': sorted(predicted) if outcome['valid'] else None,
                'missing_ids': sorted(expected[kind] - predicted),
                'extra_ids': sorted(predicted - expected[kind]),
                'exact': outcome['valid'] and predicted == expected[kind]}
        queries.append(query)
        for name, question in audit['questions'].items():
            probability = probabilities.get(name)
            target = int(question['id'] in expected[question['kind']])
            decision = None if probability is None else int(probability >= .5)
            rows.append({'request_id': audit['request_id'], 'case_id': audit['case_id'],
                         'message_id': audit['message_id'], 'language': audit['language'],
                         'family': case['family'], 'question': name, **question, 'label': target,
                         'probability': probability, 'decision': decision,
                         'error': ('missing_positive' if target else 'missing_negative') if decision is None
                                  else ('false_positive' if decision else 'false_negative') if decision != target else None})
    groups = {}
    for field in ('language', 'family'):
        groups[field] = {}
        for group in sorted({q[field] for q in queries}):
            selected = [q for q in queries if q[field] == group]
            selected_rows = [r for r in rows if r[field] == group]
            groups[field][group] = summary(selected, selected_rows)
    return {'overall': summary(queries, rows), 'groups': groups, 'queries': queries, 'rows': rows,
            'errors': [r for r in rows if r['error'] is not None]}


def score(manifest, run_dir, prepared, fixture=context.FIXTURE):
    offline, mapping = verified_bundle(prepared, manifest)
    if set(offline['source_files_sha256']) != {'inputs.jsonl', 'split.json', 'protocol.json', 'materialize.py'}:
        raise ValueError('source_hash_inventory_invalid')
    for name, digest in offline['source_files_sha256'].items():
        if hashlib.sha256((Path(fixture) / name).read_bytes()).hexdigest() != digest:
            raise ValueError('source_fixture_changed_before_label_read')
    if hashlib.sha256((Path(fixture) / 'gold.jsonl').read_bytes()).hexdigest() != EXPECTED_GOLD_SHA256:
        raise ValueError('gold_fixture_changed_before_label_read')
    directory = Path(run_dir)
    ledger = jev.read_json(directory / 'ledger.json')
    attempts = jev.validate_ledger(ledger, manifest, directory)
    results = {}
    for request, attempt in zip(manifest['requests'], attempts):
        result = {'valid': False, 'status': attempt['state'], 'probabilities': {},
                  'response_sha256': attempt.get('response_sha256')}
        if attempt['state'] == 'completed':
            if 'response_file' not in attempt or 'response_sha256' not in attempt:
                result['error'] = 'completed_response_missing'
            else:
                try:
                    raw = (directory / attempt['response_file']).read_bytes()
                    if hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
                        raise ValueError('response_changed_after_ledger_verification')
                    parsed = jev.parse_response(raw, request, manifest['model_aliases'])
                    result.update(valid=True, probabilities=parsed['probabilities'])
                except jev.safe.RunnerError as error:
                    result.update(status='invalid_response', error=str(error))
        results[request['id']] = result
    gold = development_gold(fixture, {m['case_id'] for m in mapping})
    if {m['case_id'] for m in mapping} != set(gold):
        raise ValueError('development_map_inventory_mismatch')
    scored = evaluate(mapping, gold, results)
    return {'schema': VERSION, 'manifest_hash': context.digest(manifest),
            'offline_manifest_hash': context.digest(offline), 'ledger_hash': context.digest(ledger),
            'development_labels_hash': context.digest(gold), 'threshold': .5,
            'pinned_gold_file_sha256': EXPECTED_GOLD_SHA256,
            'score_code_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'reported_cost_usd': ledger.get('reported_cost_usd'),
            'split': 'development', 'no_graph_promotion': True,
            'synthetic_supplied_candidates_not_archive_retrieval_accuracy': True, **scored}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = score(jev.read_json(args.manifest), args.run_dir, args.prepared)
    jev.write_new(args.output, result)
    print(json.dumps(result['overall'], indent=2))


if __name__ == '__main__':
    main()
