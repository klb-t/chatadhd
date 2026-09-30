import json
import math
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cross_encoder as cross


class FakeTensor:
    def __init__(self, shape, value):
        self.shape = shape
        self.value = value
    def __getitem__(self, item):
        return [self.value]


class PairAndOutputTests(unittest.TestCase):
    def test_raw_negative_logit_is_not_clamped_or_sigmoid(self):
        self.assertEqual(cross.extract_logit(FakeTensor((1, 1), -7)), -7)

    def test_output_shape_rejected(self):
        for shape in ((1, 2), (1,), (2, 1)):
            with self.assertRaises(ValueError):
                cross.extract_logit(FakeTensor(shape, 1))

    def test_nonfinite_output_rejected(self):
        for value in (float('nan'), float('inf'), -float('inf')):
            with self.assertRaises(ValueError):
                cross.extract_logit(FakeTensor((1, 1), value))

    def test_pair_cache_respects_order_and_cap(self):
        self.assertNotEqual(cross.pair_key('a', 'b', 128), cross.pair_key('b', 'a', 128))
        self.assertNotEqual(cross.pair_key('a', 'b', 128), cross.pair_key('a', 'b', 256))

    def test_pair_cache_prevents_separator_collision(self):
        self.assertNotEqual(cross.pair_key('a|b', 'c', 128), cross.pair_key('a', 'b|c', 128))

    def test_unicode_bytes_retained(self):
        self.assertNotEqual(cross.pair_key('Żółć', 'nie', 128), cross.pair_key('Zolc', 'nie', 128))


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.policy = json.loads((cross.HERE / 'policy.json').read_text())
        self.templates = json.loads((cross.PANEL / 'representation_templates_v2.json').read_text())
        self.q = {'query_id': 'q', 'case_id': 'c', 'language': 'en', 'query_text': 'NOT A to B', 'prefix_sha256': 'prefix',
            'query': {'source': 'a', 'target': 'b', 'relation': 'implies', 'attributed_to': 'someone'},
            'prefix_payload': {'node_inventory': [{'id': 'a', 'text': 'NOT A', 'aliases': []}, {'id': 'b', 'text': 'B', 'aliases': []}]},
            'candidates': [{'turn_id': 't', 'representation_text': 'NOT A implies B', 'eligible_order': 0}]}
        self.old = {'rows': [{'query_id': 'q', 'prefix_sha256': 'prefix', 'rankings': {'old': ['t']},
                     'candidates': [{'turn_id': 't', 'scores': {'old': 1}}]}]}
        views = cross.renderer.render(self.q, self.templates)
        self.outputs = {}
        for variant, cap in [(v, 256) for v in self.policy['query_variants']] + [('structured', 128)]:
            key = cross.pair_key(views[variant], 'NOT A implies B', cap)
            self.outputs[key] = {'query_text': views[variant], 'candidate_text': 'NOT A implies B', 'max_tokens': cap, 'raw_logit': -5}

    def test_all_candidates_and_raw_scores_preserved(self):
        rows = cross.score_rows({'queries': [self.q]}, self.old, self.outputs, self.policy, self.templates)
        self.assertEqual(rows[0]['candidates'][0]['scores']['crossencoder_structured_256'], -5)
        self.assertEqual(rows[0]['rankings']['crossencoder_structured_256'], ['t'])
        self.assertIsNone(rows[0]['prediction'])
        self.assertFalse(rows[0]['source_judgment_available'])

    def test_prefix_drift_rejected(self):
        self.old['rows'][0]['prefix_sha256'] = 'changed'
        with self.assertRaises(ValueError):
            cross.score_rows({'queries': [self.q]}, self.old, self.outputs, self.policy, self.templates)

    def test_candidate_drift_rejected(self):
        self.old['rows'][0]['candidates'][0]['turn_id'] = 'future'
        with self.assertRaises(ValueError):
            cross.score_rows({'queries': [self.q]}, self.old, self.outputs, self.policy, self.templates)

    def test_pair_binding_drift_rejected(self):
        next(iter(self.outputs.values()))['candidate_text'] = 'OTHER'
        with self.assertRaises(ValueError):
            cross.score_rows({'queries': [self.q]}, self.old, self.outputs, self.policy, self.templates)

    def test_nonfinite_replay_score_rejected(self):
        next(iter(self.outputs.values()))['raw_logit'] = float('nan')
        with self.assertRaises(ValueError):
            cross.score_rows({'queries': [self.q]}, self.old, self.outputs, self.policy, self.templates)


if __name__ == '__main__':
    unittest.main()
