#!/usr/bin/env python3
"""DEV citation-basis ablation of the unchanged frozen direct lookup."""
from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
import argparse
try:
    from . import new_graph_direct_lookup as direct
except ImportError:
    import new_graph_direct_lookup as direct

panel = direct.panel
ROOT = direct.ROOT
HERE = Path(__file__).resolve().parent
DATA = HERE / 'turn_reference_binding_v1' / 'first_results'
GRAPH = DATA / 'compiled_binding_first.json'
PROVENANCE = DATA / 'provenance_first.json'
RAW = DATA / 'raw_model_contents_first.json'
SOURCE_FREEZE = DATA / 'freeze_before_gold.json'
SOURCE_REPLAY_FREEZE = DATA.parent / 'freeze_before_replay.json'
BASE = ROOT / 'docs/research/graph_method_panel_v1/direct_lookup_v1'
PROTOCOL = ROOT / 'docs/research/graph_method_panel_v1/DIRECT_BINDING_ABLATION_PROTOCOL.md'
BASIS = 'turn_id_full_source_copied_by_deterministic_binder'


def verify_binding_receipt():
    receipt = direct.read(SOURCE_FREEZE)
    names = {'binding_inputs_first.json', 'compiled_binding_first.json', 'compiled_original_first.json',
             'provenance_first.json', 'raw_model_contents_first.json'}
    if (receipt.get('split') != 'dev' or receipt.get('gold_loaded_by_driver') is not False or
            set(receipt['files']) != names or receipt['freeze_sha256'] != direct.file_hash(SOURCE_REPLAY_FREEZE)):
        raise ValueError('binding_source_receipt_drift')
    for name, expected in receipt['files'].items():
        if direct.file_hash(DATA / name) != expected:
            raise ValueError('binding_source_artifact_drift')
    replay = direct.read(SOURCE_REPLAY_FREEZE)
    if replay.get('split') != 'dev' or replay.get('live_calls') is not False:
        raise ValueError('binding_source_replay_contract_drift')
    for name, expected in replay['files'].items():
        path = (ROOT / name).resolve()
        if not path.is_relative_to(ROOT.resolve()) or direct.file_hash(path) != expected:
            raise ValueError('binding_source_replay_hash_drift')
    return receipt


def freeze():
    baseline = direct.read(BASE / 'freeze_before_predictions.json')
    if baseline != direct.freeze():
        raise ValueError('baseline_direct_freeze_drift')
    verify_binding_receipt()
    direct.inputs()
    graph = direct.read(GRAPH)
    if not isinstance(graph, list):
        raise ValueError('compiled_graph_list_required')
    files = [Path(__file__), HERE / 'test_graph_direct_binding_ablation.py', PROTOCOL,
             GRAPH, PROVENANCE, RAW, SOURCE_FREEZE, SOURCE_REPLAY_FREEZE, BASE / 'freeze_before_predictions.json',
             BASE / 'first_predictions.json', BASE / 'first_score.json',
             HERE / 'turn_reference_binding_v1/policy.json', HERE / 'turn_reference_binding_v1/binding.py']
    return {'schema': 'loom.graph_direct_binding_ablation_freeze/1', 'split': 'dev',
            'query_count': 96, 'case_count': 24, 'baseline_direct_freeze': baseline,
            'source_binding_basis': BASIS, 'changed_variable': 'compiled_evidence_binding_basis_only',
            'unchanged_lookup_policy': True, 'same_raw_model_responses': True,
            'files_sha256': {direct.file_reference(p): direct.file_hash(p) for p in files},
            'pipeline_mode': direct.PIPELINE_MODE, 'causal_prefix_extraction_verified': False,
            'post_dev_results_informed_ablation': True, 'no_graph_promotion': True}


def predict(frozen):
    if frozen != freeze():
        raise ValueError('binding_ablation_freeze_drift')
    cases = direct.inputs()
    graph = direct.read(GRAPH)
    by_case = {g['case_id']: g for g in graph}
    if len(by_case) != len(graph) or not set(by_case) <= {c['id'] for c in cases}:
        raise ValueError('duplicate_or_unknown_compiled_case')
    provenance = direct.read(PROVENANCE)
    if not isinstance(provenance, list):
        raise ValueError('invalid_binding_provenance')
    computed = datetime.now(timezone.utc).isoformat()
    rows = []
    for case in cases:
        case_provenance = [record for record in provenance if record['case_id'] == case['id']]
        for query in case['judgment_queries']:
            row = direct.lookup(case, by_case.get(case['id']), query, frozen['baseline_direct_freeze']['policy'])
            row['computed_at'] = computed
            row['graph_provenance'] = {'compiled_file': direct.file_reference(GRAPH),
                'compiled_graph_sha256': direct.file_hash(GRAPH), 'case_id': case['id'],
                'source_binding_basis': BASIS, 'binding_provenance_file': direct.file_reference(PROVENANCE),
                'binding_provenance_sha256': direct.file_hash(PROVENANCE),
                'raw_model_contents_file': direct.file_reference(RAW), 'raw_model_contents_sha256': direct.file_hash(RAW),
                'model_claim_available_at': 'not_in_compiled_abi', 'full_conversation_extraction': True}
            # Hints remain hints, including wrong or future ones; these traces do
            # not participate in lookup and cannot prove semantic adequacy.
            row['case_binding_provenance'] = deepcopy(case_provenance)
            rows.append(row)
    return {'schema': 'loom.graph_direct_binding_ablation_predictions/1', 'split': 'dev',
            'freeze_hash': panel.safe.digest(frozen), 'computed_at': computed, 'query_count': 96, 'rows': rows,
            'source_binding_basis': BASIS, 'pipeline_mode': direct.PIPELINE_MODE,
            'causal_prefix_extraction_verified': False, 'gold_read_during_prediction': False,
            'same_raw_model_responses': True, 'no_graph_promotion': True}


def effective_label(row):
    return row.get('label') if row.get('state') == 'completed' and row.get('label') != 'conflicting' else 'unavailable'


def score(frozen, predictions):
    if frozen != freeze() or predictions.get('freeze_hash') != panel.safe.digest(frozen):
        raise ValueError('binding_ablation_score_freeze_drift')
    cases = direct.inputs()
    expected = {q['id'] for case in cases for q in case['judgment_queries']}
    rows = predictions['rows']
    if len(rows) != 96 or {r['query_id'] for r in rows} != expected:
        raise ValueError('fixed_prediction_inventory_required')
    golds = panel.load_dev_gold()
    baseline_predictions = direct.read(BASE / 'first_predictions.json')
    baseline_score = panel.score_judgments(golds, baseline_predictions['rows'])
    saved_baseline = direct.read(BASE / 'first_score.json')
    if baseline_score != saved_baseline['actual_model_graph_pipeline']:
        raise ValueError('saved_baseline_score_drift')
    gold_by_id = {q['query_id']: q['label'] for c in golds for q in c['judgments']}
    base_by_id = {r['query_id']: r for r in baseline_predictions['rows']}
    paired = {key: 0 for key in ('both_correct', 'binding_only_correct', 'original_only_correct', 'both_wrong')}
    changes = []
    for row in rows:
        ident = row['query_id']; new, old = effective_label(row), effective_label(base_by_id[ident])
        nc, oc = new == gold_by_id[ident], old == gold_by_id[ident]
        key = 'both_correct' if nc and oc else 'binding_only_correct' if nc else 'original_only_correct' if oc else 'both_wrong'
        paired[key] += 1
        if new != old:
            changes.append({'query_id': ident, 'gold_dev': gold_by_id[ident], 'original': old, 'binding': new})
    groups = {}
    for dimension in ('family', 'language'):
        groups[dimension] = {}
        for value in sorted({c[dimension] for c in golds}):
            subset = [c for c in golds if c[dimension] == value]
            ids = {q['query_id'] for c in subset for q in c['judgments']}
            groups[dimension][value] = panel.score_judgments(subset, [r for r in rows if r['query_id'] in ids])
    return {'schema': 'loom.graph_direct_binding_ablation_score/1', 'split': 'dev',
            'freeze_hash': panel.safe.digest(frozen), 'predictions_hash': panel.safe.digest(predictions),
            'gold_sha256': direct.file_hash(direct.GOLD_PATH),
            'actual_model_graph_pipeline': panel.score_judgments(golds, rows),
            'original_binding_pipeline': baseline_score, 'paired': paired, 'changes': changes, 'groups': groups,
            'oracle_graph_mechanism_only_preserved': saved_baseline['oracle_graph_mechanism_only'],
            'source_binding_basis': BASIS, 'pipeline_mode': direct.PIPELINE_MODE,
            'causal_prefix_extraction_verified': False, 'information_equivalent_to_raw_prefix_judges': False,
            'same_raw_model_responses': True, 'api_calls_by_author': 0, 'no_graph_promotion': True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    f = commands.add_parser('freeze'); f.add_argument('--output', type=Path, required=True)
    p = commands.add_parser('predict'); p.add_argument('--freeze', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    s = commands.add_parser('score'); s.add_argument('--freeze', type=Path, required=True); s.add_argument('--predictions', type=Path, required=True); s.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    value = freeze() if args.command == 'freeze' else predict(direct.read(args.freeze)) if args.command == 'predict' else score(direct.read(args.freeze), direct.read(args.predictions))
    panel.write_new(args.output, value)


if __name__ == '__main__':
    main()
