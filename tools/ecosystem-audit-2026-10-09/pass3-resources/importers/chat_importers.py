#!/usr/bin/env python3
"""Independent synthetic probes of actual Python importer and Database consumers."""
import argparse, contextlib, hashlib, io, json, logging, pathlib, socket, subprocess, sys, tempfile, zipfile
from unittest.mock import patch

def sha(data):return hashlib.sha256(data).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--phase',choices=['both','reproduction','acceptance'],default='both');a=p.parse_args();a.repo=a.repo.resolve();a.out=a.out.resolve();a.out.parent.mkdir(parents=True,exist_ok=True)
 actual=subprocess.check_output(['git','-C',str(a.repo),'rev-parse','HEAD'],text=True).strip();assert actual==a.sha,'SHA mismatch'
 sources=['engine/importer.py','engine/db.py','engine/events.py','gui/import_panel.py']
 subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',a.sha,'--',*sources],check=True,stdout=subprocess.DEVNULL)
 sys.path.insert(0,str(a.repo));sys.dont_write_bytecode=True
 network_attempts=[]
 def blocked(*args,**kwargs):network_attempts.append('blocked_socket');raise RuntimeError('Audit blocked external transport')
 rows=[];observations={}
 def check(id,phase,condition,evidence=None):
  if a.phase in ['both',phase]:rows.append({'id':id,'phase':phase,'status':'PASS' if condition else 'FAIL','evidence':evidence or {}})
 def unavailable(id,reason):rows.append({'id':id,'phase':'acceptance','status':'BLOCKED_MISSING_CONTRACT','reason':reason})
 with patch.object(socket.socket,'connect',blocked),patch.object(socket.socket,'connect_ex',blocked),patch.object(socket,'create_connection',blocked),tempfile.TemporaryDirectory(prefix='audit-resource-import-') as temp:
  from engine.db import Database
  from engine.importer import ConversationImporter
  from engine.events import bus,IMPORT_DONE
  tmp=pathlib.Path(temp);dbs=[];events=[];bus.on(IMPORT_DONE,lambda event:events.append(event))
  def harness(name):
   db=Database(tmp/(name+'.sqlite'));dbs.append(db);return db,ConversationImporter(db,None)
  def write(name,data):
   f=tmp/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(data);return f
  def conversation(db,conv):return db.get_msgs(conv['id'])
  def safe_rows(ms):return [{'role':x['role'],'text':x['text'],'parent_id':x.get('parent_id'),'attachments':x.get('attachments'),'metadata':x.get('metadata'),'model':x.get('model')} for x in ms]
  def zip_bytes(entries):
   out=io.BytesIO()
   with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
    for name,value in entries:z.writestr(name,value)
   return out.getvalue()
  rich={'id':'source-conversation-A','title':'Synthetic branching conversation','current_node':'a2','audit_unknown_conversation':{'marker':'AUDIT_UNKNOWN_CONVERSATION'},'mapping':{
   'root':{'id':'root','parent':None,'children':['u'],'message':{'id':'source-system','author':{'role':'system'},'content':{'parts':['AUDIT_SYSTEM']}}},
   'u':{'id':'u','parent':'root','children':['a1','a2'],'message':{'id':'source-user','author':{'role':'user'},'create_time':1,'metadata':{'audit_unknown_message':'AUDIT_UNKNOWN_MESSAGE'},'content':{'content_type':'multimodal_text','parts':['AUDIT_USER',{'content_type':'image_asset_pointer','asset_pointer':'asset://attachment.bin'}]}}},
   'a1':{'id':'a1','parent':'u','children':[],'message':{'id':'source-a1','author':{'role':'assistant'},'content':{'parts':['AUDIT_BRANCH_A']},'metadata':{'model_slug':'synthetic-model-A'}}},
   'a2':{'id':'a2','parent':'u','children':['tool'],'message':{'id':'source-a2','author':{'role':'assistant'},'content':{'parts':['AUDIT_BRANCH_B']}}},
   'tool':{'id':'tool','parent':'a2','children':[],'message':{'id':'source-tool','author':{'role':'tool'},'content':{'parts':['AUDIT_TOOL']}}}}}
  rich_bytes=json.dumps(rich,ensure_ascii=False).encode();rich_file=write('rich.json',rich_bytes);db,imp=harness('rich');result=imp.import_file(rich_file);msgs=conversation(db,result[0]);stored=safe_rows(msgs)
  material=json.dumps({'conversations':db.list_convs(),'messages':stored,'nodes':db.list_nodes(),'links':db.get_links()},ensure_ascii=False)
  fidelity={'rows':stored,'source_sha256':sha(rich_bytes),'source_hash_reachable_from_stored_records':sha(rich_bytes) in material,'unknown_conversation_reachable':'AUDIT_UNKNOWN_CONVERSATION' in material,'unknown_message_reachable':'AUDIT_UNKNOWN_MESSAGE' in material,'source_ids_reachable':'source-a1' in material,'graph_links':len(db.get_links())}
  observations['rich_import']=fidelity
  lost=not fidelity['unknown_message_reachable'] and all(x['parent_id'] is None for x in stored) and len(stored)==3
  check('A3-IMP-CH001.lossy-legacy-export','reproduction',lost)
  unavailable('A3-IMP-CH001.lossless-resource-equality','Actual legacy writes lose the tested fields/relations. The required full graph projection/reference resolver contract is absent here; a valid fix may retain bytes outside SQLite. Requiring every marker in database JSON would exclude that permitted implementation, so no invented full-graph acceptance adapter is supplied.')
  # Same production importer handles bare JSON and two container depths.
  outer=zip_bytes([('nested/inner.zip',zip_bytes([('conversation.json',rich_bytes),('attachment.bin',b'AUDIT_BINARY_ATTACHMENT')]))]);outer_file=write('outer.zip',outer)
  zipped,zimp=harness('nested');zr=zimp.import_file(outer_file);zmsgs=conversation(zipped,zr[0]);observations['nested_archive']={'conversations':len(zr),'source_sha256':sha(outer),'text_rows':safe_rows(zmsgs),'temp_source_paths_retained':any('chatadhd_import_' in json.dumps(x) for x in zr)}
  check('A3-IMP-CH002.nested-container-consumer','acceptance',len(zr)==1 and [(x['role'],x['text']) for x in zmsgs]==[(x['role'],x['text']) for x in msgs])
  # Unknown mapping and an empty known data array must remain distinguishable.
  unknown=write('unknown.json',b'{"alien_turns":{"message":"AUDIT_UNKNOWN_MAPPING"}}');empty=write('empty.json',b'[]');udb,uimp=harness('unknown');unknown_error=None
  try: ur=uimp.import_file(unknown)
  except Exception as e:unknown_error=type(e).__name__;ur=None
  er=uimp.import_file(empty);observations['unknown_mapping']={'unknown_result':ur,'known_empty_result':er,'exception_type':unknown_error}
  check('A3-IMP-CH003.unknown-equals-empty','reproduction',ur==er==[] and unknown_error is None)
  check('A3-IMP-CH003.explicit-unknown-mapping','acceptance',unknown_error is not None or ur!=er)
  # The existing >5 MB branch seeks any '[' rather than the top-level shape.
  small={'title':'Synthetic object export','messages':[{'role':'user','content':'AUDIT_STREAM_MESSAGE'}]};large={**small,'padding':'X'*5_000_010}
  sdf,sdi=harness('small');ldf,ldi=harness('large');sr=sdi.import_file(write('small.json',json.dumps(small).encode()));lr=ldi.import_file(write('large.json',json.dumps(large).encode()));observations['stream_threshold']={'small_conversations':len(sr),'large_conversations':len(lr),'large_bytes':len(json.dumps(large))}
  check('A3-IMP-CH004.size-dependent-object-loss','reproduction',len(sr)==1 and lr==[])
  check('A3-IMP-CH004.streaming-preserves-supported-shape','acceptance',len(sr)==len(lr)==1 and [(m['role'],m['text']) for m in conversation(sdf,sr[0])]==[(m['role'],m['text']) for m in conversation(ldf,lr[0])])
  # A bad member is logged but the outer consumer only returns successful convs.
  mix=write('partial.zip',zip_bytes([('good.json',json.dumps(small).encode()),('bad.json',b'{broken'),('unknown.json',unknown.read_bytes())]));mdb,mi=harness('partial');before=len(events)
  with contextlib.redirect_stderr(io.StringIO()): mr=mi.import_file(mix)
  observations['partial_archive']={'returned_conversations':len(mr),'events':events[before:],'result_type':type(mr).__name__,'members':3,'structured_member_outcomes':False}
  check('A3-IMP-CH003.partial-container-reported-only-as-success-list','reproduction',len(mr)==1 and isinstance(mr,list) and bool(events[before:]))
  unavailable('A3-IMP-CH003.partial-container-outcomes','The current caller receives only successful conversation records; errors remain log entries. No per-member outcome/resource mapping contract exists. Future acceptance must consume that actual contract, not require a particular replacement of the list return type.')
  # Snapshot behavior survives a source change/deletion; no live policy claimed.
  before_msgs=safe_rows(db.get_msgs(result[0]['id']));rich_file.write_text('{"changed":true}');rich_file.unlink();db.close();db=Database(tmp/'rich.sqlite');dbs.append(db)
  check('A3-IMP-CH005.snapshot-survives-unavailable-source','acceptance',safe_rows(db.get_msgs(result[0]['id']))==before_msgs)
  # Headless raw HTML route; no browser rendering or arbitrary script execution.
  html=b'<html><body><p>User: AUDIT_ONE question long</p><p>Assistant: AUDIT_TWO reply long</p><p>User: AUDIT_THREE question long</p><p>Assistant: AUDIT_FOUR reply long</p></body></html>'
  hdb,hi=harness('html');hr=hi.import_file(write('plain.html',html));hm=conversation(hdb,hr[0]) if hr else [];roles=[m['role'] for m in hm];observations['html']={'rows':safe_rows(hm),'browser_started':False}
  check('A3-IMP-CH006.html-role-group-order','reproduction',roles==['user','user','assistant','assistant'])
  check('A3-IMP-CH006.html-source-order','acceptance',roles==['user','assistant','user','assistant'])
  unavailable('A3-IMP-ZIP-REFERENCE-EQUALITY','ConversationImporter has only materializing import_file/path; no binding/selector/version/policy graph-reference contract. Native catalog is audited separately; this probe must not invent that implementation.')
  unavailable('A3-IMP-LIVE-REFRESH-WRITEBACK','No existing Python importer public source refresh, reference read, lazy/eager/cache or writable projection contract identified. Snapshot persistence PASS does not prove live availability monitoring.')
  unavailable('A3-IMP-UI-HEADLESS-RESOURCE-QUERY','Headless import_file runs; GUI import_panel source calls the same importer. Actual rendered view→graph resource read and ZIP reference→task API are not implemented in this Python path and GUI was not launched.')
  check('A3-IMP-NETWORK-GUARD','acceptance',network_attempts==[],{'attempts':len(network_attempts),'block':'socket connect/connect_ex/create_connection'})
  for x in dbs:
   try:x.close()
   except Exception:pass
 out={'schema':'klbt.audit.receipt/3','repo':'klb-t/chatadhd','sha':actual,'suite':'Python conversation importer resources','source_hashes':{x:sha((a.repo/x).read_bytes()) for x in sources},'tool_sha256':sha(pathlib.Path(__file__).read_bytes()),'network_attempts':len(network_attempts),'observations':observations,'cases':rows,'boundaries':['Actual Database/ConversationImporter; no copied parser or stub database.','Actual SQLite reopen; only synthetic publishable fixtures.','GUI source wiring only; not browser/Kivy E2E.','Native import/catalog/graph kernel outside this probe.']}
 out['counts']={s:sum(x['status']==s for x in rows) for s in ['PASS','FAIL','BLOCKED_MISSING_CONTRACT']};a.out.write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n');print(json.dumps(out['counts']));return int(out['counts']['FAIL']>0 or out['counts']['BLOCKED_MISSING_CONTRACT']>0)
if __name__=='__main__':sys.exit(main())
