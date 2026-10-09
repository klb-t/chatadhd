"""Mechanics tests use authored fixtures, never model-quality observations."""
import copy
import hashlib
import itertools
import json
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure import experiment_context_preparation_v1 as c

ROOT = Path(__file__).resolve().parents[3]
PLAN = ROOT / 'docs/research/thread7_real_2026-10-09/continuation_01/context-plan.json'


def fixture():
    def node(i, parent, role, text):
        return {'node_id': i, 'parent_node_id': parent, 'role': role,
                'source_pointer': '/mapping/' + i,
                'native_message': {'content': {'parts': [text]}, 'metadata': {'retained': True}}}
    source = {'schema': 'loom.research_context_source/1', 'family_id': 'mechanics-only',
              'source_sha256': 'a' * 64, 'split': 'development', 'prior_use': 'mechanics_fixture',
              'nodes': [node('r', None, 'user', ' exact\n  whitespace '),
                        node('a', 'r', 'assistant', 'response'),
                        node('u', 'a', 'user', 'Please be concise.'),
                        node('sibling', 'r', 'assistant', 'sibling'),
                        node('later', 'u', 'assistant', 'future response')]}
    task = {'id': 'mechanics-question', 'family_id': source['family_id'], 'query': {'instruction': 'Answer request at u'},
            'checkpoint_boundary_node_id': 'u'}
    plan = json.loads(PLAN.read_text())
    profiles = {plan['selected_profile']: {'source_reference': 'mechanical-fixture-only', 'source_sha256': 'b'*64,
                'version': 'fixture-v1', 'value': {'style': 'concise'}, 'selection_authority': 'research_protocol'}}
    variant = dict(context_representation='source_graph', context_resolution='full',
                   response_form='graph_direct', preference_mode='none')
    return source, task, plan, profiles, variant


class ContextPreparationTests(unittest.TestCase):
    def setUp(self):
        self.s, self.t, self.p, self.profiles, self.v = fixture()

    def view(self, representation='source_graph', resolution='full'):
        return c.prepare_view(self.s, self.t, representation, resolution, self.p['context_policy'])

    def prepare(self):
        return c.prepare_variant(self.s, self.t, self.v, self.p, self.profiles)

    def test_full_graph_preserves_native_messages(self):
        result = self.view()
        self.assertEqual(result['exact_output']['payload']['nodes'], self.s['nodes'])
        self.assertEqual(result['loss']['omitted_node_ids'], [])

    def test_text_preserves_string_bytes_and_declares_metadata_loss(self):
        result = self.view('exact_text')
        self.assertEqual(result['exact_output']['payload']['segments'][0]['exact_text_parts'], [' exact\n  whitespace '])
        self.assertTrue(result['loss']['native_metadata_omitted_from_request'])

    def test_nontext_omission_is_counted(self):
        self.s['nodes'][0]['native_message']['content']['parts'].append({'audio': 'opaque'})
        self.assertEqual(self.view('exact_text')['loss']['nontext_parts_omitted'], 1)
        self.assertIn({'audio': 'opaque'}, self.view()['exact_output']['payload']['nodes'][0]['native_message']['content']['parts'])

    def test_explicit_fields_not_semantic_extraction(self):
        result = self.view('explicit_fields')
        self.assertEqual(result['exact_output']['payload']['records'][0]['native_message'], self.s['nodes'][0]['native_message'])
        self.assertFalse(result['semantic_inference_performed'])

    def test_structural_fields_cannot_drop_source_content(self):
        self.p['context_policy']['explicit_fields'] = ['node_id']
        with self.assertRaisesRegex(ValueError, 'requires_source_content'):
            self.view('explicit_fields')

    def test_checkpoint_is_derived_ancestor_path_not_user_summary(self):
        result = self.view(resolution='checkpoint')
        self.assertEqual([n['node_id'] for n in result['exact_output']['payload']['nodes']], ['r', 'a', 'u'])
        self.assertFalse(result['exact_output']['selection']['user_declared_checkpoint'])
        self.assertFalse(result['exact_output']['selection']['summary_generated'])
        self.assertEqual(set(result['loss']['omitted_node_ids']), {'sibling', 'later'})

    def test_missing_checkpoint_pending_without_placeholder(self):
        self.t.pop('checkpoint_boundary_node_id')
        result = self.view(resolution='checkpoint')
        self.assertEqual(result['status'], 'pending')
        self.assertIsNone(result['output_sha256'])
        self.assertNotIn('exact_output', result)

    def test_selected_subgraph_boundary_is_explicit(self):
        self.p['context_policy']['ancestor_window_nodes'] = 1
        result = self.view(resolution='selected_subgraph')
        self.assertEqual(result['loss']['boundary_edges'], [{'child': 'u', 'parent': 'a'}])
        self.assertEqual(result['exact_output']['payload']['edges'], [])

    def test_unknown_selected_node_rejected(self):
        self.t['selected_node_ids'] = ['missing']
        with self.assertRaisesRegex(ValueError, 'unknown_selected_node'):
            self.view(resolution='selected_subgraph')

    def test_cycle_rejected(self):
        self.s['nodes'][0]['parent_node_id'] = 'u'
        with self.assertRaisesRegex(ValueError, 'source_parent_cycle'):
            self.view()

    def test_dangling_parent_rejected(self):
        self.s['nodes'][0]['parent_node_id'] = 'missing'
        with self.assertRaisesRegex(ValueError, 'unresolved_source_parent'):
            self.view()

    def test_declared_missing_native_parent_remains_a_gap(self):
        self.s['nodes'][0]['parent_node_id'] = 'not-in-export'
        self.s['external_parent_ids'] = ['not-in-export']
        result = self.view(resolution='checkpoint')
        self.assertEqual(result['loss']['unresolved_native_parent_ids'], ['not-in-export'])
        self.assertEqual(c.ancestor_ids(self.s, 'u'), ['r', 'a', 'u'])

    def test_missing_native_ancestry_uses_labelled_array_prefix(self):
        for node in self.s['nodes']:
            node['parent_node_id'] = None
        self.s['parent_semantics'] = 'not_available'
        self.s['source_order_node_ids'] = [n['node_id'] for n in self.s['nodes']]
        result = self.view(resolution='checkpoint')
        self.assertEqual(result['exact_output']['selection']['kind'], 'derived_source_array_prefix_snapshot')
        self.assertTrue(result['exact_output']['selection']['array_order_is_not_claimed_chronology'])
        self.assertEqual(result['exact_output']['payload']['edges'], [])

    def test_native_anthropic_text_fallback_does_not_invent_parts(self):
        self.assertEqual(c.text_parts({'text': ' exact\n', 'content': []}), ([' exact\n'], 0))

    def test_anthropic_top_level_text_with_nontext_blocks_is_preserved(self):
        result = c.text_projection({'text': 'available literal text', 'content': [{'type': 'thinking', 'thinking': 'uninterpreted'}]})
        self.assertEqual(result['parts'], ['available literal text'])
        self.assertEqual(result['fields'], ['/text'])
        self.assertEqual(result['nontext_parts_omitted'], 1)

    def test_distinct_native_text_alternatives_are_explicit(self):
        result = c.text_projection({'text': ' alternative ', 'content': [{'type': 'text', 'text': 'different'}]})
        self.assertEqual(result['parts'], ['different', ' alternative '])
        self.assertEqual(result['fields'], ['/content/0/text', '/text'])

    def test_identical_native_text_is_explicit_alias(self):
        result = c.text_projection({'text': 'same', 'content': [{'type': 'text', 'text': 'same'}]})
        self.assertEqual(result['parts'], ['same'])
        self.assertEqual(result['equivalent_aliases'], [{'field': '/text', 'identical_to_field': '/content/0/text'}])

    def test_duplicate_node_rejected(self):
        self.s['nodes'].append(copy.deepcopy(self.s['nodes'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate_source_node'):
            self.view()

    def test_preference_missing_is_pending(self):
        self.v['preference_mode'] = 'source_explicit'
        result = self.prepare()
        self.assertEqual(result['status'], 'pending')
        self.assertEqual(result['calls'], [])

    def test_exact_user_preference_annotation_is_attributed(self):
        self.s['explicit_preference_evidence'] = [{'node_id': 'u', 'quote': 'be concise',
            'classification': 'explicit_preference', 'annotation_authority': 'researcher_annotation',
            'source_pointer': '/mapping/u', 'source_message_sha256': c.digest(self.s['nodes'][2]['native_message']), 'scope': 'this request only'}]
        self.v['preference_mode'] = 'source_explicit'
        result = self.prepare()
        self.assertEqual(result['status'], 'prepared')
        self.assertEqual(result['preference']['annotation_status'], 'provisional_unless_owner_annotation')

    def test_invented_preference_quote_rejected(self):
        self.s['explicit_preference_evidence'] = [{'node_id': 'u', 'quote': 'Use full explanations',
            'classification': 'explicit_preference', 'annotation_authority': 'researcher_annotation',
            'source_pointer': '/mapping/u', 'source_message_sha256': c.digest(self.s['nodes'][2]['native_message']), 'scope': 'this request only'}]
        self.v['preference_mode'] = 'source_explicit'
        with self.assertRaisesRegex(ValueError, 'quote_not_exact'):
            self.prepare()

    def test_assistant_statement_cannot_be_user_preference(self):
        self.s['explicit_preference_evidence'] = [{'node_id': 'a', 'quote': 'response',
            'classification': 'explicit_preference', 'annotation_authority': 'researcher_annotation',
            'source_pointer': '/mapping/u', 'source_message_sha256': c.digest(self.s['nodes'][2]['native_message']), 'scope': 'this request only'}]
        self.v['preference_mode'] = 'source_explicit'
        with self.assertRaisesRegex(ValueError, 'not_user_statement'):
            self.prepare()

    def test_existing_profile_does_not_claim_user_selection(self):
        self.v['preference_mode'] = 'selected_profile'
        result = self.prepare()
        self.assertFalse(result['preference']['user_preference_claimed'])
        self.assertFalse(result['preference']['profile_engine_mutation'])

    def test_unknown_profile_rejected(self):
        self.v['preference_mode'] = 'selected_profile'
        self.p['selected_profile'] = 'invented'
        with self.assertRaisesRegex(ValueError, 'unknown_selected_profile'):
            self.prepare()

    def test_all_parameters_preserved(self):
        self.p['request_parameters']['reasoning'] = {'effort': 'low'}
        body = self.prepare()['calls'][0]['request']
        for k, v in self.p['request_parameters'].items():
            self.assertEqual(body[k], v)

    def test_parameters_cannot_override_context(self):
        self.p['request_parameters']['messages'] = []
        with self.assertRaisesRegex(ValueError, 'parameters_override_messages'):
            self.prepare()

    def test_inference_and_current_cost_not_fabricated(self):
        result = self.prepare()
        self.assertFalse(result['inference_performed'])
        self.assertIsNone(result['cost_estimate_usd'])
        self.assertEqual(result['provider_admission'], 'unknown')
        self.assertEqual(result['calls'][0]['headers']['X-OpenRouter-Cache'], 'false')

    def test_one_call_structure_and_two_call_extraction_are_distinct(self):
        self.v['response_form'] = 'text_plus_structure'
        one = self.prepare()
        self.v['response_form'] = 'text_then_structure'
        two = self.prepare()
        self.assertEqual(len(one['calls']), 1)
        self.assertEqual(len(two['calls']), 2)
        self.assertNotEqual(one['id'], two['id'])
        self.assertIsNone(two['calls'][1]['request'])
        self.assertEqual(two['calls'][1]['status'], 'pending')

    def test_second_call_cannot_use_placeholder(self):
        self.v['response_form'] = 'text_then_structure'
        row = self.prepare()
        with self.assertRaisesRegex(ValueError, 'complete_model_response_required'):
            c.materialize_extraction(row, {'call_id': row['calls'][0]['id'], 'status': 'pending'}, self.p)

    def test_second_call_binds_capture_without_mutating_first(self):
        self.v['response_form'] = 'text_then_structure'
        row = self.prepare(); original = copy.deepcopy(row)
        response = {'call_id': row['calls'][0]['id'], 'status': 'completed', 'origin': 'model_inference',
                    'response_cache_hit': False, 'text': 'mechanical captured fixture',
                    'text_sha256': hashlib.sha256(b'mechanical captured fixture').hexdigest(),
                    'immutable_capture_sha256': 'b'*64}
        result = c.materialize_extraction(row, response, self.p)
        self.assertEqual(row, original)
        self.assertEqual(result['input_capture_sha256'], 'b'*64)
        self.assertEqual(result['status'], 'prepared')
        self.assertFalse(result['inference_performed'])

    def test_cross_family_split_hash_leakage(self):
        other = copy.deepcopy(self.s); other['family_id'] = 'same-source'; other['split'] = 'validation'
        with self.assertRaisesRegex(ValueError, 'split_leakage'):
            c.validate_corpus([self.s, other])

    def test_prior_blind_cannot_be_relabelled(self):
        self.s['prior_use'] = 'used_blind'
        with self.assertRaisesRegex(ValueError, 'used_blind'):
            c.validate_source(self.s)

    def test_hash_changes_with_source_and_policy(self):
        first = self.view()
        self.s['nodes'][0]['native_message']['metadata']['new'] = 1
        second = self.view()
        self.assertNotEqual(first['input_sha256'], second['input_sha256'])
        self.assertNotEqual(first['output_sha256'], second['output_sha256'])

    def test_full_dimension_matrix_preserves_pending_denominator(self):
        rows = list(c.iter_preparations([self.s], [self.t], self.p, self.profiles))
        self.assertEqual(len(rows), 81)
        self.assertEqual(sum(r['status'] == 'pending' for r in rows), 27)
        self.assertEqual(sum(len(r['calls']) for r in rows), 72)

    def test_matrix_lazy_before_product_exhaustion(self):
        self.p['variants']['axes'] += [{'name': 'mechanics-extra-'+str(i), 'values': list(range(100))} for i in range(10)]
        iterator = c.iter_preparations([self.s], [self.t], self.p, self.profiles)
        rows = list(itertools.islice(iterator, 3))
        self.assertEqual(len(rows), 3)

    def test_private_release_manifest_verifies_all_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            target = Path(d) / 'release'
            receipt = c.prepare_release([self.s], [self.t], self.p, self.profiles, target)
            manifest = json.loads((target/'MANIFEST.json').read_text())
            for name, sha in manifest['files'].items():
                self.assertEqual(hashlib.sha256((target/name).read_bytes()).hexdigest(), sha)
            self.assertEqual(receipt['slots'], 81)
            with self.assertRaisesRegex(ValueError, 'new_context_release'):
                c.prepare_release([self.s], [self.t], self.p, self.profiles, target)


if __name__ == '__main__':
    unittest.main()
