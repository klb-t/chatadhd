"""Prices as data: ``data/prices/*.json`` (model pattern → per-million input / output / cache write / cache read,
currency, ``valid_from``, source), the legacy ``data/prices.json`` (exact model id) and the app's ``models.json``
(``pricing``, USD per token).  One lookup for every caller (flows, token economics).  Nothing is invented: an entry
without a price, a model no entry matches, a price not yet valid on the day — each is ``known: false`` with a reason,
and a cost computed from it is "unknown (no price)", never zero.
"""
import fnmatch
import json
import logging
from typing import Any, Optional

from .datafiles import DataStore, store as default_store
from .i18n import label

log = logging.getLogger(__name__)

KINDS = ("input", "output", "cache_write", "cache_write_1h", "cache_read")
# usage counter → price field (cache_creation is written at the 5-minute price unless split into 1 h)
USAGE_PRICE = {"input": "input", "output": "output", "cache_creation": "cache_write", "cache_read": "cache_read"}


def _specificity(pattern: str) -> int:
    return sum(1 for ch in pattern if ch not in "*?[]")


def _num(v: Any) -> Optional[float]:
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None


class PriceBook:
    def __init__(self, ds: Optional[DataStore] = None, *, app_models: bool = True) -> None:
        self.ds = ds or default_store()
        self.entries: list[dict[str, Any]] = []
        try:
            files = self.ds.entries("prices")
        except Exception:  # noqa: BLE001 - a broken price file is said, and every price is then unknown
            log.warning("prices/*.json unreadable", exc_info=True)
            files = {}
        for fid, doc in sorted(files.items()):
            for i, e in enumerate(doc.get("entries") or []):
                if not isinstance(e, dict) or not e.get("pattern"):
                    continue
                self.entries.append({**e, "currency": e.get("currency") or doc.get("currency") or "USD",
                                     "file": f"prices/{fid}.json#{i}", "origin": doc.get("_source", "preset")})
        try:
            legacy = self.ds.file("prices.json")
        except Exception:  # noqa: BLE001 - the legacy table is optional
            log.debug("prices.json unavailable", exc_info=True)
            legacy = {}
        for mid, p in sorted(((legacy or {}).get("models") or {}).items()):
            if isinstance(p, dict):
                self.entries.append({"pattern": mid, "input": p.get("input_per_mtok"), "output": p.get("output_per_mtok"),
                                     "cache_write": p.get("cache_write_per_mtok"), "cache_read": p.get("cache_read_per_mtok"),
                                     "valid_from": p.get("as_of") or legacy.get("as_of"), "source": p.get("source") or "prices.json",
                                     "currency": legacy.get("currency", "USD"), "file": "prices.json", "origin": legacy.get("_source")})
        self.app_models = app_models
        self._app: Optional[dict[str, dict[str, Any]]] = None

    # -- the app's model registry (OpenRouter pricing: USD per token, strings) ----------------------------------
    def _app_prices(self) -> dict[str, dict[str, Any]]:
        if self._app is not None:
            return self._app
        self._app = {}
        if not self.app_models:
            return self._app
        from .paths import app_data_dir
        d = app_data_dir()
        if d is None or not (d / "models.json").exists():
            return self._app
        try:
            data = json.loads((d / "models.json").read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            log.debug("models.json unreadable", exc_info=True)
            return self._app
        items = data.get("models") if isinstance(data, dict) else data
        for m in items or []:
            if not (isinstance(m, dict) and m.get("id") and isinstance(m.get("pricing"), dict)):
                continue
            pr = m["pricing"]

            def per_m(key: str) -> Optional[float]:
                try:
                    return float(pr[key]) * 1e6 if pr.get(key) not in (None, "") else None
                except (TypeError, ValueError):
                    log.debug("models.json pricing %s of %s unreadable", key, m.get("id"), exc_info=True)
                    return None
            self._app[str(m["id"])] = {"input": per_m("prompt"), "output": per_m("completion"),
                                       "cache_read": per_m("input_cache_read"), "cache_write": per_m("input_cache_write"),
                                       "currency": "USD", "valid_from": None, "source": "app models.json (per token × 1e6)",
                                       "pattern": str(m["id"]), "file": "models.json"}
        return self._app

    # -- lookup -------------------------------------------------------------------------------------------------
    def lookup(self, model: Optional[str], day: Optional[str] = None) -> dict[str, Any]:
        """The price of ``model`` on ``day`` (ISO date; None = latest).  ``known`` is true when at least one price
        field is a number; missing fields stay None (a cost that needs them is unknown, with the field named)."""
        if not model:
            return {"known": False, "reason": "no_model", "model": model}
        names = [model]
        if "/" in model:  # "anthropic/claude-…" (OpenRouter) also matches "claude-…"
            names.append(model.split("/", 1)[1])
        app = self._app_prices().get(model)
        cands: list[dict[str, Any]] = []
        for e in self.entries:
            pat = str(e["pattern"]).lower()
            if any(fnmatch.fnmatchcase(n.lower(), pat) for n in names):
                cands.append(e)
        if not cands and app is None:
            return {"known": False, "reason": "no_entry", "model": model}
        if cands:
            best = max(_specificity(str(e["pattern"])) for e in cands)
            cands = [e for e in cands if _specificity(str(e["pattern"])) == best]
            dated = [e for e in cands if e.get("valid_from")]
            undated = [e for e in cands if not e.get("valid_from")]
            pick: Optional[dict[str, Any]] = None
            if dated:
                ok = sorted((e for e in dated if day is None or str(e["valid_from"])[:10] <= day[:10]), key=lambda e: str(e["valid_from"]))
                if ok:
                    pick = ok[-1]
                elif not undated:
                    first = min(str(e["valid_from"]) for e in dated)
                    return {"known": False, "reason": "not_yet_valid", "model": model, "valid_from": first, "pattern": cands[0]["pattern"]}
            if pick is None and undated:
                pick = undated[-1]
            if pick is not None:
                prices = {k: _num(pick.get(k)) for k in KINDS}
                if any(v is not None for v in prices.values()):
                    return {"known": True, "model": model, "prices": prices, "currency": pick.get("currency", "USD"),
                            "valid_from": pick.get("valid_from"), "source": pick.get("source"), "pattern": pick["pattern"],
                            "file": pick.get("file")}
                if app is None:
                    return {"known": False, "reason": "entry_without_price", "model": model, "pattern": pick["pattern"],
                            "file": pick.get("file"), "why": label(pick.get("unknown_reason")) if pick.get("unknown_reason") else None}
        if app is not None:
            return {"known": True, "model": model, "prices": {k: app.get(k) for k in KINDS}, "currency": "USD",
                    "valid_from": None, "source": app["source"], "pattern": app["pattern"], "file": app["file"]}
        return {"known": False, "reason": "no_entry", "model": model}


def cost_of(tokens: dict[str, int], hit: dict[str, Any]) -> dict[str, Any]:
    """Cost of measured or estimated ``tokens`` (keys input / output / cache_creation / cache_creation_1h / cache_read)
    at the price ``hit``.  Every kind with tokens > 0 needs its price; a missing one makes the cost unknown and is named."""
    if not hit.get("known"):
        return {"amount": None, "known": False, "reason": hit.get("reason", "no_entry"), "missing": []}
    prices = hit["prices"]
    amount = 0.0
    missing: list[str] = []
    one_h = int(tokens.get("cache_creation_1h") or 0)
    for kind, field in USAGE_PRICE.items():
        n = int(tokens.get(kind) or 0)
        if kind == "cache_creation" and one_h:
            p1 = prices.get("cache_write_1h")
            if p1 is None:
                missing.append("cache_write_1h")
            else:
                amount += one_h * p1 / 1e6
            n = max(n - one_h, 0)
        if n <= 0:
            continue
        p = prices.get(field)
        if p is None:
            missing.append(field)
            continue
        amount += n * p / 1e6
    if missing:
        return {"amount": None, "known": False, "reason": "price_field_missing", "missing": sorted(set(missing)),
                "currency": hit.get("currency", "USD")}
    return {"amount": round(amount, 6), "known": True, "currency": hit.get("currency", "USD"), "source": hit.get("source"),
            "valid_from": hit.get("valid_from")}
