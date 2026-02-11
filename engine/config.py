"""
ChatADHD v0.07.00 - Configuration Management
Secrets are stored separately from config.  Config is auditable; secrets are not.
"""
import json
import logging
import os
import stat
from pathlib import Path
from typing import Any, Optional

log = logging.getLogger(__name__)

# Default configuration values.  Used when no config file exists.
DEFAULTS: dict[str, Any] = {
    "base_url": "https://openrouter.ai/api/v1",
    "default_model": "anthropic/claude-sonnet-4-20250514",
    "temperature": 0.7,
    "max_tokens": 4096,
    "theme": "dark",
    "system_prompt": "You are a helpful assistant with access to the user's hierarchical memory.",
    "auto_title": True,
    "stream": True,
}


class _JsonStore:
    """Thread-safe JSON file backed key-value store."""

    def __init__(self, path: Path, defaults: dict[str, Any] | None = None,
                 restrict_perms: bool = False) -> None:
        self._path = Path(path)
        self._data: dict[str, Any] = dict(defaults or {})
        self._restrict = restrict_perms
        self._load()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------
    def get(self, key: str, fallback: Any = None) -> Any:
        return self._data.get(key, fallback)

    def set(self, key: str, value: Any) -> None:
        self._data[key] = value

    def delete(self, key: str) -> None:
        self._data.pop(key, None)

    def all(self) -> dict[str, Any]:
        return dict(self._data)

    def save(self) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self._path.with_suffix(".tmp")
        tmp.write_text(
            json.dumps(self._data, indent=2, ensure_ascii=False),
            encoding="utf-8",
        )
        tmp.replace(self._path)
        if self._restrict:
            self._lock_perms(self._path)
        log.debug("Saved %s (%d keys)", self._path.name, len(self._data))

    # ------------------------------------------------------------------
    # Internals
    # ------------------------------------------------------------------
    def _load(self) -> None:
        if not self._path.exists():
            log.info("%s not found — using defaults", self._path.name)
            return
        try:
            raw = self._path.read_text(encoding="utf-8")
            on_disk = json.loads(raw)
            if not isinstance(on_disk, dict):
                raise ValueError("root must be a JSON object")
            self._data.update(on_disk)
            log.info("Loaded %s (%d keys)", self._path.name, len(on_disk))
        except Exception:
            log.exception("Failed to load %s — using defaults", self._path.name)

    @staticmethod
    def _lock_perms(path: Path) -> None:
        """Restrict file permissions to owner-only (best effort on Android)."""
        try:
            os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
        except OSError:
            pass  # Android may not support chmod


class Config(_JsonStore):
    """Application configuration (non-secret settings)."""

    def __init__(self, path: Path) -> None:
        super().__init__(path, defaults=DEFAULTS)


class Secrets(_JsonStore):
    """
    Sensitive credentials: API keys, tokens, passwords.
    File permissions are restricted to owner-only.
    """

    def __init__(self, path: Path) -> None:
        super().__init__(path, defaults={}, restrict_perms=True)
