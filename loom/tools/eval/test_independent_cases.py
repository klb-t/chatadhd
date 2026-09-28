"""Metric/source-integrity checks for the independent diagnostic protocol."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import independent_cases as suite


class IndependentProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = suite.load_fixture()

    def test_frozen_sources_and_annotations_validate(self):
        self.assertEqual(suite.validate(self.fixture)["cases"], 100)
        changed = copy.deepcopy(self.fixture)
        next(c for c in changed["cases"] if c.get("logic"))["logic"][0]["formula"]["op"] = "modal"
        with self.assertRaisesRegex(AssertionError, "frozen labels"):
            suite.validate(changed)

    def test_source_span_must_verify_even_when_snapshot_is_refrozen(self):
        changed = copy.deepcopy(self.fixture)
        c = next(c for c in changed["cases"] if c.get("logic"))
        c["logic"][0]["assessment"]["basis"]["support"][0]["quote"] = "fabricated quotation"
        changed["frozen_labels_sha256"] = suite.digest(suite.label_payload(changed))
        with self.assertRaises(AssertionError):
            suite.validate(changed)

    def test_group_cannot_cross_split(self):
        changed = copy.deepcopy(self.fixture)
        changed["cases"][1]["split"] = "validation"
        changed["frozen_labels_sha256"] = suite.digest(suite.label_payload(changed))
        with self.assertRaisesRegex(AssertionError, "group crosses"):
            suite.validate(changed)

    def test_export_contains_source_not_gold_and_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            first = suite.materialize(self.fixture, Path(directory), "development")
            content = Path(first["path"]).read_bytes()
            second = suite.materialize(self.fixture, Path(directory), "development")
            self.assertEqual(first["sha256"], second["sha256"])
            self.assertEqual(hashlib.sha256(content).hexdigest(), first["sha256"])
            rows = json.loads(content)
            self.assertEqual(len(rows), 50)
            self.assertFalse(any("logic" in row or "goals" in row or "labels" in row for row in rows))

    def test_missing_positive_prediction_does_not_inflate_recall(self):
        positive = {"id": "p", "goals": {"g": "include"}, "labels": {"rationale": "positive"}}
        negative = {"id": "n", "goals": {"g": "exclude"}, "labels": {"rationale": "negative"}}
        r = suite.confusion([(positive, {}), (negative, {"selected": False})], "g")
        self.assertEqual(r["recall"], 0.0)
        self.assertEqual(r["decision_coverage"], 0.5)
        self.assertEqual(r["prediction_missing_or_abstain"], 1)

    def test_ambiguous_membership_is_not_negative_philosophy(self):
        case = next(c for c in self.fixture["cases"] if c["category"] == "philosophy")
        self.assertEqual(suite.confusion([(case, {"selected": True})], "self_project")["gold_abstain"], 1)
        self.assertEqual(suite.confusion([(case, {"selected": True})], "owner_philosophy")["tp"], 1)

    def test_ordering_reports_ties_instead_of_threshold_tuning(self):
        pairs = [{"id": p["id"], "scores": {"flat": 0.5}} for p in self.fixture["pairs"]]
        report = suite.evaluate(self.fixture, {"goal": "conceptual_structure", "pairs": pairs})
        self.assertEqual(len(report["structural_pair_ordering"]), 22)
        self.assertTrue(all(r["tie"] and not r["correct"] for r in report["structural_pair_ordering"]))

    def test_unavailable_alignment_is_not_zero_similarity_or_a_tie(self):
        pairs = [{"id": p["id"], "scores": {"missing": None}} for p in self.fixture["pairs"]]
        report = suite.evaluate(self.fixture, {"goal": "conceptual_structure", "pairs": pairs})
        self.assertEqual(len(report["structural_pair_ordering"]), 22)
        self.assertTrue(all(r["correct"] is None and not r["tie"] and not r["available"]
                            for r in report["structural_pair_ordering"]))

    def test_formula_accuracy_keeps_negation_direction_and_quantifier(self):
        p = {"op": "atom", "predicate": "charged", "args": ["rover"]}
        q = {"op": "atom", "predicate": "mobile", "args": ["rover"]}
        forward = {"op": "implies", "left": p, "right": q}
        reverse = {"op": "implies", "left": q, "right": p}
        self.assertNotEqual(suite.formula_signature(forward), suite.formula_signature(reverse))
        self.assertNotEqual(suite.formula_signature(p), suite.formula_signature({"op": "not", "arg": p}))
        self.assertNotEqual(suite.formula_signature({"op": "forall", "var": "rover", "body": p}),
                            suite.formula_signature({"op": "exists", "var": "rover", "body": p}))
        named = {"op": "atom", "predicate": "property:CHARGED", "args": ["ROVER"]}
        self.assertEqual(suite.formula_signature(named), suite.formula_signature(p))

    def test_fresh_scope_probe_metadata_is_frozen(self):
        path = suite.DEFAULT_FIXTURE.parent.parent / "independent_scope_v1/cases.json"
        fixture = suite.load_fixture(path)
        self.assertEqual(len(fixture["cases"]), 21)
        self.assertEqual(len(fixture["scope_probes"]), 5)
        fixture["scope_probes"][0]["records"][0]["segment_id"] = "different"
        with self.assertRaisesRegex(AssertionError, "frozen labels"):
            suite.validate(fixture)


if __name__ == "__main__":
    unittest.main()
