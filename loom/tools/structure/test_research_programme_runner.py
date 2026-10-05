"""Adversarial fake-key/fake-transport checks: never contacts a provider."""
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import unittest

import research_programme_runner as runner


def wire(value):
    return json.dumps(value, separators=(",", ":")).encode()


class MockTransport:
    def __init__(self, test):
        self.test = test
        self.calls = []
        self.usage = Decimal(0)
        self.cost = Decimal("0.01")
        self.post_usage_mismatch = False
        self.response_override = None
        self.generation_override = None
        self.after_post_crash = False
        self.generation_pending = 0
        self.key_lagging = 0
        self.key_usage_override = None
        self.distinct_generations = False
        self.post_count = 0
        self.on_post = None

    def __call__(self, method, route, body=None, params=None):
        self.calls.append((method, route, body, params))
        result = {"http_status": 200, "latency_seconds": 0.25}
        if route == "key":
            usage = self.usage + (Decimal("0.001") if self.post_usage_mismatch and self.usage else 0)
            if self.usage and self.key_lagging:
                self.key_lagging -= 1
                usage = Decimal(0)
            if self.key_usage_override is not None:
                usage = self.key_usage_override
            result["raw"] = wire({"data": {"usage": str(usage), "limit": "5", "limit_remaining": str(5-usage),
                "byok_usage": "0", "include_byok_in_limit": False, "is_management_key": False, "limit_reset": None}})
        elif route == "model_endpoints":
            result["raw"] = wire({"data": {"id": "fake/model", "endpoints": [{"model_id": "fake/model",
                "provider_name": "Fake", "pricing": {"prompt": "0.0001", "completion": "0.0002", "discount": 0}}]}})
        elif route == "chat":
            # Both the database row and private started evidence predate POST.
            with sqlite3.connect(self.test.private / "ledger.sqlite3") as db:
                row = json.loads(db.execute("SELECT payload FROM attempts ORDER BY rowid DESC LIMIT 1").fetchone()[0])
            self.test.assertEqual(row["state"], "reserved")
            self.test.assertTrue(list((self.test.private / "records").glob("*.started.json")))
            self.test.assertEqual(body, self.test.body)
            self.usage += self.cost
            self.post_count += 1
            generation_id = "gen-fake-" + str(self.post_count) if self.distinct_generations else "gen-fake"
            result["raw"] = wire({"id": generation_id, "model": "fake/model", "provider": "Fake",
                "usage": {"cost": str(self.cost), "is_byok": False, "prompt_tokens": 1, "completion_tokens": 1}})
            if self.response_override is not None:
                result.update(self.response_override)
            if self.on_post is not None:
                self.on_post()
        elif route == "generation":
            if self.after_post_crash:
                raise SystemExit("simulated process death")
            result["raw"] = wire({"data": {"id": params["id"], "model": "fake/model", "provider_name": "Fake",
                "api_type": "completions", "total_cost": str(self.cost), "is_byok": False}})
            if self.generation_override is not None:
                result.update(self.generation_override)
            if self.generation_pending:
                self.generation_pending -= 1
                result.update(http_status=404, raw=b'{"error":"pending"}')
        else:
            raise AssertionError(route)
        return result

    def posts(self):
        return [call for call in self.calls if call[0] == "POST"]


class ProgrammeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.private = self.root / "private"
        self.keydir = self.root / "key"
        self.keydir.mkdir(mode=0o700)
        self.keyfile = self.keydir / "key.txt"
        self.keyfile.write_text("fixture-credential-not-a-real-key\n")
        self.keyfile.chmod(0o600)
        self.body = b'{ "model": "fake/model", "provider": {"only":["fake"],"allow_fallbacks":false}, "messages":[], "max_tokens":10 }\n'
        (self.repo / "body.json").write_bytes(self.body)
        self.manifest = self.repo / "manifest.json"
        self.manifest.write_bytes(wire({"schema": "loom.research_programme_manifest/1", "programme_id": "fake-programme",
            "stage_id": "stage-one", "operations": [{"operation_id": "one", "route_id": "chat",
                "request_file": "body.json", "request_sha256": hashlib.sha256(self.body).hexdigest(),
                "model_id": "fake/model", "provider_id": "fake",
                "units_upper_bounds": {"prompt": "200", "completion": "10", "request": "1"}}]}))
        policy_path = Path(__file__).resolve().parents[3] / "docs/research/model_research_2026-10-04/billing/runner-policy.json"
        self.policy = json.loads(policy_path.read_bytes())
        self.policy.update(programme_id="fake-programme", usd_cap="5", provider_aliases={"fake": ["Fake"]})
        self.policy["fx_snapshot"] = {"format": "json", "rate_path": ["rate"],
            "source_url_prefix": "https://fake.example/", "maximum_rate_age_seconds": 86400}
        for option in self.policy["read_only_reconciliation"].values():
            option["delay_seconds"] = "0"
        fxfile = self.repo / "fx.json"
        fxfile.write_bytes(b'{"rate":"1.1"}')
        now = datetime.now(timezone.utc)
        self.evidence = {"fx": {"base_currency": "EUR", "quote_currency": "USD", "usd_per_eur": "1.1",
            "source_ref": "https://fake.example/fx", "checked_at": now.isoformat(), "rate_date": now.date().isoformat(),
            "raw_file": str(fxfile), "raw_sha256": hashlib.sha256(fxfile.read_bytes()).hexdigest()}}
        self.send = MockTransport(self)

    def run_stage(self):
        return runner.run_stage(self.policy, self.manifest, self.evidence, self.private, self.keyfile, self.repo, transport_fn=self.send)

    def two_operations(self):
        manifest = json.loads(self.manifest.read_bytes())
        second = dict(manifest["operations"][0])
        second["operation_id"] = "two"
        manifest["operations"].append(second)
        self.manifest.write_bytes(wire(manifest))
        self.send.distinct_generations = True

    def reconcile(self):
        return runner.reconcile_stop(self.policy, self.manifest, self.evidence, self.private, self.keyfile, self.repo, transport_fn=self.send)

    def test_exact_bytes_durable_reserve_receipts_and_usage(self):
        result = self.run_stage()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["cumulative_actual_usd"], "0.01")
        self.assertEqual(result["unresolved_reservations_usd"], "0")
        self.assertEqual(len(self.send.posts()), 1)
        self.assertEqual(self.send.calls[-1][1], "key")
        for path in self.private.rglob("*"):
            self.assertEqual(path.stat().st_mode & 0o077, 0)
        raw = next((self.private / "records").glob("*.response.bin")).read_bytes()
        self.assertIn(b'"cost":"0.01"', raw)
        self.assertNotIn("fixture-credential", json.dumps(result))

    def test_completed_bound_operation_is_never_redispatched(self):
        self.run_stage()
        self.send.calls.clear()
        self.run_stage()
        self.assertEqual(self.send.calls, [])

    def test_orphan_response_without_ledger_blocks_all_network(self):
        (self.private / "records").mkdir(parents=True, mode=0o700)
        self.private.chmod(0o700)
        orphan = self.private / "records" / "orphan.response.bin"
        orphan.write_bytes(b"first response")
        orphan.chmod(0o600)
        with self.assertRaisesRegex(runner.ProgrammeError, "orphan_evidence_without_ledger"):
            self.run_stage()
        self.assertFalse(self.send.calls)

    def test_orphan_response_with_ledger_blocks_all_network(self):
        self.run_stage()
        self.send.calls.clear()
        orphan = self.private / "records" / "orphan.response.bin"
        orphan.write_bytes(b"first response")
        orphan.chmod(0o600)
        with self.assertRaisesRegex(runner.ProgrammeError, "orphan_or_missing"):
            self.run_stage()
        self.assertFalse(self.send.calls)

    def test_crash_after_first_response_is_never_adopted_or_retried(self):
        self.send.after_post_crash = True
        with self.assertRaises(SystemExit):
            self.run_stage()
        self.assertTrue(list((self.private / "records").glob("*.response.bin")))
        self.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "ambiguous_attempt"):
            self.run_stage()
        self.assertFalse(self.send.calls)

    def test_unknown_cost_retains_reservation_and_stops(self):
        self.send.response_override = {"http_status": 400, "raw": b'{"error":{"message":"invented"}}'}
        result = self.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(result["unresolved_reservations_usd"], "0.0220")
        self.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "durable_programme_stop"):
            self.run_stage()
        self.assertFalse(self.send.calls)

    def test_generation_pair_or_cost_mismatch_stops_without_retry(self):
        for field, value in (("model", "other/model"), ("provider_name", "Other"), ("total_cost", "0.011"), ("is_byok", True)):
            with self.subTest(field=field):
                # Each variant uses a separate non-resetting ledger.
                self.private = self.root / ("private-" + field)
                self.send = MockTransport(self)
                data = {"id": "gen-fake", "model": "fake/model", "provider_name": "Fake",
                    "api_type": "completions", "total_cost": "0.01", "is_byok": False}
                data[field] = value
                self.send.generation_override = {"raw": wire({"data": data})}
                result = self.run_stage()
                self.assertEqual(result["status"], "stopped")
                self.assertEqual(len(self.send.posts()), 1)

    def test_post_operation_usage_mismatch_stops(self):
        self.send.post_usage_mismatch = True
        result = self.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(result["reason"], "post_operation_metadata_or_budget_mismatch")
        self.assertEqual(result["cumulative_actual_usd"], "0.01")

    def test_ledger_binds_exact_loaded_credential(self):
        self.run_stage()
        self.keyfile.write_text("other-fake-key")
        self.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "key_mismatch"):
            self.run_stage()
        self.assertFalse(self.send.calls)

    def test_first_response_or_started_record_tamper_stops(self):
        self.run_stage()
        raw = next((self.private / "records").glob("*.response.bin"))
        raw.write_bytes(b"replacement")
        self.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "first_response_missing_or_changed"):
            self.run_stage()
        self.assertFalse(self.send.calls)

    def test_fx_raw_hash_rate_date_and_authority_are_verified(self):
        for field, value in (("raw_sha256", "0"*64), ("usd_per_eur", "0.9"), ("rate_date", "2000-01-01")):
            original = self.evidence["fx"][field]
            self.evidence["fx"][field] = value
            with self.assertRaises(runner.ProgrammeError):
                self.run_stage()
            self.evidence["fx"][field] = original
        self.assertFalse(self.send.calls)

    def test_quote_component_unknown_blocks_before_post(self):
        self.policy["absent_component_prices_usd"]["unmodelled_component"] = "0.5"
        with self.assertRaisesRegex(runner.ProgrammeError, "unaccounted_price_component"):
            self.run_stage()
        self.assertFalse(self.send.posts())

    def test_rolling_tenfold_baseline_and_explicit_unknown_policy(self):
        guard = runner.escalation_guard(self.policy, [], Decimal("0.1"), "a"*64)
        self.assertEqual(guard["baseline_status"], "unknown")
        rows = [{"state": "completed", "actual_cost_usd": "0.00002", "expected_cost_usd": "0.01"}]
        with self.assertRaisesRegex(runner.ProgrammeError, "tenfold"):
            runner.escalation_guard(self.policy, rows, Decimal("0.1"), "a"*64)
        self.policy["escalation_guard"]["confirmations"] = [{"manifest_sha256": "a"*64, "source_ref": "invented-owner-confirmation"}]
        runner.escalation_guard(self.policy, rows, Decimal("0.1"), "a"*64)
        self.policy["escalation_guard"]["unknown_baseline_policy"] = "stop"
        with self.assertRaisesRegex(runner.ProgrammeError, "baseline_unknown"):
            runner.escalation_guard(self.policy, [], Decimal("0.1"), "a"*64)

    def test_unchanged_jev_reservation_does_not_create_false_escalation(self):
        rows = [{"state": "completed", "actual_cost_usd": "0.00002", "expected_cost_usd": "0.0002", "reservation_usd": "0.001"}]
        guard = runner.escalation_guard(self.policy, rows, Decimal("0.0002"), "a"*64)
        self.assertEqual(guard["forecast_ratio"], "1")

    def test_stage_total_scope_detects_count_growth_and_excludes_current_stage(self):
        self.policy["escalation_guard"]["scope"] = "stage_total"
        rows = [{"state": "completed", "stage_id": "old", "expected_cost_usd": "0.01"},
                {"state": "completed", "stage_id": "current", "expected_cost_usd": "0.001"}]
        with self.assertRaisesRegex(runner.ProgrammeError, "tenfold"):
            runner.escalation_guard(self.policy, rows, Decimal("0.1"), "a"*64, "current")

    def test_manual_prompt_or_output_bound_below_request_is_rejected(self):
        original = json.loads(self.manifest.read_bytes())
        for component in ("prompt", "completion"):
            changed = json.loads(wire(original))
            changed["operations"][0]["units_upper_bounds"][component] = "0"
            self.manifest.write_bytes(wire(changed))
            with self.assertRaisesRegex(runner.ProgrammeError, "declared_.*bound_below"):
                self.run_stage()
        self.assertFalse(self.send.calls)

    def test_post_operation_checks_do_not_reapprove_frozen_mixed_stage(self):
        # Same admitted plan, cheap first response, expensive remaining request.
        # Budget/key evidence must still pass; the rolling sample cannot turn an
        # existing request into newly expanded scope within the same stage.
        op = {"operation_id": "next", "model_id": "fake/model", "provider_id": "fake",
              "units_upper_bounds": {"prompt": "2000", "completion": "10", "request": "1"}}
        manifest = {"stage_id": "stage-one"}
        quote = {"model_id": "fake/model", "provider_id": "fake", "currency": "USD",
                 "source_ref": "https://fake.example/prices", "checked_at": datetime.now(timezone.utc).isoformat(),
                 "raw_sha256": "b"*64, "all_charge_components_accounted": True,
                 "component_prices_usd": {"prompt": "0.0001", "completion": "0.0002", "request": "0"}}
        fingerprint = "a"*64
        row = {"operation_id": "previous", "programme_id": self.policy["programme_id"],
               "key_fingerprint_sha256": fingerprint, "state": "completed", "actual_cost_usd": "0.01",
               "expected_cost_usd": "0.001", "is_byok": False, "response_sha256": "c"*64}
        metadata = {"http_status": 200, "key_fingerprint_sha256": fingerprint,
                    "checked_at": datetime.now(timezone.utc).isoformat(), "usage_usd": "0.01", "limit_usd": "5",
                    "remaining_usd": "4.99", "byok_usage_usd": "0", "include_byok_in_limit": False,
                    "is_management_key": False, "limit_reset": None}
        evidence = {**self.evidence, "pricing": [quote]}
        with self.assertRaisesRegex(runner.ProgrammeError, "tenfold"):
            runner.stage_evidence(self.policy, manifest, "d"*64, [op], evidence, metadata, fingerprint, [row])
        result = runner.stage_evidence(self.policy, manifest, "d"*64, [op], evidence, metadata, fingerprint, [row], phase="after_operation")
        self.assertEqual(result[2]["decision"], "within_frozen_admitted_stage")
        metadata["usage_usd"] = "0.02"
        with self.assertRaisesRegex(runner.ProgrammeError, "budget_evidence_gate_blocked"):
            runner.stage_evidence(self.policy, manifest, "d"*64, [op], evidence, metadata, fingerprint, [row], phase="after_operation")

    def test_duplicate_generation_is_rejected_across_distinct_operations(self):
        manifest = json.loads(self.manifest.read_bytes())
        second = dict(manifest["operations"][0])
        second["operation_id"] = "two"
        manifest["operations"].append(second)
        self.manifest.write_bytes(wire(manifest))
        result = self.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(result["completed_operations"], 1)
        self.assertEqual(result["cumulative_actual_usd"], "0.01")
        self.assertEqual(len(self.send.posts()), 2)

    def test_read_only_pending_generation_and_usage_lag_are_bounded(self):
        self.send.generation_pending = 1
        self.send.key_lagging = 1
        result = self.run_stage()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(len(self.send.posts()), 1)
        self.assertEqual(sum(call[1] == "generation" for call in self.send.calls), 2)
        self.assertEqual(sum(call[1] == "key" for call in self.send.calls), 3)
        self.send.calls.clear()
        self.run_stage()  # All immutable read records are included in replay.
        self.assertFalse(self.send.calls)

    def test_unresolved_read_only_usage_lag_stops_after_configured_reads(self):
        self.send.key_lagging = 100
        result = self.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(sum(call[1] == "key" for call in self.send.calls), 4)
        self.assertEqual(len(self.send.posts()), 1)

    def test_stage_identity_cannot_admit_another_manifest_with_new_ids(self):
        self.run_stage()
        manifest = json.loads(self.manifest.read_bytes())
        manifest["operations"][0]["operation_id"] = "new-operation"
        self.manifest.write_bytes(wire(manifest))
        self.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "stage_identity_already_bound"):
            self.run_stage()
        self.assertFalse(self.send.calls)

    def test_cooperative_pause_finishes_accounting_and_resumes_without_duplicate(self):
        self.two_operations()
        def pause_during_post():
            runner.request_pause(self.policy, self.private, self.repo)
        self.send.on_post = pause_during_post
        result = self.run_stage()
        self.assertEqual(result["status"], "paused")
        self.assertEqual(result["completed_operations"], 1)
        self.assertEqual(self.send.calls[-1][1], "key")
        with sqlite3.connect(self.private / "ledger.sqlite3") as db:
            self.assertFalse(db.execute("SELECT * FROM programme_state WHERE name='stop_reason'").fetchall())
        (self.private / self.policy["pause"]["request_filename"]).unlink()
        self.send.on_post = None
        resumed = self.run_stage()
        self.assertEqual(resumed["status"], "completed")
        self.assertEqual(resumed["completed_operations"], 2)
        self.assertEqual(len(self.send.posts()), 2)
        self.assertEqual(resumed["cumulative_actual_usd"], "0.02")

    def test_cooperative_signal_request_only_sets_boundary_flag(self):
        import signal
        self.two_operations()
        original = signal.getsignal(signal.SIGUSR1)
        self.send.on_post = lambda: signal.raise_signal(signal.SIGUSR1)
        result = self.run_stage()
        self.assertEqual(result["status"], "paused")
        self.assertEqual(self.send.calls[-1][1], "key")
        self.assertEqual(signal.getsignal(signal.SIGUSR1), original)
        self.assertEqual(len(self.send.posts()), 1)

    def test_configured_invocation_limit_is_cooperative_and_default_disabled_limit(self):
        self.two_operations()
        self.policy["pause"]["maximum_operations_per_invocation"] = 1
        result = self.run_stage()
        self.assertEqual(result["status"], "paused")
        self.assertEqual(result["completed_operations"], 1)
        self.assertEqual(self.send.calls[-1][1], "key")
        self.policy["pause"]["maximum_operations_per_invocation"] = None
        self.assertEqual(self.run_stage()["status"], "completed")
        self.assertEqual(len(self.send.posts()), 2)

    def test_pause_before_first_operation_does_not_access_provider(self):
        self.private.mkdir(mode=0o700)
        runner.request_pause(self.policy, self.private, self.repo)
        result = self.run_stage()
        self.assertEqual(result["status"], "paused")
        self.assertEqual(result["attempted_operations"], 0)
        self.assertFalse(self.send.calls)

    def test_explicit_verified_lag_never_fakes_observed_usage(self):
        self.policy["provider_usage_lag"] = {"mode": "allow_verified_lower_usage", "authority_ref": "invented-owner-authority"}
        self.send.key_usage_override = Decimal(0)
        result = self.run_stage()
        self.assertEqual(result["status"], "completed")
        reconciliation = result["provider_usage_reconciliation"]
        self.assertEqual(reconciliation["observed_provider_usage_usd"], "0")
        self.assertEqual(reconciliation["verified_cumulative_actual_usd"], "0.01")
        self.assertEqual(reconciliation["effective_available_usd"], "4.99")
        self.assertEqual(reconciliation["strict_gate_status"], "blocked")
        self.assertEqual(reconciliation["strict_gate_blockers"], ["provider_usage_reconciles_with_cumulative_actuals"])

    def test_read_only_stop_resolution_preserves_originals_then_resumes(self):
        self.two_operations()
        self.send.key_usage_override = Decimal(0)
        stopped = self.run_stage()
        self.assertEqual(stopped["status"], "stopped")
        old_receipts = {path: path.read_bytes() for path in self.private.glob("*.actual.json")}
        with sqlite3.connect(self.private / "ledger.sqlite3") as db:
            old_attempt = db.execute("SELECT payload FROM attempts").fetchone()[0]
        self.policy["provider_usage_lag"] = {"mode": "allow_verified_lower_usage", "authority_ref": "invented-owner-authority"}
        self.send.calls.clear()
        result = self.reconcile()
        self.assertEqual(result["status"], "stop_resolved_read_only")
        self.assertEqual(result["paid_calls"], 0)
        self.assertFalse(self.send.posts())
        self.assertTrue(all(method == "GET" for method, *_ in self.send.calls))
        self.assertTrue(all(path.read_bytes() == raw for path, raw in old_receipts.items()))
        with sqlite3.connect(self.private / "ledger.sqlite3") as db:
            self.assertEqual(db.execute("SELECT payload FROM attempts").fetchone()[0], old_attempt)
            self.assertEqual(db.execute("SELECT COUNT(*) FROM resolutions").fetchone()[0], 1)
            self.assertFalse(db.execute("SELECT * FROM programme_state WHERE name='stop_reason'").fetchall())
        resolution = json.loads(next(self.private.glob("*.resolution.json")).read_bytes())
        self.assertEqual(resolution["previous_active_stop_projection"]["reason"], stopped["reason"])
        self.assertTrue(resolution["original_stop_receipts"])
        self.send.calls.clear()
        resumed = self.run_stage()
        self.assertEqual(resumed["status"], "completed")
        self.assertEqual(len(self.send.posts()), 1)
        self.assertEqual(resumed["cumulative_actual_usd"], "0.02")

    def test_stop_resolution_default_strict_or_higher_usage_keeps_stop(self):
        self.send.key_usage_override = Decimal(0)
        self.assertEqual(self.run_stage()["status"], "stopped")
        with self.assertRaisesRegex(runner.ProgrammeError, "budget_evidence_gate_blocked"):
            self.reconcile()
        self.policy["provider_usage_lag"] = {"mode": "allow_verified_lower_usage", "authority_ref": "invented-owner-authority"}
        self.send.key_usage_override = Decimal("0.02")
        self.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "budget_evidence_gate_blocked"):
            self.reconcile()
        self.assertFalse(self.send.posts())
        with sqlite3.connect(self.private / "ledger.sqlite3") as db:
            self.assertTrue(db.execute("SELECT * FROM programme_state WHERE name='stop_reason'").fetchall())
            self.assertEqual(db.execute("SELECT COUNT(*) FROM resolutions").fetchone()[0], 0)

    def test_uncertain_attempt_is_never_adopted_by_read_only_resolution(self):
        self.send.response_override = {"http_status": 400, "raw": b'{"error":"invented"}'}
        self.assertEqual(self.run_stage()["status"], "stopped")
        self.policy["provider_usage_lag"] = {"mode": "allow_verified_lower_usage", "authority_ref": "invented-owner-authority"}
        self.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "unresolved_attempt"):
            self.reconcile()
        self.assertFalse(self.send.calls)


if __name__ == "__main__":
    unittest.main()
