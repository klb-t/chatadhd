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

    def prices_overlay(self) -> None:
        d = store().overlay / "prices"
        d.mkdir(parents=True, exist_ok=True)
        (d / "synthetic.json").write_text(json.dumps({"id": "synthetic", "currency": "USD", "entries": [
            {"pattern": "claude-x-large", "input": 10.0, "output": 50.0, "cache_write": 12.5, "cache_read": 1.0,
             "valid_from": "2026-01-01", "source": "synthetic test price"},
            {"pattern": "claude-x-small", "input": 1.0, "output": 5.0, "cache_write": 1.25, "cache_read": 0.1,
             "valid_from": "2026-01-01", "source": "synthetic test price"}]}), encoding="utf-8")


class ReaderAndInventoryTest(EconTest):
    def test_per_turn_usage_and_transcript_placement(self):
        eng, units = self.units(PROJ)
        by = {u.ext_id: u for u in units}
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
        days = {d["day"]: d["commits"] for d in doc["days"]}
        self.assertEqual((days["2026-10-10"], days["2026-10-11"], days["2026-10-12"]), (1, 2, 1))
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
        a3 = (4 * 10 + 60 * 50 + 100 * 12.5 + 1200 * 1) / 1e6
        self.assertAlmostEqual(run["cost"]["amount"], round(a1 + a2 + a3, 6), places=6)
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
        self.assertTrue(tot["methods"].startswith("chars/"), tot["methods"])
        prov = doc["providers"][0]
        self.assertFalse(prov["has_usage"])
        self.assertTrue(any(m["model"].startswith("gpt-") for m in doc["models"]), "model_slug per message")
        self.assertEqual(doc["method"]["expected_error"]["basis"], "none", "no measured turns: the error is unknown, said")

    def test_calibration_and_chars(self):
        cal = calibration([(90, 100), (110, 100), (100, 100)])
        self.assertEqual((cal["turns"], cal["median"]), (3, 0.0))
        self.assertIsNone(calibration([]))
        est = Estimator(store(), "chars")
        n, how = est.count("a" * 32, "gpt-4o", "pl")
        self.assertEqual((n, how), (10, "chars/3.2 (pl)"))
        eng, doc = self.econ(PROJ)
        self.assertEqual(doc["method"]["expected_error"]["basis"], "calibration")

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
        tables = [t["id"] for t in layout(store())["tables"]]
        for tid in tables:
            rows = list(csv.reader(io.StringIO((econ / f"{tid}.csv").read_text(encoding="utf-8"))))
            self.assertEqual(rows[0], next(t for t in layout(store())["tables"] if t["id"] == tid)["columns"])
        self.assertTrue((econ / "ekonomia.md").exists())
        doc = json.loads((econ / "economics.json").read_text(encoding="utf-8"))
        self.assertEqual(doc["schema"], "loom.extract.economics/1")
        man_a = json.loads((self.tmp / "a" / "manifest.json").read_text(encoding="utf-8"))
        man_b = json.loads((self.tmp / "b" / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(man_a["reproducible_hash"], man_b["reproducible_hash"], "same inputs, same bytes")
        self.assertIn("economics/economics.json", [o["path"] for o in man_a["outputs"]])
        self.assertEqual(man_a["economics"]["turns_measured"], E["turns"])
        self.assertIn("economics/", (self.tmp / "a" / "RAPORT.md").read_text(encoding="utf-8"))
        # unknown is visible in every format, never an empty cell or 0
        csv_text = (econ / "totals.csv").read_text(encoding="utf-8")
        self.assertIn("nieznane (brak ceny)", csv_text)
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
                                       "git_unavailable", "git_log_failed"],
                    "economics.role.": ["main", "subagent", "workflow_agent", "resumed", "conversation"],
                    "economics.link.": ["explicit", "same_id", "trailer", "trailer_ambiguous", "none"],
                    "economics.reasoning.": ["full", "summary", "none", "redacted", "short"],
                    "economics.attribution.": ["split", "full"],
                    "economics.label.": ["session", "run", "workflow", "total"]}
        for prefix, names in families.items():
            used.update(prefix + n for n in names)
        notes = set(re.findall(r'note\("([a-z_]+)"', src)) | {"no_session_source", "sessions_unreadable", "sessions_bad_line",
                                                              "sessions_no_id", "sessions_duplicate"}
        used.update("economics.note." + n for n in notes)
        self.assertEqual(sorted(k for k in used if k not in pl), [])
        self.assertEqual(sorted(k for k in used if k not in en), [])


if __name__ == "__main__":
    unittest.main()
