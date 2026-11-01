"""Material-risk checks against the installed library; shared app/browser covers positive graphs.
Only synthetic blobs are damaged temporarily. No old format suites are replayed.
"""
import hashlib,io,json,runpy,sqlite3,sys,zipfile
from pathlib import Path
base=Path(__file__).resolve().parents[1]
helpers=runpy.run_path(str(base/'structure_readers/verify_native.py'))
Native,counts=helpers['Native'],helpers['counts']

def run(stage,fixtures,root):
    root.mkdir(mode=0o700)
    n=Native(stage/'lib/libloom.so',root);checks=[]
    try:
        raw=(fixtures/'workbook.xlsx').read_bytes()
        req={'hash':n.upload(raw),'source_key':'synthetic:file-graph-risk','name':'workbook.xlsx','format':'xlsx'}
        made=n.ok('client.blob_text',req)
        chosen=next(i for i,o in enumerate(made['observations']) if o['text']=='Żółw WYBRANY')
        node=made['occurrence_nodes'][chosen]
        baseline=n.ok('client.blob_text_read',{'node_id':node})
        before=counts(root)
        assert n.ok('client.blob_text',req)==made and counts(root)==before
        assert 'error' in n.invoke('client.blob_text',dict(req,receipt={'trusted':True}))
        assert counts(root)==before
        checks.append('native idempotence and client receipt refusal before mutation')
        d=made['derivation']
        for digest in (req['hash'],made['host_reader']['receipt_blob'],d['extraction_blob'],d['text_sha256']):
            paths=[p for p in root.rglob('*') if p.is_file() and (p.name==digest or (p.name==digest[2:] and p.parent.name==digest[:2]))]
            assert len(paths)==1
            p=paths[0];original=p.read_bytes();assert hashlib.sha256(original).hexdigest()==digest
            def put(value):
                tmp=p.with_name(p.name+'.synthetic-fault');tmp.write_bytes(value);tmp.replace(p)
            try:
                put(original+b' ')
                assert 'error' in n.invoke('client.blob_text_read',{'node_id':node})
            finally:put(original)
        assert n.ok('client.blob_text_read',{'node_id':node})==baseline
        checks.append('original, host receipt, extraction and derived blob tamper refused')
        with sqlite3.connect(root/'chatadhd.db') as db:
            old=db.execute('SELECT metadata FROM nodes WHERE id=?',(node,)).fetchone()[0]
            meta=json.loads(old);meta['unit']['locator']['byte_start']+=1
            db.execute('UPDATE nodes SET metadata=? WHERE id=?',(json.dumps(meta),node));db.commit()
            try:assert 'error' in n.invoke('client.blob_text_read',{'node_id':node})
            finally:db.execute('UPDATE nodes SET metadata=? WHERE id=?',(old,node));db.commit()
        checks.append('persisted locator tamper refused')
        bad=io.BytesIO()
        with zipfile.ZipFile(bad,'w') as z:z.writestr('ok.txt','synthetic first member');z.writestr('z-bad.xlsx',b'not a workbook')
        badreq={'hash':n.upload(bad.getvalue()),'source_key':'synthetic:rollback','name':'bad.zip','format':'zip'}
        before=counts(root);assert 'error' in n.invoke('client.blob_text',badreq)
        assert counts(root)==before # SQL only; content-addressed orphan bytes can remain.
        checks.append('recursive member failure rolls back SQL graph/source records; not physical blob rollback')
        # One continuity sample per affected representation, not old suites.
        saved=[]
        samples=[('text','Żółw tekst'.encode()),('csv','a;b\r\nŻółw;Łódź\r\n'.encode()),('docx',(base/'fixtures/docx_parts/base.docx').read_bytes())]
        for fmt,data in samples:
            result=n.ok('client.blob_text',{'hash':n.upload(data),'source_key':'synthetic:sentinel:'+fmt,'name':'synthetic.'+fmt,'format':fmt})
            assert result['occurrence_nodes']
            saved.append(n.ok('client.blob_text_read',{'node_id':result['occurrence_nodes'][0]}))
        n.close();n=Native(stage/'lib/libloom.so',root)
        assert n.ok('client.blob_text_read',{'node_id':node})==baseline
        for r in saved:assert n.ok('client.blob_text_read',{'node_id':r['node_id']})==r
        checks.append('one shared reopen: selected XLSX plus UTF8/CSV/DOCX sentinels')
        return {'checks':checks,'passed':len(checks),'counts':counts(root)}
    finally:n.close()
if __name__=='__main__':print(json.dumps(run(*(Path(v).resolve() for v in sys.argv[1:])),ensure_ascii=False,indent=2))
