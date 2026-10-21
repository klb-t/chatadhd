"""X-Port on the phone says in one plain Polish sentence what a ready recipe did or would do (preview / apply / undo), and
what an export contained.  The numbers are counted from the data (a message with the field, a block of the kind), never
from the engine's per-step "changed: N", which says nothing about which keys vanished."""
import json
import re
import zipfile
from collections import Counter
from pathlib import Path

from _support import EXPORTS, ExtractTest

from loom.tools.extract import effects, plural
from loom.tools.extract.datafiles import store
from test_xport_labels import load_entry

PL_KT = Path(__file__).resolve().parents[3] / "android" / "xport" / "src" / "main" / "java" / "com" / "chatadhd" / "xport" / "Pl.kt"
SOURCES = [str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")]


def snap(*rows):
    """A hand-made snapshot: rows of (message id, filled keys, blocks per kind); keys 'x' = field, 'meta.x' = meta key."""
    out = {}
    for mid, keys, blocks in rows:
        ks = frozenset(("m", k[5:]) if k.startswith("meta.") else ("f", k) for k in keys)
        out[("u:1", mid)] = (ks, Counter(blocks))
    return out


class PluralTest(ExtractTest):
    def test_the_rule_and_the_nouns_are_the_ones_of_the_phone_app(self):
        for n, want in ((0, "many"), (1, "one"), (2, "few"), (4, "few"), (5, "many"), (12, "many"), (14, "many"), (21, "many"),
                        (22, "few"), (24, "few"), (25, "many"), (101, "many"), (112, "many"), (122, "few")):
            self.assertEqual(plural.category(n), want, n)
        self.assertEqual([plural.count("message", n, "pl") for n in (1, 2, 5)], ["1 wiadomość", "2 wiadomości", "5 wiadomości"])
        self.assertEqual([plural.count("message_loc", n, "pl") for n in (1, 2, 5)], ["1 wiadomości", "2 wiadomościach", "5 wiadomościach"])
        # Pl.kt `Nouns` and ui_labels.json#nouns are two copies of the same words: they must not drift apart
        kt = {m.group(1): m.groups()[1:] for m in re.finditer(r'val (\w+) = Forms\("([^"]+)", "([^"]+)", "([^"]+)"\)', PL_KT.read_text(encoding="utf-8"))}
        data = store().file("ui_labels.json")["nouns"]
        shared = {"CONVERSATION": "conversation", "MESSAGE": "message", "FILE": "file"}
        for k, key in shared.items():
            f = data[key]["pl"]
            self.assertEqual((f["one"], f["few"], f["many"]), kt[k], key)

    def test_every_noun_has_all_three_forms_in_both_languages(self):
        for key, row in store().file("ui_labels.json")["nouns"].items():
            for lang in ("pl", "en"):
                self.assertEqual(set(row[lang]), {"one", "few", "many"}, (key, lang))


class SentenceTest(ExtractTest):
    def test_the_owners_example_shape_from_hand_made_snapshots(self):
        base = ["role", "created", "created_utc", "author", "model"]
        before = snap(*[(f"m{i}", base + ["meta.status"] * (i % 2), {"text": 1}) for i in range(4)], ("m9", ["role"], {"text": 1}))
        after = snap(*[(f"m{i}", ["role", "created", "created_utc"], {"text": 1}) for i in range(4)], ("m9", ["role"], {"text": 1}))
        d = effects.diff(before, after)
        self.assertEqual((d["changed"], d["messages"]), (4, 5), "the message that did not change is not counted")
        self.assertEqual(sorted(d["lost"].items()), [(("f", "author"), 4), (("f", "model"), 4), (("m", "status"), 2)])
        self.assertEqual(effects.sentence("preview", d, lang="pl"),
                         "Podgląd — nic jeszcze nie zmieniono. Po zastosowaniu: z 4 wiadomości zniknęłoby: 4× author, 4× model, 2× status; "
                         "zostałyby: role, created, created_utc.")
        self.assertEqual(effects.sentence("apply", d, lang="pl"),
                         "Zastosowano. W 4 wiadomościach usunięto: 4× author, 4× model, 2× status; zostały: role, created, created_utc. "
                         "Można cofnąć przyciskiem Cofnij przepis.")
        back = effects.diff(after, before)
        self.assertEqual(effects.sentence("undo", back, lang="pl"),
                         "Cofnięto: w 4 wiadomościach przywrócono 4× author, 4× model, 2× status.")

    def test_limit_is_a_preset_and_zero_lists_everything(self):
        keys = ["role"] + [f"meta.k{i}" for i in range(9)]
        d = effects.diff(snap(("a", keys, {})), snap(("a", ["role"], {})))
        short = effects.sentence("apply", d, limit=3, lang="pl")
        self.assertIn("1× k0, 1× k1, 1× k2, …;", short)
        full = effects.sentence("apply", d, limit=0, lang="pl")
        self.assertIn("1× k8;", full)
        self.assertNotIn("…", full)
        self.assertEqual(store().defaults()["edits"]["effect_names"], 6)

    def test_blocks_whole_messages_and_the_singular_forms(self):
        before = snap(("a", ["role"], {"text": 2, "thinking": 1}), ("b", ["role"], {"text": 1}))
        after = snap(("a", ["role"], {"text": 2}))
        d = effects.diff(before, after)
        s = effects.sentence("apply", d, lang="pl")
        self.assertIn("W 1 wiadomości usunięto: bloki: 1× rozumowanie; 1 wiadomość;", s)
        self.assertEqual(effects.structured(d)["blocks_removed"], [["thinking", 1]])
        self.assertEqual(effects.structured(d)["messages_gone"], 1)

    def test_nothing_changed_is_said_and_why(self):
        s = snap(("a", ["role"], {"text": 1}))
        d = effects.diff(s, s)
        self.assertEqual(effects.sentence("preview", d, lang="pl", tree_changed=False),
                         "Podgląd — nic jeszcze nie zmieniono. Po zastosowaniu nic by się nie zmieniło w wiadomościach.")
        self.assertIn("Zmiana dotyczy czegoś innego", effects.sentence("apply", d, lang="pl", tree_changed=True))
        self.assertTrue(effects.sentence("undo", d, lang="pl", tree_changed=False).startswith("Cofnięto. Pola ani bloki"))
        empty = effects.diff({}, {})
        self.assertIn("Pakiet roboczy nie ma żadnych wiadomości", effects.sentence("preview", empty, lang="pl"))

    def test_steps_that_are_not_edits_are_said_to_be_unknown(self):
        d = effects.diff(snap(("a", ["role", "author"], {})), snap(("a", ["role"], {})))
        self.assertTrue(effects.sentence("preview", d, lang="pl", other_steps=2).endswith("Skutek 2 kroków innych niż edycje: nieznany przed zastosowaniem."))


class RecipeSentenceTest(ExtractTest):
    MACRO = "tylko_znaczniki_czasu"

    def _open(self):
        entry = load_entry()
        self.assertTrue(json.loads(entry.open_sources(json.dumps(SOURCES)))["ok"])
        self.assertTrue(json.loads(entry.select("all"))["ok"])
        return entry

    def _msgs(self, entry):
        return [m for e in entry._eng.ensure_package().entries for m in e.unit.messages]

    def _call(self, entry, **req):
        r = json.loads(entry.call(json.dumps(req)))
        self.assertTrue(r["ok"], r)
        return r

    def test_preview_apply_undo_say_different_things_with_counts_from_the_data(self):
        entry = self._open()
        try:
            msgs = self._msgs(entry)
            hit = [m for m in msgs if m.author or m.model or m.meta]
            n_author, n_model = sum(1 for m in msgs if m.author), sum(1 for m in msgs if m.model)
            meta_keys = Counter(k for m in msgs for k in m.meta)
            self.assertTrue(n_author and n_model and meta_keys, "the fixtures carry metadata")
            pkg = entry._eng.package
            log, redo = len(pkg.oplog), len(pkg.redo)

            prev = self._call(entry, call="macro", name=self.MACRO, preview=True)
            self.assertEqual((len(pkg.oplog), len(pkg.redo)), (log, redo), "a preview leaves no step behind")
            self.assertEqual(sum(1 for m in self._msgs(entry) if m.author), n_author, "a preview changes nothing")
            want = f"z {len(hit)} wiadomości zniknęłoby: "
            self.assertTrue(prev["summary_text"].startswith("Podgląd — nic jeszcze nie zmieniono. Po zastosowaniu: " + want), prev["summary_text"])
            self.assertIn(f"{n_author}× author", prev["summary_text"])
            self.assertIn(f"{n_model}× model", prev["summary_text"])
            top_key, top_n = meta_keys.most_common(1)[0]
            self.assertIn(f"{top_n}× {top_key}", prev["summary_text"])
            self.assertIn("zostałyby: ", prev["summary_text"])
            removed = dict((k, n) for k, n in prev["summary"]["removed"])
            self.assertEqual((removed["author"], removed["model"], removed[top_key]), (n_author, n_model, top_n))
            self.assertEqual(prev["summary"]["messages_changed"], len(hit))
            self.assertIn("results", prev, "the engine's own answer is still there for the details tree")

            done = self._call(entry, call="macro", name=self.MACRO)
            self.assertTrue(done["summary_text"].startswith(f"Zastosowano. W {len(hit)} wiadomościach usunięto: "), done["summary_text"])
            self.assertTrue(done["summary_text"].endswith("Można cofnąć przyciskiem Cofnij przepis."))
            self.assertEqual(done["summary"]["removed"], prev["summary"]["removed"], "apply did what the preview said")
            self.assertEqual(done["summary"]["kept"], prev["summary"]["kept"])

            undo = self._call(entry, call="undo", steps=done["steps"])
            self.assertTrue(undo["summary_text"].startswith(f"Cofnięto: w {len(hit)} wiadomościach przywrócono "), undo["summary_text"])
            self.assertEqual(undo["summary"]["added"], prev["summary"]["removed"], "undo restored what apply removed")
            self.assertEqual(len({prev["summary_text"], done["summary_text"], undo["summary_text"]}), 3)
            self.assertEqual(sum(1 for m in self._msgs(entry) if m.author), n_author)
        finally:
            entry.open_sources("[]")

    def test_the_other_recipe_counts_blocks(self):
        entry = self._open()
        try:
            msgs = self._msgs(entry)
            kinds = Counter(b.kind for m in msgs for b in m.blocks if b.kind in ("thinking", "tool_use", "tool_result"))
            self.assertTrue(kinds, "the fixtures carry reasoning or tool blocks")
            prev = self._call(entry, call="macro", name="bez_rozumowania_i_narzedzi", preview=True)
            self.assertIn("bloki: ", prev["summary_text"])
            got = {k: n for k, n in prev["summary"]["blocks_removed"]}
            self.assertEqual(got, dict(kinds))
            self.assertIn(f"{kinds['thinking']}× rozumowanie", prev["summary_text"])
        finally:
            entry.open_sources("[]")

    def test_a_recipe_before_anything_is_selected_says_the_package_is_empty(self):
        entry = load_entry()
        try:
            self.assertTrue(json.loads(entry.open_sources(json.dumps(SOURCES)))["ok"])
            prev = self._call(entry, call="macro", name=self.MACRO, preview=True)
            self.assertIn("Pakiet roboczy nie ma żadnych wiadomości", prev["summary_text"])
        finally:
            entry.open_sources("[]")

    def test_an_undo_with_nothing_to_undo_says_so(self):
        entry = self._open()
        try:
            r = self._call(entry, call="undo")
            self.assertEqual(r["summary_text"], "Nie było czego cofać — nic nie zmieniono.")
        finally:
            entry.open_sources("[]")


class ExportReportTest(ExtractTest):
    def _open(self, select=True):
        entry = load_entry()
        self.assertTrue(json.loads(entry.open_sources(json.dumps(SOURCES)))["ok"])
        if select:
            self.assertTrue(json.loads(entry.select("all"))["ok"])
        return entry

    def _export(self, entry, profiles="", **kw):
        z = self.tmp / "o.zip"
        r = json.loads(entry.export_selected(str(z), "", profiles, **kw))
        self.assertTrue(r["ok"], r)
        return r, zipfile.ZipFile(z).namelist()

    def test_the_answer_names_the_profiles_and_counts_the_directories(self):
        entry = self._open()
        try:
            r, names = self._export(entry, "katalog_rozmowy")
            self.assertEqual([p["id"] for p in r["profiles_used"]], ["katalog_rozmowy"])
            self.assertEqual(r["profiles_used"][0]["label"], "Wyciągnij spod zagnieżdżenia: katalog /[znacznik czasu]-[tytuł rozmowy]")
            n = len(entry._eng.package.entries)
            self.assertEqual(r["conversation_dirs"], n)
            self.assertEqual(len(r["top_level_dirs"]), n)
            self.assertEqual(r["files"], len(names))
            self.assertEqual(r["top_level_entries"], len({x.split("/", 1)[0] for x in names}))
            self.assertGreater(r["top_level_entries"], r["conversation_dirs"], "the report files sit beside the directories")
            # nothing ticked: the default profile; its directories are not conversation directories
            r0, names0 = self._export(entry, "")
            self.assertEqual(r0["profiles_used"], [])
            self.assertEqual(r0["conversation_dirs"], 0)
            self.assertTrue({"text", "preserve"} <= set(r0["top_level_dirs"]), r0["top_level_dirs"])
        finally:
            entry.open_sources("[]")

    def test_two_profiles_are_both_named(self):
        entry = self._open()
        try:
            r, _ = self._export(entry, "katalog_rozmowy,text")
            self.assertEqual([p["id"] for p in r["profiles_used"]], ["katalog_rozmowy", "text"])
            self.assertTrue(all(p["label"] and p["label"] != p["id"] for p in r["profiles_used"]))
        finally:
            entry.open_sources("[]")

    def test_a_package_made_before_the_selection_does_not_empty_the_export(self):
        """What S2 saw on the emulator: 12 files, all in the root, no directory.  A recipe previewed before anything was
        selected made the working package from the empty selection; the later selection never reached it, and the cut
        worked on an empty package (only the report files)."""
        entry = self._open(select=False)
        try:
            json.loads(entry.call(json.dumps({"call": "macro", "name": "tylko_znaczniki_czasu", "preview": True})))
            self.assertEqual(len(entry._eng.package.entries), 0)
            self.assertTrue(json.loads(entry.select("all"))["ok"])
            r, names = self._export(entry, "katalog_rozmowy")
            n = len(entry._eng.selection.units)
            self.assertEqual((r["package_added"], r["conversation_dirs"]), (n, n), r)
            self.assertGreater(len(names), 12)
        finally:
            entry.open_sources("[]")

    def test_conversations_deselected_after_the_package_was_made_are_left_out_and_not_lost(self):
        entry = self._open()
        try:
            entry.call(json.dumps({"call": "macro", "name": "tylko_znaczniki_czasu", "preview": True}))
            total = len(entry._eng.package.entries)
            keep = sorted(entry._eng.selection.units)[:3]
            for uid in list(entry._eng.selection.units):
                if uid not in keep:
                    del entry._eng.selection.units[uid]
            r, names = self._export(entry, "katalog_rozmowy")
            self.assertEqual((r["conversation_dirs"], r["package_left_out"], r["package_added"]), (3, total - 3, 0), r)
            self.assertEqual(len(entry._eng.package.entries), total, "the working package got its entries back")
        finally:
            entry.open_sources("[]")
