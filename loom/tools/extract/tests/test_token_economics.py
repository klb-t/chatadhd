"""P38 requirement 37, token economics (slice 4): measured per-turn counters from Claude Code transcripts (one API
response written as several lines counted once, subagent and workflow transcripts linked to their session),
the inventory's has_usage / has_tool_traces / reasoning, a saved session list (never an API call), commits per
session from a git repository built here (trailers, main, lines), prices as data (unknown = said, never 0),
the estimator for sources without counters (method named, tiktoken only offline), the report in Markdown / CSV /
JSON, determinism, aggregate mode, and the three faces (CLI verb + alias, commander, serve) on one op.
Synthetic fixtures only (``gen_extract_fixtures.py``: ``economics/``)."""
import contextlib
import csv
import io
import json
import os
import shutil
import subprocess
import sys
import types
import unittest
from pathlib import Path

from _support import EXPECTED_EXTRACT, EXTRACT, ExtractTest

from loom.tools.extract import cli, i18n
from loom.tools.extract.datafiles import store
from loom.tools.extract.economics import load_session_lists, trailer_regex
from loom.tools.extract.prices import PriceBook, cost_of
from loom.tools.extract.tokencount import Estimator, calibration

E = EXPECTED_EXTRACT["economics"]
PROJ = EXTRACT / "economics" / "projects" / "-home-user-econ"
SESSIONS = EXTRACT / "economics" / "sessions.json"
CHATGPT = EXTRACT / "versions" / "chatgpt_2025-10-01.zip"
PKG_DIR = Path(__file__).resolve().parents[1]
HAS_GIT = shutil.which("git") is not None


def make_repo(path: Path) -> Path:
    """A tiny repository: two commits of session_TEST1 on main, one of session_TEST3 on a side branch, one
    without a trailer on main; fixed dates, so the hashes are the same on every run."""
    path.mkdir(parents=True, exist_ok=True)
    base = {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.invalid", "GIT_COMMITTER_NAME": "Test",
            "GIT_COMMITTER_EMAIL": "test@example.invalid", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
            "HOME": str(path), "PATH": os.environ.get("PATH", "/usr/bin:/bin")}

    def git(*args: str, date: str = "2026-10-10T08:10:00Z") -> None:
        subprocess.run(["git", "-C", str(path), "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=main", *args], check=True,
                       capture_output=True, env={**base, "GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date})

    def commit(msg: str, date: str) -> None:
        git("add", "-A", date=date)
        git("commit", "-q", "-m", msg, date=date)
    git("init", "-q")
    (path / "a.txt").write_text("1\n2\n3\n", encoding="utf-8")
    commit("feat: a\n\nClaude-Session: https://claude.ai/code/session_TEST1", "2026-10-10T08:10:00Z")
    (path / "a.txt").write_text("1\n2\nthree\n", encoding="utf-8")
    commit("fix: a\n\nCo-Authored-By: Test <test@example.invalid>\nClaude-Session: https://claude.ai/code/session_TEST1", "2026-10-11T09:05:00Z")
    git("checkout", "-q", "-b", "side")
    (path / "b.txt").write_text("x\ny\n", encoding="utf-8")
    commit("docs: b\n\nClaude-Session: https://claude.ai/code/session_TEST3", "2026-10-11T10:00:00Z")
    git("checkout", "-q", "main")
    (path / "c.txt").write_text("z\n", encoding="utf-8")
    commit("chore: no trailer", "2026-10-12T10:00:00Z")
    return path


class EconTest(ExtractTest):
    def econ(self, *sources, aggregate: bool = False, **params):
        eng = self.engine(*sources, aggregate=aggregate)
        res = eng.op("token_economics", params, by="api")
        return eng, res["economics"]

    def prices_overlay(self, *, web_search: bool = True, small_currency: str = "USD", gpt: bool = False) -> None:
        d = store().overlay / "prices"
        d.mkdir(parents=True, exist_ok=True)
        entries = [{"pattern": "claude-x-large", "input": 10.0, "output": 50.0, "cache_write": 12.5, "cache_read": 1.0,
                    "web_search_per_1000": 10.0 if web_search else None, "valid_from": "2026-01-01", "source": "synthetic test price"},
                   {"pattern": "claude-x-small", "input": 1.0, "output": 5.0, "cache_write": 1.25, "cache_read": 0.1,
                    "valid_from": "2026-01-01", "source": "synthetic test price", "currency": small_currency}]
        if gpt:
            entries.append({"pattern": "gpt-*", "input": 2.0, "output": 8.0, "valid_from": "2020-01-01", "source": "synthetic test price"})
        (d / "synthetic.json").write_text(json.dumps({"id": "synthetic", "currency": "USD", "entries": entries}), encoding="utf-8")
        from loom.tools.extract.datafiles import DataStore, set_store
        set_store(DataStore(overlay=store().overlay))  # a fresh store: the overlay written now is the one read


class ReaderAndInventoryTest(EconTest):
    def test_per_turn_usage_and_transcript_placement(self):
        eng, units = self.units(PROJ)
        by = {u.ext_id: u for u in units if u.kind == "session"}  # the meta/run files next to them are other members
        self.assertEqual(set(by), {"sess-E1", "sess-E2", "sess-E1/subagents/agent-s1", "sess-E1/subagents/workflows/run-1/agent-w1",
                                   "sess-E1/subagents/workflows/run-1/agent-w2"})
        main = by["sess-E1"]
        self.assertEqual(main.flags["transcript_role"], "main")
        lines_a1 = [m for m in main.messages if m.meta.get("api_id") == "msg_A1"]
        self.assertEqual(len(lines_a1), 3, "one response, three lines, each kept as written")
        self.assertEqual(lines_a1[-1].meta["usage"]["output"], 120)
        self.assertEqual(lines_a1[0].meta["usage"]["output"], 30, "the first line carries a partial count")
        self.assertEqual(lines_a1[-1].meta["usage"]["tier"], "standard")
        a3 = next(m for m in main.messages if m.meta.get("api_id") == "msg_A3")
        self.assertIn("future_counter", a3.meta["usage"]["extra"], "an unmapped usage field is kept, not dropped")
        self.assertEqual(a3.meta["usage"]["web_search_requests"], 1, "server tool requests are counters of the format map")
        self.assertEqual(a3.meta["request_id"], "req_msg_A3")
        sub = by["sess-E1/subagents/agent-s1"]
        self.assertEqual((sub.flags["transcript_role"], sub.flags["parent_session"], sub.flags["parent_session_how"]),
                         ("subagent", "sess-E1", "path"))
        wf = by["sess-E1/subagents/workflows/run-1/agent-w1"]
        self.assertEqual((wf.flags["transcript_role"], wf.flags["workflow_run"], wf.flags["parent_session"]), ("workflow_agent", "run-1", "sess-E1"))
        self.assertEqual(main.stats["usage_messages"], 6, "every line with counters (3 responses + the synthetic + 2 repeated)")
        # stop_reason: set on the line with the final counters, null while the response streams
        self.assertEqual([m.meta.get("usage_final") for m in lines_a1], [False, False, True])
        w2 = by["sess-E1/subagents/workflows/run-1/agent-w2"]
        self.assertEqual([m.meta.get("usage_final") for m in w2.messages if m.role == "assistant"], [False, False],
                         "an agent transcript whose every line has stop_reason null: the stream-start snapshot")
        # measured metadata next to the transcripts: the agent's meta file and the workflow run's file
        self.assertEqual(sub.flags["agent_meta"], {"agent_type": "general-purpose", "description": "Czytanie raportu"})
        self.assertEqual(wf.flags["workflow_meta"]["agents"], 2)
        self.assertEqual(wf.flags["workflow_meta"]["total_tokens"], 1300)

    def test_inventory_has_usage_tool_traces_reasoning(self):
        eng = self.engine(PROJ, CHATGPT)
        inv = eng.op("inventory")["inventory"]
        ef = inv["economics"]["by_format"]
        cc, gpt = ef["anthropic.claude_code.session"], ef["openai.chatgpt.export"]
        self.assertTrue(cc["has_usage"])
        self.assertTrue(cc["has_tool_traces"])
        self.assertEqual(cc["reasoning"], "short", "a thinking block below thinking.full_min_chars: present, not full")
        self.assertFalse(gpt["has_usage"])
        self.assertEqual(gpt["reasoning"], "none")
        arch = {a["format"]: a["economics"] for a in inv["archives"]}
        self.assertTrue(arch["anthropic.claude_code.session"]["has_usage"])
        self.assertIn("has_usage", inv["how"]["economics"])

    def test_reasoning_full_and_summary_measured(self):
        proj = self.tmp / "p"
        long = "x" * 250
        line = {"uuid": "a", "parentUuid": None, "type": "assistant", "timestamp": "2026-10-10T08:00:00Z", "sessionId": "s",
                "message": {"role": "assistant", "content": [{"type": "thinking", "thinking": long}]}}
        self.write(proj / "s.jsonl", json.dumps(line) + "\n")
        inv = self.engine(proj).op("inventory")["inventory"]
        self.assertEqual(inv["economics"]["total"]["reasoning"], "full")
        self.assertFalse(inv["economics"]["total"]["has_usage"])
        # ChatGPT keeps only summaries of reasoning (thoughts / reasoning_recap): the export fixture of the importer has them
        from _support import EXPORTS
        inv2 = self.engine(EXPORTS / "openai_2026_sharded.zip").op("inventory")["inventory"]
        self.assertIn(inv2["economics"]["total"]["reasoning"], ("summary", "none"))
        if inv2["economics"]["total"]["reasoning_blocks"]["blocks"]:
            self.assertEqual(inv2["economics"]["total"]["reasoning"], "summary")


class MeasuredTest(EconTest):
    def test_totals_deduplication_and_notes(self):
        eng, doc = self.econ(PROJ)
        tot = doc["totals"]
        for k, v in E["totals"].items():
            self.assertEqual(tot[k], v, k)
        self.assertEqual(tot["turns_measured"], E["turns"])
        self.assertEqual(tot["turns_estimated"], 0)
        self.assertEqual(tot["total"], sum(E["totals"].values()))
        self.assertAlmostEqual(tot["cache_share"], round(E["totals"]["cache_read"] / sum(E["totals"].values()), 4))
        notes = {n["key"]: n for n in doc["unknowns"]}
        self.assertEqual(notes["duplicate_lines"]["n"], E["duplicate_lines"])
        self.assertEqual(notes["ignored_model_lines"]["n"], E["ignored_synthetic"])
        self.assertEqual(notes["assistant_without_counters"]["n"], E["assistant_without_counters"])
        self.assertEqual(sorted(notes["usage_fields_unmapped"]["fields"]), E["unmapped_usage_fields"])
        self.assertEqual(tot["web_search_requests"], E["web_search_requests"])
        self.assertEqual(notes["cost_unknown_turns"]["n"], E["turns"], "no shipped price: every cost unknown, never 0")
        self.assertFalse(tot["cost"]["known"])
        self.assertIsNone(tot["cost"]["amount"])

    def test_sessions_runs_workflows_models_tools_days(self):
        eng, doc = self.econ(PROJ)
        fams = {r["transcript_id"]: r for r in doc["sessions"]}
        e1 = fams["sess-E1"]
        for k, v in E["family_E1"].items():
            self.assertEqual(e1["tr_" + k], v, k)
        self.assertEqual((e1["transcripts"], e1["agents"]), (E["transcripts_E1"], E["agents_E1"]))
        self.assertAlmostEqual(e1["main_share"], round(sum(E["main_E1"].values()) / sum(E["family_E1"].values()), 4))
        self.assertEqual((e1["files_touched"], e1["edit_calls"], e1["edits_failed"]),
                         (len(E["files_touched_E1"]), E["edit_calls_E1"], E["edits_failed_E1"]))
        self.assertEqual(e1["commit_commands"], 1)
        roles = sorted(r["role"] for r in doc["runs"])
        self.assertEqual(roles.count(i18n.t("economics.role.workflow_agent")), E["workflow_agents"])
        self.assertEqual(len(doc["workflow_runs"]), 1)
        self.assertEqual(doc["workflow_runs"][0]["agents"], 2)
        models = {m["model"]: m for m in doc["models"]}
        self.assertEqual({k: models[k]["turns_measured"] for k in E["models"]}, E["models"])
        tools = {t["tool"]: t for t in doc["tools"]}
        self.assertEqual({k: tools[k]["calls"] for k in E["tool_calls"]}, E["tool_calls"])
        self.assertEqual(tools["Edit"]["failed"], E["tool_failed"]["Edit"])
        # msg_A3 called Write and Edit: its tokens split half and half (preset economics.tool_attribution: split)
        a1, a3 = (10, 120, 1000, 0), (4, 60, 100, 1200)
        self.assertEqual((tools["Edit"]["input"], tools["Edit"]["output"], tools["Edit"]["cache_read"]),
                         (a1[0] + a3[0] // 2, a1[1] + a3[1] // 2, a1[3] + a3[3] // 2))
        split_sum = sum(t["total"] for t in doc["tools"])
        self.assertEqual(split_sum, doc["totals"]["total"], "split attribution adds up to the total")
        days = {d["day"]: d for d in doc["days"]}
        self.assertEqual({k: days[k]["turns_measured"] for k in E["days"]}, E["days"])

    def test_day_zone_preset_and_unknown_zone(self):
        try:
            from zoneinfo import ZoneInfo
            ZoneInfo("Pacific/Kiritimati")
        except Exception:  # noqa: BLE001
            self.skipTest("no IANA zone data on this machine (tzdata)")
        eng, doc = self.econ(PROJ, day_zone="Pacific/Kiritimati")  # UTC+14: every 2026-10-10 turn falls on 10-10 or 10-11
        days = {d["day"]: d["turns_measured"] for d in doc["days"]}
        self.assertEqual(sum(days.values()), E["turns"])
        self.assertNotEqual(days, E["days"], "the zone moves turns across midnight")
        eng2, doc2 = self.econ(PROJ, day_zone="Mars/Olympus_Mons")
        self.assertIn("day_zone_unknown", {n["key"] for n in doc2["unknowns"]})
        self.assertEqual({d["day"]: d["turns_measured"] for d in doc2["days"]}, E["days"], "counted in UTC, said")

    def test_session_list_link_by_trailer_and_coverage(self):
        eng, doc = self.econ(PROJ, sessions=[str(SESSIONS)])
        rows = {r["id"]: r for r in doc["sessions"]}
        t1 = rows[E["link_E1"]]
        self.assertEqual(t1["cost_api"], {"amount": E["api_cost_TEST1"], "known": True, "currency": "USD", "basis": "session_api"})
        self.assertEqual(t1["link"], i18n.t("economics.link.trailer"))
        self.assertAlmostEqual(t1["coverage"], round(E["family_E1"]["cache_read"] / E["api_cache_read_TEST1"], 4))
        self.assertEqual(rows["session_TEST2"]["transcripts"], 0)
        self.assertEqual(rows["session_TEST2"]["why"]["tr_turns"], "no_transcript")
        self.assertEqual(rows["sess-E2"]["why"]["api_input"], "not_in_session_list")
        notes = {n["key"] for n in doc["unknowns"]}
        self.assertIn("api_sessions_without_transcript", notes)
        self.assertIn("transcripts_without_session_list", notes)
        # an explicit link wins over the trailer
        eng2, doc2 = self.econ(PROJ, sessions=[str(SESSIONS)], links={"sess-E2": "session_TEST2"})
        r2 = {r["id"]: r for r in doc2["sessions"]}
        self.assertEqual(r2["session_TEST2"]["link"], i18n.t("economics.link.explicit"))
        self.assertEqual(r2["session_TEST2"]["tr_turns"], 1)

    def test_session_list_shapes(self):
        rows, problems = load_session_lists([str(SESSIONS)], store())
        self.assertEqual(set(rows), {"session_TEST1", "session_TEST2"})
        jl = self.tmp / "s.jsonl"
        doc = json.loads(SESSIONS.read_text(encoding="utf-8"))
        jl.write_text("\n".join(json.dumps(x) for x in doc["ccr"]["data"]) + "\nnot json\n", encoding="utf-8")
        rows2, problems2 = load_session_lists([str(jl), str(SESSIONS)], store())
        self.assertEqual(set(rows2), set(rows))
        keys = [p["key"] for p in problems2]
        self.assertIn("sessions_bad_line", keys)
        self.assertEqual(keys.count("sessions_duplicate"), 2, "the same sessions in two files: counted once, said")
        one = self.tmp / "one.json"  # get_session: {"ccr": {session}, "lineage": …}
        one.write_text(json.dumps({"ccr": doc["ccr"]["data"][0], "lineage": {"depth": 0}}), encoding="utf-8")
        rows1, _ = load_session_lists([str(one)], store())
        self.assertEqual(list(rows1), ["session_TEST1"])
        self.assertEqual(rows1["session_TEST1"]["cost_usd"], 1.5)
        self.assertEqual(load_session_lists([str(SESSIONS)], store())[0]["session_TEST2"]["parent"], "session_TEST1")
        partial = self.tmp / "partial.json"
        partial.write_text(json.dumps([{"id": "session_X", "title": "bez liczników"}]), encoding="utf-8")
        rows3, _ = load_session_lists([str(partial)], store())
        self.assertIsNone(rows3["session_X"]["cache_read"], "a missing counter is unknown, never 0")
        self.assertIn("cost_usd", rows3["session_X"]["missing"])

    def test_trailer_regex(self):
        rx = trailer_regex({})
        self.assertEqual(rx.findall('git commit -m "x\\n\\nClaude-Session: https://claude.ai/code/session_A1"'), ["session_A1"])
        self.assertEqual(trailer_regex({}, line=True).findall("t\n\nClaude-Session: https://claude.ai/code/session_B2\n"), ["session_B2"])


@unittest.skipUnless(HAS_GIT, "git is not installed: commits per session cannot be read")
class GitOutcomesTest(EconTest):
    def test_commits_per_session_main_lines_cost_per_commit(self):
        repo = make_repo(self.tmp / "repo")
        eng, doc = self.econ(PROJ, sessions=[str(SESSIONS)], repos=[str(repo)])
        rows = {r["id"]: r for r in doc["sessions"]}
        t1 = rows["session_TEST1"]
        self.assertEqual((t1["commits"], t1["commits_main"], t1["lines_added"], t1["lines_removed"]), (2, 2, 4, 1))
        self.assertEqual(t1["cost_per_commit"]["amount"], 0.75)
        self.assertEqual(t1["cost_per_commit_main"]["amount"], 0.75)
        t3 = rows["session_TEST3"]  # known only from a commit trailer, on a side branch
        self.assertEqual((t3["commits"], t3["commits_main"]), (1, 0))
        self.assertFalse(t3["cost_per_commit"]["known"])
        self.assertEqual(rows["session_TEST2"]["commits"], 0)
        self.assertEqual(rows["session_TEST2"]["cost_per_commit"]["reason"], "no_commits")
        oc = doc["outcomes"]
        self.assertEqual((oc["commits_all"], oc["commits_with_session"], oc["commits_without_session"]), (4, 3, 1))
        self.assertEqual((doc["totals"]["commits"], doc["totals"]["commits_main"]), (3, 2))
        days = {d["day"]: (d["commits"], d["commits_all"]) for d in doc["days"]}
        self.assertEqual((days["2026-10-10"], days["2026-10-11"], days["2026-10-12"]), ((1, 1), (2, 2), (0, 1)),
                         "Commits = with a session trailer (as in the totals); all commits in their own column")
        self.assertEqual(sum(d["commits"] for d in doc["days"]), doc["totals"]["commits"])
        self.assertEqual(sum(d["commits_all"] for d in doc["days"]), doc["totals"]["commits_all"])
        self.assertIn("commits_session_without_rows", {n["key"] for n in doc["unknowns"]})
        # no main branch: commits on main are unknown with the reason, never 0
        eng2, doc2 = self.econ(PROJ, sessions=[str(SESSIONS)], repos=[str(repo)], main_ref="trunk")
        r2 = {r["id"]: r for r in doc2["sessions"]}
        self.assertIsNone(r2["session_TEST1"]["commits_main"])
        self.assertEqual(r2["session_TEST1"]["why"]["commits_main"], "main_ref_unknown")
        # a directory that is not a repository: said, the rest of the report stands
        eng3, doc3 = self.econ(PROJ, repos=[str(self.tmp / "nothing-here")])
        self.assertIn("git_error", {n["key"] for n in doc3["unknowns"]})
        md = eng3.op("token_economics", {"repos": [str(self.tmp / "nothing-here")], "main_ref": "trunk"})["markdown"]
        self.assertIn(i18n.t("economics.why.git_log_failed"), md, "the code is said as a sentence")
        self.assertNotIn("economics.why.", md, "no key shown in place of a sentence")

    def test_sources_less_run_session_list_and_repo_only(self):
        repo = make_repo(self.tmp / "repo")
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["--json", "ekonomia", "--sesje", str(SESSIONS), "--repo", str(repo), "--pakiet", str(self.tmp / "pkg")])
        self.assertEqual(code, 0, err.getvalue())
        doc = next(r for r in json.loads(out.getvalue())["results"] if r["op"] == "token_economics")["economics"]
        rows = {r["id"]: r for r in doc["sessions"]}
        self.assertEqual(rows["session_TEST1"]["cost_per_commit"]["amount"], 0.75, "the S1 table: API cost / commits")
        self.assertTrue((self.tmp / "pkg" / "economics" / "sessions_api.csv").exists())


class PricesTest(EconTest):
    def test_price_book_patterns_dates_and_unknowns(self):
        d = store().overlay / "prices"
        d.mkdir(parents=True, exist_ok=True)
        (d / "t.json").write_text(json.dumps({"id": "t", "currency": "USD", "entries": [
            {"pattern": "model-a*", "input": 1, "output": 2, "cache_write": None, "cache_read": None, "valid_from": "2026-01-01", "source": "s1"},
            {"pattern": "model-a*", "input": 3, "output": 4, "valid_from": "2026-06-01", "source": "s2"},
            {"pattern": "model-a-special", "input": 9, "output": 9, "valid_from": "2026-01-01", "source": "s3"}]}), encoding="utf-8")
        book = PriceBook(store())
        self.assertEqual(book.lookup("model-a-1", "2026-03-01")["source"], "s1")
        self.assertEqual(book.lookup("model-a-1", "2026-07-01")["source"], "s2", "the newest price valid on that day")
        self.assertEqual(book.lookup("vendor/model-a-1", "2026-07-01")["source"], "s2", "a provider prefix is ignored")
        self.assertEqual(book.lookup("model-a-special", "2026-07-01")["source"], "s3", "the most specific pattern wins")
        self.assertEqual(book.lookup("model-a-1", "2025-12-31")["reason"], "not_yet_valid")
        self.assertEqual(book.lookup("claude-anything", "2026-07-01")["reason"], "entry_without_price", "shipped: no invented price")
        self.assertEqual(book.lookup("totally-unknown", None)["reason"], "no_entry")
        hit = book.lookup("model-a-1", "2026-03-01")
        self.assertEqual(cost_of({"input": 1_000_000, "output": 1_000_000}, hit)["amount"], 3.0)
        c = cost_of({"input": 10, "cache_read": 5}, hit)
        self.assertFalse(c["known"])
        self.assertEqual(c["missing"], ["cache_read"], "a token kind without its price: unknown, named")

    def test_costs_with_prices(self):
        self.prices_overlay()
        eng, doc = self.econ(PROJ)
        # msg_A1 at claude-x-large: 10×10 + 120×50 + 1000×12.5 + 0×1 per million
        a1 = (10 * 10 + 120 * 50 + 1000 * 12.5) / 1e6
        run = next(r for r in doc["runs"] if r["id"] == "sess-E1")
        self.assertTrue(run["cost"]["known"])
        a2 = (5 * 10 + 40 * 50 + 200 * 12.5 + 1000 * 1) / 1e6
        a3 = (4 * 10 + 60 * 50 + 100 * 12.5 + 1200 * 1) / 1e6 + 1 * 10.0 / 1000  # + one web search at 10 per 1000
        self.assertAlmostEqual(run["cost"]["amount"], round(a1 + a2 + a3, 6), places=6)
        self.assertEqual(run["cost"]["basis"], "measured")
        self.assertTrue(doc["totals"]["cost"]["known"])
        models = {m["model"]: m for m in doc["models"]}
        self.assertIn("synthetic test price", models["claude-x-large"]["price"])
        self.assertTrue(all(p["known"] for p in doc["prices"]))


class EstimateTest(EconTest):
    def test_sources_without_counters_are_estimated_and_labelled(self):
        eng, doc = self.econ(CHATGPT)
        tot = doc["totals"]
        self.assertEqual(tot["turns_measured"], 0)
        self.assertGreater(tot["turns_estimated"], 0)
        self.assertIsNone(tot["total"], "no counters: the measured total is unknown, not 0")
        self.assertEqual(tot["why"]["total"], "no_counters")
        self.assertGreaterEqual(tot["est_context"], tot["est_input"])
        self.assertTrue(all(k.startswith("chars/") for k in tot["methods_raw"]), tot["methods_raw"])
        if i18n.language() == "pl":
            self.assertIn("znaki/", tot["methods"], "the method is said in the run's language")
        prov = doc["providers"][0]
        self.assertFalse(prov["has_usage"])
        self.assertTrue(any(m["model"].startswith("gpt-") for m in doc["models"]), "model_slug per message")
        self.assertTrue(doc["method"]["expected_error"])
        self.assertTrue(all(e["basis"] == "none" for e in doc["method"]["expected_error"].values()),
                        "no measured turns: the error is unknown, said")

    def test_calibration_and_chars(self):
        cal = calibration([(90, 100), (110, 100), (100, 100)])
        self.assertEqual((cal["turns"], cal["median"]), (3, 0.0))
        self.assertIsNone(calibration([]))
        est = Estimator(store(), "chars")
        n, how = est.count("a" * 32, "gpt-4o", "pl")
        self.assertEqual((n, how), (10, "chars/3.2 (pl)"))
        eng, doc = self.econ(PROJ)
        ee = doc["method"]["expected_error"]
        self.assertEqual([e["basis"] for e in ee.values()], ["calibration"])
        # only final, thinking-free responses with output >= economics.estimate.calibration_min_output (50): msg_A3
        self.assertEqual(next(iter(ee.values()))["turns"], 1)

    def test_tiktoken_only_offline(self):
        """A fake tiktoken that would download its encoding: the load runs with the network refused, so the
        estimator says why and counts characters — no connection is ever attempted past the guard."""
        attempts = []

        def get_encoding(name):
            import socket
            attempts.append(name)
            socket.create_connection(("example.invalid", 443))  # what a download would do
            raise AssertionError("unreachable")
        fake = types.ModuleType("tiktoken")
        fake.get_encoding = get_encoding
        old = sys.modules.get("tiktoken")
        sys.modules["tiktoken"] = fake
        try:
            est = Estimator(store(), "auto")
            n, how = est.count("abcd" * 10, "gpt-4o-mini", "en")
            self.assertTrue(how.startswith("chars/"), how)
            self.assertEqual(est.describe()["unavailable"], {"o200k_base": "tiktoken_offline_missing"})
            self.assertEqual(attempts, ["o200k_base"])

            class Enc:
                def encode(self, text, disallowed_special=()):
                    return text.split()
            fake.get_encoding = lambda name: Enc()
            est2 = Estimator(store(), "auto")
            self.assertEqual(est2.count("one two three", "gpt-4o", "en"), (3, "tiktoken:o200k_base"))
            self.assertTrue(est2.count("one two three", "claude-x-large", "en")[1].startswith("chars/"),
                            "other vendors: characters (preset economics.estimate.tiktoken.other_models)")
        finally:
            if old is None:
                sys.modules.pop("tiktoken", None)
            else:
                sys.modules["tiktoken"] = old


class OutputsTest(EconTest):
    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(list(argv))
        self.assertEqual(code, 0, err.getvalue())
        return out.getvalue()

    def test_package_files_markdown_csv_json_and_determinism(self):
        args = ["ekonomia", str(PROJ), str(CHATGPT), "--sesje", str(SESSIONS)]
        text = self.run_cli(*args, "--pakiet", str(self.tmp / "a"))
        self.assertIn("# Ekonomia tokenów", text)
        self.run_cli(*args, "--pakiet", str(self.tmp / "b"))
        econ = self.tmp / "a" / "economics"
        from loom.tools.extract.economics import layout
        from loom.tools.extract.economics import csv_header
        lay = layout(store())
        for tab in lay["tables"]:
            rows = list(csv.reader(io.StringIO((econ / f"{tab['id']}.csv").read_text(encoding="utf-8"))))
            self.assertEqual(rows[0], csv_header(tab, lay["columns"]))
        self.assertTrue((econ / "ekonomia.md").exists())
        doc = json.loads((econ / "economics.json").read_text(encoding="utf-8"))
        self.assertEqual(doc["schema"], "loom.extract.economics/1")
        man_a = json.loads((self.tmp / "a" / "manifest.json").read_text(encoding="utf-8"))
        man_b = json.loads((self.tmp / "b" / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(man_a["reproducible_hash"], man_b["reproducible_hash"], "same inputs, same bytes")
        self.assertIn("economics/economics.json", [o["path"] for o in man_a["outputs"]])
        self.assertEqual(man_a["economics"]["turns_measured"], E["turns"])
        self.assertIn("economics/", (self.tmp / "a" / "RAPORT.md").read_text(encoding="utf-8"))
        # unknown is visible in every format, never an empty cell or 0: in CSV the reason code sits in <col>_why
        tot = next(csv.DictReader(io.StringIO((econ / "totals.csv").read_text(encoding="utf-8"))))
        self.assertEqual((tot["cost_amount"], tot["cost_why"]), ("", "entry_without_price"))
        self.assertIn(i18n.t("economics.unknown_cost", why=i18n.t("economics.why.entry_without_price")), (econ / "ekonomia.md").read_text(encoding="utf-8"))
        # the recipe carries the op; replaying it gives the same economics bytes
        rec = json.loads((self.tmp / "a" / "recipe.json").read_text(encoding="utf-8"))
        self.assertIn("token_economics", [s["op"] for s in rec["steps"]])
        self.run_cli("przepis", "uruchom", str(self.tmp / "a" / "recipe.json"), "--pakiet", str(self.tmp / "c"))
        self.assertEqual((self.tmp / "c" / "economics" / "economics.json").read_bytes(), (econ / "economics.json").read_bytes())

    def test_english_and_csv_to_stdout(self):
        text = self.run_cli("--jezyk", "en", "tokens", str(PROJ), "--pakiet", str(self.tmp / "en"))
        self.assertIn("# Token economics", text)
        self.assertTrue((self.tmp / "en" / "economics" / "economics.md").exists())
        i18n.set_language("pl")
        out = self.run_cli("ekonomia", str(PROJ), "--csv", "models")
        rows = list(csv.reader(io.StringIO(out)))
        self.assertEqual(rows[0][0], "model")
        self.assertEqual(sorted(r[0] for r in rows[1:]), sorted(E["models"]))

    def test_aggregate_mode_counts_only(self):
        if not HAS_GIT:
            self.skipTest("git is not installed")
        repo = make_repo(self.tmp / "repo")
        pkg = self.tmp / "agg"
        text = self.run_cli("--zbiorczo", "--json", "ekonomia", str(PROJ), str(CHATGPT), "--sesje", str(SESSIONS), "--repo", str(repo),
                            "--polacz", "sess-E2=session_TEST2", "--pakiet", str(pkg))
        files = sorted(p for p in pkg.rglob("*") if p.is_file())
        blob = text + "".join(p.read_text(encoding="utf-8") for p in files) + "\n".join(str(p.relative_to(pkg)) for p in files)
        names = [p.name for p in PROJ.rglob("*")] + [SESSIONS.name, CHATGPT.name]
        for needle in E["forbidden_in_aggregate"] + EXPECTED_EXTRACT["forbidden_in_aggregate"] + names + [str(repo), str(EXTRACT)]:
            self.assertNotIn(needle, blob, needle)
        doc = json.loads((pkg / "economics" / "economics.json").read_text(encoding="utf-8"))
        self.assertTrue(doc["aggregate"])
        self.assertTrue(all(r["label"].startswith(i18n.t("economics.label.session") + "-") for r in doc["sessions"]))
        self.assertTrue(any(r.get("commits") == 2 for r in doc["sessions"]), "the numbers stay")
        rec = json.loads((pkg / "recipe.json").read_text(encoding="utf-8"))
        step = next(s for s in rec["steps"] if s["op"] == "token_economics")
        self.assertEqual(step["params"]["sessions"], ["sessions-1"])
        self.assertEqual(step["params"]["repos"], ["repos-1"])
        self.assertEqual(step["params"]["links"], {"count": 1})


class FacesTest(EconTest):
    def test_registered_once_reachable_from_cli_commander_and_serve(self):
        from loom.tools.extract.api import Session
        from loom.tools.extract.commander.driver import Driver
        from loom.tools.extract.commander.state import CommanderState
        params = {"sessions": [str(SESSIONS)]}
        # API (the dispatcher serve and the commander share)
        sess = Session(face="api", ds=store())
        self.addCleanup(sess.close)
        sess.call({"call": "open", "sources": [str(PROJ)]})
        res = sess.call({"call": "op", "op": "token_economics", "params": params, "wait": True})
        doc_api = (res.get("result") or res)["economics"]
        # CLI verb
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["--json", "ekonomia", str(PROJ), "--sesje", str(SESSIONS)])
        self.assertEqual(code, 0, err.getvalue())
        doc_cli = next(r for r in json.loads(out.getvalue())["results"] if r["op"] == "token_economics")["economics"]
        self.assertEqual(doc_cli, doc_api, "one engine: the same document from the CLI and the API")
        # commander: the F2 menu lists it (data: layout.json#menu_ops) and the command line runs it
        tui = Session(face="tui", ds=store())
        self.addCleanup(tui.close)
        tui.call({"call": "open", "sources": [str(PROJ)]})
        st = CommanderState(tui, width=140, height=40)
        d = Driver(st).settle()
        self.assertIn("token_economics", st.layout["menu_ops"])
        d.press(":")
        d.type("op token_economics " + json.dumps(params))
        d.press("Enter")
        d.settle()
        self.assertEqual(st.mode, "viewer")
        self.assertIn("# Ekonomia tokenów", "\n".join(st.overlay["lines"]))
        self.assertEqual(tui.eng.economics, doc_api)
        # recorded as a recipe step by every face
        self.assertIn("token_economics", [s["op"] for s in tui.eng.recipe["steps"]])

    def test_ops_and_i18n_registered(self):
        from loom.tools.extract import ops as _ops
        from loom.tools.extract.recipe import ops_registry
        self.assertIn("token_economics", _ops.OPS)
        self.assertIn("token_economics", ops_registry(store())["ops"])
        pl, en = i18n.table("pl"), i18n.table("en")
        src = (PKG_DIR / "economics.py").read_text(encoding="utf-8") + (PKG_DIR / "cli.py").read_text(encoding="utf-8")
        import re
        used = {k for k in re.findall(r'(?<![A-Za-z_.])t\(["\']((?:economics|cli|report)\.[a-z_.]+)["\']', src) if not k.endswith(".")}
        families = {"economics.why.": ["no_entry", "no_model", "not_yet_valid", "entry_without_price", "price_field_missing", "no_price",
                                       "no_turns", "commits_unknown", "no_commits", "no_commits_on_main", "main_ref_unknown", "no_cost_field",
                                       "no_session_list", "not_in_session_list", "no_transcript", "not_measured", "no_column",
                                       "no_calibration", "tiktoken_missing", "tiktoken_offline_missing", "tiktoken_error", "no_counters",
                                       "measured_instead", "no_field_in_session_list", "no_main_ref", "main_ref_off",
                                       "git_unavailable", "git_log_failed", "no_turns_measured", "no_turns_estimated", "mixed_currency",
                                       "api_zero", "git_error", "partial_clone", "no_calibration_for_method", "no_agent_meta",
                                       "not_an_agent", "no_workflow_file", "no_field_in_workflow_file"],
                    "economics.role.": ["main", "subagent", "workflow_agent", "resumed", "conversation"],
                    "economics.link.": ["explicit", "same_id", "trailer", "trailer_ambiguous", "commit", "none"],
                    "economics.reasoning.": ["full", "summary", "none", "redacted", "short", "empty", "mixed"],
                    "economics.attribution.": ["split", "full"],
                    "economics.label.": ["session", "run", "workflow", "total", "repo"],
                    "economics.qual.": ["lower_bound", "estimated_output", "hidden_reasoning", "commits_lower_bound"],
                    "economics.basis.": ["estimated", "mixed", "session_api", "transcript_prices", "mixed_sources"],
                    "economics.price_field.": ["input", "output", "cache_write", "cache_write_1h", "cache_read", "web_search", "web_fetch"],
                    "economics.stale.": ["keep", "estimate"],
                    "economics.context.": ["path", "none"],
                    "economics.note_field.": ["tokens", "tokens_estimated"]}
        for prefix, names in families.items():
            used.update(prefix + n for n in names)
        notes = set(re.findall(r'note\("([a-z_]+)"', src)) | {"no_session_source", "sessions_unreadable", "sessions_bad_line",
                                                              "sessions_no_id", "sessions_duplicate"}
        used.update("economics.note." + n for n in notes)
        self.assertEqual(sorted(k for k in used if k not in pl), [])
        self.assertEqual(sorted(k for k in used if k not in en), [])



# ───────────────────────── review findings of slice 4 (2026-10-17) ─────────────────────────
def cc_line(uuid, parent, typ, ts, content, *, session, model=None, mid=None, usage=None, stop="end_turn", sidechain=False):
    """A synthetic Claude Code transcript line (the shape of gen_extract_fixtures.econ_line)."""
    d = {"uuid": uuid, "parentUuid": parent, "type": typ, "timestamp": ts, "sessionId": session, "isSidechain": sidechain,
         "message": {"role": typ, "content": content}}
    if typ == "assistant":
        d["message"].update({"model": model, "id": mid, "usage": usage, "stop_reason": stop})
    return json.dumps(d, ensure_ascii=False)


def usage(i, o, cc=0, cr=0):
    return {"input_tokens": i, "output_tokens": o, "cache_creation_input_tokens": cc, "cache_read_input_tokens": cr}


class ReviewFindingsTest(EconTest):
    def overlay_presets(self, frag: dict) -> None:
        from loom.tools.extract.datafiles import DataStore, set_store
        d = self.tmp / "no-overlay" / "presets"
        d.mkdir(parents=True, exist_ok=True)
        (d / "defaults.json").write_text(json.dumps(frag), encoding="utf-8")
        set_store(DataStore(overlay=self.tmp / "no-overlay"))

    def copy_proj(self, *under: str) -> Path:
        dst = self.tmp.joinpath(*under, "-home-user-econ")
        shutil.copytree(PROJ, dst)
        return dst

    # -- the stream-start output counter (blocking) ---------------------------------------------------------------
    def test_stale_output_is_a_lower_bound_out_of_calibration(self):
        eng, doc = self.econ(PROJ)
        notes = {n["key"]: n for n in doc["unknowns"]}
        self.assertEqual((notes["output_lower_bound"]["n"], notes["output_lower_bound"]["tokens"]),
                         (E["stale_output_turns"], E["stale_output_tokens"]))
        tot = doc["totals"]
        self.assertEqual(tot["output_lower_bound"], E["stale_output_tokens"])
        self.assertEqual(tot["qual"]["output"]["code"], "lower_bound", "the measured output is labelled a lower bound")
        w2 = next(r for r in doc["runs"] if r["id"] == "sess-E1/subagents/workflows/run-1/agent-w2")
        self.assertEqual(w2["qual"]["output"], {"code": "lower_bound", "n": 1, "tokens": E["stale_output_tokens"]})
        main = next(r for r in doc["runs"] if r["id"] == "sess-E1")
        self.assertNotIn("output", main["qual"], "responses with a final line carry no qualifier")
        md = eng.op("token_economics", {})["markdown"]
        self.assertIn(i18n.t("economics.qual.lower_bound", n="1", tokens=str(E["stale_output_tokens"])), md)
        # the calibration never uses it: with the minimum output at 1, the five final thinking-free responses count
        self.overlay_presets({"economics": {"estimate": {"calibration_min_output": 1}}})
        eng2, doc2 = self.econ(PROJ)
        ee = next(iter(doc2["method"]["expected_error"].values()))
        self.assertEqual(ee["turns"], 5, "A2, A3, S1, W1, B1 — not W2 (stream-start), not A1 (thinking)")
        # economics.usage.stale_output: estimate → the larger of the counter and an estimate, labelled estimated
        eng3, doc3 = self.econ(PROJ, stale_output="estimate")
        w2e = next(r for r in doc3["runs"] if r["id"] == "sess-E1/subagents/workflows/run-1/agent-w2")
        self.assertGreater(w2e["output"], E["stale_output_tokens"])
        self.assertEqual(w2e["qual"]["output"]["code"], "estimated_output")
        self.assertIn("output_estimated", {n["key"] for n in doc3["unknowns"]})
        self.assertEqual(w2e["cost"]["basis"], "estimated", "a cost with an estimated output is not called measured")

    # -- cost bases and currencies (blocking + should) -------------------------------------------------------------
    def test_mixed_run_never_one_unlabelled_amount(self):
        self.prices_overlay(gpt=True)
        eng, doc = self.econ(PROJ, CHATGPT)
        tot = doc["totals"]
        self.assertTrue(tot["cost"]["known"])
        self.assertEqual(tot["cost"]["basis"], "mixed")
        self.assertAlmostEqual(tot["cost"]["measured"] + tot["cost"]["estimated"], tot["cost"]["amount"], places=5)
        self.assertEqual((tot["cost_measured"]["basis"], tot["cost_estimated"]["basis"]), ("measured", "estimated"))
        md = eng.op("token_economics", {})["markdown"]
        totals_line = next(ln for ln in md.splitlines() if ln.startswith("| " + i18n.t("economics.label.total") + " |"))
        self.assertIn(i18n.t("economics.basis.mixed", measured="", estimated="").split(":")[0], totals_line)
        none = next(r for r in doc["tools"] if r["tool"] == i18n.t("economics.no_tool"))
        self.assertEqual(none["turns"], none["turns_measured"] + none["turns_estimated"])
        self.assertGreater(none["turns_estimated"], 0)
        for tab in ("totals", "days", "models", "tools"):
            from loom.tools.extract.economics import layout
            cols = next(t for t in layout(store())["tables"] if t["id"] == tab)["columns"]
            self.assertTrue({"cost_measured", "cost_estimated", "cost"} <= set(cols), tab)
        # a currency per entry: amounts in different currencies are never added
        self.prices_overlay(small_currency="PLN")
        eng2, doc2 = self.econ(PROJ)
        self.assertFalse(doc2["totals"]["cost"]["known"])
        self.assertEqual(doc2["totals"]["cost"]["reason"], "mixed_currency")
        self.assertEqual(set(doc2["totals"]["cost"]["partial_by_currency"]), {"USD", "PLN"})
        self.assertIn("mixed_currency", {n["key"] for n in doc2["unknowns"]})

    @unittest.skipUnless(HAS_GIT, "git is not installed")
    def test_cost_per_commit_says_its_basis(self):
        self.prices_overlay()
        repo = make_repo(self.tmp / "repo")
        eng, doc = self.econ(PROJ, repos=[str(repo)])  # no session list: the cost comes from transcript prices
        t1 = next(r for r in doc["sessions"] if r["id"] == "session_TEST1")
        self.assertEqual(t1["cost_per_commit"]["basis"], "transcript_prices")
        md = eng.op("token_economics", {"repos": [str(repo)]})["markdown"]
        self.assertIn(i18n.t("economics.basis.transcript_prices"), md)
        eng2, doc2 = self.econ(PROJ, sessions=[str(SESSIONS)], repos=[str(repo)])
        t1b = next(r for r in doc2["sessions"] if r["id"] == "session_TEST1")
        self.assertEqual(t1b["cost_per_commit"]["basis"], "session_api")

    def test_web_searches_without_a_price_are_not_free(self):
        hit = {"known": True, "prices": {"input": 1.0, "output": 1.0, "cache_write": None, "cache_read": None}, "currency": "USD"}
        c = cost_of({"input": 10, "output": 10, "web_search_requests": 2}, hit)
        self.assertFalse(c["known"])
        self.assertEqual(c["missing"], ["web_search"])
        hit["prices"]["web_search_per_1000"] = 10.0
        self.assertEqual(cost_of({"input": 0, "output": 0, "web_search_requests": 2}, hit)["amount"], 0.02)
        self.prices_overlay(web_search=False)
        eng, doc = self.econ(PROJ)
        run = next(r for r in doc["runs"] if r["id"] == "sess-E1")
        self.assertFalse(run["cost"]["known"])
        self.assertEqual(run["cost"]["missing"], ["web_search"])
        md = eng.op("token_economics", {})["markdown"]
        self.assertIn(i18n.t("economics.why.price_field_missing_named", fields=i18n.t("economics.price_field.web_search")), md)

    def test_shipped_anthropic_prices_from_the_repository_source(self):
        book = PriceBook(store())
        hit = book.lookup("claude-opus-5-5", "2026-10-10")
        self.assertTrue(hit["known"])
        self.assertIn("loom/tools/eval/pricing_2026-09-25.json", hit["source"])
        self.assertEqual(hit["valid_from"], "2026-09-25")
        self.assertEqual(book.lookup("claude-opus-5-5", "2026-09-01")["reason"], "not_yet_valid",
                         "before the source's date: the price is not yet valid, said (never back-dated)")
        c = cost_of({"input": 10, "output": 10, "cache_creation": 5, "cache_read": 5}, hit)
        self.assertEqual((c["known"], c["missing"]), (False, ["cache_write"]), "no cache-write price in the source: said, not derived")
        self.assertTrue(cost_of({"input": 10, "output": 10, "cache_read": 5}, hit)["known"])
        self.assertEqual(book.lookup("claude-haiku-5-5", "2026-10-10")["reason"], "entry_without_price")

    # -- several transcript families for one API session (blocking) --------------------------------------------------
    def test_several_families_for_one_session_add_up(self):
        proj = self.copy_proj("p3")
        cmd = 'git commit -m "x\n\nClaude-Session: https://claude.ai/code/session_TEST1"'
        lines = [cc_line("x1", None, "user", "2026-10-11T10:00:00Z", "Trzecia sesja", session="sess-E3"),
                 cc_line("x2", "x1", "assistant", "2026-10-11T10:00:05Z",
                         [{"type": "tool_use", "id": "toolu_x", "name": "Bash", "input": {"command": cmd}}],
                         session="sess-E3", model="claude-x-small", mid="msg_X1", usage=usage(1, 5, 0, 7000), stop="tool_use"),
                 cc_line("x3", "x2", "user", "2026-10-11T10:00:06Z", [{"type": "tool_result", "tool_use_id": "toolu_x",
                                                                        "content": "[main 9f9f9f9] x"}], session="sess-E3")]
        self.write(proj / "sess-E3.jsonl", "\n".join(lines) + "\n")
        eng, doc = self.econ(proj, sessions=[str(SESSIONS)])
        tot = doc["totals"]
        rows = [r for r in doc["sessions"] if r.get("tr_cache_read") is not None]
        self.assertEqual(sum(r["tr_cache_read"] for r in rows), tot["cache_read"], "the session tables add up to the totals")
        self.assertEqual(sum(r["tr_turns"] for r in rows), tot["turns"])
        t1 = next(r for r in doc["sessions"] if r["id"] == "session_TEST1")
        self.assertEqual(t1["transcript_ids"], ["sess-E1", "sess-E3"])
        self.assertAlmostEqual(t1["coverage"], round((E["family_E1"]["cache_read"] + 7000) / E["api_cache_read_TEST1"], 4))
        self.assertIn("several_transcripts_one_session", {n["key"] for n in doc["unknowns"]})
        self.assertEqual(sum(1 for r in doc["sessions"] if r["id"] == "session_TEST1"), 1)
        # the same through explicit links (--polacz A=S --polacz B=S)
        eng2, doc2 = self.econ(proj, sessions=[str(SESSIONS)], links={"sess-E2": "session_TEST2", "sess-E3": "session_TEST2"})
        t2 = next(r for r in doc2["sessions"] if r["id"] == "session_TEST2")
        self.assertEqual(t2["transcript_ids"], ["sess-E2", "sess-E3"])
        self.assertEqual(sum(r["tr_cache_read"] for r in doc2["sessions"] if r.get("tr_cache_read") is not None), doc2["totals"]["cache_read"])

    # -- trailers only from confirmed commit commands (should) -------------------------------------------------------
    def test_trailers_only_from_confirmed_commit_commands(self):
        proj = self.tmp / "q" / "-home-user-q"
        fake = 'git add -A && git commit -m "t\n\nClaude-Session: https://claude.ai/code/session_FAKE"'
        heredoc = "cat > t.sh <<'EOF'\n" + fake + "\nEOF"
        lines = [cc_line("q1", None, "user", "2026-10-11T10:00:00Z", "Napisz test", session="sess-Q"),
                 cc_line("q2", "q1", "assistant", "2026-10-11T10:00:05Z",
                         [{"type": "tool_use", "id": "toolu_w", "name": "Write", "input": {"file_path": "t.py", "content": fake}},
                          {"type": "tool_use", "id": "toolu_h", "name": "Bash", "input": {"command": heredoc}}],
                         session="sess-Q", model="claude-x-small", mid="msg_Q1", usage=usage(1, 5), stop="tool_use"),
                 cc_line("q3", "q2", "user", "2026-10-11T10:00:06Z",
                         [{"type": "tool_result", "tool_use_id": "toolu_w", "content": "File created"},
                          {"type": "tool_result", "tool_use_id": "toolu_h", "content": ""}], session="sess-Q")]
        self.write(proj / "sess-Q.jsonl", "\n".join(lines) + "\n")
        eng, doc = self.econ(proj)
        row = next(r for r in doc["sessions"] if r["transcript_id"] == "sess-Q")
        self.assertEqual(row["link"], i18n.t("economics.link.none"), "a Write's content and an unconfirmed heredoc link nothing")
        self.assertEqual(row["commit_commands"], 1, "Write is not a commit tool; the Bash heredoc is a command, not a commit made")
        self.assertEqual(doc["outcomes"]["commit_commands_confirmed"], 0)
        self.assertIn("commit_unconfirmed", {n["key"] for n in doc["unknowns"]})
        self.assertNotIn("session_FAKE", json.dumps(doc))

    # -- side files inside session directories (should) ---------------------------------------------------------------
    def test_side_files_in_session_directories_are_not_estimated(self):
        proj = self.copy_proj("side")
        self.write(proj / "sess-E1" / "tool-results" / "x.txt", "wynik narzędzia\n" * 50)
        eng, doc = self.econ(proj)
        self.assertEqual(doc["totals"]["turns_estimated"], 0)
        self.assertEqual({n["key"]: n["n"] for n in doc["unknowns"]}.get("side_files_skipped"), 1)
        eng2, doc2 = self.econ(proj, skip_session_side_files=False)
        self.assertGreater(doc2["totals"]["turns_estimated"], 0, "preset reversed: the side file is estimated as text")

    # -- placement from the path below the source (should) -----------------------------------------------------------
    def test_ancestor_directories_named_workflows_or_subagents_decide_nothing(self):
        base = self.copy_proj("workflows", "backup", "subagents", "projects")
        eng, units = self.units(base)
        by = {u.ext_id: u for u in units if u.kind == "session"}
        self.assertEqual(set(by), {"sess-E1", "sess-E2", "sess-E1/subagents/agent-s1", "sess-E1/subagents/workflows/run-1/agent-w1",
                                   "sess-E1/subagents/workflows/run-1/agent-w2"})
        self.assertEqual((by["sess-E1"].flags["transcript_role"], by["sess-E2"].flags["transcript_role"]), ("main", "main"))
        self.assertEqual(by["sess-E1/subagents/workflows/run-1/agent-w1"].flags["workflow_run"], "run-1")
        eng2, doc = self.econ(base)
        self.assertEqual({r["transcript_id"] for r in doc["sessions"]}, {"sess-E1", "sess-E2"})
        # single files: a main transcript stays main; an agent file is placed by its nearest directories, confirmed by sessionId
        eng3, units3 = self.units(base / "sess-E2.jsonl", base / "sess-E1" / "subagents" / "agent-s1.jsonl")
        roles = {u.ext_id: (u.flags["transcript_role"], u.flags.get("parent_session")) for u in units3 if u.kind == "session"}
        self.assertEqual(roles, {"sess-E2": ("main", None), "sess-E1/subagents/agent-s1": ("subagent", "sess-E1")})

    # -- days in one clock (should) -------------------------------------------------------------------------------------
    def test_commit_days_are_utc_like_turns(self):
        from loom.tools.extract.economics import _day
        self.assertEqual(_day("2026-10-12T01:30:00+02:00", "UTC"), "2026-10-11")
        self.assertEqual(_day("2026-10-11T23:30:00Z", "UTC"), "2026-10-11")
        if not HAS_GIT:
            self.skipTest("git is not installed")
        repo = make_repo(self.tmp / "repo")
        env = {"GIT_AUTHOR_NAME": "T", "GIT_AUTHOR_EMAIL": "t@example.invalid", "GIT_COMMITTER_NAME": "T",
               "GIT_COMMITTER_EMAIL": "t@example.invalid", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
               "HOME": str(repo), "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
               "GIT_AUTHOR_DATE": "2026-10-13T01:30:00+02:00", "GIT_COMMITTER_DATE": "2026-10-13T01:30:00+02:00"}
        (repo / "d.txt").write_text("d\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(repo), "add", "-A"], check=True, capture_output=True, env=env)
        subprocess.run(["git", "-C", str(repo), "-c", "commit.gpgsign=false", "commit", "-q", "-m",
                        "near midnight\n\nClaude-Session: https://claude.ai/code/session_TEST1"], check=True, capture_output=True, env=env)
        eng, doc = self.econ(repos=[str(repo)])
        days = {d["day"]: d["commits"] for d in doc["days"]}
        self.assertEqual(days.get("2026-10-12"), 1, "01:30 +02:00 on the 13th is 23:30 UTC on the 12th")
        self.assertNotIn("2026-10-13", days)

    # -- git: never silent, never a network fetch, no repository-configured program (should + nit) -------------------
    @unittest.skipUnless(HAS_GIT, "git is not installed")
    def test_unreadable_repository_is_unknown_not_zero(self):
        eng, doc = self.econ(PROJ, sessions=[str(SESSIONS)], repos=[str(self.tmp / "nothing-here")])
        t1 = next(r for r in doc["sessions"] if r["id"] == "session_TEST1")
        self.assertIsNone(t1["commits"])
        self.assertEqual(t1["why"]["commits"], "git_error")
        self.assertEqual(t1["cost_per_commit"]["reason"], "git_error")
        self.assertIsNone(doc["totals"]["commits"])
        self.assertTrue(all(d["commits"] is None for d in doc["days"]))
        # one readable next to one that is not: counts kept, labelled a lower bound
        repo = make_repo(self.tmp / "repo")
        eng2, doc2 = self.econ(PROJ, repos=[str(repo), str(self.tmp / "nothing-here")])
        self.assertEqual(doc2["totals"]["commits"], 3)
        self.assertEqual(doc2["totals"]["qual"]["commits"]["code"], "commits_lower_bound")
        self.assertIn("commits_lower_bound", {n["key"] for n in doc2["unknowns"]})

    @unittest.skipUnless(HAS_GIT, "git is not installed")
    def test_partial_and_shallow_clones_fetch_nothing(self):
        from loom.tools.extract.economics import git_commits
        src = make_repo(self.tmp / "src")
        subprocess.run(["git", "-C", str(src), "config", "uploadpack.allowFilter", "true"], check=True, capture_output=True)
        env = {"GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1", "HOME": str(self.tmp), "PATH": os.environ.get("PATH", "/usr/bin:/bin")}
        part = self.tmp / "partial"
        r = subprocess.run(["git", "clone", "-q", "--no-local", "--filter=blob:none", "--no-checkout", f"file://{src}", str(part)],
                           capture_output=True, env=env)
        if r.returncode != 0:
            self.skipTest("this git cannot make a partial clone: " + r.stderr.decode()[:100])

        def objects(repo):
            out = subprocess.run(["git", "-C", str(repo), "count-objects", "-v"], capture_output=True, env=env).stdout.decode()
            return {k: v for k, v in (ln.split(": ") for ln in out.splitlines())}
        before = objects(part)
        res = git_commits(str(part), {"main_ref": ["main", "origin/main"]})
        self.assertIsNone(res["error"])
        self.assertTrue(res["partial_clone"])
        self.assertFalse(res["lines_known"])
        self.assertEqual(len(res["commits"]), 4)
        self.assertEqual(objects(part), before, "no lazy fetch: the clone's objects are what they were")
        eng, doc = self.econ(PROJ, repos=[str(part)])
        t1 = next(r for r in doc["sessions"] if r["id"] == "session_TEST1")
        self.assertEqual((t1["lines_added"], t1["why"]["lines_added"]), (None, "partial_clone"))
        self.assertEqual(doc["outcomes"]["main_ref_used"], ["main"])
        shallow = self.tmp / "shallow"
        subprocess.run(["git", "clone", "-q", "--depth", "1", f"file://{src}", str(shallow)], check=True, capture_output=True, env=env)
        eng2, doc2 = self.econ(PROJ, repos=[str(shallow)])
        self.assertIn("git_shallow", {n["key"] for n in doc2["unknowns"]})
        self.assertEqual(doc2["totals"]["qual"]["commits"]["code"], "commits_lower_bound")

    @unittest.skipUnless(HAS_GIT, "git is not installed")
    def test_git_runs_no_program_the_repository_configures_and_main_ref_list(self):
        from loom.tools.extract.economics import _first_line, git_commits
        repo = make_repo(self.tmp / "repo")
        marker = self.tmp / "textconv-ran"
        subprocess.run(["git", "-C", str(repo), "config", "diff.x.textconv", f"sh -c 'touch {marker}; cat \"$1\"' -"], check=True)
        subprocess.run(["git", "-C", str(repo), "config", "log.showSignature", "true"], check=True)
        (repo / ".gitattributes").write_text("*.txt diff=x\n", encoding="utf-8")
        res = git_commits(str(repo), {"main_ref": "main"})
        self.assertIsNone(res["error"])
        self.assertFalse(marker.exists(), "textconv of the repository's config never runs")
        self.assertEqual(sum(c["added"] for c in res["commits"]), 7)
        # the main ref is a preference list: the first that resolves wins and is named
        subprocess.run(["git", "-C", str(repo), "update-ref", "refs/remotes/origin/main", "main"], check=True)
        subprocess.run(["git", "-C", str(repo), "branch", "-q", "-m", "main", "trunk"], check=True)
        res2 = git_commits(str(repo), {"main_ref": ["main", "origin/main"]})
        self.assertEqual(res2["main_ref"], "origin/main")
        res3 = git_commits(str(repo), {"main_ref": ["main"]})
        self.assertEqual(res3["main_reason"], "no_main_ref")
        md = self.engine(PROJ).op("token_economics", {"repos": [str(repo)], "main_ref": "nope"})["markdown"]
        self.assertIn(i18n.t("economics.why.main_ref_unknown", ref="nope"), md, "the message names the branch the user chose")
        self.assertEqual(_first_line(b"\n  \n"), "", "whitespace-only stderr: no crash")

    # -- reasoning by share (should) -----------------------------------------------------------------------------------
    def test_reasoning_by_share_and_signature_only_blocks(self):
        from loom.tools.extract.inventory import reasoning_class
        proj = self.tmp / "r"
        lines = []
        for i in range(10):
            text = "y" * 300 if i == 0 else ""
            lines.append(json.dumps({"uuid": f"t{i}", "parentUuid": f"t{i - 1}" if i else None, "type": "assistant",
                                     "timestamp": f"2026-10-10T08:00:{i:02d}Z", "sessionId": "s",
                                     "message": {"role": "assistant", "content": [{"type": "thinking", "thinking": text, "signature": "sig"}]}}))
        self.write(proj / "s.jsonl", "\n".join(lines) + "\n")
        inv = self.engine(proj).op("inventory")["inventory"]
        tot = inv["economics"]["total"]
        self.assertEqual(tot["reasoning_blocks"]["empty"], 9, "a signature-only block: the text is withheld, not 'short'")
        self.assertEqual(tot["reasoning_blocks"]["full"], 1)
        self.assertEqual(tot["reasoning"], "empty", "one full block among ten does not make the source 'full'")
        self.assertEqual(reasoning_class({"blocks": 4, "full": 2, "short": 2}, 0.5), "full")
        self.assertEqual(reasoning_class({"blocks": 4, "full": 1, "short": 1, "empty": 1, "summary": 1}, 0.5), "mixed")
        eng, doc = self.econ(proj)
        prov = doc["providers"][0]
        self.assertEqual(prov["reasoning"], i18n.t("economics.reasoning.empty"))
        self.assertIn(i18n.t("economics.reasoning.full") + " 1", prov["reasoning_blocks"])

    # -- calibration per method, hidden reasoning (should + nit) ------------------------------------------------------
    def test_expected_error_only_for_the_method_it_was_measured_with(self):
        eng, doc = self.econ(PROJ, CHATGPT)
        ee = doc["method"]["expected_error"]
        cal = [k for k, v in ee.items() if v["basis"] == "calibration"]
        self.assertEqual(len(cal), 1)
        self.assertEqual(ee[cal[0]]["measured_on"], {"anthropic/claude_code": 1})
        md = eng.op("token_economics", {})["markdown"]
        for how, v in ee.items():
            if v["basis"] != "calibration":
                self.assertEqual(v["reason"], "no_calibration_for_method")
        # the ChatGPT fixture's model is a thinking model: its estimated output is a lower bound, said
        tot = doc["totals"]
        self.assertEqual(tot["qual"]["est_output"]["code"], "hidden_reasoning")
        self.assertIn("estimated_hidden_reasoning", {n["key"] for n in doc["unknowns"]})
        self.assertIn(i18n.t("economics.method.hidden_reasoning", patterns="")[:20], md)

    # -- package content (should) ---------------------------------------------------------------------------------------
    def test_economics_package_has_no_full_text_snapshot_by_default(self):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(cli.main(["ekonomia", str(PROJ), "--pakiet", str(self.tmp / "p1")]), 0, err.getvalue())
            self.assertEqual(cli.main(["ekonomia", str(PROJ), "--pakiet", str(self.tmp / "p2"), "--z-trescia"]), 0, err.getvalue())
        self.assertFalse((self.tmp / "p1" / "units.jsonl").exists())
        self.assertTrue((self.tmp / "p1" / "economics" / "economics.json").exists())
        self.assertIn(i18n.t("report.economics_units_no"), (self.tmp / "p1" / "RAPORT.md").read_text(encoding="utf-8"))
        self.assertIn(i18n.t("cli.economics.package_units_no"), out.getvalue())
        self.assertTrue((self.tmp / "p2" / "units.jsonl").exists())

    # -- per-turn files (should) ------------------------------------------------------------------------------------------
    def test_touched_files_are_paths_not_only_counts(self):
        eng, doc = self.econ(PROJ, per_turn=True)
        e1 = next(r for r in doc["sessions"] if r["transcript_id"] == "sess-E1")
        self.assertEqual(e1["files"], E["files_touched_E1"])
        main = next(r for r in doc["runs"] if r["id"] == "sess-E1")
        self.assertEqual(main["files"], E["files_touched_E1"])
        self.assertEqual(sorted({f for x in doc["turn_files"] for f in x["files"]}), E["files_touched_E1"])
        eng2, doc2 = self.econ(PROJ, aggregate=True, per_turn=True)
        self.assertNotIn("files", doc2["sessions"][0])
        self.assertNotIn("turn_files", doc2)

    # -- session list: repositories, totals, zero counters (should + nit) -----------------------------------------------
    def test_totals_carry_session_api_and_repositories_table(self):
        eng, doc = self.econ(PROJ, sessions=[str(SESSIONS)])
        tot = doc["totals"]
        self.assertEqual(tot["cost_api"]["amount"], 1.75)
        self.assertEqual(tot["api_cache_read"], 3100)
        from loom.tools.extract.economics import layout
        cols = next(t for t in layout(store())["tables"] if t["id"] == "totals")["columns"]
        self.assertTrue({"cost_api", "api_cache_read", "api_cache_creation", "api_input", "api_output"} <= set(cols))
        repos = {r["repo"]: r for r in doc["repos"]}
        self.assertEqual(repos["https://example.invalid/econ/repo-a"]["cost_api"]["amount"], 1.5)
        self.assertEqual(repos["https://example.invalid/other/repo-b"]["sessions"], 1)
        t1 = next(r for r in doc["sessions"] if r["id"] == "session_TEST1")
        self.assertEqual(t1["repos"], "https://example.invalid/econ/repo-a")
        # a counter of 0 on the list is not a missing field
        d = json.loads(SESSIONS.read_text(encoding="utf-8"))
        d["ccr"]["data"][0]["external_metadata"]["usage"]["output_tokens"] = 0
        zero = self.tmp / "zero.json"
        zero.write_text(json.dumps(d), encoding="utf-8")
        eng2, doc2 = self.econ(PROJ, sessions=[str(zero)])
        t1z = next(r for r in doc2["sessions"] if r["id"] == "session_TEST1")
        self.assertEqual((t1z["coverage_output"], t1z["why"]["coverage_output"]), (None, "api_zero"))

    # -- workflow run and agent metadata (nit) ----------------------------------------------------------------------------
    def test_agent_and_workflow_run_metadata(self):
        eng, doc = self.econ(PROJ)
        sub = next(r for r in doc["runs"] if r["id"] == "sess-E1/subagents/agent-s1")
        self.assertEqual((sub["label"], sub["agent_type"]), ("Czytanie raportu", "general-purpose"))
        wr = doc["workflow_runs"][0]
        self.assertEqual((wr["label"], wr["file_agents"], wr["file_total_tokens"]), ("Przepływ testowy", 2, 1300))
        eng2, doc2 = self.econ(PROJ, aggregate=True)
        self.assertNotIn("Czytanie raportu", json.dumps(doc2, ensure_ascii=False))
        self.assertNotIn("Przepływ testowy", json.dumps(doc2, ensure_ascii=False))

    # -- tool split adds up with odd numbers (nit) --------------------------------------------------------------------------
    def test_tool_split_adds_up_with_odd_numbers(self):
        proj = self.tmp / "odd"
        calls = [{"type": "tool_use", "id": f"toolu_{n}", "name": n, "input": {}} for n in ("Grep", "Bash", "Read")]
        lines = [cc_line("o1", None, "user", "2026-10-10T08:00:00Z", "x", session="s-odd"),
                 cc_line("o2", "o1", "assistant", "2026-10-10T08:00:01Z", calls, session="s-odd", model="m", mid="msg_O1",
                         usage=usage(7, 100, 11, 13), stop="tool_use"),
                 cc_line("o3", "o2", "user", "2026-10-10T08:00:02Z", "y", session="s-odd"),
                 cc_line("o4", "o3", "assistant", "2026-10-10T08:00:03Z", calls[:2], session="s-odd", model="m", mid="msg_O2",
                         usage=usage(3, 61, 5, 9), stop="tool_use")]
        self.write(proj / "s-odd.jsonl", "\n".join(lines) + "\n")
        eng, doc = self.econ(proj)
        for k in ("input", "output", "cache_creation", "cache_read", "total"):
            self.assertEqual(sum(t[k] for t in doc["tools"]), doc["totals"][k], k)

    # -- CSV for spreadsheets, recipe with --csv, i18n (nit) ----------------------------------------------------------------
    def test_csv_numbers_are_numbers_and_recipe_saved_with_csv(self):
        out, err = io.StringIO(), io.StringIO()
        rec = self.tmp / "rec.json"
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(cli.main(["ekonomia", str(PROJ), "--csv", "tools", "--zapisz-przepis", str(rec)]), 0, err.getvalue())
        self.assertTrue(rec.exists(), "--zapisz-przepis is honoured with --csv")
        rows = list(csv.DictReader(io.StringIO(out.getvalue())))
        edit = next(r for r in rows if r["tool"] == "Edit")
        self.assertEqual(int(edit["calls"]), 2)
        self.assertEqual((edit["cost_amount"], edit["cost_currency"], edit["cost_why"]), ("", "USD", "entry_without_price"))
        for r in rows:
            for k, v in r.items():
                if k.endswith(("_why", "_basis", "_currency")) or k == "tool" or v == "":
                    continue
                float(v)  # every other cell is a number
        md = self.engine(PROJ).op("token_economics", {})["markdown"]
        self.assertIn(i18n.t("economics.method_name.measured"), md)
        self.assertNotIn("measured (", md, "method codes are said in the run's language")
        self.assertIn(i18n.t("economics.unknown_cost", why=i18n.t("economics.why.entry_without_price")), md)

if __name__ == "__main__":
    unittest.main()
