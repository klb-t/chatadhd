"""Where the tool's data lives: shipped presets and the user's overlay.

Precedence (most specific wins, R47): task file → CLI options → user overlay
``<app data dir>/extract/<kind>/*.json`` → shipped presets ``data/``.  The app
data directory is resolved through ``engine.paths.resolve_data_dir`` so
``CHATADHD_DATA`` keeps working; when the app package is not importable the
overlay is simply absent (reported, never silently defaulted).
"""
import logging
import os
import sys
from pathlib import Path
from typing import Optional

from . import DATA_DIR, REPO_ROOT

log = logging.getLogger(__name__)


def ensure_repo_on_path() -> None:
    if str(REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(REPO_ROOT))


def app_data_dir(override: Optional[str] = None) -> Optional[Path]:
    """The ChatADHD data directory (or None when it cannot be resolved)."""
    env = override or os.environ.get("LOOM_EXTRACT_DATA_DIR")
    if env:
        return Path(env).expanduser()
    if not os.environ.get("CHATADHD_DATA"):
        # Without an explicit data dir we do not create ~/.chatadhd as a side
        # effect of a read-only tool; the overlay is then absent.
        return None
    try:
        ensure_repo_on_path()
        from engine.paths import resolve_data_dir
        return resolve_data_dir()
    except Exception:
        log.debug("app data dir not resolvable", exc_info=True)
        return None


def overlay_dir(override: Optional[str] = None) -> Optional[Path]:
    base = app_data_dir(override)
    if base is None:
        return None
    return base / "extract"


def shipped_dir() -> Path:
    return DATA_DIR


def state_dir(ds: Optional[object] = None) -> Path:
    """Per-user state outside every repository and every package (commander autosave without a package or an
    overlay, the local dictionary of redacted packages): ``LOOM_EXTRACT_STATE`` → preset ``state.dir`` →
    ``$XDG_STATE_HOME/loom-extract`` → ``~/.local/state/loom-extract``."""
    env = os.environ.get("LOOM_EXTRACT_STATE")
    if env:
        return Path(env).expanduser()
    preset = None
    if ds is not None:
        try:
            preset = ds.preset("state.dir")  # type: ignore[attr-defined]
        except Exception:  # noqa: BLE001 - a broken preset file is reported where it is loaded
            log.debug("state.dir preset unreadable", exc_info=True)
    if preset:
        return Path(str(preset)).expanduser()
    xdg = os.environ.get("XDG_STATE_HOME")
    return (Path(xdg).expanduser() if xdg else Path.home() / ".local" / "state") / "loom-extract"
