"""Fabricated outputs and development-gold wiring, never measured model accuracy."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import jev_context_score as scorer
except ImportError:
    import jev_context_score as scorer


def toy():
    mapping = [{'case_id': 'c', 'message_id': 'm', 'request_id': 'c-m', 'language': 'en',
                'questions': {'q01': {'kind': 'membership', 'id': 't1'},
                              'q02': {'kind': 'membership', 'id': 't2'},
                              'q03': {'kind': 'selected_claim', 'id': 'k1'},
                              'q04': {'kind': 'selected_claim', 'id': 'k2'}}}]
    gold = {'c': {'language': 'en', 'family': 'family', 'labels': {'m': {
        'memberships': [{'topic_id': 't1'}], 'selected_claim_ids': ['k1']}}}}
    return mapping, gold


class ContextScoringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.root = Path(cls.tmp.name)
        cls.prepared = cls.root / 'prepared'
        scorer.context.prepare_offline(cls.prepared)
        cls.mapping = scorer.jev.read_json(cls.prepared / 'question_map.json')
        cls.gold = scorer.development_gold(scorer.context.FIXTURE)
        request = scorer.jev.read_json(cls.prepared / 'request.disabled.json')
        request['enabled'] = True  # Only the fabricated, no-network transport below.
        catalog = {'data': {'endpoints': [{'tag': 'typesafe', 'status': 0,
                    'name': 'TypeSafe | typesafe/jev-1.13-20260917',
                    'pricing': {'prompt': '.000000042', 'completion': '0'}}]}}
        def endpoint(method, path, body, key):
            if method != 'GET' or path != scorer.jev.ENDPOINT or key is not None:
                raise AssertionError('unexpected operation')
            return 200, scorer.context.canonical(catalog)
        cls.manifest = scorer.jev.prepare(request, scorer.jev.read_json(cls.prepared / 'inputs.json'),
                                         cls.root / 'mock-manifest', transport_fn=endpoint)

    def write_oracle(self, directory):
        directory.mkdir()
        attempts = []
        for request, audit in zip(self.manifest['requests'], self.mapping):
            label = self.gold[audit['case_id']]['labels'][audit['message_id']]
            wanted = {'membership': {m['topic_id'] for m in label['memberships']},
                      'selected_claim': set(label['selected_claim_ids'])}
            answers = {name: {'type': 'noul', 'noul': int(q['id'] in wanted[q['kind']])}
                       for name, q in audit['questions'].items()}
            raw = scorer.context.canonical({'id': 'gen-unit', 'model': scorer.jev.MODEL,
                'provider': 'TypeSafe', 'answers': answers,
                'usage': {'input_tokens': 1, 'output_tokens': 1, 'cost': 0}})
            filename = request['id'] + '.response.bin'
            (directory / filename).write_bytes(raw)
            attempts.append({'id': request['id'], 'request_hash': request['request_hash'],
                'reservation_usd': request['reservation_usd'], 'state': 'completed',
                'response_file': filename, 'response_sha256': hashlib.sha256(raw).hexdigest()})
        ledger = {'schema': 'loom.jev_ledger/1', 'manifest_hash': scorer.context.digest(self.manifest),
                  'attempts': attempts, 'reported_cost_usd': '0'}
        scorer.jev.write_new(directory / 'ledger.json', ledger)
        return ledger

    def test_development_gold_oracle_checks_wiring_not_model_accuracy(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / 'run'
            self.write_oracle(directory)
            score = scorer.score(self.manifest, directory, self.prepared)
            self.assertEqual(score['overall']['queries'], 48)
            self.assertEqual(score['overall']['joint_exact_set']['correct'], 48)
            for kind in scorer.KINDS:
                self.assertEqual(score['overall']['bits'][kind]['accuracy_all_queries'], 1)
                self.assertEqual(score['overall']['exact_sets'][kind]['correct'], 48)
            self.assertEqual(sum(v['queries'] for v in score['groups']['family'].values()), 48)
            self.assertEqual(set(score['groups']['language']), {'pl', 'en'})
            self.assertFalse(score['errors'])

    def test_fabricated_mixed_probabilities_and_selective_boundaries(self):
        mapping, gold = toy()
        score = scorer.evaluate(mapping, gold, {'c-m': {'valid': True, 'status': 'completed',
            'probabilities': {'q01': .8, 'q02': .5, 'q03': .2, 'q04': .19}}})
        topics = score['overall']['bits']['membership']
        claims = score['overall']['bits']['selected_claim']
        self.assertEqual(topics['all_query_counts']['tp'], 1)
        self.assertEqual(topics['all_query_counts']['fp'], 1)
        self.assertEqual(topics['selective']['retained'], 1)
        self.assertEqual(claims['all_query_counts']['fn'], 1)
        self.assertEqual(claims['all_query_counts']['tn'], 1)
        self.assertEqual(claims['selective']['errors'], 1)
        self.assertEqual({e['error'] for e in score['errors']}, {'false_positive', 'false_negative'})
        self.assertEqual(score['queries'][0]['sets']['membership']['extra_ids'], ['t2'])
        self.assertEqual(score['queries'][0]['sets']['selected_claim']['missing_ids'], ['k1'])

    def test_missing_is_fn_for_positive_never_valid_negative(self):
        mapping, gold = toy()
        score = scorer.evaluate(mapping, gold, {})
        topics = score['overall']['bits']['membership']
        self.assertEqual(topics['all_query_counts']['fn'], 1)
        self.assertEqual(topics['all_query_counts']['tn'], 0)
        self.assertEqual(topics['all_query_counts']['unavailable_negative'], 1)
        self.assertIsNone(topics['valid_output_counts']['recall'])
        self.assertEqual(topics['accuracy_all_queries'], 0)
        self.assertIsNone(topics['accuracy_valid_outputs'])
        self.assertIsNone(score['queries'][0]['sets']['membership']['predicted_ids'])

    def test_empty_claim_sets_require_valid_output_and_subset_excludes_them(self):
        mapping, gold = toy()
        mapping[0]['questions'] = {'q01': {'kind': 'membership', 'id': 't1'}}
        gold['c']['labels']['m']['selected_claim_ids'] = []
        missing = scorer.evaluate(mapping, gold, {})
        good = scorer.evaluate(mapping, gold, {'c-m': {'valid': True, 'status': 'completed', 'probabilities': {'q01': .9}}})
        self.assertEqual(missing['overall']['exact_sets']['selected_claim']['correct'], 0)
        self.assertEqual(good['overall']['exact_sets']['selected_claim']['correct'], 1)
        self.assertEqual(good['overall']['claim_candidate_subset']['planned'], 0)
        self.assertIsNone(good['overall']['bits']['selected_claim']['accuracy_all_queries'])

    def test_response_hash_mismatch_aborts(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / 'run'
            ledger = self.write_oracle(directory)
            (directory / ledger['attempts'][0]['response_file']).write_bytes(b'{}')
            with self.assertRaisesRegex(scorer.jev.safe.RunnerError, 'hash_mismatch'):
                scorer.score(self.manifest, directory, self.prepared)

    def test_invalid_completed_response_fails_whole_query(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / 'run'
            ledger = self.write_oracle(directory)
            attempt = ledger['attempts'][0]
            raw = (directory / attempt['response_file']).read_bytes()
            value = json.loads(raw)
            value['answers']['q99'] = {'type': 'noul', 'noul': 1}
            raw = scorer.context.canonical(value)
            (directory / attempt['response_file']).write_bytes(raw)
            attempt['response_sha256'] = hashlib.sha256(raw).hexdigest()
            (directory / 'ledger.json').write_bytes(scorer.context.canonical(ledger))
            score = scorer.score(self.manifest, directory, self.prepared)
            self.assertEqual(score['overall']['valid_queries'], 47)
            self.assertEqual(score['overall']['joint_exact_set']['correct'], 47)
            self.assertEqual(score['queries'][0]['status'], 'invalid_response')

    def test_live_mapping_and_source_hash_tampering_rejected(self):
        changed = deepcopy(self.manifest)
        changed['requests'][0]['language'] = 'en' if changed['requests'][0]['language'] == 'pl' else 'pl'
        with self.assertRaisesRegex(ValueError, 'mapping_mismatch'):
            scorer.verified_bundle(self.prepared, changed)
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp)
            for name in ('offline_manifest.json', 'inputs.json', 'question_map.json', 'causal_exports.json'):
                (dest / name).write_bytes((self.prepared / name).read_bytes())
            mapping = scorer.jev.read_json(dest / 'question_map.json')
            mapping[0]['questions']['q01']['id'] = 'wrong_topic'
            (dest / 'question_map.json').write_bytes(scorer.context.canonical(mapping))
            with self.assertRaisesRegex(ValueError, 'artifact_hash_mismatch'):
                scorer.verified_bundle(dest, self.manifest)
            offline = scorer.jev.read_json(dest / 'offline_manifest.json')
            offline['question_map_sha256'] = scorer.context.digest(mapping)
            (dest / 'offline_manifest.json').write_bytes(scorer.context.canonical(offline))
            with self.assertRaisesRegex(ValueError, 'projection_or_map_mismatch'):
                scorer.verified_bundle(dest, self.manifest)

    def test_validation_label_bodies_not_parsed(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp)
            (dest / 'split.json').write_text(json.dumps({'cases': [
                {'case_id': 'dev', 'split': 'development', 'family': 'f', 'language': 'en'},
                {'case_id': 'val', 'split': 'validation', 'family': 'v', 'language': 'en'}]}))
            (dest / 'gold.jsonl').write_bytes(
                b'{"case_id":"val", invalid unread validation label body\n'
                b'{"case_id":"dev","labels":[]}\n')
            self.assertEqual(set(scorer.development_gold(dest)), {'dev'})

    def test_gold_file_tampering_aborts_before_parsing_labels(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Path(tmp)
            for name in ('inputs.jsonl', 'split.json', 'protocol.json', 'materialize.py', 'gold.jsonl'):
                (fixture / name).write_bytes((scorer.context.FIXTURE / name).read_bytes())
            with (fixture / 'gold.jsonl').open('ab') as handle:
                handle.write(b'\n')
            with patch.object(scorer, 'development_gold', side_effect=AssertionError('labels parsed before hash gate')):
                with self.assertRaisesRegex(ValueError, 'gold_fixture_changed_before_label_read'):
                    scorer.score(self.manifest, fixture / 'unused-run', self.prepared, fixture)

    def test_foreign_gold_and_bool_probabilities_rejected(self):
        mapping, gold = toy()
        gold['c']['labels']['m']['selected_claim_ids'].append('k_future')
        with self.assertRaisesRegex(ValueError, 'outside_candidate'):
            scorer.evaluate(mapping, gold, {})
        mapping, gold = toy()
        with self.assertRaisesRegex(ValueError, 'probabilities'):
            scorer.evaluate(mapping, gold, {'c-m': {'valid': True, 'status': 'completed',
                'probabilities': {q: True for q in mapping[0]['questions']}}})

    def test_all_missing_keeps_the_frozen_development_denominators(self):
        score = scorer.evaluate(self.mapping, self.gold, {})['overall']
        self.assertEqual(score['queries'], 48)
        self.assertEqual(score['valid_queries'], 0)
        self.assertEqual(score['joint_exact_set']['correct'], 0)
        for kind, planned in (('membership', 84), ('selected_claim', 44)):
            bits = score['bits'][kind]
            self.assertEqual(bits['planned'], planned)
            self.assertEqual(bits['missing'], planned)
            self.assertEqual(bits['accuracy_all_queries'], 0)
            self.assertIsNone(bits['accuracy_valid_outputs'])
            counts = bits['all_query_counts']
            self.assertEqual(counts['fn'], counts['unavailable_positive'])
            self.assertEqual(counts['unavailable_positive'] + counts['unavailable_negative'], planned)
            self.assertEqual(counts['tn'], 0)

    def test_question_names_are_local_positions_not_semantic_labels(self):
        mapping, gold = toy()
        probabilities = {'q01': .9, 'q02': .1, 'q03': .9, 'q04': .1}
        original = scorer.evaluate(mapping, gold, {'c-m': {'valid': True, 'status': 'completed',
            'probabilities': probabilities}})
        questions = mapping[0]['questions']
        questions['q01'], questions['q02'] = questions['q02'], questions['q01']
        probabilities['q01'], probabilities['q02'] = probabilities['q02'], probabilities['q01']
        swapped = scorer.evaluate(mapping, gold, {'c-m': {'valid': True, 'status': 'completed',
            'probabilities': probabilities}})
        self.assertEqual(original['overall'], swapped['overall'])
        self.assertEqual(swapped['rows'][0]['id'], 't2')
        self.assertEqual(swapped['rows'][0]['label'], 0)

    def test_no_positive_opportunities_do_not_become_perfect_precision_recall(self):
        mapping, gold = toy()
        gold['c']['labels']['m'] = {'memberships': [], 'selected_claim_ids': []}
        score = scorer.evaluate(mapping, gold, {'c-m': {'valid': True, 'status': 'completed',
            'probabilities': {name: .1 for name in mapping[0]['questions']}}})['overall']
        self.assertEqual(score['joint_exact_set']['correct'], 1)
        for kind in scorer.KINDS:
            counts = score['bits'][kind]['all_query_counts']
            self.assertEqual(counts['tn'], 2)
            for metric in ('precision', 'recall', 'f1'):
                self.assertIsNone(counts[metric])


if __name__ == '__main__':
    unittest.main()
