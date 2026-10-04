"""N3 real FFI/store boundary, immutable receipts, CAS and atomic drift checks."""
from copy import deepcopy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import unittest

from compat_common import CompatTestCase
from loom.tools.coordination.graph_store import GraphStoreError, NativeGraphStore
from loom.tools.structure.agentic_graph_v1 import packet as codec
from loom.tools.structure.agentic_graph_v1.test_packet import fixture, MODEL, AUTO, rehash


class GraphPacketStoreTests(CompatTestCase):
    def setUp(self):
        super().setUp()
        library = os.environ.get('LOOM_LIBRARY')
        if not library:
            self.skipTest('LOOM_LIBRARY not set (build with LOOM_SHARED=ON)')
        self.data = self.tmp.path / 'graph-store'
        self.store = NativeGraphStore(library, self.data)
        self.addCleanup(self.store.close)
        self.packet = fixture()
        self.selection = {name: [codec.record_id(name, row) for row in self.packet[name]]
                          for name in ('entities', 'claims', 'sources')}
        self.expected = {name: {id_: None for id_ in ids} for name, ids in self.selection.items()}

    def accept(self, packet=None, **changes):
        arguments = {'target': 'synthetic-review', 'selection': self.selection,
                     'expected_rows': self.expected, 'explicitly_accepted': True}
        arguments.update(changes)
        return self.store.accept(packet or self.packet, **arguments)

    def database(self):
        return sqlite3.connect(self.data / 'chatadhd.db')

    def test_native_compiled_reply_history_and_model_sources_survive_acceptance(self):
        fixtures = json.loads((Path(__file__).resolve().parents[2] /
                               'src/packet/tests/reply-fixtures.json').read_text())['cases']
        library = self.store.library
        library.loom_packet.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        library.loom_packet.restype = ctypes.c_void_p
        for i, fixture in enumerate(fixtures):
            with self.subTest(fixture=i):
                compiled = self.store._take(library.loom_packet(
                    self.store.context, json.dumps(fixture['request'], ensure_ascii=False).encode()))
                self.assertEqual(compiled, fixture['expected'])
                applied = self.store._take(library.loom_packet(self.store.context, json.dumps(
                    {'operation': 'apply_compiled_reply', 'packet': fixture['request']['packet'],
                     'compilation': compiled, 'policy': AUTO}, ensure_ascii=False).encode()))
                self.assertNotIn('error', applied)
                packet = applied['packet']
                selection = {name: [codec.record_id(name, row) for row in packet[name]]
                             for name in ('entities', 'claims', 'sources')}
                expected = {name: {id_: None for id_ in ids} for name, ids in selection.items()}
                accepted = self.store.execute({'operation': 'accept', 'target': 'compiled-' + str(i),
                    'packet': packet, 'selection': selection, 'expected_rows': expected,
                    'explicitly_accepted': True})
                receipt = accepted['receipt']
                self.assertEqual(receipt['packet'], packet)
                self.assertEqual(self.store.replay(receipt['id'])['receipt'], receipt)
                self.assertEqual(self.store.read(receipt['id'])['receipt']['packet']['sources'],
                                 packet['sources'])
                for source in compiled['diff']['sources']['add']:
                    self.assertEqual(source['observation']['attrs']['model_origin']['kind'], 'model')

    def test_versioned_method_graph_reply_bindings_survive_native_store_replay(self):
        fixture = json.loads((Path(__file__).resolve().parents[2] /
                              'src/packet/tests/method-graph-fixture.json').read_text())
        contract = fixture['contract']
        bindings = contract['bindings']
        predicates = contract['vocabulary']['predicates']
        expected = fixture['expected']
        library = self.store.library
        library.loom_packet.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        library.loom_packet.restype = ctypes.c_void_p

        def execute(request):
            return self.store._take(library.loom_packet(
                self.store.context, json.dumps(request, ensure_ascii=False).encode()))

        base = execute(fixture['base_make_request'])
        self.assertEqual(base['packet_id'], expected['base_packet_id'])
        compiled = execute({'operation': 'compile_reply', 'packet': base,
                            **fixture['reply_request']})
        self.assertEqual(compiled['node_ids'], expected['node_ids'])
        self.assertEqual(compiled['compilation_sha256'], expected['compilation_sha256'])
        self.assertEqual(compiled['response_text'], expected['response_text'])
        reply = execute({'operation': 'apply_compiled_reply', 'packet': base,
                         'compilation': compiled, 'policy': fixture['apply_policy']})['packet']
        self.assertEqual(reply['packet_id'], expected['reply_packet_id'])
        applied = execute({'operation': 'apply', 'packet': reply,
                           'diff': fixture['binding_diff'], 'policy': fixture['apply_policy']})
        packet = applied['packet']
        self.assertFalse(applied['receipt']['acceptance_establishes_content_truth'])
        self.assertEqual(packet['packet_id'], expected['bound_packet_id'])
        self.assertEqual(execute({'operation': 'validate', 'packet': packet}), packet)
        self.assertEqual(execute({'operation': 'invert', 'packet': packet,
                                  'receipt': applied['receipt']}), reply)

        entities = {row['id']: row for row in packet['entities']}
        claims = {row['id']: row for row in packet['claims']}
        sources = {row['observation']['id']: row for row in packet['sources']}
        for name in ('method_version', 'recipe_version', 'preset_version', 'combination_version',
                     'parameter_set_version'):
            attrs = entities[bindings[name + '_id']]['attrs']
            self.assertEqual(codec.digest(attrs['definition']), attrs['definition_sha256'])
        prompt = entities[bindings['prompt_version_id']]['attrs']
        self.assertEqual(hashlib.sha256(prompt['text'].encode()).hexdigest(),
                         contract['definition_hashes']['prompt_bytes'])
        trace = entities[bindings['run_id']]['attrs']
        for name in ('effective_parameters', 'user_overrides', 'parameter_set_version_id',
                     'parameter_set_sha256', 'preset_sha256',
                     'combination_sha256', 'prompt_sha256', 'recipe_sha256', 'measurements'):
            self.assertEqual(trace[name], contract['trace'][name])
        self.assertEqual(trace['projection_status'], 'response_projected')
        self.assertEqual(trace['response_sha256'], compiled['raw_capture']['sha256'])
        self.assertIsNone(trace['measurements']['accuracy'])
        parameters = entities[bindings['parameter_set_version_id']]['attrs']
        self.assertEqual(parameters['definition']['effective_parameters'], trace['effective_parameters'])
        self.assertEqual(parameters['definition']['user_overrides'], trace['user_overrides'])
        self.assertEqual(parameters['definition_sha256'], trace['parameter_set_sha256'])
        self.assertEqual(entities[bindings['method_version_id']]['attrs']['definition']['parameter_set_sha256'],
                         trace['parameter_set_sha256'])
        self.assertEqual(entities[bindings['recipe_version_id']]['attrs']['definition']['parameters'],
                         trace['effective_parameters'])
        for subject, role, target in (
                (bindings['method_version_id'], 'uses_parameter_set', bindings['parameter_set_version_id']),
                (bindings['run_id'], 'uses_parameter_set', bindings['parameter_set_version_id']),
                (bindings['run_id'], 'uses_combination', bindings['combination_version_id'])):
            edges = [c for c in claims.values()
                     if c['subject'] == subject and c['predicate'] == predicates[role]]
            self.assertEqual([edge['object'] for edge in edges], [target])
        captured_definitions = json.loads(sources[expected['source_ids'][0]]['observation']['text'])
        self.assertEqual(captured_definitions['definition_records'], contract['definition_records'])
        for role, attrs in contract['definition_records'].items():
            self.assertEqual(entities[bindings[role]]['attrs'], attrs)
        for result_id in compiled['node_ids'].values():
            for role, target in (('produced_in_run', bindings['run_id']),
                                 ('produced_by_method_version', bindings['method_version_id']),
                                 ('projected_by_compiler', bindings['compiler_transform_id'])):
                edges = [c for c in claims.values()
                         if c['subject'] == result_id and c['predicate'] == predicates[role]]
                self.assertEqual([edge['object'] for edge in edges], [target])
                self.assertEqual(edges[0]['qualifiers']['extra']['confidence_scope'], 'structure_only')
            origin = entities[result_id]['attrs']['model_origin']
            self.assertEqual(origin['kind'], 'model')
            self.assertEqual(origin['recipe_sha256'], trace['recipe_sha256'])
        for source in compiled['diff']['sources']['add']:
            self.assertEqual(sources[source['observation']['id']], source)
        evaluation = claims[expected['evaluation_claim_id']]
        self.assertEqual(evaluation['subject'], bindings['method_version_id'])
        self.assertEqual(evaluation['qualifiers']['valid_from'], evaluation['value']['evaluated_at'])
        self.assertEqual(evaluation['value']['measurement_status'], 'unavailable')
        self.assertEqual(evaluation['assessment']['origin'], 'model_knowledge')
        self.assertEqual(evaluation['qualifiers']['extra']['model_origin']['kind'], 'model')
        self.assertIsNone(evaluation['qualifiers']['extra']['model_origin']['recipe_sha256'])
        self.assertIsNone(sources[expected['source_ids'][-1]]['observation']['attrs']['model_origin']['recipe_sha256'])
        self.assertEqual(evaluation['qualifiers']['extra']['content_verification'], 'unverified')
        self.assertEqual(evaluation['assessment']['basis']['support'][0]['observation'],
                         expected['source_ids'][-1])

        bad = deepcopy(fixture['binding_diff'])
        bad['claims']['add'][0]['object'] = 'missing-run-target'
        rejected = execute({'operation': 'apply', 'packet': reply, 'diff': bad,
                            'policy': fixture['apply_policy']})
        self.assertIn('error', rejected)
        self.assertEqual(rejected['error']['message'], 'graph_packet_unknown_claim_entity')
        self.assertEqual(execute({'operation': 'validate', 'packet': reply}), reply)

        selection = {name: [codec.record_id(name, row) for row in packet[name]]
                     for name in ('entities', 'claims', 'sources')}
        accepted = self.store.execute({'operation': 'accept', 'target': 'method-graph',
            'packet': packet, 'selection': selection,
            'expected_rows': {name: {id_: None for id_ in ids} for name, ids in selection.items()},
            'explicitly_accepted': True})['receipt']
        self.assertEqual(accepted['packet'], packet)
        self.assertFalse(accepted['acceptance_establishes_content_truth'])
        self.store.close()
        self.store = NativeGraphStore(os.environ['LOOM_LIBRARY'], self.data)
        self.addCleanup(self.store.close)
        for result in (self.store.read(accepted['id']), self.store.replay(accepted['id'])):
            self.assertEqual(result['receipt'], accepted)
            self.assertTrue(result['row_drift']['matches'])

    def test_full_packet_history_and_metadata_survive_accept_read_replay_restart(self):
        diff = codec.empty_diff(self.packet, proposal_id='review', origin=MODEL)
        after = deepcopy(self.packet['claims'][0]); after['assessment']['status'] = 'contested'
        diff['claims']['update'].append({'id': after['id'],
            'before_sha256': codec.digest(self.packet['claims'][0]), 'after': after})
        packet, _ = codec.apply_diff(self.packet, diff, AUTO)
        result = self.accept(packet)
        receipt = result['receipt']
        self.assertEqual(receipt['packet'], packet)
        self.assertFalse(receipt['acceptance_establishes_content_truth'])
        self.assertEqual(receipt['reversible_history_validation'],
                         'native_codec_backwards_and_forwards_history_replay')
        self.assertFalse(result['replayed'])
        self.assertEqual(self.accept(packet)['receipt'], receipt)
        with self.database() as db:
            self.assertEqual(db.execute("SELECT value FROM _meta WHERE key='schema_version'").fetchone()[0], '4')
            self.assertEqual(db.execute("SELECT value FROM loom_kb_meta WHERE key='schema_version'").fetchone()[0], '3')
            claim = json.loads(db.execute('SELECT body FROM loom_kb_claims WHERE run_id=?',
                                         (receipt['run_id'],)).fetchone()[0])
            self.assertEqual(claim, packet['claims'][0])
        self.store.close()
        self.store = NativeGraphStore(os.environ['LOOM_LIBRARY'], self.data)
        self.addCleanup(self.store.close)
        self.assertEqual(self.store.read(receipt['id'])['receipt'], receipt)
        self.assertEqual(self.store.replay(receipt['id'])['receipt'], receipt)

    def test_retry_preserves_exact_receipt_without_a_second_insertion(self):
        result = self.accept()
        receipt = result['receipt']
        with self.database() as db:
            body = db.execute('SELECT body FROM loom_kb_graph_receipts WHERE id=?',
                              (receipt['id'],)).fetchone()[0]
        retry = self.accept()
        self.assertTrue(retry['replayed'])
        self.assertEqual(retry['receipt'], receipt)
        with self.database() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_graph_receipts').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_runs').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT body FROM loom_kb_graph_receipts WHERE id=?',
                                        (receipt['id'],)).fetchone()[0], body)

    def test_legacy_receipt_keeps_original_validation_scope_on_read_replay_and_retry(self):
        # Reconstruct a synthetic pre-upgrade receipt table before its immutable
        # triggers are installed. The native rows/snapshots and request identity
        # are the same as today; only the historical validation receipt differs.
        legacy = deepcopy(self.accept()['receipt'])
        legacy['native_validation_scope'] = (
            'packet_head_and_provenance_hashes_selected_native_rows_entity_claim_'
            'observation_reference_closure_source_hashes_quotes_utf8_subspans')
        legacy['reversible_history_validation'] = (
            'not_performed_by_native_adapter_use_graph_packet_codec')
        legacy.pop('packet_codec_validation', None)
        legacy.pop('receipt_sha256')
        legacy['receipt_sha256'] = codec.digest(legacy)
        original_body = json.dumps(legacy, ensure_ascii=False, indent=2)
        self.store.close()
        with self.database() as db:
            db.execute('DROP TABLE loom_kb_graph_receipts')
            db.execute('CREATE TABLE loom_kb_graph_receipts '
                       '(id TEXT PRIMARY KEY, run_id TEXT NOT NULL, body TEXT NOT NULL)')
            db.execute('INSERT INTO loom_kb_graph_receipts (id,run_id,body) VALUES (?,?,?)',
                       (legacy['id'], legacy['run_id'], original_body))
        self.store = NativeGraphStore(os.environ['LOOM_LIBRARY'], self.data)
        self.addCleanup(self.store.close)
        for result in (self.store.read(legacy['id']), self.store.replay(legacy['id']), self.accept()):
            self.assertEqual(result['receipt'], legacy)
            self.assertNotIn('packet_codec_validation', result['receipt'])
            self.assertTrue(result['row_drift']['matches'])
        with self.database() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_graph_receipts').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_runs').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT body FROM loom_kb_graph_receipts WHERE id=?',
                                        (legacy['id'],)).fetchone()[0], original_body)

    def test_utf8_subspan_projection_does_not_rewrite_quote_or_locator(self):
        packet = deepcopy(self.packet)
        support = packet['claims'][0]['assessment']['basis']['support'][0]
        text = packet['sources'][0]['observation']['text'].encode('utf-8')
        support['quote'] = 'lampa zgaśnie'
        support['locator']['byte_start'] += text.index(support['quote'].encode('utf-8'))
        support['locator']['byte_len'] = len(support['quote'].encode('utf-8'))
        packet['provenance']['claims']['cl_test']['record_sha256'] = codec.digest(packet['claims'][0])
        rehash(packet)
        self.assertEqual(self.accept(packet)['receipt']['packet'], packet)

    def test_object_key_order_is_not_native_content_loss_or_identity_change(self):
        def permuted(value):
            if isinstance(value, dict):
                return {key: permuted(value[key]) for key in reversed(list(value))}
            if isinstance(value, list):
                return [permuted(item) for item in value]
            return value

        original = deepcopy(self.packet)
        original['claims'][0]['qualifiers']['extra']['ordered'] = [1, 2]
        original['provenance']['claims']['cl_test']['record_sha256'] = codec.digest(original['claims'][0])
        rehash(original)
        packet = permuted(original)
        receipt = self.accept(packet)['receipt']
        self.assertEqual(receipt['packet'], packet)
        self.assertEqual(list(receipt['packet']['entities'][0]), list(packet['entities'][0]),
                         'submitted packet object member order is retained in the receipt')
        self.assertEqual(receipt['packet']['sources'][0]['observation']['text'],
                         self.packet['sources'][0]['observation']['text'])
        changed_request = self.accept(original, expected_rows=receipt['stored_row_sha256'])
        self.assertEqual(changed_request['receipt']['packet'], original,
                         'CAS preserves immutable semantic identity across member ordering')
        for ordered in ([2, 1], [1, 2, 3]):
            altered = deepcopy(original)
            altered['claims'][0]['qualifiers']['extra']['ordered'] = ordered
            altered['provenance']['claims']['cl_test']['record_sha256'] = codec.digest(altered['claims'][0])
            rehash(altered)
            with self.assertRaisesRegex(GraphStoreError, 'new record identity'):
                self.accept(altered, expected_rows=changed_request['receipt']['stored_row_sha256'])
        self.assertEqual(self.store.replay(changed_request['receipt']['id'])['receipt'],
                         changed_request['receipt'], 'array changes cannot replace accepted content')

    def test_member_order_tolerance_still_rejects_discarded_fields(self):
        for collection in ('entities', 'sources'):
            with self.subTest(collection=collection):
                packet = deepcopy(self.packet)
                row = packet[collection][0]
                native = row['observation'] if collection == 'sources' else row
                native['must_not_be_discarded'] = {'retained_bytes': 'synthetic'}
                packet['provenance'][collection][native['id']]['record_sha256'] = codec.digest(row)
                rehash(packet)
                # Bypass the Python DTO gate deliberately to exercise the
                # native boundary, as any HTTP/C ABI caller can do.
                request = {'operation': 'accept', 'target': 'synthetic-discard-check', 'packet': packet,
                           'selection': self.selection, 'expected_rows': self.expected,
                           'explicitly_accepted': True}
                result = self.store._take(self.store.library.loom_graph_packet_store(
                    self.store.context, json.dumps(request, ensure_ascii=False).encode('utf-8')))
                self.assertIn('projection would discard or change fields', result['error']['message'])

    def test_oversized_unsigned_ordinal_rejects_without_native_writes(self):
        for ordinal in (1 << 31, 1 << 63, (1 << 64) - 1):
            with self.subTest(ordinal=ordinal):
                packet = deepcopy(self.packet)
                packet['sources'][0]['observation']['ordinal'] = ordinal
                packet['provenance']['sources']['ob_test']['record_sha256'] = codec.digest(packet['sources'][0])
                rehash(packet)
                request = {'operation': 'accept', 'target': 'synthetic-unsigned-loss', 'packet': packet,
                           'selection': self.selection, 'expected_rows': self.expected,
                           'explicitly_accepted': True}
                with self.assertRaisesRegex(GraphStoreError, 'projection would discard or change fields'):
                    self.store.execute(request)
                with self.database() as db:
                    for table in ('loom_kb_observations', 'loom_kb_entities', 'loom_kb_claims',
                                  'loom_kb_runs', 'loom_kb_graph_receipts'):
                        if db.execute("SELECT 1 FROM sqlite_master WHERE name=?", (table,)).fetchone():
                            self.assertEqual(db.execute('SELECT COUNT(*) FROM ' + table).fetchone()[0], 0)

    def test_integer_to_float_precision_loss_rejects_without_native_writes(self):
        for time in ((1 << 53) + 1, -((1 << 53) + 1), (1 << 64) - 1):
            with self.subTest(time=time):
                packet = deepcopy(self.packet)
                for locator in (packet['sources'][0]['observation']['locator'],
                                packet['claims'][0]['assessment']['basis']['support'][0]['locator']):
                    locator['time_start'] = locator['time_end'] = time
                for collection in ('sources', 'claims'):
                    for row in packet[collection]:
                        packet['provenance'][collection][codec.record_id(collection, row)]['record_sha256'] = codec.digest(row)
                rehash(packet)
                request = {'operation': 'accept', 'target': 'synthetic-double-loss', 'packet': packet,
                           'selection': self.selection, 'expected_rows': self.expected,
                           'explicitly_accepted': True}
                with self.assertRaisesRegex(GraphStoreError, 'projection would discard or change fields'):
                    self.store.execute(request)
                with self.database() as db:
                    self.assertFalse(db.execute("SELECT 1 FROM sqlite_master WHERE name='loom_kb_graph_receipts'").fetchone())

    def test_lossless_numeric_normalization_preserves_large_json_attrs(self):
        packet = deepcopy(self.packet)
        packet['entities'][0]['confidence'] = 1  # Native DTO faithfully emits 1.0.
        packet['entities'][0]['attrs']['exact_numbers'] = [
            (1 << 64) - 1, -(1 << 63), (1 << 53) + 1, {'one': 1, 'true': True}]
        packet['sources'][0]['observation']['ordinal'] = (1 << 31) - 1
        for collection in ('sources', 'entities'):
            for row in packet[collection]:
                packet['provenance'][collection][codec.record_id(collection, row)]['record_sha256'] = codec.digest(row)
        rehash(packet)
        receipt = self.accept(packet)['receipt']
        self.assertEqual(receipt['packet'], packet)
        with self.database() as db:
            entity = json.loads(db.execute('SELECT body FROM loom_kb_entities WHERE run_id=? AND id=?',
                                           (receipt['run_id'], 'e_test_a')).fetchone()[0])
            source = json.loads(db.execute('SELECT body FROM loom_kb_observations WHERE run_id=? AND id=?',
                                           (receipt['run_id'], 'ob_test')).fetchone()[0])
        self.assertEqual(entity['confidence'], 1.0)
        self.assertEqual(entity['attrs']['exact_numbers'], packet['entities'][0]['attrs']['exact_numbers'])
        self.assertEqual(source['ordinal'], (1 << 31) - 1)

    def test_missing_or_changed_or_index_only_drift_blocks_replay_and_retry(self):
        receipt = self.accept()['receipt']
        with self.database() as db:
            db.execute("UPDATE loom_kb_entities SET canonical_key='drift' WHERE run_id=? AND id='e_test_a'",
                       (receipt['run_id'],))
        report = self.store.read(receipt['id'])
        self.assertFalse(report['row_drift']['matches'])
        self.assertEqual(report['row_drift']['rows'][0]['reason'], 'changed')
        with self.assertRaises(GraphStoreError): self.store.replay(receipt['id'])
        with self.assertRaises(GraphStoreError): self.accept()
        with self.database() as db:
            db.execute("UPDATE loom_kb_entities SET canonical_key='lampa' WHERE run_id=? AND id='e_test_a'",
                       (receipt['run_id'],))
            db.execute("DELETE FROM loom_kb_claims WHERE run_id=?", (receipt['run_id'],))
        self.assertEqual(self.store.read(receipt['id'])['row_drift']['rows'][0]['reason'], 'missing')
        with self.assertRaises(GraphStoreError): self.store.replay(receipt['id'])
        with self.database() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_claims').fetchone()[0], 0)

    def test_receipt_updates_and_deletes_are_rejected_by_database(self):
        receipt = self.accept()['receipt']
        with self.database() as db:
            with self.assertRaises(sqlite3.IntegrityError):
                db.execute("UPDATE loom_kb_graph_receipts SET body='{}' WHERE id=?", (receipt['id'],))
            with self.assertRaises(sqlite3.IntegrityError):
                db.execute('DELETE FROM loom_kb_graph_receipts WHERE id=?', (receipt['id'],))
            with self.assertRaises(sqlite3.IntegrityError):
                db.execute('INSERT OR REPLACE INTO loom_kb_graph_receipts (id,run_id,body) VALUES (?,?,?)',
                           (receipt['id'], receipt['run_id'], '{}'))
        self.assertEqual(self.store.replay(receipt['id'])['receipt'], receipt)

    def test_explicit_acceptance_and_closed_selection_are_required(self):
        with self.assertRaises(ValueError): self.accept(explicitly_accepted=False)
        omitted = deepcopy(self.selection); omitted['entities'].remove('e_test_b')
        expected = deepcopy(self.expected); del expected['entities']['e_test_b']
        with self.assertRaises(GraphStoreError): self.accept(selection=omitted, expected_rows=expected)
        with self.database() as db:
            self.assertFalse(db.execute("SELECT 1 FROM sqlite_master WHERE name='loom_kb_graph_receipts'").fetchone())

    def test_cas_mismatch_rolls_back_new_packet_and_receipt(self):
        receipt = self.accept()['receipt']
        packet = deepcopy(self.packet)
        packet['task']['revised'] = True
        rehash(packet)
        with self.assertRaises(GraphStoreError): self.accept(packet)
        with self.database() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_graph_receipts').fetchone()[0], 1)
        next_result = self.accept(packet, expected_rows=receipt['stored_row_sha256'])
        self.assertNotEqual(next_result['receipt']['id'], receipt['id'])
        self.assertEqual(self.store.replay(receipt['id'])['receipt'], receipt)

    def test_late_sql_error_rolls_back_rows_run_and_receipt(self):
        # Create schema and its immutability trigger in an independent target.
        self.accept(target='baseline')
        for table in ('loom_kb_claims', 'loom_kb_graph_receipts'):
            with self.subTest(failure=table):
                with self.database() as db:
                    db.execute('CREATE TRIGGER synthetic_fail_write BEFORE INSERT ON ' + table +
                               " BEGIN SELECT RAISE(ABORT, 'injected write failure'); END")
                with self.assertRaises(GraphStoreError): self.accept(target='atomic-failure-' + table)
                with self.database() as db:
                    self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_runs').fetchone()[0], 1)
                    self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_observations').fetchone()[0], 1)
                    self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_entities').fetchone()[0], 2)
                    self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_claims').fetchone()[0], 1)
                    self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_graph_receipts').fetchone()[0], 1)
                    db.execute('DROP TRIGGER synthetic_fail_write')

    def test_direct_abi_rejects_bad_head_source_hash_and_quote_without_python_validation(self):
        for mutation in ('head', 'hash', 'quote', 'span'):
            with self.subTest(mutation=mutation):
                packet = deepcopy(self.packet)
                if mutation == 'head': packet['packet_id'] = '0' * 64
                elif mutation == 'hash': packet['sources'][0]['text_sha256'] = '0' * 64
                elif mutation == 'quote': packet['claims'][0]['assessment']['basis']['support'][0]['quote'] = 'invented'
                else: packet['claims'][0]['assessment']['basis']['support'][0]['locator']['byte_start'] += 1
                if mutation != 'head':
                    for name in codec.COLLECTIONS:
                        for row in packet[name]:
                            packet['provenance'][name][codec.record_id(name, row)]['record_sha256'] = codec.digest(row)
                    rehash(packet)
                request = {'operation': 'accept', 'target': mutation, 'packet': packet,
                           'selection': self.selection, 'expected_rows': self.expected, 'explicitly_accepted': True}
                with self.assertRaises(GraphStoreError): self.store.execute(request)
        result = self.store._take(self.store.library.loom_graph_packet_store(None, None))
        self.assertEqual(result['error']['code'], 'invalid_argument')

    def test_python_caller_rejects_forged_reversible_history_before_native_write(self):
        packet = deepcopy(self.packet)
        packet['history'] = [{'forged': True}]
        rehash(packet)
        with self.assertRaises(ValueError): self.accept(packet)

    def test_direct_abi_rejects_rehashed_corrupt_history_without_any_native_writes(self):
        diff = codec.empty_diff(self.packet, proposal_id='native-history', origin=MODEL)
        after = deepcopy(self.packet['claims'][0])
        after['assessment']['status'] = 'contested'
        diff['claims']['update'].append({'id': after['id'],
            'before_sha256': codec.digest(self.packet['claims'][0]), 'after': after})
        valid, _ = codec.apply_diff(self.packet, diff, AUTO)
        for mutation, error in (
                ('event_hash', 'graph_packet_history_hash_drift'),
                ('parent', 'graph_packet_history_parent_hash_mismatch'),
                ('forward', 'graph_packet_history_forward_replay_mismatch')):
            with self.subTest(mutation=mutation):
                packet = deepcopy(valid)
                event = packet['history'][0]
                if mutation == 'event_hash':
                    event['application_id'] = '0' * 64
                elif mutation == 'parent':
                    event['previous_task']['fabricated_earlier_task'] = True
                else:
                    event['diff']['claims']['update'][0]['after']['assessment']['status'] = 'rejected'
                if mutation != 'event_hash':
                    event['application_id'] = codec.digest(
                        {key: value for key, value in event.items() if key != 'application_id'})
                # Rehash the outer head too: rejection must come from full
                # history replay, not the existing packet-head hash gate.
                rehash(packet)
                request = {'operation': 'accept', 'target': 'corrupt-history-' + mutation,
                           'packet': packet, 'selection': self.selection,
                           'expected_rows': self.expected, 'explicitly_accepted': True}
                # Bypass acceptance_request/Python validation, just like a
                # direct HTTP or C ABI client supplying an exchange packet.
                result = self.store._take(self.store.library.loom_graph_packet_store(
                    self.store.context, json.dumps(request, ensure_ascii=False).encode('utf-8')))
                self.assertEqual(result['error']['code'], 'invalid_argument')
                self.assertIn(error, result['error']['message'])
                with self.database() as db:
                    for table in ('loom_kb_observations', 'loom_kb_entities', 'loom_kb_claims',
                                  'loom_kb_runs', 'loom_kb_graph_receipts'):
                        if db.execute('SELECT 1 FROM sqlite_master WHERE name=?', (table,)).fetchone():
                            self.assertEqual(db.execute('SELECT COUNT(*) FROM ' + table).fetchone()[0], 0)

    def test_v2_to_v3_migration_preserves_existing_knowledge_and_core_v4(self):
        receipt = self.accept(target='v2-existing')['receipt']
        with self.database() as db:
            # Recreate the exact relevant v2 situation: all knowledge rows,
            # no N3 table, independent KB schema version 2.
            before = db.execute('SELECT * FROM loom_kb_claims ORDER BY run_id,id').fetchall()
            db.execute('DROP TABLE loom_kb_graph_receipts')
            db.execute("UPDATE loom_kb_meta SET value='2' WHERE key='schema_version'")
            db.execute("UPDATE _meta SET value='2' WHERE key='loom_kb_schema_version'")
        accepted = self.accept(target='new-v3')['receipt']
        with self.database() as db:
            self.assertEqual(db.execute('SELECT * FROM loom_kb_claims WHERE run_id=? ORDER BY run_id,id',
                                        (receipt['run_id'],)).fetchall(), before)
            self.assertEqual(db.execute("SELECT value FROM _meta WHERE key='schema_version'").fetchone()[0], '4')
            self.assertEqual(db.execute("SELECT value FROM loom_kb_meta WHERE key='schema_version'").fetchone()[0], '3')
        self.assertEqual(self.store.replay(accepted['id'])['receipt'], accepted)

    def test_cas_cannot_rewrite_immutable_source_or_entity_and_claim_identity(self):
        receipt = self.accept()['receipt']
        for mutation in ('source', 'entity', 'claim'):
            with self.subTest(mutation=mutation):
                packet = deepcopy(self.packet)
                if mutation == 'source':
                    packet['sources'][0]['observation']['text'] += ' retained source change'
                    packet['sources'][0]['text_sha256'] = hashlib.sha256(
                        packet['sources'][0]['observation']['text'].encode('utf-8')).hexdigest()
                elif mutation == 'entity': packet['entities'][0]['canonical_key'] = 'different-identity'
                else: packet['claims'][0]['predicate'] = 'different-content'
                for name in codec.COLLECTIONS:
                    for row in packet[name]:
                        packet['provenance'][name][codec.record_id(name, row)]['record_sha256'] = codec.digest(row)
                rehash(packet)
                with self.assertRaisesRegex(GraphStoreError, 'new record identity'):
                    self.accept(packet, expected_rows=receipt['stored_row_sha256'])
        self.assertEqual(self.store.replay(receipt['id'])['receipt'], receipt)

    def test_existing_owner_judgement_remains_authoritative_after_later_acceptance(self):
        library = self.store.library
        library.loom_kb_judge.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        library.loom_kb_judge.restype = ctypes.c_void_p
        judgement = {'target_kind': 'claim', 'target': 'cl_test', 'verdict': 'reject',
                     'payload': {}, 'reason': 'synthetic owner decision'}
        result = self.store._take(library.loom_kb_judge(self.store.context, json.dumps(judgement).encode()))
        self.assertNotIn('error', result)
        receipt = self.accept()['receipt']
        self.assertEqual(receipt['packet']['claims'][0]['assessment']['status'], 'active')
        actual = json.loads(receipt['row_snapshots']['claims']['cl_test']['row'][0]['body'])
        self.assertEqual(actual['assessment']['status'], 'rejected')
        self.assertEqual(receipt['judgement_replay']['applied'], 1)
        packet = deepcopy(self.packet); packet['task']['later'] = True; rehash(packet)
        later = self.accept(packet, expected_rows=receipt['stored_row_sha256'])['receipt']
        actual = json.loads(later['row_snapshots']['claims']['cl_test']['row'][0]['body'])
        self.assertEqual(actual['assessment']['status'], 'rejected')
        self.assertEqual(later['judgement_replay']['applied'], 1)
        readback = self.store.read(later['id'])
        self.assertEqual(readback['row_drift']['current_row_snapshots'], later['row_snapshots'])

    def test_selection_imports_only_explicit_rows_and_reports_opaque_typed_references(self):
        selection = {'entities': ['e_test_a'], 'claims': [], 'sources': []}
        expected = {'entities': {'e_test_a': None}, 'claims': {}, 'sources': {}}
        selected = self.accept(selection=selection, expected_rows=expected)['receipt']
        self.assertEqual(selected['packet'], self.packet)
        with self.database() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_entities').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_claims').fetchone()[0], 0)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM loom_kb_observations').fetchone()[0], 0)
        packet = deepcopy(self.packet)
        packet['claims'][0]['assessment']['premises']['principles'] = ['pr_opaque']
        packet['provenance']['claims']['cl_test']['record_sha256'] = codec.digest(packet['claims'][0])
        rehash(packet)
        accepted = self.accept(packet, target='opaque-reference')['receipt']
        self.assertEqual(accepted['opaque_native_references'],
                         [{'claim': 'cl_test', 'kind': 'principle', 'id': 'pr_opaque'}])
        self.assertIn('not_resolved', accepted['opaque_native_reference_validation'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
