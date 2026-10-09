"""
ChatADHD v0.07.09 - Background Semantic Worker

Persistent daemon thread that drains 'pending' messages.  Three modes:

  1. REGEX-ONLY  - instant, free, runs locally
  2. LLM ONLINE  - one-by-one via OpenRouter (throttled, ~2 req/s)
  3. LLM BATCH   - Anthropic Message Batches API (50% cheaper, async)

The worker sleeps when the queue is empty and wakes on new messages
(event bus) or periodic poll.
"""
import hashlib
import json
import logging
import threading
import time
import uuid
from datetime import datetime, timezone
from typing import Any, Optional

import requests

from engine.events import bus, IMPORT_DONE, GRAPH_CHANGED

log = logging.getLogger(__name__)

SEMANTIC_PROGRESS = "semantic:progress"

_DRAIN_BATCH = 50
_IDLE_POLL = 30.0
_LLM_RATE_LIMIT = 2.0


class SemanticWorker:
    """Background thread for semantic analysis of pending messages."""

    def __init__(self, db, semantic_llm, graph_engine, config, secrets):
        self.db = db
        self.semantic_llm = semantic_llm
        self.graph_engine = graph_engine
        self.config = config
        self.secrets = secrets

        self._thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._wake_event = threading.Event()
        self._paused = False

        self._processed = 0
        self._errors = 0
        self._rate = 0.0
        self._mode = "idle"

        self._active_batch_id: Optional[str] = None
        self._batch_submitted = 0

        bus.on(IMPORT_DONE, lambda _: self.wake())

    # -- Lifecycle ---------------------------------------------------

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(
            target=self._run, name="SemanticWorker", daemon=True
        )
        self._thread.start()
        log.info("SemanticWorker started")

    def stop(self) -> None:
        self._stop_event.set()
        self._wake_event.set()
        if self._thread:
            self._thread.join(timeout=5)
        log.info("SemanticWorker stopped (%d processed, %d errors)",
                 self._processed, self._errors)

    def wake(self) -> None:
        self._wake_event.set()

    def pause(self) -> None:
        self._paused = True
        self._mode = "paused"

    def resume(self) -> None:
        self._paused = False
        self.wake()

    @property
    def status(self) -> dict:
        pending = 0
        try:
            pending = self.db.count_pending_semantic()
        except Exception:
            pass
        return {
            "pending": pending,
            "processed": self._processed,
            "errors": self._errors,
            "mode": self._mode,
            "rate": f"{self._rate:.1f}/s",
            "batch_id": self._active_batch_id,
            "batch_submitted": self._batch_submitted,
        }

    # -- Main loop ---------------------------------------------------

    def _run(self) -> None:
        time.sleep(3)  # let startup finish
        while not self._stop_event.is_set():
            try:
                if self._paused:
                    self._wake_event.wait(timeout=5)
                    self._wake_event.clear()
                    continue

                processed = self._drain_batch()
                if processed == 0:
                    if self._active_batch_id:
                        self._check_batch_status()
                    self._mode = "idle"
                    self._wake_event.wait(timeout=_IDLE_POLL)
                    self._wake_event.clear()
            except Exception:
                log.exception("SemanticWorker loop error")
                time.sleep(5)

    def _drain_batch(self) -> int:
        msgs = self.db.get_unanalysed_msgs(limit=_DRAIN_BATCH)
        if not msgs:
            return 0

        use_llm = (
            self.semantic_llm
            and self.semantic_llm.enabled
            and self.config.get("semantic_analysis", True)
        )

        pending_total = self.db.count_pending_semantic()
        if pending_total > 500 and use_llm and self._can_use_batch_api():
            return self._submit_batch_api()

        mode = "llm" if use_llm else "regex"
        self._mode = mode
        t0 = time.monotonic()
        count = 0

        for msg in msgs:
            if self._stop_event.is_set() or self._paused:
                break
            message = None
            started = datetime.now(timezone.utc).isoformat()
            try:
                message = self.db.get_msg(msg["id"])
                if not message or message["semantic_status"] != "pending":
                    continue
                self._failure_metadata(message)
                if use_llm:
                    analysis = self.semantic_llm.analyse(message["text"])
                    time.sleep(1.0 / _LLM_RATE_LIMIT)
                else:
                    analysis = self._regex_analyse(message["text"])

                self.graph_engine.ingest_analysis(
                    message["id"], message["conv_id"], analysis
                )
                self.db.mark_analysed(msg["id"], analysis)
                count += 1
                self._processed += 1

            except Exception as exc:
                log.debug("Worker fail on %s", msg["id"], exc_info=True)
                self._errors += 1
                try:
                    if message is None:
                        raise RuntimeError("Cannot record failure without a message snapshot") from exc
                    self._record_failure(message, started, exc, mode)
                except Exception:
                    # Do not replay a possibly dispatched request when its
                    # failure cannot be persisted. Existing pause/resume is
                    # explicit; no retry or destructive metadata fallback.
                    self.pause()
                    log.exception("Cannot persist semantic failure for %s; worker paused", msg["id"])

        elapsed = time.monotonic() - t0
        self._rate = count / max(elapsed, 0.01)
        if count > 0:
            remaining = self.db.count_pending_semantic()
            log.info("Semantic: %d done (%s, %.1f/s, %d left)",
                     count, self._mode, self._rate, remaining)
            bus.emit(SEMANTIC_PROGRESS, {
                "processed": count, "pending": remaining, "mode": self._mode,
            })
        return count

    @staticmethod
    def _failure_metadata(message: dict) -> dict:
        metadata = message["metadata"]
        if not isinstance(metadata, dict):
            raise ValueError("Semantic failure evidence requires object message metadata")
        if "loom_semantic_failures" in metadata and not isinstance(metadata["loom_semantic_failures"], list):
            raise ValueError("Occupied loom_semantic_failures must be preserved as an array")
        return metadata

    def _record_failure(self, message: dict, started: str, error: Exception,
                        mode: str, evidence: Optional[dict] = None) -> None:
        """Preserve failure evidence; this is not native atomic attempt tracking.

        Failed rows leave the pending queue. Only an explicit existing
        update_msg(semantic_status="pending") selects them for another attempt.
        GraphEngine's absorbed errors and partial graph writes remain a separate
        legacy limitation; this records exceptions visible at the worker seam.
        """
        failure = {
            "schema": "loom.semantic_failure/1",
            "id": "sf_" + uuid.uuid4().hex,
            "state": "failed",
            "started": started,
            "finished": datetime.now(timezone.utc).isoformat(),
            "source": {
                "message_id": message["id"], "conv_id": message["conv_id"],
                "role": message["role"],
                "text_sha256": hashlib.sha256(message["text"].encode("utf-8")).hexdigest(),
                "version_num": message["version_num"],
                "version_group_id": message["version_group_id"],
            },
            "provenance": {"consumer": "python.semantic_worker", "mode": mode},
            "error": {"code": type(error).__name__, "message": str(error)},
        }
        if evidence is not None:
            failure["evidence"] = evidence
        # Re-read under the existing store lock so changes made during a request
        # survive. Only this metadata/status update is atomic, not graph writes.
        with self.db._lock:
            current = self.db.get_msg(message["id"])
            if not current or current["semantic_status"] != "pending":
                return
            metadata = self._failure_metadata(current)
            metadata["loom_semantic_failures"] = metadata.get("loom_semantic_failures", []) + [failure]
            self.db.update_msg(message["id"], metadata=metadata, semantic_status="failed")

    def _regex_analyse(self, text: str) -> dict:
        from core.semantic import analyzer as regex
        raw = regex.analyse(text)
        return {
            "entities": [
                {"name": e.text, "kind": e.entity_type.value,
                 "relevance": e.confidence}
                for e in raw.get("entities", [])
            ],
            "topics": [
                {"label": t, "confidence": 0.5}
                for t in raw.get("topics", [])
            ],
            "relations": [
                {"subject": r.subject, "predicate": r.predicate.value,
                 "object": r.obj}
                for r in raw.get("relations", [])
            ],
            "summary": "",
            "sentiment": "neutral",
            "source": "regex",
        }

    # ================================================================
    # Anthropic Message Batches API
    #
    # POST /v1/messages/batches   (submit up to 10k requests)
    # GET  /v1/messages/batches/{id}   (poll status)
    # GET  /v1/messages/batches/{id}/results   (JSONL stream)
    #
    # 50% discount vs real-time.
    # Haiku batch: ~$0.125/M input, 200k msgs ~ $12
    # ================================================================

    def _can_use_batch_api(self) -> bool:
        return bool(self.secrets.get("anthropic_batch_key"))

    def _get_batch_headers(self) -> dict:
        key = self.secrets.get("anthropic_batch_key", "")
        return {
            "x-api-key": key,
            "anthropic-version": "2023-06-01",
            "content-type": "application/json",
        }

    def _submit_batch_api(self) -> int:
        if self._active_batch_id:
            return 0

        self._mode = "batch_submit"
        msgs = self.db.get_unanalysed_msgs(limit=10000)
        if not msgs:
            return 0

        model = self.config.get("semantic_model", "")
        if "/" in model:
            model = model.split("/", 1)[1]

        from engine.semantic_llm import _ANALYSIS_PROMPT

        batch_requests = []
        for msg in msgs:
            batch_requests.append({
                "custom_id": msg["id"],
                "params": {
                    "model": model,
                    "max_tokens": 800,
                    "messages": [
                        {"role": "user",
                         "content": _ANALYSIS_PROMPT + msg["text"][:3000]},
                    ],
                },
            })

        try:
            resp = requests.post(
                "https://api.anthropic.com/v1/messages/batches",
                headers=self._get_batch_headers(),
                json={"requests": batch_requests},
                timeout=120,
            )
            if resp.status_code in (200, 201):
                data = resp.json()
                self._active_batch_id = data.get("id")
                self._batch_submitted = len(batch_requests)
                cost = self._batch_submitted * 0.000065
                log.info("Batch submitted: %s (%d msgs, ~$%.2f)",
                         self._active_batch_id, self._batch_submitted, cost)
                self._mode = "batch_wait"
                return 0
            else:
                log.warning("Batch submit failed %d: %s",
                            resp.status_code, resp.text[:200])
                return 0
        except Exception:
            log.exception("Batch submit error")
            return 0

    def _check_batch_status(self) -> None:
        if not self._active_batch_id:
            return
        self._mode = "batch_poll"
        try:
            resp = requests.get(
                "https://api.anthropic.com/v1/messages/batches/"
                + self._active_batch_id,
                headers=self._get_batch_headers(),
                timeout=30,
            )
            if resp.status_code != 200:
                log.warning("Batch poll failed: %d", resp.status_code)
                return

            data = resp.json()
            status = data.get("processing_status", "")
            counts = data.get("request_counts", {})
            log.info("Batch %s: %s (ok=%s, err=%s)",
                     self._active_batch_id, status,
                     counts.get("succeeded", "?"),
                     counts.get("errored", "?"))

            if status == "ended":
                self._fetch_batch_results()
                self._active_batch_id = None
                self._batch_submitted = 0
            elif status in ("failed", "canceled", "expired"):
                log.warning("Batch %s: %s", self._active_batch_id, status)
                self._active_batch_id = None
                self._batch_submitted = 0
        except Exception:
            log.exception("Batch poll error")

    def _fetch_batch_results(self) -> None:
        self._mode = "batch_ingest"
        try:
            resp = requests.get(
                "https://api.anthropic.com/v1/messages/batches/"
                + self._active_batch_id + "/results",
                headers=self._get_batch_headers(),
                timeout=300,
                stream=True,
            )
            if resp.status_code != 200:
                log.warning("Batch results failed: %d", resp.status_code)
                return

            count = 0
            errors = 0
            for line in resp.iter_lines(decode_unicode=True):
                if not line:
                    continue
                message = None
                started = datetime.now(timezone.utc).isoformat()
                evidence = {"batch_id": self._active_batch_id}
                try:
                    item = json.loads(line)
                    msg_id = item.get("custom_id", "")
                    message = self.db.get_msg(msg_id)
                    if not message or message["semantic_status"] != "pending":
                        continue
                    self._failure_metadata(message)
                    result = item.get("result", {})
                    evidence["batch_result"] = result

                    if result.get("type") == "succeeded":
                        raw = ""
                        for block in result.get("message", {}).get("content", []):
                            if block.get("type") == "text":
                                raw += block.get("text", "")

                        raw = raw.strip()
                        if raw.startswith("```"):
                            raw = raw.split("\n", 1)[-1]
                        if raw.endswith("```"):
                            raw = raw.rsplit("```", 1)[0]

                        analysis = json.loads(raw.strip())
                        analysis["source"] = "llm_batch"

                        self.graph_engine.ingest_analysis(
                            msg_id, message["conv_id"], analysis
                        )
                        self.db.mark_analysed(msg_id, analysis)
                        count += 1
                        self._processed += 1
                    else:
                        self._record_failure(message, started,
                            RuntimeError("Batch result did not succeed: " + str(result.get("type"))),
                            "batch", evidence)
                        errors += 1
                        self._errors += 1

                except Exception as exc:
                    errors += 1
                    self._errors += 1
                    if message is not None:
                        try:
                            self._record_failure(message, started, exc, "batch", evidence)
                        except Exception:
                            self.pause()
                            log.exception("Cannot persist batch failure for %s; worker paused", message["id"])
                            break
                    else:
                        log.warning("Cannot associate invalid batch result with a message", exc_info=True)

            log.info("Batch ingested: %d ok, %d errors", count, errors)
            bus.emit(SEMANTIC_PROGRESS, {
                "processed": count,
                "pending": self.db.count_pending_semantic(),
                "mode": "batch",
            })
        except Exception:
            log.exception("Batch results fetch error")
