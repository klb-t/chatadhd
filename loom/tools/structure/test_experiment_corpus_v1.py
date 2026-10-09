"""Mechanical fixtures; no model-quality claims or paid inference."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import experiment_corpus_v1 as corpus

POLICY = {"cue_rules": {"correction_candidate": "(?i)correction", "checkpoint_candidate": "(?i)checkpoint"},
          "structural_features": ["branching", "tool_calls"], "provider_quota": {"openai": 3},
          "provisional_validation_per_provider": 1, "length_bins": ["short", "medium", "long"]}

def oa(cid="one", text="test"):
    return {"id": cid, "mapping": {"root-" + cid: {"parent": None, "children": ["node-" + cid], "message": None},
        "node-" + cid: {"parent": "root-" + cid, "children": [], "message": {"id": "msg-" + cid,
          "author": {"role": "user"}, "content": {"parts": [text, {"image": "reference"}]},
          "create_time": 100, "metadata": {"uninterpreted": [1, 2]}}}}}

def normal(cid="one", text="test"):
    return corpus.normalized(oa(cid, text), "openai", "source#/0")

class CorpusTests(unittest.TestCase):
    def test_raw_object_and_all_metadata_preserved(self):
        raw = oa(); record = corpus.normalized(raw, "openai", "s#/0")
        self.assertEqual(raw, record["native_conversation"])
        self.assertEqual(raw["mapping"]["node-one"], record["messages"][0]["native_node"])
        self.assertEqual(record["messages"][0]["native_message"]["metadata"], {"uninterpreted": [1, 2]})
    def test_null_message_nodes_retained(self):
        r = normal(); self.assertEqual(len(r["source_graph"]), 2); self.assertEqual(len(r["messages"]), 1)
    def test_text_is_explicit_projection(self):
        m = normal()["messages"][0]; self.assertEqual(m["text"], "test")
        self.assertIn("nontext_omitted", m["text_projection"])
    def test_anthropic_native_parent_only(self):
        raw = {"uuid": "c", "chat_messages": [{"uuid": "a", "sender": "human", "text": "one"},
             {"uuid": "b", "sender": "assistant", "text": "two"},
             {"uuid": "c", "sender": "human", "text": "three", "parent_message_uuid": "a"}]}
        r = corpus.normalized(raw, "anthropic", "a#/0")
        self.assertEqual(r["messages"][1]["parent_ids"], [])
        self.assertEqual(r["messages"][2]["parent_ids"], ["a"])
        self.assertEqual(r["messages"][0]["role"], "user")
        self.assertEqual(r["native_conversation"], raw)
    def test_branching_evidence_is_structural(self):
        r = normal(); r["source_graph"][0]["child_ids"].append("other")
        self.assertTrue(corpus.features(r, POLICY)["branching"])
    def test_tool_evidence_is_native(self):
        r = normal(); r["messages"][0]["role"] = "tool"
        self.assertTrue(corpus.features(r, POLICY)["tool_calls"])
    def test_only_user_cues(self):
        r = normal(text="correction checkpoint"); r["messages"][0]["role"] = "assistant"
        self.assertFalse(corpus.features(r, POLICY)["checkpoint_candidate"])
    def test_duplicate_id_union(self):
        a = normal(); b = normal(text="changed version")
        groups, reasons = corpus.group_families([a, b]); self.assertEqual(len(groups), 1)
        self.assertEqual(a["family_id"], b["family_id"])
    def test_exact_substantial_overlap_union(self):
        a = normal("a", "x" * 220); b = normal("b", "x" * 220)
        self.assertEqual(len(corpus.group_families([a, b])[0]), 1)
    def test_short_common_text_does_not_union(self):
        a = normal("a", "yes"); b = normal("b", "yes")
        self.assertEqual(len(corpus.group_families([a, b])[0]), 2)
    def test_role_bound_overlap(self):
        a = normal("a", "x" * 220); b = normal("b", "x" * 220); b["messages"][0]["role"] = "assistant"
        self.assertEqual(len(corpus.group_families([a, b])[0]), 2)
    def test_family_transitivity(self):
        a = normal("a", "x" * 220); b = normal("b", "x" * 220); c = normal("b", "different")
        self.assertEqual(len(corpus.group_families([a, b, c])[0]), 1)
    def candidates(self):
        rows = [normal(str(i), "x" * (i + 1)) for i in range(10)]
        corpus.group_families(rows)
        for r in rows:
            r.update(features=corpus.features(r, POLICY), historical_overlap=False, prior_exposure=False)
        return rows
    def test_historical_overlap_excluded(self):
        rows = self.candidates(); rows[0]["historical_overlap"] = True
        self.assertNotIn(rows[0], corpus.choose(rows, POLICY))
    def test_family_not_in_both_splits(self):
        selected = corpus.choose(self.candidates(), POLICY)
        self.assertEqual(len({r["family_id"] for r in selected}), 3)
        self.assertEqual(len({r["length_bin"] for r in selected}), 3)
        self.assertEqual(sum(r["split"] == "provisional_validation" for r in selected), 1)
    def test_exposed_never_validation(self):
        rows = self.candidates()
        for r in rows:
            r["prior_exposure"] = True
        self.assertTrue(all(r["split"] == "tuning" for r in corpus.choose(rows, POLICY)))
    def test_deterministic_selection(self):
        rows = self.candidates()
        a = [r["source_id"] for r in corpus.choose(copy.deepcopy(rows), POLICY)]
        b = [r["source_id"] for r in corpus.choose(list(reversed(copy.deepcopy(rows))), POLICY)]
        self.assertEqual(a, b)
    def test_output_is_immutable_new_directory(self):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "new"
            corpus.private_directory(output)
            with self.assertRaises(FileExistsError):
                corpus.private_directory(output)
    def test_output_git_guard(self):
        with tempfile.TemporaryDirectory() as root:
            (Path(root) / ".git").mkdir()
            with self.assertRaisesRegex(ValueError, "inside_git"):
                corpus.private_directory(Path(root) / "private")
    def test_mixed_timestamp_formats(self):
        r = normal(); r["messages"].append({"created_at": "2026-01-01T00:00:00Z"})
        self.assertEqual(corpus.time_range([r])["messages_with_valid_timestamp"], 2)
    def test_bad_timestamp_remains_unknown(self):
        r = normal(); r["messages"][0]["created_at"] = None
        self.assertIsNone(corpus.time_range([r])["start"])

if __name__ == "__main__":
    unittest.main()
