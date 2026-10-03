from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_panel_v2 as panel
import evaluate_native_v2 as evaluator


def case():
    return {'id': 'case', 'language': 'pl', 'source_id': 'synthetic:case',
        'turns': [{'id': 'a', 'speaker': 'reporter', 'known_at': '2026-08-10T09:00:00Z', 'text': 'Żółć nie jest A.'},
                  {'id': 'b', 'speaker': 'analyst', 'known_at': '2026-08-10T09:01:00Z', 'text': 'B nie implikuje A.'}]}


class NativeWrapperTests(unittest.TestCase):
    def setUp(self):
        self.policy = json.loads((panel.HERE / 'policy.json').read_text())

    def test_source_only_strips_all_supplied_nodes_queries_gold(self):
        original = case() | {'node_inventory': [{'id': 'X'}], 'judgment_queries': ['goldlike'], 'gold': ['SECRET_LABEL']}
        self.assertEqual(set(panel.source_only(original)), {'id', 'language', 'source_id', 'turns'})

    def test_exact_unicode_source_content_preserved(self):
        wrapped = panel.to_openai(case(), 'transport_user', self.policy)
        self.assertEqual(wrapped['mapping']['a']['message']['content']['parts'][0].encode(), case()['turns'][0]['text'].encode())

    def test_original_speaker_and_date_metadata_preserved(self):
        wrapped = panel.to_openai(case(), 'transport_user', self.policy)
        m = wrapped['mapping']['a']['message']
        self.assertEqual(m['author'], {'role': 'user', 'name': 'reporter'})
        self.assertEqual(m['metadata']['loom_source_speaker'], 'reporter')
        self.assertEqual(m['metadata']['loom_source_known_at'], '2026-08-10T09:00:00Z')

    def test_epoch_roundtrip_exact(self):
        wrapped = panel.to_openai(case(), 'transport_user', self.policy)
        epoch = wrapped['mapping']['a']['message']['create_time']
        self.assertEqual(datetime.fromtimestamp(epoch, timezone.utc), datetime.fromisoformat('2026-08-10T09:00:00+00:00'))

    def test_linear_parent_children_order(self):
        wrapped = panel.to_openai(case(), 'transport_user', self.policy)
        self.assertEqual(wrapped['mapping']['a']['children'], ['b'])
        self.assertEqual(wrapped['mapping']['b']['parent'], 'a')
        self.assertEqual(wrapped['current_node'], 'b')

    def test_literal_speaker_control_is_explicit_not_silent_repair(self):
        wrapped = panel.to_openai(case(), 'literal_speaker_role', self.policy)
        self.assertEqual(wrapped['mapping']['a']['message']['author']['role'], 'reporter')

    def test_unknown_arm_rejected(self):
        with self.assertRaises(ValueError):
            panel.to_openai(case(), 'hidden', self.policy)

    def test_duplicate_turn_identity_rejected(self):
        source = case()
        source['turns'][1]['id'] = 'a'
        with self.assertRaises(ValueError):
            panel.to_openai(source, 'transport_user', self.policy)


class NativeGroundingTests(unittest.TestCase):
    def setUp(self):
        self.source = case()
        policy = json.loads((panel.HERE / 'policy.json').read_text())
        self.document = panel.to_openai(self.source, 'transport_user', policy)
        self.observation = {'locator': {'json_pointer': '/mapping/a/message/content/parts/0', 'byte_start': 0,
            'byte_len': len(self.source['turns'][0]['text'].encode())}, 'text': self.source['turns'][0]['text'],
            'date': '2026-08-10T09:00:00Z', 'speaker': 'user'}

    def test_exact_pointer_unicode_byte_grounding(self):
        out = evaluator.grounding(self.observation, self.document, self.source)
        self.assertTrue(out['valid'])
        self.assertTrue(out['same_original_date'])
        self.assertFalse(out['same_original_speaker'])

    def test_inside_unicode_codepoint_rejected(self):
        self.observation['locator']['byte_start'] = 1
        self.assertFalse(evaluator.grounding(self.observation, self.document, self.source)['valid'])

    def test_wrong_quote_rejected(self):
        self.observation['text'] = 'B'
        self.assertFalse(evaluator.grounding(self.observation, self.document, self.source)['valid'])

    def test_rfc6901_escaping(self):
        self.assertEqual(evaluator.resolve_pointer({'a/b': {'x~y': [1]}}, '/a~1b/x~0y/0'), 1)


class NativeProcessMetricsTests(unittest.TestCase):
    def test_exact_binary_streams_and_actual_single_child_usage(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'metrics.json'
            code = 'import sys;sys.stdout.buffer.write(bytes([0,255,65]));sys.stderr.buffer.write(b"diagnostic");sum(i*i for i in range(100000))'
            process = subprocess.run([sys.executable, str(panel.HERE / 'native_process_metrics_v2.py'),
                '--metric-output', str(output), '--', sys.executable, '-c', code], capture_output=True)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(process.stdout, bytes([0,255,65]))
            self.assertEqual(process.stderr, b'diagnostic')
            metrics = json.loads(output.read_text())
            self.assertEqual(metrics['child_processes'], 1)
            self.assertGreater(metrics['max_rss_kib'], 0)
            self.assertGreater(metrics['wall_seconds'], 0)
            self.assertGreaterEqual(metrics['user_cpu_seconds'], 0)

    def test_child_failure_is_preserved_not_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'metrics.json'
            process = subprocess.run([sys.executable, str(panel.HERE / 'native_process_metrics_v2.py'),
                '--metric-output', str(output), '--', sys.executable, '-c', 'raise SystemExit(7)'], capture_output=True)
            self.assertEqual(process.returncode, 7)
            self.assertEqual(json.loads(output.read_text())['native_exit_code'], 7)


if __name__ == '__main__':
    unittest.main()
