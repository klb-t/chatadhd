"""Static guard against dangling pointers from json::find on a temporary.

    const Json* x = json::find(cfg.value("k", Json::object()), "sub");   // BUG

json::find returns a pointer INTO its first argument. When that argument is a by-value Json (the result of
.value(...), json::parse_or(...), a constructed Json, .get<Json>()) the temporary dies at the end of the
declaration and `x` dangles. This happened three times (runner.cpp, graph_entry storage.probe, guardian/data.cpp).
Scans src/, server/src and include/ (also the `if (const Json* x = json::find(...); ...)` form).

An unavoidable hit may be listed in ALLOWLIST as "relative/path.cpp:line" with the reason in a comment; it must be
provably safe (the temporary is bound to a named lifetime-extended reference on the same statement, etc.).
"""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCAN = ["src", "server/src", "include"]
ALLOWLIST: set[str] = set()  # "src/x.cpp:123"  # reason: ...

DECL = re.compile(r"\b(?:const\s+)?(?:Json|auto)\s*\*\s*(?:const\s+)?\w+\s*=\s*(?:loom::)?json::find\s*\(")
# first-argument shapes that yield a Json BY VALUE (.value(key, default); the argument-less iterator it.value() is a reference)
BY_VALUE = re.compile(r"\.value\s*\(\s*[^)\s]|\bparse_or\s*\(|\bJson\s*[({]|\bJson::(?:object|array|parse)\s*\(|\.get\s*<\s*(?:loom::)?Json\s*>|\bjson::(?:object|array)\s*\(")


def first_argument(text: str, start: int) -> str:
    depth, i, n = 1, start, len(text)
    while i < n:
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return text[start:i]
        elif c == "," and depth == 1:
            return text[start:i]
        elif c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        i += 1
    return text[start:]


def scan_text(text: str):
    for m in DECL.finditer(text):
        arg = first_argument(text, m.end())
        if BY_VALUE.search(arg):
            yield text.count("\n", 0, m.start()) + 1, " ".join(arg.split())


class DanglingJsonFind(unittest.TestCase):
    def test_detector_catches_known_shapes(self):
        bad = ['const Json* x = json::find(cfg.value("k", Json::object()), "s");',
               'if (const Json* x = json::find(j.value("a", Json()), "b"); x) {}',
               'const auto* p = json::find(json::parse_or(s, Json()), "k");',
               'auto* p = json::find(Json{{"a", 1}}, "a");']
        good = ['const Json* x = json::find(cfg, "k");', 'const Json* x = json::find(*parsed, w);',
                'const Json* x = json::find(pack.policy("relevance"), "w");',
                'const Json* x = json::find(o.at("a"), "b");']
        for s in bad:
            self.assertTrue(list(scan_text(s)), s)
        for s in good:
            self.assertFalse(list(scan_text(s)), s)

    def test_no_find_on_temporary(self):
        hits = []
        for d in SCAN:
            for p in sorted((ROOT / d).rglob("*")):
                if p.suffix not in (".cpp", ".cc", ".h", ".hpp") or not p.is_file():
                    continue
                rel = p.relative_to(ROOT).as_posix()
                for line, arg in scan_text(p.read_text(encoding="utf-8", errors="replace")):
                    if f"{rel}:{line}" not in ALLOWLIST:
                        hits.append(f"{rel}:{line}: json::find({arg}, ...) points into a temporary")
        self.assertEqual(hits, [], "\n".join(hits))


if __name__ == "__main__":
    unittest.main(verbosity=2)
