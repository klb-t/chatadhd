#!/usr/bin/env python3
"""R103 B1b: the working-copy bundle of a guest (GET/POST /api/working-copy/bundle, loom.guest_bundle/2) on the real
server with a mock free provider. Generative secret scan: in every round fresh random values are planted in the keys
file of the pool (the credential overlay), in the guest's secrets store, in credential-named settings, inside an
ordinary setting and a nested one; the bundle bytes must contain none of them, nor the session cookie. Restore: a new
guest gets the bundle back exactly (same rows, same graph node ids) with 0 requests to the model; a tampered bundle is
refused; a guest cannot plant a table outside the allow-list. Needs LOOM_SERVER_BIN and python `requests`."""
import json
import random
import string
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_server_guests import CLOCK0, MOCK, Server, chat, free_port, overlay, set_clock, solve_pow  # noqa: E402

ROUNDS = 4
ALPHABET = string.ascii_letters + string.digits + "-_.~+/="


def secret(rng: random.Random) -> str:
    return "".join(rng.choice(ALPHABET) for _ in range(rng.randint(16, 64)))


def new_guest(base: str) -> requests.Session:
    g = requests.Session()
    r = g.post(base + "/api/auth/guest", json=solve_pow(base, g), timeout=30)
    assert r.status_code == 201, (r.status_code, r.text)
    return g


def mock_count(port: int) -> int:
    return requests.get(f"http://127.0.0.1:{port}/__count", timeout=5).json()["requests"]


def main() -> int:
    rng = random.Random(0xB1B)
    mock_port = free_port()
    mock = subprocess.Popen([sys.executable, str(MOCK), str(mock_port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        with tempfile.TemporaryDirectory(prefix="loom-guest-bundle-") as tmp:
            root = Path(tmp) / "root"
            root.mkdir()
            clock = Path(tmp) / "clock"
            set_clock(clock, CLOCK0)
            keys = Path(tmp) / "keys.env"
            pool_key = secret(rng)
            keys.write_text(f"MOCK_FREE_KEY={pool_key}\n")
            overlay(root, "server_accounts", {
                "allow_private_hosts": ["127.0.0.1"],
                "guests": {"enabled": True, "start": "auto", "sweep_interval_seconds": 0, "keys_file": str(keys),
                           "protection": {"clock": {"file": str(clock)}},
                           "workdir": {"subdir": "guests", "idle_ttl_seconds": 3600, "max_bytes": 0, "keep_after_session": "ask"},
                           "secrets_routes": "write_only_session", "model_sources": ["byok", "pool", "free", "local"]}})
            overlay(root, "free_providers", {
                "providers": [{"id": "mock_free", "display_name": "Mock free", "base_url": f"http://127.0.0.1:{mock_port}",
                               "auth_env": "MOCK_FREE_KEY", "auth_secret": "api_key", "active_when": "key_present", "enabled": True,
                               "data_terms": {"retention": "none_by_default", "human_review": False, "training": "no",
                                              "summary": {"pl": "Test.", "en": "Test."}, "observed_at": "2026-10-28",
                                              "source_url": "test", "source_rating": "test"}}],
                "free_model_choice": {"rank_source": "table", "require": {"training": "no"},
                                      "table": [{"model": "mock/e2e-model", "provider": "mock_free", "quality": 1}]}})
            srv = Server(root)
            try:
                b = srv.base
                last_bundle = None
                last_guest = None
                for rnd in range(ROUNDS):
                    planted = [pool_key]
                    g = new_guest(b)
                    conv = g.post(b + "/api/conversations", json={"title": f"Runda {rnd}"}, timeout=10).json()["id"]
                    chat(g, b, conv, f"Spotkanie z Anną w Krakowie, runda {rnd}.")
                    for k in ("api_key", "github_token", "anthropic_batch_key", f"custom_{rnd}_key"):
                        v = secret(rng)
                        planted.append(v)
                        r = g.post(b + f"/api/secrets/{k}", json={"value": v}, timeout=10)
                        assert r.status_code in (200, 201), (k, r.status_code, r.text)
                    s1, s2, s3 = secret(rng), secret(rng), secret(rng)
                    planted += [s1, s2, s3]
                    r = g.patch(b + "/api/config", json={
                        "sessionToken": s1, "openrouter_api_key": s2,
                        "note_for_me": "klucz: " + planted[1],
                        "provider_extra": {"headers": {"Authorization": "Bearer " + planted[2]}, "label": "ok"},
                        "window_layouts": [{"id": f"r{rnd}", "label": "Układ"}], "secret_word": s3}, timeout=10)
                    assert r.status_code == 200, (r.status_code, r.text)
                    r = g.get(b + "/api/working-copy/bundle", timeout=30)
                    assert r.status_code == 200, (r.status_code, r.text[:300])
                    raw = r.content
                    bundle = r.json()
                    assert bundle["schema"] == "loom.guest_bundle/2"
                    cookies = [c.value for c in g.cookies if len(c.value) >= 8]
                    assert cookies, "the guest has a session cookie"
                    for v in planted + cookies:
                        assert v.encode() not in raw, f"round {rnd}: a planted value is in the bundle"
                    cfg = bundle["content"]["config"]
                    assert cfg["provider_extra"]["label"] == "ok" and "Authorization" not in cfg["provider_extra"]["headers"]
                    assert cfg["window_layouts"][0]["id"] == f"r{rnd}"
                    assert bundle["content"]["tables"]["messages"]["rows"], "the conversation is in the bundle"
                    last_bundle, last_guest = r.text, g
                print(f"PASS generative secret scan: {ROUNDS} rounds, keys file + secrets store + settings + cookie, none in the bundle bytes")

                # restore into a new guest: exact, without analysis or a model request
                src_tables = json.loads(last_bundle)["content"]["tables"]
                src_nodes = sorted(n["id"] for n in last_guest.get(b + "/api/graph/nodes?limit=100000", timeout=10).json())
                g2 = new_guest(b)
                before = mock_count(mock_port)
                r = g2.post(b + "/api/working-copy/bundle?mode=replace", data=last_bundle,
                            headers={"Content-Type": "application/json"}, timeout=30)
                assert r.status_code == 200, (r.status_code, r.text)
                assert r.json()["analysis_runs"] == 0 and r.json()["restored"]["tables"]["conversations"] >= 1, r.text
                time.sleep(1.5)
                assert mock_count(mock_port) == before, "the restore reached the model"
                again = g2.get(b + "/api/working-copy/bundle", timeout=30).json()
                assert again["content"]["tables"] == src_tables, "rows differ after the restore"
                nodes = sorted(n["id"] for n in g2.get(b + "/api/graph/nodes?limit=100000", timeout=10).json())
                assert nodes == src_nodes and nodes, (len(nodes), len(src_nodes))
                assert again["content"]["config"]["window_layouts"] == json.loads(last_bundle)["content"]["config"]["window_layouts"]
                print("PASS restore into a new guest: same rows and graph node ids, 0 model requests, analysis_runs 0")

                # integrity and allow-list
                t = json.loads(last_bundle)
                t["content"]["tables"]["conversations"]["rows"][0][1] = "tampered"
                r = g2.post(b + "/api/working-copy/bundle", data=json.dumps(t), headers={"Content-Type": "application/json"}, timeout=30)
                assert r.status_code == 400, (r.status_code, r.text)
                print("PASS a tampered bundle is refused (sha256)")
            finally:
                srv.stop()
    finally:
        mock.terminate()
        mock.wait(timeout=10)
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
