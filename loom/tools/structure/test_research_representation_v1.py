"""Controlled mechanics only: fixtures are not authentic research observations."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure import experiment_context_preparation_v1 as producer
from loom.tools.structure import research_representation_v1 as measurement


def fixture():
    return {
        "schema": "loom.research_context_source/1", "family_id": "mechanics-family",
        "source_sha256": "a" * 64, "split": "development", "prior_use": "mechanics_only",
        "external_parent_ids": ["external"], "native_conversation": {"title": "synthetic mechanics"},
        "nodes": [
            {"node_id": "a", "parent_node_id": "external", "role": "user", "source_pointer": "/0",
             "native_message": {"author": {"role": "user"}, "content": {"parts": ["old target", {"image": "binary-ref"}]}, "metadata": {"local": True}}},
            {"node_id": "b", "parent_node_id": "a", "role": "user", "source_pointer": "/1",
             "native_message": {"author": {"role": "user"}, "content": {"parts": ["corrected target"]}, "text": "corrected target"}},
            {"node_id": "c", "parent_node_id": "a", "role": "assistant", "source_pointer": "/2",
             "native_message": {"content": {"parts": ["other branch"]}}},
        ]}


POLICY = {"ancestor_window_nodes": 1, "explicit_fields": ["node_id", "parent_node_id", "role", "source_pointer", "native_message"]}
TASK = {"id": "mechanics", "checkpoint_boundary_node_id": "b"}


def view(source=None, representation="source_graph", resolution="full"):
    return producer.prepare_view(source or fixture(), TASK, representation, resolution, POLICY)["exact_output"]


class RepresentationMetamorphicTests(unittest.TestCase):
    def test_reserialization_and_technical_key_order_do_not_change_identity_or_coverage(self):
        source = fixture()
        reformatted = json.loads(json.dumps(source, ensure_ascii=True, indent=7, sort_keys=True))
        original = producer.prepare_view(source, TASK, "source_graph", "checkpoint", POLICY)
        transformed = producer.prepare_view(reformatted, TASK, "source_graph", "checkpoint", POLICY)
        self.assertEqual(original, transformed)
        self.assertEqual(measurement.inspect_view(source, original["exact_output"], producer),
                         measurement.inspect_view(reformatted, transformed["exact_output"], producer))

    def test_location_change_preserves_source_version_and_view(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            encoded = measurement.canonical(fixture())
            (root / "first.json").write_bytes(encoded)
            (root / "new").mkdir()
            (root / "new" / "second.json").write_bytes(encoded)
            a, b = (json.loads(p.read_bytes()) for p in (root / "first.json", root / "new" / "second.json"))
            self.assertEqual(measurement.digest(a), measurement.digest(b))
            self.assertEqual(view(a), view(b))

    def test_explicit_deletion_reduces_field_and_literal_coverage(self):
        source = fixture()
        intact = view(source)
        missing = copy.deepcopy(intact)
        del missing["payload"]["nodes"][1]["native_message"]["content"]
        before = measurement.inspect_view(source, intact, producer)
        after = measurement.inspect_view(source, missing, producer)
        self.assertLess(after["retained_native_message_leaves"], before["retained_native_message_leaves"])
        self.assertLess(after["literal_text_field_coverage_selected"], 1)
        self.assertNotEqual(measurement.digest(intact), measurement.digest(missing))
        self.assertEqual(after["mismatched_or_unknown_native_fields"], 0)

    def test_modified_native_value_is_not_counted_as_preserved(self):
        source = fixture()
        edited = view(source)
        edited["payload"]["nodes"][1]["native_message"]["text"] = "changed fixture text"
        result = measurement.inspect_view(source, edited, producer)
        self.assertEqual(result["mismatched_or_unknown_native_fields"], 1)
        self.assertLess(result["native_message_leaf_coverage_selected"], 1)

    def test_text_retains_literals_but_does_not_claim_metadata_or_parent_links(self):
        source = fixture()
        result = measurement.inspect_view(source, view(source, "exact_text"), producer)
        self.assertEqual(result["literal_text_field_coverage_selected"], 1)
        self.assertLess(result["native_message_leaf_coverage_selected"], 1)
        self.assertEqual(result["retained_parent_links"], 0)
        self.assertFalse(result["conversation_wrapper_reconstructible"])
        self.assertFalse(result["original_file_serialization_reconstructible"])

    def test_explicit_fields_preserve_native_messages_without_claiming_missing_wrappers(self):
        source = fixture()
        source["nodes"][0]["extra_wrapper"] = "omitted"
        result = measurement.inspect_view(source, view(source, "explicit_fields"), producer)
        self.assertEqual(result["native_message_leaf_coverage_selected"], 1)
        self.assertLess(result["normalized_node_leaf_coverage_selected"], 1)
        self.assertEqual(result["native_messages_reconstructed"], 3)

    def test_graph_reconstructs_nodes_but_omits_conversation_wrapper_and_binary(self):
        source = fixture()
        result = measurement.inspect_view(source, view(source), producer)
        self.assertEqual(result["normalized_nodes_reconstructed"], 3)
        self.assertEqual(result["retained_parent_links"], 3)
        self.assertEqual(result["unresolved_external_parent_links"], 1)
        self.assertFalse(result["attachment_binaries_in_view"])
        self.assertFalse(result["conversation_wrapper_reconstructible"])

    def test_checkpoint_is_path_boundary_not_summary_or_sibling(self):
        source = fixture()
        prepared = view(source, resolution="checkpoint")
        result = measurement.inspect_view(source, prepared, producer)
        self.assertEqual([r["node_id"] for r in measurement.output_records(prepared)], ["a", "b"])
        self.assertFalse(prepared["selection"]["user_declared_checkpoint"])
        self.assertEqual(result["selected_nodes"], 2)
        self.assertLess(result["source_node_coverage"], 1)

    def test_subgraph_boundary_edge_remains_retained_not_invented(self):
        source = fixture()
        result = measurement.inspect_view(source, view(source, resolution="selected_subgraph"), producer)
        self.assertEqual(result["selected_nodes"], 1)
        self.assertEqual(result["retained_parent_links"], 1)
        self.assertEqual(result["invented_parent_links"], 0)

    def test_json_syntax_measure_does_not_confuse_bytes_with_tokens(self):
        source = fixture()
        source["nodes"][0]["native_message"]["content"]["parts"][0] = "żółć\n\"quoted\""
        prepared = view(source)
        result = measurement.inspect_view(source, prepared, producer)
        self.assertEqual(result["view_canonical_bytes"], result["view_json_scalar_bytes"] + result["view_json_keys_and_syntax_bytes"])
        self.assertGreater(result["view_json_keys_and_syntax_bytes"], 0)
        self.assertIsNone(result["token_count"])
        self.assertIsNone(result["tokenizer"])

    def test_manifest_mutation_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "source.json").write_text("changed")
            with self.assertRaisesRegex(ValueError, "preparation_payload_hash_mismatch"):
                measurement.verify_manifest(root, {"files": {"source.json": "a" * 64}})

    def test_fabricated_node_is_rejected(self):
        changed = view()
        changed["payload"]["nodes"][0]["node_id"] = "not-a-source-node"
        with self.assertRaisesRegex(ValueError, "view_contains_unknown_source_record"):
            measurement.inspect_view(fixture(), changed, producer)

    def test_controlled_correction_changes_target_and_marks_old_version_wrong(self):
        # The correction relation and its current scope are explicit fixture data.
        # This verifies ranking/evaluation mechanics, not automatic interpretation.
        from loom.tools.structure import research_retrieval_v1 as retrieval
        records = [
            {"message_id": "earlier", "node_id": "earlier", "role": "user", "text": "Output format CSV",
             "parent_ids": [], "child_ids": ["correction"], "created_at": "1"},
            {"message_id": "correction", "node_id": "correction", "role": "user", "text": "Correction. Output format JSON",
             "parent_ids": ["earlier"], "child_ids": [], "created_at": "2"},
        ]
        config = {"methods": [{"id": "words", "operator": "token_cosine", "parameters": {}}]}
        original_task = {"query": "CSV", "answerability": "answerable",
                         "expected_evidence": [{"message_id": "earlier", "quote": "Output format CSV"}]}
        corrected_task = {"query": "JSON", "answerability": "answerable",
                          "expected_evidence": [{"message_id": "correction", "quote": "Output format JSON"}],
                          "forbidden_evidence": [{"message_id": "earlier", "reason": "wrong_version"}]}
        engine = retrieval.Engine(records, config)
        old_scores, _ = engine.score(original_task)
        new_scores, _ = engine.score(corrected_task)
        old = retrieval.ranked(records, old_scores["words"])
        new = retrieval.ranked(records, new_scores["words"])
        self.assertEqual(old[0]["message_id"], "earlier")
        self.assertEqual(new[0]["message_id"], "correction")
        evaluation = {"ranking_cutoffs": [1], "ndcg_cutoffs": [1]}
        selection = retrieval.select_context(new, max_bytes=1000, max_records=1)
        metrics = retrieval.evaluate(corrected_task, new, selection, evaluation)
        self.assertEqual(metrics["context_evidence_recall"], 1)
        self.assertEqual(metrics["annotated_version_distractors_selected"], 0)
        stale = retrieval.select_context(old, max_bytes=1000, max_records=1)
        self.assertEqual(retrieval.evaluate(corrected_task, old, stale, evaluation)["annotated_version_distractors_selected"], 1)
        self.assertNotEqual(measurement.digest(original_task), measurement.digest(corrected_task))


if __name__ == "__main__":
    unittest.main()
