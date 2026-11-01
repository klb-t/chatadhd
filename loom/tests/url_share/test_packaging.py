"""Light artifact checks only; no claim of C++ materialization/execution."""
import ast
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
import unittest

LOOM = Path(__file__).resolve().parents[2]
RESOURCE = LOOM / 'src/import/url_share/worker.inc'
CANONICAL = RESOURCE.with_name('worker.py')


def embedded_bytes():
    text = RESOURCE.read_text('ascii')
    payload = text.split('kWorkerBase64 =', 1)[1].split(';', 1)[0]
    return base64.b64decode(''.join(re.findall(r'"([A-Za-z0-9+/=]+)"', payload)), validate=True)


def private_file(path, raw):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, 'wb', closefd=False) as out:
            out.write(raw)
    finally:
        os.close(fd)


class PackagingTests(unittest.TestCase):
    def test_provider_parser_from_embedded_worker_without_checkout(self):
        tree = ast.parse(Path(__file__).with_name('test_provider_share.py').read_text())
        classes = '\n\n'.join(ast.unparse(n) for n in tree.body if isinstance(n, ast.ClassDef) and n.name in ('Response', 'MemoryTransport'))
        runner = '''import io, json, runpy
from pathlib import Path
from types import SimpleNamespace
root = Path(__file__).resolve().parent
w = SimpleNamespace(**runpy.run_path(str(root / "worker.py"), run_name="embedded_worker"))
''' + classes + '''
policy = json.loads((root / "policy.json").read_text())
for name in ("chatgpt_next_data.html", "chatgpt_turbo_stream.html"):
    raw = (root / name).read_bytes()
    transport = MemoryTransport(raw)
    url = "https://chatgpt.com/share/synthetic-share"
    result = w.snapshot_request({"schema":"loom.url_import_request/1", "mode":"share", "url":url}, policy, transport)
    assert result["parsed"]["status"] == "complete"
    assert len(result["parsed"]["messages"]) == 4
    assert transport.calls == [url]
    assert result["llm_calls"] == 0
print("embedded provider parser: both HTML formats, 0 model calls")
'''
        self.assertNotIn(str(LOOM), runner)
        with tempfile.TemporaryDirectory(prefix='provider_artifact_') as tmp:
            root = Path(tmp)
            private_file(root / 'worker.py', embedded_bytes())
            private_file(root / 'runner.py', runner.encode())
            private_file(root / 'policy.json', (LOOM / 'data/presets/url_share.pack').read_bytes())
            for name in ('chatgpt_next_data.html', 'chatgpt_turbo_stream.html'):
                private_file(root / name, (LOOM / 'tests/fixtures/url_share' / name).read_bytes())
            result = subprocess.run([sys.executable, '-I', str(root / 'runner.py')], cwd=root, capture_output=True, timeout=3)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(result.stderr, b'')
            self.assertIn(b'both HTML formats', result.stdout)

    def test_exact_bytes_hash_and_generator(self):
        raw = embedded_bytes()
        self.assertEqual(raw, CANONICAL.read_bytes())
        self.assertIn(hashlib.sha256(raw).hexdigest(), RESOURCE.read_text('ascii'))
        subprocess.run([sys.executable, str(RESOURCE.with_name('gen_worker.py')), '--check'], check=True)

    def test_unpack_and_execute_existing_isolated_transport_without_checkout(self):
        # Reuse the exact existing controlled transport/server definitions; no
        # duplicate parser, import of the test module, or checkout path in child.
        tree = ast.parse(Path(__file__).with_name('test_worker.py').read_text())
        classes = '\n\n'.join(ast.unparse(n) for n in tree.body if isinstance(n, ast.ClassDef) and n.name in ('Handler', 'IsolatedTransport'))
        self.assertEqual(sum(isinstance(n, ast.ClassDef) and n.name in ('Handler', 'IsolatedTransport') for n in tree.body), 2)
        runner = '''import base64, http.client, json, runpy, time, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlsplit
root = Path(__file__).resolve().parent
w = SimpleNamespace(**runpy.run_path(str(root / "worker.py"), run_name="embedded_worker"))
SHARE = json.loads((root / "share.json").read_text())
ASSET = b"synthetic attachment\\x00\\xff\\n"
''' + classes + '''
server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    policy = json.loads((root / "policy.json").read_text())
    request = {"schema":"loom.url_import_request/1","mode":"share","url":"http://fixture.example.invalid/share"}
    result = w.snapshot_request(request, policy, IsolatedTransport(server.server_port))
    assert [(m["role"],m["text"]) for m in result["parsed"]["messages"]] == [(m["role"],m["text"]) for m in SHARE["messages"]]
    attachments = [a for m in result["parsed"]["messages"] for a in m["attachments"]]
    assert [a["status"] for a in attachments] == ["complete","missing","unsupported"]
    assert base64.b64decode(attachments[0]["body_base64"]) == ASSET
    assert result["llm_calls"] == 0
    assert len(Handler.requests) == 3
    print("isolated artifact: 5 messages, 3 attachment states, 0 model calls")
finally:
    server.shutdown(); server.server_close(); thread.join()
'''
        self.assertNotIn(str(LOOM), runner)
        with tempfile.TemporaryDirectory(prefix='m2_packaging_') as tmp:
            root = Path(tmp)
            os.chmod(root, 0o700)
            private_file(root / 'worker.py', embedded_bytes())
            private_file(root / 'runner.py', runner.encode())
            private_file(root / 'share.json', (LOOM / 'tests/fixtures/url_share/conversation.json').read_bytes())
            private_file(root / 'policy.json', (LOOM / 'data/presets/url_share.pack').read_bytes())
            self.assertEqual(stat.S_IMODE(root.stat().st_mode), 0o700)
            self.assertTrue(all(stat.S_IMODE(p.stat().st_mode) == 0o600 for p in root.iterdir()))
            result = subprocess.run([sys.executable, '-I', str(root / 'runner.py')], cwd=root, capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(result.stderr, b'')
            self.assertIn(b'0 model calls', result.stdout)
        self.assertFalse(root.exists())

    def test_unpacked_production_entrypoint_rejects_loopback(self):
        envelope = {'request': {'schema':'loom.url_import_request/1','mode':'page','url':'http://127.0.0.1/'},
                    'policy': json.loads((LOOM / 'data/presets/url_share.pack').read_text())}
        with tempfile.TemporaryDirectory(prefix='m2_artifact_main_') as tmp:
            worker = Path(tmp) / 'worker.py'
            private_file(worker, embedded_bytes())
            r = subprocess.run([sys.executable, '-I', str(worker)], cwd=tmp, input=json.dumps(envelope).encode(), capture_output=True, timeout=3)
            self.assertEqual(r.returncode, 0)
            self.assertEqual(r.stderr, b'')
            result = json.loads(r.stdout)
            self.assertEqual(result['parsed']['reason'], 'address_not_public')
            self.assertEqual(result['parsed']['status'], 'denied')
            self.assertEqual(result['llm_calls'], 0)
        self.assertFalse(Path(tmp).exists())

    def test_native_production_path_has_no_checkout_dependency(self):
        source = (LOOM / 'src/import/url_share_import.cpp').read_text()
        self.assertNotIn('__FILE__', source)
        for token in ('mkdtemp', '0700', '0600', 'O_EXCL', 'O_NOFOLLOW', 'kWorkerBase64', 'kWorkerSha256', 'private_write(worker, embedded)', 'WorkerGuard child', '"-I"'):
            self.assertIn(token, source)
        # Source inspection only; the integrator still must execute the C++ gate.

if __name__ == '__main__':
    unittest.main()
