"""Slice B — restructuring profiles as data (branches, blocks, fields, outputs),
times (zone, shift, anonymise, format), redaction with a report and no values
anywhere, statistics."""
import csv
import io
import json

from _support import EXPORTS, ExtractTest
from loom.tools.extract.cut.profiles import check_profile
from loom.tools.extract.cut.views import choose_paths
from loom.tools.extract.datafiles import store
from loom.tools.extract.redact import Redactor
from loom.tools.extract.timestamps import TimePolicy, from_iso


class ProfilesTest(ExtractTest):
    def _pkg(self, profiles, **cut):
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        r = eng.op("cut", {"profiles": profiles, **cut})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        return eng, r, self.tmp / "pkg"

    def test_shipped_profiles_are_valid_data(self):
        profs = store().entries("profiles")
        self.assertTrue({"preserve", "flat", "structure", "reasoning", "text", "graph", "table", "timeline", "cards", "bundle"} <= set(profs))
        for pid, prof in profs.items():
            self.assertEqual(check_profile(prof), [], pid)
        bad = {"schema": "loom.extract.profile/1", "id": "x", "branches": {"keep": "sideways"}, "blocks": {"text": "shred"},
               "outputs": [{"kind": "pdf"}]}
        self.assertEqual(len(check_profile(bad)), 4)

    def test_branch_policies_on_a_forked_conversation(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip")
        c1 = next(u for u in units if u.ext_id == "c1-0000-aaaa")
        self.assertEqual(c1.leaf_count(), 4)
        tree, info = choose_paths(c1, "tree")
        self.assertEqual(len(tree[0]), len(c1.messages))
        cur, info = choose_paths(c1, "current")
        self.assertEqual([m.id for m in cur[0]], [m.id for m in c1.messages if m.on_current_path])
        self.assertEqual(info["branches_cut"], 3)
        self.assertEqual(info["messages_cut"], len(c1.messages) - len(cur[0]))
        allp, _ = choose_paths(c1, "all_paths")
        self.assertEqual(len(allp), 4)
        longest, _ = choose_paths(c1, "longest")
        self.assertEqual(len(longest[0]), max(len(p) for p in allp))
        newest, _ = choose_paths(c1, "newest")
        self.assertEqual(newest[0][-1].created_utc, max(p[-1].created_utc or "" for p in allp))
        marked, info = choose_paths(c1, "marked", allp[1][-1].id)
        self.assertEqual([m.id for m in marked[0]], [m.id for m in allp[1]])

    def test_flat_structure_reasoning_table_text_graph(self):
        eng, r, pkg = self._pkg(["flat", "structure", "reasoning", "table", "text", "graph"])
        convs = [json.loads(x) for x in (pkg / "flat/conversations.jsonl").read_text(encoding="utf-8").splitlines()]
        c1 = next(c for c in convs if c["title"] == "Migracja bazy danych")
        self.assertIn("wycięto 3 gałęzi", c1["branch_note"], "cut branches are noted, never silent")
        msgs = [json.loads(x) for x in (pkg / "flat/messages.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual(set(msgs[0]), {"conversation", "id", "role", "created", "text", "attachments", "notes"})
        self.assertTrue(all(m["text"] or m["attachments"] or m["notes"] for m in msgs), "flat drops empty messages, notes what it omits")
        struct = json.loads((pkg / "structure/structure.json").read_text(encoding="utf-8"))
        self.assertFalse(any("text" in m for c in struct["items"] for m in c["messages"]), "structure carries no content")
        self.assertTrue(any(c["forks"] for c in struct["items"]))
        blocks = [json.loads(x) for x in (pkg / "reasoning/blocks.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertTrue(blocks)
        self.assertTrue({b["kind"] for b in blocks} <= {"thinking", "tool_use", "tool_result", "execution_output", "code"})
        rows = list(csv.reader(io.StringIO((pkg / "table/messages.csv").read_text(encoding="utf-8"))))
        self.assertEqual(rows[0], store().entry("profiles", "table")["fields"]["message"])
        md = (pkg / "text/0003-48b774da.md").read_text(encoding="utf-8") if (pkg / "text/0003-48b774da.md").exists() else \
            next(p.read_text(encoding="utf-8") for p in (pkg / "text").glob("*.md") if "Obrazy" in p.read_text(encoding="utf-8"))
        link = md.split("](", 1)[1].split(")", 1)[0]
        self.assertTrue((pkg / "text" / link).resolve().exists(), "attachment links are relative and point into the package")
        self.assertIn("file-NoSuchId0", md + "".join(p.read_text(encoding="utf-8") for p in (pkg / "text").glob("*.md")))
        index = (pkg / "text/INDEX.md").read_text(encoding="utf-8")
        self.assertIn("Migracja bazy danych", index)
        packet = json.loads((pkg / "graph/packet.json").read_text(encoding="utf-8"))
        preds = {c["predicate"] for c in packet["claims"]}
        self.assertTrue({"part_of", "reply_to", "derived_from", "uses_tool", "mentions"} <= preds)
        back = self.engine(pkg / "graph/packet.json")
        back.ensure_units()
        self.assertEqual(len(back.units[0].messages), len(packet["claims"]), "the packet re-reads as our graph format")
        man = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual({o["profile"] for o in man["outputs"] if o.get("profile")} - {"attachments"},
                         {"flat", "structure", "reasoning", "table", "text", "graph"})

    def test_timeline_cards_bundle_and_custom_profile(self):
        custom = {"schema": "loom.extract.profile/1", "id": "mine", "kind": "restructure", "branches": {"keep": "longest"},
                  "fields": {"message": ["conversation", "role", "text"]}, "blocks": {"text": "keep", "*": "omit"},
                  "outputs": [{"kind": "csv", "path": "mine/rows.csv", "rows": "messages"}]}
        eng, r, pkg = self._pkg(["timeline", "cards", "bundle", custom])
        rows = [json.loads(x) for x in (pkg / "timeline/timeline.jsonl").read_text(encoding="utf-8").splitlines()]
        stamped = [x["t"] for x in rows if x.get("t")]
        self.assertEqual(stamped, sorted(stamped))
        self.assertTrue(any(x["kind"] == "gap" for x in rows))
        self.assertIn("## 2025-10-09", (pkg / "timeline/DZIENNIK.md").read_text(encoding="utf-8"))
        cards = json.loads((pkg / "cards/cards.json").read_text(encoding="utf-8"))["cards"]
        self.assertTrue(all(c["summary"] is None and c["note"] for c in cards), "no summary without a model, said so")
        import zipfile
        names = zipfile.ZipFile(pkg / "bundle.zip").namelist()
        self.assertIn("bundle/messages.jsonl", names)
        self.assertTrue(any(n.startswith("attachments/") for n in names))
        self.assertEqual((pkg / "mine/rows.csv").read_text(encoding="utf-8").splitlines()[0], "conversation,role,text")

    def test_time_policy_zone_shift_anonymise_format(self):
        tp = TimePolicy({"zone": "source"})
        self.assertEqual(tp.render("2025-10-09T10:00:00+02:00", "2025-10-09T08:00:00Z"), "2025-10-09T10:00:00+02:00", "source keeps the native")
        self.assertEqual(tp.render(1760000000.0, "2025-10-09T08:53:20Z"), "2025-10-09T08:53:20Z", "epoch → ISO UTC")
        self.assertEqual(TimePolicy({"zone": "+02:00"}).render(None, "2025-10-09T08:00:00Z"), "2025-10-09T10:00:00+02:00")
        self.assertEqual(TimePolicy({"shift": "-1d"}).render(None, "2025-10-09T08:00:00Z"), "2025-10-08T08:00:00Z")
        an = TimePolicy({"anonymise": "package"})
        an.origin = from_iso("2025-10-09T08:00:00Z")
        self.assertEqual(an.render(None, "2025-10-09T09:30:00Z"), "1970-01-01T01:30:00Z")
        self.assertEqual(TimePolicy({"format": "epoch"}).render(None, "1970-01-02T00:00:00Z"), 86400)
        self.assertEqual(TimePolicy({"format": "date", "granularity": "month"}).render(None, "2025-10-09T08:00:00Z"), "2025-10-01")
        with self.assertRaises(ValueError):
            TimePolicy({"zone": "Mars/Olympus"})
        eng, r, pkg = self._pkg(["flat"], anonymise="package")
        msgs = [json.loads(x) for x in (pkg / "flat/messages.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual(min(m["created"] for m in msgs if m["created"]), "1970-01-01T00:00:00Z")


class RedactionTest(ExtractTest):
    TEXT = ("Napisz do jan.kowalski@example.com albo +48 600 700 800, IBAN PL61109010140000071219812874, klucz "
            "sk-proj-abcdefghijklmnopqrstu, karta 4111 1111 1111 1111 i 4111 1111 1111 1112, IP 10.0.0.1, ul. Długa 12/3, "
            "PESEL 44051401359 i 44051401358, password: hunter22x, znowu jan.kowalski@example.com; Łukasza Nowaka pytałem")

    def test_masks_dictionary_checks_and_consistency(self):
        r = Redactor("personal", terms=["Łukasz Nowak"])
        out = r.text(self.TEXT, {"c": 1})
        for secret in ("jan.kowalski@example.com", "600 700 800", "PL6110901014", "sk-proj-", "4111 1111 1111 1111", "10.0.0.1",
                       "Długa 12", "44051401359", "hunter22x", "Łukasza Nowaka"):
            self.assertNotIn(secret, out, secret)
        self.assertEqual(out.count("[EMAIL#1]"), 2, "the same value gets the same mask")
        self.assertIn("4111 1111 1111 1112", out, "a number failing the Luhn check is not a card")
        self.assertIn("44051401358", out, "a number failing the PESEL check is not a PESEL")
        rep = r.report()
        self.assertEqual(rep["by_kind"]["EMAIL"], 2)
        self.assertNotIn("jan.kowalski", json.dumps(rep) + json.dumps(r.entries), "no value in the report or the entries")
        self.assertTrue(all({"where", "kind", "rule", "mask", "length"} <= set(e) for e in r.entries))
        self.assertFalse(Redactor("none").active)
        self.assertEqual(Redactor("secrets").text("a@b.com sk-ant-abcdefghijklmnopqrstuv", {}), "a@b.com [SECRET#1]")

    def test_redacted_package_has_no_values_and_a_report(self):
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("cut", {"profiles": ["preserve", "text", "table"], "redaction": "personal"})
        eng.op("handoff", {})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        pkg = self.tmp / "pkg"
        import zipfile
        blob = ""
        for p in pkg.rglob("*"):
            if p.is_file() and p.suffix in (".json", ".jsonl", ".md", ".csv"):
                blob += p.read_text(encoding="utf-8", errors="replace")
        for z in [p for p in pkg.rglob("*.zip") if p.is_file()]:
            for n in zipfile.ZipFile(z).namelist():
                if n.endswith((".json", ".md", ".jsonl")):
                    blob += zipfile.ZipFile(z).read(n).decode("utf-8", errors="replace")
        self.assertNotIn("user@example.invalid", blob)
        self.assertNotIn("+48000000000", blob)
        red = json.loads((pkg / "redaction.json").read_text(encoding="utf-8"))
        self.assertGreater(red["entries"], 0)
        self.assertIn("EMAIL", red["by_kind"])
        report = (pkg / "RAPORT.md").read_text(encoding="utf-8")
        self.assertIn("zamaskowano", report)
        self.assertIn("załączniki binarne nie są redagowane", report)


class StatsTest(ExtractTest):
    def test_stats_measured_from_the_package(self):
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        st = eng.op("stats", {})["stats"]
        self.assertEqual(st["total"]["conversations"], len(eng.selection.units))
        self.assertEqual(st["total"]["messages"], sum(len(eng.by_uid[u].messages) for u in eng.selection.units))
        self.assertEqual(sum(r["conversations"] for r in st["topic_timeline"]), st["total"]["conversations"])
        self.assertEqual(set(st["services"]), {"openai", "anthropic"})
        self.assertIn("python", st["tools"])


class AttachmentTextTest(ExtractTest):
    def test_text_attachments_are_embedded_on_request(self):
        import zipfile
        conv = [{"id": "cx", "conversation_id": "cx", "title": "Notatka", "create_time": 1760000000.0, "update_time": 1760000100.0,
                 "current_node": "m2", "mapping": {
                     "root": {"id": "root", "message": None, "parent": None, "children": ["m1"]},
                     "m1": {"id": "m1", "parent": "root", "children": ["m2"], "message": {
                         "id": "m1", "author": {"role": "user"}, "create_time": 1760000001.0,
                         "content": {"content_type": "text", "parts": ["Zobacz notatkę"]},
                         "metadata": {"attachments": [{"id": "file-Note1", "name": "notatka.txt", "mime_type": "text/plain"}]}}},
                     "m2": {"id": "m2", "parent": "m1", "children": [], "message": {
                         "id": "m2", "author": {"role": "assistant"}, "create_time": 1760000002.0,
                         "content": {"content_type": "text", "parts": ["Przeczytane."]}}}}}]
        z = self.tmp / "export.zip"
        with zipfile.ZipFile(z, "w") as zf:
            zf.writestr("conversations.json", json.dumps(conv))
            zf.writestr("file-Note1-notatka.txt", "Treść notatki 42")
        for extract, expect in ((False, False), (True, True)):
            eng = self.engine(z)
            eng.op("select.expr", {"expr": "kind:conversation"})
            eng.op("cut", {"profiles": ["text"], "extract_text": extract})
            eng.op("write", {"package": str(self.tmp / f"p{int(extract)}")})
            md = "".join(p.read_text(encoding="utf-8") for p in (self.tmp / f"p{int(extract)}/text").glob("0*.md"))
            self.assertIn("notatka.txt", md)
            self.assertEqual("Treść notatki 42" in md, expect)
