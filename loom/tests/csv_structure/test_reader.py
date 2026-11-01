"""Focused synthetic CSV contract tests; Python stdlib only, no backend build."""

import copy
import csv
import hashlib
import io
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "loom" / "tools"))
from csv_structure import CsvError, Options, recognize, read_selection
from csv_structure.reader import canonical

FIXTURES = Path(__file__).parent / "fixtures"
READER = ROOT / "loom" / "tools" / "csv_structure" / "reader.py"
KEY = "fixture:csv/synthetic"


def parse(raw, **options):
    return recognize(raw, source_key=KEY, name="Dane.csv", options=Options(**options))


def values(result):
    obs = {o["id"]: o for o in result["observations"]}
    return [[obs[i]["attrs"]["decoded"]["value"] for i in row["cells"]] for row in result["rows"]]


def selection(result, kind, rows, columns=None):
    s = {"schema": "loom.csv_selection/1", "original_sha256": result["original_sha256"],
         "method_sha256": result["method_sha256"], "kind": kind, "rows": rows}
    if columns is not None:
        s["columns"] = columns
    return s


class CsvStructureTests(unittest.TestCase):
    def assert_code(self, code, fn, *args, **kwargs):
        with self.assertRaises(CsvError) as ctx:
            fn(*args, **kwargs)
        self.assertEqual(ctx.exception.detail["code"], code)
        return ctx.exception.detail

    def test_polish_fixture_quoting_multiline_and_independent_expected(self):
        raw = (FIXTURES / "polish.csv").read_bytes()
        expected = json.loads((FIXTURES / "polish.expected.json").read_text(encoding="utf-8"))
        result = parse(raw, delimiter=";", header=True, language="pl")
        self.assertEqual(values(result), expected["values"])
        self.assertEqual([r["physical_lines"] for r in result["rows"]], expected["physical_lines"])
        self.assertEqual([len(r["cells"]) for r in result["rows"]], expected["widths"])
        self.assertEqual(result["original_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(raw[:3], b"\xef\xbb\xbf")
        self.assertTrue(result["units"][0]["attrs"]["header"])
        self.assertEqual(result["source"]["blob_hash"], result["original_sha256"])
        self.assertEqual(result["source"]["size"], len(raw))
        self.assertEqual(result["warnings"], [{"code": "csv_ragged_rows", "widths": expected["widths"]}])

    def test_every_quote_is_exact_original_utf8_bytes(self):
        raw = (FIXTURES / "polish.csv").read_bytes()
        result = parse(raw, delimiter=";", header=True, language="pl")
        for ob in result["observations"]:
            loc = ob["locator"]
            token = raw[loc["byte_start"]:loc["byte_start"] + loc["byte_len"]]
            self.assertEqual(token.decode(), ob["text"])
            self.assertEqual(ob["attrs"]["evidence_class"], "observed")
            self.assertEqual(ob["attrs"]["decoded"]["evidence_class"], "derived")
            read = read_selection(raw, ob["attrs"]["csv_locator"], source_key=KEY,
                                  options=Options(";", '"', True, "pl"))
            self.assertEqual(read["cells"], [ob])
        multiline = result["observations"][5]
        self.assertEqual(multiline["text"], '"pierwsza\r\ndruga; część"')
        self.assertEqual(multiline["attrs"]["physical_lines"], [2, 3])
        self.assertEqual(multiline["attrs"]["decoded"]["value"], "pierwsza\r\ndruga; część")
        city = result["observations"][4]
        self.assertEqual(city["locator"]["byte_start"], raw.index('"Łódź"'.encode()))
        self.assertEqual(city["locator"]["byte_len"], len('"Łódź"'.encode()))
        # Covers every raw byte, including BOM and endings outside row quotes.
        rebuilt = raw[:3] + b"".join(
            raw[row["locator"]["byte_start"]:row["record_end"]] for row in result["rows"])
        self.assertEqual(rebuilt, raw)

    def test_row_read_and_genesis_cuts_keep_raw_record(self):
        raw = b'a,"b\r\nc"\r\n\r\n""\r\n'
        r = parse(raw)
        for row, unit, cut in zip(r["rows"], r["units"], r["genesis_input"]):
            read = read_selection(raw, row["csv_locator"], source_key=KEY)
            self.assertEqual(read["text"].encode(), raw[cut["begin"]:cut["end"]])
            self.assertEqual(unit["locator"], read["locator"])
            self.assertEqual(cut["text"], read["text"])
            self.assertEqual(unit["bytes"], len(read["text"].encode()))
        self.assertEqual(values(r), [["a", "b\r\nc"], [], [""]])
        self.assertEqual(r["rows"][1]["physical_lines"], [3, 3])

    def test_columns_ranges_are_discontiguous_and_missing_cells_explicit(self):
        raw = b"h1,h2,h3\na,b,c\nd\n\nx,y\n"
        r = parse(raw, header=True)
        opts = Options(header=True)
        col = read_selection(raw, r["columns"][1]["csv_locator"], source_key=KEY, options=opts)
        self.assertEqual([o["text"] for o in col["cells"]], ["h2", "b", "y"])
        self.assertEqual(col["missing"], [{"row": 3, "column": 2}, {"row": 4, "column": 2}])
        self.assertNotIn("byte_start", col)
        subset = read_selection(raw, selection(r, "range", [2, 5], [2, 3]), source_key=KEY, options=opts)
        self.assertEqual([o["text"] for o in subset["cells"]], ["b", "c", "y"])
        self.assertEqual(len(subset["missing"]), 5)
        self.assertIn(r["original_sha256"], subset["citation"])
        self.assertIn(r["method_sha256"], subset["citation"])
        self.assert_code("csv_cell_missing", read_selection, raw, selection(r, "cell", [3, 3], [2, 2]),
                         source_key=KEY, options=opts)

    def test_empty_tokens_blank_rows_trailing_newlines(self):
        for raw, expected in [(b"", []), (b"\xef\xbb\xbf", []), (b"\n", [[]]),
                              (b"\n\n", [[], []]), (b",", [["", ""]]),
                              (b'""', [[""]]), (b"a,\n", [["a", ""]]),
                              (b"a\r\nb\rc\n", [["a"], ["b"], ["c"]])]:
            with self.subTest(raw=raw):
                self.assertEqual(values(parse(raw)), expected)

    def test_escaped_quote_whitespace_and_unicode_dialect(self):
        self.assertEqual(values(parse(b'"a""b", c ,\t\n')), [['a"b', " c ", "\t"]])
        raw = "‹żółć‹§‹a§b‹\r\nŁódź§🌌".encode()
        self.assertEqual(values(parse(raw, delimiter="§", quotechar="‹")), [["żółć", "a§b"], ["Łódź", "🌌"]])

    def test_strict_malformed_quotes_with_physical_error_location(self):
        for raw, code, offset, line in [(b'a,b\r\n"bad\r\n', "csv_unclosed_quote", 5, 2),
                                        (b'a"b,c', "csv_quote_in_unquoted_field", 1, 1),
                                        (b'"a"x,b', "csv_after_quote", 3, 1),
                                        (b'"a" ,b', "csv_after_quote", 3, 1)]:
            with self.subTest(raw=raw):
                detail = self.assert_code(code, parse, raw)
                self.assertEqual((detail["byte_start"], detail["line"]), (offset, line))
                self.assertEqual(detail["original_sha256"], hashlib.sha256(raw).hexdigest())

    def test_invalid_utf8_is_rejected_without_repair(self):
        raw = b"a\r\n\xff"
        detail = self.assert_code("csv_invalid_utf8", parse, raw)
        self.assertEqual((detail["byte_start"], detail["line"]), (3, 2))

    def test_hash_method_tampering_and_bounds(self):
        raw = b"a,b\nc,d"
        r = parse(raw)
        s = r["observations"][0]["attrs"]["csv_locator"]
        self.assert_code("csv_original_mismatch", read_selection, raw + b"\n", s, source_key=KEY)
        self.assert_code("csv_method_mismatch", read_selection, raw, s, source_key=KEY, options=Options(header=True))
        bad = copy.deepcopy(s)
        bad["method_sha256"] = "0" * 64
        self.assert_code("csv_method_mismatch", read_selection, raw, bad, source_key=KEY)
        for rows, cols in [([0, 1], [1, 1]), ([True, True], [1, 1]), ([2, 1], [1, 1]),
                           ([1.0, 1.0], [1, 1]), ([1], [1, 1])]:
            self.assert_code("csv_invalid_selection", read_selection, raw, selection(r, "range", rows, cols), source_key=KEY)
        self.assert_code("csv_selection_out_of_bounds", read_selection, raw, selection(r, "range", [1, 3], [1, 1]), source_key=KEY)
        self.assert_code("csv_selection_out_of_bounds", read_selection, raw, selection(r, "cell", [1, 1], [3, 3]), source_key=KEY)

    def test_reproducible_identity_and_explicit_source_key(self):
        raw = "żółć,a".encode()
        a = parse(raw)
        b = parse(raw)
        self.assertEqual(a, b)
        renamed = recognize(raw, source_key=KEY, name="renamed.csv")
        self.assertEqual(a["source"]["id"], renamed["source"]["id"])
        changed = parse(raw, header=True)
        self.assertEqual(a["source"]["id"], changed["source"]["id"])
        self.assertNotEqual(a["method_sha256"], changed["method_sha256"])
        self.assertNotEqual(a["source"]["id"], recognize(raw, source_key="fixture:other")["source"]["id"])
        # Lock the common native make_id input, including null locator fields.
        unit = a["units"][0]
        native_key = unit["source"] + "\x1f" + canonical(unit["locator"])
        self.assertEqual(unit["id"], "un_" + hashlib.sha256(native_key.encode()).hexdigest()[:16])

    def test_no_header_inference_and_duplicate_empty_labels(self):
        raw = b"x,x,\n1,2,3"
        plain = parse(raw)
        explicit = parse(raw, header=True)
        self.assertTrue(all("header_observation" not in c for c in plain["columns"]))
        self.assertEqual(len({c["header_observation"] for c in explicit["columns"]}), 3)
        self.assertEqual(values(plain), values(explicit))

    def test_invalid_dialect_and_source_identity(self):
        for options in [{"delimiter": ""}, {"delimiter": "ab"}, {"delimiter": "\n"},
                        {"quotechar": ","}, {"header": 1}, {"quotechar": "\ud800"}]:
            self.assert_code("csv_invalid_dialect", Options, **options)
        self.assert_code("csv_expected_original_bytes", recognize, "a", source_key=KEY)
        self.assert_code("csv_invalid_source_identity", recognize, b"a", source_key="")
        self.assert_code("csv_invalid_source_identity", recognize, b"a", source_key="\ud800")
        self.assert_code("csv_invalid_source_identity", recognize, b"a", source_key=KEY, name="\ud800")
        self.assert_code("csv_invalid_language", Options, language="\ud800")
        self.assert_code("csv_invalid_dialect", recognize, b"a", source_key=KEY, options={})

    def test_differential_valid_field_matrix_against_stdlib(self):
        fields = ["", "Zażółć", "a,b", 'a"b', "a\r\nb", " x ", "🌌"]
        # 49 independently serialized records, with both supported common dialects.
        for delimiter in (",", ";"):
            for pair in itertools.product(fields, repeat=2):
                stream = io.StringIO(newline="")
                csv.writer(stream, delimiter=delimiter, lineterminator="\r\n").writerow(pair)
                raw = stream.getvalue().encode()
                self.assertEqual(values(parse(raw, delimiter=delimiter)), [list(pair)])

    def test_cli_json_receipt_and_content_free_errors(self):
        fixture = FIXTURES / "polish.csv"
        run = subprocess.run([sys.executable, "-B", str(READER), str(fixture), "--source-key", KEY,
                              "--delimiter", ";", "--header", "--language", "pl"], capture_output=True, check=False)
        self.assertEqual(run.returncode, 0, run.stderr)
        receipt = json.loads(run.stdout)
        self.assertEqual(receipt, recognize(fixture.read_bytes(), source_key=KEY, options=Options(";", '"', True, "pl")))
        run = subprocess.run([sys.executable, "-B", str(READER), str(FIXTURES / "malformed.csv"),
                              "--source-key", KEY], capture_output=True, check=False)
        self.assertEqual(run.returncode, 2)
        self.assertEqual(run.stdout, b"")
        self.assertEqual(json.loads(run.stderr)["error"]["code"], "csv_unclosed_quote")
        self.assertNotIn(b"SYNTHETIC_PRIVATE_MARKER", run.stderr)

    def test_cli_selection_and_output_limit(self):
        raw = (FIXTURES / "polish.csv").read_bytes()
        r = parse(raw, delimiter=";")
        s = selection(r, "cell", [2, 2], [3, 3])
        with tempfile.TemporaryDirectory(prefix="csv-structure-") as temp:
            selector = Path(temp) / "selector.json"
            selector.write_text(canonical(s))
            argv = [sys.executable, "-B", str(READER), str(FIXTURES / "polish.csv"),
                    "--source-key", KEY, "--delimiter", ";", "--selection", str(selector)]
            run = subprocess.run(argv, capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(json.loads(run.stdout)["cells"][0]["text"], '"pierwsza\r\ndruga; część"')
            run = subprocess.run(argv + ["--max-output-bytes", "1"], capture_output=True, check=False)
            self.assertEqual(run.returncode, 2)
            self.assertEqual(run.stdout, b"")
            self.assertEqual(json.loads(run.stderr)["error"]["code"], "csv_output_limit")
            run = subprocess.run(argv + ["--max-output-bytes", "0"], capture_output=True, check=False)
            self.assertEqual(run.returncode, 0)
            selector.write_text("{")
            run = subprocess.run(argv, capture_output=True, check=False)
            self.assertEqual(run.returncode, 2)
            self.assertEqual(json.loads(run.stderr)["error"]["code"], "csv_invalid_selection_json")


if __name__ == "__main__":
    unittest.main()
