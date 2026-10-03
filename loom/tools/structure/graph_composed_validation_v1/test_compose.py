"""Independent synthetic composition mechanisms; no sealed inputs or gold."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from . import compose as c

T1 = '2026-08-01T10:00:00Z'
T2 = '2026-08-01T10:01:00Z'


def fixture():
    case = {'id': 'authored_mechanism', 'language': 'en', 'source_id': 'synthetic:composition',
        'turns': [{'id': 't1', 'speaker': 'author', 'known_at': T1,
                   'text': 'A implies B. B implies D. A implies C. C implies D.'},
                  {'id': 't2', 'speaker': 'other', 'known_at': T2, 'text': 'I deny A implies B.'}],
        'node_inventory': [{'id': n, 'text': n, 'aliases': []} for n in 'ABCD'],
        'judgment_queries': [
            {'id': 'q_direct', 'source': 'A', 'target': 'B', 'relation': 'implies',
             'attributed_to': 'author', 'as_of': T2, 'scope': 'explicit_source'},
            {'id': 'q_formal', 'source': 'A', 'target': 'D', 'relation': 'implies',
             'attributed_to': 'author', 'as_of': T2, 'scope': 'formal_implication'}]}
    value = {'source_assertions': [
        {'id': source + target, 'source': source, 'target': target, 'relation': 'implies',
         'polarity': 'positive', 'attributed_to': 'author', 'known_at': T1,
         'evidence': [{'turn_id': 't1', 'quote': case['turns'][0]['text']}]}
        for source, target in [('A', 'B'), ('B', 'D'), ('A', 'C'), ('C', 'D')]], 'status_events': []}
    compiled = c.panel.compile_extraction(value, case)
    gold = {'id': case['id'], 'language': 'en', 'family': 'authored_mechanism',
        'source_assertions': deepcopy(compiled['source_assertions']), 'status_events': [],
        'judgments': [{'query_id': 'q_direct', 'label': 'supported'},
                      {'query_id': 'q_formal', 'label': 'supported'}],
        'formal_paths': [{'query_id': 'q_formal', 'evidence_class': 'inferred',
                          'content_truth': 'unverified', 'minimal_support_paths': [['AB', 'BD'], ['AC', 'CD']]}]}
    return case, value, gold


def record(case, value):
    return {'case_id': case['id'], 'state': 'completed',
            'content': c.panel.safe.canonical(value).decode(), 'raw_response_sha256': 'a' * 64,
            'compiled_original': c.panel.compile_extraction(value, case)}


class CompositionMechanisms(unittest.TestCase):
    def predict(self, case, value, directory):
        with patch.object(c, 'verify_freeze', return_value={}):
            return c.predict_cases([case], [record(case, value)], directory,
                                   split='synthetic', expected_freeze_sha256='synthetic-pin')

    def test_direct_and_formal_scopes_remain_separate(self):
        case, value, _ = fixture()
        with tempfile.TemporaryDirectory() as folder:
            result = self.predict(case, value, Path(folder) / 'first')
        self.assertEqual(result['queries_by_scope'], {'explicit_source': 1, 'formal_implication': 1})
        for arm in c.ARMS:
            self.assertEqual([r['query_id'] for r in result['arms'][arm]['explicit_source']], ['q_direct'])
            formal, = result['arms'][arm]['formal_implication']
            self.assertEqual(formal['label'], 'supported')
            self.assertEqual(formal['basis_class'], 'inferred')
            self.assertEqual(formal['content_truth'], 'unverified')
            self.assertEqual({tuple(p['premise_assertion_ids']) for p in formal['paths']}, {('AB', 'BD'), ('AC', 'CD')})

    def test_bad_model_quote_repaired_without_changing_typed_fields(self):
        case, value, _ = fixture(); value['source_assertions'][0]['evidence'][0]['quote'] = 'invented wording'
        frozen_input = deepcopy(value)
        with tempfile.TemporaryDirectory() as folder:
            self.predict(case, value, Path(folder) / 'first')
            graphs = c.read(Path(folder) / 'first/compiled_variants_first.json')
            hints = c.read(Path(folder) / 'first/binding_provenance_first.json')
        self.assertEqual(value, frozen_input)
        self.assertEqual(graphs['original'][0]['invalid_assertions'], 1)
        self.assertEqual(len(graphs['citation_bound'][0]['source_assertions']), 4)
        self.assertEqual(hints[0]['model_hint'], 'invented wording')
        self.assertEqual(hints[0]['source']['derivation'], 'full_source_copied_by_turn_id')

    def test_cross_actor_event_withheld_only_in_projected_view(self):
        case, value, _ = fixture()
        value['source_assertions'].append({'id': 'other_negative', 'relation': 'implies', 'source': 'A', 'target': 'B',
            'polarity': 'negative', 'attributed_to': 'other', 'known_at': T2,
            'evidence': [{'turn_id': 't2', 'quote': case['turns'][1]['text']}]})
        value['status_events'].append({'assertion_id': 'AB', 'status': 'superseded',
            'superseded_by': 'other_negative', 'known_at': T2,
            'evidence': [{'turn_id': 't2', 'quote': case['turns'][1]['text']}]})
        with tempfile.TemporaryDirectory() as folder:
            result = self.predict(case, value, Path(folder) / 'first')
            audits = c.read(Path(folder) / 'first/projection_audits_first.json')
            graphs = c.read(Path(folder) / 'first/compiled_variants_first.json')
        self.assertEqual(result['arms']['citation_bound']['explicit_source'][0]['label'], 'unknown')
        self.assertEqual(result['arms']['individual_source_view']['explicit_source'][0]['label'], 'supported')
        self.assertEqual(result['arms']['citation_bound']['formal_implication'][0]['state'], 'unavailable')
        self.assertEqual(result['arms']['individual_source_view']['formal_implication'][0]['label'], 'supported')
        self.assertEqual(len(audits[0]['audit']['raw_candidate_events']), 1)
        self.assertEqual(len(audits[0]['audit']['withheld_events']), 1)
        self.assertEqual(graphs['citation_bound'][0]['source_assertions'], graphs['individual_source_view'][0]['source_assertions'])

    def test_unknown_evidence_turn_never_repaired(self):
        case, value, _ = fixture(); value['source_assertions'][0]['evidence'][0]['turn_id'] = 'missing'
        with tempfile.TemporaryDirectory() as folder:
            self.predict(case, value, Path(folder) / 'first')
            graphs = c.read(Path(folder) / 'first/compiled_variants_first.json')
        self.assertEqual(graphs['citation_bound'][0]['invalid_assertions'], 1)
        self.assertEqual(len(graphs['citation_bound'][0]['source_assertions']), 3)

    def test_missing_record_inventory_rejected_before_files(self):
        case, _, _ = fixture()
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
            path = Path(folder) / 'first'
            with self.assertRaisesRegex(ValueError, 'complete_record_inventory_required'):
                c.predict_cases([case], [], path, split='synthetic', expected_freeze_sha256='pin')
            self.assertFalse(path.exists())

    def test_duplicate_query_and_unknown_scope_rejected(self):
        for mutation in ('duplicate', 'scope'):
            case, value, _ = fixture()
            if mutation == 'duplicate': case['judgment_queries'].append(deepcopy(case['judgment_queries'][0]))
            else: case['judgment_queries'][0]['scope'] = 'world_truth'
            with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
                with self.assertRaises(ValueError):
                    c.predict_cases([case], [record(case, value)], Path(folder) / 'first', split='synthetic', expected_freeze_sha256='pin')

    def test_original_compiler_disagreement_is_fatal(self):
        case, value, _ = fixture(); row = record(case, value); row['compiled_original']['source_assertions'] = []
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
            with self.assertRaisesRegex(RuntimeError, 'original_compiler_replay_drift'):
                c.predict_cases([case], [row], Path(folder) / 'first', split='synthetic', expected_freeze_sha256='pin')

    def test_unavailable_graph_retains_every_planned_query(self):
        case, _, gold = fixture()
        row = {'case_id': case['id'], 'state': 'unavailable',
               'compiled_original': {'case_id': case['id'], 'state': 'unavailable', 'reason': 'not_attempted'}}
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
            path = Path(folder) / 'first'
            c.predict_cases([case], [row], path, split='synthetic', expected_freeze_sha256='pin')
            result = c.evaluate_cases([case], [gold], path, expected_freeze_sha256='pin')
        for arm in c.ARMS:
            self.assertEqual(result['arms'][arm]['explicit_source']['unavailable'], 1)
            self.assertEqual(result['arms'][arm]['formal_implication_labels']['unavailable'], 1)
            self.assertEqual(result['arms'][arm]['formal_paths']['paths']['fn'], 2)

    def test_all_prediction_artifacts_frozen_before_gold(self):
        case, value, gold = fixture()
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
            path = Path(folder) / 'first'; self.predict(case, value, path)
            before = c.read(path / 'freeze_before_gold.json')
            self.assertFalse(before['gold_read_by_wrapper'])
            self.assertIn('first_predictions.json', before['files_sha256'])
            self.assertNotIn('first_results.json', before['files_sha256'])
            result = c.evaluate_cases([case], [gold], path, expected_freeze_sha256='synthetic-pin')
            self.assertEqual(result['arms']['original']['formal_paths']['paths']['tp'], 2)
            self.assertIsNone(result['arms']['individual_source_view']['extraction'])

    def test_existing_first_directory_never_overwritten(self):
        case, value, _ = fixture()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'first'; self.predict(case, value, path)
            digest = c.panel.digest_file(path / 'first_predictions.json')
            with self.assertRaises(FileExistsError): self.predict(case, value, path)
            self.assertEqual(c.panel.digest_file(path / 'first_predictions.json'), digest)

    def test_drifted_prediction_cannot_be_scored(self):
        case, value, gold = fixture()
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
            path = Path(folder) / 'first'; self.predict(case, value, path)
            (path / 'first_predictions.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'first_prediction_artifact_drift'):
                c.evaluate_cases([case], [gold], path, expected_freeze_sha256='synthetic-pin')

    def test_unmapped_premise_is_fp_and_does_not_hide_missing_gold(self):
        case, value, gold = fixture(); graph = c.panel.compile_extraction(value, case)
        graph['source_assertions'][0]['attributed_to'] = 'unannotated_actor'
        row = {'query_id': 'q_formal', 'case_id': case['id'], 'state': 'completed', 'complete': True,
               'paths': [{'premise_assertion_ids': ['AB', 'BD']}]}
        result = c.path_metrics([case], [gold], [graph], [row])
        self.assertEqual(result['paths'], c.panel._metric(0, 1, 2))
        self.assertEqual(result['unique_premises_per_query'], c.panel._metric(1, 1, 3))

    def test_duplicate_predicted_path_counts_false_positive(self):
        case, value, gold = fixture(); graph = c.panel.compile_extraction(value, case)
        row = {'query_id': 'q_formal', 'case_id': case['id'], 'state': 'completed', 'complete': True,
               'paths': [{'premise_assertion_ids': p} for p in [['AB', 'BD'], ['AC', 'CD'], ['AB', 'BD']]]}
        result = c.path_metrics([case], [gold], [graph], [row])
        self.assertEqual(result['paths'], c.panel._metric(2, 1, 0))
        self.assertEqual(result['all_minimal_alternatives_preserved_queries'], 0)

    def test_bounded_partial_paths_do_not_claim_complete_support(self):
        case, value, gold = fixture(); graph = c.panel.compile_extraction(value, case)
        row = {'query_id': 'q_formal', 'case_id': case['id'], 'state': 'unavailable', 'complete': False,
               'paths': [{'premise_assertion_ids': ['AB', 'BD']}]}
        result = c.path_metrics([case], [gold], [graph], [row])
        self.assertEqual(result['paths'], c.panel._metric(0, 0, 2))

    def test_extraction_loader_requires_verified_ticket_before_file_read(self):
        case, _, _ = fixture()
        with patch.object(c, 'read') as reader:
            with self.assertRaises(ValueError): c.extraction_records([case], [('manifest', 'run')], {})
            reader.assert_not_called()

    def test_inference_does_not_turn_negative_operand_into_complement(self):
        case, value, _ = fixture(); case['node_inventory'][2]['text'] = 'not C'
        with tempfile.TemporaryDirectory() as folder:
            result = self.predict(case, value, Path(folder) / 'first')
        self.assertEqual(len(result['arms']['original']['formal_implication'][0]['paths']), 2)

    def test_self_consistent_hash_rewrite_cannot_hide_prediction_change(self):
        case, value, gold = fixture()
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
            path = Path(folder) / 'first'; self.predict(case, value, path)
            predicted = c.read(path / 'first_predictions.json')
            predicted['arms']['original']['explicit_source'][0]['label'] = 'refuted'
            (path / 'first_predictions.json').write_bytes(c.panel.safe.canonical(predicted))
            before = c.read(path / 'freeze_before_gold.json')
            before['files_sha256']['first_predictions.json'] = c.panel.digest_file(path / 'first_predictions.json')
            (path / 'freeze_before_gold.json').write_bytes(c.panel.safe.canonical(before))
            with self.assertRaisesRegex(ValueError, 'first_computation_replay_drift'):
                c.evaluate_cases([case], [gold], path, expected_freeze_sha256='synthetic-pin')

    def test_raw_json_failure_retains_unavailable_denominators(self):
        case, _, _ = fixture()
        row = {'case_id': case['id'], 'state': 'completed', 'content': 'not JSON',
            'raw_response_sha256': 'b' * 64, 'compiled_original': {
                'case_id': case['id'], 'state': 'unavailable', 'reason': 'response_compile_or_identity_rejected'}}
        with tempfile.TemporaryDirectory() as folder, patch.object(c, 'verify_freeze', return_value={}):
            result = c.predict_cases([case], [row], Path(folder) / 'first', split='synthetic', expected_freeze_sha256='pin')
        self.assertEqual(result['query_count'], 2)
        for arm in c.ARMS:
            self.assertEqual(result['arms'][arm]['explicit_source'][0]['state'], 'unavailable')

    def test_extraction_batch_missing_case_is_rejected(self):
        case, _, _ = fixture()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder); (path / 'manifest.json').write_bytes(c.panel.safe.canonical({
                'metadata': {'instrument': 'gpt', 'track': 'assisted_extraction'}, 'requests': []}))
            (path / 'ledger.json').write_bytes(c.panel.safe.canonical({'attempts': []}))
            with patch.object(c.validation, 'require_verified_release'), patch.object(c.validation, 'replay', return_value=([], {})):
                with self.assertRaisesRegex(ValueError, 'complete_assisted_case_inventory_required'):
                    c.extraction_records([case], [(path / 'manifest.json', path)], {})

    def test_billing_replay_error_remains_fatal(self):
        case, _, _ = fixture()
        with patch.object(c.validation, 'require_verified_release'), patch.object(c, 'read', return_value={
                'metadata': {'instrument': 'gpt', 'track': 'assisted_extraction'}}), patch.object(c.validation, 'replay',
                side_effect=c.integrity.BillingIntegrityError('synthetic billing mismatch')):
            with self.assertRaises(c.integrity.BillingIntegrityError):
                c.extraction_records([case], [('manifest', 'run')], {})


if __name__ == '__main__':
    unittest.main()
