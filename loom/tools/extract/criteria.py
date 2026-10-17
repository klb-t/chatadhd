"""Criteria (``criteria/*.json``): keywords with inflection, regexes,
exclusions, masks, vector queries, grain/expand/cascade.  Loaded from a
preset id, a file path or an inline object; quick criteria can be built from
bare terms (``--slowa``).  Masks are the L0 level: they exclude or pass,
never include, and every exclusion carries a reason."""
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

from .datafiles import DataFileError, DataStore, read_json, store as default_store
from .lexical import Keyword, Lexicon, compile_keyword, compile_regexes, lexicon as default_lexicon
from .model import Unit
from . import timestamps as ts

SCHEMA = "loom.extract.criteria/1"

MASK_DEFAULTS: dict[str, Any] = {"date": {"from": None, "to": None}, "services": [], "accounts": [], "roles": [], "tools": [],
                                 "title_regex": None, "with_attachments": None, "with_branches": None, "agentic_only": False,
                                 "languages": [], "models": [], "min_messages": 0, "max_messages": 0, "min_chars": 0, "max_chars": 0,
                                 "kinds": [], "formats": []}


@dataclass
class Criteria:
    id: str
    raw: dict[str, Any]
    keywords: list[Keyword]
    regexes: list[tuple[re.Pattern, float, str]]
    exclude_keywords: list[Keyword]
    exclude_regexes: list[tuple[re.Pattern, float, str]]
    masks: dict[str, Any]
    vectors: list[dict[str, Any]]
    clusters: list[str]
    grain: str
    expand: str
    cascade: str
    flows: dict[str, Any]
    weights: dict[str, float]
    language: str = "pl"
    description: str = ""
    source: str = "preset"
    terms: list[str] = field(default_factory=list)

    def summary(self) -> str:
        return ", ".join(self.terms[:12]) + (" …" if len(self.terms) > 12 else "")

    def as_dict(self) -> dict[str, Any]:
        return dict(self.raw, id=self.id, _source=self.source)


def _normalise(obj: dict[str, Any], ds: DataStore) -> dict[str, Any]:
    out = dict(obj)
    out.setdefault("schema", SCHEMA)
    out.setdefault("keywords", [])
    out.setdefault("regex", [])
    out.setdefault("exclude", {"keywords": [], "regex": []})
    out["exclude"].setdefault("keywords", [])
    out["exclude"].setdefault("regex", [])
    masks = dict(MASK_DEFAULTS)
    masks.update(out.get("masks") or {})
    out["masks"] = masks
    out.setdefault("vectors", [])
    out.setdefault("clusters", [])
    out.setdefault("grain", ds.preset("grain", "conversation"))
    out.setdefault("expand", ds.preset("expand", "conversation"))
    out.setdefault("cascade", ds.preset("cascade", "default"))
    out.setdefault("flows", ds.preset("flows", {}))
    out.setdefault("weights", {"lexical": 1.0, "vectors": 1.0, "cheap": 1.5, "strong": 3.0})
    out.setdefault("language", ds.preset("language", "pl"))
    return out


def build_criteria(obj: dict[str, Any], ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None,
                   *, source: str = "inline") -> Criteria:
    ds = ds or default_store()
    lex = lex or default_lexicon()
    raw = _normalise(obj, ds)
    lexd = ds.preset("lexical", {}) or {}
    kws = [compile_keyword(k, lex, lexd) for k in raw["keywords"]]
    exk = [compile_keyword(k, lex, lexd) for k in raw["exclude"]["keywords"]]
    try:
        rxs = compile_regexes(raw["regex"])
        exr = compile_regexes(raw["exclude"]["regex"])
    except re.error as exc:
        raise DataFileError(f"criteria {raw.get('id')}: invalid regex: {exc}") from None
    if raw["masks"].get("title_regex"):
        try:
            re.compile(raw["masks"]["title_regex"])
        except re.error as exc:
            raise DataFileError(f"criteria {raw.get('id')}: invalid title_regex: {exc}") from None
    terms = [k.term for k in kws] + [r[2] for r in rxs]
    return Criteria(id=str(raw.get("id") or "inline"), raw=raw, keywords=kws, regexes=rxs, exclude_keywords=exk,
                    exclude_regexes=exr, masks=raw["masks"], vectors=list(raw["vectors"]), clusters=list(raw["clusters"]),
                    grain=str(raw["grain"]), expand=str(raw["expand"]), cascade=str(raw["cascade"]), flows=dict(raw["flows"]),
                    weights={k: float(v) for k, v in raw["weights"].items()}, language=str(raw["language"]),
                    description=str(raw.get("description") or ""), source=source, terms=terms)


def load_criteria(ident: Any, ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None,
                  *, overrides: Optional[dict[str, Any]] = None) -> Criteria:
    """``ident``: preset id, path to a JSON file, or a dict."""
    ds = ds or default_store()
    if isinstance(ident, dict):
        obj, source = dict(ident), "inline"
    elif isinstance(ident, (str, Path)) and (str(ident).endswith(".json") or Path(str(ident)).exists()):
        obj, source = read_json(Path(str(ident))), f"file:{ident}"
    else:
        entry = ds.entry("criteria", str(ident))
        obj, source = {k: v for k, v in entry.items() if not k.startswith("_")}, entry.get("_source", "preset")
    if overrides:
        obj = _merge_overrides(obj, overrides)
    return build_criteria(obj, ds, lex, source=source)


def criteria_from_terms(terms: list[str], ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None,
                        *, regexes: Optional[list[str]] = None, ident: str = "terms") -> Criteria:
    """Quick criteria from CLI words: every term is a keyword with rule inflection; a term like /re/ is a regex."""
    kws: list[Any] = []
    rxs: list[Any] = list(regexes or [])
    for t in terms:
        if len(t) >= 2 and t.startswith("/") and t.endswith("/"):
            rxs.append(t[1:-1])
        else:
            kws.append({"term": t, "inflect": "rules", "weight": 1.0})
    return build_criteria({"id": ident, "keywords": kws, "regex": rxs,
                           "vectors": [{"query": " ".join(t for t in terms if not t.startswith("/")), "grain": "window"}] if terms else []},
                          ds, lex, source="terms")


def _merge_overrides(obj: dict[str, Any], over: dict[str, Any]) -> dict[str, Any]:
    out = json.loads(json.dumps(obj))
    for k, v in over.items():
        if k == "masks" and isinstance(v, dict):
            out.setdefault("masks", {}).update(v)
        elif k in ("keywords", "regex") and isinstance(v, list):
            out.setdefault(k, []).extend(v)
        else:
            out[k] = v
    return out


# ───────────────────────────── masks (L0) ─────────────────────────────
def mask_check(unit: Unit, masks: dict[str, Any], *, tolerance_hours: float = 14.0) -> tuple[bool, list[str]]:
    """(passes, reasons): a mask excludes or passes; every exclusion names the mask."""
    reasons: list[str] = []
    m = dict(MASK_DEFAULTS)
    m.update(masks or {})
    st = unit.stats or {}
    d = m.get("date") or {}
    if d.get("from") or d.get("to"):
        created = ts.from_iso(unit.created_utc)
        tol = ts.timedelta(hours=tolerance_hours)
        if created is None:
            reasons.append("date: no timestamp")
        else:
            lo = ts.parse_date_bound(d["from"]) if d.get("from") else None
            hi = ts.parse_date_bound(d["to"], end=True) if d.get("to") else None
            if lo and created < lo - tol:
                reasons.append(f"date: {unit.created_utc} < {d['from']}")
            if hi and created >= hi + tol:
                reasons.append(f"date: {unit.created_utc} >= {d['to']}")
    if m["services"] and (unit.account.get("service") or "") not in m["services"]:
        reasons.append(f"service: {unit.account.get('service')} not in {m['services']}")
    if m["accounts"] and (unit.account.get("id") or unit.account.get("label") or "") not in m["accounts"]:
        reasons.append(f"account: {unit.account.get('id')} not in {m['accounts']}")
    if m["kinds"] and unit.kind not in m["kinds"]:
        reasons.append(f"kind: {unit.kind} not in {m['kinds']}")
    if m["formats"] and (unit.source.get("format") or "") not in m["formats"]:
        reasons.append(f"format: {unit.source.get('format')} not in {m['formats']}")
    if m["roles"]:
        have = set((st.get("roles") or {}).keys())
        if not have & set(m["roles"]):
            reasons.append(f"roles: none of {m['roles']} present")
    if m["tools"]:
        have = set((st.get("tools") or {}).keys())
        if not have & set(m["tools"]):
            reasons.append(f"tools: none of {m['tools']} used")
    if m["title_regex"] and not re.search(m["title_regex"], unit.title or ""):
        reasons.append(f"title_regex: {m['title_regex']!r} does not match")
    if m["with_attachments"] is True and not unit.attachments:
        reasons.append("with_attachments: none")
    if m["with_attachments"] is False and unit.attachments:
        reasons.append("with_attachments: has attachments")
    if m["with_branches"] is True and not (st.get("forks") or 0):
        reasons.append("with_branches: no forks")
    if m["with_branches"] is False and (st.get("forks") or 0):
        reasons.append("with_branches: has forks")
    if m["agentic_only"] and not st.get("agentic"):
        reasons.append("agentic_only: no tool use")
    if m["languages"] and (st.get("lang_main") or "und") not in m["languages"]:
        reasons.append(f"languages: {st.get('lang_main')} not in {m['languages']}")
    if m["models"]:
        have = set((st.get("models") or {}).keys()) | ({unit.flags.get("model")} if unit.flags.get("model") else set())
        if not any(any(x in h for h in have) for x in m["models"]):
            reasons.append(f"models: none of {m['models']}")
    n = st.get("messages", len(unit.messages))
    if m["min_messages"] and n < m["min_messages"]:
        reasons.append(f"min_messages: {n} < {m['min_messages']}")
    if m["max_messages"] and n > m["max_messages"]:
        reasons.append(f"max_messages: {n} > {m['max_messages']}")
    c = st.get("chars", 0)
    if m["min_chars"] and c < m["min_chars"]:
        reasons.append(f"min_chars: {c} < {m['min_chars']}")
    if m["max_chars"] and c > m["max_chars"]:
        reasons.append(f"max_chars: {c} > {m['max_chars']}")
    return (not reasons), reasons
