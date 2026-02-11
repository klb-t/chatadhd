"""
ChatADHD v0.07.00 - Path Resolution
Implements KOD≠DANE architecture:
  CODE lives wherever you unzip it (versioned, replaceable).
  DATA lives in a fixed, persistent location (never overwritten by upgrades).

Military-grade: deterministic, auditable, no hardcoded user paths.
"""
import os
import sys
import logging
from pathlib import Path
from typing import Optional

log = logging.getLogger(__name__)

# Sentinel file that marks a valid ChatADHD data directory.
_SENTINEL = ".chatadhd_data"


def _is_android() -> bool:
    return hasattr(sys, 'getandroidapilevel')


def _android_candidates() -> list[Path]:
    """Return candidate data directories on Android, ordered by preference."""
    base = Path("/storage/emulated/0")
    return [
        base / "Documents" / "ChatADHD",
        base / "Download" / "chatadhd_data",
    ]


def _desktop_candidates() -> list[Path]:
    """Return candidate data directories on desktop platforms."""
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        return [Path(xdg) / "chatadhd", Path.home() / ".chatadhd"]
    return [Path.home() / ".chatadhd"]


def resolve_data_dir(override: Optional[str] = None) -> Path:
    """
    Find or create the persistent data directory.

    Resolution order:
      1. Explicit override (CLI flag / env var CHATADHD_DATA)
      2. Existing directory that contains the sentinel file
      3. First candidate directory (created automatically)

    Returns an absolute Path that is guaranteed to exist.
    """
    # 1. Explicit override
    if override:
        p = Path(override).expanduser().resolve()
        _ensure_dir(p)
        return p

    env = os.environ.get("CHATADHD_DATA")
    if env:
        p = Path(env).expanduser().resolve()
        _ensure_dir(p)
        return p

    # 2. Scan candidates for existing data
    candidates = _android_candidates() if _is_android() else _desktop_candidates()
    for c in candidates:
        if (c / _SENTINEL).exists():
            log.info("Found existing data dir: %s", c)
            return c

    # 3. Fall back to first candidate, create it
    target = candidates[0]
    _ensure_dir(target)
    log.info("Initialized new data dir: %s", target)
    return target


def _ensure_dir(p: Path) -> None:
    """Create directory structure and sentinel if missing."""
    p.mkdir(parents=True, exist_ok=True)
    sentinel = p / _SENTINEL
    if not sentinel.exists():
        sentinel.write_text(f"ChatADHD data directory\n", encoding="utf-8")

    # Ensure standard subdirectories
    for sub in ("attachments", "exports", "logs"):
        (p / sub).mkdir(exist_ok=True)


def get_code_dir() -> Path:
    """Return the directory where ChatADHD source code lives."""
    return Path(__file__).resolve().parent.parent
