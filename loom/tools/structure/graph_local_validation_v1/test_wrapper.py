"""Transport/mechanism equivalence only; validation is never opened."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import graph_local_validation as wrapper


def example():
    case = {'id': 'mechanism', 'language': 'pl', 'source_id': 'synthetic:wrapper',
            'turns': [{'id': 't1', 'speaker': 'tester', 'known_at': '2026-08-10T09:00:00Z', 'text': 'A powoduje B.'},
                      {'id': 't2', 'speaker': 'tester', 'known_at': '2026-08-10T09:01:00Z', 'text': 'A nie powoduje B.'}],
            'node_inventory': [{'id': 'A', 'text': 'A', 'aliases': []}, {'id': 'B', 'text': 'B', 'aliases': []}],
            'judgment_queries': [{'id': 'q1', 'relation': 'causes', 'source': 'A', 'target': 'B',
                                  'attributed_to': 'tester', 'as_of': '2026-08-10T09:00:00Z', 'scope': 'explicit_source'}]}
    return case


class GenericWrapperMechanismTests(unittest.TestCase):
    def prepare(self, cases, split='validation'):
        with patch.object(wrapper, 'verify_release'):
            return wrapper.prepare_cases(cases, split=split, expected_release_sha256='synthetic-release')

    def test_physical_prefix_and_null_semantics(self):
        case = example(); original = deepcopy(case); prepared = self.prepare([case]); q = prepared['queries'][0]
        self.assertEqual(case, original)
        self.assertEqual([c['turn_id'] for c in q['candidates']], ['t1'])
        self.assertEqual(q['future_excluded_turn_count'], 1)
        self.assertNotIn('nie powoduje', json.dumps(q, ensure_ascii=False))
        self.assertIsNone(q['prediction']); self.assertFalse(q['source_judgment_available'])

    def test_formal_scope_explicitly_excluded(self):
        case = example(); formal = deepcopy(case['judgment_queries'][0]); formal.update(id='formal', scope='formal_closure')
        case['judgment_queries'].append(formal); prepared = self.prepare([case])
        self.assertEqual(prepared['query_count'], 1)
        self.assertEqual(prepared['excluded_non_explicit_source_queries'], [{'case_id': 'mechanism', 'query_id': 'formal', 'scope': 'formal_closure'}])

    def test_duplicate_inventory_refused(self):
        case = example()
        with self.assertRaises(ValueError): self.prepare([case, case])
        case['judgment_queries'].append(deepcopy(case['judgment_queries'][0]))
        with self.assertRaises(ValueError): self.prepare([case])

    def test_embedding_wrong_prefix_and_inventory_refused(self):
        prepared = self.prepare([example()]); q = prepared['queries'][0]
        embedded = {'queries': [{'query_id': 'q1', 'prefix_sha256': 'wrong', 'candidates': [{'turn_id': 't1', 'score': .1}]}]}
        with self.assertRaises(ValueError): wrapper.score_prepared(prepared, embedded)
        embedded['queries'][0]['prefix_sha256'] = q['prefix_sha256']
        embedded['queries'][0]['candidates'][0]['turn_id'] = 't2'
        with self.assertRaises(ValueError): wrapper.score_prepared(prepared, embedded)

    def test_unavailable_learned_channel_cannot_veto_lexical(self):
        prepared = self.prepare([example()]); q = prepared['queries'][0]
        embedded = {'queries': [{'query_id': 'q1', 'prefix_sha256': q['prefix_sha256'], 'candidates': [{'turn_id': 't1', 'score': None}]}]}
        scores = wrapper.score_prepared(prepared, embedded)['rows'][0]['candidates'][0]['scores']
        self.assertIsNone(scores['learned_minilm_cosine'])
        self.assertEqual(scores['nongating_union'], max(scores['lexical_token_cosine'], scores['character_3_5_cosine']))

    def test_wrong_expected_release_refused(self):
        with self.assertRaises(ValueError):
            wrapper.verify_release('incorrect-release')

    def test_all_dev_prefixes_and_scores_exact_transport_equivalence(self):
        cases = wrapper.local.adapter.load_dev_inputs()
        prepared = self.prepare(cases, 'dev')
        existing = json.loads((wrapper.DEV / 'prepared_inputs.json').read_text())
        self.assertEqual(prepared['queries'], existing['queries'])
        embedding = json.loads((wrapper.DEV / 'embedding/scores.json').read_text())
        scores = wrapper.score_prepared(prepared, embedding)
        first = json.loads((wrapper.DEV / 'first_scores.json').read_text())
        self.assertEqual(scores['rows'], first['rows'])
        self.assertEqual(scores['query_count'], 96)


if __name__ == '__main__': unittest.main()
