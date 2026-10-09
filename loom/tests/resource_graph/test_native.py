"""Actual C ABI/native persistence checks; fixtures are synthetic, not user data."""
from copy import deepcopy
import os
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

from loom.tools.coordination.graph_store import GraphStoreError, NativeGraphStore
from loom.tools.resource_graph.native import roundtrip
from loom.tools.structure.agentic_graph_v1 import packet as codec
from loom.tools.structure.agentic_graph_v1.test_packet import fixture


class NativeBoundaryValidationTests(unittest.TestCase):
    def test_invalid_packet_is_rejected_before_loading_native_library(self):
        packet = fixture()
        packet["entities"][0]["id"] = "modified-without-rehash"
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                roundtrip(packet, Path(directory) / "missing.so", directory)
            self.assertFalse((Path(directory) / "chatadhd.db").exists())

    def test_missing_native_library_is_not_reported_as_native_success(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(OSError):
                roundtrip(fixture(), Path(directory) / "missing.so", directory)


@unittest.skipUnless(os.environ.get("LOOM_LIBRARY"),
                     "actual native checks require LOOM_LIBRARY pointing to a shared build")
class NativeRoundtripTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name) / "native"
        self.library = os.environ["LOOM_LIBRARY"]

    def test_existing_packet_preserves_records_unknown_attributes_and_utf8(self):
        packet = fixture()
        before = deepcopy(packet)
        result = roundtrip(packet, self.library, self.directory)
        self.assertEqual(packet, before)
        self.assertEqual(result["packet"], before)
        self.assertTrue(result["native_executed"])
        self.assertTrue(result["reopened"])
        self.assertTrue(result["row_drift_matches"])
        self.assertFalse(result["acceptance_establishes_content_truth"])
        self.assertEqual(result["selected_counts"], {"entities": 2, "claims": 1, "sources": 1})
        self.assertTrue((self.directory / "chatadhd.db").is_file())

    def test_exact_acceptance_is_idempotent_in_existing_native_store(self):
        packet = fixture()
        first = roundtrip(packet, self.library, self.directory)
        second = roundtrip(packet, self.library, self.directory)
        self.assertEqual(first["receipt_id"], second["receipt_id"])
        self.assertEqual(first["packet"], second["packet"])

    def test_on_demand_external_fields_replay_without_reading_original(self):
        from loom.tools.resource_graph.core import ResourceGraph
        source = Path(self.temp.name) / "profile.json"
        source.write_text(json.dumps({"theme": "dark", "future": {"flag": True}}))
        graph = ResourceGraph()
        graph.attach(str(source), logical_id="test:external-profile", format="json")
        packet = graph.project("test:external-profile", depth=2)
        self.assertFalse(graph.describe("test:external-profile")["embedded_bytes"])
        result = roundtrip(packet, self.library, self.directory)
        source.unlink()
        with NativeGraphStore(self.library, self.directory) as store:
            replayed = store.replay(result["receipt_id"])["receipt"]["packet"]
        self.assertEqual(replayed, packet)
        self.assertGreater(result["selected_counts"]["entities"], 1)
        self.assertGreater(result["selected_counts"]["claims"], 0)

    def test_changed_packet_version_preserves_original_receipt_and_rows(self):
        packet = fixture()
        original = roundtrip(packet, self.library, self.directory)
        changed = deepcopy(packet["entities"])
        changed[0]["attrs"]["future_field"] = {"keep": "unknown"}
        replacement = codec.make_packet(entities=changed, claims=packet["claims"],
            sources=packet["sources"], origin=packet["provenance"]["entities"][changed[0]["id"]]["origin"])
        updated = roundtrip(replacement, self.library, self.directory)
        self.assertNotEqual(original["receipt_id"], updated["receipt_id"])
        with NativeGraphStore(self.library, self.directory) as store:
            self.assertEqual(store.replay(original["receipt_id"])["receipt"]["packet"], packet)
            self.assertEqual(store.replay(updated["receipt_id"])["receipt"]["packet"], replacement)

    def test_native_replay_reports_deleted_row_as_drift_without_repair(self):
        packet = fixture()
        result = roundtrip(packet, self.library, self.directory)
        with sqlite3.connect(self.directory / "chatadhd.db") as database:
            database.execute("DELETE FROM loom_kb_entities WHERE id = ?", (packet["entities"][0]["id"],))
        with NativeGraphStore(self.library, self.directory) as store:
            self.assertFalse(store.read(result["receipt_id"])["row_drift"]["matches"])
            with self.assertRaises(GraphStoreError):
                store.replay(result["receipt_id"])
        with sqlite3.connect(self.directory / "chatadhd.db") as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM loom_kb_entities WHERE id = ?",
                            (packet["entities"][0]["id"],)).fetchone()[0], 0)


if __name__ == "__main__":
    unittest.main()
