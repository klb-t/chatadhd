"""Accounting regressions use fabricated public records and local Git only."""
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import billing_reconciliation_v1 as billing


def attempt(identifier, day, cost, generation=None):
    return {"experiment_id": "fixture", "attempt_id": identifier,
            "started_at": day + "T00:00:00Z", "request_sha256": identifier * 64,
            "response_sha256": None, "reported_cost_usd": cost,
            "reservation_usd": "0.01", "state": "uncertain" if cost is None else "completed",
            "generation_id": generation, "ledger_locations": ["fixture/ledger.json"]}


def saved(rows):
    known = sum((billing.money(row["reported_cost_usd"]) for row in rows
                 if row["reported_cost_usd"] is not None), Decimal(0))
    return {"attempts": rows,
            "summary": {"known_reported_usd": str(known), "raw_hash_verified_attempts": 0,
                        "raw_cost_matches": 0, "raw_cost_mismatches": 0},
            "ledger_receipts": [{"attempt_records": len(rows) * 2}]}


def allocation():
    return {"saved_campaign_accounting": {"prior_key_usage_usd": "0.1", "known_incremental_usd": "0.02"},
            "provider_key_metadata": {"usage_usd": "1.1", "checked_at": "2026-09-30T12:00:00Z"},
            "planned_requests": 107, "reserved_usd": "0.4", "actual_resume_attempts_at_creation": 0}


class BillingReconciliationTests(unittest.TestCase):
    def base(self):
        rows = [attempt("a", "2026-09-28", "0.03", "old"),
                attempt("b", "2026-09-30", "0.02", "campaign"),
                attempt("c", "2026-09-30", None)]
        return rows, saved(rows), allocation()

    def test_opening_already_contains_older_costs_no_double_charge(self):
        _, source, plan = self.base()
        report = billing.reconcile(source, plan)
        self.assertEqual(Decimal(report["accounting"]["opening_plus_campaign_known_usd"]), Decimal("0.12"))
        self.assertEqual(Decimal(report["accounting"]["opening_usage_without_individual_receipts_usd"]), Decimal("0.07"))
        self.assertEqual(Decimal(report["accounting"]["later_usage_difference_unassigned_usd"]), Decimal("0.98"))

    def test_unknown_and_planned_reservations_are_not_spend(self):
        _, source, plan = self.base()
        report = billing.reconcile(source, plan)
        self.assertEqual(report["accounting"]["unknown_reservations_usd"], "0.01")
        self.assertEqual(report["accounting"]["planned_resume_attempts_at_creation"], 0)
        self.assertEqual(report["saved_evidence"]["known_reported_usd"], "0.05")

    def test_copied_artifact_rows_not_counted_as_real_attempts(self):
        rows, source, plan = self.base()
        report = billing.reconcile(source, plan)
        self.assertEqual(report["saved_evidence"]["unique_attempts"], len(rows))
        self.assertEqual(report["saved_evidence"]["copied_rows_not_counted_twice"], len(rows))

    def test_repeated_request_at_different_time_is_separate_attempt(self):
        rows, source, plan = self.base()
        row = deepcopy(rows[1]); row.update(started_at="2026-09-30T00:01:00Z", generation_id="second")
        rows.append(row); plan["saved_campaign_accounting"]["known_incremental_usd"] = "0.04"
        report = billing.reconcile(saved(rows), plan)
        self.assertEqual(report["saved_evidence"]["unique_attempts"], 4)

    def test_duplicate_attempt_identity_rejected(self):
        rows, _, plan = self.base(); rows.append(deepcopy(rows[1]))
        with self.assertRaisesRegex(ValueError, "duplicate attempt"):
            billing.reconcile(saved(rows), plan)

    def test_saved_summary_and_campaign_claim_checked_against_receipts(self):
        _, source, plan = self.base(); source["summary"]["known_reported_usd"] = "0"
        with self.assertRaisesRegex(ValueError, "summary"):
            billing.reconcile(source, plan)
        rows, source, plan = self.base(); plan["saved_campaign_accounting"]["known_incremental_usd"] = "0"
        with self.assertRaisesRegex(ValueError, "campaign"):
            billing.reconcile(source, plan)

    def test_verification_summary_counters_and_source_row_count_must_match(self):
        _, source, plan = self.base(); source["summary"]["raw_hash_verified_attempts"] = 300
        with self.assertRaisesRegex(ValueError, "verification counter"):
            billing.reconcile(source, plan)
        _, source, plan = self.base(); source["ledger_receipts"][0]["attempt_records"] = 1
        with self.assertRaisesRegex(ValueError, "smaller than unique"):
            billing.reconcile(source, plan)

    def test_checkpoint_billing_timing_difference_retained_without_attribution(self):
        rows, source, plan = self.base(); rows[1]["finished_at"] = "2026-09-30T00:00:01Z"
        check = {"checked_at": "2026-09-30T00:01:00Z", "usage_usd_inferred_from_limit_minus_remaining": "0.115"}
        report = billing.reconcile(source, plan, checkpoints=[check])
        delta = report["campaign_checkpoint_receipt_comparisons"][0]
        self.assertEqual(Decimal(delta["observed_minus_opening_minus_completed_known_usd"]), Decimal("-0.005"))

    def test_export_duplicate_costs_dedup_and_conflicts_fail(self):
        normalized, copies = billing.normalize_export([
            {"id": "campaign", "total_cost": "0.02"}, {"id": "campaign", "total_cost": "0.020"}], "id", "total_cost")
        self.assertEqual(len(normalized), 1); self.assertEqual(copies, 1)
        with self.assertRaisesRegex(ValueError, "Conflicting provider"):
            billing.normalize_export([{"generation_id": "a", "cost_usd": "0.01"},
                                      {"generation_id": "a", "cost_usd": "0.02"}])

    def test_provider_join_reports_cost_mismatch_and_unmatched_without_guessing(self):
        rows, source, plan = self.base()
        exports = [{"generation_id": "campaign", "cost_usd": "0.04"},
                   {"generation_id": "other", "cost_usd": "0.98"}]
        report = billing.reconcile(source, plan, provider_rows=exports)["provider_export_join"]
        self.assertEqual(report["matched_generations"], 1)
        self.assertEqual(len(report["matched_cost_mismatches"]), 1)
        self.assertEqual(report["unmatched_generations"], 1)
        self.assertEqual(report["key_usage_difference_automatically_assigned_usd"], "0")

    def test_unknown_export_cost_preserved_as_evidence_without_fake_key_identity(self):
        rows, source, plan = self.base(); rows[2]["generation_id"] = "uncertain"
        joined = billing.reconcile(source, plan, provider_rows=[
            {"generation_id": "uncertain", "cost_usd": "0.003"}])["provider_export_join"]
        self.assertEqual(len(joined["unknown_costs_with_supplied_provider_record"]), 1)
        self.assertEqual(joined["identity_binding"], "unverified_by_this_offline_tool")

    def test_generation_alias_across_saved_attempts_is_ambiguous(self):
        rows, source, plan = self.base(); rows[1]["generation_id"] = rows[0]["generation_id"]
        with self.assertRaisesRegex(ValueError, "multiple saved attempts"):
            billing.reconcile(source, plan)

    def test_invalid_money_rejected_and_long_decimal_preserved(self):
        for value in (True, "NaN", "Infinity", "-0.01"):
            with self.assertRaises(ValueError):
                billing.money(value)
        value = Decimal("0.123456789123456789123456789")
        rows, _ = billing.normalize_export([{"generation_id": "a", "cost_usd": value}])
        self.assertEqual(rows[0]["cost_usd"], str(value))

    def test_generation_ids_and_checkpoint_recovered_from_hashed_local_git(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            def git(*args):
                return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.DEVNULL)
            git("init", "-q"); git("config", "user.name", "Offline fixture")
            git("config", "user.email", "fixture@example.invalid")
            root = repo / "docs/research/fake/run"; root.mkdir(parents=True)
            raw = json.dumps({"id": "gen-fixture", "usage": {"cost": "0.02"}}).encode()
            digest = hashlib.sha256(raw).hexdigest()
            ledger = {"experiment_id": "fixture", "attempts": [{
                "request_hash": "b" * 64, "started_at": "2026-09-30T00:00:00Z",
                "finished_at": "2026-09-30T00:00:01Z"}],
                "key_check": {"checked_at": "2026-09-30T00:00:00Z", "limit_usd": "2",
                              "remaining_usd": "1.875", "nonresetting": True,
                              "label": "this secret-looking label must not be copied"}}
            ledger_bytes = json.dumps(ledger).encode()
            (root / "ledger.json").write_bytes(ledger_bytes)
            (root / "fake.response.bin").write_bytes(raw)
            git("add", "."); git("commit", "-qm", "fixture")
            row = attempt("b", "2026-09-30", "0.02"); row["response_sha256"] = digest
            source = {"attempts": [row], "snapshots": [{"commit": git("rev-parse", "HEAD").decode().strip()}],
                      "ledger_receipts": [{"location": "docs/research/fake/run/ledger.json",
                                           "git_container_blob": git("hash-object", str(root / "ledger.json")).decode().strip(),
                                           "sha256": hashlib.sha256(ledger_bytes).hexdigest()}]}
            rows, checkpoints = billing.enrich_from_git(repo, source)
            self.assertEqual(rows[0]["generation_id"], "gen-fixture")
            self.assertEqual(rows[0]["finished_at"], "2026-09-30T00:00:01Z")
            self.assertEqual(checkpoints[0]["usage_usd_inferred_from_limit_minus_remaining"], "0.125")
            self.assertNotIn("label", checkpoints[0])
            source["ledger_receipts"][0]["sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "receipt hash mismatch"):
                billing.enrich_from_git(repo, source)

    def test_cli_json_export_preserves_decimal_and_omits_unknown_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); _, source, plan = self.base()
            (root / "audit.json").write_text(json.dumps(source))
            (root / "allocation.json").write_text(json.dumps(plan))
            (root / "provider.json").write_text('[{"generation_id":"other","cost_usd":0.123456789123456789123456789,"credential":"must-not-be-copied"}]')
            output = root / "result.json"
            command = [sys.executable, str(Path(billing.__file__)), "--saved-audit", str(root / "audit.json"),
                       "--allocation", str(root / "allocation.json"), "--provider-export", str(root / "provider.json"),
                       "--output", str(output)]
            subprocess.check_output(command)
            data = output.read_text(); report = json.loads(data)
            self.assertEqual(report["provider_export_join"]["unmatched"][0]["cost_usd"], "0.123456789123456789123456789")
            self.assertEqual(report["network_calls"], 0)
            self.assertEqual(report["saved_evidence"]["raw_byte_verification"], "upstream_saved_flags_only")
            self.assertNotIn("must-not-be-copied", data)
            with self.assertRaises(subprocess.CalledProcessError):
                subprocess.check_output(command, stderr=subprocess.DEVNULL)

    def test_cli_csv_export_uses_configured_columns(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); _, source, plan = self.base()
            (root / "audit.json").write_text(json.dumps(source))
            (root / "allocation.json").write_text(json.dumps(plan))
            (root / "provider.csv").write_text('id,total_cost\ncampaign,0.02\ncampaign,0.020\n')
            output = root / "result.json"
            subprocess.check_output([sys.executable, str(Path(billing.__file__)),
                                     "--saved-audit", str(root / "audit.json"), "--allocation", str(root / "allocation.json"),
                                     "--provider-export", str(root / "provider.csv"),
                                     "--export-id-field", "id", "--export-cost-field", "total_cost", "--output", str(output)])
            joined = json.loads(output.read_text())["provider_export_join"]
            self.assertEqual(joined["matched_generations"], 1)
            self.assertEqual(joined["export_rows_deduplicated"], 1)


if __name__ == "__main__":
    unittest.main()
