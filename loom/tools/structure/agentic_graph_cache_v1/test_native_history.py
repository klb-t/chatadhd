"""Nonempty native journal differential tests, independent of timing fixtures."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import unittest

from . import cache as module
from .test_cache import fixture, policy, MODEL, AUTO

codec = module.codec
TOMBSTONES = dict(AUTO, allow_source_tombstones=True)
RESULTS = []


def native_sequence():
    root = fixture(); heads, diffs, receipts = [root], [], []
    original_source, original_claim = deepcopy(root['sources'][0]), deepcopy(root['claims'][0])
    for step in range(12):
        before = heads[-1]
        diff = codec.empty_diff(before, proposal_id='native-event-' + str(step), origin=MODEL,
                                known_at='2026-09-30T12:20:' + format(step, '02d') + 'Z')
        if step == 0:
            diff['task'] = {'before_sha256': codec.digest(before['task']),
                            'after': {'operation': 'mixed_graph_methods', 'budget': {'cpu': 1000000000}, 'options': [True, None]}}
        elif step == 1:
            diff['definitions']['add'] = [{'id': 'definition_open', 'kind': 'unknown_native_kind',
                'description': 'instrument proposal only', 'examples': [], 'origin': deepcopy(MODEL), 'attrs': {'uncertain': True}}]
        elif step == 2:
            entity = deepcopy(before['entities'][0]); entity['label'] = 'Żółta lampa'
            entity['aliases'].append({'key': 'light', 'surface': 'light', 'lang': 'en',
                                     'method': 'scripted-audit', 'count': 1, 'confidence': .5})
            diff['entities']['update'] = [{'id': entity['id'], 'before_sha256': codec.digest(before['entities'][0]), 'after': entity}]
        elif step == 3:
            claim = deepcopy(before['claims'][0]); claim['assessment']['status'] = 'contested'
            diff['claims']['update'] = [{'id': claim['id'], 'before_sha256': codec.digest(before['claims'][0]), 'after': claim}]
        elif step == 4:
            source = deepcopy(original_source); source['observation']['id'] = 'ob_ephemeral'; source['known_at'] = None
            diff['sources']['add'] = [source]
        elif step == 5:
            entity = deepcopy(root['entities'][0]); entity.update(id='entity_child', kind='unknown_native_kind',
                canonical_key='new child concept', label='child concept', parent=root['entities'][0]['id'])
            diff['entities']['add'] = [entity]
            diff['annotations']['operations'] = [{'kind': 'split_entities', 'inputs': [root['entities'][0]['id']],
                                                  'outputs': ['entity_child'], 'reason': 'explicit added child; no hidden edit'}]
        elif step == 6:
            claim = deepcopy(original_claim); claim.update(id='claim_child', subject='entity_child', predicate='new_unverified_relation')
            diff['claims']['add'] = [claim]
        elif step == 7:
            source = next(source for source in before['sources'] if source['observation']['id'] == 'ob_ephemeral')
            diff['sources']['remove'] = [{'id': 'ob_ephemeral', 'before_sha256': codec.digest(source), 'reason': 'projection tombstone'}]
        elif step == 8:
            for claim in before['claims']:
                diff['claims']['remove'].append({'id': claim['id'], 'before_sha256': codec.digest(claim), 'reason': 'competing proposal kept in history'})
            child = next(entity for entity in before['entities'] if entity['id'] == 'entity_child')
            diff['entities']['remove'] = [{'id': child['id'], 'before_sha256': codec.digest(child), 'reason': 'explicit undo child'}]
        elif step == 9:
            diff['sources']['remove'] = [{'id': original_source['observation']['id'], 'before_sha256': codec.digest(original_source),
                                        'reason': 'source projection hidden; raw before bytes retained'}]
        elif step == 10:
            diff['sources']['add'] = [deepcopy(original_source)]; diff['claims']['add'] = [deepcopy(original_claim)]
        else:
            definition = before['definitions'][0]
            diff['definitions']['remove'] = [{'id': definition['id'], 'before_sha256': codec.digest(definition),
                                             'reason': 'definition remains in reversible history'}]
        after, receipt = codec.apply_diff(before, diff, TOMBSTONES)
        heads.append(after); diffs.append(diff); receipts.append(receipt)
    return heads, diffs, receipts


def rehash_event_and_head(packet, index):
    event = packet['history'][index]
    event['application_id'] = codec.digest({key: value for key, value in event.items() if key != 'application_id'})
    packet['packet_id'] = codec.digest(codec._packet_payload(packet))
    return packet


class NativeHistoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.heads, cls.diffs, cls.receipts = native_sequence()

    def test_all_native_heads_application_receipts_and_inverse_exact(self):
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode))
            for index, head in enumerate(self.heads):
                cached, diagnostic = cache.validate_with_receipt(head)
                self.assertEqual(codec.safe.canonical(cached), codec.safe.canonical(codec.validate_packet(head)))
                expected = 'verified_immediate_parent_hit' if mode == 'verified_history' and index else 'strict_fallback'
                self.assertEqual(diagnostic['path'], expected)
                if index:
                    applied, receipt = codec.apply_diff(cache.validate_packet(self.heads[index - 1]), self.diffs[index - 1], TOMBSTONES)
                    self.assertEqual(codec.safe.canonical(receipt), codec.safe.canonical(self.receipts[index - 1]))
                    self.assertEqual(applied, head); self.assertEqual(codec.invert_application(receipt, cached), self.heads[index - 1])
                RESULTS.append({'case': 'native_head_equivalence', 'mode': mode, 'event_count': index, 'pass': True,
                                'path': diagnostic['path'], 'receipt_and_inverse_checked': bool(index)})

    def test_raw_tombstones_and_origin_known_at_are_exactly_retained(self):
        cache = module.VerifiedPacketCache(policy()); cache.validate_packet(self.heads[9])
        hidden = cache.validate_packet(self.heads[10]); self.assertEqual(hidden['sources'], [])
        source_change = hidden['history'][9]['changes'][0]
        self.assertEqual(source_change['before'], self.heads[0]['sources'][0])
        self.assertEqual(source_change['before_provenance'], self.heads[0]['provenance']['sources']['ob_test'])
        ephemeral_change = hidden['history'][7]['changes'][0]
        self.assertIsNone(ephemeral_change['before']['known_at'])
        self.assertEqual(ephemeral_change['before_provenance']['origin'], MODEL)
        restored = cache.validate_packet(self.heads[11])
        self.assertEqual(restored['sources'][0], self.heads[0]['sources'][0])
        self.assertEqual(restored['claims'][0]['assessment']['origin'], 'archive')
        self.assertEqual(restored['provenance']['claims']['cl_test']['origin'], MODEL)
        self.assertEqual(restored['provenance']['sources']['ob_test']['known_at'], self.heads[0]['sources'][0]['known_at'])
        RESULTS.append({'case': 'source_tombstone_restore_origin_time', 'pass': True})

    def test_rehashed_native_before_after_and_old_journal_corruption_reject(self):
        cases = []
        for index in (2, 7, 9):
            original = self.heads[10]
            for field in ('before_record', 'before_provenance', 'wrong_action', 'missing_before', 'duplicate_change', 'previous_order'):
                packet = deepcopy(original); event = packet['history'][index]; change = event['changes'][0]
                if field == 'before_record':
                    if change['collection'] == 'sources':
                        change['before']['observation']['text'] += ' altered retained raw text'
                        change['before']['text_sha256'] = hashlib.sha256(change['before']['observation']['text'].encode()).hexdigest()
                    else: change['before']['label'] = 'tampered old label'
                    change['before_provenance']['record_sha256'] = codec.digest(change['before'])
                elif field == 'before_provenance': change['before_provenance']['origin']['actor'] = 'different actor'
                elif field == 'wrong_action': change['action'] = 'remove' if change['action'] == 'update' else 'update'
                elif field == 'missing_before': change['before'] = None; change['before_provenance'] = None
                elif field == 'duplicate_change': event['changes'].append(deepcopy(change))
                else: event['previous_order'][change['collection']] = []
                cases.append((str(index) + ':' + field, rehash_event_and_head(packet, index)))
        packet = deepcopy(self.heads[4]); packet['history'][3]['changes'][0]['after']['assessment']['status'] = 'rejected'
        cases.append(('latest:after_record', rehash_event_and_head(packet, 3)))
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode))
            cache.validate_packet(self.heads[3]); cache.validate_packet(self.heads[9]); cache.validate_packet(self.heads[10])
            for name, packet in cases:
                with self.assertRaises(ValueError, msg=name): codec.validate_packet(packet)
                with self.assertRaises(ValueError, msg=name): cache.validate_packet(packet)
                RESULTS.append({'case': 'rehashed_native_history_corruption', 'variant': name, 'mode': mode,
                                'strict_rejects': True, 'cache_rejects': True, 'pass': True})

    def test_stale_native_diff_and_obsolete_inverse_remain_rejected(self):
        packet = self.heads[4]
        for mode in ('whole_packet', 'verified_history'):
            cache = module.VerifiedPacketCache(policy(mode)); cache.validate_packet(packet)
            cached = cache.validate_packet(packet)
            stale = codec.empty_diff(packet, proposal_id='stale-native-edit', origin=MODEL)
            stale['claims']['update'] = [{'id': 'cl_test', 'before_sha256': codec.digest(self.heads[0]['claims'][0]),
                                         'after': deepcopy(self.heads[0]['claims'][0])}]
            with self.assertRaises(ValueError): codec.apply_diff(packet, stale, TOMBSTONES)
            with self.assertRaises(ValueError): codec.apply_diff(cached, stale, TOMBSTONES)
            with self.assertRaises(ValueError): codec.invert_application(self.receipts[2], cached)
            RESULTS.append({'case': 'stale_update_and_obsolete_inverse', 'mode': mode, 'pass': True})


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(NativeHistoryTests))
    path = Path(__file__).resolve().parents[4] / 'docs/research/agentic_graph_cache_v1/native_history_repaired_fixture_results.json'
    path.write_text(json.dumps({'complete': result.wasSuccessful(), 'tests_run': result.testsRun,
                                'failures': len(result.failures), 'errors': len(result.errors), 'rows': RESULTS,
                                'model_quality_measured': False, 'paid_calls': 0}, ensure_ascii=False, indent=2) + '\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
