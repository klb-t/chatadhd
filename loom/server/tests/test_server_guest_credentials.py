#!/usr/bin/env python3
"""Credential boundary through real HTTP -> C API -> two loopback providers.

Only invented markers and newly created data under the current working directory.
Run with LOOM_SERVER_BIN pointing to this worktree's own build.
"""
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest

import requests

from test_server_guests import free_port, overlay, solve_pow


OPERATOR_A = "fake-operator-A-credential-boundary"
BYOK_B = "fake-user-B-credential-boundary"
BYOK_A = "fake-user-A-credential-boundary"


class Provider:
    def __init__(self):
        self.calls = []
        self.model_auth = []
        calls = self.calls
        model_auth = self.model_auth

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_GET(self):
                model_auth.append(self.headers.get("Authorization"))
                body = json.dumps({"data": [{"id": "mock/credentials"}]}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                calls.append((self.path, self.headers.get("Authorization"), body))
                answer = {"choices": [{"message": {"role": "assistant", "content": "Local credential test reply"},
                                       "finish_reason": "stop"}],
                          "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}}
                data = json.dumps(answer).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.base = f"http://127.0.0.1:{self.server.server_port}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        print(f"provider pid={os.getpid()} port={self.server.server_port}", flush=True)

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)


class Server:
    def __init__(self, root):
        self.port = free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.log = (root / "server-test.log").open("a")
        # Do not inherit any provider credentials, proxy settings or data locations.
        env = {k: os.environ[k] for k in ("PATH", "LANG", "TMPDIR") if k in os.environ}
        self.proc = subprocess.Popen([os.environ["LOOM_SERVER_BIN"], "--host", "127.0.0.1",
                                      "--port", str(self.port), "--accounts-root", str(root)],
                                     env=env, stdout=self.log, stderr=self.log)
        print(f"loom-server pid={self.proc.pid} port={self.port}", flush=True)
        probe = requests.Session()
        probe.trust_env = False
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if self.proc.poll() is not None:
                    raise RuntimeError("server exited; see server-test.log")
                try:
                    if probe.get(self.base + "/api/healthz", timeout=1).status_code == 200:
                        return
                except requests.RequestException:
                    time.sleep(0.1)
            raise RuntimeError("server did not start")
        except BaseException:
            self.close()
            raise
        finally:
            probe.close()

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)
        self.log.close()


class CredentialBoundary(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="loom-credential-boundary-", dir=Path.cwd())
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.a = Provider()
        self.addCleanup(self.a.close)
        self.b = Provider()
        self.addCleanup(self.b.close)
        keys = self.root / "invented-keys.txt"
        keys.write_text(f"LOOM_TEST_OPERATOR_A={OPERATOR_A}\n")
        overlay(self.root, "server_accounts", {
            "allow_private_hosts": ["127.0.0.1"],
            "guests": {"enabled": True, "sweep_interval_seconds": 0, "keys_file": str(keys),
                       "secrets_routes": "write_only_session", "model_sources": ["byok", "free", "local"],
                       "protection": {"pow": {"bits": 0, "bits_under_load": 0}},
                       "workdir": {"subdir": "guests", "idle_ttl_seconds": 0, "max_bytes": 0}}})
        overlay(self.root, "free_providers", {
            "providers": [{"id": "provider_a", "display_name": "Invented A", "tier": "free", "enabled": True,
                           "base_url": self.a.base, "auth_env": "LOOM_TEST_OPERATOR_A", "auth_secret": "api_key",
                           "active_when": "key_present", "data_terms": {"training": "no"}}],
            "free_model_choice": {"tier_order": ["free"], "table": [
                {"model": "mock/credentials", "provider": "provider_a", "quality": 1.0}]}})
        self.srv = Server(self.root)
        self.addCleanup(lambda: self.srv.close())
        self.g = requests.Session()
        self.g.trust_env = False
        self.addCleanup(self.g.close)
        r = self.g.post(self.srv.base + "/api/auth/guest", json=solve_pow(self.srv.base, self.g), timeout=30)
        self.assertEqual(r.status_code, 201, r.text)
        self.patch({"stream": False, "semantic_analysis": False, "auto_title": False})
        r = self.g.post(self.srv.base + "/api/conversations", json={"title": "Credential test"}, timeout=15)
        self.assertEqual(r.status_code, 201, r.text)
        self.conv = r.json()["id"]

    def patch(self, values):
        r = self.g.patch(self.srv.base + "/api/config", json=values, timeout=15)
        self.assertEqual(r.status_code, 200, r.text)

    def key(self, value):
        r = self.g.post(self.srv.base + "/api/secrets/api_key", json={"value": value}, timeout=15)
        self.assertEqual(r.status_code, 200, r.text)

    def chat(self):
        return self.g.post(self.srv.base + "/api/chat", json={"conv_id": self.conv, "message": "test", "stream": False}, timeout=45)

    def reply(self):
        r = self.chat()
        self.assertEqual(r.status_code, 200, r.text)
        self.assertIn("Local credential test reply", r.text)

    def restart(self):
        self.srv.close()
        self.srv = Server(self.root)

    def test_endpoint_change_without_byok_never_sends_operator_a_to_b(self):
        self.reply()
        self.assertEqual(self.a.calls[0][0], "/chat/completions")
        self.assertEqual(self.a.calls[0][2]["model"], "mock/credentials")
        self.assertEqual([c[1] for c in self.a.calls], [f"Bearer {OPERATOR_A}"])
        self.patch({"base_url": self.b.base})  # keep loom_guest_source unchanged, as the settings UI can
        self.chat()
        self.assertEqual(self.b.calls, [], "B must receive no request without its own credential")
        self.key(BYOK_B)
        self.reply()
        self.assertEqual([c[1] for c in self.b.calls], [f"Bearer {BYOK_B}"])

    def test_byok_before_endpoint_change_and_after_restart(self):
        self.key(BYOK_B)
        self.patch({"base_url": self.b.base})
        self.reply()
        self.assertEqual([c[1] for c in self.b.calls], [f"Bearer {BYOK_B}"])
        self.restart()
        self.reply()
        self.assertEqual([c[1] for c in self.b.calls], [f"Bearer {BYOK_B}"] * 2)
        self.assertEqual(self.a.calls, [])

    def test_return_to_operator_endpoint_restores_chat_credential(self):
        self.reply()
        self.patch({"base_url": self.b.base})
        self.chat()
        self.assertEqual(self.b.calls, [])
        self.patch({"base_url": self.a.base})
        self.reply()
        self.assertEqual([c[1] for c in self.a.calls], [f"Bearer {OPERATOR_A}"] * 2)

    def test_return_to_operator_endpoint_restores_model_refresh_credential(self):
        self.patch({"base_url": self.b.base})
        self.g.post(self.srv.base + "/api/models/refresh", json={}, timeout=15)
        self.assertEqual(self.b.model_auth, [])
        self.patch({"base_url": self.a.base})
        r = self.g.post(self.srv.base + "/api/models/refresh", json={}, timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(self.a.model_auth, [f"Bearer {OPERATOR_A}"])

    def test_return_from_own_marker_restores_operator_credential(self):
        self.patch({"loom_guest_source": "own"})
        self.chat()
        self.assertEqual(self.a.calls, [])
        self.patch({"loom_guest_source": "provider_a"})
        self.reply()
        self.assertEqual([c[1] for c in self.a.calls], [f"Bearer {OPERATOR_A}"])

    def test_empty_byok_never_restores_operator_credential(self):
        self.key("")
        self.chat()
        self.assertEqual(self.a.calls, [])
        self.restart()
        self.chat()
        self.assertEqual(self.a.calls, [])

    def test_byok_on_original_endpoint_survives_restart(self):
        self.key(BYOK_A)
        self.reply()
        self.assertEqual([c[1] for c in self.a.calls], [f"Bearer {BYOK_A}"])
        self.restart()
        self.reply()
        self.assertEqual([c[1] for c in self.a.calls], [f"Bearer {BYOK_A}"] * 2)

    def test_source_marker_change_also_releases_operator_overlay(self):
        self.patch({"loom_guest_source": "own"})
        self.chat()
        self.assertEqual(self.a.calls, [])

    def test_model_refresh_after_endpoint_change_never_sends_operator_a_to_b(self):
        self.patch({"base_url": self.b.base})
        self.g.post(self.srv.base + "/api/models/refresh", json={}, timeout=15)
        self.assertEqual(self.b.model_auth, [])
        self.key(BYOK_B)
        r = self.g.post(self.srv.base + "/api/models/refresh", json={}, timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(self.b.model_auth[-1:], [f"Bearer {BYOK_B}"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
