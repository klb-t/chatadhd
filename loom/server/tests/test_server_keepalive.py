#!/usr/bin/env python3
"""S1 (intermittent "other side closed" at sign-in): how long loom-server keeps an idle persistent connection is a setting, not a constant
of the HTTP library. The server advertises it on every answer ("Keep-Alive: timeout=N") so a client can drop an idle socket in time, and
closes the idle connection at it. A client whose event loop is stopped for longer than that cannot drop it, which is why the time has to be
known, configurable (--keep-alive-timeout-s / LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S, preset 5, 0 = no persistent connections) and testable
with a short value. e2e/stale-connection.mjs covers the client side of the same failure.
Raw sockets only: no third-party package is needed."""
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

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


class Conn:
    """One HTTP/1.1 connection, spoken by hand."""

    def __init__(self, port: int):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.buf = b""

    def get(self, path: str = "/api/healthz") -> tuple[str, bytes, float]:
        self.sock.sendall(f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n".encode())
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(65536)
            assert chunk, "the server closed the connection before it answered"
            self.buf += chunk
        head, _, rest = self.buf.partition(b"\r\n\r\n")
        match = re.search(rb"(?im)^content-length:\s*(\d+)", head)
        length = int(match.group(1)) if match else 0
        while len(rest) < length:
            chunk = self.sock.recv(65536)
            assert chunk, "the server closed the connection inside the body"
            rest += chunk
        self.buf = rest[length:]
        return head.decode("latin1"), rest[:length], time.monotonic()

    def idle_until_closed(self, limit: float) -> float:
        """Seconds until the server closes this (idle) connection; AssertionError if it is still open after `limit`."""
        start = time.monotonic()
        self.sock.settimeout(limit)
        try:
            data = self.sock.recv(1)
        except socket.timeout:
            raise AssertionError(f"the idle connection was still open after {limit} s")
        except ConnectionResetError:
            data = b""
        assert data == b"", f"the server sent {data!r} on an idle connection"
        return time.monotonic() - start

    def close(self):
        self.sock.close()


class Server:
    def __init__(self, extra, env=None):
        self.dir = tempfile.TemporaryDirectory()
        self.port = free_port()
        full_env = dict(os.environ)
        full_env.pop("LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S", None)
        full_env.update(env or {})
        self.proc = subprocess.Popen([BINARY, "--host", "127.0.0.1", "--port", str(self.port), "--data-dir", self.dir.name, *extra],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=full_env)
        deadline = time.time() + 15
        while time.time() < deadline:
            assert self.proc.poll() is None, "loom-server exited: " + self.proc.stderr.read().decode(errors="replace")[-600:]
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=1).close()
                return
            except OSError:
                time.sleep(0.1)
        self.stop()
        raise RuntimeError("loom-server never came up")

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=15)
        finally:
            for pipe in (self.proc.stdout, self.proc.stderr):
                pipe.close()
            self.dir.cleanup()


def advertised(head: str):
    match = re.search(r"(?im)^keep-alive:\s*timeout=(\d+)", head)
    return int(match.group(1)) if match else None


def main():
    # 1. the preset: 5 s, advertised, and the connection stays usable
    server = Server([])
    try:
        conn = Conn(server.port)
        head, body, _ = conn.get()
        assert head.startswith("HTTP/1.1 200"), head
        assert advertised(head) == 5, ("preset is 5 s and is advertised", head)
        assert not re.search(r"(?im)^connection:\s*close", head), head
        head, _, _ = conn.get()  # the same connection answers again
        assert head.startswith("HTTP/1.1 200"), head
        conn.close()
    finally:
        server.stop()

    # 2. a lowered time: advertised, the connection is kept within it, closed at it
    server = Server(["--keep-alive-timeout-s", "1"])
    try:
        conn = Conn(server.port)
        head, _, _ = conn.get()
        assert advertised(head) == 1, ("the flag is advertised", head)
        time.sleep(0.3)
        head, _, _ = conn.get()
        assert head.startswith("HTTP/1.1 200"), ("a request within the time uses the same connection", head)
        closed_after = conn.idle_until_closed(limit=6)
        assert 0.6 <= closed_after <= 3.5, f"closed after {closed_after:.2f} s of idleness, expected about 1 s"
        conn.close()
    finally:
        server.stop()

    # 3. the environment variable sets it; the flag wins over it
    server = Server([], env={"LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S": "2"})
    try:
        conn = Conn(server.port)
        assert advertised(conn.get()[0]) == 2, "environment"
        conn.close()
    finally:
        server.stop()
    server = Server(["--keep-alive-timeout-s", "1"], env={"LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S": "2"})
    try:
        conn = Conn(server.port)
        assert advertised(conn.get()[0]) == 1, "the flag wins over the environment"
        conn.close()
    finally:
        server.stop()

    # 4. 0 = no persistent connections: the answer says so and the connection is closed at once
    server = Server(["--keep-alive-timeout-s", "0"])
    try:
        conn = Conn(server.port)
        head, body, _ = conn.get()
        assert head.startswith("HTTP/1.1 200") and body, head
        assert re.search(r"(?im)^connection:\s*close", head), ("0 closes after the answer", head)
        assert advertised(head) is None, ("no keep-alive time is advertised", head)
        closed_after = conn.idle_until_closed(limit=3)
        assert closed_after < 1.0, f"closed after {closed_after:.2f} s, expected at once"
        conn.close()
    finally:
        server.stop()
    print("server keep-alive timeout: ok")


if __name__ == "__main__":
    main()
