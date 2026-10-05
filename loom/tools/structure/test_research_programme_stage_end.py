"""Stage-end billing timing with fabricated keys and exact offline transport."""
from decimal import Decimal
import hashlib
import json
import sqlite3
import unittest

import research_programme_runner as runner
import test_research_programme_runner as fixtures

wire = fixtures.wire


class ProgrammeStageEndTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.ProgrammeRunnerTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.policy.update(billing_verification_timing="stage_end", unknown_cost_policy="reserve", require_credit_proof_per_pair=False)
        self.fixture.two_operations()

    def originals(self):
        with sqlite3.connect(self.fixture.private / "ledger.sqlite3") as db:
            return [json.loads(row[0]) for row in db.execute("SELECT payload FROM attempts ORDER BY rowid")]

    def proof_count(self):
        with sqlite3.connect(self.fixture.private / "ledger.sqlite3") as db:
            return db.execute("SELECT COUNT(*) FROM attempt_resolutions").fetchone()[0]

    def test_all_posts_precede_generation_gets_and_originals_remain_pending(self):
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["completed_operations"], 2)
        self.assertEqual(result["pending_billing_operations"], 0)
        self.assertEqual(result["cumulative_actual_usd"], "0.02")
        self.assertEqual(result["unresolved_reservations_usd"], "0")
        calls = self.fixture.send.calls
        self.assertLess(max(index for index, call in enumerate(calls) if call[0] == "POST"),
                        min(index for index, call in enumerate(calls) if call[1] == "generation"))
        for row in self.originals():
            self.assertEqual(row["state"], "pending_billing")
            self.assertIsNone(row["actual_cost_usd"])
            self.assertFalse(row["billing_verified"])
            self.assertEqual(row["reservation_usd"], "0.0220")
            self.assertNotIn("generation_sha256", row)
            token = runner.sha(row["operation_id"].encode())
            self.assertEqual(json.loads((self.fixture.private / "records" / (token + ".result.json")).read_bytes()), row)
        self.assertEqual(self.proof_count(), 2)
        self.fixture.send.calls.clear()
        self.assertEqual(self.fixture.run_stage()["status"], "already_completed_saved_receipt")
        self.assertEqual(self.fixture.send.calls, [])

    def test_pair_bootstrap_requires_real_credit_proof_before_next_post(self):
        self.fixture.policy["require_credit_proof_per_pair"] = True
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "completed")
        routes = [call[1] for call in self.fixture.send.calls]
        self.assertLess(routes.index("generation"), routes.index("chat", routes.index("chat") + 1))
        rows = self.originals()
        self.assertEqual(rows[0]["state"], "completed")
        self.assertTrue(rows[0]["billing_verified"])
        self.assertEqual(rows[1]["state"], "pending_billing")
        self.assertEqual(self.proof_count(), 1)

    def test_new_pair_pending_bootstrap_never_posts_second(self):
        self.fixture.policy["require_credit_proof_per_pair"] = True
        self.fixture.send.generation_pending = 100
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(len(self.fixture.send.posts()), 1)
        self.assertEqual(self.originals()[0]["state"], "uncertain")
        self.assertIsNone(self.originals()[0]["actual_cost_usd"])

    def test_existing_pair_credit_proof_allows_fast_pending_same_pair(self):
        # A strict, already verified prefix is the bootstrap witness.
        self.fixture.policy["billing_verification_timing"] = "per_operation"
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = 1
        self.assertEqual(self.fixture.run_stage()["status"], "paused")
        self.fixture.policy["billing_verification_timing"] = "stage_end"
        self.fixture.policy["require_credit_proof_per_pair"] = True
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = None
        self.fixture.send.calls.clear()
        self.assertEqual(self.fixture.run_stage()["status"], "completed")
        self.assertEqual(len(self.fixture.send.posts()), 1)
        self.assertEqual(self.originals()[1]["state"], "pending_billing")

    def test_pause_keeps_full_reservation_and_resume_skips_bound_first_capture(self):
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = 1
        self.assertEqual(self.fixture.run_stage()["status"], "paused")
        original = self.originals()[0]
        token = runner.sha(original["operation_id"].encode())
        raw = (self.fixture.private / "records" / (token + ".response.bin")).read_bytes()
        self.assertIsNone(original["actual_cost_usd"])
        self.assertEqual(self.fixture.send.post_count, 1)
        self.assertFalse(any(call[1] == "generation" for call in self.fixture.send.calls))
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = None
        self.fixture.send.calls.clear()
        self.assertEqual(self.fixture.run_stage()["status"], "completed")
        self.assertEqual(len(self.fixture.send.posts()), 1)
        self.assertEqual((self.fixture.private / "records" / (token + ".response.bin")).read_bytes(), raw)
        self.assertEqual(self.originals()[0], original)

    def test_interior_usage_is_bounded_and_preserved_not_normalized_to_cost(self):
        base = self.fixture.send
        generation_reads = 0

        def send(method, route, body=None, params=None):
            nonlocal generation_reads
            response = base(method, route, body, params)
            if route == "generation":
                generation_reads += 1
            if route == "key" and base.usage and generation_reads < 2:
                data = json.loads(response["raw"])
                data["data"].update(usage=str(base.usage * Decimal("1.5")), limit_remaining=str(5-base.usage*Decimal("1.5")))
                response["raw"] = wire(data)
            return response

        self.fixture.send = send
        self.assertEqual(self.fixture.run_stage()["status"], "completed")
        witnesses = [json.loads(path.read_bytes())["readiness"] for path in self.fixture.private.glob("*.pending-accounting-*.json")]
        self.assertEqual(len(witnesses), 2)
        witness = next(item for item in witnesses if item["provider_usage_reconciliation"]["observed_provider_usage_usd"] == "0.015")
        self.assertEqual(witness["status"], "ready_with_bound_pending_reservations")
        self.assertFalse(witness["checks"]["provider_usage_reconciles_with_cumulative_actuals"])
        self.assertEqual(witness["provider_usage_reconciliation"]["verified_cumulative_actual_usd"], "0")
        self.assertEqual(witness["provider_usage_reconciliation"]["usage_interval_upper_usd"], "0.0220")

    def test_explicit_unknown_cost_stop_is_not_overridden_by_stage_end(self):
        self.fixture.policy["unknown_cost_policy"] = "stop"
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(len(self.fixture.send.posts()), 1)
        self.assertEqual(self.originals()[0]["state"], "pending_billing")
        self.assertEqual(result["unresolved_reservations_usd"], "0.0220")

    def test_higher_usage_or_full_reservations_exceeding_cap_stop_next_post(self):
        self.fixture.send.on_post = lambda: setattr(self.fixture.send, "key_usage_override", Decimal("0.023"))
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(len(self.fixture.send.posts()), 1)

    def test_full_reservations_must_fit_even_when_reported_costs_would_fit(self):
        self.fixture.policy["usd_cap"] = "0.03"
        base = self.fixture.send

        def send(method, route, body=None, params=None):
            response = base(method, route, body, params)
            if route == "key":
                data = json.loads(response["raw"])
                data["data"].update(limit="0.03", limit_remaining="0.03")
                response["raw"] = wire(data)
            return response

        self.fixture.send = send
        with self.assertRaisesRegex(runner.ProgrammeError, "stage_fits_cumulative_budget"):
            self.fixture.run_stage()
        self.assertEqual(base.posts(), [])

    def test_invalid_first_pair_identity_credit_cost_or_partial_never_continues(self):
        cases = [
            {"model": "wrong/model"}, {"provider": "Wrong"}, {"id": None}, {"id": 9},
            {"usage": {"cost": None}}, {"usage": {"cost": "100"}}, {"usage": {"cost": "NaN"}},
            {"usage": {"cost": "0.01", "is_byok": True}},
        ]
        for index, update in enumerate(cases):
            with self.subTest(update=update):
                self.fixture.private = self.fixture.root / ("case-" + str(index))
                self.fixture.send.calls.clear()
                self.fixture.send.usage = Decimal(0)
                self.fixture.send.post_count = 0
                receipt = {"id": "gen-invalid", "model": "fake/model", "provider": "Fake", "usage": {"cost": "0.01", "is_byok": False}}
                receipt.update(update)
                self.fixture.send.response_override = {"raw": wire(receipt)}
                result = self.fixture.run_stage()
                self.assertEqual(result["status"], "stopped")
                self.assertEqual(len(self.fixture.send.posts()), 1)
                self.assertEqual(self.originals()[0]["state"], "uncertain")
                self.assertIsNone(self.originals()[0]["actual_cost_usd"])

    def test_partial_or_http_error_first_response_never_continues(self):
        for index, response in enumerate(({"http_status": 400}, {"transport_error": "partial_read"})):
            with self.subTest(response=response):
                self.fixture.private = self.fixture.root / ("http-case-" + str(index))
                self.fixture.send.calls.clear()
                self.fixture.send.usage = Decimal(0)
                self.fixture.send.post_count = 0
                self.fixture.send.response_override = response
                self.assertEqual(self.fixture.run_stage()["status"], "stopped")
                self.assertEqual(len(self.fixture.send.posts()), 1)
                self.assertEqual(self.originals()[0]["state"], "uncertain")

    def test_duplicate_first_generation_keeps_second_uncertain_and_blocks_resume(self):
        self.fixture.send.distinct_generations = False
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual([row["state"] for row in self.originals()], ["pending_billing", "uncertain"])
        self.fixture.send.calls.clear()
        with self.assertRaises(runner.ProgrammeError):
            self.fixture.run_stage()
        self.assertEqual(self.fixture.send.calls, [])

    def test_delayed_final_generation_retains_stop_then_get_only_resolves(self):
        self.fixture.send.generation_pending = 100
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(result["reason"], "stage_end_billing_not_yet_verified")
        self.assertEqual(result["pending_billing_operations"], 2)
        self.assertEqual(result["unresolved_reservations_usd"], "0.0440")
        originals = self.originals()
        self.fixture.send.generation_pending = 0
        self.fixture.send.calls.clear()
        resolved = runner.reconcile_stop(self.fixture.policy, self.fixture.manifest, self.fixture.evidence,
            self.fixture.private, self.fixture.keyfile, self.fixture.repo, transport_fn=self.fixture.send, captured_pending=True)
        self.assertEqual(resolved["status"], "stop_resolved_read_only")
        self.assertEqual(self.fixture.send.posts(), [])
        self.assertEqual(self.originals(), originals)
        self.fixture.send.calls.clear()
        self.assertEqual(self.fixture.run_stage()["status"], "already_completed_saved_receipt")
        self.assertEqual(self.fixture.send.calls, [])

    def test_generation_contradiction_is_durable_and_not_retried(self):
        self.fixture.send.generation_override = {"raw": wire({"data": {"id": "gen-fake-1", "model": "fake/model", "provider_name": "Fake",
            "api_type": "completions", "total_cost": "0.02", "is_byok": False}})}
        self.assertEqual(self.fixture.run_stage()["status"], "stopped")
        self.fixture.send.generation_override = None
        self.fixture.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "durable_billing_verification_contradiction"):
            runner.reconcile_stop(self.fixture.policy, self.fixture.manifest, self.fixture.evidence, self.fixture.private,
                self.fixture.keyfile, self.fixture.repo, transport_fn=self.fixture.send, captured_pending=True)
        self.assertEqual(self.fixture.send.calls, [])

    def test_deleting_failure_projection_cannot_forget_retained_contradictory_read(self):
        self.fixture.policy["require_credit_proof_per_pair"] = True
        base = self.fixture.send
        generation_reads = 0

        def send(method, route, body=None, params=None):
            nonlocal generation_reads
            response = base(method, route, body, params)
            if route == "generation":
                generation_reads += 1
                if generation_reads == 2:
                    data = json.loads(response["raw"])
                    data["data"]["model"] = "wrong/model"
                    response["raw"] = wire(data)
            return response

        self.fixture.send = send
        self.assertEqual(self.fixture.run_stage()["status"], "stopped")
        with sqlite3.connect(self.fixture.private / "ledger.sqlite3") as db:
            db.execute("DELETE FROM verification_failures")
        for path in self.fixture.private.glob("*.billing-failure.json"):
            path.unlink()
        base.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "durable_billing_verification_contradiction"):
            runner.reconcile_stop(self.fixture.policy, self.fixture.manifest, self.fixture.evidence, self.fixture.private,
                self.fixture.keyfile, self.fixture.repo, transport_fn=base, captured_pending=True)
        self.assertEqual(base.calls, [])

    def test_bootstrap_lower_usage_cannot_authorize_next_post_even_with_lag_optin(self):
        self.fixture.policy["require_credit_proof_per_pair"] = True
        self.fixture.policy["provider_usage_lag"] = {"mode": "allow_verified_lower_usage", "authority_ref": "fixture-owner"}
        self.fixture.send.key_usage_override = Decimal(0)
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(len(self.fixture.send.posts()), 1)
        self.assertEqual(result["cumulative_actual_usd"], "0.01")

    def test_all_proven_final_no_dispatch_gate_retains_explicit_lower_lag_option(self):
        self.fixture.policy["provider_usage_lag"] = {"mode": "allow_verified_lower_usage", "authority_ref": "fixture-owner"}
        self.fixture.send.key_usage_override = Decimal(0)
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["cumulative_actual_usd"], "0.02")
        witness = result["provider_usage_reconciliation"]
        self.assertEqual(witness["observed_provider_usage_usd"], "0")
        self.assertEqual(witness["strict_gate_status"], "blocked")
        self.assertEqual(witness["verified_cumulative_actual_usd"], "0.02")

    def test_http200_contradiction_cannot_be_reclassified_as_pending_by_policy(self):
        self.fixture.policy["read_only_reconciliation"]["generation"]["pending_http_statuses"] = [200]
        self.fixture.send.generation_override = {"raw": wire({"data": {"id": "gen-fake-1", "model": "wrong/model", "provider_name": "Fake",
            "api_type": "completions", "total_cost": "0.01", "is_byok": False}})}
        self.assertEqual(self.fixture.run_stage()["status"], "stopped")
        self.assertEqual(sum(call[1] == "generation" for call in self.fixture.send.calls), 1)
        self.fixture.send.generation_override = None
        self.fixture.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "durable_billing_verification_contradiction"):
            runner.reconcile_stop(self.fixture.policy, self.fixture.manifest, self.fixture.evidence, self.fixture.private,
                self.fixture.keyfile, self.fixture.repo, transport_fn=self.fixture.send, captured_pending=True)
        self.assertEqual(self.fixture.send.calls, [])

    def test_unreferenced_late_raw_after_crash_is_never_adopted(self):
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = 1
        self.fixture.run_stage()
        orphan = self.fixture.private / ("f" * 32 + ".captured-generation-read-0.bin")
        orphan.write_bytes(b'{"error":"metadata capture interrupted"}')
        orphan.chmod(0o600)
        self.fixture.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "orphan_captured_generation_read_evidence"):
            self.fixture.run_stage()
        self.assertEqual(self.fixture.send.calls, [])

    def test_surviving_paused_receipt_blocks_deleted_attempt_and_all_its_records(self):
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = 1
        self.assertEqual(self.fixture.run_stage()["status"], "paused")
        original = self.originals()[0]
        token = runner.sha(original["operation_id"].encode())
        with sqlite3.connect(self.fixture.private / "ledger.sqlite3") as db:
            db.execute("DELETE FROM attempts WHERE operation_id=?", (original["operation_id"],))
        for path in (self.fixture.private / "records").glob(token + ".*"):
            path.unlink()
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = None
        self.fixture.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "saved_actual_attempt_history_missing"):
            self.fixture.run_stage()
        self.assertEqual(self.fixture.send.calls, [])

    def test_another_stage_or_strict_timing_cannot_spend_with_pending_prefix(self):
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = 1
        self.fixture.run_stage()
        original_manifest = self.fixture.manifest.read_bytes()
        data = json.loads(original_manifest)
        data["stage_id"] = "other-stage"
        data["operations"] = [{**data["operations"][0], "operation_id": "other"}]
        self.fixture.manifest.write_bytes(wire(data))
        self.fixture.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "prior_stage_pending_billing"):
            self.fixture.run_stage()
        self.assertEqual(self.fixture.send.calls, [])
        self.fixture.manifest.write_bytes(original_manifest)
        self.fixture.policy["billing_verification_timing"] = "per_operation"
        with self.assertRaises(runner.ProgrammeError):
            self.fixture.run_stage()
        self.assertEqual(self.fixture.send.calls, [])

    def test_provider_limit_change_blocks_resume_before_next_post(self):
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = 1
        self.fixture.run_stage()
        self.fixture.policy["pause"]["maximum_operations_per_invocation"] = None
        base = self.fixture.send

        def send(method, route, body=None, params=None):
            response = base(method, route, body, params)
            if route == "key":
                data = json.loads(response["raw"])
                data["data"].update(limit="4", limit_remaining="3.99")
                response["raw"] = wire(data)
            return response

        self.fixture.send = send
        base.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "pending_billing_key_limit_binding_changed"):
            self.fixture.run_stage()
        self.assertEqual(base.posts(), [])

    def test_crash_after_reserve_blocks_all_replay_network(self):
        def crash():
            raise SystemExit("fixture crash")
        self.fixture.send.on_post = crash
        with self.assertRaises(SystemExit):
            self.fixture.run_stage()
        self.fixture.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "ambiguous_attempt_no_adoption_or_retry"):
            self.fixture.run_stage()
        self.assertEqual(self.fixture.send.calls, [])

    def test_unknown_timing_and_missing_stage_end_policy_data_fail_before_network(self):
        for value in ("unknown", None):
            self.fixture.policy["billing_verification_timing"] = value
            with self.assertRaises(runner.ProgrammeError):
                self.fixture.run_stage()
            self.assertEqual(self.fixture.send.calls, [])
        self.fixture.policy["billing_verification_timing"] = "stage_end"
        del self.fixture.policy["pending_key_metadata_binding_fields"]
        with self.assertRaisesRegex(runner.ProgrammeError, "stage_end_policy_data_missing_or_invalid"):
            self.fixture.run_stage()
        self.assertEqual(self.fixture.send.calls, [])


if __name__ == "__main__":
    unittest.main()
