"""Recipes as data (schema, validation, normalisation, TOML, hash) and the engine: selection modes,
op registry, recording, replay determinism (same recipe twice → same reproducible_hash)."""
import json

from _support import EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract import recipe as R
from loom.tools.extract.engine import EngineError
from loom.tools.extract.expand import Selection


class RecipeTest(ExtractTest):
    def test_validate_reports_every_problem(self):
        rec = {"schema": R.SCHEMA, "sources": [], "steps": [
            {"op": "nope"}, {"op": "select.expr"}, {"op": "select.expr", "params": {"expr": 5}},
            {"op": "select.expr", "params": {"expr": "x", "mode": "sideways"}}, {"op": "expand", "params": {"bogus": 1}},
            {"id": "s1", "op": "inventory"}, {"id": "s1", "op": "inventory"}, {"op": "user.call", "params": {"module": "m", "function": "f"}}]}
        problems = R.validate(rec)
        joined = "\n".join(problems)
        for needle in ("unknown op 'nope'", "missing required param 'expr'", "must be expr", "mode must be one of", "unknown param 'bogus'",
                       "duplicate id", "allow_user_code"):
            self.assertIn(needle, joined)
        self.assertEqual(R.validate({"schema": R.SCHEMA, "sources": [], "steps": []}), [])
        with self.assertRaises(R.RecipeError):
            R.normalise(rec)

    def test_normalise_is_canonical_and_fills_presets_with_origin(self):
        a = {"schema": R.SCHEMA, "id": "r", "sources": ["~/x.zip"], "steps": [
            {"op": "select.expr", "params": {"mode": "add", "expr": "has:tools"}, "by": "tui", "at": "2026-01-01T00:00:00Z"},
            {"op": "timeline"}], "tags": {"u2": ["b", "a"], "u1": ["z"]}}
        b = {"schema": R.SCHEMA, "id": "r", "sources": ["~/x.zip"], "steps": [
            {"op": "select.expr", "params": {"expr": "has:tools", "mode": "add"}, "by": "tui"},
            {"op": "timeline", "params": {}}], "tags": {"u1": ["z"], "u2": ["a", "b"]}}
        na, nb = R.normalise(a), R.normalise(b)
        self.assertEqual(na, nb)
        self.assertEqual(R.recipe_hash(na), R.recipe_hash(nb))
        self.assertEqual([s["id"] for s in na["steps"]], ["s1", "s2"])
        self.assertNotIn("at", na["steps"][0])
        tl = na["steps"][1]
        self.assertEqual(tl["params"]["key"], "created")
        self.assertEqual(tl["params_source"]["key"], "preset:timeline.key")
        self.assertEqual(na["steps"][0]["params_source"]["expr"], "step")
        self.assertFalse(na["sources"][0].startswith("~"))

    def test_toml_and_json_files(self):
        toml = self.write(self.tmp / "r.toml", 'schema = "loom.extract.recipe/1"\nid = "t"\nsources = ["a.zip"]\n[[steps]]\nop = "inventory"\n')
        rec = R.load_recipe(toml)
        self.assertEqual(rec["id"], "t")
        norm = R.normalise(rec, base_dir=self.tmp)
        self.assertEqual(norm["sources"], [(self.tmp / "a.zip").as_posix()])
        js = self.write(self.tmp / "r.json", json.dumps(rec))
        self.assertEqual(R.normalise(R.load_recipe(js), base_dir=self.tmp), norm)
        with self.assertRaises(R.RecipeError):
            R.load_recipe(self.tmp / "missing.json")
        bad = self.write(self.tmp / "bad.toml", "= nope")
        with self.assertRaises(R.RecipeError):
            R.load_recipe(bad)


class EngineTest(ExtractTest):
    @staticmethod
    def _package_diff(a, b) -> str:
        import difflib
        out = []
        for f in sorted(a.iterdir()):
            if f.name == "manifest.json" or not (b / f.name).exists():
                continue
            x, y = f.read_text(encoding="utf-8").splitlines(True), (b / f.name).read_text(encoding="utf-8").splitlines(True)
            if x != y:
                out.append(f"--- {f.name}\n" + "".join(list(difflib.unified_diff(x, y, n=0))[:12]))
        return "\n".join(out) or "no file differs"

    def test_registry_is_fully_implemented_and_errors_are_explicit(self):
        eng = self.engine()
        ops = eng.ops_available()
        self.assertTrue(all(spec["implemented"] for spec in ops.values()), [k for k, v in ops.items() if not v["implemented"]])
        with self.assertRaises(R.RecipeError):
            eng.op("nope")
        with self.assertRaises(EngineError):
            eng.op("select.expr")
        with self.assertRaises(EngineError):
            eng.op("user.call", {"module": "m", "function": "f"})
        with self.assertRaises(ValueError):
            eng.op("scan")

    def test_selection_modes(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip")
        convs = [u for u in units if u.kind == "conversation"]
        a, b, c = convs[0], convs[1], convs[2]
        s = Selection()
        s.add(a.uid, None, "a")
        s.add(b.uid, {b.messages[0].id}, "b")
        eng.apply_selection(s, "replace")
        self.assertEqual(set(eng.selection.units), {a.uid, b.uid})
        s2 = Selection()
        s2.add(b.uid, {b.messages[1].id}, "b2")
        s2.add(c.uid, None, "c")
        eng.apply_selection(s2, "add")
        self.assertEqual(eng.selection.units[b.uid], {b.messages[0].id, b.messages[1].id})
        s3 = Selection()
        s3.add(c.uid, None, "c")
        s3.add(b.uid, {b.messages[0].id}, "b")
        eng.apply_selection(s3, "subtract")
        self.assertNotIn(c.uid, eng.selection.units)
        self.assertEqual(eng.selection.units[b.uid], {b.messages[1].id})
        s4 = Selection()
        s4.add(a.uid, {a.messages[0].id}, "a")
        eng.apply_selection(s4, "intersect")
        self.assertEqual(set(eng.selection.units), {a.uid})
        self.assertEqual(eng.selection.units[a.uid], {a.messages[0].id})
        r = eng.op("select.invert")
        self.assertEqual(r["counts"]["selected"], len(convs) - 1)
        eng.op("select.clear")
        self.assertEqual(eng.selection.units, {})
        with self.assertRaises(EngineError):
            eng.apply_selection(s4, "sideways")

    def test_ops_are_recorded_with_origin_and_events(self):
        eng = self.engine(EXPORTS / "openai_legacy.zip")
        r = eng.op("select.expr", {"expr": "role:user"}, by="tui")
        steps = eng.recipe["steps"]
        self.assertEqual([s["op"] for s in steps], ["scan", "read", "merge", "select.expr"])
        self.assertEqual([s["by"] for s in steps], ["recipe", "recipe", "recipe", "tui"])
        self.assertEqual(steps[0]["note"], "implicit")
        self.assertEqual(r["step"], "s4")
        kinds = [e["kind"] for e in eng.events]
        self.assertIn("op.start", kinds)
        self.assertIn("op.done", kinds)
        self.assertEqual(eng.op("explain", {"unit": steps and list(eng.by_uid)[0]})["op"], "explain")
        self.assertEqual(len(eng.recipe["steps"]), 4, "explain is not recorded (records: false)")

    def test_same_recipe_twice_gives_the_same_reproducible_hash(self):
        rec = {"schema": R.SCHEMA, "id": "det", "sources": [str(EXPORTS / "openai_2026_sharded.zip"), str(EXTRACT / "versions/chatgpt_2025-10-01.zip")],
               "steps": [{"op": "select.criteria", "params": {"terms": ["bazy", "danych", "rekurencja"]}},
                         {"op": "select.structural", "params": {"pattern": "agentic"}},
                         {"op": "expand", "params": {"preset": "conversation"}},
                         {"op": "timeline", "params": {"key": "activity"}}, {"op": "clusters"}]}
        hashes = []
        for i in (1, 2):
            eng = self.engine()
            eng.run_recipe(dict(rec, steps=rec["steps"] + [{"op": "write", "params": {"package": str(self.tmp / f"p{i}")}}]))
            m = json.loads((self.tmp / f"p{i}" / "manifest.json").read_text(encoding="utf-8"))
            hashes.append(m["reproducible_hash"])
            self.assertEqual(m["schema"], "loom.extract.manifest/1")
            self.assertEqual(m["cost_total"]["amount"], 0.0)
            self.assertEqual(m["model_calls"], [])
            self.assertTrue(m["recipe"]["sha256"])
            self.assertEqual(m["run"]["slice"], "B")
        self.assertEqual(hashes[0], hashes[1], self._package_diff(self.tmp / "p1", self.tmp / "p2"))
        # replay from the package's own recipe → identical package
        eng = self.engine()
        rec2 = R.load_recipe(self.tmp / "p1" / "recipe.json")
        rec2["steps"].append({"op": "write", "params": {"package": str(self.tmp / "p3")}})
        eng.run_recipe(rec2)
        m3 = json.loads((self.tmp / "p3" / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(m3["reproducible_hash"], hashes[0], self._package_diff(self.tmp / "p1", self.tmp / "p3"))
        for name in ("inventory.json", "timeline.json", "timeline.jsonl", "selection.json", "explain.jsonl", "clusters.json",
                     "decisions.json", "recipe.json", "units.jsonl", "RAPORT.md", "manifest.json"):
            self.assertTrue((self.tmp / "p1" / name).exists(), name)
        report = (self.tmp / "p1" / "RAPORT.md").read_text(encoding="utf-8")
        self.assertIn("## Co znaleziono", report)
        self.assertIn("0 — bez modelu", report)

    def test_dry_run_counts_and_aggregate_hides_content(self):
        eng = self.engine(EXTRACT / "gemini/takeout.zip", aggregate=True)
        eng.op("select.expr", {"expr": "role:user"})
        r = eng.op("dry_run")
        self.assertEqual(r["counts"]["conversations"], 2)
        self.assertEqual(r["units"], [], "aggregate: no per-unit titles")
        self.assertEqual(r["cost_total"]["amount"], 0.0)
        w = eng.op("write", {"package": str(self.tmp / "agg")})
        self.assertNotIn("units.jsonl", w["outputs"])
        self.assertNotIn("explain.jsonl", w["outputs"])
        text = "".join(p.read_text(encoding="utf-8") for p in (self.tmp / "agg").iterdir())
        for s in ("pamięć hierarchiczna", "takeout.zip", "schemat.png"):
            self.assertNotIn(s, text)
