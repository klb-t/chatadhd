#!/usr/bin/env python3
"""Guests on a service-account source (R103 B3), against a local fake so CI never needs Vertex: the fake plays the
metadata server (short-lived access tokens) and the Vertex OpenAI-compatible endpoint. Checks: the guest gets the
Vertex entry with {project} from the metadata server; the request shape (path, model id, Bearer token); the token is
refreshed before expiry in memory only (a new token reaches the provider) and never appears in an answer, a file of
the accounts root or the server log; spend from usage × the dated price in GET /api/pool/summary; an exhaustion
marker (429 RESOURCE_EXHAUSTED) puts the provider on cool-down and the guest is switched explicitly to the next source
(model_source.switched, §4.4); the cool-down ends; the daily budget (budget.credits.per_day) switches too. Needs the
built binary (LOOM_SERVER_BIN) and python `requests`."""
import http.server
import json
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
from test_server_guests import CLOCK0, Server, free_port, overlay, set_clock, solve_pow  # noqa: E402

TOKEN_PREFIX = "ya29.fake-test-token-"  # test values from the fake; no real token is used anywhere
SECOND_KEY = "sk-second-test-key-0123456789"
PRICE_IN, PRICE_OUT, TO_EUR = 1.5, 9.0, 0.92
USAGE = {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 120,
         "completion_tokens_details": {"reasoning_tokens": 105}}
EUR_PER_ANSWER = (10 * PRICE_IN + 110 * PRICE_OUT) / 1e6 * TO_EUR


class Fake(http.server.BaseHTTPRequestHandler):
    tokens = 0
    mode = "ok"
    calls = []  # (path, authorization, body)

    def log_message(self, *_a):
        pass

    def do_GET(self):
        if self.headers.get("Metadata-Flavor") != "Google":
            self.send_response(403)
            self.end_headers()
            return
        if self.path.endswith("/service-accounts/default/token"):
            Fake.tokens += 1
            body = json.dumps({"access_token": f"{TOKEN_PREFIX}{Fake.tokens}", "expires_in": 6, "token_type": "Bearer"})
        elif self.path.endswith("/project/project-id"):
            body = "fake-project"
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.end_headers()
        self.wfile.write(body.encode())

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        Fake.calls.append((self.path, self.headers.get("Authorization", ""), json.loads(raw or b"{}")))
        if Fake.mode == "429" and "/openapi/" in self.path:
            err = json.dumps([{"error": {"code": 429, "message": "Resource exhausted. Please try again later.",
                                         "status": "RESOURCE_EXHAUSTED"}}]).encode()
            self.send_response(429)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(err)))
            self.end_headers()
            self.wfile.write(err)
            return
        who = "vertex fake" if "/openapi/" in self.path else "second fake"
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        for c in ({"choices": [{"delta": {"role": "assistant"}}]},
                  {"choices": [{"delta": {"content": f"Answer from the {who}."}}]},
                  {"choices": [{"delta": {}, "finish_reason": "stop"}]},
                  {"choices": [], "usage": USAGE}):
            self.wfile.write(f"data: {json.dumps(c)}\n\n".encode())
            self.wfile.flush()
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


def chat_text(s, base, conv, text):
    r = s.post(base + "/api/chat", json={"conv_id": conv, "message": text}, timeout=60, stream=True)
    assert r.status_code == 200, (r.status_code, r.text)
    return r.text


def new_guest(base):
    g = requests.Session()
    r = g.post(base + "/api/auth/guest", json=solve_pow(base, g), timeout=30)
    assert r.status_code == 201, (r.status_code, r.text)
    assert TOKEN_PREFIX not in r.text
    conv = g.post(base + "/api/conversations", json={"title": "t"}, timeout=10).json()["id"]
    return g, conv


def source(g, base):
    r = g.get(base + "/api/auth/me", timeout=10)
    assert TOKEN_PREFIX not in r.text and SECOND_KEY not in r.text
    return r.json()["guest"]["model_source"]


def main() -> int:
    fport = free_port()
    fake = http.server.ThreadingHTTPServer(("127.0.0.1", fport), Fake)
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    fb = f"http://127.0.0.1:{fport}"
    log = ""
    with tempfile.TemporaryDirectory(prefix="loom-guest-vertex-") as tmp:
        root = Path(tmp) / "root"
        root.mkdir()
        keys = Path(tmp) / "keys.env"
        keys.write_text(f"SECOND_KEY={SECOND_KEY}\n")
        # The guests' timing (token expiry, cool-down, sessions) runs on the injected test clock, frozen unless the test
        # moves it: on a slow server (CI asan) the first token can no longer be refreshed behind the test's back.
        clock = Path(tmp) / "clock"
        set_clock(clock, CLOCK0)
        overlay(root, "server_accounts", {
            "registration": "open", "allow_private_hosts": ["127.0.0.1"],
            "guests": {"enabled": True, "start": "auto", "sweep_interval_seconds": 0, "keys_file": str(keys),
                       "protection": {"clock": {"file": str(clock)}},
                       "workdir": {"subdir": "guests", "idle_ttl_seconds": 600, "max_bytes": 0, "keep_after_session": "ask"}}})
        terms = {"retention": "per_vertex_terms", "human_review": "no", "training": "no", "observed_at": "2026-10-29",
                 "source_url": "test", "source_rating": "test", "summary": {"pl": "Testowy Vertex.", "en": "Test Vertex."}}
        overlay(root, "free_providers", {
            "providers": [
                {"id": "vertex_gemini", "display_name": "Vertex (fake)", "tier": "free", "enabled": True,  # TEST ONLY: a service-account source that is cost-free; the real vertex_gemini is tier paid_credits and never reaches a guest (test_server_guest_source.py)
                 "base_url": fb + "/v1/projects/{project}/locations/global/endpoints/openapi",
                 "adapter": "vertex_openai_compatible", "auth_env": "", "auth_kind": "vm_service_account",
                 "auth_secret": "api_key", "active_when": "service_account",
                 "auth": {"metadata_base": fb, "refresh_before_seconds": 3},
                 "pricing": {"currency": "USD", "input_per_million": PRICE_IN, "output_per_million": PRICE_OUT, "to_eur": TO_EUR},
                 "budget": {"credits": {"per_day": EUR_PER_ANSWER * 2.5, "advisory_at_share": 0.8, "amounts_eur": []}},
                 "exhaustion_markers": ["429", "RESOURCE_EXHAUSTED"], "exhausted_cooldown_seconds": 4,
                 "data_terms": terms, "fallback_order": 30},
                {"id": "second", "display_name": "Second (fake)", "tier": "free", "enabled": True, "base_url": fb + "/second",
                 "auth_env": "SECOND_KEY", "auth_secret": "api_key", "active_when": "key_present",
                 "data_terms": dict(terms, retention="none_by_default"), "fallback_order": 10}],
            "free_model_choice": {"rank_source": "table", "require": {"training": "no"}, "tier_order": [],
                                  "table": [{"model": "google/gemini-3.5-flash", "provider": "vertex_gemini", "quality": 0.9},
                                            {"model": "second/model", "provider": "second", "quality": 0.5}]}})
        srv = Server(root)
        b = srv.base
        try:
            acc = requests.Session()
            assert acc.post(b + "/api/auth/register", json={"login": "owner", "password": "owner-password-1"},
                            timeout=60).status_code in (200, 201)
            # 1. the guest gets the Vertex entry, {project} from the metadata server, no token in the answer
            ga, ca = new_guest(b)
            src = source(ga, b)
            assert src["provider"] == "vertex_gemini" and "fake-project" in src["base_url"], src
            assert chat_text(ga, b, ca, "first").count("Answer from the vertex fake") >= 1
            path, auth, body = Fake.calls[-1]
            assert path == "/v1/projects/fake-project/locations/global/endpoints/openapi/chat/completions", path
            assert auth == f"Bearer {TOKEN_PREFIX}1" and body["model"] == "google/gemini-3.5-flash", (auth, body.get("model"))
            print("PASS request shape: path, model id, Bearer token from the metadata server")
            # 2. refresh before expiry (expires_in 6, refresh_before 3): a later request carries a new token
            set_clock(clock, CLOCK0 + 3)  # 6 - 3 = 3 s left = refresh_before: due now
            chat_text(ga, b, ca, "second")
            assert Fake.calls[-1][1].startswith("Bearer " + TOKEN_PREFIX) and Fake.calls[-1][1] != auth, Fake.calls[-1][1]
            print("PASS token refreshed in memory before expiry")
            # R103 B1b: the working-copy bundle never carries the access token (in this runtime's overlay right now)
            rb = ga.get(b + "/api/working-copy/bundle", timeout=10)
            assert rb.status_code == 200 and rb.json()["schema"] == "loom.guest_bundle/2", rb.text[:300]
            assert TOKEN_PREFIX not in rb.text and SECOND_KEY not in rb.text
            assert all(c.value not in rb.text for c in ga.cookies)
            print("PASS the working-copy bundle carries no access token, key or session cookie")
            # 3. spend from usage × price
            summ = acc.get(b + "/api/pool/summary", timeout=10).json()
            v = summ["spend"]["providers"]["vertex_gemini"]
            assert v["requests"] == 2 and v["input_tokens"] == 20 and v["output_tokens"] == 220, v
            assert abs(v["eur"] - 2 * EUR_PER_ANSWER) < 1e-12, (v, EUR_PER_ANSWER)
            assert summ["token_refreshes"] >= 2
            print(f"PASS spend: 2 answers = {v['eur']:.9f} EUR")
            # 4. 429 -> cool-down -> explicit switch to the next source
            Fake.mode = "429"
            chat_text(ga, b, ca, "third")
            Fake.mode = "ok"
            assert "vertex_gemini" in acc.get(b + "/api/pool/summary", timeout=10).json()["exhausted"]
            out = chat_text(ga, b, ca, "fourth")
            assert "Answer from the second fake" in out, out[-400:]
            assert Fake.calls[-1][0] == "/second/chat/completions" and Fake.calls[-1][1] == f"Bearer {SECOND_KEY}"
            src = source(ga, b)
            assert src["provider"] == "second" and src["switched"]["from"] == "vertex_gemini" \
                and src["switched"]["why"] == "provider_exhausted", src
            cfg = ga.get(b + "/api/config", timeout=10).json()
            assert cfg.get("loom_provider_terms", {}).get("provider") == "second", cfg.get("loom_provider_terms")
            print("PASS 429 -> provider on cool-down, guest switched explicitly (model_source.switched, receipt terms)")
            # 5. the cool-down ends: a new guest gets Vertex again; the daily budget then switches it (budget_day)
            # still cooling down however long the requests above took: the clock has not moved
            assert "vertex_gemini" in acc.get(b + "/api/pool/summary", timeout=10).json()["exhausted"]
            set_clock(clock, CLOCK0 + 3 + 4)  # the cool-down (4 s from the 429) is over
            gb, cb = new_guest(b)
            assert source(gb, b)["provider"] == "vertex_gemini"
            chat_text(gb, b, cb, "fifth")  # third paid answer: 3 × price >= per_day (2.5 × price)
            chat_text(gb, b, cb, "sixth")
            src = source(gb, b)
            assert src["provider"] == "second" and src["switched"]["why"] == "budget_day", src
            assert Fake.calls[-1][0] == "/second/chat/completions"
            print("PASS daily budget reached -> switched (budget_day)")
        except Exception:
            srv.stop()
            print((srv.proc.stderr.read() if srv.proc.stderr else "")[-3000:])
            raise
        finally:
            if srv.proc.poll() is None:
                srv.stop()
            log = srv.proc.stderr.read() if srv.proc.stderr else ""
            fake.shutdown()
        # 6. the token never reaches a file or the log
        for f in root.rglob("*"):
            if f.is_file():
                assert TOKEN_PREFIX.encode() not in f.read_bytes(), f
        assert TOKEN_PREFIX not in log and SECOND_KEY not in log
        assert "guest budget advisory: vertex_gemini" in log, log[-2000:]
        spend = json.loads((root / "guest_spend.json").read_text())
        assert spend["providers"]["vertex_gemini"]["requests"] == 3
        print("PASS no token in any file or in the log; advisory logged; spend persisted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
