"""Actual production worker tests with synthetic HTTP through an isolated dialer.
No native binary, provider, credentials, OCR, model or web build is involved.
"""
import base64
import copy
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time
import unittest
from unittest.mock import patch, Mock
from urllib.parse import urlsplit

LOOM = Path(__file__).resolve().parents[2]
WORKER = LOOM / "src/import/url_share/worker.py"
spec = importlib.util.spec_from_file_location("url_share_worker", WORKER)
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)
POLICY = json.loads((LOOM / "data/presets/url_share.pack").read_text())
SHARE = json.loads((LOOM / "tests/fixtures/url_share/conversation.json").read_text())
ASSET = b"synthetic attachment\x00\xff\n"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    requests = []
    def log_message(self, *args):
        pass
    def do_GET(self):
        type(self).requests.append((self.path, dict(self.headers)))
        status, mime, headers = 200, "text/plain; charset=utf-8", {}
        body = "Rzeczywista treść strony — żółw 🐢".encode()
        if self.path == "/share":
            mime, body = "application/json", json.dumps(SHARE, ensure_ascii=False).encode()
        elif self.path == "/asset":
            mime, body = "application/octet-stream", ASSET
        elif self.path in ("/forbidden", "/missing"):
            status, body = (403 if self.path == "/forbidden" else 404), b"synthetic error"
        elif self.path == "/redirect":
            status, headers, body = 302, {"Location": "dir/../page#fragment", "Set-Cookie": "test=never-forward"}, b""
        elif self.path == "/cross":
            status, headers, body = 302, {"Location": "http://other.example.invalid/page"}, b""
        elif self.path == "/private":
            status, headers, body = 302, {"Location": "http://127.0.0.1/metadata"}, b""
        elif self.path == "/loop":
            status, headers, body = 302, {"Location": "/loop"}, b""
        elif self.path == "/downgrade":
            status, headers, body = 302, {"Location": "http://fixture.example.invalid/page"}, b""
        elif self.path == "/slow":
            time.sleep(.2)
        elif self.path == "/large":
            body = b"L" * 512
        elif self.path == "/partial":
            status = 206
        elif self.path == "/html":
            mime, body = "text/html", '<html><head><title>Title alone</title></head><body><p>Zażółć &amp; text</p><script>ignored()</script></body></html>'.encode()
        elif self.path == "/title":
            mime, body = "text/html", b"<html><head><title>Not a conversation</title></head><body></body></html>"
        elif self.path == "/truncated":
            mime, body = "text/html", b"<html><body><p>partial body"
        elif self.path == "/login":
            mime, body = "text/html", b'<html><body><input type="password"></body></html>'
        elif self.path == "/binary":
            body = b"\xff\xfe"
        elif self.path == "/gzip":
            headers = {"Content-Encoding": "gzip"}
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        for name, value in headers.items():
            self.send_header(name, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


class IsolatedTransport:
    """Maps only explicit synthetic hosts to this test's own loopback port.
    Production PublicTransport is unchanged and can never select this class.
    """
    def __init__(self, port):
        self.port = port
        self.calls = []
    def open(self, url, deadline):
        self.calls.append(url)
        u = urlsplit(url)
        if u.hostname not in ("fixture.example.invalid", "assets.example.invalid", "other.example.invalid"):
            raise w.Rejected("address_not_public")
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=w.remaining(deadline))
        try:
            conn.request("GET", u.path + (("?" + u.query) if u.query else ""), headers={"Accept-Encoding": "identity", "Connection": "close"})
            return conn, conn.getresponse()
        except BaseException:
            conn.close()
            raise


class WorkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
    def setUp(self):
        self.transport = IsolatedTransport(self.server.server_port)
        self.policy = copy.deepcopy(POLICY)
        Handler.requests = []
    def request(self, path, mode="page", url=None):
        return w.snapshot_request({"schema": "loom.url_import_request/1", "url": url or "http://fixture.example.invalid" + path, "mode": mode}, self.policy, self.transport)
    def test_page_bytes_text_and_provenance(self):
        s = self.request("/page")
        self.assertEqual(s["parsed"]["status"], "complete")
        raw = base64.b64decode(s["resource"]["body_base64"])
        self.assertEqual(s["parsed"]["messages"][0]["text"], raw.decode())
        self.assertEqual(s["resource"]["sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(s["resource"]["source_url"], s["resource"]["final_url"])
    def test_redirect_normalization_and_provenance(self):
        s = self.request("/redirect")
        self.assertEqual(s["resource"]["final_url"], "http://fixture.example.invalid/page")
        self.assertEqual(s["resource"]["redirects"][0]["from"], "http://fixture.example.invalid/redirect")
    def test_cross_host_no_credentials_or_cookies(self):
        self.request("/cross")
        self.assertEqual(len(Handler.requests), 2)
        self.assertTrue(self.transport.calls[-1].startswith("http://other.example.invalid/"))
        for _, headers in Handler.requests:
            self.assertFalse(set(k.lower() for k in headers) & {"authorization", "cookie", "proxy-authorization", "referer"})
    def test_403_is_denied(self):
        s = self.request("/forbidden")
        self.assertEqual((s["parsed"]["status"], s["parsed"]["reason"]), ("denied", "http_403"))
        self.assertEqual(s["parsed"]["messages"], [])
    def test_404_is_explicit(self):
        self.assertEqual(self.request("/missing")["parsed"]["reason"], "http_404")
    def test_timeout(self):
        self.policy["limits"]["timeout_ms"] = 30
        self.assertEqual(self.request("/slow")["parsed"]["reason"], "timeout")
    def test_oversize_preserves_bounded_prefix(self):
        self.policy["limits"]["max_bytes"] = 100
        s = self.request("/large")
        self.assertEqual(s["parsed"]["reason"], "oversize")
        self.assertEqual(base64.b64decode(s["resource"]["body_base64"]), b"L" * 100)
        self.assertEqual(s["parsed"]["messages"], [])
    def test_redirect_private_destination_denied(self):
        s = self.request("/private")
        self.assertEqual(s["parsed"]["reason"], "address_not_public")
        self.assertEqual(len(Handler.requests), 1)
    def test_redirect_loop_and_limit(self):
        self.assertEqual(self.request("/loop")["parsed"]["reason"], "redirect_loop")
        self.policy["limits"]["max_redirects"] = 0
        self.assertEqual(self.request("/redirect")["parsed"]["reason"], "redirect_limit")
    def test_tls_downgrade_denied(self):
        s = self.request("", url="https://fixture.example.invalid/downgrade")
        self.assertEqual(s["parsed"]["reason"], "redirect_tls_downgrade")
    def test_share_roles_order_unicode_empty_text(self):
        s = self.request("/share", "share")
        self.assertEqual([(m["role"], m["text"]) for m in s["parsed"]["messages"]], [(m["role"], m["text"]) for m in SHARE["messages"]])
        self.assertEqual([m["pointer"] for m in s["parsed"]["messages"]], ["/messages/" + str(i) for i in range(5)])
        self.assertEqual(json.loads(base64.b64decode(s["resource"]["body_base64"])), SHARE)
    def test_attachment_available_missing_and_local_never_read(self):
        s = self.request("/share", "share")
        attachments = [a for m in s["parsed"]["messages"] for a in m["attachments"]]
        self.assertEqual([a["status"] for a in attachments], ["complete", "missing", "unsupported"])
        self.assertEqual(base64.b64decode(attachments[0]["body_base64"]), ASSET)
        self.assertEqual(s["parsed"]["status"], "partial")
        self.assertEqual(len(self.transport.calls), 3)
    def test_same_snapshot_is_deterministic(self):
        a, b = self.request("/share", "share"), self.request("/share", "share")
        self.assertEqual(a, b)  # Native store idempotence is tested separately, not inferred here.
    def test_attachment_count_and_total_limits(self):
        self.policy["limits"]["max_attachments"] = 0
        s = self.request("/share", "share")
        self.assertEqual(len(self.transport.calls), 1)
        self.assertEqual(s["parsed"]["messages"][1]["attachments"][0]["reason"], "attachment_limit")
        self.policy["limits"]["max_attachments"] = 64
        self.policy["limits"]["max_total_bytes"] = s["resource"]["bytes"]
        s = self.request("/share", "share")
        self.assertEqual(s["parsed"]["messages"][1]["attachments"][0]["reason"], "oversize")
    def test_html_visible_text_excludes_title_script(self):
        s = self.request("/html")
        self.assertEqual(s["parsed"]["messages"][0]["text"], "Zażółć & text")
    def test_title_is_never_a_conversation(self):
        self.assertEqual(self.request("/title", "share")["parsed"]["status"], "unsupported")
        self.assertEqual(self.request("/title")["parsed"]["messages"], [])
    def test_login_denied(self):
        for mode in ("page", "share"):
            self.assertEqual(self.request("/login", mode)["parsed"]["reason"], "login_required")
    def test_incomplete_html_partial(self):
        for mode in ("page", "share"):
            self.assertEqual(self.request("/truncated", mode)["parsed"]["status"], "partial")
    def test_http_partial_not_promoted(self):
        self.assertEqual(self.request("/partial")["parsed"]["reason"], "incomplete_http")
    def test_unknown_encoding_and_compression(self):
        self.assertEqual(self.request("/binary")["parsed"]["reason"], "encoding_not_utf8")
        self.assertEqual(self.request("/gzip")["parsed"]["reason"], "content_encoding")
    def test_invalid_share_rows_partial_no_invented_text(self):
        fixture = copy.deepcopy(SHARE)
        fixture["messages"].insert(1, {"role": "assistant", "text": {"hidden": "do not infer"}})
        resource = {"status": "complete", "reason": "fetched", "mime": "application/json", **w.encoded(json.dumps(fixture).encode())}
        parsed = w.parse_content(resource, "share", "assistant")
        self.assertEqual(parsed["status"], "partial")
        self.assertEqual(len(parsed["messages"]), len(SHARE["messages"]))
        self.assertEqual(parsed["messages"][1]["pointer"], "/messages/2")
    def test_zero_model_calls(self):
        s = self.request("/share", "share")
        self.assertEqual(s["llm_calls"], 0)
        self.assertEqual([urlsplit(u).path for u in self.transport.calls], ["/share", "/asset", "/missing"])
        self.assertEqual(len(Handler.requests), 3)


class SecurityTests(unittest.TestCase):
    def test_nonpublic_addresses(self):
        for address in ("127.0.0.1", "10.0.0.1", "172.16.0.1", "192.168.1.1", "169.254.169.254", "100.100.100.200", "0.0.0.0", "224.0.0.1", "::1", "fc00::1", "fe80::1", "::ffff:127.0.0.1", "2002:7f00:1::", "64:ff9b::7f00:1", "168.63.129.16"):
            with self.subTest(address=address):
                self.assertFalse(w.public_address(address))
        self.assertTrue(w.public_address("8.8.8.8"))
    def test_urls_reject_auth_scheme_control_and_ambiguous_host(self):
        for url in ("http://user:secret@example.invalid/", "file:///etc/passwd", "http://example.invalid/\r\nX:1", "http://example.invalid\\@127.0.0.1/", "http://[fe80::1%eth0]/", "http://example.invalid:99999/", "http://example.invalid:0/"):
            with self.subTest(url=url), self.assertRaises(w.Rejected):
                w.normalize_url(url)
    def test_canonical_source_url(self):
        self.assertEqual(w.normalize_url("HTTPS://EXAMPLE.INVALID.:443/ą#ignored"), "https://example.invalid/%C4%85")
    def test_production_dns_mixed_private_denied_before_connect(self):
        infos = [(socket.AF_INET, socket.SOCK_STREAM, 6, "", (ip, 80)) for ip in ("8.8.8.8", "127.0.0.1")]
        with patch.dict(os.environ, {}, clear=True), patch.object(socket, "getaddrinfo", return_value=infos), patch.object(socket, "socket") as dial:
            with self.assertRaises(w.Rejected) as caught:
                w.PublicTransport().open("http://fixture.example.invalid/", time.monotonic() + 1)
            self.assertEqual(caught.exception.reason, "address_not_public")
            dial.assert_not_called()
    def test_production_pin_has_no_second_dns_or_auth(self):
        infos = [(socket.AF_INET, socket.SOCK_STREAM, 6, "", ("8.8.8.8", 80))]
        with patch.dict(os.environ, {}, clear=True), patch.object(socket, "getaddrinfo", return_value=infos) as dns, patch.object(socket, "socket") as dial, patch.object(http.client, "HTTPConnection") as connection:
            w.PublicTransport().open("http://fixture.example.invalid/page", time.monotonic() + 1)
            dns.assert_called_once()
            dial.return_value.connect.assert_called_once_with(("8.8.8.8", 80))
            headers = connection.return_value.request.call_args.kwargs["headers"]
            self.assertEqual(set(headers), {"Accept-Encoding", "Connection"})
    def test_production_private_literal_is_denied_even_with_proxy(self):
        with patch.dict(os.environ, {"HTTPS_PROXY": "http://proxy.invalid"}, clear=True), patch.object(socket, "socket") as dial:
            for url in ("http://127.0.0.1/", "http://169.254.169.254/", "http://localhost/", "http://[::1]/"):
                with self.assertRaises(w.Rejected) as caught:
                    w.PublicTransport().open(url, time.monotonic() + 1)
                self.assertEqual(caught.exception.reason, "address_not_public")
            dial.assert_not_called()
    def test_proxy_is_never_bypassed(self):
        with patch.dict(os.environ, {"HTTPS_PROXY": "http://proxy.invalid"}, clear=True), patch.object(socket, "getaddrinfo") as dns:
            with self.assertRaises(w.Rejected) as caught:
                w.PublicTransport().open("https://fixture.example.invalid/page", time.monotonic() + 1)
            self.assertEqual(caught.exception.reason, "proxy_destination_pinning_unavailable")
            dns.assert_not_called()
    def test_untrusted_request_cannot_select_transport(self):
        for key in ("transport", "headers", "worker", "allow_private"):
            request = {"schema": "loom.url_import_request/1", "mode": "page", "url": "http://example.invalid", key: "bad"}
            with self.assertRaises(ValueError):
                w.snapshot_request(request, POLICY)
    def test_worker_cli_invalid_input_has_no_logs(self):
        result = subprocess.run([sys.executable, "-I", str(WORKER)], input=b'{"secret":"SYNTHETIC-DO-NOT-LOG"}', capture_output=True, timeout=2)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, b"")
        self.assertNotIn(b"SYNTHETIC-DO-NOT-LOG", result.stdout)
        self.assertEqual(json.loads(result.stdout), {"error": "url_worker_failed"})
    def test_isolated_worker_process_contract(self):
        envelope = {"request": {"schema": "loom.url_import_request/1", "url": "http://fixture.example.invalid/share", "mode": "share"}, "policy": POLICY}
        result = subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("fixture_worker.py"))], input=json.dumps(envelope).encode(), capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, b"")
        snapshot = json.loads(result.stdout)
        self.assertEqual(snapshot["llm_calls"], 0)
        self.assertEqual(len(snapshot["parsed"]["messages"]), 5)
        self.assertEqual([a["status"] for m in snapshot["parsed"]["messages"] for a in m["attachments"]], ["complete", "missing", "unsupported"])
    def test_generated_preset(self):
        subprocess.run([sys.executable, str(WORKER.with_name("gen_policy.py")), "--check"], check=True)


if __name__ == "__main__":
    unittest.main()
