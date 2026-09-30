import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import template_renderer_v2 as renderer


def row():
    return {'language': 'en', 'query_text': 'NOT X', 'query': {'source': 'a', 'target': 'b', 'relation': 'implies', 'attributed_to': 'someone'},
        'prefix_payload': {'node_inventory': [{'id': 'a', 'text': 'NOT A', 'aliases': ['no alpha']}, {'id': 'b', 'text': 'B', 'aliases': []}]}}


class DataRendererTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((renderer.explore.HERE / 'representation_templates_v2.json').read_text())

    def test_exact_primary_rendering(self):
        self.assertEqual(renderer.render(row(), self.config), renderer.explore.render_variants(row()))

    def test_policy_extension_needs_no_code_change(self):
        self.config['variants']['new'] = {'template': 'SOURCE {source_text}; TARGET {target_text}'}
        self.assertEqual(renderer.render(row(), self.config)['new'], 'SOURCE NOT A; TARGET B')

    def test_negation_and_aliases_preserved(self):
        out = renderer.render(row(), self.config)
        self.assertIn('NOT A', out['reverse'])
        self.assertIn('no alpha', out['aliases'])

    def test_unknown_language_rejected(self):
        q = row()
        q['language'] = 'missing'
        with self.assertRaises(ValueError):
            renderer.render(q, self.config)

    def test_unknown_operation_rejected(self):
        self.config['variants']['bad'] = {'execute': 'anything'}
        with self.assertRaises(ValueError):
            renderer.render(row(), self.config)


if __name__ == '__main__':
    unittest.main()
