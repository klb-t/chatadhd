"""
ChatADHD v0.07.10 - Model Registry

Fetches and caches the list of available models from OpenRouter.
Provides human-friendly short names, provider grouping, pricing, and descriptions.
Supports semantic search and cost analysis.
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
        if not self._path.exists():
            return
        try:
            raw_text = self._path.read_text(encoding="utf-8")
            if not raw_text.strip():
                return
            raw = json.loads(raw_text)

            models: list[dict[str, Any]] = []

            if isinstance(raw, list):
                for m in raw:
                    if isinstance(m, dict) and "id" in m:
                        models.append(m)
                    elif isinstance(m, str):
                        # v0.06.x stored plain model ID strings.
                        models.append({"id": m, "name": m.split("/")[-1],
                                       "context_length": 0})
                    # Skip anything else silently.

            elif isinstance(raw, dict):
                # Raw API response cached by v0.06.x.
                data = raw.get("data", raw.get("models", []))
                if isinstance(data, list):
                    for m in data:
                        if isinstance(m, dict) and "id" in m:
                            models.append({
                                "id": m["id"],
                                "name": m.get("name", m["id"]),
                                "context_length": m.get("context_length", 0),
                            })

            self._models = models
            if models:
                log.info("Loaded %d cached models", len(models))
                # Re-save in canonical format so next load is clean.
                self._save()
            else:
                log.info("Model cache empty or unrecognized — will refresh from API")
        except Exception:
            log.warning("Corrupted model cache — removing %s", self._path)
            try:
                self._path.unlink()
            except OSError:
                pass
            self._models = []

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
