"""Offline Python counterpart of test_github.cpp's configurable sync preset.

Only the HTTP boundary is mocked. No request reaches a network transport.
"""
import base64
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from engine.github_sync import GitHubSync, SyncConfig


class GitHubSyncDefaultsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.content = {
            "notes.json": '{"note":"shareable fixture"}',
            "secrets.json": '{"api_key":"fixture-root-only"}',
            "nested/deeper/secrets.json": '{"api_key":"fixture-nested-only"}',
        }
        for name, content in self.content.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        reply = Mock(status_code=200, json=Mock(return_value=[]))
        self.get = self.enterContext(patch("engine.github_sync.requests.get", return_value=reply))
        self.put = self.enterContext(patch("engine.github_sync.requests.put", return_value=reply))

    def upload(self, **patterns):
        # JSON round-trip represents caller/persisted settings, including [].
        settings = json.loads(json.dumps({
            "repo": "o/r", "local_path": str(self.root), **patterns,
        }))
        outcome = GitHubSync(SyncConfig(**settings)).push_all()
        self.assertEqual(outcome["failed"], 0)
        self.get.assert_called_once()
        uploaded = {
            call.args[0].split("/contents/", 1)[1]:
                base64.b64decode(call.kwargs["json"]["content"]).decode("utf-8")
            for call in self.put.call_args_list
        }
        self.assertEqual(outcome["success"], len(uploaded))
        for name, content in self.content.items():
            self.assertEqual((self.root / name).read_text(encoding="utf-8"), content)
        return uploaded

    def test_default_push_excludes_root_and_nested_credential_bytes(self):
        uploaded = self.upload()
        self.assertEqual(uploaded, {"notes.json": self.content["notes.json"]})
        for content in uploaded.values():
            self.assertNotIn("fixture-root-only", content)
            self.assertNotIn("fixture-nested-only", content)

    def test_explicit_empty_exclusions_can_upload_the_same_files(self):
        self.assertEqual(self.upload(exclude_patterns=[]), self.content)


if __name__ == "__main__":
    unittest.main()
