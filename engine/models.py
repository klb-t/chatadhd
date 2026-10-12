"""
ChatADHD v0.07.10 - Model Registry

Fetches and caches the list of available models from OpenRouter.
Provides human-friendly short names, provider grouping, pricing, and descriptions.
Supports semantic search and cost analysis.
"""
import json
import logging
from datetime import datetime
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
        # ok | missing | corrupt (+ human-readable reason) for the GUI.
        self.cache_status = "missing"
        self.cache_reason = ""
        self._load()

    @staticmethod
    def _norm_entry(m: Any) -> Optional[dict[str, Any]]:
        """Normalize one cached entry, keeping every field (unknown ones too)."""
        if isinstance(m, dict) and "id" in m:
            return m
        if isinstance(m, str):
            # v0.06.x stored plain model ID strings.
            return {"id": m, "name": m.split("/")[-1], "context_length": 0}
        return None

    def _load(self) -> None:
        self.cache_status = "missing"
        self.cache_reason = ""
        if not self._path.exists():
            self.cache_reason = "no cache file"
            return
        try:
            raw_text = self._path.read_text(encoding="utf-8")
            if not raw_text.strip():
                self.cache_reason = "empty cache file"
                return
            raw = json.loads(raw_text)

            models: list[dict[str, Any]] = []
            wrapper = False

            if isinstance(raw, list):
                items = raw
            elif isinstance(raw, dict):
                # Wrapper / raw API response ({"data": [...]}): same field set
                # as the list form, unknown fields preserved.
                wrapper = True
                items = raw.get("data", raw.get("models", []))
                if not isinstance(items, list):
                    items = []
            else:
                raise ValueError(f"unexpected top-level type {type(raw).__name__}")

            for m in items:
                e = self._norm_entry(m)
                if e is None:
                    log.warning("Skipping unrecognized model cache entry: %r", m)
                    continue
                if wrapper:
                    e = dict(e)
                    e.setdefault("name", e["id"])
                    e.setdefault("context_length", 0)
                models.append(e)

            self._models = models
            if models:
                self.cache_status = "ok"
                log.info("Loaded %d cached models", len(models))
                if wrapper:
                    # Re-save wrapper form in canonical list format (lossless).
                    self._save()
            else:
                self.cache_reason = "cache has no models"
                log.info("Model cache empty or unrecognized — will refresh from API")
        except Exception as e:
            # Never delete the cache: its bytes stay on disk untouched.
            log.warning("Unreadable model cache %s (left untouched)", self._path,
                        exc_info=True)
            self.cache_status = "corrupt"
            self.cache_reason = f"{type(e).__name__}: {e}"
            self._models = []

    def _preserve_corrupt(self) -> None:
        """Before writing over a corrupt cache, move its bytes aside."""
        if self.cache_status != "corrupt" or not self._path.exists():
            return
        stamp = datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")
        dst = self._path.with_name(self._path.name + f".corrupt-{stamp}")
        self._path.replace(dst)
        log.warning("Moved corrupt model cache to %s", dst.name)

    def _save(self) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._preserve_corrupt()
        self._path.write_text(
            json.dumps(self._models, indent=1, ensure_ascii=False),
            encoding="utf-8",
        )
        if self.cache_status != "ok":
            self.cache_status = "ok"
            self.cache_reason = ""

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
                {
                    "id": m["id"],
                    "name": m.get("name", m["id"]),
                    "context_length": m.get("context_length", 0),
                    "pricing": m.get("pricing", {}),
                    "description": m.get("description", ""),
                }
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

    def get_pricing(self, model_id: str) -> dict[str, float]:
        """Get pricing info for a model (input/output tokens per million)."""
        for m in self._models:
            if m["id"] == model_id:
                return m.get("pricing", {})
        return {}

    def get_description(self, model_id: str) -> str:
        """Get model description."""
        for m in self._models:
            if m["id"] == model_id:
                return m.get("description", "")
        return ""

    def get_context_length(self, model_id: str) -> int:
        """Get model context window."""
        for m in self._models:
            if m["id"] == model_id:
                return m.get("context_length", 0)
        return 0

    def estimate_cost(self, model_id: str, input_tokens: int, output_tokens: int) -> float:
        """Estimate cost in USD for a message (input + output tokens)."""
        pricing = self.get_pricing(model_id)
        if not pricing:
            return 0.0

        input_price = pricing.get("prompt", 0) or 0
        output_price = pricing.get("completion", 0) or 0

        # Prices are per million tokens
        total_cost = (input_tokens * input_price + output_tokens * output_price) / 1_000_000
        return round(total_cost, 8)
