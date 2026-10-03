"""Cache equivalence/adversarial tests; no model/network/native/holdout work."""
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import hashlib
import unittest
from unittest.mock import patch

from . import cache as module

try:
    from ..agentic_graph_v1.test_packet import fixture, MODEL, AUTO
except ImportError:
    from agentic_graph_v1.test_packet import fixture, MODEL, AUTO

codec = module.codec


def policy(mode='verified_history', **kwargs):
    return {'schema': 'loom.graph_packet_cache_policy/1', 'mode': mode,
            'max_entries': 128, 'max_retained_bytes': 16 * 1024 * 1024,
            'max_entry_bytes': 2 * 1024 * 1024, 'on_capacity': 'evict_lru', **kwargs}


def histories(count, prefix='original'):
    """Scripted fixture constructor; measured/tested heads get strict validation."""
    result = [fixture()]; template = codec.empty_diff(result[0], proposal_id='fixture', origin=MODEL)
    for i in range(count):
        diff = deepcopy(template); diff['base_packet_sha256'] = result[-1]['packet_id']
        diff['proposal_id'] = prefix + '-' + str(i)
        after, _ = codec._candidate(result[-1], diff, validate_result=False)
        result.append(after)
    return result


def rehash(packet):
    for name in codec.COLLECTIONS:
        for item in packet[name]:
            packet['provenance'][name][codec.record_id(name, item)]['record_sha256'] = codec.digest(item)
    for event in packet['history']:
        event['application_id'] = codec.digest({k: v for k, v in event.items() if k != 'application_id'})
    packet['packet_id'] = codec.digest(codec._packet_payload(packet))
    return packet


class CacheTests(unittest.TestCase):
    def test_cold_warm_result_and_application_receipt_bytes_match_strict(self):
        packet = histories(4)[-1]; codec.validate_packet(packet)
        diff = codec.empty_diff(packet, proposal_id='application', origin=MODEL)
        baseline_after, baseline_receipt = codec.apply_diff(packet, diff, AUTO)
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode))
            result, receipt = cache.validate_with_receipt(packet)
            self.assertEqual(receipt['path'], 'strict_fallback'); self.assertEqual(result, packet)
            result, receipt = cache.validate_with_receipt(packet)
            self.assertEqual(receipt['path'], 'whole_packet_hit'); self.assertEqual(result, packet)
            after, applied = codec.apply_diff(result, diff, AUTO)
            self.assertEqual(codec.safe.canonical(after), codec.safe.canonical(baseline_after))
            self.assertEqual(codec.safe.canonical(applied), codec.safe.canonical(baseline_receipt))
            self.assertEqual(codec.invert_application(applied, after), packet)

    def test_verified_immediate_parent_reuse_is_separate_from_whole_memo_control(self):
        heads = histories(5)
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode)); cache.validate_packet(heads[4])
            result, receipt = cache.validate_with_receipt(heads[5])
            self.assertEqual(result, heads[5])
            self.assertEqual(receipt['path'], 'verified_immediate_parent_hit' if mode == 'verified_history' else 'strict_fallback')
        codec.validate_packet(heads[5])

    def test_rebuilt_changed_old_prefix_misses_without_trusting_current_hash(self):
        original, fork = histories(8), histories(8, prefix='changed')
        codec.validate_packet(fork[8])
        cache = module.VerifiedPacketCache(policy()); cache.validate_packet(original[8])
        result, receipt = cache.validate_with_receipt(fork[8])
        self.assertEqual(result, fork[8]); self.assertEqual(receipt['path'], 'strict_fallback')
        self.assertEqual(fork[8]['claims'], original[8]['claims'])

    def test_valid_last_event_fork_reuses_identical_parent_without_forced_consensus(self):
        heads = histories(6); parent = heads[5]
        diff = deepcopy(heads[6]['history'][-1]['diff']); diff['proposal_id'] = 'competing-last-event'
        fork, _ = codec._candidate(parent, diff, validate_result=False); codec.validate_packet(fork)
        cache = module.VerifiedPacketCache(policy()); cache.validate_packet(heads[6])
        result, receipt = cache.validate_with_receipt(fork)
        self.assertEqual(receipt['path'], 'verified_immediate_parent_hit'); self.assertEqual(result, fork)
        self.assertNotEqual(result['packet_id'], heads[6]['packet_id'])

    def test_caller_mutation_and_return_mutation_do_not_change_private_bytes(self):
        packet = fixture(); original = deepcopy(packet)
        cache = module.VerifiedPacketCache(policy())
        result = cache.validate_packet(packet)
        self.assertIsNot(result, packet)
        result['claims'][0]['assessment']['confidence'] = 1
        packet['sources'][0]['observation']['text'] = 'mutated caller'
        self.assertEqual(cache.validate_packet(original), original)
        with self.assertRaises(ValueError): cache.validate_packet(packet)
        self.assertEqual(cache.validate_packet(original), original)

    def test_input_mutated_after_capture_cannot_enter_cache_as_valid_other_bytes(self):
        packet = fixture(); original = deepcopy(packet); cache = module.VerifiedPacketCache(policy('whole_packet'))
        store = cache._store_authorized
        def concurrent_mutation(raw, limits_hash):
            packet['sources'][0]['observation']['text'] = 'changed after immutable capture'
            return store(raw, limits_hash)
        with patch.object(cache, '_store_authorized', side_effect=concurrent_mutation):
            result = cache.validate_packet(packet)
        self.assertEqual(result, original)
        self.assertEqual(cache.validate_packet(original), original)
        with self.assertRaises(ValueError): cache.validate_packet(packet)

    def test_cycle_rejection_shared_acyclic_json_and_resources_before_hit(self):
        cache = module.VerifiedPacketCache(policy()); cache.validate_packet(fixture())
        circular = {}; circular['self'] = circular
        with self.assertRaisesRegex(ValueError, 'circular_json'): cache.validate_packet(circular)
        circular = []; circular.append(circular)
        with self.assertRaisesRegex(ValueError, 'circular_json'): cache.validate_packet(circular)
        shared = {'v': [1, 2]}; packet = fixture(); packet['task'] = {'a': shared, 'b': shared}; rehash(packet)
        self.assertEqual(cache.validate_packet(packet), packet)
        with self.assertRaises(ValueError): cache.validate_packet(fixture(), resource_limits={'max_nodes': 2})
        cache.validate_packet(fixture(), resource_limits={'max_nodes': None})
        self.assertEqual(cache.stats()['whole_hits'], 0)

    def test_foreign_entry_or_public_receipt_never_authorizes_a_hit(self):
        first, second = module.VerifiedPacketCache(policy()), module.VerifiedPacketCache(policy())
        packet = fixture(); _, receipt = first.validate_with_receipt(packet)
        key, entry = next(iter(first._entries.items()))
        second._entries[key] = entry
        with self.assertRaises(module.CacheIntegrityError): second.validate_packet(packet)
        with self.assertRaises(ValueError): first.validate_packet(receipt)

    def test_exact_bytes_are_checked_even_with_matching_public_cache_key(self):
        cache = module.VerifiedPacketCache(policy()); packet = fixture(); cache.validate_packet(packet)
        key, entry = next(iter(cache._entries.items()))
        entry.raw = b'{}'
        with self.assertRaises(module.CacheIntegrityError): cache.validate_packet(packet)

    def test_strict_runtime_binding_changes_fail_closed(self):
        cache = module.VerifiedPacketCache(policy()); cache.validate_packet(fixture())
        with patch.object(codec, '_validate_packet_content', return_value=None):
            with self.assertRaises(module.CacheIntegrityError): cache.validate_packet(fixture())
        self.assertEqual(cache.validate_packet(fixture()), fixture())
        with patch.object(module, 'STRICT_PACKET_SHA256', '0' * 64):
            with self.assertRaises(module.CacheIntegrityError): module.VerifiedPacketCache(policy())

    def test_transitive_validator_and_mutable_vocabulary_changes_fail_closed(self):
        packet = histories(2)[-1]
        cache = module.VerifiedPacketCache(policy()); cache.validate_packet(packet)
        for target, name in ((codec, '_validate_history'), (codec.safe, '_keys')):
            with patch.object(target, name, side_effect=ValueError('changed strict helper')):
                with self.assertRaises(module.CacheIntegrityError): cache.validate_packet(packet)
        codec.EVIDENCE.add('not_native')
        try:
            with self.assertRaises(module.CacheIntegrityError): cache.validate_packet(packet)
        finally:
            codec.EVIDENCE.remove('not_native')
        self.assertEqual(cache.validate_packet(packet), packet)
        validators = dict(codec.VALIDATORS)
        with patch.dict(codec.VALIDATORS, {'claims': lambda value: None}):
            with self.assertRaises(module.CacheIntegrityError): cache.validate_packet(packet)
        self.assertEqual(codec.VALIDATORS, validators)

    def test_lru_bypass_entry_size_and_zero_storage_preserve_validation(self):
        heads = histories(3)
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode, max_entries=1))
            cache.validate_packet(heads[1]); cache.validate_packet(heads[2])
            self.assertEqual(cache.stats()['entries'], 1)
            self.assertGreater(cache.stats()['evictions'], 0)
            cache.validate_packet(heads[1])
            self.assertEqual(cache.stats()['entries'], 1)
        cache = module.VerifiedPacketCache(policy('whole_packet', max_entries=1, on_capacity='bypass'))
        cache.validate_packet(heads[1]); cache.validate_packet(heads[2])
        self.assertEqual(cache.stats()['capacity_bypasses'], 1)
        self.assertEqual(cache.validate_with_receipt(heads[1])[1]['path'], 'whole_packet_hit')
        for options in ({'max_entries': 0}, {'max_retained_bytes': 0}, {'max_entry_bytes': 1}, {'max_retained_bytes': 1}):
            cache = module.VerifiedPacketCache(policy('whole_packet', **options))
            self.assertEqual(cache.validate_packet(heads[1]), heads[1]); self.assertEqual(cache.stats()['entries'], 0)

    def test_policy_and_resource_identity_reconfigure_invalidates_private_receipts(self):
        p = policy(); cache = module.VerifiedPacketCache(p); p['mode'] = 'changed outside cache'
        self.assertEqual(cache.policy['mode'], 'verified_history')
        cache.validate_packet(fixture()); cache.reconfigure(policy('whole_packet'))
        self.assertEqual(cache.stats()['entries'], 0)
        self.assertEqual(cache.validate_with_receipt(fixture())[1]['path'], 'strict_fallback')
        self.assertEqual(cache.validate_with_receipt(fixture(), resource_limits={})[1]['path'], 'strict_fallback')
        cache.clear(); self.assertEqual(cache.stats()['entries'], 0)

    def test_parallel_same_input_keeps_immutable_valid_snapshots(self):
        cache = module.VerifiedPacketCache(policy()); packet = histories(5)[-1]
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda _: cache.validate_packet(packet), range(12)))
        self.assertTrue(all(result == packet for result in results))
        self.assertLessEqual(cache.stats()['entries'], 2)
        for result in results: self.assertIsNot(result, packet)

    def test_invalid_native_source_assessment_dependency_rejections_match_baseline(self):
        invalid = []
        for field in ('quote', 'source_hash', 'locator', 'confidence', 'assessment', 'reference', 'truth_field'):
            packet = fixture()
            if field == 'quote': packet['claims'][0]['assessment']['basis']['support'][0]['quote'] = 'invented'
            elif field == 'source_hash': packet['sources'][0]['text_sha256'] = '0' * 64
            elif field == 'locator': packet['claims'][0]['assessment']['basis']['support'][0]['locator']['member'] = 'different'
            elif field == 'confidence': packet['claims'][0]['assessment']['confidence'] = 2
            elif field == 'assessment': packet['claims'][0]['assessment']['basis']['support'] = []
            elif field == 'reference': packet['claims'][0]['assessment']['premises']['claims'] = ['absent_ref']
            else: packet['claims'][0]['truth'] = True
            invalid.append((field, rehash(packet)))
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode)); cache.validate_packet(fixture())
            for field, packet in invalid:
                with self.assertRaises(ValueError, msg=field): codec.validate_packet(packet)
                with self.assertRaises(ValueError, msg=field): cache.validate_packet(packet)

    def test_utf8_subspan_xor_value_and_stale_diff_rejections_match(self):
        valid = fixture(); support = valid['claims'][0]['assessment']['basis']['support'][0]
        raw = valid['sources'][0]['observation']['text'].encode(); quote = 'lampa zgaśnie'
        support['quote'] = quote; support['locator']['byte_start'] += raw.index(quote.encode())
        support['locator']['byte_len'] = len(quote.encode()); rehash(valid)
        invalid = []
        for field in ('utf8_start', 'utf8_length', 'missing_source', 'both_value', 'neither_value', 'native_origin'):
            packet = deepcopy(valid); support = packet['claims'][0]['assessment']['basis']['support'][0]
            if field == 'utf8_start': support['locator']['byte_start'] -= 1
            elif field == 'utf8_length': support['locator']['byte_len'] += 1
            elif field == 'missing_source': support['observation'] = 'missing_observation'
            elif field == 'both_value': packet['claims'][0]['value'] = 'both object and value'
            elif field == 'neither_value': packet['claims'][0]['object'] = ''
            else: packet['claims'][0]['assessment']['origin'] = 'model'
            invalid.append((field, rehash(packet)))
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode)); self.assertEqual(cache.validate_packet(valid), valid)
            for field, packet in invalid:
                with self.assertRaises(ValueError, msg=field): codec.validate_packet(packet)
                with self.assertRaises(ValueError, msg=field): cache.validate_packet(packet)
            diff = codec.empty_diff(valid, proposal_id='stale', origin=MODEL)
            diff['claims']['remove'] = [{'id': 'cl_test', 'before_sha256': '0' * 64, 'reason': 'stale'}]
            with self.assertRaises(ValueError): codec.apply_diff(valid, diff, AUTO)
            with self.assertRaises(ValueError): codec.apply_diff(cache.validate_packet(valid), diff, AUTO)

    def test_malformed_rehashed_cached_and_uncached_history_rejections_match_baseline(self):
        heads = histories(5); invalid = []
        for location in (0, 4):
            for field in ('diff_schema', 'change_shape', 'base_id', 'order', 'hash'):
                packet = deepcopy(heads[5]); event = packet['history'][location]
                if field == 'diff_schema': event['diff']['schema'] = 'garbage'
                elif field == 'change_shape': event['changes'] = [{}]
                elif field == 'base_id': event['base_packet_id'] = '0' * 64
                elif field == 'order': event['previous_order']['claims'] = []
                else: event['application_id'] = '0' * 64
                if field != 'hash': event['application_id'] = codec.digest({k: v for k, v in event.items() if k != 'application_id'})
                packet['packet_id'] = codec.digest(codec._packet_payload(packet))
                invalid.append((str(location) + field, packet))
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode)); cache.validate_packet(heads[4]); cache.validate_packet(heads[5])
            for name, packet in invalid:
                with self.assertRaises(ValueError, msg=name): codec.validate_packet(packet)
                with self.assertRaises(ValueError, msg=name): cache.validate_packet(packet)


if __name__ == '__main__':
    unittest.main()
