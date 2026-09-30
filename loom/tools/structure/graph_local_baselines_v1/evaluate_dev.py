#!/usr/bin/env python3
"""Gold-bound DEV evaluation after frozen retrieval scores; no validation access."""
from collections import Counter
from datetime import datetime,timezone
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import local_baselines as local
HERE=local.HERE
METHODS=('lexical_token_cosine','character_3_5_cosine','learned_minilm_cosine','nongating_union')


def same_relation(edge,query):
    return all(edge[k]==query[k]for k in('relation','source','target','attributed_to'))


def evidence_key(e):
    return tuple(e[k]for k in('source_id','turn_id','char_start','char_end','byte_start','byte_end'))


def relevant_evidence(case,query,gold_case,gold_judgment):
    """Use annotation references only in evaluation; support/denial remain distinct."""
    if gold_judgment['label']=='unknown':return [],{'out_of_prefix_annotations':0,'status_events_used':0}
    assertions={a['id']:a for a in gold_case['source_assertions']}
    evidence=[];events_used=0;cutoff=local.adapter._time(query['as_of'])
    required_polarity='positive'if gold_judgment['label']=='supported'else'negative'
    for ident in gold_judgment['support_assertion_ids']:
        a=assertions[ident]
        if not same_relation(a,query):raise ValueError('gold_query_assertion_relation_drift')
        if a['polarity']==required_polarity and local.adapter._time(a['known_at'])<=cutoff:
            evidence.extend(a['evidence'])
    if gold_judgment['label']=='refuted':
        for event in gold_case['status_events']:
            old=assertions[event['assertion_id']]
            if event['status']=='superseded'and same_relation(old,query)and local.adapter._time(event['known_at'])<=cutoff:
                evidence.extend(event['evidence']);events_used+=1
    turns={t['id']:t for t in case['turns']};valid={};outside=0
    for e in evidence:
        if e['source_id']!=case['source_id']or e['coordinate_space']!='turn.text':raise ValueError('gold_evidence_source_drift')
        turn=turns[e['turn_id']];text=turn['text']
        if text[e['char_start']:e['char_end']]!=e['quote']or text.encode()[e['byte_start']:e['byte_end']].decode()!=e['quote']:
            raise ValueError('gold_evidence_span_drift')
        if local.adapter._time(turn['known_at'])>cutoff:outside+=1;continue
        valid[evidence_key(e)]=e
    return list(valid.values()),{'out_of_prefix_annotations':outside,'status_events_used':events_used}


def ranking_summary(rows,method,ks,thresholds):
    eligible=matched=0;future_excluded=0
    hits=Counter();covered=Counter();rr=0.;threshold_counts={t:Counter()for t in thresholds}
    for r in rows:
        relevant_turns=set(r['relevant_turn_ids']);relevant_spans=r['relevant_evidence']
        has=bool(relevant_spans);matched+=has;eligible+=len(relevant_spans)
        future_excluded+=r['score']['future_excluded_turn_count']
        ranking=r['score']['rankings'][method]
        first=next((i+1 for i,ident in enumerate(ranking)if ident in relevant_turns),None)
        if first is not None:rr+=1/first
        for k in ks:
            chosen=set(ranking[:k]);hits[k]+=int(bool(chosen&relevant_turns))
            covered[k]+=sum(e['turn_id']in chosen for e in relevant_spans)
        for t in thresholds:
            selected={c['turn_id']for c in r['score']['candidates']if c['scores'][method]is not None and c['scores'][method]>=t}
            c=threshold_counts[t]
            c['selected_candidate_turns']+=len(selected)
            c['relevant_selected_turns']+=len(selected&relevant_turns)
            c['annotated_relevant_turns']+=len(relevant_turns)
            c['annotated_evidence_spans']+=len(relevant_spans)
            c['covered_evidence_spans']+=sum(e['turn_id']in selected for e in relevant_spans)
            c['unknown_queries']+=r['label']=='unknown'
            c['unknown_queries_with_selected_candidates']+=r['label']=='unknown'and bool(selected)
    return {'queries':len(rows),'queries_with_eligible_annotated_evidence':matched,'eligible_annotated_evidence_spans':eligible,
        'source_prefix_future_exclusions_query_turn_pairs':future_excluded,
        'hit_at_k':{str(k):{'hit_queries':hits[k],'denominator_queries_with_evidence':matched,'recall':hits[k]/matched if matched else None}for k in ks},
        'evidence_recall_at_k':{str(k):{'covered_spans':covered[k],'eligible_spans':eligible,'recall':covered[k]/eligible if eligible else None}for k in ks},
        'mean_reciprocal_first_evidence_rank':rr/matched if matched else None,
        'thresholded_relevance':{str(t):dict(c)|{'turn_precision':c['relevant_selected_turns']/c['selected_candidate_turns']if c['selected_candidate_turns']else None,
            'turn_recall':c['relevant_selected_turns']/c['annotated_relevant_turns']if c['annotated_relevant_turns']else None,
            'evidence_recall':c['covered_evidence_spans']/c['annotated_evidence_spans']if c['annotated_evidence_spans']else None,
            'unknown_false_relevance_query_rate':c['unknown_queries_with_selected_candidates']/c['unknown_queries']if c['unknown_queries']else None}for t,c in threshold_counts.items()},
        'interpretation':'conditional_full_turn_evidence_retrieval_not_graph_truth_or_clause_adequacy'}


def evaluate():
    before=json.loads((HERE/'freeze_before_gold.json').read_text())
    for name,h in before['files_sha256'].items():
        if local.file_sha(HERE/name)!=h:raise ValueError('first_score_freeze_drift:'+name)
    policy=json.loads((HERE/'policy.json').read_text())
    prepared=json.loads((HERE/'prepared_inputs.json').read_text());scores=json.loads((HERE/'first_scores.json').read_text())
    cases=local.adapter.load_dev_inputs();gold=local.adapter.load_dev_gold()
    case_map={c['id']:c for c in cases};gold_map={g['id']:g for g in gold}
    queries={q['query_id']:q for q in prepared['queries']}
    score_map={r['query_id']:r for r in scores['rows']}
    if len(queries)!=96 or len(score_map)!=96 or set(queries)!=set(score_map):raise ValueError('matched96queryinventory')
    rows=[];outside=events=leaks=0
    for g in gold:
        for judgment in g['judgments']:
            ident=judgment['query_id'];q=queries[ident];s=score_map[ident]
            relevant,counts=relevant_evidence(case_map[g['id']],q['query'],g,judgment)
            outside+=counts['out_of_prefix_annotations'];events+=counts['status_events_used']
            cutoff=local.adapter._time(q['query']['as_of'])
            leaks+=sum(local.adapter._time(c['known_at'])>cutoff for c in s['candidates'])
            rows.append({'query_id':ident,'case_id':g['id'],'family':g['family'],'language':g['language'],'label':judgment['label'],
                'relevant_evidence':relevant,'relevant_turn_ids':sorted({e['turn_id']for e in relevant}),'score':s})
    if leaks:raise ValueError('future_candidate_leakage')
    judgments={};retrieval={};predictions={}
    always=[{'query_id':r['query_id'],'state':'completed','label':'unknown'}for r in rows]
    judgments['always_unknown']=local.adapter.score_judgments(gold,always);predictions['always_unknown']=always
    for method in METHODS:
        retrieval[method]={'overall':ranking_summary(rows,method,policy['ranking_k'],policy['thresholds']),'groups':{}}
        for field in('family','language','label'):
            groups={}
            for r in rows:groups.setdefault(r[field],[]).append(r)
            retrieval[method]['groups'][field]={name:ranking_summary(items,method,policy['ranking_k'],policy['thresholds'])for name,items in sorted(groups.items())}
        judgments[method]={}
        for t in policy['thresholds']:
            pred=[{'query_id':r['query_id'],'state':'completed'if r['score']['maximum_scores'][method]is not None else'unavailable',
                'label':local.naive_label(r['score']['maximum_scores'][method],t)}for r in rows]
            predictions[method+'/'+str(t)]=pred
            report=local.adapter.score_judgments(gold,pred);groups={}
            for field in('family','language'):
                parts={}
                for g in gold:parts.setdefault(g[field],[]).append(g)
                groups[field]={}
                for name,items in sorted(parts.items()):
                    ids={q['query_id']for g in items for q in g['judgments']}
                    groups[field][name]=local.adapter.score_judgments(items,[p for p in pred if p['query_id']in ids])
            judgments[method][str(t)]=report|{'groups':groups,'interpretation':'naive_diagnostic_only_not_semantic_or_truth_judgment'}
    result={'schema':'loom.research.graph_local_dev_results/1','split':'dev','queries':len(rows),'cases':len(cases),
        'protocol_sha256':local.file_sha(HERE/'PROTOCOL.md'),'policy_sha256':local.file_sha(HERE/'policy.json'),
        'scorer_sha256':local.file_sha(__file__),'score_artifact_sha256':local.file_sha(HERE/'first_scores.json'),
        'gold_dev_sha256':local.file_sha(local.adapter.FIXTURE/'gold_dev.json'),'unchanged_adapter_sha256':local.file_sha(local.adapter.__file__),
        'gold_evaluation_started_after_first_score_freeze':True,'validation_accessed':False,
        'label_counts':dict(Counter(r['label']for r in rows)),'source_judgment_available':False,
        'future_candidate_leaks':leaks,'out_of_prefix_gold_annotations':outside,'query_status_event_references_used':events,
        'retrieval':retrieval,'naive_judgments':judgments,'query_evidence_targets':[dict(r,score=None)for r in rows],
        'no_graph_promotion':True,'content_truth_accuracy':None}
    local.write_new(HERE/'first_results.json',result)
    local.write_new(HERE/'naive_predictions.json',{'schema':'loom.research.graph_local_naive_predictions/1','split':'dev','predictions':predictions})
    print('DEV96first evaluation preserved; never infer refutation from low/missing similarity.')


if __name__=='__main__':evaluate()
