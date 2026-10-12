"""Regression tests for legacy-Python data-loss bugs (A2-CH-001, A2-CH-004,
A4-CH-MOD001). No network, no kivy, no libloom."""
import json
import logging
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
logging.disable(logging.CRITICAL)

from engine.memory_engine import MemoryEngine  # noqa: E402
from engine.models import ModelRegistry  # noqa: E402
from engine import github_sync as gs  # noqa: E402


class _Tmp(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.addCleanup(self._td.cleanup)
        self.dir = Path(self._td.name)


def _rec(i, **kw):
    d = {"id": i, "content": "c" + i, "parent_id": None, "node_type": "text",
         "active": True, "depth": 0, "weight": 1.0, "tags": [],
         "created": "2026-01-01T00:00:00Z", "metadata": {}}
    d.update(kw)
    return d


class MemoryLoss(_Tmp):
    def test_extension_key_survives_update_add_restart(self):
        p = self.dir / "memory.json"
        p.write_text(json.dumps([_rec("a", revision=7)], indent=2))
        m = MemoryEngine(p)
        m.update_node("a", content="new")
        m.add_node("other")
        m2 = MemoryEngine(p)
        raw = {r["id"]: r for r in json.loads(p.read_text())}
        self.assertEqual(raw["a"]["revision"], 7)
        self.assertEqual(m2.get_node("a").content, "new")
        self.assertEqual(list(raw["a"])[-1], "revision")

    def test_valid_malformed_valid_preserved(self):
        p = self.dir / "memory.json"
        bad = {"id": "bad", "weird": [1, 2]}  # missing content
        p.write_text(json.dumps([_rec("a"), bad, _rec("c", x=1)], indent=2))
        m = MemoryEngine(p)
        self.assertEqual(len(m.load_issues), 1)
        self.assertEqual(m.load_issues[0]["index"], 1)
        m.add_node("fresh")
        data = json.loads(p.read_text())
        self.assertEqual(len(data), 4)
        self.assertEqual(data[0], _rec("a"))
        self.assertEqual(data[1], bad)
        self.assertEqual(data[2], _rec("c", x=1))
        self.assertEqual(json.dumps(data[1]), json.dumps(bad))

    def test_unparseable_never_truncated(self):
        p = self.dir / "memory.json"
        blob = b'[{"id": "a", "content": '  # truncated JSON
        p.write_bytes(blob)
        m = MemoryEngine(p)
        self.assertTrue(m.load_issues)
        m.add_node("x")
        self.assertEqual(p.read_bytes(), blob)
        backups = list(self.dir.glob("memory.json.corrupt-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), blob)

    def test_clean_roundtrip_format_unchanged(self):
        p = self.dir / "memory.json"
        m = MemoryEngine(p)
        nid = m.add_node("hello")
        d = json.loads(p.read_text())
        self.assertEqual(list(d[0]), ["id", "content", "parent_id", "node_type",
                                      "active", "depth", "weight", "tags",
                                      "created", "metadata"])
        self.assertEqual(d[0]["id"], nid)


class _Resp:
    def __init__(self, items):
        self._i = items

    def raise_for_status(self):
        pass

    def json(self):
        return self._i


class GitHubStatus(_Tmp):
    def _sync(self, content: bytes, remote_item):
        local = self.dir / "local"
        local.mkdir()
        (local / "f.md").write_bytes(content)
        cfg = gs.SyncConfig(repo="o/r", local_path=str(local), token="tok")
        sync = gs.GitHubSync(cfg)
        item = {"type": "file", "path": "f.md", "size": len(content), "download_url": ""}
        item.update(remote_item)
        with mock.patch.object(gs.requests, "get", return_value=_Resp([item])):
            return sync.get_sync_status()

    def test_same_size_different_content_not_synced(self):
        other = gs.git_blob_sha(b"bbbb")
        st = self._sync(b"aaaa", {"sha": other})
        self.assertEqual(st[0].status, "modified")

    def test_identical_synced(self):
        st = self._sync(b"aaaa", {"sha": gs.git_blob_sha(b"aaaa")})
        self.assertEqual(st[0].status, "synced")

    def test_missing_sha_not_synced(self):
        st = self._sync(b"aaaa", {})
        self.assertEqual(st[0].status, "uncertain")

    def test_blob_sha_known_vector(self):
        self.assertEqual(gs.git_blob_sha(b""), "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391")


class ModelCache(_Tmp):
    def _reg(self, p):
        return ModelRegistry(p, mock.MagicMock(), mock.MagicMock())

    MODELS = [{"id": "a/b", "name": "B", "context_length": 8,
               "pricing": {"prompt": "0.1", "completion": "0.2"},
               "description": "desc here", "future": {"k": 1}}]

    def test_wrapped_keeps_fields(self):
        p = self.dir / "models.json"
        p.write_text(json.dumps({"data": self.MODELS}))
        r = self._reg(p)
        self.assertEqual(r.cache_status, "ok")
        self.assertEqual(r.all(), self.MODELS)
        r2 = self._reg(p)  # after canonical re-save
        self.assertEqual(r2.all(), self.MODELS)
        self.assertIsInstance(json.loads(p.read_text()), list)

    def test_list_form_unchanged_bytes(self):
        p = self.dir / "models.json"
        txt = json.dumps(self.MODELS, indent=1, ensure_ascii=False)
        p.write_text(txt, encoding="utf-8")
        self._reg(p)
        self.assertEqual(p.read_text(encoding="utf-8"), txt)

    def test_corrupt_kept_on_disk(self):
        p = self.dir / "models.json"
        blob = b'{"data": [{"id": '
        p.write_bytes(blob)
        r = self._reg(p)
        self.assertEqual(r.cache_status, "corrupt")
        self.assertTrue(r.cache_reason)
        self.assertEqual(p.read_bytes(), blob)

    def test_unloadable_structure_preserved_and_moved_on_write(self):
        p = self.dir / "models.json"
        blob = b'"just a string"'
        p.write_bytes(blob)
        r = self._reg(p)
        self.assertEqual(r.cache_status, "corrupt")
        self.assertEqual(p.read_bytes(), blob)
        r._models = [{"id": "x/y"}]
        r._save()
        backups = list(self.dir.glob("models.json.corrupt-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), blob)
        self.assertEqual(r.cache_status, "ok")

    def test_missing_status(self):
        r = self._reg(self.dir / "models.json")
        self.assertEqual(r.cache_status, "missing")


if __name__ == "__main__":
    unittest.main()
