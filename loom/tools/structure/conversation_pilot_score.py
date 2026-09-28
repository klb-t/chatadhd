#!/usr/bin/env python3
"""Offline scoring of first conversation-context responses; never calls a model.

An envelope is {case_id,message_id,transport_status,raw_response}. raw_response
is the unmodified JSON text (or null for a missing response). Only completed
transport and a structurally valid complete object earn semantic credit.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path

VERSION = 'conversation-pilot-score/1'
FIELDS = {'case_id', 'message_id', 'memberships', 'boundaries', 'relations',
          'selected_claim_ids', 'reference_decisions', 'history_decisions'}
ARRAYS = FIELDS - {'case_id', 'message_id'}
SPAN_FIELDS = {'observation', 'byte_start', 'byte_len', 'quote'}
METRICS = ('topic_message', 'topic_span', 'boundary', 'onset', 'return',
           'relations', 'old_claim_selection', 'old_source_selection',
           'old_source_groups', 'unknown_time_claim_selection', 'reference_abstention')


def strict_json(raw):
    def pairs(rows):
        result = {}
        for key, value in rows:
            if key in result:
                raise ValueError('duplicate JSON key')
            result[key] = value
        return result
    def constant(value):
        raise ValueError('non-finite JSON number')
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)


def ratio(a, b):
    return a / b if b else None


def counts(predicted, expected):
    return {'tp': len(predicted & expected), 'fp': len(predicted - expected),
            'fn': len(expected - predicted)}


def measure(values):
    tp, fp, fn = (values[k] for k in ('tp', 'fp', 'fn'))
    p, r = ratio(tp, tp + fp), ratio(tp, tp + fn)
    f = None if p is None or r is None else (2 * p * r / (p + r) if p + r else 0.0)
    return {**values, 'precision': p, 'recall': r, 'f1': f}


def instant(value):
    try:
        t = datetime.fromisoformat(value.replace('Z', '+00:00'))
        return t if t.tzinfo else None
    except (AttributeError, TypeError, ValueError):
        return None


def chronology(prefix, claim):
    """All endpoints and every support must be strictly earlier than target."""
    packet = prefix['source_packet']
    obs = {o['id']: o for o in packet['observations']}
    entities = {e['id']: e for e in packet['entities']}
    target = next(m for m in prefix['messages'] if m['id'] == prefix['target_message_id'])
    cut = instant(target['observed_at'])
    dependencies = [claim]
    for field in ('subject', 'object'):
        ref = claim.get(field)
        if ref:
            dependencies.append(entities.get(ref, {}))
    supports = claim.get('assessment', {}).get('basis', {}).get('support', [])
    if not supports:
        return 'unknown'
    dependencies.extend(obs.get(s['observation'], {}) for s in supports)
    dates = [instant(d.get('observed_at')) for d in dependencies]
    return 'known_prior' if cut and all(t is not None and t < cut for t in dates) else 'unknown'


def validate_response(prefix, response):
    """Validate against this causal prefix only, never the full conversation."""
    errors, diagnostics = [], Counter()
    packet = prefix['source_packet']
    observations = {o['id']: o for o in packet['observations']}
    claims = {c['id']: c for c in packet['claims']}
    topics = {t['id'] for t in prefix['topics']}
    target = next(m for m in prefix['messages'] if m['id'] == prefix['target_message_id'])
    def err(path, reason):
        errors.append({'path': path, 'reason': reason})
    def shape(row, keys, path):
        if not isinstance(row, dict) or set(row) != set(keys):
            err(path, 'exact_fields_required')
            if isinstance(row, dict):
                diagnostics['identity_merges'] += sum(k in row for k in ('identity_merges', 'merges', 'merge'))
            return False
        return True
    def identifier(value, allowed, path, citation=False):
        diagnostics['ids_checked'] += 1
        if not isinstance(value, str) or value not in allowed:
            err(path, 'foreign_or_out_of_prefix_id')
            diagnostics['invalid_or_foreign_ids'] += 1
            if citation:
                diagnostics['out_of_prefix_citations'] += 1
            return False
        return True
    def span(value, path):
        diagnostics['spans_submitted'] += 1
        before = len(errors)
        if shape(value, SPAN_FIELDS, path):
            if identifier(value['observation'], observations, path, True):
                if value['observation'] != target['observation']:
                    err(path, 'span_must_cite_target_observation')
                    diagnostics['non_target_citations'] += 1
                start, length, quote = value['byte_start'], value['byte_len'], value['quote']
                raw = observations[value['observation']]['text'].encode('utf-8')
                if type(start) is not int or type(length) is not int or start < 0 or length <= 0 or not isinstance(quote, str):
                    err(path, 'invalid_utf8_span')
                else:
                    try:
                        match = start + length <= len(raw) and raw[start:start + length].decode('utf-8') == quote
                    except UnicodeDecodeError:
                        match = False
                    if not match:
                        err(path, 'fabricated_quote_or_invalid_byte_boundary')
                        diagnostics['fabricated_quotes'] += 1
        if len(errors) > before:
            diagnostics['spans_invalid'] += 1
    if not shape(response, FIELDS, '$'):
        return errors, diagnostics
    for field, expected in (('case_id', prefix['case_id']), ('message_id', prefix['target_message_id'])):
        if response[field] != expected:
            err(field, 'response_identity_mismatch')
            diagnostics['invalid_or_foreign_ids'] += 1
    for field in ARRAYS:
        if not isinstance(response[field], list):
            err(field, 'array_required')
    if errors:
        return errors, diagnostics
    item_fields = {'memberships': {'topic_id', 'span'}, 'boundaries': {'topic_id', 'type', 'span'},
                   'relations': {'type', 'target_claim_id', 'span'},
                   'reference_decisions': {'span', 'status', 'alternative_topic_ids', 'selected_topic_id'},
                   'history_decisions': {'claim_id', 'status'}}
    for field, keys in item_fields.items():
        seen_history = set()
        for i, row in enumerate(response[field]):
            path = f'{field}[{i}]'
            if not shape(row, keys, path):
                continue
            if 'span' in row:
                span(row['span'], path + '.span')
            if 'topic_id' in row:
                identifier(row['topic_id'], topics, path + '.topic_id')
            if field == 'boundaries' and row['type'] not in ('onset', 'return'):
                err(path, 'invalid_boundary_type')
            if field == 'relations':
                identifier(row['target_claim_id'], claims, path + '.target_claim_id', True)
                if row['type'] not in ('correction', 'contradiction', 'analogy'):
                    err(path, 'invalid_relation_type')
                    diagnostics['identity_merges'] += row['type'] in ('identity', 'merge', 'same_as', 'identity_merge')
            if field == 'reference_decisions':
                if row['status'] not in ('unresolved', 'ambiguous', 'resolved'):
                    err(path, 'invalid_reference_status')
                alternatives = row['alternative_topic_ids']
                if not isinstance(alternatives, list):
                    err(path, 'alternative_topics_array_required')
                else:
                    for topic in alternatives:
                        identifier(topic, topics, path + '.alternative_topic_ids')
                if row['selected_topic_id'] is not None:
                    identifier(row['selected_topic_id'], topics, path + '.selected_topic_id')
                if row['status'] in ('unresolved', 'ambiguous') and row['selected_topic_id'] is not None:
                    err(path, 'abstention_cannot_select_topic')
            if field == 'history_decisions':
                valid = identifier(row['claim_id'], claims, path + '.claim_id', True)
                if row['status'] not in ('known_prior', 'unknown'):
                    err(path, 'invalid_history_status')
                if valid:
                    if row['claim_id'] in seen_history:
                        err(path, 'duplicate_history_decision')
                    seen_history.add(row['claim_id'])
                    if row['status'] == 'known_prior' and chronology(prefix, claims[row['claim_id']]) != 'known_prior':
                        diagnostics['asserted_unknown_chronology'] += 1
    seen_selected = set()
    for i, claim in enumerate(response['selected_claim_ids']):
        if identifier(claim, claims, f'selected_claim_ids[{i}]', True):
            if claim in seen_selected:
                err('selected_claim_ids', 'duplicate_claim_selection')
            seen_selected.add(claim)
    return errors, diagnostics


def span_key(span):
    return (span['observation'], span['byte_start'], span['byte_len'])


def projections(prefix, response):
    claims = {c['id']: c for c in prefix['source_packet']['claims']}
    observations = {o['id']: o for o in prefix['source_packet']['observations']}
    selected = set(response['selected_claim_ids'])
    sources = {s['observation'] for c in selected for s in claims[c]['assessment']['basis']['support']}
    groups = {observations[o]['source_group'] for o in sources if observations[o].get('source_group') is not None}
    # Set of byte positions implements interval union, including overlap/repetition.
    span_bytes = {(x['topic_id'], x['span']['observation'], i) for x in response['memberships']
                  for i in range(x['span']['byte_start'], x['span']['byte_start'] + x['span']['byte_len'])}
    boundaries = {(x['topic_id'], x['type']) for x in response['boundaries']}
    return {'topic_message': {x['topic_id'] for x in response['memberships']},
            'topic_span': span_bytes, 'boundary': boundaries,
            'onset': {x for x in boundaries if x[1] == 'onset'},
            'return': {x for x in boundaries if x[1] == 'return'},
            'relations': {(x['type'], x['target_claim_id']) for x in response['relations']},
            'old_claim_selection': selected, 'old_source_selection': sources, 'old_source_groups': groups,
            'unknown_time_claim_selection': {c for c in selected if chronology(prefix, claims[c]) == 'unknown'},
            'reference_abstention': {(span_key(x['span']), x['status'], tuple(sorted(set(x['alternative_topic_ids']))),
                                     x['selected_topic_id']) for x in response['reference_decisions']
                                    if x['status'] in ('unresolved', 'ambiguous')}}


def score_unit(prefix, gold, envelope=None):
    """Return JSON-safe metrics for exactly one registered causal query."""
    expected = {k: gold[k] for k in ARRAYS}
    expected.update(case_id=prefix['case_id'], message_id=prefix['target_message_id'])
    gold_errors, _ = validate_response(prefix, expected)
    if gold_errors:
        raise ValueError(f'invalid evaluator labels: {gold_errors}')
    errors, diagnostics, response = [], Counter(), None
    status, raw_hash = 'missing', None
    if envelope is not None:
        if set(envelope) != {'case_id', 'message_id', 'transport_status', 'raw_response'}:
            raise ValueError('envelope requires exact fields')
        if envelope['case_id'] != prefix['case_id'] or envelope['message_id'] != prefix['target_message_id']:
            raise ValueError('envelope identity mismatch')
        status = envelope['transport_status']
        if status not in ('completed', 'missing', 'truncated', 'refused', 'error'):
            raise ValueError('unknown transport status')
        raw = envelope['raw_response']
        if raw is not None and not isinstance(raw, str):
            raise ValueError('raw_response must be untouched text or null')
        if raw is not None:
            raw_hash = hashlib.sha256(raw.encode('utf-8')).hexdigest()
        if status == 'completed':
            try:
                response = strict_json(raw) if raw is not None else None
                errors, diagnostics = validate_response(prefix, response)
            except (ValueError, TypeError, RecursionError):
                errors = [{'path': '$', 'reason': 'malformed_json'}]
            if errors:
                status = 'invalid_output'
    valid = status == 'completed'
    empty = {k: [] for k in ARRAYS}
    predicted = response if valid else empty
    p, g = projections(prefix, predicted), projections(prefix, expected)
    history = {x['claim_id']: x['status'] for x in predicted['history_decisions']}
    required = {x['claim_id']: x['status'] for x in expected['history_decisions']}
    reference_opportunities = len(expected['reference_decisions'])
    # Diagnostics inspect individually valid, located decisions even when another
    # field invalidates the output. An ambiguous decision that selects a topic is
    # itself contradictory, but its validated span/topic still expose resolution.
    located_memberships, located_resolutions = [], []
    if isinstance(response, dict):
        for field in ('memberships', 'reference_decisions'):
            rows = response.get(field, [])
            if not isinstance(rows, list):
                continue
            for item in rows:
                probe = {key: [] for key in ARRAYS}
                probe.update(case_id=prefix['case_id'], message_id=prefix['target_message_id'])
                checked = item
                if field == 'reference_decisions':
                    if (not isinstance(item, dict)
                            or set(item) != {'span', 'status', 'alternative_topic_ids', 'selected_topic_id'}
                            or item['status'] not in ('resolved', 'ambiguous', 'unresolved')
                            or item['selected_topic_id'] is None):
                        continue
                    # Normalize only in the diagnostic probe, never in scored or
                    # retained output, to validate contradictory status/selection.
                    checked = {**item, 'status': 'resolved'}
                probe[field] = [checked]
                element_errors, _ = validate_response(prefix, probe)
                if not element_errors:
                    (located_memberships if field == 'memberships' else located_resolutions).append(item)
    unsupported = 0
    for ref in expected['reference_decisions']:
        s = ref['span']; a, b = s['byte_start'], s['byte_start'] + s['byte_len']
        def overlaps(x):
            t = x['span']
            return t['observation'] == s['observation'] and max(a, t['byte_start']) < min(b, t['byte_start'] + t['byte_len'])
        unsupported += any(overlaps(x) for x in located_resolutions) or any(overlaps(x) for x in located_memberships)
    diagnostics['unsupported_reference_resolution'] += unsupported
    confusion = Counter()
    for target in {t for _, t in g['relations']}:
        for actual in sorted(t for t, c in g['relations'] if c == target):
            found = sorted(t for t, c in p['relations'] if c == target)
            for guess in found or ['missing']:
                confusion[actual + '->' + guess] += 1
    target = next(m for m in prefix['messages'] if m['id'] == prefix['target_message_id'])
    return {'case_id': prefix['case_id'], 'message_id': prefix['target_message_id'],
            'language': prefix.get('language'), 'ordinal': target['ordinal'], 'status': status,
            'raw_response_sha256': raw_hash, 'errors': errors, 'diagnostics': dict(diagnostics),
            'metrics': {k: counts(p[k], g[k]) for k in METRICS},
            'topic_set_exact': bool(valid and p['topic_message'] == g['topic_message']),
            'claim_set_exact': bool(valid and p['old_claim_selection'] == g['old_claim_selection']),
            'history': {'correct': sum(history.get(c) == s for c, s in required.items()),
                        'required': len(required), 'extra': len(set(history) - set(required))},
            'reference_opportunities': reference_opportunities,
            'abstained_message': bool(p['reference_abstention']), 'relation_confusion': dict(confusion),
            'predicted_onsets': sorted(t for t, _ in p['onset']),
            'gold_onsets': sorted(t for t, _ in g['onset'])}


def aggregate(rows):
    total = len(rows)
    metrics = {k: measure({n: sum(r['metrics'][k][n] for r in rows) for n in ('tp', 'fp', 'fn')}) for k in METRICS}
    diagnostics = Counter()
    confusion = Counter()
    pred, gold = defaultdict(list), defaultdict(list)
    for row in rows:
        diagnostics.update(row['diagnostics']); confusion.update(row['relation_confusion'])
        for field, dest in (('predicted_onsets', pred), ('gold_onsets', gold)):
            for topic in row[field]:
                dest[(row['case_id'], topic)].append(row['ordinal'])
    common = set(pred) & set(gold)
    correct = sum(r['history']['correct'] for r in rows)
    required = sum(r['history']['required'] for r in rows)
    reference_opportunities = sum(r['reference_opportunities'] for r in rows)
    return {'queries': total, 'successful_outputs': sum(r['status'] == 'completed' for r in rows),
            'statuses': dict(Counter(r['status'] for r in rows)), 'metrics': metrics,
            'topic_set_accuracy': ratio(sum(r['topic_set_exact'] for r in rows), total),
            'claim_set_accuracy': ratio(sum(r['claim_set_exact'] for r in rows), total),
            'history': {'correct': correct, 'required': required, 'accuracy': ratio(correct, required),
                        'extra': sum(r['history']['extra'] for r in rows)},
            'onset_ordinal': {'matched_topics': len(common), 'missing_topics': len(set(gold) - set(pred)),
                              'extra_topics': len(set(pred) - set(gold)),
                              'mae_first_onset': ratio(sum(abs(min(pred[k]) - min(gold[k])) for k in common), len(common))},
            'reference_opportunities': reference_opportunities,
            'unsupported_reference_resolution_rate': ratio(diagnostics['unsupported_reference_resolution'], reference_opportunities),
            'diagnostic_counts_per_query': {k: ratio(v, total) for k, v in diagnostics.items()
                                            if k not in ('spans_submitted', 'ids_checked')},
            'invalid_id_rate': ratio(diagnostics['invalid_or_foreign_ids'], diagnostics['ids_checked']),
            'abstention_frequency': ratio(sum(r['abstained_message'] for r in rows), total),
            'span_invalid_rate': ratio(diagnostics['spans_invalid'], diagnostics['spans_submitted']),
            'diagnostics': dict(diagnostics), 'relation_confusion': dict(confusion)}


def score_experiment(units, envelopes):
    """A unit contains prefix, gold and optional family; absent responses fail."""
    units = list(units)
    planned = {(u['prefix']['case_id'], u['prefix']['target_message_id']) for u in units}
    if len(planned) != len(units):
        raise ValueError('duplicate registered query')
    indexed = {}
    for envelope in envelopes:
        key = (envelope['case_id'], envelope['message_id'])
        if key not in planned or key in indexed:
            raise ValueError('unregistered or duplicate first response')
        indexed[key] = envelope
    rows = []
    for unit in units:
        prefix = unit['prefix']; key = (prefix['case_id'], prefix['target_message_id'])
        row = score_unit(prefix, unit['gold'], indexed.get(key))
        row['family'] = unit.get('family', 'unspecified')
        rows.append(row)
    by = {field: {str(value): aggregate([r for r in rows if r[field] == value])
                  for value in sorted({r[field] for r in rows}, key=str)} for field in ('family', 'language')}
    macro = {field: {metric: {'f1': ratio(sum(v['metrics'][metric]['f1'] for v in groups.values() if v['metrics'][metric]['f1'] is not None),
                                         sum(v['metrics'][metric]['f1'] is not None for v in groups.values())),
                             'defined_groups': sum(v['metrics'][metric]['f1'] is not None for v in groups.values())}
                     for metric in METRICS} for field, groups in by.items()}
    return {'schema': 'loom.eval.conversation_context_score/1', 'scorer_version': VERSION,
            'all_queries': aggregate(rows), 'successful_output_conditional': aggregate([r for r in rows if r['status'] == 'completed']),
            'by': by, 'macro': macro, 'units': rows, 'live_model_calls_by_scorer': 0,
            'limitations': ['Synthetic supplied-topic tracking; no population accuracy claim.',
                           'Structurally invalid outputs earn no semantic credit, including their otherwise valid entries.',
                           'An absent ID is classified as foreign or out-of-prefix; a causal packet cannot distinguish the two.',
                           'Unknown source groups are omitted from group counts, never invented as independent evidence.',
                           'Unsupported-structure diagnostics are bounded checks, not exhaustive hallucination detection.',
                           'No confidence intervals; bilingual pairs are correlated.']}


def fixture_units(directory, split):
    spec = importlib.util.spec_from_file_location('conversation_materialize', directory / 'materialize.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    assignments = {r['case_id']: r for r in strict_json((directory / 'split.json').read_text())['cases'] if r['split'] == split}
    cases = {r['case_id']: r for r in (strict_json(x) for x in (directory / 'inputs.jsonl').read_text().splitlines()) if r['case_id'] in assignments}
    # Evaluator-only labels; never exported to a request or model.
    gold = {r['case_id']: r for r in (strict_json(x) for x in (directory / 'gold.jsonl').read_text().splitlines()) if r['case_id'] in assignments}
    for case_id, case in cases.items():
        for i, label in enumerate(gold[case_id]['labels']):
            yield {'prefix': module.project(case, i), 'gold': label, 'family': assignments[case_id]['family']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture-dir', type=Path, default=Path(__file__).resolve().parents[2] / 'tests/fixtures/eval/conversation_context_pilot_v1')
    parser.add_argument('--split', choices=('development', 'validation'), default='development')
    parser.add_argument('--responses', required=True, type=Path, help='First response envelopes in JSONL; an empty file means all missing.')
    parser.add_argument('--output', required=True, type=Path, help='New report path; never overwrites earlier results.')
    args = parser.parse_args()
    envelopes = [strict_json(line) for line in args.responses.read_text(encoding='utf-8').splitlines() if line.strip()]
    result = score_experiment(fixture_units(args.fixture_dir, args.split), envelopes)
    with args.output.open('x', encoding='utf-8') as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, allow_nan=False)
        handle.write('\n')


if __name__ == '__main__':
    main()
