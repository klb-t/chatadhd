"""CH-011: real legacy worker/store outcomes, with controlled provider boundaries.

This narrow regression suite does not claim graph-write atomicity: the legacy
GraphEngine still absorbs some ingestion failures. Native lifecycle gates cover
the separate executing/attempt contract.
"""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from engine.config import Config, Secrets
from engine.db import Database
from engine.events import EventBus
from engine.graph_engine import GraphEngine
from engine.semantic_llm import SemanticLLM
from engine.semantic_worker import SemanticWorker


TEXT = "Synthetic worker evidence audit-worker@example.invalid."
EMPTY = {"entities": [], "topics": [], "relations": [], "summary": "", "source": "llm"}


class SemanticWorkerOutcomesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.events = EventBus()
        self.enterContext(patch("engine.semantic_worker.bus", self.events))
        self.enterContext(patch("engine.graph_engine.bus", self.events))
        self.http = self.enterContext(patch("requests.sessions.Session.request",
                                           side_effect=AssertionError("Unexpected real HTTP dispatch")))
        self.enterContext(patch("engine.semantic_worker.time.sleep"))
        self.config = Config(self.root / "config.json")
        self.secrets = Secrets(self.root / "secrets.json")
        self.db = Database(self.root / "worker.db")
        self.graph = GraphEngine(self.db)
        self.llm = SimpleNamespace(enabled=True, analyse=Mock(side_effect=TimeoutError("synthetic timeout")))
        self.worker = SemanticWorker(self.db, self.llm, self.graph, self.config, self.secrets)
        self.conv = self.db.create_conv("Public synthetic worker fixture")

    def tearDown(self):
        self.graph.stop()
        self.db.close()
        self.assertEqual(self.http.call_count, 0)

    def message(self, metadata=None, text=TEXT):
        return self.db.create_msg(self.conv["id"], text, "user", metadata=metadata)

    def reopen(self):
        self.graph.stop()
        self.db.close()
        self.db = Database(self.root / "worker.db")
        self.graph = GraphEngine(self.db)
        self.worker = SemanticWorker(self.db, self.llm, self.graph, self.config, self.secrets)

    def test_timeout_is_failed_with_evidence_and_survives_reopen_without_replay(self):
        previous = {"future": {"preserve": [1, "unknown"]}}
        original = {"owner": {"unknown": [True, "keep"]}, "semantic_source": "earlier",
                    "loom_semantic_failures": [previous]}
        mid = self.message(original)
        self.assertEqual(self.worker._drain_batch(), 0)
        row = self.db.get_msg(mid)
        self.assertEqual(row["semantic_status"], "failed")
        self.assertEqual(self.worker.status["errors"], 1)
        self.assertEqual(self.worker.status["processed"], 0)
        self.assertEqual(self.worker.status["pending"], 0)
        meta = row["metadata"]
        self.assertEqual(meta["owner"], original["owner"])
        self.assertEqual(meta["semantic_source"], "earlier")
        self.assertEqual(meta["loom_semantic_failures"][0], previous)
        failure = meta["loom_semantic_failures"][1]
        self.assertEqual(failure["schema"], "loom.semantic_failure/1")
        self.assertEqual(failure["state"], "failed")
        self.assertEqual(failure["error"], {"code": "TimeoutError", "message": "synthetic timeout"})
        self.assertEqual(failure["source"]["text_sha256"], hashlib.sha256(TEXT.encode()).hexdigest())
        self.assertEqual(failure["source"]["message_id"], mid)
        self.assertEqual(failure["source"]["version_num"], row["version_num"])
        self.assertTrue(failure["started"])
        self.assertTrue(failure["finished"])
        self.reopen()
        self.assertEqual(self.db.get_msg(mid)["semantic_status"], "failed")
        self.assertEqual(self.db.get_msg(mid)["metadata"], meta)
        self.worker.resume()
        self.assertEqual(self.worker._drain_batch(), 0)
        self.assertEqual(self.llm.analyse.call_count, 1)

    def test_pause_resume_does_not_retry_and_explicit_requeue_preserves_history(self):
        mid = self.message({"owner": "keep"})
        self.worker.pause()
        self.assertEqual(self.worker._drain_batch(), 0)
        self.llm.analyse.assert_not_called()
        self.assertEqual(self.db.get_msg(mid)["semantic_status"], "pending")
        self.worker.resume()
        self.assertEqual(self.worker._drain_batch(), 0)
        self.assertEqual(self.db.get_msg(mid)["semantic_status"], "failed")
        self.worker.pause()
        self.worker.resume()
        self.assertEqual(self.worker._drain_batch(), 0)
        self.assertEqual(self.llm.analyse.call_count, 1)
        self.db.update_msg(mid, semantic_status="pending")
        self.assertEqual(self.worker._drain_batch(), 0)
        failures = self.db.get_msg(mid)["metadata"]["loom_semantic_failures"]
        self.assertEqual(len(failures), 2)
        self.assertNotEqual(failures[0]["id"], failures[1]["id"])
        self.db.update_msg(mid, semantic_status="pending")
        self.llm.analyse.side_effect = None
        self.llm.analyse.return_value = EMPTY.copy()
        self.assertEqual(self.worker._drain_batch(), 1)
        row = self.db.get_msg(mid)
        self.assertEqual(row["semantic_status"], "done")
        self.assertEqual(row["metadata"]["loom_semantic_failures"], failures)
        self.assertEqual(row["metadata"]["owner"], "keep")
        self.assertEqual(self.worker.status["errors"], 2)
        self.assertEqual(self.worker.status["processed"], 1)
        self.reopen()
        self.assertEqual(self.db.get_msg(mid)["metadata"]["loom_semantic_failures"], failures)

    def test_default_regex_and_valid_empty_analysis_remain_successful(self):
        self.worker.semantic_llm = None
        mid = self.message({"owner": {"unknown": 7}})
        expected = self.worker._regex_analyse(TEXT)
        self.assertEqual(self.worker._drain_batch(), 1)
        meta = self.db.get_msg(mid)["metadata"]
        self.assertEqual(meta["semantic_source"], "regex")
        self.assertEqual(meta["entity_count"], len(expected["entities"]))
        self.assertEqual(meta["topic_count"], len(expected["topics"]))
        self.assertEqual(meta["owner"], {"unknown": 7})
        self.assertNotIn("loom_semantic_failures", meta)
        self.worker.semantic_llm = self.llm
        self.llm.analyse.side_effect = None
        self.llm.analyse.return_value = EMPTY.copy()
        empty_mid = self.message(text="A sufficiently long plain synthetic sentence.")
        self.assertEqual(self.worker._drain_batch(), 1)
        empty = self.db.get_msg(empty_mid)
        self.assertEqual(empty["semantic_status"], "done")
        self.assertEqual(empty["metadata"]["entity_count"], 0)
        self.assertEqual(empty["metadata"]["topic_count"], 0)
        self.assertNotIn("loom_semantic_failures", empty["metadata"])

    def test_failure_merges_metadata_written_while_analysis_was_in_flight(self):
        mid = self.message({"owner": {"original": True}})
        def analyse(_text):
            meta = self.db.get_msg(mid)["metadata"]
            meta["owner"]["concurrent"] = ["keep"]
            self.db.update_msg(mid, metadata=meta)
            raise TimeoutError("synthetic concurrent timeout")
        self.llm.analyse.side_effect = analyse
        self.assertEqual(self.worker._drain_batch(), 0)
        row = self.db.get_msg(mid)
        self.assertEqual(row["semantic_status"], "failed")
        self.assertEqual(row["metadata"]["owner"], {"original": True, "concurrent": ["keep"]})
        self.assertEqual(len(row["metadata"]["loom_semantic_failures"]), 1)

    def test_real_llm_timeout_with_valid_regex_fallback_is_still_success(self):
        self.config.set("semantic_model", "synthetic/offline")
        self.secrets.set("api_key", "synthetic-not-a-credential")
        self.worker.semantic_llm = SemanticLLM(self.config, self.secrets)
        mid = self.message({"owner": "keep"})
        with patch("engine.semantic_llm.requests.post", side_effect=TimeoutError("controlled HTTP timeout")) as post:
            self.assertEqual(self.worker._drain_batch(), 1)
            self.assertEqual(post.call_count, 1)
        row = self.db.get_msg(mid)
        self.assertEqual(row["semantic_status"], "done")
        self.assertEqual(row["metadata"]["semantic_source"], "regex")
        self.assertNotIn("loom_semantic_failures", row["metadata"])
        self.assertGreater(row["metadata"]["entity_count"], 0)

    def test_completion_sql_exception_is_failed_without_claiming_graph_rollback(self):
        self.worker.semantic_llm = None
        mid = self.message({"owner": "keep"})
        with self.db._lock:
            self.db._conn.executescript("""
                CREATE TRIGGER synthetic_completion_failure
                BEFORE UPDATE OF semantic_status ON messages
                WHEN NEW.semantic_status = 'done'
                BEGIN SELECT RAISE(ABORT, 'synthetic completion failure'); END;
            """)
        self.assertEqual(self.worker._drain_batch(), 0)
        row = self.db.get_msg(mid)
        self.assertEqual(row["semantic_status"], "failed")
        failure = row["metadata"]["loom_semantic_failures"][0]
        self.assertEqual(failure["error"]["code"], "IntegrityError")
        self.assertEqual(row["metadata"]["owner"], "keep")
        self.assertEqual(self.worker.status["errors"], 1)
        # These already committed legacy graph writes remain an explicit limit
        # of this fix, not evidence of an atomic analysis transaction.
        self.assertGreater(self.db._conn.execute("SELECT COUNT(*) FROM nodes").fetchone()[0], 0)
        self.assertEqual(self.worker._drain_batch(), 0)
        self.reopen()
        self.assertEqual(self.db.get_msg(mid)["semantic_status"], "failed")

    def test_occupied_failure_history_is_preserved_and_blocks_dispatch(self):
        for value in ({"future_schema": 2}, None, "owner value"):
            with self.subTest(value=value):
                meta = {"owner": "keep", "loom_semantic_failures": value}
                mid = self.message(meta)
                self.worker.resume()
                self.assertEqual(self.worker._drain_batch(), 0)
                self.assertEqual(self.db.get_msg(mid)["metadata"], meta)
                self.assertEqual(self.db.get_msg(mid)["semantic_status"], "pending")
                self.assertTrue(self.worker._paused)
                self.llm.analyse.assert_not_called()
                self.db.update_msg(mid, semantic_status="failed")

    def fetch(self, items):
        response = Mock(status_code=200)
        response.iter_lines.return_value = [json.dumps(item) for item in items]
        self.worker._active_batch_id = "synthetic-batch"
        with patch("engine.semantic_worker.requests.get", return_value=response) as get:
            self.worker._fetch_batch_results()
            self.assertEqual(get.call_count, 1)
            self.assertTrue(get.call_args.kwargs["stream"])

    def test_batch_non_success_is_failed_preserves_provider_evidence_and_other_rows(self):
        failures = []
        items = []
        for outcome in ("errored", "canceled", "expired"):
            mid = self.message({"owner": {"future": outcome}})
            result = {"type": outcome, "error": {"type": "synthetic", "message": "safe failure"},
                      "unknown_provider_field": ["preserve", 7]}
            failures.append((mid, result))
            items.append({"custom_id": mid, "result": result})
        success = self.message({"owner": "valid empty"})
        items.append({"custom_id": success, "result": {"type": "succeeded", "message": {
            "content": [{"type": "text", "text": json.dumps(EMPTY)}]}}})
        self.fetch(items)
        for mid, result in failures:
            row = self.db.get_msg(mid)
            self.assertEqual(row["semantic_status"], "failed")
            self.assertEqual(row["metadata"]["owner"], {"future": result["type"]})
            evidence = row["metadata"]["loom_semantic_failures"][0]
            self.assertEqual(evidence["evidence"]["batch_result"], result)
            self.assertEqual(evidence["evidence"]["batch_id"], "synthetic-batch")
        self.assertEqual(self.db.get_msg(success)["semantic_status"], "done")
        self.assertEqual(self.worker.status["processed"], 1)
        self.assertEqual(self.worker.status["errors"], 3)
        snapshots = {mid: self.db.get_msg(mid) for mid, _ in failures}
        self.fetch(items)  # replaying completed results cannot replace history/outcome
        for mid, _ in failures:
            self.assertEqual(self.db.get_msg(mid), snapshots[mid])
        self.reopen()
        for mid, _ in failures:
            self.assertEqual(self.db.get_msg(mid)["semantic_status"], "failed")
        self.assertEqual(self.db.get_msg(success)["semantic_status"], "done")

    def test_batch_invalid_analysis_for_known_message_records_failure(self):
        mid = self.message({"owner": "keep"})
        self.fetch([{"custom_id": mid, "result": {"type": "succeeded", "message": {
            "content": [{"type": "text", "text": "invalid synthetic JSON"}]}}}])
        row = self.db.get_msg(mid)
        self.assertEqual(row["semantic_status"], "failed")
        self.assertEqual(row["metadata"]["owner"], "keep")
        self.assertEqual(row["metadata"]["loom_semantic_failures"][0]["error"]["code"], "JSONDecodeError")
        self.assertEqual(self.worker.status["processed"], 0)
        self.assertEqual(self.worker.status["errors"], 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
