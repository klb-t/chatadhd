"""engine.chat_engine.ChatEngine <-> Loom ChatEngine, driven against the same
live HTTP server (a local Python http.server mock), so this is a true
end-to-end differential test of the request/response cycle: URL, method,
headers (minus the Authorization value), JSON body shape, and the resulting
DB effects (conversations + messages), not just unit-level behaviour.

Known, documented difference (see loom/README.md "Deliberate differences"):
Python's ChatEngine sends the current user message twice (once via history,
once as the final turn); Loom sends it once. Every comparison below accounts
for that explicitly instead of asserting byte-identical message arrays.
"""
import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from compat_common import CompatTestCase, Normalizer, normalized_dump, tool
from engine.chat_engine import ChatEngine
from engine.config import Config, Secrets
from engine.db import Database

ASSISTANT_REPLY = "Hello! I'm doing well, thanks for asking today."
REASONING_TEXT = "considering how to respond politely"
USER_MESSAGE = "Hello there, running the chat compat test today"


class MockOpenRouter(BaseHTTPRequestHandler):
    """Minimal OpenRouter-shaped /chat/completions mock. Records every
    request (method, path, headers minus Authorization's value, parsed JSON
    body) onto self.server.requests for the test to inspect afterwards."""

    protocol_version = "HTTP/1.1"

    def log_message(self, *a):  # silence default stderr logging
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b""
        try:
            body = json.loads(raw) if raw else None
        except json.JSONDecodeError:
            body = {"_raw": raw.decode("utf-8", "replace")}

        headers = {k: v for k, v in self.headers.items() if k.lower() != "authorization"}
        headers["authorization_present"] = "authorization" in {k.lower() for k in self.headers.keys()}
        self.server.requests.append({"method": "POST", "path": self.path, "headers": headers, "body": body})

        if self.path != "/chat/completions":
            self.send_response(404)
            self.end_headers()
            return

        response = {
            "choices": [{"message": {"role": "assistant", "content": ASSISTANT_REPLY, "reasoning": REASONING_TEXT}}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15},
        }
        payload = json.dumps(response).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


class MockServer:
    def __init__(self):
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), MockOpenRouter)
        self.httpd.requests = []
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    @property
    def base_url(self):
        return f"http://127.0.0.1:{self.httpd.server_port}"

    @property
    def requests(self):
        return self.httpd.requests

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=5)


def strip_auth_headers(reqs):
    """headers minus the Authorization value (already stripped by the mock);
    also drop connection-level headers that differ between requests libraries
    (Host, User-Agent, Accept-Encoding, Connection) since those are transport
    details, not application-level request shape."""
    drop = {"host", "user-agent", "accept-encoding", "connection", "content-length"}
    out = []
    for r in reqs:
        h = {k.lower(): v for k, v in r["headers"].items() if k.lower() not in drop}
        out.append({"method": r["method"], "path": r["path"], "headers": h, "body": r["body"]})
    return out


class ChatEngineCompatTest(CompatTestCase):

    def _run_python(self, server, db_path):
        cfg = Config(self.tmp.path / "py_config.json")
        cfg.set("base_url", server.base_url)
        cfg.set("default_model", "test/model")
        secrets = Secrets(self.tmp.path / "py_secrets.json")
        secrets.set("api_key", "sk-test-key")
        db = Database(db_path)
        engine = ChatEngine(cfg, secrets, db)
        text = engine.send(USER_MESSAGE)
        return text

    def _run_loom(self, server, db_path):
        args = {
            "config": {"base_url": server.base_url, "default_model": "test/model"},
            "secrets": {"api_key": "sk-test-key"},
            "request": {"message": USER_MESSAGE, "stream": False},
        }
        out = tool("chat-send", str(db_path), self.tmp.file_json("chat_args.json", args))
        self.assertNotIn("error", out, out)
        return out["result"]

    def test_request_shape_and_db_effects_match(self):
        py_server = MockServer()
        loom_server = MockServer()
        try:
            py_db = self.tmp.path / "py.db"
            loom_db = self.tmp.path / "loom.db"

            py_text = self._run_python(py_server, py_db)
            loom_result = self._run_loom(loom_server, loom_db)

            self.assertEqual(py_text, ASSISTANT_REPLY)
            self.assertEqual(loom_result["text"], ASSISTANT_REPLY)
            self.assertEqual(loom_result["model"], "test/model")
            self.assertFalse(loom_result["cancelled"])

            # ── HTTP request shape ──────────────────────────────────────
            py_reqs = strip_auth_headers(py_server.requests)
            loom_reqs = strip_auth_headers(loom_server.requests)
            self.assertEqual(len(py_reqs), 1)
            self.assertEqual(len(loom_reqs), 1)

            for reqs, name in ((py_reqs, "python"), (loom_reqs, "loom")):
                self.assertEqual(reqs[0]["method"], "POST", name)
                self.assertEqual(reqs[0]["path"], "/chat/completions", name)
                self.assertTrue(reqs[0]["headers"]["authorization_present"], name)
                self.assertEqual(reqs[0]["headers"].get("content-type"), "application/json", name)
                self.assertEqual(reqs[0]["headers"].get("x-title"), "ChatADHD", name)
                self.assertEqual(reqs[0]["headers"].get("http-referer"), "https://github.com/chatadhd", name)

            py_body = py_reqs[0]["body"]
            loom_body = loom_reqs[0]["body"]
            self.assertEqual(py_body["model"], loom_body["model"])
            self.assertEqual(py_body["stream"], loom_body["stream"])
            self.assertEqual(py_body["stream"], False)
            self.assertEqual(py_body["temperature"], loom_body["temperature"])
            self.assertEqual(py_body["max_tokens"], loom_body["max_tokens"])
            self.assertEqual(py_body["include_reasoning"], loom_body["include_reasoning"])
            self.assertNotIn("plugins", py_body)
            self.assertNotIn("plugins", loom_body)

            # Documented deviation: Python sends the current user turn twice
            # (once from history, once as the current message); Loom once.
            py_messages = py_body["messages"]
            loom_messages = loom_body["messages"]
            self.assertEqual(py_messages[-1], py_messages[-2],
                             "Python's own duplicate-send behaviour changed; update the compat note")
            self.assertEqual(py_messages[:-1], loom_messages,
                             "message arrays differ beyond the documented duplicate-send difference")
            self.assertEqual(loom_messages[-1], {"role": "user", "content": USER_MESSAGE})

            # ── DB effects (conversations + messages), IDs/timestamps normalised ──
            py_dump = tool("db-api-dump", str(py_db))
            loom_dump = tool("db-api-dump", str(loom_db))
            py_norm = normalized_dump(Normalizer(), py_dump)
            loom_norm = normalized_dump(Normalizer(), loom_dump)
            self.assertSameJson(py_norm, loom_norm, "db-api-dump")

        finally:
            py_server.stop()
            loom_server.stop()

    def test_non_200_status_is_a_well_formed_error_on_both_sides(self):
        class Error500(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                self.rfile.read(length)
                payload = b'{"error": "boom"}'
                self.send_response(500)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

        httpd = ThreadingHTTPServer(("127.0.0.1", 0), Error500)
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        base_url = f"http://127.0.0.1:{httpd.server_port}"
        try:
            cfg = Config(self.tmp.path / "py_config2.json")
            cfg.set("base_url", base_url)
            cfg.set("default_model", "test/model")
            secrets = Secrets(self.tmp.path / "py_secrets2.json")
            secrets.set("api_key", "sk-test-key")
            db = Database(self.tmp.path / "py2.db")
            engine = ChatEngine(cfg, secrets, db)
            with self.assertRaises(ValueError) as cm:
                engine.send("this will fail")
            self.assertIn("500", str(cm.exception))

            args = {
                "config": {"base_url": base_url, "default_model": "test/model"},
                "secrets": {"api_key": "sk-test-key"},
                "request": {"message": "this will fail", "stream": False},
            }
            out = tool("chat-send", str(self.tmp.path / "loom2.db"), self.tmp.file_json("chat_args2.json", args))
            self.assertIn("error", out)
            self.assertEqual(out["error"]["code"], "http")
            self.assertIn("500", out["error"]["message"])
        finally:
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
