"""Fictional canaries verify aggregate-only release, including embedded sources."""
from copy import deepcopy
import gzip
import json
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure import jev_public_projection_v1 as tool


CANARIES = ('PRIVATE_SOURCE_QUOTE_ĄĘ_CANARY', 'PRIVATE_SOURCE_FILENAME_CANARY',
            'PRIVATE_PERSON_NODE_QUERY_CANARY', 'PRIVATE_GOLD_CANARY', 'PRIVATE_RAW_RESPONSE_CANARY')


def fixture():
    def cohort(queries, families):
        return {'planned_queries': queries, 'planned_families': families, 'correct': queries, 'available': queries,
                'unavailable': 0, 'protocol_valid': queries, 'accuracy_all_planned': 1.0,
                'first_attempt_outcomes_measured': True, 'error_classes': {'correct': queries},
                'actual_model_answer_labels_measured': True,
                'known_verified_cost_usd': '0.004' if queries == 4 else '0.002',
                'unknown_cost_operations': 0, 'actual_total_cost_usd': '0.004' if queries == 4 else '0.002',
                'private_source_filename': CANARIES[1], 'gold': CANARIES[3],
                'nested_private_fields': {'quotes': CANARIES[0], 'person': CANARIES[2]}}
    reports = {}
    for arm in tool.ARMS:
        reports[arm] = cohort(4, 2)
        reports[arm]['by_language'] = {language: cohort(2, 1) for language in tool.LANGUAGES}
        reports[arm]['confusion'] = {gold: {label: 4 if gold == label == 'supported' else 0
                                          for label in (*tool.LABELS, 'unavailable')} for gold in tool.LABELS}
    private = {'schema': 'loom.jev_validation_results/1', 'observed_on': '2026-10-05', 'reports': reports,
               'paired_changes': [{'baseline': 'j_active', 'candidate': 'j_directed', 'planned_pairs': 4,
                                   'counts': {'disagreement': 0, 'correction': 0, 'regression': 0,
                                              'availability_gained': 0, 'availability_lost': 0},
                                   'pairs': [{'query_id': CANARIES[2], 'gold': CANARIES[3]}]}],
               'records': [{'source_quote': CANARIES[0], 'response': CANARIES[4]}],
               'population': {'source_path': CANARIES[1]}, 'known_family_history': {'arbitrary': CANARIES[3]},
               'methods': {'unrestricted_source_prompt': CANARIES[0]}}
    contract = {'schema': 'loom.jev_publication_contract/1', 'binding_status': 'bound',
                'private_validation_sha256': tool.results.sha(tool.wire.canonical(private)), 'cohort_release_sha256': 'a' * 64,
                'public_release_id': 'jev-real-20261005-7c000001', 'observed_on': '2026-10-05',
                'aggregates': list(tool.AGGREGATES), 'languages': list(tool.LANGUAGES),
                'population': {'planned_queries_per_arm': 4, 'planned_families': 2,
                               'language_queries': {'pl': 2, 'en': 2}, 'language_families': {'pl': 1, 'en': 1}},
                'methods': {arm: {'requested_model_id': 'typesafe/jev-1.13', 'prompt_sha256': 'b' * 64,
                                 'version_sha256': ('c' if arm == 'j_active' else 'd') * 64,
                                 'sampling_parameters': {'temperature': 0.0}} for arm in tool.ARMS}}
    private['publication_method_bindings'] = deepcopy(contract['methods'])
    private['cohort_release_sha256'] = contract['cohort_release_sha256']
    return private, contract


def bound(private, contract):
    validation_raw = tool.wire.canonical(private)
    contract['private_validation_sha256'] = tool.results.sha(validation_raw)
    contract_raw = tool.wire.canonical(contract)
    return validation_raw, contract_raw, {'contract_sha256': tool.results.sha(contract_raw)}


class PublicProjection(unittest.TestCase):
    def test_nested_canaries_never_reach_any_public_file_or_embedded_packet_source(self):
        private, contract = fixture(); raw, policy, bindings = bound(private, contract)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / CANARIES[1]
            public, packet, receipt = tool.export(raw, policy, output, **bindings)
            tool.graph.codec.validate_packet(packet)
            for artifact in output.iterdir():
                content = gzip.decompress(artifact.read_bytes()) if artifact.suffix == '.gz' else artifact.read_bytes()
                decoded = content.decode('utf-8')
                for canary in CANARIES:
                    self.assertNotIn(canary, decoded, artifact.name)
            self.assertEqual(public['reports']['j_active']['metrics']['accuracy_all_planned']['denominator'], 4)
            self.assertNotIn('records', public); self.assertNotIn('pairs', public['paired_aggregate'])
            self.assertTrue(receipt['private_validation_input_read'])
            self.assertFalse(receipt['full_request_response_replay_read'])
            self.assertFalse(receipt['private_material_published'])
            self.assertTrue(all(source['observation']['attrs']['path'] in ('METHODS.json', 'PUBLIC_VALIDATION.json')
                                for source in packet['sources']))
            self.assertTrue(any(claim['predicate'] == 'produced_by' for claim in packet['claims']))
            methods = json.loads((output / 'METHODS.json').read_bytes())
            self.assertTrue(all(m['recipe_material']['private_prompt_text_withheld'] is True for m in methods))

    def test_contract_and_private_input_hashes_are_mandatory(self):
        private, contract = fixture(); raw, policy, bindings = bound(private, contract)
        for changed_raw, changed_policy, expected in ((raw + b' ', policy, 'private_validation_hash_changed'),
                                                       (raw, policy + b' ', 'publication_contract_hash_changed')):
            with self.assertRaisesRegex(tool.ProjectionError, expected):
                tool.project(changed_raw, changed_policy, **bindings)

    def test_pending_7a_contract_never_emits_metrics_or_files(self):
        private, contract = fixture(); contract['binding_status'] = 'pending_private_validation'
        raw, policy, bindings = bound(private, contract)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'public'
            with self.assertRaisesRegex(tool.ProjectionError, 'publication_contract_not_bound'):
                tool.export(raw, policy, output, **bindings)
            self.assertFalse(output.exists())

    def test_contract_cannot_relabel_frozen_private_method_or_cohort(self):
        for mutation in (lambda c: c['methods']['j_active'].update(version_sha256='f' * 64),
                         lambda c: c['methods']['j_directed'].update(requested_model_id='other/model'),
                         lambda c: c['methods']['j_active']['sampling_parameters'].update(temperature=0),
                         lambda c: c.update(cohort_release_sha256='e' * 64)):
            private, contract = fixture(); mutation(contract); raw, policy, bindings = bound(private, contract)
            with self.assertRaisesRegex(tool.ProjectionError, 'private_method_or_cohort_binding_changed'):
                tool.project(raw, policy, **bindings)

    def test_invalid_contract_date_never_echoes_private_text(self):
        private, contract = fixture(); contract['observed_on'] = CANARIES[0]
        raw, policy, bindings = bound(private, contract)
        with self.assertRaisesRegex(tool.ProjectionError, 'publication_date_invalid') as caught:
            tool.project(raw, policy, **bindings)
        self.assertNotIn(CANARIES[0], str(caught.exception))

    def test_arbitrary_contract_strings_or_nested_sampling_values_rejected(self):
        for mutation in (lambda c: c.update(private_source_path=CANARIES[1]),
                         lambda c: c['methods']['j_active']['sampling_parameters'].update(secret=CANARIES[0]),
                         lambda c: c['methods']['j_active'].update(requested_model_id=CANARIES[2]),
                         lambda c: c['methods']['j_active']['sampling_parameters'].update(temperature={'text': CANARIES[0]})):
            private, contract = fixture(); mutation(contract); raw, policy, bindings = bound(private, contract)
            with self.assertRaises(tool.ProjectionError) as caught:
                tool.project(raw, policy, **bindings)
            for canary in CANARIES: self.assertNotIn(canary, str(caught.exception))

    def test_error_labels_are_enumerated_and_denominators_cannot_shrink(self):
        for mutation in (lambda p: p['reports']['j_active']['error_classes'].update({CANARIES[0]: 0}),
                         lambda p: p['reports']['j_active'].update(planned_queries=3),
                         lambda p: p['reports']['j_active'].update(available=3),
                         lambda p: p['reports']['j_active']['confusion']['supported'].update(supported=3)):
            private, contract = fixture(); mutation(private); raw, policy, bindings = bound(private, contract)
            with self.assertRaises(tool.ProjectionError): tool.project(raw, policy, **bindings)

    def test_unknown_cost_stays_null_and_selected_aggregates_only(self):
        private, contract = fixture(); source = private['reports']['j_active']
        source.update(unknown_cost_operations=1, actual_total_cost_usd=None)
        source['by_language']['pl'].update(unknown_cost_operations=1, actual_total_cost_usd=None)
        contract['aggregates'] = ['cost', 'availability']; contract['languages'] = ['en']
        raw, policy, bindings = bound(private, contract); public = tool.project(raw, policy, **bindings)
        report = public['reports']['j_active']
        self.assertIsNone(report['cost']['actual_total_usd']); self.assertFalse(report['cost']['complete'])
        self.assertEqual(set(report['metrics']), {'label_availability'})
        self.assertNotIn('error_counts', report); self.assertNotIn('confusion_counts', report)
        self.assertEqual(set(report['by_language']), {'en'}); self.assertIsNone(public['paired_aggregate'])

    def test_unmeasured_errors_language_and_paired_statistics_must_reconcile(self):
        def wrong_language(private):
            source = private['reports']['j_active']['by_language']['pl']
            source.update(correct=1, accuracy_all_planned=.5, error_classes={'correct': 1, 'source_label_mismatch': 1})
        def wrong_pair(private):
            private['paired_changes'][0]['counts'].update(disagreement=1, availability_gained=1)
        for mutation in (lambda p: p['reports']['j_active'].update(first_attempt_outcomes_measured=False, accuracy_all_planned=None),
                         lambda p: p['reports']['j_active'].update(error_classes={'correct': 3, 'source_label_mismatch': 1}),
                         wrong_language, wrong_pair):
            private, contract = fixture(); mutation(private); raw, policy, bindings = bound(private, contract)
            with self.assertRaises(tool.ProjectionError): tool.project(raw, policy, **bindings)

    def test_wire_protocol_can_be_valid_without_scorer_label_measurement(self):
        private, contract = fixture()
        for source in private['reports'].values():
            for cohort in (source, *source['by_language'].values()):
                queries = cohort['planned_queries']
                cohort.update(correct=0, available=0, unavailable=queries, accuracy_all_planned=0.0,
                              actual_model_answer_labels_measured=False, error_classes={'protocol_invalid': queries},
                              known_verified_cost_usd='0', unknown_cost_operations=queries, actual_total_cost_usd=None)
            source['confusion']['supported'].update(supported=0, unavailable=source['planned_queries'])
        raw, policy, bindings = bound(private, contract)
        with tempfile.TemporaryDirectory() as directory:
            public, packet, _ = tool.export(raw, policy, Path(directory) / 'public', **bindings)
            report = public['reports']['j_active']
            self.assertEqual(report['metrics']['protocol_validity']['value'], 1)
            self.assertEqual(report['metrics']['accuracy_all_planned']['value'], 0)
            self.assertFalse(report['model_answer_labels_measured'])
            evaluations = [claim for claim in packet['claims'] if claim['predicate'] == 'method_evaluation']
            self.assertTrue(all(c['qualifiers']['extra']['measurement_kind'] != 'historical_actual_model_response_replay' for c in evaluations))

    def test_legitimate_entirely_unattempted_population_keeps_null_accuracy_and_private_canaries(self):
        private, contract = fixture()
        for source in private['reports'].values():
            for cohort in (source, *source['by_language'].values()):
                queries = cohort['planned_queries']
                cohort.update(correct=0, available=0, unavailable=queries, protocol_valid=0, accuracy_all_planned=None,
                              first_attempt_outcomes_measured=False, actual_model_answer_labels_measured=False,
                              error_classes={'missing_first_row': queries}, known_verified_cost_usd='0',
                              unknown_cost_operations=queries, actual_total_cost_usd=None)
            source['confusion']['supported'].update(supported=0, unavailable=source['planned_queries'])
        raw, policy, bindings = bound(private, contract)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / CANARIES[1]
            public, packet, _ = tool.export(raw, policy, output, **bindings)
            for report in public['reports'].values():
                self.assertIsNone(report['metrics']['accuracy_all_planned']['value'])
                self.assertEqual(report['metrics']['accuracy_all_planned']['denominator'], 4)
                self.assertEqual(report['metrics']['label_availability']['numerator'], 0)
                self.assertIsNone(report['cost']['actual_total_usd'])
                for language in report['by_language'].values():
                    self.assertIsNone(language['metrics']['accuracy_all_planned']['value'])
                    self.assertEqual(language['metrics']['accuracy_all_planned']['denominator'], 2)
            for artifact in output.iterdir():
                content = gzip.decompress(artifact.read_bytes()) if artifact.suffix == '.gz' else artifact.read_bytes()
                for canary in CANARIES: self.assertNotIn(canary, content.decode('utf-8'))
            evaluations = [claim for claim in packet['claims'] if claim['predicate'] == 'method_evaluation']
            self.assertTrue(all(c['qualifiers']['extra']['measurement_kind'] == 'private_unattempted_population_aggregate_attestation'
                                for c in evaluations))

    def test_unattempted_population_cannot_claim_captures_or_protocol_failures(self):
        private, contract = fixture()
        for source in private['reports'].values():
            for cohort in (source, *source['by_language'].values()):
                queries = cohort['planned_queries']
                cohort.update(correct=0, available=0, unavailable=queries, protocol_valid=0, accuracy_all_planned=None,
                              first_attempt_outcomes_measured=False, actual_model_answer_labels_measured=False,
                              error_classes={'missing_first_capture': queries}, known_verified_cost_usd='0',
                              unknown_cost_operations=queries, actual_total_cost_usd=None)
            source['confusion']['supported'].update(supported=0, unavailable=source['planned_queries'])
        raw, policy, bindings = bound(private, contract)
        with self.assertRaisesRegex(tool.ProjectionError, 'aggregate_error_classes_inconsistent'):
            tool.project(raw, policy, **bindings)


if __name__ == '__main__':
    unittest.main()
