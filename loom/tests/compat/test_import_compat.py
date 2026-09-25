"""Differential tests: engine.importer.ConversationImporter (Python, the
reference) vs loom::ConversationImporter (C++, via loom_compat_tool), run on
every fixture under tests/fixtures/import/. Zero diffs is the bar; any
intentional difference is called out in a comment next to the test.
"""
import pathlib
import shutil
import sys
import tempfile
import unittest

from compat_common import CompatTestCase, REPO, tool

sys.path.insert(0, str(REPO))
from engine.db import Database  # noqa: E402
from engine.importer import ConversationImporter  # noqa: E402

FIXTURES = REPO / "loom" / "tests" / "fixtures" / "import"


def import_dump_py(path: pathlib.Path, title: str = ""):
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="engine_compat_import_"))
    try:
        db = Database(tmp / "c.db")
        imp = ConversationImporter(db, None)
        results = imp.import_file(str(path), title or None)
        if isinstance(results, dict):
            results = [results]
        results = [r for r in (results or []) if r is not None]
        convs = []
        for c in results:
            msgs = db.get_msgs(c["id"], include_all=True)
            convs.append({
                "title": c["title"],
                "messages": [{"role": m["role"], "text": m["text"]} for m in msgs],
            })
        return {"conversations": convs}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


class ImportCompatTests(CompatTestCase):
    def check(self, name: str, title: str = ""):
        path = FIXTURES / name
        py = import_dump_py(path, title)
        cpp = tool("import-dump", str(path), title)
        cpp.pop("format", None)
        cpp.pop("messages", None)
        self.assertSameJson(py, cpp, msg=name)

    def test_detect_format_matches_every_fixture(self):
        cases = {
            "chatgpt_conversations.json": "json",
            "claude_export.json": "json",
            "openai_messages.json": "json",
            "conversation_objects.json": "json",
            "wrapped_data.json": "json",
            "messages.jsonl": "jsonl",
            "chat.html": "html",
            "chat_labels.htm": "html",
            "chat.mht": "mht",
            "chat.md": "markdown",
            "chat.txt": "text",
            "note.log": "text",
            "claude.db": "sqlite",
            "generic.db": "sqlite",
            "bundle.zip": "zip",
            "nested.zip": "zip",
            "chatgpt_edits.json": "json",
            "claude_unicode.json": "json",
            "odd_messages.json": "json",
            "malformed.json": "json",
            "export_without_extension": "json",  # sniffed: starts with '['
        }
        for name, expected in cases.items():
            with self.subTest(name=name):
                cpp = tool("detect-format", str(FIXTURES / name))
                self.assertEqual(cpp["format"], expected, name)
                self.assertEqual(cpp["format"], ConversationImporter(None, None).detect_format(FIXTURES / name))

    def test_chatgpt_mapping_tree(self):
        self.check("chatgpt_conversations.json")

    def test_claude_export(self):
        self.check("claude_export.json")

    def test_openai_message_list(self):
        self.check("openai_messages.json")

    def test_conversation_objects_wrapper(self):
        self.check("conversation_objects.json")

    def test_wrapped_data_key(self):
        self.check("wrapped_data.json")

    def test_jsonl(self):
        self.check("messages.jsonl")

    def test_html_tag_parser(self):
        self.check("chat.html")

    def test_html_label_fallback(self):
        self.check("chat_labels.htm")

    def test_mht(self):
        self.check("chat.mht")

    def test_markdown(self):
        self.check("chat.md")

    def test_text_with_labels(self):
        self.check("chat.txt")

    def test_text_without_labels(self):
        self.check("note.log")

    def test_sqlite_claude_layout(self):
        self.check("claude.db")

    def test_sqlite_generic_layout(self):
        self.check("generic.db")

    def test_json_no_extension_sniffed(self):
        self.check("export_without_extension")

    def test_zip_bundle(self):
        self.check("bundle.zip")

    def test_chatgpt_edited_branch(self):
        self.check("chatgpt_edits.json")

    def test_claude_unicode_and_ignored_fields(self):
        self.check("claude_unicode.json")

    def test_odd_and_empty_messages(self):
        self.check("odd_messages.json")

    def test_nested_zip(self):
        self.check("nested.zip")

    def test_malformed_json_errors_on_both_sides(self):
        path = FIXTURES / "malformed.json"
        with self.assertRaises(Exception):
            import_dump_py(path)
        proc_err = None
        try:
            tool("import-dump", str(path))
        except AssertionError as e:
            proc_err = str(e)
        self.assertIsNotNone(proc_err)

    def test_explicit_title_overrides_everything(self):
        # `title` wins over any title found inside the source, on both sides.
        self.check("claude_export.json", title="Forced Title")
        self.check("chatgpt_conversations.json", title="Forced Title")

    def test_unknown_format_raises_on_both_sides(self):
        path = FIXTURES / "chat.html"  # any real file works; we corrupt the check via a bogus extension
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="engine_compat_import_unknown_"))
        try:
            bogus = tmp / "note.xyz123"
            bogus.write_bytes(b"\x00\x01\x02 not any known format at all")
            with self.assertRaises(ValueError):
                ConversationImporter(Database(tmp / "c.db"), None).import_file(str(bogus))
            proc_err = None
            try:
                tool("import-dump", str(bogus))
            except AssertionError as e:
                proc_err = str(e)
            self.assertIsNotNone(proc_err)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
