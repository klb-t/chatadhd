"""
engine/logging_setup.py
========================

Centralna konfiguracja logowania dla ChatADHD.

Zasada z kodeksu: każde wysokopoziomowe zdarzenie (rozgałęzienie runtime, zapis,
wysyłka, mount/unmount, init/teardown komponentu, każdy branch wyboru modelu/
providera/fallbacku) musi być logowane. Bez dodawania printów ad-hoc przy
debugowaniu.

Standard:
  - Równoległy output: konsola + plik.
  - Plik: data_dir/logs/DDMMYYYYHHMMSS.log (jeden na każde uruchomienie).
  - Format czytelny maszynowo (timestamp z ms, level, logger, thread, message).
  - Poziom konfigurowalny, domyślnie INFO.

Env vars:
  CHATADHD_LOG_LEVEL       = DEBUG | INFO | WARNING | ERROR  (default INFO)
  CHATADHD_LOG_CONSOLE     = 0 żeby wyłączyć konsolę (default on)
  CHATADHD_LOG_FILE        = 0 żeby wyłączyć plik       (default on)
  CHATADHD_LOG_KIVY_LEVEL  = poziom dla loggera 'kivy'   (default WARNING —
                             Kivy jest gadatliwy na DEBUG/INFO)
  CHATADHD_TRACE_EVENTS    = 1 żeby event bus emity logował na INFO zamiast DEBUG

Nazewnictwo pliku: DDMMYYYYHHMMSS.log (np. 18042026150344.log).
"""

from __future__ import annotations

import logging
import os
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Optional


# ─── Formatters ─────────────────────────────────────────────────

class _MillisFormatter(logging.Formatter):
    """Format z milisekundami i thread name."""

    def __init__(self, long: bool) -> None:
        if long:
            fmt = ("%(asctime)s.%(msecs)03d [%(levelname)-5s] "
                   "[%(threadName)s] %(name)s: %(message)s")
        else:
            fmt = "%(asctime)s [%(levelname)-5s] %(name)s: %(message)s"
        super().__init__(fmt=fmt, datefmt="%H:%M:%S")
        self._long = long

    def formatTime(self, record, datefmt=None):  # noqa: N802
        return datetime.fromtimestamp(record.created).strftime(
            datefmt or self.datefmt
        )


# ─── Public API ─────────────────────────────────────────────────

_STATE: dict = {
    "configured": False,
    "log_file_path": None,
    "file_handler": None,
    "console_handler": None,
}


def configure_logging(data_dir: Optional[Path] = None, *,
                       level: Optional[str] = None,
                       console: Optional[bool] = None,
                       file: Optional[bool] = None,
                       kivy_level: Optional[str] = None) -> Path:
    """
    Konfiguruje root logger z console + file handler.

    Zwraca ścieżkę do utworzonego pliku loga (albo None gdy file=False).

    Bezpieczne do wielokrotnego wywołania — drugi call jest no-op
    (nie podwaja handlerów).
    """
    if _STATE["configured"]:
        return _STATE["log_file_path"]

    # Rozwiąż parametry z env.
    level_name = (level or os.environ.get("CHATADHD_LOG_LEVEL", "INFO")).upper()
    level_int = getattr(logging, level_name, logging.INFO)

    if console is None:
        console = os.environ.get("CHATADHD_LOG_CONSOLE", "1") != "0"
    if file is None:
        file = os.environ.get("CHATADHD_LOG_FILE", "1") != "0"

    kivy_level_name = (kivy_level
                        or os.environ.get("CHATADHD_LOG_KIVY_LEVEL", "WARNING")
                        ).upper()

    # Root logger.
    root = logging.getLogger()
    root.setLevel(level_int)

    # Wyczyść tylko domyślne handlery (bez wystawiania live_debug _BufferHandler
    # gdyby juz był podpięty).
    for h in list(root.handlers):
        # Konserwatywnie: usuń tylko StreamHandler z stderr, tj. default basicConfig.
        if (isinstance(h, logging.StreamHandler)
                and getattr(h, "stream", None) in (sys.stderr, sys.stdout)
                and not isinstance(h, logging.FileHandler)):
            root.removeHandler(h)

    # ── Console handler ──────────────────────────────────
    if console:
        ch = logging.StreamHandler(sys.stdout)
        ch.setLevel(level_int)
        ch.setFormatter(_MillisFormatter(long=False))
        root.addHandler(ch)
        _STATE["console_handler"] = ch

    # ── File handler ─────────────────────────────────────
    log_file_path: Optional[Path] = None
    if file and data_dir is not None:
        logs_dir = Path(data_dir).expanduser() / "logs"
        try:
            logs_dir.mkdir(parents=True, exist_ok=True)
            # Format: DDMMYYYYHHMMSS.log (np. 18042026150344.log).
            # To co user napisał: "1804202617033444.log" miało YYYY 2026 i
            # trailing digits — zakładam że typo i idę z sekundami.
            ts = datetime.now().strftime("%d%m%Y%H%M%S")
            log_file_path = logs_dir / f"{ts}.log"
            fh = logging.FileHandler(str(log_file_path), encoding="utf-8")
            fh.setLevel(logging.DEBUG)  # plik zawsze DEBUG (pełny ślad)
            fh.setFormatter(_MillisFormatter(long=True))
            root.addHandler(fh)
            _STATE["file_handler"] = fh
            _STATE["log_file_path"] = log_file_path
        except Exception as e:
            # Fail-open: log tylko do konsoli, nie przerywamy startu.
            logging.getLogger(__name__).warning(
                "Could not create log file in %s: %s", logs_dir, e)

    # ── Kivy logger poziom ──────────────────────────────
    # Kivy loguje bardzo dużo na INFO/DEBUG — zostawiamy osobno.
    kivy_level_int = getattr(logging, kivy_level_name, logging.WARNING)
    logging.getLogger("kivy").setLevel(kivy_level_int)

    _STATE["configured"] = True

    # Pierwszy zapis — potwierdzenie setupu.
    logging.getLogger("chatadhd.logging").info(
        "Logging configured: level=%s console=%s file=%s kivy=%s log_file=%s",
        level_name, console, file, kivy_level_name, log_file_path
    )

    return log_file_path


def get_log_file_path() -> Optional[Path]:
    """Zwraca ścieżkę do aktualnego pliku loga (albo None)."""
    return _STATE.get("log_file_path")


def set_level(level: str) -> None:
    """Runtime change poziomu logowania. Na request usera."""
    level_int = getattr(logging, level.upper(), logging.INFO)
    logging.getLogger().setLevel(level_int)
    ch = _STATE.get("console_handler")
    if ch:
        ch.setLevel(level_int)
    # File handler zostaje na DEBUG — zawsze pełny ślad.
    logging.getLogger("chatadhd.logging").info("Log level changed to %s", level)
