#!/usr/bin/env python3
"""DEV direct typed-graph projection; no model, network, index or canonical writes."""
from __future__ import annotations
import argparse
from collections import defaultdict
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
try:
    from . import graph_panel_live as panel
except ImportError:
    import graph_panel_live as panel

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
DATA = HERE / 'graph_direct_lookup_v1'
POLICY_PATH = DATA / 'policy.json'
INPUT_PATH = panel.FIXTURE / 'inputs_dev.json'
GOLD_PATH = panel.FIXTURE / 'gold_dev.json'
COMPILED_PATH = ROOT / 'docs/research/graph_method_panel_v1/extraction/first_score/compiled_first.json'
PROTOCOL_PATH = ROOT / 'docs/research/graph_method_panel_v1/DIRECT_LOOKUP_PROTOCOL.md'
PIPELINE_MODE = 'retrospective_full_conversation_extraction_then_source_cutoff'
UNSUPPORTED_WITHDRAWAL = 'unsupported_withdrawal_event'
CROSS_ATTRIBUTION = 'cross_attribution_supersession'


def read(path):
    return panel.safe.parse_json(Path(path).read_bytes())


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def file_reference(path):
    try:
        return str(Path(path).relative_to(ROOT))
    except ValueError:
        return str(Path(path))


def load_policy(path=POLICY_PATH):
    policy = read(path)
    panel.safe._keys(policy, {'schema', 'scope', 'match_fields', 'source_time_filter', 'supersession',
        'replacement_query_match_required', 'cross_attribution_supersession', 'selection',
        'same_latest_timestamp_opposite_polarities', 'no_match', 'missing_graph', 'withdrawal_only_abi',
        'evidence_validation', 'formal_paths', 'operand_complements', 'content_truth', 'pipeline_mode',
        'causal_prefix_extraction_verified'})
    # Versioned behavior is closed; alternative semantics need their own freeze.
    if (policy.get('schema') != 'loom.graph_direct_lookup_policy/1' or
            policy.get('match_fields') != ['relation', 'source', 'target', 'attributed_to'] or
            policy.get('scope') != 'explicit_source' or policy.get('formal_paths') is not False or
            policy.get('operand_complements') is not False or policy.get('content_truth') != 'unverified' or
            policy.get('source_time_filter') != 'known_at_lte_as_of' or
            policy.get('supersession') != 'all_applicable_events_by_old_assertion_id' or
            policy.get('replacement_query_match_required') is not False or
            policy.get('cross_attribution_supersession') != 'apply_with_warning_not_semantic_repair' or
            policy.get('selection') != 'latest_active_matching_timestamp' or
            policy.get('same_latest_timestamp_opposite_polarities') != 'conflicting_unavailable' or
            policy.get('no_match') != 'unknown' or policy.get('missing_graph') != 'unavailable' or
            policy.get('withdrawal_only_abi') != 'unsupported_unavailable_with_warning' or
            policy.get('evidence_validation') != 'exact_source_binding_not_semantic_proof' or
            policy.get('pipeline_mode') != PIPELINE_MODE or policy.get('causal_prefix_extraction_verified') is not False):
        raise ValueError('unsupported_direct_lookup_policy')
    return policy


def _base(case, query):
    return {'query_id': query.get('id') if isinstance(query, dict) else None,
            'case_id': case.get('id') if isinstance(case, dict) else None,
            'as_of': query.get('as_of') if isinstance(query, dict) else None,
            'state': 'unavailable', 'label': None, 'paths': [], 'history_assertion_ids': [], 'active_assertion_ids': [],
            'matching_active_assertion_ids': [], 'latest_match_ids': [], 'eligible_status_events': [], 'warnings': [],
            'pipeline_mode': PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
            'source_known_at_semantics': 'supporting_source_turn_timestamp_not_model_claim_availability',
            'content_truth': 'unverified', 'source_binding_is_semantic_proof': False, 'no_graph_promotion': True}


def lookup(case, compiled, query, policy=None):
    result = _base(case, query)
    if not isinstance(compiled, dict) or compiled.get('state') != 'completed':
        result['reason'] = 'missing_or_unavailable_compiled_graph'
        return result
    try:
        active_policy = load_policy() if policy is None else policy
        if active_policy != load_policy():
            raise ValueError('unsupported_direct_lookup_policy')
        if compiled['case_id'] != case['id']:
            raise ValueError('compiled_case_identity_mismatch')
        panel.safe._keys(query, {'id', 'relation', 'source', 'target', 'attributed_to', 'as_of', 'scope'})
        if (query['scope'] != active_policy['scope'] or query['relation'] not in panel.RELATIONS or
                any(not isinstance(query[k], str) or not query[k] for k in ('id', 'source', 'target', 'attributed_to'))):
            raise ValueError('invalid_query')
        cutoff = panel._time(query['as_of'])
        nodes = {n['id'] for n in case['node_inventory']}
        if query['source'] not in nodes or query['target'] not in nodes:
            raise ValueError('query_endpoint_not_in_inventory')
        assertions, events = compiled['source_assertions'], compiled['status_events']
        if not isinstance(assertions, list) or not isinstance(events, list):
            raise ValueError('invalid_graph_arrays')
        by_id, times, eligible = {}, {}, []
        for edge in assertions:
            ident = edge['id']
            if not isinstance(ident, str) or not ident or ident in by_id:
                raise ValueError('ambiguous_assertion_id')
            if (edge['relation'] not in panel.RELATIONS or edge['source'] not in nodes or edge['target'] not in nodes or
                    edge['polarity'] not in ('positive', 'negative') or
                    not isinstance(edge['attributed_to'], str) or not edge['attributed_to']):
                raise ValueError('invalid_typed_assertion')
            timestamp = panel._time(edge['known_at'])
            by_id[ident], times[ident] = edge, timestamp
            if timestamp <= cutoff:
                panel._validated_compiled_evidence(edge, case)
                eligible.append(edge)
        eligible.sort(key=lambda edge: (times[edge['id']], edge['id']))
        removed = set()
        ordered_events = sorted(events, key=lambda event: (panel._time(event['known_at']),
            str(event.get('assertion_id', '')), str(event.get('superseded_by', '')), panel.safe.canonical(event)))
        for event in ordered_events:
            event_time = panel._time(event['known_at'])
            if event_time > cutoff:
                continue
            if event.get('status') == 'withdrawn' or event.get('status') == 'superseded' and not event.get('superseded_by'):
                result['warnings'].append(UNSUPPORTED_WITHDRAWAL)
                result['reason'] = 'unsupported_withdrawal_only_abi'
                return result
            if event['status'] != 'superseded':
                raise ValueError('unsupported_status_event')
            old, new = by_id[event['assertion_id']], by_id[event['superseded_by']]
            if not times[old['id']] < event_time or times[new['id']] != event_time:
                raise ValueError('invalid_supersession_time')
            panel._validated_compiled_evidence(event, case)
            if old['attributed_to'] != new['attributed_to']:
                result['warnings'].append(CROSS_ATTRIBUTION)
            removed.add(old['id'])
            result['eligible_status_events'].append(deepcopy(event))
        active = [edge for edge in eligible if edge['id'] not in removed]
        matches = [edge for edge in active if all(edge[k] == query[k] for k in active_policy['match_fields'])]
        result.update(state='completed', label='unknown', history_assertion_ids=[edge['id'] for edge in eligible],
                      active_assertion_ids=[edge['id'] for edge in active],
                      matching_active_assertion_ids=[edge['id'] for edge in matches])
        if not matches:
            return result
        latest_time = max(times[edge['id']] for edge in matches)
        latest = [edge for edge in matches if times[edge['id']] == latest_time]
        polarities = {edge['polarity'] for edge in latest}
        result['label'] = 'conflicting' if len(polarities) > 1 else 'supported' if 'positive' in polarities else 'refuted'
        result['latest_match_ids'] = [edge['id'] for edge in latest]
        for edge in latest:
            result['paths'].append({'assertion_ids': [edge['id']], 'assertions': [deepcopy(edge)],
                'known_at': edge['known_at'], 'evidence': deepcopy(edge['evidence']),
                'provenance': {'case_id': case['id'], 'source_id': case['source_id'], 'graph_assertion_id': edge['id'],
                               'source_binding_verified': True, 'typed_claim_semantically_verified': False,
                               'content_truth': 'unverified', 'basis_class': 'lookup_of_compiled_candidate_record'}})
        return result
    except (KeyError, TypeError, ValueError, OverflowError):
        result.update(state='unavailable', reason='invalid_typed_graph_or_query', paths=[])
        result['label'] = None
        return result


def inputs():
    value = read(INPUT_PATH)
    if value.get('split') != 'dev' or len(value['cases']) != 24:
        raise ValueError('expected_fixed_24_dev_cases')
    cases = value['cases']
    ids = [c['id'] for c in cases]
    queries = [q['id'] for c in cases for q in c['judgment_queries']]
    if len(set(ids)) != 24 or len(queries) != 96 or len(set(queries)) != 96:
        raise ValueError('expected_distinct_96_queries')
    return cases


def freeze():
    cases = inputs(); policy = load_policy(); graphs = read(COMPILED_PATH)
    if not isinstance(graphs, list):
        raise ValueError('compiled_graph_list_required')
    return {'schema': 'loom.graph_direct_lookup_freeze/1', 'split': 'dev', 'case_count': len(cases), 'query_count': 96,
            'policy': policy, 'policy_sha256': file_hash(POLICY_PATH), 'code_sha256': file_hash(__file__),
            'input_sha256': file_hash(INPUT_PATH), 'compiled_graph_sha256': file_hash(COMPILED_PATH),
            'dependencies_sha256': {file_reference(p): file_hash(p)
                for p in (Path(panel.__file__), Path(panel.safe.__file__), PROTOCOL_PATH,
                          HERE / 'test_new_graph_direct_lookup.py', HERE / 'test_new_graph_direct_lookup_driver.py')},
            'pipeline_mode': PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
            'dev_extraction_outcome_informed_method': True, 'no_graph_promotion': True}


def predict(frozen):
    if frozen != freeze():
        raise ValueError('direct_lookup_freeze_drift')
    cases = inputs(); graphs = read(COMPILED_PATH); by_case = {}
    for graph in graphs:
        ident = graph['case_id']
        if ident in by_case or ident not in {c['id'] for c in cases}:
            raise ValueError('duplicate_or_unknown_compiled_case')
        by_case[ident] = graph
    computed = datetime.now(timezone.utc).isoformat()
    rows = []
    for case in cases:
        for query in case['judgment_queries']:
            row = lookup(case, by_case.get(case['id']), query, frozen['policy'])
            row['computed_at'] = computed
            row['graph_provenance'] = {'compiled_file': file_reference(COMPILED_PATH),
                                     'compiled_graph_sha256': frozen['compiled_graph_sha256'],
                                     'case_id': case['id'], 'model_claim_available_at': 'not_in_compiled_abi',
                                     'full_conversation_extraction': True}
            rows.append(row)
    return {'schema': 'loom.graph_direct_lookup_predictions/1', 'freeze_hash': panel.safe.digest(frozen),
            'computed_at': computed, 'query_count': 96, 'rows': rows,
            'pipeline_mode': PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
            'gold_read_during_prediction': False, 'no_graph_promotion': True}


def score(frozen, predictions):
    if frozen != freeze() or predictions.get('freeze_hash') != panel.safe.digest(frozen):
        raise ValueError('score_freeze_drift')
    cases = inputs(); value = read(GOLD_PATH)
    if value.get('split') != 'dev' or len(value['cases']) != 24:
        raise ValueError('dev_only_gold')
    golds = value['cases']
    expected = {q['id'] for c in cases for q in c['judgment_queries']}
    if len(predictions['rows']) != 96 or {r['query_id'] for r in predictions['rows']} != expected:
        raise ValueError('fixed_prediction_inventory_required')
    primary = panel.score_judgments(golds, predictions['rows'])
    oracle_rows = []
    by_case = {c['id']: c for c in cases}
    for gold in golds:
        oracle = {'case_id': gold['id'], 'state': 'completed', 'source_assertions': gold['source_assertions'],
                  'status_events': gold['status_events']}
        for query in by_case[gold['id']]['judgment_queries']:
            row = lookup(by_case[gold['id']], oracle, query, frozen['policy'])
            row['graph_origin'] = 'authored_oracle_mechanism_only_not_extraction_quality'
            oracle_rows.append(row)
    groups = {}
    for dimension in ('family', 'language'):
        subsets = defaultdict(list)
        for gold in golds:
            subsets[gold[dimension]].append(gold)
        groups[dimension] = {key: panel.score_judgments(subset, [r for r in predictions['rows'] if r['query_id'] in
                                {q['query_id'] for g in subset for q in g['judgments']}])
                             for key, subset in sorted(subsets.items())}
    baselines = {label: panel.score_judgments(golds, [{'query_id': q, 'state': 'completed', 'label': label}
                                                    for q in sorted(expected)]) for label in panel.LABELS}
    return {'schema': 'loom.graph_direct_lookup_score/1', 'split': 'dev', 'freeze_hash': panel.safe.digest(frozen),
            'predictions_hash': panel.safe.digest(predictions), 'gold_sha256': file_hash(GOLD_PATH),
            'actual_model_graph_pipeline': primary, 'groups': groups, 'baselines': baselines,
            'oracle_graph_mechanism_only': panel.score_judgments(golds, oracle_rows), 'oracle_rows': oracle_rows,
            'pipeline_mode': PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
            'information_equivalent_to_raw_prefix_judges': False, 'no_graph_promotion': True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    frozen = commands.add_parser('freeze'); frozen.add_argument('--output', type=Path, required=True)
    predictions = commands.add_parser('predict'); predictions.add_argument('--freeze', type=Path, required=True)
    predictions.add_argument('--output', type=Path, required=True)
    scoring = commands.add_parser('score'); scoring.add_argument('--freeze', type=Path, required=True)
    scoring.add_argument('--predictions', type=Path, required=True); scoring.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'freeze':
            result = freeze()
        elif args.command == 'predict':
            result = predict(read(args.freeze))
        else:
            result = score(read(args.freeze), read(args.predictions))
        panel.write_new(args.output, result)
        print(json.dumps({'saved': args.command, 'paid_calls': 0, 'split': 'dev'}))
        return 0
    except (OSError, KeyError, ValueError, TypeError):
        print(json.dumps({'ok': False, 'error': 'direct_lookup_operation_rejected'})); return 2


if __name__ == '__main__':
    raise SystemExit(main())
