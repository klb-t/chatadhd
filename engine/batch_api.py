"""
ChatADHD v0.07.08 - Batch Semantic API

Bulk semantic analysis using:
  1. Anthropic Message Batches API (50% cheaper, async, up to 100k reqs)
  2. Concurrent OpenRouter requests (rate-limited parallel)
  3. Local regex-only (free, instant)

The Anthropic Batch API:
  POST /v1/messages/batches
  - Accepts up to 100,000 requests in one batch
  - Results available within 24h (usually ~1h)
  - 50% discount on token pricing
  - Poll for completion via GET /v1/messages/batches/{id}

Usage:
    batcher = SemanticBatchAPI(config, secrets, db)
    job_id = batcher.submit_batch(msg_ids)      # -> batch job ID
    status = batcher.check_batch(job_id)         # -> {status, progress}
    results = batcher.collect_results(job_id)    # -> download & apply
"""
import json
import logging
import time
import threading
from typing import Any, Optional
from concurrent.futures import ThreadPoolExecutor, as_completed

import requests

from core.semantic import analyzer as regex_analyzer

log = logging.getLogger(__name__)

# Reuse the same prompt as SemanticLLM.
_ANALYSIS_PROMPT = """\
Analyse the following chat message. Return ONLY valid JSON (no markdown, no backticks).

{
  "entities": [{"name": "...", "kind": "person|org|place|code|file|concept|product|event", "relevance": 0.0-1.0}],
  "topics": [{"label": "...", "confidence": 0.0-1.0}],
  "relations": [{"subject": "...", "predicate": "mentions|depends_on|references|contradicts|implements|part_of", "object": "..."}],
  "summary": "one line summary",
  "sentiment": "positive|negative|neutral|mixed"
}

Rules:
- Extract ALL named entities (people, orgs, code modules, files, concepts).
- Topics should be domain labels (legal, finance, tech, ai, security, health, project, personal).
- Relations connect entities mentioned in the text.
- Be precise. Fewer high-confidence items > many low-confidence ones.
- If the message is trivial (greetings, "ok", etc.) return empty arrays.

Message:
"""


class SemanticBatchAPI:
    """Batch semantic analysis via Anthropic Batches or concurrent requests."""

    def __init__(self, config, secrets, db):
        self.config = config
        self.secrets = secrets
        self.db = db
        self._active_batches: dict[str, dict] = {}  # job_id -> info

    # ── Anthropic Batch API ──────────────────────────────────

    def submit_anthropic_batch(self, msg_ids: list[str],
                                model: str = "") -> Optional[str]:
        """Submit a batch to Anthropic's Message Batches API.
        Returns batch_id or None on failure.
        
        Requires direct Anthropic API key (not OpenRouter).
        """
        key = self.secrets.get("api_key", "")
        model = model or self.config.get("semantic_model", "")

        if not key or not model:
            log.warning("Batch API: missing API key or model")
            return None

        # Build batch requests.
        requests_list = []
        for msg_id in msg_ids:
            # Fetch message text.
            msgs = self.db.get_unanalysed_msgs(limit=1)  # This is inefficient
            # Better: fetch by ID directly.
            text = self._get_msg_text(msg_id)
            if not text:
                continue

            requests_list.append({
                "custom_id": msg_id,
                "params": {
                    "model": model,
                    "max_tokens": 800,
                    "messages": [
                        {"role": "user", "content": _ANALYSIS_PROMPT + text[:3000]},
                    ],
                },
            })

        if not requests_list:
            return None

        # Determine API base.
        base_url = self.config.get("base_url", "").rstrip("/")
        is_anthropic_direct = "anthropic.com" in base_url

        if is_anthropic_direct:
            return self._submit_anthropic_native(requests_list, key)
        else:
            # OpenRouter doesn't have batch API — use concurrent.
            return self._submit_concurrent(requests_list, base_url, key, model)

    def _submit_anthropic_native(self, requests_list: list,
                                  api_key: str) -> Optional[str]:
        """Submit to Anthropic's /v1/messages/batches endpoint."""
        try:
            resp = requests.post(
                "https://api.anthropic.com/v1/messages/batches",
                headers={
                    "x-api-key": api_key,
                    "anthropic-version": "2023-06-01",
                    "Content-Type": "application/json",
                },
                json={"requests": requests_list},
                timeout=60,
            )

            if resp.status_code in (200, 201):
                data = resp.json()
                batch_id = data.get("id", "")
                self._active_batches[batch_id] = {
                    "type": "anthropic",
                    "count": len(requests_list),
                    "submitted_at": time.time(),
                    "status": "processing",
                }
                log.info("Anthropic batch submitted: %s (%d requests)",
                         batch_id, len(requests_list))
                return batch_id
            else:
                log.warning("Anthropic batch submit failed %d: %s",
                            resp.status_code, resp.text[:200])
                return None

        except Exception:
            log.exception("Anthropic batch submit error")
            return None

    def check_batch(self, batch_id: str) -> dict:
        """Check status of an active batch."""
        info = self._active_batches.get(batch_id)
        if not info:
            return {"status": "unknown", "batch_id": batch_id}

        if info["type"] == "anthropic":
            return self._check_anthropic_batch(batch_id)
        elif info["type"] == "concurrent":
            return info  # Already contains final status.
        return {"status": "unknown"}

    def _check_anthropic_batch(self, batch_id: str) -> dict:
        """Poll Anthropic batch status."""
        key = self.secrets.get("api_key", "")
        try:
            resp = requests.get(
                f"https://api.anthropic.com/v1/messages/batches/{batch_id}",
                headers={
                    "x-api-key": key,
                    "anthropic-version": "2023-06-01",
                },
                timeout=30,
            )
            if resp.status_code == 200:
                data = resp.json()
                status = data.get("processing_status", "unknown")
                counts = data.get("request_counts", {})
                return {
                    "batch_id": batch_id,
                    "status": status,
                    "succeeded": counts.get("succeeded", 0),
                    "errored": counts.get("errored", 0),
                    "total": counts.get("processing", 0) + counts.get("succeeded", 0),
                    "results_url": data.get("results_url"),
                }
        except Exception:
            log.debug("Batch check failed", exc_info=True)
        return {"batch_id": batch_id, "status": "error"}

    def collect_results(self, batch_id: str) -> int:
        """Download and apply batch results. Returns count applied."""
        info = self._active_batches.get(batch_id, {})
        if info.get("type") == "concurrent":
            # Already applied during submission.
            return info.get("applied", 0)

        status = self._check_anthropic_batch(batch_id)
        results_url = status.get("results_url")
        if not results_url:
            log.warning("No results URL for batch %s", batch_id)
            return 0

        key = self.secrets.get("api_key", "")
        try:
            resp = requests.get(
                results_url,
                headers={
                    "x-api-key": key,
                    "anthropic-version": "2023-06-01",
                },
                timeout=120,
                stream=True,
            )
            applied = 0
            for line in resp.iter_lines():
                if not line:
                    continue
                try:
                    result = json.loads(line)
                    msg_id = result.get("custom_id", "")
                    resp_body = result.get("result", {})
                    if resp_body.get("type") == "succeeded":
                        content = resp_body["message"]["content"][0]["text"]
                        analysis = self._parse_analysis(content)
                        if analysis:
                            self.db.mark_analysed(msg_id, analysis)
                            applied += 1
                except Exception:
                    log.debug("Failed to parse batch result line", exc_info=True)

            log.info("Batch %s: applied %d results", batch_id, applied)
            return applied

        except Exception:
            log.exception("Failed to collect batch results")
            return 0

    # ── Concurrent (OpenRouter / any provider) ───────────────

    def _submit_concurrent(self, requests_list: list, base_url: str,
                            api_key: str, model: str) -> str:
        """Process batch via concurrent HTTP requests (rate-limited).
        Returns a synthetic batch_id. Processes in background thread.
        """
        batch_id = f"concurrent_{int(time.time())}"
        self._active_batches[batch_id] = {
            "type": "concurrent",
            "count": len(requests_list),
            "submitted_at": time.time(),
            "status": "processing",
            "applied": 0,
        }

        def _process():
            applied = 0
            max_workers = 3  # Conservative for rate limits.

            with ThreadPoolExecutor(max_workers=max_workers) as pool:
                futures = {}
                for req in requests_list:
                    msg_id = req["custom_id"]
                    text = req["params"]["messages"][0]["content"]
                    fut = pool.submit(
                        self._single_request,
                        base_url, api_key, model, text,
                    )
                    futures[fut] = msg_id

                for fut in as_completed(futures):
                    msg_id = futures[fut]
                    try:
                        analysis = fut.result()
                        if analysis:
                            self.db.mark_analysed(msg_id, analysis)
                            applied += 1
                    except Exception:
                        log.debug("Concurrent req failed for %s", msg_id)

            self._active_batches[batch_id]["status"] = "completed"
            self._active_batches[batch_id]["applied"] = applied
            log.info("Concurrent batch %s: applied %d/%d",
                     batch_id, applied, len(requests_list))

        t = threading.Thread(target=_process, daemon=True)
        t.start()
        return batch_id

    def _single_request(self, base_url: str, api_key: str,
                         model: str, prompt: str) -> Optional[dict]:
        """Single semantic API call."""
        resp = requests.post(
            f"{base_url}/chat/completions",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": model,
                "messages": [{"role": "user", "content": prompt}],
                "temperature": 0.1,
                "max_tokens": 800,
            },
            timeout=30,
        )
        if resp.status_code != 200:
            return None

        raw = resp.json()["choices"][0]["message"]["content"]
        return self._parse_analysis(raw)

    # ── Regex-only batch (instant, free) ─────────────────────

    def batch_regex(self, limit: int = 5000) -> int:
        """Process unanalysed messages with regex only.
        Very fast, zero cost. Good for Phase 1 of import.
        """
        msgs = self.db.get_unanalysed_msgs(limit=limit)
        count = 0
        for msg in msgs:
            text = msg.get("text", "")
            if not text.strip():
                self.db.mark_analysed(msg["id"], {"source": "skip"})
                continue

            result = regex_analyzer.analyse(text)
            analysis = {
                "entities": [
                    {"name": e.text, "kind": e.entity_type.value,
                     "relevance": e.confidence}
                    for e in result.get("entities", [])
                ],
                "topics": [
                    {"label": t, "confidence": 0.5}
                    for t in result.get("topics", [])
                ],
                "summary": "",
                "sentiment": "neutral",
                "source": "regex",
            }
            self.db.mark_analysed(msg["id"], analysis)
            count += 1

        log.info("Regex batch: analysed %d messages", count)
        return count

    # ── Helpers ───────────────────────────────────────────────

    def _get_msg_text(self, msg_id: str) -> str:
        """Get message text by ID."""
        try:
            with self.db._lock:
                row = self.db._conn.execute(
                    "SELECT text FROM messages WHERE id = ?", (msg_id,),
                ).fetchone()
            return row["text"] if row else ""
        except Exception:
            return ""

    @staticmethod
    def _parse_analysis(raw: str) -> Optional[dict]:
        """Parse LLM response into analysis dict."""
        raw = raw.strip()
        if raw.startswith("```"):
            raw = raw.split("\n", 1)[-1]
        if raw.endswith("```"):
            raw = raw.rsplit("```", 1)[0]
        raw = raw.strip()

        try:
            result = json.loads(raw)
            result["source"] = "llm"
            return result
        except json.JSONDecodeError:
            return None
