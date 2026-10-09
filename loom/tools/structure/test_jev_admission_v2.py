"""Mechanics plus opt-in byte/provenance tests on actual recovered intentions."""
import copy
from decimal import Decimal
import json
import os
from pathlib import Path
import tempfile
import unittest

try:
    from . import jev_admission_v2 as adapter
except ImportError:
    import jev_admission_v2 as adapter

ROOT = Path(__file__).resolve().parents[3]
PLAN = ROOT / "docs/research/thread7_real_2026-10-09/continuation_01/jev-plan-v2.json"


def policy():
    return json.loads(PLAN.read_text())["policy"]


def body():
    return {"model": adapter.legacy.MODEL, "provider": policy()["provider"],
        "state": {"text": "mechanics-only input"}, "questions": {"q01": {"type": "noul",
        "instructions": "Mechanics only", "criteria": {"true": "present", "false": "absent"}}}}


class AdmissionMechanics(unittest.TestCase):
    def admit(self, value=None, settings=None):
        return adapter.admit(adapter.legacy.safe.canonical(body() if value is None else value),
                             policy() if settings is None else settings)

    def test_valid_stays_unexecuted_and_unknown(self):
        row = self.admit()
        self.assertEqual(row["local_admission"], "admitted")
        self.assertEqual(row["execution_status"], "not_executed")
        self.assertIsNone(row["provider_eligibility"])
        self.assertIsNone(row["campaign_admission"])
        self.assertIsNone(row["reservation_reference"])
        self.assertFalse(row["dispatch_ready"])

    def test_instrument_and_study_are_distinct(self):
        value = body(); value["state"]["text"] = "a" * 40000
        row = self.admit(value)
        self.assertEqual(row["local_admission"], "admitted")
        self.assertEqual(row["historical_profile"]["status"], "rejected")
        self.assertTrue(row["historical_profile"]["byte_limit_exceeded"])
        self.assertTrue(row["historical_profile"]["allowance_exceeded"])

    def test_independent_byte_gate(self):
        settings = policy(); settings["max_body_bytes"] = 100
        row = self.admit(settings=settings)
        self.assertEqual(row["reasons"], ["study_wire_body_byte_policy_exceeded"])

    def test_independent_planning_allowance_gate(self):
        settings = policy(); settings["per_request_planning_allowance_usd"] = "0.000000001"
        row = self.admit(settings=settings)
        self.assertEqual(row["reasons"], ["study_planning_allowance_exceeded"])

    def test_whitespace_counts_for_wire_units(self):
        raw = adapter.legacy.safe.canonical(body()) + b" " * 2000
        row = adapter.admit(raw, policy())
        self.assertEqual(row["wire_body_bytes"] - row["canonical_body_bytes"], 2000)
        self.assertEqual(Decimal(row["historical_planning_upper_usd"]), (len(raw) + 1024) * adapter.legacy.INPUT_CAP)

    def test_no_silent_parameter_drop(self):
        value = body(); value["temperature"] = 0
        before = copy.deepcopy(value)
        row = self.admit(value)
        self.assertEqual(row["local_admission"], "rejected")
        self.assertEqual(row["reasons"], ["invalid_object_fields"])
        self.assertEqual(value, before)

    def test_state_scope_not_renamed(self):
        value = body(); value["state"]["scope"] = "real source scope"
        row = self.admit(value)
        self.assertEqual(row["local_admission"], "rejected")
        self.assertIn("scope", value["state"])

    def test_unsupported_question_type_not_executed(self):
        value = body(); value["questions"]["q01"]["type"] = "open_answer"
        row = self.admit(value)
        self.assertEqual(row["reasons"], ["jev_noul_question_invalid"])
        self.assertEqual(row["execution_status"], "not_executed")

    def test_provider_fallback_rejected(self):
        value = body(); value["provider"]["allow_fallbacks"] = True
        self.assertEqual(self.admit(value)["reasons"], ["jev_model_provider_or_price_pin_invalid"])

    def test_malformed_inputs_fixed_safe_codes(self):
        for raw in (b"not JSON private text", b'{"state":1,"state":2}', b"null", b'{"x":NaN}'):
            with self.subTest(raw=raw[:4]):
                row = adapter.admit(raw, policy())
                self.assertEqual(row["local_admission"], "rejected")
                self.assertNotIn("private text", json.dumps(row))

    def test_negative_or_nonfinite_policy_fails(self):
        for value in ("NaN", "Infinity", "-1", True, 0):
            settings = policy(); settings["per_request_planning_allowance_usd"] = value
            with self.assertRaises(adapter.Error):
                self.admit(settings=settings)

    def test_invalid_policy_integer_fails(self):
        for value in (True, -1, 1.5):
            settings = policy(); settings["max_body_bytes"] = value
            with self.assertRaises(adapter.Error):
                self.admit(settings=settings)

    def test_historical_tariff_cannot_claim_fresh(self):
        settings = policy(); settings["price_status"] = "current"
        with self.assertRaisesRegex(adapter.Error, "price_status_invalid"):
            self.admit(settings=settings)

    def test_question_count_and_empty_text(self):
        value = body(); value["questions"] = {}
        self.assertEqual(self.admit(value)["reasons"], ["jev_questions_invalid"])
        value = body(); value["state"]["text"] = ""
        self.assertEqual(self.admit(value)["reasons"], ["jev_text_required"])


@unittest.skipUnless(os.environ.get("THREAD7_JEV_PRIOR"), "private real intentions supplied explicitly")
class RecoveredRealPreparation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.prior = Path(os.environ["THREAD7_JEV_PRIOR"])
        cls.plan = json.loads(PLAN.read_text())
        cls.temporary = tempfile.TemporaryDirectory(prefix="thread7-jev-test-")
        cls.output = Path(cls.temporary.name) / "new"
        cls.receipt = adapter.prepare(cls.prior, cls.output, cls.plan)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_all16_have_explicit_admission(self):
        rows = json.loads((self.output / "ADMISSIONS.json").read_text())
        self.assertEqual(len(rows), 16)
        self.assertTrue(all(row["local_admission"] == "admitted" and row["reasons"] for row in rows))
        self.assertTrue(all(row["provider_eligibility"] is None and row["campaign_admission"] is None for row in rows))

    def test_legacy_exactly4_of16(self):
        self.assertEqual(self.receipt["historically_admitted"], 4)
        self.assertEqual(self.receipt["locally_admitted"], 16)

    def test_raw_and_all_parameter_trees_unchanged(self):
        operations = adapter.manifests.load_operations(self.output / "payer/manifest.json")
        self.assertEqual(len(operations), 16)
        for op in operations:
            stem = op["operation_id"].split(".")[-1]
            source = self.prior / "desired-requests" / op["metadata"]["arm_id"] / (stem + ".json")
            self.assertEqual(source.read_bytes(), op["request_bytes"])
            self.assertEqual(json.loads(source.read_bytes()), op["request_body"])
            self.assertEqual(op["units_upper_bounds"]["prompt"], len(op["request_bytes"]) + 1024)

    def test_no_reservation_or_inference(self):
        self.assertEqual(self.receipt["planned_minimum_reservations_usd"], "0.048")
        self.assertFalse(self.receipt["reservation_actually_booked"])
        self.assertEqual(self.receipt["new_paid_calls"], 0)
        self.assertEqual(self.receipt["new_paid_cost_usd"], "0")

    def test_frozen_output_has_all_hashes(self):
        freeze = json.loads((self.output / "FREEZE.json").read_text())
        for name, expected in freeze["files"].items():
            self.assertEqual(adapter.sha((self.output / name).read_bytes()), expected)

    def test_cannot_overwrite_previous_or_output(self):
        with self.assertRaisesRegex(adapter.Error, "new_private_output_required"):
            adapter.prepare(self.prior, self.output, self.plan)

    def test_prior_integrity_required(self):
        changed = copy.deepcopy(self.plan); changed["prior_freeze_sha256"] = "0" * 64
        with self.assertRaisesRegex(adapter.Error, "prior_freeze_sha256_mismatch"):
            adapter.prepare(self.prior, Path(self.temporary.name) / "wrong", changed)

    def test_existing_campaign_identity_cannot_change(self):
        changed = copy.deepcopy(self.plan); changed["programme_id"] = "unrelated-campaign"
        with self.assertRaisesRegex(adapter.Error, "campaign_programme_identity_mismatch"):
            adapter.prepare(self.prior, Path(self.temporary.name) / "wrong-campaign", changed)

    def test_historical_validator_version_cannot_drift(self):
        changed = copy.deepcopy(self.plan); changed["legacy_validator_sha256"] = "0" * 64
        with self.assertRaisesRegex(adapter.Error, "legacy_validator_version_mismatch"):
            adapter.prepare(self.prior, Path(self.temporary.name) / "wrong-validator", changed)

    def test_tighter_study_policy_keeps_all_intentions_and_rejections(self):
        changed = copy.deepcopy(self.plan)
        changed["policy"]["max_body_bytes"] = 16384
        changed["policy"]["per_request_planning_allowance_usd"] = "0.001"
        output = Path(self.temporary.name) / "tighter"
        receipt = adapter.prepare(self.prior, output, changed)
        rows = json.loads((output / "ADMISSIONS.json").read_text())
        self.assertEqual(len(rows), 16)
        self.assertEqual(receipt["locally_admitted"], 4)
        self.assertEqual(receipt["locally_rejected"], 12)
        self.assertEqual(len(list((output / "intentions").rglob("*.json"))), 16)
        self.assertTrue(all(row["reasons"] and row["execution_status"] == "not_executed" for row in rows))


if __name__ == "__main__":
    unittest.main()
