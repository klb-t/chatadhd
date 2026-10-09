#!/usr/bin/env python3
"""Verify private source evidence, emitting only aggregate diagnostics."""
import argparse, collections, hashlib, json
from pathlib import Path

def sha(raw): return hashlib.sha256(raw).hexdigest()

def validate(path):
    raw=Path(path).read_bytes(); bundle=json.loads(raw); seen=set(); evidence_count=0; sources={}
    for t in bundle['tasks']:
        if t['task_id'] in seen: raise ValueError('duplicate_task')
        seen.add(t['task_id']); source_raw=Path(t['source_path']).read_bytes()
        if sha(source_raw)!=t['source_sha256']: raise ValueError('source_hash_mismatch')
        d=json.loads(source_raw); sources[t['family_id']]=(t['split'],t['provider']); messages=d['messages']; byid={m['message_id']:m for m in messages}; candidates=t['candidate_message_ids']
        if len(set(candidates))!=len(candidates) or not set(candidates)<=byid.keys(): raise ValueError('invalid_candidate_pool')
        ordered=sorted(messages,key=lambda m:str(m.get('created_at') or '')); scope=t['candidate_scope']
        if scope['mode']=='family_full': expected=[m['message_id'] for m in ordered]
        elif scope['mode']=='family_prefix':
            bound=next(i for i,m in enumerate(ordered) if m['message_id']==scope['through_message_id']);expected=[m['message_id'] for m in ordered[:bound+1]]
        else: raise ValueError('unknown_scope')
        if candidates!=expected: raise ValueError('scope_mismatch')
        for e in t['expected_evidence']+t.get('absence_support',[]):
            p=e['pointer'].split('/')
            if len(p)!=4 or p[1]!='messages' or p[3]!='text':raise ValueError('unsupported_pointer')
            m=messages[int(p[2])]; txt=m['text']; a,b=e['quote_span']
            if m['message_id']!=e['message_id'] or m['role']!=e['role']:raise ValueError('evidence_identity_mismatch')
            if txt[a:b]!=e['quote'] or sha(e['quote'].encode())!=e['quote_sha256']:raise ValueError('evidence_quote_mismatch')
            if sha(txt.encode())!=e['message_text_sha256']:raise ValueError('evidence_message_hash_mismatch')
            evidence_count+=1
        pos={e['message_id'] for e in t['expected_evidence']}
        if not pos<=set(candidates):raise ValueError('gold_outside_pool')
        if t['answerability']=='unanswerable_in_source':
            if pos or t['evidence_sets'] or not t['absence_support']:raise ValueError('absence_contract_mismatch')
        else:
            if not pos or not t['evidence_sets']:raise ValueError('missing_gold')
            if set().union(*(set(s) for s in t['evidence_sets']))!=pos:raise ValueError('evidence_sets_mismatch')
        forbidden={e['message_id'] for e in t['forbidden_evidence']}
        if forbidden&pos or not forbidden<=set(candidates):raise ValueError('distractor_contract_mismatch')
        if t['independent_holdout'] is not False:raise ValueError('unsupported_independence_claim')
    return {'schema':'loom.retrieval_task_validation/1','tasks_sha256':sha(raw),'tasks':len(bundle['tasks']),'families':len(sources),'evidence_spans_checked':evidence_count,'family_split_counts':dict(collections.Counter(s[0] for s in sources.values())),'provider_family_counts':dict(collections.Counter(s[1] for s in sources.values())),'task_category_counts':dict(collections.Counter(t['category'] for t in bundle['tasks'])),'source_hashes_verified':len(sources),'status':'pass','semantic_judgment_validation':'separate_same_session_review_not_proven_by_this_validator','new_api_calls':0,'new_cost_usd':'0'}

def main():
    p=argparse.ArgumentParser();p.add_argument('tasks');p.add_argument('--receipt',required=True);a=p.parse_args()
    try:r=validate(a.tasks)
    except (ValueError,KeyError,TypeError,IndexError,StopIteration,OSError):
        print(json.dumps({'status':'fail','reason':'task_validation_failed'}));return 1
    Path(a.receipt).write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));return 0
if __name__=='__main__':raise SystemExit(main())
