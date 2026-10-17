"""New readers against the generator's independent oracle (tests/fixtures/extract/EXPECTED.json):
Claude Code sessions, Gemini Takeout (JSON + HTML), Grok (two shapes), our graph packet and
loom.export, the mixed text-like directory with mixed time stamps."""
import json

from _support import EXPECTED_EXTRACT as E, EXTRACT, ExtractTest


class ClaudeCodeTest(ExtractTest):
    def test_session_with_tools_sidechain_compact_and_unknown_lines(self):
        eng, units = self.units(EXTRACT / "claude_code/projects/-home-user-proj")
        exp = E["claude_code/projects/-home-user-proj"]
        self.assertEqual([u.kind for u in units], [exp["kind"]] * exp["units"])
        s1 = next(u for u in units if u.ext_id == "sess-0001")
        o = exp["sess-0001"]
        roles = {r: sum(1 for m in s1.messages if m.role == r) for r in ("user", "assistant", "system")}
        self.assertEqual(len(s1.messages), o["messages"])
        self.assertEqual(roles, {"user": o["user"], "assistant": o["assistant"], "system": o["system"]})
        bc = self.block_counts(s1)
        self.assertEqual(bc["tool_use"], o["tool_use"])
        self.assertEqual(bc["tool_result"], o["tool_result"])
        self.assertEqual(bc["thinking"], o["thinking"])
        self.assertEqual(bc["text"], o["text"])
        self.assertEqual(bc["system"], o["system_blocks"])
        self.assertEqual(len(s1.tree["forks"]), o["forks"])
        self.assertEqual(s1.leaf_count(), o["leaves"])
        self.assertEqual(len(s1.unknown), o["unknown_entries"])
        self.assertEqual(s1.flags["internal_lines"], o["internal_lines"])
        self.assertEqual(len(s1.flags["summaries"]), o["summaries"])
        self.assertEqual(s1.flags["sidechain_messages"], o["sidechain"])
        self.assertEqual(s1.flags["compacted_messages"], o["compacted"])
        self.assertEqual(s1.tool_names(), o["tool_mentions"])
        self.assertEqual(s1.created_utc, o["created_utc"], "zone +02:00 normalised to UTC")
        self.assertIn("+02:00", str(s1.created), "the original stamp is kept")
        self.assertTrue(s1.is_agentic())
        # the tool result bound to its call carries the tool name
        results = [b for m in s1.messages for b in m.blocks if b.kind == "tool_result"]
        self.assertEqual(sorted(b.name for b in results), ["Edit", "Read"])
        self.assertTrue(any(b.input == {"is_error": True} for b in results))
        s2 = next(u for u in units if u.ext_id == "sess-0002")
        self.assertEqual(len(s2.messages), exp["sess-0002"]["messages"])
        self.assertFalse(s2.is_agentic())


class ClaudeCodeIdentityTest(ExtractTest):
    """F22/F23: a subagent transcript next to its session is its own unit (linked by flags.parent_session), never a
    'version' that hides the main session; a sidechain written last never decides the main path."""

    @staticmethod
    def line(uuid, parent, typ, ts, text, session="sess-A", **extra):
        d = {"uuid": uuid, "parentUuid": parent, "type": typ, "timestamp": ts, "sessionId": session, "isSidechain": False,
             "message": {"role": typ, "content": text}}
        d.update(extra)
        return json.dumps(d, ensure_ascii=False)

    def _project(self):
        proj = self.tmp / "projects" / "-home-user-x"
        main = [self.line("a1", None, "user", "2026-10-12T10:00:00Z", "Główne pytanie o importer"),
                self.line("a2", "a1", "assistant", "2026-10-12T10:01:00Z", "Główna odpowiedź"),
                self.line("a3", "a2", "user", "2026-10-12T10:02:00Z", "Dalsze pytanie"),
                # a subagent's lines interleaved at the end (its own root, written last)
                self.line("x1", None, "user", "2026-10-12T10:03:00Z", "Zadanie dla podagenta", isSidechain=True, agentId="ag1")]
        agent = [self.line("g1", None, "user", "2026-10-12T10:05:00Z", "Podagent: sprawdź testy", isSidechain=True, agentId="ag1"),
                 self.line("g2", "g1", "assistant", "2026-10-12T10:06:00Z", "Testy przechodzą", isSidechain=True, agentId="ag1")]
        self.write(proj / "sess-A.jsonl", "\n".join(main) + "\n")
        self.write(proj / "agent-ag1.jsonl", "\n".join(agent) + "\n")
        return proj

    def test_agent_file_is_a_related_unit_and_the_main_path_is_the_main_thread(self):
        eng, units = self.units(self._project())
        sessions = {u.ext_id: u for u in units if u.kind == "session"}
        self.assertEqual(set(sessions), {"sess-A", "agent-ag1"}, "two units, not one with a version")
        main, agent = sessions["sess-A"], sessions["agent-ag1"]
        self.assertEqual(main.versions, [], "the agent transcript is not a version of the session")
        self.assertEqual(agent.flags.get("parent_session"), "sess-A")
        self.assertTrue(agent.flags.get("agent_transcript"))
        on = {m.id for m in main.messages if m.on_current_path}
        self.assertEqual(on, {"a1", "a2", "a3"}, "the last line is a sidechain: the main thread stays current")
        eng.op("select.expr", {"expr": "kind:session"})
        eng.op("cut", {"profiles": ["flat"]})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        rows = [json.loads(x) for x in (self.tmp / "pkg/flat/messages.jsonl").read_text(encoding="utf-8").splitlines()]
        texts = [r["text"] for r in rows]
        self.assertIn("Dalsze pytanie", texts, "the flat profile keeps the main conversation")
        self.assertIn("Testy przechodzą", texts, "and the agent's transcript, as its own conversation")


class GeminiTest(ExtractTest):
    def test_takeout_json_grouped_by_gap_flagged_inferred(self):
        eng, units = self.units(EXTRACT / "gemini/takeout.zip")
        exp = E["gemini/takeout.zip"]
        convs = [u for u in units if u.kind == "conversation"]
        self.assertEqual(len(convs), exp["conversations"])
        self.assertEqual(sum(len(u.messages) for u in convs), exp["messages"])
        self.assertEqual(sum(len(u.attachments) for u in convs), exp["attachments"])
        self.assertEqual(sum(1 for u in convs for a in u.attachments if a.status == "missing"), exp["attachments_missing"])
        self.assertEqual(sum(len(u.unknown) for u in convs), exp["unknown_entries"])
        self.assertTrue(all(u.flags.get("grouping_inferred") for u in convs))
        self.assertEqual(min(u.created_utc for u in convs), exp["created_utc_first"])
        self.assertTrue(eng.archives[0].format and eng.ds.entry("formats", eng.archives[0].format).get("hypothesis"))

    def test_takeout_html(self):
        eng, units = self.units(EXTRACT / "gemini/MyActivity.html")
        exp = E["gemini/MyActivity.html"]
        convs = [u for u in units if u.kind == "conversation"]
        self.assertEqual(len(convs), exp["conversations"])
        self.assertEqual(sum(len(u.messages) for u in convs), exp["messages"])
        self.assertEqual(convs[0].created_utc, exp["created_utc"])


class GeminiDatesTest(ExtractTest):
    def test_zone_abbreviations_and_polish_dates_in_takeout_html(self):
        """F31: CEST/CET/PST are offsets from a data table (not dropped), and Polish-locale dates parse."""
        cells = [("Zapytano: Migracja pamięci", "Odpowiedź pierwsza.", "2 mar 2026, 10:00:00 CET"),
                 ("Zapytano: Druga sprawa", "Odpowiedź druga.", "2 mar 2026, 10:05:00 CET"),
                 ("Prompted Summer time", "Answer.", "Jul 1, 2026, 9:00:00 AM CEST"),
                 ("Prompted Pacific", "Answer.", "Jul 1, 2026, 9:00:00 PM PDT")]
        body = "".join(f'<div class="outer-cell"><div class="content-cell mdl-cell">{p}<br>{r}<br>{t}</div></div>' for p, r, t in cells)
        html = f"<!DOCTYPE html><html><head><title>Aplikacje Gemini</title></head><body><h1>Aplikacje Gemini</h1>{body}</body></html>"
        f = self.write(self.tmp / "Moja aktywność" / "MyActivity.html", html)
        eng, units = self.units(f)
        convs = sorted((u for u in units if u.kind == "conversation"), key=lambda u: u.created_utc)
        stamps = sorted(m.created_utc for u in convs for m in u.messages if m.role == "user")
        self.assertEqual(stamps[0], "2026-03-02T09:00:00Z", "10:00 CET = 09:00 UTC")
        self.assertIn("2026-07-01T07:00:00Z", stamps, "9:00 AM CEST = 07:00 UTC, not 2 h off")
        self.assertIn("2026-07-02T04:00:00Z", stamps, "9:00 PM PDT = 04:00 UTC next day")


class GrokTest(ExtractTest):
    def test_grok_com_shape_with_regeneration_fork(self):
        eng, units = self.units(EXTRACT / "grok/grok_com.json")
        exp = E["grok/grok_com.json"]
        convs = [u for u in units if u.kind == "conversation"]
        self.assertEqual(len(convs), exp["conversations"])
        self.assertEqual(sum(len(u.messages) for u in convs), exp["messages"])
        self.assertEqual(sum(len(u.tree["forks"]) for u in convs), exp["forks"])
        models: dict = {}
        for u in convs:
            for k, v in u.models().items():
                models[k] = models.get(k, 0) + v
        self.assertEqual(models, exp["models"])

    def test_x_archive_js(self):
        eng, units = self.units(EXTRACT / "grok/x_archive.zip")
        exp = E["grok/x_archive.zip"]
        convs = [u for u in units if u.kind == "conversation"]
        self.assertEqual(len(convs), exp["conversations"])
        self.assertEqual(sum(len(u.messages) for u in convs), exp["messages"])
        self.assertEqual(len(units), exp["units"], "the unknown member becomes an unknown unit, not silence")
        self.assertEqual(sum(1 for m in eng.archives[0].members if not m.format), exp["members_unknown"])


class OurFormatsTest(ExtractTest):
    def test_graph_packet(self):
        eng, units = self.units(EXTRACT / "ours/packet.json")
        exp = E["ours/packet.json"]
        self.assertEqual(len(units), exp["units"])
        g = units[0]
        self.assertEqual(g.kind, exp["kind"])
        self.assertEqual(len(g.messages), exp["messages"])
        self.assertEqual(len(g.unknown), exp["unknown_entries"], "the 'extras' key is reported, not dropped")

    def test_loom_export_conversation(self):
        eng, units = self.units(EXTRACT / "ours/conversation.json")
        exp = E["ours/conversation.json"]
        u = next(x for x in units if x.kind == "conversation")
        self.assertEqual(len(u.messages), exp["messages"])
        self.assertEqual(u.service, exp["service"])
        self.assertEqual(self.block_counts(u), exp["blocks"])
        self.assertEqual(len(u.attachments), exp["attachments"])
        self.assertEqual(u.attachments[0].status, "missing")


class MixedDirTest(ExtractTest):
    def test_text_like_files_through_engine_importer(self):
        eng, units = self.units(EXTRACT / "mixed_dir")
        exp = E["mixed_dir"]
        convs = [u for u in units if u.kind == "conversation"]
        self.assertEqual(len(convs), exp["conversations"])
        self.assertEqual(sum(len(u.messages) for u in convs), exp["messages"])
        self.assertTrue(all(u.flags.get("via_importer") for u in convs), "text-like formats go through engine.importer")
        self.assertEqual(sum(1 for u in units if u.kind == "unknown"), 1)

    def test_mixed_time_stamps_epoch_iso_zone_nozone_none(self):
        eng, units = self.units(EXTRACT / "mixed_dir")
        api = next(u for u in units if u.source.get("member") == "api_messages.json")
        got = [{"utc": m.created_utc if m.created_how != "conversation" else None, "how": m.created_how} for m in api.messages]
        self.assertEqual(got, E["mixed_dir"]["api_messages_times"])
        nozone = api.messages[2]
        self.assertTrue(nozone.created_inferred, "no zone in the source → flagged inferred")
        self.assertEqual(api.messages[1].created, "2026-05-05T10:00:00+02:00", "original kept")
