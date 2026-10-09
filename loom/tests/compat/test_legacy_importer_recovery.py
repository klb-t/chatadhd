"""A3-IMP-CH003/004/006: actual legacy importer and durable SQLite regressions.

Fixtures are synthetic and local. These are bug-migration tests, not a second
parser or a claim that the legacy importer is a lossless resource projection.
"""
import hashlib
import json
import pathlib
import socket
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from engine.db import Database
from engine.events import IMPORT_DONE, bus
from engine.importer import ConversationImporter


class LegacyImporterRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="legacy-import-recovery-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.db_path = self.root / "chat.db"
        self.db = Database(self.db_path)
        self.addCleanup(lambda: self.db.close())
        self.importer = ConversationImporter(self.db, None)
        self.events = []
        self.listener = self.events.append
        bus.on(IMPORT_DONE, self.listener)
        self.addCleanup(bus.off, IMPORT_DONE, self.listener)
        for target in ("socket.socket.connect", "socket.socket.connect_ex",
                       "socket.create_connection"):
            guard = patch(target, side_effect=AssertionError("Import attempted egress"))
            self.addCleanup(guard.stop)
            guard.start()

    def write_json(self, name, data, prefix=""):
        path = self.root / name
        path.write_text(prefix + json.dumps(data, ensure_ascii=False), encoding="utf-8")
        return path

    def rows(self, result):
        return [[(m["role"], m["text"]) for m in self.db.get_msgs(c["id"])]
                for c in result]

    def assert_reopens(self, result, expected):
        self.assertEqual(self.rows(result), expected)
        self.db.close()
        self.db = Database(self.db_path)
        self.importer = ConversationImporter(self.db, None)
        self.assertEqual(self.rows(result), expected)

    def test_unknown_json_is_not_reported_as_known_empty(self):
        unknown = {"alien_turns": {"message": "SYNTHETIC_UNKNOWN_MAPPING"}}
        for index, data in enumerate((unknown, [unknown], "not a conversation", 42, [42], [None])):
            with self.subTest(data=data):
                path = self.write_json(f"unknown-{index}.json", data)
                before = hashlib.sha256(path.read_bytes()).hexdigest()
                with self.assertRaises(ValueError) as caught:
                    self.importer.import_file(path)
                self.assertEqual(caught.exception.status, "unknown")
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)
                self.assertEqual(self.db.list_convs(), [])
                self.assertEqual(self.events, [])
        self.assert_reopens([], [])

    def test_recognized_empty_containers_remain_empty(self):
        for index, data in enumerate(([], {"messages": []}, {"mapping": {}},
                                      {"chat_messages": []}, {"conversations": []},
                                      {"data": []}, {"items": []})):
            with self.subTest(data=data):
                self.assertEqual(self.importer.import_file(
                    self.write_json(f"empty-{index}.json", data)), [])
        self.assertEqual(self.events, [])
        self.assert_reopens([], [])

    def test_large_top_level_object_uses_same_mapping_and_preserves_source(self):
        messages = [{"role": "user", "content": "Synthetic question"},
                    {"role": "assistant", "content": "Synthetic answer"}]
        small = {"title": "Synthetic object export", "messages": messages}
        expected = [[("user", "Synthetic question"), ("assistant", "Synthetic answer")]]
        baseline = self.importer.import_file(self.write_json("small.json", small))
        self.assertEqual(self.rows(baseline), expected)
        for index, large in enumerate(({**small, "padding": "X" * 5_000_010},
                                       {"padding": "[not an array]" + "X" * 5_000_010, **small})):
            with self.subTest(index=index):
                path = self.write_json(f"large-object-{index}.json", large, " \n\t")
                source_hash = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertGreater(path.stat().st_size, 5_000_000)
                result = self.importer.import_file(path)
                self.assertEqual(result[0]["title"], baseline[0]["title"])
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), source_hash)
                self.assert_reopens(result, expected)

    def test_large_direct_message_array_is_one_conversation(self):
        messages = [{"role": "user", "content": "Synthetic question"},
                    {"role": "assistant", "content": "Synthetic answer"}]
        expected = [[("user", "Synthetic question"), ("assistant", "Synthetic answer")]]
        small = self.importer.import_file(self.write_json("small-messages.json", messages), "Shared title")
        large = [{**messages[0], "unknown_source_field": "X" * 5_000_010}, messages[1]]
        path = self.write_json("large-messages.json", large, " \n\t")
        result = self.importer.import_file(path, "Shared title")
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["title"], small[0]["title"])
        self.assert_reopens(result, expected)

    def test_large_conversation_array_still_streams_supported_conversations(self):
        data = [{"title": "One", "messages": [{"role": "user", "content": "First"}],
                 "unknown_source_field": "X" * 5_000_010},
                {"title": "Two", "chat_messages": [{"sender": "assistant", "text": "Second"}]}]
        result = self.importer.import_file(self.write_json("large-conversations.json", data))
        self.assertEqual([c["title"] for c in result], ["[Import] One", "[Import] Two"])
        self.assert_reopens(result, [[("user", "First")], [("assistant", "Second")]])

    def test_large_unknown_object_keeps_explicit_unknown_status(self):
        for index, data in enumerate(({"alien_turns": [], "padding": "X" * 5_000_010},
                                       [{"alien_turns": [], "padding": "X" * 5_000_010}])):
            with self.subTest(index=index):
                path = self.write_json(f"large-unknown-{index}.json", data)
                with self.assertRaises(ValueError) as caught:
                    self.importer.import_file(path)
                self.assertEqual(caught.exception.status, "unknown")
        self.assertEqual(self.db.list_convs(), [])
        self.assertEqual(self.events, [])

    def test_large_scalar_array_is_unknown_not_empty(self):
        path = self.root / "scalar.json"
        path.write_text('[42' + ' ' * 5_000_001 + ']', encoding="utf-8")
        with self.assertRaises(ValueError) as caught:
            self.importer.import_file(path)
        self.assertEqual(caught.exception.status, "unknown")
        self.assertEqual(self.db.list_convs(), [])
        self.assertEqual(self.events, [])

    def test_html_label_fallback_persists_source_order_and_excludes_script(self):
        html = ('<html><body><script>User: synthetic hidden script text</script>'
                '<style>Assistant: synthetic hidden style text</style>'
                '<p>User: Synthetic first question</p><p>Assistant: Synthetic first answer</p>'
                '<p>User: Synthetic second question</p><p>Assistant: Synthetic second answer</p>'
                '</body></html>')
        path = self.root / "labels.html"
        path.write_text(html, encoding="utf-8")
        result = self.importer.import_file(path)
        path.unlink()
        self.assert_reopens(result, [[("user", "Synthetic first question"),
                                     ("assistant", "Synthetic first answer"),
                                     ("user", "Synthetic second question"),
                                     ("assistant", "Synthetic second answer")]])

    def test_html_structured_parser_retains_existing_order(self):
        path = self.root / "structured.html"
        path.write_text('<html><body><div data-role="assistant">Synthetic initial answer</div>'
                        '<div data-role="user">Synthetic next question</div></body></html>',
                        encoding="utf-8")
        self.assert_reopens(self.importer.import_file(path),
                            [[("assistant", "Synthetic initial answer"),
                              ("user", "Synthetic next question")]])


if __name__ == "__main__":
    unittest.main()
