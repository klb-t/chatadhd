"""Requirement 3 (multi-gigabyte files), review F33: the ChatGPT ``{"conversations": [...]}`` wrapper streams
like the bare array (the whole member is never loaded at once), and a generated large export is read within a
memory bound that is measured in a separate process (the resident units are the cost; the bound is a preset of
this test, not of the tool)."""
import json
import subprocess
import sys
import textwrap

from _support import REPO, ExtractTest


def _conv(i: int) -> dict:
    mapping = {"root": {"id": "root", "message": None, "parent": None, "children": ["m1"]}}
    prev = "root"
    for j in range(1, 7):
        mid = f"m{j}"
        mapping[mid] = {"id": mid, "parent": prev, "children": [f"m{j + 1}"] if j < 6 else [],
                        "message": {"id": mid, "author": {"role": "user" if j % 2 else "assistant"}, "create_time": 1760000000 + i * 60 + j,
                                    "content": {"content_type": "text", "parts": [f"Rozmowa {i}, wiadomość {j}: " + "treść " * 40]}}}
        prev = mid
    return {"title": f"Rozmowa {i}", "create_time": 1760000000 + i * 60, "update_time": 1760000000 + i * 60 + 10,
            "mapping": mapping, "current_node": "m6", "conversation_id": f"conv-{i:06d}"}


class LargeFilesTest(ExtractTest):
    def _export(self, n: int):
        d = self.tmp / "export"
        d.mkdir()
        with open(d / "conversations-000.json", "w", encoding="utf-8") as fh:
            fh.write('{"conversations": [')
            for i in range(n):
                fh.write(("," if i else "") + json.dumps(_conv(i), ensure_ascii=False))
            fh.write("]}")
        return d

    def test_wrapper_form_streams_without_loading_the_member(self):
        from loom.tools.extract.readers import chatgpt
        d = self._export(30)
        orig = chatgpt.load_member_json

        def refuse(member):  # the wrapper form must not be loaded whole
            raise AssertionError(f"load_member_json called on {member.name}")
        chatgpt.load_member_json = refuse
        try:
            eng, units = self.units(d)
        finally:
            chatgpt.load_member_json = orig
        convs = [u for u in units if u.kind == "conversation"]
        self.assertEqual(len(convs), 30)
        self.assertEqual(convs[0].source["locator"]["json_pointer"], "/conversations/0")

    def test_large_export_is_read_within_a_memory_bound(self):
        n = 2000
        d = self._export(n)
        size = (d / "conversations-000.json").stat().st_size
        script = textwrap.dedent("""
            import json, resource, sys
            sys.path.insert(0, sys.argv[1])
            from loom.tools.extract.engine import Engine
            base = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
            eng = Engine()
            eng.set_sources([sys.argv[2]])
            eng.op("scan", {})
            eng.op("read", {"keep_raw": False})
            peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
            print(json.dumps({"base_kb": base, "peak_kb": peak, "units": len(eng.units_raw)}))
        """)
        out = subprocess.run([sys.executable, "-I", "-c", script, str(REPO), str(d)], capture_output=True, text=True, timeout=300,
                             env={"LOOM_EXTRACT_STATE": str(self.tmp / "state"), "PATH": "/usr/bin:/bin"})
        self.assertEqual(out.returncode, 0, out.stderr)
        res = json.loads(out.stdout.strip().splitlines()[-1])
        self.assertEqual(res["units"], n)
        grew = (res["peak_kb"] - res["base_kb"]) * 1024
        # units with their text stay resident (index-then-reread is not done: spec §11); the bound catches a
        # regression such as loading the member whole next to its units
        self.assertLess(grew, 8 * size, f"read {size} B, memory grew {grew} B")
