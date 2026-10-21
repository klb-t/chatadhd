"""Detection of encoded data (encoded.py + op select.encoded): synthetic input only; findings never carry content."""
import base64
import json
import unittest

from _support import ExtractTest

from loom.tools.extract import encoded
from loom.tools.extract.engine import Engine

PNG = base64.b64encode(b"\x89PNG\r\n\x1a\n" + bytes(range(256))).decode()


class EncodedTest(ExtractTest):
    def test_kinds(self) -> None:
        text = f"a {PNG} b data:image/png;base64,{PNG} c " + "ab" * 40 + " " + "%41" * 14 + " " + "\\u0041" * 14
        got = {f["kind"] for f in encoded.scan_text(text)}
        self.assertEqual(got, set(encoded.KINDS))
        b64 = next(f for f in encoded.scan_text(f"x {PNG} y") if f["kind"] == "base64")
        self.assertEqual(b64["decoded_type"], "image/png")
        self.assertNotIn(PNG, json.dumps(b64))

    def test_plain_text_and_paths_are_not_encoded(self) -> None:
        self.assertEqual(list(encoded.scan_text("zwykły tekst " * 40 + "/usr/local/share/a/very/long/path/to/some/file/name/x.py")), [])

    def test_thresholds_are_parameters(self) -> None:
        self.assertEqual(list(encoded.scan_text(f"x {PNG[:40]} y")), [])
        self.assertEqual(len(list(encoded.scan_text(f"x {PNG[:40]} y", {"min_base64": 20}))), 1)

    def test_op_selects_conversations(self) -> None:
        f = self.tmp / "c.json"
        f.write_text(json.dumps({"title": "t", "messages": [{"role": "user", "content": "look " + PNG}, {"role": "assistant", "content": "ok"}]}))
        eng = Engine()
        self._engines.append(eng)
        eng.set_sources([str(f)])
        res = eng.op("select.encoded", {})
        self.assertEqual(res["counts"]["conversations"], 1)
        self.assertEqual(res["by_kind"], {"base64": 1})
        self.assertEqual(eng.selection_summary()["conversations"], 1)


if __name__ == "__main__":
    unittest.main()
