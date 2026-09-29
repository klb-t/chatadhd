"""Mechanism checks with developer examples, separate from quality fixtures."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

try:
    from .extract import extract_record, candidate_logic
    from .relation_envelopes import extract_relations, load_policy, VERSION, _projection
except ImportError:
    from extract import extract_record, candidate_logic
    from relation_envelopes import extract_relations, load_policy, VERSION, _projection


class RelationEnvelopeMechanismTests(unittest.TestCase):
    def assert_spans(self, text, result):
        def walk(value):
            if isinstance(value, dict):
                if "char_start" in value and "quote" in value:
                    self.assertEqual(text[value["char_start"]:value["char_end"]], value["quote"])
                    self.assertEqual(text.encode()[value["byte_start"]:value["byte_end"]].decode(), value["quote"])
                for child in value.values():
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)
        walk(result)

    def test_default_legacy_remains_independent(self):
        record = {"text": "If pump is ready, then valve is open."}
        baseline = extract_record(record)
        self.assertEqual(baseline["version"], "bounded-text-structure/2")
        self.assertIsNotNone(baseline["candidates"][0]["formula_candidate"])
        expanded = extract_record(record, policy="explicit_relations")
        self.assertEqual(expanded["version"], VERSION)
        self.assertEqual(expanded["candidates"][0]["operation"], "implies")
        self.assertIsNone(expanded["candidates"][0]["formula_candidate"])
        with self.assertRaises(ValueError):
            extract_record(record, policy="unknown")

    def test_provenance_known_at_and_unknown_metadata_survive(self):
        record = {"id": "unit-1", "locator": {"source": "sha256:source", "byte_start": 914},
                  "text": "Jeśli kruk nie śpi, żuraw nie odlatuje.", "known_at": "2026-01-02",
                  "assessment": {"confidence": .42}, "future": {"a": [1, 2]},
                  "topic_id": "birds", "turn_id": "turn-1"}
        original = deepcopy(record)
        result = extract_relations(record)
        self.assertEqual(record, original)
        self.assertEqual(result["input"], original)
        candidate = result["candidates"][0]
        self.assertEqual(candidate["known_at"], record["known_at"])
        self.assertEqual(candidate["source_id"], "sha256:source")
        self.assertEqual(candidate["scope"]["topic_id"], "birds")
        self.assertEqual(candidate["slots"]["condition"]["text"], "kruk nie śpi")
        self.assertFalse(result["persistable_claim"])
        self.assertIsNone(candidate["confidence"])
        self.assertEqual(candidate_logic(result, accept_unchecked=True), [])
        self.assert_spans(record["text"], result)

    def test_markdown_line_wrap_has_reversible_projection(self):
        text = "- **If** żuraw is ready,\n  then **crane** is active.\n"
        result = extract_relations({"text": text})
        self.assertEqual(len(result["candidates"]), 1)
        candidate = result["candidates"][0]
        self.assertEqual(candidate["cue"]["text"], "If")
        slot = candidate["slots"]["consequence"]
        self.assertEqual(slot["normalized_text"], "crane is active")
        self.assertEqual(slot["text"], "crane** is active")
        self.assertEqual(len(slot["source_fragments"]), 2)
        self.assert_spans(text, result)
        for unit in result["units"]:
            self.assertEqual(len(unit["projection"]), len(unit["source_map"]))
            previous = -1
            for a, b in unit["source_map"]:
                self.assertLess(a, b)
                self.assertGreaterEqual(a, previous)
                previous = b

    def test_identifier_underscores_survive_the_detection_projection(self):
        text = "foo__bar__baz and __displayed text__"
        projected, mapping = _projection(text, 0, len(text))
        self.assertEqual(projected, "foo__bar__baz and displayed text")
        self.assertEqual(len(projected), len(mapping))

    def test_paragraphs_lists_and_sentences_do_not_merge_topics(self):
        text = "If bell rings, door closes. If rain stops, roof opens.\n\n- If light fades, lamp starts.\n- Unknown."
        result = extract_relations({"text": text})
        self.assertEqual(len(result["candidates"]), 3)
        self.assertEqual(len(result["unknown"]), 1)
        self.assertEqual(result["coverage"]["physical_units"], 4)
        self.assert_spans(text, result)
        self.assertTrue(all(c["scope"]["topic_id"] is None for c in result["candidates"]))

    def test_question_code_heading_and_blockquote_contexts_abstain(self):
        for text in ("If bell rings, door closes?", "`If bell rings, door closes.`",
                     "# If bell rings, door closes.", "> If bell rings, door closes.",
                     "```text\nIf bell rings, door closes.\n```",
                     "~~~\n```\nIf bell rings, door closes.\n~~~"):
            with self.subTest(text=text):
                result = extract_relations({"text": text})
                self.assertEqual(result["candidates"], [])
                self.assert_spans(text, result)

    def test_quoted_multiple_sentences_never_leak_middle_assertion(self):
        for text in ('„Intro. If bell rings, door closes. Outro.”',
                     "'Intro. If bell rings, door closes. Outro.'"):
            with self.subTest(text=text):
                self.assertEqual(extract_relations({"text": text})["candidates"], [])

    def test_relation_negation_and_operand_negation_are_separate(self):
        for text in ("Air did not cause the noise.", "Powietrze nie powoduje hałasu.",
                     "Sensors do not support the claim that pressure rises."):
            self.assertEqual(extract_relations({"text": text})["candidates"], [])
        result = extract_relations({"text": "The lamp did not light because the circuit was open."})
        self.assertEqual(result["candidates"][0]["slots"]["effect"]["text"], "The lamp did not light")

    def test_purpose_qualifier_and_scope_are_preserved_in_graph(self):
        result = extract_relations({"text": "We installed a timer in order to avoid noise.",
                                    "known_at": "2026-01-02", "turn_id": "t1"})
        candidate = result["candidates"][0]
        self.assertEqual(candidate["qualifiers"]["success"], "not_asserted")
        node = next(x for x in result["structure"]["nodes"] if x["kind"] == "thought_operation")
        self.assertEqual(node["qualifiers"]["envelope_qualifiers"], candidate["qualifiers"])
        self.assertEqual(node["provenance"]["known_at"], "2026-01-02")
        self.assertEqual(node["provenance"]["scope"]["turn_id"], "t1")

    def test_nested_attachment_and_multiple_policy_matches_abstain(self):
        for text in ("If bell rings, if light fades, lamp starts.",
                     "The bell rings and the roof closes or the door opens."):
            self.assertEqual(extract_relations({"text": text})["candidates"], [])
        policy = load_policy()
        policy["patterns"].append({**deepcopy(policy["patterns"][0]), "id": "competing-hypothesis"})
        result = extract_relations({"text": "If bell rings, door closes."}, policy=policy)
        self.assertEqual(result["candidates"], [])
        self.assertEqual(result["unknown"][0]["reason"], "multiple_supported_envelopes_or_ambiguous_attachment")

    def test_custom_policy_has_content_identity_without_mutation(self):
        record = {"text": "If bell rings, door closes."}
        policy = load_policy()
        original = deepcopy(policy)
        result = extract_relations(record, policy=policy)
        self.assertEqual(policy, original)
        self.assertEqual(result, extract_relations(record, policy=policy))
        policy["id"] = "independent-variant"
        changed = extract_relations(record, policy=policy)
        self.assertNotEqual(result["policy_sha256"], changed["policy_sha256"])
        self.assertNotEqual(result["id"], changed["id"])

    def test_invalid_policy_missing_groups_and_duplicate_ids_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "policy.json"
            policy = load_policy()
            policy["patterns"][0]["pattern"] = "bad"
            path.write_text(json.dumps(policy))
            with self.assertRaises(ValueError):
                load_policy(path)
            policy = load_policy()
            policy["patterns"].append(policy["patterns"][0])
            path.write_text(json.dumps(policy))
            with self.assertRaises(ValueError):
                load_policy(path)

    def test_empty_input_and_missing_source_are_explicit(self):
        result = extract_relations({"text": " \n "})
        self.assertEqual(result["source_identity_status"], "missing")
        self.assertIsNone(result["coverage"]["envelope_character_coverage"])
        self.assertEqual(result["units"], [])
        with self.assertRaises(ValueError):
            extract_relations({"value": "If bell rings, door closes."})

    def test_empty_cli_preserves_default_shape_and_selected_version(self):
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder) / "input.json", Path(folder) / "output.json"
            source.write_text("[]")
            command = [sys.executable, str(Path(__file__).with_name("extract.py")),
                       str(source), "--output", str(target)]
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(json.loads(target.read_text()),
                             {"version": "bounded-text-structure/2", "extractions": []})
            subprocess.run(command + ["--policy", "explicit_relations"], check=True, capture_output=True)
            self.assertEqual(json.loads(target.read_text()),
                             {"version": VERSION, "extractions": [], "policy": "explicit_relations"})


if __name__ == "__main__":
    unittest.main()
