#!/usr/bin/env python3
"""Separate-key staged runner. Planning is offline; dispatch is explicit.

No compiler or scorer lives here. Frozen body files are sent byte-for-byte.
Private accounting is cumulative across stages and is never reset by a run.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
from decimal import Decimal
import fcntl
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import stat
import sys
import time
import uuid
import signal
import xml.etree.ElementTree as ET

try:
    from . import new_budget5eur_gate as gate
    from . import research_programme_manifest as manifests
    from . import research_programme_transport as transport
    from . import endpoint_pricing_v1 as endpoint_pricing
except ImportError:
    import new_budget5eur_gate as gate
    import research_programme_manifest as manifests
    import research_programme_transport as transport
    import endpoint_pricing_v1 as endpoint_pricing


class ProgrammeError(ValueError):
    """Only constant, secret-safe reasons cross the CLI boundary."""


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"), default=str).encode("utf-8")


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def utc():
    return datetime.now(timezone.utc)


def billing_timing(policy):
    selected = policy.get("billing_verification_timing", "per_operation")
    if selected not in ("per_operation", "stage_end"):
        raise ProgrammeError("unknown_billing_verification_timing")
    if selected == "stage_end":
        fields = policy.get("pending_key_metadata_binding_fields")
        if (type(policy.get("require_credit_proof_per_pair")) is not bool or not isinstance(fields, list)
                or not fields or any(not isinstance(name, str) or not name for name in fields) or len(fields) != len(set(fields))):
            raise ProgrammeError("stage_end_policy_data_missing_or_invalid")
    return selected


def verify_bound_first_receipt(receipt, operation, reservation):
    generation = receipt.get("generation_id")
    models = [operation["model_id"], *operation["model_aliases"]]
    providers = [operation["provider_id"], *operation["provider_aliases"]]
    if (not isinstance(generation, str) or not generation
            or receipt.get("model") not in models or receipt.get("provider") not in providers
            or receipt.get("api_type") != operation["api_type"] or receipt.get("is_byok") is True
            or receipt.get("billing_verified") is not False
            or gate.amount(receipt["reported_cost_usd"]) > gate.amount(reservation)):
        raise ProgrammeError("first_response_not_bound_for_pending_billing")


def pair_credit_proven(rows, operation):
    return any(row.get("state") == "completed" and row.get("billing_verified") is True and row.get("is_byok") is False
               and row.get("actual_cost_usd") is not None and gate.digest(row.get("generation_sha256"))
               and all(row.get("receipt_operation", {}).get(name) == operation[name] for name in ("model_id", "provider_id", "route_id", "api_type"))
               for row in rows)


def read_json(path):
    return json.loads(Path(path).read_bytes(), parse_float=Decimal)


def private_path(path, repo_root, *, directory=False):
    """Refuse symlinks, Git trees, non-owned paths and public permissions."""
    path = Path(path).absolute()
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise ProgrammeError("private_path_symlink")
    resolved = path.resolve()
    if resolved == repo_root or repo_root in resolved.parents:
        raise ProgrammeError("private_path_inside_git")
    if any((p / ".git").exists() for p in (resolved, *resolved.parents)):
        raise ProgrammeError("private_path_inside_git")
    if path.exists():
        info = path.stat()
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ProgrammeError("private_path_permissions")
        if not directory and info.st_nlink != 1:
            raise ProgrammeError("private_path_hardlink")
        if directory != stat.S_ISDIR(info.st_mode):
            raise ProgrammeError("private_path_type")
    return resolved


def write_private(path, raw):
    """Immutable evidence, fsynced before another operation can start."""
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        parent_fd = os.open(Path(path).parent, os.O_RDONLY)
        try:
            os.fsync(parent_fd)
        finally:
            os.close(parent_fd)


class PauseControl:
    """Cooperative requests are observed only at accounted operation boundaries."""
    def __init__(self, policy, private_dir, repo_root):
        options = policy.get("pause", {"enabled": False})
        self.enabled = options.get("enabled") is True
        self.requested_by_signal = False
        self.handlers = {}
        self.path = None
        self.repo_root = repo_root
        self.maximum = options.get("maximum_operations_per_invocation")
        if self.maximum is not None and (type(self.maximum) is not int or self.maximum < 1):
            raise ProgrammeError("invalid_pause_operation_boundary")
        if self.enabled:
            name = options["request_filename"]
            if not isinstance(name, str) or Path(name).name != name or name in ("", ".", ".."):
                raise ProgrammeError("invalid_pause_request_filename")
            self.path = Path(private_dir) / name
            names = options["signals"]
            if not isinstance(names, list) or any(name not in ("SIGUSR1", "SIGINT", "SIGTERM") or not hasattr(signal, name) for name in names):
                raise ProgrammeError("invalid_pause_signal")
            for name in names:
                number = getattr(signal, name)
                self.handlers[number] = signal.getsignal(number)
                signal.signal(number, self._request)

    def _request(self, _number, _frame):
        self.requested_by_signal = True

    def requested(self, completed_this_invocation):
        if not self.enabled:
            return False
        if self.path.exists():
            private_path(self.path, self.repo_root)
            if not self.path.is_file():
                raise ProgrammeError("invalid_pause_request_type")
            return True
        return self.requested_by_signal or (self.maximum is not None and completed_this_invocation >= self.maximum)

    def close(self):
        for number, handler in self.handlers.items():
            signal.signal(number, handler)

    def __enter__(self):
        return self

    def __exit__(self, _type, _value, _traceback):
        self.close()


def request_pause(policy, private_dir, repo_root):
    directory = private_path(private_dir, repo_root, directory=True)
    if not directory.is_dir() or policy.get("pause", {}).get("enabled") is not True:
        raise ProgrammeError("pause_not_configured")
    options = {**policy, "pause": {**policy["pause"], "signals": []}}
    control = PauseControl(options, directory, repo_root)
    if control.path.exists():
        private_path(control.path, repo_root)
    else:
        write_private(control.path, b"")
    return {"status": "pause_requested", "boundary": "after_current_operation_accounting"}


class PrivateLedger:
    def __init__(self, directory, repo_root, programme_id, fingerprint, *, captured_pending_statuses=()):
        self.directory = private_path(directory, repo_root, directory=True)
        self.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        private_path(self.directory, repo_root, directory=True)
        self.records = self.directory / "records"
        if self.records.exists():
            private_path(self.records, repo_root, directory=True)
        else:
            self.records.mkdir(mode=0o700)
        self.path = self.directory / "ledger.sqlite3"
        self.repo_root = repo_root
        self.programme_id, self.fingerprint = programme_id, fingerprint
        self.captured_pending_statuses = tuple(captured_pending_statuses)
        self.db = None

    @contextmanager
    def locked(self, *, allow_stopped=False, allow_captured_pending=False, allow_bound_pending=False):
        lockpath = self.directory / "ledger.lock"
        if lockpath.exists():
            private_path(lockpath, self.repo_root)
        fd = os.open(lockpath, os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0), 0o600)
        fcntl.flock(fd, fcntl.LOCK_EX)
        try:
            if not self.path.exists():
                # Orphans prohibit creation of an empty replacement ledger.
                if any(self.records.iterdir()) or any(self.directory.glob("*.response*")) or any(self.directory.glob("*.actual.json")) or any(self.directory.glob("*.generation.bin")):
                    raise ProgrammeError("orphan_evidence_without_ledger")
                filefd = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                os.close(filefd)
            private_path(self.path, self.repo_root)
            self.db = sqlite3.connect(self.path)
            self.db.row_factory = sqlite3.Row
            self.db.execute("PRAGMA synchronous=FULL")
            self.db.execute("PRAGMA journal_mode=DELETE")
            self.db.execute("CREATE TABLE IF NOT EXISTS binding (programme_id TEXT PRIMARY KEY, fingerprint TEXT NOT NULL)")
            self.db.execute("CREATE TABLE IF NOT EXISTS attempts (operation_id TEXT PRIMARY KEY, payload TEXT NOT NULL)")
            self.db.execute("CREATE TABLE IF NOT EXISTS programme_state (name TEXT PRIMARY KEY, value TEXT NOT NULL)")
            self.db.execute("CREATE TABLE IF NOT EXISTS stages (stage_id TEXT PRIMARY KEY, manifest_sha256 TEXT NOT NULL)")
            self.db.execute("CREATE TABLE IF NOT EXISTS resolutions (resolution_id TEXT PRIMARY KEY, payload TEXT NOT NULL)")
            self.db.execute("CREATE TABLE IF NOT EXISTS attempt_resolutions (operation_id TEXT PRIMARY KEY, payload TEXT NOT NULL)")
            self.db.execute("CREATE TABLE IF NOT EXISTS verification_failures (operation_id TEXT PRIMARY KEY, payload TEXT NOT NULL)")
            self.db.execute("CREATE TABLE IF NOT EXISTS metadata_read_audits (capture_id TEXT PRIMARY KEY, payload TEXT NOT NULL)")
            bound = self.db.execute("SELECT * FROM binding").fetchall()
            if not bound:
                self.db.execute("INSERT INTO binding VALUES (?, ?)", (self.programme_id, self.fingerprint))
            elif len(bound) != 1 or tuple(bound[0]) != (self.programme_id, self.fingerprint):
                raise ProgrammeError("ledger_programme_or_key_mismatch")
            self.db.commit()
            if not allow_stopped and self.db.execute("SELECT value FROM programme_state WHERE name='stop_reason'").fetchone():
                raise ProgrammeError("durable_programme_stop_requires_independent_reconciliation")
            self.validate(allow_captured_pending=allow_captured_pending, allow_bound_pending=allow_bound_pending)
            yield self
        finally:
            if self.db is not None:
                self.db.close()
                self.db = None
            fcntl.flock(fd, fcntl.LOCK_UN)
            os.close(fd)

    def original_rows(self):
        return [json.loads(row["payload"]) for row in self.db.execute("SELECT payload FROM attempts ORDER BY rowid")]

    def late_proofs(self):
        return {row["operation_id"]: json.loads(row["payload"]) for row in self.db.execute("SELECT * FROM attempt_resolutions")}

    def rows(self):
        proofs = self.late_proofs()
        return [{**row, **proofs[row["operation_id"]]["projection"]} if row["operation_id"] in proofs else row for row in self.original_rows()]

    def private_witness(self, filename, digest):
        if not isinstance(filename, str) or Path(filename).name != filename or filename in ("", ".", "..") or not gate.digest(digest):
            raise ProgrammeError("invalid_private_evidence_witness")
        path = self.directory / filename
        private_path(path, self.repo_root)
        if not path.is_file() or sha(path.read_bytes()) != digest:
            raise ProgrammeError("resolution_evidence_missing_or_changed")
        return path

    def validate_resolutions(self):
        expected = set()
        for row in self.db.execute("SELECT * FROM resolutions"):
            record = json.loads(row["payload"])
            filename = row["resolution_id"] + ".resolution.json"
            expected.add(filename)
            path = self.private_witness(filename, sha(canonical(record)))
            if path.read_bytes() != canonical(record) or record.get("resolution_id") != row["resolution_id"] or record.get("programme_id") != self.programme_id or record.get("key_fingerprint_sha256") != self.fingerprint:
                raise ProgrammeError("resolution_binding_mismatch")
            previous = record["previous_active_stop_projection"]
            if previous.get("sha256") != sha(canonical({"reason": previous["reason"]})):
                raise ProgrammeError("original_stop_projection_changed")
            witnesses = record["original_stop_receipts"]
            if not isinstance(witnesses, list) or not witnesses:
                raise ProgrammeError("original_stop_witness_missing")
            for witness in witnesses:
                stop = read_json(self.private_witness(witness["file"], witness["sha256"]))
                if stop.get("status") != "stopped" or stop.get("reason") != previous["reason"]:
                    raise ProgrammeError("original_stop_witness_mismatch")
        if set(path.name for path in self.directory.glob("*.resolution.json")) != expected:
            raise ProgrammeError("orphan_resolution_evidence")

    def captured_pending(self, row, statuses=None):
        statuses = self.captured_pending_statuses if statuses is None else statuses
        if not isinstance(statuses, (list, tuple)) or not statuses or any(type(status) is not int or not 100 <= status <= 599 for status in statuses):
            return False
        reads = row.get("generation_reads")
        if (row.get("state") != "uncertain" or row.get("actual_cost_usd") is not None
                or row.get("http_status") != 200 or row.get("transport_error")
                or row.get("billing_verified") is not False or not isinstance(reads, list) or not reads
                or any(read.get("http_status") == 200 or read.get("http_status") not in statuses or read.get("transport_error") for read in reads)):
            return False
        token = sha(row["operation_id"].encode())
        try:
            path = self.records / (token + ".response.bin")
            private_path(path, self.repo_root)
            raw = path.read_bytes()
            parsed = transport.extract_receipt({"http_status": 200, "raw": raw}, row["receipt_operation"])
            return (bool(parsed.get("generation_id")) and parsed.get("is_byok") is not True
                    and all(row.get(name) == value for name, value in parsed.items())
                    and gate.amount(parsed["reported_cost_usd"]) <= gate.amount(row["reservation_usd"])
                    and row.get("generation_sha256") == reads[-1].get("raw_sha256"))
        except Exception:
            return False

    def bound_pending(self, row):
        if (row.get("state") != "pending_billing" or row.get("billing_verification_timing") != "stage_end"
                or row.get("actual_cost_usd") is not None or row.get("billing_verified") is not False
                or row.get("http_status") != 200 or row.get("transport_error")
                or row.get("generation_reads") or row.get("generation_sha256")):
            return False
        try:
            path = self.records / (sha(row["operation_id"].encode()) + ".response.bin")
            private_path(path, self.repo_root)
            parsed = transport.extract_receipt({"http_status": 200, "raw": path.read_bytes()}, row["receipt_operation"])
            verify_bound_first_receipt(parsed, row["receipt_operation"], row["reservation_usd"])
            return all(row.get(name) == value for name, value in parsed.items())
        except Exception:
            return False

    def validate(self, *, allow_captured_pending=False, allow_bound_pending=False):
        self.validate_resolutions()
        failures = list(self.db.execute("SELECT payload FROM verification_failures"))
        failure_files = set()
        for saved in failures:
            failure = json.loads(saved["payload"])
            filename = failure["failure_id"] + ".billing-failure.json"
            failure_files.add(filename)
            self.private_witness(filename, sha(canonical(failure)))
            if failure.get("programme_id") != self.programme_id or failure.get("key_fingerprint_sha256") != self.fingerprint:
                raise ProgrammeError("billing_failure_binding_mismatch")
            for witness in failure["late_reads"]:
                self.private_witness(witness["file"], witness["raw_sha256"])
        if set(path.name for path in self.directory.glob("*.billing-failure.json")) != failure_files:
            raise ProgrammeError("orphan_billing_failure_evidence")
        if failures:
            raise ProgrammeError("durable_billing_verification_contradiction")
        expected = set()
        generations = set()
        originals = {row["operation_id"]: row for row in self.original_rows()}
        proofs = self.late_proofs()
        for path in self.directory.glob("*.actual.json"):
            private_path(path, self.repo_root)
            receipt = read_json(path)
            if receipt.get("schema") != "loom.research_programme_stage_receipt/1":
                raise ProgrammeError("unknown_saved_actual_receipt")
            stage_count = sum(row.get("stage_id") == receipt.get("stage_id") for row in originals.values())
            if stage_count < receipt["attempted_operations"]:
                raise ProgrammeError("saved_actual_attempt_history_missing")
            for witness in receipt.get("attempt_history", []):
                original = originals.get(witness["operation_id"])
                if original is None or sha(canonical(original)) != witness["original_attempt_sha256"]:
                    raise ProgrammeError("saved_actual_attempt_history_missing_or_changed")
        audit_files, late_files = set(), {read["file"] for proof in proofs.values() for read in proof["late_reads"]}
        for saved in self.db.execute("SELECT payload FROM metadata_read_audits"):
            audit = json.loads(saved["payload"])
            filename = audit["capture_id"] + ".generation-audit.json"
            audit_files.add(filename)
            self.private_witness(filename, sha(canonical(audit)))
            original = originals.get(audit["operation_id"])
            if (original is None or audit.get("programme_id") != self.programme_id
                    or audit.get("key_fingerprint_sha256") != self.fingerprint
                    or audit.get("original_attempt_sha256") != sha(canonical(original))):
                raise ProgrammeError("generation_read_audit_binding_mismatch")
            for index, read in enumerate(audit["late_reads"]):
                if read["file"] != audit["capture_id"] + ".captured-generation-read-" + str(index) + ".bin":
                    raise ProgrammeError("generation_read_audit_filename_mismatch")
                self.private_witness(read["file"], read["raw_sha256"])
                late_files.add(read["file"])
            if not audit["late_reads"]:
                raise ProgrammeError("generation_read_audit_empty")
            if audit["outcome"] == "contradiction":
                raise ProgrammeError("durable_billing_verification_contradiction")
            if audit["outcome"] == "verified":
                if proofs.get(audit["operation_id"], {}).get("resolution_id") != audit["capture_id"]:
                    raise ProgrammeError("orphan_verified_generation_read_audit")
            elif (audit["outcome"] != "pending" or audit["late_reads"][-1]["http_status"] == 200 or audit["late_reads"][-1]["http_status"] not in audit["pending_http_statuses"]
                  or audit["late_reads"][-1].get("transport_error")):
                raise ProgrammeError("generation_read_audit_outcome_mismatch")
        if set(path.name for path in self.directory.glob("*.generation-audit.json")) != audit_files:
            raise ProgrammeError("orphan_generation_read_audit")
        if set(path.name for path in self.directory.glob("*.captured-generation-read-*.bin")) != late_files:
            raise ProgrammeError("orphan_captured_generation_read_evidence")
        proof_files = set()
        if set(proofs) - set(originals):
            raise ProgrammeError("orphan_attempt_resolution")
        for row in self.rows():
            original = originals[row["operation_id"]]
            if row.get("programme_id") != self.programme_id or row.get("key_fingerprint_sha256") != self.fingerprint:
                raise ProgrammeError("ledger_attempt_binding_mismatch")
            token = sha(row["operation_id"].encode())
            expected.add(token + ".started.json")
            startedpath = self.records / (token + ".started.json")
            private_path(startedpath, self.repo_root)
            if not startedpath.is_file():
                raise ProgrammeError("durable_started_record_missing")
            started = read_json(startedpath)
            binding_fields = ("operation_id", "programme_id", "key_fingerprint_sha256", "stage_id",
                              "manifest_sha256", "request_sha256", "reservation_usd", "started_at",
                              "receipt_operation", "expected_cost_usd", "billing_verification_timing")
            if any(started.get(name) != row.get(name) for name in binding_fields):
                raise ProgrammeError("durable_started_record_mismatch")
            expected.add(token + ".request.bin")
            requestpath = self.records / (token + ".request.bin")
            private_path(requestpath, self.repo_root)
            if not requestpath.is_file() or sha(requestpath.read_bytes()) != row.get("request_sha256"):
                raise ProgrammeError("frozen_request_missing_or_changed")
            if row.get("state") == "reserved":
                raise ProgrammeError("ambiguous_attempt_no_adoption_or_retry")
            bound_pending = self.bound_pending(original)
            if row["operation_id"] not in proofs and (row.get("state") != "completed" or row.get("actual_cost_usd") is None) and not ((allow_captured_pending and self.captured_pending(original)) or (allow_bound_pending and bound_pending)):
                raise ProgrammeError("unresolved_attempt_no_continuation")
            generation_id = row.get("generation_id")
            if not isinstance(generation_id, str) or not generation_id or generation_id in generations:
                raise ProgrammeError("duplicate_or_missing_generation_identity")
            generations.add(generation_id)
            expected.update((token + ".response.bin", token + ".result.json"))
            rawpath = self.records / (token + ".response.bin")
            private_path(rawpath, self.repo_root)
            if not rawpath.is_file() or sha(rawpath.read_bytes()) != row.get("response_sha256"):
                raise ProgrammeError("first_response_missing_or_changed")
            resultpath = self.records / (token + ".result.json")
            private_path(resultpath, self.repo_root)
            if not resultpath.is_file() or resultpath.read_bytes() != canonical(original):
                raise ProgrammeError("terminal_receipt_mismatch")
            # Completion is evidence, never a trusted state label. Rebuild its
            # billing projection from the immutable first bytes and generation
            # lookup using the pair frozen before dispatch.
            operation = row.get("receipt_operation")
            operation_fields = {"model_id", "provider_id", "provider_aliases", "model_aliases", "route_id", "api_type"}
            if not isinstance(operation, dict) or not operation_fields <= set(operation) or (not bound_pending and not gate.digest(original.get("generation_sha256"))):
                raise ProgrammeError("completed_billing_proof_missing")
            genpath = None
            if not bound_pending:
                expected.add(token + ".generation.bin")
                genpath = self.records / (token + ".generation.bin")
                private_path(genpath, self.repo_root)
                if not genpath.is_file() or sha(genpath.read_bytes()) != original["generation_sha256"]:
                    raise ProgrammeError("generation_receipt_missing_or_changed")
            for index, read in enumerate(original.get("generation_reads", [])):
                filename = token + ".generation-read-" + str(index) + ".bin"
                expected.add(filename)
                path = self.records / filename
                private_path(path, self.repo_root)
                if not path.is_file() or sha(path.read_bytes()) != read["raw_sha256"]:
                    raise ProgrammeError("generation_read_evidence_changed")
            if row["operation_id"] in proofs:
                proof = proofs[row["operation_id"]]
                filename = proof["resolution_id"] + ".attempt-resolution.json"
                proof_files.add(filename)
                self.private_witness(filename, sha(canonical(proof)))
                if (not (bound_pending or self.captured_pending(original, proof.get("eligible_generation_http_statuses"))) or proof.get("original_attempt_sha256") != sha(canonical(original))
                        or proof.get("programme_id") != self.programme_id or proof.get("key_fingerprint_sha256") != self.fingerprint
                        or proof.get("operation_id") != row["operation_id"] or proof.get("manifest_sha256") != original["manifest_sha256"]):
                    raise ProgrammeError("captured_resolution_binding_mismatch")
                original_witnesses = []
                suffixes = (".started.json", ".request.bin", ".response.bin", ".result.json") + (() if bound_pending else (".generation.bin",))
                for suffix in suffixes:
                    path = self.records / (token + suffix)
                    original_witnesses.append({"file": "records/" + path.name, "sha256": sha(path.read_bytes())})
                original_witnesses.extend({"file": "records/" + token + ".generation-read-" + str(index) + ".bin", "sha256": read["raw_sha256"]} for index, read in enumerate(original.get("generation_reads", [])))
                if proof.get("original_evidence") != original_witnesses:
                    raise ProgrammeError("captured_original_evidence_binding_mismatch")
                for witness in proof["late_reads"]:
                    self.private_witness(witness["file"], witness["raw_sha256"])
                final = proof["late_reads"][-1]
                if final["http_status"] != 200 or final.get("transport_error") or final["raw_sha256"] != row["generation_sha256"] or row.get("state") != "completed" or row.get("original_state") != original["state"] or row.get("attempt_resolution_id") != proof["resolution_id"]:
                    raise ProgrammeError("captured_resolution_generation_mismatch")
                genpath = self.private_witness(final["file"], final["raw_sha256"])
            elif row.get("state") != "completed" or row.get("actual_cost_usd") is None:
                if allow_bound_pending and bound_pending:
                    continue
                if allow_captured_pending and self.captured_pending(original):
                    continue
                raise ProgrammeError("unresolved_attempt_no_continuation")
            if row.get("is_byok") is not False or row.get("billing_verified") is not True or row.get("http_status") != 200 or row.get("transport_error"):
                raise ProgrammeError("completed_billing_proof_missing")
            try:
                first = {"http_status": row["http_status"], "raw": rawpath.read_bytes()}
                generation = {"http_status": 200, "raw": genpath.read_bytes()}
                parsed = transport.extract_receipt(first, operation)
                rebuilt = transport.verify_generation_receipt(generation, parsed, operation)
                if any(row.get(name) != value for name, value in rebuilt.items()):
                    raise ValueError()
                if row["operation_id"] in proofs:
                    projection = {**rebuilt, "state": "completed", "generation_sha256": proofs[row["operation_id"]]["late_reads"][-1]["raw_sha256"],
                                  "attempt_resolution_id": proofs[row["operation_id"]]["resolution_id"], "original_state": original["state"]}
                    if proofs[row["operation_id"]]["projection"] != projection:
                        raise ValueError()
                if gate.amount(rebuilt["actual_cost_usd"]) > gate.amount(row["reservation_usd"]):
                    raise ValueError()
            except Exception:
                raise ProgrammeError("completed_billing_proof_replay_mismatch") from None
        actual = set(path.name for path in self.records.iterdir())
        if actual != expected:
            raise ProgrammeError("orphan_or_missing_evidence_with_ledger")
        if set(path.name for path in self.directory.glob("*.attempt-resolution.json")) != proof_files:
            raise ProgrammeError("orphan_attempt_resolution_evidence")

    def exported(self):
        return {"schema": "loom.new_programme_billing_ledger/1", "programme_id": self.programme_id,
                "attempts": self.rows()}

    def reserve(self, operation, manifest_hash, reservation, expected_cost, *, billing_timing=None):
        row = {"operation_id": operation["operation_id"], "programme_id": self.programme_id,
               "stage_id": operation["stage_id"], "key_fingerprint_sha256": self.fingerprint,
               "manifest_sha256": manifest_hash, "request_sha256": operation["request_sha256"],
               "reservation_usd": str(reservation), "actual_cost_usd": None,
               "expected_cost_usd": str(expected_cost),
               "receipt_operation": {name: operation[name] for name in ("model_id", "provider_id", "provider_aliases", "model_aliases", "route_id", "api_type")},
               "state": "reserved", "started_at": utc().isoformat()}
        if billing_timing is not None:
            row["billing_verification_timing"] = billing_timing
        # Commit is the admission boundary: no POST may precede this durable row.
        with self.db:
            self.db.execute("INSERT INTO attempts VALUES (?, ?)", (row["operation_id"], canonical(row).decode()))
        write_private(self.records / (sha(row["operation_id"].encode()) + ".started.json"), canonical(row))
        write_private(self.records / (sha(row["operation_id"].encode()) + ".request.bin"), operation["request_bytes"])
        return row

    def finish(self, row):
        row["finished_at"] = utc().isoformat()
        write_private(self.records / (sha(row["operation_id"].encode()) + ".result.json"), canonical(row))
        with self.db:
            self.db.execute("UPDATE attempts SET payload=? WHERE operation_id=?", (canonical(row).decode(), row["operation_id"]))

    def stop(self, reason):
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO programme_state VALUES ('stop_reason', ?)", (reason,))

    def bind_stage(self, stage_id, manifest_hash):
        bound = self.db.execute("SELECT manifest_sha256 FROM stages WHERE stage_id=?", (stage_id,)).fetchone()
        if bound and bound[0] != manifest_hash:
            raise ProgrammeError("stage_identity_already_bound_to_other_manifest")
        if any(row["stage_id"] == stage_id and row["manifest_sha256"] != manifest_hash for row in self.rows()):
            raise ProgrammeError("stage_identity_already_bound_to_other_manifest")
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO stages VALUES (?, ?)", (stage_id, manifest_hash))

    def active_stop(self):
        row = self.db.execute("SELECT value FROM programme_state WHERE name='stop_reason'").fetchone()
        return row[0] if row else None

    def resolve_stop(self, record):
        # Immutable old receipts and exact active projection are retained before
        # clearing only the projection. Resolutions never create paid attempts.
        write_private(self.directory / (record["resolution_id"] + ".resolution.json"), canonical(record))
        with self.db:
            self.db.execute("INSERT INTO resolutions VALUES (?, ?)", (record["resolution_id"], canonical(record).decode()))
            self.db.execute("DELETE FROM programme_state WHERE name='stop_reason'")

    def append_attempt_proof(self, record):
        write_private(self.directory / (record["resolution_id"] + ".attempt-resolution.json"), canonical(record))
        with self.db:
            self.db.execute("INSERT INTO attempt_resolutions VALUES (?, ?)", (record["operation_id"], canonical(record).decode()))

    def append_verification_failure(self, record):
        write_private(self.directory / (record["failure_id"] + ".billing-failure.json"), canonical(record))
        with self.db:
            self.db.execute("INSERT INTO verification_failures VALUES (?, ?)", (record["operation_id"], canonical(record).decode()))

    def append_metadata_audit(self, record):
        write_private(self.directory / (record["capture_id"] + ".generation-audit.json"), canonical(record))
        with self.db:
            self.db.execute("INSERT INTO metadata_read_audits VALUES (?, ?)", (record["capture_id"], canonical(record).decode()))


def endpoint_quote(response, operation, checked_at, policy):
    if response["http_status"] != 200 or response.get("transport_error"):
        raise ProgrammeError("endpoint_quote_unavailable")
    source_ref = policy["transport"]["base_url"] + policy["transport"]["routes"]["model_endpoints"]["path"].format(model_id=operation["model_id"])
    if policy.get("endpoint_pricing") is not None:
        try:
            quote = endpoint_pricing.resolve_endpoint_pricing(response["raw"], operation, policy["endpoint_pricing"])
        except (KeyError, TypeError, ValueError):
            raise ProgrammeError("endpoint_policy_quote_invalid") from None
        return {**quote, "currency": "USD", "checked_at": checked_at, "source_ref": source_ref}
    try:
        data = transport._json(response, require_success=True)[0]["data"]
        aliases = policy["provider_aliases"].get(operation["provider_id"], [operation["provider_id"]])
        matches = [row for row in data["endpoints"] if row.get("provider_name") in aliases
                   and row.get("model_id") == operation["model_id"]]
        if data["id"] != operation["model_id"] or len(matches) != 1:
            raise ValueError()
        prices = dict(matches[0]["pricing"])
        # Discounts reduce the charge; the preset conservatively ignores them.
        for name in policy.get("noncharge_price_fields", []):
            gate.amount(prices.pop(name, "0"))
        for name, value in policy.get("absent_component_prices_usd", {}).items():
            prices.setdefault(name, value)
        if not prices:
            raise ValueError()
        prices = {name: str(gate.amount(value)) for name, value in prices.items()}
    except (KeyError, TypeError, ValueError):
        raise ProgrammeError("endpoint_pair_or_price_unknown") from None
    return {"model_id": operation["model_id"], "provider_id": operation["provider_id"],
            "currency": "USD", "checked_at": checked_at, "raw_sha256": sha(response["raw"]),
            "source_ref": source_ref,
            "all_charge_components_accounted": True, "component_prices_usd": prices}


def verify_fx(evidence, policy):
    """Verify a dated public snapshot's bytes and declared rate, without a key.

    The source URI/quote basis remains recorded evidence, not a claim about the
    owner's payment-card conversion. Parsing paths and currency are policy data.
    """
    try:
        fx = evidence["fx"]
        raw = Path(fx["raw_file"]).read_bytes()
        if sha(raw) != fx["raw_sha256"]:
            raise ValueError()
        options = policy["fx_snapshot"]
        if not fx["source_ref"].startswith(options["source_url_prefix"]):
            raise ValueError()
        if options["format"] == "ecb_xml":
            element = ET.fromstring(raw)
            rows = [node for node in element.iter() if node.attrib.get("time") == fx["rate_date"]]
            if len(rows) != 1:
                raise ValueError()
            quotes = [node.attrib["rate"] for node in rows[0] if node.attrib.get("currency") == fx["quote_currency"]]
            if len(quotes) != 1 or gate.amount(quotes[0]) != gate.amount(fx["usd_per_eur"]):
                raise ValueError()
        elif options["format"] == "json":
            data = transport._json(raw)[0]
            for name in options["rate_path"]:
                data = data[name]
            if gate.amount(data) != gate.amount(fx["usd_per_eur"]):
                raise ValueError()
        else:
            raise ValueError()
        dated = gate.timepoint(fx["rate_date"] + "T00:00:00Z")
        age = (utc() - dated).total_seconds()
        if not -float(gate.amount(policy["freshness_presets_seconds"]["future_clock_skew"])) <= age <= float(gate.amount(options["maximum_rate_age_seconds"])):
            raise ValueError()
    except (KeyError, TypeError, ValueError, OSError, ET.ParseError):
        raise ProgrammeError("dated_fx_snapshot_invalid_or_stale") from None


def price_plan(operations, pricing, policy):
    amounts, pairs, expected_costs = {}, {}, {}
    for op in operations:
        quotes = [q for q in pricing if q["model_id"] == op["model_id"] and q["provider_id"] == op["provider_id"]]
        if len(quotes) != 1:
            raise ProgrammeError("pair_quote_missing_or_duplicate")
        quote = quotes[0]
        prices = quote["component_prices_usd"]
        if policy.get("endpoint_pricing") is not None:
            try:
                subpolicy = policy["endpoint_pricing"]
                if quote.get("policy_sha256") != sha(canonical(subpolicy)):
                    raise ValueError()
                if quote.get("routing") != endpoint_pricing.resolve_operation_routing(op, subpolicy):
                    raise ValueError()
                bounds = endpoint_pricing.resolve_units_upper_bounds(op, prices, subpolicy)
            except (KeyError, TypeError, ValueError):
                raise ProgrammeError("endpoint_policy_operation_or_quote_invalid") from None
        else:
            bounds = dict(op["units_upper_bounds"])
            for component, rule in policy.get("unit_bounds_defaults", {}).items():
                if component in prices and component not in bounds:
                    bounds[component] = bounds[rule["copy_bound"]] if "copy_bound" in rule else rule["value"]
        if set(bounds) != set(prices):
            raise ProgrammeError("unaccounted_price_component")
        forecast = sum((gate.amount(prices[name]) * gate.amount(bounds[name]) for name in prices), Decimal(0))
        expected_costs[op["operation_id"]] = forecast
        reservation = max(forecast, gate.amount(op.get("minimum_reservation_usd", "0")))
        amounts[op["operation_id"]] = reservation
        pair = pairs.setdefault((op["model_id"], op["provider_id"]), {name: Decimal(0) for name in prices})
        for name in prices:
            pair[name] += gate.amount(bounds[name])
    return amounts, [{"model_id": model, "provider_id": provider, "units_upper_bounds": {k: str(v) for k, v in bounds.items()}}
                     for (model, provider), bounds in pairs.items()], expected_costs


def escalation_guard(policy, ledger_rows, forecast, manifest_hash, stage_id=None):
    options = policy["escalation_guard"]
    metric = options["baseline_metric"]
    if metric not in ("expected_cost_usd", "actual_cost_usd"):
        raise ProgrammeError("unknown_baseline_metric")
    completed = [row for row in ledger_rows if row.get("state") == "completed" and row.get(metric) is not None]
    scope = options["scope"]
    if scope == "per_operation":
        rows = [gate.amount(row[metric]) for row in completed]
    elif scope == "stage_total":
        stages = {}
        for row in completed:
            if row["stage_id"] != stage_id:
                stages[row["stage_id"]] = stages.get(row["stage_id"], Decimal(0)) + gate.amount(row[metric])
        rows = list(stages.values())
    else:
        raise ProgrammeError("unknown_escalation_scope")
    window = int(options["rolling_window_operations"])
    if window < 1:
        raise ProgrammeError("invalid_baseline_window")
    samples = rows[-window:]
    if not samples or sum(samples) == 0:
        decision = options.get("unknown_baseline_policy")
        if decision != "allow_with_existing_programme_authorization":
            raise ProgrammeError("baseline_unknown")
        return {"baseline_status": "unknown", "decision": decision}
    baseline = sum(samples) / len(samples)
    ratio = forecast / baseline
    if ratio >= gate.amount(options["factor"]):
        confirmations = options.get("confirmations", [])
        if not any(row.get("manifest_sha256") == manifest_hash and row.get("source_ref") for row in confirmations):
            raise ProgrammeError("tenfold_escalation_confirmation_required")
    return {"baseline_status": "rolling_" + metric, "scope": options["scope"],
            "baseline_usd_per_operation": str(baseline), "forecast_ratio": str(ratio)}


def apply_provider_usage_policy(policy, readiness, metadata, ledger_rows, *, allow_verified_lower_usage=True):
    """Keep the strict gate result and observed provider amount untouched.

    Explicit lag admission may use independently verified credit receipts as
    the more conservative spend basis. Unknown or higher provider usage is not
    lag and never gains admission here.
    """
    result = dict(readiness)
    options = policy.get("provider_usage_lag", {"mode": "strict"})
    mode = options["mode"]
    if mode not in ("strict", "allow_verified_lower_usage"):
        raise ProgrammeError("unknown_provider_usage_lag_policy")
    observed = gate.amount(metadata["usage_usd"])
    verified = sum((gate.amount(row["actual_cost_usd"]) for row in ledger_rows if row.get("actual_cost_usd") is not None), Decimal(0))
    unresolved = sum((gate.amount(row["reservation_usd"]) for row in ledger_rows if row.get("actual_cost_usd") is None), Decimal(0))
    effective = min(gate.amount(metadata["remaining_usd"]), gate.amount(policy["usd_cap"]) - verified - unresolved,
                    gate.amount(metadata["limit_usd"]) - verified - unresolved)
    reconciliation = {"policy": mode, "observed_provider_usage_usd": str(observed),
                      "verified_cumulative_actual_usd": str(verified), "unresolved_reservations_usd": str(unresolved),
                      "effective_available_usd": str(effective), "provider_usage_lag_usd": str(max(Decimal(0), verified - observed)),
                      "strict_gate_status": readiness["status"], "strict_gate_blockers": list(readiness["blockers"])}
    result["provider_usage_reconciliation"] = reconciliation
    pending = [row for row in ledger_rows if row.get("state") == "pending_billing"]
    if pending:
        upper = verified + unresolved
        reconciliation.update(billing_verification_timing=billing_timing(policy),
                              pending_operations=len(pending), usage_interval_lower_usd=str(verified),
                              usage_interval_upper_usd=str(upper), basis="bound_first_captures_full_reservations")
        generations = [row.get("generation_id") for row in ledger_rows]
        bounded_rows = len(generations) == len(set(generations)) and all(
            (row.get("state") == "completed" and row.get("billing_verified") is True and row.get("is_byok") is False
             and row.get("actual_cost_usd") is not None and gate.digest(row.get("generation_sha256")))
            or (row.get("state") == "pending_billing" and row.get("billing_verification_timing") == "stage_end"
                and row.get("actual_cost_usd") is None and row.get("billing_verified") is False
                and row.get("http_status") == 200 and not row.get("transport_error") and row.get("is_byok") is not True
                and isinstance(row.get("generation_id"), str) and bool(row["generation_id"])
                and gate.digest(row.get("response_sha256")) and gate.digest(row.get("request_sha256"))
                and gate.amount(row["reported_cost_usd"]) <= gate.amount(row["reservation_usd"]))
            for row in ledger_rows)
        next_reservation = gate.amount(readiness["accounting"]["next_stage_reservation_usd"])
        admissible_blockers = {"provider_usage_reconciles_with_cumulative_actuals", "stage_fits_cumulative_budget"}
        if (billing_timing(policy) == "stage_end" and policy.get("unknown_cost_policy") == "reserve"
                and bounded_rows and verified <= observed <= upper
                and next_reservation <= effective and set(readiness["blockers"]) <= admissible_blockers):
            result["status"] = "ready_with_bound_pending_reservations"
            result["blockers"] = []
        return result
    if mode == "strict" or observed >= verified or not allow_verified_lower_usage:
        return result
    generations = [row.get("generation_id") for row in ledger_rows]
    proof = bool(ledger_rows) and len(generations) == len(set(generations)) and all(
        row.get("state") == "completed" and row.get("billing_verified") is True
        and row.get("is_byok") is False and row.get("actual_cost_usd") is not None
        and isinstance(row.get("generation_id"), str) and bool(row["generation_id"])
        and gate.digest(row.get("generation_sha256")) and gate.digest(row.get("response_sha256"))
        for row in ledger_rows)
    if (not options.get("authority_ref") or not proof or unresolved != 0
            or readiness["blockers"] != ["provider_usage_reconciles_with_cumulative_actuals"]):
        return result
    reconciliation["basis"] = "replayed_unique_credit_generation_receipts"
    # The original strict check stays False and its blocker stays in the nested
    # witness. This is a separately selected policy, never normalized equality.
    result["status"] = "ready_with_verified_provider_usage_lag"
    result["blockers"] = []
    return result


def stage_evidence(policy, manifest, manifest_hash, operations, evidence, metadata, fingerprint, ledger_rows, *, phase="stage_admission", admitted_pricing=None, no_paid_dispatch=False):
    reservations, pairs, expected_costs = price_plan(operations, evidence["pricing"], policy)
    if no_paid_dispatch:
        # Validate original operation bodies/routing/quantities above. A GET-only
        # accounting boundary reserves no new spend; fabricated zero quantities
        # must never pass through the real-request bound validators.
        reservations = {name: Decimal(0) for name in reservations}
        expected_costs = {name: Decimal(0) for name in expected_costs}
        pairs = [{**pair, "units_upper_bounds": {name: "0" for name in pair["units_upper_bounds"]}} for pair in pairs]
    binding = {"programme_id": policy["programme_id"], "key_fingerprint_sha256": fingerprint,
               "bound_to_loaded_credential": True,
               "separate_new_key_owner_confirmation_ref": policy["separate_new_key_owner_confirmation_ref"]}
    stage = {"stage_id": manifest["stage_id"], "manifest_sha256": manifest_hash,
             "reservation_usd": str(sum(reservations.values(), Decimal(0))), "model_provider_pairs": pairs}
    bound = {**evidence, "key_binding": binding, "key_metadata": metadata, "stage": stage}
    ledger = {"schema": "loom.new_programme_billing_ledger/1", "programme_id": policy["programme_id"], "attempts": ledger_rows}
    readiness = gate.evaluate(policy, bound, ledger, utc())
    readiness = apply_provider_usage_policy(policy, readiness, metadata, ledger_rows,
                                           allow_verified_lower_usage=billing_timing(policy) == "per_operation" or no_paid_dispatch)
    if readiness["status"] not in ("ready_for_bound_transport_preflight", "ready_with_verified_provider_usage_lag", "ready_with_bound_pending_reservations"):
        error = ProgrammeError("budget_evidence_gate_blocked:" + ",".join(readiness["blockers"]))
        error.readiness = readiness
        raise error
    scope = policy["escalation_guard"]["scope"]
    forecast_metric = max(expected_costs.values(), default=Decimal(0)) if scope == "per_operation" else sum(expected_costs.values(), Decimal(0))
    boundary = policy["escalation_guard"]["evaluation_boundary"]
    if boundary not in ("stage_admission", "each_operation"):
        raise ProgrammeError("unknown_escalation_evaluation_boundary")
    if phase == "stage_admission" and boundary == "stage_admission" and admitted_pricing is not None:
        # Resume compares unchanged request quantities against their original
        # admitted quotes. A cheap completed prefix cannot redefine the already
        # approved mixed stage, while newly increased prices still trigger ×10.
        _, _, prior_costs = price_plan(operations, admitted_pricing, policy)
        old_metric = max(prior_costs.values(), default=Decimal(0)) if scope == "per_operation" else sum(prior_costs.values(), Decimal(0))
        if old_metric == 0:
            if forecast_metric != 0:
                raise ProgrammeError("resumed_admission_baseline_unknown")
            ratio = Decimal(1)
        else:
            ratio = forecast_metric / old_metric
        if ratio >= gate.amount(policy["escalation_guard"]["factor"]) and not any(row.get("manifest_sha256") == manifest_hash and row.get("source_ref") for row in policy["escalation_guard"].get("confirmations", [])):
            raise ProgrammeError("tenfold_escalation_confirmation_required")
        guard = {"decision": "resume_frozen_admitted_stage", "forecast_price_ratio": str(ratio), "manifest_sha256": manifest_hash}
    elif phase == "stage_admission" or boundary == "each_operation":
        guard = escalation_guard(policy, ledger_rows, forecast_metric, manifest_hash, manifest["stage_id"])
    else:
        # The remaining requests use the same immutable, already admitted stage
        # forecast. A changing rolling sample does not expand that approved plan.
        guard = {"decision": "within_frozen_admitted_stage", "manifest_sha256": manifest_hash}
    return reservations, readiness, guard, expected_costs


def original_admitted_forecast(ledger, manifest_hash, full_operation_count):
    forecasts = []
    for path in ledger.directory.glob("*.forecast.json"):
        private_path(path, ledger.repo_root)
        record = read_json(path)
        if record.get("manifest_sha256") == manifest_hash and record.get("remaining_operations") == full_operation_count:
            forecasts.append(record)
    if not forecasts:
        raise ProgrammeError("original_frozen_stage_admission_missing")
    return forecasts[0]


def original_admitted_pricing(ledger, manifest_hash, full_operation_count):
    return original_admitted_forecast(ledger, manifest_hash, full_operation_count)["pricing"]


def verify_pending_key_limits(policy, metadata, reference):
    for name in policy["pending_key_metadata_binding_fields"]:
        if name not in metadata or name not in reference or metadata[name] != reference[name]:
            raise ProgrammeError("pending_billing_key_limit_binding_changed")


def aggregate_receipt(policy, stage_id, rows, stage_start, planned_count, reason=None):
    stage_rows = [row for row in rows if row["stage_id"] == stage_id]
    actual = sum((gate.amount(r["actual_cost_usd"]) for r in rows if r.get("actual_cost_usd") is not None), Decimal(0))
    unresolved = sum((gate.amount(r["reservation_usd"]) for r in rows if r.get("actual_cost_usd") is None), Decimal(0))
    return {"schema": "loom.research_programme_stage_receipt/1", "stage_id": stage_id,
            "status": "stopped" if reason else "completed", "reason": reason,
            "planned_operations": planned_count, "attempted_operations": len(stage_rows),
            "completed_operations": sum(r.get("state") == "completed" for r in stage_rows),
            "pending_billing_operations": sum(r.get("state") == "pending_billing" for r in stage_rows),
            "stage_actual_usd": str(sum((gate.amount(r["actual_cost_usd"]) for r in stage_rows if r.get("actual_cost_usd") is not None), Decimal(0))),
            "cumulative_actual_usd": str(actual), "unresolved_reservations_usd": str(unresolved),
            "remaining_configured_cap_usd": str(gate.amount(policy["usd_cap"]) - actual - unresolved)}


def reconcile_key_reads(send, key, fingerprint, expected_usage, policy, ledger, prefix):
    """Collect bounded read-only evidence for documented accounting lag.

    Higher/unknown usage is never treated as lag. No POST is repeated or newly
    dispatched while these reads are unresolved.
    """
    options = policy["read_only_reconciliation"]["key"]
    maximum = int(options["maximum_reads"])
    if maximum < 1:
        raise ProgrammeError("invalid_metadata_read_count")
    for index in range(maximum):
        response = send("GET", "key", None, None)
        if key.encode() in response["raw"]:
            raise ProgrammeError("credential_in_provider_response")
        write_private(ledger.directory / (prefix + "-" + str(index) + ".bin"), response["raw"])
        metadata = transport.normalize_key_metadata(response, fingerprint, utc().isoformat())
        usage = gate.amount(metadata["usage_usd"])
        if usage >= expected_usage or index + 1 == maximum:
            return metadata
        time.sleep(float(gate.amount(options["delay_seconds"])))


def generation_reads(send, key, generation_id, policy, ledger, token):
    options = policy["read_only_reconciliation"]["generation"]
    maximum = int(options["maximum_reads"])
    if maximum < 1:
        raise ProgrammeError("invalid_metadata_read_count")
    reads = []
    for index in range(maximum):
        response = send("GET", "generation", None, {"id": generation_id})
        write_private(ledger.records / (token + ".generation-read-" + str(index) + ".bin"), response["raw"])
        reads.append({"http_status": response["http_status"], "raw_sha256": sha(response["raw"]),
                      "latency_seconds": response["latency_seconds"]})
        if key.encode() in response["raw"]:
            raise ProgrammeError("credential_in_private_response_no_continuation")
        if response["http_status"] == 200 or response["http_status"] not in options["pending_http_statuses"] or index + 1 == maximum:
            return response, reads
        time.sleep(float(gate.amount(options["delay_seconds"])))


def run_stage(policy, manifest_path, evidence, private_dir, key_file, repo_root, *, transport_fn=None):
    """Injected transport is for offline tests only and is absent from the CLI."""
    timing = billing_timing(policy)
    raw_manifest = Path(manifest_path).read_bytes()
    # The exact manifest hash and operations share one strict parsing snapshot.
    parsed_manifest = manifests.read_manifest_bytes(raw_manifest)
    base_dir = Path(manifest_path).resolve().parent
    manifest = manifests.load_manifest(parsed_manifest, base_dir=base_dir)
    operations = manifests.load_operations(parsed_manifest, base_dir=base_dir)
    if manifest["programme_id"] != policy["programme_id"]:
        raise ProgrammeError("manifest_programme_mismatch")
    verify_fx(evidence, policy)
    key = transport.load_key_file(key_file, repo_root)
    if key.encode() in raw_manifest or key.encode() in canonical(policy) or key.encode() in canonical(evidence):
        raise ProgrammeError("credential_in_supplied_data")
    for op in operations:
        op["stage_id"] = manifest["stage_id"]
        op["provider_aliases"] = policy["provider_aliases"].get(op["provider_id"], [op["provider_id"]])
        op["model_aliases"] = policy.get("model_aliases", {}).get(op["model_id"], [op["model_id"]])
        op["api_type"] = policy["transport"]["routes"][op["route_id"]]["api_type"]
        if key.encode() in op["request_bytes"]:
            raise ProgrammeError("credential_in_request")
        bounds_options = policy["units_bound_verifiers"]
        if bounds_options.get("prompt_at_least_request_bytes") and gate.amount(op["units_upper_bounds"].get("prompt", "0")) < len(op["request_bytes"]):
            raise ProgrammeError("declared_prompt_bound_below_request_bytes")
        if bounds_options.get("completion_at_least_max_tokens") and op["route_id"] == "chat":
            body = op["request_body"]
            declared_max = body.get("max_tokens", body.get("max_completion_tokens"))
            if declared_max is None or gate.amount(op["units_upper_bounds"].get("completion", "0")) < gate.amount(declared_max):
                raise ProgrammeError("declared_completion_bound_below_request_maximum")
    fingerprint, manifest_hash = transport.key_fingerprint(key), sha(raw_manifest)
    send = transport_fn or transport.OpenRouterTransport(policy["transport"], key).request
    ledger = PrivateLedger(private_dir, repo_root, policy["programme_id"], fingerprint)
    with PauseControl(policy, private_dir, repo_root) as pause, ledger.locked(allow_bound_pending=timing == "stage_end"):
        ledger.bind_stage(manifest["stage_id"], manifest_hash)
        existing = {row["operation_id"]: row for row in ledger.rows()}
        for op in operations:
            old = existing.get(op["operation_id"])
            if old and (old["manifest_sha256"] != manifest_hash or old["request_sha256"] != op["request_sha256"]):
                raise ProgrammeError("existing_operation_changed")
        for row in existing.values():
            if row.get("state") == "pending_billing" and (row["stage_id"] != manifest["stage_id"]
                    or row["manifest_sha256"] != manifest_hash or row["operation_id"] not in {op["operation_id"] for op in operations}):
                raise ProgrammeError("prior_stage_pending_billing_requires_get_only_resolution")
        remaining = [op for op in operations if op["operation_id"] not in existing]
        stage_start = len(existing)
        if not remaining and not any(row.get("state") == "pending_billing" for row in existing.values()):
            result = aggregate_receipt(policy, manifest["stage_id"], ledger.rows(), stage_start, len(operations))
            result.update(status="already_completed_saved_receipt", fresh_preflight_performed=False)
            return result
        if pause.requested(0):
            result = aggregate_receipt(policy, manifest["stage_id"], ledger.rows(), stage_start, len(operations))
            result.update(status="paused", fresh_preflight_performed=False, pause_boundary="no_operation_in_flight")
            return result
        # Actual public endpoint snapshots are fetched before every stage.
        fresh_pricing = []
        price_raw_records = []
        seen = set()
        for op in remaining or operations[:1]:
            pair = op["model_id"], op["provider_id"]
            if pair not in seen:
                response = send("GET", "model_endpoints", None, {"model_id": op["model_id"]})
                if key.encode() in response["raw"]:
                    raise ProgrammeError("credential_in_provider_response")
                fresh_pricing.append(endpoint_quote(response, op, utc().isoformat(), policy))
                price_raw_records.append(response["raw"])
                seen.add(pair)
        evidence = {**evidence, "pricing": fresh_pricing}
        key_response = send("GET", "key", None, None)
        if key.encode() in key_response["raw"]:
            raise ProgrammeError("credential_in_provider_response")
        metadata = transport.normalize_key_metadata(key_response, fingerprint, utc().isoformat())
        prior_forecast = original_admitted_forecast(ledger, manifest_hash, len(operations)) if any(row["manifest_sha256"] == manifest_hash for row in existing.values()) else None
        prior_pricing = prior_forecast["pricing"] if prior_forecast is not None else None
        pending_key_reference = prior_forecast["key_metadata"] if timing == "stage_end" and prior_forecast is not None else metadata
        if timing == "stage_end":
            verify_pending_key_limits(policy, metadata, pending_key_reference)
        reservations, readiness, guard, expected_costs = stage_evidence(policy, manifest, manifest_hash, remaining or operations[:1], evidence, metadata, fingerprint, ledger.rows(), admitted_pricing=prior_pricing, no_paid_dispatch=not remaining)
        forecast = {"schema": "loom.research_programme_forecast/1", "stage_id": manifest["stage_id"],
                    "manifest_sha256": manifest_hash, "remaining_operations": len(remaining),
                    "accounting": readiness["accounting"], "escalation_guard": guard,
                    "pricing": fresh_pricing, "fx": evidence["fx"], "key_metadata": metadata,
                    "billing_verification_timing": timing, "policy_sha256": sha(canonical(policy)),
                    "provider_usage_reconciliation": readiness["provider_usage_reconciliation"]}
        receipt_token = uuid.uuid4().hex
        for index, raw in enumerate(price_raw_records):
            write_private(ledger.directory / (receipt_token + ".price-" + str(index) + ".bin"), raw)
        write_private(ledger.directory / (receipt_token + ".key-before.bin"), key_response["raw"])
        write_private(ledger.directory / (receipt_token + ".forecast.json"), canonical(forecast))
        reason = None
        paused, completed_this_invocation = False, 0
        for position, op in enumerate(remaining):
            if pause.requested(completed_this_invocation):
                paused = True
                break
            row = ledger.reserve(op, manifest_hash, reservations[op["operation_id"]], expected_costs[op["operation_id"]], billing_timing=timing)
            try:
                response = send("POST", op["route_id"], op["request_bytes"], None)
                raw = response["raw"]
                if not isinstance(raw, bytes):
                    raise ProgrammeError("provider_response_unsafe")
                write_private(ledger.records / (sha(row["operation_id"].encode()) + ".response.bin"), raw)
                row.update(response_sha256=sha(raw), http_status=response["http_status"], latency_seconds=response["latency_seconds"], transport_error=response.get("transport_error"))
                if key.encode() in raw:
                    raise ProgrammeError("credential_in_private_response_no_continuation")
                parsed = transport.extract_receipt(response, op)
                row.update(parsed)
                if response.get("transport_error"):
                    raise ProgrammeError("partial_response_billing_uncertain")
                if not parsed.get("generation_id"):
                    raise ProgrammeError("generation_identity_unknown")
                if any(saved.get("generation_id") == parsed["generation_id"] for saved in ledger.rows() if saved["operation_id"] != row["operation_id"]):
                    raise ProgrammeError("duplicate_generation_identity_no_adoption")
                defer_billing = timing == "stage_end" and (not policy["require_credit_proof_per_pair"] or pair_credit_proven(ledger.rows(), row["receipt_operation"]))
                if defer_billing:
                    if response["http_status"] != 200:
                        raise ProgrammeError("http_error_no_retry")
                    verify_bound_first_receipt(parsed, row["receipt_operation"], row["reservation_usd"])
                    row["state"] = "pending_billing"
                else:
                    generation, reads = generation_reads(send, key, parsed["generation_id"], policy, ledger, sha(row["operation_id"].encode()))
                    row["generation_reads"] = reads
                    write_private(ledger.records / (sha(row["operation_id"].encode()) + ".generation.bin"), generation["raw"])
                    row["generation_sha256"] = sha(generation["raw"])
                    if key.encode() in generation["raw"]:
                        raise ProgrammeError("credential_in_private_response_no_continuation")
                    verified = transport.verify_generation_receipt(generation, parsed, op)
                    row.update(verified)
                    row["actual_cost_usd"] = row.get("actual_cost_usd", row.get("reported_cost_usd"))
                    if row.get("is_byok") is not False or row.get("actual_cost_usd") is None:
                        raise ProgrammeError("credit_billing_unknown")
                    if gate.amount(row["actual_cost_usd"]) > gate.amount(row["reservation_usd"]):
                        raise ProgrammeError("actual_cost_exceeds_reservation")
                    if response["http_status"] != 200:
                        raise ProgrammeError("http_error_no_retry")
                    row["state"] = "completed"
            except Exception:
                row["state"] = "uncertain"
                reason = "attempt_failed_or_billing_uncertain_no_retry"
                ledger.finish(row)
                break
            ledger.finish(row)
            # A fresh exact-key usage check after every paid operation is mandatory.
            known = None
            try:
                if timing == "stage_end":
                    ledger.validate(allow_bound_pending=True)
                known = sum((gate.amount(saved["actual_cost_usd"]) for saved in ledger.rows() if saved.get("actual_cost_usd") is not None), Decimal(0))
                metadata = reconcile_key_reads(send, key, fingerprint, known, policy, ledger, receipt_token + ".key-after-" + str(position))
                if timing == "stage_end":
                    verify_pending_key_limits(policy, metadata, pending_key_reference)
                next_operations = remaining[position + 1:]
                # Empty stages still reconcile the just-finished cost without dispatch.
                accounting_only = not next_operations
                if accounting_only:
                    next_operations = [op]
                _, readiness, _, _ = stage_evidence(policy, manifest, manifest_hash, next_operations, evidence, metadata, fingerprint, ledger.rows(), phase="after_operation", no_paid_dispatch=accounting_only)
                if timing == "stage_end":
                    write_private(ledger.directory / (receipt_token + ".pending-accounting-" + str(position) + ".json"),
                                  canonical({"schema": "loom.research_programme_pending_accounting/1", "manifest_sha256": manifest_hash,
                                             "operation_id": row["operation_id"], "policy_sha256": sha(canonical(policy)),
                                             "key_metadata": metadata, "readiness": readiness}))
            except Exception as failure:
                reason = "post_operation_metadata_or_budget_mismatch"
                if isinstance(failure, ProgrammeError) and hasattr(failure, "readiness"):
                    readiness = failure.readiness
                else:
                    readiness = {"provider_usage_reconciliation": {"policy": policy.get("provider_usage_lag", {"mode": "strict"})["mode"],
                        "observed_provider_usage_usd": None, "verified_cumulative_actual_usd": str(known) if known is not None else None,
                        "strict_gate_status": "post_operation_evidence_unavailable"}}
                break
            completed_this_invocation += 1
            if position + 1 < len(remaining) and pause.requested(completed_this_invocation):
                paused = True
                break
        if not reason and not paused and timing == "stage_end":
            try:
                for original in ledger.original_rows():
                    if original.get("state") == "pending_billing" and original["operation_id"] not in ledger.late_proofs():
                        resolve_captured_attempt(ledger, original, policy, send, key)
                ledger.validate()
                known = sum((gate.amount(saved["actual_cost_usd"]) for saved in ledger.rows()), Decimal(0))
                metadata = reconcile_key_reads(send, key, fingerprint, known, policy, ledger, receipt_token + ".key-stage-final")
                verify_pending_key_limits(policy, metadata, pending_key_reference)
                # A stage cannot complete until every receipt is verified and
                # the fresh cumulative key check clears the ordinary final gate.
                _, readiness, _, _ = stage_evidence(policy, manifest, manifest_hash, remaining[-1:] or operations[:1], evidence, metadata, fingerprint, ledger.rows(), phase="after_operation", no_paid_dispatch=True)
            except Exception as failure:
                reason = "stage_end_billing_not_yet_verified"
                if isinstance(failure, ProgrammeError) and hasattr(failure, "readiness"):
                    readiness = failure.readiness
                else:
                    rows = ledger.rows()
                    readiness = {"provider_usage_reconciliation": {
                        "policy": policy.get("provider_usage_lag", {"mode": "strict"})["mode"],
                        "billing_verification_timing": timing, "observed_provider_usage_usd": None,
                        "verified_cumulative_actual_usd": str(sum((gate.amount(saved["actual_cost_usd"]) for saved in rows if saved.get("actual_cost_usd") is not None), Decimal(0))),
                        "unresolved_reservations_usd": str(sum((gate.amount(saved["reservation_usd"]) for saved in rows if saved.get("actual_cost_usd") is None), Decimal(0))),
                        "strict_gate_status": "stage_end_billing_evidence_unavailable"}}
        receipt = aggregate_receipt(policy, manifest["stage_id"], ledger.rows(), stage_start, len(operations), reason)
        receipt["provider_usage_reconciliation"] = readiness["provider_usage_reconciliation"]
        if paused and not reason:
            receipt.update(status="paused", pause_boundary="after_operation_accounting", fresh_preflight_performed=True)
        if reason:
            ledger.stop(reason)
        private_receipt = {**receipt, "attempt_history": [{"operation_id": original["operation_id"],
                           "original_attempt_sha256": sha(canonical(original))} for original in ledger.original_rows()]}
        write_private(ledger.directory / (receipt_token + ".actual.json"), canonical(private_receipt))
        return receipt


def resolve_captured_attempt(ledger, original, policy, send, key):
    bound_pending = ledger.bound_pending(original)
    if not (bound_pending or ledger.captured_pending(original)):
        raise ProgrammeError("captured_attempt_not_eligible_for_late_proof")
    token = sha(original["operation_id"].encode())
    parsed = transport.extract_receipt({"http_status": 200, "raw": (ledger.records / (token + ".response.bin")).read_bytes()}, original["receipt_operation"])
    if any(row["operation_id"] != original["operation_id"] and row.get("generation_id") == parsed["generation_id"] for row in ledger.rows()):
        raise ProgrammeError("duplicate_generation_identity_no_resolution")
    resolution_id = uuid.uuid4().hex
    options = policy["read_only_reconciliation"]["generation"]
    maximum = int(options["maximum_reads"])
    if maximum < 1:
        raise ProgrammeError("invalid_metadata_read_count")
    reads = []
    for index in range(maximum):
        response = send("GET", "generation", None, {"id": parsed["generation_id"]})
        filename = resolution_id + ".captured-generation-read-" + str(index) + ".bin"
        write_private(ledger.directory / filename, response["raw"])
        reads.append({"file": filename, "http_status": response["http_status"], "raw_sha256": sha(response["raw"]),
                      "latency_seconds": response["latency_seconds"], "transport_error": response.get("transport_error")})
        if key.encode() in response["raw"]:
            raise ProgrammeError("credential_in_private_response_no_resolution")
        if response["http_status"] == 200 or response["http_status"] not in options["pending_http_statuses"] or index + 1 == maximum:
            break
        time.sleep(float(gate.amount(options["delay_seconds"])))
    try:
        verified = transport.verify_generation_receipt(response, parsed, original["receipt_operation"])
        if gate.amount(verified["actual_cost_usd"]) > gate.amount(original["reservation_usd"]):
            raise ProgrammeError("late_proof_cost_exceeds_original_reservation")
    except Exception:
        pending_read = response["http_status"] != 200 and response["http_status"] in options["pending_http_statuses"] and not response.get("transport_error")
        ledger.append_metadata_audit({"schema": "loom.research_programme_generation_read_audit/1", "capture_id": resolution_id,
            "operation_id": original["operation_id"], "programme_id": ledger.programme_id, "key_fingerprint_sha256": ledger.fingerprint,
            "original_attempt_sha256": sha(canonical(original)), "late_reads": reads,
            "pending_http_statuses": list(options["pending_http_statuses"]), "outcome": "pending" if pending_read else "contradiction", "paid_calls": 0})
        if bound_pending and not pending_read:
            ledger.append_verification_failure({"schema": "loom.research_programme_billing_failure/1",
                "failure_id": resolution_id, "operation_id": original["operation_id"], "programme_id": ledger.programme_id,
                "key_fingerprint_sha256": ledger.fingerprint, "original_attempt_sha256": sha(canonical(original)),
                "late_reads": reads, "reason": "generation_verification_contradiction", "paid_calls": 0})
        raise
    ledger.append_metadata_audit({"schema": "loom.research_programme_generation_read_audit/1", "capture_id": resolution_id,
        "operation_id": original["operation_id"], "programme_id": ledger.programme_id, "key_fingerprint_sha256": ledger.fingerprint,
        "original_attempt_sha256": sha(canonical(original)), "late_reads": reads,
        "pending_http_statuses": list(options["pending_http_statuses"]), "outcome": "verified", "paid_calls": 0})
    witnesses = []
    suffixes = (".started.json", ".request.bin", ".response.bin", ".result.json") + (() if bound_pending else (".generation.bin",))
    for suffix in suffixes:
        path = ledger.records / (token + suffix)
        witnesses.append({"file": "records/" + path.name, "sha256": sha(path.read_bytes())})
    for index, read in enumerate(original.get("generation_reads", [])):
        witnesses.append({"file": "records/" + token + ".generation-read-" + str(index) + ".bin", "sha256": read["raw_sha256"]})
    projection = {**verified, "state": "completed", "generation_sha256": reads[-1]["raw_sha256"],
                  "attempt_resolution_id": resolution_id, "original_state": original["state"]}
    record = {"schema": "loom.research_programme_captured_attempt_resolution/1", "resolution_id": resolution_id,
              "operation_id": original["operation_id"], "programme_id": ledger.programme_id,
              "key_fingerprint_sha256": ledger.fingerprint, "manifest_sha256": original["manifest_sha256"],
              "resolved_at": utc().isoformat(), "original_attempt_sha256": sha(canonical(original)),
              "original_evidence": witnesses, "eligible_generation_http_statuses": [] if bound_pending else list(ledger.captured_pending_statuses),
              "late_reads": reads, "projection": projection, "paid_calls": 0,
              "no_attempt_adoption_or_retry": True, "policy_sha256": sha(canonical(policy))}
    ledger.append_attempt_proof(record)


def reconcile_stop(policy, manifest_path, evidence, private_dir, key_file, repo_root, *, transport_fn=None, captured_pending=False):
    """GET-only resolution of a stop after fully completed proven attempts.

    Reserved/uncaptured uncertainty fails before provider access. The explicitly
    selected captured mode can append late generation proof to an already bound
    HTTP200 first response; original attempt payloads/bytes remain unchanged.
    This command never dispatches or retries a POST or adopts an orphan.
    """
    directory = private_path(private_dir, repo_root, directory=True)
    if not (directory / "ledger.sqlite3").is_file():
        raise ProgrammeError("existing_private_ledger_required")
    raw_manifest = Path(manifest_path).read_bytes()
    parsed = manifests.read_manifest_bytes(raw_manifest)
    manifest = manifests.load_manifest(parsed, base_dir=Path(manifest_path).resolve().parent)
    operations = manifests.load_operations(parsed, base_dir=Path(manifest_path).resolve().parent)
    if manifest["programme_id"] != policy["programme_id"]:
        raise ProgrammeError("manifest_programme_mismatch")
    verify_fx(evidence, policy)
    key = transport.load_key_file(key_file, repo_root)
    if key.encode() in raw_manifest or key.encode() in canonical(policy) or key.encode() in canonical(evidence):
        raise ProgrammeError("credential_in_supplied_data")
    fingerprint, manifest_hash = transport.key_fingerprint(key), sha(raw_manifest)
    send = transport_fn or transport.OpenRouterTransport(policy["transport"], key).request
    ledger = PrivateLedger(directory, repo_root, policy["programme_id"], fingerprint,
                           captured_pending_statuses=policy["read_only_reconciliation"]["generation"]["pending_http_statuses"])
    with ledger.locked(allow_stopped=True, allow_captured_pending=captured_pending, allow_bound_pending=captured_pending):
        previous_stop = ledger.active_stop()
        if previous_stop is None:
            raise ProgrammeError("active_stop_required_for_resolution")
        ledger.bind_stage(manifest["stage_id"], manifest_hash)
        if captured_pending:
            for original in ledger.original_rows():
                if original["state"] in ("uncertain", "pending_billing") and original["operation_id"] not in ledger.late_proofs():
                    resolve_captured_attempt(ledger, original, policy, send, key)
            ledger.validate()
        rows = ledger.rows()  # locked().validate() has replayed every receipt.
        if not rows or any(row["state"] != "completed" for row in rows):
            raise ProgrammeError("only_completed_verified_attempts_can_reconcile")
        for operation in operations:
            if key.encode() in operation["request_bytes"]:
                raise ProgrammeError("credential_in_request")
        remaining = [operation for operation in operations if operation["operation_id"] not in {row["operation_id"] for row in rows}]
        selected = remaining or operations[:1]
        pricing, price_raw_records, seen = [], [], set()
        for operation in selected:
            pair = operation["model_id"], operation["provider_id"]
            if pair not in seen:
                response = send("GET", "model_endpoints", None, {"model_id": operation["model_id"]})
                if key.encode() in response["raw"]:
                    raise ProgrammeError("credential_in_provider_response")
                pricing.append(endpoint_quote(response, operation, utc().isoformat(), policy))
                price_raw_records.append(response["raw"])
                seen.add(pair)
        resolution_id = uuid.uuid4().hex
        for index, raw in enumerate(price_raw_records):
            write_private(ledger.directory / (resolution_id + ".resolution-price-" + str(index) + ".bin"), raw)
        known = sum((gate.amount(row["actual_cost_usd"]) for row in rows), Decimal(0))
        metadata = reconcile_key_reads(send, key, fingerprint, known, policy, ledger, resolution_id + ".resolution-key")
        if any(row.get("state") == "pending_billing" and row.get("manifest_sha256") == manifest_hash for row in ledger.original_rows()):
            reference = original_admitted_forecast(ledger, manifest_hash, len(operations))["key_metadata"]
            verify_pending_key_limits(policy, metadata, reference)
        admitted = original_admitted_pricing(ledger, manifest_hash, len(operations))
        _, readiness, guard, _ = stage_evidence(policy, manifest, manifest_hash, selected, {**evidence, "pricing": pricing}, metadata, fingerprint, rows, admitted_pricing=admitted, no_paid_dispatch=not remaining)
        witnesses = []
        for path in ledger.directory.glob("*.actual.json"):
            private_path(path, repo_root)
            raw = path.read_bytes()
            saved = json.loads(raw)
            if saved.get("status") == "stopped" and saved.get("reason") == previous_stop:
                witnesses.append({"file": path.name, "sha256": sha(raw)})
        if not witnesses:
            raise ProgrammeError("immutable_original_stop_receipt_required")
        record = {"schema": "loom.research_programme_stop_resolution/1", "resolution_id": resolution_id,
                  "programme_id": policy["programme_id"], "key_fingerprint_sha256": fingerprint,
                  "manifest_sha256": manifest_hash, "resolved_at": utc().isoformat(),
                  "previous_active_stop_projection": {"reason": previous_stop, "sha256": sha(canonical({"reason": previous_stop}))},
                  "original_stop_receipts": witnesses, "verified_completed_operations": len(rows),
                  "paid_calls": 0, "no_attempt_adoption_or_retry": True,
                  "key_metadata": metadata, "provider_usage_reconciliation": readiness["provider_usage_reconciliation"],
                  "accounting": readiness["accounting"], "escalation_guard": guard,
                  "policy_sha256": sha(canonical(policy))}
        ledger.resolve_stop(record)
        return {"status": "stop_resolved_read_only", "paid_calls": 0,
                "verified_completed_operations": len(rows), "cumulative_actual_usd": str(known),
                "provider_usage_reconciliation": readiness["provider_usage_reconciliation"]}


def plan_stage(policy, manifest_path, evidence):
    timing = billing_timing(policy)
    raw_manifest = Path(manifest_path).read_bytes()
    parsed = manifests.read_manifest_bytes(raw_manifest)
    base_dir = Path(manifest_path).resolve().parent
    manifest = manifests.load_manifest(parsed, base_dir=base_dir)
    operations = manifests.load_operations(parsed, base_dir=base_dir)
    result = {"schema": "loom.research_programme_forecast/1", "stage_id": manifest["stage_id"],
              "manifest_sha256": sha(raw_manifest), "operations": len(operations),
              "network_calls": 0, "paid_calls": 0, "status": "pricing_evidence_missing"}
    result["billing_verification_timing"] = timing
    if evidence.get("pricing"):
        reservations, pairs, expected_costs = price_plan(operations, evidence["pricing"], policy)
        result.update(status="offline_forecast_only", reservation_usd=str(sum(reservations.values(), Decimal(0))), model_provider_pairs=pairs)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "run", "request-pause", "reconcile", "reconcile-captured"))
    parser.add_argument("--policy", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--evidence", help="FX plus optional offline prices; no credential")
    parser.add_argument("--private-dir")
    parser.add_argument("--key-file")
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[3]))
    args = parser.parse_args(argv)
    try:
        policy = read_json(args.policy)
        if args.command == "request-pause":
            if not args.private_dir:
                raise ProgrammeError("private_ledger_directory_required")
            result = request_pause(policy, args.private_dir, Path(args.repo_root).resolve())
        elif not args.manifest or not args.evidence:
            raise ProgrammeError("manifest_and_evidence_required")
        elif args.command == "plan":
            evidence = read_json(args.evidence)
            result = plan_stage(policy, args.manifest, evidence)
        else:
            if not args.private_dir or not args.key_file:
                raise ProgrammeError("private_key_file_and_ledger_required")
            if args.command in ("reconcile", "reconcile-captured"):
                result = reconcile_stop(policy, args.manifest, read_json(args.evidence), args.private_dir, args.key_file, Path(args.repo_root).resolve(), captured_pending=args.command == "reconcile-captured")
            else:
                result = run_stage(policy, args.manifest, read_json(args.evidence), args.private_dir, args.key_file, Path(args.repo_root).resolve())
        # Only aggregate fields are exposed; never raw bodies/key fingerprints.
        print(json.dumps(result, sort_keys=True, default=str))
        return 0 if result.get("status") != "stopped" else 2
    except Exception:
        print(json.dumps({"status": "blocked", "reason": "programme_preflight_or_accounting_failed"}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
