"""Foundation utilities vs CPython: json.dumps, float repr, UTF-8 decoding with
errors="replace", str.lower/upper/strip, datetime isoformat, id format, and
the contract data Loom embeds from the Python sources."""
import datetime
import json
import random
import re
import struct
import unittest

from compat_common import CompatTestCase, tool


def json_corpus():
    rnd = random.Random(1234)
    values = [
        None, True, False, 0, -1, 2 ** 53, -(2 ** 63), 0.0, -0.0, 0.1, 1.0, 1e16, 1e15, 1.5e-5, 0.0001, 1e-4,
        123456789.125, 3.141592653589793, 1e308, 5e-324, 2.5e-7, 100.0, 1e22,
        "", "plain", "quote\"back\\slash", "ctrl\x00\x01\x1f\x7f", "tab\tnl\nret\rbs\bff\f",
        "zażółć gęślą jaźń", "emoji 😀🎉", "cjk 漢字", "mixed     ﻿",
        [], {}, [1, [2, [3, []]]], {"a": {}, "b": [], "c": {"d": [None]}},
        {"z": 1, "a": 2, "m": {"y": 1, "b": 2}},  # insertion order kept, sort_keys only for canonical
        {"ünïcödé": "ключ", "😀": ["x"]},
    ]
    for _ in range(200):
        values.append(struct.unpack("<d", struct.pack("<Q", rnd.getrandbits(64)))[0])
    values = [v for v in values if not (isinstance(v, float) and (v != v or v in (float("inf"), float("-inf"))))]
    for _ in range(50):
        values.append({f"k{i}": rnd.choice([1, 2.5, "s", None, [1, "x"], {"n": rnd.random()}]) for i in range(5)})
    return values


def utf8_corpus():
    rnd = random.Random(99)
    cases = [
        b"", b"ascii", "zażółć GĘŚLĄ jaźń".encode(), "ΟΔΟΣ ΣΑ Σ".encode(), "İstanbul ß ǅ ﬁ".encode(),
        b"\xff", b"a\xffz", b"\xe2\x82", b"\xe2\x82A", b"\xed\xa0\x80", b"\xf0\x9f\x98", b"\xf4\x90\x80\x80",
        b"\xc0\xaf", b"\xe0\x80\xaf", b"\x80\x80", "  \t hi 　\n".encode(), " x ".encode(),
        " \x1c\x1d\x1e\x1f\x85 ".encode(), "Ǆǅǆ ΐ ΰ ŉ".encode(), "ⅷ Ⅻ ⓐ Ⓐ".encode(),
    ]
    for _ in range(300):
        n = rnd.randint(0, 24)
        if rnd.random() < 0.5:
            cases.append(bytes(rnd.getrandbits(8) for _ in range(n)))
        else:
            cases.append("".join(chr(rnd.choice([rnd.randint(0x20, 0x7e), rnd.randint(0xa0, 0x2fff),
                                                  rnd.randint(0x1f300, 0x1f6ff)])) for _ in range(n)).encode())
    return cases


class UtilCompatTest(CompatTestCase):

    def test_json_dumps_is_byte_identical(self):
        values = json_corpus()
        out = tool("json-dumps", self.tmp.file_json("values.json", values))
        self.assertEqual(len(out), len(values))
        for v, o in zip(values, out):
            self.assertEqual(o["compact"], json.dumps(v), repr(v)[:80])
            self.assertEqual(o["indent2"], json.dumps(v, indent=2, ensure_ascii=False), repr(v)[:80])
            self.assertEqual(o["canonical"], json.dumps(v, sort_keys=True, separators=(",", ":"), ensure_ascii=False))

    def test_float_repr(self):
        rnd = random.Random(7)
        floats = [0.1, 1.0, 1e16, 1e-5, 123.456, 2.0 ** 70, 1 / 3, 5e-324, 1.7976931348623157e308]
        floats += [struct.unpack("<d", struct.pack("<Q", rnd.getrandbits(64)))[0] for _ in range(2000)]
        floats = [f for f in floats if f == f and abs(f) != float("inf")]
        out = tool("float-repr", self.tmp.file_json("floats.json", floats))
        for f, s in zip(floats, out):
            self.assertEqual(s, repr(f))

    def test_utf8_semantics_match_python_str(self):
        cases = utf8_corpus()
        out = tool("utf8", self.tmp.file_json("utf8.json", [c.hex() for c in cases]))
        for raw, o in zip(cases, out):
            s = raw.decode("utf-8", errors="replace")
            with self.subTest(raw=raw):
                self.assertEqual(bytes.fromhex(o["repaired"]), s.encode())
                self.assertEqual(o["valid"], _is_valid_utf8(raw))
                self.assertEqual(o["length"], len(s))
                self.assertEqual(bytes.fromhex(o["lower"]).decode(), s.lower())
                self.assertEqual(bytes.fromhex(o["upper"]).decode(), s.upper())
                self.assertEqual(bytes.fromhex(o["strip"]).decode(), s.strip())
                self.assertEqual(bytes.fromhex(o["prefix5"]).decode(), s[:5])
                self.assertEqual(o["blank"], not s.strip())

    def test_timestamps_match_utcnow_isoformat(self):
        micros = [0, 1, 999999, 1_000_000, 1_700_000_000_000_000, 1_700_000_000_123_456, 1_234_567_890_000_001,
                  4_102_444_800_000_000]
        out = tool("time-format", self.tmp.file_json("micros.json", micros))
        for us, s in zip(micros, out):
            dt = datetime.datetime(1970, 1, 1) + datetime.timedelta(microseconds=us)
            self.assertEqual(s, dt.isoformat() + "Z")
        now = datetime.datetime.fromisoformat(out[-1][:-1])
        self.assertLess(abs((datetime.datetime.utcnow() - now).total_seconds()), 60)

    def test_generated_ids_have_python_shape(self):
        for prefix in ("c_", "m_", "vg_", "n_", "l_"):
            ids = tool("gen-ids", prefix, 200)
            self.assertEqual(len(set(ids)), 200)
            for i in ids:
                self.assertRegex(i, "^" + re.escape(prefix) + r"[0-9a-f]{12}$")

    def test_embedded_contract_data_matches_python_sources(self):
        from core import semantic
        from engine import batch_api, config, semantic_llm
        data = tool("contract-data")
        self.assertEqual(data["analysis_prompt"], semantic_llm._ANALYSIS_PROMPT)
        self.assertEqual(data["analysis_prompt"], batch_api._ANALYSIS_PROMPT)
        # Same keys, same values, same order as engine.config.DEFAULTS.
        self.assertEqual(list(data["config_defaults"].items()), list(config.DEFAULTS.items()))
        rules = data["analyzer_rules"]
        py_patterns = [(et.value, p.pattern) for et, p in semantic._PATTERNS]
        self.assertEqual([(e["entity_type"], e["pattern"]) for e in rules["entity_patterns"]], py_patterns)
        for e, (_, p) in zip(rules["entity_patterns"], semantic._PATTERNS):
            self.assertEqual("IGNORECASE" in e["flags"], bool(p.flags & re.IGNORECASE))
            self.assertEqual("MULTILINE" in e["flags"], bool(p.flags & re.MULTILINE))
        self.assertEqual([(t["topic"], t["keywords"]) for t in rules["topics"]], list(semantic._TOPIC_KEYWORDS.items()))
        self.assertEqual([r["predicate"] for r in rules["relation_patterns"]], ["depends_on", "references"])


def _is_valid_utf8(b):
    try:
        b.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


if __name__ == "__main__":
    unittest.main(verbosity=2)
