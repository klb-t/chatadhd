#!/usr/bin/env python3
"""Independent native resource-contract probes; never implements a resolver.
Each call starts a fresh real Runtime with workers off and outbound HTTP blocked.
"""
import argparse,hashlib,io,json,pathlib,subprocess,tempfile,zipfile
HERE=pathlib.Path(__file__).resolve().parent
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=pathlib.Path);p.add_argument('--sha',required=True);p.add_argument('--build-dir',required=True,type=pathlib.Path);p.add_argument('--output',required=True,type=pathlib.Path);p.add_argument('--core-archive',type=pathlib.Path);p.add_argument('--build-manifest',type=pathlib.Path);a=p.parse_args()
 a.repo=a.repo.resolve();a.build_dir=a.build_dir.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
 resolved=subprocess.check_output(['git','-C',str(a.repo),'rev-parse',a.sha],text=True).strip()
 subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',resolved,'--','loom/src','loom/include','loom/data','loom/third_party'],check=True,stdout=subprocess.PIPE)
 core=(a.core_archive or a.build_dir/'libloom_core.a').resolve()
 manifest=json.loads(a.build_manifest.read_text()) if a.build_manifest else None
 objects=[]
 cache=(a.build_dir/'CMakeCache.txt').read_text();build_repo=pathlib.Path(next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_HOME_DIRECTORY:'))).parent
 build_sha=manifest['base_sha'] if manifest else resolved
 subprocess.run(['git','-C',str(build_repo),'diff','--exit-code',build_sha,'--','loom/src','loom/include','loom/data','loom/third_party'],check=True,stdout=subprocess.PIPE)
 if manifest:
  assert manifest['sha']==resolved,'selective object manifest targets a different SHA'
  for item in manifest['objects']:
   assert sha(a.repo/item['source'])==item['source_sha256'],'changed selected source'
   assert sha(pathlib.Path(item['path']))==item['object_sha256'],'changed selected object'
   objects.append(item['path'])
 checks=[];calls=[]
 def check(id,ok,observed,kind='acceptance'):
  checks.append({'id':id,'kind':kind,'status':'PASS' if ok else ('NOT_REPRODUCED' if kind=='reproduction' else 'FAIL'),'observed':observed})
 def blocked(id,reason):checks.append({'id':id,'kind':'missing_contract','status':'BLOCKED','reason':reason})
 with tempfile.TemporaryDirectory(prefix='native-resources-audit-') as work:
  tmp=pathlib.Path(work);exe=tmp/'driver';linkmap=tmp/'driver.map'
  command=['c++','-Wl,--no-keep-memory','-std=c++20','-O0','-g','-I'+str(a.repo/'loom/include'),'-I'+str(a.repo/'loom/third_party/nlohmann'),'-I'+str(a.repo/'loom/third_party/sqlite'),str(HERE/'driver.cpp'),str(core),str(a.build_dir/'libloom_sqlite3_amalgamation.a'),str(a.build_dir/'libloom_miniz.a'),'-lssl','-lcrypto','-pthread','-ldl','-lm','-Wl,-Map='+str(linkmap),'-o',str(exe)]
  command[command.index(str(core)):command.index(str(core))]=objects
  built=subprocess.run(command,capture_output=True,text=True);(a.output/'compile.log').write_text(built.stdout+built.stderr)
  if built.returncode:raise SystemExit('compile blocked; see compile.log')
  (a.output/'link-closure.txt').write_text(linkmap.read_text().split('Discarded input sections')[0])
  if manifest:
   selected=linkmap.read_text().split('Discarded input sections')[0]
   for name in ('chat_engine.cpp.o','context_engine.cpp.o','layers.cpp.o','graph.cpp.o','runtime_adapter.cpp.o'):
    assert 'libloom_core.a('+name+')' not in selected,'stale archive member '+name
   rows=selected.splitlines();refs=[rows[i+1] for i,x in enumerate(rows[:-1]) if x.endswith('libloom_core.a(store.cpp.o)')]
   assert all('(loom::kb::' in x for x in refs),'stale onboarding store member'
  def call(op,data,**kw):
   request={'op':op,'data_dir':str(data),**kw};r=subprocess.run([str(exe)],input=json.dumps(request),text=True,capture_output=True,timeout=90)
   response=json.loads(r.stdout);calls.append({'op':op,'request':request,'response':response,'returncode':r.returncode,'stderr':r.stderr})
   if r.returncode:raise RuntimeError(response)
   return response
  def value(op,data,**kw):
   r=call(op,data,**kw)
   if not r.get('ok'):raise RuntimeError(r)
   return r['value']
  def fixture(suffix=''):
   def msg(id,role,text,parent,children):return {'id':id,'parent':parent,'children':children,'message':{'id':id,'author':{'role':role},'content':{'content_type':'text','parts':[text+suffix]},'create_time':1700000001,'metadata':{'synthetic_unknown_message':'retained-canary'}}}
   return [{'id':'synthetic-one','title':'Synthetic branch','create_time':1700000000,'current_node':'a','synthetic_unknown_conversation':{'retained':['canary',None,7]},'mapping':{'r':{'id':'r','parent':None,'children':['u'],'message':None},'u':msg('u','user','Synthetic question','r',['a','b']),'a':msg('a','assistant','Synthetic first answer','u',[]),'b':msg('b','assistant','Synthetic alternate answer','u',[])}},{'id':'synthetic-two','title':'Synthetic second','create_time':1700000100,'current_node':'z','mapping':{'z':msg('z','user','Synthetic second conversation',None,[])}}]
  def writezip(path,body):
   with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr('conversations.json',json.dumps(body,ensure_ascii=False));z.writestr('unknown-record.json',json.dumps({'future_field':'synthetic-unknown-canary','mapping_uncertainty':['alternate-a','alternate-b']}))
  original=fixture();zip_path=tmp/'source.zip';writezip(zip_path,original);fixture_sha=sha(zip_path)
  (a.output/'synthetic-conversations.json').write_text(json.dumps(original,indent=2)+'\n')
  states={};unit_ids={};raws={}
  for mode in ('copy','link'):
   data=tmp/mode;scan=value('scan',data,config={'sources':[str(zip_path)],'threads':1,'retain_raw':'none'});units=value('query',data);unit_ids[mode]=[u['unit']['id'] for u in units if u['unit']['kind']=='conversation'];raws[mode]=[value('read',data,id=id) for id in unit_ids[mode]]
   result=value('catalog_import',data,options={'mode':'full','store_mode':mode,'import_messages':True});states[mode]=value('state',data)
   check('RES-NATIVE-'+mode.upper()+'-SCAN',len(unit_ids[mode])==2,{'scan':scan,'conversation_units':len(unit_ids[mode]),'import':result},'behavior_probe')
  direct=tmp/'direct';value('direct_import',direct,path=str(zip_path));states['direct']=value('state',direct)
  def canonical(state):
   out=[]
   for conv in state['conversations']:
    byid={m['id']:m['text'] for m in conv['messages']}
    out.append({'title':conv['conversation']['title'],'messages':sorted([(m['role'],m['text'],byid.get(m.get('parent_id'))) for m in conv['messages']],key=lambda x:json.dumps(x))})
   return sorted(out,key=lambda c:c['title'])
  signatures={mode:canonical(s) for mode,s in states.items()}
  check('RES-NATIVE-001-REPRO',signatures['link']!=signatures['direct'],signatures,'reproduction')
  check('RES-NATIVE-001',signatures['link']==signatures['direct'],signatures)
  check('RES-NATIVE-002',signatures['copy']==signatures['direct'],signatures)
  check('RES-NATIVE-003',raws['copy']==raws['link'] and all(json.loads(r).get('mapping') for r in raws['link']),{'raw_units_equal':raws['copy']==raws['link'],'raw_sha256':[hashlib.sha256(r.encode()).hexdigest() for r in raws['link']]})
  previews=[value('preview',tmp/'link',id=id) for id in unit_ids['link']]
  check('RES-NATIVE-004',all(p['verified'] for p in previews),{'preview_verified':[p['verified'] for p in previews],'read_unit_success':True},'behavior_probe')
  extraction={mode:call('extract',tmp/mode,units=unit_ids[mode]) for mode in ('copy','link')}
  check('RES-NATIVE-016',all(e.get('ok') for e in extraction.values()) and extraction['copy']['persisted_observations']==extraction['link']['persisted_observations'] and len(extraction['link']['persisted_observations'])>0,{'copy':extraction['copy'],'link':extraction['link']})
  blocked('RES-NATIVE-005','Catalog preview and read_unit share verified raw access, but no parsed linked-conversation resolver is exposed to both conversation view and headless workflow. Source-read parity does not establish graph/conversation parity.')
  retained_unknown=any('synthetic_unknown_conversation' in json.dumps(c) and 'synthetic_unknown_message' in json.dumps(c) for c in states['direct']['conversations'])
  check('RES-NATIVE-006',retained_unknown,{'direct_full_import_retains_unknown_fields_in_conversation_or_message_metadata':retained_unknown})
  writezip(zip_path,fixture(' changed'))
  changed=call('read',tmp/'link',id=unit_ids['link'][0]);after_change=value('state',tmp/'link')
  check('RES-NATIVE-007',not changed['ok'] and changed.get('code')=='conflict' and after_change==states['link'],{'read':changed,'state_unchanged':after_change==states['link']})
  saved=tmp/'source-changed-preserved.zip';zip_path.rename(saved)
  missing=call('read',tmp/'link',id=unit_ids['link'][0]);after_missing=value('state',tmp/'link');preview_missing=value('preview',tmp/'link',id=unit_ids['link'][0])
  check('RES-NATIVE-008',not missing['ok'] and after_missing==states['link'] and preview_missing['verified'] is False,{'read':missing,'state_unchanged':after_missing==states['link'],'preview_verified':preview_missing['verified']})
  extraction_missing=call('extract',tmp/'link',units=unit_ids['link'])
  check('RES-NATIVE-017',not extraction_missing['ok'] and extraction_missing['persisted_observations']==extraction['link']['persisted_observations'],{'unavailable_error':extraction_missing.get('error'),'observation_count_before':len(extraction['link']['persisted_observations']),'observation_count_after':len(extraction_missing['persisted_observations']),'preserved':extraction_missing['persisted_observations']==extraction['link']['persisted_observations']})
  copied=[value('read',tmp/'copy',id=id) for id in unit_ids['copy']]
  check('RES-NATIVE-009',copied==raws['copy'],{'copy_snapshot_survives_missing_source':copied==raws['copy']})
  saved.rename(zip_path);new_scan=value('scan',tmp/'link',config={'sources':[str(zip_path)],'threads':1,'retain_raw':'none'});new_units=value('query',tmp/'link');old_ids=set(unit_ids['link']);remaining=old_ids <= {u['unit']['id'] for u in new_units}
  check('RES-NATIVE-010',remaining and len([u for u in new_units if u['unit']['kind']=='conversation'])==4,{'scan':new_scan,'old_units_retained':remaining,'version_links':[{'id':u['unit']['id'],'prev_version':u.get('prev_version'),'source':u['unit']['source']} for u in new_units]},'behavior_probe')
  profile_dir=tmp/'profile';(profile_dir/'profiles').mkdir(parents=True);path=profile_dir/'profiles/net.pack';effective=[]
  for timeout in (701,1701):
   path.write_text(json.dumps({'schema':'loom.runtime_profile_overlay/1','domain':'net','overrides':{'default_timeout_ms':timeout,'follow_redirects':False}}));effective.append(value('profile',profile_dir))
  check('RES-NATIVE-011',[x['request']['timeout_ms'] for x in effective]==[701,1701] and [x['policy']['timeout_ms'] for x in effective]==[701,1701] and effective[0]['policy']['profile_hash']!=effective[1]['policy']['profile_hash'],{'profiles':effective})
  raw=json.dumps({'schema':'loom.runtime_profile_overlay/1','domain':'net','overrides':{'unknown_future_setting':'synthetic-retain'}});path.write_text(raw);invalid=call('profile',profile_dir)
  check('RES-NATIVE-012',not invalid['ok'] and 'unknown executable setting' in invalid.get('error','') and path.read_text()==raw,{'response':invalid,'source_bytes_unchanged':path.read_text()==raw})
  path.rename(profile_dir/'net-preserved.pack');optional_missing=value('profile',profile_dir)
  check('RES-NATIVE-013',optional_missing['inspection']['is_builtin'] is True,{'is_builtin':optional_missing['inspection']['is_builtin'],'contract':'existing optional fixed-path overlay; not a bound external-resource snapshot'},'behavior_probe')
  blocked('RES-NATIVE-014','RuntimeProfile.load(domain,data_dir) supports builtin + fixed local profiles/<domain>.pack overlay. No resource_ref/selector/version/mapping contract connects an arbitrary external profile graph entity to this loader; do not fabricate one or count raw profile loading as graph→runtime.')
  blocked('RES-NATIVE-015','No inspected common resource descriptor exposes independent retain/reference/both, eager/lazy, cache/index, snapshot/live, readonly/overlay/writeback and mapping alternatives to existing native consumers. Catalog copy/link plus fixed overlay loading are narrower existing contracts.')
  check('RES-NATIVE-NETWORK',all(c['response'].get('blocked_transport_attempts',0)==0 for c in calls),{'blocked_attempts':sum(c['response'].get('blocked_transport_attempts',0) for c in calls),'real_transport':'injected ScriptedTransport rejects every call; workers off'})
  report={'schema':'ecosystem-audit.native-resources/1','sha':resolved,'repo':'klb-t/chatadhd','core_archive':str(core),'core_sha256':sha(core),'compile_command':command,'build_manifest':json.loads(a.build_manifest.read_text()) if a.build_manifest else None,'fixture_zip_sha256':fixture_sha,'checks':checks,'calls':calls,'counts':{s:sum(c['status']==s for c in checks) for s in ['PASS','FAIL','BLOCKED','NOT_REPRODUCED']},'limitations':['Synthetic two conversations/one branch, one flat ZIP; not all formats or nested/remote sources.','No browser execution: native preview backing the route, plus static route wiring only.','Missing generic resource contracts are BLOCKED, not failed fake resolver tests.']}
  (a.output/'receipt.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print(json.dumps(report['counts']))
 return int(any(x['status'] in ('FAIL','BLOCKED') for x in checks))
if __name__=='__main__':raise SystemExit(main())
