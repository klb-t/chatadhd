"""Slice B — the package: hand-off imports in one step (engine/importer.py, this
tool, libloom when present), the manifest (sources with hashes, recipe,
decisions, stats, cost, version), reproduction from the manifest, the check,
and determinism: the same inputs and recipe give identical package bytes."""
import json
import os
import shutil
import zipfile

from _support import EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract.manifest import check_package, reproduce
from loom.tools.extract.readers.textlike import _capturing_importer_class

ALL = ["preserve", "flat", "structure", "reasoning", "text", "graph", "table", "timeline", "cards", "bundle"]
SOURCES = [EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip", EXTRACT / "claude_code/projects",
           EXTRACT / "grok/x_archive.zip", EXTRACT / "gemini/takeout.zip"]


class PackageTest(ExtractTest):
    def setUp(self) -> None:
        super().setUp()
        os.environ["SOURCE_DATE_EPOCH"] = "1760000000"

    def _run(self, pkg, *, recipe=None):
        eng = self.engine()
        rec = recipe or {"sources": [str(s) for s in SOURCES],
                         "steps": [{"op": "select.criteria", "params": {"terms": ["migracja", "schemat", "sesja", "grok", "gemini", "lista"]}},
                                   {"op": "select.expr", "params": {"expr": "is:agentic", "mode": "add"}},
                                   {"op": "expand"},
                                   {"op": "edit.relabel_roles", "params": {"where": "message", "map": {"human": "user"}}},
                                   {"op": "cut", "params": {"profiles": ALL}},
                                   {"op": "handoff", "params": {"graph": True}},
                                   {"op": "write", "params": {"package": str(pkg)}}]}
        eng.run_recipe(rec)
        return eng

    @staticmethod
    def _files(root):
        return {str(p.relative_to(root)): p.read_bytes() for p in sorted(root.rglob("*")) if p.is_file() and "cache" not in p.parts}

    def test_same_inputs_and_recipe_give_identical_package_bytes(self):
        a, b = self.tmp / "a", self.tmp / "b"
        self._run(a)
        self._run(b)
        fa, fb = self._files(a), self._files(b)
        self.assertEqual(sorted(fa), sorted(fb))
        diff = [k for k in fa if fa[k] != fb[k]]
        self.assertEqual(diff, [], "every file byte for byte, the manifest included (SOURCE_DATE_EPOCH)")
        man = json.loads(fa["manifest.json"])
        self.assertEqual(man["run"]["started"], "2025-10-09T08:53:20Z")
        self.assertGreater(len(fa), 60)

    def test_manifest_contents_and_check(self):
        pkg = self.tmp / "p"
        eng = self._run(pkg)
        man = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
        for key in ("sources", "recipe", "decisions", "selection", "cut", "preserve", "attachments", "redaction", "edits", "model_calls",
                    "cost_total", "stats", "handoff", "outputs", "tool", "reproducible_hash", "settings_sources"):
            self.assertIn(key, man, key)
        self.assertTrue(all(s["sha256"] and s["format"] for s in man["sources"]))
        self.assertEqual(man["cost_total"]["amount"], 0.0)
        self.assertNotIn("write", [st["op"] for st in man["recipe"]["body"]["steps"]], "the embedded recipe has no write: it is location-free")
        self.assertEqual(man["tool"]["name"], "loom.tools.extract")
        self.assertTrue(man["settings_sources"])
        res = check_package(pkg)
        self.assertTrue(res["ok"], res["problems"])
        self.assertEqual(res["handoff_import"]["messages_with_text"], man["handoff"]["messages_with_text"])
        # tampering is found
        victim = pkg / "flat/messages.jsonl"
        victim.write_text(victim.read_text(encoding="utf-8") + "{}\n", encoding="utf-8")
        (pkg / "text/INDEX.md").unlink()
        res = check_package(pkg)
        self.assertFalse(res["ok"])
        self.assertTrue(any("hash differs: flat/messages.jsonl" in p for p in res["problems"]))
        self.assertTrue(any("missing output: text/INDEX.md" in p for p in res["problems"]))

    def test_reproduce_from_the_manifest_alone(self):
        pkg = self.tmp / "p"
        self._run(pkg)
        (pkg / "recipe.json").unlink()  # the manifest embeds the recipe
        res = reproduce(self.engine(), pkg / "manifest.json", target=self.tmp / "again")
        self.assertTrue(res["equal"], res["differences"])
        self.assertEqual(res["sources_changed"], [])

    def test_handoff_imports_in_one_step_and_rereads_with_blocks(self):
        pkg = self.tmp / "p"
        eng = self._run(pkg)
        h = json.loads((pkg / "handoff/handoff.json").read_text(encoding="utf-8"))
        imp = _capturing_importer_class()()
        imp.import_file(str(pkg / "handoff.zip"))
        self.assertEqual(len(imp.captured), h["counts"]["conversations_with_messages"])
        self.assertEqual(sum(len([m for m in ms if str(m.get("content") or "").strip()]) for ms, _ in imp.captured),
                         h["counts"]["messages_with_text"])
        names = zipfile.ZipFile(pkg / "handoff.zip").namelist()
        self.assertFalse([n for n in names if not n.startswith("conversations/")], "no images for the OCR importer to misread")
        back = self.engine(pkg / "handoff")
        back.ensure_units()
        convs = back.conversations
        self.assertEqual(len(convs), h["counts"]["conversations"])
        tools = sum(1 for u in convs for m in u.messages for b in m.blocks if b.kind == "tool_use")
        self.assertGreater(tools, 0, "native blocks survive the hand-off (metadata.export.blocks)")
        resolved = [a for u in convs for a in u.attachments if a.status == "resolved"]
        self.assertEqual(len(resolved), h["attachments"]["copied"], "attachment references resolve inside handoff/")
        conv = json.loads(next((pkg / "handoff/conversations").glob("*.json")).read_text(encoding="utf-8"))
        self.assertEqual(conv["metadata"]["export"]["schema"], "loom.export.v1")
        self.assertTrue(conv["metadata"]["export"]["locator"])

    def test_handoff_through_libloom_when_present(self):
        import ctypes
        lib_path = os.environ.get("LOOM_LIBRARY")
        if not lib_path or not os.path.exists(lib_path):
            self.skipTest("libloom not available (LOOM_LIBRARY unset or missing): the C++ importer round trip runs in the gate")
        try:
            lib = ctypes.CDLL(lib_path)
        except OSError as exc:
            self.skipTest(f"LOOM_LIBRARY is not a loadable shared library ({exc})")
        if not hasattr(lib, "loom_import_file_ex"):
            self.skipTest("this libloom has no loom_import_file_ex")
        pkg = self.tmp / "p"
        self._run(pkg)
        h = json.loads((pkg / "handoff/handoff.json").read_text(encoding="utf-8"))
        lib.loom_init_ex.restype = ctypes.c_void_p
        lib.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
        lib.loom_import_file_ex.restype = ctypes.c_void_p
        lib.loom_import_file_ex.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p]
        lib.loom_free_string.argtypes = [ctypes.c_void_p]
        lib.loom_shutdown.argtypes = [ctypes.c_void_p]
        err = ctypes.c_char_p()
        ctx = lib.loom_init_ex(json.dumps({"data_dir": str(self.tmp / "loomdata"), "start_workers": False}).encode("utf-8"),
                               ctypes.byref(err))
        self.assertTrue(ctx, err.value)
        try:
            raw = lib.loom_import_file_ex(ctx, str(pkg / "handoff.zip").encode("utf-8"), None, None, None)
            doc = json.loads(ctypes.string_at(raw).decode("utf-8"))
            lib.loom_free_string(raw)
        finally:
            lib.loom_shutdown(ctx)
        self.assertNotIn("error", doc, doc)
        self.assertEqual(len(doc.get("conversations") or []), h["counts"]["conversations_with_messages"],
                         "libloom imports every hand-off conversation in one step")
        self.assertGreaterEqual(int(doc.get("messages") or 0), h["counts"]["messages_with_text"])

    def test_rewrite_removes_files_of_an_earlier_run(self):
        pkg = self.tmp / "p"
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("cut", {"profiles": ["text", "table"]})
        eng.op("write", {"package": str(pkg)})
        self.assertTrue((pkg / "table/messages.csv").exists())
        eng2 = self.engine(EXPORTS / "openai_2026_sharded.zip")
        eng2.op("select.expr", {"expr": "kind:conversation"})
        eng2.op("cut", {"profiles": ["text"]})
        eng2.op("write", {"package": str(pkg)})
        self.assertFalse((pkg / "table/messages.csv").exists(), "the package is exactly its manifest")
        self.assertTrue(check_package(pkg)["ok"])
