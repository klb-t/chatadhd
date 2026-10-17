"""Model steps — one way in, cost always visible, "no" by default.

A model flow (``data/flows/*.json``, ``kind: model``) runs only when the run
enabled models explicitly (``--model`` / ``settings.flows.model.enabled``)
AND the estimate was accepted (``--tak`` or a "yes" to the question; the
question's default is "no").  The ×10 guard compares the estimate with the
last accepted run in ``<data dir>/extract/usage.jsonl``: more than ``ratio``
times that needs its own confirmation (``--zgoda-x10``); limits per run and per
day are settings (``0`` = no limit).  The route is ``provider`` (an explicit
OpenAI-compatible ``chat/completions`` endpoint: ``base_url``, ``model`` per
class, the key read from an environment variable or the app's
``secrets.json`` only after models were enabled) — ``loom_server`` is
described and refused with a reason in this slice.  Every call is logged
(flow, model, route, tokens, cost, input/output hashes — never the key, never
the text) and cached by (flow, model, input hash): a replay never pays twice
and, with ``replay_only``, never calls at all.
"""
import hashlib
import json
import logging
import os
import re
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Optional

from .i18n import label, language

log = logging.getLogger(__name__)


class ModelRefused(Exception):
    """A paid step was refused (off, estimate not accepted, ×10, limit, no route): exit code 5."""

    def __init__(self, reason: str, *, estimate: Optional[dict[str, Any]] = None, question: bool = False) -> None:
        super().__init__(reason)
        self.reason = reason
        self.estimate = estimate
        self.question = question


def _now_iso() -> str:
    sde = os.environ.get("SOURCE_DATE_EPOCH")
    dt = datetime.fromtimestamp(int(sde), timezone.utc) if sde and sde.isdigit() else datetime.now(timezone.utc)
    return dt.isoformat(timespec="seconds").replace("+00:00", "Z")


def _strip_fences(text: str) -> str:
    m = re.search(r"```(?:json)?\s*(.*?)```", text, re.S)
    return (m.group(1) if m else text).strip()


class ModelRunner:
    def __init__(self, eng: Any, *, ask: Optional[Callable[[dict[str, Any]], bool]] = None, cache_dir: Optional[Path] = None) -> None:
        self.eng = eng
        self.ds = eng.ds
        base = dict(self.ds.preset("flows.model", {}) or {})
        over = ((eng.settings.get("flows") or {}).get("model") or {})
        self.cfg = _merge(base, over)
        self.enabled = bool(self.cfg.get("enabled"))
        self.accept = bool(self.cfg.get("accept"))
        self.accept_x10 = bool(self.cfg.get("accept_x10"))
        self.replay_only = bool(self.cfg.get("replay_only"))
        self.ask = ask
        self.cache_dir = cache_dir
        self.calls: list[dict[str, Any]] = []
        self.authorized: Optional[dict[str, Any]] = None
        self.flows = self.ds.entries("flows")

    # -- configuration ---------------------------------------------------------------
    def flow(self, flow_id: str) -> dict[str, Any]:
        f = self.flows.get(flow_id)
        if f is None:
            raise ModelRefused(f"unknown flow {flow_id!r}")
        if f.get("kind") != "model":
            raise ModelRefused(f"flow {flow_id!r} is not a model flow")
        return f

    def model_for(self, flow: dict[str, Any]) -> str:
        m = (flow.get("model") or {})
        if m.get("model"):
            return str(m["model"])
        cls = m.get("class") or flow.get("class") or "cheap"
        model = (self.cfg.get("classes") or {}).get(cls)
        if not model:
            raise ModelRefused(f"no model for class {cls!r}: set settings.flows.model.classes.{cls} (a model is never inherited from the chat)")
        return str(model)

    def price(self, model: str) -> Optional[dict[str, Any]]:
        try:
            prices = self.ds.file("prices.json")
        except Exception:  # noqa: BLE001 - a missing price table means "unknown", never zero
            log.debug("prices.json unavailable", exc_info=True)
            prices = {}
        p = (prices.get("models") or {}).get(model)
        if p:
            return {"in": float(p["input_per_mtok"]), "out": float(p["output_per_mtok"]), "currency": prices.get("currency", "USD"),
                    "source": f"prices.json ({p.get('as_of') or prices.get('as_of') or '?'})"}
        app = self._app_models_price(model)
        return app

    def _app_models_price(self, model: str) -> Optional[dict[str, Any]]:
        from .paths import app_data_dir
        d = app_data_dir()
        if d is None or not (d / "models.json").exists():
            return None
        try:
            data = json.loads((d / "models.json").read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            log.debug("models.json unreadable", exc_info=True)
            return None
        items = data.get("models") if isinstance(data, dict) else data
        for m in items or []:
            if isinstance(m, dict) and m.get("id") == model and isinstance(m.get("pricing"), dict):
                pr = m["pricing"]
                try:  # OpenRouter pricing is USD per token (strings)
                    return {"in": float(pr.get("prompt") or 0) * 1e6, "out": float(pr.get("completion") or 0) * 1e6, "currency": "USD",
                            "source": "app models.json (per token × 1e6)"}
                except (TypeError, ValueError):
                    return None
        return None

    # -- estimate and consent ---------------------------------------------------------------
    def _cpt(self) -> float:
        cpt = self.ds.preset("tokens.chars_per_token", {}) or {}
        return float(cpt.get(language(), cpt.get("default", 3.6)) or 3.6)

    def estimate(self, work: dict[str, dict[str, Any]]) -> dict[str, Any]:
        """``work``: flow id → {"calls": n, "chars": total input chars} → per flow and total estimate."""
        cpt = self._cpt()
        out: dict[str, Any] = {"flows": {}, "total": {"amount": 0.0, "currency": "USD", "known": True}, "calls": 0}
        for fid, w in sorted(work.items()):
            f = self.flow(fid)
            try:
                model = self.model_for(f)
            except ModelRefused as exc:
                out["flows"][fid] = {"calls": w.get("calls", 0), "model": None, "note": exc.reason,
                                     "cost": {"amount": None, "currency": "USD", "known": False}}
                out["total"]["known"] = False
                out["calls"] += int(w.get("calls", 0))
                continue
            prompt = f.get("prompt") or {}
            overhead = len(label(prompt.get("system"))) + len(label(prompt.get("user")))
            calls = int(w.get("calls", 0))
            tin = int((int(w.get("chars", 0)) + calls * overhead) / cpt)
            tout = calls * int(((f.get("model") or {}).get("parameters") or {}).get("max_tokens", 300))
            pr = self.price(model)
            cost = {"amount": None, "currency": "USD", "known": False}
            if pr is not None:
                cost = {"amount": round((tin * pr["in"] + tout * pr["out"]) / 1e6, 6), "currency": pr["currency"], "known": True,
                        "price_source": pr["source"]}
                out["total"]["amount"] = round(out["total"]["amount"] + cost["amount"], 6)
            else:
                out["total"]["known"] = False
            out["flows"][fid] = {"calls": calls, "model": model, "tokens_in_est": tin, "tokens_out_max": tout, "cost": cost}
            out["calls"] += calls
        if not out["total"]["known"]:
            out["total"]["amount"] = None
        out["tokens_note"] = f"local estimate: {cpt} chars per token; output = max_tokens per call (upper bound)"
        return out

    def _usage_path(self) -> Optional[Path]:
        p = self.cfg.get("usage_file")
        if p:
            return Path(str(p)).expanduser()
        return (self.ds.overlay / "usage.jsonl") if self.ds.overlay is not None else None

    def _history(self) -> list[dict[str, Any]]:
        p = self._usage_path()
        if p is None or not p.exists():
            return []
        rows = []
        for line in p.read_text(encoding="utf-8").splitlines():
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                log.debug("usage.jsonl: bad line skipped", exc_info=True)
        return rows

    @staticmethod
    def _volume(est: dict[str, Any]) -> dict[str, int]:
        """Size of a run independent of prices: calls and estimated tokens (input estimate + output upper bound)."""
        tokens = sum(int(f.get("tokens_in_est") or 0) + int(f.get("tokens_out_max") or 0) for f in (est.get("flows") or {}).values())
        return {"calls": int(est.get("calls") or 0), "tokens": tokens}

    def _provider_check(self) -> None:
        """A provider host or key named by a recipe / task file / manifest (not by the user's config or a face flag)
        is used only after a yes/no that shows the host; otherwise nothing is sent."""
        data = getattr(self.eng, "data_provider", None)
        if not data:
            return
        from urllib.parse import urlparse
        host = urlparse(str(data.get("base_url") or (self.cfg.get("provider") or {}).get("base_url") or "")).hostname or "?"
        q = {"kind": "provider", "host": host, "keys": sorted(data)}
        if not (self.ask and self.ask(q)):
            raise ModelRefused(f"the recipe names a provider the user did not configure (host {host}, {', '.join(sorted(data))}); "
                               f"confirm by answering yes or pass --dostawca", question=True)
        self.cfg["provider"] = {**(self.cfg.get("provider") or {}), **data}
        self.eng.data_provider = None

    def authorize(self, est: dict[str, Any]) -> dict[str, Any]:
        """Raise ModelRefused unless the run may spend ``est``; returns the decision record."""
        if est.get("calls", 0) == 0:
            return {"accepted": True, "reason": "nothing to call"}
        if self.replay_only:
            # nothing is sent and nothing is paid: answers come from the cache or the step stops (reproduction)
            rec = {"at": _now_iso(), "accepted": True, "replay_only": True, "calls": est.get("calls")}
            self.authorized = rec
            return rec
        if not self.enabled:
            raise ModelRefused("model steps are off (enable explicitly with --model)", estimate=est)
        self._provider_check()
        guard = self.cfg.get("guard") or {}
        amount = (est.get("total") or {}).get("amount")
        known = bool((est.get("total") or {}).get("known"))
        per_run = float(guard.get("limit_per_run") or 0)
        per_day = float(guard.get("limit_per_day") or 0)
        if (per_run or per_day) and not known:
            raise ModelRefused("a cost limit is set but the price is unknown (add the model to prices.json)", estimate=est)
        if per_run and amount is not None and amount > per_run:
            raise ModelRefused(f"estimate {amount} exceeds limit_per_run {per_run}", estimate=est)
        hist = self._history()
        if per_day and amount is not None:
            today = _now_iso()[:10]
            spent = sum(float((h.get("estimate") or {}).get("amount") or 0) for h in hist if str(h.get("at", ""))[:10] == today and h.get("accepted"))
            if spent + amount > per_day:
                raise ModelRefused(f"estimate {amount} + {round(spent, 6)} spent today exceeds limit_per_day {per_day}", estimate=est)
        ratio = float(guard.get("ratio") or 0)
        prev = next((h for h in reversed(hist) if h.get("accepted") and not h.get("replay_only")), None)
        last = float(((prev or {}).get("estimate") or {}).get("amount") or 0)
        vol = self._volume(est)
        x10 = bool(ratio and known and last > 0 and amount is not None and amount > ratio * last)
        basis = "amount"
        if ratio and not x10 and prev is not None and (not known or not last):
            # the price is unknown (prices.json ships empty): the guard compares the size of the run instead —
            # estimated tokens, else the number of calls — with the last accepted run
            pv = prev.get("volume") or {}
            if int(pv.get("tokens") or 0) > 0 and vol["tokens"] > ratio * int(pv["tokens"]):
                x10, basis, last = True, "tokens", int(pv["tokens"])
            elif int(pv.get("calls") or prev.get("calls") or 0) > 0 and vol["calls"] > ratio * int(pv.get("calls") or prev.get("calls") or 0):
                x10, basis, last = True, "calls", int(pv.get("calls") or prev.get("calls") or 0)
        if x10 and not self.accept_x10:
            q = {"kind": "x10", "estimate": est, "last": last, "ratio": ratio, "basis": basis}
            if not (self.ask and self.ask(q)):
                what = {"amount": f"estimate {amount}", "tokens": f"{vol['tokens']} estimated tokens",
                        "calls": f"{vol['calls']} calls"}[basis]
                raise ModelRefused(f"{what} is more than {ratio}× the last accepted run ({last}); confirm with --zgoda-x10",
                                   estimate=est, question=True)
        if not self.accept:
            q = {"kind": "estimate", "estimate": est}
            if not (self.ask and self.ask(q)):
                raise ModelRefused("estimate not accepted (answer 'yes' or pass --tak)", estimate=est, question=True)
        rec = {"at": _now_iso(), "estimate": est.get("total"), "calls": est.get("calls"), "volume": vol, "accepted": True, "x10": x10}
        self._usage_append(rec)
        self.authorized = rec
        self.eng.emit("model.authorized", estimate=est.get("total"), calls=est.get("calls"))
        return rec

    def _usage_append(self, rec: dict[str, Any]) -> None:
        p = self._usage_path()
        if p is None:
            return
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "a", encoding="utf-8") as fh:
            fh.write(json.dumps(rec, ensure_ascii=False) + "\n")

    # -- calls -------------------------------------------------------------------------------
    def _key(self, route: dict[str, Any]) -> Optional[str]:
        env = route.get("api_key_env")
        if env and os.environ.get(env):
            return os.environ[env]
        name = route.get("secret")
        if name:
            from .paths import app_data_dir
            d = app_data_dir()
            if d is not None and (d / "secrets.json").exists():
                try:
                    return json.loads((d / "secrets.json").read_text(encoding="utf-8")).get(name)
                except (OSError, json.JSONDecodeError):
                    log.debug("secrets.json unreadable", exc_info=True)
        return None

    def call(self, flow_id: str, variables: dict[str, Any]) -> dict[str, Any]:
        if self.authorized is None:
            raise ModelRefused("model call before an accepted estimate (authorize first)")
        f = self.flow(flow_id)
        model = self.model_for(f)
        prompt = f.get("prompt") or {}
        system = _fill(label(prompt.get("system")), variables)
        user = _fill(label(prompt.get("user")), variables)
        params = ((f.get("model") or {}).get("parameters") or {})
        key_src = json.dumps({"flow": flow_id, "model": model, "system": system, "user": user, "params": params}, sort_keys=True, ensure_ascii=False)
        h = hashlib.sha256(key_src.encode("utf-8")).hexdigest()
        cached = self._cache_get(h)
        route_name = self.cfg.get("route", "provider")
        entry = {"flow": flow_id, "model": model, "route": route_name, "input_sha256": h}
        if cached is not None:
            entry.update({"cache": True, "tokens_in": cached.get("tokens_in", 0), "tokens_out": cached.get("tokens_out", 0),
                          "cost": {"amount": 0.0, "currency": "USD", "known": True}, "output_sha256": cached.get("output_sha256")})
            self.calls.append(entry)
            return cached["parsed"]
        if self.replay_only:
            raise ModelRefused(f"replay: no cached answer for {flow_id} (input {h[:12]}); run with models enabled to fill the cache",
                               question=True)
        if route_name != "provider":
            raise ModelRefused(f"route {route_name!r} is not available in this slice (use route 'provider')")
        route = dict(self.cfg.get("provider") or {})
        base_url = route.get("base_url")
        if not base_url:
            raise ModelRefused("route 'provider' needs settings.flows.model.provider.base_url")
        body = {"model": model, "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}], **params}
        req = urllib.request.Request(base_url.rstrip("/") + "/chat/completions", data=json.dumps(body).encode("utf-8"),
                                     headers={"Content-Type": "application/json"}, method="POST")
        key = self._key(route)
        if key:
            req.add_header("Authorization", f"Bearer {key}")
        t0 = time.time()
        try:
            with urllib.request.urlopen(req, timeout=float(route.get("timeout", 120))) as resp:
                data = json.loads(resp.read().decode("utf-8"))
        except (OSError, ValueError) as exc:  # URLError/HTTPError are OSError; a body that is not JSON is ValueError
            err = f"{type(exc).__name__}: {exc}"
            entry.update({"cache": False, "error": err, "cost": {"amount": None, "currency": "USD", "known": False},
                          "seconds": round(time.time() - t0, 3)})
            self.calls.append(entry)
            self._usage_append({"at": _now_iso(), "flow": flow_id, "model": model, "error": err, "accepted": False,
                                "cost": {"amount": None, "known": False}})
            log.warning("model call failed: %s", err)
            log.debug("model call failure", exc_info=True)
            raise ModelRefused(f"the provider call failed ({err}); nothing was cached, run again when it works") from exc
        text = (((data.get("choices") or [{}])[0].get("message") or {}).get("content")) or ""
        usage = data.get("usage") or {}
        tin, tout = int(usage.get("prompt_tokens") or 0), int(usage.get("completion_tokens") or 0)
        try:
            parsed = json.loads(_strip_fences(text))
        except json.JSONDecodeError:
            parsed = {"raw": text, "parse_error": True}
        pr = self.price(model)
        cost = {"amount": round((tin * pr["in"] + tout * pr["out"]) / 1e6, 6), "currency": pr["currency"], "known": True} if pr else \
            {"amount": None, "currency": "USD", "known": False}
        out_h = hashlib.sha256(text.encode("utf-8")).hexdigest()
        entry.update({"cache": False, "tokens_in": tin, "tokens_out": tout, "cost": cost, "output_sha256": out_h,
                      "seconds": round(time.time() - t0, 3)})
        self.calls.append(entry)
        self._cache_put(h, {"parsed": parsed, "tokens_in": tin, "tokens_out": tout, "output_sha256": out_h, "model": model, "flow": flow_id})
        return parsed

    def _cache_get(self, h: str) -> Optional[dict[str, Any]]:
        if self.cache_dir is None:
            return getattr(self, "_mem", {}).get(h)
        p = self.cache_dir / "model" / f"{h}.json"
        if p.exists():
            return json.loads(p.read_text(encoding="utf-8"))
        return None

    def _cache_put(self, h: str, rec: dict[str, Any]) -> None:
        if self.cache_dir is None:
            if not hasattr(self, "_mem"):
                self._mem: dict[str, Any] = {}
            self._mem[h] = rec
            return
        p = self.cache_dir / "model" / f"{h}.json"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(rec, ensure_ascii=False), encoding="utf-8")

    # -- summaries for the manifest ---------------------------------------------------------------
    def summary(self) -> tuple[list[dict[str, Any]], dict[str, Any]]:
        by: dict[tuple[str, str], dict[str, Any]] = {}
        total = 0.0
        known = True
        for c in self.calls:
            k = (c["flow"], c["model"])
            s = by.setdefault(k, {"flow": c["flow"], "model": c["model"], "route": c["route"], "calls": 0, "cache_hits": 0,
                                  "tokens_in": 0, "tokens_out": 0, "cost": {"amount": 0.0, "currency": "USD", "known": True}})
            s["calls"] += 1
            s["cache_hits"] += int(bool(c.get("cache")))
            s["tokens_in"] += int(c.get("tokens_in") or 0)
            s["tokens_out"] += int(c.get("tokens_out") or 0)
            amt = (c.get("cost") or {}).get("amount")
            if amt is None:
                s["cost"]["known"] = False
                known = False
            else:
                s["cost"]["amount"] = round(s["cost"]["amount"] + amt, 6)
                total += amt
        rows = [by[k] for k in sorted(by)]
        return rows, {"amount": round(total, 6) if known else None, "currency": "USD", "known": known}

    def log_rows(self) -> list[dict[str, Any]]:
        return [{k: v for k, v in c.items() if k != "seconds"} for c in self.calls]


def _fill(template: str, variables: dict[str, Any]) -> str:
    """``{name}`` placeholders only (prompts also contain literal JSON braces)."""
    return re.sub(r"\{([a-z_]+)\}", lambda m: str(variables[m.group(1)]) if m.group(1) in variables else m.group(0), template)


def _merge(a: Any, b: Any) -> Any:
    if isinstance(a, dict) and isinstance(b, dict):
        out = dict(a)
        for k, v in b.items():
            out[k] = _merge(a.get(k), v) if k in a else v
        return out
    return b


def verdict_outcome(parsed: dict[str, Any]) -> tuple[Optional[str], float, str]:
    v = str(parsed.get("verdict") or "").lower()
    if v not in ("include", "exclude", "ambiguous"):
        return None, 0.0, f"unparsed model answer ({'parse_error' if parsed.get('parse_error') else 'no verdict'})"
    try:
        conf = max(0.0, min(1.0, float(parsed.get("confidence", 0.5))))
    except (TypeError, ValueError):
        conf = 0.5
    return v, conf, str(parsed.get("reason") or "")[:300]
