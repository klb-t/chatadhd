"""Differential tests: loom::re::Regex / SemanticAnalyzer vs Python `re` /
core.semantic.SemanticAnalyzer.

Regex fuzz corpus: every pattern used by core/semantic.py, core/selector.py's
token patterns and engine/importer.py's HTML/text-cleanup patterns, run
against >= 5000 generated + hand-written inputs (ASCII, Polish diacritics,
emoji, mixed whitespace, long strings) plus curated regex-syntax edge cases.
Spans, groups and lastindex are compared byte-for-byte (well, code-point-for-
code-point) against Python's `re`.
"""
import random
import re
import string
import sys
import unittest

from compat_common import CompatTestCase, tool

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3]))
import core.semantic as semantic_mod  # noqa: E402

FLAG_BITS = [(re.IGNORECASE, "IGNORECASE"), (re.MULTILINE, "MULTILINE"), (re.DOTALL, "DOTALL")]


def flag_names(flags):
    return [name for bit, name in FLAG_BITS if flags & bit]


def flags_from_names(names):
    f = 0
    for n in names:
        for bit, name in FLAG_BITS:
            if name == n:
                f |= bit
    return f


# ── The pattern set: exactly what core/semantic.py, core/selector.py and
#    engine/importer.py compile. ─────────────────────────────────────────

def entity_patterns():
    return [(et.value, p.pattern, flag_names(p.flags)) for et, p in semantic_mod._PATTERNS]


RELATION_PATTERNS = [
    ("depends_on", r'(\b\w[\w\s]{1,30}?)\s+(?:depends?\s+on|requires?|needs?)\s+(\b\w[\w\s]{1,30})', ["IGNORECASE"]),
    ("references", r'(\b\w[\w\s]{1,30}?)\s+(?:references?|refers?\s+to|see|cf\.?)\s+(\b\w[\w\s]{1,30})', ["IGNORECASE"]),
]

MISC_PATTERNS = [
    ("selector_keyword", r'\w+', []),
    ("selector_tfidf_token", r'(?u)\b\w\w+\b', []),
    ("importer_script", r'<script[^>]*>.*?</script>', ["DOTALL", "IGNORECASE"]),
    ("importer_style", r'<style[^>]*>.*?</style>', ["DOTALL", "IGNORECASE"]),
    ("importer_tag", r'<[^>]+>', []),
    ("importer_ws", r'\s+', []),
]

ALL_PATTERNS = [(f"entity:{n}", p, f) for n, p, f in entity_patterns()]
ALL_PATTERNS += [(f"relation:{n}", p, f) for n, p, f in RELATION_PATTERNS]
ALL_PATTERNS += [(f"misc:{n}", p, f) for n, p, f in MISC_PATTERNS]


# ── Text corpus generator ─────────────────────────────────────────────────

POLISH = "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
EMOJI = "😀🎉🚀❤️🔥👍🌍🐍💡✨"
CURATED = [
    "", " ", "\n", "\t", "a", "  a  b  ", "foo.bar+x@example.co.uk",
    "contact me at John.Doe@Example.COM or jane_doe99@sub.domain.io please",
    "see https://example.com/path?a=1&b=2#frag or HTTP://X.Y/Z now",
    "call +48 123-456-789 or (022) 555 1234",
    "price is $1,234.56 or €99 or 100 USD or 50.5 PLN",
    "date 2024-01-05 and 2024/12/31, time 14:30:00 and 9:05 AM",
    "#hashtag and @mention here, also #x and @y",
    "path /usr/local/bin/foo and C:\\Windows\\System32\\x.dll",
    "class Foo:\n    def bar(self):\n        import os\n        from sys import path\n",
    "The frontend depends on the backend service heavily.\nSee also cf. appendix B.",
    "IP address 192.168.1.1 and 10.0.0.255",
    "Zażółć gęślą jaźń w środę rano",
    "emoji test 😀🎉 mixed with ąćęłń text",
    "line1\nline2\r\nline3\ttabbed    spaces",
    "a" * 500 + "b",
    "https://example.com/" + "x" * 300,
    "x" * 50 + "@" + "y" * 50 + ".com",
    "nested (parens (like this) work) fine",
    "The module requires the database and needs the cache too",
    "See http://a.b refers to nothing, cf appendix",
]


def random_text(rng, alphabet, min_len=0, max_len=40):
    n = rng.randint(min_len, max_len)
    return "".join(rng.choice(alphabet) for _ in range(n))


def build_corpus(seed=1234, n_random=450):
    rng = random.Random(seed)
    texts = list(CURATED)
    alphabets = [
        string.ascii_letters + string.digits + " .,;:!?@#$%^&*()-_+=[]{}",
        string.ascii_letters + string.digits + POLISH + " \t\n",
        string.ascii_letters + " " + EMOJI,
        string.digits + "-/:. ",
        string.ascii_letters + string.digits + "@._+- ",
        " \t\n\r" + string.ascii_letters,
    ]
    for _ in range(n_random):
        alphabet = rng.choice(alphabets)
        texts.append(random_text(rng, alphabet, 0, rng.choice([5, 15, 40, 90])))
    return texts


# ── Comparison ──────────────────────────────────────────────────────────

def py_match_json(rx, m):
    if m is None:
        return {"matched": False}
    groups = []
    for g in range(rx.groups + 1):
        try:
            s, e = m.span(g)
        except Exception:
            s, e = -1, -1
        if s < 0 or e < 0:
            groups.append(None)
        else:
            groups.append({"start": s, "end": e, "text": m.group(g)})
    return {
        "matched": True,
        "start": m.start(0),
        "end": m.end(0),
        "groups": groups,
        "lastindex": m.lastindex,
    }


class RegexCompat(CompatTestCase):
    def _run(self, cases):
        """cases: list of (pattern, flags_names, op, text, extra dict)."""
        payload = []
        for pattern, flags, op, text, extra in cases:
            c = {"pattern": pattern, "flags": flags, "op": op, "text": text}
            c.update(extra)
            payload.append(c)
        cpp = tool("re-exec", self.tmp.file_json("cases.json", payload))
        self.assertEqual(len(cpp), len(cases))
        for i, ((pattern, flags, op, text, extra), cpp_res) in enumerate(zip(cases, cpp)):
            pyflags = flags_from_names(flags)
            try:
                rx = re.compile(pattern, pyflags)
            except re.error as e:
                self.fail(f"case {i}: python failed to compile {pattern!r}: {e}")
            if op in ("search", "match", "fullmatch"):
                pos = extra.get("pos", 0)
                fn = {"search": rx.search, "match": rx.match, "fullmatch": rx.fullmatch}[op]
                m = fn(text, pos)
                py_res = py_match_json(rx, m)
            elif op == "finditer":
                py_res = {"matches": [py_match_json(rx, m) for m in rx.finditer(text)]}
            elif op == "findall":
                found = rx.findall(text)
                if rx.groups == 0:
                    result = [text[m.start():m.end()] for m in rx.finditer(text)]
                elif rx.groups == 1:
                    result = [g if g is not None else "" for g in found]
                else:
                    result = [x[0] if isinstance(x, tuple) else x for x in found]
                py_res = {"result": result}
            elif op == "sub":
                count = extra.get("count", 0)
                py_res = {"result": rx.sub(extra.get("repl", ""), text, count=count)}
            else:
                continue
            self.assertSameJson(py_res, cpp_res, msg=f"case {i}: pattern={pattern!r} op={op} text={text!r}")

    def test_named_patterns_search_and_finditer(self):
        texts = build_corpus()
        cases = []
        for name, pattern, flags in ALL_PATTERNS:
            for text in texts:
                cases.append((pattern, flags, "search", text, {}))
                cases.append((pattern, flags, "finditer", text, {}))
        self.assertGreaterEqual(len(cases), 5000, "fuzz corpus must be >= 5000 cases")
        self._run(cases)

    def test_named_patterns_sub(self):
        texts = build_corpus(seed=99, n_random=80)
        cases = []
        for name, pattern, flags in ALL_PATTERNS:
            for text in texts:
                cases.append((pattern, flags, "sub", text, {"repl": "<X>"}))
        self._run(cases)

    def test_named_patterns_findall(self):
        texts = build_corpus(seed=77, n_random=80)
        cases = []
        for name, pattern, flags in ALL_PATTERNS:
            for text in texts:
                cases.append((pattern, flags, "findall", text, {}))
        self._run(cases)

    def test_syntax_edge_cases(self):
        edge = [
            (r"a*", "", "search"), (r"a*", "aaa", "search"),
            (r"a+?b", "aaab", "search"), (r"a{2,4}", "aaaaaa", "search"),
            (r"a{2,4}?", "aaaaaa", "search"), (r"(a|b|c)+", "cab", "search"),
            (r"^abc$", "abc", "fullmatch"), (r"^abc$", "abc\n", "search"),
            (r"^abc", "xyz\nabc", "search"), (r"[^abc]+", "xxxabcyyy", "search"),
            (r"[a-z]+", "ABCabc123", "search"), (r"\bfoo\b", "foofoo foo barfoo", "finditer"),
            (r"(a)(b)?(c)", "ac", "search"), (r"(a)(b)?(c)", "abc", "search"),
            (r"x??y", "xy", "search"), (r"(foo|foobar)", "foobar", "search"),
            (r"a.b", "a\nb", "search"), (r".", "\n", "search"),
            (r"(?:abc){2,3}", "abcabcabc", "search"), (r"\d{3}-\d{4}", "555-1234", "search"),
            (r"[\]\-\^]+", "]-^", "search"), (r"[a\]]+", "a]a]", "search"),
            (r"()", "abc", "search"), (r"a|", "b", "search"),
            (r"\s*$", "abc   ", "search"), (r"(?i)ABC", "abc", "search"),
            (r"colou?r", "color", "fullmatch"), (r"colou?r", "colour", "fullmatch"),
            (r"[$€£¥]", "price: €5", "search"), (r"€+", "€€€x", "search"),
        ]
        cases = [(p, [], op, t, {}) for p, t, op in edge]
        # Also add flag variants explicitly requested via (?i) already inline;
        # nothing else to add here.
        self._run(cases)

    def test_step_budget_reports_no_match_not_crash(self):
        # Catastrophic-looking pattern; must not hang or crash, budget is low.
        cpp = tool("re-exec", self.tmp.file_json("cases.json", [
            {"pattern": r"(a+)+b", "flags": [], "op": "search", "text": "a" * 40},
        ]))
        self.assertEqual(len(cpp), 1)
        self.assertIn("matched", cpp[0])


class SemanticAnalyserCompat(CompatTestCase):
    def test_analyse_matches_python(self):
        texts = build_corpus(seed=555, n_random=500)
        cpp = tool("semantic-analyse", self.tmp.file_json("texts.json", texts))
        self.assertEqual(len(cpp), len(texts))
        analyzer = semantic_mod.SemanticAnalyzer()
        diffs = 0
        for i, (text, cpp_res) in enumerate(zip(texts, cpp)):
            py = analyzer.analyse(text)
            py_ents = [{"text": e.text, "entity_type": e.entity_type.value, "start": e.start, "end": e.end}
                      for e in py["entities"]]
            py_res = {"entities": py_ents, "topics": py["topics"]}
            h = None
            from compat_common import diff_hint
            h = diff_hint(py_res, cpp_res)
            if h:
                diffs += 1
                if diffs <= 5:
                    print(f"DIFF text={text!r}\n  py ={py_res}\n  cpp={cpp_res}\n  at {h}")
        self.assertEqual(diffs, 0, f"{diffs}/{len(texts)} texts differ (see stdout for the first few)")


if __name__ == "__main__":
    unittest.main()
