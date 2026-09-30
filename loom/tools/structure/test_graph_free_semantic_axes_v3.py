"""Synthetic checks of the separate semantic recipe; no model-quality evidence."""
from copy import deepcopy
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import graph_free_semantic_axes_v3 as arm
    from .test_graph_free_schema_hint_v2 import raw_response, identity_snapshot, pricing
    from .test_graph_free_extraction import case, output
except ImportError:
    import graph_free_semantic_axes_v3 as arm
    from test_graph_free_schema_hint_v2 import raw_response, identity_snapshot, pricing
    from test_graph_free_extraction import case, output

free, panel, replay, safe = arm.free, arm.panel, arm.replay, arm.safe


class SemanticRecipeMechanisms(unittest.TestCase):
    def replay_fixture(self, folder, raw, ledger_cost='.002'):
        c = case(); rows = arm.requests([c])
        manifest = {'schema': safe.MANIFEST_SCHEMA,
            'experiment_id': 'graph-dev-free-semantic-axes-v3-01', 'budget_usd': '2',
            'max_requests': 1, 'requests': rows, 'pricing_evidence': pricing(),
            'metadata': arm.metadata()}
        folder = Path(folder); mp = folder / 'manifest.json'; panel.write_new(mp, manifest)
        run = folder / 'run'; run.mkdir()
        plan = safe.plan_manifest(manifest); request = plan['requests'][0]
        attempt = {key: request[key] for key in ('id', 'request_hash', 'reservation_usd')}
        attempt.update(state='completed', http_status=200, response_file=c['id'] + '.response.bin',
            response_sha256=hashlib.sha256(raw).hexdigest(), reported_cost_usd=ledger_cost)
        (run / attempt['response_file']).write_bytes(raw)
        panel.write_new(run / 'ledger.json', {'schema': 'loom.openrouter_ledger/1',
            'manifest_hash': plan['manifest_hash'], 'attempts': [attempt]})
        return c, mp, run

    def load(self, mp, run, cases):
        original = replay.read
        def snapshot_or_read(path):
            return identity_snapshot() if Path(path) == replay.SNAPSHOT else original(path)
        with patch.object(replay, 'read', side_effect=snapshot_or_read):
            return arm.load_run(mp, run, cases)

    def test_only_generic_semantic_suffix_changes_model_body(self):
        c = case(); before = deepcopy(c)
        baseline = arm.grammar.requests([c])[0]; changed = arm.requests([c])[0]
        expected = deepcopy(baseline)
        expected['body']['messages'][0]['content'] = arm.grammar.SYSTEM + arm.SEMANTIC_SUFFIX
        expected['reservation_usd'] = safe.estimate_reservation(expected['body'])['minimum_reservation_usd']
        self.assertEqual(changed, expected)
        self.assertEqual(c, before)
        self.assertEqual(safe.parse_json(changed['body']['messages'][1]['content']), free.source_payload(c))
        self.assertEqual(changed['body']['max_tokens'], 2048)
        self.assertEqual(changed['body']['provider'], baseline['body']['provider'])

    def test_semantic_examples_are_generic_and_preserve_grammar_bytes(self):
        self.assertTrue(arm.SYSTEM.startswith(arm.grammar.SYSTEM))
        self.assertIn('If not P, then not Q', arm.SEMANTIC_SUFFIX)
        self.assertIn('I deny that P implies Q', arm.SEMANTIC_SUFFIX)
        self.assertIn('do not endorse', arm.SEMANTIC_SUFFIX)
        for forbidden in ('lampa nie świeci', 'żaba czeka', 'gpv1_dev_', 'Mira', 'sensor reports'):
            self.assertNotIn(forbidden, arm.SEMANTIC_SUFFIX)
        self.assertIs(arm.grammar.compile_free, free.compile_free)
        self.assertIs(arm.grammar.score_free, free.score_free)

    def test_prepare_uses_no_gold_or_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            def source_only(path):
                self.assertEqual(Path(path), free.ROOT / 'docs/research/graph_method_panel_v1/extraction/prepared/manifest.json')
                return {'pricing_evidence': pricing()}
            with patch.object(panel, 'load_dev_inputs', return_value=[case()]), \
                 patch.object(panel, 'load_dev_gold', side_effect=AssertionError('gold read')), \
                 patch.object(replay, 'read', side_effect=source_only):
                result = arm.prepare(Path(temporary) / 'prepared')
            self.assertEqual(result['paid_calls'], 0)
            self.assertFalse(result['gold_read'])

    def test_valid_synthetic_response_uses_exact_existing_compiler(self):
        with tempfile.TemporaryDirectory() as temporary:
            c, mp, run = self.replay_fixture(temporary, raw_response())
            compiled, summary = self.load(mp, run, [c])
            self.assertEqual(compiled, [free.compile_free(output(), free.source_payload(c))])
            self.assertEqual(summary['compiled_complete'], 1)

    def test_billing_mismatch_hard_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            c, mp, run = self.replay_fixture(temporary, raw_response(), '.000001')
            with self.assertRaises(replay.BillingIntegrityError): self.load(mp, run, [c])

    def test_prior_v2_or_changed_whole_batch_rejected_before_ledger(self):
        with tempfile.TemporaryDirectory() as temporary:
            c, mp, run = self.replay_fixture(temporary, raw_response())
            original = replay.read(mp)
            for variant in ('prior_v2', 'suffix', 'metadata', 'batch_identity'):
                changed = deepcopy(original)
                if variant == 'prior_v2': changed['requests'] = arm.grammar.requests([c])
                elif variant == 'suffix': changed['requests'][0]['body']['messages'][0]['content'] += ' '
                elif variant == 'metadata': changed['metadata']['semantic_suffix_sha256'] = '0' * 64
                else: changed['experiment_id'] = 'graph-dev-free-semantic-axes-v3-02'
                mp.write_bytes(safe.canonical(changed))
                reads = []
                def manifest_only(path):
                    reads.append(Path(path)); self.assertEqual(Path(path), mp)
                    return deepcopy(changed)
                with patch.object(replay, 'read', side_effect=manifest_only):
                    with self.assertRaises(ValueError): arm.load_run(mp, run, [c])
                self.assertEqual(reads, [mp])

    def test_duplicate_keys_and_bad_evidence_still_rejected_without_salvage(self):
        malformed = deepcopy(output()); malformed['source_assertions'][0]['evidence'] = ['t1']
        for kind in ('duplicate', 'string_evidence'):
            raw = raw_response(content='{"nodes":[],"nodes":[],"source_assertions":[],"status_events":[]}') if kind == 'duplicate' else raw_response(malformed)
            with tempfile.TemporaryDirectory() as temporary:
                c, mp, run = self.replay_fixture(temporary, raw)
                compiled, summary = self.load(mp, run, [c])
                if kind == 'duplicate':
                    self.assertEqual(compiled[0]['state'], 'unavailable')
                else:
                    self.assertEqual(compiled[0]['invalid_assertions'], 1)
                self.assertEqual(summary['reported_known_cost_usd'], '0.002')


if __name__ == '__main__': unittest.main()
