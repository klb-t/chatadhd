"""N3 real FFI/store boundary, immutable receipts, CAS and atomic drift checks."""
from copy import deepcopy
import ctypes
import hashlib
import json
import os
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
        self.assertIn('not_performed', receipt['reversible_history_validation'])
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
