"""Preparation gates use invented prices, FX and fingerprints; no live calls."""
from copy import deepcopy
from datetime import datetime, timezone
import json
from pathlib import Path
import unittest

from new_budget5eur_gate import evaluate


NOW = datetime(2026, 10, 4, 14, 0, tzinfo=timezone.utc)


def fixture():
    policy_path = Path(__file__).resolve().parents[3] / "docs/research/model_research_2026-10-04/billing/new-programme-5eur.json"
    policy = json.loads(policy_path.read_text())
    policy["usd_cap"] = "5"
    programme = policy["programme_id"]
    fingerprint = "a" * 64
    evidence = {
        "key_binding": {"programme_id": programme, "key_fingerprint_sha256": fingerprint,
                        "bound_to_loaded_credential": True, "separate_new_key_owner_confirmation_ref": "invented-owner-evidence"},
        "key_metadata": {"key_fingerprint_sha256": fingerprint, "checked_at": "2026-10-04T13:59:59Z", "http_status": 200,
                         "usage_usd": "0", "limit_usd": "5", "remaining_usd": "5", "limit_reset": None,
                         "is_management_key": False, "byok_usage_usd": "0", "include_byok_in_limit": False},
        "fx": {"base_currency": "EUR", "quote_currency": "USD", "checked_at": "2026-10-04T13:59:59Z",
               "source_ref": "invented-fx-source-not-a-real-rate", "usd_per_eur": "1.1"},
        "pricing": [{"model_id": "fake/model", "provider_id": "fake/provider", "currency": "USD",
                     "source_ref": "invented-price-source", "checked_at": "2026-10-04T13:59:59Z", "raw_sha256": "b" * 64,
                     "all_charge_components_accounted": True,
                     "component_prices_usd": {"input_token": "0.00001", "output_token": "0.00002", "request": "0"}}],
        "stage": {"stage_id": "fixture-stage", "manifest_sha256": "c" * 64, "reservation_usd": "0.1",
                  "model_provider_pairs": [{"model_id": "fake/model", "provider_id": "fake/provider",
                                            "units_upper_bounds": {"input_token": "1000", "output_token": "2000", "request": "1"}}]}}
    return policy, evidence, deepcopy(policy["initial_ledger"])


class NewBudget5EURGateTests(unittest.TestCase):
    def test_authorized_fresh_separate_key_and_bound_stage_ready(self):
        p, e, ledger = fixture(); report = evaluate(p, e, ledger, NOW)
        self.assertEqual(report["status"], "ready_for_bound_transport_preflight")
        self.assertEqual(report["accounting"]["declared_price_projection_usd"], "0.05000")
        self.assertEqual(report["paid_calls"], 0)
        self.assertNotIn("a" * 64, json.dumps(report))

    def test_missing_key_is_not_missing_owner_permission(self):
        p, _, ledger = fixture(); report = evaluate(p, {}, ledger, NOW)
        self.assertEqual(report["status"], "blocked")
        self.assertTrue(report["checks"]["owner_authorization_record"])
        self.assertIn("new_private_key_binding", report["blockers"])

    def test_usd_cap_requires_fx_and_does_not_equal_five_eur_automatically(self):
        p, e, ledger = fixture(); e["fx"]["usd_per_eur"] = "0.9"
        self.assertIn("documented_usd_cap_within_eur_authority", evaluate(p, e, ledger, NOW)["blockers"])
        e["fx"]["usd_per_eur"] = None
        self.assertIn("documented_usd_cap_within_eur_authority", evaluate(p, e, ledger, NOW)["blockers"])

    def test_new_key_mismatch_and_reset_or_management_metadata_rejected(self):
        for update in ({"key_fingerprint_sha256": "d" * 64}, {"limit_reset": "monthly"}, {"is_management_key": True}):
            p, e, ledger = fixture(); e["key_metadata"].update(update)
            self.assertEqual(evaluate(p, e, ledger, NOW)["status"], "blocked")

    def test_stage_price_freshness_components_and_reservation_checked(self):
        p, e, ledger = fixture(); e["pricing"][0]["checked_at"] = "2026-09-01T00:00:00Z"
        self.assertIn("fresh_endpoint_price_evidence_for_every_stage_pair", evaluate(p, e, ledger, NOW)["blockers"])
        p, e, ledger = fixture(); e["stage"]["reservation_usd"] = "0.01"
        self.assertIn("stage_reservation_covers_price_projection", evaluate(p, e, ledger, NOW)["blockers"])
        p, e, ledger = fixture(); e["stage"]["model_provider_pairs"][0]["units_upper_bounds"].pop("output_token")
        self.assertIn("fresh_endpoint_price_evidence_for_every_stage_pair", evaluate(p, e, ledger, NOW)["blockers"])

    def test_cumulative_actual_cost_reconciled_after_paid_operation(self):
        p, e, ledger = fixture()
        ledger["attempts"] = [{"operation_id": "one", "programme_id": p["programme_id"],
                               "key_fingerprint_sha256": "a" * 64, "actual_cost_usd": "0.04",
                               "is_byok": False, "response_sha256": "b" * 64}]
        e["key_metadata"].update(usage_usd="0.04", remaining_usd="4.96")
        report = evaluate(p, e, ledger, NOW)
        self.assertEqual(report["accounting"]["cumulative_actual_usd"], "0.04")
        self.assertEqual(report["status"], "ready_for_bound_transport_preflight")
        e["key_metadata"]["usage_usd"] = "0.05"
        self.assertIn("provider_usage_reconciles_with_cumulative_actuals", evaluate(p, e, ledger, NOW)["blockers"])

    def test_unknown_charge_retains_reservation_and_default_stops(self):
        p, e, ledger = fixture()
        ledger["attempts"] = [{"operation_id": "one", "programme_id": p["programme_id"],
                               "key_fingerprint_sha256": "a" * 64, "actual_cost_usd": None,
                               "reservation_usd": "0.4"}]
        report = evaluate(p, e, ledger, NOW)
        self.assertEqual(report["accounting"]["unresolved_reservations_usd"], "0.4")
        self.assertIn("unknown_cost_continuation_policy", report["blockers"])

    def test_duplicate_actuals_and_stage_budget_overflow_rejected(self):
        p, e, ledger = fixture()
        row = {"operation_id": "one", "programme_id": p["programme_id"], "key_fingerprint_sha256": "a" * 64,
               "actual_cost_usd": "4.95", "is_byok": False, "response_sha256": "b" * 64}
        ledger["attempts"] = [row]; e["key_metadata"].update(usage_usd="4.95", remaining_usd="0.05")
        self.assertIn("stage_fits_cumulative_budget", evaluate(p, e, ledger, NOW)["blockers"])
        ledger["attempts"].append(deepcopy(row))
        self.assertIn("cumulative_attempt_receipts_valid", evaluate(p, e, ledger, NOW)["blockers"])


if __name__ == "__main__":
    unittest.main()
