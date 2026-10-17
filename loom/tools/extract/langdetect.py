"""Cheap local language share estimate from function-word profiles
(``data/langdetect/profiles.json``).  Never a guess: without enough hits the
answer is ``und`` and the inventory says how many characters were undecided.
"""
import re
from typing import Any, Optional

from .datafiles import DataStore, store as default_store

_WORD = re.compile(r"[^\W\d_]+", re.UNICODE)


class LanguageDetector:
    def __init__(self, ds: Optional[DataStore] = None) -> None:
        ds = ds or default_store()
        rel = ds.preset("language_detection.profiles", "langdetect/profiles.json")
        data = ds.file(rel)
        self.min_chars = int(ds.preset("language_detection.min_chars", 20))
        self.min_hits = int(data.get("min_hits", 2))
        self.profiles: dict[str, set[str]] = {k: set(v) for k, v in data["profiles"].items()}
        scripts = data.get("scripts", {})
        cyr = scripts.get("cyrillic", {})
        self.cyr_range = (int(cyr.get("range", ["0400", "04FF"])[0], 16), int(cyr.get("range", ["0400", "04FF"])[1], 16))
        self.uk_letters = set(cyr.get("uk_letters", "їєіґ"))
        self.ru_letters = set(cyr.get("ru_letters", "ыэъё"))
        self.cyrillic_langs = {k for k in self.profiles if k in ("ru", "uk")}

    def detect(self, text: str) -> str:
        """Best language code for ``text`` or ``und``."""
        if not text or len(text) < self.min_chars:
            return "und"
        cyr = sum(1 for ch in text if self.cyr_range[0] <= ord(ch) <= self.cyr_range[1])
        letters = sum(1 for ch in text if ch.isalpha())
        if letters and cyr / letters > 0.5:
            uk = sum(1 for ch in text if ch in self.uk_letters)
            ru = sum(1 for ch in text if ch in self.ru_letters)
            if uk > ru:
                return "uk" if "uk" in self.profiles else "und"
            if ru > uk:
                return "ru" if "ru" in self.profiles else "und"
        tokens = [t.lower() for t in _WORD.findall(text)]
        if not tokens:
            return "und"
        best, best_hits = "und", 0
        for lang in sorted(self.profiles):
            if lang in self.cyrillic_langs and not cyr:
                continue
            hits = sum(1 for t in tokens if t in self.profiles[lang])
            if hits > best_hits:
                best, best_hits = lang, hits
        return best if best_hits >= self.min_hits else "und"

    def shares(self, texts: list[str]) -> dict[str, float]:
        """Character-weighted shares per language over ``texts``; rounded to 3 places."""
        weights: dict[str, int] = {}
        total = 0
        for t in texts:
            if not t:
                continue
            lang = self.detect(t)
            weights[lang] = weights.get(lang, 0) + len(t)
            total += len(t)
        if not total:
            return {}
        return {k: round(v / total, 3) for k, v in sorted(weights.items(), key=lambda kv: (-kv[1], kv[0]))}


_default: Optional[LanguageDetector] = None


def detector() -> LanguageDetector:
    global _default
    if _default is None:
        _default = LanguageDetector()
    return _default


def reset_for_tests() -> None:
    global _default
    _default = None
