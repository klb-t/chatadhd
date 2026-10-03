"""Secondary byte-cost allocation over immutable DEV retrieval rankings."""
from collections import Counter
from datetime import datetime, timezone
from itertools import combinations
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import explore
import evaluate


def allocate(ranking, costs, budget, policy):
    if policy not in ('rank_prefix', 'rank_skip') or budget < 0:
        raise ValueError('invalid_allocation_policy_or_budget')
    if len(ranking) != len(set(ranking)) or not set(ranking) <= set(costs):
        raise ValueError('ranking_cost_identity_drift')
    if any(not isinstance(v, int) or isinstance(v, bool) or v < 0 for v in costs.values()):
        raise ValueError('invalid_byte_cost')
    selected = []
    used = 0
    for ident in ranking:
        if used + costs[ident] > budget:
            if policy == 'rank_prefix':
                break
            continue
        selected.append(ident)
        used += costs[ident]
    return selected, used


def ceiling(costs, budget, relevant_spans):
    """Gold-conditioned tiny-pool ceiling only; never a method or feature."""
    if len(costs) > 3:
        raise ValueError('only_declared_tiny_dev_pool_ceiling')
    ids = list(costs)
    best = (0, 0, 0, ())
    for k in range(len(ids) + 1):
        for subset in combinations(ids, k):
            used = sum(costs[x] for x in subset)
            if used > budget:
                continue
            covered = sum(e['turn_id'] in subset for e in relevant_spans)
            key = (covered, -used, -len(subset), tuple(subset))
            if key > best:
                best = key
    return {'covered_spans': best[0], 'used_bytes': -best[1], 'selected_turn_ids': list(best[3])}


def freeze():
    paths = [explore.HERE / 'byte_budget.py', explore.HERE / 'test_byte_budget.py',
             explore.DOCS / 'BYTE_BUDGET_PROTOCOL.md', explore.HERE / 'first_scores.json',
             explore.HERE / 'first_results.json', explore.HERE / 'evaluate.py']
    explore.write_new(explore.HERE / 'byte_budget_freeze.json', {
        'created_at': datetime.now(timezone.utc).isoformat(),
        'files_sha256': {str(p.relative_to(explore.ROOT)): explore.sha(p) for p in paths},
        'prior_dev_outcomes_seen': True, 'validation_accessed': False})


def run():
    frozen = json.loads((explore.HERE / 'byte_budget_freeze.json').read_text())
    for name, h in frozen['files_sha256'].items():
        if explore.sha(explore.ROOT / name) != h:
            raise ValueError('secondary_freeze_drift:' + name)
    first = json.loads((explore.HERE / 'first_results.json').read_text())
    scores = json.loads((explore.HERE / 'first_scores.json').read_text())
    score_map = {r['query_id']: r for r in scores['rows']}
    rows = [r | {'score': score_map[r['query_id']]} for r in first['query_targets']]
    costs = {r['query_id']: {c['turn_id']: c['span']['byte_end'] - c['span']['byte_start']
             for c in r['score']['candidates']} for r in rows}
    # Identity/size are verified from actual raw bytes, not copied model quotes.
    prepared = json.loads((explore.BASE / 'prepared_inputs.json').read_text())
    for q in prepared['queries']:
        for c in q['candidates']:
            if costs[q['query_id']][c['turn_id']] != len(c['text'].encode()):
                raise ValueError('raw_source_byte_cost_drift')
    arms = {}
    for union_name, union in first['membership_unions'].items():
        budgets = {ident: sum(costs[ident][turn] for turn in chosen)
                   for ident, chosen in union['selections'].items()}
        if set(budgets) != set(score_map):
            raise ValueError('budget_query_inventory_drift')
        report = {'budgets_source_bytes': budgets, 'budget_source_bytes_sum': sum(budgets.values()),
                  'union': union['union'], 'allocations': {}, 'oracle_recall_ceiling': {}}
        for policy in ('rank_prefix', 'rank_skip'):
            methods = {}
            for method in scores['methods']:
                selected, used = {}, {}
                for r in rows:
                    ident = r['query_id']
                    selected[ident], used[ident] = allocate(r['score']['rankings'][method], costs[ident], budgets[ident], policy)
                methods[method] = evaluate.selected_summary(rows, lambda r: selected[r['query_id']]) | {
                    'source_bytes_used': sum(used.values()), 'source_bytes_unused': sum(budgets.values()) - sum(used.values()),
                    'empty_selection_queries': sum(not v for v in selected.values()),
                    'selections': selected, 'used_bytes_per_query': used}
            report['allocations'][policy] = methods
        oracle = {r['query_id']: ceiling(costs[r['query_id']], budgets[r['query_id']], r['relevant_evidence']) for r in rows}
        report['oracle_recall_ceiling'] = {'covered_spans': sum(v['covered_spans'] for v in oracle.values()),
            'annotated_spans': sum(len(r['relevant_evidence']) for r in rows), 'per_query': oracle,
            'not_a_method': True, 'precision_claim': False, 'does_not_feed_features_or_ranking': True}
        arms[union_name] = report
    differing = []
    deltas = []
    for r in scores['rows']:
        a, b = r['rankings']['minilm_question'], r['rankings']['minilm_reverse']
        if a != b:
            differing.append({'query_id': r['query_id'], 'forward': a, 'reverse': b})
        deltas.extend(abs(c['scores']['minilm_question'] - c['scores']['minilm_reverse']) for c in r['candidates'])
    result = {'schema': 'loom.research.retrieval_byte_budget_results/1', 'created_at': datetime.now(timezone.utc).isoformat(),
        'split': 'dev', 'queries': len(rows), 'methods': len(scores['methods']), 'cost_unit': 'exact_full_raw_turn_utf8_bytes',
        'not_llm_token_or_monetary_cost': True, 'arms': arms,
        'direction_diagnostic': {'equal_complete_ranking_queries': len(rows) - len(differing), 'planned_queries': len(rows),
            'differing': differing, 'candidate_score_pairs': len(deltas), 'max_absolute_score_delta': max(deltas),
            'mean_absolute_score_delta': sum(deltas) / len(deltas), 'semantic_judgment_claim': False},
        'score_sha256': explore.sha(explore.HERE / 'first_scores.json'), 'first_results_sha256': explore.sha(explore.HERE / 'first_results.json'),
        'freeze_sha256': explore.sha(explore.HERE / 'byte_budget_freeze.json'), 'validation_accessed': False, 'paid_calls': 0}
    explore.write_new(explore.HERE / 'byte_budget_results.json', result)


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', choices=('freeze', 'run'))
    args = p.parse_args()
    globals()[args.stage]()
