#!/usr/bin/env python3
"""Protection of the public guest endpoint (R103 B2). (1) Load from one address at the PRESETS of server_accounts.pack
-> guests.protection: how many guest sessions and model requests pass (printed as MEASURE lines for the card), while a
signed-in account keeps working (every one of its requests succeeds). (2) Every mechanism, with a small overlay value,
answers 429 (or 403/413 where waiting does not help) with retry_after and words; the proof of work is required and
verified (missing, bad nonce, reused challenge refused; an invited start skips it); the rejection counters of
GET /api/pool/summary grow. (3) Every limit at 0 = no limit: nothing is refused and no proof of work is asked.
Needs the built binary (LOOM_SERVER_BIN) and python `requests`; the model provider is a small mock started here."""
import hashlib
import http.server
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

try:
    import requests
except ImportError:
    print("SKIP: python 'requests' package not installed")
    sys.exit(0)

BIN = os.environ.get("LOOM_SERVER_BIN", "loom-server")
FAKE_KEY = "sk-guest-protection-test-not-a-real-key"
MOCK_DELAY = {"s": 0.0}


class Mock(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"data": [{"id": "mock/e2e-model"}]}).encode())

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        self.rfile.read(n)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        for c in [{"choices": [{"delta": {"role": "assistant"}}]},
                  {"choices": [{"delta": {"content": "A short free answer."}}]},
                  {"choices": [{"delta": {}, "finish_reason": "stop"}]}]:
            time.sleep(MOCK_DELAY["s"])
            self.wfile.write(f"data: {json.dumps(c)}\n\n".encode())
            self.wfile.flush()
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


CLOCK0 = 1_000_000.0


def set_clock(root: Path, seconds: float) -> None:
    tmp = root / "clock.tmp"
    tmp.write_text(repr(seconds))
    tmp.replace(root / "clock")  # atomic: the server never reads half a number


def advance(root: Path, seconds: float) -> None:
    set_clock(root, float((root / "clock").read_text()) + seconds)


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def overlay(root: Path, domain: str, values: dict) -> None:
    (root / "profiles").mkdir(parents=True, exist_ok=True)
    (root / "profiles" / f"{domain}.pack").write_text(json.dumps(
        {"schema": "loom.runtime_profile_overlay/1", "domain": domain, "overrides": values}))


def deep(base: dict, extra: dict) -> dict:
    out = json.loads(json.dumps(base))
    for k, v in extra.items():
        out[k] = deep(out.get(k, {}), v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


class Server:
    def __init__(self, root: Path):
        self.port = free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.proc = subprocess.Popen([BIN, "--port", str(self.port), "--accounts-root", str(root)],
                                     stderr=subprocess.PIPE, text=True)
        # A start timeout is not a product assertion: a sanitizer build on a loaded shard needs far longer than a dev build.
        deadline = time.time() + float(os.environ.get("LOOM_TEST_SERVER_START_SECONDS", "120"))
        while time.time() < deadline:
            try:
                if requests.get(self.base + "/api/healthz", timeout=1).status_code == 200:
                    return
            except requests.RequestException:
                time.sleep(0.1)
        raise RuntimeError("server did not start")

    def stop(self):
        self.proc.terminate()
        _, err = self.proc.communicate(timeout=15)
        assert FAKE_KEY not in (err or ""), "the key reached the server log"


def solve(base: str, s) -> dict:
    c = s.get(base + "/api/auth/guest/challenge", timeout=10).json()
    bits = c.get("bits", 0)
    if not bits:
        return {}
    n = 0
    while True:
        if int.from_bytes(hashlib.sha256(f"{c['challenge']}:{n}".encode()).digest(), "big") >> (256 - bits) == 0:
            return {"challenge": c["challenge"], "nonce": str(n)}
        n += 1


def start_guest(base: str, s=None, body=None):
    s = s or requests.Session()
    r = s.post(base + "/api/auth/guest", json=solve(base, s) if body is None else body, timeout=30)
    return s, r


def chat(s, base: str, text: str = "hello"):
    return s.post(base + "/api/chat", json={"message": text}, timeout=60)


def limited(r, mechanism: str, status: int = 429, retry: bool = True) -> dict:
    assert r.status_code == status, (mechanism, r.status_code, r.text[:300])
    j = r.json()
    assert j["limit"]["mechanism"] == mechanism, (mechanism, j)
    assert j["words"]["text"] and "{{" not in j["words"]["text"], j
    assert j["words"]["alternatives"], j
    if retry:
        assert isinstance(j.get("retry_after"), int) and j["retry_after"] >= 1, j
        assert r.headers.get("Retry-After") == str(j["retry_after"]), r.headers
    return j


def main() -> int:
    # Three CTest entries run one part each (server.guest_protection = load, ..._mechanisms, ..._zero), so each ends
    # well inside its limit on an ASan runner; no argument = all. Measured 2026-10-29 on ASan: starting a guest session costs
    # the server ~4.5 s of CPU and every model request ~6 s (dev: ~1 s); proof of work in Python ~0.27 s and an Argon2id
    # registration ~1 s are negligible. So the cost of a part is (guest starts + model requests) x those figures.
    parts = set(sys.argv[1:]) or {"load", "mechanisms", "zero"}
    mock_port = free_port()
    mock = http.server.ThreadingHTTPServer(("127.0.0.1", mock_port), Mock)
    threading.Thread(target=mock.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="loom-guest-protection-") as tmp:
        keys = Path(tmp) / "keys.env"
        keys.write_text(f"MOCK_FREE_KEY={FAKE_KEY}\n")
        accounts = {"registration": "open", "argon2id_preset": "owasp_min", "allow_private_hosts": ["127.0.0.1"],
                    "guests": {"enabled": True, "sweep_interval_seconds": 0, "keys_file": str(keys)}}
        providers = {
            "providers": [{"id": "mock_free", "display_name": "Mock free", "base_url": f"http://127.0.0.1:{mock_port}",
                           "auth_env": "MOCK_FREE_KEY", "auth_secret": "api_key", "tier": "free", "active_when": "key_present", "enabled": True,
                           "data_terms": {"training": "no", "observed_at": "2026-10-29", "source_url": "test"}}],
            "free_model_choice": {"rank_source": "table", "require": {"training": "no"},
                                  "table": [{"model": "mock/e2e-model", "provider": "mock_free", "quality": 0.5}]}}
        case_no = [0]

        def server(protection: dict, workdir: dict | None = None) -> tuple[Server, Path]:
            case_no[0] += 1
            root = Path(tmp) / f"root{case_no[0]}"
            root.mkdir()
            # The guard's clock is frozen (protection.clock.file): refill and challenge age move only when the test
            # rewrites the file, so every count below is exact on a fast laptop and on a slow ASan runner alike.
            set_clock(root, CLOCK0)
            g = {"protection": deep({"clock": {"file": str(root / "clock")}}, protection)}
            if workdir:
                g["workdir"] = workdir
            overlay(root, "server_accounts", deep(accounts, {"guests": g}))
            overlay(root, "free_providers", providers)
            return Server(root), root

        if "load" in parts:
            # ── 1. load from one address at the presets; the signed-in account is unaffected ──────────────────────
            # The limits are the numbers of the preset divided down so the part ends well inside its limit on ASan: sessions
            # stay at the preset (20), the model-request walls are 4 per session per minute and 8 per address per minute
            # instead of 20 / 60 (the same buckets and counters; the preset numbers themselves are asserted from the pack).
            pk = json.loads((Path(__file__).resolve().parents[2] / "data" / "runtime" / "server_accounts.pack").read_text())
            pre = pk["defaults"]["guests"]["protection"]
            assert pre["per_address"]["sessions_per_hour"] == 20 and pre["per_address"]["chat_per_minute"] == 60, pre["per_address"]
            assert pre["per_session"]["chat_per_minute"] == 20 and pre["pow"]["bits"] == 18, pre
            SCM, ACM = 4, 8
            srv, root1 = server({"per_session": {"chat_per_minute": SCM}, "per_address": {"chat_per_minute": ACM}})
            b = srv.base
            try:
                acc = requests.Session()
                assert acc.post(b + "/api/auth/register", json={"login": "marcin", "password": "marcin-password-1"}, timeout=60).status_code in (200, 201)
                account_ok, account_total = [0], [0]
                stop = threading.Event()

                def account_load():  # the account works the whole time the guests hammer the server
                    while not stop.is_set():
                      try:
                        for r in (acc.get(b + "/api/conversations", timeout=30),
                                  acc.post(b + "/api/conversations", json={"title": "account work"}, timeout=30),
                                  acc.get(b + "/api/auth/me", timeout=30)):
                            account_total[0] += 1
                            account_ok[0] += r.status_code in (200, 201)
                        time.sleep(0.25)  # steady work, not a second load: each round adds a conversation the listing reads
                      except requests.RequestException:
                        return
                t = threading.Thread(target=account_load)
                t.start()
                t0 = time.time()
                started, refused, pow_s = [], {}, []
                # Under the frozen clock every count is exact, so one request past each wall proves it (2026-10-29: fewer requests,
                # so a 5x slower ASan server ends well inside the CTest limit; before: 30 sessions, 40 + 120 chats, 25 + 70 at 0).
                for _ in range(21):
                    p0 = time.time()
                    s = requests.Session()
                    body = solve(b, s)
                    pow_s.append(time.time() - p0)
                    _, r = start_guest(b, s, body)
                    if r.status_code == 201:
                        started.append(s)
                    else:
                        m = limited(r, "address_sessions_hour")["limit"]["mechanism"]
                        refused[m] = refused.get(m, 0) + 1
                print(f"MEASURE sessions_from_one_address passed={len(started)} of 21 refused={refused} "
                      f"pow_bits={requests.get(b + '/api/auth/guest/challenge', timeout=10).json()['bits']} "
                      f"pow_python_avg_s={sum(pow_s) / len(pow_s):.2f} elapsed_s={time.time() - t0:.1f}")
                assert len(started) == 20 and refused == {"address_sessions_hour": 1}, (len(started), refused)  # preset per_address.sessions_per_hour
                # one session, sequential model requests, clock frozen: exactly the bucket (preset chat_per_minute 20)
                t1 = time.time()
                rs = [chat(started[0], b) for _ in range(SCM + 1)]
                one = [r.status_code for r in rs]
                print(f"MEASURE chats_one_session passed={one.count(200)} of {SCM + 1} elapsed_s={time.time() - t1:.1f} (clock frozen)")
                assert one == [200] * SCM + [429], one
                refill = 60 // SCM  # SCM/min refills 1 per 15 s
                assert limited(rs[-1], "session_chat_minute")["retry_after"] == refill
                # refill: the clock advances one interval -> exactly one more, two intervals -> exactly two more
                advance(root1, refill)
                assert [chat(started[0], b).status_code for _ in range(2)] == [200, 429]
                advance(root1, 2 * refill)
                assert [chat(started[0], b).status_code for _ in range(3)] == [200, 200, 429]
                advance(root1, 60)  # every per-minute bucket is full again
                # several sessions of the same address: the address bucket (here ACM per minute) is the wall
                t2 = time.time()
                counts = {"passed": 0}
                mech = {}
                lock = threading.Lock()

                def hammer(s):
                    for _ in range(3):  # 4 x 3 = 12: past the address wall (ACM = 8), each session under its own (SCM = 4)
                        r = chat(s, b)
                        with lock:
                            if r.status_code == 200:
                                counts["passed"] += 1
                            else:
                                m = limited(r, r.json()["limit"]["mechanism"])["limit"]["mechanism"]
                                mech[m] = mech.get(m, 0) + 1
                workers = [threading.Thread(target=hammer, args=(s,)) for s in started[1:5]]
                for w in workers:
                    w.start()
                for w in workers:
                    w.join()
                many = counts["passed"]
                print(f"MEASURE chats_six_sessions_parallel_one_address passed={many} of {4 * 3} refused={mech} elapsed_s={time.time() - t2:.1f}")
                assert many == ACM and mech == {"address_chat_minute": 12 - ACM}, (many, mech)
                stop.set()
                t.join()
                print(f"MEASURE account_marcin_requests_during_load ok={account_ok[0]} of {account_total[0]}")
                assert account_total[0] > 0 and account_ok[0] == account_total[0], (account_ok, account_total)
                summary = acc.get(b + "/api/pool/summary", timeout=10)
                assert summary.status_code == 200, summary.text
                sj = summary.json()
                assert sj["rejections"]["address_sessions_hour"] == 1 and sj["rejections"]["session_chat_minute"] == 3, sj
                assert sj["rejections"]["address_chat_minute"] == 12 - ACM and sj["admitted"]["model_requests"] == SCM + 3 + ACM, sj
                assert "hello" not in summary.text and FAKE_KEY not in summary.text
                assert started[0].get(b + "/api/pool/summary", timeout=10).status_code == 403  # not a guest's business
                assert requests.get(b + "/api/pool/summary", timeout=10).status_code == 401
            finally:
                srv.stop()
            print("PASS load at the presets: sessions and model requests bounded, the account unaffected, counters")

            # ── 2. proof of work ─────────────────────────────────────────────────────────────────────────────────
            srv, root = server({"pow": {"bits": 10}})
            b = srv.base
            try:
                limited(start_guest(b, body={})[1], "pow_required", 403, retry=False)
                c = requests.get(b + "/api/auth/guest/challenge", timeout=10).json()
                assert c["bits"] == 10 and c["algorithm"] == "sha256_leading_zero_bits", c
                bad = next(str(n) for n in range(10 ** 6)
                           if int.from_bytes(hashlib.sha256(f"{c['challenge']}:{n}".encode()).digest(), "big") >> 246 != 0)
                limited(start_guest(b, body={"challenge": c["challenge"], "nonce": bad})[1], "pow_invalid", 403, retry=False)
                forged = c["challenge"].rsplit(".", 1)[0].replace(".10.", ".1.") + "." + c["challenge"].rsplit(".", 1)[1]
                limited(start_guest(b, body={"challenge": forged, "nonce": "0"})[1], "pow_invalid", 403, retry=False)
                good = solve(b, requests)
                assert start_guest(b, body=good)[1].status_code == 201
                limited(start_guest(b, body=good)[1], "pow_invalid", 403, retry=False)  # single use
                late = solve(b, requests)
                advance(root, 301)  # preset challenge_ttl_seconds 300: a challenge older than that is refused
                limited(start_guest(b, body=late)[1], "pow_invalid", 403, retry=False)
                assert start_guest(b)[1].status_code == 201  # a fresh one, at the new time, passes
                pl = requests.post(b + "/api/auth/guest", json={}, headers={"Accept-Language": "pl-PL,pl;q=0.9"}, timeout=10).json()
                assert pl["words"]["language"] == "pl" and "przeglądar" in pl["words"]["text"], pl
            finally:
                srv.stop()
            code = subprocess.run([BIN, "user", "invite", "--accounts-root", str(root)], capture_output=True, text=True, timeout=60).stdout.strip()
            srv = Server(root)
            try:
                assert code and start_guest(srv.base, body={"invite": code})[1].status_code == 201  # invited: no proof of work
                limited(start_guest(srv.base, body={"invite": "not-a-code"})[1], "pow_required", 403, retry=False)
            finally:
                srv.stop()
            print("PASS proof of work: required, bad/forged/reused refused, invitation skips it, words in the browser's language")

        if "mechanisms" in parts:
            # ── 3. every other mechanism ─────────────────────────────────────────────────────────────────────────
            def case(protection, mechanism, run, status=429, retry=True, workdir=None):
                srv, _ = server(deep({"pow": {"bits": 0}}, protection), workdir)
                try:
                    j = limited(run(srv.base), mechanism, status, retry)
                    print(f"PASS {mechanism}: {status}" + (f" retry_after={j['retry_after']} s" if retry else ""))
                finally:
                    srv.stop()

            def nth_chat(n, sessions=1):
                def run(base):
                    ss = [start_guest(base)[0] for _ in range(sessions)]
                    r = None
                    for i in range(n):
                        r = chat(ss[i % sessions], base)
                        if i < n - 1:
                            assert r.status_code == 200, (i, r.status_code, r.text[:200])
                    return r
                return run

            case({"per_session": {"chat_per_minute": 2}}, "session_chat_minute", nth_chat(3))
            case({"per_session": {"chat_per_day": 2}}, "session_chat_day", nth_chat(3))
            case({"per_session": {"output_tokens_per_day": 1}}, "session_output_tokens_day", nth_chat(2))
            case({"per_session": {"api_per_minute": 3}}, "session_api_minute",
                 lambda base: [s.get(base + "/api/conversations", timeout=10) for s in [start_guest(base)[0]] for _ in range(4)][-1])
            case({"per_address": {"chat_per_minute": 2}}, "address_chat_minute", nth_chat(3, sessions=2))
            case({"global": {"pool_chat_per_day": 2}}, "pool_chat_day", nth_chat(3, sessions=3))
            case({"global": {"pool_output_tokens_per_day": 1}}, "pool_output_tokens_day", nth_chat(2, sessions=2))
            case({"per_address": {"sessions_per_hour": 2}}, "address_sessions_hour",
                 lambda base: [start_guest(base)[1] for _ in range(3)][-1])
            case({"global": {"live_sessions": 2}}, "global_live_sessions", lambda base: [start_guest(base)[1] for _ in range(3)][-1])
            case({"sizes": {"max_body_bytes": 64}}, "size_body",
                 lambda base: start_guest(base)[0].post(base + "/api/chat", json={"message": "x" * 200}, timeout=10), 413, False)
            case({}, "workdir_full", lambda base: chat(start_guest(base)[0], base), 413, False,
                 workdir={"max_bytes": 1})

            def parallel(base):
                MOCK_DELAY["s"] = 0.7
                try:
                    s = start_guest(base)[0]
                    first = threading.Thread(target=lambda: chat(s, base))
                    first.start()
                    time.sleep(0.4)
                    r = chat(s, base)
                    first.join()
                    return r
                finally:
                    MOCK_DELAY["s"] = 0.0
            case({"per_session": {"parallel_streams": 1}}, "session_parallel_streams", parallel)

        if "zero" in parts:
            # ── 4. a preset at 0 = no limit ──────────────────────────────────────────────────────────────────────
            zero = {"pow": {"bits": 0}, "per_session": {"chat_per_minute": 0, "chat_per_day": 0, "output_tokens_per_day": 0,
                                                         "parallel_streams": 0, "api_per_minute": 0},
                    "per_address": {"sessions_per_hour": 0, "chat_per_minute": 0},
                    "global": {"live_sessions": 0, "pool_chat_per_day": 0, "pool_output_tokens_per_day": 0},
                    "sizes": {"max_body_bytes": 0, "max_upload_bytes": 0}}
            srv, _ = server(zero, {"max_bytes": 0})
            try:
                ss = []
                for _ in range(21):  # one past the preset per_address.sessions_per_hour (20); sequential: the server creates sessions one at a time
                    s, r = start_guest(srv.base, body={})
                    assert r.status_code == 201, r.text
                    ss.append(s)
                # 3 sessions x 21 in parallel from one address: past the address preset (60) and each past the session preset (20);
                # parallel because one session's growing conversation made 61 sequential answers the slowest part (93 s on dev)
                codes, lock0 = [], threading.Lock()

                def burst(s):
                    got = [chat(s, srv.base).status_code for _ in range(21)]
                    with lock0:
                        codes.extend(got)
                ws = [threading.Thread(target=burst, args=(s,)) for s in ss[:3]]
                for w in ws:
                    w.start()
                for w in ws:
                    w.join()
                assert len(codes) == 63 and codes.count(200) == 63, codes
                assert requests.get(srv.base + "/api/auth/guest/challenge", timeout=10).json() == {"bits": 0}
            finally:
                srv.stop()
            print("PASS every limit at 0: 21 sessions without proof of work and 63 model requests from one address (21 per session), none refused")
    mock.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
