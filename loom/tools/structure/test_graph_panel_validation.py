"""Validation-wrapper mechanism checks on synthetic inputs only; no sealed reads."""
from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import graph_panel_validation as val, graph_panel_live as panel
    from . import openrouter_runner as safe, graph_panel_score_run as integrity
except ImportError:
    import graph_panel_validation as val, graph_panel_live as panel
    import openrouter_runner as safe, graph_panel_score_run as integrity


def toy_case():
    return {'id': 'gpv1_validation_mechanism', 'language': 'pl', 'source_id': 'synthetic:mechanism',
        'turns': [
            {'id': 't1', 'speaker': 'writer', 'known_at': '2026-08-10T09:00:00Z',
             'text': 'Jeśli ćma leci, żaba czeka.'},
            {'id': 't2', 'speaker': 'writer', 'known_at': '2026-08-10T09:01:00Z',
             'text': 'Sprostowanie: ćma leci nie implikuje żaba czeka.'}],
        'node_inventory': [{'id': 'A', 'text': 'ćma leci', 'aliases': []},
                           {'id': 'B', 'text': 'żaba czeka', 'aliases': []},
                           {'id': 'C', 'text': 'lis śpi', 'aliases': []}],
        'judgment_queries': [
            {'id': 'toy_q1', 'relation': 'implies', 'source': 'A', 'target': 'B',
             'attributed_to': 'writer', 'as_of': '2026-08-10T09:00:00Z', 'scope': 'explicit_source'},
            {'id': 'toy_q2', 'relation': 'implies', 'source': 'A', 'target': 'B',
             'attributed_to': 'writer', 'as_of': '2026-08-10T09:01:00Z', 'scope': 'explicit_source'},
            {'id': 'toy_formal', 'relation': 'implies', 'source': 'A', 'target': 'C',
             'attributed_to': 'writer', 'as_of': '2026-08-10T09:00:00Z', 'scope': 'formal_implication'}]}


def toy_release():
    return {'released_at': '2026-09-30T00:00:00Z', 'release_sha256': 'toy-external-pin',
            'batch_cap_usd': '.10', 'remaining_reservation_allowance_usd': '1.9'}


def fake_raw(label='supported', cost=.002, provider='OpenAI', model=panel.MODEL):
    return safe.canonical({'model': model, 'provider': provider,
        'usage': {'cost': cost, 'is_byok': False},
        'choices': [{'finish_reason': 'stop', 'message': {
            'content': safe.canonical({'query_id': 'toy_q1', 'label': label}).decode()}}]})


class ValidationMechanismTests(unittest.TestCase):
    def auth_fixture(self, directory, *, cost_unknown=True):
        """A wholly synthetic miniature repo; no production fixture access."""
        root = Path(directory); fixture = root / 'fixture'; fixture.mkdir()
        here = root / 'docs'; here.mkdir()
        (root / 'method.py').write_text('frozen synthetic method\n')
        method_hash = panel.digest_file(root / 'method.py')
        (here / 'SCORER_DRIVER_FREEZE4.json').write_bytes(safe.canonical({'files_sha256': {'method.py': method_hash}}))
        public = root / 'docs/research/model_method_panel_v1/public_preflight'
        public.mkdir(parents=True)
        public.joinpath('jev_endpoints.json').write_bytes(safe.canonical({'data': {
            'id': val.jev.MODEL, 'endpoints': [{'tag': 'typesafe', 'status': 0,
                'model_id': val.jev.MODEL, 'name': 'Typesafe | ' + val.jev.MODEL,
                'pricing': {'prompt': '.000000042', 'completion': '0'}}]}}))
        ledger_dir = root / 'run'; ledger_dir.mkdir()
        attempt = {'reservation_usd': '.001366'} if cost_unknown else {'reported_cost_usd': '.02'}
        (ledger_dir / 'ledger.json').write_bytes(safe.canonical({'schema': 'loom.openrouter_ledger/1', 'attempts': [attempt]}))
        manifest = {'files': {'inputs_validation.json': 'not-accessed', 'gold_validation.json': 'not-accessed'}}
        (fixture / 'manifest.json').write_bytes(safe.canonical(manifest))
        fixture_sha = panel.digest_file(fixture / 'manifest.json')
        release = {'schema': val.RELEASE_SCHEMA, 'issued_by': '/root', 'authorized': True,
            'released_at': '2026-09-30T00:00:00Z', 'fixture_manifest_sha256': fixture_sha,
            'methods_sha256': {'method.py': method_hash},
            'inherited_ledger_sha256': {'run/ledger.json': panel.digest_file(ledger_dir / 'ledger.json')},
            'global_budget_usd': '2', 'batch_cap_usd': '.10', 'known_reported_spent_usd': '.02',
            'retained_unknown_cost_reservation_usd': '.001366', 'remaining_reservation_allowance_usd': '1.9',
            'no_retries': True, 'session_budget_reset': False, 'all_intended_methods_frozen': True}
        path = root / 'RELEASE.json'; path.write_bytes(safe.canonical(release))
        patches = [patch.object(val, 'ROOT', root), patch.object(val, 'FIXTURE', fixture),
                   patch.object(val, 'HERE', here), patch.object(val, 'FIXTURE_MANIFEST_SHA256', fixture_sha),
                   patch.object(val, 'MINIMUM_PINS', {'method.py'})]
        return path, release, patches

    def auth_context(self, patches):
        from contextlib import ExitStack
        stack = ExitStack()
        for item in patches:
            stack.enter_context(item)
        return stack

    def test_bad_external_pin_fails_before_any_sealed_access(self):
        with tempfile.TemporaryDirectory() as temporary:
            path, _, patches = self.auth_fixture(temporary)
            with self.auth_context(patches), patch.object(val, 'load_inputs') as sealed:
                with self.assertRaises(ValueError): val.prepare(path, 'wrong-root-digest', Path(temporary) / 'out')
                sealed.assert_not_called()

    def test_auth_never_opens_missing_sealed_files_and_retains_unknown_cost(self):
        with tempfile.TemporaryDirectory() as temporary:
            path, _, patches = self.auth_fixture(temporary)
            with self.auth_context(patches):
                result = val.authorize(path, panel.digest_file(path))
                self.assertEqual(result['retained_unknown_cost_reservation_usd'], '.001366')
                self.assertFalse((val.FIXTURE / 'inputs_validation.json').exists())
                self.assertFalse((val.FIXTURE / 'gold_validation.json').exists())

    def test_release_cannot_reset_budget_or_understate_known_spend(self):
        for mutation in ({'session_budget_reset': True}, {'global_budget_usd': '4'},
                         {'known_reported_spent_usd': '.001'}, {'remaining_reservation_allowance_usd': '2'}):
            with tempfile.TemporaryDirectory() as temporary:
                path, release, patches = self.auth_fixture(temporary, cost_unknown=False)
                release.update(mutation); path.write_bytes(safe.canonical(release))
                with self.auth_context(patches):
                    with self.assertRaises(ValueError): val.authorize(path, panel.digest_file(path))

    def test_release_rejects_method_drift_and_older_fixture_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            path, release, patches = self.auth_fixture(temporary)
            (Path(temporary) / 'method.py').write_text('changed\n')
            with self.auth_context(patches):
                with self.assertRaises(ValueError): val.authorize(path, panel.digest_file(path))
            release['fixture_manifest_sha256'] = 'older-held-out-fixture'
            path.write_bytes(safe.canonical(release))
            with self.auth_context(patches):
                with self.assertRaises(ValueError): val.authorize(path, panel.digest_file(path))

    def test_sealed_pin_and_path_escape_refused(self):
        for path in ('../older-holdout.json', '/tmp/older-holdout.json', 'fixture/gold_validation.json'):
            with self.assertRaises(ValueError): val.pin_path(path)

    def test_both_input_and_gold_load_require_authorization(self):
        with patch.object(val, 'read') as reader:
            for fake in ({}, {'release_sha256': 'not-verified'}):
                with self.assertRaises(ValueError): val.load_inputs(fake)
                with self.assertRaises(ValueError): val.load_gold(fake, [])
            reader.assert_not_called()

    def test_verified_ticket_cannot_be_fabricated_or_mutated_after_authorize(self):
        with self.assertRaises(ValueError): val.VerifiedRelease({'release_sha256': 'not-verified'})
        with tempfile.TemporaryDirectory() as temporary:
            path, _, patches = self.auth_fixture(temporary)
            with self.auth_context(patches):
                ticket = val.authorize(path, panel.digest_file(path))
                val.require_verified_release(ticket)
                ticket['authorized'] = False
                with patch.object(val, 'read') as reader:
                    with self.assertRaises(ValueError): val.load_inputs(ticket)
                    reader.assert_not_called()

    def test_regex_valid_jev_alias_and_bogus_snapshot_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = val.materialize_plan([toy_case()], toy_release(), Path(temporary) / 'prepared')
            entry = next(x for x in report['batches'] if x['instrument'] == 'jev')
            manifest = val.read(Path(temporary) / 'prepared' / entry['manifest'])
            val.preflight_manifest_identity(manifest)
            for change in ({'model_aliases': [val.jev.MODEL, 'typesafe/jev-1.13-20990101']},
                           {'endpoint_snapshot_hash': 'bogus'}):
                altered = deepcopy(manifest); altered.update(change)
                # Demonstrate the old parser's syntactic manifest validator accepts
                # these shapes, but released wrapper pins genuine public identity.
                val.jev.validate_manifest(altered)
                with self.assertRaises(ValueError): val.preflight_manifest_identity(altered)

    def test_manifest_public_identity_guard_precedes_sealed_source_load(self):
        with tempfile.TemporaryDirectory() as temporary:
            d = Path(temporary)
            report = val.materialize_plan([toy_case()], toy_release(), d / 'prepared')
            entry = next(x for x in report['batches'] if x['instrument'] == 'jev')
            path = d / 'prepared' / entry['manifest']
            manifest = val.read(path); manifest['endpoint_snapshot_hash'] = 'bogus'
            path.write_bytes(safe.canonical(manifest))
            with patch.object(val, 'authorize', return_value=toy_release()), patch.object(val, 'load_inputs') as sealed:
                with self.assertRaises(ValueError): val.evaluate('RELEASE.json', 'pin', path, d / 'run', d / 'score')
                sealed.assert_not_called()

    def test_direct_recipes_exclude_formal_queries_and_future_prefix(self):
        case = toy_case(); before = deepcopy(case)
        rows, _ = val.expected_gpt([case], 'supplied_edge_judgment')
        self.assertEqual([r['id'] for r in rows], ['toy_q1', 'toy_q2'])
        payload = safe.parse_json(rows[0]['body']['messages'][1]['content'])
        self.assertEqual([t['id'] for t in payload['turns']], ['t1'])
        self.assertNotIn('Sprostowanie', rows[0]['body']['messages'][1]['content'])
        self.assertIn('Return one JSON object exactly', rows[0]['body']['messages'][0]['content'])
        jev_rows = val.expected_jev([case])
        self.assertEqual([r['id'] for r in jev_rows], ['toy_q1', 'toy_q2'])
        self.assertEqual(safe.parse_json(jev_rows[0]['body']['state']['text']), payload)
        self.assertEqual(case, before)

    def test_greedy_rows_cap_budget_and_original_order(self):
        rows = [{'id': f'q{i}', 'reservation_usd': '.003'} for i in range(50)]
        packed = val.greedy_batches(rows, '.10', 48)
        self.assertEqual([len(b) for b in packed], [33, 17])
        self.assertEqual([r for b in packed for r in b], rows)
        self.assertTrue(all(sum(Decimal(r['reservation_usd']) for r in b) <= Decimal('.10') for b in packed))
        tiny = [{'id': f'q{i}', 'reservation_usd': '.001'} for i in range(100)]
        self.assertEqual([len(b) for b in val.greedy_batches(tiny, '.10', 48)], [48, 48, 4])
        self.assertEqual([len(b) for b in val.greedy_batches(tiny[:25], '.10', 24)], [24, 1])

    def test_overcap_row_duplicate_and_unregistered_scope_fail_closed(self):
        for rows in ([{'id': 'x', 'reservation_usd': '.101'}],
                     [{'id': 'x', 'reservation_usd': '.001'}] * 2):
            with self.assertRaises(ValueError): val.greedy_batches(rows, '.10', 48)
        case = toy_case(); case['judgment_queries'][0]['scope'] = 'world_truth'
        with self.assertRaises(ValueError): val.explicit_cases([case])

    def prepared_fixture(self, directory, raw=None, cost='.002', state='completed', http_status=200):
        d = Path(directory); case, release = toy_case(), toy_release()
        report = val.materialize_plan([case], release, d / 'prepared-all')
        entry = next(e for e in report['batches'] if e['instrument'] == 'gpt' and e['track'] == 'supplied_edge_judgment')
        manifest_path = d / 'prepared-all' / entry['manifest']
        manifest = val.read(manifest_path); plan = safe.plan_manifest(manifest)
        run = d / 'run'; run.mkdir()
        response = fake_raw() if raw is None else raw
        a = {k: plan['requests'][0][k] for k in ('id', 'request_hash', 'reservation_usd')}
        a.update(state=state, http_status=http_status, response_file=a['id'] + '.response.bin',
                 response_sha256=hashlib.sha256(response).hexdigest(), elapsed_seconds=.3)
        if cost is not None:
            a['reported_cost_usd'] = cost
        (run / a['response_file']).write_bytes(response)
        (run / 'ledger.json').write_bytes(safe.canonical({'schema': 'loom.openrouter_ledger/1',
            'manifest_hash': plan['manifest_hash'], 'attempts': [a]}))
        return case, release, report, manifest_path, run

    def test_pure_packaging_separates_formal_arm_and_writes_once(self):
        with tempfile.TemporaryDirectory() as temporary:
            d = Path(temporary)
            with patch.object(val, 'load_gold') as gold:
                report = val.materialize_plan([toy_case()], toy_release(), d / 'prepared')
                gold.assert_not_called()
            self.assertEqual(report['explicit_query_count'], 2)
            self.assertEqual(report['formal_query_count'], 1)
            self.assertEqual(len(report['batches']), 3)
            formal = val.read(d / 'prepared/formal_query_inventory.json')
            self.assertEqual([t['id'] for t in formal[0]['turns']], ['t1'])
            self.assertEqual(formal[0]['query']['scope'], 'formal_implication')
            with self.assertRaises(FileExistsError): val.materialize_plan([toy_case()], toy_release(), d / 'prepared')

    def test_missing_attempt_keeps_planned_denominator_and_primary_class_counts(self):
        with tempfile.TemporaryDirectory() as temporary:
            case, release, _, manifest, run = self.prepared_fixture(temporary)
            outputs, summary = val.replay(manifest, run, [case], release)
            self.assertEqual([o['state'] for o in outputs], ['completed', 'unavailable'])
            self.assertEqual(summary['planned_requests'], 2)
            gold = [{'id': case['id'], 'judgments': [{'query_id': 'toy_q1', 'label': 'supported'},
                {'query_id': 'toy_q2', 'label': 'refuted'}, {'query_id': 'toy_formal', 'label': 'supported'}]}]
            score = val.score_primary([case], gold, outputs, 'supplied_edge_judgment')
            self.assertEqual(score['query_count'], 2)
            self.assertEqual(score['per_class']['refuted']['fn'], 1)
            self.assertEqual(score['split'], 'validation')
            self.assertIsNone(score['content_truth_accuracy'])

    def test_raw_ledger_billing_mismatch_remains_fatal(self):
        with tempfile.TemporaryDirectory() as temporary:
            case, release, _, manifest, run = self.prepared_fixture(temporary, cost='.000001')
            with self.assertRaises(integrity.BillingIntegrityError): val.replay(manifest, run, [case], release)

    def test_http_error_unknown_cost_keeps_reserve_instead_of_certifying_zero(self):
        with tempfile.TemporaryDirectory() as temporary:
            case, release, _, manifest, run = self.prepared_fixture(temporary,
                raw=safe.canonical({'error': {'message': 'synthetic400'}}), cost=None, state='http_error', http_status=400)
            outputs, summary = val.replay(manifest, run, [case], release)
            self.assertEqual(summary['missing_cost_attempts'], 1)
            self.assertGreater(Decimal(summary['unknown_attempt_reserved_usd']), 0)
            self.assertTrue(all(o['state'] == 'unavailable' for o in outputs))

    def test_wrong_provider_rejected_but_paid_cost_and_denominator_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            case, release, _, manifest, run = self.prepared_fixture(temporary, raw=fake_raw(provider='Other'))
            outputs, summary = val.replay(manifest, run, [case], release)
            self.assertEqual(outputs[0]['state'], 'unavailable')
            self.assertEqual(summary['reported_known_cost_usd'], '0.002')
            self.assertEqual(summary['planned_requests'], 2)

    def test_cannot_remove_unattempted_row_from_deterministic_batch(self):
        with tempfile.TemporaryDirectory() as temporary:
            case, release, _, manifest_path, run = self.prepared_fixture(temporary)
            manifest = val.read(manifest_path)
            manifest['requests'] = manifest['requests'][:1]
            plan = safe.plan_manifest(manifest)
            manifest_path.write_bytes(safe.canonical(manifest))
            ledger = val.read(run / 'ledger.json'); ledger['manifest_hash'] = plan['manifest_hash']
            (run / 'ledger.json').write_bytes(safe.canonical(ledger))
            with self.assertRaises(ValueError): val.replay(manifest_path, run, [case], release)

    def test_formal_output_cannot_enter_explicit_source_score(self):
        case = toy_case()
        with self.assertRaises(ValueError):
            val.score_primary([case], [], [{'query_id': 'toy_formal', 'state': 'completed', 'label': 'supported'}], 'supplied_edge_judgment')

    def test_validation_metadata_does_not_change_edge_span_or_primary_metric(self):
        case = toy_case(); turn = case['turns'][0]
        candidate = {'id': 'p1', 'relation': 'implies', 'source': 'A', 'target': 'B',
            'polarity': 'positive', 'attributed_to': 'writer', 'known_at': turn['known_at'],
            'evidence': [{'turn_id': 't1', 'quote': turn['text']}]}
        compiled = panel.compile_extraction({'source_assertions': [candidate], 'status_events': []}, case)
        edge = deepcopy(compiled['source_assertions'][0]); edge['id'] = 'g1'
        gold = [{'id': case['id'], 'family': 'synthetic', 'language': 'pl', 'source_assertions': [edge], 'status_events': []}]
        report = val.score_primary([case], gold, [compiled], 'assisted_extraction')
        self.assertEqual(report['strict_edges']['tp'], 1)
        self.assertEqual(report['strict_edges']['fp'], 0)
        self.assertEqual(report['split'], 'validation')
        span = compiled['source_assertions'][0]['evidence'][0]
        self.assertEqual(span['byte_end'], len(turn['text'].encode()))
        self.assertGreater(span['byte_end'], span['char_end'])


if __name__ == '__main__':
    unittest.main()
