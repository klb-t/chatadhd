#!/usr/bin/env python3
"""DEV-only source-prefix retrieval and deliberately naive diagnostics; no API."""
from __future__ import annotations
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import sys

HERE=Path(__file__).resolve().parent
STRUCTURE=HERE.parent
sys.path.insert(0,str(STRUCTURE))
import graph_panel_live as adapter


def canonical(value):
    return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'))


def sha_bytes(raw):return hashlib.sha256(raw).hexdigest()


def file_sha(path):return sha_bytes(Path(path).read_bytes())


def write_new(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('x',encoding='utf-8')as out:out.write(json.dumps(value,ensure_ascii=False,sort_keys=True,indent=2)+'\n')


def tokens(text):return Counter(re.findall(r'\w+',text.casefold()))


def chars(text):
    value=' '.join(text.casefold().split())
    return Counter((n,value[i:i+n])for n in range(3,6)for i in range(len(value)-n+1))


def cosine(a,b):
    denominator=math.sqrt(sum(v*v for v in a.values())*sum(v*v for v in b.values()))
    return min(1.,max(0.,sum(v*b.get(k,0)for k,v in a.items())/denominator))if denominator else None


def union(scores):
    available=[s for s in scores if s is not None]
    return max(available)if available else None


def prepare_query(case,query,policy):
    payload=adapter.query_payload(case,query)
    inventory={n['id']:n for n in payload['node_inventory']}
    query_text=policy['query_representation'].format(attributed_to=query['attributed_to'],relation=query['relation'],
        source_text=inventory[query['source']]['text'],target_text=inventory[query['target']]['text'])
    candidates=[]
    for order,turn in enumerate(payload['turns']):
        text=turn['text']
        candidate_text=policy['candidate_representation'].format(speaker=turn['speaker'],text=text)
        candidates.append({'source_id':case['source_id'],'turn_id':turn['id'],'speaker':turn['speaker'],
            'known_at':turn['known_at'],'text':text,'representation_text':candidate_text,'text_sha256':sha_bytes(text.encode()),
            'representation_sha256':sha_bytes(candidate_text.encode()),'eligible_order':order,
            'span':{'char_start':0,'char_end':len(text),'byte_start':0,'byte_end':len(text.encode()),'coordinate_space':'turn.text'}})
    return {'case_id':case['id'],'source_id':case['source_id'],'query_id':query['id'],'language':case['language'],
        'query':query,'query_text':query_text,'query_text_sha256':sha_bytes(query_text.encode()),
        'prefix_payload':payload,'prefix_sha256':adapter.safe.digest(payload),'candidates':candidates,
        'future_excluded_turn_count':len(case['turns'])-len(payload['turns']),
        'prediction':None,'source_judgment_available':False,'retrieval_available':bool(candidates)}


def prepare():
    policy=json.loads((HERE/'policy.json').read_text())
    if file_sha(adapter.FIXTURE/'inputs_dev.json')!=policy['input_sha256']:raise ValueError('dev_input_drift')
    cases=adapter.load_dev_inputs()
    rows=[prepare_query(c,q,policy)for c in cases for q in c['judgment_queries']]
    if len(rows)!=96 or len({r['query_id']for r in rows})!=96:raise ValueError('expected96matchedqueries')
    value={'schema':'loom.research.graph_local_prepared/1','split':'dev','inputs_sha256':policy['input_sha256'],
        'policy_sha256':file_sha(HERE/'policy.json'),'protocol_sha256':file_sha(HERE/'PROTOCOL.md'),
        'adapter_sha256':file_sha(adapter.__file__),'cases':len(cases),'query_count':len(rows),'queries':rows}
    write_new(HERE/'prepared_inputs.json',value)
    return value


def rank_candidates(candidates,method):
    return sorted(candidates,key=lambda c:(c['scores'][method]is None,
        -c['scores'][method]if c['scores'][method]is not None else 0,c['eligible_order'],c['turn_id']))


def naive_label(score,threshold):
    return None if score is None else 'supported'if score>=threshold else 'unknown'


def score(prepared,embedding):
    if prepared['split']!='dev' or embedding['prepared_input_sha256']!=file_sha(HERE/'prepared_inputs.json'):
        raise ValueError('prepared_embedding_binding_drift')
    vectors_by_query={r['query_id']:r for r in embedding['queries']}
    if len(vectors_by_query)!=len(prepared['queries']):raise ValueError('embedding_query_inventory_drift')
    rows=[]
    for query in prepared['queries']:
        scores=[];embedded=vectors_by_query[query['query_id']]
        if embedded['prefix_sha256']!=query['prefix_sha256']:raise ValueError('embedding_prefix_drift')
        cosines={r['turn_id']:r['score']for r in embedded['candidates']}
        if set(cosines)!={c['turn_id']for c in query['candidates']}:raise ValueError('embedding_candidate_inventory_drift')
        qt,qc=tokens(query['query_text']),chars(query['query_text'])
        for c in query['candidates']:
            values={'lexical_token_cosine':cosine(qt,tokens(c['representation_text'])),
                'character_3_5_cosine':cosine(qc,chars(c['representation_text'])),
                'learned_minilm_cosine':cosines[c['turn_id']]}
            values['nongating_union']=union(values.values())
            scores.append({k:c[k]for k in ('source_id','turn_id','known_at','text_sha256','representation_sha256','eligible_order','span')}|{'scores':values})
        methods=('lexical_token_cosine','character_3_5_cosine','learned_minilm_cosine','nongating_union')
        rankings={m:[c['turn_id']for c in rank_candidates(scores,m)if c['scores'][m]is not None]for m in methods}
        maximum={m:union(c['scores'][m]for c in scores)for m in methods}
        rows.append({'case_id':query['case_id'],'query_id':query['query_id'],'language':query['language'],
            'prefix_sha256':query['prefix_sha256'],'as_of':query['query']['as_of'],'query_text_sha256':query['query_text_sha256'],
            'future_excluded_turn_count':query['future_excluded_turn_count'],'candidates':scores,'rankings':rankings,
            'maximum_scores':maximum,'prediction':None,'source_judgment_available':False,
            'retrieval_available':bool(scores)})
    value={'schema':'loom.research.graph_local_scores/1','split':'dev','prepared_input_sha256':file_sha(HERE/'prepared_inputs.json'),
        'embedding_scores_sha256':file_sha(HERE/'embedding/scores.json'),'rows':rows,'query_count':len(rows),
        'source_judgment_available':False,'no_graph_promotion':True}
    write_new(HERE/'first_scores.json',value)
    return value


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('prepare','score'))
    args=parser.parse_args()
    if args.stage=='prepare':prepare()
    else:score(json.loads((HERE/'prepared_inputs.json').read_text()),json.loads((HERE/'embedding/scores.json').read_text()))
    print(args.stage+' DEV local panel complete; validation inaccessible.')
