"""
ChatADHD v0.07.00 - Model Registry

Fetches and caches the list of available models from OpenRouter.
Provides human-friendly short names and provider grouping.
"""
import json
import logging
from pathlib import Path
from typing import Any, Optional

import requests

log = logging.getLogger(__name__)


class ModelRegistry:
    """Cache of models available through the configured API."""

    def __init__(self, path: Path, config, secrets) -> None:
        self._path = Path(path)
        self._config = config
        self._secrets = secrets
        self._models: list[dict[str, Any]] = []
        self._load()

    def _load(self) -> None:
        if self._path.exists():
            try:
                self._models = json.loads(self._path.read_text(encoding="utf-8"))
                log.info("Loaded %d cached models", len(self._models))
            except Exception:
                log.exception("Failed to load model cache")

    def _save(self) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._path.write_text(
            json.dumps(self._models, indent=1, ensure_ascii=False),
            encoding="utf-8",
        )

    def update_from_api(self) -> bool:
        """Fetch model list from OpenRouter. Returns True on success."""
        key = self._secrets.get("api_key")
        base = self._config.get("base_url", "").rstrip("/")
        if not key or not base:
            log.warning("Cannot refresh models — no API key or base URL")
            return False

        try:
            resp = requests.get(
                f"{base}/models",
                headers={"Authorization": f"Bearer {key}"},
                timeout=15,
            )
            resp.raise_for_status()
            data = resp.json().get("data", [])
            self._models = [
                {"id": m["id"], "name": m.get("name", m["id"]),
                 "context_length": m.get("context_length", 0)}
                for m in data
            ]
            self._save()
            log.info("Refreshed model list: %d models", len(self._models))
            return True
        except Exception:
            log.exception("Failed to refresh models from API")
            return False

    def all(self) -> list[dict[str, Any]]:
        return list(self._models)

    def name(self, model_id: str) -> str:
        """Human-friendly short name for a model ID."""
        for m in self._models:
            if m["id"] == model_id:
                return m.get("name", model_id.split("/")[-1])
        return model_id.split("/")[-1] if "/" in model_id else model_id

    def grouped(self) -> dict[str, list[dict]]:
        """Models grouped by provider (first segment of ID)."""
        groups: dict[str, list[dict]] = {}
        for m in self._models:
            provider = m["id"].split("/")[0] if "/" in m["id"] else "other"
            groups.setdefault(provider, []).append(m)
        return groups
