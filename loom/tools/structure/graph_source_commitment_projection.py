#!/usr/bin/env python3
"""Opt-in individual-source commitment view of immutable candidate graph."""
from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
import argparse
try:
    from . import graph_direct_binding_ablation as binding
except ImportError:
    import graph_direct_binding_ablation as binding
direct = binding.direct
panel = direct.panel
HERE = Path(__file__).resolve().parent
POLICY = HERE / 'graph_source_commitment_projection_v1/policy.json'
PROTOCOL = direct.ROOT / 'docs/research/graph_method_panel_v1/SOURCE_COMMITMENT_PROJECTION_PROTOCOL.md'
BASE = direct.ROOT / 'docs/research/graph_method_panel_v1/direct_binding_ablation_v1'


def load_policy(path=POLICY):
    policy = direct.read(path)
    expected = {'schema': 'loom.graph_source_commitment_projection_policy/1',
        'source_scope': 'individual_source_commitment', 'supersession_application': 'exact_equal_attributed_to_only',
        'cross_source_events': 'withhold_in_view_preserve_raw_candidate_audit', 'identity_aliases': False,
        'malformed_events': 'delegate_unchanged_to_frozen_lookup', 'typed_assertions': 'unchanged',
        'canonical_mutation': False, 'world_authority': 'unresolved_outside_this_projection'}
    if policy != expected:
        raise ValueError('unsupported_source_commitment_policy')
    return policy


def project(graph, case, policy=None):
    policy = load_policy() if policy is None else policy
    if policy != load_policy():
        raise ValueError('unsupported_source_commitment_policy')
    view = deepcopy(graph)
    audit = {'source_scope': policy['source_scope'], 'raw_candidate_events': [],
             'withheld_events': [], 'applied_in_view': [], 'canonical_mutation': False,
             'world_authority_verified': False, 'typed_assertions_changed': False}
    if not isinstance(view, dict) or view.get('state') != 'completed':
        audit['state'] = 'missing_or_unavailable_graph'
        return view, audit
    assertions, events = view.get('source_assertions'), view.get('status_events')
    if not isinstance(assertions, list) or not isinstance(events, list):
        audit['state'] = 'malformed_delegate_unchanged'
        return view, audit
    audit['raw_candidate_events'] = deepcopy(events)
    try:
        ids = [a['id'] for a in assertions]
        if any(not isinstance(i, str) or not i for i in ids) or len(set(ids)) != len(ids):
            raise ValueError('malformed_ids')
        by_id = {a['id']: a for a in assertions}
    except (KeyError, TypeError, ValueError):
        audit['state'] = 'malformed_delegate_unchanged'
        return view, audit
    retained = []
    for index, event in enumerate(events):
        try:
            old, new = by_id[event['assertion_id']], by_id[event['superseded_by']]
            actors = old['attributed_to'], new['attributed_to']
            if event['status'] != 'superseded' or any(not isinstance(a, str) or not a for a in actors):
                raise ValueError('malformed_event')
            timestamp = panel._time(event['known_at'])
            if not panel._time(old['known_at']) < timestamp or panel._time(new['known_at']) != timestamp:
                raise ValueError('malformed_event_time')
            # Withholding must not sanitize malformed provenance. Delegate such
            # events unchanged so the original cutoff-aware lookup can reject.
            panel._validated_compiled_evidence(event, case)
        except (KeyError, TypeError, ValueError):
            retained.append(event)
            audit['applied_in_view'].append({'candidate_index': index, 'state': 'delegate_malformed_to_lookup'})
            continue
        if actors[0] != actors[1]:
            audit['withheld_events'].append({'candidate_index': index, 'event': deepcopy(event),
                'reason': 'different_attributed_sources_cannot_supersede_individual_commitment',
                'old_attributed_to': actors[0], 'new_attributed_to': actors[1]})
        else:
            retained.append(event)
            audit['applied_in_view'].append({'candidate_index': index, 'state': 'same_attributed_source'})
    view['status_events'] = retained
    audit['state'] = 'completed'
    return view, audit


def freeze():
    baseline = direct.read(BASE / 'freeze_before_predictions.json')
    if baseline != binding.freeze():
        raise ValueError('citation_baseline_freeze_drift')
    policy = load_policy()
    files = [Path(__file__), POLICY, PROTOCOL, HERE / 'test_graph_source_commitment_projection.py',
             BASE / 'freeze_before_predictions.json', BASE / 'first_predictions.json', BASE / 'first_score.json']
    return {'schema': 'loom.graph_source_commitment_projection_freeze/1', 'split': 'dev',
        'query_count': 96, 'policy': policy, 'baseline_binding_freeze': baseline,
        'files_sha256': {direct.file_reference(p): direct.file_hash(p) for p in files},
        'changed_variable': 'supersession_projection_source_scope_only',
        'typed_assertions_unchanged': True, 'raw_candidate_events_preserved': True,
        'pipeline_mode': direct.PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
        'post_dev_results_informed_ablation': True, 'no_graph_promotion': True}


def predict(frozen):
    if frozen != freeze():
        raise ValueError('source_commitment_freeze_drift')
    cases = direct.inputs(); graphs = direct.read(binding.GRAPH)
    by_case = {g['case_id']: g for g in graphs}
    if len(by_case) != len(graphs) or not set(by_case) <= {c['id'] for c in cases}:
        raise ValueError('duplicate_or_unknown_compiled_case')
    baseline = direct.read(BASE / 'first_predictions.json')
    base_by_id = {r['query_id']: r for r in baseline['rows']}
    rows = []; computed = datetime.now(timezone.utc).isoformat()
    for case in cases:
        view, audit = project(by_case.get(case['id']), case, frozen['policy'])
        for query in case['judgment_queries']:
            row = direct.lookup(case, view, query, frozen['baseline_binding_freeze']['baseline_direct_freeze']['policy'])
            row['computed_at'] = computed
            row['graph_provenance'] = deepcopy(base_by_id[query['id']]['graph_provenance'])
            row['graph_provenance'].update(projection_source_scope=frozen['policy']['source_scope'],
                projection_policy_sha256=direct.file_hash(POLICY), canonical_graph_mutated=False)
            row['case_binding_provenance'] = deepcopy(base_by_id[query['id']]['case_binding_provenance'])
            row['projection_audit'] = deepcopy(audit)
            rows.append(row)
    return {'schema': 'loom.graph_source_commitment_projection_predictions/1', 'split': 'dev',
        'freeze_hash': panel.safe.digest(frozen), 'query_count': 96, 'computed_at': computed, 'rows': rows,
        'gold_read_during_prediction': False, 'pipeline_mode': direct.PIPELINE_MODE,
        'causal_prefix_extraction_verified': False, 'source_scope': frozen['policy']['source_scope'],
        'typed_assertions_unchanged': True, 'raw_candidate_events_preserved': True, 'no_graph_promotion': True}


def score(frozen, predictions):
    if frozen != freeze() or predictions.get('freeze_hash') != panel.safe.digest(frozen):
        raise ValueError('source_commitment_score_freeze_drift')
    expected = {q['id'] for c in direct.inputs() for q in c['judgment_queries']}
    rows = predictions['rows']
    if len(rows) != 96 or {r['query_id'] for r in rows} != expected:
        raise ValueError('fixed_prediction_inventory_required')
    golds = panel.load_dev_gold(); baseline = direct.read(BASE / 'first_predictions.json')
    baseline_score = panel.score_judgments(golds, baseline['rows'])
    saved = direct.read(BASE / 'first_score.json')
    if baseline_score != saved['actual_model_graph_pipeline']:
        raise ValueError('citation_baseline_score_drift')
    base_by_id = {r['query_id']: r for r in baseline['rows']}
    labels = {q['query_id']: q['label'] for c in golds for q in c['judgments']}
    paired = {key: 0 for key in ('both_correct', 'projection_only_correct', 'baseline_only_correct', 'both_wrong')}
    changes = []
    for row in rows:
        ident = row['query_id']; new, old = binding.effective_label(row), binding.effective_label(base_by_id[ident])
        nc, oc = new == labels[ident], old == labels[ident]
        key = 'both_correct' if nc and oc else 'projection_only_correct' if nc else 'baseline_only_correct' if oc else 'both_wrong'
        paired[key] += 1
        if new != old:changes.append({'query_id': ident, 'gold_dev': labels[ident], 'baseline': old, 'projection': new})
    audit_by_case = {r['case_id']: r['projection_audit'] for r in rows}
    return {'schema': 'loom.graph_source_commitment_projection_score/1', 'split': 'dev',
        'freeze_hash': panel.safe.digest(frozen), 'predictions_hash': panel.safe.digest(predictions),
        'gold_sha256': direct.file_hash(direct.GOLD_PATH), 'actual_model_graph_pipeline': panel.score_judgments(golds, rows),
        'citation_baseline_pipeline': baseline_score, 'paired': paired, 'changes': changes,
        'raw_event_count': sum(len(a['raw_candidate_events']) for a in audit_by_case.values()),
        'withheld_event_count': sum(len(a['withheld_events']) for a in audit_by_case.values()),
        'projection_audit_by_case': audit_by_case, 'model_extraction_quality_rescored': False,
        'pipeline_mode': direct.PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
        'information_equivalent_to_raw_prefix_judges': False, 'source_scope': frozen['policy']['source_scope'],
        'api_calls_by_author': 0, 'no_graph_promotion': True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    f = commands.add_parser('freeze'); f.add_argument('--output', type=Path, required=True)
    p = commands.add_parser('predict'); p.add_argument('--freeze', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    s = commands.add_parser('score'); s.add_argument('--freeze', type=Path, required=True); s.add_argument('--predictions', type=Path, required=True); s.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    value = freeze() if args.command == 'freeze' else predict(direct.read(args.freeze)) if args.command == 'predict' else score(direct.read(args.freeze), direct.read(args.predictions))
    panel.write_new(args.output, value)


if __name__ == '__main__':main()
