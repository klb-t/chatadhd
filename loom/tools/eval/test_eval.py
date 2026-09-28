"""Protocol and safety regression tests; all repositories/data are temporary."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import knowledge_eval
from kbeval import common, realrun, synthetic


class RepositoryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Evaluation Test")
        self.git("config", "user.email", "evaluation@example.invalid")
        self.git("config", "core.hooksPath", "/dev/null")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args, env=None):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], env=env, stderr=subprocess.PIPE).decode().strip()

    def put(self, name, content="source text"):
        p = self.repo / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding="utf-8")
        return p

    def commit(self, when, message="source", author=None):
        self.git("add", ".")
        env = {**os.environ, "GIT_AUTHOR_DATE": author or when, "GIT_COMMITTER_DATE": when}
        self.git("commit", "--allow-empty", "-m", message, env=env)
        return self.git("rev-parse", "HEAD")

    def test_historical_blobs_not_hindsight_dates_or_worktree(self):
        self.put("docs/design.md", "Original design")
        before = self.commit("2026-03-01T12:00:00Z")
        self.put("docs/design.md", "Changed after cutoff")
        self.put("docs/2025-retrospective.md", "date: 2025-01-01\nHindsight")
        self.commit("2026-04-01T12:00:00Z")
        self.put("docs/design.md", "Uncommitted change")
        self.put("untracked.md", "Not in history")
        stage = self.root / "stage"
        manifest = realrun.stage_snapshot(self.repo, stage, "2026-03-06")
        self.assertEqual(manifest["revision"], before)
        self.assertEqual((stage / "docs/design.md").read_text(), "Original design")
        self.assertEqual([x["path"] for x in manifest["files"]], ["docs/design.md"])
        self.assertFalse((stage / ".git").exists())

    def test_unmerged_branch_is_not_a_historical_source(self):
        self.put("main.md")
        before = self.commit("2026-02-01T12:00:00Z")
        self.git("checkout", "-b", "unrelated")
        self.put("secret-other-branch.md", "other branch")
        self.commit("2026-03-01T12:00:00Z")
        self.git("checkout", "main")
        self.put("main.md", "present")
        self.commit("2026-04-01T12:00:00Z")
        stage = self.root / "stage"
        manifest = realrun.stage_snapshot(self.repo, stage, "2026-03-06")
        self.assertEqual(manifest["revision"], before)
        self.assertFalse((stage / "secret-other-branch.md").exists())

    def test_future_author_date_rejects_backdated_commit(self):
        self.put("main.md")
        before = self.commit("2026-02-01T12:00:00Z")
        self.put("future.md")
        self.commit("2026-03-02T12:00:00Z", author="2026-09-01T12:00:00Z")
        self.assertEqual(realrun.select_snapshot(self.repo, "2026-03-06"), before)

    def test_future_ancestor_rejects_backdated_descendant(self):
        self.put("main.md")
        before = self.commit("2026-02-01T12:00:00Z")
        self.put("future.md")
        self.commit("2026-09-01T12:00:00Z")
        self.put("backdated.md")
        self.commit("2026-03-01T12:00:00Z")
        self.assertEqual(realrun.select_snapshot(self.repo, "2026-03-06"), before)

    def test_no_old_snapshot_does_not_accept_dates_in_current_docs(self):
        self.put("2020-notes.md", "created_at: 2020-01-01")
        self.commit("2026-09-01T12:00:00Z")
        with self.assertRaisesRegex(realrun.EvaluationUnavailable, "no verifiable snapshot"):
            realrun.stage_snapshot(self.repo, self.root / "stage", "2026-03-06")
        self.assertFalse((self.root / "stage").exists())

    def test_shallow_history_is_unavailable(self):
        self.put("source.md")
        self.commit("2026-02-01T12:00:00Z")
        original = realrun.git
        def shallow(repo, *args):
            return b"true\n" if args == ("rev-parse", "--is-shallow-repository") else original(repo, *args)
        with patch.object(realrun, "git", side_effect=shallow):
            with self.assertRaisesRegex(realrun.EvaluationUnavailable, "shallow"):
                realrun.select_snapshot(self.repo, "2026-03-06")

    def test_excluded_keys_products_and_symlinks_are_never_read(self):
        self.put("source.md", "allowed")
        excluded = ["eval/real-holdout-key/artificial.txt", "docs/selfhost/v2/SELF.md",
                    "nested/generated/predictions.json", "tests/fixture.md",
                    "docs/answer_key.json", "loom/data/profiles/self.json",
                    "odd-output/.loom-archive", "odd-output/report.md", "dossier_x.md"]
        for n, name in enumerate(excluded):
            self.put(name, f"artificial excluded content {n}")
        outside = self.root / "outside.txt"
        outside.write_text("do not follow")
        (self.repo / "link.md").symlink_to(outside)
        self.commit("2026-03-01T12:00:00Z")
        forbidden_oids = {oid for _, _, oid, name in realrun.tree_entries(self.repo, "HEAD") if name in excluded or name == "link.md"}
        original = realrun.git
        with patch.object(realrun, "git", wraps=original) as calls:
            manifest = realrun.stage_snapshot(self.repo, self.root / "stage", "2026-03-06")
        self.assertEqual([x["path"] for x in manifest["files"]], ["source.md"])
        for call in calls.call_args_list:
            args = call.args
            if len(args) >= 3 and args[1] == "cat-file":
                self.assertNotIn(args[-1], forbidden_oids)

    def test_holdout_emits_unavailable_and_never_runs_current_pack(self):
        self.put("source.md")
        self.commit("2026-03-01T12:00:00Z")
        with patch.object(realrun, "run_knowledge") as run:
            result = realrun.holdout("unused", self.repo, "2026-03-06", self.root / "work")
        run.assert_not_called()
        self.assertEqual(result["status"], "unavailable")
        self.assertFalse(result["benchmark_valid"])
        self.assertEqual(result["predictions"], [])
        self.assertIsNone(result["predictive_accuracy"])
        self.assertTrue((Path(result["work_dir"]) / "input_manifest.json").exists())

    def test_selfhost_has_isolated_sources_and_keeps_caller_work(self):
        self.put("source.md", "HEAD content")
        revision = self.commit("2026-03-01T12:00:00Z")
        self.put("source.md", "dirty content")
        work = self.root / "work"
        work.mkdir()
        sentinel = work / "important.txt"
        sentinel.write_text("preserve me")
        out = self.root / "published-products"
        def run(loom, data_dir, config):
            self.assertNotIn("repo", config)
            sources = Path(config["sources"][0])
            self.assertEqual((sources / "source.md").read_text(), "HEAD content")
            self.assertFalse((sources / ".git").exists())
            products = Path(config["out_dir"])
            products.mkdir()
            (products / "SELF.md").write_text("discovered")
            return {"run": "test-run", "status": "done", "stages": []}
        with patch.object(realrun, "run_knowledge", side_effect=run):
            result = realrun.selfhost("fake-loom", self.repo, work, out)
        self.assertEqual(sentinel.read_text(), "preserve me")
        self.assertEqual(result["evaluation"]["inputs"]["revision"], revision)
        self.assertTrue((out / ".loom-archive").exists())
        self.assertEqual(json.loads((out / "run_result.json").read_text())["status"], "done")
        with self.assertRaises(FileExistsError):
            realrun.selfhost("fake-loom", self.repo, work, out)

    def test_holdout_cli_writes_honest_unavailable_result(self):
        self.put("source.md")
        self.commit("2026-09-01T12:00:00Z")
        out = self.root / "nested/result.json"
        code = knowledge_eval.main(["holdout", "--loom", "unused", "--repo", str(self.repo),
                                    "--cut", "2026-03-06", "--work", str(self.root / "work"), "--out", str(out)])
        self.assertEqual(code, 2)
        result = json.loads(out.read_text())
        self.assertFalse(result["pipeline_executed"])
        self.assertIsNone(result["predictive_accuracy"])
        self.assertIn("no verifiable snapshot", result["reason"])


class HarnessTests(unittest.TestCase):
    def test_catalog_precision_uses_labeled_conversations_and_reports_auxiliary_selection(self):
        kb = SimpleNamespace(selection=lambda: {"relevant": True, "trap": False,
                                                "generic": True, "provider-project": True})
        gt = {"units": {"relevant": [{"conv_id": "relevant"}],
                        "noise_traps": [{"conv_id": "trap"}],
                        "noise_generic": [{"conv_id": "generic"}]}}
        score = synthetic.score_catalog(kb, gt)
        self.assertEqual(score["precision"], 0.5)
        self.assertEqual(score["precision_population"], "labeled_conversations")
        self.assertEqual(score["selected"], 3)
        self.assertEqual(score["labeled_selected"], 2)
        self.assertEqual(score["unlabeled_selected"], 1)
        self.assertEqual(score["recall"], 1.0)
        self.assertEqual(score["trap_fpr"], 0.0)

    def test_synthetic_does_not_delete_the_work_argument(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            work = root / "work"
            work.mkdir()
            sentinel = work / "irreplaceable.txt"
            sentinel.write_text("keep")
            gt = root / "fixture"
            gt.mkdir()
            (gt / "ground_truth.json").write_text(json.dumps({"projects": [], "temporal_cut": {"date": "2026-03-06"}}))
            with patch.object(synthetic, "gt_dir", return_value=gt), patch.object(synthetic, "run_knowledge", side_effect=RuntimeError("stop before model")):
                with self.assertRaisesRegex(RuntimeError, "stop before model"):
                    synthetic.evaluate("loom", root, work)
            self.assertEqual(sentinel.read_text(), "keep")
            self.assertEqual(len(list(work.glob("synthetic-*"))), 1)

    def test_retrospective_metric_is_never_named_predictive_accuracy(self):
        kb = SimpleNamespace(table=lambda _: [])
        gt = {"projects": [], "predictions": []}
        result = synthetic.score_predictions(kb, SimpleNamespace(), gt, {})
        self.assertFalse(result["benchmark_valid_for_prediction"])
        self.assertIsNone(result["predictive_accuracy"])
        self.assertNotIn("prediction_accuracy_solution_class", result)
        self.assertIn("solution_class_match_rate", result)
        self.assertTrue(synthetic.check_floors({"retrospective_consistency": result}, {"holdout.prediction_accuracy_solution_class": 0.5}))

    def test_nonzero_cli_exit_is_failure_even_with_done_json(self):
        with tempfile.TemporaryDirectory() as temp:
            proc = SimpleNamespace(stdout='{"status":"done"}', stderr="failure", returncode=7)
            with patch.object(common.subprocess, "run", return_value=proc):
                with self.assertRaisesRegex(RuntimeError, "exited 7"):
                    common.run_knowledge("loom", Path(temp), {})

    def test_cut_is_a_strict_inclusive_utc_date(self):
        self.assertEqual(realrun.cutoff_timestamp("1970-01-01"), 86399)
        for value in ["2026-3-6", "2026-02-30", "../outside", "2026-03-06T00:00:00"]:
            with self.assertRaises(ValueError):
                realrun.cutoff_timestamp(value)

    def test_command_help_imports_all_modes(self):
        p = subprocess.run([sys.executable, str(Path(knowledge_eval.__file__)), "--help"], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("selfhost", p.stdout)
        self.assertIn("holdout", p.stdout)


if __name__ == "__main__":
    unittest.main()
