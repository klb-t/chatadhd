"""Counterexamples from independent review of generic resource composition."""
from copy import deepcopy
import base64
import json
import hashlib
from pathlib import Path
import tempfile
import unittest

from loom.tools.resource_graph import ResourceGraph, ResourceError
from loom.tools.resource_graph.core import SyntaxAdapter, select_value
from loom.tools.resource_graph.discovery import Discovery
from loom.tools.resource_graph.mapping_adapter import MappingAdapter
from loom.tools.structure.agentic_graph_v1 import packet as codec


class TrackingAdapter:
    """Fixture for a caller-registered closeable, hash-only resource adapter."""
    def __init__(self):
        self.value = "first"
        self.closed = []

    def open(self, resource, access):
        value, owner = self.value, self

        class Handle:
            def metadata(self):
                return {"source_version": None,
                        "content_sha256": hashlib.sha256(value.encode()).hexdigest(),
                        "parser_version": "fixture/1", "status": "available"}

            def select(self, pointer):
                if pointer:
                    raise ResourceError("unavailable", "fixture_fragment_not_found")
                return value

            def close(self):
                owner.closed.append(value)

        return Handle()


class MutableHashAdapter:
    def __init__(self, *, change_on_select=False):
        self.value = "first"
        self.closed = 0
        self.change_on_select = change_on_select

    def open(self, resource, access):
        owner = self

        class Handle:
            def metadata(self):
                return {"source_version": None, "parser_version": "fixture/1", "status": "available",
                        "content_sha256": hashlib.sha256(owner.value.encode()).hexdigest()}

            def select(self, pointer):
                if owner.change_on_select:
                    owner.value = "second"
                return owner.value

            def close(self):
                owner.closed += 1

        return Handle()


class ReviewedResourceRegressions(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "sample.json"
        self.source = {"left": "alpha", "right": "beta",
                       "records": [{"ticket": "one", "amount": 1},
                                   {"ticket": "two", "amount": 2}]}
        self.path.write_text(json.dumps(self.source))

    def mapping(self):
        report = Discovery().discover(self.source)
        return next(row["mapping"] for row in report["alternatives"] if row.get("mapping"))

    def mapped_graph(self, mapping):
        graph = ResourceGraph()
        graph.register_adapter("mapping", MappingAdapter(SyntaxAdapter(), mapping))
        graph.attach(self.path, logical_id="source", adapter="mapping")
        return graph

    def test_index_retains_same_revision_fragments_without_handle_cache(self):
        graph = ResourceGraph(policy={"index": True, "cache": False})
        graph.attach(self.path, logical_id="source")
        graph.select("source", "/left")
        graph.select("source", "/right")
        self.assertEqual(graph.search_index("source", "alpha")["selectors"], ["/left"])
        self.assertEqual(graph.search_index("source", "beta")["selectors"], ["/right"])
        self.path.write_text(json.dumps({"left": "changed", "right": "gamma"}))
        graph.select("source", "/right")
        self.assertEqual(graph.search_index("source", "alpha")["selectors"], [])
        self.assertEqual(graph.search_index("source", "gamma")["selectors"], ["/right"])

    def test_mapping_change_does_not_reuse_changed_field_identity(self):
        mapping = self.mapping()
        graph = self.mapped_graph(mapping)
        first = graph.project("source", depth=4)
        changed = deepcopy(mapping)
        changed["id"] = mapping["id"] + ":revision2"
        changed["version"] = "2"
        changed["rules"][0]["kind"] += "_revised"
        graph.register_adapter("mapping", MappingAdapter(SyntaxAdapter(), changed))
        second = graph.project("source", depth=4)
        for packet in (first, second):
            codec.validate_packet(packet)
        before = {row["attrs"]["selector"]: row for row in first["entities"]
                  if row["kind"] == "resource_field"}
        after = {row["attrs"]["selector"]: row for row in second["entities"]
                 if row["kind"] == "resource_field"}
        self.assertIn("/0/kind", before)
        self.assertNotEqual(before["/0/kind"]["attrs"]["value"],
                            after["/0/kind"]["attrs"]["value"])
        self.assertNotEqual(before["/0/kind"]["id"], after["/0/kind"]["id"])

    def test_mapping_projection_does_not_fabricate_original_source_observations(self):
        mapping = self.mapping()
        graph = self.mapped_graph(mapping)
        packet = graph.project("source", depth=4)
        self.assertEqual(packet["sources"], [])
        method = next(row for row in packet["entities"] if row["kind"] == "analysis_method")
        self.assertEqual(method["attrs"]["parser"]["mapping_id"], mapping["id"])
        self.assertEqual(method["attrs"]["parser"]["mapping_version"], mapping["version"])
        rows = graph.select("source")
        self.assertGreater(len(rows), 0)
        for row in rows:
            original = select_value(self.source, row["source_reference"]["selector"])
            self.assertIsNotNone(original)
            for field in row["fields"]:
                if field["status"] == "available":
                    self.assertIsNotNone(select_value(self.source, field["source_reference"]["selector"]))

    def test_snapshot_detects_changed_hash_without_service_version(self):
        adapter = TrackingAdapter()
        graph = ResourceGraph(policy={"mode": "snapshot", "cache": False})
        graph.register_adapter("tracking", adapter)
        graph.attach("fixture:hash-only", logical_id="source", adapter="tracking")
        self.assertEqual(graph.select("source"), "first")
        adapter.value = "changed"
        with self.assertRaisesRegex(ResourceError, "snapshot_source_changed"):
            graph.select("source")
        self.assertEqual(adapter.closed, ["first", "changed"])

    def test_noncached_handle_closes_on_success_and_selector_failure(self):
        adapter = TrackingAdapter()
        graph = ResourceGraph(policy={"cache": False})
        graph.register_adapter("tracking", adapter)
        graph.attach("fixture:closeable", logical_id="source", adapter="tracking")
        self.assertEqual(graph.select("source"), "first")
        self.assertEqual(adapter.closed, ["first"])
        with self.assertRaisesRegex(ResourceError, "fixture_fragment_not_found"):
            graph.select("source", "/missing")
        self.assertEqual(adapter.closed, ["first", "first"])

    def test_mapping_adapter_releases_underlying_source_handle(self):
        source_adapter = TrackingAdapter()
        mapping = {"schema": "loom.resource_mapping/1", "id": "fixture:empty-rules",
                   "version": "1", "preserve_unknown": True, "rules": []}
        graph = ResourceGraph()
        graph.register_adapter("mapping", MappingAdapter(source_adapter, mapping))
        graph.attach("fixture:mapping-lifecycle", logical_id="source", adapter="mapping")
        self.assertEqual(graph.select("source"), [])
        self.assertEqual(source_adapter.closed, ["first"])

    def test_cached_snapshot_releases_handle_when_its_hash_changes(self):
        adapter = MutableHashAdapter()
        graph = ResourceGraph(policy={"cache": True, "retention_seconds": 100, "mode": "snapshot"})
        graph.register_adapter("mutable", adapter)
        graph.attach("fixture:cached-hash", logical_id="source", adapter="mutable")
        self.assertEqual(graph.select("source"), "first")
        self.assertEqual(adapter.closed, 0)
        adapter.value = "changed"
        with self.assertRaisesRegex(ResourceError, "snapshot_source_changed"):
            graph.select("source")
        self.assertEqual(adapter.closed, 1)

    def test_hash_change_during_projection_does_not_return_mixed_packet(self):
        adapter = MutableHashAdapter(change_on_select=True)
        graph = ResourceGraph()
        graph.register_adapter("mutable", adapter)
        graph.attach("fixture:changing-hash", logical_id="source", adapter="mutable")
        with self.assertRaisesRegex(ResourceError, "source_changed_during_projection"):
            graph.project("source")
        self.assertEqual(adapter.closed, 1)

    def test_live_embedding_tracks_new_source_and_export_hash_stays_bound(self):
        graph = ResourceGraph(policy={"mode": "live", "embedding": True})
        graph.attach(self.path, logical_id="source")
        graph.capture("source")
        self.path.write_text(json.dumps({"left": "updated"}))
        self.assertEqual(graph.select("source", "/left"), "updated")
        captured = graph.export_reference("source", include_bytes=True)
        self.assertEqual(hashlib.sha256(base64.b64decode(captured["content_base64"])).hexdigest(),
                         captured["content_sha256"])
        self.path.write_text("{")
        with self.assertRaises(ResourceError):
            graph.select("source", "/left")
        retained = graph.export_reference("source", include_bytes=True)
        self.assertEqual(hashlib.sha256(base64.b64decode(retained["content_base64"])).hexdigest(),
                         retained["content_sha256"])


if __name__ == "__main__":
    unittest.main()
