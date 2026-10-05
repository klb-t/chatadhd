"""Isolation, denominator and provenance regressions; synthetic data, no model."""
from copy import deepcopy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

try:
    from . import extraction_readiness_v1 as tool
    from .test_graph_free_extraction import case, output
except ImportError:
    import extraction_readiness_v1 as tool
    from test_graph_free_extraction import case, output


class ExtractionReadiness(unittest.TestCase):
    def test_three_arms_preserve_source_without_inventory_or_gold(self):
        source = case()
        source["gold"] = "DO_NOT_LEAK_REFERENCE_LABEL"
        source["family"] = "DO_NOT_LEAK_FAMILY"
        before = deepcopy(source)
        rows = tool.prepare_rows([source])
        self.assertEqual(len(rows), 3)
        self.assertEqual(len({r["id"] for r in rows}), 3)
        for row in rows:
            payload = tool.safe.parse_json(row["body"]["messages"][1]["content"])
            self.assertEqual(payload, tool.free.source_payload(source))
            self.assertEqual(set(payload), {"id", "source_id", "turns"})
            self.assertNotIn("DO_NOT_LEAK", json.dumps(row["body"]))
            self.assertEqual(row["source_payload_sha256"], tool.safe.digest(payload))
        self.assertEqual({k: source[k] for k in before}, before)
        systems = [r["body"]["messages"][0]["content"] for r in rows]
        self.assertEqual(systems[0], tool.free.SYSTEM)
        self.assertTrue(systems[1].startswith(systems[0]))
        self.assertTrue(systems[2].startswith(systems[1]))

    def test_owner_options_are_not_capped_and_price_is_marked_unavailable(self):
        options = {"max_tokens": 1000000, "model": "caller/own-model",
                   "reasoning": {"enabled": True}}
        rows = tool.prepare_rows([case()], options)
        for row in rows:
            for key, value in options.items():
                self.assertEqual(row["body"][key], value)
            self.assertIsNone(row["historical_reservation_usd"])
        with self.assertRaises(ValueError):
            tool.prepare_rows([case()], {"messages": []})
        with self.assertRaises(ValueError):
            tool.prepare_rows([case()], [])

    def test_preparation_never_loads_labels_or_sealed_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(tool.panel, "load_dev_inputs", return_value=[case()]), \
                 patch.object(tool.panel, "load_dev_gold", side_effect=AssertionError("label read")), \
                 patch.object(tool, "git_blob", side_effect=AssertionError("archive read")):
                report = tool.prepare(Path(temporary) / "prepared")
            self.assertEqual(report["prepared_requests"], 3)
            self.assertEqual(report["new_provider_calls"], 0)
            self.assertFalse(report["gold_read"])
            self.assertFalse(report["validation_read"])
            with self.assertRaises(FileExistsError):
                tool.prepare(Path(temporary) / "prepared")

    def test_exact_binding_does_not_certify_relation_semantics(self):
        source = case()
        graph = tool.free.compile_free(output(), tool.free.source_payload(source))
        report = tool.provenance_axis([source], [graph])
        self.assertEqual(report["source_hash_and_recompile_checked_cases"], 1)
        self.assertFalse(report["source_clause_semantics_proven"])
        self.assertFalse(report["actor_authority_or_world_truth_proven"])

    def test_source_or_compiled_span_drift_is_rejected(self):
        source = case()
        graph = tool.free.compile_free(output(), tool.free.source_payload(source))
        changed_span = deepcopy(graph)
        changed_span["source_assertions"][0]["evidence"][0]["byte_end"] += 1
        with self.assertRaises(ValueError):
            tool.provenance_axis([source], [changed_span])
        changed_source = deepcopy(source)
        changed_source["turns"][0]["text"] += " added source text"
        with self.assertRaises(ValueError):
            tool.provenance_axis([changed_source], [graph])

    def test_unavailable_planned_case_remains_in_provenance_denominator(self):
        source = case()
        unavailable = {"case_id": source["id"], "state": "unavailable"}
        report = tool.provenance_axis([source], [unavailable])
        self.assertEqual(report["planned_cases"], 1)
        self.assertEqual(report["missing_or_unavailable_cases_retained"], 1)
        self.assertEqual(report["source_hash_and_recompile_checked_cases"], 0)
        with self.assertRaises(ValueError):
            tool.provenance_axis([source], [])
        with self.assertRaises(ValueError):
            tool.provenance_axis([source], [unavailable, unavailable])

    def test_archive_digest_and_member_paths_fail_closed(self):
        def archive(name):
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as zipped:
                zipped.writestr(name, b"source")
            return stream.getvalue()
        valid = archive(tool.PREFIX + "fixture.json")
        self.assertEqual(tool.archive_payloads(valid, tool.sha(valid)),
                         {tool.PREFIX + "fixture.json": b"source"})
        with self.assertRaises(ValueError):
            tool.archive_payloads(valid, "0" * 64)
        for name in (tool.PREFIX + "../../secret", "/tmp/secret", "eval/real-holdout-key"):
            bad = archive(name)
            with self.assertRaises(ValueError):
                tool.archive_payloads(bad, tool.sha(bad))

    def test_git_reader_accepts_only_named_public_artifacts_and_immutable_refs(self):
        with patch.object(tool.subprocess, "check_output", side_effect=AssertionError("git invoked")):
            for path, ref in (("eval/real-holdout-key", tool.SOURCE_COMMIT),
                              (tool.ARCHIVE_PATH, "origin/main"),
                              ("docs/research/sealed/fixture.json", tool.SOURCE_COMMIT)):
                with self.assertRaises(ValueError):
                    tool.git_blob(path, ref)

    def test_fixture_drift_is_rejected_even_if_aggregate_metric_could_tie(self):
        prefix = "loom/tests/fixtures/research/graph_methods_panel_v1/"
        inputs = {"cases": [case()]}
        gold = {"cases": [{"id": case()["id"], "source_assertions": ["edge1", "edge2"]}]}
        payloads = {prefix + "inputs_dev.json": tool.safe.canonical(inputs),
                    prefix + "gold_dev.json": tool.safe.canonical(gold),
                    prefix + "manifest.json": b"{}"}
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, data in payloads.items():
                (directory / Path(name).name).write_bytes(data)
            hashes = tool.validate_fixture_snapshot(payloads, directory)
            self.assertEqual(len(hashes), 3)
            # Alter a reference alias, then restore inputs and swap gold records.
            changed = deepcopy(inputs)
            changed["cases"][0]["node_inventory"][0]["aliases"] = ["different proposition"]
            (directory / "inputs_dev.json").write_bytes(tool.safe.canonical(changed))
            with self.assertRaisesRegex(ValueError, "inputs_dev.json"):
                tool.validate_fixture_snapshot(payloads, directory)
            (directory / "inputs_dev.json").write_bytes(payloads[prefix + "inputs_dev.json"])
            changed_gold = deepcopy(gold)
            changed_gold["cases"][0]["source_assertions"].reverse()
            (directory / "gold_dev.json").write_bytes(tool.safe.canonical(changed_gold))
            with self.assertRaisesRegex(ValueError, "gold_dev.json"):
                tool.validate_fixture_snapshot(payloads, directory)


if __name__ == "__main__":
    unittest.main()
