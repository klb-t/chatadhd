#!/usr/bin/env python3
"""Offline analysis of frozen Jev pairs; never loads a key or calls a model."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime
from decimal import Decimal
import hashlib
import math
from pathlib import Path
import statistics

try:
    from . import jev_live_pilot as pilot
except ImportError:
    import jev_live_pilot as pilot


def summary(rows):
    """Missing answers remain missing, including when the gold answer is false."""
    result = pilot.metrics(rows)
    available = [r for r in rows if r['probability'] is not None]
    counts = Counter(('tp' if r['label'] else 'fp') if r['probability'] >= .5
                     else ('fn' if r['label'] else 'tn') for r in available)
    result['confusion'] = {k: counts[k] for k in ('tp', 'tn', 'fp', 'fn')}
    result['correct'] = counts['tp'] + counts['tn']
    result['positive_labels_planned'] = sum(r['label'] for r in rows)
    result['negative_labels_planned'] = len(rows) - result['positive_labels_planned']
    specificity = counts['tn'] / (counts['tn'] + counts['fp']) if counts['tn'] + counts['fp'] else None
    result['specificity_available'] = specificity
    recall = result['recall_available']
    result['balanced_accuracy_available'] = (recall + specificity)/2 if recall is not None and specificity is not None else None
    result['probability_min'] = min((r['probability'] for r in available), default=None)
    result['probability_max'] = max((r['probability'] for r in available), default=None)
    return result


def selective(rows, low, high):
    if not 0 <= low < .5 < high <= 1:
        raise ValueError('invalid_selective_interval')
    selected = [r for r in rows if r['probability'] is not None and
                (r['probability'] <= low or r['probability'] >= high)]
    result = summary(selected)
    # Generic metrics carry a nested, fixed .2/.8 summary. It would be misleading
    # in another threshold arm, so retain only the arm's own selection metrics.
    result.pop('selective_coverage_planned')
    result.pop('selective_error')
    result.update(thresholds=[low, high], selected=len(selected),
                  original_planned=len(rows),
                  coverage_original_planned=len(selected)/len(rows) if rows else None,
                  errors=len(selected)-result['correct'],
                  error_fraction=(len(selected)-result['correct'])/len(selected) if selected else None,
                  unselected=len(rows)-len(selected))
    return result


def analyze(run_dir, fixture_dir):
    directory, fixture = Path(run_dir), Path(fixture_dir)
    frozen = pilot.read_json(fixture/'manifest.json')
    for name, expected in frozen['files_sha256'].items():
        if name not in ('README.md', 'gold.json', 'inputs.json'):
            raise ValueError('unexpected_fixture_file')
        if hashlib.sha256((fixture/name).read_bytes()).hexdigest() != expected:
            raise ValueError('fixture_hash_mismatch')
    manifest = pilot.read_json(directory/'manifest.json')
    inputs = pilot.read_json(fixture/'inputs.json')
    gold = pilot.read_json(fixture/'gold.json')
    if pilot.safe.digest(inputs) != manifest['inputs_hash']:
        raise ValueError('frozen_input_mismatch')
    if any(g['variant'] != 'base' or g['base_case_id'] != g['case_id'] or
           set(g['labels']) != {'q01'} or g['contrast_kind'] not in
           ('paraphrase', 'domain_transfer', 'foil') for g in gold):
        raise ValueError('pair_gold_contract_invalid')
    core = pilot.score(manifest, directory, gold)
    if core['comparisons']:
        raise ValueError('unexpected_legacy_comparisons')
    ledger = pilot.read_json(directory/'ledger.json')
    attempts = pilot.validate_ledger(ledger, manifest, directory)
    terminal = bool(ledger.get('stopped_reason')) or (
        len(attempts) == len(manifest['requests']) and
        all(a['state'] != 'started' for a in attempts) and 'cost_accounting_complete' in ledger)
    if not terminal:
        raise ValueError('run_not_terminal')
    targets = {g['case_id']: g for g in gold}
    states = {x['case_id']: pilot.safe.parse_json(x['state']['text'].encode()) for x in inputs}
    rows = [{**r, 'contrast_kind': targets[r['case_id']]['contrast_kind'],
             'source_case_ids': targets[r['case_id']]['source_case_ids']} for r in core['rows']]
    groups = {}
    for field in ('contrast_kind', 'language', 'family_id', 'split'):
        parts = defaultdict(list)
        for row in rows:
            parts[row[field]].append(row)
        groups[field] = {name: summary(items) for name, items in sorted(parts.items())}
    family_contrast = defaultdict(list)
    bilingual = defaultdict(list)
    for row in rows:
        family_contrast[row['family_id']+'/'+row['contrast_kind']].append(row)
        bilingual[(row['family_id'],row['contrast_kind'])].append(row)
    groups['family_contrast'] = {name: summary(items) for name, items in sorted(family_contrast.items())}
    bilingual_rows = []
    for (family, contrast), items in sorted(bilingual.items()):
        if len(items) != 2 or {x['language'] for x in items} != {'pl','en'}:
            raise ValueError('bilingual_inventory_invalid')
        available = all(x['probability'] is not None for x in items)
        bilingual_rows.append({'family_id':family, 'contrast_kind':contrast, 'available':available,
            'same_decision': len({x['probability'] >= .5 for x in items}) == 1 if available else None,
            'both_correct': all((x['probability'] >= .5) == x['label'] for x in items) if available else None})
    errors = [{**r, 'prediction': int(r['probability'] >= .5), **states[r['case_id']]}
              for r in rows if r['probability'] is not None and (r['probability'] >= .5) != r['label']]
    requests = {r['id']:r for r in manifest['requests']}
    parsed = []
    for attempt in attempts:
        if attempt['state'] == 'completed':
            raw = (directory/attempt['response_file']).read_bytes()
            item = pilot.parse_response(raw, requests[attempt['id']], manifest['model_aliases'])
            if any(attempt.get(k) != v for k,v in item.items()):
                raise ValueError('ledger_parsed_response_mismatch')
            parsed.append(item)
    raw_cost = sum((Decimal(r['reported_cost_usd']) for r in parsed), Decimal(0))
    if all(a['state'] == 'completed' for a in attempts) and ledger.get('reported_cost_usd') is not None and raw_cost != Decimal(ledger['reported_cost_usd']):
        raise ValueError('ledger_cost_total_mismatch')
    latencies = sorted(a['elapsed_seconds'] for a in attempts if 'elapsed_seconds' in a)
    if any(not math.isfinite(x) or x < 0 for x in latencies):
        raise ValueError('invalid_latency')
    audit = {'attempts':len(attempts), 'planned':len(manifest['requests']),
        'attempt_states':dict(Counter(a['state'] for a in attempts)),
        'stopped_reason':ledger.get('stopped_reason'),
        'post_key_check_failed':ledger.get('post_key_check_failed', False),
        'post_key_snapshot_present':'key_after' in ledger,
        'models':dict(Counter(p['model'] for p in parsed)),
        'providers':dict(Counter(p['provider'] for p in parsed)),
        'input_tokens':sum(p['input_tokens'] for p in parsed),
        'output_tokens':sum(p['output_tokens'] for p in parsed),
        'completed_response_cost_usd':str(raw_cost),
        'all_reported_cost_usd':ledger.get('reported_cost_usd'),
        'cost_accounting_complete':ledger.get('cost_accounting_complete', False),
        'attempts_with_unknown_cost':sum('reported_cost_usd' not in a for a in attempts),
        'generation_audits':dict(Counter(a.get('generation_billing',{}).get('audit_status','not_available') for a in attempts)),
        'latency_seconds':{'count':len(latencies),
            'median':statistics.median(latencies) if latencies else None,
            'p95_nearest_rank':latencies[math.ceil(.95*len(latencies))-1] if latencies else None,
            'maximum':max(latencies, default=None)},
        'response_hashes':{a['id']:a['response_sha256'] for a in attempts if 'response_sha256' in a},
        'no_retries':len({a['id'] for a in attempts}) == len(attempts)}
    if attempts and all('finished_at' in a for a in attempts):
        audit['attempt_window_seconds_including_auxiliary_audits'] = (
            datetime.fromisoformat(attempts[-1]['finished_at'].replace('Z','+00:00')) -
            datetime.fromisoformat(attempts[0]['started_at'].replace('Z','+00:00'))).total_seconds()
    return {'schema':'loom.jev_pair_analysis/1', 'experiment_id':manifest['experiment_id'],
        'manifest_hash':pilot.safe.digest(manifest), 'gold_hash':pilot.safe.digest(gold),
        'fixture_manifest_sha256':hashlib.sha256((fixture/'manifest.json').read_bytes()).hexdigest(),
        'evaluation_status':'exploratory_posthoc_reuse_not_independent_holdout',
        'threshold':.5, 'overall':summary(rows), 'groups':groups, 'errors':errors,
        'selective':[{'status':'predeclared' if (a,b)==(.2,.8) else 'descriptive_not_tuned',
                      **selective(rows,a,b)} for a,b in ((.1,.9),(.2,.8),(.3,.7))],
        'bilingual':{'available_pairs':sum(x['available'] for x in bilingual_rows),
            'same_decision':sum(x['same_decision'] is True for x in bilingual_rows),
            'both_correct':sum(x['both_correct'] is True for x in bilingual_rows),'rows':bilingual_rows},
        'all_six_correct_families':sum(v['available']==6 and v['correct']==6 for v in groups['family_id'].values()),
        'rows':rows,'audit':audit, 'no_graph_promotion':True,
        'interpretation':{'no_legacy_invariance_comparison':True,
            'analogy_projection':'R(a,b) mapped to R(c,d); ignores internal fan-in/fan-out topology',
            'sources_reused_after_inspection':True,'orientation_fixed_base_left':True,
            'not_free_form_structure_discovery':True}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir',required=True)
    parser.add_argument('--fixture-dir',required=True)
    parser.add_argument('--output',required=True)
    args = parser.parse_args(argv)
    value = analyze(args.run_dir,args.fixture_dir)
    pilot.write_new(args.output,value)
    print(f"Scored {value['overall']['available']}/{value['overall']['planned']}; errors={len(value['errors'])}")


if __name__ == '__main__':
    main()
