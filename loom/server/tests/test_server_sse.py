#!/usr/bin/env python3
"""P14: the live event feed (GET /api/events/stream) holds a server thread while it is open. An idle feed probes its client with an SSE
comment, so a page that went away frees its thread (without it a handful of closed tabs would exhaust the pool), and a stopping server
ends every feed instead of waiting for an event that never comes."""
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

try:
    import requests
except ImportError:
    print("SKIP: python 'requests' package not installed")
    sys.exit(0)

BINARY = os.environ.get("LOOM_SERVER_BIN")
if not BINARY or not Path(BINARY).is_file():
    print("SKIP: built LOOM_SERVER_BIN required")
    sys.exit(0)


def free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start(extra, data):
    port = free_port()
    process = subprocess.Popen([BINARY, "--host", "127.0.0.1", "--port", str(port), "--data-dir", data, *extra],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f"http://127.0.0.1:{port}"
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            if requests.get(base + "/api/healthz", timeout=1).status_code == 200:
                return process, base
        except requests.RequestException:
            time.sleep(0.1)
    process.kill()
    raise RuntimeError("loom-server never came up")


def main():
    with tempfile.TemporaryDirectory(prefix="loom-sse-") as data:
        # 1. an idle feed says it is alive
        process, base = start(["--sse-keepalive-ms", "300"], data)
        try:
            feed = requests.get(base + "/api/events/stream", params={"event": "action:client_step"}, stream=True, timeout=10)
            assert feed.status_code == 200 and feed.headers["Content-Type"].startswith("text/event-stream"), feed.headers
            started = time.time()
            for line in feed.iter_lines():
                if line == b": keep-alive":
                    break
            else:
                raise AssertionError("the feed ended without a probe")
            assert time.time() - started < 5, "the probe comes within the interval, not when an event happens"
            feed.close()

            # 2. tabs that went away free their threads: more feeds than the pool has threads, all dropped, the server still answers
            feeds = [requests.get(base + "/api/events/stream", params={"event": "action:client_step"}, stream=True, timeout=10) for _ in range(6)]
            for f in feeds:
                f.close()
            extra = []
            for _ in range(24):
                try:
                    extra.append(requests.get(base + "/api/events/stream", params={"event": "x"}, stream=True, timeout=0.3))
                except requests.RequestException:
                    pass  # beyond the pool a connection waits; it is dropped below like the others
            for f in extra:
                f.close()
            deadline = time.time() + 20
            answered = False
            while time.time() < deadline and not answered:
                try:
                    answered = requests.get(base + "/api/healthz", timeout=2).status_code == 200
                except requests.RequestException:
                    time.sleep(0.2)
            assert answered, "feeds of clients that went away still hold the server's threads"

            # 3. a stopping server does not wait for an idle feed
            feed = requests.get(base + "/api/events/stream", params={"event": "*"}, stream=True, timeout=10)
            assert feed.status_code == 200
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=10)
            feed.close()
        finally:
            if process.poll() is None:
                process.kill()
        # 4. keep-alive 0 is the old behaviour: no probe is written
        process, base = start(["--sse-keepalive-ms", "0"], data)
        try:
            feed = requests.get(base + "/api/events/stream", params={"event": "action:client_step"}, stream=True, timeout=1.5)
            got = b""
            try:
                for chunk in feed.iter_content(chunk_size=1):
                    got += chunk
            except requests.RequestException:
                pass
            assert b"keep-alive" not in got, got
        finally:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
    print("server.sse: ok")


if __name__ == "__main__":
    main()
