#!/usr/bin/env python3
"""Run actual SQL literals extracted from the pinned Room sources, with SQLite.
This is SQL/query validation, not Room runtime nor Android integration.
"""
import argparse,hashlib,json,re,sqlite3,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.parent.mkdir(parents=True,exist_ok=True)
base='app/src/main/java/com/example/core/data/'
def src(name):return subprocess.check_output(['git','-C',str(a.repo),'show',a.sha+':'+base+name],text=True)
def strings(s):return ''.join(json.loads(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',s))
text=src('KeyboardDatabase.kt');dao=src('Daos.kt');proof=[]
def sql(name):
 m=re.search(r'@Query\(([\s\S]*?)\)\s+(?:suspend )?fun '+name+r'\(',dao)
 # Query search starts at prior annotation; constrain to closest annotation.
 if not m:raise ValueError(name)
 s=m.group(1).split('@Query(')[-1];q=strings(s);proof.append({'source':base+'Daos.kt','method':name,'sql_sha256':hashlib.sha256(q.encode()).hexdigest()});return q
conn=sqlite3.connect(':memory:');conn.row_factory=sqlite3.Row
conn.execute('CREATE TABLE clipboard_items(id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,type TEXT NOT NULL,content TEXT NOT NULL,timestamp INTEGER NOT NULL)')
conn.execute("INSERT INTO clipboard_items(type,content,timestamp) VALUES('TEXT','fixture-old',10)")
for v in range(1,5):
 m=re.search(r'val MIGRATION_'+str(v)+'_'+str(v+1)+r' = object[\s\S]*?^        }',text,re.M)
 if not m:raise ValueError('migration seam')
 for statement in re.finditer(r'db.execSQL\(\s*((?:"(?:[^"\\]|\\.)*"\s*\+?\s*)+)\)',m.group()):
  q=strings(statement.group(1));conn.execute(q);proof.append({'source':base+'KeyboardDatabase.kt','migration':str(v)+'_'+str(v+1),'sql_sha256':hashlib.sha256(q.encode()).hexdigest()})
cases=[]
def test(name,cond):cases.append(dict(id=name,lane='acceptance',status='PASS' if cond else 'FAIL'))
row=conn.execute('SELECT * FROM clipboard_items').fetchone();test('A2-CKP-MIGRATION-1-5',row['content']=='fixture-old' and row['mime']=='text/plain' and len(row['syncId'])==32)
# Snapshot confirmed by the repository; then independently pin row and add a new one.
conn.execute("INSERT INTO clipboard_items(type,content,timestamp,pinned,syncId) VALUES('TEXT','fixture-to-pin',11,0,'fixture-pin')")
ids=[r['id'] for r in conn.execute(sql('allUnpinned'))]
conn.execute("UPDATE clipboard_items SET pinned=1 WHERE content='fixture-to-pin'")
conn.execute("INSERT INTO clipboard_items(type,content,timestamp,pinned,syncId) VALUES('TEXT','fixture-new',12,0,'fixture-new')")
query=sql('trash').replace(':ids',','.join(str(int(i)) for i in ids));conn.execute(query,dict(now=100,batch='audit',allowPinned=0))
trashed=[r['content'] for r in conn.execute(sql('deletedBatch'),dict(batch='audit'))];test('A2-CKP-DELETE-SNAPSHOT',trashed==['fixture-old'])
conn.execute(sql('recordDeletedBatch'),dict(batch='audit'));test('A2-CKP-DELETE-TOMBSTONE',len(list(conn.execute(sql('syncDeletions'))))==1)
q=sql('restoreBatch').replace(':ids',str(ids[0]));n=conn.execute(q,dict(batch='wrong-batch',now=101)).rowcount;test('A2-CKP-UNDO-OPERATION-BOUND',n==0)
n=conn.execute(q,dict(batch='audit',now=101)).rowcount;test('A2-CKP-UNDO-RESTORE',n==1)
# Reopen via real sqlite file outside checkout, preserving SQL data.
conn.commit();file=a.out.with_suffix('.sqlite');disk=sqlite3.connect(file);conn.backup(disk);disk.close();disk=sqlite3.connect(file);test('A2-CKP-SQL-REOPEN',disk.execute('SELECT content FROM clipboard_items WHERE id=1').fetchone()[0]=='fixture-old');disk.close();file.unlink()
result=dict(repo='klb-t/Custom-Keyboard-Pro',sha=a.sha,level='SQLite executing SQL literals from actual pinned Room sources; not Room/KSP/device',cases=cases,proof=proof)
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2));print(json.dumps(cases));raise SystemExit(any(x['status']=='FAIL' for x in cases))
