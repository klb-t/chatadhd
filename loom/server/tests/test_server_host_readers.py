"""Packaged pinned CSV/Python -> real native HTTP/context/reopen; synthetic only."""
import hashlib
from pathlib import Path
from server_flow_harness import HttpFlowCase


class HostReaders(HttpFlowCase):
    def post(self, path, body):
        r=self.client.post(self.srv.base+path,json=body,timeout=30)
        self.assertEqual(r.status_code,200,r.text);self.assertNotIn('error',r.json());return r.json()

    def upload(self, raw, kind, key):
        r=self.client.post(self.srv.base+'/api/client/blob/upload',files={'file':('synthetic.'+kind,raw,'text/plain')},timeout=30)
        self.assertEqual(r.status_code,200,r.text)
        request={'hash':r.json()['hash'],'source_key':key,'name':'synthetic.'+kind,'format':kind}
        self.assertEqual(request['hash'],hashlib.sha256(raw).hexdigest())
        return self.post('/api/client/blob/text',request),request

    def read(self, node):
        return self.post('/api/client/blob/text/read',{'node_id':node})

    def choose(self,node):
        self.patch({'context_execution':{'unified':{'enabled':True,'include_knowledge':False,
            'legacy_seed_ids':[node],'legacy_relation_hops':0}}})

    def system(self):
        return '\n'.join(self.text(m) for m in self.provider.calls[-1]['request']['messages'] if m['role']=='system')

    def test_csv_package_selected_cell_context_and_reopen(self):
        raw=b'\xef\xbb\xbf'+'imię;miasto;opis\r\nŁukasz;Łódź;"pierwsza\r\ndruga; część"\r\nNIEWYBRANY;sekcja;tekst\r\n'.encode()
        result,request=self.upload(raw,'csv','synthetic:http-csv')
        reads=[self.read(n) for n in result['occurrence_nodes']]
        chosen=next(r for r in reads if r['provenance']['unit']['kind']=='csv_cell' and r['text']=='"pierwsza\r\ndruga; część"')
        self.assertEqual(chosen['provenance']['unit']['attrs']['physical_lines'],[2,3])
        self.assertEqual(self.post('/api/client/blob/text',request),result)
        for field in ['csv_receipt','profile','reader','pack']:
            refused=self.client.post(self.srv.base+'/api/client/blob/text',json={**request,field:{}},timeout=30)
            self.assertGreaterEqual(refused.status_code,400)
            self.assertIn('host_reader_request_override_refused',refused.text)
        self.choose(chosen['node_id'])
        self.success('Przytocz wybraną komórkę grafu.')
        self.assertIn(chosen['text'],self.system())
        self.assertNotIn('NIEWYBRANY',self.system())
        # Actual selected node ID is retained in the source heading/provenance.
        self.assertIn(chosen['node_id'],self.system())
        self.restart()
        self.assertEqual(self.read(chosen['node_id']),chosen)
        self.success('Ponownie przytocz wybraną komórkę.')
        self.assertIn(chosen['text'],self.system())
        original=self.client.get(self.srv.base+'/api/client/blob/'+request['hash']+'/raw',timeout=15)
        self.assertEqual(original.status_code,200);self.assertEqual(original.content,raw)
        # Same-account graph edits cannot erase the host binding to bypass replay.
        self.assertEqual(len(self.provider.calls),2)

    def test_python_host_derived_declaration_context_and_reopen(self):
        raw='raise RuntimeError("NEVER_EXECUTE")\r\nclass Żółw:\r\n    def wybrana(self): return "żółć"\r\n'.encode()
        result,request=self.upload(raw,'python_ast','synthetic:http-python')
        self.assertEqual(len(result['occurrence_nodes']),3)
        selected=self.read(result['occurrence_nodes'][2])
        self.assertEqual(selected['text'],'def wybrana(self): return "żółć"')
        self.assertEqual(selected['provenance']['structural_evidence_class'],'derived')
        forged=self.client.post(self.srv.base+'/api/client/blob/text',json={**request,'python_ast_receipt':{'approved':True}},timeout=30)
        self.assertGreaterEqual(forged.status_code,400)
        self.choose(selected['node_id']);self.success('Pokaż wskazaną deklarację.')
        self.assertIn(selected['text'],self.system());self.assertNotIn('NEVER_EXECUTE',self.system())
        self.restart();self.assertEqual(self.read(selected['node_id']),selected)
        self.success('Przytocz tę deklarację po otwarciu.')
        self.assertIn(selected['text'],self.system())
