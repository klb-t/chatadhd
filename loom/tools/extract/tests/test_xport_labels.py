"""X-Port on the phone shows Polish labels for the names the engine speaks in (parameter names, enum values, parameter
types, node kinds, viewer formats, roots): the label file covers every name the shipped operations and edits use, the
catalog the app builds its forms from carries them next to the engine key (the key is what is sent back), and a name
without a label is shown as it is, never dropped."""
import glob
import importlib.util
import json
import unittest
from pathlib import Path

from _support import EXPORTS, ExtractTest

from loom.tools.extract.datafiles import store

PKG = Path(__file__).resolve().parents[1]
ENTRY = PKG.parents[1] / "android" / "xport" / "src" / "main" / "python" / "xport_entry.py"


def load_entry():
    spec = importlib.util.spec_from_file_location("xport_entry_under_test", ENTRY)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def shipped_params():
    """Every parameter spec of the shipped operations and edits: (name, spec)."""
    out = []
    ops = json.loads((PKG / "data" / "ops.json").read_text(encoding="utf-8"))["ops"]
    for v in ops.values():
        out.extend((v.get("params") or {}).items())
    for f in glob.glob(str(PKG / "data" / "edits" / "*.json")):
        out.extend((json.loads(Path(f).read_text(encoding="utf-8")).get("params") or {}).items())
    return [(n, p if isinstance(p, dict) else {"type": str(p)}) for n, p in out]


class UiLabelsTest(ExtractTest):
    def test_every_shipped_name_has_a_polish_and_an_english_label(self):
        ui = store().file("ui_labels.json")
        names = {n for n, _ in shipped_params()}
        values = {str(v) for _, p in shipped_params() for v in p.get("values") or []}
        types = {p.get("type") or "string" for _, p in shipped_params()}
        for table, need in (("params", names), ("values", values), ("types", types)):
            self.assertEqual(sorted(need - set(ui[table])), [], f"{table} without a label in data/ui_labels.json")
        for table in ("params", "values", "types", "kinds", "formats"):
            for key, lab in ui[table].items():
                self.assertTrue(lab.get("pl") and lab.get("en"), f"{table}.{key}: both languages")

    def test_the_labels_named_by_the_owner(self):
        ui = store().file("ui_labels.json")["params"]
        self.assertEqual(ui["of"]["pl"], "w")
        self.assertEqual(ui["mime"]["pl"], "typ pliku (MIME)")
        self.assertEqual(ui["min_size"]["pl"], "min. rozmiar")
        self.assertEqual(ui["conversation"]["pl"], "rozmowa")
        self.assertEqual(ui["missing"]["pl"], "brakujące")
        self.assertEqual(store().file("ui_labels.json")["values"]["all"]["pl"], "wszystkie")

    def test_params_keep_the_engine_key_and_add_labels(self):
        entry = load_entry()
        ui = store().file("ui_labels.json")
        spec = {"of": {"type": "enum", "values": ["all", "selection"], "default": "all"},
                "min_size": {"type": "number"}, "missing": {"type": "bool", "default": False},
                "zz_custom": {"type": "frobnicate"}}
        got = {p["name"]: p for p in entry._params(spec, ui)}
        self.assertEqual(set(got), set(spec), "the engine keys are what the form sends")
        self.assertEqual(got["of"]["label"], "w")
        self.assertEqual(got["of"]["values"], ["all", "selection"])
        self.assertEqual(got["of"]["value_labels"], ["wszystkie", "zaznaczenie"])
        self.assertEqual(got["min_size"]["label"], "min. rozmiar")
        self.assertEqual(got["min_size"]["type_label"], "liczba")
        self.assertEqual(got["missing"]["label"], "brakujące")
        self.assertEqual(got["zz_custom"]["label"], "zz_custom", "an unknown name is shown as it is")
        self.assertEqual(got["zz_custom"]["type_label"], "frobnicate")
        plain = {p["name"]: p for p in entry._params(spec)}
        self.assertEqual(plain["of"]["label"], "of", "without a label file the old names still work")

    def test_ui_labels_call_has_polish_roots_kinds_and_formats(self):
        entry = load_entry()
        got = json.loads(entry.ui_labels())
        self.assertTrue(got["ok"], got)
        layout = store().file("commander/layout.json")
        self.assertEqual(set(got["roots"]), set(layout["roots"]))
        self.assertEqual(got["roots"]["sources"], "Źródła")
        self.assertEqual(got["kinds"]["archive"], "archiwum")
        self.assertEqual(got["kinds"]["conversation"], "rozmowa")
        self.assertEqual(set(got["formats"]), set(layout["viewer_formats"]))
        self.assertEqual(got["formats"]["markdown"], "tekst (markdown)")

    def test_catalog_of_an_open_file_carries_the_labels(self):
        entry = load_entry()
        try:
            opened = json.loads(entry.open_sources(json.dumps([str(EXPORTS / "openai_2026_sharded.zip")])))
            self.assertTrue(opened["ok"], opened)
            cat = json.loads(entry.catalog())
            self.assertTrue(cat["ok"], cat)
            att = next(o for o in cat["ops"] if o["id"] == "attachments")
            by = {p["name"]: p for p in att["params"]}
            self.assertEqual({"of", "mime", "min_size", "conversation", "missing"} & set(by),
                             {"of", "mime", "min_size", "conversation", "missing"})
            self.assertEqual(by["of"]["label"], "w")
            self.assertEqual(by["of"]["value_labels"], ["wszystkie", "zaznaczenie"])
            self.assertEqual(by["min_size"]["label"], "min. rozmiar")
            self.assertEqual(by["missing"]["label"], "brakujące")
            for group in (cat["ops"], cat["edits"]):
                for item in group:
                    for p in item["params"]:
                        self.assertTrue(p["label"] and p["type_label"], (item["id"], p["name"]))
                        self.assertEqual(len(p["value_labels"]), len(p["values"]))
        finally:
            entry.open_sources("[]")


if __name__ == "__main__":
    unittest.main()
