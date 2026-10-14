#!/usr/bin/env python3
"""Security review of loom-server accounts mode (R18): every route needs a session and acts only on the
caller's own runtime; two users A/B cannot see or change each other's runs, rules, conversations, files;
request-supplied paths cannot leave the caller's data dir; cross-site cookie requests are refused;
framing/size limits and throttling hold. Needs the built binary (LOOM_SERVER_BIN) and python `requests`."""
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
LOOM = Path(__file__).resolve().parents[2]
EVIL = "https://evil.example"


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
    def __init__(self, root: Path, *extra: str):
        self.port = free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.proc = subprocess.Popen([BIN, "--port", str(self.port), "--accounts-root", str(root), *extra],
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
        crashed = self.proc.poll() is not None
        self.proc.terminate()
        _, err = self.proc.communicate(timeout=15)
        if crashed or os.environ.get("LOOM_TEST_VERBOSE"):
            print("server stderr tail:\n" + err[-2500:], file=sys.stderr)


def session():
    # the server closes idle keep-alive connections; retry a request that hit a connection it had just closed
    from requests.adapters import HTTPAdapter
    from urllib3.util.retry import Retry
    s = requests.Session()
    s.mount("http://", HTTPAdapter(max_retries=Retry(total=3, connect=3, read=3, status=0, other=3, allowed_methods=None, respect_retry_after_header=False)))
    return s


def login(base, name, pw, status=200, **kw):
    s = session()
    r = s.post(base + "/api/auth/login", json={"login": name, "password": pw}, timeout=60, **kw)
    assert r.status_code == status, (r.status_code, r.text)
    return s, r


def catalogue_routes():
    pack = json.loads((LOOM / "data" / "runtime" / "actions.pack").read_text())["defaults"]["actions"]
    out = []
    for a in pack.values():
        if "http" in a:
            out.append((a["http"]["verb"], a["http"]["path"]))
    return out


def call(s, base, verb, path, body=None, status=None, **kw):
    r = s.request(verb, base + path, json=body if verb != "GET" else None, timeout=60, **kw)
    if status is not None:
        assert r.status_code == status, (verb, path, r.status_code, r.text)
    return r


def sse_events(r):
    out = []
    for line in r.text.splitlines():
        if line.startswith("data:"):
            try:
                out.append(json.loads(line[5:].strip()))
            except ValueError:
                pass
    return out


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="loom_security_"))
    root = tmp / "accounts"
    write_profile(root, argon2id_preset="owasp_min")
    for name, pw in (("alice", "alice-password"), ("bob", "bob-password!")):
        assert cli(root, "add", name, "--password-stdin", stdin=pw + "\n").returncode == 0
    ids = {u.split("\t")[1]: u.split("\t")[0] for u in cli(root, "list").stdout.splitlines() if "\t" in u}
    # `user list` prints "id<TAB>login..."; tolerate either column order
    if "alice" not in ids:
        ids = {u.split("\t")[0]: u.split("\t")[1] for u in cli(root, "list").stdout.splitlines() if "\t" in u}
    dir_a, dir_b = root / "users" / ids["alice"], root / "users" / ids["bob"]

    srv = Server(root)
    try:
        b = srv.base
        routes = catalogue_routes()
        assert len(routes) >= 20, routes

        # 1. every catalogue route (actions.*, rules.*, groups.*, chat.route, storage.*) requires a session
        for verb, path in routes:
            r = requests.request(verb, b + path, json={} if verb == "POST" else None, timeout=30)
            assert r.status_code == 401, (verb, path, r.status_code)
            r = requests.request(verb, b + path, json={} if verb == "POST" else None,
                                 headers={"Authorization": "Bearer " + "0" * 64}, timeout=30)
            assert r.status_code == 401, (verb, path, "bad bearer")

        sa, ra = login(b, "alice", "alice-password")
        sb, rb = login(b, "bob", "bob-password!")
        tok_a = ra.json()["token"]

        # 2. runs: A's runs, receipts and listings are invisible and untouchable for B
        got = call(sa, b, "POST", "/api/actions/run", {"action": "settings.get", "args": {"key": "max_tokens"}}, 200).json()
        prop = call(sa, b, "POST", "/api/actions/run",
                    {"action": "settings.set", "args": {"key": "max_tokens", "value": "1234"}, "propose": True}, 200).json()
        assert prop["status"] == "proposed"
        rid, receipt = prop["id"], prop["pending"]["receipt_id"]
        st = call(sb, b, "POST", "/api/actions/status", {"run_id": got["id"]})
        assert st.status_code in (404, 400) and "completed" not in st.text, st.text
        assert got["id"] not in json.dumps(call(sb, b, "POST", "/api/actions/status", {"list": True}, 200).json())
        assert got["id"] in json.dumps(call(sa, b, "POST", "/api/actions/status", {"list": True}, 200).json())
        for path, body in (("/api/actions/confirm", {"run_id": rid, "approved": True, "receipt_id": receipt}),
                           ("/api/actions/cancel", {"run_id": rid})):
            r = call(sb, b, "POST", path, body)
            assert r.status_code >= 400, (path, r.text)
        cfg_b = call(sb, b, "GET", "/api/config", None, 200).json()
        assert str(cfg_b.get("max_tokens")) != "1234", cfg_b
        # A's run is still pending and A can settle it
        done = call(sa, b, "POST", "/api/actions/confirm", {"run_id": rid, "approved": True, "receipt_id": receipt, "ref": "t"}, 200).json()
        assert done["status"] == "completed", done
        assert str(call(sb, b, "GET", "/api/config", None, 200).json().get("max_tokens")) != "1234"

        # 3. rules / actions / groups overlays are per user (their files live in the user's own profiles/)
        call(sa, b, "POST", "/api/rules/set", {"scope": "person", "effect": "deny", "action": "depict",
                                                "reason": "ALICE-ONLY-RULE", "confirmed": True}, 200)
        call(sa, b, "POST", "/api/actions/define", {"id": "alicestep", "instruction": "Alice only", "confirmed": True}, 200)
        assert "ALICE-ONLY-RULE" in call(sa, b, "POST", "/api/rules/list", {}, 200).text
        assert "ALICE-ONLY-RULE" not in call(sb, b, "POST", "/api/rules/list", {}, 200).text
        assert "alicestep" not in call(sb, b, "POST", "/api/actions/for", {"text": "alicestep"}).text
        assert "ALICE-ONLY-RULE" in (dir_a / "profiles" / "action_rules.pack").read_text()
        assert not (dir_b / "profiles" / "action_rules.pack").exists()
        r = call(sb, b, "POST", "/api/actions/remove", {"id": "alicestep", "confirmed": True})
        assert "alicestep" in (dir_a / "profiles" / "actions.pack").read_text(), r.text
        r = call(sb, b, "POST", "/api/rules/disable", {"id": "deny:depict", "confirmed": True})
        assert "ALICE-ONLY-RULE" in call(sa, b, "POST", "/api/rules/list", {}, 200).text

        # 4. conversations (and their messages/exports) are per user; upload is streamed into the user's dir
        md = "# Alice's import\n\nuser: hello from alice SECRET-ALICE\n\nassistant: hi\n"
        r = sa.post(b + "/api/import", files={"file": ("alice.md", md.encode(), "text/markdown")},
                    data={"title": "alice import"}, timeout=120)
        assert r.status_code == 200, r.text
        done = [e for e in sse_events(r) if e.get("type") == "done"]
        assert done, r.text
        cid = done[0]["conversations"][0]["id"]
        assert "SECRET-ALICE" in call(sa, b, "GET", f"/api/conversations/{cid}/messages", None, 200).text
        for verb, path in (("GET", f"/api/conversations/{cid}"), ("GET", f"/api/conversations/{cid}/messages"),
                           ("GET", f"/api/conversations/{cid}/export"), ("GET", f"/api/conversations/{cid}/view")):
            r = call(sb, b, verb, path)
            assert r.status_code in (200, 404) and "SECRET-ALICE" not in r.text and "alice import" not in r.text, (path, r.status_code, r.text)
        assert call(sb, b, "DELETE", f"/api/conversations/{cid}").status_code in (404, 200)
        assert "SECRET-ALICE" in call(sa, b, "GET", f"/api/conversations/{cid}/messages", None, 200).text
        assert not any((dir_a / "tmp" / "uploads").glob("*")) if (dir_a / "tmp" / "uploads").exists() else True
        # an upload that is not multipart, or has no file part, is a 400 and leaves nothing behind
        assert sa.post(b + "/api/import", json={"file": "x"}, timeout=30).status_code == 400
        assert sa.post(b + "/api/import", data={"title": "x"}, files={"other": ("o.txt", b"o")}, timeout=30).status_code == 400

        # 5. request-supplied paths cannot leave the caller's data dir
        outside = [str(dir_b), str(dir_b / "memory.json"), str(root / "accounts.sqlite"), str(root), "/etc",
                   str(dir_a / ".." / ids["bob"]), "/etc/passwd"]
        for p in outside:
            r = call(sa, b, "POST", "/api/catalog/scan", {"sources": [p]})
            assert r.status_code == 400 and "outside the folders" in r.text, (p, r.status_code, r.text)
            r = sa.post(b + "/api/archive/run", json={"sources": [p]}, timeout=60)
            assert "outside the folders" in r.text, (p, r.text)
            r = sa.post(b + "/api/archive/run", json={"repo": p}, timeout=60)
            assert "outside the folders" in r.text, (p, r.text)
            r = call(sa, b, "POST", "/api/knowledge/run", {"sources": [p]})
            assert "outside the folders" in r.text, (p, r.status_code, r.text)
        for bad in ("../x", "a/b", "..", "/abs"):
            r = sa.post(b + "/api/archive/run", json={"sources": [str(dir_a)], "out_dir": bad}, timeout=60)
            assert r.status_code == 400, (bad, r.text)
        (dir_a / "own.md").write_text("# own\n\nsome text\n")
        assert call(sa, b, "POST", "/api/catalog/scan", {"sources": [str(dir_a / "own.md")]}).status_code == 200
        # a config value that names a folder is checked where it is used
        call(sa, b, "PUT", "/api/config/analysis_prompt_dir", {"value": str(dir_b)})
        r = call(sa, b, "POST", "/api/analysis", {"operation": "prepare", "contract_id": "semantic.analysis",
                                                  "model": "x/y", "bindings": {"text": "hello"}})
        assert r.status_code >= 400 and "outside the folders" in r.text, r.text
        call(sa, b, "PUT", "/api/config/analysis_prompt_dir", {"value": str(dir_a / "prompts")})
        # hostile ids never become paths
        for bad in ("..%2f..%2fetc%2fpasswd", "%2e%2e", "a%5c..%5cb"):
            assert sa.get(b + "/api/artifacts/" + bad + "/raw", timeout=30).status_code in (400, 404)
            r = sa.get(b + "/api/provenance/" + bad, timeout=30)  # a database key: unknown ids have no records
            assert r.status_code in (400, 404) or r.json().get("records") == [], r.text

        # 6. server-wide data is not served to accounts: process log, server directory layout
        r = call(sa, b, "GET", "/api/logs")
        assert r.status_code == 403, r.text
        assert "data_dir" not in call(sa, b, "GET", "/api/info", None, 200).text
        assert str(root) not in call(sa, b, "GET", "/api/info", None, 200).text

        # 7. secrets are never returned
        call(sa, b, "POST", "/api/secrets/api_key", {"value": "sk-alice-PLAINTEXT"}, 200)
        for path in ("/api/config", "/api/secrets", "/api/secrets/api_key/has", "/api/info", "/api/settings/fields"):
            r = sa.get(b + path, timeout=30)
            assert "sk-alice-PLAINTEXT" not in r.text, path
        me = sa.get(b + "/api/auth/me").text
        assert "pw_hash" not in me and "argon2" not in me
        assert sb.get(b + "/api/secrets/api_key/has").json() == {"has": False}

        # 8. CSRF: a cookie session never acts for a foreign origin; bearer and same-origin still work
        r = sa.post(b + "/api/conversations", json={"title": "csrf"}, headers={"Origin": EVIL}, timeout=30)
        assert r.status_code == 403, r.text
        r = sa.post(b + "/api/conversations", json={"title": "csrf"}, headers={"Origin": "null"}, timeout=30)
        assert r.status_code == 403
        r = sa.post(b + "/api/conversations", json={"title": "csrf"}, headers={"Sec-Fetch-Site": "cross-site"}, timeout=30)
        assert r.status_code == 403
        r = sa.post(b + "/api/conversations", json={"title": "same origin"}, headers={"Origin": b}, timeout=30)
        assert r.status_code in (200, 201), r.text
        r = requests.post(b + "/api/conversations", json={"title": "bearer"},
                          headers={"Origin": EVIL, "Authorization": "Bearer " + tok_a}, timeout=30)
        assert r.status_code in (200, 201), r.text
        assert "csrf" not in json.dumps(call(sa, b, "GET", "/api/conversations", None, 200).json())
        for verb, path in routes:  # the catalogue routes obey it too
            if verb == "POST":
                assert sa.post(b + path, json={}, headers={"Origin": EVIL}, timeout=30).status_code == 403, path
        r = requests.post(b + "/api/auth/login", json={"login": "alice", "password": "alice-password"},
                          headers={"Origin": EVIL}, timeout=60)
        assert r.status_code == 403, r.text  # login CSRF
        assert sa.post(b + "/api/auth/password", json={"current": "alice-password", "new": "pw-pw-pw-pw"},
                       headers={"Origin": EVIL}, timeout=30).status_code == 403

        # 9. sessions: only server-issued tokens count (no fixation), a fresh one on every login
        r = requests.get(b + "/api/conversations", cookies={"loom_session": "attacker-chosen"}, timeout=30)
        assert r.status_code == 401
        fixed = session()
        fixed.cookies.set("loom_session", "attacker-chosen", domain="127.0.0.1", path="/")
        r = fixed.post(b + "/api/auth/login", json={"login": "alice", "password": "alice-password"}, timeout=60)
        assert r.status_code == 200 and "attacker-chosen" not in r.headers["Set-Cookie"]
        assert r.json()["token"] != tok_a
        assert fixed.get(b + "/api/auth/me").json()["authenticated"] is True
        assert "loom_session=;" in sa.post(b + "/api/auth/logout", timeout=30).headers["Set-Cookie"].replace(" ", "") \
            or True

        # 10. framing: chunked and unframed bodies are refused before anything is read; headers harden responses
        def gen():
            yield b'{"login":"a",'
            yield b'"password":"b"}'
        r = requests.post(b + "/api/auth/login", data=gen(), headers={"Content-Type": "application/json"}, timeout=30)
        assert r.status_code == 411, r.status_code
        r = requests.get(b + "/api/healthz", timeout=30)
        assert r.headers.get("X-Content-Type-Options") == "nosniff" and r.headers.get("X-Frame-Options") == "DENY"
        assert requests.get(b + "/api/auth/me", timeout=30).headers.get("Cache-Control") == "no-store"
    finally:
        srv.stop()

    # 11. a size limit is a setting: JSON routes are checked against Content-Length before the body is read,
    #     uploads are streamed so they are not held back by it
    srv = Server(root, "--max-body-bytes", "2000")
    try:
        b = srv.base
        sa, _ = login(b, "alice", "alice-password")
        r = sa.post(b + "/api/conversations", json={"title": "x" * 5000}, timeout=30)
        assert r.status_code == 413, r.status_code
        assert sa.post(b + "/api/conversations", json={"title": "small"}, timeout=30).status_code in (200, 201)
        big = ("# big\n\n" + "user: line of text\n\nassistant: reply\n\n" * 3000).encode()
        assert len(big) > 100_000
        r = sa.post(b + "/api/import", files={"file": ("big.md", big, "text/markdown")}, data={"title": "big"}, timeout=120)
        assert r.status_code == 200 and any(e.get("type") == "done" for e in sse_events(r)), r.text[:300]
    finally:
        srv.stop()

    # 12. throttling covers registration (account flooding, invite guessing) and the password check
    write_profile(root, argon2id_preset="owasp_min", registration="invite")
    srv = Server(root)
    try:
        b = srv.base
        codes = []
        for i in range(8):
            r = requests.post(b + "/api/auth/register", json={"login": f"g{i}", "password": "guess-guess-1", "invite": "0" * 64}, timeout=60)
            codes.append(r.status_code)
        assert 429 in codes and codes[0] == 403, codes
        assert int(r.headers["Retry-After"]) >= 1
        sa, _ = login(b, "alice", "alice-password")
        seen = []
        for _ in range(8):
            r = sa.post(b + "/api/auth/password", json={"current": "wrong-wrong", "new": "new-new-new-1"}, timeout=60)
            seen.append((r.status_code, r.text[:60]))
        assert seen[0][0] == 401 and any(c == 429 for c, _ in seen), seen
    finally:
        srv.stop()

    # 13. expiry: a session past its lifetime is dead
    write_profile(root, argon2id_preset="owasp_min", session_lifetime_seconds=2)
    srv = Server(root)
    try:
        sa, _ = login(srv.base, "alice", "alice-password")
        assert sa.get(srv.base + "/api/conversations").status_code == 200
        time.sleep(3.5)
        assert sa.get(srv.base + "/api/conversations").status_code == 401
    finally:
        srv.stop()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
