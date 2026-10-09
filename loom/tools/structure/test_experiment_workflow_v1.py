"""Mechanism tests only; no model-quality fixture or provider calls."""
from copy import deepcopy
import itertools
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure import experiment_workflow_v1 as w


def spec():
    return {'schema': 'loom.experiment_spec/1', 'id': 'mechanism-test', 'campaign_id': 'existing',
            'variants': {'mode': 'list', 'values': [{'model': 'fixture/a'}, {'model': 'fixture/b'}, {'model': 'fixture/c'}]},
            'scope': [{'id': 'c1', 'family': 'f1', 'source_sha256': 'a'*64, 'split': 'development', 'input': 'fixture input'}],
            'repetitions': 2, 'request_template': {'model': {'$variant': 'model'},
                  'messages': [{'role': 'user', 'content': {'$variant': 'case'}}]},
            'response_cache': False, 'prompt_cache_policy': 'provider_observed_only',
            'cache_prefix_messages': 1, 'route_id': 'chat', 'endpoint_identity': 'fixture',
            'ordering': {'mode': 'balanced_blocks', 'seed': 17}, 'adoption_policy': 'manual',
            'metrics': ['source_agreement', 'native_execution'], 'evaluators': ['source_reference'],
            'budget': {'campaign_reset': False},
            'triggers': {'opt_in': False, 'events': ['checkpoint'], 'frequency_seconds': 60}}


class WorkflowTests(unittest.TestCase):
    def test_three_and_five(self):
        s = spec(); self.assertEqual(len(list(w.variants(s))), 3)
        s['variants']['values'] += [{'model': 'fixture/d'}, {'model': 'fixture/e'}]
        self.assertEqual(len(list(w.jobs(s))), 10)

    def test_lazy_large_matrix_and_order(self):
        s = spec(); s['variants'] = {'mode': 'matrix', 'axes': [
            {'name': 'model', 'values': ['fixture/a', 'fixture/b']},
            *[{'name': str(i), 'values': list(range(10))} for i in range(20)]]}
        self.assertEqual(len(list(itertools.islice(w.variants(s), 5))), 5)
        self.assertEqual(len(list(itertools.islice(w.ordered_jobs(s), 5))), 5)

    def test_matrix_product(self):
        s = spec(); s['variants'] = {'mode': 'matrix', 'axes': [
            {'name': 'model', 'values': ['a', 'b']}, {'name': 'temperature', 'values': [0, 1]}]}
        self.assertEqual(len(list(w.jobs(s))), 8)
        self.assertEqual(len({r['operation_id'] for r in w.ordered_jobs(s)}), 8)

    def test_duplicate_variants_and_axes(self):
        s = spec(); s['variants']['values'] *= 2
        with self.assertRaisesRegex(ValueError, 'duplicate_variants'): list(w.variants(s))
        s['variants'] = {'mode': 'matrix', 'axes': [{'name': 'x', 'values': [1]}, {'name': 'x', 'values': [2]}]}
        with self.assertRaisesRegex(ValueError, 'duplicate_axis'): list(w.variants(s))

    def test_typed_render_no_text_rewriting(self):
        self.assertEqual(w.render({'x': {'$variant': 'value'}}, {'value': {'text': 'a\n\u0105'}}), {'x': {'text': 'a\n\u0105'}})

    def test_exact_request_selection(self):
        self.assertEqual(w.render({'$select': {'table': 'case', 'key': 'cfg'}}, {'case': {'a': {'model': 'm'}}, 'cfg': 'a'}), {'model': 'm'})

    def test_matrix_request_match_and_ambiguity(self):
        t = {'$match': {'table': 'case', 'fields': ['x'], 'value_field': 'request'}}
        values = {'x': 0, 'case': [{'x': 0, 'request': {'model': 'm'}}]}
        self.assertEqual(w.render(t, values), {'model': 'm'})
        values['case'] *= 2
        with self.assertRaisesRegex(ValueError, 'ambiguous'): w.render(t, values)

    def test_family_leakage(self):
        s = spec(); s['scope'].append({**s['scope'][0], 'id': 'c2', 'source_sha256': 'b'*64, 'split': 'validation'})
        with self.assertRaisesRegex(ValueError, 'split_leakage'): list(w.jobs(s))

    def test_exact_duplicate_leakage(self):
        s = spec(); s['scope'].append({**s['scope'][0], 'id': 'c2', 'family': 'f2', 'split': 'validation'})
        with self.assertRaisesRegex(ValueError, 'split_leakage'): list(w.jobs(s))

    def test_used_blind_rejected(self):
        s = spec(); s['scope'][0]['split'] = 'used_blind'
        with self.assertRaisesRegex(ValueError, 'used_blind'): list(w.jobs(s))

    def test_cache_and_budget_reset_rejected(self):
        s = spec(); s['response_cache'] = True
        with self.assertRaisesRegex(ValueError, 'cache_disabled'): list(w.jobs(s))
        s = spec(); s['budget']['campaign_reset'] = True
        with self.assertRaisesRegex(ValueError, 'cannot_reset'): list(w.jobs(s))

    def test_repetition_identity_not_request_identity(self):
        rows = list(w.jobs(spec()))
        self.assertNotEqual(rows[0]['operation_id'], rows[3]['operation_id'])
        self.assertEqual(rows[0]['request_sha256'], rows[3]['request_sha256'])
        self.assertEqual(rows[0]['headers']['X-OpenRouter-Cache'], 'false')

    def test_cache_groups_do_not_share_models(self):
        rows = list(w.jobs(spec()))
        self.assertNotEqual(rows[0]['cache_group'], rows[1]['cache_group'])

    def test_cache_groups_separate_actual_endpoint(self):
        s = spec(); a = next(w.jobs(s)); s['endpoint_identity'] = 'another'
        self.assertNotEqual(a['cache_group'], next(w.jobs(s))['cache_group'])

    def test_order_deterministic_and_rotates(self):
        s = spec(); a = list(w.ordered_jobs(s))
        self.assertEqual(a, list(w.ordered_jobs(s)))
        self.assertNotEqual(a[0]['variant_index'], a[3]['variant_index'])

    def test_prefix_order_preserves_exact_requests(self):
        s = spec(); s['ordering']['mode'] = 'prefix_grouped'
        a = list(w.ordered_jobs(s))
        self.assertEqual({r['request_sha256'] for r in a}, {r['request_sha256'] for r in w.jobs(s)})

    def test_unknown_budget_and_reservations(self):
        r = {'campaign_verified': True, 'key_bound': True, 'limit_reset': 'nonrenewing',
             'cap_usd': '5', 'current_usage_usd': '.7036619', 'reservations_usd': None}
        self.assertIsNone(w.available_budget(r)); r['reservations_usd'] = '.1'
        self.assertEqual(str(w.available_budget(r)), '4.1963381')
        r['campaign_verified'] = False; self.assertIsNone(w.available_budget(r))

    def test_negative_or_nonfinite_budget(self):
        for v in ['-1', 'NaN', 'Infinity']:
            with self.subTest(v=v), self.assertRaises(ValueError):
                w.available_budget({'campaign_verified': True, 'key_bound': True, 'limit_reset': 'nonrenewing',
                     'cap_usd': '5', 'current_usage_usd': v, 'reservations_usd': 0})

    def test_summary_unknown_no_adoption(self):
        s = spec(); s['adoption_policy'] = 'automatic'
        result = w.summary(s, [{'state': 'prepared'}])
        self.assertFalse(result['settings_changed']); self.assertIsNone(result['quality']['source_agreement'])

    def test_result_instrumentation_unknown_and_bound(self):
        job=next(w.ordered_jobs(spec()))
        record=w.result_record(job,b'first',{'response_cache_status':'MISS'})
        self.assertEqual(record['request_sha256'],job['request_sha256'])
        self.assertIsNone(record['observed_model']);self.assertIsNone(record['actual_cost_usd'])
        self.assertIsNone(record['omitted_parameters']);self.assertIn('response_sha256',record)
        with self.assertRaisesRegex(ValueError,'unsupported_result_metadata'):
            w.result_record(job,b'first',{'api_key':'fixture-secret'})


class QueueTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'queue.sqlite'
        self.q = w.Queue(self.path); self.addCleanup(self.q.db.close)
        self.s = spec(); self.rows = list(w.ordered_jobs(self.s))

    def test_resume_no_new_operations(self):
        self.assertEqual(self.q.add(self.rows), 6); self.assertEqual(self.q.add(self.rows), 0)
        another = w.Queue(self.path); self.addCleanup(another.db.close)
        self.assertEqual(len(another.snapshot()), 6)

    def test_identity_conflict_rolls_back_batch(self):
        self.q.add(self.rows); bad = deepcopy(self.rows[0]); bad['request'] = {}
        with self.assertRaisesRegex(ValueError, 'identity_reused'): self.q.add([bad])
        self.assertEqual(len(self.q.snapshot()), 6)

    def test_opt_in_frequency_dedup_self_trigger(self):
        e = {'id': 'e1', 'kind': 'checkpoint'}
        self.assertFalse(self.q.trigger(self.s, e, 1))
        self.s['triggers']['opt_in'] = True
        self.assertFalse(self.q.trigger(self.s, {**e, 'origin_experiment_id': 'other-experiment'}, 1))
        self.assertTrue(self.q.trigger(self.s, e, 1)); self.assertFalse(self.q.trigger(self.s, e, 100))
        self.assertFalse(self.q.trigger(self.s, {**e, 'id': 'e2'}, 50))
        self.assertTrue(self.q.trigger(self.s, {**e, 'id': 'e2'}, 61))

    def test_trigger_version_requires_new_identity(self):
        self.s['triggers']['opt_in'] = True
        self.q.trigger(self.s, {'id': 'e1', 'kind': 'checkpoint'}, 1)
        self.s['repetitions'] = 3
        with self.assertRaisesRegex(ValueError, 'identity_reused'):
            self.q.trigger(self.s, {'id': 'e2', 'kind': 'checkpoint'}, 100)

    def test_pending_never_retried(self):
        self.q.add(self.rows); ident = self.rows[0]['operation_id']
        self.q.mark_dispatched(ident, 'existing-ledger-reservation')
        with self.assertRaisesRegex(ValueError, 'already_dispatched'): self.q.mark_dispatched(ident, 'other')
        row = next(x for x in self.q.snapshot() if x['job']['operation_id'] == ident)
        self.assertEqual(row['payer_reference'], 'existing-ledger-reservation')

    def test_first_response_immutable(self):
        self.q.add(self.rows); job = self.rows[0]; ident = job['operation_id']
        r = {'request_sha256': job['request_sha256'], 'response': 'first', 'response_cache_status': 'MISS'}
        with self.assertRaisesRegex(ValueError, 'without_dispatch'): self.q.capture_first(ident, r)
        self.q.mark_dispatched(ident, 'existing-ledger'); self.q.capture_first(ident, r); self.q.capture_first(ident, r)
        with self.assertRaisesRegex(ValueError, 'immutable'): self.q.capture_first(ident, {**r, 'response': 'second'})

    def test_cached_and_wrong_request_rejected(self):
        self.q.add(self.rows); job = self.rows[0]; ident = job['operation_id']
        self.q.mark_dispatched(ident, 'existing-ledger')
        with self.assertRaisesRegex(ValueError, 'not_independent'):
            self.q.capture_first(ident, {'request_sha256': job['request_sha256'], 'response_cache_status': 'HIT'})
        with self.assertRaisesRegex(ValueError, 'request_mismatch'):
            self.q.capture_first(ident, {'request_sha256': 'b'*64})

    def test_git_private_queue_rejected(self):
        (Path(self.temp.name) / '.git').mkdir()
        with self.assertRaisesRegex(ValueError, 'inside_git'): w.Queue(Path(self.temp.name) / 'bad.sqlite')


if __name__ == '__main__':
    unittest.main()
