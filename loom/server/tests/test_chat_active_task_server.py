#!/usr/bin/env python3
"""Synthetic HTTP facade -> C ABI -> native client -> loopback provider evidence."""
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request


def encoded(value):
    return json.dumps(value, ensure_ascii=False).encode("utf-8")


class ActiveTaskServerTest(unittest.TestCase):
    def setUp(self):
        self.binary = os.environ.get("LOOM_SERVER_BIN")
        if not self.binary or not Path(self.binary).is_file():
            self.skipTest("built LOOM_SERVER_BIN required")
        # Preserve this synthetic fixture and logs for failed-run inspection.
        self.directory = Path(tempfile.mkdtemp(prefix="loom-w1-server-"))
        self.calls = []
        self.next_status = 200
        fixture = self

        class Provider(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                status = fixture.next_status
                fixture.next_status = 200
                number = len(fixture.calls) + 1
                fixture.calls.append({"path": self.path, "payload": payload, "status": status})
                answer = ({"choices": [{"message": {"content": f"REJECTED_SERVER_ATTEMPT_{number}"}}]}
                          if status == 200 else {"error": {"message": "synthetic provider unavailable"}})
                body = encoded(answer)
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        self.provider = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Provider)
        self.provider_thread = threading.Thread(target=self.provider.serve_forever, daemon=True)
        self.provider_thread.start()
        self.addCleanup(self.stop_provider)
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.process = None
        self.server_log = None
        self.starts = 0
        self.addCleanup(self.stop_server)
        config = {"base_url": f"http://127.0.0.1:{self.provider.server_port}",
                  "default_model": "synthetic/local", "auto_title": False,
                  "semantic_analysis": False, "system_prompt": "", "stream": False}
        (self.directory / "config.json").write_bytes(encoded(config))
        self.start_server()
        self.request("POST", "/api/secrets/api_key", {"value": "local-test-placeholder"})
        self.conv = self.request("POST", "/api/conversations", {"title": "W1 server revision fixture"})["id"]
        self.base = {"conv_id": self.conv, "stream": False, "include_memory": False,
                     "include_graph_memory": False}

    def stop_provider(self):
        self.provider.shutdown()
        self.provider.server_close()
        self.provider_thread.join(timeout=5)

    def start_server(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        self.url = f"http://127.0.0.1:{port}"
        self.starts += 1
        self.server_log = (self.directory / f"server-{self.starts}.log").open("wb")
        self.process = subprocess.Popen([self.binary, "--host", "127.0.0.1", "--port", str(port),
                                         "--data-dir", str(self.directory), "--token", "local-test-token"],
                                        stdout=self.server_log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self.fail(f"server exited; logs preserved at {self.directory}")
            try:
                if self.request("GET", "/api/healthz")["ok"]:
                    return
            except (OSError, urllib.error.URLError):
                time.sleep(0.05)
        self.fail(f"server startup timeout; logs preserved at {self.directory}")

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

    def request(self, method, path, body=None, sse=False):
        req = urllib.request.Request(self.url + path, data=None if body is None else encoded(body),
                                     method=method, headers={"Content-Type": "application/json",
                                                             "Authorization": "Bearer local-test-token"})
        with self.opener.open(req, timeout=20) as response:
            content = response.read().decode("utf-8")
            if sse:
                self.assertEqual(response.headers.get_content_type(), "text/event-stream")
                return [json.loads(line[5:].strip()) for line in content.splitlines() if line.startswith("data:")]
            return json.loads(content)

    def chat(self, text, **options):
        return self.request("POST", "/api/chat", dict(self.base, message=text, **options), sse=True)

    def rows(self):
        return self.request("GET", f"/api/conversations/{self.conv}/messages?all=1")

    def message(self, identity):
        return self.request("GET", f"/api/messages/{identity}")

    def accepted(self, events):
        self.assertEqual([e["type"] for e in events], ["start", "done"])
        self.assertEqual(events[0]["request_id"], events[1]["request_id"])
        self.assertEqual(events[0]["conv_id"], self.conv)
        return events[0]["user_message_id"], events[1]

    def specification(self, version, sources, previous=None):
        events = [f"server-v{version}-source-{i}" for i in range(len(sources))]
        spec = {"schema": "loom.active_task_spec/1", "product_ref": {"kind": "product", "id": f"server-product-{version}"},
                "goal_id": "server-report", "knowledge_run": None,
                "scope": {"conversation_id": self.conv, "branch_id": "native:active", "task_id": "server-report"},
                "version": version, "previous_product_ref": previous,
                "known_at": "2026-10-01T00:00:00Z", "representation": "derived_product",
                "materializer": {"id": "synthetic-server-test", "version": "1"},
                "history_event_ids": events,
                "source_refs": [{"event_id": event, "locator": {"source": "synthetic-server"},
                                 "known_at": "2026-09-30T22:00:00Z", "quote": row["text"]}
                                for event, row in zip(events, sources)],
                "statements": [{"id": "goal", "kind": "goal", "status": "active",
                                "text": f"Compile report revision {version}; retain Żółć audit identifiers.",
                                "source_event_ids": [events[0]], "claim_ids": [], "conditions": [], "supersedes": []}],
                "compiled_instruction": {"text": "UNTRUSTED", "source_map": [
                    {"span": {"byte_start": 0, "byte_len": 9}, "statement_ids": ["goal"]}]}}
        bindings = {event: {"message_id": row["id"], "text_sha256": hashlib.sha256(row["text"].encode()).hexdigest()}
                    for event, row in zip(events, sources)}
        return {"active_task_spec": spec, "active_task_bindings": bindings}

    def assert_payload(self, done, current, absent):
        payload = self.calls[-1]["payload"]
        self.assertEqual(self.calls[-1]["path"], "/chat/completions")
        self.assertEqual(payload["messages"], done["context_trace"]["messages"])
        self.assertEqual(sum(m["content"] == current for m in payload["messages"]), 1)
        text = json.dumps(payload["messages"], ensure_ascii=False)
        self.assertIn("Żółć", text)
        self.assertNotIn("UNTRUSTED", text)
        for marker in absent:
            self.assertNotIn(marker, text)
        self.assertEqual(sum(m["content"].startswith("[Active task specification;") for m in payload["messages"]), 1)

    def test_revision_rejection_and_trace_off_failure_survive_server_restart(self):
        self.accepted(self.chat("SOURCE_SERVER_ORIGINAL: write the report."))
        original = self.rows()
        self.assertEqual(len(original), 2)
        v1 = self.specification(1, original)
        current1 = "SOURCE_SERVER_REFINEMENT_2: use numbered sections."
        uid1, done1 = self.accepted(self.chat(current1, **v1))
        self.assert_payload(done1, current1, [r["text"] for r in original])
        new_sources = [self.message(uid1), self.message(done1["message_id"])]
        v2 = self.specification(2, new_sources, v1["active_task_spec"]["product_ref"])
        current2 = "SOURCE_SERVER_REFINEMENT_3: keep a compact appendix."
        uid2, done2 = self.accepted(self.chat(current2, **v2))
        covered = original + new_sources
        self.assert_payload(done2, current2, [r["text"] for r in covered])
        self.assertEqual(set(done2["context_trace"]["replaced_history_message_ids"]), {r["id"] for r in covered})
        saved_before = self.rows()
        rejected = self.chat("Do not revive the stale server task.", **v1)
        self.assertEqual([e["type"] for e in rejected], ["error"])
        self.assertEqual(rejected[0]["code"], "invalid_argument")
        self.assertEqual(len(self.calls), 3)
        self.assertEqual(self.rows(), saved_before)

        v3_sources = [self.message(uid2), self.message(done2["message_id"])]
        v3 = self.specification(3, v3_sources, v2["active_task_spec"]["product_ref"])
        self.next_status = 503
        failed = self.chat("Accepted even though provider fails.", trace_context=False, **v3)
        self.assertEqual([e["type"] for e in failed], ["start", "error"])
        self.assertEqual(failed[0]["request_id"], failed[-1]["request_id"])
        self.assertEqual(failed[-1]["code"], "http")
        self.assertIn("503", failed[-1]["message"])
        self.assertEqual(self.calls[-1]["status"], 503)
        self.assertEqual(self.calls[-1]["payload"]["messages"], [
            {"role": "user", "content": "[Active task specification; derived from the selected source messages]\n"
                                       "[goal] Compile report revision 3; retain Żółć audit identifiers."},
            {"role": "user", "content": "Accepted even though provider fails."},
        ])
        failed_row = self.message(failed[0]["user_message_id"])
        self.assertEqual(failed_row["role"], "user")
        self.assertEqual(failed_row["text"], "Accepted even though provider fails.")
        self.assertEqual(failed_row["metadata"]["active_task"]["supplied_spec"], v3["active_task_spec"])
        self.assertEqual(failed_row["metadata"]["active_task"]["bindings"], v3["active_task_bindings"])
        self.assertNotIn("context_trace", failed_row["metadata"])
        self.assertEqual(len(self.rows()), len(saved_before) + 1)
        before_restart = self.rows()
        old_process = self.process
        self.stop_server()
        self.assertIsNotNone(old_process.returncode)
        self.start_server()
        self.assertEqual(self.rows(), before_restart)
        stale = self.chat("Do not restart from v2 after provider failure.", **v2)
        self.assertEqual([e["type"] for e in stale], ["error"])
        self.assertEqual(stale[0]["code"], "invalid_argument")
        self.assertEqual(len(self.calls), 4)
        self.assertEqual(self.rows(), before_restart)
        _, replayed = self.accepted(self.chat("Replay the latest accepted task after restart.", **v3))
        self.assert_payload(replayed, "Replay the latest accepted task after restart.",
                            [r["text"] for r in covered + v3_sources])
        for row in covered + v3_sources:
            self.assertEqual(self.message(row["id"]), row)
        self.assertEqual(self.message(failed_row["id"]), failed_row)
        self.assertEqual(len(self.calls), 5)
        print("W1 server transport: 1 scenario, 5 local provider calls, 0 remote; "
              "stale versions emit SSE error without start/persistence/transport; full server restart verified.")
        print(f"Synthetic server fixture retained at {self.directory}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
