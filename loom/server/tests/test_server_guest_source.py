#!/usr/bin/env python3
"""A guest without an account never lands on a paid source (R104, R108, R111-R113), end to end on the built server.
Synthetic data on purpose: a fake keys file, a mock model server (web/e2e/mock_llm_server.py) and a fake metadata server
that COUNTS requests - the paid (tier paid_credits, service account) entry must never even be asked for a token.
Scenarios: (A) a free key works -> the guest gets the free provider although the paid one ranks higher and is listed
first in tier_order; (B) no keys file -> explicit state `none` at the start, a plain pl/en message in /api/auth/me,
chat answered with 503 no_guest_source (no model request, no paid source); (C) guests.model_sources without `free` ->
none even with a key; with `paid` listed and `free` present -> still the free one; (D) a key appearing later is picked up
by the next model request. Needs LOOM_SERVER_BIN and python `requests`."""
import http.server
import json
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

try:
    import requests
except ImportError:
    print("SKIP: python 'requests' package not installed")
    sys.exit(0)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_server_guests import CLOCK0, FAKE_KEY, MOCK, Server, free_port, overlay, set_clock, solve_pow  # noqa: E402


class Meta(http.server.BaseHTTPRequestHandler):
    hits = 0

    def log_message(self, *_a):
        pass

    def do_GET(self):
        Meta.hits += 1
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'{"access_token":"fake-meta-token","expires_in":3000}')


TERMS = {"retention": "none_by_default", "human_review": False, "training": "no", "observed_at": "2026-10-30",
         "source_url": "test", "source_rating": "test", "summary": {"pl": "Testowy.", "en": "Test."}}


def scenario(name, tmp, mock_port, meta_base, model_sources, keys_text):
    root = Path(tmp) / name
    root.mkdir()
    keys = Path(tmp) / f"{name}.env"
    if keys_text is not None:
        keys.write_text(keys_text)
    clock = Path(tmp) / f"{name}.clock"
    set_clock(clock, CLOCK0)
    overlay(root, "server_accounts", {
        "registration": "open", "allow_private_hosts": ["127.0.0.1"],
        "guests": {"enabled": True, "start": "auto", "sweep_interval_seconds": 0, "keys_file": str(keys),
                   "protection": {"clock": {"file": str(clock)}}, "model_sources": model_sources,
                   "workdir": {"subdir": "guests", "idle_ttl_seconds": 600, "max_bytes": 0, "keep_after_session": "ask"}}})
    overlay(root, "free_providers", {
        "providers": [
            {"id": "paid_fake", "display_name": "Paid (fake)", "tier": "paid_credits", "enabled": True,
             "base_url": meta_base + "/v1/{project}", "auth_env": "", "auth_kind": "vm_service_account", "auth_secret": "api_key",
             "active_when": "service_account", "auth": {"metadata_base": meta_base, "refresh_before_seconds": 3},
             "data_terms": TERMS, "project": "p"},
            {"id": "free_fake", "display_name": "Free (fake)", "tier": "free", "enabled": True, "base_url": f"http://127.0.0.1:{mock_port}",
             "auth_env": "MOCK_FREE_KEY", "auth_secret": "api_key", "active_when": "key_present", "data_terms": TERMS}],
        "free_model_choice": {"rank_source": "table", "require": {"training": "no"}, "tier_order": ["paid_credits", "free"],
                              "table": [{"model": "paid/best", "provider": "paid_fake", "quality": 0.99},
                                        {"model": "mock/e2e-model", "provider": "free_fake", "quality": 0.1}]}})
    return root, keys


def guest(base):
    g = requests.Session()
    r = g.post(base + "/api/auth/guest", json=solve_pow(base, g), timeout=30)
    assert r.status_code == 201, (r.status_code, r.text)
    conv = g.post(base + "/api/conversations", json={"title": "t"}, timeout=10).json()["id"]
    return g, conv, r.json()["guest"]["id"]


def say(g, base, conv):
    return g.post(base + "/api/chat", json={"conv_id": conv, "message": "hej"}, timeout=60, stream=True)


def main() -> int:
    mock_port = free_port()
    mock = subprocess.Popen([sys.executable, str(MOCK), str(mock_port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mport = free_port()
    meta = http.server.ThreadingHTTPServer(("127.0.0.1", mport), Meta)
    threading.Thread(target=meta.serve_forever, daemon=True).start()
    mb = f"http://127.0.0.1:{mport}"
    try:
        with tempfile.TemporaryDirectory(prefix="loom-guest-source-") as tmp:
            def run(name, sources, keys_text, body):
                root, keys = scenario(name, tmp, mock_port, mb, sources, keys_text)
                srv = Server(root)
                try:
                    body(srv.base, root, keys)
                finally:
                    srv.stop()

            def a(b, root, keys):
                g, conv, gid = guest(b)
                src = g.get(b + "/api/auth/me", timeout=10).json()["guest"]["model_source"]
                assert src["provider"] == "free_fake" and src["tier"] == "free", src
                assert {"provider": "paid_fake", "why": "tier_not_allowed", "tier": "paid_credits"} in src["skipped"], src
                r = say(g, b, conv)
                assert r.status_code == 200 and "mock model" in r.text, (r.status_code, r.text[-300:])
                assert g.get(b + "/api/config", timeout=10).json().get("loom_guest_source") == "free_fake"
            run("A", ["byok", "free", "local"], f"MOCK_FREE_KEY={FAKE_KEY}\n", a)
            print("PASS (a) a free provider works -> the guest gets it, the paid one is skipped as tier_not_allowed")

            def b_(b, root, keys):
                g, conv, gid = guest(b)
                me = g.get(b + "/api/auth/me", timeout=10).json()
                src = me["guest"]["model_source"]
                assert src["provider"] is None and src["state"] == "none", src
                assert "własne konto" in src["message"]["pl"] and "own account" in src["message"]["en"], src
                assert g.get(b + "/api/config", timeout=10).json().get("loom_guest_source") == "none"
                r = say(g, b, conv)
                assert r.status_code == 503, (r.status_code, r.text[:300])
                j = r.json()
                assert j["error"] == "no_guest_source" and j["message"]["pl"] and j["message"]["en"], j
                # D. a key appears later: the next model request picks it up (the state is re-evaluated)
                keys.write_text(f"MOCK_FREE_KEY={FAKE_KEY}\n")
                r = say(g, b, conv)
                assert r.status_code == 200 and "mock model" in r.text, (r.status_code, r.text[:300])
                assert g.get(b + "/api/auth/me", timeout=10).json()["guest"]["model_source"]["provider"] == "free_fake"
            run("B", ["byok", "free", "local"], None, b_)
            print("PASS (c) no free key: explicit none + pl/en message, 503 without a model request; (d) a later key is picked up")

            def c(b, root, keys):
                g, conv, gid = guest(b)
                src = g.get(b + "/api/auth/me", timeout=10).json()["guest"]["model_source"]
                assert src["provider"] is None and src["state"] == "none", src  # free not listed: nothing from the server
                assert say(g, b, conv).status_code == 503
            run("C1", ["byok", "local"], f"MOCK_FREE_KEY={FAKE_KEY}\n", c)

            def c2(b, root, keys):
                g, conv, gid = guest(b)
                src = g.get(b + "/api/auth/me", timeout=10).json()["guest"]["model_source"]
                assert src["provider"] == "free_fake", src  # `paid` (and the old name `pool`) listed: still never a paid source
            run("C2", ["byok", "pool", "paid", "free", "local"], f"MOCK_FREE_KEY={FAKE_KEY}\n", c2)
            run("C3", ["paid"], f"MOCK_FREE_KEY={FAKE_KEY}\n", c)
            print("PASS model_sources is read: without `free` none; `paid`/`pool` listed never serve a guest")
            assert Meta.hits == 0, f"the paid source was asked for a token {Meta.hits} times"
            print("PASS the paid (service-account) entry was never asked for a token: 0 metadata requests")
    finally:
        meta.shutdown()
        mock.terminate()
        mock.wait(timeout=10)
    print("OK guest source")
    return 0


if __name__ == "__main__":
    sys.exit(main())
