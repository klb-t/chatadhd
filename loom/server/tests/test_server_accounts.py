#!/usr/bin/env python3
"""Accounts mode of loom-server (P1): isolation of two users, session requirement, logout,
wrong password, registration setting, public-bind refusal. Needs the built binary
(LOOM_SERVER_BIN) and python `requests`."""
import json
import os
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

BIN = os.environ.get("LOOM_SERVER_BIN", "loom-server")


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def cli(root: Path, *args: str, stdin: str = "") -> subprocess.CompletedProcess:
    return subprocess.run([BIN, "user", *args, "--accounts-root", str(root)], input=stdin, text=True,
                          capture_output=True, timeout=120)


def write_profile(root: Path, **values) -> None:
    (root / "profiles").mkdir(parents=True, exist_ok=True)
    (root / "profiles" / "server_accounts.pack").write_text(json.dumps(
        {"schema": "loom.runtime_profile_overlay/1", "domain": "server_accounts", "overrides": values}))


class Server:
    def __init__(self, root: Path):
        self.port = free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.proc = subprocess.Popen([BIN, "--port", str(self.port), "--accounts-root", str(root)],
                                     stderr=subprocess.PIPE, text=True)
        deadline = time.time() + 20
        while time.time() < deadline:
            try:
                if requests.get(self.base + "/api/healthz", timeout=1).status_code == 200:
                    return
            except requests.RequestException:
                time.sleep(0.1)
        raise RuntimeError("server did not start")

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=15)


def login(base, name, pw, status=200):
    s = requests.Session()
    r = s.post(base + "/api/auth/login", json={"login": name, "password": pw}, timeout=60)
    assert r.status_code == status, (r.status_code, r.text)
    return s, r


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="loom_accounts_"))
    # 1. public bind without auth is refused with the reason and the override flag
    r = subprocess.run([BIN, "--host", "0.0.0.0", "--port", str(free_port())], capture_output=True, text=True, timeout=20)
    assert r.returncode == 2 and "--allow-unauthenticated-public" in r.stderr and "refusing" in r.stderr, r.stderr

    root = tmp / "accounts"
    # a fast Argon2id preset keeps the test quick; the stored hash carries its own parameters
    write_profile(root, argon2id_preset="owasp_min")
    p = cli(root, "add", "alice", "--password-stdin", stdin="alice-password\n")
    assert p.returncode == 0, p.stderr
    assert cli(root, "add", "bob", "--password-stdin", stdin="bob-password!\n").returncode == 0
    assert cli(root, "add", "bob", "--password-stdin", stdin="x-password-y\n").returncode != 0
    assert (root / "accounts.sqlite").exists()

    srv = Server(root)
    try:
        b = srv.base
        me = requests.get(b + "/api/auth/me").json()
        assert me == {"accounts": True, "authenticated": False, "registration": "closed"}, me
        # unauthenticated requests are rejected
        for path in ("/api/conversations", "/api/config", "/api/secrets"):
            assert requests.get(b + path).status_code == 401, path
        # wrong password / unknown user
        login(b, "alice", "nope-nope-nope", 401)
        login(b, "ghost", "whatever-1234", 401)
        # registration closed
        r = requests.post(b + "/api/auth/register", json={"login": "eve", "password": "eve-password"})
        assert r.status_code == 403, r.text

        sa, ra = login(b, "alice", "alice-password")
        sb, rb = login(b, "bob", "bob-password!")
        set_cookie = ra.headers["Set-Cookie"]
        assert "HttpOnly" in set_cookie and "SameSite=Strict" in set_cookie, set_cookie
        assert sa.get(b + "/api/auth/me").json()["user"]["login"] == "alice"

        # data isolation: alice's conversation and secret are invisible to bob
        c = sa.post(b + "/api/conversations", json={"title": "alice private"})
        assert c.status_code in (200, 201), c.text
        assert sa.post(b + "/api/secrets/api_key", json={"value": "sk-alice"}).status_code == 200
        la = sa.get(b + "/api/conversations").json()
        lb = sb.get(b + "/api/conversations").json()
        assert "alice private" in json.dumps(la) and "alice private" not in json.dumps(lb), (la, lb)
        assert sa.get(b + "/api/secrets/api_key/has").json() == {"has": True}
        assert sb.get(b + "/api/secrets/api_key/has").json() == {"has": False}
        ids = [u.split("\t")[0] for u in cli(root, "list").stdout.splitlines()]
        assert len(ids) == 2 and all((root / "users" / i).is_dir() for i in ids)
        assert any((root / "users" / i / "secrets.json").exists() for i in ids)

        # bearer token works without the cookie
        tok = ra.json()["token"]
        assert requests.get(b + "/api/conversations", headers={"Authorization": "Bearer " + tok}).status_code == 200
        assert requests.get(b + "/api/conversations", headers={"Authorization": "Bearer nope"}).status_code == 401

        # password change ends old sessions
        assert sb.post(b + "/api/auth/password", json={"current": "bad", "new": "new-password-1"}).status_code == 401
        rr = sb.post(b + "/api/auth/password", json={"current": "bob-password!", "new": "new-password-1"})
        assert rr.status_code == 200, rr.text
        login(b, "bob", "bob-password!", 401)
        login(b, "bob", "new-password-1")

        # logout invalidates the session (cookie and bearer)
        assert sa.post(b + "/api/auth/logout").status_code == 200
        assert requests.get(b + "/api/conversations", headers={"Authorization": "Bearer " + tok}).status_code == 401
        assert sa.get(b + "/api/conversations").status_code == 401

        # operator disables a user -> sessions end
        s2, r2 = login(b, "alice", "alice-password")
        assert cli(root, "disable", "alice").returncode == 0
        assert s2.get(b + "/api/conversations").status_code == 401
        login(b, "alice", "alice-password", 401)
    finally:
        srv.stop()

    # 2. registration=open and invite honoured (setting read from the profile overlay)
    write_profile(root, argon2id_preset="owasp_min", registration="open")
    srv = Server(root)
    try:
        r = requests.post(srv.base + "/api/auth/register", json={"login": "eve", "password": "eve-password"}, timeout=60)
        assert r.status_code == 201 and "token" in r.json(), r.text
        r = requests.post(srv.base + "/api/auth/register", json={"login": "eve", "password": "eve-password"}, timeout=60)
        assert r.status_code == 409, r.text
        r = requests.post(srv.base + "/api/auth/register", json={"login": "x", "password": "short"}, timeout=60)
        assert r.status_code == 400, r.text
    finally:
        srv.stop()
    write_profile(root, argon2id_preset="owasp_min", registration="invite")
    code = cli(root, "invite").stdout.strip()
    srv = Server(root)
    try:
        body = {"login": "dan", "password": "dan-password"}
        assert requests.post(srv.base + "/api/auth/register", json=body, timeout=60).status_code == 403
        assert requests.post(srv.base + "/api/auth/register", json={**body, "invite": code}, timeout=60).status_code == 201
        assert requests.post(srv.base + "/api/auth/register", json={**body, "login": "dan2", "invite": code}, timeout=60).status_code == 403
    finally:
        srv.stop()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
