#!/usr/bin/env python3
"""Independent offline native pack/persistence probes; never implements product semantics.
Compiles a thin JSON transport against the supplied real CMake build. Each invocation
opens/closes the native Database, so consecutive requests exercise process restarts.
"""
import argparse, copy, hashlib, io, json, os, pathlib, sqlite3, subprocess, tarfile, tempfile, traceback
HERE=pathlib.Path(__file__).resolve().parent
HISTORICAL='b302df25e1a65f20c395eadc5ad5ef065d26e33d'
def digest(v):return hashlib.sha256(json.dumps(v,sort_keys=True,ensure_ascii=False,separators=(',',':')).encode()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=pathlib.Path);p.add_argument('--sha',required=True);p.add_argument('--build-dir',required=True,type=pathlib.Path);p.add_argument('--output',required=True,type=pathlib.Path);p.add_argument('--cxx',default='c++');p.add_argument('--sanitizer',action='store_true');a=p.parse_args()
 a.repo=a.repo.resolve();a.build_dir=a.build_dir.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
 resolved=subprocess.check_output(['git','-C',str(a.repo),'rev-parse',a.sha],text=True).strip()
 # Builds must consume this product revision; audit/report additions are irrelevant.
 check=subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',resolved,'--','loom/src','loom/include','loom/data','loom/third_party','loom/CMakeLists.txt'],stdout=subprocess.PIPE,text=True)
 if check.returncode:raise SystemExit('Product checkout differs from --sha; refuse mislabeled probe')
 cache=(a.build_dir/'CMakeCache.txt').read_text()
 source_line=next((line for line in cache.splitlines() if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')),None)
 if source_line is None:raise SystemExit('Missing CMake build source provenance')
 build_repo=pathlib.Path(source_line.split('=',1)[1]).parent
 build_check=subprocess.run(['git','-C',str(build_repo),'diff','--exit-code',resolved,'--','loom/src','loom/include','loom/data','loom/third_party'],stdout=subprocess.PIPE,text=True)
 if build_check.returncode:raise SystemExit('Build source differs from --sha; refuse mislabeled probe')
 executable=a.output/'native-driver'
 command=[a.cxx,'-std=c++20','-O0','-g','-I'+str(a.repo/'loom/include'),'-I'+str(a.repo/'loom/third_party/nlohmann'),'-I'+str(a.repo/'loom/third_party/sqlite'),str(HERE/'native_driver.cpp'),str(a.build_dir/'libloom_core.a'),str(a.build_dir/'libloom_sqlite3_amalgamation.a'),str(a.build_dir/'libloom_miniz.a'),'-lssl','-lcrypto','-pthread','-ldl','-lm','-o',str(executable)]
 if a.sanitizer:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=undefined']
 built=subprocess.run(command,capture_output=True,text=True);(a.output/'compile.log').write_text(built.stdout+built.stderr)
 if built.returncode:raise SystemExit('Audit transport compile failed; see compile.log')
 receipts=[];checks=[]
 def invoke(**request):
  result=subprocess.run([str(executable)],input=json.dumps(request,ensure_ascii=False),text=True,capture_output=True,timeout=60)
  data=json.loads(result.stdout)
  receipts.append({'request_op':request.get('op'),'request_sha256':digest(request),'response_sha256':digest(data),'returncode':result.returncode,'ok':data.get('ok'),'error':data.get('error'),'stderr':result.stderr[:1500]})
  if result.returncode and 'exception' not in data:raise RuntimeError(result.stderr)
  return data
 def value(**request):
  response=invoke(**request)
  if not response.get('ok'):raise AssertionError(response)
  return response['value']
 def check(id,condition,observed=None,kind='acceptance',requirement='R15/R40'):
  checks.append({'id':id,'kind':kind,'status':'PASS' if condition else ('NOT_REPRODUCED' if kind=='reproduction' else 'FAIL'),'requirement':requirement,'observed':observed})
 def process(id,fn):
  try:fn()
  except Exception as ex:checks.append({'id':id,'kind':'harness_case','status':'BLOCKED','error':str(ex),'trace':traceback.format_exc()})
 with tempfile.TemporaryDirectory(prefix='loom-native-audit-') as tmp:
  tmp=pathlib.Path(tmp)
  historical=json.loads((HERE/'fixtures/historical-stemming-1.json').read_text())
  fixture_manifest=json.loads((HERE/'fixtures/historical-stemming-1.manifest.json').read_text())
  assert hashlib.sha256((HERE/'fixtures/historical-stemming-1.json').read_bytes()).hexdigest()==fixture_manifest['sha256']
  expected=subprocess.check_output(['git','-C',str(a.repo),'show',HISTORICAL+':loom/data/lexicons/stemming.json'])
  assert expected==(HERE/'fixtures/historical-stemming-1.json').read_bytes()
  def packs():
   old=tmp/'old';old.mkdir()
   archive=subprocess.check_output(['git','-C',str(a.repo),'archive',HISTORICAL,'loom/data'])
   with tarfile.open(fileobj=io.BytesIO(archive)) as tar:tar.extractall(old,filter='data')
   old_dir=old/'loom/data';before=hashlib.sha256((old_dir/'lexicons/stemming.json').read_bytes()).hexdigest()
   v1=value(op='pack_validate',schema='loom.kb.stemming/1',document=historical)
   check('NATIVE-PACK-001',not v1['valid'] and any('unknown schema' in x['message'] for x in v1['issues']),v1,kind='behavior_probe',requirement='Explicit schema version handling; not proof of regression')
   full=invoke(op='pack_load',directory=str(old_dir));check('NATIVE-PACK-002',not full['ok'],full.get('error'),kind='behavior_probe',requirement='Historical full pack compatibility gate')
   overlay=tmp/'overlay';(overlay/'lexicons').mkdir(parents=True);(overlay/'lexicons/stemming.json').write_bytes(expected)
   oldover=invoke(op='pack_load',directory=str(overlay),overlay=True);check('NATIVE-PACK-003',not oldover['ok'],oldover.get('error'),kind='behavior_probe',requirement='R42 explicit invalid data failure')
   current=json.loads(subprocess.check_output(['git','-C',str(a.repo),'show',resolved+':loom/data/lexicons/stemming.json']))
   proposed=copy.deepcopy(historical);proposed['schema']='loom.kb.stemming/2';proposed['normalization']=current['normalization']
   # Candidate conversion exists only in this test. It is not a shipped migration.
   (overlay/'lexicons/stemming.json').write_text(json.dumps(proposed,ensure_ascii=False))
   samples=['running stored created class status analysis','wersji wersjach grafu wiedzy','czat ADHD chat ADHD','good better best','Nie podejmuj decyzji za użytkownika']
   converted=value(op='pack_load',directory=str(overlay),overlay=True,samples=samples)
   original=value(op='pack_load',directory=str(a.repo/'loom/data'),samples=samples)
   check('NATIVE-PACK-004',converted['samples']==original['samples'],{'converted':converted['samples'],'current':original['samples'],'sample_count':len(samples),'conversion':'schema/2 plus normalization copied from pinned current data; existing historical fields preserved'},requirement='Candidate migration semantic sample equivalence, not exhaustive')
   check('NATIVE-PACK-005',before==hashlib.sha256((old_dir/'lexicons/stemming.json').read_bytes()).hexdigest(),{'original_sha256':before,'unchanged':True},requirement='Preserve original bytes')
   broken=copy.deepcopy(proposed);del broken['normalization'];(overlay/'lexicons/stemming.json').write_text(json.dumps(broken))
   bad=invoke(op='pack_load',directory=str(overlay),overlay=True);check('NATIVE-PACK-006',not bad['ok'],bad.get('error'),requirement='R42 invalid recipe never revives handwritten default')
   future=copy.deepcopy(proposed);future['schema']='loom.kb.stemming/999';(overlay/'lexicons/stemming.json').write_text(json.dumps(future))
   bad=invoke(op='pack_load',directory=str(overlay),overlay=True);check('NATIVE-PACK-007',not bad['ok'],bad.get('error'),requirement='Unsupported versions fail explicitly')
  process('pack-module',packs)
  db=tmp/'profile.db';args={'database':str(db),'user':'synthetic/native-audit'}
  def persistence():
   state=value(op='open',legacy={'unknown_extension':{'raw':'  synthetic\nbytes  ','array':[1,None,'ą']}},**args)
   read=value(op='read',**args)
   check('NATIVE-STORE-001',state==read,{'revision':state['revision'],'profile_extension':read['profile'].get('unknown_extension')},requirement='Real native write, process exit, Database reopen, read')
   state=value(op='apply',revision=state['revision'],action={'target':'layers','op':'override','key':'preference.style','value':'audit-detailed','provenance':'form','source_refs':['synthetic/field']},**args)
   effective=lambda s,k:next(x for x in s['effectiveDefaults'] if x['key']==k)
   read=value(op='read',**args);eff=effective(read,'preference.style')
   check('NATIVE-STORE-002',eff.get('value')=='audit-detailed' and eff.get('layer')=='user' and bool(eff.get('source')) and bool(eff.get('explanation')),{'resolution':eff},requirement='R40 effective value source/reason after restart')
   stale=invoke(op='apply',revision=0,action={'target':'layers','op':'override','key':'preference.style','value':'stale writer'},**args)
   check('NATIVE-STORE-003',not stale['ok'] and stale.get('error',{}).get('code')=='conflict' and value(op='read',**args)==read,stale.get('error'),requirement='Concurrent branch CAS must not overwrite state')
   state=value(op='apply',revision=read['revision'],action={'target':'layers','op':'exclude','key':'preference.style'},**args)
   disabled_key='preference.detail';keys=[x['key'] for x in state['pack']['entries']]
   if disabled_key not in keys:disabled_key=next(k for k in keys if k.startswith('preference.') and k!='preference.style')
   state=value(op='apply',revision=state['revision'],action={'target':'layers','op':'disable','key':disabled_key},**args)
   updated=copy.deepcopy(state['pack']);updated['revision']+=1
   for entry in updated['entries']:
    if entry['key']=='preference.style':entry['revision']+=1;entry['value']='replacement pack style'
   state=value(op='update_pack',revision=state['revision'],pack=updated,scenario=state['scenario_definition'],**args)
   restored=value(op='read',**args)
   check('NATIVE-STORE-004',effective(restored,'preference.style')['status']=='excluded' and 'value' not in effective(restored,'preference.style') and effective(restored,disabled_key)['status']=='disabled',{'excluded':effective(restored,'preference.style'),'disabled':effective(restored,disabled_key)},requirement='R40 disabled/excluded survive restart and actual pack update')
   check('NATIVE-STORE-005',restored['profile']['unknown_extension']=={'raw':'  synthetic\nbytes  ','array':[1,None,'ą']},restored['profile']['unknown_extension'],requirement='Unknown profile extension survives native DB and pack update')
   same_revision=copy.deepcopy(updated);same_revision['entries'][0]['value']='changed without revision'
   rejected=invoke(op='update_pack',revision=restored['revision'],pack=same_revision,scenario=state['scenario_definition'],**args)
   check('NATIVE-STORE-006',not rejected['ok'] and value(op='read',**args)==restored,rejected.get('error'),requirement='Divergent same pack revision fails atomically')
   duplicate=copy.deepcopy(updated);duplicate['revision']+=1;duplicate['entries'].append(copy.deepcopy(duplicate['entries'][0]))
   rejected=invoke(op='update_pack',revision=restored['revision'],pack=duplicate,scenario=state['scenario_definition'],**args)
   check('NATIVE-STORE-007',not rejected['ok'] and value(op='read',**args)==restored,rejected.get('error'),requirement='Colliding identities fail atomically')
   recycled=copy.deepcopy(updated);recycled['revision']+=1
   for entry in recycled['entries']:
    if entry['key']=='preference.style':entry['id']='synthetic/replacement-bypass';entry['revision']+=1
   rejected=invoke(op='update_pack',revision=restored['revision'],pack=recycled,scenario=state['scenario_definition'],**args)
   check('NATIVE-STORE-008',not rejected['ok'] and value(op='read',**args)==restored,rejected.get('error'),requirement='R40 permanent exclusion cannot be escaped by ID recycling')
   supported=value(op='apply',revision=restored['revision'],action={'op':'settings','id':'synthetic/settings-control','time':'2000-01-01T00:00:00Z','settings':{'preference_mode':'candidate'}},**args)
   check('NATIVE-STORE-009-CONTROL',supported['profile']['settings']['preference_mode']=='candidate',{'preference_mode':supported['profile']['settings']['preference_mode']},requirement='Valid operational settings action reaches consumer')
   unsupported=invoke(op='apply',revision=supported['revision'],action={'op':'settings','id':'synthetic/settings-unsupported','time':'2000-01-01T00:00:00Z','settings':{'audit_unsupported_setting':True}},**args)
   check('NATIVE-STORE-009-REPRO',unsupported.get('ok') and unsupported['value']['profile']['settings'].get('audit_unsupported_setting') is True,{'accepted':unsupported.get('ok'),'persisted':value(op='read',**args)['profile']['settings'].get('audit_unsupported_setting'),'runtime_profile_available':unsupported.get('value',{}).get('runtime_profile',{}).get('available'),'profile_settings':unsupported.get('value',{}).get('profile',{}).get('settings')},kind='reproduction',requirement='CH-P2-N001 unsupported setting accepted and persisted')
   check('NATIVE-STORE-009',not unsupported['ok'],{'ok':unsupported.get('ok'),'error':unsupported.get('error')},requirement='CH-P2-N001 reject unsupported operational setting')
   # The shipped native legacy-import entrypoint consumes PROFILE, not outer layer snapshot.
   imported=value(op='open',database=str(tmp/'legacy-import.db'),user=args['user'],legacy=restored['profile'])
   check('NATIVE-STORE-010',imported['profile']['unknown_extension']==restored['profile']['unknown_extension'] and imported['profile']['fields']==restored['profile']['fields'],{'unknown_preserved':True,'field_count':len(imported['profile']['fields']),'import_api':'OnboardingStore.open(user, legacy_profile)'},requirement='Native legacy-profile import then persistence; not full layers/workflow backup')
   # Corrupt/newer source DB fixture: tested engine read/open must reject, never downgrade.
   with sqlite3.connect(db) as sql:
    raw=json.loads(sql.execute('SELECT body FROM loom_onboarding_profiles WHERE user_id=?',(args['user'],)).fetchone()[0]);raw['schema']='loom.onboarding_store/999';sql.execute('UPDATE loom_onboarding_profiles SET body=? WHERE user_id=?',(json.dumps(raw),args['user']))
   rejected=invoke(op='open',**args)
   with sqlite3.connect(db) as sql:after=json.loads(sql.execute('SELECT body FROM loom_onboarding_profiles WHERE user_id=?',(args['user'],)).fetchone()[0])
   check('NATIVE-STORE-011',not rejected['ok'] and rejected.get('error',{}).get('code')=='unsupported' and after==raw,rejected.get('error'),requirement='Unsupported persisted version remains intact and rejects downgrade')
  process('persistence-module',persistence)
  def provenance():
   local={'database':str(tmp/'recipe.db'),'user':'synthetic/recipe-user'}
   state=value(op='open',**local)
   privacy=copy.deepcopy(state['profile']['privacy']);privacy['rules'][0]['providers']=['synthetic/offline'];privacy['rules'][0]['infer']=True;privacy['rules'][0]['explicit_only']=False
   state=value(op='apply',revision=state['revision'],action={'target':'layers','op':'override','key':'onboarding.privacy','value':privacy},**local)
   request=value(op='request',**local)
   questions=request['questions'];field=questions[0]['field'];candidate='synthetic/candidate'
   reply={'provider':request['provider'],'request_token':request['request_token'],'section':request['section'],'summary':'Synthetic independent audit candidate.','questions':[], 'candidates':[{'id':candidate,'field':field,'value':'synthetic value','time':'2000-01-01T00:00:00Z','source_refs':['synthetic/saved-provider-reply']}]}
   state=value(op='apply',revision=state['revision'],action={'target':'model_reply','reply':reply,'time':'2000-01-01T00:00:00Z'},**local)
   old_version=state['profile']['method_executions'][0]['method_version_id'];old_receipt=copy.deepcopy(state['profile']['method_executions'][0]);produced_by=state['pack']['vocabulary']['predicates']['produced_by_method_version']
   prompt_key=state['pack']['scenario_bindings']['/prompt']
   state=value(op='apply',revision=state['revision'],action={'target':'layers','op':'override','key':prompt_key,'value':'Synthetic second prompt, independent variant.'},**local)
   next_request=value(op='request',**local);graph=value(op='graph',**local);read=value(op='read',**local)
   check('NATIVE-STORE-012',next_request['method_ref']['version_id']!=old_version and read['profile']['method_executions'][0]==old_receipt and any(e['id']==old_version for e in graph['entities']) and any(c['predicate']==produced_by and c['object']==old_version for c in graph['claims']),{'old_version':old_version,'new_version':next_request['method_ref']['version_id'],'old_execution_unchanged':read['profile']['method_executions'][0]==old_receipt,'old_entity_present':any(e['id']==old_version for e in graph['entities']),'old_produced_by_edge_present':any(c['predicate']==produced_by and c['object']==old_version for c in graph['claims']),'claim_count':len(graph['claims'])},requirement='R41 old native result retains exact prior method recipe after prompt edit and restart')
   check('NATIVE-STORE-013',request['system_prompt']!=next_request['system_prompt'] if 'system_prompt' in request else request.get('prompt')!=next_request.get('prompt'),{'request_fields':list(request),'prior_method':old_version,'next_method':next_request['method_ref']['version_id']},requirement='Two profile prompts alter actual model_request consumer')
  process('recipe-module',provenance)
  def privacy_caps():
   local={'database':str(tmp/'privacy-caps.db'),'user':'synthetic/capped-user'}
   state=value(op='open',**local);field='work.projects';category=state['profile']['fields'][field]['category']
   privacy=copy.deepcopy(state['profile']['privacy']);privacy['rules'][0]['providers']=['synthetic/offline'];privacy['rules'][0]['max_detail']=2;privacy['rules'][0]['max_sensitivity']=2
   state=value(op='apply',revision=state['revision'],action={'target':'layers','op':'override','key':'onboarding.privacy','value':privacy},**local)
   base={'op':'send','category':category,'field':field,'provider':'synthetic/offline'}
   permitted=value(op='policy',request={**base,'detail':1,'sensitivity':1},**local)
   check('B-POLICY001-CONTROL',permitted['allowed'],{'allowed':permitted['allowed'],'resolution_source':permitted['resolution']['source']},requirement='Explicit profile cap permits in-scope field')
   for dimension in ('detail','sensitivity'):
    restrictive=copy.deepcopy(privacy);restrictive['rules'][0]['max_'+dimension]=0.5
    state=value(op='apply',revision=state['revision'],action={'target':'layers','op':'override','key':'onboarding.privacy','value':restrictive},**local)
    for variant,extra in [('understate',{'detail':0,'sensitivity':0}),('omit',{})]:
     response=invoke(op='policy',request={**base,**extra},**local)
     allowed=response.get('ok') and response['value'].get('allowed')
     observation={'dimension':dimension,'variant':variant,'field_declared':state['profile']['fields'][field][dimension],'rule_limit':0.5,'accepted':response.get('ok'),'allowed':bool(allowed),'reason':response.get('value',{}).get('reason'),'error':response.get('error')}
     check('B-POLICY001-'+dimension+'-'+variant+'-REPRO',allowed,observation,kind='reproduction',requirement='B-POLICY001 caller understates graph field classification')
     check('B-POLICY001-'+dimension+'-'+variant,not allowed,observation,requirement='B-POLICY001 graph field classification remains authoritative at policy consumer')
  process('privacy-caps-module',privacy_caps)

 report={'schema':'ecosystem-audit.native-conformance/1','repo':'klb-t/chatadhd','sha':resolved,'historical_fixture':fixture_manifest,'scope':'REAL native Pack/Normalizer/OnboardingStore/DefaultLayers/KnowledgeStore; separate process per request. No models, no Runtime workers, no network path invoked. No browser/UI tier equivalence asserted.','compile_command':command,'library_sha256':hashlib.sha256((a.build_dir/'libloom_core.a').read_bytes()).hexdigest(),'build_source':str(build_repo),'checks':checks,'calls':receipts,'counts':{s:sum(c['status']==s for c in checks) for s in ('PASS','FAIL','BLOCKED','NOT_REPRODUCED')},'limitations':['Only supplied synthetic values and 5 normalization samples; migration candidate is audit-only and not shipped.','Native legacy-profile import is not full backup of outer layers, exclusions, workflow or UI state.','Basic/Advanced/Expert browser paths are outside this native harness.']}
 (a.output/'receipt.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
 print(json.dumps(report['counts']))
 return 1 if report['counts']['FAIL'] or report['counts']['BLOCKED'] else 0
if __name__=='__main__':raise SystemExit(main())
