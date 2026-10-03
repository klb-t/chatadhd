#!/usr/bin/env python3
"""Read-only frozen T3 first32 scorer; no credentials, requests or holdouts."""
from __future__ import annotations
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import random
try:
    from . import recipe_live_pilot as runner
except ImportError:
    import recipe_live_pilot as runner

GOLD_SHA256 = '00b9ee9264c18b20516d180767835e77eb2a6a81932f594aedf411fd8962726b'
BOOTSTRAPS = 2000
SEED = 29092026


def ratio(n, d):
    return n / d if d else None


def metrics(rows):
    observed = [r for r in rows if r['probability'] is not None]
    missing = [r for r in rows if r['probability'] is None]
    positives = sum(r['label'] for r in rows)
    tp = sum(r['label'] == 1 and r['probability'] >= .5 for r in observed)
    tn = sum(r['label'] == 0 and r['probability'] < .5 for r in observed)
    fp = sum(r['label'] == 0 and r['probability'] >= .5 for r in observed)
    fn = sum(r['label'] == 1 and r['probability'] < .5 for r in observed)
    selective = [r for r in observed if r['probability'] <= .2 or r['probability'] >= .8]
    bins = []
    for i in range(5):
        subset = [r for r in observed if min(int(r['probability'] * 5), 4) == i]
        bins.append({'index': i, 'count': len(subset),
                     'mean_probability': ratio(sum(r['probability'] for r in subset), len(subset)),
                     'positive_fraction': ratio(sum(r['label'] for r in subset), len(subset))})
    logloss = 0
    for r in observed:
        q = min(1 - 1e-6, max(1e-6, r['probability']))
        logloss -= r['label'] * math.log(q) + (1 - r['label']) * math.log(1 - q)
    return {'planned': len(rows), 'observed': len(observed), 'missing': len(missing),
            'planned_positive': positives, 'planned_negative': len(rows) - positives,
            'missing_positive': sum(r['label'] for r in missing),
            'missing_negative': sum(1 - r['label'] for r in missing),
            'tp': tp, 'tn': tn, 'fp': fp, 'fn': fn,
            'predicted_positive_observed': tp + fp, 'gold_positive_observed': tp + fn,
            'precision_observed': ratio(tp, tp + fp), 'recall_observed': ratio(tp, tp + fn),
            'recall_planned': ratio(tp, positives), 'coverage': ratio(len(observed), len(rows)),
            'accuracy_observed': ratio(tp + tn, len(observed)), 'accuracy_planned': ratio(tp + tn, len(rows)),
            'brier_observed': ratio(sum((r['probability'] - r['label']) ** 2 for r in observed), len(observed)),
            'logloss_observed': ratio(logloss, len(observed)),
            'ece_observed': ratio(sum(b['count'] * abs(b['mean_probability'] - b['positive_fraction'])
                                     for b in bins if b['count']), len(observed)), 'ece_bins': bins,
            'selective_observed': len(selective), 'selective_coverage_planned': ratio(len(selective), len(rows)),
            'selective_error': ratio(sum((r['probability'] >= .5) != r['label'] for r in selective), len(selective)),
            'baselines_planned': {'all_negative_correct': len(rows) - positives, 'all_positive_correct': positives,
                                  'all_negative_accuracy': ratio(len(rows) - positives, len(rows)),
                                  'all_positive_accuracy': ratio(positives, len(rows))}}


def percentile(values, q):
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * q
    lower, upper = int(position), math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def paired(rows):
    pairs = defaultdict(dict)
    for row in rows:
        pairs[row['case_id']][row['arm']] = row
    complete, unresolved = [], []
    for case, arms in sorted(pairs.items()):
        if (set(arms) != set(runner.ARMS) or
                any(r['probability'] is None for r in arms.values())):
            unresolved.append(case)
            continue
        a, b = arms['object_meaningful'], arms['string']
        if a['label'] != b['label']:
            raise runner.safe.RunnerError('recipe_paired_gold_mismatch')
        label = a['label']
        complete.append({'case_id': case,
                         'accuracy_delta': int((a['probability'] >= .5) == label) - int((b['probability'] >= .5) == label),
                         'brier_delta': (a['probability'] - label) ** 2 - (b['probability'] - label) ** 2})
    output = {'planned_pairs': len(pairs), 'complete_pairs': len(complete), 'unresolved_case_ids': unresolved,
              'direction': 'object_meaningful_minus_string', 'seed': SEED, 'bootstrap_replicates': BOOTSTRAPS}
    rng = random.Random(SEED)
    distributions = {'accuracy_delta': [], 'brier_delta': []}
    if complete:
        for _ in range(BOOTSTRAPS):
            sampled = [rng.choice(complete) for _ in complete]
            for field, values in distributions.items():
                values.append(sum(r[field] for r in sampled) / len(sampled))
    for field, values in distributions.items():
        output[field] = {'mean': ratio(sum(r[field] for r in complete), len(complete)),
                         'percentile_95_interval': [percentile(values, .025), percentile(values, .975)]}
    return output


def score(manifest, run_dir):
    runner.validate_manifest(manifest)
    directory = Path(run_dir)
    ledger = runner.pilot.read_json(directory / 'ledger.json')
    runner.validate_ledger(ledger, manifest, directory)
    gold_path = runner.FIXTURE / 'gold.json'
    if hashlib.sha256(gold_path.read_bytes()).hexdigest() != GOLD_SHA256:
        raise runner.safe.RunnerError('recipe_original_gold_hash_mismatch')
    gold = runner.pilot.read_json(gold_path)
    if gold.get('schema') != 'loom.jev_recipes_gold/1':
        raise runner.safe.RunnerError('recipe_gold_schema_invalid')
    attempts = {r['id']: r for r in ledger['attempts']}
    rows = []
    for request in manifest['requests']:
        attempt = attempts.get(request['id'], {})
        probabilities = {}
        if attempt.get('state') == 'completed':
            raw = (directory / attempt['response_file']).read_bytes()
            probabilities = runner.pilot.parse_response(raw, request, manifest['model_aliases'])['probabilities']
        labels = gold['cases'][request['case_id']]
        for question in ('expressed', 'inferred'):
            if type(labels[question]) is not int or labels[question] not in (0, 1):
                raise runner.safe.RunnerError('recipe_gold_label_invalid')
            rows.append({'request_id': request['id'], 'case_id': request['case_id'], 'arm': request['arm'],
                         'question': question, 'language': request['language'],
                         'split': 'development' if int(request['case_id'][1:]) <= 8 else 'validation',
                         'label': labels[question], 'probability': probabilities.get(question),
                         'attempt_state': attempt.get('state', 'not_attempted')})
    groups = {}
    for dimension in ('question', 'arm', 'language', 'split', 'question_arm'):
        buckets = defaultdict(list)
        for row in rows:
            key = row[dimension] if dimension != 'question_arm' else row['question'] + '/' + row['arm']
            buckets[key].append(row)
        groups[dimension] = {key: metrics(values) for key, values in sorted(buckets.items())}
    return {'schema': 'loom.jev_recipes_first32_score/1', 'manifest_hash': runner.safe.digest(manifest),
            'gold_sha256': GOLD_SHA256, 'scorer_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'status': {'planned_calls': 32, 'attempted_calls': len(attempts),
                       'completed_calls': sum(a['state'] == 'completed' for a in attempts.values()),
                       'stopped_reason': ledger.get('stopped_reason'),
                       'reported_cost_usd': ledger.get('reported_cost_usd'),
                       'attempts_with_unknown_cost': ledger.get('attempts_with_unknown_cost')},
            'evaluation_scope': 'known synthetic development; authored validation is not blind holdout',
            'overall': metrics(rows), 'groups': groups,
            'paired': {q: paired([r for r in rows if r['question'] == q]) for q in ('expressed', 'inferred')},
            'rows': rows, 'no_graph_promotion': True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = score(runner.pilot.read_json(args.manifest), args.run_dir)
        runner.write_new(args.output, result)
        print(json.dumps({'observed': result['overall']['observed'], 'planned': result['overall']['planned'],
                          'stopped_reason': result['status']['stopped_reason']}))
        return 0
    except (runner.safe.RunnerError, OSError, ValueError, KeyError, TypeError):
        print(json.dumps({'ok': False, 'error': 'recipe_score_rejected'}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
