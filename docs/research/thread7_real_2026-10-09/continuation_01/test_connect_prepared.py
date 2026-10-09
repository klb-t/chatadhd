"""Synthetic mechanics only; actual real preparation gets a separate receipt."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('connect_prepared', Path(__file__).with_name('connect_prepared.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class ConnectionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='thread7-connection-mechanics-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.prepared, self.corpus = self.root/'prepared', self.root/'corpus'
        self.prepared.mkdir(); self.corpus.mkdir()
        self.plan = json.loads(Path(__file__).with_name('connection-plan.json').read_text())
        self.plan['scopes'] = self.plan['scopes'][:1]
        self.rows = []
        sources = []
        for index, provider in enumerate(self.plan['provider_order']):
            family = 'mechanical-' + provider
            sources.append({'family_id': family, 'provider': provider, 'split': 'tuning',
                            'features': {'text_chars': index+1}, 'raw_sha256': str(index+1)*64})
            for variant in self.plan['scopes'][0]['variants']:
                body = {'model': 'mechanics/model', 'provider': {'only': ['openai'], 'allow_fallbacks': False},
                        'max_tokens': 8, 'messages': [{'role':'system','content':'mechanics fixture'},
                          {'role':'user','content':json.dumps({'context': {'variant':variant, 'family':family}})}]}
                raw = c.workflow.canonical(body)
                name = 'requests/' + c.sha(raw) + '.json'
                c.write(self.prepared/name, raw)
                self.rows.append({'family_id': family, 'phase': 'answer', 'variant': variant,
                                  'body_ready': True, 'status': 'prepared', 'body_path':name,
                                  'body_sha256': c.sha(raw), 'preparation_id': c.sha(raw),
                                  'task_id': 'mechanics-task', 'source_sha256': str(index+1)*64})
        c.dump(self.corpus/'panel.json', {'sources':sources})
        self.plan['corpus_panel_sha256'] = c.sha((self.corpus/'panel.json').read_bytes())
        self.freeze()

    def freeze(self):
        (self.prepared/'jobs-index.jsonl').write_bytes(b'\n'.join(c.workflow.canonical(x) for x in self.rows))
        manifest = {'files': {str(p.relative_to(self.prepared)):c.sha(p.read_bytes())
                             for p in self.prepared.rglob('*') if p.is_file() and p.name!='MANIFEST.json'}}
        (self.prepared/'MANIFEST.json').write_bytes(c.workflow.canonical(manifest))
        self.plan['preparation_manifest_sha256'] = c.sha((self.prepared/'MANIFEST.json').read_bytes())

    def run_connect(self):
        return c.connect(self.prepared, self.corpus, self.root/'output', self.plan)

    def test_exact_bodies_queue_and_existing_payer_manifest(self):
        receipt = self.run_connect()[0]
        self.assertEqual((receipt['families'], receipt['variants'], receipt['queued_operations']), (2,3,6))
        self.assertFalse(receipt['dispatch_ready'])
        target = self.root/'output'/self.plan['scopes'][0]['id']
        c.payer_manifest.validate_manifest(c.read(target/'payer/manifest.json'),base_dir=target/'payer')
        operations=c.read(target/'payer/manifest.json')['operations']
        self.assertEqual([o['metadata']['queue_ordinal'] for o in operations],list(range(6)))
        q = c.workflow.Queue(target/'queue.sqlite'); self.addCleanup(q.db.close)
        self.assertEqual(len(q.snapshot()),6)
        self.assertTrue(all(x['state']=='prepared' for x in q.snapshot()))
        self.assertEqual(q.actual_order(),[])

    def test_tampered_source_is_rejected(self):
        (self.prepared/self.rows[0]['body_path']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'frozen_payload_changed'): self.run_connect()

    def test_missing_dependency_cannot_enter_queue(self):
        self.rows[0].update(body_ready=False,status='pending_inference')
        self.freeze()
        with self.assertRaisesRegex(ValueError,'pending_preparation'): self.run_connect()

    def test_ambiguous_mapping_is_rejected(self):
        self.rows.append(deepcopy(self.rows[0]));self.freeze()
        with self.assertRaisesRegex(ValueError,'selection_ambiguous'): self.run_connect()

    def test_changed_manifest_binding_is_rejected(self):
        self.plan['preparation_manifest_sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'preparation_version_mismatch'): self.run_connect()

    def test_validation_cannot_silently_replace_tuning(self):
        panel=c.read(self.corpus/'panel.json')
        panel['sources'][0]['split']='provisional_validation'
        (self.corpus/'panel.json').write_bytes(c.workflow.canonical(panel))
        self.plan['corpus_panel_sha256']=c.sha((self.corpus/'panel.json').read_bytes())
        with self.assertRaisesRegex(ValueError,'tuning_provider_unavailable'): self.run_connect()

    def test_multicall_primary_is_not_a_completed_single_call_variant(self):
        self.plan['scopes'][0]['variants']=[{**self.plan['scopes'][0]['variants'][0],
                                         'response_form':'text_then_structure'}]
        with self.assertRaisesRegex(ValueError,'multi_call_variant_requires_dependency_executor'): self.run_connect()


if __name__ == '__main__':
    unittest.main()
