import json
from pathlib import Path
import tempfile
import unittest
from loom.tools.resource_graph import ResourceGraph, ResourceError
from loom.tools.structure.agentic_graph_v1.packet import decode_packet, encode_packet


class CoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'profile.json'
        self.path.write_text(json.dumps({'temperature': 0.25, 'future': {'name': 'unknown', 'empty': [], 'null': None}}))
    def tearDown(self):
        self.tmp.cleanup()
    def graph(self, **kwargs):
        graph = ResourceGraph(**kwargs)
        graph.attach(self.path, logical_id='profile')
        return graph
    def test_reference_is_addressable_without_io_or_native(self):
        graph = self.graph()
        self.path.unlink()
        packet = graph.reference_packet('profile')
        self.assertEqual(packet['task']['materialized_fields'], 0)
        self.assertEqual(graph.metrics['opens'], 0)
        self.assertEqual(graph.describe('profile')['status'], 'unloaded')
        with self.assertRaises(ResourceError) as caught:
            graph.select('profile', '/temperature')
        self.assertEqual(caught.exception.status, 'unavailable')
        self.assertEqual(graph.reference_packet('profile')['entities'][0]['attrs']['status'], 'unavailable')
    def test_project_fragment_preserves_unknown_and_null_with_existing_codec(self):
        graph = self.graph()
        self.assertEqual(graph.select('profile', '/future/name'), 'unknown')
        packet = graph.project('profile', '/future', depth=2)
        self.assertEqual(decode_packet(encode_packet(packet)), packet)
        fields = [e for e in packet['entities'] if e['kind'] == 'resource_field']
        self.assertEqual({e['attrs']['selector'] for e in fields}, {'/future','/future/name','/future/empty','/future/null'})
        self.assertNotIn('/temperature', {e['attrs']['selector'] for e in fields})
        self.assertEqual(graph.select('profile', '/future/null'), None)
    def test_live_changed_source_and_snapshot_reference_detects_drift(self):
        graph = self.graph()
        first = graph.project('profile', '/temperature')
        self.path.write_text('{"temperature":0.75}')
        second = graph.project('profile', '/temperature')
        self.assertNotEqual(first['task']['source_revision'], second['task']['source_revision'])
        self.assertEqual(graph.describe('profile')['logical_id'], 'profile')
        self.assertEqual(len(graph.describe('profile')['history']), 2)
        snap = self.graph(policy={'mode':'snapshot'})
        self.assertEqual(snap.select('profile', '/temperature'), .75)
        self.path.write_text('{"temperature":0.95}')
        with self.assertRaisesRegex(ResourceError, 'snapshot_source_changed'):
            snap.select('profile', '/temperature')
    def test_inline_and_reference_recognize_same_values(self):
        graph = self.graph()
        graph.attach_inline(self.path.read_bytes(), logical_id='inline', format='json')
        self.assertEqual(graph.select('profile'), graph.select('inline'))
        self.assertNotEqual(graph.describe('profile')['logical_id'], graph.describe('inline')['logical_id'])
        self.assertEqual(graph.describe('profile')['content_sha256'], graph.describe('inline')['content_sha256'])
    def test_embedding_independent_of_snapshot_cache_and_index(self):
        graph = self.graph(policy={'mode':'snapshot', 'cache':False, 'index':False})
        graph.capture('profile')
        self.path.unlink()
        self.assertEqual(graph.select('profile', '/temperature'), .25)
        self.assertEqual(graph.search_index('profile','temperature')['status'], 'unloaded')
        self.assertTrue(graph.export_reference('profile', include_bytes=True)['content_base64'])
    def test_cache_retention_is_explicit_and_expired_unavailable_is_not_empty(self):
        now = [0]
        graph = self.graph(policy={'cache':True,'retention_seconds':10}, clock=lambda:now[0])
        self.assertEqual(graph.select('profile', '/temperature'), .25)
        self.path.unlink()
        now[0] = 5
        self.assertEqual(graph.select('profile','/temperature'), .25)
        self.assertEqual(graph.metrics['cache_hits'],1)
        now[0] = 11
        with self.assertRaises(ResourceError): graph.select('profile')
    def test_partial_index_and_budget_are_explicit(self):
        graph = self.graph(policy={'cache':True,'retention_seconds':100,'index':True})
        graph.select('profile','/future/name')
        result=graph.search_index('profile','unknown')
        self.assertEqual(result['selectors'], ['/future/name'])
        self.assertEqual(result['coverage'], 'requested_fragments_only')
        packet=graph.project('profile',depth=5,limit=2)
        self.assertEqual(packet['task']['status'], 'partial')
        self.assertEqual(packet['task']['materialized_fields'], 2)
    def test_empty_corrupt_unsupported_remain_distinct(self):
        graph=self.graph()
        self.path.write_text('{}')
        self.assertEqual(graph.select('profile'),{})
        self.assertEqual(graph.describe('profile')['status'],'empty')
        self.path.write_text('{')
        with self.assertRaises(ResourceError) as caught: graph.select('profile')
        self.assertEqual(caught.exception.status,'corrupt')
        graph.attach(self.path,logical_id='unknown',format='unrecognized')
        with self.assertRaises(ResourceError): graph.select('unknown')
    def test_permissions_and_credential_locator(self):
        graph=ResourceGraph()
        graph.attach(self.path,logical_id='private',permissions={'read':False})
        with self.assertRaises(ResourceError): graph.select('private')
        graph.attach(self.path,logical_id='noexport',permissions={'export_values':False})
        with self.assertRaises(ResourceError): graph.project('noexport')
        self.assertEqual(graph.select('noexport','/temperature'),.25)
        for url in ('https://user:pass@example.org/x', 'https://example.org/x?token=secret'):
            with self.assertRaises(ValueError): graph.attach(url,logical_id=url)
    def test_adapter_registration_extends_structure_without_core_change(self):
        class Handle:
            def select(self,pointer): return {'new':{'shape':42}} if not pointer else 42
            def children(self,pointer,offset,limit): return [('new/shape',42)][offset:offset+limit]
            def metadata(self): return {'source_version':'v1','content_sha256':None,'parser_version':'custom/1','status':'partial'}
        class Adapter:
            def open(self,resource,access): return Handle()
        graph=ResourceGraph()
        graph.attach('custom:opaque',logical_id='new',adapter='extension')
        self.assertEqual(graph.describe('new')['status'],'unloaded')
        graph.register_adapter('extension',Adapter())
        self.assertEqual(graph.select('new','/new/shape'),42)
        packet=graph.project('new',depth=1)
        self.assertEqual(packet['task']['materialized_fields'],2)
        self.assertEqual(graph.children('new')[0]['selector'],'/new~1shape')
    def test_new_config_consumer_reads_selected_source_without_activation(self):
        from engine.config import Config
        graph=self.graph()
        consumer=Config(self.path)
        self.assertEqual(consumer.get('temperature'),graph.select('profile','/temperature'))
        proposal=graph.overlay('profile','/temperature',0.99)
        self.assertEqual(proposal['status'],'proposed')
        self.assertEqual(consumer.get('temperature'),.25)
