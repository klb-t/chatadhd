"""Immutable cross-encoder first retrieval scores, gold-bound DEV evaluation."""
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cross_encoder as cross
sys.path.insert(0, str(cross.PANEL))
import evaluate
import byte_budget


def run():
    before = json.loads((cross.HERE / 'freeze_before_gold.json').read_text())
    for name, h in [('first_scores.json', before['scores_sha256']), ('first_outputs.json', before['outputs_sha256'])]:
        if cross.explore.sha(cross.HERE / name) != h:
            raise ValueError('crossencoder_first_measurement_drift')
    frozen = json.loads((cross.HERE / 'freeze_before_scores.json').read_text())
    for name, h in frozen['files_sha256'].items():
        if cross.explore.sha(cross.explore.ROOT / name) != h:
            raise ValueError('crossencoder_dependency_drift')
    scores = json.loads((cross.HERE / 'first_scores.json').read_text())
    first = json.loads((cross.PANEL / 'first_results.json').read_text())
    budgets = json.loads((cross.PANEL / 'byte_budget_results.json').read_text())
    score_map = {r['query_id']: r for r in scores['rows']}
    if len(score_map) != 96 or set(score_map) != {r['query_id'] for r in first['query_targets']}:
        raise ValueError('planned_query_denominator_drift')
    rows = [r | {'score': score_map[r['query_id']]} for r in first['query_targets']]
    methods = {}
    gains = {}
    for method in scores['methods']:
        overall = evaluate.old_evaluation.ranking_summary(rows, method, [1, 2, 3], []) | evaluate.within_query_auc_ap(rows, method)
        groups = {}
        for field in ('language', 'family', 'label'):
            partitions = {}
            for r in rows:
                partitions.setdefault(r[field], []).append(r)
            groups[field] = {name: evaluate.old_evaluation.ranking_summary(items, method, [1, 2, 3], []) |
                evaluate.within_query_auc_ap(items, method) for name, items in sorted(partitions.items())}
        methods[method] = {'overall': overall, 'groups': groups,
            'top1_precision_recall': evaluate.selected_summary(rows, lambda r: r['score']['rankings'][method][:1])}
        if method.startswith('crossencoder_'):
            gains[method] = {}
            for reference in ('lexical_token_cosine', 'learned_minilm_cosine', 'bm25_structured_b0.0', 'minilm_denial'):
                gained, lost = [], []
                for r in rows:
                    relevant = set(r['relevant_turn_ids'])
                    hit = bool(set(r['score']['rankings'][method][:1]) & relevant)
                    ref_hit = bool(set(r['score']['rankings'][reference][:1]) & relevant)
                    if hit and not ref_hit:
                        gained.append(r['query_id'])
                    if ref_hit and not hit:
                        lost.append(r['query_id'])
                gains[method][reference] = {'gained': gained, 'lost': lost, 'gain_count': len(gained), 'loss_count': len(lost)}
    allocations = {}
    for family, budget in budgets['arms'].items():
        allocations[family] = {}
        for policy in ('rank_prefix', 'rank_skip'):
            allocations[family][policy] = {}
            for method in scores['methods']:
                selected, used = {}, {}
                for r in rows:
                    ident = r['query_id']
                    costs = {c['turn_id']: c['span']['byte_end'] - c['span']['byte_start'] for c in r['score']['candidates']}
                    selected[ident], used[ident] = byte_budget.allocate(r['score']['rankings'][method], costs,
                        budget['budgets_source_bytes'][ident], policy)
                allocations[family][policy][method] = evaluate.selected_summary(rows, lambda r: selected[r['query_id']]) | {
                    'source_bytes_used': sum(used.values()), 'source_bytes_budget': budget['budget_source_bytes_sum'],
                    'empty_selection_queries': sum(not v for v in selected.values()), 'selections': selected}
    forward, reverse = 'crossencoder_question_256', 'crossencoder_reverse_256'
    direction = {'equal_complete_ranking_queries': sum(r['score']['rankings'][forward] == r['score']['rankings'][reverse] for r in rows),
        'planned_queries': len(rows), 'equal_top1_queries': sum(r['score']['rankings'][forward][:1] == r['score']['rankings'][reverse][:1] for r in rows),
        'semantic_judgment_claim': False}
    control_deltas = [abs(c['scores']['crossencoder_structured_256'] - c['scores']['crossencoder_structured_128'])
        for r in rows for c in r['score']['candidates']]
    control = {'candidate_pairs': len(control_deltas), 'max_absolute_logit_delta': max(control_deltas),
        'identical_candidate_logit_pairs': sum(v == 0 for v in control_deltas),
        'equal_complete_rank_queries': sum(r['score']['rankings']['crossencoder_structured_256'] == r['score']['rankings']['crossencoder_structured_128'] for r in rows)}
    result = {'schema': 'loom.research.local_crossencoder_results/1', 'created_at': datetime.now(timezone.utc).isoformat(),
        'split': 'dev', 'queries': len(rows), 'new_methods': 5, 'all_compared_methods': len(methods),
        'methods': methods, 'top1_gains_losses': gains, 'byte_budget_allocations': allocations,
        'direction_diagnostic': direction, 'context_cap_control': control,
        'scores_sha256': cross.explore.sha(cross.HERE / 'first_scores.json'),
        'outputs_sha256': cross.explore.sha(cross.HERE / 'first_outputs.json'),
        'inherited_gold_targets_sha256': cross.explore.sha(cross.PANEL / 'first_results.json'),
        'source_judgment_available': False, 'content_truth_accuracy': None, 'validation_accessed': False, 'paid_calls': 0}
    cross.explore.write_new(cross.HERE / 'first_results.json', result)
    print(json.dumps({'queries': len(rows), 'new_methods': 5, 'all_methods': len(methods), 'paid_calls': 0}))


if __name__ == '__main__':
    run()
