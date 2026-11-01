"""Restored candidate only; existing synthetic accounts, no credentials/login."""
from pathlib import Path
import base64,ctypes as c,hashlib,json,os,signal,socket,sqlite3,subprocess,sys,time,traceback
import requests
inp=json.loads(Path(sys.argv[1]).read_text());root=Path(inp['restored']);out=Path(inp['output']);assert out.is_dir() and not any(out.iterdir()), 'fresh gate directory required'
stage=Path(inp['stage']);result={'success':False,'choice':'accepted new candidate+its lib, existing schema and LOOM_LOG_ENABLED=0; no prior executable needed for this restore gate','server':{},'schema':{},'native':[],'model_calls':0,'new_credentials':False}
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha((stage/'bin/loom-server').read_bytes())==inp['server_sha256']
assert sha((stage/'lib/libloom.so').read_bytes())==inp['lib_sha256']
proc=None;log=None

def observe():
 results={}
 for label,rel in inp['databases'].items():
  path=root/rel
  conn=sqlite3.connect(path.as_uri()+'?mode=ro',uri=True)
  try:
   conn.execute('PRAGMA query_only=ON')
   assert [r[0] for r in conn.execute('PRAGMA integrity_check')]==['ok'],'database integrity'
   assert list(conn.execute('PRAGMA foreign_key_check'))==[],'foreign keys'
   ddl=list(conn.execute("SELECT type,name,tbl_name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name"))
   tables={r[1] for r in ddl if r[0]=='table'}
   counts={t:conn.execute('SELECT count(*) FROM "'+t+'"').fetchone()[0] for t in ['users','sessions','conversations','messages'] if t in tables}
   meta=dict(conn.execute('SELECT key,value FROM _meta ORDER BY key')) if '_meta' in tables else {}
   rec={'integrity':'ok','foreign_keys':0,'counts':counts,'schema_sha256':sha(json.dumps(ddl).encode()),'meta':meta,'user_version':conn.execute('PRAGMA user_version').fetchone()[0]}
   if label in inp['expected_schema']:
    expected=inp['expected_schema'][label]
    assert rec['schema_sha256']==expected['schema_sha256'],'accepted schema'
    assert meta==expected['meta'],'accepted meta'
    for k,v in expected['counts'].items():assert counts[k]==v,'accepted counts'
   results[label]=rec
  finally:conn.close()
 return results

try:
 result['schema']['before_start']=observe()
 sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
 assert port!=8787
 command=[str(stage/'bin/loom-server'),'--host','127.0.0.1','--port',str(port),'--accounts-root',str(root),'--data-dir',str(root),'--static-dir',str(stage/'web'),'--threads','4']
 log=(out/'server.log').open('wb');proc=subprocess.Popen(command,env=dict(os.environ),cwd=out,stdout=log,stderr=subprocess.STDOUT)
 result['server']={'pid':proc.pid,'port':port,'executable_sha256':inp['server_sha256']}
 client=requests.Session();client.trust_env=False;base='http://127.0.0.1:'+str(port)
 for _ in range(150):
  assert proc.poll() is None,'restored server exit before readiness'
  try:
   r=client.get(base+'/api/healthz',timeout=.3)
   if r.status_code==200 and ('listening on 127.0.0.1:'+str(port)) in (out/'server.log').read_text():break
  except requests.RequestException:pass
  time.sleep(.1)
 else:raise AssertionError('restored readiness timeout')
 assert r.json().get('ok') is True
 me=client.get(base+'/api/auth/me',timeout=10);assert me.status_code==200
 assert me.json().get('accounts') is True and me.json().get('authenticated') is False
 refused=client.get(base+'/api/conversations',timeout=10);assert refused.status_code==401
 result['server'].update(health_status=200,accounts_mode=True,anonymous_status=401)
 client.close();proc.send_signal(signal.SIGTERM);rc=proc.wait(timeout=20);result['server']['exit_code']=rc;proc=None;log.close();log=None;assert rc==0
 # No tokens/passwords exist outside the DB. Native local admin reads verify the
 # actual two runtimes after the accounts server closes, without changing auth.
 lib=c.CDLL(str(stage/'lib/libloom.so'))
 lib.loom_init_ex.argtypes=[c.c_char_p,c.POINTER(c.c_void_p)];lib.loom_init_ex.restype=c.c_void_p
 lib.loom_shutdown.argtypes=[c.c_void_p];lib.loom_shutdown.restype=None
 lib.loom_list_conversations.argtypes=[c.c_void_p,c.c_int];lib.loom_list_conversations.restype=c.c_void_p
 lib.loom_get_conversation.argtypes=[c.c_void_p,c.c_char_p];lib.loom_get_conversation.restype=c.c_void_p
 lib.loom_invoke.argtypes=[c.c_void_p,c.c_char_p,c.c_char_p,c.c_char_p];lib.loom_invoke.restype=c.c_void_p
 lib.loom_free_string.argtypes=[c.c_void_p];lib.loom_free_string.restype=None
 def decoded(ptr):
  assert ptr,'null C API result'
  try:return json.loads(c.string_at(ptr))
  finally:lib.loom_free_string(ptr)
 for label in ['runtime_A','runtime_B']:
  runtime=(root/inp['databases'][label]).parent
  conn=sqlite3.connect((runtime/'chatadhd.db').as_uri()+'?mode=ro',uri=True)
  try:
   conn.execute('PRAGMA query_only=ON')
   expected=[]
   for row in conn.execute('SELECT id,title,created,updated,source,metadata FROM conversations ORDER BY id'):
    d=dict(zip(['id','title','created','updated','source','metadata'],row));d['metadata']=json.loads(d['metadata'] or '{}');expected.append(d)
  finally:conn.close()
  err=c.c_void_p();ctx=lib.loom_init_ex(json.dumps({'data_dir':str(runtime),'start_workers':False,'enable_fts':False}).encode(),c.byref(err))
  assert ctx and not err.value,'restored runtime open'
  try:
   actual=decoded(lib.loom_list_conversations(ctx,100));assert sorted(actual,key=lambda d:d['id'])==expected,'restored conversation content'
   for conv in expected:assert decoded(lib.loom_get_conversation(ctx,conv['id'].encode()))==conv,'exact native conversation'
   original=inp['originals'][label[-1]]
   blob=decoded(lib.loom_invoke(ctx,b'actions',b'client.blob_read',json.dumps({'hash':original,'offset':0,'length':65536}).encode()))
   raw=base64.b64decode(blob['data_base64'],validate=True)
   assert blob['eof'] is True and blob['hash']==original and sha(raw)==original,'native original bytes'
   result['native'].append({'runtime':label,'conversations':len(expected),'conversation_records_sha256':sha(json.dumps(expected,sort_keys=True,separators=(',',':')).encode()),'original_sha256':original,'original_bytes':len(raw),'native_original_read':True,'native_exact_conversations':True})
  finally:lib.loom_shutdown(ctx)
 result['schema']['after_close']=observe()
 assert result['schema']['before_start']==result['schema']['after_close'],'counts/schema/meta changed'
 result['success']=True
except Exception:result['failure']=traceback.format_exc()
finally:
 if proc:
  proc.terminate()
  try:result['server']['cleanup_exit_code']=proc.wait(timeout=20)
  except subprocess.TimeoutExpired:proc.kill();proc.wait();result['server']['forced_cleanup']=True
 if log:log.close()
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'success':result['success'],'server':result['server'],'native':result['native'],'failure':result.get('failure')}))
raise SystemExit(0 if result['success'] else 1)
