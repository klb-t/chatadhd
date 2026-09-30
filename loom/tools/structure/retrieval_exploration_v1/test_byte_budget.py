import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import byte_budget


class AllocationTests(unittest.TestCase):
    def test_prefix_preserves_ranking_and_stops(self):
        self.assertEqual(byte_budget.allocate(['a', 'b', 'c'], {'a': 3, 'b': 9, 'c': 2}, 5, 'rank_prefix'), (['a'], 3))

    def test_skip_is_explicit_budget_heuristic(self):
        self.assertEqual(byte_budget.allocate(['a', 'b', 'c'], {'a': 3, 'b': 9, 'c': 2}, 5, 'rank_skip'), (['a', 'c'], 5))

    def test_oversized_first_turn_retains_empty_selection(self):
        self.assertEqual(byte_budget.allocate(['a'], {'a': 5}, 4, 'rank_prefix'), ([], 0))

    def test_full_source_bytes_no_partial_turn(self):
        cost = len('Żółć'.encode())
        self.assertEqual(byte_budget.allocate(['a'], {'a': cost}, cost - 1, 'rank_skip'), ([], 0))

    def test_exact_budget_is_inclusive(self):
        self.assertEqual(byte_budget.allocate(['a'], {'a': 5}, 5, 'rank_prefix'), (['a'], 5))

    def test_identity_drift_rejected(self):
        with self.assertRaises(ValueError):
            byte_budget.allocate(['b'], {'a': 5}, 5, 'rank_skip')

    def test_negative_and_noninteger_cost_rejected(self):
        for cost in (-1, 1.5, True):
            with self.assertRaises(ValueError):
                byte_budget.allocate(['a'], {'a': cost}, 5, 'rank_skip')

    def test_duplicate_rank_rejected(self):
        with self.assertRaises(ValueError):
            byte_budget.allocate(['a', 'a'], {'a': 5}, 10, 'rank_skip')


class CeilingTests(unittest.TestCase):
    def test_oracle_maximizes_annotated_spans_only(self):
        out = byte_budget.ceiling({'a': 9, 'b': 4, 'c': 4}, 8, [{'turn_id': 'a'}, {'turn_id': 'b'}, {'turn_id': 'c'}])
        self.assertEqual(out['covered_spans'], 2)
        self.assertEqual(out['selected_turn_ids'], ['b', 'c'])

    def test_oracle_unknown_selects_no_gold_evidence(self):
        out = byte_budget.ceiling({'a': 5}, 5, [])
        self.assertEqual(out['covered_spans'], 0)
        self.assertEqual(out['selected_turn_ids'], [])

    def test_zero_byte_irrelevant_source_not_added_to_ceiling(self):
        out = byte_budget.ceiling({'a': 0}, 0, [])
        self.assertEqual(out['selected_turn_ids'], [])

    def test_ceiling_cannot_expand_to_undeclared_large_pool(self):
        with self.assertRaises(ValueError):
            byte_budget.ceiling(dict.fromkeys('abcd', 1), 2, [])


if __name__ == '__main__':
    unittest.main()
