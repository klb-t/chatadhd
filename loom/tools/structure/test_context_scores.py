"""Author-owned replay mechanisms; authored scores are not model quality gold."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from context_delta import canonical
from context_scores import compare_score_runs, example, make_score_request, replay_context_scores


def rehash(packet):
    packet["packet_hash"] = hashlib.sha256(canonical({k: v for k, v in packet.items() if k != "packet_hash"}).encode()).hexdigest()


def bind(data):
    request = make_score_request(data["packet"], data["candidate_claim_ids"], question=data["question"])
    for key in ("packet_hash", "question_hash", "request_hash"):
        data["response"][key] = request[key]


class ContextScoresTests(unittest.TestCase):
    def test_example_is_deterministic_and_keeps_overlapping_topics(self):
        self.assertEqual(example(), example())
        data = example()
        before = deepcopy(data)
        result = replay_context_scores(**data)
        self.assertEqual(result["status"], "ready")
        self.assertEqual(result["counts"], {"keep": 2, "review": 0, "drop": 0, "unknown": 0})
        self.assertEqual([r["probability"] for r in result["rows"]], [0.9, 0.85])
        self.assertEqual(data, before)
        self.assertIsNone(result["semantic_quality"])
        self.assertTrue(result["no_persistence"])

    def test_source_handles_metadata_and_assessment_are_copied(self):
        data = example()
        result = replay_context_scores(**data)
        self.assertEqual(result["source_packet"], data["packet"])
        self.assertEqual(result["rows"][0]["context_claim"], data["packet"]["context_claims"][0])
        self.assertEqual(result["rows"][0]["context_claim"]["assessment"]["status"], "contested")
        self.assertEqual(result["rows"][0]["current_span_ids"], ["current"])
        result["source_packet"]["claims"][0]["value"] = "modified local result"
        self.assertNotEqual(result["source_packet"], data["packet"])

    def test_missing_answers_stay_visible_as_unknown_review(self):
        data = example()
        data["response"]["rows"] = []
        result = replay_context_scores(**data)
        self.assertEqual(result["status"], "ready")
        self.assertEqual(result["counts"]["unknown"], 2)
        self.assertTrue(all(r["reason"] == "missing_answer" and r["suggestion"] == "review" for r in result["rows"]))

    def test_invalid_individual_score_cannot_drop_other_candidate(self):
        for value in (True, False, None, "0.9", -0.1, 1.1, float("nan"), float("inf"), 10 ** 500):
            with self.subTest(value=repr(value)):
                data = example()
                data["response"]["rows"][0]["probability"] = value
                result = replay_context_scores(**data)
                self.assertEqual(result["status"], "ready")
                self.assertEqual(result["rows"][0]["reason"], "invalid_answer")
                self.assertEqual(result["rows"][0]["suggestion"], "review")
                self.assertEqual(result["rows"][1]["suggestion"], "keep")
                json.dumps(result, allow_nan=False)

    def test_missing_probability_and_extra_answer_fields_are_invalid_rows(self):
        for row in ({"candidate_id": "aster"}, {"candidate_id": "aster", "probability": 0.1, "action": "delete"}):
            data = example()
            data["response"]["rows"][0] = row
            result = replay_context_scores(**data)
            self.assertEqual(result["status"], "ready")
            self.assertEqual(result["rows"][0]["reason"], "invalid_answer")

    def test_duplicate_unknown_and_missing_identity_reject_whole_response(self):
        for row in ({"candidate_id": "aster", "probability": 0.2},
                    {"candidate_id": "unknown", "probability": 0.2}, {"probability": 0.2}, None):
            data = example()
            data["response"]["rows"].append(row)
            result = replay_context_scores(**data)
            self.assertEqual(result["status"], "invalid")
            self.assertEqual(len(result["rows"]), 2)
            self.assertEqual(result["counts"]["unknown"], 2)
            self.assertTrue(all(r["reason"] == "response_rejected" for r in result["rows"]))

    def test_answer_for_known_but_unrequested_claim_is_rejected(self):
        data = example()
        data["candidate_claim_ids"] = ["aster"]
        bind(data)
        result = replay_context_scores(**data)
        self.assertEqual(result["status"], "invalid")
        self.assertEqual(result["rows"][0]["suggestion"], "review")

    def test_identity_binding_covers_packet_question_rubric_and_candidate_order(self):
        for key in ("packet_hash", "question_hash", "request_hash"):
            data = example()
            data["response"][key] = "0" * 64
            self.assertEqual(replay_context_scores(**data)["status"], "invalid")
        for field in ("id", "text", "rubric_id", "rubric"):
            data = example()
            data["question"][field] += " changed"
            self.assertEqual(replay_context_scores(**data)["status"], "invalid")
        data = example()
        data["candidate_claim_ids"].reverse()
        self.assertEqual(replay_context_scores(**data)["status"], "invalid")

    def test_thresholds_are_explicit_finite_and_exclude_booleans(self):
        for drop, keep in ((0.8, 0.8), (0.9, 0.2), (False, 0.8), (0.2, True), (0.2, float("nan"))):
            data = example()
            data.update(drop_threshold=drop, keep_threshold=keep)
            result = replay_context_scores(**data)
            self.assertEqual(result["status"], "invalid")
            self.assertEqual(result["counts"]["review"], 2)
            json.dumps(result, allow_nan=False)

    def test_threshold_edges_and_unconfident_score(self):
        for probability, expected in ((0.2, "drop"), (0.5, "review"), (0.8, "keep")):
            data = example()
            data["response"]["rows"][0]["probability"] = probability
            result = replay_context_scores(**data)
            self.assertEqual(result["rows"][0]["suggestion"], expected)
            self.assertEqual(result["rows"][0]["score_status"], "measured")

    def test_candidate_allowlist_duplicates_and_packet_only_boundary(self):
        data = example()
        for ids in (["aster", "aster"], ["absent"], [True], "aster"):
            self.assertEqual(make_score_request(data["packet"], ids, question=data["question"])["status"], "invalid")
        for packet in ({"packet": data["packet"], "audit": {"secret": "do not read"}},
                       {**data["packet"], "retained_input": {"secret": "do not read"}}):
            self.assertEqual(make_score_request(packet, ["aster"], question=data["question"])["status"], "invalid")

    def test_duplicate_packet_ids_rejected_even_with_recomputed_hash(self):
        for field in ("observations", "claims", "context_claims", "current_spans"):
            data = example()
            data["packet"][field].append(deepcopy(data["packet"][field][0]))
            rehash(data["packet"])
            self.assertEqual(make_score_request(data["packet"], ["aster"], question=data["question"])["status"], "invalid")

    def test_wrapper_sidecars_are_not_traversed_before_rejection(self):
        class DoNotTraverse(dict):
            def values(self):
                raise AssertionError("local-only sidecar was traversed")
        data = example()
        sidecar = DoNotTraverse(secret="private original snapshot")
        for wrapper in ({"packet": data["packet"], "audit": sidecar}, {**data["packet"], "retained_input": sidecar}):
            self.assertEqual(make_score_request(wrapper, ["aster"], question=data["question"])["status"], "invalid")

    def test_inconsistent_source_bytes_and_context_metadata_rejected(self):
        for mutation in ("quote", "hash", "assessment", "dependency", "temporal", "cut", "missing_context"):
            data = example()
            p = data["packet"]
            if mutation == "quote": p["current_spans"][0]["quote"] = "different"
            elif mutation == "hash": p["current_spans"][0]["observation_text_hash"] = "bad"
            elif mutation == "assessment": p["context_claims"][0]["assessment"] = {"status": "active"}
            elif mutation == "dependency": p["context_claims"][0]["observation_dependencies"] = []
            elif mutation == "temporal": p["current_spans"][0]["temporal"]["status"] = "at_or_before_cut"
            elif mutation == "cut": p["time_cut"] = "2026-09-28"
            else: p["context_claims"].pop()
            rehash(p)
            self.assertEqual(make_score_request(p, ["aster"], question=data["question"])["status"], "invalid")

    def test_comparison_handles_candidate_and_answer_order_permutation(self):
        data = example()
        left = replay_context_scores(**data)
        data["candidate_claim_ids"].reverse()
        data["response"]["rows"].reverse()
        bind(data)
        right = replay_context_scores(**data)
        result = compare_score_runs(left, right, {"aster": "aster", "beacon": "beacon"})
        self.assertEqual(result["status"], "ready")
        self.assertEqual(result["suggestion_agreement"], 1)
        self.assertEqual(result["measured_pairs"], 2)
        self.assertEqual(result["max_absolute_score_drift"], 0)
        self.assertIsNone(result["semantic_quality"])

    def test_comparison_explicit_nonidentity_mapping_and_drift(self):
        data = example()
        left = replay_context_scores(**data)
        data["response"]["rows"][0]["probability"] = 0.85
        data["response"]["rows"][1]["probability"] = 0.9
        right = replay_context_scores(**data)
        result = compare_score_runs(left, right, {"aster": "beacon", "beacon": "aster"})
        self.assertEqual(result["max_absolute_score_drift"], 0)
        self.assertEqual(result["correspondence"], "caller_declared_not_verified")
        result = compare_score_runs(left, right, {"aster": "aster", "beacon": "beacon"})
        self.assertAlmostEqual(result["mean_absolute_score_drift"], 0.05)

    def test_comparison_does_not_count_unknown_scores_as_zero_drift(self):
        data = example()
        left = replay_context_scores(**data)
        data["response"]["rows"] = []
        right = replay_context_scores(**data)
        result = compare_score_runs(left, right, {"aster": "aster", "beacon": "beacon"})
        self.assertEqual(result["measured_pairs"], 0)
        self.assertIsNone(result["mean_absolute_score_drift"])
        self.assertEqual(result["suggestion_agreement"], 0)

    def test_comparison_rejects_partial_many_to_one_and_unknown_mapping(self):
        run = replay_context_scores(**example())
        for mapping in ({}, {"aster": "aster"}, {"aster": "aster", "beacon": "aster"}, {"aster": "x", "beacon": "beacon"}):
            self.assertEqual(compare_score_runs(run, run, mapping)["status"], "invalid")

    def test_comparison_rejects_forged_incomplete_rows_and_different_policy(self):
        run = replay_context_scores(**example())
        for mutation in ("missing_row", "bad_score", "bad_suggestion", "bad_source", "bad_question", "threshold"):
            changed = deepcopy(run)
            if mutation == "missing_row": changed["rows"].pop()
            elif mutation == "bad_score": changed["rows"][0]["probability"] = True
            elif mutation == "bad_suggestion": changed["rows"][0]["suggestion"] = "drop"
            elif mutation == "bad_source": changed["rows"][0]["current_span_ids"] = []
            elif mutation == "bad_question": changed["question"]["rubric"] += "modified"
            else: changed["thresholds"]["drop"] = 0.1
            self.assertEqual(compare_score_runs(run, changed, {"aster": "aster", "beacon": "beacon"})["status"], "invalid")

    def test_empty_candidate_set_has_no_fabricated_agreement(self):
        data = example()
        data["candidate_claim_ids"] = []
        data["response"]["rows"] = []
        bind(data)
        run = replay_context_scores(**data)
        result = compare_score_runs(run, run, {})
        self.assertEqual(result["status"], "ready")
        self.assertIsNone(result["suggestion_agreement"])

    def test_cli_example_replay_compare_and_duplicate_keys(self):
        script = str(Path(__file__).with_name("context_scores.py"))
        with tempfile.TemporaryDirectory() as directory:
            example_run = subprocess.run([sys.executable, script, "example"], check=True, capture_output=True)
            path = Path(directory) / "replay.json"
            path.write_bytes(example_run.stdout)
            replay = subprocess.run([sys.executable, script, "replay", str(path)], check=True, capture_output=True)
            output = json.loads(replay.stdout)
            path.write_text(json.dumps({"left": output, "right": output, "mapping": {"aster": "aster", "beacon": "beacon"}}))
            compare = subprocess.run([sys.executable, script, "compare", str(path)], check=True, capture_output=True)
            self.assertEqual(json.loads(compare.stdout)["max_absolute_score_drift"], 0)
            path.write_text('{"packet":{},"packet":{}}')
            rejected = subprocess.run([sys.executable, script, "replay", str(path)], capture_output=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn("duplicate_json_key", rejected.stdout.decode())


if __name__ == "__main__":
    unittest.main()
