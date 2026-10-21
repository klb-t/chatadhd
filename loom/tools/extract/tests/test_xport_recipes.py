"""Two ready recipes on the phone, both as data: the macro "Zostaw w metadanych tylko znaczniki czasu" (Edycje) and the
profile "katalog /[znacznik czasu]-[tytuł rozmowy]" (Eksport), each with a Polish name and a one-line description in
the catalog the app builds its screens from.  Run on the synthetic export archives of the fixtures."""
import dataclasses
import json
import re
from pathlib import Path

from _support import EXPORTS, ExtractTest

from loom.tools.extract.cut.common import conversation_dirs, dir_title
from loom.tools.extract.datafiles import store
from loom.tools.extract.model import Message
from test_xport_labels import load_entry

PKG = Path(__file__).resolve().parents[1]
STAMPED = re.compile(r"^\d{4}-\d\d-\d\dT\d\d-\d\d-\d\d(Z|[+-]\d\d-\d\d)-.+$|^bez-daty-.+$")
SOURCES = [str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")]


class TimestampOnlyMacroTest(ExtractTest):
    MACRO = "tylko_znaczniki_czasu"

    def _open(self, entry):
        opened = json.loads(entry.open_sources(json.dumps(SOURCES)))
        self.assertTrue(opened["ok"], opened)
        sel = json.loads(entry.select("all"))
        self.assertTrue(sel["ok"], sel)

    def _messages(self, entry):
        return [m for e in entry._eng.ensure_package().entries for m in e.unit.messages]

    def test_dropped_fields_are_the_model_minus_structure_times_and_provenance(self):
        """Derived, not invented: the times are the date-typed message fields of the field catalogue (and their
        created_* companions in the model); the macro deletes exactly the remaining non-structural fields."""
        date_fields = {k for k, v in store().file("select_fields.json")["fields"]["message"].items() if v.get("type") == "date"}
        self.assertEqual(date_fields, {"created"})
        times = {f.name for f in dataclasses.fields(Message) if any(f.name == d or f.name.startswith(d + "_") for d in date_fields)}
        self.assertEqual(times, {"created", "created_utc", "created_how", "created_inferred"})
        structure = {"id", "parent", "role", "branch", "on_current_path", "blocks"}
        provenance = {"raw_pointer"}
        expected = {f.name for f in dataclasses.fields(Message)} - times - structure - provenance
        steps = store().entry("macros", self.MACRO)["steps"]
        self.assertEqual({s["params"]["field"] for s in steps}, expected)
        self.assertTrue(all(s["op"] == "edit.delete_field" and s["params"]["where"] == "message" for s in steps))

    def test_no_reader_puts_a_time_into_meta(self):
        """Why clearing `meta` keeps every timestamp: the readers never store one there."""
        keys = set()
        for f in (PKG / "readers").glob("*.py"):
            src = f.read_text(encoding="utf-8")
            keys |= set(re.findall(r"\.meta\[\"([^\"]+)\"\]", src))
            for tup in re.findall(r"for k in \(([^)]*)\):\s*\n\s+v = raw\.get\(k\)", src):
                keys |= set(re.findall(r"\"([^\"]+)\"", tup))
            keys |= set(re.findall(r"for key in \(([^)]*)\):\s*\n\s+v = getpath", src))
        self.assertTrue({"status", "usage", "sidechain"} <= keys, keys)  # the scan sees the real keys
        timey = re.compile(r"time|date|stamp|created|updated|_at$|^at$", re.I)
        self.assertEqual([k for k in sorted(keys) if timey.search(k)], [])

    def test_macro_leaves_only_times_and_structure_and_preview_changes_nothing(self):
        entry = load_entry()
        try:
            self._open(entry)
            before = [(m.id, m.role, m.created, m.created_utc, m.created_how, m.created_inferred, len(m.blocks), m.parent,
                       m.raw_pointer) for m in self._messages(entry)]
            self.assertTrue(any(m.meta or m.author or m.model for m in self._messages(entry)), "the fixtures carry metadata")
            prev = json.loads(entry.call(json.dumps({"call": "macro", "name": self.MACRO, "preview": True})))
            self.assertTrue(prev["ok"] and prev["preview"], prev)
            self.assertTrue(any(m.meta or m.author or m.model for m in self._messages(entry)), "preview must not change anything")
            done = json.loads(entry.call(json.dumps({"call": "macro", "name": self.MACRO})))
            self.assertTrue(done["ok"], done)
            self.assertEqual(done["steps"], 3)
            msgs = self._messages(entry)
            self.assertEqual([m.meta for m in msgs if m.meta], [])
            self.assertEqual([m.author for m in msgs if m.author], [])
            self.assertEqual([m.model for m in msgs if m.model], [])
            after = [(m.id, m.role, m.created, m.created_utc, m.created_how, m.created_inferred, len(m.blocks), m.parent,
                      m.raw_pointer) for m in msgs]
            self.assertEqual(after, before)
            self.assertTrue(any(b[3] for b in before))
            undo = json.loads(entry.call(json.dumps({"call": "undo", "steps": done["steps"]})))
            self.assertTrue(undo["ok"], undo)
            self.assertTrue(any(m.meta or m.author or m.model for m in self._messages(entry)), "undo of all steps restores")
        finally:
            entry.open_sources("[]")


class DirectoryLayoutTest(ExtractTest):
    def test_title_is_a_safe_file_name_that_keeps_polish_letters(self):
        self.assertEqual(dir_title('Plan: a/b\\c*d?"e<f>g|h'), "Plan_a_b_c_d_e_f_g_h")
        self.assertEqual(dir_title("  Zażółć gęślą jaźń  "), "Zażółć_gęślą_jaźń")
        self.assertEqual(dir_title(None), "bez_tytułu")
        self.assertEqual(dir_title("...//"), "bez_tytułu")
        self.assertEqual(dir_title("a" * 300, max_chars=0).encode().__len__() <= 255, True)
        self.assertEqual(len(dir_title("x" * 300, max_chars=80)), 80)

    def test_same_stamp_and_title_never_share_a_directory(self):
        class U:
            def __init__(self, uid): self.uid = uid
        class E:
            def __init__(self, key, uid): self.key, self.unit = key, U(uid)
        es = [E("0001-a", "u:aaaaaaaa11"), E("0002-b", "u:bbbbbbbb22"), E("0003-c", "u:cccccccc33")]
        got = conversation_dirs(es, lambda e: "2025-01-01T00-00-00Z", lambda e: "Ten sam" if e.key != "0003-c" else "TEN SAM")
        self.assertEqual(len(set(v.casefold() for v in got.values())), 3, got)
        self.assertEqual(got["0001-a"], "2025-01-01T00-00-00Z-Ten_sam")
        self.assertTrue(got["0002-b"].endswith("-bbbbbbbb"), got)

    def _cut(self, out="pkg", **cut):
        eng = self.engine(*[__import__("pathlib").Path(s) for s in SOURCES])
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("package.copy", {})
        eng.op("cut", dict({"profiles": ["katalog_rozmowy"]}, **cut))
        pkg = self.tmp / out
        eng.op("write", {"package": str(pkg)})
        return eng, pkg

    def test_every_conversation_is_a_directory_under_the_root_with_its_attachments_beside_the_file(self):
        eng, pkg = self._cut()
        dirs = sorted(p.name for p in pkg.iterdir() if p.is_dir())
        n = len(eng.package.entries)
        self.assertEqual(len(dirs), n)
        self.assertTrue(all(STAMPED.match(d) for d in dirs), dirs)
        self.assertFalse((pkg / "attachments").exists(), "attachments live in the conversation directories")
        for d in dirs:
            self.assertTrue((pkg / d / "rozmowa.md").is_file(), d)
        # a plain sort is the time order of the first messages
        first = {}
        for e in eng.package.entries:
            ms = [m.created_utc for m in e.unit.messages if m.created_utc] or [e.unit.created_utc]
            first[min(ms)] = e
        self.assertEqual(len(first), n)
        self.assertEqual([d[:19] for d in dirs], [k[:19].replace(":", "-") for k in sorted(first)])
        # the conversation with images: its files sit next to rozmowa.md and the links point at them
        images = next(d for d in dirs if "Obrazy" in d)
        beside = sorted(p.name for p in (pkg / images).iterdir() if p.name != "rozmowa.md")
        self.assertTrue(beside, "attachments next to the conversation file")
        md = (pkg / images / "rozmowa.md").read_text(encoding="utf-8")
        links = [x.split(")", 1)[0] for x in md.split("](")[1:]]
        local = [l for l in links if not l.startswith(("http", "#"))]
        self.assertTrue(local)
        for l in local:
            self.assertNotIn("/", l, l)
            self.assertTrue((pkg / images / l).is_file(), l)
        self.assertLessEqual(set(local), set(beside))

    def test_zone_of_the_run_moves_the_stamp_and_other_profiles_are_unchanged(self):
        _, utc = self._cut("pkg_utc")
        eng, plus = self._cut("pkg_plus", zone="+02:00")
        a = sorted(p.name for p in utc.iterdir() if p.is_dir())[0]
        b = sorted(p.name for p in plus.iterdir() if p.is_dir())[0]
        self.assertTrue(a[:10] == "2024-05-01" and "T08-00-00Z" in a, a)
        self.assertIn("T10-00-00+02-00", b)
        # a profile that does not ask for directories gets no "dir" field and writes where it always did
        eng2 = self.engine(*[__import__("pathlib").Path(s) for s in SOURCES])
        eng2.op("select.expr", {"expr": "kind:conversation"})
        eng2.op("package.copy", {})
        eng2.op("cut", {"profiles": ["text"]})
        self.assertEqual(eng2.ds.entry("profiles", "text").get("attachments_to"), None)
        self.assertTrue(all(o["path"].startswith(("text/", "attachments/")) for o in eng2.stage().items if o.get("profile") in ("text", "attachments")))


class CatalogTest(ExtractTest):
    def test_both_recipes_are_listed_with_polish_name_and_one_line_description(self):
        entry = load_entry()
        try:
            self.assertTrue(json.loads(entry.open_sources(json.dumps(SOURCES)))["ok"])
            cat = json.loads(entry.catalog())
            self.assertTrue(cat["ok"], cat)
            macros = {m["id"]: m for m in cat["macros"]}
            self.assertEqual(macros["tylko_znaczniki_czasu"]["label"], "Zostaw w metadanych tylko znaczniki czasu")
            profs = {p["id"]: p for p in cat["profiles"]}
            self.assertEqual(profs["katalog_rozmowy"]["label"], "Wyciągnij spod zagnieżdżenia: katalog /[znacznik czasu]-[tytuł rozmowy]")
            for row in list(macros.values()) + [profs["katalog_rozmowy"]]:
                self.assertTrue(row["description"].strip(), row["id"])
                self.assertNotIn("\n", row["description"])
            for m in macros.values():  # every shipped macro is listed with a description, not only the new one
                self.assertGreaterEqual(m["steps"], 1)
        finally:
            entry.open_sources("[]")

    def test_shipped_recipe_data_has_both_languages(self):
        for kind, ids in (("macros", ("tylko_znaczniki_czasu", "bez_rozumowania_i_narzedzi")), ("profiles", ("katalog_rozmowy",))):
            for i in ids:
                spec = store().entry(kind, i)
                for key in ("label", "description"):
                    self.assertTrue(spec[key].get("pl") and spec[key].get("en"), (kind, i, key))
