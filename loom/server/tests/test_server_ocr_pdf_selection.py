"""Combined selected OCR/PDF gate: real native HTTP and local synthetic provider."""
import hashlib
from pathlib import Path
from server_flow_harness import HttpFlowCase

FIXTURES = Path(__file__).resolve().parents[2]/'tests/fixtures'


class OcrPdfSelection(HttpFlowCase):
    def post(self, path, body):
        r = self.client.post(self.srv.base+path, json=body, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertNotIn('error', r.json())
        return r.json()

    def upload(self, raw, name, mime):
        r = self.client.post(self.srv.base+'/api/client/blob/upload',
                             files={'file': (name, raw, mime)}, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json()['hash'], hashlib.sha256(raw).hexdigest())
        return r.json()

    def select(self, node):
        self.patch({'context_execution': {'unified': {'enabled': True, 'include_knowledge': False,
                    'legacy_seed_ids': [node], 'legacy_relation_hops': 0}}})

    def system(self):
        return '\n'.join(self.text(m) for m in self.provider.calls[-1]['request']['messages'] if m['role']=='system')

    def new_conversation(self):
        r=self.client.post(self.srv.base+'/api/conversations', json={'title':'Wybrany fragment'}, timeout=15)
        self.assertEqual(r.status_code,201,r.text); self.conv=r.json()['id']

    def original(self, digest, raw):
        r=self.client.get(self.srv.base+'/api/client/blob/'+digest+'/raw',timeout=15)
        self.assertEqual(r.status_code,200,r.text);self.assertEqual(r.content,raw)

    def test_selected_existing_local_ocr_region_and_restart(self):
        raw=(FIXTURES/'local_ocr/synthetic-pl.png').read_bytes()
        blob=self.upload(raw,'synthetic.png','image/png')
        self.success('Odczytaj tekst.', ['blob:'+blob['hash']+'/synthetic.png'])
        r=self.client.get(self.srv.base+'/api/graph/nodes',params={'kind':'image_region','limit':1000},timeout=15)
        self.assertEqual(r.status_code,200,r.text)
        nodes=[n for n in r.json() if n['metadata']['unit']['attrs']['original_sha256']==blob['hash']]
        self.assertGreater(len(nodes),1)
        chosen=next(n for n in nodes if len(n['content'])>3)
        siblings=[n['content'] for n in nodes if n['content'] not in chosen['content'] and len(n['content'])>3]
        self.assertTrue(siblings)
        self.new_conversation();self.select(chosen['id'])
        self.success('Przytocz wskazany fragment.', include_history=False)
        system=self.system()
        self.assertIn(chosen['content'],system);self.assertIn(chosen['id'],system)
        self.assertIn('inferred',system);self.assertIn('null',system)
        for text in siblings:self.assertNotIn(text,system)
        self.restart();self.success('Ponownie przytocz wskazany fragment.',include_history=False)
        self.assertIn(chosen['content'],self.system());self.assertIn(chosen['id'],self.system())
        self.original(blob['hash'],raw)
        # Native contract cases cover denial/corruption; this tests the actual
        # runtime callback's refusal for a missing original, before any provider.
        paths=list((self.root/'guests').glob('*/blobs/**/'+blob['hash']))
        self.assertEqual(len(paths),1)
        path=paths[0];saved=path.with_name(path.name+'.held-by-test')
        path.rename(saved)
        try:
            count=len(self.provider.calls)
            events=self.send('Przytocz wskazany fragment.',include_history=False)
            self.assertTrue(any(e['type']=='error' for e in events),events)
            self.assertEqual(len(self.provider.calls),count)
        finally:saved.rename(path)

    def test_pdf_projection_selected_context_reopen_and_receipt_refusal(self):
        raw=(FIXTURES/'documents/unicode-compressed.pdf').read_bytes()
        blob=self.upload(raw,'synthetic.pdf','application/pdf')
        req={'hash':blob['hash'],'source_key':'synthetic:batch-pdf','name':'synthetic.pdf','format':'pdf'}
        result=self.post('/api/client/blob/text',req)
        self.assertEqual(len(result['structure_nodes']['pages']),2)
        self.assertEqual(result['coordinate'],'derived_utf8_bytes')
        self.assertEqual(result['derivation']['original_sha256'],blob['hash'])
        reads=[self.post('/api/client/blob/text/read',{'node_id':n}) for n in result['occurrence_nodes']]
        self.assertGreaterEqual(len(reads),2)
        chosen=next(p for p in reads if p['text'].strip())
        self.assertEqual(chosen['provenance']['structure_evidence'],'inferred')
        self.assertEqual(self.post('/api/client/blob/text',req),result)
        forged=self.client.post(self.srv.base+'/api/client/blob/text',json={**req,'receipt':{'approved':True}},timeout=30)
        self.assertGreaterEqual(forged.status_code,400)
        self.select(chosen['node_id']);self.success('Przytocz wybrany fragment PDF.',include_history=False)
        self.assertIn(chosen['text'],self.system());self.assertIn(chosen['node_id'],self.system())
        for p in reads:
            if p['text'].strip() and p['text'] not in chosen['text']:self.assertNotIn(p['text'],self.system())
        self.restart()
        self.assertEqual(self.post('/api/client/blob/text/read',{'node_id':chosen['node_id']}),chosen)
        self.original(blob['hash'],raw)
        # Change only our own immutable test receipt by replacing its pathname.
        digest=result['derivation']['extraction_blob']
        paths=list((self.root/'guests').glob('*/blobs/**/'+digest));self.assertEqual(len(paths),1)
        path=paths[0];saved=path.with_name(path.name+'.held-by-test');path.rename(saved)
        try:
            path.write_bytes(b'corrupt synthetic extraction')
            refused=self.client.post(self.srv.base+'/api/client/blob/text/read',json={'node_id':chosen['node_id']},timeout=30)
            self.assertGreaterEqual(refused.status_code,400)
            count=len(self.provider.calls)
            events=self.send('Przytocz wybrany fragment PDF.',include_history=False)
            self.assertTrue(any(e['type']=='error' for e in events),events)
            self.assertEqual(len(self.provider.calls),count)
        finally:path.unlink();saved.rename(path)
