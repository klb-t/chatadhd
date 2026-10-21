"""The Polish (and English) form of a counted noun: one place that knows the plural rule.

The rule is the CLDR one for integers and is the same as ``Pl.form`` in the phone app (``Pl.kt``): 1 -> one;
ends in 2..4 except 12..14 -> few; everything else (0, 5..21, 25..31...) -> many.  The nouns themselves are data
(``data/ui_labels.json#nouns``): a new noun is one entry there, never a new condition at a call site.
"""
from __future__ import annotations

from typing import Any

from .datafiles import store
from .i18n import language


def category(n: int, lang: str = "pl") -> str:
    a = abs(int(n))
    if a == 1:
        return "one"
    if lang == "pl" and a % 10 in (2, 3, 4) and a % 100 not in (12, 13, 14):
        return "few"
    return "many"


def form(n: int, forms: dict[str, str], lang: str = "pl") -> str:
    return forms.get(category(n, lang)) or forms.get("many") or ""


def noun(key: str, n: int, lang: str | None = None) -> str:
    """The form of the noun ``key`` (a key of ui_labels.json#nouns) that goes with the count ``n``."""
    lang = lang or language()
    table: dict[str, Any] = store().file("ui_labels.json").get("nouns") or {}
    forms = (table.get(key) or {}).get(lang) or (table.get(key) or {}).get("pl") or {}
    return form(n, forms, lang if lang in ("pl", "en") else "pl") if forms else key


def count(key: str, n: int, lang: str | None = None) -> str:
    """``"1 wiadomość"``, ``"3 wiadomości"``, ``"5 wiadomości"``."""
    return f"{int(n)} {noun(key, n, lang)}"
