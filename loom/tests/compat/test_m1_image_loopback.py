#!/usr/bin/env python3
"""M1 native acceptance, NOT a model quality test.

After integrator's reserved build:
  OMP_THREAD_LIMIT=1 LOOM_LIBRARY=/absolute/new/libloom.so python3 <this file>
No compiler or external endpoint is launched here. Missing library is an ERROR,
not a skipped/passed gate. All runtime data and credential strings are synthetic.
"""
import base64
import ctypes
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT/'loom/tests/fixtures/local_ocr/synthetic-pl.png'
EXPECTED = (FIXTURE.parent/'expected.txt').read_text().strip()
def encoded(value): return json.dumps(value,ensure_ascii=False).encode()

class ImageLoopbackTests(unittest.TestCase):
    def setUp(self):
        library = os.environ.get('LOOM_LIBRARY')
        if not library:
            raise RuntimeError('M1 heavy gate not run: explicit integrator-built LOOM_LIBRARY required')
        self.lib = ctypes.CDLL(str(Path(library).resolve(strict=True)))
        ptr=ctypes.c_void_p; text=ctypes.c_char_p
        signatures={
            'loom_init_ex':([text,ptr],ptr), 'loom_shutdown':([ptr],None),
            'loom_free_string':([ptr],None), 'loom_set_config_json':([ptr,text],ctypes.c_int),
            'loom_set_secret':([ptr,text,text],ctypes.c_int),
            'loom_invoke':([ptr,text,text,text],ptr),
            'loom_create_conversation':([ptr,text],ptr), 'loom_get_messages':([ptr,text],ptr),
            'loom_chat_ex':([ptr,text,ptr,ptr],ptr), 'loom_import_file':([ptr,text,text,ptr,ptr],ptr),
        }
        for name,(args,restype) in signatures.items():
            fn=getattr(self.lib,name);fn.argtypes=args;fn.restype=restype
        self.temp=tempfile.TemporaryDirectory(prefix='m1-native-loopback-')
        self.addCleanup(self.temp.cleanup)
        self.calls=[]
        calls=self.calls
        class Provider(BaseHTTPRequestHandler):
            def do_POST(self):
                body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                calls.append(body)
                # Deliberately no extraction/answer logic: this proves transport only.
                answer=encoded({'choices':[{'message':{'role':'assistant','content':'SYNTHETIC_LOOPBACK_ACK'}}]})
                self.send_response(200);self.send_header('Content-Type','application/json')
                self.send_header('Content-Length',str(len(answer)));self.end_headers();self.wfile.write(answer)
            def log_message(self,*args): pass
        self.server=ThreadingHTTPServer(('127.0.0.1',0),Provider)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        self.addCleanup(self.stop_server)
        self.ctx=None;self.open();self.addCleanup(self.close)
        self.config({'base_url':f'http://127.0.0.1:{self.server.server_port}',
                     'default_model':'synthetic/local', 'auto_title':False,'semantic_analysis':False,
                     'system_prompt':'','stream':False})
        self.assertEqual(self.lib.loom_set_secret(self.ctx,b'api_key',b'synthetic-loopback-key'),0)

    def stop_server(self):
        self.server.shutdown();self.server.server_close();self.thread.join(timeout=5)
    def close(self):
        if self.ctx: self.lib.loom_shutdown(self.ctx);self.ctx=None
    def open(self):
        self.ctx=self.lib.loom_init_ex(encoded({'data_dir':self.temp.name,'start_workers':False}),None)
        self.assertTrue(self.ctx)
    def read(self,pointer):
        self.assertTrue(pointer)
        try: return json.loads(ctypes.string_at(pointer))
        finally: self.lib.loom_free_string(pointer)
    def config(self,patch): self.assertEqual(self.lib.loom_set_config_json(self.ctx,encoded(patch)),0)
    def new_conv(self): return self.read(self.lib.loom_create_conversation(self.ctx,b'M1 synthetic'))['id']
    def send(self,conv,message,attachments=(),**options):
        req={'conv_id':conv,'message':message,'attachments':list(attachments),'stream':False,
             'include_memory':False,'include_graph_memory':False,'selector_profile':'compat'}
        req.update(options)
        return self.read(self.lib.loom_chat_ex(self.ctx,encoded(req),None,None))
    def imported(self): return self.read(self.lib.loom_import_file(self.ctx,str(FIXTURE).encode(),None,None,None))
    def messages(self,conv): return self.read(self.lib.loom_get_messages(self.ctx,conv.encode()))
    def parts(self):
        return [p for m in self.calls[-1]['messages'] if isinstance(m.get('content'),list) for p in m['content']]
    def image_capability(self,value):
        self.config({'model_capabilities':{'synthetic/local':{'accepts':['text','image'] if value else ['text']}}})

    def synthetic_secret_recipe(self):
        path=Path(self.temp.name)/'profiles';path.mkdir(exist_ok=True)
        (path/'chat_secrets.pack').write_text(json.dumps({
            'schema':'loom.runtime_profile_overlay/1','domain':'chat_secrets',
            'overrides':{'patterns':[{'id':'synthetic_ocr_field','regex':'godzina 09:00','flags':'',
                                     'label':{'pl':'pole syntetyczne','en':'synthetic field'}}]}}))
    def image_state(self):
        with sqlite3.connect(Path(self.temp.name)/'chatadhd.db') as db:
            counts={t:db.execute('SELECT count(*) FROM '+t).fetchone()[0]
                    for t in ['loom_sources','loom_blobs','nodes','links']}
            blobs=[row[0] for row in db.execute('SELECT hash FROM loom_blobs')]
            counts['image_links']=db.execute("SELECT * FROM links WHERE src GLOB 'n_ocr_*' OR dst GLOB 'n_ocr_*' ORDER BY id").fetchall()
        counts['bytes']={h:[hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in Path(self.temp.name).rglob(h)] for h in blobs}
        return counts

    def test_ocr_secret_preview_hold_refusal_have_no_new_image_artifacts_or_transport(self):
        self.synthetic_secret_recipe()
        # External file has never been imported. The preview must not import it.
        conv=self.new_conv();before=self.image_state()
        preview=self.read(self.lib.loom_invoke(self.ctx,b'chat',b'request',encoded({
            'preview':True,'conv_id':conv,'message':'Odczytaj tekst',
            'attachments':[str(FIXTURE)],'include_memory':False,'include_graph_memory':False})))
        self.assertNotIn('error',preview)
        self.assertEqual(self.image_state(),before);self.assertEqual(self.calls,[])
        for mode,status in [('ask','needs_confirmation'),('never','refused')]:
            self.config({'secrets_in_chat':mode})
            result=self.send(conv,'Odczytaj tekst',[str(FIXTURE)])
            self.assertEqual(result['secret_pending']['status'],status)
            self.assertEqual(self.image_state(),before);self.assertEqual(self.calls,[])
        self.assertEqual(hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),
                         json.loads((FIXTURE.parent/'recognized.json').read_text())['source_sha256'])

    def test_reopened_history_ocr_secret_requires_fresh_consent_and_disabled_history_is_absent(self):
        imported=self.imported();conv=imported['conversations'][0]['id']
        self.close();self.synthetic_secret_recipe();self.open()
        before=self.image_state()
        held=self.send(conv,'Odczytaj tekst') # no new attachment
        self.assertEqual(held['secret_pending']['status'],'needs_confirmation')
        self.assertEqual(self.calls,[]);self.assertEqual(self.image_state(),before)
        old=self.send(conv,'Odczytaj tekst',secret_confirmation={'approved':True})
        self.assertEqual(old['secret_pending']['status'],'needs_confirmation')
        self.assertEqual(self.calls,[]);self.assertEqual(self.image_state(),before)
        sent=self.send(conv,'Odczytaj tekst',include_history=False)
        self.assertNotIn('error',sent);self.assertEqual(len(self.calls),1)
        self.assertNotIn('godzina 09:00',json.dumps(self.calls[0],ensure_ascii=False))
        # A completed dialog adds message membership edges, not image artifacts.
        after=self.image_state()
        self.assertEqual({k:v for k,v in after.items() if k!='links'},
                         {k:v for k,v in before.items() if k!='links'})
        self.assertEqual(after['links']-before['links'],2)

    def test_import_region_locator_reopen_no_duplicates_and_question_context(self):
        imported=self.imported();self.assertNotIn('error',imported)
        conv=imported['conversations'][0]['id'];messages=self.messages(conv)
        self.assertEqual(len(messages),1);self.assertEqual(messages[0]['role'],'user')
        self.assertEqual(messages[0]['text'],EXPECTED)
        projection=messages[0]['metadata']['image_representation']
        self.assertEqual(len(projection['region_nodes']),13)
        self.assertEqual(self.calls,[])
        self.close()
        # Verify persisted native stores and exact quote -> derived JSON -> original chain.
        db=sqlite3.connect(Path(self.temp.name)/'chatadhd.db')
        try:
            counts=tuple(db.execute('SELECT count(*) FROM '+t).fetchone()[0] for t in ['nodes','links','loom_sources','messages'])
            original=db.execute('SELECT blob_hash FROM loom_sources WHERE id=?',(projection['source_id'],)).fetchone()[0]
            self.assertEqual(original,hashlib.sha256(FIXTURE.read_bytes()).hexdigest())
            candidates=list(Path(self.temp.name).rglob(original))
            self.assertEqual(len(candidates),1);self.assertEqual(candidates[0].read_bytes(),FIXTURE.read_bytes())
            rep=list(Path(self.temp.name).rglob(projection['representation_blob']))
            self.assertEqual(len(rep),1);derived=json.loads(rep[0].read_text())
            for nid in projection['region_nodes']:
                node=json.loads(db.execute('SELECT metadata FROM nodes WHERE id=?',(nid,)).fetchone()[0])
                observation=node['observation'];location=observation['locator']
                self.assertEqual(location['source'],projection['representation_source'])
                index=int(location['json_pointer'].split('/')[2])
                self.assertEqual(observation['text'],derived['regions'][index]['text'])
                self.assertEqual(node['unit']['attrs']['original_sha256'],original)
        finally: db.close()
        self.open();again=self.imported();self.assertTrue(again['already_imported'])
        self.assertEqual(again['conversations'][0]['id'],conv)
        db=sqlite3.connect(Path(self.temp.name)/'chatadhd.db')
        try: self.assertEqual(counts,tuple(db.execute('SELECT count(*) FROM '+t).fetchone()[0] for t in ['nodes','links','loom_sources','messages']))
        finally: db.close()
        response=self.send(conv,'Odczytaj termin spotkania z dokumentu.')
        self.assertNotIn('error',response);self.assertEqual(len(self.calls),1)
        self.assertIn('godzina 09:00.',json.dumps(self.calls[0],ensure_ascii=False))
        self.assertFalse(any(p.get('type')=='image_url' for p in self.parts()))
        self.assertEqual([m['role'] for m in self.calls[0]['messages'] if (EXPECTED in m['content'] if isinstance(m.get('content'),str) else any(EXPECTED in p.get('text','') for p in m.get('content',[])))],['user'])

    def test_bound_consent_dispatches_once_persists_then_changed_question_holds(self):
        # Integrator: positive resend across the actual ABI/HTTP boundary.
        self.synthetic_secret_recipe()
        conv = self.new_conv(); before = self.image_state()
        held = self.send(conv, 'Odczytaj tekst', [str(FIXTURE)])
        self.assertEqual(held['secret_pending']['status'], 'needs_confirmation')
        self.assertEqual(self.calls, []); self.assertEqual(self.image_state(), before)
        approval = {'approved': True, 'image_payload_sha256': held['secret_pending']['image_payload_sha256']}
        sent = self.send(conv, 'Odczytaj tekst', [str(FIXTURE)], secret_confirmation=approval)
        self.assertNotIn('error', sent); self.assertNotIn('secret_pending', sent)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(any(EXPECTED in part.get('text', '') for part in self.parts()))
        after = self.image_state()
        self.assertGreater(after['loom_sources'], before['loom_sources'])
        self.assertGreater(after['nodes'], before['nodes'])
        changed = self.send(conv, 'Odczytaj inny tekst', [str(FIXTURE)], secret_confirmation=approval)
        self.assertEqual(changed['secret_pending']['status'], 'needs_confirmation')
        self.assertNotEqual(changed['secret_pending']['image_payload_sha256'], approval['image_payload_sha256'])
        self.assertEqual(len(self.calls), 1); self.assertEqual(self.image_state(), after)

    def test_upload_ref_no_prompt_ocr_then_image_exact_payload(self):
        imported=self.imported();conv=self.new_conv();self.image_capability(True)
        ref='blob:'+imported['blob_hash']+'/synthetic-pl.png'
        response=self.send(conv,'',[ref]);self.assertNotIn('error',response)
        self.assertEqual(len(self.calls),1)
        parts=self.parts();images=[p for p in parts if p.get('type')=='image_url']
        self.assertEqual(len(images),1)
        self.assertEqual(base64.b64decode(images[0]['image_url']['url'].split(',',1)[1]),FIXTURE.read_bytes())
        self.assertTrue(any(EXPECTED in p.get('text','') for p in parts))
        self.assertLess(next(i for i,p in enumerate(parts) if EXPECTED in p.get('text','')),parts.index(images[0]))

    def test_text_goal_and_image_goal_select_distinct_inputs(self):
        imported=self.imported();ref='blob:'+imported['blob_hash']+'/synthetic-pl.png';self.image_capability(True)
        self.assertNotIn('error',self.send(self.new_conv(),'Odczytaj tekst',[ref]))
        self.assertFalse(any(p.get('type')=='image_url' for p in self.parts()))
        self.assertNotIn('error',self.send(self.new_conv(),'Jaki kolor ma obraz?',[ref]))
        self.assertTrue(any(p.get('type')=='image_url' for p in self.parts()))

    def test_text_only_model_rejects_image_before_transport(self):
        self.image_capability(False)
        result=self.send(self.new_conv(),'Jaki kolor ma obraz?',[str(FIXTURE)])
        self.assertIn('error',result);self.assertIn('vision_model_image_input_unsupported',json.dumps(result))
        self.assertEqual(self.calls,[])

    def test_model_name_cannot_claim_vision_capability(self):
        self.config({'default_model':'synthetic/vision-capable-name'})
        result=self.send(self.new_conv(),'Jaki kolor ma obraz?',[str(FIXTURE)])
        self.assertIn('error',result);self.assertIn('vision_model_capability_unknown',json.dumps(result))
        self.assertEqual(self.calls,[])

if __name__=='__main__': unittest.main(verbosity=2)
