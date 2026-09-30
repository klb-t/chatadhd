"""Explicit one-entry policy loses sibling parent reuse, preserving semantics."""
from copy import deepcopy
import unittest

from . import cache as module
from .test_cache import histories, policy, MODEL, AUTO

codec = module.codec


class RetentionForkTests(unittest.TestCase):
    def test_one_entry_sibling_fork_falls_back_without_semantic_changes(self):
        # Small mechanism fixture here; exact100-event first probe is retained
        # separately under chain_first/retention_fork_first.json.
        heads = histories(4); diff = deepcopy(heads[4]['history'][-1]['diff'])
        diff['proposal_id'] = 'alternative-final-proposal'
        fork, _ = codec._candidate(heads[3], diff, validate_result=False); codec.validate_packet(fork)
        application = codec.empty_diff(fork, proposal_id='application', origin=MODEL)
        expected_packet, expected_receipt = codec.apply_diff(fork, application, AUTO)
        for entries in (128, 1):
            cache = module.VerifiedPacketCache(policy(max_entries=entries)); cache.validate_packet(heads[4])
            cached, receipt = cache.validate_with_receipt(fork)
            self.assertEqual(receipt['path'], 'verified_immediate_parent_hit' if entries == 128 else 'strict_fallback')
            self.assertEqual(cached, fork); self.assertNotEqual(cached['packet_id'], heads[4]['packet_id'])
            chosen, applied = codec.apply_diff(cached, application, AUTO)
            self.assertEqual(codec.safe.canonical(applied), codec.safe.canonical(expected_receipt))
            self.assertEqual(chosen, expected_packet); self.assertEqual(codec.invert_application(applied, chosen), fork)


if __name__ == '__main__': unittest.main()
