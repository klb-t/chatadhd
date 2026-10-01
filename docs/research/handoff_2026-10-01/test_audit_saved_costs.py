"""Adversarial checks for the accounting instrument, using fabricated records."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

from audit_saved_costs import audit


class SavedCostAuditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Accounting fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.base = self.repo / "docs/research/live/run"
        self.base.mkdir(parents=True)
        self.raw = json.dumps({"model": "fixture/cheap-model", "provider": "Fake",
                               "usage": {"cost": "0.000123"}}).encode()
        self.row = {"id": "first", "request_hash": "a" * 64,
                    "started_at": "2026-09-30T00:00:00Z", "state": "completed",
                    "reservation_usd": "0.01", "reported_cost_usd": "0.000123",
                    "response_sha256": hashlib.sha256(self.raw).hexdigest()}
        self.ledger = {"schema": "loom.openrouter_ledger/1", "experiment_id": "fixture",
                       "attempts": [self.row]}
        (self.base / "first.response.bin").write_bytes(self.raw)
        (self.base / "manifest.json").write_text(json.dumps({"requests": [
            {"id": "first", "body": {"model": "fixture/cheap-model"}}]}))

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], stderr=subprocess.DEVNULL)

    def commit(self):
        (self.base / "ledger.json").write_text(json.dumps(self.ledger))
        self.git("add", ".")
        self.git("commit", "-qm", "fixture")
        return self.git("rev-parse", "HEAD").decode().strip()

    def test_copied_zip_and_branch_do_not_double_bill_repeated_request_does(self):
        first = self.commit()
        repeated = {**self.row, "id": "second", "started_at": "2026-09-30T00:01:00Z"}
        self.ledger["attempts"].append(repeated)
        self.commit()
        with zipfile.ZipFile(self.repo / "docs/research/copy.zip", "w") as archive:
            for path in self.base.iterdir():
                archive.write(path, str(path.relative_to(self.repo)))
        second = self.commit()
        result = audit(str(self.repo), [first, second])
        self.assertEqual(result["summary"]["attempts"], 2)
        self.assertEqual(result["summary"]["known_reported_usd"], "0.000246")
        self.assertEqual(result["summary"]["raw_cost_matches"], 2)

    def test_unknown_is_reserved_and_scripted_large_cost_is_excluded(self):
        self.row.pop("reported_cost_usd")
        scripted = self.repo / "docs/research/scripted/run"
        scripted.mkdir(parents=True)
        (scripted / "ledger.json").write_text(json.dumps({**self.ledger, "attempts": [
            {**self.row, "reported_cost_usd": "2000000"}]}))
        result = audit(str(self.repo), [self.commit()])
        self.assertEqual(result["summary"]["known_reported_usd"], "0")
        self.assertEqual(result["summary"]["unknown_cost_attempts"], 1)
        self.assertEqual(result["summary"]["unknown_attempt_reservations_usd"], "0.01")
        self.assertEqual(result["ignored_scripted_ledger_documents"], 1)

    def test_conflicting_copy_fails_instead_of_silently_choosing_a_cost(self):
        first = self.commit()
        self.row["reported_cost_usd"] = "0.5"
        second = self.commit()
        with self.assertRaisesRegex(ValueError, "Conflicting copies"):
            audit(str(self.repo), [first, second])

    def test_sealed_ref_rejected_before_git_read(self):
        with self.assertRaisesRegex(ValueError, "non-sealed"):
            audit("/does/not/exist", ["origin/eval/real-holdout-key"])

    def test_saved_remote_heads_excludes_local_alias_and_sealed_entry(self):
        commit = self.commit()
        self.git("update-ref", "refs/remotes/origin/local-only-alias", commit)
        heads = self.repo / "heads.tsv"
        heads.write_text(commit + "\trefs/heads/actual-branch\n" +
                         "b" * 40 + "\trefs/heads/eval/real-holdout-key\n")
        output = self.repo / "audit.json"
        subprocess.check_output([
            sys.executable, str(Path(__file__).with_name("audit_saved_costs.py")),
            "--repo", str(self.repo), "--remote-heads", str(heads), "--output", str(output)])
        result = json.loads(output.read_text())
        self.assertEqual(result["snapshots"], [{"ref": "refs/heads/actual-branch", "commit": commit}])
        self.assertEqual(result["summary"]["attempts"], 1)
        self.assertEqual(result["snapshot_source"]["kind"], "saved_git_ls_remote_heads")


if __name__ == "__main__":
    unittest.main()
