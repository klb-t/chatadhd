"""Small pl/en string table (``data/i18n/<lang>.json``).

Keys live in code, sentences in data (R42).  A key missing from the table is
printed as the key itself (visibly), never as an empty string.  Language
selection: explicit argument → ``LOOM_EXTRACT_LANG`` → ``ui_language`` of the
app's ``config.json`` when the data directory is known → preset ``pl``.
"""
import json
import logging
import os
from typing import Any, Optional

from .datafiles import DataFileError, store
from .paths import app_data_dir

log = logging.getLogger(__name__)

_current: Optional[str] = None
_tables: dict[str, dict[str, str]] = {}


def available() -> list[str]:
    return sorted(p.stem for p in (store().shipped / "i18n").glob("*.json"))


def resolve_language(explicit: Optional[str] = None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("LOOM_EXTRACT_LANG")
    if env:
        return env
    base = app_data_dir()
    if base is not None and (base / "config.json").exists():
        try:
            with open(base / "config.json", "r", encoding="utf-8") as fh:
                cfg = json.load(fh)
            lang = cfg.get("ui_language")
            if isinstance(lang, str) and lang:
                return lang
        except Exception:
            log.debug("config.json unreadable for ui_language", exc_info=True)
    return store().preset("language", "pl")


def set_language(lang: Optional[str]) -> str:
    global _current
    _current = resolve_language(lang)
    return _current


def language() -> str:
    if _current is None:
        set_language(None)
    return _current or "pl"


def table(lang: Optional[str] = None) -> dict[str, str]:
    lang = lang or language()
    if lang not in _tables:
        try:
            _tables[lang] = store().file(f"i18n/{lang}.json")
        except DataFileError:
            log.warning("i18n table for %r missing; keys will be shown", lang)
            _tables[lang] = {}
    return _tables[lang]


def t(_key: str, **kw: Any) -> str:
    """Translate ``_key`` with ``str.format`` arguments; unknown key → key."""
    key = _key
    text = table().get(key)
    if text is None:
        text = table("en").get(key) if language() != "en" else None
    if text is None:
        return key if not kw else f"{key} {kw}"
    try:
        return text.format(**kw)
    except (KeyError, IndexError):
        return text


def label(obj: Any, lang: Optional[str] = None) -> str:
    """``{"pl": …, "en": …}`` → text for the current language (fallback: any)."""
    if isinstance(obj, dict):
        lang = lang or language()
        if obj.get(lang):
            return str(obj[lang])
        for v in obj.values():
            if v:
                return str(v)
        return ""
    return "" if obj is None else str(obj)


def reset_for_tests() -> None:
    global _current
    _current = None
    _tables.clear()
