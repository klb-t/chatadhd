#!/usr/bin/env python3
"""Post-first-run DEV candidate membership diagnostic; raw scores untouched."""
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import local_baselines as local
HERE=local.HERE
CHANNELS=('lexical_token_cosine','character_3_5_cosine','learned_minilm_cosine')
ALL=(*CHANNELS,'nongating_union')


def selection_summary(selected,rows):
    total=relevant_total=selected_relevant=evidence_total=covered=known=hits=0;costs=[]
    for row in rows:
        ids=set(selected[row['query_id']]);targets=set(row['relevant_turn_ids'])
        spans=row['relevant_evidence'];has=bool(spans)
        costs.append(len(ids));total+=len(ids);selected_relevant+=len(ids&targets);relevant_total+=len(targets)
        known+=has;hits+=bool(ids&targets);evidence_total+=len(spans);covered+=sum(e['turn_id']in ids for e in spans)
    return {'queries':len(rows),'selected_unique_turns':total,'selected_relevant_turns':selected_relevant,
        'annotated_relevant_turns':relevant_total,'turn_precision':selected_relevant/total if total else None,
        'turn_recall':selected_relevant/relevant_total if relevant_total else None,
        'covered_evidence_spans':covered,'eligible_evidence_spans':evidence_total,'evidence_recall':covered/evidence_total if evidence_total else None,
        'hit_queries':hits,'queries_with_annotated_evidence':known,'hit_rate':hits/known if known else None,
        'cost_per_query':{'minimum':min(costs)if costs else None,'maximum':max(costs)if costs else None,'mean':total/len(rows)if rows else None},
        'interpretation':'candidate_membership_at_reported_context_cost_not_graph_truth'}


def run():
    first=json.loads((HERE/'first_results.json').read_text());raw=json.loads((HERE/'first_scores.json').read_text())
    score_map={s['query_id']:s for s in raw['rows']};rows=first['query_evidence_targets'];arms={};memberships={}
    for k in(1,3,5):
        selected={};per_query=[]
        for row in rows:
            q=score_map[row['query_id']];by_channel={m:q['rankings'][m][:k]for m in CHANNELS}
            chosen=set(ident for ids in by_channel.values()for ident in ids);selected[row['query_id']]=chosen
            per_query.append({'query_id':row['query_id'],'selected_turn_ids':sorted(chosen),'unique_count':len(chosen),
                'candidate_pool_count':len(q['candidates']),'channel_top_k':by_channel})
        arm={'set_union':selection_summary(selected,rows),'primary_at_k':{},'primary_at_3k':{},'primary_at_matched_actual_cardinality':{}}
        for method in ALL:
            rankings={r['query_id']:score_map[r['query_id']]['rankings'][method]for r in rows}
            arm['primary_at_k'][method]=selection_summary({ident:rank[:k]for ident,rank in rankings.items()},rows)
            arm['primary_at_3k'][method]=selection_summary({ident:rank[:3*k]for ident,rank in rankings.items()},rows)
            arm['primary_at_matched_actual_cardinality'][method]=selection_summary({ident:rank[:len(selected[ident])]for ident,rank in rankings.items()},rows)
        arms[str(k)]=arm;memberships[str(k)]=per_query
    dropped=[]
    for row in rows:
        q=score_map[row['query_id']];targets=set(row['relevant_turn_ids']);max_top=q['rankings']['nongating_union'][:1]
        if targets and not(targets&set(max_top)):
            rescuers={m:q['rankings'][m][:1]for m in CHANNELS if targets&set(q['rankings'][m][:1])}
            if rescuers:
                dropped.append({'query_id':row['query_id'],'case_id':row['case_id'],'family':row['family'],'label':row['label'],
                    'relevant_turn_ids':sorted(targets),'max_score_top1':max_top,'individual_channel_top1_hits':rescuers,
                    'candidate_scores':q['candidates']})
    result={'schema':'loom.research.graph_local_set_union_secondary/1','split':'dev','evaluation_status':'development_inspired_after_primary_results',
        'protocol_sha256':local.file_sha(HERE/'SECONDARY_SET_UNION_PROTOCOL.md'),'code_sha256':local.file_sha(__file__),
        'original_score_sha256':local.file_sha(HERE/'first_scores.json'),'original_results_sha256':local.file_sha(HERE/'first_results.json'),
        'arms':arms,'membership':memberships,'individual_hits_lost_by_shared_max_top1':dropped,'validation_accessed':False,'no_graph_judgments_generated':True}
    local.write_new(HERE/'secondary_set_union_results.json',result)
    print('Secondary DEVset-union first results preserved; context budgets explicitly matched.')


if __name__=='__main__':run()
