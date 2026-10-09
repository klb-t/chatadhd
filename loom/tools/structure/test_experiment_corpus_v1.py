"""Mechanical fixtures; no model-quality claims or paid inference."""
import copy
import io
import json
from pathlib import Path
import subprocess
import sys
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

class HistoricalGuardTests(unittest.TestCase):
    def fixture(self, root):
        root = Path(root); historical = root / "historical"; historical.mkdir()
        source_buffer = io.BytesIO()
        hashes = []
        with zipfile.ZipFile(source_buffer, "w") as source:
            for index in range(2):
                obj = oa(str(index)); hashes.append(corpus.digest(obj))
                name = str(index) + "/original-conversation.json"
                source.writestr(name, corpus.canonical(obj))
                local = historical / name; local.parent.mkdir(); local.write_bytes(corpus.canonical(obj))
        source_bytes = source_buffer.getvalue()
        manifest = {"files": {"source.zip": {"bytes": len(source_bytes), "sha256": corpus.digest(source_bytes)}}}
        checkpoint = root / "checkpoint.zip"
        with zipfile.ZipFile(checkpoint, "w") as archive:
            archive.writestr("source.zip", source_bytes)
            archive.writestr("manifest.json", corpus.canonical(manifest))
        guard = {"expected_count": 2, "canonical_object_sha256": hashes,
                 "checkpoint_sha256": corpus.digest(checkpoint.read_bytes()),
                 "checkpoint_manifest_member": "manifest.json", "source_freeze_member": "source.zip",
                 "source_freeze_sha256": corpus.digest(source_bytes)}
        return historical, checkpoint, guard
    def test_bound_historical_panel_passes(self):
        with tempfile.TemporaryDirectory() as root:
            historical, checkpoint, guard = self.fixture(root)
            self.assertEqual(len(corpus.verify_historical_panel(historical, guard, checkpoint)), 2)
    def test_missing_directory_fails_closed(self):
        with tempfile.TemporaryDirectory() as root:
            _, checkpoint, guard = self.fixture(root)
            with self.assertRaisesRegex(ValueError, "historical_directory_missing"):
                corpus.verify_historical_panel(Path(root) / "missing", guard, checkpoint)
    def test_empty_directory_fails_closed(self):
        with tempfile.TemporaryDirectory() as root:
            _, checkpoint, guard = self.fixture(root); empty = Path(root) / "empty"; empty.mkdir()
            with self.assertRaisesRegex(ValueError, "historical_count_mismatch"):
                corpus.verify_historical_panel(empty, guard, checkpoint)
    def test_wrong_count_fails_closed(self):
        with tempfile.TemporaryDirectory() as root:
            historical, checkpoint, guard = self.fixture(root)
            (historical / "1/original-conversation.json").unlink()
            with self.assertRaisesRegex(ValueError, "historical_count_mismatch"):
                corpus.verify_historical_panel(historical, guard, checkpoint)
    def test_changed_object_fails_closed(self):
        with tempfile.TemporaryDirectory() as root:
            historical, checkpoint, guard = self.fixture(root)
            (historical / "1/original-conversation.json").write_bytes(corpus.canonical(oa("changed")))
            with self.assertRaisesRegex(ValueError, "historical_object_hash_mismatch"):
                corpus.verify_historical_panel(historical, guard, checkpoint)
    def test_checkpoint_required(self):
        with tempfile.TemporaryDirectory() as root:
            historical, _, guard = self.fixture(root)
            with self.assertRaisesRegex(ValueError, "historical_checkpoint_required"):
                corpus.verify_historical_panel(historical, guard, None)
    def test_changed_checkpoint_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            historical, checkpoint, guard = self.fixture(root)
            checkpoint.write_bytes(checkpoint.read_bytes() + b"unbound")
            with self.assertRaisesRegex(ValueError, "historical_checkpoint_hash_mismatch"):
                corpus.verify_historical_panel(historical, guard, checkpoint)
    def test_checkpoint_member_binding_verified(self):
        with tempfile.TemporaryDirectory() as root:
            historical, checkpoint, guard = self.fixture(root)
            guard["source_freeze_sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "historical_checkpoint_member_binding_mismatch"):
                corpus.verify_historical_panel(historical, guard, checkpoint)
    def test_local_hash_list_must_match_checkpoint_objects(self):
        with tempfile.TemporaryDirectory() as root:
            historical, checkpoint, guard = self.fixture(root)
            replacement = oa("replacement")
            (historical / "1/original-conversation.json").write_bytes(corpus.canonical(replacement))
            guard["canonical_object_sha256"][1] = corpus.digest(replacement)
            with self.assertRaisesRegex(ValueError, "historical_checkpoint_object_binding_mismatch"):
                corpus.verify_historical_panel(historical, guard, checkpoint)
    def test_zero_expected_panel_forbidden(self):
        with tempfile.TemporaryDirectory() as root:
            historical, checkpoint, guard = self.fixture(root)
            guard.update(expected_count=0, canonical_object_sha256=[])
            with self.assertRaisesRegex(ValueError, "historical_guard_invalid_expectation"):
                corpus.verify_historical_panel(historical, guard, checkpoint)
    def test_legacy_policy_requires_explicit_upgrade(self):
        with self.assertRaisesRegex(ValueError, "historical_guard_required"):
            corpus.prepare("missing", "missing", "missing", {}, "missing")
    def test_old_cli_without_checkpoint_fails_closed(self):
        result = subprocess.run([sys.executable, corpus.__file__, "--source", "missing", "--audit", "missing",
                                 "--historical", "missing", "--policy", "missing", "--output", "missing",
                                 "--receipt", "missing"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--historical-checkpoint", result.stderr)
    def test_prepare_missing_historical_creates_no_output(self):
        with tempfile.TemporaryDirectory() as root:
            _, checkpoint, guard = self.fixture(root); output = Path(root) / "output"
            with self.assertRaisesRegex(ValueError, "historical_directory_missing"):
                corpus.prepare("missing", "missing", Path(root) / "absent", {"historical_panel_guard": guard}, output,
                               historical_checkpoint=checkpoint)
            self.assertFalse(output.exists())

if __name__ == "__main__":
    unittest.main()
