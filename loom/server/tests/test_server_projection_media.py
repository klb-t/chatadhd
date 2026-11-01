"""One combined actual HTTP entry/reopen/context gate; synthetic provider only."""
import hashlib,os
from pathlib import Path
from server_flow_harness import HttpFlowCase

class ProjectionMedia(HttpFlowCase):
    def post(self,path,body):
        r=self.client.post(self.srv.base+path,json=body,timeout=40)
        self.assertEqual(r.status_code,200,r.text);self.assertNotIn('error',r.json());return r.json()
    def upload(self,raw,name):
        r=self.client.post(self.srv.base+'/api/client/blob/upload',files={'file':(name,raw,'application/octet-stream')},timeout=30)
        self.assertEqual(r.status_code,200,r.text);self.assertEqual(r.json()['hash'],hashlib.sha256(raw).hexdigest());return r.json()['hash']
    def test_combined_new_admission_selection_and_restart(self):
        fixtures=Path(os.environ['LOOM_PROJECTION_FIXTURES'])
        raw=(fixtures/'legacy.xls').read_bytes();h=self.upload(raw,'legacy.xls')
        xls=self.post('/api/client/blob/text',{'hash':h,'source_key':'synthetic:http-xls','name':'legacy.xls','format':'xls'})
        chosen=self.post('/api/client/blob/text/read',{'node_id':xls['occurrence_nodes'][0]})
        self.assertEqual(chosen['text'],'A')
        self.patch({'context_execution':{'unified':{'enabled':True,'include_knowledge':False,'legacy_seed_ids':[chosen['node_id']],'legacy_relation_hops':0}}})
        self.success('Przytocz tylko wybraną komórkę.',include_history=False)
        system='\n'.join(self.text(m) for m in self.provider.calls[-1]['request']['messages'] if m['role']=='system')
        self.assertIn(chosen['node_id'],system);self.assertIn(chosen['text'],system)
        self.assertNotIn(xls['occurrence_nodes'][1],system)
        p=self.post('/api/client/blob/text',{'operation':'projection_check','projection_node':xls['projection_node']})
        self.assertEqual(p['current_replay'],'compatible')
        video=self.post('/api/client/blob/text',{'hash':self.upload((fixtures/'synthetic.mkv').read_bytes(),'synthetic.mkv'),'source_key':'synthetic:http-video','name':'synthetic.mkv','format':'video'})
        self.assertFalse(video['has_ocr']);self.assertEqual(len(video['occurrence_nodes']),4)
        frame=self.post('/api/client/blob/text/read',{'node_id':video['occurrence_nodes'][0]})
        self.assertFalse(frame['has_ocr'])
        exported=self.post('/api/client/blob/text',{'operation':'bundle_export','conversation_id':self.conv})
        restored=self.post('/api/client/blob/text',{'operation':'bundle_import','hash':exported['hash'],'expected_archive_sha256':exported['hash']})
        self.assertTrue(restored['complete_assets']);self.assertFalse(restored['lossless_database'])
        refused=self.client.post(self.srv.base+'/api/client/blob/text',json={'hash':h,'source_key':'synthetic:forged','name':'legacy.xls','format':'xls','receipt':{}},timeout=20)
        self.assertGreaterEqual(refused.status_code,400)
        self.restart()
        self.assertEqual(self.post('/api/client/blob/text/read',{'node_id':chosen['node_id']}),chosen)
        self.assertEqual(self.post('/api/client/blob/text/read',{'node_id':frame['node_id']}),frame)
        self.success('Ponownie przytocz wybraną komórkę.',include_history=False)
        system='\n'.join(self.text(m) for m in self.provider.calls[-1]['request']['messages'] if m['role']=='system')
        self.assertIn(chosen['node_id'],system)
        original=self.client.get(self.srv.base+'/api/client/blob/'+h+'/raw',timeout=20)
        self.assertEqual(original.status_code,200);self.assertEqual(original.content,raw)
