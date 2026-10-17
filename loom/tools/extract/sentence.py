"""Sentence mode: the first CLI argument that is not a verb is a sentence
("wyciągnij z archiwum X wszystko o ChatADHD w profilu płaskim").  A rule
grammar as data (``data/grammar/<lang>.json``) turns it into a task (a recipe),
which is SHOWN before it runs and saved in the manifest; what the grammar did
not understand is returned as ``rest`` (never guessed; with ``--model`` the
``describe`` flow may propose an interpretation).  The same module turns a
filter described in words into criteria without a model (``describe``)."""
import logging
import re
from typing import Any, Optional

from .datafiles import DataStore, store as default_store
from .recipe import SCHEMA

log = logging.getLogger(__name__)


def _grammars(ds: DataStore, lang: Optional[str]) -> list[dict[str, Any]]:
    order = [lang] if lang else []
    order += [x for x in ("pl", "en") if x not in order]
    out = []
    for g in order:
        try:
            out.append(ds.file(f"grammar/{g}.json"))
        except Exception:  # noqa: BLE001 - a missing grammar is reported by the caller as "not understood"
            log.debug("sentence grammar %s unavailable", g, exc_info=True)
            continue
    return out


def _clean(s: str) -> str:
    s = re.sub(r"\s+", " ", s).strip().strip(",.;")
    if len(s) >= 2 and s[0] == s[-1] == '"':
        s = s[1:-1]
    return s


def parse_sentence(text: str, ds: Optional[DataStore] = None, *, lang: Optional[str] = None) -> dict[str, Any]:
    """Sentence → {verb, sources, topic, profiles, range, exprs, drop_blocks, redaction, package, handoff, rest, grammar}."""
    ds = ds or default_store()
    best: Optional[dict[str, Any]] = None
    for g in _grammars(ds, lang):
        res = _apply(text, g)
        if best is None or res["score"] > best["score"]:
            best = res
    assert best is not None
    return best


def _apply(text: str, g: dict[str, Any]) -> dict[str, Any]:
    work = " " + text.strip() + " "
    res: dict[str, Any] = {"verb": None, "sources": [], "topic": None, "profiles": [], "range": {}, "exprs": [], "drop_blocks": [],
                           "redaction": None, "package": None, "handoff": False, "grammar": g.get("id"), "matched": []}
    first = text.strip().split()[0].lower() if text.strip() else ""
    verbs = {k.lower(): v for k, v in (g.get("verbs") or {}).items()}
    if first in verbs:
        res["verb"] = verbs[first]
        work = work.replace(text.strip().split()[0], " ", 1)
    stop = g.get("stop", "$")
    split = re.compile(g.get("list_split") or r"\s*,\s*")
    for rule in g.get("rules") or []:
        rx = re.compile(rule["regex"].replace("{stop}", stop), re.I)
        while True:
            m = rx.search(work)
            if not m:
                break
            act = rule["action"]
            gd = {k: v for k, v in m.groupdict().items() if v}
            if act == "sources":
                res["sources"] += [_clean(x) for x in split.split(gd.get("list", "")) if _clean(x)]
            elif act == "topic":
                res["topic"] = _clean(gd.get("topic", "")) or res["topic"]
            elif act == "profile":
                word = (gd.get("profile") or gd.get("profile2") or "").lower()
                prof = (g.get("profiles") or {}).get(word, word)
                if prof not in res["profiles"]:
                    res["profiles"].append(prof)
            elif act == "range":
                if gd.get("year"):
                    res["range"] = {"from": gd["year"], "to": gd["year"]}
                else:
                    if gd.get("from"):
                        res["range"]["from"] = gd["from"]
                    if gd.get("to"):
                        res["range"]["to"] = gd["to"]
            elif act == "drop_blocks":
                res["drop_blocks"] += [k for k in rule.get("kinds") or [] if k not in res["drop_blocks"]]
            elif act == "expr":
                res["exprs"].append(rule["expr"])
            elif act == "redaction":
                res["redaction"] = rule.get("profile", "personal")
            elif act == "package":
                res["package"] = _clean(gd.get("package", ""))
            elif act == "handoff":
                res["handoff"] = True
            res["matched"].append(rule["id"])
            work = work[:m.start()] + " " + work[m.end():]
    filler = {w.lower() for w in g.get("filler") or []}
    rest = [w for w in work.split() if w.lower() not in filler]
    res["rest"] = " ".join(rest)
    res["score"] = len(res["matched"]) * 2 + int(res["verb"] is not None) - len(rest)
    return res


def sentence_to_recipe(parsed: dict[str, Any], ds: Optional[DataStore] = None, *, sentence: str = "") -> dict[str, Any]:
    ds = ds or default_store()
    verb = parsed.get("verb") or "cut"
    steps: list[dict[str, Any]] = []
    topic = parsed.get("topic")
    if topic:
        crit_ids = set(ds.entries("criteria"))
        cid = re.sub(r"[^a-z0-9_-]+", "", topic.lower())
        steps.append({"op": "select.criteria", "params": {"criteria": cid} if cid in crit_ids else {"terms": [topic]}, "by": "sentence"})
    rng = parsed.get("range") or {}
    if rng:
        expr = f"created:{rng.get('from', '')}..{rng.get('to', '')}"
        steps.append({"op": "select.expr", "params": {"expr": expr, "mode": "intersect" if topic else "add"}, "by": "sentence"})
    for e in parsed.get("exprs") or []:
        steps.append({"op": "select.expr", "params": {"expr": e, "mode": "intersect" if (topic or rng) else "add"}, "by": "sentence"})
    if not steps:
        steps.append({"op": "select.expr", "params": {"expr": "kind:conversation", "mode": "add"}, "by": "sentence"})
    if verb == "count":
        steps = [{"op": "inventory", "by": "sentence"}] + steps + [{"op": "dry_run", "by": "sentence"}]
    elif verb == "search":
        steps.append({"op": "expand", "by": "sentence"})
        steps.append({"op": "dry_run", "by": "sentence"})
    else:
        steps.append({"op": "expand", "by": "sentence"})
        if parsed.get("drop_blocks"):
            steps.append({"op": "edit.drop_blocks", "params": {"where": "conversation", "kinds": parsed["drop_blocks"]}, "by": "sentence"})
        cut: dict[str, Any] = {}
        if parsed.get("profiles"):
            cut["profiles"] = parsed["profiles"]
        if parsed.get("redaction"):
            cut["redaction"] = parsed["redaction"]
        steps.append({"op": "cut", "params": cut, "by": "sentence"})
        if parsed.get("handoff"):
            steps.append({"op": "handoff", "by": "sentence"})
        pkg = parsed.get("package") or default_package(topic)
        steps.append({"op": "write", "params": {"package": pkg}, "by": "sentence"})
    return {"schema": SCHEMA, "id": "sentence", "label": {"pl": sentence, "en": sentence}, "sources": list(parsed.get("sources") or []),
            "settings": {}, "steps": steps}


def default_package(topic: Optional[str], ds: Optional[DataStore] = None) -> str:
    """The package directory when none is named: preset ``cut.package_default`` (``ekstrakt-{topic}``, in the
    current directory), ``{topic}`` from the topic or ``cut.package_topic_default`` (``archiwum``).  The same for
    the sentence and the ``wytnij`` verb; ``--pakiet`` overrides it."""
    from .datafiles import store as _store
    ds = ds or _store()
    tpl = str(ds.preset("cut.package_default", "ekstrakt-{topic}") or "ekstrakt-{topic}")
    slug = re.sub(r"[^A-Za-z0-9_-]+", "-", topic).strip("-").lower() if topic else ""
    return tpl.replace("{topic}", slug or str(ds.preset("cut.package_topic_default", "archiwum") or "archiwum"))


def describe_rules(text: str, ds: Optional[DataStore] = None) -> tuple[dict[str, Any], str]:
    """A filter in words → criteria without a model: quoted phrases and capitalised names → keywords,
    /…/ → regexes, 'bez X' / 'nie X' / 'without X' → exclusions, the whole description → one vector query."""
    ds = ds or default_store()
    kws: list[dict[str, Any]] = []
    excl: list[dict[str, Any]] = []
    regex = [{"pattern": m, "weight": 1.0} for m in re.findall(r"/([^/]+)/", text)]
    work = re.sub(r"/[^/]+/", " ", text)
    for m in re.findall(r"\b(?:bez|nie|oprócz|oprocz|without|not|except)\s+(\"[^\"]+\"|[\wąćęłńóśźżĄĆĘŁŃÓŚŹŻ-]+)", work, re.I):
        excl.append({"term": _clean(m), "inflect": "rules"})
    work = re.sub(r"\b(?:bez|nie|oprócz|oprocz|without|not|except)\s+(\"[^\"]+\"|[\wąćęłńóśźżĄĆĘŁŃÓŚŹŻ-]+)", " ", work, flags=re.I)
    for m in re.findall(r"\"([^\"]+)\"", work):
        kws.append({"term": m, "inflect": "rules", "weight": 1.5})
    work = re.sub(r"\"[^\"]+\"", " ", work)
    stop: set[str] = set()
    for name in ("lexicon/stop_pl.json", "lexicon/stop_en.json"):
        try:
            data = ds.file(name)
            words = data.get("words") if isinstance(data, dict) else data
            stop |= {str(w).lower() for w in (words or [])}
        except Exception:  # noqa: BLE001 - no stop list = keep every word
            log.debug("stop list %s unavailable", name, exc_info=True)
            continue
    for w in re.findall(r"[\wąćęłńóśźżĄĆĘŁŃÓŚŹŻ][\wąćęłńóśźżĄĆĘŁŃÓŚŹŻ.+#-]*", work):
        lw = w.lower()
        if lw in stop or len(lw) < 3 or any(k["term"].lower() == lw for k in kws):
            continue
        kws.append({"term": w, "inflect": "rules", "weight": 1.0 if (w[:1].isupper() or any(c.isdigit() for c in w)) else 0.6})
    crit = {"schema": "loom.extract.criteria/1", "id": "described", "description": text, "keywords": kws, "regex": regex,
            "exclude": {"keywords": excl, "regex": []}, "vectors": [{"query": text}]}
    return crit, ""
