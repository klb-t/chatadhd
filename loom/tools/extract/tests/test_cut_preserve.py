"""Slice B — the format-preserving cut: byte-exact spans, ChatGPT/Claude/zip/dir/
file/JS-assignment containers, only used attachments in the source layout,
missing ones flagged, side members by the format's preserve table, and the
round trip: cut → re-parse (this tool and engine/importer.py) → the same
conversations and messages."""
import io
import json
import random
import zipfile

from _support import EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract.cut.common import Outputs
from loom.tools.extract.cut.preserve import cut_preserve
from loom.tools.extract.jsonspan import SpanError, filter_array, iter_spans
from loom.tools.extract.readers.textlike import _capturing_importer_class


def _view(units):
    return sorted((u.ext_id, u.title, len(u.messages), tuple(m.id for m in u.messages),
                   tuple(sorted((a.ref, a.status, a.member) for a in u.attachments))) for u in units)


class JsonSpanTest(ExtractTest):
    SAMPLES = [b'[{"a":1,"s":"x]\\"}{"}, {"b":[1,2,{"c":"]"}]},\n 3, "q\\\\", null, true]\n',
               b'\xef\xbb\xbf  [\n  {"k": "\xc5\x82\xc3\xb3d\xc5\xba"},\n  {"k": 2}\n]',
               b'{"x": {"conversations": 1}, "conversations": [{"a": "]"}, {"b": 2}], "z": [3]}',
               b'window.YTD.grok_chat_item.part0 = [ {"i": 1}, {"i": 2} ];\n', b' [ ] ', b'[]']

    def test_every_event_concatenated_is_the_input_for_any_chunk_size(self):
        rnd = random.Random(7)
        for src in self.SAMPLES:
            wrapper = "conversations" if src.startswith(b"{") else None
            lead = src.startswith(b"window")
            for chunk in [1, 2, 3, 5, 7, 64, 1 << 20] + [rnd.randint(1, 40) for _ in range(5)]:
                ev = list(iter_spans(io.BytesIO(src), wrapper=wrapper, lead=lead, chunk=chunk))
                self.assertEqual(b"".join(e[2] for e in ev), src, (src, chunk))
                elems = [e for e in ev if e[0] == "element"]
                for _, i, data in elems:
                    json.loads(data.decode("utf-8"))  # every element is one complete JSON value

    def test_filter_keeps_source_separators_and_errors_are_explicit(self):
        src = self.SAMPLES[0]
        out = io.BytesIO()
        r = filter_array(io.BytesIO(src), out, lambda i, d: i in (1, 2), chunk=4)
        self.assertEqual(r, {"kept": 2, "total": 6})
        self.assertEqual(json.loads(out.getvalue()), [{"b": [1, 2, {"c": "]"}]}, 3])
        full = io.BytesIO()
        filter_array(io.BytesIO(src), full, lambda i, d: True)
        self.assertEqual(full.getvalue(), src, "everything kept = the input byte for byte")
        with self.assertRaises(SpanError):
            list(iter_spans(io.BytesIO(b'{"a": 1}')))
        with self.assertRaises(SpanError):
            list(iter_spans(io.BytesIO(b'[{"a": "unterminated')))


class PreserveTest(ExtractTest):
    def _cut(self, *sources, pick=None, redaction="none", attachments="copy_used"):
        from loom.tools.extract.redact import Redactor
        eng = self.engine(*sources)
        eng.ensure_units()
        convs = eng.conversations
        chosen = [u for u in convs if pick(u)] if pick else convs
        out = Outputs(self.tmp / "pkg")
        res = cut_preserve(eng, out, {u.uid for u in chosen}, settings={}, redactor=Redactor(redaction),
                           attachments_mode=attachments)
        return eng, chosen, out, res

    def _reparse(self, out):
        roots = sorted({"/".join(o["path"].split("/")[:2]) for o in out.items})
        eng = self.engine(*[self.tmp / "pkg" / r for r in roots])
        eng.ensure_units()
        return eng

    def test_everything_selected_is_byte_identical(self):
        eng, chosen, out, res = self._cut(EXPORTS / "openai_single_conversations.json")
        self.assertEqual((self.tmp / "pkg/preserve/openai_single_conversations.json").read_bytes(),
                         (EXPORTS / "openai_single_conversations.json").read_bytes())
        eng, chosen, out, res = self._cut(EXPORTS / "openai_2026_sharded.zip")
        src = zipfile.ZipFile(EXPORTS / "openai_2026_sharded.zip")
        dst = zipfile.ZipFile(self.tmp / "pkg/preserve/openai_2026_sharded.zip")
        for name in ("conversations-000.json", "conversations-001.json", "user.json"):
            self.assertEqual(dst.read(name), src.read(name), name)
            self.assertEqual(dst.getinfo(name).date_time, src.getinfo(name).date_time)
        self.assertTrue(res.byte_exact)

    def test_chatgpt_subset_reparses_to_the_same_conversations_and_attachments(self):
        eng, chosen, out, res = self._cut(EXPORTS / "openai_2026_sharded.zip", pick=lambda u: u.ext_id in ("c1-0000-aaaa", "c3-0000-cccc"))
        back = self._reparse(out)
        self.assertEqual(_view(back.conversations), _view(chosen))
        info = res.as_dict(False)["archives"][0]
        z = zipfile.ZipFile(self.tmp / "pkg/preserve/openai_2026_sharded.zip")
        names = set(z.namelist())
        used = {a.member for u in chosen for a in u.attachments if a.status == "resolved"}
        self.assertTrue(used and used <= names, "every used attachment is in the package, in the source layout")
        self.assertNotIn("file-Orphan9-old.png", names, "an attachment no kept conversation uses stays out")
        self.assertIn({"member": "file-Orphan9-old.png", "why": "attachment not used by a kept conversation"}, info["omitted"])
        missing = {m["ref"] for m in info["missing"]}
        self.assertTrue({"file-NoSuchId0", "file-Plot0001"} <= missing, "missing attachments are flagged, not dropped")
        self.assertNotIn("chat.html", names, "a member holding every conversation is left out with a reason")
        self.assertTrue(any(o["member"] == "chat.html" for o in info["omitted"]))
        fb = json.loads(z.read("message_feedback.json"))
        self.assertTrue(fb and all(r["conversation_id"] in ("c1-0000-aaaa", "c3-0000-cccc") for r in fb))
        self.assertIn("textdocs/7f3c9a10-0000-4000-8000-000000000001.json", names, "a record of a kept conversation is kept")

    def test_engine_importer_sees_the_same_messages(self):
        # engine/importer.py reads a plain array of mapping conversations (conversations-001.json); the wrapper form
        # {"conversations": [...]} of conversations-000.json it cannot read at all, in the source or in a cut
        member = "conversations-001.json"
        eng, chosen, out, res = self._cut(EXPORTS / "openai_2026_sharded.zip",
                                          pick=lambda u: (u.source or {}).get("member") == member and len(u.messages) > 0)
        self.assertGreaterEqual(len(chosen), 2)
        imp = _capturing_importer_class()()
        imp.import_file(str(self.tmp / "pkg/preserve/openai_2026_sharded.zip"))
        got = sorted(tuple((m["role"], m["content"]) for m in msgs) for msgs, title in imp.captured if title == member)
        ids = {u.ext_id for u in chosen}
        expected = []
        for conv in json.loads(zipfile.ZipFile(EXPORTS / "openai_2026_sharded.zip").read(member)):
            if isinstance(conv, dict) and (conv.get("conversation_id") in ids or conv.get("id") in ids):
                ref = _capturing_importer_class()()
                ref._import_json_data([conv])
                expected += [tuple((m["role"], m["content"]) for m in msgs) for msgs, _ in ref.captured]
        self.assertEqual(got, sorted(expected))
        self.assertTrue(got)

    def test_claude_claude_code_grok_and_gemini_round_trip(self):
        cases = [(EXPORTS / "anthropic_2026_full.zip", lambda u: u.title == "Widok listy"),
                 (EXTRACT / "claude_code/projects", lambda u: True if u.ext_id is None else u.ext_id.endswith("1")),
                 (EXTRACT / "grok/x_archive.zip", None),
                 (EXTRACT / "gemini/takeout.zip", None)]
        for src, pick in cases:
            with self.subTest(src=src.name):
                import shutil
                shutil.rmtree(self.tmp / "pkg", ignore_errors=True)
                eng, chosen, out, res = self._cut(src, pick=pick or (lambda u, _f=[]: not _f and not _f.append(1)))
                self.assertTrue(chosen)
                back = self._reparse(out)
                self.assertEqual(_view(back.conversations), _view(chosen))

    def test_redaction_reaches_side_members_and_is_reported_not_byte_exact(self):
        eng, chosen, out, res = self._cut(EXPORTS / "openai_2026_sharded.zip", pick=lambda u: u.ext_id == "c1-0000-aaaa",
                                          redaction="personal")
        z = zipfile.ZipFile(self.tmp / "pkg/preserve/openai_2026_sharded.zip")
        user = z.read("user.json").decode("utf-8")
        self.assertNotIn("user@example.invalid", user)
        self.assertIn("[EMAIL#", user)
        self.assertFalse(res.byte_exact)
        back = self._reparse(out)
        self.assertEqual([u.ext_id for u in back.conversations], ["c1-0000-aaaa"])
