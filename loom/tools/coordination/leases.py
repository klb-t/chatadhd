"""Durable task claims, fencing and receipts without implicit effect retries.

SQLite is the single coordination authority on one local filesystem. This is
not a distributed lock, provider budget, permission service or exactly-once
external effect protocol. Executors must dispatch only after ``begin`` succeeds.
"""
from __future__ import annotations

from contextlib import contextmanager
from copy import deepcopy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sqlite3
import time
from typing import Callable
import uuid


class CoordinationError(ValueError):
    pass


def _json(value):
    """Strict finite JSON: no coercion of mapping keys, cycles or NaN."""
    ancestors = set()

    def check(item):
        if item is None or type(item) in (str, bool, int):
            return
        if type(item) is float and math.isfinite(item):
            return
        if type(item) in (dict, list):
            ident = id(item)
            if ident in ancestors:
                raise CoordinationError("finite_json_required")
            ancestors.add(ident)
            if type(item) is dict:
                if any(type(key) is not str for key in item):
                    raise CoordinationError("finite_json_required")
                values = item.values()
            else:
                values = item
            for child in values:
                check(child)
            ancestors.remove(ident)
            return
        raise CoordinationError("finite_json_required")

    try:
        check(value)
        return json.dumps(value, sort_keys=True, separators=(",", ":"),
                          ensure_ascii=False, allow_nan=False)
    except (TypeError, ValueError, RecursionError, UnicodeError) as exc:
        raise CoordinationError("finite_json_required") from exc


def digest(value):
    try:
        return hashlib.sha256(_json(value).encode("utf-8")).hexdigest()
    except UnicodeError as exc:
        raise CoordinationError("finite_json_required") from exc


def _text(value, name):
    if type(value) is not str or not value.strip():
        raise CoordinationError(name + "_required")
    return value


def _duration(value):
    if type(value) not in (float, int) or not math.isfinite(value) or value <= 0:
        raise CoordinationError("positive_finite_lease_required")
    return value


class LeaseStore:
    """Connections are per operation; instances may be used by many threads.

    ``clock`` is injectable for deterministic tests; production participants must
    share one wall-clock authority. It is sampled *inside* the write transaction.
    The caller owns database backup/checkpoint publication and task semantics.
    """

    def __init__(self, path, *, busy_timeout_seconds=30, clock: Callable = time.time):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.timeout = _duration(busy_timeout_seconds)
        self.clock = clock
        with self._connect() as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS coordination_meta (
                    singleton INTEGER PRIMARY KEY CHECK(singleton = 1),
                    schema_version INTEGER NOT NULL
                );
                INSERT OR IGNORE INTO coordination_meta VALUES(1, 1);
                CREATE TABLE IF NOT EXISTS tasks (
                    task_id TEXT PRIMARY KEY,
                    source_commit TEXT NOT NULL,
                    spec_json TEXT NOT NULL,
                    spec_sha256 TEXT NOT NULL,
                    state TEXT NOT NULL,
                    fence INTEGER NOT NULL DEFAULT 0,
                    token TEXT,
                    owner TEXT,
                    deadline REAL,
                    latest_receipt INTEGER
                );
                CREATE TABLE IF NOT EXISTS receipts (
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                    task_id TEXT NOT NULL REFERENCES tasks(task_id),
                    body_json TEXT NOT NULL,
                    body_sha256 TEXT NOT NULL
                );
                CREATE INDEX IF NOT EXISTS receipts_task ON receipts(task_id, sequence);
                CREATE TRIGGER IF NOT EXISTS immutable_receipt_update
                    BEFORE UPDATE ON receipts BEGIN
                    SELECT RAISE(ABORT, 'receipts_are_append_only'); END;
                CREATE TRIGGER IF NOT EXISTS immutable_receipt_delete
                    BEFORE DELETE ON receipts BEGIN
                    SELECT RAISE(ABORT, 'receipts_are_append_only'); END;
            """)
            if db.execute("SELECT schema_version FROM coordination_meta").fetchone()[0] != 1:
                raise CoordinationError("unsupported_coordination_schema")

    @contextmanager
    def _connect(self):
        db = sqlite3.connect(str(self.path), timeout=self.timeout, isolation_level=None)
        db.row_factory = sqlite3.Row
        try:
            db.execute("PRAGMA synchronous=FULL")
            db.execute("PRAGMA foreign_keys=ON")
            yield db
        finally:
            db.close()

    @contextmanager
    def _transaction(self):
        with self._connect() as db:
            db.execute("BEGIN IMMEDIATE")
            try:
                yield db
                db.execute("COMMIT")
            except BaseException:
                db.execute("ROLLBACK")
                raise

    def _now(self):
        now = self.clock()
        if type(now) not in (int, float) or not math.isfinite(now):
            raise CoordinationError("finite_clock_required")
        return now

    def _task(self, db, task_id):
        row = db.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()
        if row is None:
            raise CoordinationError("task_not_registered")
        return dict(row)

    def _receipt(self, db, row, event, now, *, actor=None, detail=None):
        prior = db.execute("SELECT body_sha256 FROM receipts WHERE task_id=? "
                           "ORDER BY sequence DESC LIMIT 1", (row["task_id"],)).fetchone()
        body = {
            "schema": "loom.task_receipt/1", "task_id": row["task_id"],
            "source_commit": row["source_commit"], "spec_sha256": row["spec_sha256"],
            "fence": row["fence"], "owner": row["owner"],
            "claim_token": row["token"], "actor": actor or row["owner"],
            "event": event, "recorded_at": now, "lease_deadline": row["deadline"],
            "previous_receipt_sha256": prior[0] if prior else None,
            "detail": detail if detail is not None else {},
            "source_binding_verification": "caller_declared_not_verified",
        }
        encoded = _json(body)
        body_hash = hashlib.sha256(encoded.encode("utf-8")).hexdigest()
        seq = db.execute("INSERT INTO receipts(task_id,body_json,body_sha256) VALUES(?,?,?)",
                         (row["task_id"], encoded, body_hash)).lastrowid
        db.execute("UPDATE tasks SET latest_receipt=? WHERE task_id=?", (seq, row["task_id"]))
        return dict(json.loads(encoded), sequence=seq, receipt_sha256=body_hash)

    def register(self, task_id, *, source_commit, spec):
        _text(task_id, "task_id")
        if type(source_commit) is not str or not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", source_commit):
            raise CoordinationError("full_source_commit_required")
        body = _json(spec)
        snapshot = json.loads(body)
        spec_hash = digest({"source_commit": source_commit, "spec": snapshot})
        with self._transaction() as db:
            db.execute("INSERT OR IGNORE INTO tasks(task_id,source_commit,spec_json,spec_sha256,state) "
                       "VALUES(?,?,?,?, 'pending')", (task_id, source_commit, body, spec_hash))
            row = self._task(db, task_id)
            if row["source_commit"] != source_commit or row["spec_sha256"] != spec_hash:
                raise CoordinationError("task_identity_conflict")
            if row["latest_receipt"] is None:
                self._receipt(db, row, "registered", self._now(), detail={"spec": snapshot})
            return self._view(db, task_id)

    def _expire(self, db, row, now):
        if row["state"] not in ("leased", "dispatched") or now < row["deadline"]:
            return row
        unstarted = row["state"] == "leased"
        state = "pending" if unstarted else "outcome_unknown"
        db.execute("UPDATE tasks SET state=? WHERE task_id=?", (state, row["task_id"]))
        self._receipt(db, row, "expired_before_dispatch" if unstarted else "expired_after_dispatch",
                      now, detail={"effects": "not_dispatched" if unstarted else "unknown",
                                   "automatic_retry_allowed": unstarted})
        return self._task(db, row["task_id"])

    def claim(self, task_id, *, owner, lease_seconds):
        _text(owner, "owner")
        _duration(lease_seconds)
        with self._transaction() as db:
            now = self._now()
            row = self._expire(db, self._task(db, task_id), now)
            if row["state"] != "pending":
                return {"acquired": False, "task": self._view(db, task_id)}
            deadline = now + lease_seconds
            if not math.isfinite(deadline):
                raise CoordinationError("finite_deadline_required")
            db.execute("UPDATE tasks SET state='leased',fence=fence+1,token=?,owner=?,deadline=? "
                       "WHERE task_id=?", (uuid.uuid4().hex, owner, deadline, task_id))
            row = self._task(db, task_id)
            receipt = self._receipt(db, row, "claimed", now)
            return {"acquired": True, "lease": self._lease(row), "receipt": receipt}

    @staticmethod
    def _lease(row):
        return {key: row[key] for key in ("task_id", "source_commit", "spec_sha256",
                                          "fence", "token", "owner")}

    def _owned(self, db, lease, now, states):
        if type(lease) is not dict or type(lease.get("task_id")) is not str:
            raise CoordinationError("invalid_lease")
        row = self._task(db, lease["task_id"])
        if self._lease(row) != lease:
            raise CoordinationError("stale_or_foreign_lease")
        if row["state"] not in states:
            raise CoordinationError("invalid_task_state_" + row["state"])
        if now >= row["deadline"]:
            raise CoordinationError("lease_expired")
        return row

    def renew(self, lease, *, lease_seconds):
        _duration(lease_seconds)
        with self._transaction() as db:
            now = self._now()
            row = self._owned(db, lease, now, ("leased", "dispatched"))
            deadline = max(row["deadline"], now + lease_seconds)
            if not math.isfinite(deadline):
                raise CoordinationError("finite_deadline_required")
            db.execute("UPDATE tasks SET deadline=? WHERE task_id=?", (deadline, row["task_id"]))
            row["deadline"] = deadline
            return self._receipt(db, row, "renewed", now)

    def begin(self, lease):
        """Commit dispatch intent before invoking effects. Repeated begin fails."""
        with self._transaction() as db:
            now = self._now()
            row = self._owned(db, lease, now, ("leased",))
            db.execute("UPDATE tasks SET state='dispatched' WHERE task_id=?", (row["task_id"],))
            return self._receipt(db, row, "dispatch_started", now,
                                 detail={"effects": "unknown_until_receipted"})

    def complete(self, lease, *, outcome, evidence, status="succeeded"):
        if status not in ("succeeded", "failed"):
            raise CoordinationError("terminal_status_required")
        _json(outcome)
        self._evidence(evidence)
        with self._transaction() as db:
            now = self._now()
            row = self._owned(db, lease, now, ("dispatched",))
            receipt = self._receipt(db, row, "completed", now,
                                    detail={"status": status, "outcome": outcome, "evidence": evidence})
            db.execute("UPDATE tasks SET state=? WHERE task_id=?", (status, row["task_id"]))
            return receipt

    @staticmethod
    def _evidence(evidence):
        if type(evidence) is not list or not evidence:
            raise CoordinationError("nonempty_evidence_required")
        _json(evidence)

    def mark_unknown(self, lease, *, evidence):
        """Record an ambiguous callback/persistence failure; do not retry it."""
        self._evidence(evidence)
        with self._transaction() as db:
            row = self._task(db, lease["task_id"])
            if self._lease(row) != lease or row["state"] not in ("dispatched", "outcome_unknown"):
                raise CoordinationError("stale_or_foreign_lease")
            # An expired worker may still report uncertainty, never completion.
            db.execute("UPDATE tasks SET state='outcome_unknown' WHERE task_id=?", (row["task_id"],))
            return self._receipt(db, row, "outcome_unknown", self._now(),
                                 detail={"effects": "unknown", "evidence": evidence})

    def reconcile(self, task_id, *, expected_fence, actor, decision, outcome, evidence,
                  retry_basis=None):
        """Explicit, evidence-bearing resolution; evidence is not adjudicated here.

        ``retry`` is an executor/caller decision and may repeat an external effect.
        The previous dispatch and its uncertainty remain in the receipt chain.
        A live old worker is fenced from this store, not from external systems.
        """
        _text(actor, "actor")
        if decision not in ("succeeded", "failed", "retry"):
            raise CoordinationError("reconciliation_decision_invalid")
        if type(expected_fence) is not int:
            raise CoordinationError("integer_fence_required")
        if decision == "retry":
            _text(retry_basis, "retry_basis")
        _json(outcome)
        self._evidence(evidence)
        with self._transaction() as db:
            now = self._now()
            row = self._expire(db, self._task(db, task_id), now)
            if row["fence"] != expected_fence or row["state"] != "outcome_unknown":
                raise CoordinationError("stale_or_non_unknown_reconciliation")
            state = "pending" if decision == "retry" else decision
            receipt = self._receipt(db, row, "reconciled", now, actor=actor,
                                    detail={"decision": decision, "outcome": outcome,
                                            "evidence": evidence, "retry_basis": retry_basis,
                                            "external_effects_may_repeat": decision == "retry"})
            db.execute("UPDATE tasks SET state=? WHERE task_id=?", (state, task_id))
            return receipt

    def _view(self, db, task_id):
        row = self._task(db, task_id)
        row["spec"] = json.loads(row.pop("spec_json"))
        # Tokens are capabilities for cooperating workers, not security secrets;
        # a reader of the database is in the same trust boundary as the writer.
        row.pop("token")
        return row

    def inspect(self, task_id):
        with self._transaction() as db:
            self._expire(db, self._task(db, task_id), self._now())
            return self._view(db, task_id)

    def receipts(self, task_id):
        with self._connect() as db:
            self._task(db, task_id)
            results = []
            previous = None
            for record in db.execute("SELECT * FROM receipts WHERE task_id=? ORDER BY sequence", (task_id,)):
                body = json.loads(record["body_json"])
                if digest(body) != record["body_sha256"] or body["previous_receipt_sha256"] != previous:
                    raise CoordinationError("receipt_integrity_failure")
                results.append(dict(body, sequence=record["sequence"], receipt_sha256=record["body_sha256"]))
                previous = record["body_sha256"]
            return results

    def backup(self, path):
        """Create a new coherent SQLite snapshot, including committed WAL data.

        The returned hash can anchor a checkpoint outside this machine. This
        method does not publish it remotely or coordinate independent copies.
        Use it instead of copying the main file while writers are active.
        """
        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation prevents overwriting an earlier checkpoint.
        with destination.open("xb"):
            pass
        try:
            with self._connect() as source:
                target = sqlite3.connect(str(destination))
                try:
                    source.backup(target)
                    target.commit()
                finally:
                    target.close()
            with destination.open("rb") as file:
                os.fsync(file.fileno())
                sha256 = hashlib.file_digest(file, "sha256").hexdigest()
            directory_fd = os.open(str(destination.parent), os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except BaseException:
            destination.unlink(missing_ok=True)
            raise
        return {"path": str(destination), "sha256": sha256,
                "schema": "loom.coordination_snapshot/1"}


def execute_once(store, task_id, *, owner, lease_seconds, callback):
    """Opt-in wrapper around an injected local/graph runtime callback.

    Callback receives immutable task data and its lease, and returns JSON outcome
    data. It may renew the lease via the same store for longer work. Its first
    return is recorded as evidence with the completion in one transaction. No
    callback is invoked on a busy, terminal or uncertain task. On failure the
    exception is re-raised after attempting to preserve uncertainty.
    """
    claim = store.claim(task_id, owner=owner, lease_seconds=lease_seconds)
    if not claim["acquired"]:
        return claim
    lease = claim["lease"]
    # Obtain inputs before dispatch. If inspection observes expiry, begin must
    # reject the claim rather than execute after its loss is already known.
    task = store.inspect(task_id)
    store.begin(lease)
    returned = False
    outcome = None
    try:
        outcome = callback(deepcopy(task["spec"]), deepcopy(lease))
        returned = True
        receipt = store.complete(lease, outcome=outcome,
                                 evidence=[{"kind": "injected_callback_return", "sha256": digest(outcome)}])
        return {"acquired": True, "receipt": receipt, "outcome": outcome}
    except BaseException as exc:
        evidence = [{"kind": "callback_or_completion_failure",
                     "exception_type": type(exc).__name__}]
        if returned:
            try:
                evidence.append({"kind": "first_callback_return", "outcome": outcome,
                                 "sha256": digest(outcome)})
            except CoordinationError:
                pass  # Non-JSON return cannot become a valid JSON receipt.
        try:
            store.mark_unknown(lease, evidence=evidence)
        except (CoordinationError, sqlite3.Error, OSError):
            # Dispatch intent is already durable; the next observation expires
            # it to unknown if the store was temporarily unavailable here.
            pass
        raise
