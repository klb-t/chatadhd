"""Offline regressions; scripted envelopes exercise machinery, not model quality."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.structure import model_study_readiness_v1 as e


ORIGINAL = e.study.ROOT / 'docs/research/analysis_optimization_2026-10-02/prepared'
SNAPSHOTS = ORIGINAL.parent / 'preflight'
RECIPES = e.study.ROOT / 'docs/research/w3_directed_commitment_v1/prepared_final'


class ReadinessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='study-successor-test-')
        cls.output = Path(cls.temporary.name) / 'successor'
        def forbidden(*args, **kwargs):
            raise AssertionError('offline tooling must not load a key or call an endpoint')
        with patch.object(e.jev, 'transport', forbidden), patch.object(e.safe, 'transport', forbidden), patch.object(e.safe, 'load_key', forbidden):
            cls.receipt = e.successor_prepare(ORIGINAL, SNAPSHOTS, cls.output, recipe_original=RECIPES)
        cls.prepared = cls.output / 'prepared'
        cls.plan = e.study.verify(cls.prepared)
        cls.fresh_now = e.safe._timestamp(e.study.read(cls.prepared / 'j_active/manifest.json')['created_at']) + 60

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.temporary_run = tempfile.TemporaryDirectory(prefix='study-run-test-')
        self.addCleanup(self.temporary_run.cleanup)
        self.runs = Path(self.temporary_run.name)

    def arm(self, name):
        return next(a for a in self.plan['arms'] if a['arm'] == name)

    def ledger(self, arm_name, *, state='completed', cost='.0000042', label='unknown',
               raw_cost=None, provider='TypeSafe'):
        """One explicitly scripted first attempt, written directly, never sent."""
        arm = self.arm(arm_name)
        manifest = e.study.read(self.prepared / arm_name / 'manifest.json')
        request = manifest['requests'][0]
        planned = request if arm['kind'] == 'jev' else e.safe.plan_manifest(manifest)['requests'][0]
        row = {key: planned[key] for key in ('id', 'request_hash', 'reservation_usd')}
        row.update(state=state, started_at='2026-10-02T10:40:00Z')
        directory = self.runs / arm_name
        directory.mkdir()
        if state == 'completed':
            charged = cost if raw_cost is None else raw_cost
            if arm['kind'] == 'jev':
                probabilities = {'q01': .9 if label == 'supported' else .1,
                                 'q02': .9 if label == 'refuted' else .1}
                response = {'id': 'gen-scripted-unit', 'model': manifest['model_aliases'][0],
                            'provider': provider,
                            'answers': {key: {'type': 'noul', 'noul': probabilities[key]}
                                        for key in request['body']['questions']},
                            'usage': {'input_tokens': 100, 'output_tokens': 10, 'cost': charged}}
            else:
                content = 'broken answer' if label == 'invalid' else e.safe.canonical({'query_id': request['id'], 'label': label}).decode()
                response = {'model': request['body']['model'],
                            'choices': [{'finish_reason': 'stop', 'message': {'content': content}}],
                            'usage': {'prompt_tokens': 200, 'completion_tokens': 20, 'cost': charged}}
            raw = e.safe.canonical(response)
            name = request['id'] + '.response.bin'
            (directory / name).write_bytes(raw)
            row.update(response_file=name, response_sha256=e.digest_file(directory / name),
                       http_status=200, reported_cost_usd=cost, elapsed_seconds=.25,
                       finished_at='2026-10-02T10:40:01Z')
        ledger = {'schema': 'loom.jev_ledger/1' if arm['kind'] == 'jev' else 'loom.openrouter_ledger/1',
                  'manifest_hash': e.safe.digest(manifest), 'attempts': [row]}
        e.study.write(directory / 'ledger.json', ledger)
        return ledger

    def audit(self, **kwargs):
        return e.audit(self.prepared, self.runs, now=self.fresh_now, **kwargs)

    def test_original_first_failure_and_successor_request_equality(self):
        status = e.frozen_status(ORIGINAL)
        self.assertFalse(status['source_matches_freeze'])
        self.assertEqual({r['path'] for r in status['source_drift']},
                         {'loom/tools/structure/w3_directed_commitment_v1/experiment.py',
                          'loom/tools/structure/w3_directed_commitment_v1/test_experiment.py'})
        self.assertEqual(self.receipt['requests_equal'], 432)
        self.assertEqual(self.receipt['pricing_arms_equal'], 9)
        self.assertEqual(self.receipt['recipes']['queries_equal'], 192)
        self.assertFalse(self.receipt['gold_read_for_preparation'])
        self.assertTrue(e.frozen_status(self.prepared)['source_matches_freeze'])

    def test_payload_tamper_is_never_accepted_as_source_migration(self):
        with tempfile.TemporaryDirectory() as folder:
            copied = Path(folder)
            # Reuse paths via a miniature freeze with one actual frozen payload.
            e.study.write(copied / 'plan.json', {'changed': True})
            e.study.write(copied / 'freeze.json', {'schema': 'loom.analysis_optimization.freeze/1',
                          'source_sha256': {}, 'files_sha256': {'plan.json': '0' * 64}})
            with self.assertRaisesRegex(ValueError, 'frozen_payload_drift'):
                e.frozen_status(copied)

    def test_recipe_overlap_prevents_96_duplicate_requests(self):
        result = self.audit(recipes=self.output / 'recipes')
        self.assertEqual(sum(len(r['identical_requests']) for r in result['recipe_request_overlap']), 96)
        self.assertEqual(result['minimum_account_allowance_for_remaining_plan_usd'], '0.57124553')
        self.assertFalse(result['live_execution_authorized'])
        self.assertEqual(sum(a['planned_requests'] for a in result['arms']), 432)

    def test_old_snapshots_remain_old_and_price_age_is_a_parameter(self):
        old_now = self.fresh_now + 3 * 86400
        result = e.audit(self.prepared, self.runs, now=old_now)
        self.assertTrue(result['campaign_resume_blocked'])
        self.assertIn('stale_price_evidence', [b['reason'] for b in result['blockers']])
        extended = e.audit(self.prepared, self.runs, now=old_now, max_age_seconds=4 * 86400)
        self.assertNotIn('stale_price_evidence', [b['reason'] for b in extended['blockers']])
        self.assertFalse(extended['live_execution_authorized'])

    def test_nonfinite_or_boolean_policy_and_clock_cannot_open_readiness(self):
        for value in (float('nan'), float('inf'), True):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, 'invalid_price_age_policy'):
                    self.audit(max_age_seconds=value)
                with self.assertRaisesRegex(ValueError, 'invalid_price_age_policy'):
                    self.audit(future_tolerance_seconds=value)
                with self.assertRaisesRegex(ValueError, 'invalid_explicit_clock'):
                    e.audit(self.prepared, self.runs, now=value)

    def test_nonfinite_latency_is_missing_and_never_a_json_number(self):
        ledger = self.ledger('j_active')
        ledger['attempts'][0]['elapsed_seconds'] = float('inf')
        # Direct per-arm inspection exercises an in-memory malformed ledger.
        original = e.study.read
        def injected(path):
            return ledger if Path(path).name == 'ledger.json' else original(path)
        with patch.object(e.study, 'read', injected):
            arm = e.arm_audit(self.prepared, self.runs, self.arm('j_active'),
                              now=self.fresh_now, max_age_seconds=86400,
                              future_tolerance_seconds=300)
        self.assertIsNone(arm['evidence_rows'][0]['elapsed_seconds'])
        self.assertNotIn(b'Infinity', e.safe.canonical(arm))

    def test_writeahead_attempt_blocks_all_arms_and_is_never_retried(self):
        ledger = self.ledger('j_active', state='started')
        result = self.audit()
        self.assertTrue(result['campaign_resume_blocked'])
        arm = next(a for a in result['arms'] if a['arm'] == 'j_active')
        self.assertEqual(arm['retry_attempt_ids'], [])
        self.assertEqual(arm['never_retry_attempt_ids'], [ledger['attempts'][0]['id']])
        self.assertEqual(len(arm['unattempted_ids']), 47)
        self.assertEqual(arm['unknown_cost_reservation_usd'], '0.001')
        self.assertEqual(result['minimum_account_allowance_for_remaining_plan_usd'], '0.57124553')

    def test_completed_stop_ledger_cannot_be_bypassed_by_another_arm(self):
        ledger = self.ledger('j_active')
        path = self.runs / 'j_active/ledger.json'
        ledger['stopped_reason'] = 'jev_post_key_check_failed'
        path.write_bytes(e.safe.canonical(ledger))
        result = self.audit()
        self.assertTrue(result['campaign_resume_blocked'])
        self.assertIn({'arm': 'j_active', 'reason': 'stopped_ledger'}, result['blockers'])

    def test_orphaned_response_blocks_campaign_without_new_first_attempt(self):
        directory = self.runs / 'g_brief_t0'
        directory.mkdir()
        (directory / 'orphan.response.bin').write_bytes(b'{}')
        result = self.audit()
        self.assertIn({'arm': 'g_brief_t0', 'reason': 'stranded_responses_without_ledger'}, result['blockers'])

    def test_completed_billing_mismatch_and_provider_drift_are_rejected(self):
        self.ledger('j_active', raw_cost='.000005')
        with self.assertRaisesRegex(ValueError, 'billing_mismatch'):
            self.audit()
        with tempfile.TemporaryDirectory() as folder:
            self.runs = Path(folder)
            self.ledger('j_active', provider='Other')
            with self.assertRaisesRegex(ValueError, 'identity_mismatch'):
                self.audit()

    def test_invalid_structured_answer_keeps_cost_and_all_48_queries(self):
        self.ledger('g_brief_t0', label='invalid')
        result = e.score(self.prepared, self.runs, self.runs / 'score.json', evidence_class='scripted_mechanism')
        report = result['reports']['g_brief_t0']
        self.assertEqual(report['query_count'], 48)
        self.assertEqual(report['available'], 0)
        cost = result['resource_accounting']['g_brief_t0']
        self.assertEqual(cost['reported_cost_usd'], '0.0000042')
        self.assertEqual(cost['input_tokens_recorded'], 200)
        self.assertEqual(cost['output_tokens_recorded'], 20)
        self.assertIsNone(cost['cost_per_correct_usable_query_usd'])
        self.assertFalse(result['model_quality_measured'])

    def test_split_combines_two_charges_into_one_correct_decision(self):
        query_id = e.study.read(self.prepared / 'j_split_q01/manifest.json')['requests'][0]['id']
        gold = {q['query_id']: q['label'] for row in e.w3.load_controls('gold_dev.json') for q in row['judgments']}
        self.ledger('j_split_q01', label=gold[query_id])
        self.ledger('j_split_q02', label=gold[query_id])
        result = e.score(self.prepared, self.runs, self.runs / 'score.json', evidence_class='scripted_mechanism')
        cost = result['resource_accounting']['j_split']
        self.assertEqual(cost['attempted_requests'], 2)
        self.assertEqual(cost['correct_usable_queries'], 1)
        self.assertEqual(cost['reported_cost_usd'], '0.0000084')
        self.assertEqual(cost['cost_per_correct_usable_query_usd'], '0.0000084')
        self.assertEqual(result['reports']['j_split']['query_count'], 48)
        self.assertFalse(result['model_quality_measured'])
        self.assertFalse(result['new_model_quality_measured'])
        self.assertEqual(result['evidence_class'], 'scripted_mechanism')

    def test_unknown_billing_has_no_invented_cost_per_correct(self):
        row = {'reported_cost_usd': None, 'unknown_cost_reservation_usd': '.001',
               'input_tokens': None, 'output_tokens': None, 'elapsed_seconds': None}
        cost = e._cost_summary([row], 1)
        self.assertIsNone(cost['cost_per_correct_usable_query_usd'])
        self.assertEqual(cost['unknown_cost_reservation_usd'], '0.001')
        self.assertFalse(cost['cost_accounting_complete_for_attempts'])

    def test_existing_output_cannot_replace_first_score(self):
        path = self.runs / 'score.json'
        result = e.score(self.prepared, self.runs, path, evidence_class='saved_provider_replay')
        self.assertFalse(result['model_quality_measured'])
        with self.assertRaises(FileExistsError):
            e.score(self.prepared, self.runs, path, evidence_class='saved_provider_replay')


if __name__ == '__main__':
    unittest.main()
