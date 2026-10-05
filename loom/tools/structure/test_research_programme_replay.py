"""Adversarial completed-ledger replay, using only invented offline receipts."""
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

import research_programme_runner as runner
import research_programme_transport as transport


class CompletedReceiptReplayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.directory = self.root / "private"
        request_raw = b'{"model":"offline/tiny","provider":{"only":["offline"],"allow_fallbacks":false}}'
        self.operation = {"operation_id": "offline-one", "stage_id": "offline-stage",
            "request_sha256": runner.sha(request_raw), "request_bytes": request_raw,
            "model_id": "offline/tiny", "provider_id": "offline",
            "provider_aliases": ["Offline"], "model_aliases": ["offline/tiny"],
            "route_id": "chat", "api_type": "completions"}
        self.ledger = runner.PrivateLedger(self.directory, self.repo, "offline-programme", "a" * 64)
        self.token = runner.sha(self.operation["operation_id"].encode())
        self.first = {"id": "gen-offline", "model": "offline/tiny", "provider": "Offline",
                      "usage": {"cost": "0.01", "is_byok": False, "prompt_tokens": 2, "completion_tokens": 1}}
        self.generation = {"data": {"id": "gen-offline", "model": "offline/tiny", "provider_name": "Offline",
                                   "api_type": "completions", "total_cost": "0.01", "is_byok": False}}
        with self.ledger.locked():
            row = self.ledger.reserve(self.operation, "c" * 64, "0.1", "0.01")
            first_raw, generation_raw = runner.canonical(self.first), runner.canonical(self.generation)
            runner.write_private(self.path("response.bin"), first_raw)
            runner.write_private(self.path("generation.bin"), generation_raw)
            parsed = transport.extract_receipt({"http_status": 200, "raw": first_raw}, self.operation)
            verified = transport.verify_generation_receipt({"http_status": 200, "raw": generation_raw}, parsed, self.operation)
            row.update(verified, http_status=200, latency_seconds=0.1, state="completed",
                       generation_sha256=runner.sha(generation_raw))
            self.ledger.finish(row)

    def path(self, suffix):
        return self.directory / "records" / (self.token + "." + suffix)

    def edit_row(self, mutate):
        # Simulate matching database/result corruption, leaving original
        # started and provider byte artifacts available for reconstruction.
        with sqlite3.connect(self.directory / "ledger.sqlite3") as db:
            row = json.loads(db.execute("SELECT payload FROM attempts").fetchone()[0])
            mutate(row)
            encoded = runner.canonical(row)
            db.execute("UPDATE attempts SET payload=?", (encoded.decode(),))
            self.path("result.json").write_bytes(encoded)

    def assert_replay_rejected(self, reason="billing_proof"):
        with self.assertRaisesRegex(runner.ProgrammeError, reason):
            with self.ledger.locked():
                pass

    def test_valid_completed_receipt_reconstructs_exact_cost_and_identity(self):
        with self.ledger.locked():
            self.assertEqual(self.ledger.rows()[0]["actual_cost_usd"], "0.01")

    def test_completed_marker_without_generation_or_credit_proof_is_rejected(self):
        self.edit_row(lambda row: row.pop("generation_sha256"))
        self.assert_replay_rejected("completed_billing_proof_missing")

    def test_matching_terminal_cost_corruption_does_not_replace_original_usage(self):
        self.edit_row(lambda row: row.update(actual_cost_usd="0", reported_cost_usd="0",
                                             generation_total_cost_usd="0"))
        self.assert_replay_rejected("completed_billing_proof_replay_mismatch")

    def test_original_generation_pair_is_bound_before_dispatch(self):
        def edit(row):
            row["receipt_operation"]["provider_aliases"] = ["Other"]
        self.edit_row(edit)
        self.assert_replay_rejected("durable_started_record_mismatch")

    def test_frozen_request_tamper_is_rejected(self):
        self.path("request.bin").write_bytes(b"modified request")
        self.assert_replay_rejected("frozen_request_missing_or_changed")

    def test_stored_byok_bytes_cannot_become_credit_proof(self):
        self.first["usage"]["is_byok"] = True
        self.generation["data"]["is_byok"] = True
        first_raw, gen_raw = runner.canonical(self.first), runner.canonical(self.generation)
        self.path("response.bin").write_bytes(first_raw)
        self.path("generation.bin").write_bytes(gen_raw)
        self.edit_row(lambda row: row.update(response_sha256=runner.sha(first_raw),
            generation_sha256=runner.sha(gen_raw), generation_response_sha256=runner.sha(gen_raw)))
        self.assert_replay_rejected("completed_billing_proof_replay_mismatch")

    def test_missing_credit_flag_is_not_inferred_from_zero_byok(self):
        self.edit_row(lambda row: row.pop("is_byok"))
        self.assert_replay_rejected("completed_billing_proof_missing")

    def test_matching_raw_cost_above_original_reservation_is_rejected(self):
        self.first["usage"]["cost"] = "0.9"
        self.generation["data"]["total_cost"] = "0.9"
        first_raw, gen_raw = runner.canonical(self.first), runner.canonical(self.generation)
        self.path("response.bin").write_bytes(first_raw)
        self.path("generation.bin").write_bytes(gen_raw)
        rebuilt = transport.verify_generation_receipt({"http_status": 200, "raw": gen_raw},
            transport.extract_receipt({"http_status": 200, "raw": first_raw}, self.operation), self.operation)
        self.edit_row(lambda row: row.update(rebuilt, generation_sha256=runner.sha(gen_raw)))
        self.assert_replay_rejected("completed_billing_proof_replay_mismatch")


if __name__ == "__main__":
    unittest.main()
