"""Late generation proof tests use fictional credentials and no provider."""
from decimal import Decimal
import json
import sqlite3
import unittest

import research_programme_runner as runner
import research_programme_transport as transport
import test_research_programme_runner as fixtures


class CapturedResolutionTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.ProgrammeRunnerTests("test_exact_bytes_durable_reserve_receipts_and_usage")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)

    def pending(self, *, two=True):
        f = self.fixture
        if two:
            f.two_operations()
        f.send.generation_pending = 100
        stopped = f.run_stage()
        self.assertEqual(stopped["status"], "stopped")
        f.send.generation_pending = 0
        f.send.calls.clear()
        return stopped

    def reconcile(self):
        f = self.fixture
        return runner.reconcile_stop(f.policy, f.manifest, f.evidence, f.private, f.keyfile, f.repo,
                                     transport_fn=f.send, captured_pending=True)

    def test_bound_pending_response_resolves_append_only_and_resume_skips_post(self):
        self.pending()
        f = self.fixture
        originals = {path: path.read_bytes() for path in (f.private / "records").iterdir()}
        with sqlite3.connect(f.private / "ledger.sqlite3") as db:
            payload = db.execute("SELECT payload FROM attempts").fetchone()[0]
        result = self.reconcile()
        self.assertEqual(result["status"], "stop_resolved_read_only")
        self.assertEqual(result["paid_calls"], 0)
        self.assertFalse(f.send.posts())
        self.assertTrue(all(path.read_bytes() == raw for path, raw in originals.items()))
        with sqlite3.connect(f.private / "ledger.sqlite3") as db:
            self.assertEqual(db.execute("SELECT payload FROM attempts").fetchone()[0], payload)
            self.assertEqual(json.loads(payload)["state"], "uncertain")
            proof = json.loads(db.execute("SELECT payload FROM attempt_resolutions").fetchone()[0])
        self.assertEqual(proof["projection"]["state"], "completed")
        self.assertEqual(proof["projection"]["actual_cost_usd"], "0.01")
        self.assertEqual(proof["projection"]["original_state"], "uncertain")
        f.send.calls.clear()
        resumed = f.run_stage()
        self.assertEqual(resumed["status"], "completed")
        self.assertEqual(resumed["completed_operations"], 2)
        self.assertEqual(resumed["cumulative_actual_usd"], "0.02")
        self.assertEqual(len(f.send.posts()), 1)

    def test_normal_reconcile_never_promotes_pending_or_uncaptured_attempt(self):
        self.pending()
        f = self.fixture
        with self.assertRaisesRegex(runner.ProgrammeError, "unresolved_attempt"):
            f.reconcile()
        self.assertFalse(f.send.calls)

    def test_still_pending_or_wrong_late_identity_cost_or_byok_keeps_stop(self):
        self.pending(two=False)
        f = self.fixture
        for field, value in (("id", "other-generation"), ("model", "other/model"),
                             ("provider_name", "Other"), ("api_type", "decisions"),
                             ("total_cost", "0.02"), ("is_byok", True)):
            data = {"id": "gen-fake", "model": "fake/model", "provider_name": "Fake",
                    "api_type": "completions", "total_cost": "0.01", "is_byok": False}
            data[field] = value
            f.send.generation_override = {"raw": fixtures.wire({"data": data})}
            with self.assertRaises(transport.TransportError):
                self.reconcile()
            with sqlite3.connect(f.private / "ledger.sqlite3") as db:
                self.assertTrue(db.execute("SELECT * FROM programme_state WHERE name='stop_reason'").fetchall())
                self.assertEqual(db.execute("SELECT COUNT(*) FROM attempt_resolutions").fetchone()[0], 0)
        f.send.generation_override = None
        f.send.generation_pending = 100
        with self.assertRaises(transport.TransportError):
            self.reconcile()
        self.assertFalse(f.send.posts())

    def test_uncaptured_http_error_rejected_before_any_get(self):
        f = self.fixture
        f.send.response_override = {"http_status": 400, "raw": b'{"error":"invented"}'}
        self.assertEqual(f.run_stage()["status"], "stopped")
        f.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "unresolved_attempt"):
            self.reconcile()
        self.assertFalse(f.send.calls)

    def test_nonpending_generation_failure_rejected_before_any_get(self):
        f = self.fixture
        f.send.generation_override = {"http_status": 400, "raw": b'{"error":"bad request"}'}
        self.assertEqual(f.run_stage()["status"], "stopped")
        f.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "unresolved_attempt"):
            self.reconcile()
        self.assertFalse(f.send.calls)

    def test_late_proof_keeps_stop_if_fresh_usage_is_higher(self):
        self.pending()
        f = self.fixture
        f.policy["provider_usage_lag"] = {"mode": "allow_verified_lower_usage", "authority_ref": "fictional-authority"}
        f.send.key_usage_override = Decimal("0.02")
        with self.assertRaisesRegex(runner.ProgrammeError, "budget_evidence_gate_blocked"):
            self.reconcile()
        self.assertFalse(f.send.posts())
        with sqlite3.connect(f.private / "ledger.sqlite3") as db:
            self.assertEqual(db.execute("SELECT COUNT(*) FROM attempt_resolutions").fetchone()[0], 1)
            self.assertTrue(db.execute("SELECT * FROM programme_state WHERE name='stop_reason'").fetchall())

    def test_missing_or_tampered_late_proof_blocks_reopen_before_network(self):
        self.pending()
        f = self.fixture
        self.reconcile()
        proof = next(f.private.glob("*.attempt-resolution.json"))
        proof.unlink()
        f.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "resolution_evidence_missing_or_changed"):
            f.run_stage()
        self.assertFalse(f.send.calls)

    def test_missing_general_resolution_or_original_stop_blocks_reopen(self):
        self.pending()
        f = self.fixture
        self.reconcile()
        source = json.loads(next(f.private.glob("*.resolution.json")).read_bytes())
        (f.private / source["original_stop_receipts"][0]["file"]).unlink()
        f.send.calls.clear()
        with self.assertRaisesRegex(runner.ProgrammeError, "resolution_evidence_missing_or_changed"):
            f.run_stage()
        self.assertFalse(f.send.calls)

    def test_pending_duplicate_generation_identity_rejected_before_get(self):
        f = self.fixture
        f.two_operations()
        f.send.on_post = lambda: setattr(f.send, "generation_pending", 100) if f.send.post_count == 2 else None
        self.assertEqual(f.run_stage()["status"], "stopped")
        # Both original first responses remain immutable; duplicate identity
        # would already have stopped before pending reads in the actual runner.
        f.send.calls.clear()
        with sqlite3.connect(f.private / "ledger.sqlite3") as db:
            rows = [json.loads(row[0]) for row in db.execute("SELECT payload FROM attempts")]
            rows[1]["generation_id"] = rows[0]["generation_id"]
            db.execute("UPDATE attempts SET payload=? WHERE operation_id=?", (runner.canonical(rows[1]).decode(), rows[1]["operation_id"]))
        with self.assertRaises(runner.ProgrammeError):
            self.reconcile()
        self.assertFalse(f.send.calls)
