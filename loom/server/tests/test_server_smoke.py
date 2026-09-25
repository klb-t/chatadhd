#!/usr/bin/env python3
"""API smoke test for loom-server: starts the built binary against a throwaway
data dir, drives a handful of REST endpoints with `requests`, and checks the
shapes/status codes loom/server/src/app.cpp promises. Registered as a ctest
(see the LOOM_BUILD_SERVER block in the top-level CMakeLists.txt) so a broken
server build/route fails `ctest` the same way a broken C++ unit does.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

try:
    import requests
except ImportError:
    print("SKIP: python 'requests' package not installed")
    sys.exit(0)


def free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_up(base: str, timeout: float = 15.0) -> None:
    deadline = time.time() + timeout
    last_err = None
    while time.time() < deadline:
        try:
            r = requests.get(f"{base}/api/healthz", timeout=1)
            if r.status_code == 200:
                return
        except requests.RequestException as e:
            last_err = e
        time.sleep(0.1)
    raise RuntimeError(f"loom-server never came up: {last_err}")


def main() -> int:
    server_bin = os.environ.get("LOOM_SERVER_BIN")
    if not server_bin or not os.path.exists(server_bin):
        print(f"SKIP: LOOM_SERVER_BIN not set or missing ({server_bin!r})")
        return 0

    data_dir = tempfile.mkdtemp(prefix="loom_server_smoke_")
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    proc = subprocess.Popen(
        [server_bin, "--host", "127.0.0.1", "--port", str(port), "--data-dir", data_dir],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        wait_up(base)

        r = requests.get(f"{base}/api/healthz")
        assert r.status_code == 200 and r.json().get("ok") is True, r.text

        r = requests.get(f"{base}/api/version")
        assert r.status_code == 200 and "version" in r.json(), r.text

        r = requests.get(f"{base}/api/conversations")
        assert r.status_code == 200 and r.json() == [], r.text

        r = requests.post(f"{base}/api/conversations", json={"title": "Smoke test"})
        assert r.status_code == 201, r.text
        conv = r.json()
        assert conv["title"] == "Smoke test" and conv["id"].startswith("c_"), conv

        r = requests.get(f"{base}/api/conversations/{conv['id']}")
        assert r.status_code == 200 and r.json()["id"] == conv["id"], r.text

        r = requests.get(f"{base}/api/conversations/does-not-exist")
        assert r.status_code == 404, r.text
        assert r.json()["error"]["code"] == "not_found", r.text

        r = requests.patch(f"{base}/api/config", json={"default_model": "smoke/model"})
        assert r.status_code == 200 and r.json()["default_model"] == "smoke/model", r.text

        r = requests.post(f"{base}/api/secrets/api_key", json={"value": "sk-smoke"})
        assert r.status_code == 200 and r.json()["set"] is True, r.text

        r = requests.get(f"{base}/api/secrets/api_key/has")
        assert r.status_code == 200 and r.json()["has"] is True, r.text

        r = requests.get(f"{base}/api/secrets")
        assert r.status_code == 200 and "api_key" in r.json(), r.text
        # Never leaks the value itself.
        assert "sk-smoke" not in r.text

        r = requests.get(f"{base}/api/semantic/status")
        assert r.status_code == 200 and "pending" in r.json(), r.text

        r = requests.post(f"{base}/api/memory", json={"content": "root note"})
        assert r.status_code == 201, r.text
        mem = r.json()

        r = requests.get(f"{base}/api/memory")
        assert r.status_code == 200 and any(n["id"] == mem["id"] for n in r.json()["nodes"]), r.text

        r = requests.get(f"{base}/api/graph/data")
        assert r.status_code == 200 and "nodes" in r.json() and "edges" in r.json(), r.text

        r = requests.get(f"{base}/api/models")
        assert r.status_code == 200, r.text

        r = requests.get(f"{base}/api/logs?max_lines=5")
        assert r.status_code == 200 and isinstance(r.json(), list), r.text

        # SSE chat: no upstream configured, so this must fail cleanly with a
        # well-formed "error" chunk rather than hang or crash the server.
        with requests.post(f"{base}/api/chat", json={"message": "hi"}, stream=True, timeout=10) as r:
            assert r.status_code == 200
            saw_start = saw_terminal = False
            for line in r.iter_lines(decode_unicode=True):
                if not line or not line.startswith("data:"):
                    continue
                chunk = json.loads(line[len("data:"):].strip())
                if chunk["type"] == "start":
                    saw_start = True
                if chunk["type"] in ("done", "error"):
                    saw_terminal = True
                    break
            assert saw_start and saw_terminal

        print("OK: all loom-server smoke checks passed")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
        shutil.rmtree(data_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
