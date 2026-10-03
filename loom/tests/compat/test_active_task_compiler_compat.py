"""Differential checks for the native deterministic ActiveTaskSpec compiler.

The public refinement fixture renderer is the reference for representation,
not an oracle for semantic extraction.  This test makes no provider calls and
does not modify the frozen fixture corpus.
"""
from __future__ import annotations

import copy
import hashlib
import json
import pathlib
import sys
import unittest

from compat_common import TempDir, tool

REPO = pathlib.Path(__file__).resolve().parents[3]
EVAL = REPO / "loom" / "tools" / "eval"
CONTRACTS = REPO / "loom" / "tools" / "contracts"
sys.path.insert(0, str(EVAL))
sys.path.insert(0, str(CONTRACTS))

from refinement_fixture import (  # noqa: E402
    load_cases,
    materialize_case,
    render_statements,
    verify_manifest,
)
from validate import ContractValidator  # noqa: E402

FIXTURES = REPO / "loom" / "tests" / "fixtures" / "eval" / "refinement_v1"


def native(spec):
    tmp = TempDir()
    try:
        return tool("active-task-compile", tmp.file_json("spec.json", spec))
    finally:
        tmp.cleanup()


def public_specs():
    if verify_manifest(FIXTURES):
        raise AssertionError("public refinement fixture manifest mismatch")
    specs = []
    for name in ("dev.jsonl", "validation.jsonl"):
        for case in load_cases(FIXTURES / name):
            specs.extend(materialize_case(case)["specs"])
    return specs


class ActiveTaskCompilerCompat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.validator = ContractValidator()
        manifest = json.loads((FIXTURES / "manifest.json").read_text(encoding="utf-8"))
        materializer = EVAL / "refinement_fixture.py"
        if hashlib.sha256(materializer.read_bytes()).hexdigest() != manifest["materializer_sha256"]:
            raise AssertionError("public refinement materializer changed")
        cls.specs = public_specs()
        if len(cls.specs) != 44:
            raise AssertionError(f"expected 44 public gold specs, got {len(cls.specs)}")
        for name in ("dev", "validation"):
            cases = load_cases(FIXTURES / f"{name}.jsonl")
            expanded = "".join(json.dumps(materialize_case(case), ensure_ascii=False,
                                          separators=(",", ":")) + "\n" for case in cases).encode()
            if hashlib.sha256(expanded).hexdigest() != manifest["expanded_gold"][name]["sha256"]:
                raise AssertionError(f"public {name} expanded gold changed")

    def test_all_public_gold_specs_match_reference_bytes(self):
        for spec in self.specs:
            with self.subTest(product=spec["product_ref"]["id"]):
                self.assertEqual([], self.validator.validate(spec))
                result = native(spec)
                self.assertTrue(result["ok"], result)
                self.assertEqual(render_statements(spec["statements"]),
                                 result["compiled"]["compiled_instruction"])
                expected = copy.deepcopy(spec)
                expected["compiled_instruction"] = render_statements(spec["statements"])
                self.assertEqual(expected, result["compiled"])

    def test_new_multilingual_mixed_status_case_matches_reference(self):
        spec = copy.deepcopy(self.specs[0])
        source = spec["source_refs"][0]["event_id"]
        spec["product_ref"]["id"] = "synthetic-compat-mixed-status"
        spec["statements"] = [
            {"id": "old", "kind": "style", "status": "superseded",
             "text": "Stary styl.", "source_event_ids": [source], "claim_ids": [],
             "conditions": [], "supersedes": []},
            {"id": "goal", "kind": "goal", "status": "active",
             "text": "Zażółć gęślą jaźń 🧪.", "source_event_ids": [source], "claim_ids": [],
             "conditions": [], "supersedes": ["old"]},
            {"id": "question", "kind": "open_issue", "status": "contested",
             "text": "Keep שלום and مرحبا unresolved.", "source_event_ids": [source],
             "claim_ids": [], "conditions": ["po zgodzie", "when α ≠ β"],
             "supersedes": []},
            {"id": "private", "kind": "executor_context", "status": "active",
             "text": "Internal context only.", "source_event_ids": [source],
             "claim_ids": [], "conditions": [], "supersedes": []},
            {"id": "rejected", "kind": "alternative", "status": "rejected",
             "text": "Delete evidence.", "source_event_ids": [source], "claim_ids": [],
             "conditions": [], "supersedes": []},
        ]
        # The supplied field is structurally valid but deliberately forged.
        spec["compiled_instruction"] = {"text": "x", "source_map": [
            {"span": {"byte_start": 0, "byte_len": 1}, "statement_ids": ["goal"]}
        ]}
        self.assertEqual([], self.validator.validate(spec))
        result = native(spec)
        self.assertTrue(result["ok"], result)
        self.assertEqual(render_statements(spec["statements"]),
                         result["compiled"]["compiled_instruction"])

    def test_malformed_types_reject_in_both_validators(self):
        mutations = [
            ("zero version", lambda s: s.__setitem__("version", 0)),
            ("string version", lambda s: s.__setitem__("version", "1")),
            ("condition number", lambda s: s["statements"][0].__setitem__("conditions", [1])),
            ("unknown status", lambda s: s["statements"][0].__setitem__("status", "pending")),
            ("unknown map id", lambda s: s["compiled_instruction"]["source_map"][0]
             .__setitem__("statement_ids", ["missing"])),
            ("large reversed time range", lambda s: s["source_refs"][0]["locator"].update(
                {"time_start": 9007199254740993, "time_end": 9007199254740992})),
        ]
        for label, mutate in mutations:
            with self.subTest(case=label):
                spec = copy.deepcopy(self.specs[0])
                mutate(spec)
                self.assertTrue(self.validator.validate(spec))
                result = native(spec)
                self.assertFalse(result["ok"], result)
                self.assertEqual("invalid_argument", result["error"]["code"])

    def test_numeric_representation_boundary_is_explicit(self):
        spec = copy.deepcopy(self.specs[0])
        spec["version"] = 1.0
        # JSON Schema defines integer mathematically, so 1.0 is accepted there;
        # native JSON deliberately requires integer storage (W1 contract).
        self.assertEqual([], self.validator.validate(spec))
        result = native(spec)
        self.assertFalse(result["ok"], result)
        self.assertEqual("invalid_argument", result["error"]["code"])


if __name__ == "__main__":
    unittest.main()
