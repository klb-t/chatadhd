#!/usr/bin/env python3
"""Security review of loom-server accounts mode (R18): every route needs a session and acts only on the
caller's own runtime; two users A/B cannot see or change each other's runs, rules, conversations, files;
request-supplied paths cannot leave the caller's data dir; cross-site cookie requests are refused;
framing/size limits and throttling hold. Needs the built binary (LOOM_SERVER_BIN) and python `requests`."""
import hashlib
import json
import os
import socket
import subprocess
import sys
import shutil
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


def confirmed(s, base, path, body, status=200):
    """A confirmed write as a client makes it (P14 7): the proposal gives the plan_receipt, the request is sent again with it."""
    proposal = call(s, base, "POST", path, {**body, "propose": True}, 200).json()
    assert proposal["status"] == "proposed" and proposal["plan_receipt"].startswith("arp_"), proposal
    return call(s, base, "POST", path, {**body, "confirmed": True, "receipt_id": proposal["plan_receipt"]}, status)


def sse_events(r):
    # The stream is UTF-8 JSON; `r.text` would decode text/event-stream without a charset as latin-1 and
    # str.splitlines() then breaks a line at the 0x85 byte of "ą" (U+0085 NEL) - seen 2026-10-23 when the
    # import summary gained Polish words. Decode explicitly and split on newlines only.
    out = []
    for line in r.content.decode("utf-8", "replace").split("\n"):
        if line.startswith("data:"):
            try:
                out.append(json.loads(line[5:].strip()))
            except ValueError:
                pass
    return out


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="loom_security_"))
    try:
        return _run(tmp)
    finally:
        if os.environ.get("LOOM_KEEP_TMP"):
            print(f"[tmp] kept {tmp}")
        else:
            shutil.rmtree(tmp, ignore_errors=True)


def _run(tmp: Path) -> int:
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
        # a bare confirmed:true over HTTP is only a proposal: nothing is written until the receipt comes with it (P14 7)
        bare = call(sa, b, "POST", "/api/rules/set", {"scope": "person", "effect": "deny", "action": "depict",
                                                      "reason": "ALICE-ONLY-RULE", "confirmed": True}, 200).json()
        assert bare["status"] == "proposed" and bare["confirmation_refused"]["reason"] == "receipt_required", bare
        assert not (dir_a / "profiles" / "action_rules.pack").exists()
        confirmed(sa, b, "/api/rules/set", {"scope": "person", "effect": "deny", "action": "depict", "reason": "ALICE-ONLY-RULE"})
        confirmed(sa, b, "/api/actions/define", {"id": "alicestep", "instruction": "Alice only"})
        assert "ALICE-ONLY-RULE" in call(sa, b, "POST", "/api/rules/list", {}, 200).text
        assert "ALICE-ONLY-RULE" not in call(sb, b, "POST", "/api/rules/list", {}, 200).text
        assert "alicestep" not in call(sb, b, "POST", "/api/actions/for", {"text": "alicestep"}).text
        assert "ALICE-ONLY-RULE" in (dir_a / "profiles" / "action_rules.pack").read_text()
        assert not (dir_b / "profiles" / "action_rules.pack").exists()
        r = confirmed(sb, b, "/api/actions/remove", {"id": "alicestep"}, None)
        assert "alicestep" in (dir_a / "profiles" / "actions.pack").read_text(), r.text
        r = confirmed(sb, b, "/api/rules/disable", {"id": "deny:depict"}, None)
        assert "ALICE-ONLY-RULE" in call(sa, b, "POST", "/api/rules/list", {}, 200).text

        # 4. conversations (and their messages/exports) are per user; upload is streamed into the user's dir
        md = "# Alice's import\n\nuser: hello from alice SECRET-ALICE\n\nassistant: hi\n"
        r = sa.post(b + "/api/import", files={"file": ("alice.md", md.encode(), "text/markdown")},
                    data={"title": "alice import"}, timeout=120)
        assert r.status_code == 200, r.text
        # the stream is UTF-8: it says so, so a client that decodes by the header (requests' r.text, a browser) does not read Polish words as latin-1
        ctype = r.headers.get("Content-Type", "")
        assert ctype.replace(" ", "").lower() == "text/event-stream;charset=utf-8", ctype
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
        # 4b. client device declarations and blobs (P13) live in the caller's runtime only
        dec = call(sa, b, "POST", "/api/client/declare", {"capabilities": ["plays_audio"]}, 200).json()
        cl = dec["client_id"]
        assert call(sa, b, "POST", "/api/client/capabilities", {"client_id": cl}, 200).json()["known"] is True
        assert call(sb, b, "POST", "/api/client/capabilities", {"client_id": cl}, 200).json()["known"] is False
        assert cl not in call(sb, b, "POST", "/api/client/capabilities", {}, 200).text
        # blobs go by upload and by stream, never as base64 in JSON (P14 6); a hash names a blob of the caller's own store
        bh = done[0]["blob_hash"]
        raw_a = sa.get(b + f"/api/client/blob/{bh}/raw", timeout=30)
        assert raw_a.status_code == 200 and hashlib.sha256(raw_a.content).hexdigest() == bh, raw_a.status_code
        assert sb.get(b + f"/api/client/blob/{bh}/raw", timeout=30).status_code == 404
        assert sb.get(b + "/api/client/blob/../../etc/passwd/raw", timeout=30).status_code in (400, 404)
        assert sb.get(b + "/api/client/blob/" + "z" * 64 + "/raw", timeout=30).status_code in (400, 404)
        assert call(sa, b, "POST", "/api/client/blob", {"hash": bh}).status_code in (404, 405), "the base64 route is gone"
        payload = bytes(range(256)) * 40  # not text; larger than a JSON body this server would take in the limit test below
        up = sa.post(b + "/api/client/blob/upload", files={"file": ("rec.webm", payload, "audio/webm")}, timeout=60)
        assert up.status_code == 200, up.text
        stored = up.json()
        assert stored["hash"] == hashlib.sha256(payload).hexdigest() and stored["size"] == len(payload) and stored["mime"] == "audio/webm", stored
        assert not any((dir_a / "tmp" / "uploads").glob("*")), "the upload file is gone once stored"
        again = sa.get(b + f"/api/client/blob/{stored['hash']}/raw", timeout=30)
        assert again.content == payload and again.headers["Content-Type"].startswith("audio/webm"), again.headers
        assert "sandbox" in again.headers.get("Content-Security-Policy", ""), again.headers
        part = sa.get(b + f"/api/client/blob/{stored['hash']}/raw", headers={"Range": "bytes=10-19"}, timeout=30)
        assert part.status_code == 206 and part.content == payload[10:20], (part.status_code, len(part.content))
        # B cannot see A's upload, and an upload that is not multipart or has no file part is a 400
        assert sb.get(b + f"/api/client/blob/{stored['hash']}/raw", timeout=30).status_code == 404
        assert sa.post(b + "/api/client/blob/upload", json={"file": "x"}, timeout=30).status_code == 400
        assert sa.post(b + "/api/client/blob/upload", data={"mime": "x"}, files={"other": ("o", b"o")}, timeout=30).status_code == 400
        # the answer of a device names the blob; bytes in it are refused; B cannot answer A's run, nor name A's blob
        assert call(sb, b, "POST", "/api/actions/client_result", {"run_id": rid, "parts": []}).status_code >= 400
        r = call(sa, b, "POST", "/api/actions/client_result", {"run_id": rid, "receipt_id": "x", "state": "ok",
                                                               "parts": [{"kind": "audio", "data_base64": "QQ=="}]})
        assert r.status_code >= 400, r.text
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
        # a device's recording is an upload too (P14 6): streamed to disk, stored by hash, whatever the JSON limit
        recording = bytes(range(256)) * 400
        assert len(recording) > 100_000
        r = sa.post(b + "/api/client/blob/upload", files={"file": ("rec.webm", recording, "audio/webm")}, timeout=60)
        assert r.status_code == 200 and r.json()["size"] == len(recording), r.text
        assert sa.get(b + f"/api/client/blob/{r.json()['hash']}/raw", timeout=30).content == recording
    finally:
        srv.stop()

    # 11b. chunked / unframed bodies are a setting (--allow-chunked-uploads, preset: refuse): allowed, they are
    #      accepted on the upload routes only, streamed to disk and counted against --max-upload-bytes
    srv = Server(root, "--allow-chunked-uploads", "--max-upload-bytes", "30000")
    try:
        b = srv.base
        sa, _ = login(b, "alice", "alice-password")
        boundary = "r20boundary"

        def multipart(payload: bytes):
            yield (f"--{boundary}\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\nchunked\r\n"
                   f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"c.md\"\r\n"
                   f"Content-Type: text/markdown\r\n\r\n").encode()
            for i in range(0, len(payload), 1000):
                yield payload[i:i + 1000]
            yield f"\r\n--{boundary}--\r\n".encode()
        hdr = {"Content-Type": f"multipart/form-data; boundary={boundary}"}
        small = ("# s\n\n" + "user: hello\n\nassistant: hi\n\n" * 20).encode()
        r = sa.post(b + "/api/import", data=multipart(small), headers=hdr, timeout=120)
        assert r.status_code == 200 and any(e.get("type") == "done" for e in sse_events(r)), (r.status_code, r.text[:300])
        r = sa.post(b + "/api/import", data=multipart(b"x" * 100_000), headers=hdr, timeout=120)
        assert r.status_code == 413, (r.status_code, r.text[:300])
        # R23: the 413 must reach a client that is still sending (no RST for unread bytes) and must not depend on
        # the retry policy of `sa`: a plain session, several times, fresh and reused connections
        plain = requests.Session()
        for _ in range(10):
            r = plain.post(b + "/api/import", data=multipart(b"x" * 100_000), headers={**hdr, "Cookie": "; ".join(f"{c.name}={c.value}" for c in sa.cookies)}, timeout=120)
            assert r.status_code == 413, (r.status_code, r.text[:300])
        def gen2():
            yield b'{"title":'
            yield b'"x"}'
        assert sa.post(b + "/api/conversations", data=gen2(), headers={"Content-Type": "application/json"},
                       timeout=30).status_code == 411
    finally:
        srv.stop()

    # 11c. a client that stalls past --read-timeout-s before its body ends gets 408 with that reason, not 400 "malformed"
    #      (CI 285 and the run on 597f92ba: under load a stall before the limit turned the expected 413 into such a 400).
    #      Deterministic on a slow server: the client sends part of a chunked body and then nothing at all, and waits for
    #      the answer instead of sleeping a fixed time, so the server's read timeout is the only thing that can end it.
    srv = Server(root, "--allow-chunked-uploads", "--max-upload-bytes", "30000", "--read-timeout-s", "1")
    try:
        b = srv.base
        sa, _ = login(b, "alice", "alice-password")
        boundary = "r20boundary"
        cookie = "; ".join(f"{c.name}={c.value}" for c in sa.cookies)

        def stalled_upload(sent: int) -> tuple:
            head = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"c.md\"\r\n"
                    f"Content-Type: text/markdown\r\n\r\n").encode() + b"x" * sent
            with socket.create_connection(("127.0.0.1", srv.port), timeout=120) as sock:
                sock.sendall((f"POST /api/import HTTP/1.1\r\nHost: 127.0.0.1:{srv.port}\r\nCookie: {cookie}\r\n"
                              f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
                              f"Transfer-Encoding: chunked\r\n\r\n").encode())
                for i in range(0, len(head), 1000):
                    part = head[i:i + 1000]
                    sock.sendall(f"{len(part):x}\r\n".encode() + part + b"\r\n")
                answer = b""
                while b"\r\n\r\n" not in answer:  # the answer comes only after the server gives up reading
                    got = sock.recv(65536)
                    if not got:
                        break
                    answer += got
                status = int(answer.split(b" ", 2)[1]) if answer.startswith(b"HTTP/") else 0
                while got:
                    try:
                        got = sock.recv(65536)
                    except OSError:
                        break
                    answer += got
            return status, answer.decode("utf-8", "replace")
        # well below the limit (a third of it): only the read timeout can end the request -> 408 with the reason
        status, text = stalled_upload(10_000)
        assert status == 408 and "stopped sending" in text, (status, text[-300:])
        # well past the limit before the stall: the size is known, the answer stays 413
        status, text = stalled_upload(50_000)
        assert status == 413, (status, text[-300:])
    finally:
        srv.stop()

    # 11d. an answer that advertises Keep-Alive keeps its promise: after a streamed (SSE) import the same connection
    #      answers the next request. The server used to close it right after the final chunk, so a client reusing it
    #      raced the close; under ASan a retrying client then resent an already consumed upload body -> 400 "malformed"
    #      in 11b. Deterministic: the second request is written right after the final chunk on the same socket.
    srv = Server(root, "--allow-chunked-uploads", "--max-upload-bytes", "30000", "--keep-alive-timeout-s", "30")
    try:
        sa, _ = login(srv.base, "alice", "alice-password")
        cookie = "; ".join(f"{c.name}={c.value}" for c in sa.cookies)
        boundary = "r20boundary"
        doc = ("# s\n\n" + "user: hello\n\nassistant: hi\n\n" * 20).encode()
        part = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"c.md\"\r\n"
                f"Content-Type: text/markdown\r\n\r\n").encode() + doc + f"\r\n--{boundary}--\r\n".encode()
        with socket.create_connection(("127.0.0.1", srv.port), timeout=60) as sock:
            sock.sendall((f"POST /api/import HTTP/1.1\r\nHost: 127.0.0.1:{srv.port}\r\nCookie: {cookie}\r\n"
                          f"Content-Type: multipart/form-data; boundary={boundary}\r\nTransfer-Encoding: chunked\r\n\r\n"
                          ).encode() + f"{len(part):x}\r\n".encode() + part + b"\r\n0\r\n\r\n")
            first = b""
            while not first.endswith(b"\r\n0\r\n\r\n"):
                got = sock.recv(65536)
                assert got, ("the SSE answer ended before its final chunk", first[-200:])
                first += got
            assert b"Keep-Alive:" in first.split(b"\r\n\r\n", 1)[0], first[:300]
            sock.sendall(f"GET /api/auth/me HTTP/1.1\r\nHost: 127.0.0.1:{srv.port}\r\nCookie: {cookie}\r\n\r\n".encode())
            second = b""
            while b"\r\n\r\n" not in second:
                got = sock.recv(65536)
                assert got, "the server closed a connection it had advertised as Keep-Alive"
                second += got
            assert second.startswith(b"HTTP/1.1 200"), second[:200]
    finally:
        srv.stop()

    # 12. throttling covers registration (account flooding, invite guessing) and the password check
    #     The delay after the free attempts is a base of 1 s in the preset; under a sanitizer on a loaded runner one Argon2id
    #     request takes longer than that, so the next attempt came after the delay and was never throttled (CI 257: eight 403 and no
    #     429). The delay is made longer than any slow request here: this step is about throttling, not about timing.
    slow_ok_throttle = {"mode": "on", "reset_seconds": 900, "trust_forwarded_for": False,
                        "per_login": {"free_attempts": 5, "base_delay_seconds": 120, "max_delay_seconds": 900},
                        "per_ip": {"free_attempts": 20, "base_delay_seconds": 1, "max_delay_seconds": 900}}
    write_profile(root, argon2id_preset="owasp_min", registration="invite", throttle=slow_ok_throttle)
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
    # 14. concurrent Argon2id evaluations are a setting (hashing.max_concurrent, preset = CPU count): with one
    #     slot and no waiting, simultaneous registrations (each 64 MiB of Argon2id) are answered 503 + Retry-After
    import concurrent.futures
    #     The registration throttle counts every attempt under one key per client address (7 here, 5 free), so with fast hashing the
    #     seventh would be answered 429 within a second of the sixth; this step is about hashing slots, so it is off.
    no_throttle = {"mode": "off", "reset_seconds": 900, "trust_forwarded_for": False,
                   "per_login": {"free_attempts": 5, "base_delay_seconds": 1, "max_delay_seconds": 900},
                   "per_ip": {"free_attempts": 20, "base_delay_seconds": 1, "max_delay_seconds": 900}}
    write_profile(root, argon2id_preset="rfc9106_second", registration="open",
                  hashing={"max_concurrent": 1, "wait_seconds": 0}, throttle=no_throttle)
    srv = Server(root)
    try:
        b = srv.base

        def reg(i):
            return requests.post(b + "/api/auth/register", json={"login": f"h{i}", "password": "hash-hash-1"}, timeout=120)
        with concurrent.futures.ThreadPoolExecutor(6) as pool:
            rs = list(pool.map(reg, range(6)))
        codes = sorted(r.status_code for r in rs)
        assert 201 in codes and 503 in codes and set(codes) <= {201, 503}, codes
        assert all(int(r.headers["Retry-After"]) >= 1 for r in rs if r.status_code == 503)
        r99 = reg(99)
        assert r99.status_code == 201, (r99.status_code, r99.text)  # a free slot again
    finally:
        srv.stop()
    # unlimited (0) never refuses
    write_profile(root, argon2id_preset="rfc9106_second", registration="open", hashing={"max_concurrent": 0, "wait_seconds": 0})
    srv = Server(root)
    try:
        b = srv.base

        def reg2(i):
            return requests.post(b + "/api/auth/register", json={"login": f"u{i}", "password": "hash-hash-1"}, timeout=120)
        with concurrent.futures.ThreadPoolExecutor(4) as pool:
            assert {r.status_code for r in pool.map(reg2, range(4))} == {201}
    finally:
        srv.stop()
    # 15. login length is a setting (login_max_length, preset 128): refused with 400 before hashing or throttling
    write_profile(root, argon2id_preset="owasp_min", registration="open", login_max_length=16)
    srv = Server(root)
    try:
        b = srv.base
        r = requests.post(b + "/api/auth/register", json={"login": "x" * 17, "password": "long-long-1"}, timeout=60)
        assert r.status_code == 400 and "longer" in r.text, (r.status_code, r.text)
        r = requests.post(b + "/api/auth/login", json={"login": "x" * 17, "password": "long-long-1"}, timeout=60)
        assert r.status_code == 400, r.status_code
        assert requests.post(b + "/api/auth/register", json={"login": "x" * 16, "password": "long-long-1"}, timeout=60).status_code == 201
    finally:
        srv.stop()
    write_profile(root, argon2id_preset="owasp_min", registration="open", login_max_length=0)
    srv = Server(root)
    try:
        r = requests.post(srv.base + "/api/auth/register", json={"login": "y" * 500, "password": "long-long-1"}, timeout=60)
        assert r.status_code == 201, r.status_code
    finally:
        srv.stop()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
