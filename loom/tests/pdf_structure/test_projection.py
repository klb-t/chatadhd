import copy
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import unittest

HERE = Path(__file__).resolve().parent
MODULE = HERE.parents[1] / "src/text/pdf_structure/projection.py"
SPEC = importlib.util.spec_from_file_location("pdf_projection", MODULE)
projection = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(projection)


class ProjectionTests(unittest.TestCase):
    def setUp(self):
        self.request = json.loads((HERE / "reader-output.json").read_text())

    def build(self, texts=None):
        if texts is not None:
            self.request["document"] = {
                "text": "\f".join(texts), "pages": len(texts),
                "pages_without_text": [i + 1 for i, t in enumerate(texts) if not t.strip(" \t\r\n\v")]}
        return projection.project(self.request)

    def assert_partition(self, result):
        raw = result["text"].encode()
        cursor = 0
        for block, span in zip(result["blocks"], result["projection"]["ranges"]):
            begin, end = span["byte_range"]
            a, b = span["text_range"]
            self.assertEqual(begin, cursor)
            self.assertTrue(begin <= a <= b <= end <= len(raw))
            self.assertEqual(raw[a:b].decode(), block["text"])
            page = result["pages"][block["locator"]["page"] - 1]
            x, y = block["locator"]["page_text_range"]
            self.assertEqual([page["byte_range"][0] + x, page["byte_range"][0] + y], [a, b])
            self.assertLessEqual(b, page["byte_range"][1])
            cursor = end
        self.assertEqual(cursor, len(raw))

    def test_synthetic_source_hash_and_exact_bytes(self):
        raw = (HERE / "original.synthetic").read_bytes()
        self.assertEqual(self.request["source"], {"sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw)})
        result = self.build()
        self.assertEqual(result, json.loads((HERE / "projection-fixture.json").read_text()))
        self.assertEqual(result["text"], self.request["document"]["text"])
        self.assertEqual(result["projection"]["sha256"], hashlib.sha256(result["text"].encode()).hexdigest())
        self.assert_partition(result)

    def test_blank_page_retained_and_not_called_paragraph(self):
        result = self.build()
        self.assertEqual([p["page"] for p in result["pages"]], [1, 2, 3])
        self.assertFalse(result["pages"][1]["has_text"])
        gap = [b for b in result["blocks"] if b["locator"]["page"] == 2]
        self.assertEqual([b["kind"] for b in gap], ["page_gap"])
        self.assertEqual(gap[0]["text"], "")

    def test_utf8_emoji_offsets_are_bytes(self):
        result = self.build(["Żółć\n\n🧪."])
        self.assert_partition(result)
        self.assertEqual(result["projection"]["ranges"][1]["text_range"], [10, 15])

    def test_repeated_quotes_have_distinct_page_block_locators(self):
        result = self.build()
        repeats = [b for b in result["blocks"] if b["text"] == "Powtórka."]
        self.assertEqual(len(repeats), 2)
        self.assertNotEqual(repeats[0]["locator"], repeats[1]["locator"])

    def test_chapters_are_inferred_and_can_span_pages(self):
        result = self.build()
        self.assertEqual(len(result["chapters"]), 2)
        self.assertEqual(result["chapters"][0]["evidence_class"], "inferred")
        self.assertGreater(result["chapters"][0]["byte_range"][1], result["pages"][1]["byte_range"][1])
        self.assertTrue(all(b["quote_evidence"] == "derived" and b["structure_evidence"] == "inferred" for b in result["blocks"]))
        self.assertNotIn('"confidence"', json.dumps(result))
        self.assertNotIn('"observed"', json.dumps(result))

    def test_page_boundary_does_not_merge_paragraphs_or_invent_continuation(self):
        result = self.build(["Start of a", "sentence continued."])
        self.assertEqual([b["text"] for b in result["blocks"]], ["Start of a", "sentence continued."])
        self.assert_partition(result)

    def test_empty_first_middle_and_last_pages(self):
        for texts in itertools.product(["", "\r\n \t", "tekst"], repeat=3):
            if "tekst" not in texts:
                continue
            with self.subTest(texts=texts):
                result = self.build(texts)
                self.assertEqual(len(result["pages"]), 3)
                self.assert_partition(result)

    def test_crlf_is_one_newline_not_blank_line(self):
        result = self.build(["a\r\nb\r\n\r\nc\rd\r\re"])
        self.assertEqual([b["text"] for b in result["blocks"]], ["a\r\nb", "c\rd", "e"])
        self.assert_partition(result)

    def test_blank_line_policy_is_configurable(self):
        self.request["options"]["blank_lines"] = 2
        result = self.build(["a\n\nb\n\n\nc"])
        self.assertEqual([b["text"] for b in result["blocks"]], ["a\n\nb", "c"])

    def test_large_blank_line_count_means_no_split(self):
        self.request["options"]["blank_lines"] = 2**63 - 1
        result = self.build(["a\n\nb"])
        self.assertEqual([b["text"] for b in result["blocks"]], ["a\n\nb"])

    def test_zero_blank_lines_allows_line_resolution(self):
        self.request["options"]["blank_lines"] = 0
        self.assertEqual([b["text"] for b in self.build(["a\nb"])["blocks"]], ["a", "b"])

    def test_chapter_detection_off(self):
        self.request["options"]["chapter_mode"] = "off"
        self.assertEqual(self.build()["chapters"], [])

    def test_chapter_vocabulary_and_length_are_data(self):
        self.request["options"]["chapter_prefixes"] = ["§ "]
        self.request["options"]["chapter_max_chars"] = 0
        result = self.build(["§ " + "x" * 300 + "\n\nbody"])
        self.assertEqual(len(result["chapters"]), 1)
        self.request["options"]["chapter_max_chars"] = 10
        self.assertEqual(self.build()["chapters"], [])

    def test_ambiguous_page_form_feed_is_rejected(self):
        self.request["document"]["pages"] = 2
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            self.build()

    def test_blank_page_metadata_must_match(self):
        for bad in [[], [True], [3], [2, 2], "2"]:
            self.request["document"]["pages_without_text"] = bad
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.build()

    def test_all_blank_is_not_ocr_or_success(self):
        with self.assertRaisesRegex(ValueError, "no_text"):
            self.build(["", " \t"])

    def test_invalid_options_and_quantities(self):
        for key, values in {"blank_lines": [-1, True, 1.5], "chapter_mode": ["observed", ""],
                            "chapter_prefixes": [[""], [1], "Chapter "]}.items():
            for value in values:
                request = copy.deepcopy(self.request)
                request["options"][key] = value
                with self.subTest(key=key, value=value), self.assertRaises((ValueError, TypeError)):
                    projection.project(request)

    def test_invalid_source_and_surrogate(self):
        self.request["source"]["sha256"] = "A" * 64
        with self.assertRaises(ValueError):
            self.build()
        self.setUp()
        with self.assertRaises(UnicodeError):
            self.build(["\ud800"])

    def test_saved_receipt_replay(self):
        result = self.build()
        self.assertEqual(projection.replay(result, **{"original_sha256": self.request["source"]["sha256"],
                                                     "original_size": self.request["source"]["size"]}), result)

    def test_tampering_rejected(self):
        result = self.build()
        mutations = [lambda e: e["blocks"][0].update(text="fabricated"),
                     lambda e: e["projection"]["ranges"][0].update(text_range=[1, 2]),
                     lambda e: e["pages"][0].update(page=9),
                     lambda e: e["chapters"][0].update(evidence_class="observed"),
                     lambda e: e["projection"].update(sha256="0" * 64),
                     lambda e: e["blocks"][0]["locator"].update(page=2),
                     lambda e: e["pages"][0].update(page=True),
                     lambda e: e["projection"]["ranges"][0].update(text_range=[0.0, 1]),
                     lambda e: e.update(text=e["text"] + "changed"),
                     lambda e: e.update(options={**e["options"], "chapter_mode": "off"})]
        for mutate in mutations:
            changed = copy.deepcopy(result)
            mutate(changed)
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                projection.replay(changed, self.request["source"]["sha256"], self.request["source"]["size"])
        with self.assertRaises(ValueError):
            projection.replay(result, "0" * 64, self.request["source"]["size"])

    def test_random_exact_partition_without_text_normalization(self):
        rng = random.Random(714)
        for _ in range(150):
            pages = ["".join(rng.choice(["a", "ż", "🧪", "\n", "\r\n", " ", "\t"]) for _ in range(30))
                     for _ in range(rng.randint(1, 5))]
            result = self.build(pages)
            self.assert_partition(result)

    def test_cli_executes_same_module_and_safe_error(self):
        proc = subprocess.run([sys.executable, "-I", "-B", "-S", str(MODULE)],
                              input=json.dumps(self.request), text=True, capture_output=True, timeout=5)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout), self.build())
        bad = subprocess.run([sys.executable, "-I", "-B", "-S", str(MODULE)],
                             input='{"private":"not-logged"}', text=True, capture_output=True, timeout=5)
        self.assertEqual(bad.returncode, 2)
        self.assertEqual(bad.stdout, "")
        self.assertEqual(bad.stderr, "pdf_structure_invalid_input\n")


if __name__ == "__main__":
    unittest.main()
