"""Independent metric guards; never import the candidate compiler or adapter."""
from copy import deepcopy
from collections import Counter
import hashlib
import unittest

import candidate_graph_eval as evaluation


def mock_compilation(case):
    """Simple explicit draft rewrite for auditing the auditor, not a method."""
    bundle = case['direct_bundle']
    entity_map = {e['handle']: 'draft-' + e['handle'] for e in bundle['entity_drafts']}
    entities = []
    for expected in bundle['entity_drafts']:
        entity = deepcopy(expected)
        entity['source_handle'] = entity.pop('handle')
        entity['id'] = entity_map[entity['source_handle']]
        entities.append(entity)
    claims = []
    for expected in bundle['claim_drafts']:
        claim = deepcopy(expected)
        claim['source_handle'] = claim.pop('handle')
        claim['id'] = 'draft-' + claim['source_handle']
        for key in ('subject', 'object'):
            claim[key] = entity_map.get(claim[key], claim[key])
        claim['qualifiers']['scope'] = entity_map[claim['qualifiers']['scope']]
        claims.append(claim)
    observations = {o['id']: o for o in case['source_packet']['observations']}
    for span in [s for e in entities for s in e['support']] + [s for c in claims for s in c['assessment']['basis']['support']]:
        observation = observations[span['observation']]
        span['locator'] = deepcopy(observation['locator'])
        span['observation_text_hash'] = hashlib.sha256(observation['text'].encode()).hexdigest()
    return {'drafts': {'entities': entities, 'claims': claims}, 'refmap': {'entities': entity_map}}


class CandidateEvaluationGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = evaluation.load_fixture(evaluation.FIXTURES / 'development_cases.json')

    def test_development_source_bytes_and_anchor_hashes_are_independently_valid(self):
        self.assertEqual(len(self.fixture['cases']), 16)
        self.assertEqual(sum(c['language'] == 'pl' for c in self.fixture['cases']), 8)
        for case in self.fixture['cases']:
            self.assertTrue(evaluation.source_integrity(case)['verified'], case['id'])

    def test_both_frozen_splits_have_source_integrity_and_equivalent_authored_encodings(self):
        for name in ('development_cases.json', 'validation_cases.json'):
            fixture = evaluation.load_fixture(evaluation.FIXTURES / name)
            self.assertEqual(len(fixture['cases']), 16)
            for case in fixture['cases']:
                self.assertTrue(evaluation.source_integrity(case)['verified'], case['id'])
                direct = Counter(evaluation.canonical(evaluation.normalized_claim(c)) for c in case['direct_bundle']['claim_drafts'])
                self.assertEqual(evaluation.frame_gold_claims(case), [direct], case['id'])
                self.assertEqual(case['stage1']['anchors'], case['direct_bundle']['entity_drafts'])

    def test_character_offsets_cannot_replace_utf8_byte_offsets(self):
        case = deepcopy(next(c for c in self.fixture['cases'] if c['language'] == 'pl'))
        case['direct_bundle']['entity_drafts'][0]['support'][0]['byte_len'] = len(case['source_packet']['observations'][0]['text'])
        self.assertFalse(evaluation.source_integrity(case)['verified'])

    def test_changed_source_packet_invalidates_anchor_hash(self):
        case = deepcopy(self.fixture['cases'][0])
        case['source_packet']['snapshot_id'] += '-changed'
        self.assertFalse(evaluation.source_integrity(case)['anchors_hash_verified'])

    def test_copied_input_cannot_pass_empty_compiled_drafts(self):
        case = self.fixture['cases'][0]
        audit = evaluation.draft_audit(case, {'drafts': {'entities': [], 'claims': []}, 'retained_input': deepcopy(case)})
        self.assertFalse(audit['entities_preserved'])
        self.assertFalse(audit['claims_preserved'])

    def test_reference_rewrite_roundtrip_preserves_gold(self):
        case = self.fixture['cases'][0]
        audit = evaluation.draft_audit(case, mock_compilation(case))
        self.assertTrue(audit['entities_preserved'])
        self.assertTrue(audit['claims_preserved'])

    def test_port_order_loss_is_detected(self):
        case = self.fixture['cases'][0]
        compiled = mock_compilation(case)
        for claim in compiled['drafts']['claims']:
            if claim['qualifiers']['extra'].get('port') == 'argument':
                claim['qualifiers']['extra']['ordinal'] = 0
        self.assertFalse(evaluation.draft_audit(case, compiled)['claims_preserved'])

    def test_unrewritten_local_handles_cannot_masquerade_as_resolved_references(self):
        case = self.fixture['cases'][0]
        compiled = mock_compilation(case)
        compiled['drafts']['claims'] = deepcopy(case['direct_bundle']['claim_drafts'])
        audit = evaluation.draft_audit(case, compiled)
        self.assertTrue(audit['entities_preserved'])
        self.assertFalse(audit['compiled_references_closed'])
        self.assertFalse(audit['claims_preserved'])

    def test_fabricated_assessment_is_not_allowed(self):
        case = self.fixture['cases'][0]
        compiled = mock_compilation(case)
        compiled['drafts']['claims'][0]['assessment']['evidence_class'] = 'observed'
        self.assertFalse(evaluation.draft_audit(case, compiled)['no_fabricated_assessment_fields'])

    def test_added_provenance_is_checked_even_when_original_quote_matches(self):
        case = self.fixture['cases'][0]
        compiled = mock_compilation(case)
        self.assertTrue(evaluation.draft_audit(case, compiled)['added_provenance_correct'])
        compiled['drafts']['claims'][0]['assessment']['basis']['support'][0]['locator']['source'] = 'wrong-source'
        audit = evaluation.draft_audit(case, compiled)
        self.assertTrue(audit['claims_preserved'])
        self.assertFalse(audit['added_provenance_correct'])

    def test_missing_eligibility_flags_are_not_closed_gates(self):
        self.assertFalse(evaluation.guard_state({}))
        self.assertFalse(evaluation.guard_state({'no_inference': True, 'no_persistence': False}))
        self.assertTrue(evaluation.guard_state({'no_inference': True, 'no_persistence': True}))

    def measured_pair(self, case_id):
        import json
        report = json.loads((evaluation.FIXTURES / 'initial_report.json').read_text())
        row = next(r for r in report['rows'] if r['id'] == case_id)
        case = next(c for c in self.fixture['cases'] if c['id'] == case_id)
        return case, deepcopy(row['direct_result'])

    def test_copied_drafts_cannot_pass_an_empty_comparison_graph(self):
        case, compiled = self.measured_pair('dev_order_forward')
        self.assertTrue(evaluation.graph_audit(case, compiled)['graph_restrictions_retained'])
        compiled['graph']['nodes'] = []
        compiled['graph']['edges'] = []
        self.assertFalse(evaluation.graph_audit(case, compiled)['graph_restrictions_retained'])

    def test_graph_port_order_loss_is_detected_even_when_drafts_remain(self):
        case, compiled = self.measured_pair('dev_order_forward')
        node = next(n for n in compiled['graph']['nodes'] if n.get('qualifiers', {}).get('port') == 'argument' and n['qualifiers']['ordinal'] == 1)
        node['qualifiers']['ordinal'] = 0
        self.assertTrue(evaluation.draft_audit(case, compiled)['claims_preserved'])
        self.assertFalse(evaluation.graph_audit(case, compiled)['graph_restrictions_retained'])

    def test_empty_abstention_requires_its_located_unknown(self):
        case, compiled = self.measured_pair('dev_ambiguous')
        self.assertTrue(evaluation.coverage_audit(case, compiled)['coverage_retained'])
        compiled['coverage']['located_unknowns'] = []
        self.assertFalse(evaluation.coverage_audit(case, compiled)['coverage_retained'])

    def test_declared_root_loss_is_detected(self):
        case, compiled = self.measured_pair('dev_order_forward')
        for node in compiled['graph']['nodes']:
            if node.get('qualifiers', {}).get('declared_root') is True:
                node['qualifiers']['declared_root'] = False
        self.assertFalse(evaluation.graph_audit(case, compiled)['roots_retained'])


if __name__ == '__main__':
    unittest.main()
