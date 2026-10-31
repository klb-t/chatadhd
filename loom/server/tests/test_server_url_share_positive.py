#!/usr/bin/env python3
"""Real /api/import -> default embedded worker -> native history/restart.

DNS/connect are an explicit test-only LD_PRELOAD fixture. No public network,
private credentials, production policy/config bypass or external share claim.
"""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import requests

LOOM = Path(__file__).resolve().parents[2]

class UrlHttpPositive(unittest.TestCase):
    def test_default_helper_share_page_reopen_bytes_and_no_models(self):
        binary = Path(os.environ['LOOM_SERVER_BIN']).resolve(strict=True)
        fixture = Path(os.environ['LOOM_URL_NETWORK_FIXTURE']).resolve(strict=True)
        root = LOOM.parent.resolve()
        self.assertTrue(binary.is_relative_to(root)); self.assertTrue(fixture.is_relative_to(root))
        share = json.loads((LOOM / 'tests/fixtures/url_share/conversation.json').read_text())
        # HTTP-only fixture; TLS is covered by parser/transport tests, not this loopback gate.
        share['messages'][1]['attachments'] = ['http://assets.example.invalid/asset', 'http://assets.example.invalid/missing']
        raw = json.dumps(share, ensure_ascii=False).encode()
        asset = b'synthetic attachment\x00\xff\n'
        calls = []
        class Source(BaseHTTPRequestHandler):
            def log_message(self, *_): pass
            def do_GET(self):
                calls.append((self.path, dict(self.headers)))
                status, mime, body = 200, 'text/plain', 'Strona syntetyczna — żółw.'.encode()
                if self.path == '/share': mime, body = 'application/json', raw
                elif self.path == '/asset': mime, body = 'application/octet-stream', asset
                elif self.path == '/missing': status, body = 404, b'synthetic missing'
                self.send_response(status); self.send_header('Content-Type', mime)
                self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
            def do_POST(self):
                calls.append(('UNEXPECTED_MODEL', {})); self.send_response(500); self.end_headers()
        source = ThreadingHTTPServer(('127.0.0.1', 0), Source)
        thread = threading.Thread(target=source.serve_forever, daemon=True); thread.start()
        self.addCleanup(lambda: (source.shutdown(), source.server_close(), thread.join()))
        tmp = tempfile.TemporaryDirectory(prefix='m2-http-positive-', dir=Path.cwd()); self.addCleanup(tmp.cleanup)
        data = Path(tmp.name)
        (data/'config.json').write_text(json.dumps({'semantic_analysis':False, 'auto_title':False}))
        token = 'SYNTHETIC_URL_HTTP_TEST_ONLY'
        env = {k:os.environ[k] for k in ['PATH','LANG','TMPDIR'] if k in os.environ}
        env.update(LOOM_SERVER_TOKEN=token, LD_PRELOAD=str(fixture), LOOM_TEST_URL_SOURCE_PORT=str(source.server_port))
        client = requests.Session(); client.trust_env=False; client.headers['Authorization']='Bearer '+token; self.addCleanup(client.close)
        self.proc = None
        def stop():
            if self.proc is not None:
                self.proc.terminate()
                try: self.proc.wait(timeout=10)
                except subprocess.TimeoutExpired: self.proc.kill(); self.proc.wait(timeout=5)
                self.proc = None
        self.addCleanup(stop)
        def start():
            with socket.socket() as sock: sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
            self.proc=subprocess.Popen([str(binary),'--host','127.0.0.1','--port',str(port),'--data-dir',str(data)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            base=f'http://127.0.0.1:{port}'
            for _ in range(200):
                try:
                    if client.get(base+'/api/healthz',timeout=1).status_code==200:return base
                except requests.RequestException:pass
                time.sleep(.05)
            self.fail('own server did not start')
        base=start()
        def upload(mode,path):
            descriptor={'schema':'loom.url_import_request/1','url':'http://fixture.example.invalid'+path,'mode':mode}
            response=client.post(base+'/api/import',files={'file':('source.loomurl',json.dumps(descriptor),'application/json')},timeout=30)
            self.assertEqual(response.status_code,200,response.text)
            events=[json.loads(line[5:].strip()) for line in response.text.splitlines() if line.startswith('data:')]
            self.assertFalse([e for e in events if e.get('type')=='error'],events)
            return next(e for e in events if e.get('type')=='done')
        imported=upload('share','/share'); self.assertEqual(imported['format'],'url'); self.assertEqual(imported['status'],'partial')
        self.assertEqual(imported['messages'],5);self.assertEqual(imported['llm_calls'],0)
        self.assertEqual([a['status'] for a in imported['attachments']],['complete','missing','unsupported'])
        cid=imported['conversations'][0]['id']
        def history():
            r=client.get(base+f'/api/conversations/{cid}/messages',timeout=10);self.assertEqual(r.status_code,200);return r.json()
        rows=history();self.assertEqual([(m['role'],m['text']) for m in rows],[(m['role'],m['text']) for m in share['messages']])
        for digest, expected in [(imported['blob_hash'],raw),(imported['attachments'][0]['blob_hash'],asset)]:
            r=client.get(base+f'/api/client/blob/{digest}/raw',timeout=10);self.assertEqual(r.status_code,200);self.assertEqual(r.content,expected);self.assertEqual(hashlib.sha256(r.content).hexdigest(),digest)
        stop();base=start();self.assertEqual(history(),rows)
        again=upload('share','/share');self.assertTrue(again['already_imported']);self.assertEqual(again['source_id'],imported['source_id'])
        page=upload('page','/page');self.assertEqual(page['status'],'complete');self.assertEqual(page['messages'],1)
        r=client.get(base+'/api/conversations/'+page['conversations'][0]['id']+'/messages',timeout=10)
        self.assertEqual(r.status_code,200);self.assertEqual(r.json()[0]['text'],'Strona syntetyczna — żółw.')
        self.assertEqual([c[0] for c in calls],['/share','/asset','/missing','/share','/asset','/missing','/page'])
        for _,headers in calls:self.assertFalse(set(k.lower() for k in headers)&{'authorization','cookie','proxy-authorization','referer'})

if __name__=='__main__':unittest.main(verbosity=2)
