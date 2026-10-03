"""Read-only independent DEV first-artifact audit; imports no project scorers."""
from collections import Counter
from datetime import datetime, timezone
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import time
import zipfile

ROOT=Path(__file__).resolve().parents[3]
PANEL=ROOT/'loom/tools/structure/retrieval_exploration_v1'
CROSS=PANEL/'cross_encoder_v1'
DOCS=Path(__file__).resolve().parent
FIXTURE=ROOT/'loom/tests/fixtures/research/graph_methods_panel_v1'
CACHE=ROOT.parent/'research-recovery/cross-encoder-msmarco-v1'
TOLERANCE=1e-6


def raw_sha(raw):return hashlib.sha256(raw).hexdigest()
def sha(path):return raw_sha(Path(path).read_bytes())
def read(path):return json.loads(Path(path).read_text())
def canonical(value):return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()
def digest(value):return raw_sha(canonical(value))
def stamp(value):return datetime.fromisoformat(value.replace('Z','+00:00'))
def require(condition,message):
    if not condition:raise AssertionError(message)


def equal(actual,expected,path=''):
    if isinstance(expected,float):
        require(isinstance(actual,(int,float)) and math.isclose(actual,expected,rel_tol=1e-12,abs_tol=1e-12),path)
    elif isinstance(expected,dict):
        for key,value in expected.items():
            require(key in actual,path+'.'+key);equal(actual[key],value,path+'.'+key)
    elif isinstance(expected,list):
        require(len(actual)==len(expected),path+'.length')
        for i,(a,b) in enumerate(zip(actual,expected)):equal(a,b,path+f'[{i}]')
    else:require(actual==expected,path)


def source_targets(cases,gold,queries):
    targets=[];event_count=excluded=0
    for g in gold:
        case=cases[g['id']];turns={t['id']:t for t in case['turns']}
        assertions={a['id']:a for a in g['source_assertions']}
        for judgment in g['judgments']:
            query=queries[judgment['query_id']]['query'];cutoff=stamp(query['as_of']);evidence=[]
            matching=lambda a:tuple(a[k] for k in ('relation','source','target','attributed_to'))==tuple(query[k] for k in ('relation','source','target','attributed_to'))
            if judgment['label']!='unknown':
                polarity={'supported':'positive','refuted':'negative'}[judgment['label']]
                for ident in judgment['support_assertion_ids']:
                    assertion=assertions[ident];require(matching(assertion),'assertion tuple mismatch')
                    if assertion['polarity']==polarity and stamp(assertion['known_at'])<=cutoff:evidence.extend(assertion['evidence'])
                if judgment['label']=='refuted':
                    for event in g['status_events']:
                        if event['status']=='superseded' and matching(assertions[event['assertion_id']]) and stamp(event['known_at'])<=cutoff:
                            evidence.extend(event['evidence']);event_count+=1
            unique={}
            for item in evidence:
                turn=turns[item['turn_id']];text=turn['text']
                require(item['source_id']==case['source_id'] and item['coordinate_space']=='turn.text','source evidence identity')
                require(text[item['char_start']:item['char_end']]==item['quote'],'character evidence coordinates')
                require(text.encode()[item['byte_start']:item['byte_end']].decode()==item['quote'],'UTF8 evidence coordinates')
                if stamp(turn['known_at'])>cutoff:excluded+=1;continue
                key=tuple(item[k] for k in ('source_id','turn_id','char_start','char_end','byte_start','byte_end'))
                unique[key]=item
            targets.append({'query_id':judgment['query_id'],'case_id':g['id'],'family':g['family'],'language':g['language'],
                'label':judgment['label'],'relevant_evidence':list(unique.values()),'relevant_turn_ids':sorted({e['turn_id'] for e in unique.values()})})
    return targets,event_count,excluded


def summary(rows,method):
    total=len(rows);positive=[r for r in rows if r['relevant_evidence']]
    span_count=sum(len(r['relevant_evidence']) for r in rows)
    hits={};spans={};rr=[];aps=[];comparisons=[]
    for k in (1,2,3):
        chosen=[set(r['score']['rankings'][method][:k]) for r in rows]
        count=sum(bool(set(r['relevant_turn_ids']) & selection) for r,selection in zip(rows,chosen))
        covered=sum(sum(e['turn_id'] in selection for e in r['relevant_evidence']) for r,selection in zip(rows,chosen))
        hits[str(k)]={'hit_queries':count,'denominator_queries_with_evidence':len(positive),'recall':count/len(positive) if positive else None}
        spans[str(k)]={'covered_spans':covered,'eligible_spans':span_count,'recall':covered/span_count if span_count else None}
    auc_queries=0
    for r in rows:
        relevant=set(r['relevant_turn_ids']);ranking=r['score']['rankings'][method]
        evidence_ranks=[rank for rank,ident in enumerate(ranking,1) if ident in relevant]
        if relevant:
            rr.append(Fraction(1,min(evidence_ranks)) if evidence_ranks else Fraction(0))
            aps.append(sum((Fraction(i,rank) for i,rank in enumerate(evidence_ranks,1)),Fraction(0))/len(relevant))
        candidates=r['score']['candidates'];positive_scores=[c['scores'][method] for c in candidates if c['turn_id'] in relevant]
        negative_scores=[c['scores'][method] for c in candidates if c['turn_id'] not in relevant]
        if positive_scores and negative_scores:
            auc_queries+=1
            comparisons.extend(Fraction(1) if a>b else Fraction(1,2) if a==b else Fraction(0) for a in positive_scores for b in negative_scores)
    return {'queries':total,'queries_with_eligible_annotated_evidence':len(positive),'eligible_annotated_evidence_spans':span_count,
        'source_prefix_future_exclusions_query_turn_pairs':sum(r['score']['future_excluded_turn_count'] for r in rows),
        'hit_at_k':hits,'evidence_recall_at_k':spans,'mean_reciprocal_first_evidence_rank':float(sum(rr,Fraction(0))/len(positive)) if positive else None,
        'mean_average_precision':float(sum(aps,Fraction(0))/len(positive)) if positive else None,
        'within_query_pair_auc':float(sum(comparisons,Fraction(0))/len(comparisons)) if comparisons else None,
        'concordant_pair_equivalents':float(sum(comparisons,Fraction(0))),
        'positive_negative_candidate_pairs':len(comparisons),'queries_with_auc_pairs':auc_queries,'evidence_queries':len(positive)}


def selection_summary(rows,selections):
    counts={'queries':len(rows),'evidence_queries':sum(bool(r['relevant_evidence']) for r in rows),
        'hit_queries':0,'selected_turns':0,'relevant_selected_turns':0,'annotated_relevant_turns':0,
        'annotated_spans':0,'covered_spans':0,'unknown_queries':0,'unknown_queries_with_selected_context':0}
    for r in rows:
        selected=set(selections[r['query_id']]);relevant=set(r['relevant_turn_ids'])
        counts['hit_queries']+=bool(selected & relevant);counts['selected_turns']+=len(selected)
        counts['relevant_selected_turns']+=len(selected & relevant);counts['annotated_relevant_turns']+=len(relevant)
        counts['annotated_spans']+=len(r['relevant_evidence']);counts['covered_spans']+=sum(e['turn_id'] in selected for e in r['relevant_evidence'])
        counts['unknown_queries']+=r['label']=='unknown';counts['unknown_queries_with_selected_context']+=r['label']=='unknown' and bool(selected)
    counts['turn_precision']=counts['relevant_selected_turns']/counts['selected_turns'] if counts['selected_turns'] else None
    counts['turn_recall']=counts['relevant_selected_turns']/counts['annotated_relevant_turns'] if counts['annotated_relevant_turns'] else None
    counts['span_recall']=counts['covered_spans']/counts['annotated_spans'] if counts['annotated_spans'] else None
    return counts


def audit():
    archive_meta=read(CROSS/'FIRST_ARCHIVE.json')
    require(sha(CROSS/'first_evidence.zip')==archive_meta['archive_sha256'],'archive SHA')
    with zipfile.ZipFile(CROSS/'first_evidence.zip') as archive:
        require(set(archive.namelist())=={e['path'] for e in archive_meta['entries']}|{'INVENTORY.json'} and len(archive.namelist())==len(archive_meta['entries'])+1,'archive members')
        require(json.loads(archive.read('INVENTORY.json'))['entries']==archive_meta['entries'],'archive embedded inventory')
        raw={entry['path']:archive.read(entry['path']) for entry in archive_meta['entries']}
        for entry in archive_meta['entries']:
            require(len(raw[entry['path']])==entry['bytes'] and raw_sha(raw[entry['path']])==entry['sha256'],'archived raw payload')
    outputs=json.loads(raw['first_outputs.json']);scores=json.loads(raw['first_scores.json']);results=json.loads(raw['first_results.json'])
    frozen=read(CROSS/'freeze_before_scores.json')
    for name,expected in frozen['files_sha256'].items():require(sha(ROOT/name)==expected,'frozen dependency '+name)
    before_gold=read(CROSS/'freeze_before_gold.json')
    require(raw_sha(raw['first_outputs.json'])==before_gold['outputs_sha256'] and raw_sha(raw['first_scores.json'])==before_gold['scores_sha256'],'before gold freeze')
    prepared=read(ROOT/'loom/tools/structure/graph_local_baselines_v1/prepared_inputs.json')
    inputs=read(FIXTURE/'inputs_dev.json');gold=read(FIXTURE/'gold_dev.json')
    require(inputs['split']==gold['split']==prepared['split']==scores['split']==results['split']=='dev','only DEV')
    cases={c['id']:c for c in inputs['cases']};queries={q['query_id']:q for q in prepared['queries']}
    require(len(queries)==len(prepared['queries'])==96 and len(cases)==24,'planned inventory')
    require(sha(FIXTURE/'inputs_dev.json')==prepared['inputs_sha256'],'original input hash')
    require(sha(ROOT/'loom/tools/structure/graph_local_baselines_v1/prepared_inputs.json')==scores['prepared_sha256'],'prepared hash')
    original=read(PANEL/'first_results.json')
    targets,event_count,outside=source_targets(cases,gold['cases'],queries)
    equal(original['query_targets'],targets,'source-derived gold targets')
    require(sha(FIXTURE/'gold_dev.json')==original['gold_dev_sha256'],'gold DEV identity')
    require(event_count==original['status_event_references_used'] and outside==original['out_of_prefix_gold_annotations'],'gold event counts')
    policy=read(CROSS/'policy.json');templates=read(PANEL/'representation_templates_v2.json')
    score_map={s['query_id']:s for s in scores['rows']};require(set(score_map)==set(queries),'score query inventory')
    expected_bindings=[];pair_use=Counter();candidate_count=0;future_exclusions=0;rank_checks=0
    for query_id,q in queries.items():
        case=cases[q['case_id']];source_query=next(x for x in case['judgment_queries'] if x['id']==query_id)
        prefix=[t for t in case['turns'] if stamp(t['known_at'])<=stamp(source_query['as_of'])]
        payload={'case_id':case['id'],'source_id':case['source_id'],'turns':prefix,'node_inventory':case['node_inventory'],'query':source_query}
        require(payload==q['prefix_payload'] and digest(payload)==q['prefix_sha256'],'causal raw prefix')
        require(source_query==q['query'] and q['language']==case['language'],'source query binding')
        excluded=len(case['turns'])-len(prefix);require(excluded==q['future_excluded_turn_count'],'future exclusions')
        future_exclusions+=excluded
        require([c['turn_id'] for c in q['candidates']]==[t['id'] for t in prefix],'candidate prefix inventory')
        s=score_map[query_id];require(s['prefix_sha256']==q['prefix_sha256'],'score prefix identity')
        require([c['turn_id'] for c in s['candidates']]==[t['id'] for t in prefix],'score candidates')
        nodes={n['id']:n for n in case['node_inventory']};source=nodes[source_query['source']]['text'];target=nodes[source_query['target']]['text']
        structured=f"attribution: {source_query['attributed_to']}\nrelation: {source_query['relation']}\nsource: {source}\ntarget: {target}"
        require(q['query_text']==structured and q['query_text_sha256']==raw_sha(structured.encode()),'query source text')
        values={'query_text':structured,'actor':source_query['attributed_to'],'relation':source_query['relation'],'source_text':source,'target_text':target}
        views={'structured':structured,**{key:value.format_map(values) for key,value in templates['language_relation_templates'][q['language']][source_query['relation']].items()}}
        for order,(candidate,turn,scored) in enumerate(zip(q['candidates'],prefix,s['candidates'])):
            candidate_count+=1
            representation=f"speaker: {turn['speaker']}\ntext: {turn['text']}"
            require(candidate['text']==turn['text'] and candidate['representation_text']==representation and candidate['speaker']==turn['speaker'],'full source representation')
            for key,expected in {'source_id':case['source_id'],'turn_id':turn['id'],'known_at':turn['known_at'],'text_sha256':raw_sha(turn['text'].encode()),'eligible_order':order}.items():
                require(candidate[key]==scored[key]==expected,'candidate native binding '+key)
            require(candidate['span']==scored['span']=={'coordinate_space':'turn.text','char_start':0,'char_end':len(turn['text']),'byte_start':0,'byte_end':len(turn['text'].encode())},'full turn span')
            for variant,cap in [(v,policy['primary_max_tokens']) for v in policy['query_variants']]+[(policy['control_query_variant'],policy['control_max_tokens'])]:
                key=raw_sha(json.dumps([views[variant],representation,cap],ensure_ascii=False,separators=(',',':')).encode());pair_use[key]+=1
                expected_bindings.append({'query_id':query_id,'turn_id':turn['id'],'source_id':case['source_id'],'known_at':turn['known_at'],'prefix_sha256':q['prefix_sha256'],'pair_key':key,'variant':variant,'max_tokens':cap})
                o=outputs['outputs'][key];require(o['query_text']==views[variant] and o['candidate_text']==representation and o['max_tokens']==cap,'raw pair text/cap')
                require(o['query_sha256']==raw_sha(views[variant].encode()) and o['candidate_sha256']==raw_sha(representation.encode()),'raw text hashes')
                method=f'crossencoder_{variant}_{cap}'
                require(scored['crossencoder_pair_refs'][method]==key and scored['scores'][method]==o['raw_logit'],'raw logit binding')
                require(isinstance(o['raw_logit'],(int,float)) and not isinstance(o['raw_logit'],bool) and math.isfinite(o['raw_logit']) and o['output_activation']=='identity','finite raw identity logit')
        for method in scores['methods']:
            ranked=sorted(s['candidates'],key=lambda c:(c['scores'][method] is None,-c['scores'][method] if c['scores'][method] is not None else 0,c['eligible_order'],c['turn_id']))
            require([c['turn_id'] for c in ranked if c['scores'][method] is not None]==s['rankings'][method],'ranking '+method)
            rank_checks+=1
    equal(outputs['source_bindings'],expected_bindings,'source bindings')
    require(set(pair_use)==set(outputs['outputs']) and len(pair_use)==1062,'unique pair inventory')
    require(len(expected_bindings)==outputs['query_candidate_variant_bindings']==1260 and candidate_count==252,'binding denominator')
    manifest=read(CROSS/'model_manifest.json')
    for artifact in manifest['artifacts']:
        path=CACHE/artifact['file'];require(path.stat().st_size==artifact['bytes'] and sha(path)==artifact['sha256'],'model artifact '+artifact['file'])
    require(outputs['model_id']==manifest['model_id']==policy['model_id'] and outputs['revision']==manifest['revision']==policy['revision'],'model instrument identity')
    sys.path.insert(0,str(ROOT/'loom/tools/structure/local_embedding_panel_v1/runtime_packages'))
    import numpy as np
    import onnxruntime as ort
    from tokenizers import Tokenizer
    tokenizer=Tokenizer.from_file(str(CACHE/'tokenizer.json'))
    options=ort.SessionOptions();options.intra_op_num_threads=2;options.inter_op_num_threads=1;options.execution_mode=ort.ExecutionMode.ORT_SEQUENTIAL
    session=ort.InferenceSession(str(CACHE/'onnx/model.onnx'),sess_options=options,providers=['CPUExecutionProvider'])
    maximum_delta=0.0;exact_logits=truncated=0;max_untruncated=0;begin=time.perf_counter()
    for key,o in outputs['outputs'].items():
        tokenizer.no_truncation();tokenizer.no_padding();complete=tokenizer.encode(o['query_text'],o['candidate_text'],add_special_tokens=True)
        tokenizer.enable_truncation(max_length=o['max_tokens'],strategy='longest_first',direction='right')
        encoded=tokenizer.encode(o['query_text'],o['candidate_text'],add_special_tokens=True)
        tensor_lists={'input_ids':encoded.ids,'attention_mask':encoded.attention_mask,'token_type_ids':encoded.type_ids}
        for name,values in tensor_lists.items():require(raw_sha(struct.pack('<'+'q'*len(values),*values))==o['input_tensor_sha256'][name],'token tensor hash '+name)
        require(o['untruncated_tokens']==len(complete.ids) and o['encoded_tokens']==len(encoded.ids) and o['truncated']==(len(complete.ids)>o['max_tokens']),'token count/truncation')
        max_untruncated=max(max_untruncated,len(complete.ids));truncated+=o['truncated']
        feed={i.name:np.asarray([tensor_lists[i.name]],dtype=np.int64) for i in session.get_inputs()}
        returned=session.run(None,feed)[0];require(tuple(returned.shape)==(1,1),'raw output shape')
        new=float(returned[0,0]);delta=abs(new-o['raw_logit']);maximum_delta=max(maximum_delta,delta);exact_logits+=new==o['raw_logit']
        require(math.isfinite(new) and delta<=TOLERANCE,'raw local logit discrepancy')
    local_seconds=time.perf_counter()-begin
    require(truncated==outputs['truncated_unique_pairs']==0,'hidden truncation')
    rows=[r|{'score':score_map[r['query_id']]} for r in targets];method_reports={};group_checks=0
    for method in scores['methods']:
        overall=summary(rows,method);equal(results['methods'][method]['overall'],overall,method+'.overall')
        selections={r['query_id']:r['score']['rankings'][method][:1] for r in rows}
        precision=selection_summary(rows,selections);equal(results['methods'][method]['top1_precision_recall'],precision,method+'.top1')
        groups={}
        for field in ('language','family','label'):
            groups[field]={}
            for value in sorted({r[field] for r in rows}):
                part=[r for r in rows if r[field]==value];report=summary(part,method)
                equal(results['methods'][method]['groups'][field][value],report,method+'.'+field+'.'+value)
                groups[field][value]=report;group_checks+=1
        method_reports[method]={'overall':overall,'top1_precision_recall':precision,'groups':groups}
    gains={}
    for method,record in results['top1_gains_losses'].items():
        gains[method]={}
        for reference,expected in record.items():
            gained=[];lost=[]
            for r in rows:
                relevant=set(r['relevant_turn_ids']);rank=r['score']['rankings']
                hit=bool(relevant & set(rank[method][:1]));baseline=bool(relevant & set(rank[reference][:1]))
                if hit and not baseline:gained.append(r['query_id'])
                if baseline and not hit:lost.append(r['query_id'])
            result={'gained':gained,'lost':lost,'gain_count':len(gained),'loss_count':len(lost)}
            equal(expected,result,method+'.'+reference+'.paired');gains[method][reference]=result
    budgets=read(PANEL/'byte_budget_results.json');allocation_checks=0
    for family,arm in budgets['arms'].items():
        union=original['membership_unions'][family]
        for r in rows:
            query_id=r['query_id'];costs={c['turn_id']:c['span']['byte_end']-c['span']['byte_start'] for c in r['score']['candidates']}
            expected=set(ident for channel in union['channels'] for ident in r['score']['rankings'][channel][:1])
            require(set(union['selections'][query_id])==expected,'budget source union')
            require(sum(costs[t] for t in expected)==arm['budgets_source_bytes'][query_id],'budget raw bytes')
        for policy_name in ('rank_prefix','rank_skip'):
            for method in scores['methods']:
                selections={};total_used=0
                for r in rows:
                    query_id=r['query_id'];costs={c['turn_id']:c['span']['byte_end']-c['span']['byte_start'] for c in r['score']['candidates']};left=arm['budgets_source_bytes'][query_id];chosen=[]
                    for ident in r['score']['rankings'][method]:
                        if costs[ident]>left:
                            if policy_name=='rank_prefix':break
                            continue
                        chosen.append(ident);left-=costs[ident]
                    selections[query_id]=chosen;total_used+=arm['budgets_source_bytes'][query_id]-left
                measured=selection_summary(rows,selections)|{'selections':selections,'source_bytes_used':total_used,'source_bytes_budget':sum(arm['budgets_source_bytes'].values()),'empty_selection_queries':sum(not v for v in selections.values())}
                equal(results['byte_budget_allocations'][family][policy_name][method],measured,family+'.'+policy_name+'.'+method);allocation_checks+=1
    direction={'equal_complete_ranking_queries':sum(s['rankings']['crossencoder_question_256']==s['rankings']['crossencoder_reverse_256'] for s in score_map.values()),'equal_top1_queries':sum(s['rankings']['crossencoder_question_256'][:1]==s['rankings']['crossencoder_reverse_256'][:1] for s in score_map.values()),'planned_queries':len(rows)}
    equal(results['direction_diagnostic'],direction,'direction diagnostic')
    deltas=[abs(c['scores']['crossencoder_structured_256']-c['scores']['crossencoder_structured_128']) for s in score_map.values() for c in s['candidates']]
    control={'candidate_pairs':len(deltas),'max_absolute_logit_delta':max(deltas),'identical_candidate_logit_pairs':sum(x==0 for x in deltas),'equal_complete_rank_queries':sum(s['rankings']['crossencoder_structured_256']==s['rankings']['crossencoder_structured_128'] for s in score_map.values())}
    equal(results['context_cap_control'],control,'context cap control')
    counterexamples=[]
    for ident in ['gpv1_dev_013_q2','gpv1_dev_014_q2','gpv1_dev_015_q2','gpv1_dev_007_q1','gpv1_dev_007_q2']:
        r=next(r for r in rows if r['query_id']==ident);q=queries[ident]
        counterexamples.append({'query_id':ident,'query':q['query'],'query_text':q['query_text'],'label':r['label'],'relevant_turn_ids':r['relevant_turn_ids'],
            'source_turns':q['prefix_payload']['turns'],'rankings':{m:r['score']['rankings'][m] for m in ('bm25_structured_b0.0','crossencoder_structured_256','crossencoder_denial_256')},
            'raw_candidate_scores':[{k:c[k] for k in ('turn_id','known_at')}|{'scores':{m:c['scores'][m] for m in ('bm25_structured_b0.0','crossencoder_structured_256','crossencoder_denial_256')}} for c in r['score']['candidates']]})
    return {'schema':'loom.independent_crossencoder_audit/1','auditor':'frontier_matrix','finished_at_utc':datetime.now(timezone.utc).isoformat(),'result':'PASS',
        'queries':len(rows),'cases':len(cases),'evidence_queries':sum(bool(r['relevant_evidence']) for r in rows),'annotated_spans':sum(len(r['relevant_evidence']) for r in rows),
        'languages':dict(Counter(r['language'] for r in rows)),'labels':dict(Counter(r['label'] for r in rows)),'candidate_bindings':candidate_count,'future_excluded_query_turn_bindings':future_exclusions,
        'source_target_exact_match':True,'status_event_references':event_count,'out_of_prefix_gold_spans':outside,
        'query_candidate_variant_bindings':len(expected_bindings),'unique_pair_inferences':len(pair_use),'reused_pair_bindings':sum(pair_use.values())-len(pair_use),'cache_usage_cardinality':dict(Counter(pair_use.values())),
        'raw_logit_bindings_checked':len(expected_bindings),'raw_logits_recomputed':len(pair_use),'exact_recomputed_logits':exact_logits,'max_absolute_recomputed_logit_delta':maximum_delta,'predeclared_logit_tolerance':TOLERANCE,
        'tokenizer_tensor_hashes_recomputed':len(pair_use)*3,'truncated_unique_pairs':truncated,'max_untruncated_pair_tokens':max_untruncated,'local_recompute_seconds':local_seconds,
        'complete_rankings_checked':rank_checks,'methods':method_reports,'group_summaries_checked':group_checks,'byte_allocation_summaries_checked':allocation_checks,
        'top1_gains_losses':gains,'direction_diagnostic':direction,'context_cap_control':control,'counterexamples':counterexamples,
        'actual_paid_requests':0,'actual_paid_cost_usd':'0','validation_accessed':False,'old_holdout_accessed':False,'source_judgment_available':False,'graph_fact_promoted':False,
        'limits':['Existing inspected DEV only; not independent validation.','Raw local replay checks instrument integrity, not general model accuracy.','Gold labels were not manually relabeled or independently adjudicated.','The original inference implementation hashes prior DEV outcome files before inference; exact reconstructed input strings contain no gold features.','Small correlated source families and tiny candidate pools limit generalization.']}


def main():
    destination=DOCS/'INDEPENDENT_CROSS_ENCODER_AUDIT2.json'
    require(not destination.exists(),'exclusive audit outcome already exists')
    protocol=DOCS/'INDEPENDENT_CROSS_ENCODER_AUDIT_PROTOCOL.md'
    paths=[Path(__file__),protocol,CROSS/'first_evidence.zip',CROSS/'FIRST_ARCHIVE.json',CROSS/'freeze_before_scores.json',CROSS/'freeze_before_gold.json',CROSS/'model_manifest.json',CROSS/'policy.json',PANEL/'first_results.json',PANEL/'byte_budget_results.json',PANEL/'representation_templates_v2.json',FIXTURE/'inputs_dev.json',FIXTURE/'gold_dev.json',ROOT/'loom/tools/structure/graph_local_baselines_v1/prepared_inputs.json']
    before={str(p.relative_to(ROOT)):sha(p) for p in paths}
    frozen={'schema':'loom.independent_crossencoder_audit_freeze/1','created_at_utc':datetime.now(timezone.utc).isoformat(),'files_sha256':before,'logit_tolerance':TOLERANCE,'result_file':str(destination.relative_to(ROOT))}
    with (DOCS/'INDEPENDENT_CROSS_ENCODER_AUDIT_FREEZE2.json').open('x') as out:json.dump(frozen,out,ensure_ascii=False,indent=2);out.write('\n')
    try:result=audit()
    except Exception as exc:
        result={'schema':'loom.independent_crossencoder_audit/1','result':'FAIL','failure_class':type(exc).__name__,'reason':str(exc),'actual_paid_requests':0,'actual_paid_cost_usd':'0'}
        with destination.open('x') as out:json.dump(result,out,ensure_ascii=False,indent=2);out.write('\n')
        raise
    require(before=={str(p.relative_to(ROOT)):sha(p) for p in paths},'source changed during audit')
    result['input_sha256']=before
    with destination.open('x') as out:json.dump(result,out,ensure_ascii=False,indent=2);out.write('\n')
    print(json.dumps({k:result[k] for k in ('result','queries','evidence_queries','annotated_spans','candidate_bindings','query_candidate_variant_bindings','unique_pair_inferences','max_absolute_recomputed_logit_delta','complete_rankings_checked','group_summaries_checked','byte_allocation_summaries_checked')}))


if __name__=='__main__':main()
