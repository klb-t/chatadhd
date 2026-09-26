"""
engine/async_jobs.py
=====================

Uogólniona abstrakcja dla operacji które nie wracają w jednym żądaniu:
submit → poll → fetch. Wzorzec OpenRouter Video API, Anthropic Batches,
przyszłe TTS/ASR offload, cokolwiek ciężkiego.

Fundamentalne zasady:
  - Separacja: rzeczywistość (status po stronie providera) od decyzji
    (kiedy poll, kiedy cancel, kiedy fallback).
  - Invariant queryable: każdy job odpowiada na status() w spójny sposób
    niezależnie od providera.
  - Fallback z metadanymi: job który się nie udał zostawia pełny ślad
    (error, provider_response, koszt) do analizy.
  - Multiplatformowość: logika jobs jest czysto Python. Poll odpala się
    z watka tła, powiadomienia przez event bus.
  - Nic nie ginie: jobs persistowane w SQLite przez cały cykl życia.
    Po restarcie apki niezakończone jobs można podjąć.

Relacja do istniejącego kodu:
  - engine/batch_api.py (Anthropic Message Batches) to konkretny przypadek
    tej abstrakcji. W następnej iteracji można go zrefaktorować do IAsyncJob,
    ale na teraz zostaje osobno (nie psujemy działającego).
  - engine/events.py — eventy job:submitted / job:progress / job:completed /
    job:failed / job:cancelled.
"""

from __future__ import annotations

import json
import logging
import sqlite3
import threading
import time
import uuid
from abc import ABC, abstractmethod
from dataclasses import dataclass, field, asdict
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

log = logging.getLogger(__name__)


# ─── Events (dodawane do engine/events.py) ──────────────────────

EVT_JOB_SUBMITTED = "job:submitted"
EVT_JOB_PROGRESS = "job:progress"
EVT_JOB_COMPLETED = "job:completed"
EVT_JOB_FAILED = "job:failed"
EVT_JOB_CANCELLED = "job:cancelled"


# ─── Status ─────────────────────────────────────────────────────

class JobStatus(str, Enum):
    PENDING = "pending"          # submitted, waiting provider
    IN_PROGRESS = "in_progress"  # provider working
    COMPLETED = "completed"      # done, artifact available
    FAILED = "failed"            # provider returned error
    CANCELLED = "cancelled"      # user cancelled
    STALE = "stale"              # poll timeout exceeded


# ─── Job record ─────────────────────────────────────────────────

@dataclass
class AsyncJob:
    """
    Pełny zapis job'u. Serializowalny do SQLite.

    job_kind: kategoria dla filtrowania i routingu ("video", "batch",
        "transcription", ...). Używane też do subskrybcji eventów
        per-kategoria.

    provider_job_id: ID zwrócone przez providera (u nas OR zwraca "abc123"
        w polu `id`). Używane do pollingu.

    external_urls: linki do pobrania artefaktów. Dla OR video: unsigned_urls.

    request_payload: oryginalny submit body. Jest potrzebne do retry.
    response_payload: ostatnia odpowiedź pollingu (dla diagnostyki).

    context: wolne metadane użytkownika — np. "graph_id" i "scene_id"
        z video pipeline'u. Pozwala związać job z domenowym obiektem
        bez wiedzy async_jobs.py o domenie.
    """
    job_id: str                                 # nasz ID
    job_kind: str
    provider: str                                # "openrouter", "anthropic", ...
    provider_model: Optional[str] = None
    provider_job_id: Optional[str] = None
    status: JobStatus = JobStatus.PENDING
    submitted_at: float = field(default_factory=time.time)
    updated_at: float = field(default_factory=time.time)
    completed_at: Optional[float] = None
    polling_url: Optional[str] = None
    external_urls: List[str] = field(default_factory=list)
    local_artifact_path: Optional[str] = None
    cost_usd: float = 0.0
    error: str = ""
    request_payload: Dict[str, Any] = field(default_factory=dict)
    response_payload: Dict[str, Any] = field(default_factory=dict)
    context: Dict[str, Any] = field(default_factory=dict)
    poll_count: int = 0
    max_poll_seconds: float = 3600.0  # 1h timeout default

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["status"] = self.status.value
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "AsyncJob":
        return cls(
            job_id=d["job_id"],
            job_kind=d["job_kind"],
            provider=d["provider"],
            provider_model=d.get("provider_model"),
            provider_job_id=d.get("provider_job_id"),
            status=JobStatus(d.get("status", "pending")),
            submitted_at=d.get("submitted_at", time.time()),
            updated_at=d.get("updated_at", time.time()),
            completed_at=d.get("completed_at"),
            polling_url=d.get("polling_url"),
            external_urls=list(d.get("external_urls", [])),
            local_artifact_path=d.get("local_artifact_path"),
            cost_usd=float(d.get("cost_usd", 0.0)),
            error=d.get("error", ""),
            request_payload=dict(d.get("request_payload", {})),
            response_payload=dict(d.get("response_payload", {})),
            context=dict(d.get("context", {})),
            poll_count=int(d.get("poll_count", 0)),
            max_poll_seconds=float(d.get("max_poll_seconds", 3600.0)),
        )

    def is_terminal(self) -> bool:
        return self.status in (JobStatus.COMPLETED, JobStatus.FAILED,
                                JobStatus.CANCELLED, JobStatus.STALE)

    def elapsed(self) -> float:
        return time.time() - self.submitted_at


# ─── IAsyncJobProvider ──────────────────────────────────────────

class IAsyncJobProvider(ABC):
    """
    Konkretny provider async-jobs. Implementacje:
      - OpenRouterVideoProvider
      - AnthropicBatchProvider (refaktor existing batch_api.py, później)
      - ReplicateProvider (gdy dodamy)

    Każdy provider implementuje trzy metody. Reszta jest wspólna
    (store, poller, retry) w AsyncJobManager.
    """

    provider_name: str = "abstract"

    @abstractmethod
    def submit(self, job: AsyncJob) -> AsyncJob:
        """
        Wysyła request do providera. Zwraca job z wypełnionym
        provider_job_id, polling_url, status=pending albo in_progress.
        Może rzucić wyjątek — manager obsłuży.
        """
        ...

    @abstractmethod
    def poll(self, job: AsyncJob) -> AsyncJob:
        """
        Sprawdza status. Zwraca job z zaktualizowanym status/external_urls/
        error. Nie pobiera artefaktu — to osobny krok.
        """
        ...

    @abstractmethod
    def fetch_artifact(self, job: AsyncJob, output_path: Path) -> AsyncJob:
        """
        Ściąga artefakt z external_urls do output_path. Aktualizuje
        local_artifact_path i koszt. Wywoływane po status=completed.
        """
        ...

    def cancel(self, job: AsyncJob) -> AsyncJob:
        """
        Opcjonalne. Domyślnie tylko oznacza lokalnie jako cancelled —
        większość providerów nie pozwala na cancel po submit.
        """
        job.status = JobStatus.CANCELLED
        job.updated_at = time.time()
        return job


# ─── Store (SQLite) ─────────────────────────────────────────────

class AsyncJobStore:
    """
    Persystencja jobs w tej samej SQLite co reszta ChatADHD.
    Osobna tabela żeby nie kolidować ze schema_version w db.py.

    Uwaga: nie dziedziczymy z engine/db.py — jobs są ortogonalne do
    conversations/messages/nodes/links. Mogą być trzymane w osobnym pliku
    albo w tym samym (opcja).
    """

    _SCHEMA = """
    CREATE TABLE IF NOT EXISTS async_jobs (
        job_id TEXT PRIMARY KEY,
        job_kind TEXT NOT NULL,
        provider TEXT NOT NULL,
        status TEXT NOT NULL,
        submitted_at REAL NOT NULL,
        updated_at REAL NOT NULL,
        completed_at REAL,
        data TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_jobs_status ON async_jobs(status);
    CREATE INDEX IF NOT EXISTS idx_jobs_kind ON async_jobs(job_kind);
    CREATE INDEX IF NOT EXISTS idx_jobs_updated ON async_jobs(updated_at DESC);
    """

    def __init__(self, db_path: Path) -> None:
        self._db_path = Path(db_path)
        self._lock = threading.RLock()
        self._init_schema()

    def _conn(self) -> sqlite3.Connection:
        c = sqlite3.connect(str(self._db_path), timeout=10.0,
                             isolation_level=None)
        c.execute("PRAGMA journal_mode=WAL")
        c.execute("PRAGMA synchronous=NORMAL")
        return c

    def _init_schema(self) -> None:
        with self._lock:
            c = self._conn()
            try:
                c.executescript(self._SCHEMA)
            finally:
                c.close()

    def save(self, job: AsyncJob) -> None:
        with self._lock:
            c = self._conn()
            try:
                c.execute(
                    "INSERT OR REPLACE INTO async_jobs "
                    "(job_id, job_kind, provider, status, submitted_at, "
                    " updated_at, completed_at, data) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                    (job.job_id, job.job_kind, job.provider,
                     job.status.value, job.submitted_at, job.updated_at,
                     job.completed_at, json.dumps(job.to_dict())),
                )
            finally:
                c.close()

    def load(self, job_id: str) -> Optional[AsyncJob]:
        with self._lock:
            c = self._conn()
            try:
                row = c.execute(
                    "SELECT data FROM async_jobs WHERE job_id = ?",
                    (job_id,)
                ).fetchone()
                if row is None:
                    return None
                return AsyncJob.from_dict(json.loads(row[0]))
            finally:
                c.close()

    def list_by_status(self, status: JobStatus,
                       kind: Optional[str] = None) -> List[AsyncJob]:
        with self._lock:
            c = self._conn()
            try:
                if kind:
                    rows = c.execute(
                        "SELECT data FROM async_jobs "
                        "WHERE status = ? AND job_kind = ? "
                        "ORDER BY submitted_at ASC",
                        (status.value, kind)
                    ).fetchall()
                else:
                    rows = c.execute(
                        "SELECT data FROM async_jobs WHERE status = ? "
                        "ORDER BY submitted_at ASC",
                        (status.value,)
                    ).fetchall()
                return [AsyncJob.from_dict(json.loads(r[0])) for r in rows]
            finally:
                c.close()

    def list_active(self, kind: Optional[str] = None) -> List[AsyncJob]:
        """Wszystkie non-terminalne jobs. Używane po restarcie do wznowienia."""
        out = []
        for s in (JobStatus.PENDING, JobStatus.IN_PROGRESS):
            out.extend(self.list_by_status(s, kind))
        return out

    def delete(self, job_id: str) -> bool:
        with self._lock:
            c = self._conn()
            try:
                r = c.execute(
                    "DELETE FROM async_jobs WHERE job_id = ?",
                    (job_id,)
                )
                return r.rowcount > 0
            finally:
                c.close()


# ─── Manager ────────────────────────────────────────────────────

class AsyncJobManager:
    """
    Koordynuje providers + store + polling worker + event bus.

    Użycie:
        mgr = AsyncJobManager(store, providers={
            "openrouter:video": OpenRouterVideoProvider(api_key),
        })
        mgr.start()  # uruchamia worker

        job = mgr.submit(
            kind="video",
            provider="openrouter:video",
            request_payload={"model": "google/veo-3.1", "prompt": "..."},
            context={"graph_id": "g_abc", "scene_id": "sc_xyz"},
        )

        # Pipeline słucha eventu job:completed i pobiera artefakt.

    Worker w tle:
      - co poll_interval sekund listuje active jobs
      - dla każdego: provider.poll()
      - jeśli completed → provider.fetch_artifact() → event
      - jeśli failed → event
      - jeśli stale (elapsed > max_poll_seconds) → status=stale + event

    Zatrzymanie: mgr.stop(). Worker kończy bieżącą iterację i kończy.
    """

    def __init__(self, store: AsyncJobStore,
                 providers: Dict[str, IAsyncJobProvider],
                 artifacts_dir: Path,
                 poll_interval_s: float = 30.0,
                 event_bus: Any = None) -> None:
        self._store = store
        self._providers = dict(providers)
        self._artifacts_dir = Path(artifacts_dir)
        self._artifacts_dir.mkdir(parents=True, exist_ok=True)
        self._poll_interval = poll_interval_s
        self._bus = event_bus or self._try_import_bus()
        self._worker: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._poll_lock = threading.Lock()

    @staticmethod
    def _try_import_bus() -> Any:
        try:
            from engine.events import bus
            return bus
        except ImportError:
            return None

    # ─── Registration ────────────────────────────────────

    def register_provider(self, key: str, provider: IAsyncJobProvider) -> None:
        self._providers[key] = provider

    # ─── Submit / cancel ─────────────────────────────────

    def submit(self, *, kind: str, provider: str,
                request_payload: Dict[str, Any],
                context: Optional[Dict[str, Any]] = None,
                provider_model: Optional[str] = None,
                max_poll_seconds: float = 3600.0) -> AsyncJob:
        log.info("AsyncJobManager.submit: kind=%s provider=%s model=%s ctx=%s",
                  kind, provider, provider_model,
                  list(context.keys()) if context else [])
        prov = self._providers.get(provider)
        if prov is None:
            log.error("AsyncJobManager.submit: provider not registered: %s "
                       "(available: %s)", provider, list(self._providers.keys()))
            raise ValueError(f"Provider not registered: {provider}")

        job = AsyncJob(
            job_id=f"j_{uuid.uuid4().hex[:12]}",
            job_kind=kind,
            provider=provider,
            provider_model=provider_model,
            request_payload=dict(request_payload),
            context=dict(context or {}),
            max_poll_seconds=max_poll_seconds,
        )
        log.info("AsyncJobManager.submit: created job_id=%s", job.job_id)

        try:
            job = prov.submit(job)
            log.info("AsyncJobManager.submit: provider.submit OK, "
                      "provider_job_id=%s status=%s",
                      job.provider_job_id, job.status.value)
        except Exception as e:
            log.exception("AsyncJobManager.submit: provider.submit FAILED "
                           "for job=%s", job.job_id)
            job.status = JobStatus.FAILED
            job.error = f"submit_failed: {e}"
            job.updated_at = time.time()
            self._store.save(job)
            self._emit(EVT_JOB_FAILED, job)
            return job

        self._store.save(job)
        self._emit(EVT_JOB_SUBMITTED, job)
        return job

    def cancel(self, job_id: str) -> Optional[AsyncJob]:
        log.info("AsyncJobManager.cancel: %s", job_id)
        job = self._store.load(job_id)
        if job is None:
            log.warning("AsyncJobManager.cancel: job not found: %s", job_id)
            return None
        if job.is_terminal():
            log.info("AsyncJobManager.cancel: already terminal (status=%s)",
                      job.status.value)
            return job
        prov = self._providers.get(job.provider)
        if prov is not None:
            try:
                job = prov.cancel(job)
                log.info("AsyncJobManager.cancel: provider.cancel OK")
            except Exception:
                log.exception("AsyncJobManager.cancel: provider.cancel raised; "
                               "marking locally")
                job.status = JobStatus.CANCELLED
                job.updated_at = time.time()
        else:
            log.warning("AsyncJobManager.cancel: provider %s not registered; "
                         "marking locally", job.provider)
            job.status = JobStatus.CANCELLED
            job.updated_at = time.time()
        self._store.save(job)
        self._emit(EVT_JOB_CANCELLED, job)
        return job

    def get(self, job_id: str) -> Optional[AsyncJob]:
        return self._store.load(job_id)

    def list_active(self, kind: Optional[str] = None) -> List[AsyncJob]:
        return self._store.list_active(kind)

    # ─── Worker ──────────────────────────────────────────

    def start(self) -> None:
        if self._worker is not None and self._worker.is_alive():
            return
        self._stop_event.clear()
        self._worker = threading.Thread(
            target=self._run_worker,
            daemon=True,
            name="AsyncJobWorker",
        )
        self._worker.start()
        log.info("AsyncJobManager worker started (poll interval %.1fs)",
                 self._poll_interval)

    def stop(self, timeout: float = 10.0) -> None:
        self._stop_event.set()
        if self._worker is not None:
            self._worker.join(timeout=timeout)
        self._worker = None

    def _run_worker(self) -> None:
        while not self._stop_event.is_set():
            try:
                self._poll_all_active()
            except Exception:
                log.exception("Worker iteration failed; continuing")
            # Sleep with periodic wake to allow stop.
            self._stop_event.wait(timeout=self._poll_interval)

    def _poll_all_active(self) -> None:
        # Lock: prevent concurrent polls from double-fetching.
        with self._poll_lock:
            active = self._store.list_active()

        for job in active:
            if self._stop_event.is_set():
                return

            # Stale check
            if job.elapsed() > job.max_poll_seconds:
                job.status = JobStatus.STALE
                job.error = "poll timeout exceeded"
                job.updated_at = time.time()
                self._store.save(job)
                self._emit(EVT_JOB_FAILED, job)
                continue

            prov = self._providers.get(job.provider)
            if prov is None:
                log.warning("Provider %s not registered; skipping job %s",
                            job.provider, job.job_id)
                continue

            try:
                updated = prov.poll(job)
            except Exception as e:
                log.exception("Poll failed for job %s", job.job_id)
                job.error = f"poll_failed: {e}"
                job.poll_count += 1
                job.updated_at = time.time()
                self._store.save(job)
                # Nie mark jako FAILED po jednym błędzie polla — będziemy dalej próbować.
                continue

            updated.poll_count += 1
            updated.updated_at = time.time()

            if updated.status == JobStatus.COMPLETED:
                # Fetch artifact
                try:
                    artifact_path = (self._artifacts_dir
                                      / f"{updated.job_id}_artifact")
                    updated = prov.fetch_artifact(updated, artifact_path)
                    updated.completed_at = time.time()
                    self._store.save(updated)
                    self._emit(EVT_JOB_COMPLETED, updated)
                except Exception as e:
                    log.exception("Artifact fetch failed for job %s",
                                  updated.job_id)
                    updated.status = JobStatus.FAILED
                    updated.error = f"fetch_failed: {e}"
                    self._store.save(updated)
                    self._emit(EVT_JOB_FAILED, updated)
            elif updated.status == JobStatus.FAILED:
                self._store.save(updated)
                self._emit(EVT_JOB_FAILED, updated)
            else:
                # in_progress / pending — keep polling
                self._store.save(updated)
                self._emit(EVT_JOB_PROGRESS, updated)

    def _emit(self, event: str, job: AsyncJob) -> None:
        if self._bus is None:
            return
        try:
            # engine/events.py bus.emit(event, data)
            self._bus.emit(event, job.to_dict())
        except Exception:
            log.exception("Event emit failed: %s", event)
