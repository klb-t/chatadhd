#!/usr/bin/env python3
"""Optional smoke test of the chat / action flow against a REAL model (OpenRouter).

NOT part of CTest or CI: it spends money and needs network. Run it by hand:

    python3 loom/tools/real_model_smoke.py [--model google/gemini-2.5-flash-lite] [--budget 0.50]
    python3 loom/tools/real_model_smoke.py --mock          # offline self-test of this script (no money)

What it does (standard library only):
  1. starts a dev loom-server on a fresh temp data dir (deleted at the end unless --keep-data),
  2. configures api_key (any non-empty placeholder: a proxy injects the real key), base_url and a cheap model,
  3. starts a small METERING FORWARDER on 127.0.0.1 between the server and the provider. The kernel has no hard
     total-cost cap (the usage policy only asks for confirmation on a x10 growth), so the cap is enforced here:
     every provider call is forwarded with `usage.include=true`, its cost is read from the answer (or estimated
     from tokens with the --price-* fallback), and a call is REFUSED (HTTP 402) when spent + worst case of this
     call would exceed --budget. The whole run therefore cannot exceed the budget. Use --direct to skip the
     forwarder and talk to --base-url straight from the kernel (then the cost is only known from the answers).
  4. drives the scenario through the same HTTP API the web app uses (POST /api/chat SSE, GET /api/graph/*) with
     synthetic content only, and writes a JSON record per step (request, outcome, excerpt, tokens, cost).

Outcome vocabulary per step: works / partially / doesn't. The script never retries a failing paid step.
"""
from __future__ import annotations

import argparse
import http.client
import http.server
import json
import os
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SERVER = ROOT / "loom" / "build" / "dev" / "server" / "loom-server"
DEFAULT_MODEL = "google/gemini-2.5-flash-lite"
UPSTREAM = "https://openrouter.ai/api/v1"
SCRIPT_VERSION = 1


# ── metering forwarder ────────────────────────────────────────────────────────────────────────────────────

class Meter:
    """Thread-safe ledger of the money this run has spent (and may still spend)."""

    def __init__(self, budget: float, price_in: float, price_out: float):
        self.budget = budget
        self.price_in = price_in      # USD per 1M input tokens (fallback / worst case)
        self.price_out = price_out    # USD per 1M output tokens
        self.lock = threading.Lock()
        self.spent = 0.0
        self.calls: list[dict] = []
        self.refused = 0

    def worst_case(self, body: dict, raw_len: int) -> float:
        max_out = body.get("max_tokens") or body.get("max_completion_tokens") or 4096
        in_tokens = raw_len / 2.0     # deliberately pessimistic: 2 bytes per token
        return (in_tokens * self.price_in + max_out * self.price_out) / 1e6

    def admit(self, worst: float) -> bool:
        with self.lock:
            if self.spent + worst > self.budget:
                self.refused += 1
                return False
            return True

    def record(self, entry: dict) -> None:
        with self.lock:
            self.spent += entry["cost"]
            entry["spent_total"] = round(self.spent, 8)
            self.calls.append(entry)

    def snapshot(self) -> tuple[float, int]:
        with self.lock:
            return self.spent, len(self.calls)


def _ssl_context() -> ssl.SSLContext:
    for cand in (os.environ.get("SSL_CERT_FILE"), os.environ.get("REQUESTS_CA_BUNDLE"), "/root/.ccr/ca-bundle.crt"):
        if cand and Path(cand).exists():
            return ssl.create_default_context(cafile=cand)
    return ssl.create_default_context()


def make_forwarder(meter: Meter, upstream: str, port: int, mock: bool):
    parsed = urllib.parse.urlparse(upstream)

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):  # quiet
            pass

        def _send_json(self, code: int, obj: dict) -> None:
            data = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            raw = self.rfile.read(int(self.headers.get("Content-Length") or 0))
            path = self.path
            if not path.endswith("/chat/completions"):
                return self._send_json(403, {"error": {"message": "meter: only /chat/completions is forwarded"}})
            try:
                body = json.loads(raw)
            except ValueError:
                return self._send_json(400, {"error": {"message": "meter: bad JSON"}})
            worst = meter.worst_case(body, len(raw))
            if not meter.admit(worst):
                spent, _ = meter.snapshot()
                return self._send_json(402, {"error": {"code": 402, "message":
                    f"meter: budget {meter.budget:.2f} USD would be exceeded (spent {spent:.6f}, worst case of this call {worst:.6f})"}})
            body["usage"] = {"include": True}
            if body.get("stream"):
                body.setdefault("stream_options", {"include_usage": True})
            payload = json.dumps(body).encode()
            t0 = time.time()
            entry = {"model": body.get("model"), "stream": bool(body.get("stream")), "request_bytes": len(raw),
                     "max_tokens": body.get("max_tokens"), "messages": len(body.get("messages") or []),
                     "worst_case": round(worst, 8), "status": None, "cost": 0.0, "prompt_tokens": None,
                     "completion_tokens": None, "cost_source": "none", "seconds": None}
            try:
                if mock:
                    status, headers, chunks = _mock_upstream(body)
                else:
                    status, headers, chunks = _real_upstream(parsed, payload, self.headers)
            except Exception as exc:  # network failure: nothing was billed that we can see
                entry.update(status=0, error=repr(exc), seconds=round(time.time() - t0, 3))
                meter.record(entry)
                return self._send_json(502, {"error": {"message": f"meter: upstream failure: {exc}"}})
            entry["status"] = status
            self.send_response(status)
            ctype = headers.get("Content-Type", "application/json")
            self.send_header("Content-Type", ctype)
            self.send_header("Connection", "close")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            collected = bytearray()
            try:
                for chunk in chunks:
                    collected += chunk
                    self.wfile.write(b"%x\r\n" % len(chunk) + chunk + b"\r\n")
                    self.wfile.flush()
                self.wfile.write(b"0\r\n\r\n")
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True
            usage = _find_usage(bytes(collected))
            if usage:
                entry["prompt_tokens"] = usage.get("prompt_tokens")
                entry["completion_tokens"] = usage.get("completion_tokens")
                if isinstance(usage.get("cost"), (int, float)):
                    entry["cost"], entry["cost_source"] = float(usage["cost"]), "provider"
                elif entry["prompt_tokens"] is not None:
                    entry["cost"] = ((entry["prompt_tokens"] or 0) * meter.price_in
                                     + (entry["completion_tokens"] or 0) * meter.price_out) / 1e6
                    entry["cost_source"] = "tokens*price"
            elif status == 200:
                # a billed answer without usage: charge the worst case, never zero
                entry["cost"], entry["cost_source"] = worst, "worst_case"
            entry["seconds"] = round(time.time() - t0, 3)
            entry["response_excerpt"] = _excerpt(bytes(collected))
            meter.record(entry)

    class Srv(http.server.ThreadingHTTPServer):
        daemon_threads = True

    return Srv(("127.0.0.1", port), Handler)


def _real_upstream(parsed, payload: bytes, in_headers):
    ctx = _ssl_context()
    proxy = os.environ.get("HTTPS_PROXY") or os.environ.get("https_proxy")
    if proxy:
        p = urllib.parse.urlparse(proxy)
        conn = http.client.HTTPSConnection(p.hostname, p.port, context=ctx, timeout=180)
        conn.set_tunnel(parsed.hostname, parsed.port or 443)
    else:
        conn = http.client.HTTPSConnection(parsed.hostname, parsed.port or 443, context=ctx, timeout=180)
    hdrs = {"Content-Type": "application/json", "Content-Length": str(len(payload)),
            "Authorization": in_headers.get("Authorization", "Bearer proxy-injected"),
            "X-Title": "loom real_model_smoke"}
    conn.request("POST", parsed.path.rstrip("/") + "/chat/completions", body=payload, headers=hdrs)
    resp = conn.getresponse()

    def gen():
        while True:
            part = resp.read1(4096) if hasattr(resp, "read1") else resp.read(4096)
            if not part:
                break
            yield part
        conn.close()
    return resp.status, {"Content-Type": resp.getheader("Content-Type", "application/json")}, gen()


def _mock_upstream(body: dict):
    """Offline stand-in for the provider (self-test of this script only; no network, no money)."""
    text = json.dumps(body, ensure_ascii=False)
    last = (body.get("messages") or [{}])[-1].get("content") or ""
    if isinstance(last, list):
        last = " ".join(str(p.get("text", "")) for p in last if isinstance(p, dict))
    reply = "Mock answer."
    if "- summarise:" in text:  # intent classification call
        low = last.strip().splitlines()[-1].lower() if last.strip() else ""
        action = None
        for words, name in ((("boil", "streść", "summar"), "summarise"), (("wytłumacz", "explain"), "explain_simply"),
                            (("tłumacz", "translat"), "translate")):
            if any(w in low for w in words):
                action = name
                break
        reply = json.dumps({"kind": "command" if action else "message", "action": action,
                            "reference": "conversation" if action else None, "args": {},
                            "confidence": 0.9 if action else 0.5})
    usage = {"prompt_tokens": max(1, len(text) // 4), "completion_tokens": 12, "cost": 0.00002}
    if body.get("stream"):
        def gen():
            yield ("data: " + json.dumps({"choices": [{"delta": {"content": reply}}]}) + "\n\n").encode()
            yield ("data: " + json.dumps({"choices": [{"delta": {}, "finish_reason": "stop"}], "usage": usage}) + "\n\n").encode()
            yield b"data: [DONE]\n\n"
        return 200, {"Content-Type": "text/event-stream"}, gen()
    data = json.dumps({"id": "mock", "choices": [{"message": {"role": "assistant", "content": reply}, "finish_reason": "stop"}],
                       "usage": usage}).encode()
    return 200, {"Content-Type": "application/json"}, iter([data])


def _find_usage(blob: bytes) -> dict | None:
    txt = blob.decode("utf-8", "replace")
    found = None
    for line in txt.splitlines():
        line = line.strip()
        if line.startswith("data:"):
            line = line[5:].strip()
        if not line.startswith("{"):
            continue
        try:
            obj = json.loads(line)
        except ValueError:
            continue
        if isinstance(obj, dict) and isinstance(obj.get("usage"), dict):
            found = obj["usage"]
    if found is None:
        try:
            obj = json.loads(txt)
            if isinstance(obj, dict) and isinstance(obj.get("usage"), dict):
                found = obj["usage"]
        except ValueError:
            pass
    return found


def _excerpt(blob: bytes, n: int = 300) -> str:
    txt = blob.decode("utf-8", "replace")
    if txt.lstrip().startswith("{") and '"error"' in txt:
        return txt[:n]
    return txt[:n].replace("\n", " ")


# ── loom-server driver ────────────────────────────────────────────────────────────────────────────────────

def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Api:
    def __init__(self, base: str):
        self.base = base

    def req(self, method: str, path: str, body=None, timeout=240):
        data = None if body is None else json.dumps(body).encode()
        r = urllib.request.Request(self.base + path, data=data, method=method,
                                   headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(r, timeout=timeout) as resp:
                raw = resp.read()
                return resp.status, (json.loads(raw) if raw else None)
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw)
            except ValueError:
                return e.code, {"raw": raw.decode("utf-8", "replace")[:500]}

    def chat(self, body: dict, timeout=240) -> list[dict]:
        """POST /api/chat and return the SSE chunks."""
        r = urllib.request.Request(self.base + "/api/chat", data=json.dumps(body).encode(), method="POST",
                                   headers={"Content-Type": "application/json", "Accept": "text/event-stream"})
        chunks: list[dict] = []
        try:
            with urllib.request.urlopen(r, timeout=timeout) as resp:
                for line in resp:
                    line = line.decode("utf-8", "replace").strip()
                    if line.startswith("data:"):
                        line = line[5:].strip()
                    if line.startswith("{"):
                        try:
                            chunks.append(json.loads(line))
                        except ValueError:
                            chunks.append({"type": "unparsed", "raw": line[:200]})
        except urllib.error.HTTPError as e:
            chunks.append({"type": "http_error", "status": e.code, "body": e.read().decode("utf-8", "replace")[:400]})
        except Exception as e:  # noqa: BLE001
            chunks.append({"type": "client_error", "message": repr(e)})
        return chunks


def wait_ready(api: Api, proc: subprocess.Popen, seconds=60):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if proc.poll() is not None:
            raise RuntimeError(f"loom-server exited early with {proc.returncode}")
        try:
            if api.req("GET", "/api/healthz", timeout=2)[0] == 200:
                return
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.3)
    raise RuntimeError("loom-server did not become ready")


# ── scenario ──────────────────────────────────────────────────────────────────────────────────────────────

def final_chunk(chunks: list[dict]) -> dict:
    for c in reversed(chunks):
        if c.get("type") in ("done", "action_proposal", "action_result", "usage_pending", "error", "http_error", "client_error"):
            return c
    return chunks[-1] if chunks else {}


def text_of(chunks: list[dict]) -> str:
    acc = "".join(c.get("text", "") for c in chunks if c.get("type") == "delta")
    if acc:
        return acc
    fc = final_chunk(chunks)
    return str(fc.get("text") or fc.get("message") or fc.get("body") or "")


class Runner:
    def __init__(self, api: Api, meter: Meter, args):
        self.api, self.meter, self.args = api, meter, args
        self.steps: list[dict] = []
        self.conv_id: str | None = None
        self.aborted = False

    def over_budget(self) -> bool:
        spent, _ = self.meter.snapshot()
        return spent >= self.meter.budget * 0.9

    def step(self, name: str, message: str, judge, note: str = "", config: dict | None = None):
        """Send one chat input; judge(chunks, final) -> (outcome, why). Never retried."""
        rec = {"step": name, "request": {"message": message, "conv_id": self.conv_id, "config": config or {}}, "note": note}
        if self.aborted or self.over_budget():
            self.aborted = True
            rec.update(outcome="skipped", why="budget guard: stopped before the cap", tokens=None, cost=0.0)
            self.steps.append(rec)
            return rec, []
        if config:
            code, resp = self.api.req("PATCH", "/api/config", config)
            rec["config_result"] = code
        spent0, n0 = self.meter.snapshot()
        body = {"message": message, "model": self.args.model}
        if self.conv_id:
            body["conv_id"] = self.conv_id
        chunks = self.api.chat(body)
        fc = final_chunk(chunks)
        if fc.get("conv_id"):
            self.conv_id = fc["conv_id"]
        for c in chunks:
            if c.get("type") == "start" and c.get("conv_id"):
                self.conv_id = c["conv_id"]
        spent1, n1 = self.meter.snapshot()
        calls = self.meter.calls[n0:n1]
        outcome, why = judge(chunks, fc)
        rec.update(outcome=outcome, why=why, chunk_types=[c.get("type") for c in chunks][:12] + (["..."] if len(chunks) > 12 else []),
                   final_type=fc.get("type"), response_excerpt=text_of(chunks)[:400],
                   provider_calls=len(calls),
                   tokens={"prompt": sum(c.get("prompt_tokens") or 0 for c in calls),
                           "completion": sum(c.get("completion_tokens") or 0 for c in calls)},
                   cost=round(spent1 - spent0, 8), cost_sources=sorted({c["cost_source"] for c in calls}),
                   provider_status=[c["status"] for c in calls])
        if any(c["status"] not in (200, None) for c in calls) and outcome == "works":
            rec["outcome"] = "partially"
        self.steps.append(rec)
        return rec, chunks

    # judges -----------------------------------------------------------------------------------------
    @staticmethod
    def j_message(chunks, fc):
        if fc.get("type") == "done" and text_of(chunks).strip():
            return "works", "streamed deltas + done" if any(c.get("type") == "delta" for c in chunks) else "done without deltas (not streamed)"
        return "doesn't", f"final chunk {fc.get('type')}: {str(fc.get('message') or fc.get('body') or '')[:200]}"

    @staticmethod
    def j_proposal(action_hint):
        def j(chunks, fc):
            if fc.get("type") != "action_proposal":
                return "doesn't", f"expected action_proposal, got {fc.get('type')}"
            p = fc.get("proposal") or {}
            act = p.get("action") or ""
            ok = (action_hint in act) if action_hint else True
            return ("works" if ok else "partially"), f"proposal action={act!r} kind={p.get('kind')!r} status={p.get('status')!r}"
        return j

    @staticmethod
    def j_result(chunks, fc):
        if fc.get("type") == "action_result" and fc.get("proposal_status") == "executed" and str(fc.get("text") or "").strip():
            return "works", "action_result, proposal executed"
        return "doesn't", f"final {fc.get('type')} proposal_status={fc.get('proposal_status')!r} text={str(fc.get('text') or '')[:160]!r}"


def graph_summary(api: Api) -> dict:
    out = {}
    for key, path in (("nodes", "/api/graph/nodes?limit=2000"), ("edges", "/api/graph/edges?limit=5000")):
        code, data = api.req("GET", path)
        rows = data if isinstance(data, list) else (data or {}).get(key) or (data or {}).get("items") or []
        out[key] = rows if code == 200 else {"error": code, "body": data}
    return out


def run_scenario(r: Runner) -> None:
    api = r.api
    # 1. normal streamed message (synthetic content)
    r.step("1 message (streamed)",
           "Fictional team Acme Widgets plans to ship version 2.0 on Friday if the tests pass. "
           "Anna owns testing, Bartek owns the release notes. Answer in two short sentences.",
           Runner.j_message, config={"intent_routing_method": "heuristic"})
    # 2. intent recognition with method model: a paraphrase the local lexicon does not know
    rec2, _ = r.step("2 intent recognition (method=model)", "Could you boil this chat down to a paragraph?",
                     Runner.j_proposal("summar"), config={"intent_routing_method": "model", "intent_routing_model": r.args.model})
    if rec2["final_type"] == "action_proposal":
        r.step("2b answer no (nothing runs, no cost)", "nie", lambda ch, fc: (
            ("works", "proposal rejected") if fc.get("proposal_status") == "rejected" else ("doesn't", f"final {fc.get('type')} {fc.get('proposal_status')!r}")))
    # 3. summarise this conversation -> yes
    rec3, _ = r.step("3a streść tę rozmowę (proposal)", "Streść tę rozmowę", Runner.j_proposal("summar"))
    if rec3["final_type"] == "action_proposal":
        r.step("3b tak (run)", "tak", Runner.j_result)
    # 4. translate
    rec4, _ = r.step("4a przetłumacz na niemiecki (proposal)", "Przetłumacz na niemiecki", Runner.j_proposal("translat"),
                     note="no element named: the router must take the last message / conversation")
    if rec4["final_type"] == "action_proposal":
        r.step("4b tak (run)", "tak", Runner.j_result)
    # 5. explain simpler
    rec5, _ = r.step("5a wytłumacz prościej (proposal)", "Wytłumacz prościej", Runner.j_proposal("explain"))
    if rec5["final_type"] == "action_proposal":
        r.step("5b tak (run)", "tak", Runner.j_result)
    elif rec5["final_type"] == "done":
        rec5["why"] += " (went to the model as an ordinary message: a command needs an element)"
        rec5b, _ = r.step("5c wytłumacz prościej tę rozmowę (proposal)", "Wytłumacz prościej tę rozmowę", Runner.j_proposal("explain"),
                          note="same command with the element named")
        if rec5b["final_type"] == "action_proposal":
            r.step("5d tak (run)", "tak", Runner.j_result)
    # 6. unknown command -> definition proposal -> yes
    rec6, _ = r.step("6a unknown command (definition proposal)", "Rozpisz tę wiadomość na kroki",
                     lambda ch, fc: (("works", f"definition proposal kind={(fc.get('proposal') or {}).get('kind')!r}")
                                     if fc.get("type") == "action_proposal" and "define" in (str((fc.get("proposal") or {}).get("kind")) + str((fc.get("proposal") or {}).get("action"))) else
                                     ("doesn't", f"final {fc.get('type')} kind={(fc.get('proposal') or {}).get('kind')!r}")))
    if rec6["final_type"] == "done":
        rec6["why"] += " (method=model classified it as a message: it went to the model, no definition was offered)"
        rec6, _ = r.step("6c same sentence with method=heuristic (definition proposal)", "Rozpisz tę wiadomość na kroki",
                         lambda ch, fc: (("works", "definition proposal") if fc.get("type") == "action_proposal"
                                         else ("doesn't", f"final {fc.get('type')}")),
                         note="different configuration, not a retry of the same call", config={"intent_routing_method": "heuristic"})
    if rec6["final_type"] == "action_proposal":
        r.step("6b tak (define + run)", "tak", Runner.j_result)
    # 7. a rule from chat (no model call expected: input is a rules.set command)
    rec7, _ = r.step("7a rule from chat (proposal)", "zawsze pytaj mnie przed summarise",
                     lambda ch, fc: (("works", f"proposal action={(fc.get('proposal') or {}).get('action')!r}")
                                     if fc.get("type") == "action_proposal" and "rules.set" in str((fc.get("proposal") or {}).get("action"))
                                     else ("doesn't", f"final {fc.get('type')} action={(fc.get('proposal') or {}).get('action')!r}")))
    if rec7["final_type"] == "action_proposal":
        r.step("7b tak (rule written)", "tak", lambda ch, fc: (("works", "executed") if fc.get("proposal_status") == "executed"
                                                                   else ("doesn't", f"final {fc.get('type')} {fc.get('proposal_status')!r}")))
        code, rules = api.req("POST", "/api/rules/list", {})
        r.steps[-1]["rules_list_status"] = code
        r.steps[-1]["rules_list_excerpt"] = json.dumps(rules, ensure_ascii=False)[:300]
    # 8. graph
    g = graph_summary(api)
    nodes, edges = g["nodes"], g["edges"]
    kinds: dict[str, int] = {}
    types: dict[str, int] = {}
    if isinstance(nodes, list):
        for n in nodes:
            kinds[str(n.get("kind"))] = kinds.get(str(n.get("kind")), 0) + 1
    if isinstance(edges, list):
        for e in edges:
            types[str(e.get("link_type") or e.get("type"))] = types.get(str(e.get("link_type") or e.get("type")), 0) + 1
    produced = [k for k in kinds if k in ("proposal", "run", "action_run", "content", "form", "action") or "run" in k or "proposal" in k]
    outcome = "works" if produced and types.get("derived_from") else ("partially" if kinds else "doesn't")
    r.steps.append({"step": "8 graph (nodes/links via API)", "request": {"GET": ["/api/graph/nodes", "/api/graph/edges"]},
                    "outcome": outcome, "why": f"node kinds={kinds}; link types={types}",
                    "response_excerpt": json.dumps(nodes[:6] if isinstance(nodes, list) else nodes, ensure_ascii=False)[:400],
                    "tokens": None, "cost": 0.0})


# ── main ──────────────────────────────────────────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--server", default=str(DEFAULT_SERVER), help="loom-server binary (dev build)")
    ap.add_argument("--model", default=DEFAULT_MODEL, help="OpenRouter model id (cheap chat model)")
    ap.add_argument("--base-url", default=UPSTREAM, help="provider base url the forwarder talks to")
    ap.add_argument("--api-key", default="proxy-injected", help="placeholder; a proxy injects the real key")
    ap.add_argument("--budget", type=float, default=0.50, help="hard total spend cap in USD (enforced by the forwarder)")
    ap.add_argument("--price-in", type=float, default=0.10, help="fallback USD per 1M input tokens (also worst case)")
    ap.add_argument("--price-out", type=float, default=0.40, help="fallback USD per 1M output tokens")
    ap.add_argument("--max-tokens", type=int, default=400, help="max_tokens configured for every call")
    ap.add_argument("--direct", action="store_true", help="no forwarder: the kernel calls --base-url directly (no hard cap!)")
    ap.add_argument("--mock", action="store_true", help="offline self-test: fake provider, no network, no money")
    ap.add_argument("--keep-data", action="store_true", help="keep the temp data dir")
    ap.add_argument("--out", default="", help="write the JSON record here (default: stdout summary only)")
    args = ap.parse_args()

    if not Path(args.server).exists():
        print(f"loom-server not found at {args.server}; build it: cd loom && cmake --preset dev -DLOOM_BUILD_SERVER=ON "
              "&& cmake --build --preset dev", file=sys.stderr)
        return 2
    tmp = Path(tempfile.mkdtemp(prefix="loom-real-smoke-"))
    data_dir = tmp / "data"
    data_dir.mkdir()
    meter = Meter(args.budget, args.price_in, args.price_out)
    fwd = fwd_thread = None
    if args.direct and not args.mock:
        base_url = args.base_url
    else:
        fport = free_port()
        fwd = make_forwarder(meter, args.base_url, fport, args.mock)
        fwd_thread = threading.Thread(target=fwd.serve_forever, daemon=True)
        fwd_thread.start()
        base_url = f"http://127.0.0.1:{fport}/api/v1"
    port = free_port()
    env = dict(os.environ)
    env["NO_PROXY"] = env["no_proxy"] = "127.0.0.1,localhost," + env.get("no_proxy", "")
    log = open(tmp / "server.log", "w")
    proc = subprocess.Popen([args.server, "--host", "127.0.0.1", "--port", str(port), "--data-dir", str(data_dir)],
                            stdout=log, stderr=subprocess.STDOUT, env=env)
    api = Api(f"http://127.0.0.1:{port}")
    result: dict = {"script_version": SCRIPT_VERSION, "model": args.model, "budget_usd": args.budget, "mock": args.mock,
                    "base_url_for_kernel": base_url, "provider_base_url": args.base_url, "direct": args.direct}
    rc = 0
    try:
        wait_ready(api, proc)
        code, _ = api.req("POST", "/api/secrets/api_key", {"value": args.api_key})
        result["secret_set"] = code
        cfg = {"base_url": base_url, "default_model": args.model, "max_tokens": args.max_tokens, "stream": True}
        code, resp = api.req("PATCH", "/api/config", cfg)
        result["config_set"] = {"status": code, "keys": sorted(cfg)}
        if code != 200:
            result["config_error"] = resp
        code, pol = api.req("POST", "/api/usage-policy", {"action": "settings"})
        result["usage_policy_settings_status"] = code
        runner = Runner(api, meter, args)
        run_scenario(runner)
        result["steps"] = runner.steps
        result["graph"] = {"nodes_total": len(graph_summary(api)["nodes"]) if isinstance(graph_summary(api)["nodes"], list) else None}
        code, eco = api.req("GET", "/api/economy/report")
        result["economy_report_status"] = code
    except Exception as exc:  # noqa: BLE001
        result["fatal"] = repr(exc)
        rc = 1
    finally:
        spent, ncalls = meter.snapshot()
        result["total_cost_usd"] = round(spent, 8)
        result["provider_calls"] = ncalls
        result["refused_by_budget"] = meter.refused
        result["calls"] = meter.calls
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
        if fwd:
            fwd.shutdown()
        log.close()
        result["server_log_tail"] = (tmp / "server.log").read_text(errors="replace")[-600:]
        if not args.keep_data:
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            result["data_dir"] = str(data_dir)
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).write_text(text)
    for s in result.get("steps", []):
        print(f"[{s['outcome']:9}] {s['step']:48} cost={s.get('cost')} {s.get('why', '')[:110]}")
    print(f"TOTAL cost {result['total_cost_usd']} USD of {args.budget} (provider calls {result['provider_calls']}, "
          f"refused {result['refused_by_budget']})" + (" [MOCK]" if args.mock else ""))
    if not args.out:
        print("(use --out FILE for the full JSON record)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
