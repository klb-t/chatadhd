"""Synthetic schema-hint ablation checks; no APIs, keys or real gold access."""
from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import graph_free_extraction as free, graph_panel_live as panel
    from . import graph_panel_score_run as integrity, openrouter_runner as safe
    from .test_graph_free_extraction import case, output, gold
except ImportError:
    import graph_free_extraction as free, graph_panel_live as panel
    import graph_panel_score_run as integrity, openrouter_runner as safe
    from test_graph_free_extraction import case, output, gold

# Capture the original module before importing the adapter: import-time mutation
# is as unacceptable as temporarily replacing globals during request creation.
ORIGINAL_SYSTEM = free.SYSTEM
ORIGINAL_CAPS = deepcopy(free.CAPS)
ORIGINAL_REQUESTS = free.requests
ORIGINAL_BATCHES = free.batches

try:
    from . import graph_free_schema_hint_v2 as hint
except ImportError:
    import graph_free_schema_hint_v2 as hint


def synthetic_cases(count=24):
    return [dict(case(), id=f'hint-synthetic-{index:02d}') for index in range(count)]


def pricing():
    return [{'model': panel.MODEL, 'provider': panel.PROVIDER,
             'pricing': {'prompt': '.0000004', 'completion': '.0000016'},
             'source_url': safe.API_ROOT + '/models/' + panel.MODEL + '/endpoints',
             'retrieved_at': '2026-09-30T00:00:00Z'}]


def identity_snapshot():
    return {'data': {'id': panel.MODEL, 'endpoints': [
        {'tag': panel.PROVIDER, 'status': 0, 'model_id': panel.MODEL,
         'provider_name': 'OpenAI'}]}}


def raw_response(model_object=None, content=None, cost=.002):
    if content is None:
        content = safe.canonical(output() if model_object is None else model_object).decode()
    return safe.canonical({'model': panel.MODEL, 'provider': 'OpenAI',
                           'usage': {'cost': cost, 'is_byok': False},
                           'choices': [{'finish_reason': 'stop',
                                        'message': {'content': content}}]})


class SchemaHintTests(unittest.TestCase):
    def prepare_synthetic(self, directory, cases):
        """Only mocked pricing and synthetic source packets enter preparation."""
        def pricing_read(path):
            self.assertEqual(Path(path), free.ROOT /
                'docs/research/graph_method_panel_v1/extraction/prepared/manifest.json')
            return {'pricing_evidence': pricing()}

        with patch.object(panel, 'load_dev_inputs', return_value=deepcopy(cases)), \
             patch.object(panel, 'load_dev_gold', side_effect=AssertionError('gold read during preparation')), \
             patch.object(integrity, 'read', side_effect=pricing_read):
            report = hint.prepare(directory)
        return report

    def replay_fixture(self, directory, raw, ledger_cost='.002'):
        root = Path(directory)
        c = case()
        prepared = root / 'prepared'
        self.prepare_synthetic(prepared, [c])
        manifest_path = prepared / 'batch01/prepared/manifest.json'
        manifest = safe.parse_json(manifest_path.read_bytes())
        plan = safe.plan_manifest(manifest)
        request = plan['requests'][0]
        attempt = {key: request[key] for key in ('id', 'request_hash', 'reservation_usd')}
        attempt.update(state='completed', http_status=200,
                       response_file=c['id'] + '.response.bin',
                       response_sha256=hashlib.sha256(raw).hexdigest(),
                       reported_cost_usd=ledger_cost, elapsed_seconds=.5)
        run = root / 'run'
        run.mkdir()
        (run / attempt['response_file']).write_bytes(raw)
        (run / 'ledger.json').write_bytes(safe.canonical({
            'schema': 'loom.openrouter_ledger/1', 'manifest_hash': plan['manifest_hash'],
            'attempts': [attempt]}))
        return c, manifest_path, run

    def load_replay(self, manifest, run, cases):
        read = integrity.read

        def synthetic_snapshot(path):
            if Path(path) == integrity.SNAPSHOT:
                return identity_snapshot()
            return read(path)

        with patch.object(integrity, 'read', side_effect=synthetic_snapshot):
            return hint.load_run(manifest, run, cases)

    def test_only_system_suffix_changes_model_payload(self):
        cases = synthetic_cases(2)
        before = deepcopy(cases)
        original, variant = free.requests(cases), hint.requests(cases)
        self.assertEqual(hint.SYSTEM, ORIGINAL_SYSTEM + hint.HINT_SUFFIX)
        self.assertTrue(hint.HINT_SUFFIX)
        self.assertEqual(len(variant), len(original))
        for c, base, changed in zip(cases, original, variant):
            expected = deepcopy(base)
            expected['body']['messages'][0]['content'] = hint.SYSTEM
            expected['reservation_usd'] = safe.estimate_reservation(expected['body'])['minimum_reservation_usd']
            self.assertEqual(changed, expected)
            payload = safe.parse_json(changed['body']['messages'][1]['content'])
            self.assertEqual(payload, free.source_payload(c))
            self.assertEqual(set(payload), {'id', 'source_id', 'turns'})
            self.assertNotIn('never-in-body', changed['body']['messages'][1]['content'])
            self.assertNotIn('node_inventory', changed['body']['messages'][1]['content'])
        self.assertEqual(cases, before)

    def test_original_module_remains_unmodified(self):
        with tempfile.TemporaryDirectory() as temporary:
            hint.requests(synthetic_cases())
            self.prepare_synthetic(Path(temporary) / 'prepared', synthetic_cases())
        self.assertEqual(free.SYSTEM, ORIGINAL_SYSTEM)
        self.assertEqual(free.CAPS, ORIGINAL_CAPS)
        self.assertIs(free.requests, ORIGINAL_REQUESTS)
        self.assertIs(free.batches, ORIGINAL_BATCHES)
        self.assertIs(hint.batches, ORIGINAL_BATCHES)
        self.assertEqual(free.ROW_LIMIT, 12)

    def test_hint_is_exact_parseable_symbolic_contract_example(self):
        start, finish = hint.HINT_SUFFIX.index('{'), hint.HINT_SUFFIX.rindex('}') + 1
        parsed = safe.parse_json(hint.HINT_SUFFIX[start:finish])
        self.assertEqual(parsed, hint.SCHEMA_HINT_OBJECT)
        self.assertEqual(set(parsed), {'nodes', 'source_assertions', 'status_events'})
        self.assertEqual([n['id'] for n in parsed['nodes']], ['node1', 'node2'])
        self.assertEqual([n['text'] for n in parsed['nodes']], ['proposition1', 'proposition2'])
        self.assertTrue(all(set(node) == {'id', 'text'} for node in parsed['nodes']))
        assertions, events = parsed['source_assertions'], parsed['status_events']
        self.assertEqual(len(assertions), 2)
        self.assertEqual(len(events), 1)
        self.assertEqual(len({a['id'] for a in assertions}), 2)
        node_ids = {n['id'] for n in parsed['nodes']}
        for assertion in assertions:
            self.assertEqual(set(assertion), {'id', 'relation', 'source', 'target',
                'polarity', 'attributed_to', 'known_at', 'evidence'})
            self.assertIn(assertion['source'], node_ids)
            self.assertIn(assertion['target'], node_ids)
            self.assertEqual(assertion['attributed_to'], 'speaker')
            self.assertIn(assertion['known_at'], {'time1', 'time2'})
        self.assertEqual(events[0]['status'], 'superseded')
        self.assertEqual(set(events[0]), {'assertion_id', 'status', 'superseded_by', 'known_at', 'evidence'})
        self.assertIn(events[0]['known_at'], {'time1', 'time2'})
        self.assertNotEqual(events[0]['assertion_id'], events[0]['superseded_by'])
        self.assertEqual({events[0]['assertion_id'], events[0]['superseded_by']},
                         {a['id'] for a in assertions})
        for record in assertions + events:
            self.assertTrue(record['evidence'])
            self.assertTrue(all(isinstance(e, dict) and set(e) == {'turn_id'}
                                for e in record['evidence']))
            self.assertEqual(len({e['turn_id'] for e in record['evidence']}), len(record['evidence']))
            self.assertTrue(all(e['turn_id'] in {'turn1', 'turn2'} for e in record['evidence']))
        for text in ('lampa', 'żaba', 'lis', 'synthetic:free-mechanism', 'never-in-body'):
            self.assertNotIn(text, hint.HINT_SUFFIX)
        # The parser must reject duplicated keys instead of accepting a repaired
        # interpretation of the exemplar or model source objects.
        with self.assertRaises(ValueError):
            safe.parse_json('{"nodes":[],"nodes":[],"source_assertions":[],"status_events":[]}')

    def test_deterministic_twenty_four_cases_form_two_capped_batches(self):
        cases = synthetic_cases()
        rows = hint.requests(cases)
        packed = hint.batches(rows)
        self.assertEqual(rows, hint.requests(deepcopy(cases)))
        self.assertEqual([len(batch) for batch in packed], [12, 12])
        self.assertEqual([row for batch in packed for row in batch], rows)
        for batch in packed:
            self.assertLessEqual(sum(Decimal(row['reservation_usd']) for row in batch), Decimal('.10'))
        with tempfile.TemporaryDirectory() as temporary:
            report = self.prepare_synthetic(Path(temporary) / 'prepared', cases)
            self.assertEqual([len(b['request_ids']) for b in report['batches']], [12, 12])
            self.assertEqual(report['cases'], 24)
            for index, batch in enumerate(report['batches'], 1):
                manifest = safe.parse_json((Path(temporary) / 'prepared' / batch['manifest']).read_bytes())
                self.assertEqual(manifest['experiment_id'], f'graph-dev-free-schema-hint-v2-{index:02d}')
                meta = manifest['metadata']
                self.assertEqual(meta['track'], free.TRACK)
                self.assertEqual(meta['variant'], 'free_schema_hint_v2')
                self.assertEqual(meta['base_system_sha256'], hashlib.sha256(ORIGINAL_SYSTEM.encode()).hexdigest())
                self.assertEqual(meta['hint_suffix_sha256'], hashlib.sha256(hint.HINT_SUFFIX.encode()).hexdigest())
                self.assertEqual(meta['system_sha256'], hashlib.sha256(hint.SYSTEM.encode()).hexdigest())
                self.assertEqual(meta['recipe_sha256'], meta['system_sha256'])
                self.assertEqual(meta['original_free_sha256'], hashlib.sha256(Path(free.__file__).read_bytes()).hexdigest())
                self.assertFalse(meta['gold_read_during_preparation'])

    def test_manifest_drift_is_rejected_before_any_ledger_read(self):
        cases = synthetic_cases()
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'prepared'
            self.prepare_synthetic(directory, cases)
            manifest_path = directory / 'batch01/prepared/manifest.json'
            pristine = safe.parse_json(manifest_path.read_bytes())
            variants = {}
            old = deepcopy(pristine)
            old['requests'] = free.batches(free.requests(cases))[0]
            old['experiment_id'] = 'graph-dev-free-extract-01'
            old['metadata'].pop('variant')
            variants['original_v1'] = old
            shortened = deepcopy(pristine)
            shortened['requests'] = shortened['requests'][:-1]
            variants['shortened'] = shortened
            reordered = deepcopy(pristine)
            reordered['requests'].reverse()
            variants['reordered'] = reordered
            body = deepcopy(pristine)
            body['requests'][0]['body']['messages'][1]['content'] += ' '
            variants['body_tamper'] = body
            for name in ('recipe_sha256', 'base_system_sha256', 'hint_suffix_sha256',
                         'system_sha256', 'original_free_sha256'):
                changed = deepcopy(pristine)
                changed['metadata'][name] = '0' * 64
                variants[name] = changed
            ident = deepcopy(pristine)
            ident['experiment_id'] = 'graph-dev-free-schema-hint-v2-02'
            variants['wrong_batch_identity'] = ident
            for name, changed in variants.items():
                with self.subTest(name=name):
                    reads = []
                    def manifest_only(path):
                        reads.append(Path(path))
                        self.assertEqual(Path(path), manifest_path, 'read artifacts before manifest rejection')
                        return deepcopy(changed)
                    with patch.object(integrity, 'read', side_effect=manifest_only):
                        with self.assertRaises(ValueError):
                            hint.load_run(manifest_path, Path(temporary) / 'absent-run', cases)
                    self.assertEqual(reads, [manifest_path])

    def test_actual_raw_response_compiles_without_reference_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            c, manifest, run = self.replay_fixture(temporary, raw_response())
            compiled, summary = self.load_replay(manifest, run, [c])
            self.assertEqual(compiled, [free.compile_free(output(), free.source_payload(c))])
            self.assertEqual(compiled[0]['source_assertions'][0]['source'], 'x')
            self.assertEqual(compiled[0]['source_assertions'][0]['evidence'][0]['quote'], c['turns'][0]['text'])
            self.assertEqual(summary['reported_known_cost_usd'], '0.002')
            self.assertEqual(summary['compiled_complete'], 1)

    def test_freeze_rejects_attempt_or_response_before_any_fixture_read(self):
        for artifact in ('ledger.json', 'hint-synthetic-00.response.bin'):
            with self.subTest(artifact=artifact), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                nested = directory / 'batch01/run'
                nested.mkdir(parents=True)
                (nested / artifact).write_bytes(b'{}')
                with patch.object(integrity, 'read', side_effect=AssertionError('read after freeze became ineligible')):
                    with self.assertRaises(ValueError):
                        hint.freeze_record(directory)

    def test_billing_mismatch_is_fatal_instead_of_unavailable(self):
        with tempfile.TemporaryDirectory() as temporary:
            c, manifest, run = self.replay_fixture(temporary, raw_response(), '.000001')
            with self.assertRaises(integrity.BillingIntegrityError):
                self.load_replay(manifest, run, [c])

    def test_malformed_evidence_strings_remain_invalid_record_false_positives(self):
        value = output()
        value['source_assertions'][0]['evidence'] = ['t1']
        with tempfile.TemporaryDirectory() as temporary:
            c, manifest, run = self.replay_fixture(temporary, raw_response(value))
            compiled, summary = self.load_replay(manifest, run, [c])
            self.assertEqual(summary['compiled_complete'], 1)
            self.assertEqual(compiled[0]['invalid_assertions'], 1)
            self.assertEqual(compiled[0]['raw_model_object'], value)
            score = free.score_free([c], [gold()], compiled)
            self.assertEqual(score['strict_edges'], panel._metric(0, 1, 1))

    def test_duplicate_source_keys_become_unavailable_without_repair(self):
        content = safe.canonical(output()).decode().replace('"source":"x"', '"source":"x","source":"y"')
        self.assertIn('"source":"x","source":"y"', content)
        with tempfile.TemporaryDirectory() as temporary:
            c, manifest, run = self.replay_fixture(temporary, raw_response(content=content))
            compiled, summary = self.load_replay(manifest, run, [c])
            self.assertEqual(compiled[0]['state'], 'unavailable')
            self.assertNotIn('source_assertions', compiled[0])
            self.assertEqual(summary['compiled_complete'], 0)
            self.assertEqual(summary['reported_known_cost_usd'], '0.002')

    def test_first_compiled_and_summary_persist_before_gold_and_never_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            c, manifest, run = self.replay_fixture(temporary, raw_response())
            target = Path(temporary) / 'scored'
            original_read = integrity.read
            def read_with_snapshot(path):
                return identity_snapshot() if Path(path) == integrity.SNAPSHOT else original_read(path)
            def load_synthetic_gold():
                self.assertTrue((target / 'compiled_first.json').is_file())
                self.assertTrue((target / 'execution_summary.json').is_file())
                self.assertFalse((target / 'score_first.json').exists())
                persisted = safe.parse_json((target / 'compiled_first.json').read_bytes())
                self.assertEqual(persisted[0]['source_assertions'][0]['source'], 'x')
                return [gold()]
            with patch.object(panel, 'load_dev_inputs', return_value=[c]), \
                 patch.object(panel, 'load_dev_gold', side_effect=load_synthetic_gold) as gold_loader, \
                 patch.object(integrity, 'read', side_effect=read_with_snapshot):
                summary, score = hint.evaluate(manifest, run, target)
                self.assertEqual(score['strict_edges'], panel._metric(1, 0, 0))
                self.assertEqual(summary['compiled_complete'], 1)
                before = {p.name: p.read_bytes() for p in target.iterdir()}
                with self.assertRaises(FileExistsError):
                    hint.evaluate(manifest, run, target)
                self.assertEqual(gold_loader.call_count, 1)
                self.assertEqual(before, {p.name: p.read_bytes() for p in target.iterdir()})


if __name__ == '__main__':
    unittest.main()
