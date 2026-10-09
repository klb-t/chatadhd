"""Authored mechanics fixtures; these are not empirical preference labels."""
from copy import deepcopy
import unittest

from loom.tools.structure import experiment_preference_annotations_v1 as a
from loom.tools.structure import experiment_context_preparation_v1 as c
from loom.tools.structure.test_experiment_context_preparation_v1 import fixture
from loom.tools.structure.experiment_workflow_v1 import digest


def bundle(source, include=True, decision='accept', kind='declaration', node_index=2):
    node = source['nodes'][node_index]
    quote = c.text_projection(node['native_message'])['parts'][0]
    evidence = a.quote_evidence(source, node, quote)
    rows = [{'id': 'evidence:fixture', 'evidence': evidence,
             'interpretation': {'scope': {'kind': 'next_response', 'global': False},
                                'time': {'from': evidence['source_time'], 'until': None},
                                'utterance_kind': kind, 'relations': [],
                                'adopt_as_profile': False}}] if include else []
    pass1 = {'schema': 'loom.research_preference_evidence/1', 'records': rows,
             'coverage': {source['family_id']: {
                 'source_context_sha256': digest(source),
                 'review_method': 'authored_mechanics_fixture',
                 'user_message_inventory': {n['node_id']: digest(n['native_message'])
                                            for n in source['nodes'] if n['role'] == 'user'}}}}
    pass2 = {'schema': 'loom.research_preference_review/1', 'evidence_sha256': digest(pass1),
             'independent_adjudication': False,
             'records': [{'evidence_id': r['id'], 'decision': decision, 'reviewer': 'fixture-reviewer',
                          'reason': 'Mechanics only'} for r in rows]}
    return pass1, pass2


class PreferenceAnnotationTests(unittest.TestCase):
    def setUp(self):
        self.s, self.t, self.p, self.profiles, self.v = fixture()

    def project(self, **kwargs):
        p1, p2 = bundle(self.s, **kwargs)
        return a.project_sources([self.s], p1, p2)[0]

    def test_exact_field_pointer_and_codepoint_offsets(self):
        self.s['nodes'][2]['native_message']['content']['parts'][0] = 'ą🙂 before keep  spaces after'
        ev = a.quote_evidence(self.s, self.s['nodes'][2], 'keep  spaces')
        self.assertEqual(ev['literal_field_pointer'], '/content/parts/0')
        self.assertEqual(ev['quote_start_codepoints'], 10)
        a.validate_evidence(self.s, ev)

    def test_changed_quote_rejected(self):
        p1, p2 = bundle(self.s)
        p1['records'][0]['evidence']['quote'] = 'Not in source'
        p2['evidence_sha256'] = digest(p1)
        with self.assertRaisesRegex(ValueError, 'not_exact'):
            a.project_sources([self.s], p1, p2)

    def test_stale_review_rejected(self):
        p1, p2 = bundle(self.s)
        p1['records'][0]['interpretation']['scope']['global'] = True
        with self.assertRaisesRegex(ValueError, 'binding_mismatch'):
            a.project_sources([self.s], p1, p2)

    def test_unreviewed_candidate_rejected(self):
        p1, p2 = bundle(self.s); p2['records'] = []
        with self.assertRaisesRegex(ValueError, 'review_incomplete'):
            a.project_sources([self.s], p1, p2)

    def test_missing_user_inventory_rejected(self):
        p1, p2 = bundle(self.s)
        p1['coverage'][self.s['family_id']]['user_message_inventory'].pop('r')
        p2['evidence_sha256'] = digest(p1)
        with self.assertRaisesRegex(ValueError, 'message_coverage_incomplete'):
            a.project_sources([self.s], p1, p2)

    def test_assistant_cannot_be_accepted(self):
        p1, p2 = bundle(self.s, node_index=1)
        with self.assertRaisesRegex(ValueError, 'accepted_nonuser'):
            a.project_sources([self.s], p1, p2)

    def test_quote_cannot_be_promoted_to_user_preference(self):
        p1, p2 = bundle(self.s, kind='quote')
        with self.assertRaisesRegex(ValueError, 'unendorsed_as_preference'):
            a.project_sources([self.s], p1, p2)

    def test_hypothesis_remains_uncertain(self):
        out = self.project(kind='hypothesis', decision='uncertain')
        self.assertEqual(out['explicit_preference_evidence'], [])
        self.assertEqual(len(out['preference_review']['uncertain_candidates']), 1)

    def test_original_sources_unchanged(self):
        before = deepcopy(self.s); self.project()
        self.assertEqual(before, self.s)

    def test_existing_unreviewed_empty_source_stays_pending(self):
        got = c.preference_context(self.s, 'source_explicit', {}, None)
        self.assertEqual(got['status'], 'pending')

    def test_reviewed_empty_is_explicit_not_fake_preference(self):
        source = self.project(include=False)
        got = c.preference_context(source, 'source_explicit', {}, None, visible_node_ids=['r'])
        self.assertEqual(got['value'], [])
        self.assertTrue(got['empty_result'])
        self.assertFalse(got['user_preference_claimed'])
        self.assertFalse(got['no_preference_exists_claimed'])

    def test_pending_uncertainty_not_converted_to_false_absence(self):
        source = self.project(decision='uncertain')
        got = c.preference_context(source, 'source_explicit', {}, None, visible_node_ids=['u'])
        self.assertTrue(got['empty_result'])
        self.assertEqual(len(got['uncertain_candidates']), 1)
        self.assertFalse(got['no_preference_exists_claimed'])

    def test_reviewed_source_requires_visibility(self):
        with self.assertRaisesRegex(ValueError, 'visibility_required'):
            c.preference_context(self.project(), 'source_explicit', {}, None)

    def test_invisible_source_annotation_not_leaked(self):
        got = c.preference_context(self.project(), 'source_explicit', {}, None, visible_node_ids=['r'])
        self.assertEqual(got['value'], [])
        self.assertEqual(got['excluded_by_view_boundary_count'], 1)

    def test_variant_selects_only_visible_annotations(self):
        self.v.update(preference_mode='source_explicit', context_resolution='selected_subgraph')
        self.t['selected_node_ids'] = ['r']
        result = c.prepare_variant(self.project(), self.t, self.v, self.p, self.profiles)
        self.assertEqual(result['preference']['value'], [])
        self.assertEqual(result['status'], 'prepared')

    def test_source_drift_blocks_old_review(self):
        source = self.project()
        source['nodes'][2]['native_message']['content']['parts'][0] = 'changed'
        with self.assertRaisesRegex(ValueError, 'inventory_mismatch'):
            c.preference_context(source, 'source_explicit', {}, None, visible_node_ids=['u'])

    def test_annotation_drift_blocks_old_review(self):
        source = self.project(); source['explicit_preference_evidence'][0]['scope'] = 'global'
        with self.assertRaisesRegex(ValueError, 'evidence_mismatch'):
            c.preference_context(source, 'source_explicit', {}, None, visible_node_ids=['u'])

    def test_no_profile_adoption(self):
        p1, p2 = bundle(self.s)
        p1['records'][0]['interpretation']['adopt_as_profile'] = True
        p2['evidence_sha256'] = digest(p1)
        with self.assertRaisesRegex(ValueError, 'adoption_forbidden'):
            a.project_sources([self.s], p1, p2)

    def test_dangling_relation_rejected(self):
        p1, p2 = bundle(self.s)
        p1['records'][0]['interpretation']['relations'] = [{'relation': 'supersedes', 'target_evidence_id': 'missing'}]
        p2['evidence_sha256'] = digest(p1)
        with self.assertRaisesRegex(ValueError, 'relation_target_missing'):
            a.project_sources([self.s], p1, p2)

    def test_posterior_metadata_cannot_leak_into_bounded_request(self):
        source = self.project()
        source['explicit_preference_evidence'][0]['interpretation']['time']['until_source_time'] = 'later-hidden-time'
        source['explicit_preference_evidence'][0]['review']['reason'] = 'hidden-future-content'
        source['explicit_preference_evidence'][0]['scope'] = 'future-derived-scope'
        source['preference_review']['accepted_evidence_sha256'] = digest(source['explicit_preference_evidence'])
        got = c.preference_context(source, 'source_explicit', {}, None, visible_node_ids=['u'])
        self.assertNotIn('later-hidden-time', str(got))
        self.assertNotIn('hidden-future-content', str(got))
        self.assertNotIn('future-derived-scope', str(got))
        self.assertEqual(got['temporal_resolution'], 'unknown_not_injected_from_full_family_review')

    def test_uncertain_reason_cannot_leak_hidden_evidence(self):
        source = self.project(decision='uncertain')
        source['preference_review']['uncertain_candidates'][0]['reason'] = 'later-message-secret'
        got = c.preference_context(source, 'source_explicit', {}, None, visible_node_ids=['u'])
        self.assertNotIn('later-message-secret', str(got))
        self.assertEqual(len(got['uncertain_candidates']), 1)

    def test_release_retains_annotation_inputs_and_bindings(self):
        import tempfile
        import json
        import hashlib
        from pathlib import Path
        p1, p2 = bundle(self.s)
        self.p.update(annotation_evidence_sha256=digest(p1), annotation_review_sha256=digest(p2))
        self.p['variants'] = {'mode': 'list', 'values': [self.v]}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'release'
            _, wrapper = a.prepare_reviewed_release([self.s], p1, p2, [self.t], self.p, self.profiles, output)
            for name, expected in wrapper['files'].items():
                self.assertEqual(hashlib.sha256((output/name).read_bytes()).hexdigest(), expected)
            self.assertEqual(json.loads((output/'annotation-inputs/evidence.json').read_text()), p1)
            self.assertEqual(wrapper['preparation_manifest_sha256'], hashlib.sha256((output/'MANIFEST.json').read_bytes()).hexdigest())

    def test_release_rejects_wrong_plan_binding_before_write(self):
        import tempfile
        from pathlib import Path
        p1, p2 = bundle(self.s)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'release'
            with self.assertRaisesRegex(ValueError, 'plan_annotation_evidence_mismatch'):
                a.prepare_reviewed_release([self.s], p1, p2, [self.t], self.p, self.profiles, output)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
