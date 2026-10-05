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
import xml.etree.ElementTree as ET

try:
    from . import new_budget5eur_gate as gate
    from . import research_programme_manifest as manifests
    from . import research_programme_transport as transport
except ImportError:
    import new_budget5eur_gate as gate
    import research_programme_manifest as manifests
    import research_programme_transport as transport


class ProgrammeError(ValueError):
    """Only constant, secret-safe reasons cross the CLI boundary."""


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"), default=str).encode("utf-8")


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def utc():
    return datetime.now(timezone.utc)


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


class PrivateLedger:
    def __init__(self, directory, repo_root, programme_id, fingerprint):
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
        self.db = None

    @contextmanager
    def locked(self):
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
            bound = self.db.execute("SELECT * FROM binding").fetchall()
            if not bound:
                self.db.execute("INSERT INTO binding VALUES (?, ?)", (self.programme_id, self.fingerprint))
            elif len(bound) != 1 or tuple(bound[0]) != (self.programme_id, self.fingerprint):
                raise ProgrammeError("ledger_programme_or_key_mismatch")
            self.db.commit()
            if self.db.execute("SELECT value FROM programme_state WHERE name='stop_reason'").fetchone():
                raise ProgrammeError("durable_programme_stop_requires_independent_reconciliation")
            self.validate()
            yield self
        finally:
            if self.db is not None:
                self.db.close()
                self.db = None
            fcntl.flock(fd, fcntl.LOCK_UN)
            os.close(fd)

    def rows(self):
        return [json.loads(row["payload"]) for row in self.db.execute("SELECT payload FROM attempts ORDER BY rowid")]

    def validate(self):
        expected = set()
        generations = set()
        for row in self.rows():
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
                              "receipt_operation", "expected_cost_usd")
            if any(started.get(name) != row.get(name) for name in binding_fields):
                raise ProgrammeError("durable_started_record_mismatch")
            expected.add(token + ".request.bin")
            requestpath = self.records / (token + ".request.bin")
            private_path(requestpath, self.repo_root)
            if not requestpath.is_file() or sha(requestpath.read_bytes()) != row.get("request_sha256"):
                raise ProgrammeError("frozen_request_missing_or_changed")
            if row.get("state") == "reserved":
                raise ProgrammeError("ambiguous_attempt_no_adoption_or_retry")
            if row.get("state") != "completed" or row.get("actual_cost_usd") is None:
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
            if not resultpath.is_file() or resultpath.read_bytes() != canonical(row):
                raise ProgrammeError("terminal_receipt_mismatch")
            # Completion is evidence, never a trusted state label. Rebuild its
            # billing projection from the immutable first bytes and generation
            # lookup using the pair frozen before dispatch.
            operation = row.get("receipt_operation")
            operation_fields = {"model_id", "provider_id", "provider_aliases", "model_aliases", "route_id", "api_type"}
            if (not isinstance(operation, dict) or not operation_fields <= set(operation)
                    or row.get("is_byok") is not False or row.get("billing_verified") is not True
                    or row.get("http_status") != 200 or row.get("transport_error")
                    or not gate.digest(row.get("generation_sha256"))):
                raise ProgrammeError("completed_billing_proof_missing")
            expected.add(token + ".generation.bin")
            genpath = self.records / (token + ".generation.bin")
            private_path(genpath, self.repo_root)
            if not genpath.is_file() or sha(genpath.read_bytes()) != row["generation_sha256"]:
                raise ProgrammeError("generation_receipt_missing_or_changed")
            for index, read in enumerate(row.get("generation_reads", [])):
                filename = token + ".generation-read-" + str(index) + ".bin"
                expected.add(filename)
                path = self.records / filename
                private_path(path, self.repo_root)
                if not path.is_file() or sha(path.read_bytes()) != read["raw_sha256"]:
                    raise ProgrammeError("generation_read_evidence_changed")
            try:
                first = {"http_status": row["http_status"], "raw": rawpath.read_bytes()}
                generation = {"http_status": 200, "raw": genpath.read_bytes()}
                parsed = transport.extract_receipt(first, operation)
                rebuilt = transport.verify_generation_receipt(generation, parsed, operation)
                if any(row.get(name) != value for name, value in rebuilt.items()):
                    raise ValueError()
                if gate.amount(rebuilt["actual_cost_usd"]) > gate.amount(row["reservation_usd"]):
                    raise ValueError()
            except Exception:
                raise ProgrammeError("completed_billing_proof_replay_mismatch") from None
        actual = set(path.name for path in self.records.iterdir())
        if actual != expected:
            raise ProgrammeError("orphan_or_missing_evidence_with_ledger")

    def exported(self):
        return {"schema": "loom.new_programme_billing_ledger/1", "programme_id": self.programme_id,
                "attempts": self.rows()}

    def reserve(self, operation, manifest_hash, reservation, expected_cost):
        row = {"operation_id": operation["operation_id"], "programme_id": self.programme_id,
               "stage_id": operation["stage_id"], "key_fingerprint_sha256": self.fingerprint,
               "manifest_sha256": manifest_hash, "request_sha256": operation["request_sha256"],
               "reservation_usd": str(reservation), "actual_cost_usd": None,
               "expected_cost_usd": str(expected_cost),
               "receipt_operation": {name: operation[name] for name in ("model_id", "provider_id", "provider_aliases", "model_aliases", "route_id", "api_type")},
               "state": "reserved", "started_at": utc().isoformat()}
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


def endpoint_quote(response, operation, checked_at, policy):
    if response["http_status"] != 200 or response.get("transport_error"):
        raise ProgrammeError("endpoint_quote_unavailable")
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
            "source_ref": policy["transport"]["base_url"] + policy["transport"]["routes"]["model_endpoints"]["path"].format(model_id=operation["model_id"]),
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
        prices = quotes[0]["component_prices_usd"]
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


def stage_evidence(policy, manifest, manifest_hash, operations, evidence, metadata, fingerprint, ledger_rows, *, phase="stage_admission"):
    reservations, pairs, expected_costs = price_plan(operations, evidence["pricing"], policy)
    binding = {"programme_id": policy["programme_id"], "key_fingerprint_sha256": fingerprint,
               "bound_to_loaded_credential": True,
               "separate_new_key_owner_confirmation_ref": policy["separate_new_key_owner_confirmation_ref"]}
    stage = {"stage_id": manifest["stage_id"], "manifest_sha256": manifest_hash,
             "reservation_usd": str(sum(reservations.values(), Decimal(0))), "model_provider_pairs": pairs}
    bound = {**evidence, "key_binding": binding, "key_metadata": metadata, "stage": stage}
    ledger = {"schema": "loom.new_programme_billing_ledger/1", "programme_id": policy["programme_id"], "attempts": ledger_rows}
    readiness = gate.evaluate(policy, bound, ledger, utc())
    if readiness["status"] != "ready_for_bound_transport_preflight":
        raise ProgrammeError("budget_evidence_gate_blocked:" + ",".join(readiness["blockers"]))
    scope = policy["escalation_guard"]["scope"]
    forecast_metric = max(expected_costs.values(), default=Decimal(0)) if scope == "per_operation" else sum(expected_costs.values(), Decimal(0))
    boundary = policy["escalation_guard"]["evaluation_boundary"]
    if boundary not in ("stage_admission", "each_operation"):
        raise ProgrammeError("unknown_escalation_evaluation_boundary")
    if phase == "stage_admission" or boundary == "each_operation":
        guard = escalation_guard(policy, ledger_rows, forecast_metric, manifest_hash, manifest["stage_id"])
    else:
        # The remaining requests use the same immutable, already admitted stage
        # forecast. A changing rolling sample does not expand that approved plan.
        guard = {"decision": "within_frozen_admitted_stage", "manifest_sha256": manifest_hash}
    return reservations, readiness, guard, expected_costs


def aggregate_receipt(policy, stage_id, rows, stage_start, planned_count, reason=None):
    stage_rows = [row for row in rows if row["stage_id"] == stage_id]
    actual = sum((gate.amount(r["actual_cost_usd"]) for r in rows if r.get("actual_cost_usd") is not None), Decimal(0))
    unresolved = sum((gate.amount(r["reservation_usd"]) for r in rows if r.get("actual_cost_usd") is None), Decimal(0))
    return {"schema": "loom.research_programme_stage_receipt/1", "stage_id": stage_id,
            "status": "stopped" if reason else "completed", "reason": reason,
            "planned_operations": planned_count, "attempted_operations": len(stage_rows),
            "completed_operations": sum(r.get("state") == "completed" for r in stage_rows),
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
        if response["http_status"] not in options["pending_http_statuses"] or index + 1 == maximum:
            return response, reads
        time.sleep(float(gate.amount(options["delay_seconds"])))


def run_stage(policy, manifest_path, evidence, private_dir, key_file, repo_root, *, transport_fn=None):
    """Injected transport is for offline tests only and is absent from the CLI."""
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
    with ledger.locked():
        ledger.bind_stage(manifest["stage_id"], manifest_hash)
        existing = {row["operation_id"]: row for row in ledger.rows()}
        for op in operations:
            old = existing.get(op["operation_id"])
            if old and (old["manifest_sha256"] != manifest_hash or old["request_sha256"] != op["request_sha256"]):
                raise ProgrammeError("existing_operation_changed")
        remaining = [op for op in operations if op["operation_id"] not in existing]
        stage_start = len(existing)
        if not remaining:
            result = aggregate_receipt(policy, manifest["stage_id"], ledger.rows(), stage_start, len(operations))
            result.update(status="already_completed_saved_receipt", fresh_preflight_performed=False)
            return result
        # Actual public endpoint snapshots are fetched before every stage.
        fresh_pricing = []
        price_raw_records = []
        seen = set()
        for op in remaining:
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
        reservations, readiness, guard, expected_costs = stage_evidence(policy, manifest, manifest_hash, remaining, evidence, metadata, fingerprint, ledger.rows())
        forecast = {"schema": "loom.research_programme_forecast/1", "stage_id": manifest["stage_id"],
                    "manifest_sha256": manifest_hash, "remaining_operations": len(remaining),
                    "accounting": readiness["accounting"], "escalation_guard": guard,
                    "pricing": fresh_pricing, "fx": evidence["fx"], "key_metadata": metadata}
        receipt_token = uuid.uuid4().hex
        for index, raw in enumerate(price_raw_records):
            write_private(ledger.directory / (receipt_token + ".price-" + str(index) + ".bin"), raw)
        write_private(ledger.directory / (receipt_token + ".key-before.bin"), key_response["raw"])
        write_private(ledger.directory / (receipt_token + ".forecast.json"), canonical(forecast))
        reason = None
        for position, op in enumerate(remaining):
            row = ledger.reserve(op, manifest_hash, reservations[op["operation_id"]], expected_costs[op["operation_id"]])
            try:
                response = send("POST", op["route_id"], op["request_bytes"], None)
                raw = response["raw"]
                if not isinstance(raw, bytes):
                    raise ProgrammeError("provider_response_unsafe")
                write_private(ledger.records / (sha(row["operation_id"].encode()) + ".response.bin"), raw)
                row.update(response_sha256=sha(raw), http_status=response["http_status"], latency_seconds=response["latency_seconds"])
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
            try:
                known = sum((gate.amount(saved["actual_cost_usd"]) for saved in ledger.rows()), Decimal(0))
                metadata = reconcile_key_reads(send, key, fingerprint, known, policy, ledger, receipt_token + ".key-after-" + str(position))
                next_operations = remaining[position + 1:]
                # Empty stages still reconcile the just-finished cost without dispatch.
                if not next_operations:
                    quote = [q for q in evidence["pricing"] if q["model_id"] == op["model_id"] and q["provider_id"] == op["provider_id"]][0]
                    next_operations = [{**op, "operation_id": "reconciliation-only-no-dispatch",
                                        "minimum_reservation_usd": "0",
                                        "units_upper_bounds": {name: "0" for name in quote["component_prices_usd"]}}]
                stage_evidence(policy, manifest, manifest_hash, next_operations, evidence, metadata, fingerprint, ledger.rows(), phase="after_operation")
            except Exception:
                reason = "post_operation_metadata_or_budget_mismatch"
                break
        receipt = aggregate_receipt(policy, manifest["stage_id"], ledger.rows(), stage_start, len(operations), reason)
        if reason:
            ledger.stop(reason)
        write_private(ledger.directory / (receipt_token + ".actual.json"), canonical(receipt))
        return receipt


def plan_stage(policy, manifest_path, evidence):
    raw_manifest = Path(manifest_path).read_bytes()
    parsed = manifests.read_manifest_bytes(raw_manifest)
    base_dir = Path(manifest_path).resolve().parent
    manifest = manifests.load_manifest(parsed, base_dir=base_dir)
    operations = manifests.load_operations(parsed, base_dir=base_dir)
    result = {"schema": "loom.research_programme_forecast/1", "stage_id": manifest["stage_id"],
              "manifest_sha256": sha(raw_manifest), "operations": len(operations),
              "network_calls": 0, "paid_calls": 0, "status": "pricing_evidence_missing"}
    if evidence.get("pricing"):
        reservations, pairs, expected_costs = price_plan(operations, evidence["pricing"], policy)
        result.update(status="offline_forecast_only", reservation_usd=str(sum(reservations.values(), Decimal(0))), model_provider_pairs=pairs)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "run"))
    parser.add_argument("--policy", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--evidence", required=True, help="FX plus optional offline prices; no credential")
    parser.add_argument("--private-dir")
    parser.add_argument("--key-file")
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[3]))
    args = parser.parse_args(argv)
    try:
        policy, evidence = read_json(args.policy), read_json(args.evidence)
        if args.command == "plan":
            result = plan_stage(policy, args.manifest, evidence)
        else:
            if not args.private_dir or not args.key_file:
                raise ProgrammeError("private_key_file_and_ledger_required")
            result = run_stage(policy, args.manifest, evidence, args.private_dir, args.key_file, Path(args.repo_root).resolve())
        # Only aggregate fields are exposed; never raw bodies/key fingerprints.
        print(json.dumps(result, sort_keys=True, default=str))
        return 0 if result.get("status") != "stopped" else 2
    except Exception:
        print(json.dumps({"status": "blocked", "reason": "programme_preflight_or_accounting_failed"}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
