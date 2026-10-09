import io
from pathlib import Path
import tempfile
import unittest
import zipfile
from loom.tools.resource_graph import ResourceGraph, ResourceError


class DemandSearchTests(unittest.TestCase):
    def test_nested_archive_content_is_searchable_without_import_or_node_materialization(self):
        raw=b'{"messages":[{"content":"Alpha target"},{"content":"unrelated"}],"future":{"ref":"preserved"}}'
        inner=io.BytesIO()
        with zipfile.ZipFile(inner,'w') as z:z.writestr('conversation.json',raw)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'archive.zip'
            with zipfile.ZipFile(path,'w') as z:z.writestr('nested.zip',inner.getvalue())
            graph=ResourceGraph();graph.attach(path,logical_id='linked-archive',members=['nested.zip','conversation.json'])
            self.assertEqual(graph.metrics['projected_nodes'],0)
            result=graph.search('linked-archive','TARGET',case_sensitive=False)
            self.assertEqual([row['selector'] for row in result['matches']],['/messages/0/content'])
            self.assertTrue(result['coverage']['complete'])
            self.assertEqual(graph.metrics['projected_nodes'],0)
            self.assertEqual(result['graph_nodes_created'],0)
            self.assertNotIn('value',result['matches'][0])
    def test_search_budget_and_unavailability_are_not_empty_success(self):
        graph=ResourceGraph();graph.attach_inline(b'{"a":[1,2,3],"b":"target"}',logical_id='x',format='json')
        result=graph.search('x','target',limit=1)
        self.assertFalse(result['coverage']['complete'])
        self.assertEqual(result['status'],'partial')
        graph.attach('/missing-resource-graph-fixture.json',logical_id='missing')
        with self.assertRaises(ResourceError) as error:graph.search('missing','anything')
        self.assertEqual(error.exception.status,'unavailable')
    def test_search_values_obey_export_permission(self):
        graph=ResourceGraph();graph.attach_inline(b'{"v":"target"}',logical_id='private',format='json',permissions={'export_values':False})
        self.assertTrue(graph.search('private','target')['matches'])
        with self.assertRaises(ResourceError):graph.search('private','target',include_values=True)

    def test_seekable_adapter_search_uses_bounded_common_pages(self):
        from loom.tools.resource_graph.line_adapter import JsonLinesAdapter
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'rows.jsonl';path.write_text(''.join('{"v":"target"}\n' for _ in range(300)))
            graph=ResourceGraph();graph.register_adapter('lines',JsonLinesAdapter())
            graph.attach(path,logical_id='lines',adapter='lines')
            result=graph.search('lines','target')
            self.assertEqual(len(result['matches']),300)
            self.assertTrue(result['coverage']['complete'])
            self.assertEqual(graph.metrics['projected_nodes'],0)
