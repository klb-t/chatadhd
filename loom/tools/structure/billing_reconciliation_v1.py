#!/usr/bin/env python3
"""Reconcile saved research receipts and optional provider exports, entirely offline.

This is an additional instrument. Frozen runners and their first receipts are
not modified. No credential loading, HTTP client or live execution exists here.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import zipfile


def money(value):
    if isinstance(value, bool):
        raise ValueError("Boolean monetary amount")
    result = Decimal(str(value))
    if not result.is_finite() or result < 0:
        raise ValueError("Monetary amount must be finite and nonnegative")
    return result


def safe_name(value):
    return not any(word in value.lower() for word in ("holdout", "sealed"))


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def read_blob(repo, oid):
    if not re.fullmatch(r"[0-9a-f]{40,64}", oid):
        raise ValueError("Invalid Git object ID")
    return git(repo, "cat-file", "blob", oid)


def attempt_identity(row):
    return (row["experiment_id"], row["request_sha256"], row["started_at"])


def timestamp(value):
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("Receipt timestamps require an explicit timezone")
    return parsed.astimezone(timezone.utc)


def enrich_from_git(repo, saved):
    """Read only saved public ledgers and their referenced response hashes.

    Git snapshots and ZIP copies share content hashes. Raw message content is
    never included in the output. Only generation ID and completion time enrich
    the old auditor's per-attempt records.
    """
    container_cache = {}
    details, checkpoints = {}, {}
    wanted = {row["response_sha256"] for row in saved["attempts"]
              if row.get("response_sha256")}
    responses = {}
    for receipt in saved["ledger_receipts"]:
        location = receipt["location"]
        if not safe_name(location):
            raise ValueError("Excluded source in saved receipt")
        oid = receipt["git_container_blob"]
        data = container_cache.setdefault(oid, None)
        if data is None:
            data = container_cache[oid] = read_blob(repo, oid)
        if "!" in location:
            _, member = location.split("!", 1)
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                ledger_bytes = archive.read(member)
                for name in archive.namelist():
                    if safe_name(name) and name.endswith(".response.bin"):
                        raw = archive.read(name)
                        digest = hashlib.sha256(raw).hexdigest()
                        if digest in wanted:
                            responses[digest] = raw
        else:
            ledger_bytes = data
        if hashlib.sha256(ledger_bytes).hexdigest() != receipt["sha256"]:
            raise ValueError("Saved ledger receipt hash mismatch")
        ledger = json.loads(ledger_bytes)
        for row in ledger["attempts"]:
            identity = (ledger["experiment_id"], row["request_hash"], row["started_at"])
            detail = {"finished_at": row.get("finished_at"),
                      "generation_id": row.get("generation_id")}
            if identity in details and details[identity] != detail:
                raise ValueError("Conflicting generation/time copies")
            details[identity] = detail
        check = ledger.get("key_check")
        if check:
            # Retain safe monetary fields, not credential labels or account IDs.
            snapshot = {key: check.get(key) for key in
                        ("checked_at", "limit_usd", "remaining_usd", "nonresetting")}
            snapshot["usage_usd_inferred_from_limit_minus_remaining"] = str(
                money(snapshot["limit_usd"]) - money(snapshot["remaining_usd"]))
            pin = json.dumps(snapshot, sort_keys=True)
            if pin not in checkpoints:
                checkpoints[pin] = {**snapshot, "ledger_locations": []}
            checkpoints[pin]["ledger_locations"].append(location)

    # Some loose ledger receipts have their raw responses only in another tip.
    # Read the response-file trees, never any sealed evaluation directory.
    scanned_blobs = set()
    for snapshot in saved["snapshots"]:
        commit = snapshot["commit"]
        if not re.fullmatch(r"[0-9a-f]{40,64}", commit):
            raise ValueError("Invalid saved snapshot commit")
        for entry in git(repo, "ls-tree", "-rz", commit, "--", "docs/research").split(b"\0"):
            if not entry:
                continue
            meta, raw_path = entry.split(b"\t", 1)
            path = raw_path.decode()
            if not safe_name(path) or not path.endswith(".response.bin"):
                continue
            oid = meta.split()[2].decode()
            if oid in scanned_blobs:
                continue
            scanned_blobs.add(oid)
            raw = read_blob(repo, oid)
            digest = hashlib.sha256(raw).hexdigest()
            if digest in wanted:
                responses[digest] = raw

    rows = []
    for row in saved["attempts"]:
        detail = details.get(attempt_identity(row), {})
        generation = detail.get("generation_id")
        raw = responses.get(row.get("response_sha256"))
        raw_cost_matches = None
        if raw is not None:
            envelope = json.loads(raw, parse_float=Decimal)
            raw_generation = envelope.get("id")
            if generation and raw_generation and generation != raw_generation:
                raise ValueError("Ledger generation ID disagrees with response")
            generation = generation or raw_generation
            raw_cost = envelope.get("usage", {}).get("cost")
            if raw_cost is not None and row.get("reported_cost_usd") is not None:
                raw_cost_matches = money(raw_cost) == money(row["reported_cost_usd"])
        rows.append({**row, "generation_id": generation,
                     "finished_at": detail.get("finished_at"),
                     "raw_hash_verified": raw is not None, "raw_cost_matches": raw_cost_matches})
    return rows, sorted(checkpoints.values(), key=lambda item: item["checked_at"])


def normalize_export(rows, id_field="generation_id", cost_field="cost_usd"):
    """Explicit column mapping supports JSON/CSV without relying on model names."""
    result = {}
    duplicates = 0
    for row in rows:
        identifier = row.get(id_field)
        if not isinstance(identifier, str) or not identifier:
            raise ValueError("Provider export requires a nonempty generation ID")
        cost = str(money(row[cost_field]))
        record = {"generation_id": identifier, "cost_usd": cost}
        if identifier in result:
            if money(result[identifier]["cost_usd"]) != money(cost):
                raise ValueError("Conflicting provider export costs for one generation")
            duplicates += 1
        else:
            result[identifier] = record
    return list(result.values()), duplicates


def join_export(attempts, provider_rows, duplicate_rows=0):
    indexed = {}
    for row in attempts:
        identifier = row.get("generation_id")
        if identifier:
            if identifier in indexed:
                raise ValueError("One provider generation ID binds multiple saved attempts")
            indexed[identifier] = row
    matched, unmatched, mismatches, recovered_unknown = [], [], [], []
    for row in provider_rows:
        attempt = indexed.get(row["generation_id"])
        if attempt is None:
            unmatched.append(row)
            continue
        match = {**row, "experiment_id": attempt["experiment_id"],
                 "attempt_id": attempt["attempt_id"],
                 "saved_reported_cost_usd": attempt.get("reported_cost_usd")}
        matched.append(match)
        if match["saved_reported_cost_usd"] is None:
            recovered_unknown.append(match)
        elif money(match["saved_reported_cost_usd"]) != money(match["cost_usd"]):
            mismatches.append(match)
    return {"match_key": "exact generation_id; no temporal/model-name guessing",
            "identity_binding": "unverified_by_this_offline_tool",
            "export_rows_deduplicated": duplicate_rows,
            "matched_generations": len(matched), "matched": matched,
            "matched_cost_mismatches": mismatches,
            "unknown_costs_with_supplied_provider_record": recovered_unknown,
            "unmatched_generations": len(unmatched), "unmatched": unmatched,
            "unmatched_export_usd": str(sum((money(row["cost_usd"]) for row in unmatched), Decimal(0))),
            "key_usage_difference_automatically_assigned_usd": "0",
            "boundary": "Export authenticity, same-key identity, interval completeness and billing timing require independent evidence."}


def reconcile(saved, allocation, attempts=None, checkpoints=None, campaign_day="2026-09-30",
              provider_rows=None, export_duplicate_rows=0, raw_byte_verification="upstream_saved_flags_only"):
    attempts = attempts if attempts is not None else saved["attempts"]
    if len({attempt_identity(row) for row in attempts}) != len(attempts):
        raise ValueError("Saved audit contains duplicate attempt identities")
    known = sum((money(row["reported_cost_usd"]) for row in attempts
                 if row.get("reported_cost_usd") is not None), Decimal(0))
    if known != money(saved["summary"]["known_reported_usd"]):
        raise ValueError("Saved audit summary does not match its attempt receipts")
    for row in attempts:
        for flag in ("raw_hash_verified", "raw_cost_matches"):
            if row.get(flag) is not None and type(row[flag]) is not bool:
                raise ValueError("Saved receipt verification flags must be boolean or null")
        if row.get("raw_cost_matches") is True and (row.get("raw_hash_verified") is not True
                                                   or row.get("reported_cost_usd") is None):
            raise ValueError("Cost match requires verified raw bytes and a reported cost")
    counters = {"raw_hash_verified_attempts": sum(row.get("raw_hash_verified") is True for row in attempts),
                "raw_cost_matches": sum(row.get("raw_cost_matches") is True for row in attempts),
                "raw_cost_mismatches": sum(row.get("raw_cost_matches") is False for row in attempts)}
    for field, observed in counters.items():
        if type(saved["summary"][field]) is not int or saved["summary"][field] != observed:
            raise ValueError("Saved audit verification counter does not match attempt flags: " + field)
    days = {attempt_identity(row): timestamp(row["started_at"]).date().isoformat() for row in attempts}
    campaign = [row for row in attempts if days[attempt_identity(row)] == campaign_day]
    older = [row for row in attempts if days[attempt_identity(row)] < campaign_day]
    later = [row for row in attempts if days[attempt_identity(row)] > campaign_day]
    campaign_known = sum((money(row["reported_cost_usd"]) for row in campaign
                          if row.get("reported_cost_usd") is not None), Decimal(0))
    older_known = sum((money(row["reported_cost_usd"]) for row in older
                       if row.get("reported_cost_usd") is not None), Decimal(0))
    opening = money(allocation["saved_campaign_accounting"]["prior_key_usage_usd"])
    recorded_campaign = money(allocation["saved_campaign_accounting"]["known_incremental_usd"])
    if recorded_campaign != campaign_known:
        raise ValueError("Allocation campaign amount disagrees with saved attempts")
    checkpoint_usage = money(allocation["provider_key_metadata"]["usage_usd"])
    unknown = [{key: row.get(key) for key in
                ("experiment_id", "attempt_id", "started_at", "state", "reservation_usd",
                 "generation_id", "response_sha256", "ledger_locations")}
               for row in attempts if row.get("reported_cost_usd") is None]
    generation_ids = [row["generation_id"] for row in attempts if row.get("generation_id")]
    if len(set(generation_ids)) != len(generation_ids):
        raise ValueError("One provider generation ID binds multiple saved attempts")
    if any(type(row["attempt_records"]) is not int or row["attempt_records"] < 0 for row in saved["ledger_receipts"]):
        raise ValueError("Source ledger row counts must be nonnegative integers")
    source_rows = sum(row["attempt_records"] for row in saved["ledger_receipts"])
    if source_rows < len(attempts):
        raise ValueError("Source ledger row count cannot be smaller than unique attempts")
    checkpoint_comparisons = []
    for check in checkpoints or []:
        checked_at = timestamp(check["checked_at"])
        if checked_at.date().isoformat() != campaign_day:
            continue
        completed = [row for row in campaign if row.get("finished_at")
                     and timestamp(row["finished_at"]) <= checked_at
                     and row.get("reported_cost_usd") is not None]
        completed_known = sum((money(row["reported_cost_usd"]) for row in completed), Decimal(0))
        checkpoint_comparisons.append({"checked_at": check["checked_at"],
                                       "campaign_completed_known_usd": str(completed_known),
                                       "observed_minus_opening_minus_completed_known_usd": str(
                                           Decimal(check["usage_usd_inferred_from_limit_minus_remaining"])
                                           - opening - completed_known),
                                       "interpretation": "Temporal comparison only; provider billing timing and same-key continuity are unverified."})
    result = {
        "schema": "loom.billing_reconciliation/1", "network_calls": 0,
        "scope": "Saved public research receipts; not a complete provider invoice or key identity proof",
        "campaign_day_utc": campaign_day,
        "saved_evidence": {"source_ledger_rows": source_rows, "unique_attempts": len(attempts),
                           "copied_rows_not_counted_twice": source_rows - len(attempts),
                           "known_reported_usd": str(known), "older_attempts": len(older),
                           "campaign_attempts": len(campaign), "later_attempts": len(later),
                           "generation_ids_available": len(generation_ids),
                           "distinct_generation_ids": len(set(generation_ids)),
                           "raw_byte_verification": raw_byte_verification,
                           **counters},
        "accounting": {"opening_key_usage_usd": str(opening),
                       "older_receipts_known_usd": str(older_known),
                       "opening_usage_without_individual_receipts_usd": str(opening - older_known),
                       "campaign_receipts_known_usd": str(campaign_known),
                       "opening_plus_campaign_known_usd": str(opening + campaign_known),
                       "later_observed_key_usage_usd": str(checkpoint_usage),
                       "later_observed_at": allocation["provider_key_metadata"]["checked_at"],
                       "later_usage_difference_unassigned_usd": str(checkpoint_usage - opening - campaign_known),
                       "difference_attribution": "unassigned; same-key continuity and missing receipts are not proved",
                       "unknown_cost_attempts": len(unknown),
                       "unknown_reservations_usd": str(sum((money(row["reservation_usd"]) for row in unknown), Decimal(0))),
                       "planned_resume_requests": allocation["planned_requests"],
                       "planned_resume_reservation_usd": str(money(allocation["reserved_usd"])),
                       "planned_resume_attempts_at_creation": allocation["actual_resume_attempts_at_creation"],
                       "reservation_interpretation": "Reservation is not a charge or guaranteed billing bound; older uncertainty may already be in opening usage."},
        "unknown_cost_attempts": unknown,
        "saved_key_checkpoints": checkpoints or [],
        "campaign_checkpoint_receipt_comparisons": checkpoint_comparisons,
        "attempt_generation_index": [{key: row.get(key) for key in
                                      ("experiment_id", "attempt_id", "started_at", "finished_at",
                                       "generation_id", "reported_cost_usd", "response_sha256")}
                                     for row in attempts],
        "paid_continuation": "Requires fresh explicit owner consent; this tool performs no live calls."}
    if provider_rows is not None:
        result["provider_export_join"] = join_export(attempts, provider_rows, export_duplicate_rows)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--saved-audit", required=True)
    parser.add_argument("--allocation", required=True)
    parser.add_argument("--repo", help="Optional local Git source for generation IDs/checkpoints")
    parser.add_argument("--campaign-day", default="2026-09-30", help="Historical UTC campaign preset")
    parser.add_argument("--provider-export", help="Offline JSON array, JSON object with rows, or CSV")
    parser.add_argument("--export-id-field", default="generation_id")
    parser.add_argument("--export-cost-field", default="cost_usd")
    parser.add_argument("--output", required=True, help="New output file; no existing evidence is overwritten")
    args = parser.parse_args()
    saved_bytes = Path(args.saved_audit).read_bytes()
    allocation_bytes = Path(args.allocation).read_bytes()
    saved, allocation = json.loads(saved_bytes), json.loads(allocation_bytes)
    attempts, checkpoints = enrich_from_git(args.repo, saved) if args.repo else (saved["attempts"], [])
    provider_rows, duplicates, export_bytes = None, 0, None
    if args.provider_export:
        export_bytes = Path(args.provider_export).read_bytes()
        if Path(args.provider_export).suffix.lower() == ".csv":
            rows = list(csv.DictReader(io.StringIO(export_bytes.decode("utf-8-sig"))))
        else:
            value = json.loads(export_bytes, parse_float=Decimal)
            rows = value["rows"] if isinstance(value, dict) else value
        provider_rows, duplicates = normalize_export(rows, args.export_id_field, args.export_cost_field)
    result = reconcile(saved, allocation, attempts, checkpoints, args.campaign_day, provider_rows, duplicates,
                       "git_blobs_and_response_hashes_checked" if args.repo else "upstream_saved_flags_only")
    result["input_sha256"] = {"saved_audit": hashlib.sha256(saved_bytes).hexdigest(),
                              "allocation": hashlib.sha256(allocation_bytes).hexdigest()}
    if export_bytes is not None:
        result["input_sha256"]["provider_export"] = hashlib.sha256(export_bytes).hexdigest()
    with open(args.output, "x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps({"saved_evidence": result["saved_evidence"], "accounting": result["accounting"]}, indent=2))


if __name__ == "__main__":
    main()
