"""Author tests for the explicitly expanded source-status sampling policy."""
from copy import deepcopy
import unittest

try:
    from .native_counter_experiment import choose_relation_records
    from .test_core_projection import example, claim
except ImportError:
    from native_counter_experiment import choose_relation_records
    from test_core_projection import example, claim


class NativeCounterSelectionTests(unittest.TestCase):
    def test_contested_relation_seed_retains_status_and_target_boundary(self):
        source = example()
        assessment = source["claims"][0]["assessment"]
        assessment["status"] = "contested"
        assessment["counter"]["claims"] = ["target", "unknown", "excluded"]
        source["claims"].append(claim("target", "a1", "requires", "a2", "aob", "ascope"))
        source["claims"].append(claim("excluded", "a1", "requires", "a2", "aob", "ascope"))
        source["claims"][2]["assessment"]["evidence_class"] = "extrapolated"
        original = deepcopy(source)
        rows, report = choose_relation_records(source, context_limit=3)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["claim_ids"], ["acl", "target"])
        self.assertEqual(rows[0]["source_status_by_claim"], {"acl": "contested", "target": "active"})
        self.assertEqual(report["context_omissions_with_repeats"]["direct_reference_bodies_missing_or_ineligible"], 2)
        self.assertIsNone(report["recorded_counter_is_logical_contradiction"])
        self.assertFalse(report["assessment_promotion"])
        self.assertEqual(source, original)
        active_rows, _ = choose_relation_records(source, status_policy="active_only")
        self.assertEqual(active_rows, [])

    def test_bound_omits_bodies_without_deleting_reference_metadata(self):
        source = example()
        source["claims"][0]["assessment"]["counter"]["claims"] = ["b", "c"]
        for ident in ("b", "c"):
            source["claims"].append(claim(ident, "a1", "requires", "a2", "aob", "ascope"))
        rows, report = choose_relation_records(source, context_limit=1)
        self.assertEqual(rows[0]["claim_ids"], ["acl", "b"])
        self.assertEqual(len(rows[0]["context"]["explicit_dependency_references"]), 2)
        self.assertEqual(report["context_omissions_with_repeats"]["eligible_direct_reference_bodies_omitted_by_context_limit"], 1)
        self.assertEqual(rows[0]["context"]["selected_cooccurrence_claim_ids"], [])


if __name__ == "__main__":
    unittest.main()
