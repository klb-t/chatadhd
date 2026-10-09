import copy
import json
from pathlib import Path
import tempfile
import unittest
from loom.tools.resource_graph import ResourceGraph, ResourceError


class ReferenceCheckpointTests(unittest.TestCase):
    def test_restore_reference_without_io_and_without_native_store(self):
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'s.json';source.write_text('{"v":1}')
            first=ResourceGraph(policy={'mode':'snapshot'})
            first.attach(source,logical_id='stable')
            self.assertEqual(first.select('stable','/v'),1)
            saved=json.loads(json.dumps(first.export_reference('stable')))
            second=ResourceGraph();second.restore_reference(saved)
            self.assertEqual(second.metrics['opens'],0)
            self.assertEqual(second.select('stable','/v'),1)
            source.write_text('{"v":2}')
            with self.assertRaisesRegex(ResourceError,'snapshot_source_changed'):second.select('stable')
    def test_embedded_checkpoint_survives_source_removal_and_detects_tampering(self):
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'s.json';source.write_text('{"v":1}')
            first=ResourceGraph(policy={'mode':'snapshot'})
            first.attach(source,logical_id='stable');first.capture('stable')
            saved=first.export_reference('stable',include_bytes=True)
            source.unlink()
            second=ResourceGraph();second.restore_reference(saved)
            self.assertEqual(second.select('stable','/v'),1)
            bad=copy.deepcopy(saved);bad['content_base64']='e30='
            target=ResourceGraph()
            with self.assertRaisesRegex(ValueError,'resource_embedded_hash_mismatch'):target.restore_reference(bad)
            self.assertEqual(target.resources,{})
    def test_native_packet_descriptor_is_a_reopenable_source_reference(self):
        graph=ResourceGraph();graph.attach('unknown:offline',logical_id='unrecognized')
        packet=graph.reference_packet('unrecognized')
        restored=ResourceGraph();restored.restore_reference(packet['entities'][0]['attrs'])
        self.assertEqual(restored.describe('unrecognized')['status'],'unloaded')
        self.assertEqual(restored.metrics['opens'],0)
