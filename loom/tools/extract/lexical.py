"""Lexical level: diacritic folding, light PL/EN stemming (the kernel's tables in
``loom/data/lexicons/stemming.json`` plus ``data/lexicon/pl_inflection.json``),
keyword matching with explicit forms or rules, regexes, exclusions, and
"why it matched" (form, position, weight).  Stop words come from the kernel's
lists plus the tool's extras.  Everything here is deterministic and local.
"""
import json
import logging
import math
import re
import unicodedata
from dataclasses import dataclass, field
from typing import Any, Iterable, Optional

from .datafiles import DataFileError, DataStore, store as default_store

log = logging.getLogger(__name__)

_TOKEN = re.compile(r"[^\W_]+(?:[-'’][^\W_]+)*", re.UNICODE)


class Lexicon:
    """Folding, stemming and stop words from data files (kernel tables reused)."""

    def __init__(self, ds: Optional[DataStore] = None) -> None:
        ds = ds or default_store()
        own = ds.file("lexicon/pl_inflection.json")
        base = ds.repo_file(own.get("extends", "loom/data/lexicons/stemming.json"))
        self.fold_map: dict[str, str] = dict(base.get("fold", {}))
        self.fold_map.update(own.get("extra_fold", {}))
        self.pl = dict(base["pl"])
        self.pl["exceptions"] = dict(self.pl.get("exceptions", {}))
        self.pl["exceptions"].update({self.fold(k): self.fold(v) for k, v in own.get("extra_exceptions", {}).items()})
        self.pl["suffixes"] = sorted(set(self.pl.get("suffixes", [])) | set(own.get("extra_suffixes", [])), key=len, reverse=True)
        self.en = dict(base.get("en", {}))
        norm = base.get("normalization", {})
        self.pl_cues = set(norm.get("pl_character_cues", "ąćęłńóśźż"))
        self.pl_markers = tuple(self.pl.get("markers", []))
        self.stop: dict[str, set[str]] = {"pl": set(), "en": set()}
        for sid in ("stop_pl", "stop_en"):
            try:
                sw = ds.file(f"lexicon/{sid}.json")
            except (DataFileError, OSError, json.JSONDecodeError):
                # an extra stop list that is missing is fine (debug); one that exists but does not parse is a warning
                exists = any(p is not None and (p / "lexicon" / f"{sid}.json").exists() for p in (ds.overlay, ds.shipped))
                (log.warning if exists else log.debug)("stop list %s not loaded", sid, exc_info=True)
                continue
            lang = sid.split("_")[1]
            for rel in sw.get("extends", []):
                try:
                    kernel = ds.repo_file(rel)
                except (DataFileError, OSError, json.JSONDecodeError):
                    log.warning("kernel stop list %s not loaded (stop words of %s are the tool's own only)", rel, sid, exc_info=True)
                    continue
                for k in ("pl", "en"):
                    self.stop[k].update(self.fold(w) for w in kernel.get(k, []))
            self.stop[lang].update(self.fold(w) for w in sw.get("words", []))
        self.stop_all = self.stop["pl"] | self.stop["en"]
        # a term shorter than min_token is not stemmed, while its inflected forms (5+ letters) are: 'baza' would
        # never meet 'bazy'/'bazie'.  Such a token is also matched as a prefix (final vowel stripped) with a short
        # ending, in text not recognised as English (data: pl_inflection.json short_terms)
        st = dict(own.get("short_terms") or {})
        self.short_terms = {"enabled": bool(st.get("enabled", True)), "strip_final": str(st.get("strip_final", "aeiouyąę")),
                            "ending_max": int(st.get("ending_max", 4)), "min_prefix": int(st.get("min_prefix", 2)),
                            "skip_lang": list(st.get("skip_lang", ["en"]))}

    # -- folding ----------------------------------------------------------------
    def fold(self, text: str) -> str:
        out = []
        for ch in text.lower():
            if ch in self.fold_map:
                out.append(self.fold_map[ch])
            elif ord(ch) > 127:
                decomposed = unicodedata.normalize("NFKD", ch)
                stripped = "".join(c for c in decomposed if not unicodedata.combining(c))
                out.append(stripped or ch)
            else:
                out.append(ch)
        return "".join(out)

    # -- stemming ---------------------------------------------------------------
    def is_polish(self, token: str) -> bool:
        low = token.lower()
        if any(c in self.pl_cues for c in low):
            return True
        if self.fold(low) in self.stop["pl"] and self.fold(low) not in self.stop["en"]:
            return True
        return low.endswith(self.pl_markers) and len(low) >= int(self.pl.get("min_token", 5))

    def stem(self, token: str, lang: Optional[str] = None) -> str:
        """Match key for ``token`` (folded); never used for display.

        ``lang`` is the language of the surrounding text (``pl``/``en``) when
        known; otherwise the token decides by its own cues (diacritics, Polish
        stop word, Polish ending).  Exception forms win in either table.
        """
        low = token.lower()
        key = self.fold(low)
        pl_exc = self.pl.get("exceptions") or {}
        if key in pl_exc:
            return self.fold(pl_exc[key])
        en_exc = self.en.get("exceptions") or {}
        if key in en_exc:
            return self.fold(en_exc[key])
        if lang == "pl" or (lang is None and self.is_polish(low)):
            return self._stem_table(key, self.pl)
        if lang == "pl":
            return self._stem_table(key, self.pl)
        return self._stem_table(key, self.en) if self.en else key

    def guess_lang(self, tokens: list[str]) -> Optional[str]:
        """``pl`` / ``en`` / None from token cues (kernel's guess_min_tokens idea)."""
        if len(tokens) < 3:
            return None
        pl = sum(1 for t in tokens if self.is_polish(t))
        en = sum(1 for t in tokens if self.fold(t.lower()) in self.stop["en"])
        if pl >= max(1, len(tokens) * 0.05) and pl >= en:
            return "pl"
        if en >= max(1, len(tokens) * 0.05):
            return "en"
        return None

    def _stem_table(self, key: str, table: dict[str, Any]) -> str:
        exc = table.get("exceptions") or {}
        if key in exc:
            return self.fold(exc[key])
        min_token = int(table.get("min_token", 5))
        min_stem = int(table.get("min_stem", 4))
        if len(key) < min_token:
            return key
        for rw in table.get("rewrite") or []:
            suf = self.fold(rw.get("suffix", ""))
            if suf and key.endswith(suf) and len(key) - len(suf) + len(rw.get("to", "")) >= min_stem:
                key = key[: -len(suf)] + self.fold(rw.get("to", ""))
                break
        keep = tuple(table.get("keep_endings") or [])
        for suf in sorted((self.fold(s) for s in table.get("suffixes") or []), key=len, reverse=True):
            if key.endswith(suf) and len(key) - len(suf) >= min_stem:
                if keep and key.endswith(keep) and any(suf.endswith(k) for k in keep):
                    continue
                stem = key[: -len(suf)]
                if table is self.en:
                    if suf in (table.get("verbal") or []) and not any(c in "aeiouy" for c in stem):
                        continue
                    if len(stem) >= 3 and stem[-1] == stem[-2] and stem[-1] not in "lsz":
                        stem = stem[:-1]
                return stem
        return key

    # -- tokens -------------------------------------------------------------------
    def tokens(self, text: str) -> list[tuple[str, int, int]]:
        return [(m.group(0), m.start(), m.end()) for m in _TOKEN.finditer(text or "")]

    def term_keys(self, text: str, *, min_len: int = 3, stop: bool = True) -> list[str]:
        """Folded stems of content words (for clusters, continuations, similarity)."""
        out = []
        for tok, _, _ in self.tokens(text):
            if len(tok) < min_len:
                continue
            f = self.fold(tok)
            if stop and f in self.stop_all:
                continue
            if f.isdigit():
                continue
            out.append(self.stem(tok))
        return out


# ───────────────────────────── keywords ─────────────────────────────
@dataclass
class Keyword:
    term: str
    weight: float = 1.0
    inflect: str = "rules"            # off | rules | forms
    forms: list[str] = field(default_factory=list)
    also_rules: bool = False
    boundary: bool = True
    fold: bool = True
    when_without: list[str] = field(default_factory=list)   # for exclusions
    # compiled
    form_keys: list[tuple[str, ...]] = field(default_factory=list)   # token sequences (folded)
    stem_keys: list[tuple[str, ...]] = field(default_factory=list)   # token sequences (stems)
    prefix_keys: list[tuple[tuple[str, str], ...]] = field(default_factory=list)  # (("^", prefix) | ("=", stem), …)
    substrings: list[str] = field(default_factory=list)


@dataclass
class Match:
    term: str
    form: str
    start: int
    end: int
    weight: float
    how: str      # form | rule | substring | regex


def compile_keyword(spec: Any, lex: Lexicon, defaults: Optional[dict[str, Any]] = None) -> Keyword:
    defaults = defaults or {}
    if isinstance(spec, str):
        spec = {"term": spec}
    kw = Keyword(term=str(spec["term"]), weight=float(spec.get("weight", 1.0)),
                 inflect=str(spec.get("inflect", defaults.get("inflect", "rules"))),
                 forms=[str(f) for f in spec.get("forms", [])], also_rules=bool(spec.get("also_rules", False)),
                 boundary=bool(spec.get("boundary", defaults.get("boundary", True))),
                 fold=bool(spec.get("fold", defaults.get("fold", True))),
                 when_without=[str(w) for w in spec.get("when_without", [])])
    forms = [kw.term] + kw.forms

    def key_tokens(text: str) -> tuple[str, ...]:
        return tuple((lex.fold(t) if kw.fold else t.lower()) for t, _, _ in lex.tokens(text))

    for f in forms:
        kt = key_tokens(f)
        if kt and kt not in kw.form_keys:
            kw.form_keys.append(kt)
        if not kw.boundary:
            kw.substrings.append(lex.fold(f) if kw.fold else f.lower())
    if kw.inflect == "rules" or (kw.inflect == "forms" and kw.also_rules):
        # stems for every language hypothesis, so a term without diacritics still meets Polish text
        for f in forms:
            for lang in (None, "pl", "en"):
                st = tuple(lex.stem(t, lang) for t, _, _ in lex.tokens(f))
                if st and st not in kw.stem_keys:
                    kw.stem_keys.append(st)
        sh = lex.short_terms
        min_token = int(lex.pl.get("min_token", 5))
        if sh["enabled"]:
            for f in forms:
                toks = [lex.fold(t) for t, _, _ in lex.tokens(f)]
                if not toks or not any(len(t) < min_token for t in toks):
                    continue
                key = []
                for t in toks:
                    if len(t) < min_token:
                        p = t[:-1] if len(t) - 1 >= sh["min_prefix"] and t[-1] in sh["strip_final"] else t
                        key.append(("^", p))
                    else:
                        key.append(("=", lex.stem(t, "pl")))
                if tuple(key) not in kw.prefix_keys:
                    kw.prefix_keys.append(tuple(key))
    return kw


@dataclass
class TextIndex:
    """One tokenisation of a text reused by every keyword."""
    text: str
    tokens: list[tuple[str, int, int]]
    folded: list[str]
    lowered: list[str]
    stems: list[str]
    folded_text: str
    lang: Optional[str] = None
    ending_max: int = 4
    skip_lang: list[str] = field(default_factory=lambda: ["en"])
    stems_pl: Optional[list[str]] = None
    _lex: Any = None

    @classmethod
    def build(cls, text: str, lex: Lexicon) -> "TextIndex":
        toks = lex.tokens(text)
        words = [t for t, _, _ in toks]
        lang = lex.guess_lang(words)
        idx = cls(text, toks, [lex.fold(t) for t in words], [t.lower() for t in words],
                  [lex.stem(t, lang) for t in words], lex.fold(text))
        idx.lang = lang
        idx.ending_max = int(lex.short_terms["ending_max"])
        idx.skip_lang = list(lex.short_terms["skip_lang"])
        idx.stems_pl = None
        idx._lex = lex
        return idx


def match_keyword(kw: Keyword, idx: TextIndex) -> list[Match]:
    out: list[Match] = []
    seen: set[tuple[int, int]] = set()
    seq = idx.folded if kw.fold else idx.lowered
    n = len(seq)
    for keys, how in ((kw.form_keys, "form"), (kw.stem_keys, "rule")):
        src = seq if how == "form" else idx.stems
        for key in keys:
            k = len(key)
            if k == 0:
                continue
            for i in range(n - k + 1):
                if tuple(src[i:i + k]) == key:
                    start, end = idx.tokens[i][1], idx.tokens[i + k - 1][2]
                    if (start, end) in seen:
                        continue
                    seen.add((start, end))
                    out.append(Match(kw.term, idx.text[start:end], start, end, kw.weight, how))
    if kw.prefix_keys and getattr(idx, "lang", None) not in (getattr(idx, "skip_lang", None) or ["en"]):
        emax = int(getattr(idx, "ending_max", 4))
        if idx.stems_pl is None:
            idx.stems_pl = [idx._lex.stem(t, "pl") for t in idx.folded]
        for key in kw.prefix_keys:
            k = len(key)
            for i in range(n - k + 1):
                ok = True
                for j, (mark, val) in enumerate(key):
                    tok = idx.folded[i + j]
                    if mark == "^":
                        if not (tok.startswith(val) and len(tok) - len(val) <= emax):
                            ok = False
                            break
                    elif idx.stems_pl[i + j] != val and idx.stems[i + j] != val:
                        ok = False
                        break
                if ok:
                    start, end = idx.tokens[i][1], idx.tokens[i + k - 1][2]
                    if (start, end) in seen:
                        continue
                    seen.add((start, end))
                    out.append(Match(kw.term, idx.text[start:end], start, end, kw.weight, "rule"))
    if not kw.boundary:
        for sub in kw.substrings:
            for m in re.finditer(re.escape(sub), idx.folded_text):
                if (m.start(), m.end()) in seen:
                    continue
                seen.add((m.start(), m.end()))
                out.append(Match(kw.term, idx.text[m.start():m.end()], m.start(), m.end(), kw.weight, "substring"))
    out.sort(key=lambda m: (m.start, m.end))
    return out


def compile_regexes(specs: Iterable[Any]) -> list[tuple[re.Pattern, float, str]]:
    out = []
    for s in specs or []:
        if isinstance(s, str):
            s = {"pattern": s}
        out.append((re.compile(s["pattern"]), float(s.get("weight", 1.0)), s["pattern"]))
    return out


def match_regexes(regexes: list[tuple[re.Pattern, float, str]], text: str) -> list[Match]:
    out = []
    for pat, weight, src in regexes:
        for m in pat.finditer(text or ""):
            if m.end() > m.start():
                out.append(Match(src, m.group(0), m.start(), m.end(), weight, "regex"))
    return out


@dataclass
class LexicalResult:
    score: float
    raw: float
    matches: list[Match]
    excluded_by: list[Match]
    chars: int

    def reasons(self, top_k: int = 10) -> list[dict[str, Any]]:
        ms = self.matches if top_k <= 0 else self.matches[:top_k]
        rs = [{"term": m.term, "form": m.form, "at": m.start, "weight": m.weight, "how": m.how} for m in ms]
        rs += [{"term": m.term, "form": m.form, "at": m.start, "weight": m.weight, "how": m.how, "exclude": True}
               for m in self.excluded_by]
        return rs


def score_text(text: str, keywords: list[Keyword], regexes: list[tuple[re.Pattern, float, str]],
               excludes: list[Keyword], exclude_regexes: list[tuple[re.Pattern, float, str]], lex: Lexicon,
               *, saturation: float = 6.0) -> LexicalResult:
    """Saturated weighted match sum, normalised by length; exclusions explained."""
    idx = TextIndex.build(text or "", lex)
    matches: list[Match] = []
    for kw in keywords:
        matches.extend(match_keyword(kw, idx))
    matches.extend(match_regexes(regexes, text or ""))
    matches.sort(key=lambda m: (m.start, m.end, m.term))
    excluded: list[Match] = []
    for kw in excludes:
        hits = match_keyword(kw, idx)
        if not hits:
            continue
        if kw.when_without:
            guard = [compile_keyword(w, lex) for w in kw.when_without]
            if any(match_keyword(g, idx) for g in guard):
                continue
        excluded.extend(hits)
    excluded.extend(match_regexes(exclude_regexes, text or ""))
    raw = sum(m.weight for m in matches)
    chars = len(text or "")
    norm = math.sqrt(max(1.0, chars / 2000.0))
    score = min(1.0, raw / (saturation * norm)) if saturation > 0 else (1.0 if raw > 0 else 0.0)
    if excluded:
        score = 0.0
    return LexicalResult(score, raw, matches, excluded, chars)


_default: Optional[Lexicon] = None


def lexicon() -> Lexicon:
    global _default
    if _default is None:
        _default = Lexicon()
    return _default


def reset_for_tests() -> None:
    global _default
    _default = None
