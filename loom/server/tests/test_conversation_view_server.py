#!/usr/bin/env python3
"""Real HTTP -> C ABI -> Catalog read with a public synthetic reference.

No credentials, model requests, private archives or UI parsers. Temporary
fixtures/logs remain available for diagnosis; only the source holds the canary.
"""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
import zipfile

CANARY = "SYNTHETIC_P4_HTTP_PAYLOAD_90571"


def encoded(value):
    return json.dumps(value, ensure_ascii=False).encode("utf-8")


class ConversationViewServerTest(unittest.TestCase):
    def setUp(self):
        self.binary = os.environ.get("LOOM_SERVER_BIN")
        if not self.binary or not Path(self.binary).is_file():
            self.fail("built LOOM_SERVER_BIN is required for the registered server gate")
        self.directory = Path(tempfile.mkdtemp(prefix="loom-p4-view-server-"))
        self.data = self.directory / "store"
        self.data.mkdir()
        (self.data / "profiles").mkdir()
        (self.data / "config.json").write_bytes(encoded({"semantic_analysis": False,
            "semantic_model": "", "auto_title": False, "default_model": ""}))
        for domain, overrides in (("resource_read", {"projection_storage": "transient", "content_index": "none"}),
                                  ("worker", {"startup_delay_ms": 600000})):
            (self.data / "profiles" / f"{domain}.pack").write_bytes(encoded({
                "schema": "loom.runtime_profile_overlay/1", "domain": domain, "overrides": overrides}))
        self.source = self.directory / "public.zip"
        document = [{"id": "public-http-view", "title": "Public HTTP fixture", "current_node": "a",
                     "future": {"keep": 19}, "mapping": {
                         "u": {"parent": None, "children": ["a"], "message": {"id": "u",
                               "author": {"role": "user"}, "content": {"content_type": "text", "parts": [CANARY]}, "metadata": {}}},
                         "a": {"parent": "u", "children": [], "message": {"id": "a",
                               "author": {"role": "assistant"}, "content": {"content_type": "text", "parts": ["Public answer"]},
                               "metadata": {"model_slug": "synthetic/model"}}}}}]
        with zipfile.ZipFile(self.source, "w") as archive:
            archive.writestr("conversations.json", encoded(document))
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.process = None
        self.server_log = None
        self.starts = 0
        self.addCleanup(self.stop_server)
        self.start_server()
        self.call("POST", "/api/semantic/pause", {})
        self.call("POST", "/api/catalog/scan", {"sources": [str(self.source)], "retain_raw": "none"})
        imported, _ = self.call("POST", "/api/catalog/import", {"mode": "full", "store_mode": "link"})
        self.assertEqual(len(imported["conversations"]), 1)
        self.conv = imported["conversations"][0]
        self.view = f"/api/conversations/{self.conv}/view"

    def start_server(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        self.url = f"http://127.0.0.1:{port}"
        self.starts += 1
        self.server_log = (self.directory / f"server-{self.starts}.log").open("wb")
        self.process = subprocess.Popen([self.binary, "--host", "127.0.0.1", "--port", str(port),
            "--data-dir", str(self.data), "--token", "public-local-test-token"],
            stdout=self.server_log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self.fail(f"server exited; fixture/logs at {self.directory}")
            try:
                result, _ = self.call("GET", "/api/healthz")
                if result["ok"]:
                    return
            except (OSError, urllib.error.URLError):
                time.sleep(0.05)
        self.fail(f"server startup timeout; fixture/logs at {self.directory}")

    def stop_server(self):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
            self.process = None
        if self.server_log is not None:
            self.server_log.close()
            self.server_log = None

    def call(self, method, path, body=None, status=200, authorized=True):
        headers = {"Content-Type": "application/json"}
        if authorized:
            headers["Authorization"] = "Bearer public-local-test-token"
        request = urllib.request.Request(self.url + path, method=method, headers=headers,
            data=None if body is None else encoded(body))
        try:
            response = self.opener.open(request, timeout=20)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            content = response.read()
            self.assertEqual(response.status, status, (path, content.decode("utf-8")))
            return json.loads(content), response.headers

    def test_explicit_read_metadata_authorization_reopen_and_payload_boundary(self):
        denied, _ = self.call("POST", self.view, {}, status=401, authorized=False)
        self.assertIn("error", denied)
        metadata, headers = self.call("GET", self.view)
        self.assertEqual(headers.get("Cache-Control"), "no-store")
        self.assertEqual(metadata["resources"][0]["status"], "read_denied")
        self.assertNotIn("method_receipt", metadata["resources"][0])
        self.assertNotIn(CANARY, json.dumps(metadata))
        self.assertEqual(metadata["read_configuration"]["values"]["projection_storage"], "transient")
        for malformed in ([], {"read_authorized": True}, {"read_options": []}):
            result, invalid_headers = self.call("POST", self.view, malformed, status=400)
            self.assertEqual(result["error"]["code"], "invalid_argument")
            self.assertEqual(invalid_headers.get("Cache-Control"), "no-store")
        complete, headers = self.call("POST", self.view, {})
        self.assertEqual(headers.get("Cache-Control"), "no-store")
        self.assertEqual(complete["status"], "complete")
        self.assertEqual(len(complete["messages"]), 2)
        self.assertEqual(complete["messages"][0]["text"], CANARY)
        self.assertEqual(complete["messages"][1]["parent_id"], complete["messages"][0]["id"])
        self.assertFalse(complete["capabilities"]["source_history_send"]["available"])
        for message in complete["messages"]:
            self.assertEqual(message["storage"], "reference")
            self.assertFalse(message["capabilities"]["edit"])
        stored, _ = self.call("GET", f"/api/conversations/{self.conv}/messages?all=1")
        self.assertEqual(len(stored), 1)
        self.assertNotIn(CANARY, json.dumps(stored))
        self.stop_server()
        self.start_server()
        repeated, _ = self.call("POST", self.view, {})
        self.assertEqual(repeated["messages"], complete["messages"])
        self.source.rename(self.source.with_suffix(".away"))
        missing, _ = self.call("POST", self.view, {})
        self.assertEqual(missing["status"], "unavailable")
        self.assertFalse(missing["resources"][0]["current"])
        self.assertEqual(len(missing["messages"]), 1)
        self.assertTrue(missing["omissions"])
        self.stop_server()
        for path in [*self.data.rglob("*"), *self.directory.glob("server-*.log")]:
            if path.is_file():
                self.assertNotIn(CANARY.encode(), path.read_bytes(), str(path))


if __name__ == "__main__":
    unittest.main(verbosity=2)
