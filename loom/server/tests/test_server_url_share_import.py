#!/usr/bin/env python3
"""Deferred M2 authenticated HTTP gate. Requires the integrator's OWN server build.

This never relaxes production URL protections. A loopback source is expected to
be denied without contacting its synthetic listener. Successful fetch -> native
store/reopen uses the isolated transport in test_url_share_import.cpp; this file
is not evidence of successful URL ingestion through the server.
"""
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Source(BaseHTTPRequestHandler):
    calls = 0
    def log_message(self, *args):
        pass
    def do_GET(self):
        type(self).calls += 1
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"synthetic source that must not be reached")
    do_POST = do_GET


def main():
    binary = os.environ.get("LOOM_SERVER_BIN")
    if not binary or not Path(binary).is_file():
        raise SystemExit("Set LOOM_SERVER_BIN to the integrator's reserved build; no automatic build or skip")
    source = ThreadingHTTPServer(("127.0.0.1", 0), Source)
    source_thread = threading.Thread(target=source.serve_forever, daemon=True)
    source_thread.start()
    token = "M2_SYNTHETIC_TEST_TOKEN"
    proc = None
    with tempfile.TemporaryDirectory(prefix="loom_m2_http_") as data:
        try:
            port = free_port()
            # Synthetic model route counts any unexpected model usage.
            Path(data, "config.json").write_text(json.dumps({"base_url": f"http://127.0.0.1:{source.server_port}", "semantic_model": "synthetic-model", "semantic_analysis": True}))
            env = dict(os.environ, LOOM_SERVER_TOKEN=token)
            proc = subprocess.Popen([binary, "--host", "127.0.0.1", "--port", str(port), "--data-dir", data], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                try:
                    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=1)
                    conn.request("GET", "/api/healthz")
                    conn.getresponse().read(); conn.close()
                    break
                except OSError:
                    time.sleep(.05)
            else:
                raise AssertionError("server startup failed")
            descriptor = {"schema": "loom.url_import_request/1", "mode": "share", "url": f"http://127.0.0.1:{source.server_port}/share"}
            boundary = "M2SyntheticBoundary"
            body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="source.loomurl"\r\nContent-Type: application/json\r\n\r\n' + json.dumps(descriptor) + f'\r\n--{boundary}--\r\n').encode()
            def upload(auth):
                headers = {"Content-Type": "multipart/form-data; boundary=" + boundary}
                if auth:
                    headers["Authorization"] = "Bearer " + token
                conn = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
                try:
                    conn.request("POST", "/api/import", body=body, headers=headers)
                    response = conn.getresponse()
                    return response.status, response.read().decode()
                finally:
                    conn.close()
            assert upload(False)[0] == 401
            results = []
            for _ in range(2):
                status, text = upload(True)
                assert status == 200
                events = [json.loads(line[5:].strip()) for line in text.split("\n") if line.startswith("data:")]
                result = next(e for e in events if e.get("type") == "done")
                assert result["format"] == "url" and result["status"] == "denied"
                assert result["reason"] == "address_not_public"
                assert result["messages"] == 0 and result["conversations"] == []
                assert result["llm_calls"] == 0
                results.append(result)
            assert results[0]["source_id"] == results[1]["source_id"]
            assert results[1]["already_imported"] is True
            assert Source.calls == 0
            print("PASS: authenticated import hook, denied loopback, idempotent receipt, 0 source/model requests")
        finally:
            if proc is not None:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait(timeout=5)
            source.shutdown(); source.server_close(); source_thread.join()


if __name__ == "__main__":
    main()
