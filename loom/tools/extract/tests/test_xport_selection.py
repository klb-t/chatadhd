"""X-Port on the phone: the working package follows the selection in every call that works on it, the sentences about it
agree in Polish, files keep the names people gave them, and conversation directories have one shape."""
import json
import re
import shutil
from pathlib import Path

from _support import EXPORTS, EXTRACT, ExtractTest

from loom.tools.extract import plural
from loom.tools.extract.cut.common import archive_slugs, original_name
from loom.tools.extract.datafiles import store
from test_xport_labels import load_entry

SOURCES = [str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")]
MACRO = "tylko_znaczniki_czasu"
KT = Path(__file__).resolve().parents[3] / "android" / "xport" / "src" / "main" / "java" / "com" / "chatadhd" / "xport" / "MainActivity.kt"


class PackageFollowsSelectionTest(ExtractTest):
    def _open(self):
        entry = load_entry()
        self.assertTrue(json.loads(entry.open_sources(json.dumps(SOURCES)))["ok"])
        return entry

    def _call(self, entry, **req):
        r = json.loads(entry.call(json.dumps(req)))
        self.assertTrue(r["ok"], r)
        return r

    def _selected_messages(self, entry):
        eng = entry._eng
        return [m for u in eng.conversations if u.uid in eng.selection.units for m in u.messages]

    def test_preview_before_select_then_select_then_every_call_sees_the_messages(self):
        """What S2 saw: Podgląd przepisu before anything was selected, then Wybór -> Wszystko ('Zaznaczono: 5 rozmów, 15
        wiadomości'), then Podgląd / Zastosuj / Cofnij przepis said the package had no messages."""
        entry = self._open()
        try:
            empty = self._call(entry, call="macro", name=MACRO, preview=True)
            self.assertIn("Pakiet roboczy nie ma żadnych wiadomości", empty["summary_text"])
            self.assertEqual(empty["package_added"], 0)
            self.assertNotIn("package_added_text", empty)
            self.assertEqual(len(entry._eng.package.entries), 0)

            self.assertTrue(json.loads(entry.select("all"))["ok"])
            eng = entry._eng
            n_conv, n_msg = len(eng.selection.units), len(self._selected_messages(entry))
            self.assertGreater(n_conv, 3)  # the screen said 'Zaznaczono: N rozmów, M wiadomości'
            self.assertEqual(entry._eng.selection_summary()["messages"], n_msg)
            hit = [m for m in self._selected_messages(entry) if m.author or m.model or m.meta]
            self.assertTrue(hit)

            prev = self._call(entry, call="macro", name=MACRO, preview=True)
            self.assertEqual(prev["package_added"], n_conv)
            self.assertEqual(prev["package_added_text"], "do pakietu roboczego dołączono to, co zaznaczono po jego utworzeniu: "
                             f"{plural.count('conversation', n_conv, 'pl')} (bez wcześniejszych edycji)")
            self.assertNotIn("nie ma żadnych wiadomości", prev["summary_text"])
            self.assertIn(f"z {len(hit)} wiadomości zniknęłoby: ", prev["summary_text"])
            self.assertEqual(len(eng.package.entries), n_conv)
            log = len(eng.package.oplog)
            self.assertEqual(log, 0, "copying the selection in is not a step of the person (it cannot be 'undone' by Cofnij)")

            done = self._call(entry, call="macro", name=MACRO)
            self.assertEqual(done["package_added"], 0, "already in line: nothing more to add")
            self.assertNotIn("package_added_text", done)
            self.assertTrue(done["summary_text"].startswith(f"Zastosowano. W {len(hit)} wiadomościach usunięto: "), done["summary_text"])
            self.assertFalse(any(m.meta or m.author or m.model for e in eng.package.entries for m in e.unit.messages))

            undo = self._call(entry, call="undo", steps=done["steps"])
            self.assertTrue(undo["summary_text"].startswith(f"Cofnięto: w {len(hit)} wiadomościach przywrócono "), undo["summary_text"])
            self.assertEqual(undo["package_added"], 0)
            self.assertTrue(any(m.meta or m.author or m.model for e in eng.package.entries for m in e.unit.messages))
        finally:
            entry.open_sources("[]")

    def test_batch_edit_preview_and_apply_see_the_selection(self):
        entry = self._open()
        try:
            self._call(entry, call="macro", name=MACRO, preview=True)  # makes the empty package
            self.assertTrue(json.loads(entry.select("all"))["ok"])
            n_msg = len(self._selected_messages(entry))
            with_model = sum(1 for m in self._selected_messages(entry) if m.model)
            self.assertTrue(with_model)
            req = {"edit": "delete_field", "params": {"where": "message", "field": "model"}}
            prev = self._call(entry, call="edit", **req)
            self.assertEqual(prev["package_added"], len(entry._eng.selection.units))
            self.assertEqual((prev["count"], prev["lines"]), (n_msg, [f"{n_msg} × usunięto model"]), "the edit sees every selected message")
            self.assertEqual(sum(1 for e in entry._eng.package.entries for m in e.unit.messages if m.model), with_model, "a preview changes nothing")
            done = self._call(entry, call="apply", **req)
            self.assertEqual(done["package_added"], 0)
            self.assertEqual(sum(1 for e in entry._eng.package.entries for m in e.unit.messages if m.model), 0)
            # a conversation selected later is copied in whole; the edit made before stays on the others
            eng = entry._eng
            gone = sorted(eng.selection.units)[0]
            del eng.selection.units[gone]
            eng.package.entries = [e for e in eng.package.entries if e.unit.uid != gone]
            eng.selection.units[gone] = None
            again = self._call(entry, call="edit", **req)
            self.assertEqual(again["package_added"], 1)
            self.assertEqual(again["count"], n_msg, again)  # the conversation is back in the package, with all its messages
        finally:
            entry.open_sources("[]")

    def test_undo_restores_an_older_snapshot_and_the_selection_is_copied_in_after_it(self):
        entry = self._open()
        try:
            self.assertTrue(json.loads(entry.select("all", mode="add"))["ok"])
            eng = entry._eng
            uids = sorted(eng.selection.units)
            for uid in uids[2:]:
                del eng.selection.units[uid]
            done = self._call(entry, call="macro", name=MACRO)
            self.assertEqual((done["package_added"], len(eng.package.entries)), (0, 2))
            eng.selection.units.update({uid: None for uid in uids})  # three more conversations are selected
            undo = self._call(entry, call="undo", steps=done["steps"])
            more = len(uids) - 2
            self.assertEqual(undo["package_added"], more)
            self.assertEqual(len(eng.package.entries), len(uids), "the restore of the older snapshot did not take them away")
            self.assertEqual(len(eng.package.oplog), 0)
            self.assertIn(f": {plural.count('conversation', more, 'pl')} (bez", undo["package_added_text"])
            redo = self._call(entry, call="redo")
            self.assertEqual(redo["package_added"], 0)
            self.assertEqual(len(eng.package.entries), len(uids))
        finally:
            entry.open_sources("[]")

    def test_ops_and_the_browser_follow_the_selection_but_a_hand_made_package_edit_does_not(self):
        entry = self._open()
        try:
            eng = entry._eng
            self.assertEqual(entry._package_phase(eng, {"call": "op", "op": "package.remove"}), (None, False))
            self.assertEqual(entry._package_phase(eng, {"call": "op", "op": "package.copy"}), (None, False))
            self.assertEqual(entry._package_phase(eng, {"call": "op", "op": "select.clear"}), (None, False))
            self.assertEqual(entry._package_phase(eng, {"call": "op", "op": "edit.set"}), ("before", True))
            self.assertEqual(entry._package_phase(eng, {"call": "op", "op": "cut"}), ("before", True))
            self.assertEqual(entry._package_phase(eng, {"call": "select", "how": "all"}), (None, False))
            # looking at the package never makes one
            self.assertTrue(json.loads(entry.select("all"))["ok"])
            seen = self._call(entry, call="list", root="package", path=[], limit=0)
            self.assertEqual(seen["package_added"], 0)
            self.assertIsNone(eng.package)
            # ... but once a package exists, the browser shows it in line with the selection
            eng.selection.units.clear()
            self._call(entry, call="macro", name=MACRO, preview=True)
            self.assertEqual(len(eng.package.entries), 0)
            self.assertTrue(json.loads(entry.select("all"))["ok"])
            seen = self._call(entry, call="list", root="package", path=[], limit=0)
            n = len(eng.selection.units)
            self.assertEqual((seen["package_added"], len(eng.package.entries)), (n, n))
            self.assertEqual(seen["total"], n)
            # an operation of the Ops screen that changes the package is aligned too
            eng.package.entries = eng.package.entries[:2]
            ran = self._call(entry, call="op", op="edit.delete_field", params={"where": "message", "field": "model"})
            self.assertEqual(ran["package_added"], n - 2)
            self.assertEqual(len(eng.package.entries), n)
        finally:
            entry.open_sources("[]")


class PolishAgreementTest(ExtractTest):
    def test_the_clauses_need_no_adjective_that_would_have_to_agree_with_the_count(self):
        entry = load_entry()
        eng = type("E", (), {"ds": store()})()
        want = {1: "1 rozmowa", 2: "2 rozmowy", 4: "4 rozmowy", 5: "5 rozmów", 12: "12 rozmów", 22: "22 rozmowy", 25: "25 rozmów"}
        for n, count in want.items():
            for key in ("package_added", "package_left_out"):
                text = entry._package_note(eng, key, n)
                self.assertTrue(text.endswith(count) or text.endswith(count + " (bez wcześniejszych edycji)"), (n, text))
                self.assertNotRegex(text, r"zaznaczon(e|ych|ą|a)\b")  # no adjective that agrees with the counted noun
        for key in ("package_added", "package_left_out"):
            en = store().file("ui_labels.json")["effects"][key]["en"]
            self.assertIn("{count}", en)
        self.assertEqual(plural.count("conversation", 1, "pl"), "1 rozmowa")

    def test_the_phone_app_takes_the_clauses_from_the_engine(self):
        kt = KT.read_text(encoding="utf-8")
        self.assertNotIn("zaznaczone po jego utworzeniu", kt)
        self.assertNotIn("których już nie zaznaczono", kt)
        self.assertIn("package_added_text", kt)
        self.assertIn("package_left_out_text", kt)


class SourceNamesTest(ExtractTest):
    def _cut(self, files, profiles=("preserve", "text")):
        eng = self.engine(*files)
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("package.copy", {})
        eng.op("cut", {"profiles": list(profiles)})
        pkg = self.tmp / "pkg"
        eng.op("write", {"package": str(pkg)})
        return eng, pkg

    def test_the_prefix_is_stripped_in_one_place(self):
        self.assertEqual(original_name("1792554698851__xport-ours-conversation.json"), "xport-ours-conversation.json")
        self.assertEqual(original_name("plain.json"), "plain.json")
        self.assertEqual(original_name("12__34__a.zip"), "34__a.zip", "only the first prefix is the app's")
        self.assertEqual(original_name("1792554698851__x.zip!inner/y.zip"), "x.zip!inner/y.zip")
        self.assertEqual(original_name("1792554698851__a.json", ""), "1792554698851__a.json", "the pattern is a preset: empty = keep")
        self.assertEqual(original_name("1792554698851__", ), "1792554698851__", "a name that would become empty is kept")

    def test_output_names_are_the_original_names_unique_by_a_number_and_the_manifest_maps_them(self):
        a, b, c = self.tmp / "a", self.tmp / "b", self.tmp / "c"
        for d in (a, b, c):
            d.mkdir()
        f1 = a / "1792554698851__chat.json"
        f2 = b / "1792554700000__chat.json"
        f3 = c / "1792554711111__eksport.zip"
        shutil.copy(EXPORTS / "anthropic_single_chat.json", f1)
        shutil.copy(EXPORTS / "openai_single_conversations.json", f2)
        shutil.copy(EXPORTS / "anthropic_2026_full.zip", f3)
        eng, pkg = self._cut([f1, f2, f3])
        kept = sorted(p.relative_to(pkg).as_posix() for p in (pkg / "preserve").rglob("*") if p.is_file())
        self.assertTrue(kept)
        self.assertFalse([k for k in kept if re.search(r"/\d{8,}__", "/" + k.split("/", 1)[1])], kept)
        self.assertIn("preserve/chat.json", kept)
        self.assertIn("preserve/chat-2.json", kept)
        self.assertTrue(any(k.startswith("preserve/eksport.zip") for k in kept), kept)
        man = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
        rows = {s["label"]: s for s in man["sources"]}
        self.assertEqual(rows[f1.name]["original_name"], "chat.json")
        self.assertEqual(rows[f2.name]["original_name"], "chat.json")
        self.assertEqual({rows[f1.name]["output_name"], rows[f2.name]["output_name"]}, {"chat.json", "chat-2.json"})
        self.assertEqual(rows[f3.name]["original_name"], "eksport.zip")
        self.assertEqual(rows[f3.name]["output_name"], "eksport.zip")
        for s in man["sources"]:
            self.assertEqual(s["output_name"], archive_slugs(eng.archives, eng.ds)[(s["label"], s["sha256"])])

    def test_a_file_without_the_prefix_keeps_its_name(self):
        f = self.tmp / "moja-rozmowa.json"
        shutil.copy(EXPORTS / "anthropic_single_chat.json", f)
        _, pkg = self._cut([f])
        self.assertTrue((pkg / "preserve" / "moja-rozmowa.json").is_file())


class DirectoryNameShapeTest(ExtractTest):
    SHAPE = re.compile(r"^\d{4}-\d\d-\d\dT\d\d-\d\d-\d\dZ-.+$|^bez-daty-.+$")

    def _dirs(self, files, **cut):
        eng = self.engine(*files)
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("package.copy", {})
        eng.op("cut", dict({"profiles": ["katalog_rozmowy"]}, **cut))
        pkg = self.tmp / "pkg"
        eng.op("write", {"package": str(pkg)})
        return eng, sorted(p.name for p in pkg.iterdir() if p.is_dir())

    def test_fractions_of_a_second_never_reach_a_directory_name(self):
        takeout = EXTRACT / "gemini" / "takeout.zip"
        eng, dirs = self._dirs([takeout, *SOURCES])
        fractional = [e for e in eng.package.entries if any((m.created_utc or "").find(".") > 0 for m in e.unit.messages)
                      or "." in (e.unit.created_utc or "")]
        self.assertTrue(fractional, "the takeout fixture has a conversation that starts at 09:00:00.123")
        self.assertTrue(all(self.SHAPE.match(d) for d in dirs), dirs)
        self.assertTrue(any(d.startswith("2026-03-01T09-00-00Z-") for d in dirs), dirs)
        self.assertFalse([d for d in dirs if re.search(r"\d\.\d", d[:30])], dirs)
        self.assertEqual(len({d.casefold() for d in dirs}), len(dirs))

    def test_the_note_of_the_profile_tells_it(self):
        note = store().entry("profiles", "katalog_rozmowy")["note"]["pl"]
        self.assertIn("ułamki sekundy są zawsze pomijane", note)
        self.assertIn("z dokładnością do sekundy", note)

    def test_a_zone_keeps_the_shape_without_the_fraction(self):
        _, dirs = self._dirs([EXTRACT / "gemini" / "takeout.zip"], zone="+02:00")
        self.assertTrue(all(re.match(r"^\d{4}-\d\d-\d\dT\d\d-\d\d-\d\d(Z|[+-]\d\d-\d\d)-", d) for d in dirs), dirs)
        self.assertTrue(any("T11-00-00+02-00-" in d for d in dirs), dirs)
