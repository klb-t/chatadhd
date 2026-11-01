"""Common new-risk gate against own installed libraries; synthetic data only.
Old/candidate phases run in separate processes. No reader imports from checkout.
"""
from pathlib import Path
import argparse,base64,ctypes,hashlib,json,runpy,sqlite3

Native=runpy.run_path(str(Path(__file__).resolve().parents[1]/'structure_readers/verify_native.py'))['Native']
sha=lambda b:hashlib.sha256(b).hexdigest()

def call(n,name,*args):
    f=getattr(n.lib,name);f.argtypes=[ctypes.c_void_p]+[ctypes.c_char_p]*len(args);f.restype=ctypes.c_void_p
    p=f(n.context,*[x.encode() if isinstance(x,str) else x for x in args]);assert p
    try:r=json.loads(ctypes.string_at(p))
    finally:n.lib.loom_free_string(p)
    assert 'error' not in r,r
    return r

def config(n,value):
    f=n.lib.loom_set_config_json;f.argtypes=[ctypes.c_void_p,ctypes.c_char_p];f.restype=ctypes.c_int
    assert f(n.context,json.dumps(value).encode())==0

def imported(n,path):
    f=n.lib.loom_import_file_ex;f.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_void_p];f.restype=ctypes.c_void_p
    p=f(n.context,str(path).encode(),b'{"export_mode":"off"}',None,None);assert p
    try:r=json.loads(ctypes.string_at(p))
    finally:n.lib.loom_free_string(p)
    assert 'error' not in r,r
    return r

def counts(root):
    with sqlite3.connect('file:'+str(root/'chatadhd.db')+'?mode=ro',uri=True) as d:
        return {t:d.execute('SELECT count(*) FROM '+t).fetchone()[0] for t in ('nodes','links','conversations','messages','loom_sources','loom_provenance')}

def blob(root,h):return root/'blobs'/h[:2]/h[2:4]/h

def saved(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n')

def run(stage,root,fixtures,evidence,mode):
    n=Native(stage/'lib/libloom.so',root)
    class Checks(list):
        def append(self,check):
            super().append(check)
            saved(evidence/(mode+'-progress.json'),{'checks':list(self),'library_sha256':sha((stage/'lib/libloom.so').read_bytes()),'completed':False})
            print(check,'PASS',flush=True)
    checks=Checks();historical=[]
    try:
        if mode=='old-create':
            raw=(fixtures/'document.pdf').read_bytes();h=n.upload(raw)
            p=n.ok('client.blob_text',{'hash':h,'source_key':'synthetic:projection-upgrade','name':'document.pdf','format':'pdf'})
            assert p['occurrence_nodes']
            for id in p['occurrence_nodes']:n.ok('client.blob_text_read',{'node_id':id})
            config(n,{'context_execution':{'unified':{'enabled':True,'legacy_seed_ids':[p['occurrence_nodes'][0]],'legacy_relation_hops':0,'include_knowledge':False}},'synthetic_preserve':'keep'})
            saved(evidence/'old.json',p);checks.append('old_pinned_pdf_original_projection_selected_closed')
        elif mode in ('candidate','candidate-resume','candidate-bundle'):
            if mode=='candidate':
                p=json.loads((evidence/'old.json').read_text());before=counts(root)
                check=n.ok('client.blob_text',{'operation':'projection_check','projection_node':p['projection_node']})
                assert check['saved_integrity']=='verified' and check['current_replay']=='refused',check
                assert counts(root)==before
                request={'operation':'projection_reproject','projection_node':p['projection_node'],'expected_receipt_sha256':check['old_receipt_sha256'],'selected_nodes':p['occurrence_nodes']}
                for broken in [dict(request,expected_receipt_sha256='0'*64),dict(request,selected_nodes=['invented']),dict(request,receipt={})]:
                    assert 'error' in n.invoke('client.blob_text',broken)
                    assert counts(root)==before
                old_extract=blob(root,p['derivation']['extraction_blob']);retained=old_extract.read_bytes();old_mode=old_extract.stat().st_mode&0o777;old_extract.chmod(0o600);old_extract.write_bytes(b'synthetic tamper')
                try:
                    assert 'error' in n.invoke('client.blob_text',request) and counts(root)==before
                finally:old_extract.write_bytes(retained);old_extract.chmod(old_mode)
                upgraded=n.ok('client.blob_text',request);assert upgraded['new_projection']!=p['projection_node'] and not upgraded['selection_changed']
                conf=call(n,'loom_get_config');assert conf['synthetic_preserve']=='keep' and conf['context_execution']['unified']['legacy_seed_ids']==[p['occurrence_nodes'][0]]
                assert n.ok('client.blob_text',request)==upgraded
                with sqlite3.connect('file:'+str(root/'chatadhd.db')+'?mode=ro',uri=True) as d:
                    new=json.loads(d.execute('SELECT metadata FROM nodes WHERE id=?',(upgraded['new_projection'],)).fetchone()[0])
                    old=json.loads(d.execute('SELECT metadata FROM nodes WHERE id=?',(p['projection_node'],)).fetchone()[0]);assert old==p
                for id in new['occurrence_nodes']:n.ok('client.blob_text_read',{'node_id':id})
                assert new['derivation']['original_sha256']==p['derivation']['original_sha256']
                assert blob(root,p['derivation']['original_sha256']).read_bytes()==(fixtures/'document.pdf').read_bytes()
                # Explicit test-user decision, not an automatic migration side effect.
                conf['context_execution']['unified']['legacy_seed_ids']=[new['occurrence_nodes'][0]]
                config(n,{'context_execution':conf['context_execution']})
                saved(evidence/'upgrade.json',upgraded);saved(evidence/'new.json',new)
                checks.append('pinned_upgrade_integrity_refusal_explicit_new_version_mapping_no_silent_selection')
            else:
                new=json.loads((evidence/'new.json').read_text())
                historical.extend(json.loads((evidence/'candidate-progress.json').read_text())['checks'])
            if mode!='candidate-bundle':
                xls=n.ok('client.blob_text',{'hash':n.upload((fixtures/'legacy.xls').read_bytes()),'source_key':'synthetic:xls','name':'legacy.xls','format':'xls'})
                values=[n.ok('client.blob_text_read',{'node_id':id})['text'] for id in xls['occurrence_nodes']]
                assert values==['A','2'],values
                assert 'legacy_xls_partial_output_matrix' in xls['warnings']
                assert len([x for x in xls['structure_nodes'] if x['kind']=='sheet'])==1
                for x in xls['structure_nodes']:
                    if x['kind']=='sheet':assert x['attrs']['name'] is None
                    if x['kind']=='cell':assert x['attrs']['original_address'] is None
                video_request={'hash':n.upload((fixtures/'synthetic.mkv').read_bytes()),'source_key':'synthetic:video','name':'synthetic.mkv','format':'video'}
                video=n.ok('client.blob_text',video_request);assert video['has_ocr'] is False and len(video['occurrence_nodes'])==4
                stable=counts(root);assert n.ok('client.blob_text',video_request)==video and counts(root)==stable
                frames=[n.ok('client.blob_text_read',{'node_id':id}) for id in video['occurrence_nodes']]
                assert len({f['provenance']['unit']['id'] for f in frames})==4
                for f in frames:
                    assert f['has_ocr'] is False and blob(root,f['frame_sha256']).read_bytes().startswith(b'\x89PNG')
                    assert f['provenance']['unit']['locator']['json_pointer'].startswith('/frames/')
                assert 'error' in n.invoke('client.blob_text',{**video_request,'receipt':{'frames':[]}}) and counts(root)==stable
                # Real saved blob corruption must fail before reuse, not be silently repaired.
                target=blob(root,frames[0]['frame_sha256']);data=target.read_bytes();target_mode=target.stat().st_mode&0o777;target.chmod(0o600);target.write_bytes(b'corrupt synthetic frame')
                try:assert 'error' in n.invoke('client.blob_text',video_request) and counts(root)==stable
                finally:target.write_bytes(data);target.chmod(target_mode)
                with sqlite3.connect(root/'chatadhd.db') as d:d.execute("CREATE TRIGGER synthetic_video_failure BEFORE INSERT ON nodes WHEN NEW.kind='video_frame' BEGIN SELECT RAISE(ABORT,'synthetic_video_failure'); END")
                try:
                    failure=n.invoke('client.blob_text',{**video_request,'source_key':'synthetic:tx-failure'})
                    assert 'synthetic_video_failure' in json.dumps(failure),failure
                    assert counts(root)==stable
                finally:
                    with sqlite3.connect(root/'chatadhd.db') as d:d.execute('DROP TRIGGER synthetic_video_failure')
                checks.append('installed_xls_partial_and_video_native_hashes_idempotence_refusals_SQLrollback')
            else:
                with sqlite3.connect('file:'+str(root/'chatadhd.db')+'?mode=ro',uri=True) as d:
                    def find(kind,key):
                        return json.loads(d.execute("SELECT metadata FROM nodes WHERE kind=? AND json_extract(metadata,'$.source_key')=?",(kind,key)).fetchone()[0])
                    video=find('video_projection','synthetic:video')
                    xls=json.loads(d.execute("SELECT metadata FROM nodes WHERE json_extract(metadata,'$.schema')='loom.source_text_parts/1' AND json_extract(metadata,'$.method.format')='xls'").fetchone()[0])
                historical.extend(json.loads((evidence/'candidate-resume-progress.json').read_text())['checks'][1:])
            # Existing JSON importer generates conversations/messages; no SQLite fixture insertion.
            imported(n,fixtures/('conversation-reuse.json' if mode=='candidate-bundle' else 'conversation.json'))
            # The import result varies by C API wrapper; query actual rows read-only for its ID.
            with sqlite3.connect('file:'+str(root/'chatadhd.db')+'?mode=ro',uri=True) as d:cid=d.execute('SELECT id FROM conversations ORDER BY rowid DESC LIMIT 1').fetchone()[0]
            msgs=call(n,'loom_get_messages',cid);assert len(msgs)==2
            original=b'{"messages":[{"role":"user","content":"opaque attachment, not another conversation"}]}'
            h=n.upload(original);attachments=[{'hash':h,'name':'original.json','mime':'application/json'}]*12
            f=n.lib.loom_update_message;f.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p];f.restype=ctypes.c_int
            assert f(n.context,msgs[0]['id'].encode(),json.dumps({'attachments':attachments}).encode())==0
            export=n.ok('client.blob_text',{'operation':'bundle_export','conversation_id':cid})
            restore={'operation':'bundle_import','hash':export['hash'],'expected_archive_sha256':export['hash']}
            before=counts(root);assert 'error' in n.invoke('client.blob_text',{**restore,'expected_archive_sha256':'0'*64}) and counts(root)==before
            with sqlite3.connect(root/'chatadhd.db') as d:d.execute("CREATE TRIGGER synthetic_bundle_failure BEFORE UPDATE OF attachments ON messages BEGIN SELECT RAISE(ABORT,'synthetic_bundle_failure'); END")
            try:
                failure=n.invoke('client.blob_text',restore)
                assert 'synthetic_bundle_failure' in json.dumps(failure) and counts(root)==before, failure
            finally:
                with sqlite3.connect(root/'chatadhd.db') as d:d.execute('DROP TRIGGER synthetic_bundle_failure')
            got=n.ok('client.blob_text',restore);after=counts(root)
            assert after['conversations']==before['conversations']+1 and got['verified_slots']==12 and got['complete_assets'] and not got['lossless_database']
            for pointer,id in got['message_mapping'].items():
                m=call(n,'loom_get_message',id);assert m['metadata']['bundle_source_pointer']==pointer
                if pointer=='/messages/0':assert len(m['attachments'])==12 and all(a['hash']==h for a in m['attachments'])
            assert blob(root,h).read_bytes()==original
            assert n.ok('client.blob_text',restore)['conversation_id']==got['conversation_id'] and counts(root)==after
            checks.append('bundle_native_export_managed_original_exact_zip_pointer_ID_rebinding_and_reuse')
            # Shared subprocess change: one small DOCX/CSV read sentinel, no old suites.
            for fmt,raw in [('docx',(fixtures/'sentinel.docx').read_bytes()),('csv','kolumna\nŻółw\n'.encode())]:
                materialized=n.ok('client.blob_text',{'hash':n.upload(raw),'source_key':'synthetic:transport:'+fmt,'name':'sentinel.'+fmt,'format':fmt})
                assert materialized['occurrence_nodes']
                n.ok('client.blob_text_read',{'node_id':materialized['occurrence_nodes'][0]})
            # Confirm the video failure reached the real SQL mutation, not an earlier refusal.
            stable=counts(root)
            with sqlite3.connect(root/'chatadhd.db') as d:d.execute("CREATE TRIGGER synthetic_video_failure BEFORE INSERT ON nodes WHEN NEW.kind='video_frame' BEGIN SELECT RAISE(ABORT,'synthetic_video_failure'); END")
            try:
                failure=n.invoke('client.blob_text',{'hash':video['original_sha256'],'source_key':'synthetic:tx-observed','name':'synthetic.mkv','format':'video'})
                assert 'synthetic_video_failure' in json.dumps(failure),failure
                assert counts(root)==stable
            finally:
                with sqlite3.connect(root/'chatadhd.db') as d:d.execute('DROP TRIGGER synthetic_video_failure')
            checks.append('affected_DOCX_CSV_transport_and_observed_video_SQL_failure')
            # One common reopen, not a suite per adapter.
            saved(evidence/'batch.json',{'xls':xls,'video':video,'bundle':got,'bundle_archive':export['hash']})
            n.close();n=Native(stage/'lib/libloom.so',root)
            for id in [new['occurrence_nodes'][0],xls['occurrence_nodes'][0],video['occurrence_nodes'][0]]:n.ok('client.blob_text_read',{'node_id':id})
            assert call(n,'loom_get_message',got['message_mapping']['/messages/0'])['attachments'][0]['hash']==h
            with sqlite3.connect('file:'+str(root/'chatadhd.db')+'?mode=ro',uri=True) as d:
                assert d.execute('pragma integrity_check').fetchall()==[('ok',)] and not d.execute('pragma foreign_key_check').fetchall()
            checks.append('combined_reopen_SQL_integrity_original_readback')
        elif mode=='bundle-parent-check':
            batch=json.loads((evidence/'batch.json').read_text());got=batch['bundle']
            request={'operation':'bundle_import','hash':batch['bundle_archive'],'expected_archive_sha256':batch['bundle_archive']}
            before=counts(root);assert n.ok('client.blob_text',request)==got and counts(root)==before
            first=got['message_mapping']['/messages/0'];second=got['message_mapping']['/messages/1']
            f=n.lib.loom_update_message;f.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p];f.restype=ctypes.c_int
            original=call(n,'loom_get_message',first)['parent_id']
            assert f(n.context,first.encode(),json.dumps({'parent_id':second}).encode())==0
            try:assert 'error' in n.invoke('client.blob_text',request) and counts(root)==before
            finally:assert f(n.context,first.encode(),json.dumps({'parent_id':original}).encode())==0
            assert n.ok('client.blob_text',request)==got and counts(root)==before
            checks.append('bundle_reuse_parent_binding_and_tamper_refusal_no_repair')
            video=batch['video'];vrequest={'hash':video['original_sha256'],'source_key':'synthetic:video','name':'synthetic.mkv','format':'video'}
            corrupt=dict(video,occurrence_nodes=['invented']);before=counts(root)
            with sqlite3.connect(root/'chatadhd.db') as d:d.execute('UPDATE nodes SET metadata=? WHERE id=?',(json.dumps(corrupt),video['projection_node']))
            try:assert 'error' in n.invoke('client.blob_text',vrequest) and counts(root)==before
            finally:
                with sqlite3.connect(root/'chatadhd.db') as d:d.execute('UPDATE nodes SET metadata=? WHERE id=?',(json.dumps(video),video['projection_node']))
            assert n.ok('client.blob_text',vrequest)==video and counts(root)==before
            checks.append('video_cache_receipt_tamper_refused_without_graph_repair_claim')

        elif mode=='final-sentinels':
            empty=n.ok('client.blob_text',{'hash':n.upload(b''),'source_key':'synthetic:empty-bytes','name':'empty.txt','format':'text'})
            assert not empty['occurrence_nodes']
            checked=n.ok('client.blob_text',{'operation':'projection_check','projection_node':empty['projection_node']})
            assert checked['current_replay']=='not_exercised'
            imported(n,fixtures/'conversation.json')
            with sqlite3.connect('file:'+str(root/'chatadhd.db')+'?mode=ro',uri=True) as d:cid=d.execute('SELECT id FROM conversations ORDER BY rowid DESC LIMIT 1').fetchone()[0]
            first=call(n,'loom_get_messages',cid)[0]
            f=n.lib.loom_update_message;f.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p];f.restype=ctypes.c_int
            assert f(n.context,first['id'].encode(),b'{"attachments":[{"name":"synthetic-unmanaged.bin"}]}')==0
            assert 'error' in n.invoke('client.blob_text',{'operation':'bundle_export','conversation_id':cid})
            partial=n.ok('client.blob_text',{'operation':'bundle_export','conversation_id':cid,'allow_incomplete':True})
            assert not partial['complete_assets']
            request={'operation':'bundle_import','hash':partial['hash'],'expected_archive_sha256':partial['hash']}
            before=counts(root);assert 'error' in n.invoke('client.blob_text',request) and counts(root)==before
            restored=n.ok('client.blob_text',{**request,'allow_incomplete':True})
            assert restored['status']=='INCOMPLETE' and not restored['complete_assets'] and restored['verified_slots']==0
            checks.append('empty_projection_not_exercised_and_explicit_bundle_INCOMPLETE_refusal')
        elif mode=='old-postcopy':
            p=json.loads((evidence/'new.json').read_text());old=json.loads((evidence/'old.json').read_text())
            assert 'error' in n.invoke('client.blob_text_read',{'node_id':p['occurrence_nodes'][0]})
            # Old version retained and readable by its matching old installation.
            n.ok('client.blob_text_read',{'node_id':old['occurrence_nodes'][0]})
            assert call(n,'loom_get_config')['context_execution']['unified']['legacy_seed_ids']==[p['occurrence_nodes'][0]]
            checks.append('old_runtime_new_selected_projection_REFUSED_not_lossless_inplace')
        elif mode=='old-restored':
            p=json.loads((evidence/'old.json').read_text());new=json.loads((evidence/'new.json').read_text())
            n.ok('client.blob_text_read',{'node_id':p['occurrence_nodes'][0]})
            assert 'error' in n.invoke('client.blob_text_read',{'node_id':new['occurrence_nodes'][0]})
            assert call(n,'loom_get_config')['context_execution']['unified']['legacy_seed_ids']==[p['occurrence_nodes'][0]]
            assert sha(blob(root,p['derivation']['original_sha256']).read_bytes())==p['derivation']['original_sha256']
            checks.append('cold_restore_old_runtime_original_and_selection_RPO_postcopy_changes_lost')
        else:raise ValueError(mode)
        saved(evidence/(mode+'-result.json'),{'checks':checks,'historical_checks_not_rerun':historical,'count':len(checks),'mode':mode,'library_sha256':sha((stage/'lib/libloom.so').read_bytes()),'synthetic_only':True})
        print(json.dumps({'mode':mode,'checks':checks}),flush=True)
    finally:n.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('stage',type=Path);p.add_argument('root',type=Path);p.add_argument('fixtures',type=Path);p.add_argument('evidence',type=Path);p.add_argument('mode');a=p.parse_args();run(**vars(a))
