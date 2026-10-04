#!/usr/bin/env python3
"""API smoke test for loom-server: starts the built binary against a throwaway
data dir, drives a handful of REST endpoints with `requests`, and checks the
shapes/status codes loom/server/src/app.cpp promises. Registered as a ctest
(see the LOOM_BUILD_SERVER block in the top-level CMakeLists.txt) so a broken
server build/route fails `ctest` the same way a broken C++ unit does.
"""
import json
import hashlib
import os
import shutil
import socket
import sqlite3
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


def free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_up(base: str, timeout: float = 15.0, headers: dict | None = None) -> None:
    deadline = time.time() + timeout
    last_err = None
    while time.time() < deadline:
        try:
            r = requests.get(f"{base}/api/healthz", timeout=1, headers=headers)
            # 401 still proves the process is up and answering (relevant
            # when a bearer token is configured and healthz is gated too).
            if r.status_code in (200, 401):
                return
        except requests.RequestException as e:
            last_err = e
        time.sleep(0.1)
    raise RuntimeError(f"loom-server never came up: {last_err}")


def test_knowledge(base: str, data_dir: str) -> None:
    """Exercise the real offline catalog -> knowledge -> context HTTP path.

    The fixture is deliberately tiny and synthetic; no remote provider,
    private export or temporal-holdout answer key is involved.
    """
    def get(path: str, status: int = 200):
        r = requests.get(base + path, timeout=15)
        assert r.status_code == status, (path, r.status_code, r.text)
        return r.json()

    def post(path: str, body: dict, status: int = 200):
        r = requests.post(base + path, json=body, timeout=30)
        assert r.status_code == status, (path, r.status_code, r.text)
        return r.json()

    assert get("/api/knowledge/runs") == []
    assert get("/api/catalog/units") == []
    assert get("/api/knowledge/pack")["hash"]
    assert isinstance(get("/api/knowledge/policy?name=goal_types"), dict)
    assert get("/api/knowledge/policy?name=does-not-exist", 404)["error"]["code"] == "not_found"
    assert post("/api/knowledge/cancel", {}, 404)["error"]["code"] == "not_found"
    assert post("/api/knowledge/query", {"what": "claims"}, 404)["error"]["code"] == "not_found"
    assert post("/api/catalog/select", {}, 404)["error"]["code"] == "not_found"

    # Invalid input must never silently become a default run/import request.
    for path in ("/api/knowledge/run", "/api/knowledge/query", "/api/knowledge/judge",
                 "/api/knowledge/materialize", "/api/knowledge/predict", "/api/context/build",
                 "/api/catalog/scan", "/api/catalog/score", "/api/catalog/select", "/api/catalog/query",
                 "/api/catalog/import", "/api/catalog/override"):
        for raw, code in (("{broken", "parse"), ("[]", "invalid_argument")):
            r = requests.post(base + path, data=raw, timeout=5)
            assert r.status_code == 400, (path, raw, r.status_code, r.text)
            assert r.json()["error"]["code"] == code, r.text
    for path in ("/api/knowledge/runs?limit=oops", "/api/knowledge/runs?limit=1tail",
                 "/api/knowledge/runs?limit=99999999999999999", "/api/catalog/units?limit=0",
                 "/api/catalog/units?offset=-1", "/api/catalog/units?selected=maybe"):
        assert get(path, 400)["error"]["code"] == "invalid_argument"
    for out_dir in ("../escape", "/tmp/escape", ".", "bad\\name", 17):
        assert post("/api/knowledge/run", {"out_dir": out_dir}, 400)["error"]["code"] == "invalid_argument"

    source = Path(data_dir) / "conversations.json"
    message = (
        "# ChatADHD provider architecture\n\n"
        "ChatADHD is a software application built on the Loom kernel.\n"
        "Requirement: ChatADHD must preserve all original conversations.\n"
        "Decision: use a provider registry for every new data source.\n"
        "The provider registry is implemented in Loom.\n"
        "Principle: keep differences in data and use universal transformations.\n"
    )
    source.write_text(json.dumps([{
        "id": "http-smoke-conversation", "title": "ChatADHD provider architecture",
        "create_time": 1780272000, "update_time": 1780272000, "current_node": "turn",
        "mapping": {"turn": {"id": "turn", "parent": None, "children": [], "message": {
            "id": "http-smoke-message", "author": {"role": "user"}, "create_time": 1780272000,
            "content": {"content_type": "text", "parts": [message]}, "metadata": {},
        }}},
    }]), encoding="utf-8")
    conversations_before = len(get("/api/conversations"))
    scan = post("/api/catalog/scan", {"sources": [str(source)]})
    assert scan["units"] == 1, scan
    units = post("/api/catalog/query", {"sort": "id", "limit": 10})
    assert len(units) == 1, units
    unit_id = units[0]["unit"]["id"]
    assert get("/api/catalog/units?sort=id&limit=10") == units
    preview = get(f"/api/catalog/units/{unit_id}")
    assert preview["unit"]["unit"]["id"] == unit_id and preview["verified"] is True, preview
    assert get("/api/catalog/units/missing", 404)["error"]["code"] == "not_found"
    assert len(get("/api/conversations")) == conversations_before, "scan must not import messages"
    dry = post("/api/catalog/import", {"mode": "full", "dry_run": True})
    assert dry["imported"] == 1 and dry["conversations"] == [], dry
    assert len(get("/api/conversations")) == conversations_before, "dry run must not import messages"

    score = post("/api/catalog/score", {})
    assert score["run_id"], score
    defaults = post("/api/catalog/select", {"run_id": score["run_id"]})
    assert len(defaults["decisions"]) == 1, defaults
    assert len(get("/api/conversations")) == conversations_before, "selection must not import messages"
    choice = post("/api/catalog/override", {"unit_id": unit_id, "action": "include", "reason": "smoke fixture"})
    assert any(d["unit_id"] == unit_id and d["selected"] and d["decided_by"] == "user"
               for d in choice["decisions"]), choice
    assert len(get("/api/catalog/units?selected=true")) == 1
    assert post("/api/catalog/select", {}) == choice, "owner override must win on repeated selection"
    imported = post("/api/catalog/import", {"mode": "selective"})
    assert imported["imported"] == 1 and len(imported["conversations"]) == 1, imported
    assert len(get("/api/conversations")) == conversations_before + 1
    messages = get(f"/api/conversations/{imported['conversations'][0]}/messages")
    assert len(messages) == 1 and messages[0]["text"] == message, messages

    config = {"sources": [str(source)], "llm": "off", "out_dir": "http-smoke",
              "stage_params": {"catalog": {"import": {"mode": "full"}}}}
    run = post("/api/knowledge/run", config)
    assert run["status"] == "done", run
    assert len(run["stages"]) == 6, run
    run_id = run["run"]
    runs = get("/api/knowledge/runs?limit=1")
    assert runs[0]["id"] == run_id and runs[0]["status"] == "done", runs
    status = get(f"/api/knowledge/status?task_id={run['task_id']}")
    assert status["run"] is not None and isinstance(status["stages"], list), status
    entities = post("/api/knowledge/query", {"run": run_id, "what": "entities"})
    claims = post("/api/knowledge/query", {"run": run_id, "what": "claims"})
    assert entities["run"] == run_id and entities["items"], entities
    assert claims["items"], claims
    for claim in claims["items"]:
        assessment = claim["assessment"]
        assert assessment["evidence_class"] and assessment["origin"]
        assert 0 <= assessment["confidence"] <= 1
        if assessment["evidence_class"] in ("inferred", "extrapolated"):
            assert assessment["expected_property"] is not None, assessment
    for what in ("principles", "operators", "instances", "products", "stats"):
        assert "items" in post("/api/knowledge/query", {"run": run_id, "what": what})
    assert post("/api/knowledge/query", {"run": run_id, "what": "invalid"}, 400)["error"]["code"] == "invalid_argument"

    context = post("/api/context/build", {"run": run_id, "text": "Implement the ChatADHD provider registry",
                                           "budget_tokens": 600})
    selected = context["context_set"]
    assert 0 < selected["used_tokens"] <= selected["budget_tokens"] == 600, selected
    assert context["text"] and all(item["why"] for item in selected["items"]), context
    bands = {"stable": 0, "project": 1, "goal": 2}
    order = [bands[item["band"]] for item in selected["items"]]
    assert order == sorted(order), selected
    rendered = post("/api/knowledge/materialize", {"run": run_id, "kind": "self_description"})
    assert rendered["markdown"] and rendered["product"], rendered

    judgement = post("/api/knowledge/judge", {"target_kind": "claim", "target": claims["items"][0]["id"],
                                               "verdict": "confirm", "reason": "smoke verification", "replay_run": run_id})
    assert judgement["seq"] > 0 and judgement["replay"]["applied"] == 1, judgement

    # An orchestration failure is a native RunResult with status/error, not
    # a fabricated successful result or a transport exception.
    failed = post("/api/knowledge/run", {"sources": [str(source)], "llm": "off",
                                         "stage_params": {"catalog": {"import": {"mode": "invalid"}}}})
    assert failed["status"] == "failed" and failed["error"], failed
    print("OK: loom-server knowledge/catalog/context checks passed")


def graph_packet_profile_request() -> dict:
    """A fictional profile artifact, with exact source text and native rows."""
    def digest(value):
        raw = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)
        return hashlib.sha256(raw.encode("utf-8")).hexdigest()

    profile = {"schema": "loom.application_profile/1", "id": "synthetic-http-profile", "profile_revision": 1,
               "label": "Fikcyjny profil Żółć", "target": {"application_id": "fictional", "version": "3.7", "platform": "web"}}
    raw_source = json.dumps(profile, ensure_ascii=False, indent=2) + "\n"
    raw_hash = hashlib.sha256(raw_source.encode("utf-8")).hexdigest()
    entity = {"id": "e_profile_http", "kind": "application_profile", "canonical_key": "synthetic-http-profile@1",
              "label": profile["label"], "labels": {}, "aliases": [], "parent": "", "first_seen": "", "last_seen": "",
              "evidence_class": "user", "origin": "user", "confidence": 1, "status": "active",
              "attrs": {"raw_source": raw_source, "raw_sha256": raw_hash, "source_observation": "o_profile_http"}}
    observation = {"id": "o_profile_http", "unit": "u_profile_http", "kind": "code_block", "text": raw_source,
                   "locator": {"source": "synthetic:profile.json", "member": "", "json_pointer": "", "byte_start": 0,
                               "byte_len": len(raw_source.encode("utf-8")), "time_start": None, "time_end": None, "line": None},
                   "lang": "", "date": "", "ordinal": 0, "artifact_type": "application_profile_json",
                   "speaker": "http-smoke-user", "attrs": {"raw_sha256": raw_hash}}
    source = {"observation": observation, "known_at": None, "text_sha256": raw_hash}
    origin = {"kind": "recorded", "actor": "http-smoke-user", "model": None, "recipe_sha256": None, "response_sha256": None}
    packet = {"schema": "loom.graph_packet/1", "definitions": [], "entities": [entity], "claims": [], "sources": [source],
              "task": {"operation": "explicit-profile-save", "transformation_history": "not_asserted"},
              "provenance": {"definitions": {}, "claims": {},
                             "entities": {entity["id"]: {"known_at": None, "origin": origin, "record_sha256": digest(entity)}},
                             "sources": {observation["id"]: {"known_at": None, "origin": origin, "record_sha256": digest(source)}}},
              "history": []}
    packet["packet_id"] = digest(packet)
    return {"operation": "accept", "target": "synthetic-http-profile-storage", "packet": packet,
               "selection": {"entities": [entity["id"]], "claims": [], "sources": [observation["id"]]},
               "expected_rows": {"entities": {entity["id"]: None}, "claims": {}, "sources": {observation["id"]: None}},
               "explicitly_accepted": True}


def test_graph_packets(base: str, data_dir: str) -> None:
    """The HTTP facade keeps native acceptance, immutable receipts and drift."""
    def digest(value):
        raw = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)
        return hashlib.sha256(raw.encode("utf-8")).hexdigest()

    def post(body, status=200):
        response = requests.post(base + "/api/graph/packets/store", json=body, timeout=15)
        assert response.status_code == status, (response.status_code, response.text)
        return response.json()

    for raw, code in (("{broken", "parse"), ("[]", "invalid_argument")):
        response = requests.post(base + "/api/graph/packets/store", data=raw, timeout=5)
        assert response.status_code == 400 and response.json()["error"]["code"] == code, response.text
    assert post({"operation": "read", "receipt_id": "gpr_missing"}, 404)["error"]["code"] == "not_found"
    request = graph_packet_profile_request()
    packet = request["packet"]
    entity = packet["entities"][0]
    raw_source, raw_hash = entity["attrs"]["raw_source"], entity["attrs"]["raw_sha256"]
    config_before = requests.get(base + "/api/config", timeout=5).json()
    rejected = dict(request, explicitly_accepted=False)
    assert post(rejected, 400)["error"]["code"] == "invalid_argument"
    result = post(request)
    receipt = result["receipt"]
    assert receipt["packet"] == packet and not result["replayed"], result
    assert receipt["acceptance_establishes_content_truth"] is False
    assert receipt["reversible_history_validation"] == "native_codec_backwards_and_forwards_history_replay"
    readback = post({"operation": "read", "receipt_id": receipt["id"]})
    assert readback["receipt"] == receipt and readback["row_drift"]["matches"], readback
    assert post({"operation": "replay", "receipt_id": receipt["id"]})["receipt"] == receipt
    assert post(request)["replayed"] is True, "exact acceptance retries use the immutable native receipt"
    rows = requests.post(base + "/api/knowledge/query", json={"run": receipt["run_id"], "what": "entities", "kind": "application_profile"}, timeout=5).json()
    assert rows["items"][0]["attrs"]["raw_source"] == raw_source
    assert rows["items"][0]["attrs"]["raw_sha256"] == raw_hash
    changed = json.loads(json.dumps(request))
    changed["packet"]["task"]["revised"] = True
    changed["packet"].pop("packet_id")
    changed["packet"]["packet_id"] = digest(changed["packet"])
    assert post(changed, 400)["error"]["code"] == "invalid_argument", "CAS is not silently overwritten by HTTP"
    with sqlite3.connect(Path(data_dir) / "chatadhd.db") as database:
        assert database.execute("SELECT COUNT(*) FROM loom_kb_graph_receipts").fetchone()[0] == 1
        database.execute("UPDATE loom_kb_entities SET canonical_key='drift' WHERE run_id=? AND id=?", (receipt["run_id"], entity["id"]))
    drift = post({"operation": "read", "receipt_id": receipt["id"]})
    assert not drift["row_drift"]["matches"] and drift["receipt"] == receipt
    assert post({"operation": "replay", "receipt_id": receipt["id"]}, 400)["error"]["code"] == "invalid_argument"
    assert requests.get(base + "/api/config", timeout=5).json() == config_before
    assert "sk-smoke" not in json.dumps(receipt), "existing secrets never enter the accepted profile artifact"
    print("OK: loom-server graph-packet profile acceptance/read/replay/CAS/drift checks passed")


def main() -> int:
    server_bin = os.environ.get("LOOM_SERVER_BIN")
    if not server_bin or not os.path.exists(server_bin):
        print(f"SKIP: LOOM_SERVER_BIN not set or missing ({server_bin!r})")
        return 0

    data_dir = tempfile.mkdtemp(prefix="loom_server_smoke_")
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    proc = subprocess.Popen(
        [server_bin, "--host", "127.0.0.1", "--port", str(port), "--data-dir", data_dir],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        wait_up(base)

        r = requests.get(f"{base}/api/healthz")
        assert r.status_code == 200 and r.json().get("ok") is True, r.text

        r = requests.get(f"{base}/api/version")
        assert r.status_code == 200 and "version" in r.json(), r.text

        r = requests.get(f"{base}/api/conversations")
        assert r.status_code == 200 and r.json() == [], r.text

        r = requests.post(f"{base}/api/conversations", json={"title": "Smoke test"})
        assert r.status_code == 201, r.text
        conv = r.json()
        assert conv["title"] == "Smoke test" and conv["id"].startswith("c_"), conv

        r = requests.get(f"{base}/api/conversations/{conv['id']}")
        assert r.status_code == 200 and r.json()["id"] == conv["id"], r.text

        r = requests.get(f"{base}/api/conversations/does-not-exist")
        assert r.status_code == 404, r.text
        assert r.json()["error"]["code"] == "not_found", r.text

        r = requests.patch(f"{base}/api/config", json={"default_model": "smoke/model"})
        assert r.status_code == 200 and r.json()["default_model"] == "smoke/model", r.text

        r = requests.post(f"{base}/api/secrets/api_key", json={"value": "sk-smoke"})
        assert r.status_code == 200 and r.json()["set"] is True, r.text

        r = requests.get(f"{base}/api/secrets/api_key/has")
        assert r.status_code == 200 and r.json()["has"] is True, r.text

        r = requests.get(f"{base}/api/secrets")
        assert r.status_code == 200 and "api_key" in r.json(), r.text
        # Never leaks the value itself.
        assert "sk-smoke" not in r.text

        r = requests.get(f"{base}/api/semantic/status")
        assert r.status_code == 200 and "pending" in r.json(), r.text

        r = requests.post(f"{base}/api/memory", json={"content": "root note"})
        assert r.status_code == 201, r.text
        mem = r.json()

        r = requests.get(f"{base}/api/memory")
        assert r.status_code == 200 and any(n["id"] == mem["id"] for n in r.json()["nodes"]), r.text

        r = requests.get(f"{base}/api/graph/data")
        assert r.status_code == 200 and "nodes" in r.json() and "edges" in r.json(), r.text

        r = requests.get(f"{base}/api/models")
        assert r.status_code == 200, r.text

        r = requests.get(f"{base}/api/logs?max_lines=5")
        assert r.status_code == 200 and isinstance(r.json(), list), r.text

        test_knowledge(base, data_dir)
        test_graph_packets(base, data_dir)

        # SSE chat: no upstream configured, so this must fail cleanly with a
        # well-formed "error" chunk rather than hang or crash the server.
        # Remove the fake credential set by the secrets test above; leaving
        # it installed would attempt a real upstream request and make this
        # supposedly offline test depend on network timeouts.
        r = requests.delete(f"{base}/api/secrets/api_key", timeout=5)
        assert r.status_code == 200 and r.json()["deleted"] is True, r.text
        with requests.post(f"{base}/api/chat", json={"message": "hi"}, stream=True, timeout=10) as r:
            assert r.status_code == 200
            saw_start = saw_terminal = False
            for line in r.iter_lines(decode_unicode=True):
                if not line or not line.startswith("data:"):
                    continue
                chunk = json.loads(line[len("data:"):].strip())
                if chunk["type"] == "start":
                    saw_start = True
                if chunk["type"] in ("done", "error"):
                    saw_terminal = True
                    break
            assert saw_start and saw_terminal

        # A path with invalid-UTF-8 percent-encoded bytes (the SPA-fallback
        # 404 handler builds a JSON error message from the raw request
        # path) must still come back as a clean, well-formed JSON error -
        # never a crash, and never cpp-httplib's default EXCEPTION_WHAT
        # header (which would leak an internal exception message).
        r = requests.get(f"{base}/api/%FF%FE/bogus")
        assert r.status_code == 404, r.text
        assert "EXCEPTION_WHAT" not in r.headers, dict(r.headers)
        body = r.json()
        assert body["error"]["code"] == "not_found", body

        print("OK: all loom-server smoke checks passed")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
        shutil.rmtree(data_dir, ignore_errors=True)


def test_auth(server_bin: str) -> None:
    """A second server instance with --token set: every /api/ route (plain,
    SSE-streamed and archive alike) must require it, and the configured
    token itself must never appear in a response body."""
    data_dir = tempfile.mkdtemp(prefix="loom_server_smoke_auth_")
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    token = "s3cr3t-e2e-token"
    proc = subprocess.Popen(
        [server_bin, "--host", "127.0.0.1", "--port", str(port), "--data-dir", data_dir, "--token", token],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        wait_up(base)

        # healthz is intentionally exempt (it isn't under /api/... it is,
        # actually, so it must also be gated - confirm that explicitly).
        r = requests.get(f"{base}/api/healthz")
        assert r.status_code == 401, r.text
        assert token not in r.text

        r = requests.get(f"{base}/api/healthz", headers={"Authorization": "Bearer wrong-token"})
        assert r.status_code == 401, r.text
        assert token not in r.text

        # A same-length-but-wrong token must be rejected identically (this
        # doesn't measure timing, just that the comparison is genuinely a
        # full compare and not e.g. a prefix check).
        wrong_same_len = "x" * len(token)
        r = requests.get(f"{base}/api/healthz", headers={"Authorization": f"Bearer {wrong_same_len}"})
        assert r.status_code == 401, r.text

        r = requests.get(f"{base}/api/healthz", headers={"Authorization": f"Bearer {token}"})
        assert r.status_code == 200 and r.json()["ok"] is True, r.text

        # The archive endpoints (explicitly called out as easy to forget)
        # are gated the same way as every other /api/ route.
        r = requests.get(f"{base}/api/archive/status")
        assert r.status_code == 401, r.text
        r = requests.get(f"{base}/api/archive/status", headers={"Authorization": f"Bearer {token}"})
        assert r.status_code in (200, 404), r.text

        # SSE routes go through the same pre-routing handler, but a chunked
        # response behaves differently from a plain one - confirm the 401
        # arrives instead of the stream hanging open.
        r = requests.post(f"{base}/api/chat", json={"message": "hi"}, stream=True, timeout=5)
        assert r.status_code == 401, r.text
        r.close()

        r = requests.get(f"{base}/api/events/stream", stream=True, timeout=5)
        assert r.status_code == 401, r.text
        r.close()

        # Newly exposed knowledge reads and writes use the same auth wall.
        for method, path in (("GET", "/api/knowledge/runs"), ("GET", "/api/knowledge/pack"),
                             ("GET", "/api/catalog/units"), ("POST", "/api/knowledge/run"),
                             ("POST", "/api/knowledge/judge"), ("POST", "/api/catalog/import"),
                             ("POST", "/api/catalog/scan"), ("POST", "/api/catalog/select"),
                             ("POST", "/api/context/build"), ("POST", "/api/graph/packets/store")):
            r = requests.request(method, base + path, json={}, timeout=5)
            assert r.status_code == 401 and r.json()["error"]["code"] == "auth", (path, r.text)
            assert token not in r.text
        r = requests.get(f"{base}/api/knowledge/runs", headers={"Authorization": f"Bearer {token}"}, timeout=5)
        assert r.status_code == 200 and r.json() == [], r.text

        # Reuse a real client session after rejecting a POST with a body.
        # No client-side Connection override may hide undrained body bytes.
        request = graph_packet_profile_request()
        headers = {"Authorization": f"Bearer {token}"}
        with requests.Session() as client:
            rejected = client.post(base + "/api/graph/packets/store", json=request, timeout=5)
            assert rejected.status_code == 401 and rejected.json()["error"]["code"] == "auth", rejected.text
            assert rejected.headers.get("Connection", "").lower() == "close"
            assert token not in rejected.text
            unchanged = client.get(base + "/api/knowledge/runs", headers=headers, timeout=5)
            assert unchanged.status_code == 200 and unchanged.json() == [], unchanged.text
            accepted = client.post(base + "/api/graph/packets/store", json=request, headers=headers, timeout=5)
            assert accepted.status_code == 200, accepted.text
            receipt = accepted.json()["receipt"]
            assert receipt["packet"] == request["packet"]
            readback = client.post(base + "/api/graph/packets/store",
                                  json={"operation": "read", "receipt_id": receipt["id"]}, headers=headers, timeout=5)
            assert readback.status_code == 200 and readback.json()["receipt"] == receipt, readback.text
            retry = client.post(base + "/api/graph/packets/store", json=request, headers=headers, timeout=5)
            assert retry.status_code == 200 and retry.json()["replayed"] is True, retry.text
            assert retry.json()["receipt"] == receipt

        print("OK: loom-server auth checks passed")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
        shutil.rmtree(data_dir, ignore_errors=True)


if __name__ == "__main__":
    rc = main()
    if rc == 0:
        server_bin = os.environ.get("LOOM_SERVER_BIN")
        if server_bin and os.path.exists(server_bin):
            test_auth(server_bin)
    sys.exit(rc)
