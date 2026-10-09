"""Generated parser fixtures; no private data or model-quality measurements."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from loom.tools.resource_graph.line_adapter import JsonLinesAdapter, LineAccessError, parse_json_line


class JsonLinesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.path = self.root / "data.jsonl"

    def tearDown(self):
        self.temporary.cleanup()

    def source(self, values):
        self.path.write_bytes(b"".join(json.dumps(value, ensure_ascii=False).encode() + b"\n" for value in values))

    def handle(self, **policy):
        adapter = JsonLinesAdapter(allowed_roots=(self.root,), policy=policy)
        return adapter.open({"locator": str(self.path), "permissions": {"read": True}})

    def test_open_and_describe_do_not_read_content(self):
        self.source([{"unknown": {"opaque": [1, 2, 3]}}])
        handle = self.handle()
        self.assertEqual(handle.metadata()["status"], "unloaded")
        self.assertIsNone(handle.metadata()["content_sha256"])
        self.assertEqual(handle.select("")["length"], None)
        self.assertTrue(handle.node("")["is_container"])
        self.assertEqual(handle.metadata()["bytes_read"], 0)

    def test_large_source_page_reads_bounded_bytes(self):
        # 32 MB of synthetic data; requesting two rows reads <= 8 KiB.
        row = json.dumps({"payload": "x" * 1000, "unknown": {"a": 7}}).encode() + b"\n"
        with self.path.open("wb") as source:
            for _ in range(32000):
                source.write(row)
        observed = []
        adapter = JsonLinesAdapter(allowed_roots=(self.root,), observer=observed.append)
        handle = adapter.open({"locator": str(self.path)})
        self.assertEqual(observed, [])
        rows = handle.children("", 0, 2)
        self.assertEqual([key for key, _ in rows], ["0", "1"])
        self.assertEqual(rows[1][1]["unknown"], {"a": 7})
        self.assertLessEqual(sum(observed), 8192)
        self.assertLess(sum(observed), self.path.stat().st_size // 1000)
        self.assertEqual(handle.metadata()["parsed_lines"], 2)
        self.assertIsNone(handle.metadata()["content_sha256"])

    def test_nested_pointer_and_unknown_fields_survive(self):
        self.source([{"a/b": {"~key": [False, {"future": "zażółć"}]}, "": 3}])
        handle = self.handle()
        self.assertEqual(handle.select("/0/a~1b/~0key/1/future"), "zażółć")
        self.assertEqual(handle.select("/0/"), 3)
        self.assertEqual(handle.children("/0/a~1b/~0key", 1, 1), [("1", {"future": "zażółć"})])

    def test_parser_independent_of_access_and_projection(self):
        self.assertEqual(parse_json_line(b'{"opaque":{"other":1}}\r\n'), {"opaque": {"other": 1}})

    def test_syntax_recognition_is_not_domain_recognition(self):
        self.source([{"unspecified_record": "anything"}])
        handle = self.handle()
        self.assertEqual(handle.metadata()["recognition"]["semantics"], "unknown")
        self.assertEqual(handle.select("/0/unspecified_record"), "anything")
        self.assertEqual(handle.metadata()["status"], "partial")

    def test_bad_line_not_touched_by_earlier_selection(self):
        self.path.write_bytes(b'{"ok":1}\nnot-json\n')
        handle = self.handle()
        self.assertEqual(handle.select("/0/ok"), 1)
        with self.assertRaises(LineAccessError) as raised:
            handle.select("/1")
        self.assertEqual(raised.exception.status, "corrupt")
        self.assertEqual(handle.metadata()["status"], "corrupt")

    def test_empty_and_unavailable_are_distinct(self):
        self.path.touch()
        handle = self.handle()
        self.assertEqual(handle.metadata()["status"], "empty")
        self.assertEqual(handle.children("", 0, 2), [])
        self.path.unlink()
        with self.assertRaises(LineAccessError) as raised:
            handle.children("", 0, 2)
        self.assertEqual(raised.exception.status, "unavailable")

    def test_snapshot_rejects_source_change_without_storing_copy(self):
        self.source([{"v": 1}])
        handle = self.handle(freshness="snapshot")
        self.assertEqual(handle.select("/0/v"), 1)
        self.source([{"v": 222}])
        with self.assertRaises(LineAccessError) as raised:
            handle.select("/0/v")
        self.assertEqual(raised.exception.code, "source_version_changed")

    def test_live_change_invalidates_offset_cache(self):
        self.source([{"v": 1}, {"v": 2}, {"v": 3}])
        handle = self.handle(freshness="live", offset_cache=True, index_stride=1)
        self.assertEqual(handle.select("/2/v"), 3)
        self.source([{"v": "a much longer value"}, {"v": 200}])
        self.assertEqual(handle.select("/1/v"), 200)
        self.assertEqual(handle.select("")["length"], 2)

    def test_metadata_does_not_silently_adopt_new_source_revision(self):
        self.source([{"v": 1}])
        handle = self.handle(freshness="live")
        previous = handle.metadata()["source_version"]
        self.source([{"v": 222}])
        with self.assertRaises(LineAccessError) as raised:
            handle.metadata()
        self.assertEqual(raised.exception.code, "source_version_changed_since_observation")
        self.assertEqual(handle.source_version, previous)
        handle.refresh()
        self.assertNotEqual(handle.metadata()["source_version"], previous)
        self.assertEqual(handle.metadata()["bytes_read"], 0)

    def test_optional_sparse_offsets_reduce_later_scan(self):
        self.source([{"v": number, "padding": "x" * 100} for number in range(1000)])
        handle = self.handle(offset_cache=True, index_stride=100)
        self.assertEqual(handle.select("/900/v"), 900)
        first = handle.metadata()["last_request_bytes"]
        self.assertEqual(handle.select("/901/v"), 901)
        second = handle.metadata()["last_request_bytes"]
        self.assertLess(second, first // 5)
        self.assertGreater(handle.metadata()["offset_index_entries"], 1)

    def test_cache_disabled_does_not_accumulate_offsets(self):
        self.source([{"v": number} for number in range(300)])
        handle = self.handle(offset_cache=False, index_stride=1)
        handle.select("/299")
        self.assertEqual(handle.metadata()["offset_index_entries"], 1)

    def test_request_scan_bound_is_explicit_partial(self):
        self.source([{"payload": "x" * 100} for _ in range(1000)])
        handle = self.handle(max_scan_bytes=1024)
        with self.assertRaises(LineAccessError) as raised:
            handle.select("/999")
        self.assertEqual(raised.exception.status, "partial")
        self.assertEqual(raised.exception.code, "request_scan_budget_exceeded")
        self.assertEqual(raised.exception.limit_source, "user_policy")
        self.assertLessEqual(handle.metadata()["last_request_bytes"], 1024)

    def test_line_size_bound_is_explicit_partial(self):
        self.source([{"payload": "x" * 2000}])
        handle = self.handle(max_line_bytes=128)
        with self.assertRaises(LineAccessError) as raised:
            handle.select("/0")
        self.assertEqual(raised.exception.code, "line_byte_budget_exceeded")

    def test_environment_ceiling_separate_from_policy(self):
        self.source([{"payload": "x" * 2000}])
        adapter = JsonLinesAdapter(allowed_roots=(self.root,), policy={"max_scan_bytes": 100000},
                                  environment_limits={"max_scan_bytes": 1000})
        handle = adapter.open({"locator": str(self.path)})
        with self.assertRaises(LineAccessError) as raised:
            handle.select("/0")
        self.assertEqual(raised.exception.limit_source, "environment_limits")
        self.assertEqual(handle.metadata()["policy"]["max_scan_bytes"], 100000)
        self.assertEqual(handle.metadata()["environment_limits"]["max_scan_bytes"], 1000)
        self.assertLessEqual(handle.metadata()["bytes_read"], 1000)

    def test_page_limit_enforced_without_read(self):
        self.source([1, 2, 3])
        handle = self.handle(max_page_items=2)
        with self.assertRaises(LineAccessError):
            handle.children("", 0, 3)
        self.assertEqual(handle.metadata()["bytes_read"], 0)

    def test_invalid_pointers_do_not_alias_other_values(self):
        self.source([{"a": [1]}])
        handle = self.handle()
        for pointer in ("0", "/00", "/-1", "/0/a/01", "/0/a/-", "/0/~2"):
            with self.subTest(pointer=pointer), self.assertRaises(LineAccessError):
                handle.select(pointer)

    def test_missing_fragment_is_not_empty_source(self):
        self.source([{"v": 1}])
        with self.assertRaises(LineAccessError) as raised:
            self.handle().select("/5")
        self.assertEqual(raised.exception.code, "fragment_not_found")

    def test_strict_json_rejects_silent_loss_and_non_json_values(self):
        for raw in (b'{"a":1,"a":2}', b'{"n":NaN}', b'{"n":Infinity}', b'\n', b'\xff'):
            with self.subTest(raw=raw), self.assertRaises(LineAccessError) as raised:
                parse_json_line(raw)
            self.assertEqual(raised.exception.status, "corrupt")

    def test_out_of_range_number_never_becomes_infinity(self):
        with self.assertRaises(LineAccessError) as raised:
            parse_json_line(b'{"number":1e1000}')
        self.assertEqual(raised.exception.status, "unsupported")

    def test_last_line_without_newline_is_valid(self):
        self.path.write_bytes(b'{"last":true}')
        self.assertTrue(self.handle().select("/0/last"))

    def test_no_read_without_authorization(self):
        self.source([1])
        for resource in ({"locator": str(self.path)},
                         {"locator": str(self.path), "permissions": {"read": False}}):
            with self.assertRaises(LineAccessError):
                JsonLinesAdapter().open(resource)

    def test_common_access_authorizer_enforced_without_adapter_root_override(self):
        from loom.tools.resource_graph.access import Access
        self.source([{"allowed": True}])
        access = Access(policy={"local_roots": [str(self.root)]})
        handle = JsonLinesAdapter().open({"locator": str(self.path)}, access)
        self.assertTrue(handle.select("/0/allowed"))
        access.policy["allow_local"] = False
        with self.assertRaises(LineAccessError) as raised:
            handle.select("/0")
        self.assertEqual(raised.exception.status, "unavailable")

    def test_authority_is_checked_again_after_permission_revocation(self):
        self.source([1])
        resource = {"locator": str(self.path), "permissions": {"read": True}}
        handle = JsonLinesAdapter(allowed_roots=(self.root,)).open(resource)
        resource["permissions"]["read"] = False
        with self.assertRaises(LineAccessError):
            handle.select("/0")

    def test_remote_and_container_are_not_implicitly_fetched(self):
        adapter = JsonLinesAdapter(allowed_roots=(self.root,))
        for resource in ({"locator": "https://example.invalid/private.jsonl"},
                         {"locator": str(self.path), "members": ["inside.jsonl"]}):
            with self.assertRaises(LineAccessError) as raised:
                adapter.open(resource)
            self.assertEqual(raised.exception.status, "unsupported")

    def test_symlink_cannot_escape_authorized_root(self):
        self.source([1])
        with tempfile.TemporaryDirectory() as outside:
            target = Path(outside) / "outside.jsonl"
            target.write_text("99\n")
            self.path.unlink()
            self.path.symlink_to(target)
            with self.assertRaises(LineAccessError) as raised:
                self.handle()
            self.assertEqual(raised.exception.status, "unavailable")

    def test_no_source_modification_or_additional_files(self):
        self.source([{"v": 1}, {"v": 2}])
        digest = hashlib.sha256(self.path.read_bytes()).hexdigest()
        handle = self.handle(offset_cache=True)
        handle.select("/1")
        handle.children("", 0, 2)
        handle.close()
        self.assertEqual(hashlib.sha256(self.path.read_bytes()).hexdigest(), digest)
        self.assertEqual(list(self.root.iterdir()), [self.path])
        with self.assertRaises(LineAccessError):
            handle.select("/0")

    def test_extension_registered_without_graph_format_switch(self):
        from loom.tools.resource_graph import ResourceGraph
        from loom.tools.resource_graph.access import Access
        self.source([{"custom": {"field": "accepted"}}])
        graph = ResourceGraph(access=Access(policy={"local_roots": [str(self.root)]}))
        graph.register_adapter("new-independent-structure", JsonLinesAdapter())
        descriptor = graph.attach(str(self.path), logical_id="fixture", adapter="new-independent-structure")
        self.assertEqual(descriptor["status"], "unloaded")
        self.assertEqual(graph.metrics["opens"], 0)
        self.assertEqual(graph.select("fixture", "/0/custom/field"), "accepted")
        self.assertEqual(graph.children("fixture", "", limit=1)[0]["key"], "0")
        packet = graph.project("fixture", "/0/custom/field", depth=0)
        self.assertEqual(packet["schema"], "loom.graph_packet/1")
        self.assertFalse(graph.describe("fixture")["embedded_bytes"])

    def test_virtual_root_projects_without_whole_source_values(self):
        from loom.tools.resource_graph import ResourceGraph
        from loom.tools.resource_graph.access import Access
        self.source([{"v": number} for number in range(10000)])
        observed = []
        graph = ResourceGraph(access=Access(policy={"local_roots": [str(self.root)]}))
        graph.register_adapter("fixture-adapter", JsonLinesAdapter(observer=observed.append))
        graph.attach(str(self.path), logical_id="many-rows", adapter="fixture-adapter")
        packet = graph.project("many-rows", "", depth=0)
        self.assertEqual(packet["schema"], "loom.graph_packet/1")
        self.assertEqual(sum(observed), 0)
        graph.project("many-rows", "", depth=1, limit=3)
        self.assertLess(sum(observed), self.path.stat().st_size)


if __name__ == "__main__":
    unittest.main()
