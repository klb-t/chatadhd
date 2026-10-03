"""Author examples for selection boundaries, not retrieval-quality validation."""
from copy import deepcopy
import unittest

try:
    from .native_assertion_experiment import choose_records
    from .test_core_projection import example, claim
except ImportError:
    from native_assertion_experiment import choose_records
    from test_core_projection import example, claim


class NativeAssertionSelectionTests(unittest.TestCase):
    def test_predicates_balance_seeds_and_context_is_marked_cooccurrence(self):
        source = example()
        source["claims"] = [claim("c" + str(i), "a1", "common", "a2", "aob", "ascope") for i in range(5)]
        source["claims"].append(claim("rare", "a1", "rare", "a2", "aob", "ascope"))
        rows, report = choose_records(source, 2, 2)
        self.assertEqual(report["seed_predicate_counts"], {"common": 1, "rare": 1})
        self.assertTrue(all(row["context"]["selected_cooccurrence_claim_ids"] for row in rows))
        self.assertFalse(report["same_subject_context_is_argument"])
        self.assertEqual(report["eligible_explicit_claim_relation_counts"]["premises:claims"], 0)
        self.assertGreater(report["context_omissions_with_repeats"]["cooccurrence_context_omitted_by_limit"], 0)

    def test_explicit_dependency_context_excludes_contested_and_missing_targets(self):
        source = example()
        source["claims"][0]["assessment"]["premises"]["claims"] = ["contested", "missing", "eligible"]
        for ident in ("contested", "eligible"):
            source["claims"].append(claim(ident, "a2", "requires", "a1", "aob", "ascope"))
        source["claims"][1]["assessment"]["status"] = "contested"
        rows, report = choose_records(source, 1, 2)
        self.assertEqual(rows[0]["seed_claim_id"], "acl")
        self.assertEqual(rows[0]["context"]["selected_dependency_claim_ids"], ["eligible"])
        self.assertEqual(report["context_omissions_with_repeats"]["dependency_targets_ineligible_or_missing"], 2)
        self.assertNotIn("contested", rows[0]["claim_ids"])
        self.assertEqual(source["claims"][1]["assessment"]["status"], "contested")

    def test_same_subject_in_other_unit_is_not_cooccurrence_context(self):
        source = example()
        other = deepcopy(source["observations"][0])
        other.update(id="other-ob", unit="other-unit")
        source["observations"].append(other)
        source["claims"].append(claim("other", "a1", "requires", "a2", "other-ob", "other-scope"))
        rows, report = choose_records(source, 2, 3)
        self.assertTrue(all(len(row["claim_ids"]) == 1 for row in rows))
        self.assertEqual(report["selected_root_occurrences"], 2)


if __name__ == "__main__":
    unittest.main()
