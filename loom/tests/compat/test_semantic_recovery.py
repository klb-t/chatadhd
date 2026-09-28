"""Offline Python regressions for semantic configuration and failure recovery.

These tests use the real Config/Secrets stores and mock only the HTTP boundary.
The matching native cases live in test_config.cpp / test_semantic_llm.cpp.
"""
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from engine.config import Config, Secrets
from engine.semantic_llm import SemanticLLM

TEXT = "This message is long enough to exercise semantic analysis."


def reply(status=200):
    analysis = {"entities": [], "topics": [], "relations": [], "summary": "test"}
    return Mock(status_code=status, json=Mock(return_value={
        "choices": [{"message": {"content": json.dumps(analysis)}}]}))


class SemanticRecoveryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.config = Config(Path(self.tmp.name) / "config.json")
        self.secrets = Secrets(Path(self.tmp.name) / "secrets.json")
        self.config.set("semantic_model", "old/model")
        self.config.set("base_url", "https://old.test")
        self.secrets.set("api_key", "test-private-key")
        self.llm = SemanticLLM(self.config, self.secrets)
        self.post = self.enterContext(patch("engine.semantic_llm.requests.post", return_value=reply(500)))
        self.enterContext(patch("engine.semantic_llm.log"))

    def trip(self):
        for _ in range(5):
            self.assertEqual(self.llm.analyse(TEXT)["source"], "regex")
        self.assertFalse(self.llm.enabled)
        self.assertEqual(self.llm._consecutive_failures, 5)

    def test_preserves_configured_model_on_repeated_load(self):
        path = Path(self.tmp.name) / "preserved.json"
        for model in ("anthropic/claude-haiku-4-5", "x/claude-haiku-4", "local/future-model", ""):
            for version in (None, 3):
                with self.subTest(model=model, version=version):
                    content = {"semantic_model": model}
                    if version is not None:
                        content["_config_version"] = version
                    original = json.dumps(content)
                    path.write_text(original, encoding="utf-8")
                    for _ in range(2):
                        self.assertEqual(Config(path).get("semantic_model"), model)
                        self.assertEqual(path.read_text(encoding="utf-8"), original)

    def test_same_or_unrelated_settings_do_not_reenable_or_make_requests(self):
        self.trip()
        self.config.set("theme", "amoled")
        self.config.set("semantic_model", "old/model")
        self.config.set("base_url", "https://old.test/")  # Same effective endpoint.
        self.secrets.set("api_key", "test-private-key")
        self.assertFalse(self.llm.enabled)
        self.llm.analyse(TEXT)
        self.assertEqual(self.post.call_count, 5)

    def test_each_relevant_change_recovers_without_automatic_replay(self):
        changes = (lambda: self.config.set("semantic_model", "new/model"),
                   lambda: self.config.set("base_url", "https://new.test"),
                   lambda: self.secrets.set("api_key", "changed-private-key"),
                   lambda: self.config.set("semantic_analysis", False))
        for change in changes:
            self.trip()
            before = self.post.call_count
            change()
            self.assertEqual(self.llm.enabled, bool(self.config.get("semantic_analysis")))
            self.assertFalse(self.llm._disabled_by_errors)
            self.assertEqual(self.llm._consecutive_failures, 0)
            self.assertEqual(self.post.call_count, before)
        self.config.set("semantic_analysis", True)
        self.post.return_value = reply()
        self.assertEqual(self.llm.analyse(TEXT)["source"], "llm")
        args, kwargs = self.post.call_args
        self.assertEqual(args[0], "https://new.test/chat/completions")
        self.assertEqual(kwargs["json"]["model"], "new/model")
        self.assertEqual(kwargs["headers"]["Authorization"], "Bearer changed-private-key")
        self.assertNotIn("changed-private-key", repr(self.llm._config_identity))

    def test_success_resets_current_failure_count(self):
        for _ in range(4):
            self.llm.analyse(TEXT)
        self.post.return_value = reply()
        self.assertEqual(self.llm.analyse(TEXT)["source"], "llm")
        self.assertEqual(self.llm._consecutive_failures, 0)
        self.post.return_value = reply(500)
        self.llm.analyse(TEXT)
        self.assertEqual(self.llm._consecutive_failures, 1)

    def run_in_flight(self, old_success, action):
        entered, release = threading.Event(), threading.Event()
        result, errors = [], []

        def post(*args, **kwargs):
            if kwargs["json"]["model"] == "old/model":
                entered.set()
                if not release.wait(5):
                    raise TimeoutError("test release was not signalled")
                return reply(200 if old_success else 500)
            return reply(500)

        def work():
            try:
                result.append(self.llm.analyse(TEXT))
            except Exception as exc:
                errors.append(exc)

        self.post.side_effect = post
        worker = threading.Thread(target=work)
        worker.start()
        try:
            self.assertTrue(entered.wait(5), "request did not start")
            action()
        finally:
            release.set()
            worker.join(5)
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(result[0]["source"], "llm" if old_success else "regex")

    def test_old_failure_cannot_affect_changed_settings_without_enabled_poll(self):
        self.run_in_flight(False, lambda: self.config.set("semantic_model", "new/model"))
        self.assertEqual(self.llm._consecutive_failures, 0)
        self.assertTrue(self.llm.enabled)

    def test_old_success_cannot_clear_new_generation_failures(self):
        def new_failures():
            self.config.set("semantic_model", "new/model")
            for _ in range(4):
                self.llm.analyse(TEXT)
        self.run_in_flight(True, new_failures)
        self.assertEqual(self.llm._consecutive_failures, 4)
        self.assertTrue(self.llm.enabled)

    def test_manual_reset_invalidates_old_request(self):
        self.run_in_flight(False, self.llm.reset_failures)
        self.assertEqual(self.llm._consecutive_failures, 0)

    def test_config_identity_returning_to_old_value_still_rejects_old_completion(self):
        def round_trip():
            self.config.set("semantic_model", "new/model")
            self.assertTrue(self.llm.enabled)
            self.config.set("semantic_model", "old/model")
            self.assertTrue(self.llm.enabled)
        self.run_in_flight(False, round_trip)
        self.assertEqual(self.llm._consecutive_failures, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
