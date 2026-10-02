"""Mechanical/adversarial tests; these are not a language-model quality score."""
import base64
from copy import deepcopy
import hashlib
import json
import unittest
from unittest.mock import patch

try:
    from ..agentic_graph_v1 import packet as graph
except ImportError:
    from agentic_graph_v1 import packet as graph
from . import reply as codec

SYSTEM = {'kind': 'system', 'actor': 'graph_reply_fixture', 'model': None,
          'recipe_sha256': None, 'response_sha256': None}
AUTO = {'schema': 'loom.graph_packet_apply_policy/1', 'acceptance': 'auto',
        'allow_source_tombstones': False}
PREVIEW = dict(AUTO, acceptance='preview')


def fixture():
    entity = {'id': 'ctx_known', 'kind': 'concept', 'canonical_key': 'known',
              'label': 'Earlier user statement', 'labels': {}, 'aliases': [], 'parent': '',
              'first_seen': '', 'last_seen': '', 'evidence_class': 'user', 'origin': 'user',
              'confidence': 0.8, 'status': 'active', 'attrs': {'scope': 'project', 'immutable': True}}
    return graph.make_packet(entities=[entity], task={'preserve': True}, origin=SYSTEM)


def wire(packet=None):
    packet = packet or fixture()
    return {'schema': codec.REPLY_SCHEMA, 'base_packet_sha256': packet['packet_id'], 'response_id': 'r',
            'nodes': [{'id': 'r', 'text': 'Żaba 🐸.\nŻaba 🐸.', 'role': 'response', 'children': ['a', 'b']},
                      {'id': 'a', 'text': 'Żaba 🐸.\n', 'role': 'premise', 'children': ['aa', 'ab']},
                      {'id': 'aa', 'text': 'Żaba ', 'role': 'subject', 'children': []},
                      {'id': 'ab', 'text': '🐸.\n', 'role': 'detail', 'children': []},
                      {'id': 'b', 'text': 'Żaba 🐸.', 'role': 'conclusion', 'children': []}],
            'links': [{'from': 'b', 'predicate': 'refers_to', 'to': 'ctx_known'},
                      {'from': 'b', 'predicate': 'depends_on', 'to': 'a'}]}


def encode(value):
    # Retain noncanonical source bytes, including formatting and Unicode.
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def composed_wire(packet=None):
    value = wire(packet)
    value['schema'] = codec.COMPOSED_REPLY_SCHEMA
    for node in value['nodes']:
        if node['children']:
            node['text'] = None
    return value


def compile_wire(value=None, packet=None, **kwargs):
    packet = packet or fixture()
    return codec.compile_reply(packet, encode(value or wire(packet)), request_id=kwargs.pop('request_id', 'req_1'),
                               turn_id=kwargs.pop('turn_id', 'turn_1'), model='scripted-fixture', **kwargs)


class ReplyTests(unittest.TestCase):
    def test_wire_root_and_parts_same_shape_no_offset_fields(self):
        value = wire()
        self.assertEqual({frozenset(node) for node in value['nodes']},
                         {frozenset({'id', 'text', 'role', 'children'})})
        self.assertIs(codec.validate_reply(value, fixture()), value)
        compilation = compile_wire(value)
        self.assertEqual(compilation['response_text'], value['nodes'][0]['text'])

    def test_source_bytes_and_display_bytes_are_both_exact(self):
        packet = fixture(); value = wire(packet); raw = encode(value)
        compilation = codec.compile_reply(packet, raw, request_id='r', turn_id='t', model='fixture')
        capture = compilation['raw_capture']
        self.assertEqual(base64.b64decode(capture['raw_base64']), raw)
        self.assertEqual(capture['sha256'], hashlib.sha256(raw).hexdigest())
        raw_source, display_source = compilation['diff']['sources']['add']
        self.assertEqual(raw_source['observation']['text'].encode(), raw)
        self.assertEqual(display_source['observation']['text'], compilation['response_text'])
        self.assertEqual(raw_source['text_sha256'], capture['sha256'])

    def test_utf8_nested_repeated_segments_have_exact_distinct_spans(self):
        value = wire(); compilation = compile_wire(value)
        raw = compilation['response_text'].encode('utf-8')
        for node in value['nodes']:
            span = compilation['spans'][node['id']]
            self.assertEqual(raw[span['byte_start']:span['byte_start'] + span['byte_len']], node['text'].encode())
        self.assertEqual(compilation['spans']['b']['byte_start'], len('Żaba 🐸.\n'.encode()))
        self.assertEqual(compilation['spans']['b']['char_start'], len('Żaba 🐸.\n'))
        self.assertNotEqual(compilation['spans']['b']['byte_start'], compilation['spans']['b']['char_start'])
        self.assertEqual(compilation['spans']['aa']['parent_id'], 'a')

    def test_combining_marks_crlf_code_blocks_are_not_normalized(self):
        value = wire(); text = 'e\u0301\r\n```x\r\n```'
        value['nodes'] = [{'id': 'r', 'text': text, 'role': 'response', 'children': ['a', 'b']},
                          {'id': 'a', 'text': 'e\u0301\r\n', 'role': 'claim', 'children': []},
                          {'id': 'b', 'text': '```x\r\n```', 'role': 'example', 'children': []}]
        value['links'] = []
        compilation = compile_wire(value)
        self.assertEqual(compilation['response_text'], text)
        self.assertEqual(compilation['spans']['b']['byte_start'], len('e\u0301\r\n'.encode()))

    def test_context_and_local_semantic_links_are_drafts_only(self):
        value = wire(); value['links'].append({'from': 'a', 'predicate': 'user_confirms', 'to': 'ctx_known'})
        compilation = compile_wire(value)
        nodes = {entity['attrs']['local_id']: entity for entity in compilation['diff']['entities']['add']
                 if entity['kind'] == 'graph_reply_node'}
        links = nodes['b']['attrs']['model_links']
        self.assertEqual(links[0]['target_scope'], 'context')
        self.assertEqual(links[0]['resolved_to'], 'ctx_known')
        self.assertEqual(links[1]['target_scope'], 'local')
        self.assertEqual(links[1]['resolved_to'], nodes['a']['id'])
        for entity in nodes.values():
            self.assertEqual(entity['origin'], 'model_knowledge')
            self.assertEqual(entity['attrs']['content_verification'], 'unverified')
            self.assertEqual(entity['attrs']['confidence_scope'], 'identity/structure_only')
            for link in entity['attrs']['model_links']:
                self.assertEqual(link['status'], 'draft')
                self.assertEqual(link['origin']['kind'], 'model')
        self.assertEqual({c['predicate'] for c in compilation['diff']['claims']['add']},
                         {'has_response', 'contains_part'})

    def test_application_retains_original_record_provenance_and_exact_history(self):
        packet = fixture(); before = deepcopy(packet); compilation = compile_wire(packet=packet)
        applied, receipt = codec.apply_compiled_reply(packet, compilation, AUTO)
        self.assertEqual(packet, before)
        self.assertTrue(receipt['accepted'])
        self.assertEqual(applied['entities'][0], before['entities'][0])
        self.assertEqual(applied['provenance']['entities']['ctx_known'], before['provenance']['entities']['ctx_known'])
        self.assertEqual(applied['task'], before['task'])
        self.assertEqual(graph.invert_application(receipt, applied), before)
        self.assertFalse(receipt['canonical_store_written'])
        self.assertFalse(receipt['acceptance_establishes_content_truth'])

    def test_preview_and_explicit_acceptance_use_existing_policy(self):
        packet = fixture(); compilation = compile_wire(packet=packet)
        previewed, receipt = codec.apply_compiled_reply(packet, compilation, PREVIEW)
        self.assertEqual(previewed, packet); self.assertFalse(receipt['accepted'])
        applied, receipt = codec.apply_compiled_reply(packet, compilation, PREVIEW, explicitly_accepted=True)
        self.assertTrue(receipt['accepted']); self.assertGreater(len(applied['entities']), len(packet['entities']))

    def test_recompile_is_deterministic_host_inputs_bound(self):
        first = compile_wire(); second = compile_wire()
        self.assertEqual(first, second)
        self.assertEqual(codec.validate_compilation(first, fixture()), first)
        changed = compile_wire(turn_id='other')
        self.assertNotEqual(first['turn_entity_id'], changed['turn_entity_id'])
        self.assertNotEqual(first['compilation_sha256'], changed['compilation_sha256'])

    def test_stale_snapshot_rejected_at_compile_and_application(self):
        packet = fixture(); compilation = compile_wire(packet=packet)
        applied, _ = codec.apply_compiled_reply(packet, compilation, AUTO)
        with self.assertRaisesRegex(codec.GraphReplyError, 'stale_base'):
            compile_wire(wire(packet), applied, turn_id='next')
        with self.assertRaisesRegex(codec.GraphReplyError, 'stale_base'):
            codec.apply_compiled_reply(applied, compilation, AUTO)

    def test_branch_turns_and_correction_preserve_old_response(self):
        packet = fixture(); first = compile_wire(packet=packet)
        packet, _ = codec.apply_compiled_reply(packet, first, AUTO)
        old = deepcopy(packet)
        value = wire(packet)
        value['links'].append({'from': 'b', 'predicate': 'corrects', 'to': first['node_ids']['b']})
        branch1 = compile_wire(value, packet, turn_id='branch_1', parent_turn_id=first['turn_entity_id'])
        branch2 = compile_wire(value, packet, turn_id='branch_2', parent_turn_id=first['turn_entity_id'])
        self.assertNotEqual(branch1['turn_entity_id'], branch2['turn_entity_id'])
        after, _ = codec.apply_compiled_reply(packet, branch1, AUTO)
        self.assertEqual(after['entities'][:len(old['entities'])], old['entities'])
        self.assertEqual(after['claims'][:len(old['claims'])], old['claims'])
        topology = [c for c in branch1['diff']['claims']['add'] if c['predicate'] == 'reply_to_turn']
        self.assertEqual(topology[0]['object'], first['turn_entity_id'])
        self.assertFalse(branch1['diff']['entities']['update'])
        # The same snapshot cannot accept both siblings silently: rebase required.
        with self.assertRaisesRegex(codec.GraphReplyError, 'stale_base'):
            codec.apply_compiled_reply(after, branch2, AUTO)

    def test_parent_turn_is_host_supplied_existing_turn_only(self):
        for parent in ('ctx_known', 'missing'):
            with self.assertRaisesRegex(codec.GraphReplyError, 'unknown_parent_turn'):
                compile_wire(parent_turn_id=parent)
        value = wire(); value['parent_turn_id'] = 'ctx_known'
        with self.assertRaisesRegex(codec.GraphReplyError, 'shape_invalid'):
            compile_wire(value)

    def test_duplicate_host_turn_id_cannot_be_rewritten(self):
        packet = fixture(); first = compile_wire(packet=packet)
        packet, _ = codec.apply_compiled_reply(packet, first, AUTO)
        with self.assertRaisesRegex(codec.GraphReplyError, 'add_existing_record'):
            compile_wire(packet=packet)

    def test_partition_omitted_separator_and_overlaps_reject(self):
        for mutation in ('separator', 'repeat', 'ordering', 'invented'):
            value = wire()
            if mutation == 'separator': value['nodes'][1]['text'] = 'Żaba 🐸.'
            elif mutation == 'repeat': value['nodes'][0]['children'] = ['a', 'a']
            elif mutation == 'ordering': value['nodes'][0]['children'] = ['b', 'a']
            else: value['nodes'][4]['text'] += ' extra'
            with self.assertRaises(codec.GraphReplyError, msg=mutation): compile_wire(value)

    def test_orphans_cycles_multiple_parents_and_unknown_children_reject(self):
        for mutation in ('orphan', 'cycle', 'multiple', 'unknown'):
            value = wire()
            if mutation == 'orphan': value['nodes'].append({'id': 'z', 'text': 'z', 'role': 'claim', 'children': []})
            elif mutation == 'cycle': value['nodes'][0]['children'] = ['r']
            elif mutation == 'multiple': value['nodes'][4]['children'] = ['aa']
            else: value['nodes'][1]['children'][0] = 'missing'
            with self.assertRaises(codec.GraphReplyError, msg=mutation): compile_wire(value)

    def test_duplicate_nodes_empty_parts_and_unknown_model_authority_keys_reject(self):
        for mutation in ('duplicate', 'empty', 'authority', 'offset'):
            value = wire()
            if mutation == 'duplicate': value['nodes'].append(deepcopy(value['nodes'][0]))
            elif mutation == 'empty': value['nodes'][2]['text'] = ''
            elif mutation == 'authority': value['nodes'][4]['origin'] = 'user'
            else: value['nodes'][4]['byte_start'] = 0
            with self.assertRaises(codec.GraphReplyError, msg=mutation): compile_wire(value)

    def test_unknown_targets_claim_ids_and_ambiguous_names_reject(self):
        for target in ('missing', 'not_an_entity'):
            value = wire(); value['links'][0]['to'] = target
            with self.assertRaisesRegex(codec.GraphReplyError, 'unknown_context_entity'):
                compile_wire(value)
        value = wire(); value['nodes'][4]['id'] = 'ctx_known'; value['nodes'][0]['children'][1] = 'ctx_known'
        value['links'] = []
        with self.assertRaisesRegex(codec.GraphReplyError, 'identity_collision'): compile_wire(value)

    def test_invalid_first_raw_attempts_retained_no_repair_no_state_mutation(self):
        packet = fixture(); before = deepcopy(packet)
        attempts = [b'{"schema":"a","schema":"b"}', b'{"n":NaN}', b'```json\n{}\n```',
                    b'{"text":"\xff"}', b'{"text":', b'{} trailing', '{"text":"\\ud800"}'.encode()]
        for raw in attempts:
            with self.assertRaises(codec.GraphReplyError) as raised:
                codec.compile_reply(packet, raw, request_id='req', turn_id='turn', model='fixture')
            self.assertEqual(base64.b64decode(raised.exception.capture['raw_base64']), raw)
            self.assertEqual(raised.exception.capture['sha256'], hashlib.sha256(raw).hexdigest())
            self.assertEqual(packet, before)

    def test_rehashed_mutated_compilation_fails_replay_instead_of_trusting_hash(self):
        compilation = compile_wire(); modified = deepcopy(compilation)
        modified['diff']['entities']['add'][1]['origin'] = 'user'
        modified['compilation_sha256'] = graph.digest({k: v for k, v in modified.items() if k != 'compilation_sha256'})
        with self.assertRaisesRegex(codec.GraphReplyError, 'replay_mismatch'):
            codec.apply_compiled_reply(fixture(), modified, AUTO)
        changed = deepcopy(compilation); changed['raw_capture']['raw_base64'] = base64.b64encode(b'{}').decode()
        changed['compilation_sha256'] = graph.digest({k: v for k, v in changed.items() if k != 'compilation_sha256'})
        with self.assertRaisesRegex(codec.GraphReplyError, 'capture_hash_drift'):
            codec.validate_compilation(changed, fixture())

    def test_configured_resource_limits_are_optional_and_do_not_normalize(self):
        value = wire()
        with self.assertRaises(codec.GraphReplyError):
            codec.compile_reply(fixture(), encode(value), request_id='r', turn_id='t', model='fixture',
                                resource_limits={'max_string_bytes': 1})
        self.assertEqual(compile_wire(value)['response_text'], value['nodes'][0]['text'])

    def test_v2_nested_composition_exact_unicode_spans_and_raw_capture(self):
        packet = fixture(); value = composed_wire(packet); before = deepcopy(value)
        compilation = compile_wire(value, packet)
        self.assertEqual(value, before)
        self.assertEqual(compilation['response_text'], wire(packet)['nodes'][0]['text'])
        self.assertEqual(compilation['spans'], compile_wire(wire(packet), packet)['spans'])
        self.assertEqual(base64.b64decode(compilation['raw_capture']['raw_base64']), encode(value))
        applied, receipt = codec.apply_compiled_reply(packet, compilation, AUTO)
        self.assertTrue(receipt['accepted'])
        self.assertEqual(applied['entities'][0], packet['entities'][0])

    def test_v2_null_composite_wire_text_retained_but_rendered_entities_complete(self):
        value = composed_wire(); compilation = compile_wire(value)
        nodes = {e['attrs']['local_id']: e for e in compilation['diff']['entities']['add']
                 if e['kind'] == 'graph_reply_node'}
        self.assertIsNone(nodes['r']['attrs']['wire_text'])
        self.assertIsNone(nodes['a']['attrs']['wire_text'])
        self.assertEqual(nodes['r']['attrs']['response_text'], 'Żaba 🐸.\nŻaba 🐸.')
        self.assertEqual(nodes['aa']['attrs']['wire_text'], 'Żaba ')
        for entity in nodes.values():
            self.assertEqual(entity['attrs']['text'], entity['attrs']['response_text'])
            self.assertEqual(entity['label'], entity['attrs']['response_text'])
        display = compilation['diff']['sources']['add'][1]
        self.assertEqual(display['observation']['attrs']['transform'], 'compose_ordered_leaf_text')

    def test_v2_null_leaf_and_mismatching_composite_strings_reject(self):
        for kind in ('null_leaf', 'wrong_root', 'wrong_nested', 'empty_string'):
            value = composed_wire()
            if kind == 'null_leaf': value['nodes'][2]['text'] = None
            elif kind == 'wrong_root': value['nodes'][0]['text'] = 'rewritten answer'
            elif kind == 'wrong_nested': value['nodes'][1]['text'] = 'Żaba 🐸.'
            else: value['nodes'][4]['text'] = ''
            with self.assertRaises(codec.GraphReplyError, msg=kind): compile_wire(value)
        # Composites may carry explicit text when it is an exact checkable copy.
        value = composed_wire(); value['nodes'][1]['text'] = 'Żaba 🐸.\n'
        self.assertEqual(compile_wire(value)['response_text'], 'Żaba 🐸.\nŻaba 🐸.')

    def test_v2_deep_flat_tree_validation_and_rendering_do_not_use_recursion(self):
        packet = fixture(); count = 1500
        value = {'schema': codec.COMPOSED_REPLY_SCHEMA, 'base_packet_sha256': packet['packet_id'],
                 'response_id': 'n0', 'nodes': [
                     {'id': 'n' + str(i), 'text': None if i < count - 1 else 'Ż🐸',
                      'role': 'response' if i == 0 else 'part',
                      'children': ['n' + str(i + 1)] if i < count - 1 else []}
                     for i in range(count)], 'links': []}
        before = deepcopy(value)
        self.assertIs(codec.validate_reply(value, packet), value)
        spans = codec._spans(value)
        self.assertEqual(value, before)
        self.assertEqual(len(spans), count)
        self.assertEqual(spans['n0']['byte_len'], len('Ż🐸'.encode()))
        self.assertEqual(spans['n1499']['byte_len'], len('Ż🐸'.encode()))

    def test_v1_explicit_text_requirement_and_existing_entity_attrs_unchanged(self):
        compilation = compile_wire(wire())
        self.assertEqual(codec.REPLY_SCHEMA, 'loom.graph_reply/1')
        for entity in compilation['diff']['entities']['add']:
            self.assertNotIn('wire_text', entity['attrs'])
            self.assertNotIn('response_text', entity['attrs'])
        self.assertEqual(compilation['diff']['sources']['add'][1]['observation']['attrs']['transform'],
                         'json_decode_root_text')
        value = composed_wire(); value['schema'] = codec.REPLY_SCHEMA
        with self.assertRaisesRegex(codec.GraphReplyError, 'node_text_required'): compile_wire(value)

    def test_unknown_versions_and_resource_capability_failure_preserve_raw_attempt(self):
        for schema in ('loom.graph_reply/3', {}, None):
            value = wire(); value['schema'] = schema
            with self.assertRaisesRegex(codec.GraphReplyError, 'schema_invalid'):
                compile_wire(value)
        packet = fixture(); raw = encode(composed_wire())
        for target in ('safe.parse_json', 'graph.preview_diff'):
            with patch.object(codec.safe if target.startswith('safe') else codec.graph,
                              target.split('.')[1], side_effect=RecursionError):
                with self.assertRaisesRegex(codec.GraphReplyError, 'resource_capability_exhausted') as raised:
                    codec.compile_reply(packet, raw, request_id='r', turn_id='t', model='fixture')
                self.assertEqual(base64.b64decode(raised.exception.capture['raw_base64']), raw)


if __name__ == '__main__':
    unittest.main()
