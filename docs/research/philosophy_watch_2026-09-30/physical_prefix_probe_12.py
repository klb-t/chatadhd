#!/usr/bin/env python3
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.structure import graph_formal_paths as producer


def canonical(value):return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()
def digest(value):return hashlib.sha256(canonical(value)).hexdigest()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
T1='2026-09-01T10:00:00Z';T2='2026-09-02T10:00:00Z'
EVIDENCE={'source_id':'authored-prefix-source','turn_id':'t1','quote':'If A, B.'}


def assertion(ident='ab',when=T1,actor='Ada'):
    return {'id':ident,'relation':'implies','source':'a','target':'b','polarity':'positive',
            'attributed_to':actor,'known_at':when,'evidence':[deepcopy(EVIDENCE)],
            'basis_class':'observed_source_assertion','content_truth':'unverified'}


def event():return {'assertion_id':'ab','superseded_by':'future','known_at':T2,
                    'status':'superseded','evidence':[deepcopy(EVIDENCE)]}


def query(when=T1):return {'id':'prefix-query','relation':'implies','source':'a','target':'b',
                         'attributed_to':'Ada','as_of':when,'scope':'formal_implication'}


def prefix_solve(formal,rows,events,q):
    cutoff=formal._time(q['as_of']);selected=[[],[]];excluded=[];undated=[]
    for kind,source,target in [('source_assertions',rows,selected[0]),('status_events',events,selected[1])]:
        for ordinal,row in enumerate(source):
            try:when=formal._time(row.get('known_at'))
            except (ValueError,AttributeError) as exc:
                undated.append({'collection':kind,'ordinal':ordinal,'row':deepcopy(row),'sha256':digest(row),'reason':str(exc)})
                continue
            if when<=cutoff:target.append(deepcopy(row))
            else:excluded.append({'collection':kind,'ordinal':ordinal,'row':deepcopy(row),'sha256':digest(row)})
    receipt={'schema':'loom.philosophy_watch_prefix_receipt/1','as_of':q['as_of'],
             'raw_assertions':len(rows),'raw_events':len(events),
             'selected_assertions':len(selected[0]),'selected_events':len(selected[1]),
             'excluded_future':excluded,'undated':undated,'raw_sha256':digest({'rows':rows,'events':events}),
             'source_known_at_is_instrument_availability':False,'causal_prefix_extraction_verified':False}
    if undated:return {'state':'unavailable','label':None,'reason':'undated_input_cannot_be_assigned_to_prefix'},receipt
    return formal.solve_paths(*selected,q),receipt


def main():
    snapshot=HERE/'formal_audited_source_12.py'
    with snapshot.open('xb') as f:f.write(Path(producer.__file__).read_bytes())
    policy_snapshot=HERE/'graph_formal_paths_policy.json'
    with policy_snapshot.open('xb') as f:f.write(producer.POLICY_PATH.read_bytes())
    spec=importlib.util.spec_from_file_location('philosophy_formal_snapshot_12',snapshot)
    formal=importlib.util.module_from_spec(spec);spec.loader.exec_module(formal)
    base=assertion();future=assertion('future',T2,'Bo');ev=event()
    extrap=assertion('future',T2);extrap['basis_class']='extrapolated'
    duplicate=assertion('ab',T2)
    badpol=assertion('future',T2);badpol['polarity']='invented'
    invalid_now=deepcopy(extrap);invalid_now['known_at']=T1
    unknown=assertion('undated',T2);unknown['known_at']=None
    current_query=query(T2)
    cases=[('baseline',[base],[],query()),
           ('future_cross_speaker',[base,future],[ev],query()),
           ('future_invalid_premise',[base,extrap],[],query()),
           ('future_dangling_event',[base],[ev],query()),
           ('future_duplicate_id',[base,duplicate],[],query()),
           ('future_bad_polarity',[base,badpol],[],query()),
           ('current_cross_speaker',[base,future],[ev],current_query),
           ('current_invalid_premise',[base,invalid_now],[],query()),
           ('unknown_timestamp',[base,unknown],[],query()),
           ('later_cutoff_cross_speaker',[base,future],[ev],current_query)]
    files=[snapshot,policy_snapshot,Path(__file__),HERE/'PROTOCOL_12_PHYSICAL_PREFIX.md']
    freeze={'schema':'loom.philosophy_watch_prefix_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},
            'inputs_sha256':digest(cases),'case_ids':[r[0] for r in cases],'paid_requests':0}
    with (HERE/'PHYSICAL_PREFIX_FREEZE_12.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    baseline=formal.solve_paths([base],[],query());observations=[]
    for ident,rows,events,q in cases:
        before=digest({'rows':rows,'events':events,'query':q});record={'id':ident}
        try:record['whole_graph_baseline']={'returned':formal.solve_paths(rows,events,q)}
        except Exception as exc:record['whole_graph_baseline']={'exception_type':type(exc).__name__,'exception':str(exc)}
        try:
            result,receipt=prefix_solve(formal,rows,events,q)
            record['physical_prefix']={'returned':result,'receipt':receipt,'exact_baseline_solver_result':result==baseline}
        except Exception as exc:record['physical_prefix']={'exception_type':type(exc).__name__,'exception':str(exc)}
        record['input_unchanged']=before==digest({'rows':rows,'events':events,'query':q});observations.append(record)
    future_rows=[r for r in observations if r['id'].startswith('future_')]
    invalid_rows=[r for r in observations if r['id'].startswith('current_') or r['id'].startswith('later_')]
    criterion={'future_denominator':len(future_rows),'future_exact_baseline':sum(r['physical_prefix'].get('exact_baseline_solver_result',False) for r in future_rows),
               'current_invalid_denominator':len(invalid_rows),'current_invalid_rejected':sum('exception' in r['physical_prefix'] for r in invalid_rows),
               'undated_unavailable':observations[-2]['physical_prefix']['returned']['state']=='unavailable',
               'all_inputs_unchanged':all(r['input_unchanged'] for r in observations)}
    passed=criterion['future_exact_baseline']==5 and criterion['current_invalid_rejected']==3 and criterion['undated_unavailable'] and criterion['all_inputs_unchanged']
    report={'schema':'loom.philosophy_watch_prefix_results/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
            'criterion':criterion,'decision':'keep_separate_candidate_investigate_time_metadata' if passed else 'investigate',
            'observations':observations,'scope':'authored prefix-selection mechanics, not model-quality or causal extraction','paid_requests':0}
    with (HERE/'PHYSICAL_PREFIX_FIRST_RESULTS_12.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps({'criterion':criterion,'decision':report['decision']}))


if __name__=='__main__':main()
