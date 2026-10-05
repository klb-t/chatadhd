"""Fake 192-slot public receipts verify honest population and graph exports."""
from copy import deepcopy
import gzip
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.structure import jev_validation_results_v1 as tool


def fixture(changes=None, missing=()):
    changes = changes or {}
    fields = ['state', 'http_status', 'manifest_sha256', 'request_sha256', 'response_sha256',
              'generation_id', 'generation_sha256', 'requested_model', 'requested_provider',
              'response_model', 'response_provider', 'observed_model', 'observed_provider',
              'response_available', 'response_ledger_bound', 'exact_sent_request_capture_verified',
              'actual_cost_usd', 'billing_verified', 'billing_replay_verified', 'is_byok']
    operations = []; mappings = []; requests = []; rows = []; responses = []; records = []; groups = []
    for arm in ('j_active', 'j_directed'):
        for index in range(96):
            query = 'q' + str(index); ident = arm + '.' + query
            body = {'model': 'fake-model', 'provider': {'only': ['fake-provider']}, 'state': {'text': 'fictional ' + query},
                    'questions': {'q01': {'type': 'noul', 'instructions': arm + ':support'},
                                  'q02': {'type': 'noul', 'instructions': arm + ':refute'}}}
            request_hash = tool.results.sha(tool.wire.canonical(body))
            mapping = {'operation_id': ident, 'arm_id': arm, 'query_id': query, 'family_id': 'family' + str(index // 4),
                       'language': 'pl' if index < 48 else 'en', 'body_sha256': request_hash}
            mappings.append(mapping)
            meta = {'arm_id': arm, 'prepared_request_id': query, 'source_manifest_sha256': 'c' * 64}
            operations.append({'operation_id': ident, 'route_id': 'jev', 'request_file': 'not-read.json',
                               'model_id': 'fake-model', 'provider_id': 'fake-provider', 'request_sha256': request_hash,
                               'metadata': meta})
            requests.append({'operation_id': ident, 'route_id': 'jev', 'model_id': 'fake-model', 'provider_id': 'fake-provider',
                             'request_sha256': request_hash, 'metadata': meta, 'body': body})
            prediction_label = changes.get((arm, index), 'supported')
            prediction = {'query_id': query, 'state': 'completed', 'label': prediction_label}
            probabilities = {'q01': .9, 'q02': .9 if prediction_label == 'conflicting' else .1}
            if prediction_label == 'refuted': probabilities = {'q01': .1, 'q02': .9}
            projection = {'id': 'generation.' + ident, 'model': 'fake-observed-model',
                          'usage': {'cost': '0.001', 'is_byok': False},
                          'answers': {key: {'type': 'noul', 'noul': value} for key, value in probabilities.items()}}
            generation = {'id': projection['id'], 'model': 'fake-observed-model', 'provider_name': 'fake-observed-provider',
                          'is_byok': False, 'total_cost': '0.001'}
            row = {'operation_id': ident, 'state': 'completed', 'completed': True, 'http_status': 200,
                   'request_sha256': request_hash, 'metadata': meta, 'generation_id': projection['id'],
                   'response_sha256': tool.results.sha(tool.wire.canonical(projection)),
                   'generation_sha256': tool.results.sha(tool.wire.canonical(generation)),
                   'requested_model': 'fake-model', 'requested_provider': 'fake-provider', 'response_model': 'fake-observed-model',
                   'response_provider': 'fake-observed-provider', 'observed_model': 'fake-observed-model',
                   'observed_provider': 'fake-observed-provider', 'response_available': True, 'response_ledger_bound': True,
                   'exact_sent_request_capture_verified': True, 'actual_cost_usd': '0.001', 'billing_verified': True,
                   'billing_replay_verified': True, 'is_byok': False, 'input_tokens': 10, 'output_tokens': 0,
                   'latency_seconds': .1}
            absent = (arm, index) in missing
            if not absent:
                rows.append(row)
                responses.append({'operation_id': ident, 'response_sha256': row['response_sha256'],
                                  'generation_sha256': row['generation_sha256'], 'projection': projection,
                                  'generation_projection': generation})
            records.append({**mapping, 'prediction': {'query_id': query, 'state': 'unavailable'} if absent else prediction,
                            'label_error': 'missing_first_row' if absent else
                                'conflicting_first_source_label_answer' if prediction_label == 'conflicting' else None,
                            'source_first_row_present': not absent, 'source_first_capture_present': not absent,
                            'response_probabilities': None if absent else probabilities,
                            'actual_cost_usd': None if absent else '0.001', 'first_row_projection': None})
    manifest = {'schema': 'loom.research_programme_manifest/1', 'programme_id': 'fake-programme',
                'stage_id': 'fake-stage', 'operations': operations}
    manifest_raw = tool.wire.canonical(manifest); manifest_hash = tool.results.sha(manifest_raw)
    for row in rows: row['manifest_sha256'] = manifest_hash
    row_index = {r['operation_id']: r for r in rows}
    for record in records:
        row = row_index.get(record['operation_id'])
        record['first_row_projection'] = None if row is None else {k: row.get(k) for k in fields}
    bundle = {'schema': 'loom.programme_results/1', 'programme_id': 'fake-programme', 'stage_id': 'fake-stage',
              'manifest_sha256': manifest_hash, 'planned_operation_ids': [r['operation_id'] for r in operations],
              'planned_operations': 192, 'saved_row_count': len(rows), 'requests': requests, 'rows': rows, 'responses': responses}
    config = {'output_schema': 'loom.first_source_label_score/1', 'binding_status': 'bound',
              'paid_manifest': {'raw_sha256': manifest_hash, 'identity_fields': ['programme_id', 'stage_id'],
                                'programme_id': 'fake-programme', 'stage_id': 'fake-stage'},
              'gold': {'raw_sha256': 'a' * 64}, 'source_inputs': {'raw_sha256': 'b' * 64},
              'instrument': {'fake_only': True}, 'population': {'id': 'fake-new-data', 'blind_sealed_holdout': False},
              'operation_mapping': mappings, 'arms': [{'arm_id': arm, 'authored_planned_queries': 96} for arm in ('j_active', 'j_directed')],
              'manifest_metadata_bindings': {'arm_id': '/metadata/arm_id'},
              'normalized_request_identity_fields': ['route_id', 'model_id', 'provider_id'], 'retained_first_row_fields': fields,
              'response_policy': {'label_response_states': ['completed'], 'successful_http_status': 200,
                                  'label_required_flags': {'response_available': True, 'response_ledger_bound': True,
                                                           'exact_sent_request_capture_verified': True}, 'answer_type': 'noul'}}
    for arm in ('j_active', 'j_directed'):
        selected = [r for r in records if r['arm_id'] == arm]
        matrix = {gold: {pred: 0 for pred in (*tool.LABELS, 'unavailable')} for gold in tool.LABELS}; failures = []
        for r in selected:
            prediction = r['prediction'].get('label', 'unavailable')
            if prediction == 'conflicting': prediction = 'unavailable'
            matrix['supported'][prediction] += 1
            if prediction != 'supported': failures.append({'query_id': r['query_id'], 'gold': 'supported', 'predicted': prediction})
        groups.append({'dimensions': {'arm_id': arm}, 'quality': {'availability': {'query_count': 96}},
                       'confusion': matrix, 'failures': failures})
    bundle_raw = tool.wire.canonical(bundle); config_raw = tool.wire.canonical(config)
    score = {'schema': config['output_schema'], 'programme_id': 'fake-programme', 'stage_id': 'fake-stage',
             'input_sha256': {'config': tool.results.sha(config_raw), 'paid_manifest': manifest_hash,
                              'normalized_bundle': tool.results.sha(bundle_raw), 'gold_raw_container': 'a' * 64, 'source_inputs': 'b' * 64},
             'gold_selection': config['gold'], 'instrument': config['instrument'], 'population': config['population'],
             'first_response_only': True, 'replacement_responses_used': False, 'holdout_evaluation': False,
             'planned_operations': 192, 'received_first_rows': len(rows), 'records': records, 'groups': groups,
             'evidence_boundary': 'Fictional source-commitment controls only.'}
    score_raw = tool.wire.canonical(score)
    raws = (score_raw, bundle_raw, manifest_raw, config_raw)
    bindings = {'observed_on': '2026-10-05', 'known_family_history': {
        'scope': 'known authored families; two scientific repetitions; separate from new data',
        'j_active': {'correct': 80, 'planned_queries': 96},
        'j_directed': {'correct': 89, 'planned_queries': 96}}, **{key + '_sha256': tool.results.sha(raw) for key, raw in
               zip(('score', 'bundle', 'manifest', 'config'), raws)}}
    return raws, bindings


class JevValidation(unittest.TestCase):
    def test_bound_score_and_bundle_cannot_be_substituted(self):
        raws, bindings = fixture()
        for index, reason in ((0, 'score_hash_changed'), (1, 'bundle_hash_changed')):
            changed = list(raws); changed[index] += b' '
            with self.assertRaisesRegex(tool.ValidationResultsError, reason):
                tool.summarize(*changed, **bindings)
        score = tool.wire.parse_json(raws[0]); score['input_sha256']['normalized_bundle'] = 'f' * 64
        changed = tool.wire.canonical(score); bindings['score_sha256'] = tool.results.sha(changed)
        with self.assertRaisesRegex(tool.ValidationResultsError, 'score_input_binding_changed'):
            tool.summarize(changed, *raws[1:], **bindings)

    def test_missing_first_slots_keep_96_and_48_and_unknown_actual_cost(self):
        raws, bindings = fixture(missing=(('j_active', 0),))
        summary, _ = tool.summarize(*raws, **bindings)
        report = summary['reports']['j_active']
        self.assertEqual((report['correct'], report['planned_queries'], report['available']), (95, 96, 95))
        self.assertEqual(report['by_language']['pl']['planned_queries'], 48)
        self.assertEqual(report['by_language']['pl']['correct'], 47)
        self.assertEqual(report['planned_families'], 24)
        self.assertIsNone(report['actual_total_cost_usd'])
        self.assertEqual(report['known_verified_cost_usd'], '0.095')
        self.assertFalse(summary['known_family_history']['used_as_new_data'])

    def test_valid_external_hashes_do_not_allow_changed_request_or_denominator(self):
        raws, bindings = fixture()
        bundle = tool.wire.parse_json(raws[1]); score = tool.wire.parse_json(raws[0])
        bundle['requests'][0]['body']['state']['text'] = 'changed frozen source'
        bundle_raw = tool.wire.canonical(bundle); bindings['bundle_sha256'] = tool.results.sha(bundle_raw)
        score['input_sha256']['normalized_bundle'] = bindings['bundle_sha256']
        score_raw = tool.wire.canonical(score); bindings['score_sha256'] = tool.results.sha(score_raw)
        with self.assertRaisesRegex(tool.ValidationResultsError, 'public_request_binding_changed'):
            tool.summarize(score_raw, bundle_raw, *raws[2:], **bindings)
        raws, bindings = fixture(); score = tool.wire.parse_json(raws[0])
        score['groups'][0]['quality']['availability']['query_count'] = 95
        score_raw = tool.wire.canonical(score); bindings['score_sha256'] = tool.results.sha(score_raw)
        with self.assertRaisesRegex(tool.ValidationResultsError, 'scorer_arm_denominator_changed'):
            tool.summarize(score_raw, *raws[1:], **bindings)

    def test_generation_cost_change_cannot_become_actual_resource_claim(self):
        raws, bindings = fixture(); bundle = tool.wire.parse_json(raws[1]); score = tool.wire.parse_json(raws[0])
        bundle['responses'][0]['generation_projection']['total_cost'] = '0.002'
        bundle_raw = tool.wire.canonical(bundle); bindings['bundle_sha256'] = tool.results.sha(bundle_raw)
        score['input_sha256']['normalized_bundle'] = bindings['bundle_sha256']
        score_raw = tool.wire.canonical(score); bindings['score_sha256'] = tool.results.sha(score_raw)
        with self.assertRaisesRegex(tool.ValidationResultsError, 'score_verified_cost_changed'):
            tool.summarize(score_raw, bundle_raw, *raws[2:], **bindings)

    def test_conflict_is_protocol_valid_but_unavailable_and_pairs_retained(self):
        raws, bindings = fixture(changes={('j_active', 0): 'refuted', ('j_directed', 1): 'refuted', ('j_active', 2): 'conflicting'})
        summary, _ = tool.summarize(*raws, **bindings)
        active = summary['reports']['j_active']
        self.assertEqual((active['protocol_valid'], active['available'], active['correct']), (96, 95, 94))
        paired = summary['paired_changes'][0]
        self.assertEqual(paired['planned_pairs'], 96)
        self.assertEqual(paired['counts'], {'disagreement': 3, 'correction': 1, 'regression': 1,
                                           'availability_gained': 1, 'availability_lost': 0})
        negative = next(r for r in summary['records'] if r['error_class'] == 'conflicting_label')
        self.assertEqual(negative['response_probabilities'], {'q01': .9, 'q02': .9})
        self.assertIsNotNone(negative['public_evidence']['first_response'])

    def test_graph_has_actual_producers_version_parameters_and_dated_96_metrics(self):
        raws, bindings = fixture(missing=(('j_active', 0),))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(tool.results.graph, 'ROOT', root):
                receipt, summary = tool.export(*raws, root / 'public', **bindings)
            packet = json.loads(gzip.decompress((root / 'public/packet.json.gz').read_bytes()))
            tool.results.graph.codec.validate_packet(packet)
            self.assertEqual(receipt['planned_operations'], 192)
            self.assertEqual((root / 'public/FROZEN_SCORE_INPUT.json').read_bytes(), raws[0])
            self.assertTrue(any(c['predicate'] == 'produced_by' for c in packet['claims']))
            protocol = [c for c in packet['claims'] if c['predicate'] == 'method_evaluation'
                        and c['qualifiers']['extra']['metric_id'] == 'protocol_validity']
            self.assertTrue(all(c['qualifiers']['extra']['measurement_axis'] == 'mechanism' for c in protocol))
            self.assertEqual(receipt['output_sha256']['VALIDATION.json'],
                             tool.results.sha((root / 'public/VALIDATION.json').read_bytes()))
            metrics = [c['value'] for c in packet['claims'] if c['predicate'] == 'method_evaluation'
                       and c['qualifiers']['extra']['metric_id'] == 'accuracy_all_queries']
            self.assertEqual(sorted((m['numerator'], m['denominator']) for m in metrics), [(95, 96), (96, 96)])
            methods = json.loads((root / 'public/METHODS.json').read_bytes())
            self.assertTrue(all(m['prompt_sha256'] and m['parameters'] and m['version'] for m in methods))
            self.assertFalse(any(e['kind'] == 'model_profile' for e in packet['entities']))

    def test_unattempted_population_emits_no_measured_model_quality(self):
        raws, bindings = fixture(missing=tuple((arm, i) for arm in ('j_active', 'j_directed') for i in range(96)))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(tool.results.graph, 'ROOT', root):
                _, summary = tool.export(*raws, root / 'public', **bindings)
            self.assertFalse(any(r['first_attempt_outcomes_measured'] for r in summary['reports'].values()))
            for report in summary['reports'].values():
                self.assertIsNone(report['accuracy_all_planned'])
                self.assertEqual((report['correct'], report['planned_queries'], report['coverage']), (0, 96, 0))
            for group in (root / 'public').glob('group-*.json'):
                report = json.loads(group.read_bytes())
                self.assertFalse(report['model_quality_measured'])
                self.assertIsNone(report['metrics']['accuracy_all_queries']['value'])
                self.assertEqual(report['metrics']['accuracy_all_queries']['denominator'], 96)

    def test_attempted_missing_capture_is_zero_success_not_unexecuted_null(self):
        missing = tuple((arm, i) for arm in ('j_active', 'j_directed') for i in range(96)
                        if (arm, i) != ('j_active', 0))
        raws, bindings = fixture(missing=missing)
        bundle = tool.wire.parse_json(raws[1]); score = tool.wire.parse_json(raws[0]); config = tool.wire.parse_json(raws[3])
        bundle['responses'] = []
        row = bundle['rows'][0]; row.update(actual_cost_usd=None, billing_verified=False, billing_replay_verified=False,
                                          response_available=False, response_ledger_bound=False)
        record = score['records'][0]
        record.update(source_first_capture_present=False, response_probabilities=None, actual_cost_usd=None,
                      label_error='missing_first_capture', prediction={'query_id': 'q0', 'state': 'unavailable'},
                      first_row_projection={k: row.get(k) for k in config['retained_first_row_fields']})
        group = score['groups'][0]; group['failures'].append({'query_id': 'q0', 'gold': 'supported', 'predicted': 'unavailable'})
        group['confusion']['supported'].update(supported=0, unavailable=96)
        bundle_raw = tool.wire.canonical(bundle); bindings['bundle_sha256'] = tool.results.sha(bundle_raw)
        score['input_sha256']['normalized_bundle'] = bindings['bundle_sha256']
        score_raw = tool.wire.canonical(score); bindings['score_sha256'] = tool.results.sha(score_raw)
        summary, _ = tool.summarize(score_raw, bundle_raw, *raws[2:], **bindings)
        active, directed = summary['reports']['j_active'], summary['reports']['j_directed']
        self.assertEqual((active['accuracy_all_planned'], active['correct'], active['planned_queries']), (0, 0, 96))
        self.assertTrue(active['first_attempt_outcomes_measured'])
        self.assertFalse(active['actual_model_answer_labels_measured'])
        self.assertIsNone(directed['accuracy_all_planned'])


if __name__ == '__main__':
    unittest.main()
