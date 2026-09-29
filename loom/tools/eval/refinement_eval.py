"""T2 offline reference scorer. No consolidator or semantic judge is invoked.

Exact whole-clause matching or explicitly reviewed one-to-one alignment is used.
Unfamiliar paraphrases require review; they are not called hallucinations. Gold
self-comparison/mutations test this evaluator, not a model's consolidation quality.
"""
from __future__ import annotations
import argparse
from collections import Counter
from dataclasses import asdict
import json
from pathlib import Path
import sys
import unicodedata

sys.path.insert(0, str(Path(__file__).resolve().parent))
from refinement_fixture import (ACTS, load_cases, materialize_case, render_statements,
                                verify_manifest)
from validate import ContractValidator, read_json

ACTIVE = {'active', 'contested'}
NON_DIRECTIVE = {'question', 'dissatisfaction_without_delta'}


def normalized(text: str) -> str:
    # Do not strip negation, punctuation, case, identifiers or numeric precision.
    return ' '.join(unicodedata.normalize('NFC', text).split())


def ratio(n: int, d: int):
    return n / d if d else None


def label_metrics(expected: list[dict], supplied) -> dict | None:
    if supplied is None:
        return None
    if not isinstance(supplied, list):
        raise ValueError('turn_labels must be an array')
    def index(rows):
        out = {}
        for row in rows:
            if not isinstance(row, dict) or set(row) != {'event_id', 'task_id', 'acts'}:
                raise ValueError('bad turn-label record')
            key = (row['event_id'], row['task_id'])
            if not all(isinstance(k, str) and k for k in key) or key in out:
                raise ValueError('duplicate/invalid turn-label key')
            acts = row['acts']
            if not isinstance(acts, list) or not all(isinstance(x, str) for x in acts):
                raise ValueError('invalid acts')
            if len(set(acts)) != len(acts) or not set(acts) <= ACTS:
                raise ValueError('duplicate/unknown acts')
            out[key] = set(acts)
        return out
    truth, pred = index(expected), index(supplied)
    t = {(e, task, act) for (e, task), acts in truth.items() for act in acts}
    p = {(e, task, act) for (e, task), acts in pred.items() for act in acts}
    tp, fp, fn = len(t & p), len(p - t), len(t - p)
    keys = set(truth) | set(pred)
    exact = sum(key in truth and key in pred and truth[key] == pred[key] for key in keys)
    return {'tp': tp, 'fp': fp, 'fn': fn, 'precision': ratio(tp, tp + fp),
            'recall': ratio(tp, tp + fn), 'f1': ratio(2 * tp, 2 * tp + fp + fn),
            'exact_turn_task_sets': exact, 'turn_task_sets': len(keys)}


def evaluate_case(case: dict, prediction: dict, validator=None) -> dict:
    validator = validator or ContractValidator()
    gold = materialize_case(case)
    expected = {s['scope']['task_id']: s for s in gold['specs']}
    events = {e['id']: e for e in gold['history_events']}
    labels = {(x['event_id'], x['task_id']): set(x['acts']) for x in gold['turn_labels']}
    findings, reviews = [], []
    def fail(code, task=None, statement=None):
        findings.append({'code': code, 'task_id': task, 'statement_id': statement})
    def review(code, task=None, statement=None):
        reviews.append({'code': code, 'task_id': task, 'statement_id': statement})
    total = sum(s['status'] in ACTIVE for sp in gold['specs'] for s in sp['statements'])
    exception_total = sum(s['status'] in ACTIVE and s['kind'] == 'exception'
                          for sp in gold['specs'] for s in sp['statements'])
    covered = exception_covered = 0
    absent = []
    method = 'exact_whole_clause'
    alignment_meta = None
    metrics = None
    supplied = {}
    if not isinstance(prediction, dict) or prediction.get('case_id') != case['id'] or not isinstance(prediction.get('specs'), list):
        fail('prediction.envelope')
    else:
        for spec in prediction['specs']:
            issues = validator.validate(spec)
            if issues:
                fail('prediction.contract')
                # Codes only: no confidential source values echoed.
                findings[-1]['contract_codes'] = sorted({x.code for x in issues})
                continue
            task = spec['scope']['task_id']
            if task in supplied:
                fail('prediction.duplicate_task', task)
            supplied[task] = spec
        try:
            metrics = label_metrics(gold['turn_labels'], prediction.get('turn_labels'))
        except (KeyError, TypeError, ValueError):
            fail('prediction.label_format')

    # Optional reviewed semantic matches are evidence of an external adjudication,
    # not something this program can itself certify.
    alignment = prediction.get('alignment') if isinstance(prediction, dict) else None
    manual = {}
    if alignment is not None:
        try:
            if not isinstance(alignment, dict) or not isinstance(alignment['pairs'], list):
                raise ValueError
            if not all(isinstance(alignment.get(k), str) and alignment[k].strip()
                       for k in ('reviewer', 'method')):
                raise ValueError
            used_gold = set()
            for p in alignment['pairs']:
                task, candidate_id, gold_id = p['task_id'], p['candidate_id'], p['gold_id']
                if task not in expected or task not in supplied:
                    raise ValueError
                if candidate_id not in {s['id'] for s in supplied[task]['statements']} or gold_id not in {s['id'] for s in expected[task]['statements']}:
                    raise ValueError
                if (task, candidate_id) in manual or (task, gold_id) in used_gold:
                    raise ValueError
                manual[task, candidate_id] = gold_id
                used_gold.add((task, gold_id))
            method = 'exact_plus_reviewed_alignment'
            alignment_meta = {'reviewer': alignment['reviewer'], 'method': alignment['method'],
                              'pairs': len(manual)}
        except (KeyError, TypeError, ValueError):
            manual.clear()
            fail('prediction.alignment_format')

    for task in supplied.keys() - expected.keys():
        fail('prediction.unexpected_task', task)
    for task, truth in expected.items():
        spec = supplied.get(task)
        if spec is None:
            fail('prediction.missing_task', task)
            absent.extend({'task_id': task, 'gold_id': s['id']}
                          for s in truth['statements'] if s['status'] in ACTIVE)
            continue
        if spec['scope'] != truth['scope']:
            fail('prediction.scope', task)
        if spec['known_at'] != truth['known_at']:
            # This benchmark explicitly fixes the knowledge slice, not a universal
            # requirement that timestamps must always have identical spelling.
            fail('prediction.knowledge_slice', task)
        if not set(spec['history_event_ids']) <= set(events):
            fail('prediction.foreign_history', task)
        for ref in spec['source_refs']:
            ev = events.get(ref['event_id'])
            if ev is None or ref['locator'] != ev['source'] or ref['known_at'] != ev['known_at']:
                fail('prediction.source_reference', task)
            elif 'quote' in ref and ref['quote'] not in ev['payload']['content']:
                fail('prediction.source_quote', task)

        g = {s['id']: s for s in truth['statements']}
        by_text = {}
        for s in truth['statements']:
            by_text.setdefault(normalized(s['text']), []).append(s['id'])
        matches, used = {}, set()
        unreviewed_active = False
        for s in spec['statements']:
            exact = by_text.get(normalized(s['text']), [])
            gid = manual.get((task, s['id']))
            if gid is not None and len(exact) == 1 and exact[0] != gid:
                fail('prediction.alignment_conflict', task, s['id']); continue
            gid = gid if gid is not None else (exact[0] if len(exact) == 1 else None)
            if gid is None:
                review('clause.unaligned', task, s['id'])
                unreviewed_active |= s['status'] in ACTIVE
            elif gid in used:
                fail('clause.duplicate_alignment', task, s['id'])
            else:
                matches[s['id']] = gid
                used.add(gid)

            if s['status'] in ACTIVE and s['kind'] not in {'open_issue', 'executor_context'}:
                acts = [labels.get((event, task)) for event in s['source_event_ids']]
                if acts and all(a and a <= NON_DIRECTIVE for a in acts):
                    fail('clause.unsupported_correction', task, s['id'])
                if acts and all(a == {'executor_only_context'} for a in acts):
                    fail('clause.executor_context_leak', task, s['id'])

        satisfied = set()
        for s in spec['statements']:
            gid = matches.get(s['id'])
            if gid is None:
                continue
            target = g[gid]
            ok = True
            if target['status'] in {'superseded', 'rejected'} and s['status'] in ACTIVE:
                fail('clause.rejected_or_superseded_return', task, s['id']); ok = False
            elif target['status'] == 'contested' and s['status'] == 'active':
                fail('clause.unjustified_resolution', task, s['id']); ok = False
            elif target['status'] != s['status']:
                fail('clause.status_mismatch', task, s['id']); ok = False
            if target['kind'] != s['kind']:
                code = 'clause.executor_context_leak' if target['kind'] == 'executor_context' and s['status'] in ACTIVE else 'clause.kind_mismatch'
                fail(code, task, s['id']); ok = False
            if sorted(map(normalized, s['conditions'])) != sorted(map(normalized, target['conditions'])):
                fail('clause.condition_mismatch', task, s['id']); ok = False
            if not set(s['source_event_ids']) & set(target['source_event_ids']):
                review('clause.source_alignment', task, s['id']); ok = False
            mapped_supersedes = {matches.get(k) for k in s['supersedes']}
            if mapped_supersedes != set(target['supersedes']):
                fail('clause.supersession_mismatch', task, s['id']); ok = False
            if ok and target['status'] in ACTIVE:
                satisfied.add(gid)
        covered += len(satisfied)
        exception_covered += sum(g[x]['kind'] == 'exception' for x in satisfied)
        for s in truth['statements']:
            if s['status'] in ACTIVE and s['id'] not in satisfied:
                absent.append({'task_id': task, 'gold_id': s['id']})
                # Absence of an exact phrase cannot decide whether an unfamiliar
                # paraphrase expresses the same requirement.
                if not unreviewed_active:
                    fail('clause.reference_omission', task, s['id'])
        if spec['compiled_instruction'] != render_statements(spec['statements']):
            review('instruction.requires_semantic_review', task)

    status = 'fail' if findings else ('needs_review' if reviews or absent else 'pass')
    return {'case_id': case['id'], 'language': case['language'], 'domain': case['domain'],
            'status': status, 'evaluation_scope': 'reference_conformance_not_general_semantic_accuracy',
            'alignment_method': method, 'alignment': alignment_meta,
            'reference_coverage': {'matched': covered, 'total': total, 'ratio': ratio(covered, total)},
            'exception_coverage': {'matched': exception_covered, 'total': exception_total,
                                   'ratio': ratio(exception_covered, exception_total)},
            'missing_reference_clauses': absent, 'findings': findings, 'review_items': reviews,
            'turn_label_metrics': metrics}


def evaluate_cases(cases: list[dict], predictions: list[dict]) -> dict:
    if not isinstance(predictions, list):
        raise ValueError('predictions array required')
    pmap = {}
    for p in predictions:
        if not isinstance(p, dict) or not isinstance(p.get('case_id'), str) or p['case_id'] in pmap:
            raise ValueError('invalid/duplicate prediction case')
        pmap[p['case_id']] = p
    expected = {c['id'] for c in cases}
    if set(pmap) - expected:
        raise ValueError('prediction contains unrequested cases')
    validator = ContractValidator()
    reports = [evaluate_case(c, pmap.get(c['id'], {'case_id': c['id'], 'specs': []}), validator)
               for c in cases]
    counts = Counter(r['status'] for r in reports)
    n = sum(r['reference_coverage']['matched'] for r in reports)
    d = sum(r['reference_coverage']['total'] for r in reports)
    return {'schema': 'loom.refinement_report/1', 'cases': len(cases),
            'predictions_supplied': len(predictions), 'missing_predictions': len(expected - set(pmap)),
            'case_status_counts': dict(counts), 'reference_coverage': {'matched': n, 'total': d, 'ratio': ratio(n, d)},
            'semantic_accuracy': None,
            'limitation': 'Structural/reference scoring; unaligned text needs review. Not a model benchmark unless predictions are actual recorded outputs.',
            'reports': reports}


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('corpus', type=Path)
    p.add_argument('predictions', type=Path, help='JSON object with predictions array')
    p.add_argument('--include-validation', action='store_true')
    p.add_argument('--output', type=Path)
    args = p.parse_args(argv)
    try:
        if verify_manifest(args.corpus.parent):
            raise ValueError('frozen source changed')
        cases = load_cases(args.corpus)
        if any(c['split'] == 'validation' for c in cases) and not args.include_validation:
            raise ValueError('validation needs explicit opt-in')
        predictions = read_json(args.predictions)['predictions']
        result = evaluate_cases(cases, predictions)
        encoded = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
        if args.output:
            args.output.write_text(encoded, encoding='utf-8')
        else:
            print(encoded, end='')
        return 0  # execution completed; case verdicts are in the report
    except (OSError, KeyError, TypeError, ValueError, RecursionError):
        print(json.dumps({'error': 'input_manifest_or_opt_in_error'}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
