#!/usr/bin/env python3
"""Independent source-only conversion checks and DEV metric recount, no API."""
from collections import Counter
from copy import deepcopy
from datetime import datetime,timezone
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys
import unicodedata

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.structure import graph_panel_live as panel, graph_free_extraction as free
from loom.tools.structure import free_evidence_format_projection_v1 as producer
DEST=producer.DESTINATION


def read(path):return json.loads(Path(path).read_text())
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def digest(value):return hashlib.sha256(json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
def normalize(s):return re.sub(r'\s+',' ',unicodedata.normalize('NFKC',s).casefold()).strip().rstrip('.!?;:…。！？').rstrip()
def key(row):return tuple(row[k] for k in ('relation','source','target','polarity','attributed_to','known_at'))


def convert_only_evidence(raw,source):
    derived=deepcopy(raw);changes=[];unique=Counter(t['id'] for t in source['turns'])
    turns={t['id']:t for t in source['turns']}
    for collection in ('source_assertions','status_events'):
        for i,row in enumerate(derived.get(collection,[])):
            if not isinstance(row,dict) or not isinstance(row.get('evidence'),list):continue
            for j,item in enumerate(row['evidence']):
                if isinstance(item,str) and item and unique[item]==1:
                    row['evidence'][j]={'turn_id':item}
                    changes.append({'pointer':f'/{collection}/{i}/evidence/{j}','original':item,
                        'derived':{'turn_id':item},'raw_turn_sha256':digest(turns[item])})
    return derived,changes


def validate_binding(row,case):
    turns={t['id']:t for t in case['turns']}
    assert row['evidence']
    for evidence in row['evidence']:
        turn=turns[evidence['turn_id']];text=turn['text'];quote=evidence['quote']
        assert evidence['source_id']==case['source_id']
        assert text[evidence['char_start']:evidence['char_end']]==quote
        assert text.encode()[evidence['byte_start']:evidence['byte_end']].decode()==quote
        assert turn['known_at']==row['known_at']


def recount(cases,golds,outputs):
    by={r['case_id']:r for r in outputs};goldby={g['id']:g for g in golds}
    edges=Counter();events=Counter();nodes=Counter();details=[]
    for case in cases:
        ident=case['id'];g=goldby[ident];r=by[ident]
        used={e[k] for e in g['source_assertions'] for k in ('source','target')}
        mapping={};nodefp=0
        if r['state']=='completed':
            aliases={}
            for ref in case['node_inventory']:
                for s in (ref['text'],*ref.get('aliases',[])):aliases.setdefault(normalize(s),set()).add(ref['id'])
            for n in r['discovered_nodes']:
                matches=aliases.get(normalize(n['text']),set());mapping[n['id']]=next(iter(matches)) if len(matches)==1 else None
                nodefp+=int(len(matches)!=1)
            nodefp+=r['invalid_nodes']
        covered={x for x in mapping.values() if x in used};nodes.update(tp=len(covered),fp=nodefp,fn=len(used-covered))
        remaining=list(g['source_assertions']);matches={};tp=0;fp=r.get('invalid_assertions',0) if r['state']=='completed' else 0
        proposed=r.get('source_assertions',[]) if r['state']=='completed' else []
        for raw in proposed:
            validate_binding(raw,case);s,t=mapping.get(raw['source']),mapping.get(raw['target'])
            if s is None or t is None:fp+=1;continue
            pred={**raw,'source':s,'target':t};bound={e['turn_id'] for e in pred['evidence']}
            match=next((x for x in remaining if key(x)==key(pred) and bound and bound<={e['turn_id'] for e in x['evidence']}),None)
            if match is None:fp+=1
            else:tp+=1;remaining.remove(match);matches[pred['id']]=match['id']
        edges.update(tp=tp,fp=fp,fn=len(remaining))
        gt=Counter((e['assertion_id'],e['superseded_by'],e['status'],e['known_at']) for e in g['status_events'])
        actual=Counter();bad=r.get('invalid_events',0) if r['state']=='completed' else 0
        for e in r.get('status_events',[]):
            validate_binding(e,case)
            if e['assertion_id'] not in matches or e['superseded_by'] not in matches:bad+=1;continue
            k=(matches[e['assertion_id']],matches[e['superseded_by']],e['status'],e['known_at'])
            gold=next((x for x in g['status_events'] if k==(x['assertion_id'],x['superseded_by'],x['status'],x['known_at'])),None)
            if gold is None or not {x['turn_id'] for x in e['evidence']}<={x['turn_id'] for x in gold['evidence']}:bad+=1
            else:actual[k]+=1
        events.update(tp=sum((gt&actual).values()),fp=sum((actual-gt).values())+bad,fn=sum((gt-actual).values()))
        details.append({'case_id':ident,'tp':tp,'fp':fp,'fn':len(remaining),'state':r['state']})
    return {'edges':dict(edges),'events':dict(events),'nodes':dict(nodes),'cases':details}


def main():
    frozen=read(DEST/'FREEZE.json')
    pins=[]
    for name,expected in frozen['files_sha256'].items():
        observed=sha(ROOT/name);assert observed==expected
        pins.append({'path':name,'sha256':observed})
    ownfreeze={'schema':'loom.philosophy_watch_free_projection_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
               'producer_freeze_sha256':sha(DEST/'FREEZE.json'),'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in
               (Path(__file__),HERE/'PROTOCOL_06_FREE_PROJECTION.md',Path(free.__file__),Path(producer.__file__))},
               'development_outcomes_already_known':True,'gold_loaded_for_conversion':False}
    with (HERE/'FREE_PROJECTION_FREEZE_06.json').open('x') as f:json.dump(ownfreeze,f,indent=2);f.write('\n')
    cases=panel.load_dev_inputs();bycase={c['id']:c for c in cases};base=[];rawcost=Decimal(0);rawpins=[]
    for batch in ('batch01','batch02'):
        folder=producer.BASE/batch;manifest=read(folder/'prepared/manifest.json');ledger=read(folder/'run/ledger.json')
        assert len(manifest['requests'])==len(ledger['attempts'])==12
        for request,attempt in zip(manifest['requests'],ledger['attempts']):
            rawpath=folder/'run'/attempt['response_file'];assert sha(rawpath)==attempt['response_sha256']
            assert digest(request['body'])==attempt['request_hash'];envelope=read(rawpath)
            assert Decimal(str(envelope['usage']['cost']))==Decimal(attempt['reported_cost_usd'])
            rawcost+=Decimal(attempt['reported_cost_usd']);rawpins.append({'case_id':request['id'],'raw_sha256':sha(rawpath)})
        loaded,_=free.load_run(folder/'prepared/manifest.json',folder/'run',cases)
        assert loaded==read(folder/'first_score/compiled_first.json');base.extend(loaded)
    outputs=read(DEST/'compiled_first.json');receipts=read(DEST/'projection_receipts_first.json')
    assert len(base)==len(outputs)==len(receipts)==len(cases)==24
    byout={r['case_id']:r for r in outputs};byreceipt={r['case_id']:r for r in receipts}
    assert set(byout)==set(byreceipt)==set(bycase)
    conversions=0;retained=0;bindings=0;unavailable=[];percase=[]
    for old in base:
        ident=old['case_id'];new=byout[ident];receipt=byreceipt[ident];source=free.source_payload(bycase[ident])
        if old['state']!='completed':
            assert new==old and receipt['base_unavailability_preserved'] is True and receipt['evidence_items_converted']==0
            unavailable.append(ident);continue
        assert old['raw_model_object']==receipt['original_model_object']
        derived,changes=convert_only_evidence(old['raw_model_object'],source)
        assert changes==receipt['changes'] and len(changes)==receipt['evidence_items_converted']
        assert digest(derived)==receipt['derived_model_object_sha256']
        assert new==free.compile_free(derived,source)
        assert new['discovered_nodes']==old['discovered_nodes'] and new['source_payload_sha256']==old['source_payload_sha256']
        for field in ('source_assertions','status_events'):
            for row in old[field]:assert row in new[field];retained+=1
            for row in new[field]:validate_binding(row,bycase[ident]);bindings+=len(row['evidence'])
        conversions+=len(changes)
        percase.append({'case_id':ident,'conversions':len(changes),'new_accepted_assertions':len(new['source_assertions'])-len(old['source_assertions'])})
    # Gold/reference aliases are consumed only after source-only conversion checks.
    golds=panel.load_dev_gold();b=recount(cases,golds,base);n=recount(cases,golds,outputs)
    summary=read(DEST/'RESULTS.json')
    for own,stored in ((b,summary['baseline']),(n,summary['projection'])):
        for group,target in (('edges','strict_edges'),('events','strict_status_events'),('nodes','strict_reference_atom_alignment')):
            assert own[group]=={k:stored[target][k] for k in ('tp','fp','fn')}
    assert conversions==45 and len(unavailable)==6 and rawcost==Decimal('0.0263512')
    assert b['edges']['tp']+b['edges']['fp']==n['edges']['tp']+n['edges']['fp']==49
    assert b['events']['tp']+b['events']['fp']==n['events']['tp']+n['events']['fp']==6
    assert b['nodes']==n['nodes']=={'tp':45,'fp':15,'fn':15}
    out={'schema':'loom.philosophy_watch_free_projection_audit/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
         'freeze_pins_verified':len(pins),'raw_response_pins_verified':len(rawpins),'primary_free_compiler_sha256':sha(free.__file__),
         'planned_cases':24,'gold_assertions':sum(len(g['source_assertions']) for g in golds),'gold_events':sum(len(g['status_events']) for g in golds),
         'unavailable_case_ids_unchanged':unavailable,'conversion_only_source_turn_ids':conversions,
         'previously_accepted_records_preserved':retained,'projected_exact_source_bindings_verified':bindings,
         'source_only_projection_before_gold_recount':True,'node_alignment_unchanged':True,
         'baseline':b,'projection':n,'per_case_conversion':percase,'known_original_cost_usd':str(rawcost),
         'new_paid_requests':0,'new_paid_cost_usd':'0','sealed_validation_reads':0,
         'interpretation':'DEV syntax-decoding intervention; strict alignment lower bound, not independent model-quality or semantic hallucination rate'}
    with (HERE/'FREE_PROJECTION_AUDIT_06.json').open('x') as f:json.dump(out,f,indent=2);f.write('\n')
    print(json.dumps({k:out[k] for k in ('freeze_pins_verified','raw_response_pins_verified','conversion_only_source_turn_ids','previously_accepted_records_preserved','projected_exact_source_bindings_verified')}))


if __name__=='__main__':main()
