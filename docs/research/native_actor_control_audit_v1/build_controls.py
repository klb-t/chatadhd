"""Construct independent source controls only; no native/model/projection calls."""
from copy import deepcopy
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
DATE='2026-01-02T03:04:05Z'
EPOCH=datetime.fromisoformat(DATE.replace('Z','+00:00')).timestamp()


def encoded(value):return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def field(state,value=None):return {'state':state,'value':value}


def source(ident,*,turn='turn',actor='Nela',source_id='control:source',text='The test recorder preserves this statement.'):
    message={'id':turn,'author':{'role':'user','name':actor},'create_time':EPOCH,
        'metadata':{'loom_source_speaker':actor,'loom_source_id':source_id,'loom_source_turn_id':turn,'loom_source_known_at':DATE},
        'content':{'content_type':'text','parts':[text]}}
    document={'id':'container:'+ident,'metadata':{'loom_source_id':source_id},'mapping':{turn:{'message':message}}}
    escaped=turn.replace('~','~0').replace('/','~1')
    observation={'id':'obs:'+ident,'speaker':'user','date':DATE,'text':text,
        'locator':{'source':'caller_computes_exact_raw_hash','json_pointer':f'/mapping/{escaped}/message/content/parts/0','byte_start':0,'byte_len':len(text.encode())}}
    expected={'source_actor_label':field('resolved',actor),'source_id':field('resolved',source_id),
        'source_turn_id':field('resolved',turn),'source_known_at_statement':field('resolved',DATE),'transport_role':field('resolved','user')}
    return {'id':ident,'document':document,'observation':observation,'expected_state':'bound_source_projection','expected_fields':expected,'hash_mode':'actual'}


def main():
    destination=HERE/'controls.json'
    if destination.exists():raise ValueError('exclusive_fixture_already_exists')
    cases=[]
    case=source('equal_actor_labels');cases.append(case)
    case=source('conflicting_actor_labels');case['document']['mapping']['turn']['message']['metadata']['loom_source_speaker']='Other Nela'
    case['expected_fields']['source_actor_label']=field('conflicting_source_fields');cases.append(case)
    case=source('absent_actor_and_timestamp_no_fallback');message=case['document']['mapping']['turn']['message'];del message['author']['name'];del message['metadata']['loom_source_speaker'];del message['metadata']['loom_source_known_at']
    case['expected_fields'].update(source_actor_label=field('unknown'),source_known_at_statement=field('unknown'));cases.append(case)
    case=source('name_only_narrow_utf8_quote',actor='Żaneta',text='Początek: „łęźdź”.\nZachowaj ślad źródłowy.');message=case['document']['mapping']['turn']['message'];del message['metadata']['loom_source_speaker']
    quote='łęźdź';raw_text=message['content']['parts'][0];start=raw_text.index(quote);case['observation'].update(text=quote)
    case['observation']['locator'].update(byte_start=len(raw_text[:start].encode()),byte_len=len(quote.encode()));cases.append(case)
    case=source('conflicting_source_ids');case['document']['mapping']['turn']['message']['metadata']['loom_source_id']='control:other-source'
    case['expected_fields']['source_id']=field('conflicting_source_fields');cases.append(case)
    case=source('collision_source_a_metadata_actor_only',turn='shared-turn',actor='Lina',source_id='control:source-a',text='Source A reports a local statement.');del case['document']['mapping']['shared-turn']['message']['author']['name'];case['observation']['id']='obs:shared-native-id';cases.append(case)
    case=source('collision_source_b_same_literal_ids',turn='shared-turn',actor='Lina',source_id='control:source-b',text='Source B reports a distinct local statement.');case['observation']['id']='obs:shared-native-id';cases.append(case)
    case=source('missing_locator');del case['observation']['locator'];case.update(expected_state='unavailable',expected_reason='missing_or_invalid_native_locator',expected_fields={});cases.append(case)
    case=source('unsupported_second_text_part');case['document']['mapping']['turn']['message']['content']['parts'].append('Second source part exists but is outside the declared pointer contract.')
    case['observation'].update(text=case['document']['mapping']['turn']['message']['content']['parts'][1]);case['observation']['locator'].update(json_pointer='/mapping/turn/message/content/parts/1',byte_len=len(case['observation']['text'].encode()))
    case.update(expected_state='unavailable',expected_reason='unsupported_source_pointer_format',expected_fields={});cases.append(case)
    case=source('explicit_null_known_at');case['document']['mapping']['turn']['message']['metadata']['loom_source_known_at']=None
    case['expected_fields']['source_known_at_statement']=field('unknown');cases.append(case)
    case=source('known_at_observation_date_conflict');case['observation']['date']='2026-01-02T03:05:05Z'
    case['expected_fields']['source_known_at_statement']=field('conflicting_source_fields');cases.append(case)
    case=source('wrong_raw_source_hash');case.update(hash_mode='wrong',expected_state='unavailable',expected_reason='raw_source_hash_mismatch',expected_fields={});cases.append(case)
    require_unique={c['id'] for c in cases}
    if len(require_unique)!=len(cases) or len(cases)!=12:raise AssertionError('fixture inventory')
    pair=cases[5:7]
    if pair[0]['observation']['id']!=pair[1]['observation']['id'] or encoded(pair[0]['document'])==encoded(pair[1]['document']):raise AssertionError('collision control construction')
    for case in cases:
        locator=case['observation'].get('locator')
        if not isinstance(locator,dict):continue
        key=next(iter(case['document']['mapping']));message=case['document']['mapping'][key]['message'];part=int(locator['json_pointer'].rsplit('/',1)[1]);text=message['content']['parts'][part]
        if text.encode()[locator['byte_start']:locator['byte_start']+locator['byte_len']].decode()!=case['observation']['text']:raise AssertionError('independent quote fixture construction')
    fixture={'schema':'loom.independent_native_actor_controls/1','author':'frontier_matrix','scope':'literal source_recorded label transport controls, not verified world identity',
        'cases':cases,'cross_case_checks':[{'kind':'source_namespace_distinct','case_ids':[c['id'] for c in pair]}],
        'native_graph_modified':False,'native_runs':0,'actual_paid_requests':0,'actual_paid_cost_usd':'0','gold_or_validation_read':False}
    with destination.open('x') as out:json.dump(fixture,out,ensure_ascii=False,indent=2);out.write('\n')
    freeze={'schema':'loom.independent_native_actor_control_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
        'files_sha256':{p.name:sha(p) for p in (Path(__file__),HERE/'PROTOCOL.md',destination)},
        'cases':len(cases),'case_input_canonical_sha256':{c['id']:hashlib.sha256(encoded(c)).hexdigest() for c in cases},
        'case_document_canonical_sha256':{c['id']:hashlib.sha256(encoded(c['document'])).hexdigest() for c in cases},
        'projection_executed':False,'native_runs':0,'actual_paid_requests':0,'gold_or_validation_read':False}
    with (HERE/'FREEZE.json').open('x') as out:json.dump(freeze,out,ensure_ascii=False,indent=2);out.write('\n')
    print(json.dumps({'cases':len(cases),'fixture_sha256':sha(destination),'projection_executed':False}))


if __name__=='__main__':main()
