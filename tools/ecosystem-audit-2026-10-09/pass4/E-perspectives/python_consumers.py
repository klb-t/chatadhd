#!/usr/bin/env python3
"""Independent metamorphic oracles against real importer, SQLite and model cache.
Every fixture is synthetic. Network is denied. A reproduction PASS is not repair.
"""
import argparse, hashlib, io, json, pathlib, socket, subprocess, sys, tempfile, zipfile
from unittest.mock import patch

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',required=True,type=pathlib.Path);p.add_argument('--sha',required=True);p.add_argument('--out',required=True,type=pathlib.Path);a=p.parse_args()
 a.repo=a.repo.resolve();a.out=a.out.resolve();assert not a.out.exists(),'fresh receipt required'
 assert subprocess.check_output(['git','-C',str(a.repo),'rev-parse','HEAD'],text=True).strip()==a.sha
 sources=['engine/importer.py','engine/db.py','engine/models.py','gui/dialogs.py','main.py']
 subprocess.run(['git','-C',str(a.repo),'diff','--exit-code',a.sha,'--',*sources],check=True,stdout=subprocess.DEVNULL)
 sys.path.insert(0,str(a.repo));sys.dont_write_bytecode=True
 cases=[];observations={};calls=[]
 def check(id,kind,ok,oracle,evidence=None):cases.append(dict(id=id,kind=kind,status='PASS' if ok else 'FAIL',oracle=oracle,evidence=evidence or {}))
 def blocked(*args,**kw):calls.append('network_denied');raise RuntimeError('audit transport blocked')
 with patch.object(socket.socket,'connect',blocked),patch.object(socket.socket,'connect_ex',blocked),patch.object(socket,'create_connection',blocked),tempfile.TemporaryDirectory(prefix='audit-pass4-consumers-') as d:
  from engine.db import Database
  from engine.importer import ConversationImporter
  from engine.models import ModelRegistry
  root=pathlib.Path(d);dbs=[];n=0
  def consume(raw,suffix='.json'):
   nonlocal n
   n+=1;f=root/f'fixture-{n}{suffix}';f.write_bytes(raw);db=Database(root/f'db-{n}.sqlite');dbs.append(db)
   result=ConversationImporter(db,None).import_file(f)
   # Contract projection only; volatile generated IDs/timestamps are excluded.
   return [[{'role':m['role'],'text':m['text']} for m in db.get_msgs(c['id'])] for c in result]
  messages=[{'role':'user','content':'Synthetic question α'}, {'role':'assistant','content':'Synthetic answer β'}]
  obj={'title':'Synthetic metamorphic fixture','messages':messages,'irrelevant_metadata':{'b':2,'a':1}}
  expected=[[{'role':m['role'],'text':m['content']} for m in messages]]
  def encode(x,**kw):return json.dumps(x,ensure_ascii=False,**kw).encode()
  def reorder(x):
   if isinstance(x,dict):return {k:reorder(v) for k,v in reversed(list(x.items()))}
   if isinstance(x,list):return [reorder(v) for v in x]
   return x
  normal=encode(obj,separators=(',',':'));base=consume(normal)
  permuted=consume(encode(reorder(obj),indent=3));escaped=consume(json.dumps(obj,ensure_ascii=True).encode());whitespace=consume(b' \r\n\t'+normal+b' \n')
  check('A4-IMP-01.object-key-permutation','contract',base==permuted==expected,'JSON object member order is irrelevant here; message array order and role/text remain identical.')
  check('A4-IMP-02.unicode-escaping','contract',base==escaped==expected,'Literal UTF-8 and equivalent JSON unicode escapes decode to identical message strings.')
  check('A4-IMP-03.whitespace-small','contract',base==whitespace==expected,'Legal surrounding JSON whitespace must not change the parsed structure.')
  # Exact same parsed JSON value, now above the existing 5,000,000 byte strategy boundary.
  big=b' '*5_000_001+normal;large=consume(big)
  observations['threshold']={'small_bytes':len(normal),'large_bytes':len(big),'small_result':base,'large_result':large,'same_decoded_json':json.loads(normal)==json.loads(big)}
  check('A3-IMP-CH004.pass4-whitespace-reproduction','reproduction',base==expected and large!=expected,'Reproduce existing CH004 with semantically irrelevant whitespace, without adding any field.',observations['threshold'])
  check('A3-IMP-CH004.pass4-whitespace-invariance','acceptance',large==expected,'Crossing parser strategy threshold preserves conversation count and ordered role/text projection.')
  # A top-level conversation array is a positive sentinel for the streaming path.
  array=encode([obj]);array_small=consume(array);array_large=consume(b' '*5_000_001+array)
  check('A4-IMP-04.streaming-array-positive','contract',array_small==array_large==expected,'The valid top-level conversation-array path remains equivalent across parser threshold.')
  # Explicitly do NOT treat list order as irrelevant.
  rev={**obj,'messages':list(reversed(messages))};rev_result=consume(encode(rev))
  check('A4-IMP-05.list-order-significant','contract',rev_result==[list(reversed(expected[0]))] and rev_result!=base,'Reversing messages changes domain order; an equivalence oracle must detect this.')
  def zip_bytes(compression):
   b=io.BytesIO()
   with zipfile.ZipFile(b,'w',compression) as z:z.writestr('safe/conversation.json',normal)
   return b.getvalue()
  stored=consume(zip_bytes(zipfile.ZIP_STORED),'.zip');deflated=consume(zip_bytes(zipfile.ZIP_DEFLATED),'.zip')
  check('A4-IMP-06.container-encoding','contract',stored==deflated==base==expected,'ZIP storage versus deflate and plain-file access preserve ordered role/text. Does not claim source graph/ref equality.')
  # Same caller path can recover after an interrupted malformed read, but importer has no durable resume cursor.
  incomplete=normal[:-8]
  n+=1;f=root/'interrupted.json';f.write_bytes(incomplete);db=Database(root/'resume.sqlite');dbs.append(db);imp=ConversationImporter(db,None)
  rejected=False
  try:imp.import_file(f)
  except json.JSONDecodeError:rejected=True
  before=len(db.list_convs());f.write_bytes(normal);res=imp.import_file(f);recovered=[[{'role':m['role'],'text':m['text']} for m in db.get_msgs(c['id'])] for c in res]
  check('A4-IMP-07.retry-incomplete-json','contract',rejected and before==0 and recovered==expected,'A failed short JSON read writes no partial conversation; explicit retry after completion recovers. Not checkpoint/index resume.')
  # First load and restart exercise actual persistence, not an implementation copied into the test.
  model={'id':'synthetic/model','name':'Synthetic','context_length':2048,'pricing':{'prompt':2,'completion':3},'description':'SYNTHETIC_SEARCH_TOKEN','audit_extension':{'provenance':'safe-fixture'}}
  list_path=root/'models-list.json';wrapped_path=root/'models-wrapped.json';list_path.write_bytes(encode([model]));wrapped_path.write_bytes(encode({'data':[model]}))
  direct=ModelRegistry(list_path,{},{});wrapped=ModelRegistry(wrapped_path,{},{});after=ModelRegistry(wrapped_path,{},{})
  observations['model_cache']={'list':direct.all(),'wrapped':wrapped.all(),'wrapped_after_restart':after.all(),'wrapped_disk':json.loads(wrapped_path.read_text()),'description_search_token_in_grouped':any('SYNTHETIC_SEARCH_TOKEN' in x.get('description','') for v in wrapped.grouped().values() for x in v)}
  check('A4-CH-MOD001.wrapped-cache-loss','reproduction',direct.all()==[model] and wrapped.get_description(model['id'])=='' and wrapped.get_pricing(model['id'])=={} and after.all()==wrapped.all(),'Reproduce support-shape normalization dropping pricing, description and extension fields then persisting the loss.',observations['model_cache'])
  check('A4-CH-MOD001.supported-wrapper-invariance','acceptance',direct.all()==wrapped.all()==after.all()==[model],'Both documented cache shapes preserve model fields during migration and restart; ModelPicker sees the same description/pricing.')
  malformed=root/'models-corrupt.json';malformed.write_bytes(b'{"synthetic":"unfinished"');original=malformed.read_bytes();ModelRegistry(malformed,{},{})
  check('A4-CH-MOD001.corrupt-cache-deletion','reproduction',not malformed.exists(),'Reproduce destructive failed-load branch with safely synthetic bytes.')
  check('A4-CH-MOD001.failed-load-preserves-source','acceptance',malformed.exists() and malformed.read_bytes()==original,'Invalid cache remains available for explicit repair/recovery instead of being silently deleted.')
  check('A4-IMP-08.network-denied','contract',not calls,'All tested consumers execute locally; connect, connect_ex and create_connection are denied.')
  for db in dbs:db.close()
 out=dict(schema='klbt.audit.pass4-consumers/1',repo='klb-t/chatadhd',sha=a.sha,synthetic_only=True,source_hashes={p:hashlib.sha256((a.repo/p).read_bytes()).hexdigest() for p in sources},cases=cases,observations=observations,network_attempts=len(calls),boundaries=['Actual Python importer, Database and ModelRegistry, with real files and SQLite.','No native graph projection, paid model, UI mounting, external API or durable indexing-resume test.','Object/list oracles project ordered role/text only; no claim that metadata/full graph are preserved.'])
 out['counts']={s:sum(c['status']==s for c in cases) for s in ['PASS','FAIL','BLOCKED']};a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n');print(json.dumps(out['counts']));return int(out['counts']['FAIL']>0)
if __name__=='__main__':sys.exit(main())
