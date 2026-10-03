#!/usr/bin/env python3
from datetime import datetime,timezone
import hashlib,json
from pathlib import Path
import zipfile

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
MAIN=ROOT/'loom/tools/structure/retrieval_exploration_v1/first_evidence.zip'
CROSS=ROOT/'loom/tools/structure/retrieval_exploration_v1/cross_encoder_v1/first_evidence.zip'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def archive(path):
    values={};count=0
    with zipfile.ZipFile(path) as z:
        inventory=json.loads(z.read('INVENTORY.json'))['entries']
        for entry in inventory:
            raw=z.read(entry['path']);assert len(raw)==entry['bytes']
            assert hashlib.sha256(raw).hexdigest()==entry['sha256'];count+=1
            if entry['path'] in ('first_scores.json','first_results.json'):values[entry['path']]=json.loads(raw)
    return values,count


def selection_summary(targets,score_by,select):
    fields=dict(queries=0,evidence_queries=0,hit_queries=0,selected_turns=0,relevant_selected_turns=0,
                annotated_relevant_turns=0,annotated_spans=0,covered_spans=0,unknown_queries=0,unknown_queries_with_selected_context=0)
    for target in targets:
        query=target['query_id'];selected=set(select(score_by[query],query));relevant=set(target['relevant_turn_ids'])
        fields['queries']+=1;fields['evidence_queries']+=bool(target['relevant_evidence']);fields['hit_queries']+=bool(selected&relevant)
        fields['selected_turns']+=len(selected);fields['relevant_selected_turns']+=len(selected&relevant)
        fields['annotated_relevant_turns']+=len(relevant);fields['annotated_spans']+=len(target['relevant_evidence'])
        fields['covered_spans']+=sum(e['turn_id'] in selected for e in target['relevant_evidence'])
        fields['unknown_queries']+=target['label']=='unknown';fields['unknown_queries_with_selected_context']+=target['label']=='unknown' and bool(selected)
    fields['turn_precision']=fields['relevant_selected_turns']/fields['selected_turns'] if fields['selected_turns'] else None
    fields['turn_recall']=fields['relevant_selected_turns']/fields['annotated_relevant_turns'] if fields['annotated_relevant_turns'] else None
    fields['span_recall']=fields['covered_spans']/fields['annotated_spans'] if fields['annotated_spans'] else None
    return fields


def same(expected,reported):
    for key,value in expected.items():assert reported[key]==value,(key,value,reported.get(key))


def main():
    freeze={'schema':'loom.philosophy_watch_retrieval_freeze/1','known_at':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in (MAIN,CROSS,Path(__file__),HERE/'PROTOCOL_19_RETRIEVAL_MEMBERSHIP.md')},
            'results_previously_seen':True,'paid_requests':0,'model_inferences':0,'sealed_validation_reads':0}
    with (HERE/'RETRIEVAL_AUDIT_FREEZE_19.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    first,n1=archive(MAIN);cross,n2=archive(CROSS)
    results=first['first_results.json'];targets=results['query_targets'];target_ids={r['query_id'] for r in targets}
    assert len(targets)==96 and len(target_ids)==96
    summary={};candidate_counts=[];future_leaks=0;method_count=0
    for name,payloads in [('initial',first),('joint',cross)]:
        scores=payloads['first_scores.json'];table=payloads['first_results.json'];score_by={r['query_id']:r for r in scores['rows']}
        assert len(score_by)==96 and set(score_by)==target_ids
        assert scores['source_judgment_available'] is False and table['content_truth_accuracy'] is None
        candidate_counts.append(sum(len(r['candidates']) for r in score_by.values()))
        for row in score_by.values():
            cutoff=datetime.fromisoformat(row['as_of'].replace('Z','+00:00'))
            future_leaks+=sum(datetime.fromisoformat(c['known_at'].replace('Z','+00:00'))>cutoff for c in row['candidates'])
            assert row['source_judgment_available'] is False
            ids={c['turn_id'] for c in row['candidates']}
            for ranking in row['rankings'].values():assert len(ranking)==len(ids) and set(ranking)==ids
        for method,values in table['methods'].items():
            expected=selection_summary(targets,score_by,lambda row,q:row['rankings'][method][:1])
            same(expected,values['top1_precision_recall']);summary[name+':'+method]=expected;method_count+=1
    score_by={r['query_id']:r for r in first['first_scores.json']['rows']};controls=0;unions={}
    for name,union in results['membership_unions'].items():
        selections={}
        for query,row in score_by.items():
            expected={ident for channel in union['channels'] for ident in row['rankings'][channel][:1]}
            assert set(union['selections'][query])==expected
            selections[query]=expected
        expected=selection_summary(targets,score_by,lambda row,q:selections[q]);same(expected,union['union'])
        for method,control in union['matched_query_cardinality_controls'].items():
            expected_control=selection_summary(targets,score_by,lambda row,q:row['rankings'][method][:len(selections[q])]);same(expected_control,control);controls+=1
            assert expected_control['selected_turns']==expected['selected_turns']
        unions[name]=expected
    assert candidate_counts==[252,252] and future_leaks==0
    report={'schema':'loom.philosophy_watch_retrieval_audit/1','known_at':datetime.now(timezone.utc).isoformat(),
            'verified_payloads':n1+n2,'queries':96,'candidate_counts':candidate_counts,'future_leaks':future_leaks,
            'method_tables_independently_recounted':method_count,'membership_unions_no_veto_verified':len(unions),
            'matched_cardinality_controls_verified':controls,'unions':unions,
            'headline':{m:summary[m] for m in ('initial:bm25_structured_b0.0','initial:learned_minilm_cosine','joint:crossencoder_structured_256','joint:crossencoder_denial_256')},
            'inherited_gold_targets_independently_derived':False,'model_inferences':0,'paid_requests':0,
            'scope':'retrospective archived ranking/denominator integrity, not new model quality or typed source truth'}
    with (HERE/'RETRIEVAL_AUDIT_RESULTS_19.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps({k:report[k] for k in ('verified_payloads','queries','method_tables_independently_recounted','membership_unions_no_veto_verified','matched_cardinality_controls_verified','future_leaks')}))


if __name__=='__main__':main()
