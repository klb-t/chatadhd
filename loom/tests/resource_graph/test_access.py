"""Real byte/HTTP/ZIP execution with generated, non-user fixtures."""
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import struct
import tempfile
import threading
import unittest
import warnings
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch

from loom.tools.resource_graph.access import Access


def zipped(entries, compression=zipfile.ZIP_DEFLATED):
    output = io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(output, "w", compression=compression) as archive:
            for name, payload in entries:
                archive.writestr(name, payload)
    return output.getvalue()


class AccessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def source(self, data, name="source.bin"):
        path = self.root / name
        path.write_bytes(data)
        return str(path)

    def test_local_file_and_uri_are_identical_and_have_separate_version_hash(self):
        path = self.source(b'{"unknown":42}')
        direct = Access().read(path)
        uri = Access().read(Path(path).as_uri())
        self.assertEqual(direct["data"], uri["data"])
        self.assertEqual(direct["content_sha256"], hashlib.sha256(direct["data"]).hexdigest())
        self.assertEqual(direct["source_version"]["kind"], "local_stat")
        self.assertNotIn("content_sha256", direct["source_version"])

    def test_source_change_and_disappearance_are_not_empty(self):
        path = self.source(b"first")
        first = Access().read(path)
        Path(path).write_bytes(b"different")
        second = Access().read(path)
        self.assertNotEqual(first["source_version"], second["source_version"])
        self.assertNotEqual(first["content_sha256"], second["content_sha256"])
        Path(path).unlink()
        missing = Access().read(path)
        self.assertEqual(missing["status"], "unavailable")
        self.assertNotIn("data", missing)
        self.assertEqual(Access().read(self.source(b""))["status"], "empty")

    def test_unchanged_read_has_stable_version_and_separate_observation_time(self):
        path = self.source(b"unchanged")
        first, second = Access().read(path), Access().read(path)
        self.assertEqual(first["source_version"], second["source_version"])
        self.assertNotIn("observed_at", first["source_version"])
        self.assertIn("observed_at", first["provenance"][0])
        for headers in ({"ETag": '"fixed"'}, {}):
            def transport(*args):
                return {"data": b"unchanged", "headers": headers}
            access = Access({"allowed_origins": ["https://fixture.invalid"]}, transport)
            first, second = access.read("https://fixture.invalid/data"), access.read("https://fixture.invalid/data")
            self.assertEqual(first["source_version"], second["source_version"])
            self.assertNotIn("observed_at", first["source_version"])

    def test_regular_zip_nested_zip_and_no_unrelated_expansion(self):
        leaf = b'{"messages":["hello"]}'
        inner = zipped([("payload.json", leaf), ("not_selected", b"x" * 100_000)])
        outer = self.source(zipped([("inner.zip", inner)]))
        access = Access()
        direct = access.open_container(inner, ("payload.json",))
        nested = access.read(outer, ("inner.zip", "payload.json"))
        self.assertEqual(direct["data"], nested["data"])
        self.assertEqual(len(nested["provenance"]), 3)
        self.assertNotEqual(nested["outer_content_sha256"], nested["content_sha256"])
        with patch.object(zipfile.ZipFile, "open", side_effect=AssertionError("expanded inventory")):
            listing = access.list_members(self.source(inner))
        self.assertEqual(listing["status"], "available")
        self.assertEqual([row["member"] for row in listing["entries"]], ["payload.json", "not_selected"])

    def test_empty_archive_missing_member_and_corrupt_are_distinct(self):
        empty = self.source(zipped([]))
        self.assertEqual(Access().list_members(empty)["status"], "empty")
        self.assertEqual(Access().read(empty, ("gone",))["status"], "unavailable")
        bad = Access().list_members(self.source(b"PK\x03\x04broken"))
        self.assertEqual(bad["status"], "corrupt")
        self.assertNotEqual(bad["status"], "empty")

    def test_unsafe_zip_paths_never_escape_and_inventory_keeps_unknown_rows(self):
        names = ["../outside", "/absolute", "C:/windows", "back\\slash", "a/../b"]
        path = self.source(zipped([(name, b"danger") for name in names] + [("safe", b"yes")]))
        access = Access()
        listing = access.list_members(path)
        self.assertEqual(listing["status"], "partial")
        self.assertEqual(len(listing["entries"]), len(names) + 1)
        for name in names:
            self.assertEqual(access.read(path, (name,))["status"], "unavailable")
        self.assertEqual(access.read(path, ("safe",))["data"], b"yes")
        self.assertFalse((self.root.parent / "outside").exists())

    def test_symlinks_and_duplicate_names_are_not_silently_followed(self):
        symlink = zipfile.ZipInfo("link")
        symlink.create_system = 3
        symlink.external_attr = (stat.S_IFLNK | 0o777) << 16
        path = self.source(zipped([(symlink, b"../../secret"), ("duplicate", b"a"), ("duplicate", b"b")]))
        self.assertEqual(Access().read(path, ("link",))["status"], "unsupported")
        self.assertEqual(Access().read(path, ("duplicate",))["reason"], "ambiguous_duplicate_member")

    def test_resource_policy_and_environment_bound_are_reported_separately(self):
        path = self.source(b"12345")
        user = Access({"max_source_bytes": 4}).read(path)
        self.assertEqual(user["limit_source"], "user_policy")
        hard = Access({"max_source_bytes": 1000}, environment_limits={"max_source_bytes": 4}).read(path)
        self.assertEqual(hard["limit_source"], "environment_limits")
        with self.assertRaises(ValueError):
            Access({"environment_limits": {"max_source_bytes": 1000}})
        with self.assertRaises(ValueError):
            Access({"max_source_bytes": float("inf")})

    def test_decompression_budget_and_ratio_checked_before_expansion(self):
        archive = self.source(zipped([("huge", b"0" * 500_000)]))
        with patch.object(zipfile.ZipFile, "open", side_effect=AssertionError("must not expand")):
            member = Access({"max_member_bytes": 10}).read(archive, ("huge",))
            ratio = Access({"max_compression_ratio": 2}).read(archive, ("huge",))
        self.assertEqual(member["reason"], "budget_exceeded:max_member_bytes")
        self.assertEqual(ratio["reason"], "budget_exceeded:max_compression_ratio")
        self.assertNotIn("data", member)

    def test_nested_cumulative_limit_and_depth(self):
        leaf = b"abcdefgh"
        inner = zipped([("leaf", leaf)])
        path = self.source(zipped([("inner", inner)]))
        limited = Access({"max_expanded_bytes": len(inner) + len(leaf) - 1}).read(path, ("inner", "leaf"))
        self.assertEqual(limited["reason"], "budget_exceeded:max_expanded_bytes")
        depth = Access({"max_nesting_depth": 1}).read(path, ("inner", "leaf"))
        self.assertEqual(depth["reason"], "budget_exceeded:max_nesting_depth")

    def test_archive_count_is_checked_before_zipinfo_allocation(self):
        path = self.source(zipped([("a", b"a"), ("b", b"b")]))
        with patch.object(zipfile, "ZipFile", side_effect=AssertionError("must not allocate")):
            result = Access({"max_archive_entries": 1}).list_members(path)
        self.assertEqual(result["reason"], "budget_exceeded:max_archive_entries")

    def test_forged_directory_count_does_not_bypass_allocation_budget(self):
        payload = bytearray(zipped([("a", b"a"), ("b", b"b")]))
        end = payload.rfind(b"PK\x05\x06")
        struct.pack_into("<HH", payload, end + 8, 0, 0)
        path = self.source(payload)
        with patch.object(zipfile, "ZipFile", side_effect=AssertionError("must not allocate")):
            result = Access({"max_archive_entries": 1}).list_members(path)
        self.assertEqual(result["reason"], "budget_exceeded:max_archive_entries")

    def test_recursive_eager_preflight_reuses_lazy_guard_and_aggregate_budget(self):
        inner = zipped([("leaf", b"valid")])
        outer = zipped([("nested.zip", inner), ("other", b"bytes")])
        result = Access().validate_archive(outer)
        self.assertEqual(result["status"], "available")
        self.assertEqual(result["members_validated"], 3)
        self.assertEqual(result["expanded_bytes"], len(inner) + 10)
        budget = Access({"max_expanded_bytes": len(inner) + 5}).validate_archive(outer)
        self.assertEqual(budget["status"], "partial")
        self.assertEqual(budget["reason"], "budget_exceeded:max_expanded_bytes")
        unsafe = zipped([("nested.zip", zipped([("../escape", b"bad")]))])
        self.assertEqual(Access().validate_archive(unsafe)["status"], "partial")

    def test_authorize_local_does_not_read_file(self):
        path = self.source(b"opaque")
        access = Access()
        with patch.object(Path, "open", side_effect=AssertionError("must not read")):
            self.assertEqual(access.authorize_local(path), Path(path))

    def test_corrupt_crc_does_not_return_usable_data(self):
        payload = bytearray(zipped([("leaf", b"content")], zipfile.ZIP_STORED))
        position = payload.index(b"content")
        payload[position] ^= 0x01
        result = Access().read(self.source(payload), ("leaf",))
        self.assertEqual(result["status"], "corrupt")
        self.assertNotIn("data", result)

    def test_local_roots_and_nonregular_sources(self):
        path = self.source(b"ok")
        root = self.root / "allowed"
        root.mkdir()
        self.assertEqual(Access({"local_roots": [str(root)]}).read(path)["reason"], "outside_local_roots")
        if hasattr(os, "mkfifo"):
            fifo = root / "fifo"
            os.mkfifo(fifo)
            self.assertEqual(Access().read(str(fifo))["status"], "unsupported")

    def test_http_denied_before_transport_and_credential_query_not_in_evidence(self):
        calls = []
        def transport(*args):
            calls.append(args)
            return {"data": b"ok"}
        self.assertEqual(Access(transport=transport).read("https://example.invalid/private?token=SECRET")["status"], "unavailable")
        policy = {"allowed_origins": ["https://example.invalid"]}
        result = Access(policy, transport).read("https://user:SECRET@example.invalid/private?token=SECRET")
        self.assertNotIn("SECRET", json.dumps(result))
        self.assertEqual(calls, [])
        result = Access(policy, transport).read("https://example.invalid/private?token=SECRET")
        self.assertEqual(result["data"], b"ok")
        self.assertNotIn("SECRET", str(result))

    def test_injected_remote_equivalent_to_local_with_independent_source_version(self):
        payload = zipped([("leaf", b"content")])
        def transport(url, timeout, max_bytes):
            return {"data": payload, "status_code": 200, "headers": {"ETag": '"revision-one"'}}
        local = Access().read(self.source(payload), ("leaf",))
        remote = Access({"allowed_origins": ["https://fixture.invalid"]}, transport).read("https://fixture.invalid/data.zip", ("leaf",))
        self.assertEqual(local["content_sha256"], remote["content_sha256"])
        self.assertEqual(remote["source_version"]["etag"], '"revision-one"')
        self.assertNotEqual(local["source_version"]["kind"], remote["source_version"]["kind"])

    def test_redirect_requires_both_permission_and_independent_target_authorization(self):
        calls = []
        def transport(url, *args):
            calls.append(url)
            if url.endswith("/start"):
                return {"status_code": 302, "headers": {"Location": "https://other.invalid/end"}, "data": b""}
            return {"data": b"ok"}
        policy = {"allowed_origins": ["https://fixture.invalid"]}
        denied = Access(policy, transport).read("https://fixture.invalid/start")
        self.assertEqual(denied["reason"], "redirect_not_authorized")
        policy["allow_redirects"] = True
        denied_target = Access(policy, transport).read("https://fixture.invalid/start")
        self.assertEqual(denied_target["reason"], "origin_not_authorized")
        self.assertEqual(len(calls), 2)
        policy["allowed_origins"].append("https://other.invalid")
        self.assertEqual(Access(policy, transport).read("https://fixture.invalid/start")["data"], b"ok")

    def test_injected_transport_errors_are_credential_free(self):
        def transport(*args):
            raise RuntimeError("SECRET bearer-token")
        result = Access({"allowed_origins": ["https://fixture.invalid"]}, transport).read("https://fixture.invalid/file?secret=SECRET")
        self.assertEqual(result["status"], "unavailable")
        self.assertNotIn("SECRET", json.dumps(result))

    def test_real_loopback_http_reads_and_stops_redirect_before_target(self):
        requests = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path)
                if self.path == "/redirect":
                    self.send_response(302)
                    self.send_header("Location", "/never")
                    self.end_headers()
                else:
                    self.send_response(200)
                    self.send_header("Content-Length", "4")
                    self.send_header("ETag", '"loopback-version"')
                    self.end_headers()
                    self.wfile.write(b"data")
            def log_message(self, *args):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            origin = "http://127.0.0.1:" + str(server.server_port)
            access = Access({"allowed_origins": [origin]})
            actual = access.read(origin + "/data")
            self.assertEqual(actual["data"], b"data")
            self.assertEqual(actual["source_version"]["etag"], '"loopback-version"')
            denied = access.read(origin + "/redirect")
            self.assertEqual(denied["reason"], "redirect_not_authorized")
            self.assertEqual(requests, ["/data", "/redirect"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
