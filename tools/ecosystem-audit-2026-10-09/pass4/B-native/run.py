#!/usr/bin/env python3
"""Audit real B native consumer. Synthetic fixtures; offline ScriptedTransport.
Stable acceptance oracle comes from supplied provider IDs/parent/children and
explicit profile values, not a copy of any parser or resolver.
"""
import argparse,hashlib,json,pathlib,subprocess,tempfile,zipfile
HERE=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=pathlib.Path);p.add_argument('--sha',required=True);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--manifest',type=pathlib.Path);g.add_argument('--build-dir',type=pathlib.Path);p.add_argument('--build-receipt',type=pathlib.Path);p.add_argument('--out',required=True,type=pathlib.Path);a=p.parse_args();a.repo=a.repo.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
def h(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
if a.manifest:
 m=json.loads(a.manifest.read_text())
else:
 if not a.build_receipt:raise SystemExit('BLOCKED --build-dir requires --build-receipt from record_full_build.py; a library path alone is not provenance')
 b=a.build_dir.resolve();receipt=json.loads(a.build_receipt.read_text())
 assert receipt['build_dir']==str(b) and receipt['checkout']==str(a.repo) and receipt['sha']==a.sha and receipt['build_exit']==0
 assert h(b/'compile_commands.json')==receipt['compile_commands_sha256']
 cache=(b/'CMakeCache.txt').read_text();home=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_HOME_DIRECTORY:'))
 assert pathlib.Path(home).resolve()==a.repo/'loom','CMake source tree mismatch'
 for path,digest in receipt['source_hashes'].items():assert h(a.repo/path)==digest,'source changed after build: '+path
 assert h(b/'libloom_core.a')==receipt['archive_sha256']
 m={'sha':a.sha,'base_sha':a.sha,'base_archive':str(b/'libloom_core.a'),'base_archive_sha256':receipt['archive_sha256'],'objects':[],'full_build_receipt':receipt}
assert m['sha']==a.sha
assert subprocess.check_output(['git','-C',str(a.repo),'rev-parse','HEAD'],text=True).strip()==a.sha
subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',a.sha,'--','loom/src','loom/include','loom/data','loom/third_party'],check=True,stdout=subprocess.PIPE)
for x in m['objects']:assert h(x['path'])==x['object_sha256'] and h(a.repo/x['source'])==x['source_sha256']
assert h(m['base_archive'])==m['base_archive_sha256'];build=pathlib.Path(m['base_archive']).parent
sqlite_link=str(build/'libloom_sqlite3_amalgamation.a') if (build/'libloom_sqlite3_amalgamation.a').exists() else '-lsqlite3'
exe=a.out/'driver';cmd=['c++','-std=c++20','-g0','-O0','-Wl,--no-keep-memory','-Wl,--strip-debug','-I'+str(a.repo/'loom/include'),'-I'+str(a.repo/'loom/third_party/nlohmann'),'-I'+str(a.repo/'loom/third_party/sqlite'),str(HERE/'driver.cpp')]+[x['path'] for x in m['objects']]+[m['base_archive'],sqlite_link,str(build/'libloom_miniz.a'),'-lssl','-lcrypto','-pthread','-ldl','-lm','-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(a.out/'compile.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit('compile failed')
checks=[];calls=[]
def check(id,ok,obs,kind='contract',invariant=''):
 checks.append({'id':id,'kind':kind,'status':'PASS' if ok else 'FAIL','observed':obs,'invariant':invariant})
def blocked(id,why):checks.append({'id':id,'kind':'contract','status':'BLOCKED','reason':why})
with tempfile.TemporaryDirectory(prefix='a4-native-') as td:
 t=pathlib.Path(td)
 def call(op,data,**kw):
  req={'op':op,'data_dir':str(data),**kw};r=subprocess.run([str(exe)],input=json.dumps(req),text=True,capture_output=True,timeout=60);response=json.loads(r.stdout);calls.append({'request':req,'response':response,'exit':r.returncode,'stderr':r.stderr});return response
 def value(op,data,**kw):
  r=call(op,data,**kw)
  if not r.get('ok'):raise RuntimeError(r)
  return r['value']
 def node(id,role,text,parent,children):return {'id':id,'parent':parent,'children':children,'message':{'id':id,'author':{'role':role},'content':{'content_type':'text','parts':[text]},'create_time':1700000001,'metadata':{'future':'synthetic-preserved'}}}
 doc={'id':'synthetic-conversation','title':'Synthetic branch','create_time':1700000000,'current_node':'a','future_conversation':{'unknown':None},'mapping':{'root':{'id':'root','parent':None,'children':['u'],'message':None},'u':node('u','user','Question','root',['a','b']),'a':node('a','assistant','Answer A','u',[]),'b':node('b','assistant','Answer B','u',[])}}
 (a.out/'fixture.json').write_text(json.dumps(doc,indent=2)+'\n')
 def scan(data,path):
  value('scan',data,config={'sources':[str(path)],'retain_raw':'none','threads':1});return value('query',data)
 def sig(s):
  ns=[n for n in s['nodes'] if n['kind']=='export:message'];byid={n['id']:n['metadata']['source_key'] for n in ns};parents={e['dst']:byid[e['src']] for e in s['edges'] if e['link_type']=='parent'}
  return [(n['metadata']['source_key'],n['metadata']['role'],n['content'],parents.get(n['id']),n['metadata']['status']) for n in ns]
 oracle=[('u','user','Question',None,'active'),('a','assistant','Answer A','u','active'),('b','assistant','Answer B','u','version')]
 z=t/'source.zip'
 with zipfile.ZipFile(z,'w') as f:f.writestr('conversations.json',json.dumps([doc]))
 states={};snapshots={};uid={}
 for mode in ['link','copy']:
  data=t/mode;rows=scan(data,z);uid[mode]=next(x['unit']['id'] for x in rows if x['unit']['kind']=='conversation')
  value('catalog_import',data,options={'mode':'full','store_mode':mode,'import_messages':True});states[mode]=value('state',data)
  resource=value('resource',data,id=uid[mode]);snapshots[mode]=resource['last_successful']
  check('A4-B-RESOURCE-'+mode.upper(),sig(snapshots[mode])==oracle,{'signature':sig(snapshots[mode]),'expected':oracle},'repair','Both referenced and retained bytes expose source messages, branches and parent edges in source children order.')
 direct=t/'direct';value('direct_import',direct,path=str(z));states['direct']=value('state',direct)
 def core_sig(state):
  conv=state['conversations'][0];msgs=conv['messages'];byid={x['id']:x['text'] for x in msgs};return sorted((x['role'],x['text'],byid.get(x.get('parent_id'))) for x in msgs)
 check('CH-RES-N001-REPRO',core_sig(states['link'])!=core_sig(states['direct']),{'link':core_sig(states['link']),'direct':core_sig(states['direct'])},'reproduction')
 check('CH-RES-N001-ACCEPT',core_sig(states['link'])==core_sig(states['direct']),{'consumer':'Database.get_msgs / ordinary conversation path'},'repair')
 check('CH-RES-N002-REPRO',core_sig(states['copy'])!=core_sig(states['direct']),{'copy':core_sig(states['copy']),'direct':core_sig(states['direct'])},'reproduction')
 check('CH-RES-N002-ACCEPT',core_sig(states['copy'])==core_sig(states['direct']),{'consumer':'Catalog.import_selected(copy) → Database.get_msgs'},'repair')
 preview=value('preview',t/'link',id=uid['link']);task=value('task',t/'link',id=uid['link'],pause=True)
 check('A4-B-VIEW-HEADLESS',preview['resource']['last_successful']==snapshots['link']==task['result']['last_successful'] and task['status']=='done',{'task_status':task['status'],'same_snapshot':preview['resource']['last_successful']==task['result']['last_successful']},'integration','Preview and registered TaskEngine handler consume the same actual parser and graph projection; no browser rendering asserted.')
 graph=value('graph',t/'link');nodeids={n['id'] for n in graph['nodes']};edges={(l['src'],l['dst'],l['link_type']) for l in graph['links']}
 check('A4-B-GRAPH-STORE',all(n['id'] in nodeids for n in snapshots['link']['nodes']) and all((e['src'],e['dst'],e['link_type']) in edges for e in snapshots['link']['edges']),{'persisted_nodes':len(nodeids),'persisted_links':len(edges)},'integration','Projection persisted in existing Database and read after closing/reopening real Runtime.')
 warm=value('resource',t/'link',id=uid['link']);warmgraph=value('graph',t/'link')
 check('A4-B-COLD-WARM',warm['last_successful']==snapshots['link'] and graph==warmgraph,{'same_snapshot':warm['last_successful']==snapshots['link'],'same_store':graph==warmgraph},invariant='Repeated parse/read with warm persisted projection is idempotent; this is not a configurable cache-policy test.')
 def reverse_objects(v):
  if isinstance(v,dict):return {k:reverse_objects(v[k]) for k in reversed(v)}
  if isinstance(v,list):return [reverse_objects(x) for x in v]
  return v
 variants={'reordered':json.dumps(reverse_objects(doc)), 'whitespace':json.dumps(doc,indent=5),'plain':json.dumps(doc)}
 for name,body in variants.items():
  path=t/(name+'.json');path.write_text(body);data=t/name;rows=scan(data,path);id=rows[0]['unit']['id'];s=value('resource',data,id=id)['last_successful']
  check('A4-B-EQUIVALENCE-'+name.upper(),sig(s)==oracle,{'signature':sig(s),'expected':oracle,'mapping_status':s['mapping_status']},invariant='Domain identity/role/text/parent/status and explicit children ordering unchanged; source hash/locator/version ID may change with serialized bytes. No numeric normalization assumption.')
 # Same pinned bytes moved, then explicitly scanned at the new location.
 moved=t/'relocated.zip';z.rename(moved);rescan=value('scan',t/'link',config={'sources':[str(moved)],'retain_raw':'none','threads':1});relocated=value('resource',t/'link',id=uid['link'])
 check('A4-B-RELOCATION-REPRO',not relocated['current'],{'rescan':rescan,'status':relocated['status'],'has_retained':relocated['last_successful'] is not None},'reproduction')
 check('A4-B-RELOCATION',relocated['current'] and sig(relocated['last_successful'])==oracle,{'status':relocated['status'],'source_pinned_bytes_unchanged':True},invariant='Explicit rescan of relocated identical pinned bytes must make newly supplied source location usable or expose an actionable relocation conflict, not silently retain an unusable old locator.')
 check('A4-B-RETAIN-UNAVAILABLE',not relocated['current'] and relocated['last_successful']==snapshots['link'],{'status':relocated['status'],'retained':relocated['last_successful']==snapshots['link']},invariant='Unavailable source is not empty and last successful graph remains after reopening.')
 # Copy remains usable without live source; source restored resumes same interpretation.
 check('A4-B-COPY-OFFLINE',value('resource',t/'copy',id=uid['copy'])['last_successful']==snapshots['copy'],{'copy_retained':True})
 moved.rename(z);restored=value('resource',t/'link',id=uid['link']);check('A4-B-RESUME',restored['current'] and restored['last_successful']==snapshots['link'],{'current':restored['current']},invariant='Retry after unavailable source resumes same pinned interpretation; not in-flight crash/interrupted indexing evidence.')
 # Unknown structure remains attachable and distinguishable, without forced interpretation.
 unknown=t/'unknown.json';unknown.write_text(json.dumps({'left':[{'body':'alpha'}],'right':[{'body':'beta'}],'future':None}));data=t/'unknown';row=scan(data,unknown)[0];u=value('resource',data,id=row['unit']['id'])
 check('A4-B-UNKNOWN',u['current'] and u['last_successful']['mapping_status']=='uncertain' and u['last_successful']['coverage']=='not_implemented' and value('read',data,id=row['unit']['id'])==unknown.read_text(),{'snapshot':u},invariant='Unknown mapping is retained as attached source, explicit unsupported coverage, no auto activation or empty-success interpretation.')
 # Two explicit external bindings feed the existing real analyzer, independently of catalog renderer.
 profile_results=[]
 for i,term in enumerate(['SYNTHETIC_ALPHA','SYNTHETIC_BETA']):
  path=t/('profile'+str(i)+'.pack');overlay={'schema':'loom.runtime_profile_overlay/1','domain':'semantic_analyzer','overrides':{'rules':{'entity_patterns':[{'entity_type':'concept','pattern':term,'flags':[],'confidence':1.0}]}}};path.write_text(json.dumps(overlay));data=t/('profiledata'+str(i));(data/'profiles').mkdir(parents=True);(data/'profiles/semantic_analyzer.pack').symlink_to(path)
  id=scan(data,path)[0]['unit']['id'];s=value('resource',data,id=id)['last_successful'];actual=value('analysis',data,text='SYNTHETIC_ALPHA SYNTHETIC_BETA');fields=[x for x in s['nodes'] if x['metadata'].get('pointer')=='/rules/entity_patterns/0/pattern'];profile_results.append({'term':term,'actual':actual,'graph_field':fields[0] if fields else None,'graph_profile_hash':s['nodes'][0]['metadata']['hash']})
 check('A4-B-PROFILE-RUNTIME',all(x['graph_field']['content']==json.dumps(x['term']) and x['actual']['hash']==x['graph_profile_hash'] and [e['text'] for e in x['actual']['analysis']['entities']]==[x['term']] for x in profile_results),profile_results,'integration','Same explicitly bound external file supplies graph-visible field and actual analyzer. This does not demonstrate graph edits activating runtime.')
 # Unknown field is queryable by source pointer and does not activate as executable profile.
 bad=t/'rejected-profile.json';bad.write_text(json.dumps({**overlay,'future_mapping':{'keep':'synthetic-unknown'}}));data=t/'bad';id=scan(data,bad)[0]['unit']['id'];s=value('resource',data,id=id)['last_successful'];fields=[x for x in s['nodes'] if x['metadata'].get('pointer')=='/future_mapping/keep']
 check('A4-B-UNKNOWN-PROFILE',s['mapping_status']=='uncertain' and s['coverage']=='syntax_only' and len(fields)==1 and fields[0]['content']=='' and fields[0]['metadata']['value_ref']['unit_id']==id and s['activation']=='not_requested',{'snapshot':s},invariant='Rejected overlay retains unknown structure and hash-pinned scalar reference without silent executable adoption.')
 blocked('A4-B-INDEPENDENT-POLICIES','B4 read_resource always parses and persists full derived projection. No public selection API for eager/lazy/cache/index/live/writeback independently. User allows on-demand projection; this gate does not require mandatory materialization.')
 blocked('A4-B-GRAPH-FIELD-ACTIVATION','Projection and runtime share an explicitly bound external file. No public graph-field edit→existing resolver activation contract; shared-file parity is proven separately.')
 check('A4-B-NETWORK',all(c['response'].get('blocked_transport_attempts',0)==0 for c in calls),{'requests':sum(c['response'].get('blocked_transport_attempts',0) for c in calls),'guard':'ScriptedTransport rejects all outbound requests; background workers off'})
report={'schema':'ecosystem-audit.pass4.native/1','repo':'klb-t/chatadhd','sha':a.sha,'manifest':m,'compile_command':cmd,'calls':calls,'checks':checks,'counts':{s:sum(x['status']==s for x in checks) for s in ['PASS','FAIL','BLOCKED']},'limitations':['Synthetic mechanical fixtures, not real user conversation/model quality.','No UI browser run; native actual preview consumer only.','No in-flight process crash, nested archive or remote source tested here.','No blanket numeric 1/1.0 equivalence.','Task status done means inspection completed; current/status must still be inspected per B contract.']}
(a.out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['counts']))
raise SystemExit(int(any(x['status']=='FAIL' for x in checks)))
