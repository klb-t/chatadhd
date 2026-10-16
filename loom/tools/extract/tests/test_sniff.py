"""Format recognition from CONTENT: every fixture format, a wrong extension, zip in zip, unknown members."""
import shutil

from _support import EXPECTED_EXTRACT, EXPORTS, EXTRACT, IMPORT, ExtractTest
from loom.tools.extract.datafiles import store
from loom.tools.extract.sources import FormatCatalog, scan


class SniffTest(ExtractTest):
    def _scan(self, *paths):
        return scan([str(p) for p in paths], catalog=FormatCatalog(store()))

    def test_every_extract_fixture_is_recognised_from_content(self):
        cases = {"claude_code/projects/-home-user-proj": "anthropic.claude_code.session", "gemini/takeout.zip": "google.gemini.takeout",
                 "gemini/MyActivity.html": "google.gemini.takeout", "grok/grok_com.json": "xai.grok.export",
                 "grok/x_archive.zip": "xai.grok.export", "ours/packet.json": "loom.graph_packet", "ours/conversation.json": "loom.export",
                 "versions/chatgpt_2025-06-20.zip": "openai.chatgpt.export", "continuation/claude_part.zip": "anthropic.claude.export"}
        for rel, fmt in cases.items():
            with self.subTest(rel):
                a = self._scan(EXTRACT / rel)[0]
                self.assertEqual(a.format, fmt, [(m.name, m.format, m.sniff_how) for m in a.members])
                self.assertTrue(a.sha256 and a.size >= 0)

    def test_mixed_dir_members_formats_and_unknown(self):
        a = self._scan(EXTRACT / "mixed_dir")[0]
        exp = EXPECTED_EXTRACT["mixed_dir"]
        self.assertEqual({m.name: m.format for m in a.members}, exp["formats"])
        self.assertEqual(sum(1 for m in a.members if not m.format), exp["members_unknown"])
        unknown = next(m for m in a.members if m.name == "unknown.bin")
        self.assertTrue(unknown.sniff_how, "an unrecognised member says why")

    def test_wrong_extension_is_irrelevant(self):
        src = IMPORT / "chatgpt_conversations.json"
        dst = self.tmp / "renamed.txt"
        shutil.copy(src, dst)
        a = self._scan(dst)[0]
        self.assertEqual(a.format, "openai.chatgpt.export")
        noext = self._scan(IMPORT / "export_without_extension")[0]
        self.assertIsNotNone(noext.format)

    def test_zip_in_zip_is_opened(self):
        a = self._scan(EXPORTS / "nested_container.zip")[0]
        kinds = {m.kind for m in a.members}
        self.assertIn("container", kinds)
        self.assertTrue(any(m.format for m in a.members if m.kind != "container"), "members of the inner zip are sniffed")

    def test_unknown_provider_members_land_in_unknown(self):
        a = self._scan(EXPORTS / "unknown_provider.zip")[0]
        self.assertTrue(any(not m.format for m in a.members))
        for m in a.members:
            if not m.format:
                self.assertTrue(m.sniff_how)

    def test_import_fixtures_all_get_a_format(self):
        for p in sorted(IMPORT.iterdir()):
            if p.name in ("malformed.json",):
                continue
            with self.subTest(p.name):
                a = self._scan(p)[0]
                self.assertIsNotNone(a.format, [(m.name, m.sniff_how) for m in a.members])

    def test_known_formats_are_data_with_labels_and_absent(self):
        fmts = store().entries("formats")
        self.assertGreaterEqual(len(fmts), 13)
        for fid, f in fmts.items():
            with self.subTest(fid):
                self.assertEqual(f["schema"], "loom.extract.format/1")
                self.assertTrue(f["label"].get("pl") and f["label"].get("en"))
                self.assertIn("known_absent", f)
                self.assertIn("sniff", f)
