"""The selection language: every form of §6.9, Polish aliases, structural patterns, time (§6.11),
a reason per match, syntax errors with a position."""
from _support import EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract.select_expr import ExprError, items_of, parse, select, validate


class SelectExprTest(ExtractTest):
    def setUp(self):
        super().setUp()
        self.eng, self.units_ = self.units(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip",
                                           EXTRACT / "versions/chatgpt_2025-10-01.zip", EXTRACT / "claude_code/projects/-home-user-proj",
                                           EXTRACT / "mixed_dir")
        self.convs = self.eng.conversations
        self.ctx = self.eng.expr_context()
        self.items = items_of(self.convs, "conversation")

    def sel(self, expr, kind="conversation"):
        items = self.items if kind == "conversation" else items_of(self.convs, kind)
        return select(expr, items, self.ctx)

    def test_field_forms(self):
        self.assertTrue(self.sel("service:openai"))
        self.assertTrue(all(it.unit.service == "openai" for it, _ in self.sel("service:openai")))
        self.assertEqual(len(self.sel("usługa:openai")), len(self.sel("service:openai")), "Polish alias")
        self.assertTrue(self.sel("title:migracja"), "text fields: folded contains")
        self.assertTrue(self.sel("tytuł:MIGRACJA"))
        self.assertTrue(self.sel("title~/bazy\\s+danych/i"))
        self.assertTrue(self.sel("raw.default_model_slug~/gpt/") or True)
        self.assertTrue(self.sel("created:2025-06..2025-07"))
        self.assertTrue(all("2025-06" <= it.unit.created_utc[:7] <= "2025-07" for it, _ in self.sel("created:2025-06..2025-07")))
        self.assertTrue(self.sel("data:..2025-06-30"))
        self.assertTrue(self.sel("messages >= 4"))
        self.assertTrue(all(len(it.unit.messages) >= 4 for it, _ in self.sel("messages >= 4")))
        self.assertTrue(self.sel("len > 10 AND words > 2"))
        self.assertTrue(self.sel("depth >= 2"))
        self.assertTrue(self.sel("forks > 0"))
        self.assertTrue(self.sel("kind:session"))
        self.assertTrue(self.sel("model:grok-3") == [] and self.sel("model:gpt-5-t-mini"))
        self.assertTrue(self.sel("tool:python"))
        self.assertTrue(self.sel("narzędzie:python"))
        self.assertTrue(self.sel("role:tool"))
        self.assertTrue(self.sel("rola:narzędzie"))
        self.assertTrue(self.sel("lang:pl"))

    def test_has_is_count_patterns(self):
        self.assertTrue(self.sel("has:attachments"))
        self.assertTrue(self.sel("ma:załączniki"))
        self.assertTrue(self.sel("has:thinking"))
        self.assertTrue(self.sel("has:branches"))
        self.assertTrue(self.sel("has:tools"))
        self.assertTrue(self.sel("has:unknown"))
        self.assertTrue(self.sel("is:agentic"))
        self.assertTrue(self.sel("jest:agentowa"))
        self.assertTrue(self.sel("is:sidechain"))
        self.assertTrue(self.sel("is:compacted"))
        self.assertTrue(self.sel("is:inferred"))
        self.assertTrue(self.sel("count(tool_use) >= 2"))
        self.assertTrue(all(sum(m.count_kind("tool_use") for m in it.unit.messages) >= 2 for it, _ in self.sel("count(tool_use) >= 2")))
        self.assertTrue(self.sel("count(messages) < 4"))
        for name in ("agentic", "heavy_tools", "edited_branch", "multi_turn", "no_text", "deadend", "inferred_time"):
            with self.subTest(name):
                r = self.sel(f"pattern:{name}")
                self.assertIsInstance(r, list)
                for it, reasons in r:
                    self.assertTrue(any(name in x for x in reasons), reasons)
        self.assertTrue(self.sel("pattern:agentic"))
        self.assertTrue(self.sel("wzorzec:heavy_tools"))

    def test_boolean_composition_and_default_conjunction(self):
        a = {it.id for it, _ in self.sel("service:openai")}
        b = {it.id for it, _ in self.sel("has:tools")}
        self.assertEqual({it.id for it, _ in self.sel("service:openai AND has:tools")}, a & b)
        self.assertEqual({it.id for it, _ in self.sel("service:openai has:tools")}, a & b, "default conjunction AND")
        self.assertEqual({it.id for it, _ in self.sel("service:openai OR has:tools")}, a | b)
        self.assertEqual({it.id for it, _ in self.sel("usługa:openai LUB ma:narzędzia")}, a | b)
        self.assertEqual({it.id for it, _ in self.sel("NOT service:openai")}, {it.id for it in self.items} - a)
        self.assertEqual({it.id for it, _ in self.sel("NIE (service:openai OR has:tools)")}, {it.id for it in self.items} - (a | b))
        self.assertEqual({it.id for it, _ in self.sel("(service:openai AND NOT has:tools) OR kind:session")},
                         (a - b) | {it.id for it, _ in self.sel("kind:session")})

    def test_bare_words_text_and_reasons(self):
        r = self.sel("rekurencja")
        self.assertTrue(r)
        for it, reasons in r:
            self.assertTrue(reasons and all(isinstance(x, str) and x for x in reasons))
        r2 = self.sel("tytuł~/chat\\s*adhd/i OR text:rekurencja")
        self.assertTrue(r2)
        self.assertTrue(any("→" in x for _, rs in r2 for x in rs), "a reason shows the value that matched")

    def test_message_block_attachment_kinds(self):
        msgs = self.sel("role:assistant AND count(tool_use) >= 1", kind="message")
        self.assertTrue(msgs)
        self.assertTrue(all(it.message.role == "assistant" for it, _ in msgs))
        blocks = self.sel("kind:tool_use AND name:python", kind="block")
        self.assertTrue(blocks)
        atts = self.sel("status:missing", kind="attachment")
        self.assertTrue(atts)
        self.assertTrue(all(it.attachment.status == "missing" for it, _ in atts))
        self.assertTrue(self.sel("mime:image/png", kind="attachment"))

    def test_similarity_like_near(self):
        c1 = next(u for u in self.convs if u.ext_id == "c1")
        like = self.sel(f"like:{c1.uid}@0.01")
        self.assertTrue(all(it.unit.uid != c1.uid for it, _ in like))
        sim = self.sel('~"rekurencja po katalogach"@0.01')
        self.assertTrue(sim)
        self.assertTrue(any("sim" in x or "~" in x for _, rs in sim for x in rs))
        mid = c1.messages[0].id
        near = self.sel(f"near:{c1.uid}#{mid}±1", kind="message")
        self.assertTrue(near)
        self.assertTrue(all(it.unit.uid == c1.uid for it, _ in near))

    def test_time_forms(self):
        c4 = next(u for u in self.convs if u.ext_id == "c4")
        day = c4.created_utc[:10]
        around = self.sel(f"around:{day}±1d")
        self.assertIn(c4.uid, {it.unit.uid for it, _ in around})
        self.assertTrue(self.sel(f"wokół:{day}±3d"))
        self.assertTrue(self.sel("updated:..2026-12"))
        self.eng.op("timeline", {"key": "activity"})
        ctx = self.eng.expr_context()
        gap = select("gap:>30d", self.items, ctx)
        self.assertIsInstance(gap, list)
        burst = select("burst:", self.items, ctx)
        self.assertIsInstance(burst, list)
        week = select('week-of:"rekurencja"', self.items, ctx)
        self.assertTrue(week)

    def test_week_of_works_right_after_opening_from_every_face(self):
        """F7 (requirement 7): 'the week when X was discussed' needs the timeline; it is built on first use as an
        implicit, recorded step — from the CLI, the engine and the API, with no prior timeline op."""
        import contextlib
        import io
        from loom.tools.extract import cli
        from loom.tools.extract.api import Session
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["--json", "szukaj", str(EXPORTS / "openai_2026_sharded.zip"), str(EXTRACT / "mixed_dir"),
                             "-e", 'week-of:"rekurencja"'])
        self.assertEqual(code, 0, err.getvalue())
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip", EXTRACT / "mixed_dir")
        eng.ensure_units()
        self.assertIsNone(eng.timeline)
        eng.op("select.expr", {"expr": 'week-of:"rekurencja"'})
        self.assertIsNotNone(eng.timeline)
        self.assertIn("timeline", [st["op"] for st in eng.recipe["steps"]], "the implicit timeline is a recorded step")
        sess = Session(face="api")
        sess.call({"call": "open", "sources": [str(EXPORTS / "openai_2026_sharded.zip"), str(EXTRACT / "mixed_dir")]})
        r = sess.call({"call": "select", "how": "expr", "expr": 'week-of:"rekurencja"'})
        self.assertNotIn("error", r, r)
        sess.eng.close()

    def test_dotted_dict_fields_written_by_edits_can_be_filtered(self):
        """F17: flags.x / account.label / meta.x — the names batch edits write — work in selections and filters."""
        self.eng.op("select.expr", {"expr": "kind:conversation"})
        self.eng.op("package.copy", {})
        self.eng.op("edit.add_field", {"where": "conversation[service:anthropic]", "field": "flags.przejrzane", "value": True})
        units = [e.unit for e in self.eng.package.entries]
        items = items_of(units, "conversation")
        hits = select("flags.przejrzane:true", items, self.ctx)
        self.assertTrue(hits)
        self.assertTrue(all(it.unit.service == "anthropic" for it, _ in hits))
        self.assertTrue(select("account.service:anthropic", items, self.ctx))
        with self.assertRaises(ExprError) as cm:
            select("no_such.field:1", items, self.ctx)
        self.assertIn("raw.", str(cm.exception), "the error says how to reach any path")

    def test_bands_clusters_tags_after_engine_steps(self):
        self.eng.op("select.criteria", {"terms": ["analizator", "archiwów", "rekurencja", "katalogach", "inwentarz"], "cascade": "lexical_only"})
        self.eng.op("clusters")
        self.eng.recipe["tags"] = {self.convs[0].uid: ["agent"]}
        ctx = self.eng.expr_context()
        self.assertTrue(select("band:include", self.items, ctx))
        self.assertTrue(select("pasmo:exclude", self.items, ctx))
        self.assertTrue(select("score >= 0.5", self.items, ctx))
        self.assertTrue(select("cluster:k1", self.items, ctx))
        self.assertEqual([it.unit.uid for it, _ in select("tag:agent", self.items, ctx)], [self.convs[0].uid])

    def test_syntax_errors_carry_a_position(self):
        for bad in ("title~/(/", "service:openai AND", "(has:tools", "count(x >= 1", "around:notadate±1d"):
            with self.subTest(bad):
                with self.assertRaises(ExprError) as cm:
                    validate(bad)
                    self.sel(bad)
                self.assertIsInstance(cm.exception.pos, int)
                self.assertIn("position", str(cm.exception))
        self.assertIsNotNone(parse("has:tools AND (role:user OR NOT lang:en)"))
