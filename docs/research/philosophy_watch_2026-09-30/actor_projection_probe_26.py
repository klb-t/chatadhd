"""Independent nonblind mechanism replay with immutable first output."""
import copy, hashlib, importlib.util, json, pathlib, sys
from datetime import datetime, timezone
ROOT=pathlib.Path(__file__).resolve().parents[3]
HERE=pathlib.Path(__file__).resolve().parent
PACKAGE=ROOT/'loom/tools/structure/retrieval_exploration_v1/actor_projection_v1'
NATIVE=PACKAGE.parent/'native_source_free_v1'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def enc(value):return (json.dumps(value,ensure_ascii=False,sort_keys=True,indent=2)+'\n').encode()
def digest(value):return sha(enc(value))
def read(path):return json.loads(path.read_text())
def write_new(path,value):
    with path.open('xb') as f:f.write(enc(value))
def main():
    source=HERE/'actor_projection_audited_source_26.py'
    policy_path=HERE/'actor_projection_audited_policy_26.json'
    fixture_path=HERE/'actor_projection_audited_controls_26.json'
    for dst,src in [(source,PACKAGE/'actor_projection_v2.py'),(policy_path,PACKAGE/'actor_projection_policy_v2.json'),(fixture_path,ROOT/'docs/research/native_actor_control_audit_v1/controls.json')]:
        with dst.open('xb') as f:f.write(src.read_bytes())
    pins={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in [source,policy_path,fixture_path,pathlib.Path(__file__),HERE/'PROTOCOL_26_ACTOR_PROJECTION.md',NATIVE/'evaluate_native_v3.py',NATIVE/'native_panel_v2.py',PACKAGE/'freeze_before_outputs.json',PACKAGE/'freeze_before_outputs2.json',PACKAGE/'first_projection.json',PACKAGE/'second_projection.json',PACKAGE/'first_controls.json',PACKAGE/'second_controls.json',NATIVE/'first_run_ledger2.json']}
    write_new(HERE/'ACTOR_PROJECTION_FREEZE_26.json',{'known_at':datetime.now(timezone.utc).isoformat(),'files_sha256':pins,'scope':'disclosed nonblind DEV/mechanism replay; first upstream summaries already known','new_case_criteria_registered_in':'PROTOCOL_26_ACTOR_PROJECTION.md','actual_paid_requests':0})
    sys.path.insert(0,str(NATIVE))
    spec=importlib.util.spec_from_file_location('watch_actor_26',source);actor=importlib.util.module_from_spec(spec);spec.loader.exec_module(actor)
    policy=read(policy_path);fixture=read(fixture_path)
    freeze_checks=[]
    for filename in ['freeze_before_outputs.json','freeze_before_outputs2.json']:
        frozen=read(PACKAGE/filename);rows=[]
        for path,expected in frozen['files_sha256'].items():rows.append({'path':path,'pass':sha((ROOT/path).read_bytes())==expected})
        freeze_checks.append({'file':filename,'pins':len(rows),'passed':sum(r['pass'] for r in rows),'failures':[r for r in rows if not r['pass']]})
    ledger=read(NATIVE/'first_run_ledger2.json');rows=[];unavailable=[]
    for row in ledger['rows']:
        if row['state']!='completed':unavailable.append(row['case_id']);continue
        raw=(ROOT/row['input_path']).read_bytes();snapshot=read(ROOT/row['output_directory']/'native_snapshot.json')
        for observation in snapshot['bodies']['loom_kb_observations']:
            before=digest(observation);result=actor.project(observation,raw,policy)
            rows.append({'case_id':row['case_id'],'arm':row['arm']}|result)
            assert digest(observation)==before
    saved1=read(PACKAGE/'first_projection.json')['projections'];saved2=read(PACKAGE/'second_projection.json')['projections']
    primary={'observations':len(rows),'bound':sum(r['state']=='bound_source_projection' for r in rows),'resolved_actor':sum(r.get('fields',{}).get('source_actor_label',{}).get('state')=='resolved' for r in rows),'same_v2_exact_objects':rows==saved2,'v1_v2_canonical_objects_equal':saved1==saved2,'planned_case_arms':len(ledger['rows']),'unavailable_case_arms':len(unavailable),'result_sha256':digest(rows)}
    controls=[];bindings={}
    for case in fixture['cases']:
        raw=enc(case['document']);obs=copy.deepcopy(case['observation'])
        if isinstance(obs.get('locator'),dict):obs['locator']['source']='sha256:'+ ('0'*64 if case['hash_mode']=='wrong' else sha(raw))
        result=actor.project(obs,raw,policy);checks=[result['state']==case['expected_state']]
        if 'expected_reason' in case:checks.append(result.get('reason')==case['expected_reason'])
        for field,expected in case['expected_fields'].items():
            for k,v in expected.items():checks.append(result.get('fields',{}).get(field,{}).get(k)==v)
        controls.append({'case_id':case['id'],'checks':len(checks),'passed':sum(checks),'result':result,'raw_sha256':sha(raw)})
        bindings[case['id']]=(raw,obs,result)
    saved_controls=read(PACKAGE/'second_controls.json');controls_same=all(c['result']==s['projection'] for c,s in zip(controls,saved_controls['cases']))
    base=fixture['cases'][0];base_doc=copy.deepcopy(base['document']);base_obs=copy.deepcopy(base['observation']);base_raw=enc(base_doc);base_obs['locator']['source']='sha256:'+sha(base_raw);base_result=actor.project(base_obs,base_raw,policy)
    new=[]
    def check(name,passed,raw,obs,result,**extra):new.append({'case_id':name,'pass':bool(passed),'raw_sha256':sha(raw),'observation_sha256':digest(obs),'result':result,**extra})
    future=copy.deepcopy(base_doc);future['mapping']['future-roster']={'message':{'author':{'name':'Future aliased person'},'metadata':{'loom_source_speaker':'Future aliased person','same_as':'Nela','loom_source_known_at':'2040-01-01T00:00:00Z'}}};future_raw=enc(future)
    result=actor.project(base_obs,future_raw,policy);check('future_append_requires_new_source_version',result.get('reason')=='raw_source_hash_mismatch',future_raw,base_obs,result)
    result=actor.project(base_obs,base_raw,policy);check('preserved_raw_version_retains_exact_past_result',result==base_result,base_raw,base_obs,result)
    rebound=copy.deepcopy(base_obs);rebound['locator']['source']='sha256:'+sha(future_raw);result=actor.project(rebound,future_raw,policy);check('explicit_new_version_rebind_keeps_message_local_fields',result.get('fields')==base_result['fields'] and result['source_sha256']!=base_result['source_sha256'],future_raw,rebound,result)
    conflicted=copy.deepcopy(base_doc);conflicted['mapping']['turn']['message']['author']['name']='New claimed author';raw=enc(conflicted);obs=copy.deepcopy(base_obs);obs['locator']['source']='sha256:'+sha(raw);result=actor.project(obs,raw,policy);check('changed_author_conflicts_without_provenance_loss',result.get('fields',{}).get('source_actor_label',{}).get('state')=='conflicting_source_fields' and result.get('source_sha256')==sha(raw) and result.get('fields',{}).get('source_known_at_statement')==base_result['fields']['source_known_at_statement'],raw,obs,result)
    quote=copy.deepcopy(base_doc);text='Lina powiedziała: „To jest mój projekt”.';quote['mapping']['turn']['message']['content']['parts']=[text];raw=enc(quote);obs=copy.deepcopy(base_obs);obs['text']=text;obs['locator'].update(source='sha256:'+sha(raw),byte_len=len(text.encode()));result=actor.project(obs,raw,policy);check('quoted_third_party_does_not_replace_source_actor',result.get('fields',{}).get('source_actor_label',{}).get('value')=='Nela' and result['actor_epistemic_status']=='source_recorded',raw,obs,result)
    escaped=copy.deepcopy(base_doc);key='turn/~local';message=escaped['mapping'].pop('turn')['message'];message['id']=key;message['metadata']['loom_source_turn_id']=key;escaped['mapping'][key]={'message':message};raw=enc(escaped);obs=copy.deepcopy(base_obs);obs['locator'].update(source='sha256:'+sha(raw),json_pointer='/mapping/turn~1~0local/message/content/parts/0');result=actor.project(obs,raw,policy);check('escaped_message_key_resolves_exact_local_turn',result.get('fields',{}).get('source_turn_id',{}).get('value')==key,raw,obs,result)
    invalid=copy.deepcopy(base_doc);invalid['mapping']['turn']['message']['author']['name']={'id':'invented'};raw=enc(invalid);obs=copy.deepcopy(base_obs);obs['locator']['source']='sha256:'+sha(raw);result=actor.project(obs,raw,policy);check('malformed_actor_abstains_per_field',result.get('fields',{}).get('source_actor_label',{}).get('state')=='invalid_type_or_empty' and result.get('fields',{}).get('source_id')==base_result['fields']['source_id'],raw,obs,result)
    raw,obs,_=bindings['unsupported_second_text_part'];alt=copy.deepcopy(policy);alt['allowed_content_parts']=[1];result=actor.project(obs,raw,alt);check('explicit_second_part_policy_supported',result['state']=='bound_source_projection' and actor.project(obs,raw,policy).get('reason')=='unsupported_source_pointer_format',raw,obs,result,policy_sha256=digest(alt))
    raw,obs,result=bindings['absent_actor_and_timestamp_no_fallback'];check('missing_actor_does_not_use_role_or_impute_time',result['fields']['source_actor_label']['state']=='unknown' and result['fields']['source_known_at_statement']['state']=='unknown' and result['fields']['transport_role']['value']=='user',raw,obs,result)
    a=bindings['collision_source_a_metadata_actor_only'];b=bindings['collision_source_b_same_literal_ids'];check('equal_literal_names_do_not_create_global_same_as',a[2]['source_sha256']!=b[2]['source_sha256'] and a[2]['fields']['source_id']['value']!=b[2]['fields']['source_id']['value'] and a[2]['observation_id']==b[2]['observation_id'] and a[2]['fields']['source_actor_label']['value']==b[2]['fields']['source_actor_label']['value'] and all('same_as' not in r and r['native_graph_modified'] is False for r in (a[2],b[2])),a[0],a[1],a[2],other_source_sha256=b[2]['source_sha256'])
    results={'schema':'loom.philosophy.actor_projection_audit/1','known_at':datetime.now(timezone.utc).isoformat(),'freeze_checks':freeze_checks,'primary':primary,'controls':{'cases':len(controls),'passed':sum(r['passed']==r['checks'] for r in controls),'checks':sum(r['checks'] for r in controls),'checks_passed':sum(r['passed'] for r in controls),'exact_saved_v2_results':controls_same,'rows':controls},'new_cases':new,'new_case_count':len(new),'new_case_passed':sum(r['pass'] for r in new),'actual_paid_requests':0,'actual_paid_cost_usd':'0','native_runs':0,'canonical_graph_writes':0,'new_validation_or_old_holdout_read':False,'semantic_identity_accuracy_measured':False,'decision':'keep_named_source_projection_mechanism' if all(r['pass'] for r in new) and controls_same and rows==saved2 else 'investigate'}
    write_new(HERE/'ACTOR_PROJECTION_FIRST_RESULTS_26.json',results)
    print(json.dumps({k:v for k,v in results.items() if k not in ('controls','new_cases')}))
if __name__=='__main__':main()
