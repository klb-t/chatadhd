"""Frozen explicit-relation scorer: direction, exact spans, operation, qualifiers.

Synthetic source labels measure envelope extraction, never operand truth. The
fixture's unembedded-assertion policy does not veto other retrieval channels.
Validation requires an explicit release plus a pre-existing implementation freeze.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

try:
    from . import extract
except ImportError:
    import extract

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'loom/tests/fixtures/eval/structure_round3_v1'
VERSION = 'explicit-relation-scorer/1'
IMPLEMENTATION = ('loom/tools/structure/extract.py',
                  'loom/tools/structure/relation_envelopes.py',
                  'loom/tools/structure/relation_envelopes_policy.json',
                  'loom/tools/structure/round3_eval.py')
LEGACY = {'conditional': ('conditional', 'implies'),
          'universal_inclusion': ('generalization', 'generalizes'),
          'type_inclusion': ('generalization', 'generalizes'),
          'exception': ('exception', 'unless')}

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def implementation_hashes():
    return {path: digest(ROOT / path) for path in IMPLEMENTATION}

def load_fixture(split, *, validation_release=False, freeze=None):
    if split == 'validation':
        if not validation_release or freeze is None:
            raise ValueError('validation needs explicit release and frozen implementation')
        frozen = json.loads(Path(freeze).read_text())
        if frozen.get('implementation_sha256') != implementation_hashes():
            raise ValueError('implementation/scorer changed after freeze')
        if frozen.get('fixture_manifest_sha256') != digest(FIXTURE / 'manifest.json'):
            raise ValueError('fixture manifest changed after freeze')
    manifest = json.loads((FIXTURE / 'manifest.json').read_text())
    path = FIXTURE / (split + '.json')
    if digest(path) != manifest['files'][path.name]:
        raise ValueError('fixture hash drift')
    return json.loads(path.read_text())

def predictions(cases, policy):
    return [{'case_id': case['id'], 'extraction': extract.extract_record(
        {'id': case['id'], 'source_id': 'synthetic:' + case['id'],
         'text': case['text'], 'known_at': '2026-09-29T00:00:00Z'}, policy=policy)}
        for case in cases]

def _span(span):
    values = (span['char_start'], span['char_end'], span['byte_start'], span['byte_end'])
    if any(type(value) is not int for value in values):
        raise ValueError('source offsets must be JSON integers')
    if span.get('coordinate_space') != 'input.text':
        raise ValueError('unexpected source coordinate space')
    return values

def _expected(relation):
    operands = tuple(sorted((op['role'], op['text'], tuple(op['span']) + tuple(op['utf8_byte_span']))
                            for op in relation['operands']))
    cue = relation['cue']
    return (relation['family'], relation['operation'], operands,
            (cue['text'], tuple(cue['span']) + tuple(cue['utf8_byte_span'])),
            json.dumps(relation['qualifiers'], sort_keys=True))

def _actual(candidate, text):
    family, operation = candidate['operation_family'], candidate['operation']
    if operation in LEGACY:
        family, operation = LEGACY[operation]
    operands = []
    for role, slot in candidate['slots'].items():
        span = slot['span']; a, b, x, y = _span(span)
        if not (0 <= a < b <= len(text)) or text[a:b] != slot['text'] or span['quote'] != text[a:b]:
            raise ValueError('operand source span mismatch')
        if len(text[:a].encode()) != x or len(text[:b].encode()) != y:
            raise ValueError('operand UTF8 mismatch')
        operands.append((role, slot['text'], (a, b, x, y)))
    cue = candidate.get('cue')
    cue_key = None if cue is None else (cue['text'], _span(cue['span']))
    if cue is not None:
        a, b, x, y = cue_key[1]
        if not (0 <= a < b <= len(text)) or text[a:b] != cue['text'] or cue['span'].get('quote') != text[a:b] or (len(text[:a].encode()), len(text[:b].encode())) != (x, y):
            raise ValueError('cue source span mismatch')
    return (family, operation, tuple(sorted(operands)), cue_key,
            json.dumps(candidate.get('qualifiers', {}), sort_keys=True))

def metrics(tp, fp, fn):
    return {'tp': tp, 'fp': fp, 'fn': fn, 'gold_relations': tp + fn,
            'predicted_relations': tp + fp, 'precision': tp / (tp + fp) if tp + fp else None,
            'recall': tp / (tp + fn) if tp + fn else None}

def score(cases, outputs):
    ids = [c['id'] for c in cases]
    if len(set(ids)) != len(ids):
        raise ValueError('duplicate fixture IDs')
    by_id = {}
    for output in outputs:
        key = output['case_id']
        if key in by_id or key not in ids:
            raise ValueError('duplicate or unknown output case')
        by_id[key] = output['extraction']
    total = Counter(); family_counts = {}; failures = []; details = []
    for case in cases:
        result = by_id.get(case['id'], {'candidates': []})
        gold = [_expected(e) for e in case['expected']]
        actual = []; invalid = 0
        for candidate in result.get('candidates', []):
            try:
                actual.append(_actual(candidate, case['text']))
            except (KeyError, TypeError, ValueError):
                invalid += 1
        g, p = Counter(gold), Counter(actual)
        tp = sum((g & p).values()); fp = sum((p - g).values()) + invalid; fn = sum((g - p).values())
        fg = Counter(x[0] for x in gold); fa = Counter(x[0] for x in actual)
        ft = sum((fg & fa).values()); ff = sum((fa - fg).values()) + invalid; fm = sum((fg - fa).values())
        counted = {'tp': tp, 'fp': fp, 'fn': fn, 'family_tp': ft, 'family_fp': ff, 'family_fn': fm,
                   'cases': 1, 'abstain_gold_cases': int(not gold),
                   'abstain_predicted_cases': int(not result.get('candidates')),
                   'missing_output_cases': int(case['id'] not in by_id), 'invalid_outputs': invalid}
        total.update(counted)
        # Per-family strata are by independently authored source-case family;
        # wrong-family predictions remain FP in the source stratum.
        family_counts.setdefault(case['family'], Counter()).update(counted)
        row = {'case_id': case['id'], 'family': case['family'], **counted}
        if fp or fn:
            row['failure'] = 'false_positive' if not gold else 'missing' if not actual and not invalid else 'strict_mismatch'
            failures.append(row)
        details.append(row)
    return {'schema': VERSION, 'strict': metrics(total['tp'], total['fp'], total['fn']),
            'family_only': metrics(total['family_tp'], total['family_fp'], total['family_fn']),
            'accounting': dict(total), 'per_family': {k: {'strict': metrics(v['tp'], v['fp'], v['fn']),
                'family_only': metrics(v['family_tp'], v['family_fp'], v['family_fn']), 'accounting': dict(v)}
                for k, v in sorted(family_counts.items())}, 'failures': failures, 'cases': details,
            'semantic_truth_accuracy': None, 'limitation': 'synthetic_unembedded_source_envelopes_not_relation_truth'}

def save_new(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, sort_keys=True, indent=2)
        stream.write('\n')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--split', choices=['dev', 'validation'], default='dev')
    ap.add_argument('--policy', choices=['bounded', 'explicit_relations'], default='bounded')
    ap.add_argument('--predictions', type=Path, help='replay preserved predictions instead of parsing')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--validation-release', action='store_true')
    ap.add_argument('--freeze', type=Path)
    ap.add_argument('--write-freeze', type=Path)
    args = ap.parse_args()
    if args.split == 'validation' and args.write_freeze:
        ap.error('validation requires a pre-existing freeze; cannot create it in the same invocation')
    if args.write_freeze:
        save_new(args.write_freeze, {'implementation_sha256': implementation_hashes(),
                                    'fixture_manifest_sha256': digest(FIXTURE / 'manifest.json')})
    fixture = load_fixture(args.split, validation_release=args.validation_release, freeze=args.freeze)
    before = implementation_hashes()
    output = json.loads(args.predictions.read_text()) if args.predictions else predictions(fixture['cases'], args.policy)
    report = score(fixture['cases'], output)
    if before != implementation_hashes():
        raise ValueError('implementation changed while measuring')
    save_new(args.output, {'split': args.split, 'policy': args.policy,
             'execution': 'preserved_prediction_replay' if args.predictions else 'fresh_parser_run',
             'prediction_file_sha256': digest(args.predictions) if args.predictions else None,
             'fixture_sha256': digest(FIXTURE / (args.split + '.json')), 'implementation_sha256': before,
             'predictions': output, 'report': report})
    print(json.dumps({'strict': report['strict'], 'family_only': report['family_only'], 'cases': len(fixture['cases'])}))

if __name__ == '__main__':
    main()
