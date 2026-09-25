"""engine.memory_engine.MemoryEngine <-> loom::MemoryEngine: cross-reads of
memory.json (each side reads the other's file) and get_active_context()
parity.
"""
import unittest
from dataclasses import asdict

from compat_common import CompatTestCase, diff_hint, tool


def py_dump(mem):
    return {
        "nodes": [asdict(n) for n in mem.get_all()],
        "context": mem.get_active_context(),
        "graph": mem.get_graph_data(),
    }


class MemoryCompat(CompatTestCase):
    def _spec(self):
        # A representative tree: nested folders, text with auto-tags, a
        # file/dir node, a weighted node, an inactive node, Unicode content.
        # NOTE: "ref"/"parent_ref" index the sequence of *add* steps only
        # (0-based), matching tool_memory.cpp's cmd_memory_build.
        return [
            {"kind": "add", "content": "Root Folder", "node_type": "folder"},                       # add#0
            {"kind": "add", "content": "we need to review the git deploy pipeline and database backend",
             "parent_ref": 0, "node_type": "text"},                                                  # add#1
            {"kind": "add", "content": "config.yaml", "parent_ref": 0, "node_type": "file",
             "metadata": {"path": "/etc/app/config.yaml"}},                                          # add#2
            {"kind": "add", "content": "logs dir", "parent_ref": 0, "node_type": "dir",
             "metadata": {"path": "/var/log/app"}},                                                  # add#3
            {"kind": "add", "content": "Zażółć gęślą jaźń", "node_type": "text",
             "tags": ["custom1", "custom2"]},                                                        # add#4
            {"kind": "add", "content": "weighted node", "node_type": "text"},                        # add#5
            {"kind": "update", "ref": 5, "fields": {"weight": 2.5}},
            {"kind": "add", "content": "hidden node", "node_type": "text"},                          # add#6
            {"kind": "update", "ref": 6, "fields": {"active": False}},
            {"kind": "add", "content": "empty tags folder", "node_type": "folder"},                  # add#7
        ]

    def test_cpp_builds_python_reads(self):
        d = self.tmp.path / "cpp_build"
        cpp_dump = tool("memory-build", d, self.tmp.file_json("spec.json", self._spec()))

        from engine.memory_engine import MemoryEngine
        py_mem = MemoryEngine(d / "memory.json")
        py = py_dump(py_mem)

        h = diff_hint(py, cpp_dump)
        self.assertIsNone(h, f"cpp-built memory.json read differently by python: {h}")

    def test_python_builds_cpp_reads(self):
        d = self.tmp.path / "py_build"
        d.mkdir(parents=True)
        from engine.memory_engine import MemoryEngine
        from core.semantic import analyzer

        py_mem = MemoryEngine(d / "memory.json", semantic_analyzer=analyzer)
        ids = []
        root = py_mem.add_node("Root Folder", node_type="folder")
        ids.append(root)
        ids.append(py_mem.add_node(
            "we need to review the git deploy pipeline and database backend", parent_id=root))
        ids.append(py_mem.add_node("config.yaml", parent_id=root, node_type="file",
                                   metadata={"path": "/etc/app/config.yaml"}))
        ids.append(py_mem.add_node("logs dir", parent_id=root, node_type="dir",
                                   metadata={"path": "/var/log/app"}))
        ids.append(py_mem.add_node("Zażółć gęślą jaźń", tags=["custom1", "custom2"]))
        w = py_mem.add_node("weighted node")
        ids.append(w)
        py_mem.update_node(w, weight=2.5)
        h_id = py_mem.add_node("hidden node")
        ids.append(h_id)
        py_mem.update_node(h_id, active=False)
        ids.append(py_mem.add_node("empty tags folder", node_type="folder"))

        py = py_dump(py_mem)
        cpp_dump = tool("memory-dump", d)

        h = diff_hint(py, cpp_dump)
        self.assertIsNone(h, f"python-built memory.json read differently by C++: {h}")

    def test_get_active_context_identical_with_deep_nesting(self):
        # Deep nesting + mixed weights/tags, C++ builds, both compute context.
        spec = [{"kind": "add", "content": "level0", "node_type": "folder"}]
        for i in range(1, 8):
            spec.append({"kind": "add", "content": f"level{i} node #{i}", "parent_ref": i - 1,
                        "node_type": "folder" if i % 2 == 0 else "text", "tags": [f"t{i}"]})
        d = self.tmp.path / "deep"
        cpp_dump = tool("memory-build", d, self.tmp.file_json("spec.json", spec))
        from engine.memory_engine import MemoryEngine
        py_mem = MemoryEngine(d / "memory.json")
        self.assertEqual(py_mem.get_active_context(), cpp_dump["context"])

    def test_recursive_delete_orphaning_matches(self):
        spec = [
            {"kind": "add", "content": "a", "node_type": "folder"},
            {"kind": "add", "content": "b", "parent_ref": 0, "node_type": "folder"},
            {"kind": "add", "content": "c", "parent_ref": 1, "node_type": "text"},
            {"kind": "delete", "ref": 0, "recursive": False},  # b, c orphaned but kept
        ]
        d = self.tmp.path / "orphan"
        cpp_dump = tool("memory-build", d, self.tmp.file_json("spec.json", spec))
        from engine.memory_engine import MemoryEngine
        py_mem = MemoryEngine(d / "memory.json")
        py = py_dump(py_mem)
        self.assertEqual(len(py["nodes"]), 2)
        h = diff_hint(py, cpp_dump)
        self.assertIsNone(h)


if __name__ == "__main__":
    unittest.main(verbosity=2)
