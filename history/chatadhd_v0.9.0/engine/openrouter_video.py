"""
engine/openrouter_video.py
===========================

Konkretny provider dla OpenRouter Video Generation API.

API (zgodnie z dokumentacją z 2026-04-15):
  POST /api/v1/videos                    → submit; response 202 z {id, polling_url, status}
  GET  /api/v1/videos/{id}               → poll; response {status, unsigned_urls, usage}
  GET  /api/v1/videos/{id}/content       → download
  GET  /api/v1/videos/models             → discovery (capabilities, pricing_skus)

Async nature: video gen trwa od 30s do kilku minut. Polling co 30s.

Statusy provider → nasze:
  pending      → JobStatus.PENDING
  in_progress  → JobStatus.IN_PROGRESS
  completed    → JobStatus.COMPLETED
  failed       → JobStatus.FAILED

Unified schema OR: ten sam endpoint dla T2V/I2V/R2V. Router po stronie
OR wybiera tryb na podstawie obecności frame_images / input_references.

Integracja z VideoPipeline:
  Pipeline woła AsyncJobManager.submit() dla każdego clip generation.
  Potem słucha EVT_JOB_COMPLETED z context zawierającym {graph_id, scene_id}
  i aktualizuje scene.artifacts.clip_path.
"""

from __future__ import annotations

import logging
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

from engine.async_jobs import (
    IAsyncJobProvider, AsyncJob, JobStatus,
)

log = logging.getLogger(__name__)


class OpenRouterVideoProvider(IAsyncJobProvider):
    """
    Provider OR Video.

    Wymaga `requests` (już w requirements ChatADHD).
    """

    provider_name = "openrouter:video"

    def __init__(self, api_key: str,
                 base_url: str = "https://openrouter.ai/api/v1",
                 http_referer: Optional[str] = None,
                 app_name: Optional[str] = None) -> None:
        self._api_key = api_key
        self._base_url = base_url.rstrip("/")
        self._http_referer = http_referer  # OR app attribution
        self._app_name = app_name

    def _headers(self, content_type: Optional[str] = None) -> Dict[str, str]:
        h = {
            "Authorization": f"Bearer {self._api_key}",
        }
        if content_type:
            h["Content-Type"] = content_type
        if self._http_referer:
            h["HTTP-Referer"] = self._http_referer
        if self._app_name:
            h["X-Title"] = self._app_name
        return h

    # ─── submit ──────────────────────────────────────────

    def submit(self, job: AsyncJob) -> AsyncJob:
        import requests

        url = f"{self._base_url}/videos"
        payload = self._build_submit_payload(job)

        log.info("OR video submit: model=%s prompt_len=%d has_images=%s",
                 payload.get("model"),
                 len(payload.get("prompt", "")),
                 bool(payload.get("frame_images")
                      or payload.get("input_references")))

        resp = requests.post(url, headers=self._headers("application/json"),
                              json=payload, timeout=60)
        if resp.status_code not in (200, 202):
            job.status = JobStatus.FAILED
            job.error = f"submit HTTP {resp.status_code}: {resp.text[:500]}"
            job.response_payload = {"status_code": resp.status_code,
                                      "body": resp.text[:1000]}
            return job

        data = resp.json()
        job.provider_job_id = data.get("id")
        job.polling_url = data.get("polling_url")
        job.response_payload = data
        # Map status string → enum
        provider_status = data.get("status", "pending")
        job.status = _map_status(provider_status)
        job.updated_at = time.time()
        return job

    def _build_submit_payload(self, job: AsyncJob) -> Dict[str, Any]:
        """
        Buduje body dla POST /videos.

        Wyciąga wartości z job.request_payload — pipeline je tam umieszcza
        w formacie gotowym do OR. Walidacja minimalna (model, prompt są
        wymagane).
        """
        body = dict(job.request_payload)  # shallow copy
        # Sanity: model + prompt required
        if "model" not in body:
            raise ValueError("OR video submit: 'model' required in request_payload")
        if "prompt" not in body:
            raise ValueError("OR video submit: 'prompt' required in request_payload")
        return body

    # ─── poll ────────────────────────────────────────────

    def poll(self, job: AsyncJob) -> AsyncJob:
        import requests

        url = job.polling_url or f"{self._base_url}/videos/{job.provider_job_id}"
        if not job.provider_job_id and not job.polling_url:
            job.status = JobStatus.FAILED
            job.error = "no provider_job_id or polling_url to poll"
            return job

        resp = requests.get(url, headers=self._headers(), timeout=30)
        if resp.status_code != 200:
            # Transient — nie zmieniamy statusu, tylko zapisujemy error.
            job.error = f"poll HTTP {resp.status_code}: {resp.text[:300]}"
            return job

        data = resp.json()
        job.response_payload = data
        provider_status = data.get("status", "pending")
        job.status = _map_status(provider_status)

        if job.status == JobStatus.COMPLETED:
            job.external_urls = list(data.get("unsigned_urls", []))
            usage = data.get("usage", {}) or {}
            job.cost_usd = float(usage.get("cost", 0.0) or 0.0)
        elif job.status == JobStatus.FAILED:
            err = data.get("error") or "provider reported failed"
            if isinstance(err, dict):
                err = err.get("message", str(err))
            job.error = str(err)

        return job

    # ─── fetch ───────────────────────────────────────────

    def fetch_artifact(self, job: AsyncJob, output_path: Path) -> AsyncJob:
        import requests

        if not job.external_urls:
            raise ValueError(f"No external_urls for job {job.job_id}")

        # Wybierz index=0 (zazwyczaj jeden klip).
        url = job.external_urls[0]

        # Upewnij się że output ma .mp4 (OR zawsze zwraca mp4 dla video).
        output_path = Path(output_path)
        if output_path.suffix != ".mp4":
            output_path = output_path.with_suffix(".mp4")
        output_path.parent.mkdir(parents=True, exist_ok=True)

        log.info("OR video fetch: %s → %s", url, output_path)

        # Stream żeby nie trzymać w pamięci (duże pliki).
        with requests.get(url, headers=self._headers(),
                           stream=True, timeout=120) as r:
            r.raise_for_status()
            with open(output_path, "wb") as f:
                for chunk in r.iter_content(chunk_size=1024 * 1024):
                    if chunk:
                        f.write(chunk)

        job.local_artifact_path = str(output_path)
        return job


# ─── Helpers ────────────────────────────────────────────────────

def _map_status(provider_status: str) -> JobStatus:
    """Mapuje status OR → nasz JobStatus."""
    s = (provider_status or "").lower().strip()
    if s == "completed":
        return JobStatus.COMPLETED
    if s == "in_progress":
        return JobStatus.IN_PROGRESS
    if s == "failed":
        return JobStatus.FAILED
    # "pending" i cokolwiek innego traktujemy jako pending.
    return JobStatus.PENDING


# ─── Model discovery ────────────────────────────────────────────

def fetch_video_models_from_openrouter(api_key: str,
                                        base_url: str = "https://openrouter.ai/api/v1"
                                        ) -> List[Dict[str, Any]]:
    """
    Pobiera pełną listę video models z /api/v1/videos/models.

    Zwraca listę dict'ów z polami zgodnie z dokumentacją:
      - id, canonical_slug, name, description, created
      - supported_resolutions, supported_aspect_ratios, supported_sizes
      - pricing_skus (dict {sku_name: price_str})
      - allowed_passthrough_parameters

    Używane przez VideoModelRegistry.sync_from_openrouter().
    """
    import requests

    url = f"{base_url.rstrip('/')}/videos/models"
    resp = requests.get(
        url,
        headers={"Authorization": f"Bearer {api_key}"},
        timeout=30,
    )
    if resp.status_code != 200:
        log.warning("OR /videos/models returned %d: %s",
                    resp.status_code, resp.text[:200])
        return []
    data = resp.json()
    return list(data.get("data", []))
