"""Connector identity regression fixtures; no owner data or transport."""
from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest

DIRECTORY = Path(__file__).resolve().parents[3] / 'docs/research/thread7_real_2026-10-09/continuation_01'
loader = importlib.util.spec_from_file_location('legacy_connector_fixture', DIRECTORY / 'test_connect_prepared.py')
legacy = importlib.util.module_from_spec(loader)
loader.loader.exec_module(legacy)
c = legacy.c


class PreparedConnectorBindingTests(unittest.TestCase):
    setUp = legacy.ConnectionTests.setUp
    freeze = legacy.ConnectionTests.freeze
    run_connect = legacy.ConnectionTests.run_connect

    def target(self, output='output'):
        return self.root / output / self.plan['scopes'][0]['id']

    def context_sources(self):
        panel = c.read(self.corpus / 'panel.json')
        contexts = []
        for source in panel['sources']:
            native = {'conversation': source['family_id'], 'sequence': 1,
                      'messages': [{'role': 'user', 'text': 'mechanics only'}]}
            source['raw_sha256'] = c.workflow.digest(native)
            normalized = {'family_id': source['family_id'], 'raw_sha256': source['raw_sha256'], 'native_conversation': native}
            source['path'] = 'conversations/' + source['family_id'] + '.json'
            c.dump(self.corpus / source['path'], normalized)
            source['normalized_sha256'] = c.sha((self.corpus / source['path']).read_bytes())
            context = {'family_id': source['family_id'], 'source_sha256': source['raw_sha256'], 'native_conversation': native}
            contexts.append(context)
            for row in self.rows:
                if row['family_id'] == source['family_id']:
                    row['source_sha256'] = c.workflow.digest(context)
        c.dump(self.prepared / 'sources.json', contexts)
        (self.corpus / 'panel.json').write_bytes(c.workflow.canonical(panel))
        self.plan['corpus_panel_sha256'] = c.sha((self.corpus / 'panel.json').read_bytes())
        self.freeze()
        return contexts, panel

    def test_mismatched_legacy_source_versions_rejected_before_queue(self):
        self.rows[0]['source_sha256'] = 'f' * 64
        self.freeze()
        with self.assertRaisesRegex(ValueError, '^preparation_source_version_mismatch$'):
            self.run_connect()
        self.assertFalse((self.target() / 'queue.sqlite').exists())

    def test_frozen_context_source_and_native_content_are_bound_separately(self):
        self.context_sources()
        self.run_connect()
        bindings = c.read(self.target() / 'analysis-bindings.json')
        spec = c.read(self.target() / 'spec.json')
        for item in bindings:
            case = next(v for v in spec['scope'] if v['family'] == item['family'])
            self.assertEqual(case['source_sha256'], item['source_sha256'])
            self.assertNotEqual(item['source_sha256'], item['preparation_source_sha256'])
            self.assertEqual(item['source_hash_domain'], 'canonical_native_conversation')
            self.assertTrue(item['source_content_verified'])
        self.assertEqual(spec['preparation_connector'], 'loom.thread7_prepared_connector/2')

    def test_wrong_context_source_digest_is_rejected(self):
        self.context_sources()
        self.rows[0]['source_sha256'] = 'f' * 64
        self.freeze()
        with self.assertRaisesRegex(ValueError, '^preparation_source_version_mismatch$'):
            self.run_connect()

    def test_frozen_context_with_changed_native_content_is_rejected(self):
        contexts, _ = self.context_sources()
        contexts[0]['native_conversation']['messages'][0]['text'] = 'different mechanics version'
        for row in self.rows:
            if row['family_id'] == contexts[0]['family_id']:
                row['source_sha256'] = c.workflow.digest(contexts[0])
        (self.prepared / 'sources.json').write_bytes(c.workflow.canonical(contexts))
        self.freeze()
        with self.assertRaisesRegex(ValueError, '^preparation_native_source_content_mismatch$'):
            self.run_connect()

    def test_frozen_source_for_other_native_version_is_rejected(self):
        contexts, _ = self.context_sources()
        contexts[0]['source_sha256'] = 'f' * 64
        for row in self.rows:
            if row['family_id'] == contexts[0]['family_id']:
                row['source_sha256'] = c.workflow.digest(contexts[0])
        (self.prepared / 'sources.json').write_bytes(c.workflow.canonical(contexts))
        self.freeze()
        with self.assertRaisesRegex(ValueError, '^preparation_original_source_version_mismatch$'):
            self.run_connect()

    def test_changed_corpus_normalized_bytes_are_rejected(self):
        _, panel = self.context_sources()
        (self.corpus / panel['sources'][0]['path']).write_bytes(b'{}')
        with self.assertRaisesRegex(ValueError, '^corpus_source_version_mismatch$'):
            self.run_connect()

    def test_native_canonical_identity_is_stronger_than_python_value_equality(self):
        _, panel = self.context_sources()
        selected = panel['sources'][0]
        path = self.corpus / selected['path']
        normalized = c.read(path)
        normalized['native_conversation']['sequence'] = 1.0
        path.write_bytes(c.workflow.canonical(normalized))
        selected['normalized_sha256'] = c.sha(path.read_bytes())
        (self.corpus / 'panel.json').write_bytes(c.workflow.canonical(panel))
        self.plan['corpus_panel_sha256'] = c.sha((self.corpus / 'panel.json').read_bytes())
        with self.assertRaisesRegex(ValueError, '^corpus_native_source_content_mismatch$'):
            self.run_connect()

    def test_equivalent_plan_object_order_preserves_ids_and_readd(self):
        self.run_connect()
        a = c.read(self.target() / 'queue-snapshot.json')
        plan = c.read(self.root / 'output/PLAN.json')
        self.assertEqual(plan, self.plan)
        c.connect(self.prepared, self.corpus, self.root / 'reload', plan)
        b = c.read(self.target('reload') / 'queue-snapshot.json')
        self.assertEqual([v['job']['operation_id'] for v in a], [v['job']['operation_id'] for v in b])
        queue = c.workflow.Queue(self.target() / 'queue.sqlite')
        self.addCleanup(queue.db.close)
        self.assertEqual(queue.add([v['job'] for v in b]), 0)
        self.assertEqual(len(queue.snapshot()), 6)

    def test_variant_list_order_remains_meaningful(self):
        self.run_connect()
        a = c.read(self.target() / 'payer/manifest.json')
        plan = deepcopy(self.plan)
        plan['scopes'][0]['variants'].reverse()
        c.connect(self.prepared, self.corpus, self.root / 'reordered', plan)
        b = c.read(self.target('reordered') / 'payer/manifest.json')
        self.assertNotEqual(a['metadata']['spec_sha256'], b['metadata']['spec_sha256'])
        self.assertNotEqual([v['request_sha256'] for v in a['operations']], [v['request_sha256'] for v in b['operations']])
        queue = c.workflow.Queue(self.target() / 'queue.sqlite')
        self.addCleanup(queue.db.close)
        with self.assertRaisesRegex(ValueError, 'queue_spec_identity_reused'):
            queue.add(v['job'] for v in c.read(self.target('reordered') / 'queue-snapshot.json'))
        self.assertEqual(len(queue.snapshot()), 6)

    def test_legacy_declared_hash_is_not_claimed_as_verified_source_content(self):
        self.run_connect()
        bindings = c.read(self.target() / 'analysis-bindings.json')
        self.assertTrue(all(not b['source_content_verified'] for b in bindings))

    def test_index_must_be_part_of_frozen_manifest(self):
        self.remove_frozen('jobs-index.jsonl')
        with self.assertRaisesRegex(ValueError, '^preparation_index_not_frozen$'):
            self.run_connect()

    def test_request_must_be_part_of_frozen_manifest(self):
        self.remove_frozen(self.rows[0]['body_path'])
        with self.assertRaisesRegex(ValueError, '^prepared_body_not_frozen$'):
            self.run_connect()

    def test_context_sources_must_be_part_of_frozen_manifest(self):
        self.context_sources()
        self.remove_frozen('sources.json')
        with self.assertRaisesRegex(ValueError, '^preparation_sources_not_frozen$'):
            self.run_connect()

    def remove_frozen(self, name):
        manifest = c.read(self.prepared / 'MANIFEST.json')
        del manifest['files'][name]
        (self.prepared / 'MANIFEST.json').write_bytes(c.workflow.canonical(manifest))
        self.plan['preparation_manifest_sha256'] = c.sha((self.prepared / 'MANIFEST.json').read_bytes())


if __name__ == '__main__':
    unittest.main()
