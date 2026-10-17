"""Format recognition from CONTENT: every fixture format, a wrong extension, zip in zip, unknown members."""
import json
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


class DamagedAndUserReaderTest(ExtractTest):
    def test_a_damaged_member_is_reported_and_the_rest_is_read(self):
        """F40: one member with a CRC error does not abort the scan: it is 'unreadable' with the reason, in the
        inventory's unknown list and in RAPORT, and every other member is read."""
        from _support import EXPECTED_EXTRACT
        src = EXTRACT / "damaged" / "chatgpt_damaged.zip"
        exp = EXPECTED_EXTRACT["damaged/chatgpt_damaged.zip"]
        eng = self.engine(src)
        eng.op("scan", {})
        bad = [m for a in eng.archives for m in a.members if m.kind == "unreadable"]
        self.assertEqual([m.name for m in bad], exp["unreadable"])
        self.assertIn("CRC", bad[0].sniff_how)
        self.assertTrue(any(ev["kind"] == "note" and "uszkodzonych" in str(ev.get("text")) for ev in eng.events))
        eng.ensure_units()
        self.assertEqual(len([u for u in eng.units if u.kind == "conversation"]), exp["conversations"])
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("cut", {})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        inv = json.loads((self.tmp / "pkg/inventory.json").read_text(encoding="utf-8"))
        names = [m["name"] for a in inv["archives"] for m in a["members"]["unknown_list"]]
        self.assertIn(exp["unreadable"][0], names)
        rep = (self.tmp / "pkg/RAPORT.md").read_text(encoding="utf-8")
        self.assertIn("uszkodzony, nie da się odczytać", rep)

    def test_user_reader_registered_by_a_data_entry(self):
        """F9 (spec §8 row 35, §8.1 #52): a new parser = one module + a format entry with reader "user:<name>",
        run only with the face's grant; an unknown reader is an 'unknown' unit with the reason, not a crash."""
        ov = store().overlay
        self.write(ov / "formats" / "my_chats.json", json.dumps({
            "schema": "loom.extract.format/1", "id": "user.my_chats", "vendor": "me", "product": "notes", "service": "me",
            "label": {"pl": "Moje czaty", "en": "My chats"}, "priority": 1, "containers": ["json"],
            "sniff": {"any_of": [{"json_object_keys_all": ["my_chats"]}]}, "reader": "user:my_chats", "members": {"*": "conversations"}}))
        self.write(ov / "readers" / "my_chats.py",
                   "import json\n"
                   "from loom.tools.extract.model import Block, Message, Unit\n"
                   "from loom.tools.extract.readers.base import finalize_unit\n"
                   "def iter_units(member, fmt, ctx):\n"
                   "    data = json.loads(member.read_bytes())\n"
                   "    for i, c in enumerate(data['my_chats']):\n"
                   "        u = Unit(kind='conversation', ext_id=c['id'], title=c['title'])\n"
                   "        prev = None\n"
                   "        for j, t in enumerate(c['lines']):\n"
                   "            m = Message(id=f'{i}-{j}', parent=prev, role='user' if j % 2 == 0 else 'assistant')\n"
                   "            m.blocks.append(Block(kind='text', text=t))\n"
                   "            u.messages.append(m)\n"
                   "            prev = m.id\n"
                   "        yield finalize_unit(u, member, fmt, ctx, {'json_pointer': f'/my_chats/{i}'})\n")
        src = self.write(self.tmp / "src" / "chats.json", json.dumps({"my_chats": [{"id": "a", "title": "Moja rozmowa", "lines": ["pyt", "odp"]}]}))
        eng = self.engine(src)
        eng.ensure_units()
        self.assertEqual([u.kind for u in eng.units], ["unknown"], "without the grant the user's reader does not run")
        self.assertIn("kod-uzytkownika", json.dumps([u.unknown for u in eng.units], ensure_ascii=False))
        eng2 = self.engine()
        eng2.settings["allow_user_code"] = True
        eng2.set_sources([str(src)])
        eng2.ensure_units()
        convs = [u for u in eng2.units if u.kind == "conversation"]
        self.assertEqual([u.title for u in convs], ["Moja rozmowa"])
        eng2.op("select.expr", {"expr": "kind:conversation"})
        eng2.op("write", {"package": str(self.tmp / "pkg")})
        self.assertIn("user:my_chats", (self.tmp / "pkg/RAPORT.md").read_text(encoding="utf-8"))
        # an overlay format naming a reader that does not exist: an unknown unit with the reason, the run goes on
        fmt = json.loads((ov / "formats" / "my_chats.json").read_text(encoding="utf-8"))
        fmt["reader"] = "no_such_reader"
        (ov / "formats" / "my_chats.json").write_text(json.dumps(fmt), encoding="utf-8")
        from loom.tools.extract.datafiles import DataStore, set_store
        set_store(DataStore(overlay=ov))
        eng3 = self.engine(src, EXPORTS / "anthropic_2026_full.zip")
        eng3.ensure_units()
        self.assertTrue(any(u.kind == "unknown" and "no_such_reader" in json.dumps(u.unknown) for u in eng3.units))
        self.assertTrue(any(u.kind == "conversation" for u in eng3.units), "the other sources are read")
