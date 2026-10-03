"""First32 execution controls through fixtures/mocks only; no model-quality claim."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
try:
    from . import recipe_live_pilot as p
    from ._test_support import private_temporary_directory
    from .test_jev_live_pilot import catalog
except ImportError:
    import recipe_live_pilot as p
    from _test_support import private_temporary_directory
    from test_jev_live_pilot import catalog


class HTTP:
    def __init__(self, response=None, failure=None, key_change=None, audit_status=200):
        self.calls = []
        self.response = response
        self.failure = failure
        self.key_change = key_change
        self.audit_status = audit_status
    def __call__(self, method, path, body, key):
        self.calls.append((method, path, body))
        if path == p.pilot.ENDPOINT:
            return 200, p.safe.canonical(catalog())
        if path == '/api/v1/key':
            data = {'limit': 2, 'limit_remaining': '1.5', 'limit_reset': None,
                    'is_management_key': False, 'include_byok_in_limit': False, 'byok_usage': 0}
            if self.key_change:
                self.key_change(data, len([c for c in self.calls if c[1] == path]))
            return 200, p.safe.canonical({'data': data})
        if method == 'POST':
            if self.failure:
                raise self.failure
            request = p.safe.parse_json(body)
            value = {'id': 'gen-recipes-unit', 'model': 'typesafe/jev-1.13-20260917', 'provider': 'TypeSafe',
                     'answers': {k: {'type': 'noul', 'noul': .9} for k in request['questions']},
                     'usage': {'input_tokens': 100, 'output_tokens': 10, 'cost': .0000042}}
            if self.response:
                return self.response(value)
            return 200, p.safe.canonical(value)
        return self.audit_status, p.safe.canonical({'data': {'id': 'gen-recipes-unit',
                    'model': 'typesafe/jev-1.13-20260917', 'provider_name': 'TypeSafe', 'api_type': 'decisions',
                    'is_byok': False, 'total_cost': .0000042, 'private_account_field': 'omit-this'}})
    def posts(self):
        return [c for c in self.calls if c[0] == 'POST']


class RecipeLiveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = private_temporary_directory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.run_dir = self.root / 'run'
        self.manifest = p.prepare(p.frozen_plan(), self.root / 'prepared',
                                 transport_fn=HTTP(), run_dir=self.run_dir)
    def run_pilot(self, http):
        return p.run(self.manifest, self.run_dir, transport_fn=http, key_loader=lambda: 'unit-secret')
    def offline_resume(self):
        def forbidden(*args):
            self.fail('resume must not load credentials or contact endpoints')
        return p.run(self.manifest, self.run_dir, transport_fn=forbidden, key_loader=forbidden)

    def test_exact_input_only_selection_and_wire_rule_order(self):
        m = p.pilot.read_json(self.root / 'prepared/manifest.json')
        p.validate_manifest(m)
        self.assertEqual(len(m['requests']), 32)
        self.assertEqual(m['total_reservation_usd'], '0.032')
        self.assertEqual(len({r['id'] for r in m['requests']}), 32)
        bodies, index = p.recipes.unpack(p.FIXTURE / 'requests.zip')
        wires = p.wire_bodies()
        for r in m['requests']:
            archived = p.safe.parse_json(bodies[r['source_file']])
            sent = p.safe.parse_json(wires[r['id']])
            provider = deepcopy(sent['provider']); provider.pop('max_price')
            sent['provider'] = provider
            self.assertEqual(sent, archived)
            self.assertEqual(p.hashlib.sha256(wires[r['id']]).hexdigest(), r['request_hash'])
            if r['arm'] == 'object_meaningful':
                self.assertEqual(list(sent['questions']['expressed']['instructions']), ['task', 'criterion', 'boundary'])
            for k in ('split', 'case_id', 'arm', 'labels', 'gold'):
                self.assertNotIn(k, sent['state'])

    def test_caps_and_frozen_manifest_reject_before_network(self):
        for mutate in (lambda m: m.update(batch_cap_usd='.031'),
                       lambda m: m.update(batch_cap_usd='.11'),
                       lambda m: m.update(global_key_cap_usd='2.01'),
                       lambda m: m['requests'].append(deepcopy(m['requests'][0])),
                       lambda m: m['requests'][0]['body']['state'].update(target_turn='changed')):
            m = deepcopy(self.manifest); mutate(m)
            http = HTTP()
            with self.assertRaises(p.safe.RunnerError):
                p.run(m, self.run_dir, transport_fn=http, key_loader=lambda: 'unit-secret')
            self.assertFalse(http.calls)
        oversized = deepcopy(self.manifest['requests'][0]['body'])
        oversized['state']['target_turn'] = 'x' * 20000
        with self.assertRaisesRegex(p.safe.RunnerError, 'allowance'):
            p.validate_body(oversized)
        expensive = catalog(); expensive['data']['endpoints'][0]['pricing']['prompt'] = '.000000043'
        with self.assertRaisesRegex(p.safe.RunnerError, 'price_exceeds'):
            p.pilot.endpoint_identity(expensive)

    def test_success_has_immutable_first_receipts_and_completed_resume(self):
        http = HTTP(audit_status=404)
        result = self.run_pilot(http)
        self.assertEqual(len(http.posts()), 32)
        self.assertEqual(len(result['attempts']), 32)
        self.assertTrue(all(r['state'] == 'completed' for r in result['attempts']))
        self.assertEqual(result['reported_cost_usd'], '0.0001344')
        self.assertEqual(result['attempts_with_unknown_cost'], 0)
        self.assertEqual(len(list(self.run_dir.glob('*.started.json'))), 32)
        self.assertEqual(len(list(self.run_dir.glob('*.result.json'))), 32)
        self.assertEqual(len([c for c in http.calls if 'generation?' in c[1]]), 32)
        self.assertEqual(len([c for c in http.calls if c[1] == '/api/v1/key']), 33)
        for path in self.run_dir.iterdir():
            self.assertNotIn(b'unit-secret', path.read_bytes())
            self.assertNotIn(b'omit-this', path.read_bytes())
        self.assertEqual(self.offline_resume(), result)
        with self.assertRaises(FileExistsError):
            p.write_new(self.run_dir / '00.started.json', {})

    def test_unknown_transport_stops_first_attempt_and_offline_resume(self):
        http = HTTP(failure=p.safe.RunnerError('unknown_network_outcome'))
        result = self.run_pilot(http)
        self.assertEqual(len(http.posts()), 1)
        self.assertEqual(result['attempts_with_unknown_cost'], 1)
        self.assertEqual(result['attempts'][0]['state'], 'uncertain')
        self.assertEqual(self.offline_resume(), result)

    def test_malformed_response_preserves_raw_known_charge_and_stops(self):
        def bad(value):
            value['answers'] = {}
            return 200, p.safe.canonical(value)
        http = HTTP(response=bad)
        result = self.run_pilot(http)
        self.assertEqual(len(http.posts()), 1)
        self.assertEqual(result['reported_cost_usd'], '0.0000042')
        self.assertEqual(result['attempts'][0]['state'], 'rejected')
        raw = self.run_dir / result['attempts'][0]['response_file']
        self.assertTrue(raw.exists())
        self.assertEqual(self.offline_resume(), result)
        raw.write_bytes(b'changed')
        with self.assertRaisesRegex(p.safe.RunnerError, 'hash_mismatch'):
            self.offline_resume()

    def test_unknown_usage_and_over_reservation_stop_without_retry(self):
        for response, known in ((lambda v: (200, b'{not-json'), None),
                                (lambda v: (503, p.safe.canonical(v)), '0.0000042'),
                                (lambda v: (200, p.safe.canonical({**v, 'usage': {**v['usage'], 'cost': .002}})), '0.002')):
            with self.subTest(known=known), tempfile.TemporaryDirectory() as folder:
                m = deepcopy(self.manifest); m['run_dir'] = str(Path(folder).resolve())
                http = HTTP(response=response)
                result = p.run(m, transport_fn=http, key_loader=lambda: 'unit-secret')
                self.assertEqual(len(http.posts()), 1)
                self.assertIn('stopped_reason', result)
                if known is None:
                    self.assertEqual(result['attempts_with_unknown_cost'], 1)
                else:
                    self.assertEqual(result['reported_cost_usd'], known)

    def test_interrupt_is_durable_and_never_replayed(self):
        http = HTTP(failure=KeyboardInterrupt())
        with self.assertRaises(KeyboardInterrupt):
            self.run_pilot(http)
        result = self.offline_resume()
        self.assertEqual(len(result['attempts']), 1)
        self.assertEqual(result['attempts'][0]['state'], 'uncertain')
        self.assertEqual(result['attempts_with_unknown_cost'], 1)
        self.assertEqual(self.offline_resume(), result)

    def test_terminal_receipt_recovers_without_network_after_checkpoint_crash(self):
        self.run_pilot(HTTP(response=lambda v: (503, p.safe.canonical(v))))
        ledger = p.pilot.read_json(self.run_dir / 'ledger.json')
        start = p.pilot.read_json(self.run_dir / '00.started.json'); start.pop('manifest_hash')
        ledger['attempts'][0] = start; ledger.pop('stopped_reason')
        p.safe._atomic(self.run_dir / 'ledger.json', p.safe.canonical(ledger))
        result = self.offline_resume()
        self.assertEqual(result['attempts'][0]['state'], 'rejected')
        self.assertEqual(result['reported_cost_usd'], '0.0000042')

    def test_ledger_deletion_or_new_path_cannot_restart_paid_attempt(self):
        self.run_pilot(HTTP(response=lambda v: (503, p.safe.canonical(v))))
        with self.assertRaisesRegex(p.safe.RunnerError, 'canonical_run_path'):
            p.run(self.manifest, self.root / 'new-dir', transport_fn=HTTP(), key_loader=lambda: 'unit-secret')
        (self.run_dir / 'ledger.json').unlink()
        with self.assertRaisesRegex(p.safe.RunnerError, 'stranded_evidence'):
            self.run_pilot(HTTP())

    def test_failed_immutable_receipt_restores_deleted_stop_latch(self):
        result = self.run_pilot(HTTP(response=lambda v: (503, p.safe.canonical(v))))
        checkpoint = deepcopy(result); checkpoint.pop('stopped_reason')
        p.safe._atomic(self.run_dir / 'ledger.json', p.safe.canonical(checkpoint))
        self.assertEqual(self.offline_resume(), result)

    def test_original_archive_pin_rejects_self_consistent_new_archive(self):
        bodies, index = p.recipes.unpack(p.FIXTURE / 'requests.zip')
        path = 'requests/r01.object_meaningful.json'
        body = p.safe.parse_json(bodies[path]); body['state']['target_turn'] += ' altered'
        bodies[path] = p.recipes.encode(body)
        for record in index['records']:
            if record['file'] == path:
                record['sha256'] = p.hashlib.sha256(bodies[path]).hexdigest()
        fake = self.root / 'fixture'; fake.mkdir()
        (fake / 'requests.zip').write_bytes(p.recipes.archive_bytes(bodies, index))
        with patch.object(p, 'FIXTURE', fake):
            with self.assertRaisesRegex(p.safe.RunnerError, 'original_archive_hash'):
                p.frozen_plan()

    def test_nonresetting_unknown_and_shared_remaining_cap_reject_before_paid(self):
        for change in (lambda d,n: d.pop('limit_reset'), lambda d,n: d.update(limit_reset='daily'),
                       lambda d,n: d.update(limit=2.01), lambda d,n: d.update(limit_remaining='.031'),
                       lambda d,n: d.update(byok_usage=.01)):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                m = deepcopy(self.manifest); m['run_dir'] = str(Path(folder).resolve())
                http = HTTP(key_change=change)
                with self.assertRaises(p.safe.RunnerError):
                    p.run(m, transport_fn=http, key_loader=lambda: 'unit-secret')
                self.assertEqual(len(http.posts()), 0)
        # External use of the shared key exhausts the remaining cap between calls.
        http = HTTP(key_change=lambda d,n: d.update(limit_remaining='0.030') if n >= 2 else None)
        with self.assertRaises(p.safe.RunnerError):
            self.run_pilot(http)
        self.assertEqual(len(http.posts()), 1)

    def test_get_only_preflight_no_attempt_state(self):
        http = HTTP()
        result = p.preflight(self.manifest, transport_fn=http, key_loader=lambda: 'unit-secret')
        self.assertTrue(result['nonresetting'])
        self.assertEqual([c[0] for c in http.calls], ['GET', 'GET'])
        self.assertFalse(self.run_dir.exists())

    def test_positive_audit_contradiction_and_excess_cost_stop_and_remain_saved(self):
        for change, reason in (({'provider_name': 'Other'}, 'identity_contradiction'),
                               ({'total_cost': '.002'}, 'cost_exceeds'),
                               ({'is_byok': True}, 'byok_detected')):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                m = deepcopy(self.manifest); m['run_dir'] = str(Path(folder).resolve())
                http = HTTP()
                def send(method, path, body, key):
                    status, raw = http(method, path, body, key)
                    if 'generation?' in path:
                        value = p.safe.parse_json(raw); value['data'].update(change)
                        raw = p.safe.canonical(value)
                    return status, raw
                result = p.run(m, transport_fn=send, key_loader=lambda: 'unit-secret')
                self.assertEqual(len(http.posts()), 1)
                self.assertIn(reason, result['stopped_reason'])
                self.assertEqual(result['reported_cost_usd'], '0.0000042')
                self.assertEqual(result['generation_reported_cost_usd'], str(p.safe._money(change.get('total_cost', '.0000042'))))

    def test_key_loader_requires_explicit_private_file_and_ignores_direct_key(self):
        with patch.dict(p.os.environ, {'OPENROUTER_API_KEY': 'must-not-be-used'}, clear=True):
            with self.assertRaisesRegex(p.safe.RunnerError, 'not_configured'):
                p.load_key()
        keyfile = self.root / 'credential'; keyfile.write_text('fixture-value'); keyfile.chmod(0o600)
        with patch.dict(p.os.environ, {'OPENROUTER_KEY_FILE': str(keyfile)}, clear=True):
            self.assertEqual(p.load_key(), 'fixture-value')
            keyfile.chmod(0o644)
            with self.assertRaisesRegex(p.safe.RunnerError, 'private_regular'):
                p.load_key()


if __name__ == '__main__':
    unittest.main()
