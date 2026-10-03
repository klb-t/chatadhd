"""Independent counterexamples for reversible graph exchange mechanics."""
from copy import deepcopy
import hashlib
import unittest

from . import packet as codec


RECORDED = {'kind': 'recorded', 'actor': 'import.test', 'model': None, 'recipe_sha256': None, 'response_sha256': None}
MODEL = {'kind': 'model', 'actor': 'reviewer', 'model': 'synthetic-scripted',
         'recipe_sha256': 'a' * 64, 'response_sha256': 'b' * 64}
AUTO = {'schema': 'loom.graph_packet_apply_policy/1', 'acceptance': 'auto', 'allow_source_tombstones': False}
PREVIEW = dict(AUTO, acceptance='preview')


def entity(ident='e_test_a', text='lampa', key='lampa'):
    return {'id': ident, 'kind': 'concept', 'canonical_key': key, 'label': text, 'labels': {'pl': text},
            'aliases': [], 'parent': '', 'first_seen': '', 'last_seen': '', 'evidence_class': 'observed',
            'origin': 'archive', 'confidence': .7, 'status': 'active', 'attrs': {'custom_type_detail': [1, 2]}}


def source():
    text = 'Żaba: jeśli lampa zgaśnie, czekam.'
    locator = {'source': 'sha256:' + 'd' * 64, 'member': 'messages.json', 'json_pointer': '/0/text',
               'byte_start': 30, 'byte_len': len(text.encode()), 'time_start': None, 'time_end': None, 'line': 1}
    return {'observation': {'id': 'ob_test', 'unit': 'un_test', 'kind': 'utterance', 'text': text, 'locator': locator,
            'lang': 'pl', 'date': '2026-09-30', 'ordinal': 0, 'artifact_type': 'conversation', 'speaker': 'frog', 'attrs': {}},
            'known_at': '2026-09-30T12:00:00Z', 'text_sha256': hashlib.sha256(text.encode()).hexdigest()}


def claim(ident='cl_test'):
    s = source()
    return {'id': ident, 'subject': 'e_test_a', 'predicate': 'implies', 'object': 'e_test_b', 'value': None,
            'qualifiers': {'valid_from': '', 'valid_to': '', 'version': '', 'branch': '', 'scope': 'individual-source',
                           'lang': 'pl', 'extra': {'attributed_to': 'frog', 'content_truth': 'unverified'}},
            'assessment': {'basis': {'support': [{'observation': 'ob_test', 'locator': deepcopy(s['observation']['locator']),
                            'quote': s['observation']['text'], 'extractor': 'scripted.test@1', 'quality': 1}], 'derivation': None},
                           'evidence_class': 'observed', 'origin': 'archive', 'confidence': .4,
                           'premises': {'claims': [], 'principles': [], 'assumptions': []},
                           'counter': {'observations': [], 'claims': []}, 'status': 'active',
                           'consequences': {'claims': [], 'predictions': [], 'checks': []},
                           'open': {'slots': [], 'questions': ['Czy to prawda?'], 'fill_query': None},
                           'expected_property': None, 'check_state': 'n/a',
                           'alternatives': [{'object': '', 'value': 'unknown', 'score': .1}]}}


def fixture():
    return codec.make_packet(entities=[entity(), entity('e_test_b', 'żaba czeka', 'żaba czeka')],
                             claims=[claim()], sources=[source()], task={'operation': 'critique_graph'},
                             origin=RECORDED, known_at=None)


def rehash(packet):
    packet['packet_id'] = codec.digest(codec._packet_payload(packet))
    return packet


class PacketTests(unittest.TestCase):
    def test_full_native_claim_roundtrip_no_narrow_projection(self):
        packet = fixture(); before = deepcopy(packet)
        self.assertEqual(codec.decode_packet(codec.encode_packet(packet)), before)
        self.assertEqual(packet['claims'][0], claim())
        self.assertIs(codec.validate_packet(packet), packet)
        self.assertEqual(packet, before)

    def test_native_absent_object_xor_value_and_assessment_invariants(self):
        for change in ('both', 'neither', 'unobserved', 'inferred_missing', 'illegal_origin', 'nan'):
            c = claim()
            if change == 'both': c['value'] = 3
            elif change == 'neither': c['object'] = ''
            elif change == 'unobserved': c['assessment']['basis']['support'] = []
            elif change == 'inferred_missing': c['assessment']['evidence_class'] = 'inferred'
            elif change == 'illegal_origin': c['assessment']['origin'] = 'model'
            else: c['assessment']['confidence'] = float('nan')
            with self.assertRaises(ValueError, msg=change): codec.validate_claim(c)
        c = claim(); c['object'] = ''; c['assessment']['evidence_class'] = 'absent'; c['assessment']['basis']['support'] = []
        codec.validate_claim(c)
        c['value'] = False
        with self.assertRaises(ValueError): codec.validate_claim(c)

    def test_utf8_source_hash_quote_and_locator_counterexamples(self):
        for field in ('text', 'quote', 'locator', 'source_id'):
            packet = fixture()
            if field == 'text': packet['sources'][0]['observation']['text'] += ' changed'
            elif field == 'quote': packet['claims'][0]['assessment']['basis']['support'][0]['quote'] = 'fabricated'
            elif field == 'locator': packet['claims'][0]['assessment']['basis']['support'][0]['locator']['member'] = 'other.json'
            else: packet['claims'][0]['assessment']['basis']['support'][0]['observation'] = 'ob_missing'
            for name in codec.COLLECTIONS:
                for record in packet[name]:
                    packet['provenance'][name][codec.record_id(name, record)]['record_sha256'] = codec.digest(record)
            with self.assertRaises(ValueError, msg=field): codec.validate_packet(rehash(packet))

    def test_exact_utf8_subspan_is_retained_without_full_turn_rewrite(self):
        packet = fixture(); support = packet['claims'][0]['assessment']['basis']['support'][0]
        raw = packet['sources'][0]['observation']['text'].encode(); quote = 'lampa zgaśnie'
        support['quote'] = quote; support['locator']['byte_start'] += raw.index(quote.encode())
        support['locator']['byte_len'] = len(quote.encode())
        packet['provenance']['claims']['cl_test']['record_sha256'] = codec.digest(packet['claims'][0])
        codec.validate_packet(rehash(packet))
        self.assertEqual(support['quote'], quote)
        support['locator']['byte_start'] -= 1
        packet['provenance']['claims']['cl_test']['record_sha256'] = codec.digest(packet['claims'][0])
        with self.assertRaises(ValueError): codec.validate_packet(rehash(packet))

    def test_auto_acceptance_retains_content_assessment_and_model_instrument_origin(self):
        packet = fixture(); before = deepcopy(packet)
        diff = codec.empty_diff(packet, proposal_id='review-1', origin=MODEL, known_at='2026-09-30T12:10:00Z')
        after = deepcopy(claim()); after['assessment']['status'] = 'contested'
        diff['claims']['update'].append({'id': after['id'], 'before_sha256': codec.digest(claim()), 'after': after})
        applied, receipt = codec.apply_diff(packet, diff, AUTO)
        self.assertEqual(packet, before)
        self.assertTrue(receipt['accepted']); self.assertFalse(receipt['canonical_store_written'])
        self.assertFalse(receipt['acceptance_establishes_content_truth'])
        self.assertEqual(applied['claims'][0], after)
        self.assertEqual(applied['claims'][0]['assessment']['origin'], 'archive')
        self.assertEqual(applied['provenance']['claims']['cl_test']['origin'], MODEL)
        self.assertEqual(applied['history'][0]['changes'][0]['before'], claim())
        self.assertEqual(codec.invert_application(receipt, applied), before)

    def test_preview_returns_original_projection_with_reviewable_candidate(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='review-1', origin=MODEL)
        after = deepcopy(entity()); after['label'] = 'new display label'
        diff['entities']['update'] = [{'id': after['id'], 'before_sha256': codec.digest(entity()), 'after': after}]
        preview = codec.preview_diff(packet, diff)
        chosen, receipt = codec.apply_diff(packet, diff, PREVIEW)
        self.assertEqual(chosen, packet); self.assertFalse(receipt['accepted'])
        self.assertEqual(preview['candidate_packet']['entities'][0]['label'], 'new display label')
        chosen, receipt = codec.apply_diff(packet, diff, PREVIEW, explicitly_accepted=True)
        self.assertTrue(receipt['accepted']); self.assertEqual(codec.invert_application(receipt, chosen), packet)

    def test_optimistic_base_and_per_record_hash_reject_stale_edits(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='review-1', origin=MODEL)
        bad = deepcopy(diff); bad['base_packet_sha256'] = '0' * 64
        with self.assertRaises(ValueError): codec.preview_diff(packet, bad)
        bad = deepcopy(diff); bad['claims']['remove'] = [{'id': 'cl_test', 'before_sha256': '0' * 64, 'reason': 'review'}]
        with self.assertRaises(ValueError): codec.preview_diff(packet, bad)

    def test_tombstone_claim_preserves_full_record_and_support_bytes(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='remove', origin=MODEL)
        diff['claims']['remove'] = [{'id': 'cl_test', 'before_sha256': codec.digest(claim()), 'reason': 'unsupported hypothesis'}]
        applied, receipt = codec.apply_diff(packet, diff, AUTO)
        self.assertFalse(applied['claims']); self.assertEqual(applied['sources'], packet['sources'])
        self.assertEqual(applied['history'][0]['changes'][0]['before'], claim())
        self.assertEqual(codec.invert_application(receipt, applied), packet)

    def test_source_tombstone_is_configurable_raw_bytes_always_in_history(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='projection', origin=MODEL)
        diff['claims']['remove'] = [{'id': 'cl_test', 'before_sha256': codec.digest(claim()), 'reason': 'remove dependent projection'}]
        diff['sources']['remove'] = [{'id': 'ob_test', 'before_sha256': codec.digest(source()), 'reason': 'source outside view'}]
        with self.assertRaises(ValueError): codec.apply_diff(packet, diff, AUTO)
        applied, receipt = codec.apply_diff(packet, diff, dict(AUTO, allow_source_tombstones=True))
        self.assertEqual(applied['sources'], [])
        removed = [c for c in applied['history'][0]['changes'] if c['collection'] == 'sources'][0]
        self.assertEqual(removed['before'], source())
        self.assertEqual(codec.invert_application(receipt, applied), packet)

    def test_source_rewrite_same_id_rejected_requires_new_source_record(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='source-update', origin=MODEL)
        updated = source(); updated['observation']['text'] += ' new'
        updated['text_sha256'] = hashlib.sha256(updated['observation']['text'].encode()).hexdigest()
        diff['sources']['update'] = [{'id': 'ob_test', 'before_sha256': codec.digest(source()), 'after': updated}]
        with self.assertRaises(ValueError): codec.validate_diff(diff, packet)

    def test_semantic_identity_change_requires_explicit_new_record(self):
        packet = fixture()
        for collection, old, field, new in (('claims', claim(), 'predicate', 'prevents'),
                                             ('entities', entity(), 'kind', 'process')):
            diff = codec.empty_diff(packet, proposal_id='identity', origin=MODEL)
            updated = deepcopy(old); updated[field] = new
            diff[collection]['update'] = [{'id': old['id'], 'before_sha256': codec.digest(old), 'after': updated}]
            with self.assertRaises(ValueError): codec.validate_diff(diff, packet)

    def test_dangling_merge_or_split_rejected_but_intent_does_not_rewrite_graph(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='merge', origin=MODEL)
        diff['annotations']['operations'] = [{'kind': 'merge_entities', 'inputs': ['e_test_a', 'e_test_b'],
                                             'outputs': ['e_merged'], 'reason': 'candidate identity, not established'}]
        preview = codec.preview_diff(packet, diff)
        self.assertEqual(preview['candidate_packet']['entities'], packet['entities'])
        self.assertEqual(preview['candidate_packet']['claims'], packet['claims'])
        diff['entities']['remove'] = [{'id': 'e_test_b', 'before_sha256': codec.digest(packet['entities'][1]), 'reason': 'merge'}]
        with self.assertRaises(ValueError): codec.preview_diff(packet, diff)

    def test_new_open_definition_predicate_kind_not_restricted_to_existing_vocab(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='discover', origin=MODEL)
        definition = {'id': 'd_new', 'kind': 'owner_defined_structure', 'description': 'An unverified new pattern',
                      'examples': [{'a': 1}], 'origin': MODEL, 'attrs': {'predicate': 'new_predicate'}}
        diff['definitions']['add'] = [definition]
        fresh = entity('e_new', 'unknown thing', 'unknown thing'); fresh['kind'] = 'new_entity_kind'
        diff['entities']['add'] = [fresh]
        c = claim('cl_new'); c.update(subject='e_new', predicate='new_predicate', object='', value={'full': 'literal'})
        diff['claims']['add'] = [c]
        applied, _ = codec.apply_diff(packet, diff, AUTO)
        self.assertEqual(applied['definitions'][0], definition)
        self.assertEqual(applied['claims'][1]['predicate'], 'new_predicate')

    def test_duplicate_edits_cross_collection_ids_and_extra_truth_flag_rejected(self):
        packet = fixture(); diff = codec.empty_diff(packet, proposal_id='duplicate', origin=MODEL)
        remove = {'id': 'cl_test', 'before_sha256': codec.digest(claim()), 'reason': 'candidate'}
        diff['claims']['remove'] = [remove, remove]
        with self.assertRaises(ValueError): codec.validate_diff(diff, packet)
        packet['claims'][0]['truth'] = True
        with self.assertRaises(ValueError): codec.validate_claim(packet['claims'][0])
        packet = fixture(); packet['entities'][0]['id'] = 'cl_test'
        with self.assertRaises(ValueError): codec.validate_packet(rehash(packet))

    def test_inverse_refuses_to_overwrite_newer_projection(self):
        packet = fixture(); d1 = codec.empty_diff(packet, proposal_id='first', origin=MODEL)
        p1, receipt = codec.apply_diff(packet, d1, AUTO)
        p2, _ = codec.apply_diff(p1, codec.empty_diff(p1, proposal_id='second', origin=MODEL), AUTO)
        with self.assertRaises(ValueError): codec.invert_application(receipt, p2)
        self.assertEqual(codec.invert_application(receipt, p1), packet)

    def test_resource_limits_are_configured_data_not_legacy_whole_archive_caps(self):
        value = {'large_integer': 2 ** 256, 'nested': [[[[[[[0]]]]]]]}
        codec.validate_json_resources(value)
        for limits in ({'max_integer_bits': 128}, {'max_depth': 3}, {'max_nodes': 2}):
            with self.assertRaises(ValueError): codec.validate_json_resources(value, limits)
        codec.validate_json_resources({'x': 'a' * (16 * 1024 * 1024 + 1)})
        with self.assertRaises(ValueError): codec.validate_json_resources({'x': 'abc'}, {'max_string_bytes': 2})

    def test_exact_evidence_binding_is_not_semantic_direction_proof(self):
        packet = fixture(); c = claim('cl_reverse'); c['subject'], c['object'] = c['object'], c['subject']
        # Valid JSON, locators and quotes prove transport/source binding only.
        # The reverse interpretation is still a semantic candidate to assess.
        candidate = codec.make_packet(entities=packet['entities'], claims=[c], sources=packet['sources'],
                                      task=packet['task'], origin=MODEL)
        self.assertEqual(candidate['claims'][0]['subject'], 'e_test_b')
        self.assertEqual(candidate['provenance']['claims']['cl_reverse']['origin'], MODEL)

    def test_cyclic_python_containers_rejected_shared_acyclic_values_allowed(self):
        loop = {}; loop['self'] = loop
        with self.assertRaisesRegex(ValueError, 'circular_json'): codec.validate_json_resources(loop)
        loop = []; loop.append(loop)
        with self.assertRaisesRegex(ValueError, 'circular_json'): codec.validate_json_resources(loop)
        shared = {'same': [1, 2]}
        codec.validate_json_resources({'first': shared, 'second': shared})


if __name__ == '__main__':
    unittest.main()
