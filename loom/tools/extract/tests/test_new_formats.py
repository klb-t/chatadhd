"""New readers against the generator's independent oracle (tests/fixtures/extract/EXPECTED.json):
Claude Code sessions, Gemini Takeout (JSON + HTML), Grok (two shapes), our graph packet and
loom.export, the mixed text-like directory with mixed time stamps."""
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
