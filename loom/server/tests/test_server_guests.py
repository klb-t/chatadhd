#!/usr/bin/env python3
"""Guests of loom-server (R103 B1): use without an account. Preset off (POST /api/auth/guest = 404); with the
test overlay on: a guest session gets its own working directory and the best free model of the free_providers data
(a mock provider, its key from a keys file handed over in memory only); isolation (R103 §3.2 / R18): two guests and
one account on one server, every attempt at another's conversation, export, message, file path or secret ends in
4xx, never content; the key never appears in an answer, a guest directory or secrets.json; after idle_ttl the
session no longer works and the directory is gone. Needs the built binary (LOOM_SERVER_BIN), python `requests`
and the mock model server of the web e2e."""
import hashlib
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
HERE = Path(__file__).resolve().parent
MOCK = HERE.parent.parent / "web" / "e2e" / "mock_llm_server.py"
FAKE_KEY = "sk-guest-test-not-a-real-key-0123456789"  # a test value for the mock; no real key is used anywhere
# Idle time after which a guest is removed. The guests are created only after the account's registration (Argon2id
# can take seconds on a loaded machine) and every guest is used again well within this window; only the TTL phase
# leaves them idle. Measured cause of a gate failure with 4 s: the registration ran while guest A was idle, A was
# (correctly) swept, and the next listing answered 401.
# Since 2026-10-29 the guest sessions run on the injected test clock (guests.protection.clock.file): it is frozen while the test
# works, so no session can expire behind a slow server (CI asan: a 401 after 56 s with the real clock), and the TTL
# phase advances it explicitly instead of sleeping.
TTL = 12
CLOCK0 = 1_000_000


def set_clock(path: Path, seconds: float) -> None:
    tmp = path.with_suffix(".tmp")
    tmp.write_text(f"{seconds}\n")
    tmp.replace(path)


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


def solve_pow(base: str, s=requests) -> dict:
    """R103 B2: the proof of work a guest session needs (SHA-256(challenge:nonce) with `bits` leading zero bits)."""
    c = s.get(base + "/api/auth/guest/challenge", timeout=10).json()
    bits = c.get("bits", 0)
    if not bits:
        return {}
    n = 0
    while True:
        d = hashlib.sha256(f"{c['challenge']}:{n}".encode()).digest()
        if int.from_bytes(d, "big") >> (256 - bits) == 0:
            return {"challenge": c["challenge"], "nonce": str(n)}
        n += 1


def chat(s: requests.Session, base: str, conv_id: str, text: str) -> str:
    r = s.post(base + "/api/chat", json={"conv_id": conv_id, "message": text}, timeout=60, stream=True)
    assert r.status_code == 200, (r.status_code, r.text)
    return r.text


def main() -> int:
    mock_port = free_port()
    mock = subprocess.Popen([sys.executable, str(MOCK), str(mock_port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        with tempfile.TemporaryDirectory(prefix="loom-guests-") as tmp:
            root = Path(tmp) / "root"
            root.mkdir()
            # 0. preset: guests off - no guest route, no guest directory
            srv = Server(root)
            try:
                r = requests.post(srv.base + "/api/auth/guest", json={}, timeout=10)
                assert r.status_code in (401, 404), (r.status_code, r.text)
                assert "guests" not in requests.get(srv.base + "/api/auth/me", timeout=10).json()
            finally:
                srv.stop()
            assert not (root / "guests").exists()
            print("PASS preset: guests off")

            clock = Path(tmp) / "clock"
            set_clock(clock, CLOCK0)
            keys = Path(tmp) / "keys.env"
            keys.write_text(f"# test keys file\nexport MOCK_FREE_KEY=\"{FAKE_KEY}\"\nOTHER=1\n")
            overlay(root, "server_accounts", {
                "registration": "open", "allow_private_hosts": ["127.0.0.1"],
                "guests": {"enabled": True, "start": "auto", "sweep_interval_seconds": 0, "keys_file": str(keys),
                           "protection": {"clock": {"file": str(clock)}},
                           "first_answer_notice": {"on": True, "ask_again": "per_device"},
                           "workdir": {"subdir": "guests", "idle_ttl_seconds": TTL, "max_bytes": 0, "keep_after_session": "ask"},
                           "secrets_routes": "write_only_session", "model_sources": ["byok", "free", "local"]}})
            overlay(root, "free_providers", {
                "providers": [{"id": "mock_free", "display_name": "Mock free", "base_url": f"http://127.0.0.1:{mock_port}",
                               "auth_env": "MOCK_FREE_KEY", "auth_secret": "api_key", "tier": "free", "active_when": "key_present", "enabled": True,
                               "data_terms": {"retention": "none_by_default", "human_review": False, "training": "no",
                                              "summary": {"pl": "Testowy dostawca nic nie zapisuje.", "en": "The test provider keeps nothing."},
                                              "observed_at": "2026-10-28", "source_url": "test", "source_rating": "test"}},
                              {"id": "learns", "base_url": "http://127.0.0.1:9", "auth_env": "", "tier": "free", "active_when": "none", "enabled": True,
                               "data_terms": {"training": "yes"}}],
                "free_model_choice": {"rank_source": "table", "require": {"training": "no"},
                                      "table": [{"model": "learns/x", "provider": "learns", "quality": 0.99},
                                                {"model": "mock/e2e-model", "provider": "mock_free", "quality": 0.5}]}})
            srv = Server(root)
            b = srv.base
            try:
                me0 = requests.get(b + "/api/auth/me", timeout=10).json()
                assert me0["guests"]["enabled"] and me0["guests"]["start"] == "auto" and "guest" not in me0, me0
                assert requests.get(b + "/api/conversations", timeout=10).status_code == 401

                acc = requests.Session()
                r = acc.post(b + "/api/auth/register", json={"login": "owner", "password": "owner-password-1"}, timeout=60)
                assert r.status_code in (200, 201), (r.status_code, r.text)

                ga, gb = requests.Session(), requests.Session()
                ra = ga.post(b + "/api/auth/guest", json=solve_pow(b, ga), timeout=30)
                assert ra.status_code == 201, (ra.status_code, ra.text)
                assert FAKE_KEY not in ra.text
                id_a = ra.json()["guest"]["id"]
                assert gb.post(b + "/api/auth/guest", json=solve_pow(b, gb), timeout=30).status_code == 201
                id_b = gb.get(b + "/api/auth/me", timeout=10).json()["guest"]["id"]
                assert id_a != id_b and id_a.startswith("g_")
                again = ga.post(b + "/api/auth/guest", json=solve_pow(b, ga), timeout=30)  # the same guest gets itself back
                assert again.status_code == 200 and again.json()["guest"]["id"] == id_a
                me_a = ga.get(b + "/api/auth/me", timeout=10)
                src = me_a.json()["guest"]["model_source"]
                # the data rule: the higher-ranked provider trains on data -> skipped by require.training = no
                assert src["provider"] == "mock_free" and src["model"] == "mock/e2e-model", src
                assert {"provider": "learns", "why": "data_terms_require"} in src["skipped"], src
                assert FAKE_KEY not in me_a.text
                dir_a, dir_b = root / "guests" / id_a, root / "guests" / id_b
                assert dir_a.is_dir() and dir_b.is_dir()
                print("PASS guest sessions, best free model by data")

                # the guest talks to the free model; the key went to the model server, never to disk or an answer
                conv_a = ga.post(b + "/api/conversations", json={"title": "Rozmowa gościa A"}, timeout=10).json()["id"]
                stream = chat(ga, b, conv_a, "Ala ma kota, Warszawa jest w Polsce.")
                assert "mock model" in stream, stream[-500:]
                auth = requests.get(f"http://127.0.0.1:{mock_port}/__auth", timeout=5).json()["sha256"]
                assert auth == hashlib.sha256(f"Bearer {FAKE_KEY}".encode()).hexdigest(), "the overlay key did not reach the provider"
                for f in dir_a.rglob("*"):
                    if f.is_file() and f.stat().st_size < 50_000_000:
                        assert FAKE_KEY.encode() not in f.read_bytes(), f"key written to {f}"
                assert FAKE_KEY not in ga.get(b + "/api/secrets", timeout=10).text
                assert "api_key" not in ga.get(b + "/api/secrets", timeout=10).json(), "a pool key is listed"
                cfg = ga.get(b + "/api/config", timeout=10).json()
                assert cfg.get("loom_guest_source") == "mock_free" and cfg["loom_provider_terms"]["training"] == "no"
                msgs = ga.get(b + f"/api/conversations/{conv_a}/messages", timeout=10)
                assert msgs.status_code == 200 and "Ala ma kota" in msgs.text
                # R103 §4.3: the provider's data terms travel in the receipt of what was sent
                assert "provider_terms" in msgs.text and "The test provider keeps nothing." in msgs.text, msgs.text[:2000]
                print("PASS guest chat through the free model; key in memory only")

                # 3. an account on the same server (registered before the guests: see TTL)
                conv_o = acc.post(b + "/api/conversations", json={"title": "Konto właściciela"}, timeout=10).json()["id"]
                user_dir = next((root / "users").iterdir())
                me_acc = acc.get(b + "/api/auth/me", timeout=10).json()
                assert me_acc["authenticated"] and "guest" not in me_acc

                # 4. isolation: nobody sees another's conversation, export, messages, files or keys
                for who, other_conv in ((gb, conv_a), (acc, conv_a), (ga, conv_o), (gb, conv_o)):
                    for path in (f"/api/conversations/{other_conv}", f"/api/conversations/{other_conv}/messages",
                                 f"/api/conversations/{other_conv}/export?format=json"):
                        r = who.get(b + path, timeout=10)
                        assert r.status_code >= 400 or ("Ala ma kota" not in r.text and "Konto" not in r.text), (path, r.status_code, r.text[:300])
                    listing = who.get(b + "/api/conversations", timeout=10)
                    assert listing.status_code == 200 and isinstance(listing.json(), list), (listing.status_code, listing.text[:300])
                    titles = [c.get("title") for c in listing.json()]
                    assert "Rozmowa gościa A" not in titles or who is ga
                    assert "Konto właściciela" not in titles or who is acc
                for p in (str(dir_a), str(dir_a / "chatadhd.db"), str(user_dir), str(root / "accounts.sqlite"), str(root),
                          str(dir_b / ".." / id_a), "/etc/passwd"):
                    r = gb.post(b + "/api/catalog/scan", json={"sources": [p]}, timeout=30)
                    assert r.status_code == 400 and "outside the folders" in r.text, (p, r.status_code, r.text[:300])
                assert gb.get(b + "/api/logs", timeout=10).status_code == 403
                assert gb.post(b + "/api/auth/password", json={"current": "x", "new": "y"}, timeout=10).status_code == 401
                assert gb.get(b + "/api/admin/anything", timeout=10).status_code == 403
                # a cross-site request with the guest's cookie is refused like an account's
                assert gb.post(b + "/api/conversations", json={"title": "x"}, headers={"Origin": "https://evil.example"}, timeout=10).status_code == 403
                print("PASS isolation: two guests and an account")

                # 5. after idle_ttl: the session no longer works and the directory is gone; the account stays
                assert ga.get(b + "/api/auth/me", timeout=10).json()["guest"]["id"] == id_a  # alive while used
                # one second short of the TTL nothing is swept (the clock is frozen otherwise, so this holds on any machine)
                set_clock(clock, CLOCK0 + TTL - 1)
                acc.get(b + "/api/conversations", timeout=10)
                assert dir_a.exists() and dir_b.exists()
                set_clock(clock, CLOCK0 + 2 * TTL + 1)  # idle longer than the TTL counted from the last request
                acc.get(b + "/api/conversations", timeout=10)  # any request sweeps (sweep_interval_seconds 0)
                assert ga.get(b + "/api/conversations", timeout=10).status_code == 401
                assert "guest" not in ga.get(b + "/api/auth/me", timeout=10).json()
                assert not dir_a.exists() and not dir_b.exists(), list((root / "guests").iterdir())
                assert user_dir.is_dir()
                titles = [c.get("title") for c in acc.get(b + "/api/conversations", timeout=10).json()]
                assert "Konto właściciela" in titles
                # logout of a guest ends it at once (its directory goes with the next sweep)
                gc = requests.Session()
                id_c = gc.post(b + "/api/auth/guest", json=solve_pow(b, gc), timeout=30).json()["guest"]["id"]
                assert gc.post(b + "/api/auth/logout", json={}, timeout=10).status_code == 200
                acc.get(b + "/api/healthz", timeout=10)
                assert not (root / "guests" / id_c).exists()
                print("PASS ttl sweep and guest logout")
            finally:
                srv.stop()
    finally:
        mock.terminate()
        mock.wait(timeout=10)
    print("OK guests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
