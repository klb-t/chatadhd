"""The one dispatcher (api.Session) every face uses: views (tree, flat, global time order, sort,
live filter), node views (JSON, Markdown, raw source bytes), selection with reasons, batch-edit
preview → apply → undo/redo, package structure ops (undoable), recipe save/replay, export, compare,
search, goto, bookmarks/tags/views, status, background jobs (progress, cancel restores the state,
questions default to "no", busy instead of blocking), aggregate privacy, errors."""
import json
import threading
import time
import unittest

from _support import EXPORTS, EXTRACT, ExtractTest

from loom.tools.extract import ops as _ops
from loom.tools.extract.api import ApiError, Session
from loom.tools.extract.datafiles import store
from loom.tools.extract.nodes import parse_id

SRC = [str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")]


class ApiTest(ExtractTest):
    def session(self, *sources, face: str = "api", aggregate: bool = False) -> Session:
        s = Session(face=face, ds=store(), aggregate=aggregate)
        self.addCleanup(s.close)
        if sources:
            s.call({"call": "open", "sources": list(sources)})
        return s

    def conv(self, s: Session, title: str) -> str:
        return next(u.uid for u in s.eng.conversations if u.title == title)

    # -- registry --------------------------------------------------------------------------------
    def test_every_call_has_a_handler_and_every_handler_a_registry_entry(self):
        s = self.session()
        calls = s.calls()
        for name in calls:
            self.assertTrue(hasattr(s, "c_" + name.replace("-", "_")), name)
        handlers = {n[2:].replace("_", "-") for n in dir(s) if n.startswith("c_")}
        self.assertEqual(handlers - {k.replace("_", "-") for k in calls}, set())
        with self.assertRaises(ApiError) as cm:
            s.call({"call": "nope"})
        self.assertEqual(cm.exception.status, 404)
        ops = s.call({"call": "ops"})["ops"]
        for name in ("select.items", "tag", "bookmark", "view.save", "view.delete"):
            self.assertTrue(ops[name]["implemented"], name)

    # -- views ---------------------------------------------------------------------------------------
    def test_tree_flat_global_time_order_sort_and_filter(self):
        s = self.session(*SRC)
        top = s.call({"call": "list", "root": "sources"})
        self.assertEqual([r["kind"] for r in top["rows"]], ["archive", "archive"])
        self.assertEqual(top["breadcrumbs"][0]["label"], "Źródła")
        convs = s.call({"call": "list", "root": "sources", "path": ["arch:1"]})
        self.assertEqual(len(convs["rows"]), 7)
        self.assertIn("conversation", convs["columns"])
        mig = next(r for r in convs["rows"] if r["label"] == "Migracja bazy danych")
        self.assertEqual(mig["cols"]["branches"], 4)
        kids = s.call({"call": "list", "root": "sources", "path": ["arch:1", mig["id"]]})
        self.assertEqual([r["kind"] for r in kids["rows"]].count("branch"), 4, "a conversation with forks opens into its branches")
        self.assertIn("attachment", [r["kind"] for r in kids["rows"]])
        br = s.call({"call": "list", "root": "sources", "path": ["arch:1", mig["id"], kids["rows"][0]["id"]]})
        self.assertTrue(all(r["kind"] == "message" for r in br["rows"]))
        self.assertEqual(br["breadcrumbs"][-1]["label"], "gałąź 1")
        # one list of every message of every archive, ordered by time across services, ties stable
        flat = s.call({"call": "list", "root": "sources", "flat": "message"})
        self.assertEqual(flat["total"], sum(len(u.messages) for u in s.eng.conversations))
        times = [s.views().time_key(s.views().item(r["id"])[0]) for r in flat["rows"]]
        self.assertEqual(times, sorted(times))
        self.assertEqual({parse_id(r["id"])["uid"] for r in flat["rows"]} >= {u.uid for u in s.eng.conversations if u.messages}, True)
        tl = s.call({"call": "list", "root": "timeline"})
        self.assertEqual([r["id"] for r in tl["rows"]], [r["id"] for r in flat["rows"]], "the timeline root is that same global order")
        again = s.call({"call": "list", "root": "sources", "flat": "message"})
        self.assertEqual([r["id"] for r in again["rows"]], [r["id"] for r in flat["rows"]], "deterministic")
        by_size = s.call({"call": "list", "root": "sources", "flat": "conversation", "sort": "-messages"})
        sizes = [r["cols"]["messages"] for r in by_size["rows"]]
        self.assertEqual(sizes, sorted(sizes, reverse=True))
        # live filter: folded text on the visible columns, or an expression
        f1 = s.call({"call": "list", "root": "sources", "flat": "conversation", "filter": "migracja"})
        self.assertEqual([r["label"] for r in f1["rows"]], ["Migracja bazy danych"])
        self.assertEqual(f1["filter"]["how"], "text")
        f2 = s.call({"call": "list", "root": "sources", "flat": "message", "filter": "role:user"})
        self.assertEqual(f2["filter"]["how"], "expr")
        self.assertTrue(f2["rows"] and all(r["label"].startswith("user") for r in f2["rows"]))
        f3 = s.call({"call": "list", "root": "sources", "flat": "conversation", "filter": "count(tool_use) >= 2"})
        self.assertTrue(all(r["cols"]["tools"] >= 2 for r in f3["rows"]) and f3["rows"])
        empty = s.call({"call": "list", "root": "clusters"})
        self.assertEqual(empty["total"], 0)
        self.assertIn("F2", empty["hint"])
        page = s.call({"call": "list", "root": "sources", "flat": "message", "offset": 2, "limit": 3})
        self.assertEqual([r["id"] for r in page["rows"]], [r["id"] for r in flat["rows"][2:5]])

    def test_view_json_markdown_and_raw_source_bytes(self):
        s = self.session(*SRC)
        uid = self.conv(s, "Migracja bazy danych")
        js = json.loads(s.call({"call": "view", "node": uid, "format": "json"})["text"])
        self.assertEqual(js["title"], "Migracja bazy danych")
        md = s.call({"call": "view", "node": uid, "format": "markdown"})["text"]
        self.assertTrue(md.startswith("# Migracja bazy danych"))
        self.assertIn("Jak zaplanować migrację", md)
        raw = s.call({"call": "view", "node": uid, "format": "raw"})["text"]
        self.assertIn("mapping", json.loads(raw), "raw = the element of the source array, byte for byte")
        u = s.eng.by_uid[uid]
        m = next(m for m in u.messages if m.role == "user")
        mraw = json.loads(s.call({"call": "view", "node": f"{uid}#{m.id}", "format": "raw"})["text"])
        self.assertIsInstance(mraw, dict)
        with self.assertRaises(ApiError):
            s.call({"call": "view", "node": uid, "format": "pdf"})
        with self.assertRaises(ApiError):
            s.call({"call": "view", "node": "sha256:00000000000000000000000000000000"})

    # -- selection ---------------------------------------------------------------------------------
    def test_selection_ways_with_reasons(self):
        s = self.session(*SRC)
        uid = self.conv(s, "Migracja bazy danych")
        r = s.call({"call": "select", "how": "toggle", "items": [uid]})
        self.assertEqual(r["selection"]["conversations"], 1)
        r = s.call({"call": "select", "how": "toggle", "items": [uid]})
        self.assertEqual(r["selection"]["conversations"], 0, "toggle twice = deselected")
        u = s.eng.by_uid[uid]
        r = s.call({"call": "select", "how": "items", "items": [f"{uid}|b2"]})
        self.assertEqual(r["selection"]["partial"], 1, "a branch selects its messages")
        self.assertEqual(r["selection"]["messages"], len(u.branches()[1]))
        s.call({"call": "select", "how": "clear"})
        r = s.call({"call": "select", "how": "expr", "expr": "message[role:user AND text~/migrac/i]"})
        self.assertGreaterEqual(r["selection"]["messages"], 1)
        s.call({"call": "select", "how": "clear"})
        r = s.call({"call": "select", "how": "regex", "field": "title", "pattern": "migr/acja|wyszukiw", "flags": "i"})
        self.assertGreaterEqual(r["selection"]["conversations"], 1)
        r = s.call({"call": "select", "how": "pattern", "pattern": "agentic", "mode": "replace"})
        self.assertGreaterEqual(r["selection"]["conversations"], 1)
        r = s.call({"call": "select", "how": "time", "around": "2025-10-09", "days": 1, "mode": "replace"})
        self.assertEqual(r["selection"]["conversations"], sum(1 for c in s.eng.conversations if (c.created_utc or "").startswith("2025-10")))
        r = s.call({"call": "select", "how": "invert"})
        r = s.call({"call": "select", "how": "similar", "to": [uid], "threshold": 0.0, "mode": "replace"})
        self.assertIn("index", r["result"])
        r = s.call({"call": "select", "how": "all", "mode": "replace"})
        self.assertEqual(r["selection"]["conversations"], len(s.eng.conversations))
        sel = s.call({"call": "selection"})
        self.assertEqual(sel["total"], len(s.eng.conversations))
        self.assertTrue(all(it["reasons"] for it in sel["items"]), "a reason per item")
        steps = [st["op"] for st in s.eng.recipe["steps"]]
        self.assertIn("select.items", steps)
        self.assertTrue(all(st["by"] == "api" for st in s.eng.recipe["steps"] if st.get("note") != "implicit"))

    # -- package, batch edits, undo --------------------------------------------------------------------
    def test_package_ops_and_batch_edits_preview_apply_undo_redo(self):
        s = self.session(*SRC)
        uid = self.conv(s, "Migracja bazy danych")
        other = self.conv(s, "Wyszukiwanie w sieci")
        s.call({"call": "apply", "op": "package.copy", "params": {"items": [uid, f"{other}|b1"]}})
        pkg = s.eng.package
        self.assertEqual(len(pkg.entries), 2)
        h0 = pkg.tree_hash()
        steps0 = len(s.eng.recipe["steps"])
        prev = s.call({"call": "edit", "edit": "relabel_roles", "params": {"where": "message", "map": {"user": "człowiek"}}})
        self.assertTrue(prev["preview"] and prev["count"] > 0 and prev["patch_text"])
        self.assertEqual(pkg.tree_hash(), h0, "a preview changes nothing")
        self.assertEqual(len(s.eng.recipe["steps"]), steps0, "a preview is not a recipe step")
        ap = s.call({"call": "apply", "edit": "relabel_roles", "params": {"where": "message", "map": {"user": "człowiek"}}})
        self.assertEqual(ap["result"]["counts"]["changed"], prev["count"])
        self.assertEqual(pkg.tree_hash(), prev["after_preview"], "preview = apply")
        self.assertEqual(s.eng.recipe["steps"][-1]["op"], "edit.relabel_roles")
        s.call({"call": "apply", "op": "package.mkdir", "params": {"path": "/projekty"}})
        s.call({"call": "apply", "op": "package.move", "params": {"items": [uid], "into": "/projekty"}})
        s.call({"call": "apply", "op": "package.rename", "params": {"item": uid, "title": "Migracja (nowa nazwa)"}})
        listing = s.call({"call": "list", "root": "package", "path": ["dir:/projekty"]})
        self.assertEqual([r["label"] for r in listing["rows"]], ["Migracja (nowa nazwa)"])
        s.call({"call": "apply", "op": "package.remove", "params": {"items": [other]}})
        self.assertEqual(len(pkg.entries), 1)
        for _ in range(4):
            s.call({"call": "undo"})
        self.assertNotEqual(pkg.tree_hash(), h0)
        s.call({"call": "undo"})
        self.assertEqual(pkg.tree_hash(), h0, "every package op and edit undoes")
        self.assertEqual(len(s.eng.recipe["steps"]), steps0, "undo takes the steps out of the recipe")
        s.call({"call": "redo", "steps": 2})
        self.assertEqual([st["op"] for st in s.eng.recipe["steps"]][-2:], ["edit.relabel_roles", "package.mkdir"])
        st = s.call({"call": "status"})
        self.assertEqual(st["package"]["entries"], 2)
        self.assertGreater(st["package"]["bytes_est"], 0)
        self.assertEqual(st["undo"], 3)
        self.assertEqual(st["cost"]["calls"], 0)

    def test_save_recipe_export_and_replay_give_the_same_package(self):
        s = self.session(*SRC, face="tui")
        uid = self.conv(s, "Migracja bazy danych")
        s.call({"call": "select", "how": "items", "items": [uid]})
        s.call({"call": "apply", "op": "package.copy", "params": {}})
        s.call({"call": "apply", "edit": "drop_blocks", "params": {"where": "message", "kinds": ["thinking"]}})
        s.call({"call": "tag", "node": uid, "tag": "chatadhd"})
        s.call({"call": "bookmark", "node": uid, "name": "start"})
        saved = s.call({"call": "save-recipe", "path": str(self.tmp / "r.json")})
        self.assertTrue((self.tmp / "r.json").exists())
        self.assertEqual(saved["recipe"]["tags"], {uid: ["chatadhd"]})
        ex = s.call({"call": "export", "package": str(self.tmp / "pkg1"), "profiles": ["flat"]})
        self.assertTrue(ex["reproducible_hash"])
        man = s.call({"call": "manifest"})["manifest"]
        self.assertIn("tui", man["run"]["faces"])
        # the package's own recipe replayed elsewhere = the same bytes
        s2 = self.session()
        r2 = s2.call({"call": "open", "recipe": str(self.tmp / "pkg1" / "recipe.json"), "write": str(self.tmp / "pkg2")})
        self.assertEqual(r2["reproducible_hash"], ex["reproducible_hash"])
        s3 = self.session()
        s3.call({"call": "open", "package": str(self.tmp / "pkg1")})
        content = lambda sess: [(st["op"], st["params"], st["by"]) for st in sess.call({"call": "recipe"})["recipe"]["steps"] if st["op"] != "write"]  # noqa: E731
        self.assertEqual(content(s3), content(s2), "opening a package replays its recipe; the steps keep their face")
        self.assertIn("tui", {st[2] for st in content(s3)})

    # -- compare, search, goto, tags, views ---------------------------------------------------------
    def test_compare_search_goto_tags_bookmarks_views(self):
        s = self.session(*SRC)
        a, b = self.conv(s, "Migracja bazy danych"), self.conv(s, "Wyszukiwanie w sieci")
        cmp_ = s.call({"call": "compare", "a": a, "b": b})
        self.assertFalse(cmp_["same"])
        self.assertTrue(any(f["path"] == "/title" for f in cmp_["fields"]))
        self.assertTrue(any(line.startswith("-# Migracja") for line in cmp_["text_diff"]))
        self.assertTrue(s.call({"call": "compare", "a": a, "b": a})["same"])
        hits = s.call({"call": "search", "text": "postgresql"})["hits"]
        self.assertTrue(hits)
        h = next(x for x in hits if x["kind"] == "message")
        self.assertEqual(h["path"][0], "arch:1")
        listing = s.call({"call": "list", "root": "sources", "path": h["path"]})
        self.assertIn(h["id"], [r["id"] for r in listing["rows"]], "a hit's path opens the pane where the hit is")
        g = s.call({"call": "goto", "target": a[7:20]})
        self.assertEqual(g["node"], a)
        g = s.call({"call": "goto", "target": "2025-10-09T10:00"})
        self.assertEqual(parse_id(g["node"])["kind"], "message")
        with self.assertRaises(ApiError):
            s.call({"call": "goto", "target": "nic-takiego"})
        s.call({"call": "tag", "node": a, "tag": "projekt"})
        r = s.call({"call": "select", "how": "expr", "expr": "tag:projekt"})
        self.assertEqual(r["selection"]["conversations"], 1, "tags are part of the selection language")
        s.call({"call": "bookmark", "node": f"pkg:{b}"})
        bm = s.call({"call": "list", "root": "bookmarks"})
        self.assertEqual([x["id"] for x in bm["rows"]], [b])
        s.call({"call": "views", "action": "save", "name": "moje", "state": {"panes": [{"root": "timeline"}]}})
        self.assertEqual(s.call({"call": "views", "action": "list"})["views"], [{"name": "moje"}])
        self.assertEqual(s.call({"call": "views", "action": "load", "name": "moje"})["state"]["panes"][0]["root"], "timeline")
        s.call({"call": "views", "action": "delete", "name": "moje"})
        self.assertEqual(s.call({"call": "views", "action": "list"})["views"], [])
        rec = s.call({"call": "list", "root": "recipe", "path": ["grp:steps"]})
        self.assertTrue(all(r["kind"] == "step" for r in rec["rows"]))
        ex = s.call({"call": "explain", "node": b})
        self.assertTrue(ex["why_not"])
        flt = s.call({"call": "filter", "action": "check", "expr": "title~/(/"})
        self.assertFalse(flt["ok"])

    # -- jobs, cancel, questions, busy ----------------------------------------------------------------
    def test_background_job_progress_and_cancel_restores_state(self):
        s = self.session(*SRC)
        started = threading.Event()

        def slow(eng, p):
            for i in range(500):
                eng.emit("progress", done=i, total=500)
                started.set()
                time.sleep(0.01)
            return {"counts": {"n": 500}}
        orig = _ops.OPS["clusters"]
        _ops.OPS["clusters"] = slow
        self.addCleanup(lambda: _ops.OPS.__setitem__("clusters", orig))
        steps = len(s.eng.recipe["steps"])
        sel_before = dict(s.eng.selection.units)
        r = s.call({"call": "op", "op": "clusters", "wait": False})
        self.assertIn("job", r)
        jid = r["job"]["id"]
        self.assertTrue(started.wait(10))
        busy = s.call({"call": "list", "root": "sources"})
        self.assertTrue(busy["busy"], "a face never blocks: it hears 'busy' while the engine works")
        st = s.call({"call": "status"})
        self.assertTrue(st["busy"] and st["job"]["state"] == "running")
        s.call({"call": "cancel", "id": jid})
        job = s.wait(jid, 10)
        self.assertEqual(job["state"], "cancelled")
        self.assertEqual(len(s.eng.recipe["steps"]), steps, "a cancelled op is not a recipe step")
        self.assertEqual(dict(s.eng.selection.units), sel_before)
        kinds = [e["kind"] for e in s.events_after(0) if e.get("job") == jid]
        self.assertIn("progress", kinds)
        self.assertIn("cancelled", kinds)
        # a finished job
        _ops.OPS["clusters"] = lambda eng, p: (eng.emit("progress", done=1, total=1), {"counts": {"n": 1}})[1]
        r = s.call({"call": "op", "op": "clusters", "wait": False})
        job = s.wait(r["job"]["id"], 10)
        self.assertEqual(job["state"], "done")
        self.assertEqual(s.eng.recipe["steps"][-1]["op"], "clusters")

    def test_cost_question_defaults_to_no_and_can_be_answered(self):
        s = self.session(*SRC)
        asked = []

        def paid(eng, p):
            ok = eng.ask({"kind": "estimate", "estimate": {"calls": 3, "total": {"amount": 0.01, "currency": "USD", "known": True}}})
            asked.append(ok)
            return {"counts": {"accepted": int(ok)}}
        orig = _ops.OPS["clusters"]
        _ops.OPS["clusters"] = paid
        self.addCleanup(lambda: _ops.OPS.__setitem__("clusters", orig))
        s.call({"call": "op", "op": "clusters"})
        self.assertEqual(asked, [False], "nobody to ask in a synchronous call: the preset answer, 'no'")
        r = s.call({"call": "op", "op": "clusters", "wait": False})
        deadline = time.time() + 10
        q = None
        while time.time() < deadline and q is None:
            q = next((e for e in s.events_after(0) if e["kind"] == "question"), None)
            time.sleep(0.02)
        self.assertIsNotNone(q)
        self.assertIn("3", q["text"])
        s.call({"call": "answer", "question": q["id"], "answer": "tak"})
        s.wait(r["job"]["id"], 10)
        self.assertEqual(asked, [False, True])

    # -- privacy --------------------------------------------------------------------------------------
    def test_aggregate_mode_shows_numbers_not_content(self):
        s = self.session(*SRC, aggregate=True)
        rows = s.call({"call": "list", "root": "sources", "flat": "conversation"})["rows"]
        titles = {u.title for u in s.eng.conversations if u.title}
        for r in rows:
            self.assertNotIn(r["label"], titles)
        with self.assertRaises(ApiError):
            s.call({"call": "view", "node": rows[0]["id"]})
        top = s.call({"call": "list", "root": "sources"})["rows"]
        self.assertIsNone(top[0]["cols"].get("path"))

    def test_mixed_sources_and_deep_sessions_open(self):
        s = self.session(str(EXTRACT / "claude_code" / "projects"), str(EXTRACT / "gemini" / "takeout.zip"))
        flat = s.call({"call": "list", "root": "sources", "flat": "block"})
        self.assertGreater(flat["total"], 0)
        tools = s.call({"call": "list", "root": "sources", "flat": "block", "filter": "kind:tool_use"})
        self.assertTrue(tools["rows"])
        att = s.call({"call": "list", "root": "attachments"})
        self.assertIsInstance(att["rows"], list)


if __name__ == "__main__":
    unittest.main()
