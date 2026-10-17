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


_INFLECT_DEFAULT = {"strip_final": "aeiouyąę", "optional_inner": "e", "ending_max": 5, "min_stem": 2}
_HASHLIKE = re.compile(r"^(?:sha256:)?[0-9a-f]{32,}$")


def _char_class(ch: str, fold: bool) -> str:
    variants = {ch, ch.lower(), ch.upper()}
    if fold:
        f = ch.translate(_FOLD)
        base = unicodedata.normalize("NFKD", f)[:1] or f
        variants |= {f, f.lower(), f.upper(), base, base.lower(), base.upper()}
    return "[" + "".join(sorted(re.escape(v) for v in variants if v)) + "]"


def _term_pattern(term: str, fold: bool, inflect: bool, rules: Optional[dict[str, Any]] = None) -> str:
    """A dictionary term as a regex: case- and (optionally) diacritic-insensitive.  With ``inflect`` each word is
    matched on its stem, not on the full nominative: a final vowel (``strip_final``) is dropped before the ending
    wildcard, so Polish forms whose ending changes are masked too (Anna → Annę/Anny, Kowalska → Kowalskiej,
    migracja → migracji), and a fleeting vowel before the last consonant (``optional_inner``) may be absent
    (Marek → Marka, kupiec → kupca).  The rules are data (preset ``redaction.inflection``, a profile's
    ``dictionary_inflection``)."""
    r = dict(_INFLECT_DEFAULT)
    r.update(rules or {})
    strip_final = str(r.get("strip_final") or "")
    inner = str(r.get("optional_inner") or "")
    ending = int(r.get("ending_max") or 0)
    min_stem = int(r.get("min_stem") or 1)
    words = []
    for word in term.split():
        stem = word
        if inflect and len(word) - 1 >= min_stem and word[-1].lower() in strip_final:
            stem = word[:-1]
        parts = [_char_class(ch, fold) for ch in stem]
        if inflect and inner and len(stem) >= min_stem + 1 and stem[-2].lower() in inner \
                and stem[-1].isalpha() and stem[-1].lower() not in strip_final and stem[-3:-2].isalpha() \
                and stem[-3].lower() not in strip_final:
            parts[-2] = parts[-2] + "?"
        words.append("".join(parts) + (r"\w{0,%d}" % max(ending, 0) if inflect else ""))
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
        self.fields = set(prof.get("fields") or ["title", "text", "input", "name", "author", "file_name", "account", "meta", "extra"])
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
        rules = dict(ds.preset("redaction.inflection") or {})
        rules.update(prof.get("dictionary_inflection") or {})
        self.words = words
        if words:
            pat = "|".join(f"(?P<t{i}>{_term_pattern(w, fold, inflect, rules)})" for i, w in enumerate(words))
            self.rules.append(("dictionary", dkind, re.compile(pat), None, 0))
        self.dictionary_terms = len(words)
        self.keep_map = keep_map
        self._maps: dict[str, dict[str, str]] = {}
        self.entries: list[dict[str, Any]] = []
        self.counts: dict[str, int] = {}
        self.by_rule: dict[str, int] = {}

    @classmethod
    def merged(cls, specs: list[tuple[Any, list[str]]], ds: Optional[DataStore] = None,
               maps: Optional[list[dict[str, dict[str, str]]]] = None) -> "Redactor":
        """One redactor for the package: the cut's profile + dictionary and every ``edit.redact`` (profile + terms),
        rules and dictionaries united, fields united, attachments ``omit`` when any says so; seeded with the edits'
        value → mask maps so a value masked by an edit gets the same mask in preserve and in the side files."""
        ds = ds or default_store()
        parts = [cls(prof, ds, terms=list(terms or [])) for prof, terms in specs]
        live = [r for r in parts if r.active]
        if not live:
            red = parts[0] if parts else cls("none", ds)
        elif len(live) == 1:
            red = live[0]
        else:
            base = {k: v for k, v in live[0].profile.items() if k not in ("inherits", "only_kinds")}
            rules: list[dict[str, Any]] = []
            seen: set[str] = set()
            for r in live:
                for rule in r.profile.get("rules") or []:
                    if rule.get("id") not in seen:
                        seen.add(rule.get("id"))
                        rules.append(rule)
            base.update({"id": "+".join(dict.fromkeys(r.id for r in live)), "rules": rules,
                         "dictionary": sorted({w for r in live for w in r.words}),
                         "fields": sorted(set().union(*(r.fields for r in live))),
                         "attachments": "omit" if any(r.attachments == "omit" for r in live) else base.get("attachments", "copy")})
            red = cls(base, ds)
        for m in maps or []:
            red.seed(m)
        return red

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
                # a dictionary hit is keyed by its term (stable across redactors with other term lists, so a mask
                # seeded from a batch edit stays the same in preserve and the side files)
                key = f"term:{self.words[int(m.lastgroup[1:])]}" if rid == "dictionary" and m.lastgroup else val
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
        """Redact every string inside a JSON value (keys stay; content hashes stay)."""
        if isinstance(obj, str):
            if _HASHLIKE.match(obj):
                return obj
            return self.text(obj, where, field if field in self.fields else "text")
        if isinstance(obj, list):
            return [self.value(v, where, field) for v in obj]
        if isinstance(obj, dict):
            return {k: self.value(v, where, field) for k, v in obj.items()}
        return obj

    # -- without entries: questions and metadata ---------------------------------------------------------------
    def would_mask(self, s: Any) -> bool:
        """Whether ``s`` holds anything a rule or the dictionary masks (no entry, no mask assigned)."""
        if not self.rules or not isinstance(s, str) or not s:
            return False
        for _rid, _kind, rx, check, group in self.rules:
            for m in rx.finditer(s):
                val = m.group(group)
                if val is not None and (check is None or check(re.sub(r"\D", "", val))):
                    return True
        return False

    def scrub(self, obj: Any, skip: tuple[str, ...] = ()) -> Any:
        """Masks in a metadata value (the manifest) with the package's masks, without entries: the entries list
        the content that was masked, and the manifest is written after them."""
        if isinstance(obj, str):
            if _HASHLIKE.match(obj) or not self.would_mask(obj):
                return obj
            n_entries, counts, by_rule = len(self.entries), dict(self.counts), dict(self.by_rule)
            fields, self.fields = self.fields, self.fields | {"text"}
            try:
                out = self.text(obj, {"file": "manifest.json"}, "text")
            finally:
                self.fields = fields
                del self.entries[n_entries:]
                self.counts, self.by_rule = counts, by_rule
            return out
        if isinstance(obj, list):
            return [self.scrub(v, skip) for v in obj]
        if isinstance(obj, dict):
            return {k: (v if k in skip else self.scrub(v, skip)) for k, v in obj.items()}
        return obj

    def maps(self) -> dict[str, dict[str, str]]:
        """value → mask per kind (in memory only; used to seed the package's redactor so masks stay consistent)."""
        return {k: dict(v) for k, v in self._maps.items()}

    def seed(self, maps: dict[str, dict[str, str]]) -> None:
        for kind, m in (maps or {}).items():
            cur = self._maps.setdefault(kind, {})
            for val, mask in m.items():
                cur.setdefault(val, mask)

    def report(self) -> dict[str, Any]:
        d: dict[str, Any] = {"profile": self.id, "active": self.active, "entries": len(self.entries),
                             "by_kind": dict(sorted(self.counts.items())), "by_rule": dict(sorted(self.by_rule.items())),
                             "distinct_values": {k: len(v) for k, v in sorted(self._maps.items())},
                             "dictionary_terms": self.dictionary_terms, "attachments": self.attachments,
                             "fields": sorted(self.fields),
                             "irreversible": True,
                             "note": "values are never written; masks are consistent within the package"}
        if self.keep_map:
            d["map"] = {k: {mask: val for val, mask in v.items()} for k, v in self._maps.items()}
        return d
