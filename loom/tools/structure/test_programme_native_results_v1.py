"""Fabricated public inputs only; no provider, private record, or model calls."""
from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import programme_native_results_v1 as subject
except ImportError:
    import programme_native_results_v1 as subject


REPO = Path(__file__).resolve().parents[3]
POLICY = REPO / "docs/research/model_research_2026-10-04/native-results-policy.json"
VOCABULARY = REPO / "loom/tools/structure/candidate_graph_vocabulary.json"


def fixture(slots=2):
    packet = {"schema": "loom.source_packet/1", "snapshot_id": "snap:invented",
              "observations": [{"id": "ob:invented", "unit": "utterance", "text": "Żółć i 😀.", "ordinal": 0,
                                "speaker": "invented", "locator": {"source": "fixture"}, "attrs": {"node": "invented"}}],
              "entities": [], "claims": [], "metadata": {}}
    packet_hash = subject.digest(packet)
    bundle = {"schema": "loom.candidate_graph/1", "packet_id": packet["snapshot_id"],
              "entity_drafts": [], "claim_drafts": [], "roots": [], "coverage": [], "unknowns": []}
    body = {"model": "invented/model", "messages": [{"role": "system", "content": "Invented fixture"},
            {"role": "user", "content": subject.canonical({"packet_hash": packet_hash, "source_packet": packet}).decode()}]}
    request_hash = subject.digest(body)
    prepared = {"schema": "loom.native_semantic_study.preparation/1", "planned_requests": slots, "requests": []}
    normalized = {"schema": "loom.programme_results/1", "stage_id": "invented-stage", "planned_operations": slots,
                  "planned_operation_ids": [], "requests": [], "rows": [], "responses": []}
    for index in range(slots):
        prepared_id, operation_id = "prepared:" + str(index), "operation:" + str(index)
        prepared["requests"].append({"case_id": "invented-case", "method_id": "method:" + str(index),
                                     "packet_hash": packet_hash, "request_hash": request_hash,
                                     "request": {"id": prepared_id, "body": deepcopy(body)}})
        normalized["planned_operation_ids"].append(operation_id)
        normalized["requests"].append({"operation_id": operation_id, "request_sha256": request_hash,
            "metadata": {"prepared_request_id": prepared_id, "arm_id": "method:" + str(index)}, "body": deepcopy(body)})
        content = subject.canonical({"schema_version": 2, "packet_hash": packet_hash, "bundles": [bundle]}).decode()
        response_hash = subject.sha(("invented-http:" + str(index)).encode())
        normalized["rows"].append({"operation_id": operation_id, "request_sha256": request_hash,
            "response_sha256": response_hash, "state": "completed", "completed": True,
            "billing_replay_verified": True, "response_ledger_bound": True})
        normalized["responses"].append({"operation_id": operation_id, "response_sha256": response_hash,
            "projection": {"choices": [{"message": {"role": "assistant", "content": content}, "finish_reason": "stop"}]}})
    sources = {"schema": "loom.native_semantic_study.inputs/1", "cases": [{"id": "invented-case",
        "packet_hash": packet_hash, "source_packet": packet}]}
    return normalized, prepared, sources, subject.read(VOCABULARY), subject.read(POLICY)


def report(payload, accepted=True):
    packet, bundle, vocabulary = (payload[name] for name in ("packet", "bundle", "vocabulary"))
    result = {"version": "candidate-graph-native/1", "valid": accepted, "status": "valid" if accepted else "rejected",
              "errors": [] if accepted else [{"code": "invented_rejection", "path": "/", "message": "fixture"}],
              "coverage": {"semantic_accuracy": None, "source_status_rows": bundle.get("coverage"), "located_unknowns": bundle.get("unknowns"),
                           "source_observations": len(packet["observations"]), "source_bytes": sum(len(o["text"].encode()) for o in packet["observations"]),
                           "by_status_bytes": {}, "represented_bytes": 0, "uncovered_bytes": 0, "uncovered_spans": [],
                           "unknown_bytes": 0, "representation_status": "unrepresented"} if accepted else {"representation_status": "unknown", "semantic_accuracy": None},
              "packet_hash": subject.digest(packet) if accepted else None,
              "hash_algorithm": "loom-json-canonical-sha256", "retained_input": {"source_packet": packet, "bundle": bundle},
              "drafts": {"entities": bundle["entity_drafts"], "claims": bundle["claim_drafts"]} if accepted else {"entities": [], "claims": []},
              "no_inference": True, "no_persistence": True}
    if accepted:
        result["vocabulary_hash"] = subject.digest(vocabulary)
    return result


class Callback:
    def __init__(self, transform=None):
        self.calls, self.transform = [], transform

    def __call__(self, raw):
        payload = subject.parse(raw)
        self.calls.append(payload)
        native = report(payload)
        if self.transform:
            native = self.transform(native, len(self.calls), payload)
        return {"returncode": 0, "stdout": subject.canonical(native), "stderr": b"", "error": None}


class NativeResultsTests(unittest.TestCase):
    def run_fixture(self, inputs=None, callback=None, **kwargs):
        inputs = fixture() if inputs is None else inputs
        callback = Callback() if callback is None else callback
        return subject.evaluate(*inputs, validator=callback, callback_id="invented-test-only", **kwargs), callback

    def set_content(self, inputs, content, response=0, choice=0):
        inputs[0]["responses"][response]["projection"]["choices"][choice]["message"]["content"] = content

    def test_callback_contract_is_not_native_or_semantic_evidence(self):
        result, callback = self.run_fixture()
        self.assertEqual(len(callback.calls), 2)
        self.assertTrue(all(set(call) == {"packet", "bundle", "vocabulary"} for call in callback.calls))
        self.assertEqual(result["counts"]["mechanically_valid"], 2)
        self.assertEqual(result["counts"]["native_verified_valid"], 0)
        self.assertEqual(result["counts"]["native_execution_captures"], 0)
        self.assertFalse(result["validator"]["native_execution"])
        self.assertIsNone(result["receipts"][0]["native_contract_valid"])
        self.assertIsNone(result["manual_review_skeleton"]["records"][0]["native_contract_valid"])
        self.assertIsNone(result["semantic_accuracy"])
        self.assertEqual(result["new_model_calls"], 0)

    def test_missing_responses_keep_planned_slots_and_original_inputs(self):
        inputs = fixture()
        inputs[0]["rows"].pop()
        inputs[0]["responses"].pop()
        before = deepcopy(inputs)
        result, _ = self.run_fixture(inputs)
        self.assertEqual(inputs, before)
        self.assertEqual(result["planned_requests"], 2)
        self.assertEqual(result["counts"]["missing_responses"], 1)
        self.assertEqual(len(result["manual_review_skeleton"]["records"]), 2)
        self.assertEqual(result["receipts"][1]["transport_state"], "not_attempted")

    def test_all_duplicate_alternatives_and_all_choices_survive_rejection(self):
        inputs = fixture(1)
        choices = inputs[0]["responses"][0]["projection"]["choices"]
        envelope = subject.parse(choices[0]["message"]["content"].encode())
        envelope["bundles"] *= 2
        choices[0]["message"]["content"] = subject.canonical(envelope).decode()
        choices.extend([17, deepcopy(choices[0])])
        callback = Callback(lambda native, index, payload: report(payload, index != 2))
        result, _ = self.run_fixture(inputs, callback)
        receipt = result["receipts"][0]
        self.assertEqual(len(callback.calls), 4)
        self.assertEqual(len(receipt["choices"]), 3)
        self.assertEqual(receipt["response_projection"], inputs[0]["responses"][0]["projection"])
        self.assertFalse(receipt["validator_contract_valid"])
        skeleton = result["manual_review_skeleton"]["records"][0]
        self.assertEqual(skeleton["bundle_count"], 4)
        self.assertEqual([a["bundle_index"] for a in skeleton["alternatives"]], list(range(4)))
        self.assertFalse(skeleton["single_envelope_review_compatible"])

    def test_empty_envelope_is_explicit_abstention_without_native_claim(self):
        inputs = fixture(1)
        envelope = {"schema_version": 2, "packet_hash": inputs[2]["cases"][0]["packet_hash"], "bundles": []}
        self.set_content(inputs, subject.canonical(envelope).decode())
        result, callback = self.run_fixture(inputs)
        self.assertEqual(callback.calls, [])
        self.assertEqual(result["receipts"][0]["status"], "envelope_abstention")
        self.assertIsNone(result["receipts"][0]["native_contract_valid"])

    def test_mixed_valid_and_empty_choices_are_partial_abstention(self):
        inputs = fixture(1)
        choices = inputs[0]["responses"][0]["projection"]["choices"]
        empty = deepcopy(choices[0])
        envelope = subject.parse(empty["message"]["content"].encode())
        envelope["bundles"] = []
        empty["message"]["content"] = subject.canonical(envelope).decode()
        choices.append(empty)
        result, callback = self.run_fixture(inputs)
        self.assertEqual(len(callback.calls), 1)
        self.assertEqual(result["receipts"][0]["status"], "partial_abstention")
        self.assertIsNone(result["receipts"][0]["validator_contract_valid"])
        self.assertEqual(result["counts"]["mechanically_rejected"], 0)

    def test_no_fence_repair_or_json_schema_coercion(self):
        original = fixture(1)[0]["responses"][0]["projection"]["choices"][0]["message"]["content"]
        bad = ["```json\n" + original + "\n```", original + " trailing", original.replace('"schema_version":2', '"schema_version":true'),
               original.replace('"schema_version":2', '"schema_version":2.0'), original.replace('"schema_version":2', '"schema_version":2,"extra":0'),
               original.replace('"schema_version":2', '"schema_version":2,"schema_version":2'), original.replace('"schema_version":2', '"schema_version":NaN')]
        for content in bad:
            with self.subTest(content=content[:50]):
                inputs = fixture(1)
                self.set_content(inputs, content)
                result, callback = self.run_fixture(inputs)
                choice = result["receipts"][0]["choices"][0]
                self.assertEqual(choice["content"], content)
                self.assertEqual(choice["content_sha256"], subject.sha(content.encode()))
                self.assertFalse(choice["envelope_valid"])
                self.assertEqual(callback.calls, [])

    def test_packet_hash_mismatch_rejects_without_bundle_execution(self):
        inputs = fixture(1)
        envelope = subject.parse(inputs[0]["responses"][0]["projection"]["choices"][0]["message"]["content"].encode())
        envelope["packet_hash"] = "0" * 64
        self.set_content(inputs, subject.canonical(envelope).decode())
        result, callback = self.run_fixture(inputs)
        self.assertEqual(callback.calls, [])
        self.assertEqual(result["receipts"][0]["choices"][0]["failure"], "response_packet_hash_drift")

    def test_source_request_and_response_binding_drift_reject(self):
        mutations = [lambda d: d[2]["cases"][0]["source_packet"]["observations"][0].update(text="changed"),
                     lambda d: d[0]["requests"][0]["body"].update(model="changed"),
                     lambda d: d[0]["rows"][0].update(request_sha256="0" * 64),
                     lambda d: d[0]["rows"][0].update(response_sha256="0" * 64),
                     lambda d: d[0]["responses"][0].update(response_sha256=None),
                     lambda d: d[0].update(planned_operations=True),
                     lambda d: d[1].update(planned_requests=True)]
        for mutate in mutations:
            inputs, callback = fixture(1), Callback()
            mutate(inputs)
            with self.subTest(mutation=mutate), self.assertRaises((subject.NativeResultsError, KeyError)):
                self.run_fixture(inputs, callback)
            self.assertEqual(callback.calls, [])

    def test_report_identity_drift_is_not_candidate_validity(self):
        mutations = [lambda r: r.update(packet_hash="0" * 64), lambda r: r.update(vocabulary_hash="0" * 64),
                     lambda r: r.update(no_inference=False), lambda r: r.update(version="wrong"),
                     lambda r: r["drafts"].update(entities=[{}]),
                     lambda r: r["retained_input"]["source_packet"].update(metadata={"changed": True}),
                     lambda r: r["coverage"].update(source_status_rows=[{}]),
                     lambda r: r.update(retention={})]
        for mutate in mutations:
            def transform(native, index, payload):
                mutate(native)
                return native
            result, _ = self.run_fixture(fixture(1), Callback(transform))
            self.assertFalse(result["receipts"][0]["validator_contract_valid"])
            self.assertIsNotNone(result["receipts"][0]["choices"][0]["alternatives"][0]["failure"])

    def test_rejection_retention_and_coverage_are_exact(self):
        for drift in ("coverage", "retention"):
            def transform(native, index, payload):
                native = report(payload, False)
                native[drift] = {"semantic_accuracy": None} if drift == "coverage" else {}
                return native
            result, _ = self.run_fixture(fixture(1), Callback(transform))
            self.assertEqual(result["receipts"][0]["choices"][0]["alternatives"][0]["failure"],
                             "native_rejection_contract" if drift == "coverage" else "native_retention_contract")

    def test_transport_error_preserves_exact_first_native_bytes(self):
        captures = [{"returncode": 2, "stdout": b"\xffbad", "stderr": b"raw-error", "error": None},
                    {"returncode": 0, "stdout": b"not-json", "stderr": b"", "error": None},
                    {"returncode": None, "stdout": b"partial", "stderr": b"timeout", "error": "native_timeout"}]
        for capture in captures:
            result, _ = self.run_fixture(fixture(1), lambda raw: capture)
            alternative = result["receipts"][0]["choices"][0]["alternatives"][0]
            self.assertEqual(alternative["stdout_hex"], capture["stdout"].hex())
            self.assertEqual(alternative["stdout_sha256"], subject.sha(capture["stdout"]))
            self.assertFalse(alternative["mechanically_valid"])
            self.assertFalse(alternative["native_execution"])

    def test_callback_exception_does_not_skip_following_bundle(self):
        inputs = fixture(1)
        envelope = subject.parse(inputs[0]["responses"][0]["projection"]["choices"][0]["message"]["content"].encode())
        envelope["bundles"] *= 2
        self.set_content(inputs, subject.canonical(envelope).decode())
        calls = []
        def callback(raw):
            calls.append(raw)
            if len(calls) == 1:
                raise RuntimeError("invented failure")
            return Callback()(raw)
        result, _ = self.run_fixture(inputs, callback)
        self.assertEqual(len(calls), 2)
        self.assertTrue(result["receipts"][0]["choices"][0]["alternatives"][1]["mechanically_valid"])

    def test_callback_needs_identity_and_cannot_impersonate_native_subclass(self):
        inputs = fixture(1)
        with self.assertRaisesRegex(subject.NativeResultsError, "callback_identity_required"):
            subject.evaluate(*inputs, validator=Callback())
        class Pretender(subject.NativeValidator):
            def __init__(self):
                self.provenance = {"backend": "native_binary", "native_execution": True}
            def __call__(self, raw):
                return Callback()(raw)
        result, _ = self.run_fixture(inputs, Pretender())
        self.assertFalse(result["validator"]["native_execution"])
        self.assertIsNone(result["manual_review_skeleton"]["records"][0]["native_contract_valid"])

    def test_reference_skeleton_has_no_judgements_and_is_source_bound(self):
        inputs = fixture(1)
        reference = {"schema": "loom.native_semantic_review.reference/1", "cases": [{"case_id": "invented-case",
            "packet_hash": inputs[2]["cases"][0]["packet_hash"],
            "source_locators": [{"observation": "ob:invented", "ordinal": 0, "speaker": "invented", "locator": {"source": "fixture"}, "node": "invented"}],
            "criteria": [{"id": "invented-criterion", "expected_interpretation": "invented", "forbidden_interpretations": ["other"],
                          "support": [{"observation": "ob:invented", "byte_start": 0, "byte_len": 2, "quote": "Ż"}]}]}]}
        result, _ = self.run_fixture(inputs, review_reference=reference)
        judgment = result["manual_review_skeleton"]["records"][0]["criteria"][0]["bundle_judgements"][0]
        self.assertIsNone(judgment["judgement"])
        self.assertIsNone(judgment["rationale"])
        self.assertEqual(judgment["candidate_pointers"], [])
        for mutation in (lambda r: r["cases"][0].update(packet_hash="0" * 64),
                         lambda r: r["cases"].append(deepcopy(r["cases"][0])),
                         lambda r: r["cases"][0]["criteria"].append({"id": "invented-criterion"})):
            altered = deepcopy(reference)
            mutation(altered)
            with self.assertRaises(subject.NativeResultsError):
                self.run_fixture(inputs, review_reference=altered)
        for mutation, reason in ((lambda r: r["cases"][0].update(criteria=[]), "missing_case_criteria"),
                                 (lambda r: r["cases"][0]["criteria"][0]["support"][0].update(quote="X"), "exact_span_mismatch"),
                                 (lambda r: r["cases"][0]["source_locators"][0].update(node="changed"), "reference_locator_drift"),
                                 (lambda r: r["cases"][0]["criteria"][0].update(expected_interpretation=""), "empty_semantic_criterion")):
            altered, callback = deepcopy(reference), Callback()
            mutation(altered)
            with self.assertRaisesRegex(ValueError, reason):
                self.run_fixture(inputs, callback, review_reference=altered)
            self.assertEqual(callback.calls, [])

    def test_supplied_native_instance_must_match_policy_before_dispatch(self):
        inputs = fixture(1)
        with tempfile.TemporaryDirectory() as directory:
            filename = Path(directory) / "invented-binary"
            filename.write_bytes(b"no execution; mocked subprocess")
            options = inputs[4]["validator"]
            options["executable"] = str(filename)
            options["expected_binary_sha256"] = subject.sha(filename.read_bytes())
            validator = subject.NativeValidator(options)
            options["expected_binary_sha256"] = "0" * 64
            with patch.object(subject.subprocess, "run") as invoke, self.assertRaisesRegex(subject.NativeResultsError, "native_binary_hash_mismatch"):
                subject.evaluate(*inputs, validator=validator)
            invoke.assert_not_called()
            options["expected_binary_sha256"] = subject.sha(filename.read_bytes())
            options["timeout_seconds"] += 1
            with patch.object(subject.subprocess, "run") as invoke, self.assertRaisesRegex(subject.NativeResultsError, "native_validator_policy_binding_drift"):
                subject.evaluate(*inputs, validator=validator)
            invoke.assert_not_called()

    def test_mutated_native_instance_cannot_change_dispatched_binary(self):
        inputs = fixture(1)
        with tempfile.TemporaryDirectory() as directory:
            owned = Path(directory) / "owned"
            owned.write_bytes(b"mock only")
            foreign = Path(directory) / "foreign"
            foreign.write_bytes(b"must never dispatch")
            options = inputs[4]["validator"]
            options["executable"] = str(owned)
            options["expected_binary_sha256"] = subject.sha(owned.read_bytes())
            validator = subject.NativeValidator(options)
            validator.executable = foreign
            validator.timeout = 99999
            validator.bound_files = {}
            with patch.object(subject.subprocess, "run", return_value=subprocess.CompletedProcess([str(owned)], 2, b"", b"invented")) as invoke:
                subject.evaluate(*inputs, validator=validator)
            self.assertEqual(invoke.call_args.args[0], [str(owned)])
            self.assertEqual(invoke.call_args.kwargs["timeout"], options["timeout_seconds"])

    def test_native_binary_hash_and_timeout_provenance_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            filename = Path(directory) / "invented-binary"
            filename.write_bytes(b"not executed; mock subprocess only")
            options = {"executable": str(filename), "expected_binary_sha256": "0" * 64,
                       "timeout_seconds": 1, "provenance_files": []}
            with self.assertRaisesRegex(subject.NativeResultsError, "native_binary_hash_mismatch"):
                subject.NativeValidator(options)
            options["expected_binary_sha256"] = subject.sha(filename.read_bytes())
            validator = subject.NativeValidator(options)
            with patch.object(subject.subprocess, "run", side_effect=subprocess.TimeoutExpired("invented", 1, output=b"first", stderr=b"err")):
                capture = validator(b"stdin")
            self.assertEqual(capture["stdout"], b"first")
            self.assertTrue(capture["executed"])
            filename.write_bytes(b"changed")
            with patch.object(subject.subprocess, "run") as invoke, self.assertRaisesRegex(subject.NativeResultsError, "native_binary_or_provenance_changed"):
                validator(b"stdin")
            invoke.assert_not_called()

    def test_cli_hashes_exact_consumed_snapshots_and_never_overwrites(self):
        inputs = fixture(1)
        with tempfile.TemporaryDirectory() as directory:
            paths, argv = {}, []
            for name, value in zip(("normalized", "prepared", "source-inputs", "vocabulary", "policy"), inputs):
                paths[name] = Path(directory) / (name + ".json")
                paths[name].write_bytes(json.dumps(value, ensure_ascii=False, indent=1).encode())
                argv += ["--" + name, str(paths[name])]
            consumed_hash = subject.sha(paths["normalized"].read_bytes())
            target = Path(directory) / "result.json"
            original_evaluate = subject.evaluate
            def evaluate_mock(*arguments, **kwargs):
                paths["normalized"].write_bytes(b"modified after initial load")
                return original_evaluate(*arguments, validator=Callback(), callback_id="cli-test-only", **kwargs)
            with patch.object(subject, "evaluate", side_effect=evaluate_mock), patch("builtins.print"):
                subject.main(argv + ["--output", str(target)])
            self.assertEqual(subject.read(target)["source_file_sha256"]["normalized"], consumed_hash)
            paths["normalized"].write_bytes(subject.canonical(inputs[0]))
            with patch.object(subject, "evaluate", side_effect=evaluate_mock), patch("builtins.print"), self.assertRaises(FileExistsError):
                subject.main(argv + ["--output", str(target)])


class ExistingNativeBinaryTests(unittest.TestCase):
    def native(self, inputs):
        filename = os.environ.get("LOOM_CANDIDATE_GRAPH_NATIVE_TOOL")
        if not filename:
            self.skipTest("set LOOM_CANDIDATE_GRAPH_NATIVE_TOOL for actual native execution")
        options = inputs[4]["validator"]
        options["executable"] = filename
        options["expected_binary_sha256"] = subject.sha(Path(filename).read_bytes())
        return subject.evaluate(*inputs)

    def test_native_unrepresented_bundle_is_not_semantic_truth(self):
        result = self.native(fixture(1))
        receipt = result["receipts"][0]
        self.assertTrue(receipt["native_contract_valid"])
        self.assertTrue(receipt["native_verified_valid"])
        self.assertEqual(result["counts"]["native_execution_captures"], 1)
        coverage = receipt["choices"][0]["alternatives"][0]["native_report"]["coverage"]
        self.assertEqual(coverage["representation_status"], "unrepresented")
        self.assertIsNone(coverage["semantic_accuracy"])

    def test_native_wrong_packet_identifier_rejects_all_alternatives(self):
        inputs = fixture(1)
        choices = inputs[0]["responses"][0]["projection"]["choices"]
        envelope = subject.parse(choices[0]["message"]["content"].encode())
        wrong = deepcopy(envelope["bundles"][0])
        wrong["packet_id"] = "snap:wrong"
        envelope["bundles"].append(wrong)
        choices[0]["message"]["content"] = subject.canonical(envelope).decode()
        result = self.native(inputs)
        alternatives = result["receipts"][0]["choices"][0]["alternatives"]
        self.assertEqual(len(alternatives), 2)
        self.assertTrue(alternatives[0]["native_verified_valid"])
        self.assertFalse(alternatives[1]["native_verified_valid"])
        self.assertEqual(alternatives[1]["failure"], "native_candidate_rejected")
        self.assertFalse(result["receipts"][0]["native_contract_valid"])

    def test_native_multiple_choices_preserved_but_old_review_bridge_ineligible(self):
        inputs = fixture(1)
        choices = inputs[0]["responses"][0]["projection"]["choices"]
        choices.append(deepcopy(choices[0]))
        result = self.native(inputs)
        self.assertTrue(result["receipts"][0]["native_contract_valid"])
        skeleton = result["manual_review_skeleton"]["records"][0]
        self.assertIsNone(skeleton["native_contract_valid"])
        self.assertTrue(skeleton["mechanical_native_contract_valid"])
        self.assertEqual(skeleton["bundle_count"], 2)
        self.assertFalse(skeleton["single_envelope_review_compatible"])
        self.assertEqual(skeleton["alternatives"][1]["receipt_pointer"], "/choices/1/alternatives/0/bundle")

    def test_native_utf8_split_span_is_not_repaired(self):
        inputs = fixture(1)
        choices = inputs[0]["responses"][0]["projection"]["choices"]
        envelope = subject.parse(choices[0]["message"]["content"].encode())
        envelope["bundles"][0]["coverage"] = [{"support": [{"observation": "ob:invented", "byte_start": 1,
            "byte_len": 1, "quote": "Ż"}], "status": "unrepresented", "reason": "invented", "drafts": []}]
        choices[0]["message"]["content"] = subject.canonical(envelope).decode()
        result = self.native(inputs)
        alternative = result["receipts"][0]["choices"][0]["alternatives"][0]
        self.assertFalse(alternative["mechanically_valid"])
        self.assertEqual(alternative["bundle"], envelope["bundles"][0])
        self.assertEqual(alternative["failure"], "native_candidate_rejected")


if __name__ == "__main__":
    unittest.main()
