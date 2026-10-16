"""Redaction by masks (regex rules as data) and a user dictionary, with a report.

Profiles live in ``data/redaction/*.json`` (preset ``none``).  The same value
gets the same mask everywhere in the package (``[EMAIL#1]`` …, numbered in
the deterministic order the package is written), so a redacted package stays
readable and reproducible.  Every replacement is an entry (where, kind, rule,
mask, length) — never the value: nothing disappears without an entry, and no
secret reaches the report, the manifest or a log.  The value→mask map stays
in memory unless ``keep_map`` is set explicitly (it would contain what was
masked).
"""
import re
import unicodedata
from typing import Any, Optional

from .datafiles import DataStore, store as default_store

_FOLD = str.maketrans("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ", "acelnoszzACELNOSZZ")


def _luhn(digits: str) -> bool:
    ds = [int(c) for c in digits if c.isdigit()]
    if len(ds) < 13:
        return False
    total = 0
    for i, d in enumerate(reversed(ds)):
        if i % 2:
            d *= 2
            if d > 9:
                d -= 9
        total += d
    return total % 10 == 0


def _pesel(digits: str) -> bool:
    if len(digits) != 11 or not digits.isdigit():
        return False
    w = [1, 3, 7, 9, 1, 3, 7, 9, 1, 3]
    s = sum(int(digits[i]) * w[i] for i in range(10))
    return (10 - s % 10) % 10 == int(digits[10])


CHECKS = {"luhn": _luhn, "pesel": _pesel}


def _term_pattern(term: str, fold: bool, inflect: bool) -> str:
    """A dictionary term as a regex: case- and (optionally) diacritic-insensitive, each word may take a short
    inflection ending (Polish: Łukasz → Łukasza, Łukaszem) when ``inflect``."""
    words = []
    for word in term.split():
        parts = []
        for ch in word:
            variants = {ch, ch.lower(), ch.upper()}
            if fold:
                f = ch.translate(_FOLD)
                base = unicodedata.normalize("NFKD", f)[:1] or f
                variants |= {f, f.lower(), f.upper(), base, base.lower(), base.upper()}
            parts.append("[" + "".join(sorted(re.escape(v) for v in variants if v)) + "]")
        words.append("".join(parts) + (r"\w{0,4}" if inflect else ""))
    return r"(?<!\w)" + r"\s+".join(words) + r"(?!\w)"


class Redactor:
    def __init__(self, profile: Any = "none", ds: Optional[DataStore] = None, *, terms: Optional[list[str]] = None,
                 keep_map: bool = False) -> None:
        ds = ds or default_store()
        if isinstance(profile, dict):
            prof = dict(profile)
        else:
            prof = dict(ds.entry("redaction", str(profile or "none")))
        if prof.get("inherits"):
            base = dict(ds.entry("redaction", prof["inherits"]))
            rules = base.get("rules") or []
            only = set(prof.get("only_kinds") or [])
            prof.setdefault("rules", [r for r in rules if not only or r.get("kind") in only])
            if not prof.get("rules"):
                prof["rules"] = [r for r in rules if not only or r.get("kind") in only]
        self.profile = prof
        self.id = str(prof.get("id", "inline"))
        self.mask_tpl = str(prof.get("mask") or "[{KIND}#{n}]")
        self.mode = prof.get("mode", "mask")
        self.fields = set(prof.get("fields") or ["title", "text", "input", "name", "author"])
        self.attachments = prof.get("attachments", "copy")
        self.rules: list[tuple[str, str, re.Pattern, Optional[Any], int]] = []
        for r in prof.get("rules") or []:
            self.rules.append((r["id"], r.get("kind", r["id"].upper()), re.compile(r["regex"]), CHECKS.get(r.get("check", "")),
                               int(r.get("group", 0))))
        words = [str(t) for t in (prof.get("dictionary") or [])] + [str(t) for t in (terms or [])]
        words = sorted({w.strip() for w in words if w and w.strip()}, key=lambda w: (-len(w), w))
        dkind = prof.get("dictionary_kind", "NAME")
        fold = bool(prof.get("dictionary_fold", True))
        inflect = bool(prof.get("dictionary_inflect", True))
        if words:
            pat = "|".join(f"(?P<t{i}>{_term_pattern(w, fold, inflect)})" for i, w in enumerate(words))
            self.rules.append(("dictionary", dkind, re.compile(pat), None, 0))
        self.dictionary_terms = len(words)
        self.keep_map = keep_map
        self._maps: dict[str, dict[str, str]] = {}
        self.entries: list[dict[str, Any]] = []
        self.counts: dict[str, int] = {}
        self.by_rule: dict[str, int] = {}

    @property
    def active(self) -> bool:
        return bool(self.rules)

    def _mask(self, kind: str, value: str) -> str:
        if self.mode == "remove":
            return ""
        m = self._maps.setdefault(kind, {})
        if value not in m:
            m[value] = self.mask_tpl.format(KIND=kind, n=len(m) + 1)
        return m[value]

    def text(self, s: Any, where: dict[str, Any], field: str = "text") -> Any:
        if not self.rules or not isinstance(s, str) or not s or field not in self.fields:
            return s
        out = s
        for rid, kind, rx, check, group in self.rules:
            def repl(m: "re.Match[str]") -> str:
                val = m.group(group)
                if val is None or (check is not None and not check(re.sub(r"\D", "", val))):
                    return m.group(0)
                key = f"term:{m.lastgroup}" if rid == "dictionary" and m.lastgroup else val
                mask = self._mask(kind, key)
                self.entries.append({"where": where, "field": field, "kind": kind, "rule": rid, "mask": mask,
                                     "at": m.start(group), "length": len(val)})
                self.counts[kind] = self.counts.get(kind, 0) + 1
                self.by_rule[rid] = self.by_rule.get(rid, 0) + 1
                whole = m.group(0)
                a, b = m.start(group) - m.start(0), m.end(group) - m.start(0)
                return whole[:a] + mask + whole[b:]
            out = rx.sub(repl, out)
        return out

    def value(self, obj: Any, where: dict[str, Any], field: str = "input") -> Any:
        """Redact every string inside a JSON value (keys stay)."""
        if isinstance(obj, str):
            return self.text(obj, where, field if field in self.fields else "text")
        if isinstance(obj, list):
            return [self.value(v, where, field) for v in obj]
        if isinstance(obj, dict):
            return {k: self.value(v, where, field) for k, v in obj.items()}
        return obj

    def report(self) -> dict[str, Any]:
        d: dict[str, Any] = {"profile": self.id, "active": self.active, "entries": len(self.entries),
                             "by_kind": dict(sorted(self.counts.items())), "by_rule": dict(sorted(self.by_rule.items())),
                             "distinct_values": {k: len(v) for k, v in sorted(self._maps.items())},
                             "dictionary_terms": self.dictionary_terms, "attachments": self.attachments,
                             "irreversible": True,
                             "note": "values are never written; masks are consistent within the package"}
        if self.keep_map:
            d["map"] = {k: {mask: val for val, mask in v.items()} for k, v in self._maps.items()}
        return d
