"""End-to-end composition and dynamic registration through the actual resource API."""
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from loom.tools.resource_graph import ResourceGraph, ResourceError
from loom.tools.resource_graph.access import Access
from loom.tools.resource_graph.core import SyntaxAdapter
from loom.tools.resource_graph.mapping_adapter import MappingAdapter
from loom.tools.structure.agentic_graph_v1.packet import encode_packet, decode_packet


class PipelineTests(unittest.TestCase):
    def test_unknown_extension_recognition_mapping_registration_and_packet(self):
        # This source structure is fixture data, never a provider-specific code branch.
        raw=json.dumps({'unseen_collection':[{'riddle_id':'X','measurement':3,'unfamiliar':{'unit':'u'}},{'riddle_id':'Y','measurement':8}]}).encode()
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'opaque.blob';path.write_bytes(raw)
            graph=ResourceGraph();graph.attach(path,logical_id='opaque')
            self.assertEqual(graph.describe('opaque')['status'],'unloaded')
            with self.assertRaises(ResourceError) as caught:graph.select('opaque')
            self.assertEqual(caught.exception.status,'unsupported')
            interpretations=graph.recognize('opaque')
            self.assertTrue(any(row['recognized'] and row['format_id']=='json' for row in interpretations))
            graph.select_format('opaque','json')
            report=graph.discover('opaque')
            candidates=[row for row in report['alternatives'] if row.get('mapping')]
            self.assertTrue(candidates)
            selected=candidates[0]
            graph.register_adapter('prepared-from-data',MappingAdapter(SyntaxAdapter(),selected['mapping']))
            graph.attach(path,logical_id='structural-perspective',format='json',adapter='prepared-from-data')
            rows=graph.select('structural-perspective')
            self.assertTrue(rows)
            self.assertIn('/unseen_collection/0',[row['source_reference']['selector'] for row in rows])
            packet=graph.project('structural-perspective',depth=2)
            self.assertEqual(decode_packet(encode_packet(packet)),packet)
            self.assertEqual(path.read_bytes(),raw)
            self.assertEqual(packet['sources'],[]) # derived projection cannot invent source observations
            self.assertTrue(any(e['attrs'].get('mapping_id') for e in packet['entities']))

    def test_all_compositions_local_remote_inline_nested_and_eager_lazy(self):
        raw=b'{"future":{"answer":42,"empty":[]}}'
        inner=io.BytesIO()
        with zipfile.ZipFile(inner,'w') as archive:archive.writestr('data.json',raw)
        outer=io.BytesIO()
        with zipfile.ZipFile(outer,'w') as archive:archive.writestr('inner.zip',inner.getvalue())
        bodies={'https://fixture.invalid/direct':raw,'https://fixture.invalid/outer':outer.getvalue()}
        transport=lambda url,timeout,max_bytes:{'data':bodies[url],'status_code':200,'headers':{'etag':'fixture-v1'}}
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'data.json';path.write_bytes(raw)
            archive=Path(d)/'outer.zip';archive.write_bytes(outer.getvalue())
            graph=ResourceGraph(access=Access({'allowed_origins':['https://fixture.invalid']},transport=transport))
            graph.attach(path,logical_id='local')
            graph.attach(archive,logical_id='nested',members=['inner.zip','data.json'])
            graph.attach('https://fixture.invalid/direct',logical_id='remote',format='json')
            graph.attach('https://fixture.invalid/outer',logical_id='remote-nested',members=['inner.zip','data.json'])
            graph.attach_inline(raw,logical_id='inline',format='json')
            for key in ('local','nested','remote','remote-nested','inline'):
                self.assertEqual(graph.select(key,'/future/answer'),42)
                eager=graph.project(key,depth=5)
                lazy=graph.project(key,'/future/answer',depth=0)
                value=lambda p:[e['attrs']['value'] for e in p['entities'] if e['attrs'].get('selector')=='/future/answer' and 'value' in e['attrs']]
                self.assertEqual(value(eager),value(lazy))
                self.assertEqual(graph.describe(key)['content_sha256'],hashlib.sha256(raw).hexdigest())

    def test_snapshot_unchanged_and_config_read_leave_source_bytes_untouched(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'profile.json';path.write_bytes(b'{"temperature":0.17}')
            graph=ResourceGraph(policy={'mode':'snapshot'})
            graph.attach(path,logical_id='profile')
            before=path.read_bytes()
            self.assertEqual(graph.select('profile','/temperature'),.17)
            self.assertEqual(graph.select('profile','/temperature'),.17)
            from engine.config import Config
            self.assertEqual(Config(path).get('temperature'),graph.select('profile','/temperature'))
            self.assertEqual(path.read_bytes(),before)

    def test_parser_options_are_caller_data(self):
        graph=ResourceGraph()
        graph.attach_inline(b'a;b\n1;2\n',logical_id='csv',format='csv',parser_options={'csv_delimiter':';'})
        self.assertEqual(graph.select('csv'),[['a','b'],['1','2']])
        graph.attach_inline(b'{"a":1}',logical_id='bounded',format='json',parser_options={'max_bytes':2})
        with self.assertRaises(ResourceError) as caught: graph.select('bounded')
        self.assertEqual(caught.exception.status,'partial')
