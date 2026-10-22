#!/usr/bin/env python3
"""S1 (intermittent "other side closed" at sign-in): how long loom-server keeps an idle persistent connection is a setting, not a constant
of the HTTP library. The server advertises it on every answer ("Keep-Alive: timeout=N") so a client can drop an idle socket in time, and
closes the idle connection at it. A client whose event loop is stopped for longer than that cannot drop it, which is why the time has to be
known, configurable (--keep-alive-timeout-s / LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S, preset 5, 0 = no persistent connections) and testable
with a short value. e2e/stale-connection.mjs covers the client side of the same failure.
The number goes into a header every client reads, so it is validated (whole seconds only, clamped to 2147483647) and the time is also
the wait a NEW connection is given for its first request: both are tested here.
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
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=60)
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

    def stays_open(self, seconds: float) -> None:
        """AssertionError if the server closes this (idle) connection within `seconds`."""
        self.sock.settimeout(seconds)
        try:
            data = self.sock.recv(1)
        except socket.timeout:
            return
        except ConnectionResetError:
            data = b""
        raise AssertionError(f"the idle connection was closed within {seconds} s" if data == b"" else f"the server sent {data!r} on an idle connection")

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
        deadline = time.time() + 60   # a sanitizer build on a loaded runner needs far longer than a dev build to listen
        while time.time() < deadline:
            assert self.proc.poll() is None, "loom-server exited: " + self.proc.stderr.read().decode(errors="replace")[-600:]
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=1).close()
                return
            except OSError:
                time.sleep(0.1)
        self.stop()
        raise RuntimeError("loom-server never came up")

    def stop(self) -> str:
        """Stops the server; returns what it wrote to stderr."""
        self.proc.terminate()
        self.stderr = ""
        try:
            self.proc.wait(timeout=120)   # a sanitizer build on a loaded runner needs far longer than 15 s to shut down; the answer is awaited, not slept
            self.stderr = self.proc.stderr.read().decode(errors="replace")
        finally:
            for pipe in (self.proc.stdout, self.proc.stderr):
                pipe.close()
            self.dir.cleanup()
        return self.stderr


def rejected(extra, env=None) -> str:
    """Starts loom-server with arguments it must refuse; returns its stderr. AssertionError if it did not exit with 2 (it refuses before it opens anything, so a hang is a bug, not slowness)."""
    full_env = dict(os.environ)
    full_env.pop("LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S", None)
    full_env.update(env or {})
    with tempfile.TemporaryDirectory() as data:
        try:
            done = subprocess.run([BINARY, "--host", "127.0.0.1", "--port", str(free_port()), "--data-dir", data, *extra],
                                  stdin=subprocess.DEVNULL, capture_output=True, env=full_env,
                                  timeout=120)   # a sanitizer build on a loaded runner needs far longer than 10 s just to start; the refusal returns as soon as the process exits
        except subprocess.TimeoutExpired:
            raise AssertionError(f"loom-server {extra} {env} kept running; it had to refuse the value")
    err = done.stderr.decode(errors="replace")
    assert done.returncode == 2, (f"{extra} {env}: exit {done.returncode}, expected 2", err[-300:])
    return err


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
    # 5. a value that is not a number of seconds is refused with usage, from the flag and from the environment; none of them changes
    #    the meaning silently (strtoull made "-1" 2^64-1 -> "timeout=-1", and "abc" 0 -> persistent connections off)
    for bad in ["abc", "-1", "+5", "1.5", "5s", " 5", "0x10", ""]:
        err = rejected(["--keep-alive-timeout-s", bad])
        assert "--keep-alive-timeout-s takes a whole number of seconds" in err and "Usage:" in err, (bad, err[-300:])
    for bad in ["abc", "-1", "2.5"]:
        err = rejected([], env={"LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S": bad})
        assert "LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S takes a whole number of seconds" in err, (bad, err[-300:])

    # 6. a number too large to count is clamped to INT32_MAX seconds (said on stderr), advertised as that, and the connection is kept:
    #    9999999999 s used to be advertised and closed at once (the library's 64-bit nanoseconds wrapped)
    for flag, env in [("9999999999", None), ("18446744073709551615", None), ("99999999999999999999999", None), (None, "9999999999")]:
        server = Server(["--keep-alive-timeout-s", flag] if flag else [], env={"LOOM_SERVER_KEEP_ALIVE_TIMEOUT_S": env} if env else None)
        try:
            conn = Conn(server.port)
            head, _, _ = conn.get()
            assert advertised(head) == 2147483647, (flag, env, head)
            conn.stays_open(0.6)
            head, _, _ = conn.get()
            assert head.startswith("HTTP/1.1 200"), ("the connection is still there after the clamped time", head)
            conn.close()
        finally:
            err = server.stop()
        assert "using 2147483647 s" in err, (flag, env, err[-300:])
    server = Server(["--keep-alive-timeout-s", "2147483647"])  # the largest value itself is not clamped and says nothing
    try:
        conn = Conn(server.port)
        assert advertised(conn.get()[0]) == 2147483647
        conn.close()
    finally:
        err = server.stop()
    assert "using 2147483647 s" not in err, err[-300:]

    # 7. the time is also the wait of a NEW connection for its first request (the library checks it before every request): with 1 s a
    #    client that connects and writes 1.8 s later finds the connection closed; with 3 s the same wait is fine. Said in app.h and the usage.
    server = Server(["--keep-alive-timeout-s", "1"])
    try:
        conn = Conn(server.port)
        time.sleep(1.8)
        try:
            conn.get()
        except (AssertionError, OSError):
            pass  # closed before it answered: the sentence of app.h is true
        else:
            raise AssertionError("a new connection that waited longer than the time was served")
        conn.close()
    finally:
        server.stop()
    server = Server(["--keep-alive-timeout-s", "3"])
    try:
        conn = Conn(server.port)
        time.sleep(1.8)
        assert conn.get()[0].startswith("HTTP/1.1 200"), "a wait inside the time is served"
        conn.close()
    finally:
        server.stop()
    print("server keep-alive timeout: ok")


if __name__ == "__main__":
    main()
