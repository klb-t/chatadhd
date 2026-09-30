"""Gold enters only evaluation of immutable exploratory DEV retrieval scores."""
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import explore
sys.path.insert(0, str(explore.BASE))
import evaluate_dev as old_evaluation


def selected_summary(rows, selection):
    counts = Counter()
    for row in rows:
        selected = set(selection(row))
        relevant = set(row['relevant_turn_ids'])
        counts['queries'] += 1
        counts['evidence_queries'] += bool(row['relevant_evidence'])
        counts['hit_queries'] += bool(selected & relevant)
        counts['selected_turns'] += len(selected)
        counts['relevant_selected_turns'] += len(selected & relevant)
        counts['annotated_relevant_turns'] += len(relevant)
        counts['annotated_spans'] += len(row['relevant_evidence'])
        counts['covered_spans'] += sum(e['turn_id'] in selected for e in row['relevant_evidence'])
        counts['unknown_queries'] += row['label'] == 'unknown'
        counts['unknown_queries_with_selected_context'] += row['label'] == 'unknown' and bool(selected)
    return dict(counts) | {'turn_precision': counts['relevant_selected_turns'] / counts['selected_turns'] if counts['selected_turns'] else None,
        'turn_recall': counts['relevant_selected_turns'] / counts['annotated_relevant_turns'] if counts['annotated_relevant_turns'] else None,
        'span_recall': counts['covered_spans'] / counts['annotated_spans'] if counts['annotated_spans'] else None,
        'unknown_context_interpretation': 'not_annotated_explicit_edge_evidence_not_necessarily_useless_context'}


def within_query_auc_ap(rows, method):
    """Pair AUC only inside a shared query; BM25 has no cross-query calibration."""
    wins = 0.0
    pairs = auc_queries = evidence_queries = 0
    ap_sum = 0.0
    for row in rows:
        relevant = set(row['relevant_turn_ids'])
        candidates = row['score']['candidates']
        positive = [c['scores'][method] for c in candidates if c['turn_id'] in relevant]
        negative = [c['scores'][method] for c in candidates if c['turn_id'] not in relevant]
        if positive and negative:
            auc_queries += 1
            for a in positive:
                for b in negative:
                    pairs += 1
                    wins += 1 if a > b else .5 if a == b else 0
        if relevant:
            evidence_queries += 1
            found = 0
            for rank, ident in enumerate(row['score']['rankings'][method], 1):
                if ident in relevant:
                    found += 1
                    ap_sum += (found / rank) / len(relevant)
    return {'within_query_pair_auc': wins / pairs if pairs else None, 'concordant_pair_equivalents': wins,
        'positive_negative_candidate_pairs': pairs, 'queries_with_auc_pairs': auc_queries,
        'mean_average_precision': ap_sum / evidence_queries if evidence_queries else None,
        'evidence_queries': evidence_queries, 'cross_query_score_calibration_claim': False}


def union_analysis(rows, channels):
    def choose(row):
        return {ident for channel in channels for ident in row['score']['rankings'][channel][:1]}
    selections = {r['query_id']: sorted(choose(r)) for r in rows}
    card = Counter(len(v) for v in selections.values())
    controls = {}
    for method in rows[0]['score']['rankings']:
        controls[method] = selected_summary(rows, lambda r: r['score']['rankings'][method][:len(selections[r['query_id']])])
    return {'channels': list(channels), 'policy': 'membership_union_each_channel_top1_no_veto_or_trim',
        'union': selected_summary(rows, choose), 'query_cardinality_counts': dict(card),
        'selections': selections, 'matched_query_cardinality_controls': controls}


def evaluate():
    before = json.loads((explore.HERE / 'freeze_before_gold.json').read_text())
    for name, h in [('first_scores.json', before['scores_sha256']),
                    ('first_embedding/vectors.npy', before['vectors_sha256']),
                    ('first_embedding/index.json', before['index_sha256'])]:
        if explore.sha(explore.HERE / name) != h:
            raise ValueError('immutable_first_measurement_drift:' + name)
    scores = json.loads((explore.HERE / 'first_scores.json').read_text())
    prepared = json.loads((explore.BASE / 'prepared_inputs.json').read_text())
    if scores['split'] != 'dev' or scores['queries'] != 96 or scores['prepared_sha256'] != explore.sha(explore.BASE / 'prepared_inputs.json'):
        raise ValueError('frozen_dev_inventory_drift')
    # These calls read only explicitly authorized DEV inputs/gold.
    cases = explore.baseline.adapter.load_dev_inputs()
    gold = explore.baseline.adapter.load_dev_gold()
    case_map = {c['id']: c for c in cases}
    prepared_map = {q['query_id']: q for q in prepared['queries']}
    score_map = {r['query_id']: r for r in scores['rows']}
    if len(score_map) != 96 or set(score_map) != set(prepared_map):
        raise ValueError('planned_query_denominator_drift')
    rows = []
    leaks = outside = events = 0
    for g in gold:
        for j in g['judgments']:
            ident = j['query_id']
            q, score = prepared_map[ident], score_map[ident]
            relevant, counts = old_evaluation.relevant_evidence(case_map[g['id']], q['query'], g, j)
            outside += counts['out_of_prefix_annotations']
            events += counts['status_events_used']
            cutoff = explore.baseline.adapter._time(q['query']['as_of'])
            leaks += sum(explore.baseline.adapter._time(c['known_at']) > cutoff for c in score['candidates'])
            rows.append({'query_id': ident, 'case_id': g['id'], 'family': g['family'], 'language': g['language'],
                'label': j['label'], 'relevant_evidence': relevant,
                'relevant_turn_ids': sorted({e['turn_id'] for e in relevant}), 'score': score})
    if leaks:
        raise ValueError('future_candidate_leakage')
    methods = {}
    gains = {}
    for method in scores['methods']:
        summaries = {}
        for field in ('family', 'language', 'label'):
            groups = {}
            for row in rows:
                groups.setdefault(row[field], []).append(row)
            summaries[field] = {name: old_evaluation.ranking_summary(items, method, [1, 2, 3], []) |
                within_query_auc_ap(items, method) for name, items in sorted(groups.items())}
        methods[method] = {'overall': old_evaluation.ranking_summary(rows, method, [1, 2, 3], []) |
            within_query_auc_ap(rows, method), 'groups': summaries,
            'top1_precision_recall': selected_summary(rows, lambda r: r['score']['rankings'][method][:1])}
        gains[method] = {}
        for reference in ('lexical_token_cosine', 'learned_minilm_cosine'):
            newly, lost = [], []
            for row in rows:
                relevant = set(row['relevant_turn_ids'])
                hit = bool(set(row['score']['rankings'][method][:1]) & relevant)
                ref_hit = bool(set(row['score']['rankings'][reference][:1]) & relevant)
                if hit and not ref_hit:
                    newly.append(row['query_id'])
                if ref_hit and not hit:
                    lost.append(row['query_id'])
            gains[method][reference] = {'gained': newly, 'lost': lost, 'gain_count': len(newly), 'loss_count': len(lost)}
    unions = {}
    for name, channels in [('original3', ('lexical_token_cosine', 'character_3_5_cosine', 'learned_minilm_cosine')),
                          ('bm25_minilm', ('bm25_structured', 'learned_minilm_cosine')),
                          ('multiquery_minilm', ('minilm_forward', 'minilm_denial', 'minilm_reverse')),
                          ('lexical_bm25_minilm', ('lexical_token_cosine', 'bm25_structured', 'learned_minilm_cosine'))]:
        unions[name] = union_analysis(rows, channels)
    result = {'schema': 'loom.research.retrieval_exploration_results/1', 'created_at': datetime.now(timezone.utc).isoformat(),
        'split': 'dev', 'queries': len(rows), 'cases': len(cases), 'label_counts': dict(Counter(r['label'] for r in rows)),
        'score_sha256': explore.sha(explore.HERE / 'first_scores.json'), 'gold_dev_sha256': explore.sha(explore.baseline.adapter.FIXTURE / 'gold_dev.json'),
        'policy_sha256': explore.sha(explore.HERE / 'policy.json'), 'scorer_sha256': explore.sha(__file__),
        'validation_accessed': False, 'first_scores_frozen_before_this_gold_evaluation': True,
        'prior_dev_results_already_seen': True, 'future_candidate_leaks': leaks,
        'out_of_prefix_gold_annotations': outside, 'status_event_references_used': events,
        'methods': methods, 'top1_gains_losses': gains, 'membership_unions': unions,
        'query_targets': [{k: v for k, v in r.items() if k != 'score'} for r in rows],
        'source_judgment_available': False, 'content_truth_accuracy': None, 'paid_calls': 0}
    explore.write_new(explore.HERE / 'first_results.json', result)
    print(json.dumps({'methods': len(methods), 'queries': len(rows), 'future_leaks': leaks, 'paid_calls': 0}))


if __name__ == '__main__':
    evaluate()
