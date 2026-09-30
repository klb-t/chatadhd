import copy
import math
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import explore
import evaluate


def row(language='en'):
    return {'query_id': 'q', 'language': language, 'query_text': 'actor: speaker\nsource: NOT A\ntarget: B',
        'query': {'source': 'a', 'target': 'b', 'relation': 'implies', 'attributed_to': 'speaker'},
        'prefix_payload': {'node_inventory': [{'id': 'a', 'text': 'NOT A', 'aliases': ['no alpha']},
                                             {'id': 'b', 'text': 'B', 'aliases': []}]}}


class BM25Tests(unittest.TestCase):
    def test_single_term_hand_calculation(self):
        actual = explore.bm25('rare', ['rare rare', 'other'])
        expected = math.log(1 + 1.5 / 1.5) * 2 * 2.2 / (2 + 1.2 * (.25 + .75 * 2 / 1.5))
        self.assertAlmostEqual(actual[0], expected)
        self.assertEqual(actual[1], 0)

    def test_prefix_statistics_are_local_and_repeatable(self):
        before = explore.bm25('rare', ['rare', 'other'])
        unrelated = explore.bm25('rare', ['rare'] * 20)
        after = explore.bm25('rare', ['rare', 'other'])
        self.assertEqual(before, after)
        self.assertNotEqual(before[0], unrelated[0])

    def test_query_frequency_counts(self):
        self.assertEqual(explore.bm25('a a', ['a', 'b'])[0], 2 * explore.bm25('a', ['a', 'b'])[0])

    def test_length_normalization_control(self):
        self.assertEqual(explore.bm25('a', ['a', 'a b c'], b=0)[0], explore.bm25('a', ['a', 'a b c'], b=0)[1])
        self.assertGreater(*explore.bm25('a', ['a', 'a b c'], b=1))

    def test_empty_pool(self):
        self.assertEqual(explore.bm25('a', []), [])

    def test_no_shared_vocabulary_keeps_zero_score_available(self):
        self.assertEqual(explore.bm25('a', ['b', 'c']), [0, 0])

    def test_invalid_parameters(self):
        for kw in ({'b': -1}, {'b': 1.1}, {'k1': 0}):
            with self.assertRaises(ValueError):
                explore.bm25('a', ['a'], **kw)

    def test_unicode_and_negation_retained(self):
        self.assertEqual(explore.token_list('ŻÓŁĆ nie! NOT'), ['żółć', 'nie', 'not'])


class FusionTests(unittest.TestCase):
    def test_rrf_formula(self):
        self.assertEqual(explore.rrf(['a', 'b'], [['a', 'b'], ['b', 'a']], 60), [1 / 61 + 1 / 62] * 2)

    def test_rrf_no_channel_veto(self):
        scores = explore.rrf(['a', 'b'], [['a'], ['b']], 60)
        self.assertEqual(scores, [1 / 61, 1 / 61])

    def test_rrf_duplicate_rejected(self):
        with self.assertRaises(ValueError):
            explore.rrf(['a'], [['a', 'a']])

    def test_rrf_unknown_identity_rejected(self):
        with self.assertRaises(ValueError):
            explore.rrf(['a'], [['other']])

    def test_ties_preserve_original_order_and_zero_availability(self):
        candidates = [{'turn_id': 'z', 'eligible_order': 0}, {'turn_id': 'a', 'eligible_order': 1}]
        self.assertEqual(explore.ranked_ids(candidates, [0, 0]), ['z', 'a'])
        self.assertEqual(explore.ranked_ids(candidates, [None, 0]), ['a'])

    def test_empty_pool_rank_abstains(self):
        self.assertEqual(explore.ranked_ids([], []), [])


class RepresentationTests(unittest.TestCase):
    def test_forward_reverse_preserve_direction(self):
        views = explore.render_variants(row())
        self.assertLess(views['question'].index('NOT A'), views['question'].index('“B”'))
        self.assertGreater(views['reverse'].index('NOT A'), views['reverse'].index('“B”'))

    def test_aliases_are_supplied_not_learned(self):
        views = explore.render_variants(row())
        self.assertIn('no alpha', views['aliases'])
        self.assertNotIn('no alpha', views['forward'])

    def test_causes_relation_is_not_rendered_as_implication(self):
        q = row()
        q['query']['relation'] = 'causes'
        en = explore.render_variants(q)
        self.assertIn('causes', en['forward'])
        self.assertIn('cause', en['question'])
        self.assertNotIn('imply', en['question'])
        q['language'] = 'pl'
        self.assertIn('powoduje', explore.render_variants(q)['question'])

    def test_polish_negation_retained(self):
        views = explore.render_variants(row('pl'))
        self.assertIn('NOT A', views['denial'])
        self.assertIn('zaprzecza', views['denial'])

    def test_operator_direction_bonus_only_contiguous_forward(self):
        self.assertEqual(explore.endpoint_coverage('not a', 'b', 'not a implies b', .25), 1.25)
        self.assertEqual(explore.endpoint_coverage('not a', 'b', 'b implies not a', .25), 1)
        self.assertLess(explore.endpoint_coverage('not a', 'b', 'a implies b', .25), 1)

    def test_empty_endpoint_does_not_create_direction(self):
        self.assertEqual(explore.endpoint_coverage('', 'b', 'b', .25), 0)

    def test_case_insensitive_speaker_softness_keeps_other_sources(self):
        q = row()
        q.update({'case_id': 'c', 'prefix_sha256': 'hash', 'future_excluded_turn_count': 0,
                  'query': q['query'] | {'as_of': '2026-01-01T00:00:00Z'},
                  'candidates': [{'turn_id': 't', 'source_id': 's', 'known_at': '2026-01-01T00:00:00Z',
                    'text': 'NOT A implies B', 'text_sha256': 'x', 'span': {}, 'speaker': 'different',
                    'representation_text': 'NOT A implies B', 'eligible_order': 0}]})
        methods = ['lexical_token_cosine', 'character_3_5_cosine', 'learned_minilm_cosine', 'nongating_union']
        old = {'rows': [{'query_id': 'q', 'prefix_sha256': 'hash', 'rankings': dict.fromkeys(methods, ['t']),
                        'candidates': [{'turn_id': 't', 'scores': dict.fromkeys(methods, 1)}]}]}
        embedding = {'scores': {'q': {v: [1] for v in ['forward', 'typed', 'question', 'denial', 'reverse']}}}
        policy = __import__('json').loads((explore.HERE / 'policy.json').read_text())
        scored = explore.compute_rows({'queries': [q]}, old, embedding, policy)[0]
        self.assertEqual(scored['rankings']['bm25_speaker_soft_1.0'], ['t'])
        self.assertEqual(scored['candidates'][0]['scores']['bm25_speaker_soft_1.0'], scored['candidates'][0]['scores']['bm25_structured'])
        self.assertIsNone(scored['prediction'])


class MetricTests(unittest.TestCase):
    def test_auc_ties_and_query_denominators(self):
        r = {'relevant_turn_ids': ['a'], 'score': {'candidates': [{'turn_id': 'a', 'scores': {'m': 1}},
                 {'turn_id': 'b', 'scores': {'m': 1}}], 'rankings': {'m': ['a', 'b']}}}
        out = evaluate.within_query_auc_ap([r], 'm')
        self.assertEqual(out['within_query_pair_auc'], .5)
        self.assertEqual(out['positive_negative_candidate_pairs'], 1)
        self.assertEqual(out['mean_average_precision'], 1)

    def test_empty_relevance_has_undefined_recall(self):
        r = {'query_id': 'q', 'label': 'unknown', 'relevant_turn_ids': [], 'relevant_evidence': []}
        out = evaluate.selected_summary([r], lambda r: ['a'])
        self.assertIsNone(out['turn_recall'])
        self.assertEqual(out['turn_precision'], 0)
        self.assertEqual(out['unknown_queries'], 1)


if __name__ == '__main__':
    unittest.main()
