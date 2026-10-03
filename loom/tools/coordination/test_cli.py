"""Subprocess-level operator commands; no provider or network dependencies."""
import json
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from loom.tools.coordination.test_leases import COMMIT
from loom.tools.coordination import LeaseStore

ROOT = Path(__file__).resolve().parents[3]
SCRIPTED = ROOT / "docs/research/agentic_graph_v1/scripted_three_stage_first"


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.database = self.folder / "coordination.sqlite3"

    def command(self, *args, success=True):
        process = subprocess.run([sys.executable, "-m", "loom.tools.coordination",
                                  "--database", str(self.database), *map(str, args)],
                                 cwd=ROOT, capture_output=True, text=True, timeout=20)
        if success:
            self.assertEqual(process.returncode, 0, process.stderr)
            return json.loads(process.stdout)
        self.assertNotEqual(process.returncode, 0, process.stdout)
        return json.loads(process.stderr)

    def test_register_status_and_backup_are_usable_without_python_callback_code(self):
        spec = self.folder / "spec.json"
        spec.write_text(json.dumps({"operation": "inspect", "input_sha256": "a" * 64}))
        created = self.command("register", "--task", "operator-task", "--source-commit", COMMIT,
                               "--spec", spec)
        self.assertEqual(created["task"]["state"], "pending")
        status = self.command("status", "--task", "operator-task", "--receipts")
        self.assertEqual(status["task"]["source_commit"], COMMIT)
        self.assertEqual(status["receipts"][0]["event"], "registered")
        checkpoint = self.folder / "checkpoint.sqlite3"
        backup = self.command("backup", "--output", checkpoint)
        self.assertEqual(len(backup["sha256"]), 64)
        self.assertTrue(checkpoint.is_file())
        self.command("backup", "--output", checkpoint, success=False)
        self.database = checkpoint
        restored = self.command("status", "--task", "operator-task", "--receipts")
        self.assertEqual(restored, status)

    def test_conflicting_or_ambiguous_specs_do_not_replace_task(self):
        spec = self.folder / "spec.json"
        spec.write_text('{"value": 1}')
        self.command("register", "--task", "same", "--source-commit", COMMIT, "--spec", spec)
        spec.write_text('{"value": 2}')
        failure = self.command("register", "--task", "same", "--source-commit", COMMIT,
                               "--spec", spec, success=False)
        self.assertEqual(failure["reason"], "task_identity_conflict")
        spec.write_text('{"value": 2, "value": 1}')
        failure = self.command("register", "--task", "same", "--source-commit", COMMIT,
                               "--spec", spec, success=False)
        self.assertEqual(failure["reason"], "duplicate_json_key")
        self.assertEqual(self.command("status", "--task", "same")["task"]["spec"], {"value": 1})

    def test_missing_database_status_does_not_create_empty_authority(self):
        failure = self.command("status", "--task", "absent", success=False)
        self.assertEqual(failure["reason"], "database_not_found")
        self.assertFalse(self.database.exists())

    def replay_args(self, recorded=SCRIPTED, task="graph-replay"):
        return ("graph-replay", "--task", task, "--source-commit", COMMIT,
                "--owner", "cli-operator", "--packet", recorded / "input_packet.json",
                "--method", recorded / "method.json", "--recorded-run", recorded,
                "--output-root", self.folder / "outputs", "--lease-seconds", "30")

    def copy_recorded(self):
        recorded = self.folder / "recorded"
        shutil.copytree(SCRIPTED, recorded)
        return recorded

    def test_replay_real_saved_three_stage_workflow_and_repeat_without_dispatch(self):
        result = self.command(*self.replay_args())
        self.assertEqual(result["mode"], "exact_saved_transcript_replay")
        self.assertEqual(result["network_calls"], 0)
        self.assertEqual(result["preflight"]["exact_request_matches"], 3)
        outcome = result["execution"]["outcome"]
        self.assertEqual(outcome["workflow_result"]["completed_stages"], 3)
        directory = Path(outcome["artifact_directory"])
        self.assertTrue((directory / "0003.first_transport_response.json").is_file())
        original = (directory / "result_first.json").read_bytes()
        repeated = self.command(*self.replay_args())
        self.assertFalse(repeated["execution"]["acquired"])
        self.assertEqual((directory / "result_first.json").read_bytes(), original)
        status = self.command("status", "--task", "graph-replay", "--receipts")
        self.assertEqual(sum(row["event"] == "dispatch_started" for row in status["receipts"]), 1)
        manifest = status["task"]["spec"]["transport_binding"]["transcript"]
        saved = next(row for row in manifest["files"] if row["file"] == "0001.first_transport_response.json")
        self.assertEqual(saved["sha256"], hashlib.sha256((SCRIPTED / saved["file"]).read_bytes()).hexdigest())

    def test_missing_saved_stage_fails_before_creating_task_or_output(self):
        recorded = self.copy_recorded()
        (recorded / "0002.first_transport_response.json").unlink()
        failure = self.command(*self.replay_args(recorded), success=False)
        self.assertEqual(failure["reason"], "saved_workflow_file_missing")
        self.assertEqual(failure["evidence"]["network_calls"], 0)
        self.assertFalse(self.database.exists())
        self.assertFalse((self.folder / "outputs").exists())

    def test_mismatching_dynamic_request_fails_with_evidence_before_dispatch(self):
        recorded = self.copy_recorded()
        request_path = recorded / "0002.request.json"
        request = json.loads(request_path.read_text())
        request["workflow_id"] = "not-the-replayed-method"
        request_path.write_text(json.dumps(request))
        failure = self.command(*self.replay_args(recorded), success=False)
        self.assertEqual(failure["reason"], "saved_workflow_replay_not_exact")
        detail = failure["evidence"]["failures"][0]
        self.assertEqual(detail["ordinal"], 2)
        self.assertNotEqual(detail["expected_sha256"], detail["actual_sha256"])
        self.assertFalse(self.database.exists())

    def test_changed_raw_response_bytes_cannot_reuse_completed_task(self):
        recorded = self.copy_recorded()
        self.command(*self.replay_args(recorded))
        response = recorded / "0003.first_transport_response.json"
        response.write_bytes(response.read_bytes() + b"\n")
        failure = self.command(*self.replay_args(recorded), success=False)
        self.assertEqual(failure["reason"], "task_identity_conflict")
        status = self.command("status", "--task", "graph-replay")
        self.assertEqual(status["task"]["fence"], 1)

    def test_unknown_task_is_inspectable_and_never_automatically_replayed(self):
        self.command(*self.replay_args())
        original = self.command("status", "--task", "graph-replay")["task"]
        store = LeaseStore(self.database)
        store.register("unknown-task", source_commit=COMMIT, spec=original["spec"])
        lease = store.claim("unknown-task", owner="prior-worker", lease_seconds=30)["lease"]
        store.begin(lease)
        store.mark_unknown(lease, evidence=[{"kind": "synthetic_interruption"}])
        replay = self.command(*self.replay_args(task="unknown-task"))
        self.assertFalse(replay["execution"]["acquired"])
        self.assertEqual(replay["execution"]["task"]["state"], "outcome_unknown")
        status = self.command("status", "--task", "unknown-task", "--receipts")
        self.assertEqual(status["receipts"][-1]["event"], "outcome_unknown")

    def test_recorded_transport_exception_is_not_fabricated_into_a_response(self):
        recorded = self.copy_recorded()
        response = recorded / "0001.first_transport_response.json"
        response.write_text('{"transport_failure_class":"TimeoutError"}')
        failure = self.command(*self.replay_args(recorded), success=False)
        self.assertEqual(failure["reason"], "saved_transport_failure_is_not_a_response")
        self.assertEqual(failure["evidence"]["failure_class"], "TimeoutError")
        self.assertFalse(self.database.exists())

    def test_bool_and_float_ordinals_are_not_integer_stage_identities(self):
        recorded = self.copy_recorded()
        path = recorded / "0001.request.json"
        request = json.loads(path.read_text())
        for ordinal in (True, 1.0):
            with self.subTest(ordinal=repr(ordinal)):
                request["ordinal"] = ordinal
                path.write_text(json.dumps(request))
                failure = self.command(*self.replay_args(recorded), success=False)
                self.assertEqual(failure["reason"], "saved_workflow_stage_identity_mismatch")
                self.assertFalse(self.database.exists())
                self.assertFalse((self.folder / "outputs").exists())

    def test_nested_boolean_and_float_request_values_do_not_match_integer(self):
        recorded = self.copy_recorded()
        path = recorded / "0001.request.json"
        request = json.loads(path.read_text())
        for value in (True, 1.0):
            with self.subTest(value=repr(value)):
                request["packet"]["entities"][0]["attrs"]["custom_type_detail"][0] = value
                path.write_text(json.dumps(request))
                failure = self.command(*self.replay_args(recorded), success=False)
                self.assertEqual(failure["reason"], "saved_workflow_replay_not_exact")
                first = failure["evidence"]["failures"][0]
                self.assertNotEqual(first["expected_sha256"], first["actual_sha256"])
                self.assertFalse(self.database.exists())

    def test_packet_and_method_input_equality_preserves_json_scalar_types(self):
        recorded = self.copy_recorded()
        packet = json.loads((recorded / "input_packet.json").read_text())
        packet["entities"][0]["attrs"]["custom_type_detail"][0] = True
        packet_path = self.folder / "different-packet.json"
        packet_path.write_text(json.dumps(packet))
        arguments = list(self.replay_args(recorded))
        arguments[arguments.index("--packet") + 1] = packet_path
        failure = self.command(*arguments, success=False)
        self.assertEqual(failure["reason"], "saved_workflow_packet_mismatch")
        method = json.loads((recorded / "method.json").read_text())
        method["resources"]["expected_usage_multiplier"] = 1.0
        method_path = self.folder / "different-method.json"
        method_path.write_text(json.dumps(method))
        arguments = list(self.replay_args(recorded))
        arguments[arguments.index("--method") + 1] = method_path
        failure = self.command(*arguments, success=False)
        self.assertEqual(failure["reason"], "saved_workflow_method_mismatch")
        self.assertFalse(self.database.exists())


if __name__ == "__main__":
    unittest.main()
