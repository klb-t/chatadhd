import argparse
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("audit_scan", ROOT / "scan.py")
scan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scan)


class ScannerTests(unittest.TestCase):
    def setUp(self):
        self.rules = scan.load_rules(ROOT / "rules.json")

    def candidates(self, text, path="src/example.py"):
        row = {"path": path, "category": scan.classify(path, self.rules), "oid": "1" * 40}
        return scan.scan_text(text, "owner/repo", "2" * 40, row, self.rules)

    def test_classification_denominator_categories(self):
        cases = {
            "core/engine/gui/main.py": "product",
            "app/src/main/java/com/example/core/data/KeyboardRepository.kt": "product",
            "data/loader.py": "product",
            "loom/src/context.cpp": "product",
            "loom/src/context/tests/test_rules.cpp": "tests",
            "app/src/test/java/PolicyTest.kt": "tests",
            "vendor/example/src/main.cpp": "vendor",
            "history/chatadhd_v0.8.3/main.py": "history",
            "build/generated/main.py": "generated",
            "docs/reports/audit.md": "docs",
            "tools/check.py": "tooling",
            "research/scoring.py": "research",
            "app/build.gradle.kts": "build",
            "loom/data/policy/usage_policy.json": "data",
            "assets/icon.png": "assets",
            "no_extension": "other",
        }
        for path, expected in cases.items():
            with self.subTest(path=path):
                self.assertEqual(expected, scan.classify(path, self.rules))

    def test_deterministic_ids_and_line_shift(self):
        first, _ = self.candidates('endpoint = "https://invalid.test/v1"\n')
        repeat, _ = self.candidates('endpoint = "https://invalid.test/v1"\n')
        moved, _ = self.candidates('\n\nendpoint = "https://invalid.test/v1"\n')
        self.assertEqual(first, repeat)
        self.assertEqual([r["candidate_id"] for r in first], [r["candidate_id"] for r in moved])
        self.assertEqual(1, first[0]["span"]["start_line"])
        self.assertEqual(3, moved[0]["span"]["start_line"])

    def test_same_literal_occurrences_have_distinct_ids(self):
        found, _ = self.candidates('a = "repeat"\nb = "repeat"\n')
        ids = [r["candidate_id"] for r in found]
        self.assertEqual(len(ids), len(set(ids)))

    def test_unicode_ast_spans_and_multiline_literal(self):
        text = 'def f(żółć="secret"):\n    return """first\nsecond"""\n'
        found, _ = self.candidates(text)
        default = next(r for r in found if r["rule"] == "default_argument.ast")
        self.assertEqual(12, default["span"]["start_column"])
        string = next(r for r in found if r["rule"] == "string.literal" and r["span"]["start_line"] == 2)
        self.assertEqual(3, string["span"]["end_line"])
        self.assertEqual(10, string["span"]["end_column_exclusive"])

    def test_no_literal_values_or_source_snippets(self):
        token = "UNIQUE_SENSITIVE_SENTINEL_DO_NOT_PUBLISH"
        text = 'api_token = "' + token + '"\nendpoint="https://invalid.test/' + token + '"\n'
        found, _ = self.candidates(text)
        output = scan.encoded(found)
        self.assertNotIn(token, output)
        self.assertNotIn("https://invalid.test", output)
        self.assertNotIn("api_token", output)
        self.assertTrue(any(r["rule"] == "endpoint.literal" for r in found))

    def test_python_ast_decisions_defaults_order_and_slice(self):
        found, status = self.candidates('''def choose(rows, limit=5, *, strategy="first"):
    prepare()
    evaluate()
    try:
        if rows:
            return sorted(rows)[:limit]
        return rows or []
    except ValueError:
        return []
''')
        signals = {r["rule"] for r in found}
        self.assertEqual("parsed", status["python_ast"])
        self.assertTrue({"default_argument.ast", "branch.ast", "exception.ast",
                         "fallback_or_guard.ast", "truncation.ast", "call_order.ast",
                         "sorting.control"}.issubset(signals))
        self.assertTrue(all(r["classification"] == "candidate" for r in found))
        self.assertTrue(all(r["r42_exception"] is None for r in found))

    def test_invalid_python_uses_lexical_fallback(self):
        found, status = self.candidates('def f(:\n    if config:\n        return "https://invalid.test"\n')
        self.assertEqual("syntax_unavailable", status["python_ast"])
        self.assertIn("branch.control", {r["rule"] for r in found})
        self.assertIn("endpoint.literal", {r["rule"] for r in found})
        self.assertNotIn("branch.ast", {r["rule"] for r in found})

    def test_generic_comments_strings_not_control_flow(self):
        found, _ = self.candidates('''// if else https://comment.invalid
val prompt = "if else https://literal.invalid"
val threshold = config?.threshold ?: 0.7
if (threshold > 0.1) { items.sorted().take(5) }
''', path="app/src/main/Policy.kt")
        branches = [r for r in found if r["rule"] == "branch.control"]
        endpoints = [r for r in found if r["rule"] == "endpoint.literal"]
        self.assertEqual([4], [r["span"]["start_line"] for r in branches])
        self.assertEqual([2], [r["span"]["start_line"] for r in endpoints])
        self.assertIn("fallback.control", {r["rule"] for r in found})
        self.assertIn("policy_value.context", {r["rule"] for r in found})

    def test_exception_catalog_never_auto_exempts(self):
        self.assertEqual(6, len(self.rules["r42_exceptions"]))
        found, _ = self.candidates('schema = "loom.graph_packet/1"\n')
        self.assertTrue(found)
        self.assertTrue(all(r["r42_exception"] is None for r in found))
        bad = copy.deepcopy(self.rules)
        bad["exception_assessments"] = [{"candidate_id": found[0]["candidate_id"], "exception_id": "R42.1"}]
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "rules.json"
            path.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):
                scan.load_rules(path)

    def args(self, repo, output, **kwargs):
        values = dict(repo=Path(repo), repo_name="owner/repo", revision="HEAD", manifest=None,
                      visibility="public", base_branch="main", rules=str(ROOT / "rules.json"),
                      categories=None, output=str(output))
        values.update(kwargs)
        return argparse.Namespace(**values)

    def test_git_snapshot_inventory_exclusions_and_immutability(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()
            def command(*args):
                return subprocess.run(["git", "-C", str(repo), *args], check=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
            command("init", "--quiet")
            files = {"src/main.py": 'x = "original"\n', "tests/test_main.py": "assert True\n",
                     "docs/readme.md": "example\n", "data/settings.json": "{}\n",
                     "eval/real-holdout-key.py": 'raise RuntimeError("BLIND_SENTINEL")\n',
                     "blind/corpus.py": 'print("BLIND_SENTINEL")\n',
                     "src/secrets.py": 'print("SECRET_SENTINEL")\n'}
            for rel, content in files.items():
                path = repo / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            (repo / "src/link.py").symlink_to("main.py")
            command("add", ".")
            command("-c", "user.name=Scanner Test", "-c", "user.email=scanner@invalid.test", "commit", "-qm", "fixture")
            sha = command("rev-parse", "HEAD").decode().strip()
            (repo / "src/main.py").write_text('x = "MODIFIED_UNCOMMITTED"\n')
            (repo / "src/untracked.py").write_text('x = "UNTRACKED"\n')
            output = Path(temp) / "output"
            with redirect_stdout(io.StringIO()):
                summary = scan.run(self.args(repo, output, revision=sha))
            inventory = [json.loads(line) for line in (output / "inventory.jsonl").read_text().splitlines()]
            self.assertEqual(8, summary["denominator"]["tracked_entries"])
            self.assertEqual(1, summary["denominator"]["scanned_code_entries"])
            self.assertEqual(3, summary["scan_statuses"]["content_excluded_by_path"])
            self.assertEqual(1, summary["scan_statuses"]["symlink_not_followed"])
            self.assertEqual(0, summary["denominator"]["semantic_reviewed_entries"])
            main = next(r for r in inventory if r["path"] == "src/main.py")
            self.assertEqual(scan.digest(files["src/main.py"]), main["content_sha256"])
            output_text = "".join(p.read_text() for p in output.iterdir())
            for sentinel in ("MODIFIED_UNCOMMITTED", "UNTRACKED", "BLIND_SENTINEL", "SECRET_SENTINEL"):
                self.assertNotIn(sentinel, output_text)

    def test_manifest_scope_and_metadata_content_minimization(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "main.py").write_text("x = 7\n")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"repo": "owner/repo", "sha": "export-assertion", "base_branch": "main",
                                           "files": [{"path": "main.py", "content_sha256": scan.digest("x = 7\n"),
                                                      "source_snippet": "UNEXPECTED_PRIVATE_FIELD"}]}))
            with redirect_stdout(io.StringIO()):
                summary = scan.run(self.args(root, root / "output", manifest=str(manifest)))
            self.assertEqual("manifest_assertion_plus_local_files", summary["source"])
            self.assertNotIn("UNEXPECTED_PRIVATE_FIELD", (root / "output/inventory.jsonl").read_text())

    def test_oversize_explicit_gap(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "a.py").write_text("x = " + "1" * 100 + "\n")
            (root / "b.py").write_text("x = 1\n")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"repo": "owner/repo", "sha": "test", "base_branch": "main",
                                           "files": [{"path": "a.py"}, {"path": "b.py"}]}))
            rules = copy.deepcopy(self.rules)
            rules["max_blob_bytes"] = 20
            rules_path = root / "rules.json"
            rules_path.write_text(json.dumps(rules))
            with redirect_stdout(io.StringIO()):
                summary = scan.run(self.args(root, root / "out", manifest=str(manifest), rules=str(rules_path)))
            self.assertEqual(1, summary["scan_statuses"]["oversized"])
            self.assertEqual(1, summary["scan_statuses"]["scanned"])


if __name__ == "__main__":
    unittest.main()
