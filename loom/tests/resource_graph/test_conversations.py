"""Reference/full-import equivalence uses existing generated SAFE fixtures.

Run with LOOM_LIBRARY=/absolute/path/libloom.so to execute native tests.
The tested bridge explicitly materializes temporary native rows; syntax/reference
tests demonstrate the separate primary route which has no native requirement.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

from loom.tools.resource_graph.access import Access
from loom.tools.resource_graph.conversations import NativeConversationMapper, ConversationMappingError
from loom.tools.resource_graph.core import ResourceGraph


FIXTURES = Path(__file__).resolve().parents[1] / "fixtures/exports"
LIBRARY = os.environ.get("LOOM_LIBRARY")


class ConversationReferences(unittest.TestCase):
    def test_source_conversation_structures_are_readable_without_native(self):
        graph = ResourceGraph()
        source = FIXTURES / "openai_single_conversations.json"
        original = source.read_bytes()
        graph.attach(source, logical_id="test:reference", format="json")
        self.assertEqual(graph.metrics["opens"], 0)
        self.assertEqual(graph.select("test:reference", "/0/mapping/S1-0/message/author/role"), "user")
        self.assertEqual(graph.select("test:reference", "/0/mapping/S1-0/children"), ["S1-1"])
        self.assertEqual(graph.select("test:reference", "/0/mapping/S1-1/message/content/parts"), ["Odpowiedź"])
        self.assertEqual(source.read_bytes(), original)


@unittest.skipUnless(LIBRARY, "LOOM_LIBRARY required for real native provider-mapper tests")
class NativeConversationParity(unittest.TestCase):
    def setUp(self):
        self.mapper = NativeConversationMapper(LIBRARY)
        self.temp = tempfile.TemporaryDirectory(prefix="resource-native-parity-")
        self.addCleanup(self.temp.cleanup)

    def parity(self, filename):
        source = FIXTURES / filename
        before = source.read_bytes()
        referenced = self.mapper.map_bytes(before, filename=filename,
                                          source_identity="fixture:" + filename,
                                          source_locator=str(source))
        imported = self.mapper.import_path(source, data_directory=Path(self.temp.name) / filename,
                                           source_identity="fixture:" + filename)
        self.assertEqual(referenced["semantic_view"], imported["semantic_view"])
        self.assertEqual(referenced["source"]["content_sha256"], hashlib.sha256(before).hexdigest())
        self.assertEqual(referenced["source"]["logical_id"], "fixture:" + filename)
        self.assertEqual(referenced["execution"]["mode"], "native_temporary_materialization")
        self.assertFalse(referenced["execution"]["store_retained"])
        self.assertTrue(imported["execution"]["store_retained"])
        self.assertTrue((Path(self.temp.name) / filename / "chatadhd.db").is_file())
        self.assertEqual(source.read_bytes(), before)
        return referenced

    def test_openai_branches_authors_available_assets_and_unresolved_references(self):
        result = self.parity("openai_2026_sharded.zip")
        rows = [m for c in result["semantic_view"]["conversations"] for m in c["messages"]]
        self.assertTrue(any(row["version_group_keys"] for row in rows))
        self.assertTrue(any(row["parent_key"] for row in rows))
        self.assertTrue(any(row["available_attachment_count"] for row in rows))
        references = [ref for row in rows for key in ("attachments", "pointers")
                      for ref in row["metadata"]["export"].get(key, [])]
        self.assertTrue(any(ref.get("resolved") is True for ref in references))
        self.assertTrue(any(ref.get("resolved") is False for ref in references))
        self.assertTrue(all("raw" in row["metadata"]["export"] for row in rows))
        self.assertGreater(result["archive_preflight"]["members_validated"], 0)

    def test_anthropic_unknown_fields_and_unresolved_attachments(self):
        result = self.parity("anthropic_2026_full.zip")
        rows = [m for c in result["semantic_view"]["conversations"] for m in c["messages"]]
        self.assertTrue(rows)
        self.assertTrue(all("raw" in row["metadata"]["export"] for row in rows))
        self.assertTrue(any(ref.get("resolved") is False for row in rows
                            for ref in row["metadata"]["export"].get("attachments", [])))

    def test_nested_archive_parity(self):
        result = self.parity("nested_container.zip")
        self.assertGreater(result["archive_preflight"]["members_validated"], 1)
        self.assertTrue(result["semantic_view"]["conversations"])

    def test_controlled_remote_reference_matches_local(self):
        source = FIXTURES / "openai_single_conversations.json"
        payload = source.read_bytes()
        access = Access(policy={"allowed_origins": ["https://fixture.invalid"]},
                        transport=lambda url, timeout, maximum: {"status_code": 200,
                            "headers": {"etag": '"fixture-v1"'}, "data": payload, "final_url": url})
        local = access.read(str(source))
        remote = access.read("https://fixture.invalid/" + source.name)
        self.assertEqual(local["data"], remote["data"])
        a = self.mapper.map_bytes(local["data"], filename=source.name, source_identity="fixture:same")
        b = self.mapper.map_bytes(remote["data"], filename=source.name, source_identity="fixture:same")
        self.assertEqual(a["semantic_view"], b["semantic_view"])

    def test_bad_archive_refused_before_native_database(self):
        for member in ("../escape.json", "/escape.json"):
            raw = io.BytesIO()
            with zipfile.ZipFile(raw, "w") as archive:
                archive.writestr(member, "{}")
            with self.assertRaisesRegex(ConversationMappingError, "native_mapper_archive"):
                self.mapper.map_bytes(raw.getvalue(), filename="unsafe.zip", source_identity="fixture:bad")
        with self.assertRaisesRegex(ConversationMappingError, "native_mapper_archive"):
            self.mapper.map_bytes(b"PK corrupt", filename="corrupt.zip", source_identity="fixture:bad")


if __name__ == "__main__":
    unittest.main()
