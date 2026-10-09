"""Controlled mechanics fixtures only. No model-quality experiment or transport."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import unittest

from loom.tools.structure import experiment_analysis_v1 as analysis
from loom.tools.seeding import method_graph


BASE = Path(__file__).resolve().parents[3]/"docs/research/thread7_real_2026-10-09/continuation_01"


def protocol():
    return json.loads((BASE/"evaluation-protocol.json").read_text())


def binding(variant="a", family="family1", question="q1", repetition=0, **changes):
    row = {"operation_id": f"{variant}:{family}:{question}:{repetition}", "variant_id": variant,
           "family": family, "question_id": question, "repetition": repetition,
           "panel_id": "controlled-fixtures-not-research", "split": "development",
           "source_sha256": hashlib.sha256(family.encode()).hexdigest(),
           "view_sha256": "b"*64, "request_sha256": "c"*64,
           "measurement_kind": "controlled_fixture"}
    return {**row, **changes}


def response():
    return {"text": "fixture", "structure": {"nodes": [{"id": "n1", "source_refs": ["s1"]}, {"id": "n2", "source_refs": ["s2"]}],
                                               "edges": [{"source": "n1", "target": "n2", "source_refs": ["s1"]}], "evidence": [], "limitations": []}}


def record(b=None, raw=None, **kwargs):
    return analysis.evaluate_response(json.dumps(response()).encode() if raw is None else raw,
                                      binding() if b is None else b, protocol(), source_ids=["s1", "s2"], **kwargs)


def value(row, key, evaluator="mechanical"):
    return row["metrics"][key][evaluator]["value"]


def set_metric(row, name, score, evaluator="mechanical"):
    row["metrics"][name][evaluator] = analysis.metric(score, evaluator=evaluator)
    return row


class EvaluationMechanics(unittest.TestCase):
    def test_exact_response_and_protocol_hashes(self):
        raw = json.dumps(response(), indent=3).encode()
        r = record(raw=raw)
        self.assertEqual(r["response_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(r["protocol_sha256"], analysis.digest(protocol()))
        self.assertEqual(value(r, "format"), 1)

    def test_graph_integrity_references_and_semantics_separate(self):
        r = record()
        self.assertEqual(value(r, "graph_integrity"), 1)
        self.assertEqual(value(r, "source_references"), 1)
        self.assertIsNone(value(r, "source_fidelity", "assistant"))
        self.assertIsNone(value(r, "native_execution"))

    def test_malformed_json_fails_format_without_private_error(self):
        r = record(raw=b'{"PRIVATE-CONTENT":')
        self.assertEqual(value(r, "format"), 0)
        self.assertNotIn("PRIVATE-CONTENT", json.dumps(r))
        self.assertIsNone(value(r, "graph_integrity"))

    def test_duplicate_json_key_is_failure(self):
        self.assertEqual(value(record(raw=b'{"text":"a","text":"b","structure":{}}'), "format"), 0)

    def test_missing_response_is_not_zero_success_or_spend(self):
        r = analysis.evaluate_response(None, binding(), protocol())
        self.assertIsNone(value(r, "format"))
        self.assertIsNone(value(r, "cost_usd"))
        self.assertIsNone(r["response_sha256"])

    def test_unprepared_intention_retains_null_hashes_without_placeholder(self):
        b = binding(request_sha256=None, view_sha256=None, preparation_status="dependency_pending")
        r = analysis.evaluate_response(None, b, protocol())
        self.assertIsNone(r["request_sha256"])
        self.assertIsNone(value(r, "format"))
        with self.assertRaisesRegex(ValueError, "response_without_frozen_request"):
            analysis.evaluate_response(b"{}", b, protocol())

    def test_dangling_edge_and_duplicate_id(self):
        for field in ("edge", "node"):
            doc = response()
            if field == "edge": doc["structure"]["edges"][0]["target"] = "missing"
            else: doc["structure"]["nodes"][1]["id"] = "n1"
            self.assertEqual(value(record(raw=json.dumps(doc).encode()), "graph_integrity"), 0)

    def test_no_references_is_missing_not_perfect(self):
        doc = response()
        for r in doc["structure"]["nodes"]+doc["structure"]["edges"]: r["source_refs"] = []
        r = record(raw=json.dumps(doc).encode())
        self.assertIsNone(value(r, "source_references"))
        self.assertEqual(r["metrics"]["source_references"]["mechanical"]["denominator"], 0)

    def test_unknown_reference_is_counted(self):
        doc = response(); doc["structure"]["edges"][0]["source_refs"] = ["unknown"]
        self.assertAlmostEqual(value(record(raw=json.dumps(doc).encode()), "source_references"), 2/3)

    def test_wrong_graph_root_shape_never_aborts_analysis(self):
        forms = json.loads((BASE/"evaluation-response-forms.json").read_text())
        p = analysis.select_response_form(protocol(), forms, "graph_direct")
        for raw in (b"[]", b"1", b'"private"', b"true"):
            r = analysis.evaluate_response(raw, binding(), p)
            self.assertEqual(value(r, "format"), 0)
            self.assertEqual(value(r, "graph_integrity"), 0)
            self.assertIsNone(value(r, "source_references"))

    def assessment(self):
        r = record()
        return {**{k: r[k] for k in ("protocol_sha256", "source_sha256", "view_sha256", "request_sha256", "response_sha256")},
                "metric_id": "source_fidelity", "evaluator_class": "assistant", "evaluator_id": "fixture-evaluator",
                "source_locators": [{"json_pointer": "/fixture"}], "reference_status": "provisional", "value": 0.5}

    def test_imported_provisional_judgement_stays_assistant(self):
        r = record(assessments=[self.assessment()])
        self.assertEqual(value(r, "source_fidelity", "assistant"), 0.5)
        self.assertIsNone(value(r, "source_fidelity", "independent_adjudicator"))
        self.assertFalse(r["owner_approved"])

    def test_assessment_binding_mismatch_rejected(self):
        for key in ("protocol_sha256", "source_sha256", "view_sha256", "request_sha256", "response_sha256"):
            a = self.assessment(); a[key] = "d"*64
            with self.assertRaisesRegex(ValueError, "binding_mismatch"): record(assessments=[a])

    def test_independence_not_created_by_evaluator_label(self):
        a = self.assessment(); a["evaluator_class"] = "independent_adjudicator"
        with self.assertRaisesRegex(ValueError, "independence_evidence_required"): record(assessments=[a])
        a["independence_evidence_sha256"] = "d"*64
        r = record(assessments=[a])
        self.assertFalse(r["metrics"]["source_fidelity"]["independent_adjudicator"]["evaluator_identity_independently_verified"])

    def test_judgement_cannot_overwrite_first(self):
        a = self.assessment()
        with self.assertRaisesRegex(ValueError, "duplicate_assessment"): record(assessments=[a, a])

    def test_cost_pending_is_null_latency_separate(self):
        r = record(instrumentation={"cost_usd": "0", "payer_cost_verified": False, "latency_seconds": 3, "elapsed_measured": True})
        self.assertIsNone(value(r, "cost_usd"))
        self.assertEqual(value(r, "latency_seconds"), 3)

    def test_verified_zero_is_distinct_from_unknown(self):
        r = record(instrumentation={"cost_usd": "0", "payer_cost_verified": True})
        self.assertEqual(value(r, "cost_usd"), 0)

    def test_form_selection_changes_hash_and_measures_actual_envelope(self):
        p = protocol(); forms = json.loads((BASE/"evaluation-response-forms.json").read_text())
        selected = analysis.select_response_form(p, forms, "graph_direct")
        self.assertNotEqual(analysis.digest(p), analysis.digest(selected))
        r = analysis.evaluate_response(json.dumps(response()["structure"]).encode(), binding(), selected, source_ids=["s1", "s2"])
        self.assertEqual(value(r, "format"), 1)
        self.assertEqual(value(r, "graph_integrity"), 1)
        self.assertEqual(p["mechanical"]["graph"]["pointer"], "/structure")

    def test_two_call_text_does_not_pretend_to_have_graph(self):
        forms = json.loads((BASE/"evaluation-response-forms.json").read_text())
        with self.assertRaisesRegex(ValueError, "stage_required"):
            analysis.select_response_form(protocol(), forms, "text_then_structure")
        selected = analysis.select_response_form(protocol(), forms, "text_then_structure", stage="stage_one")
        r = analysis.evaluate_response(b"plain fixture text", binding(), selected)
        self.assertEqual(value(r, "format"), 1)
        self.assertIsNone(value(r, "graph_integrity"))


class PairedMechanics(unittest.TestCase):
    def panel(self, questions=(6, 1, 1)):
        plans, records = [], []
        for f, count in enumerate(questions):
            for q in range(count):
                for variant in ("a", "b"):
                    b = binding(variant, f"family{f}", f"q{q}")
                    r = record(b)
                    set_metric(r, "format", 0 if variant == "a" else (1 if f == 0 else 0))
                    plans.append(b); records.append(r)
        return plans, records

    def compare(self, plans, records):
        return analysis.paired_comparison(plans, records, "a", "b", "format", "mechanical", protocol())

    def test_eight_questions_are_three_families(self):
        plans, records = self.panel()
        result = self.compare(plans, records)
        self.assertEqual(result["question_count"], 8)
        self.assertEqual(result["cohorts"][0]["observed_paired_families"], 3)
        self.assertAlmostEqual(result["cohorts"][0]["family_mean_delta"], 1/3)
        self.assertIsNone(result["cohorts"][0]["family_bootstrap_interval"])
        self.assertIsNone(result["global_winner"])

    def test_missing_pairs_remain_denominator(self):
        plans, records = self.panel()
        result = self.compare(plans, records[:-1])
        self.assertEqual(result["planned_pair_slots"], 8)
        self.assertEqual(result["observed_pair_slots"], 7)
        self.assertEqual(result["missing"]["missing_or_ineligible_measurement"], 1)
        self.assertEqual(result["cohorts"][0]["missing_families"], 1)

    def test_pending_unrendered_arm_stays_in_pair_denominator(self):
        plans, records = self.panel((1,))
        plans[1].update(request_sha256=None, view_sha256=None, preparation_status="pending")
        result = self.compare(plans, records[:1])
        self.assertEqual(result["planned_pair_slots"], 1)
        self.assertEqual(result["observed_pair_slots"], 0)
        self.assertEqual(result["missing"]["missing_or_ineligible_measurement"], 1)

    def test_unknown_and_failure_not_conflated(self):
        plans, records = self.panel((1,))
        records[0]["metrics"]["format"]["mechanical"] = analysis.metric()
        result = self.compare(plans, records)
        self.assertEqual(result["observed_pair_slots"], 0)
        self.assertIsNone(result["cohorts"][0]["family_mean_delta"])

    def test_duplicate_row_and_changed_bindings_rejected(self):
        plans, records = self.panel((1,))
        with self.assertRaisesRegex(ValueError, "duplicate_result"): self.compare(plans, records+[records[0]])
        records[0]["source_sha256"] = "e"*64
        with self.assertRaisesRegex(ValueError, "binding_mismatch"): self.compare(plans, records)

    def test_same_source_different_family_cannot_split(self):
        plans, records = self.panel((1,))
        changed = deepcopy(plans[0]); changed.update(operation_id="new", split="validation", family="alias", panel_id="another")
        with self.assertRaisesRegex(ValueError, "family_split_leakage"):
            analysis.measurement_summary(plans+[changed], records)

    def test_protocol_revision_not_pooled(self):
        plans, records = self.panel((1,))
        records[1]["protocol_sha256"] = "d"*64
        self.assertEqual(self.compare(plans, records)["observed_pair_slots"], 0)

    def test_graph_versus_text_structure_uses_explicit_exact_arm_protocols(self):
        forms = json.loads((BASE/"evaluation-response-forms.json").read_text())
        p = protocol()
        protocols = {"a": analysis.select_response_form(p, forms, "graph_direct"),
                     "b": analysis.select_response_form(p, forms, "text_plus_structure")}
        plans = [binding("a"), binding("b")]
        records = [analysis.evaluate_response(json.dumps(response()["structure"]).encode(), plans[0], protocols["a"]),
                   analysis.evaluate_response(json.dumps(response()).encode(), plans[1], protocols["b"])]
        result = analysis.paired_comparison(plans, records, "a", "b", "format", "mechanical", p, arm_protocols=protocols)
        self.assertEqual(result["observed_pair_slots"], 1)
        self.assertEqual(result["arm_protocol_sha256"], {a: analysis.digest(v) for a, v in protocols.items()})
        self.assertEqual(result["cohorts"][0]["family_mean_delta"], 0)
        protocols["b"]["metrics"][0]["meaning"] = "different metric"
        with self.assertRaisesRegex(ValueError, "incompatible_metric_definitions"):
            analysis.paired_comparison(plans, records, "a", "b", "format", "mechanical", p, arm_protocols=protocols)

    def test_synthetic_and_historical_cannot_be_new_real_quality(self):
        plans, records = self.panel((1,))
        result = analysis.tradeoff(plans, records, "a", "b", "source_fidelity", "assistant", protocol())
        self.assertEqual(result["dimensions"]["quality"]["observed_pair_slots"], 0)
        self.assertIsNone(result["winner"])

    def test_minimum_family_bootstrap_is_reproducible(self):
        plans, records = self.panel((1, 1, 1, 1, 1, 1))
        result = self.compare(plans, records)
        self.assertIsNotNone(result["cohorts"][0]["family_bootstrap_interval"])
        self.assertEqual(result, self.compare(plans, records))

    def test_missing_spend_does_not_become_zero_total(self):
        plans, records = self.panel((1,))
        records[0]["instrumentation"] = {"cost_usd": "0.000000001", "payer_cost_verified": True}
        result = analysis.measurement_summary(plans, records)
        self.assertEqual(Decimal(result["observed_cost_subtotal_usd"]), Decimal("0.000000001"))
        self.assertEqual(result["cost_unknown_count"], 1)
        self.assertIsNone(result["total_cost_usd"])

    def test_panel_and_split_strata_not_pooled(self):
        plans, records = self.panel((1, 1))
        for collection in (plans, records):
            for row in collection:
                if row["family"] == "family1": row["panel_id"] = "new-panel"; row["split"] = "validation"
        result = self.compare(plans, records)
        self.assertEqual(len(result["cohorts"]), 2)


class StabilityMechanics(unittest.TestCase):
    def test_cache_hit_unknown_and_same_attempt_excluded(self):
        plans, records = [], []
        for i in range(5):
            b = binding(repetition=i); plans.append(b)
            r = record(b); r["measurement_kind"] = "new_real_export_inference"
            r["instrumentation"] = {"provider_attempt_id": "attempt" if i in (0, 3) else f"attempt{i}",
                                    "response_cache_hit": {0: False, 1: True, 2: None, 3: False, 4: False}[i]}
            records.append(r)
        result = analysis.stability(plans, records)[0]
        self.assertEqual(result["eligible_separate_attempts"], 2)
        self.assertEqual(result["exact_response_byte_agreement"], 1)
        self.assertIsNone(result["semantic_stability"])
        self.assertFalse(result["independent_inference_guaranteed"])

    def test_one_attempt_cannot_establish_stability(self):
        b = binding(); r = record(b)
        self.assertIsNone(analysis.stability([b], [r])[0]["exact_response_byte_agreement"])

    def test_request_or_view_or_protocol_drift_is_separate_cohort(self):
        for field in ("request_sha256", "view_sha256", "protocol_sha256"):
            plans, records = [], []
            for i in range(2):
                b = binding(repetition=i)
                if field != "protocol_sha256" and i: b[field] = "e"*64
                r = record(b); r["measurement_kind"] = "new_real_export_inference"
                if field == "protocol_sha256" and i: r[field] = "e"*64
                r["instrumentation"] = {"provider_attempt_id": str(i), "response_cache_hit": False}
                plans.append(b); records.append(r)
            rows = analysis.stability(plans, records)
            self.assertEqual(len(rows), 2)
            self.assertTrue(all(row["exact_response_byte_agreement"] is None for row in rows))


class TwoStageComposition(unittest.TestCase):
    def fixture(self):
        forms = json.loads((BASE/"evaluation-response-forms.json").read_text())
        p1 = analysis.select_response_form(protocol(), forms, "text_then_structure", stage="stage_one")
        p2 = analysis.select_response_form(protocol(), forms, "text_then_structure", stage="stage_two")
        raw1 = b"exact first stage fixture text"
        req2 = method_graph.canonical({"messages": [{"role": "user", "content": raw1.decode()}]})
        b1 = binding("two", measurement_kind="new_real_export_inference")
        b2 = {**b1, "operation_id": "stage-two", "request_sha256": hashlib.sha256(req2).hexdigest()}
        r1 = analysis.evaluate_response(raw1, b1, p1, instrumentation={"cost_usd": "0.1", "payer_cost_verified": True, "latency_seconds": 2, "elapsed_measured": True})
        r2 = analysis.evaluate_response(json.dumps(response()["structure"]).encode(), b2, p2, instrumentation={"cost_usd": "0.2", "payer_cost_verified": True, "latency_seconds": 3, "elapsed_measured": True})
        dependency = {"first_operation_id": b1["operation_id"], "first_response_sha256": r1["response_sha256"],
                      "second_operation_id": b2["operation_id"], "second_request_sha256": b2["request_sha256"], "input_json_pointer": "/messages/0/content"}
        return b1, r1, b2, r2, {"first_response_bytes": raw1, "second_request_bytes": req2, "dependency": dependency}

    def test_compose_verified_stages_cost_time_and_phase_errors(self):
        b1, r1, b2, r2, kwargs = self.fixture()
        plan, row = analysis.compose_two_stage_outcome(b1, r1, b2, r2, protocol(), **kwargs)
        self.assertEqual(row["instrumentation"]["cost_usd"], "0.3")
        self.assertEqual(value(row, "latency_seconds"), 5)
        self.assertEqual(row["phase_records"]["generation"], r1)
        self.assertEqual(row["phase_records"]["extraction"], r2)
        self.assertIsNone(value(row, "source_fidelity", "assistant"))
        self.assertEqual(plan["request_hash_scope"], "analysis_stage_binding_not_transport_request")

    def test_pending_second_stage_total_cost_unknown_and_subtotal_kept(self):
        b1, r1, _, _, _ = self.fixture()
        plan, row = analysis.compose_two_stage_outcome(b1, r1, None, None, protocol())
        self.assertEqual(row["status"], "extraction_pending")
        self.assertIsNone(value(row, "cost_usd"))
        self.assertEqual(row["instrumentation"]["observed_stage_cost_subtotal_usd"], "0.1")
        self.assertIsNone(row["phase_records"]["extraction"])

    def test_actual_second_request_must_consume_exact_first_bytes(self):
        b1, r1, b2, r2, kwargs = self.fixture()
        kwargs["first_response_bytes"] = b"different"
        with self.assertRaisesRegex(ValueError, "first_response_bytes_mismatch"):
            analysis.compose_two_stage_outcome(b1, r1, b2, r2, protocol(), **kwargs)
        b1, r1, b2, r2, kwargs = self.fixture()
        kwargs["dependency"]["input_json_pointer"] = "/messages/0/role"
        with self.assertRaisesRegex(ValueError, "stage_dependency_input_mismatch"):
            analysis.compose_two_stage_outcome(b1, r1, b2, r2, protocol(), **kwargs)

    def test_one_and_two_call_pairs_have_one_slot_and_both_costs(self):
        b1, r1, b2, r2, kwargs = self.fixture()
        bp, rp = analysis.compose_two_stage_outcome(b1, r1, b2, r2, protocol(), **kwargs)
        direct = binding("direct", measurement_kind="new_real_export_inference")
        rr = record(direct, instrumentation={"cost_usd": "0.15", "payer_cost_verified": True, "latency_seconds": 4, "elapsed_measured": True})
        paired = analysis.paired_comparison([direct, bp], [rr, rp], "direct", "two", "cost_usd", "mechanical", protocol())
        self.assertEqual(paired["planned_pair_slots"], 1)
        self.assertAlmostEqual(paired["cohorts"][0]["family_mean_delta"], 0.15)

    def test_failed_second_format_retains_cost_and_first_evidence(self):
        b1, r1, b2, r2, kwargs = self.fixture()
        set_metric(r2, "format", 0)
        _, row = analysis.compose_two_stage_outcome(b1, r1, b2, r2, protocol(), **kwargs)
        self.assertEqual(value(row, "format"), 0)
        self.assertEqual(value(row, "cost_usd"), 0.3)
        self.assertEqual(row["phase_records"]["generation"], r1)

    def test_unknown_stage_bill_not_zero(self):
        b1, r1, b2, r2, kwargs = self.fixture()
        r2["instrumentation"]["payer_cost_verified"] = False
        _, row = analysis.compose_two_stage_outcome(b1, r1, b2, r2, protocol(), **kwargs)
        self.assertIsNone(value(row, "cost_usd"))
        self.assertEqual(row["instrumentation"]["cost_unknown_stages"], 1)

    def test_composition_accepts_actual_context_materializer_nested_json(self):
        from loom.tools.structure import experiment_context_preparation_v1 as context
        from loom.tools.structure.test_experiment_context_preparation_v1 import fixture
        source, task, plan, profiles, variant = fixture()
        variant["response_form"] = "text_then_structure"
        prepared = context.prepare_variant(source, task, variant, plan, profiles)
        first_bytes = b"controlled captured reply for adapter regression"
        extraction = context.materialize_extraction(prepared, {
            "call_id": prepared["calls"][0]["id"], "status": "completed", "origin": "model_inference",
            "response_cache_hit": False, "text": first_bytes.decode(),
            "text_sha256": hashlib.sha256(first_bytes).hexdigest(), "immutable_capture_sha256": "a"*64}, plan)
        forms = json.loads((BASE/"evaluation-response-forms.json").read_text())
        p1 = analysis.select_response_form(protocol(), forms, "text_then_structure", stage="stage_one")
        p2 = analysis.select_response_form(protocol(), forms, "text_then_structure", stage="stage_two")
        b1 = binding("two", operation_id=prepared["calls"][0]["id"], request_sha256=prepared["calls"][0]["request_sha256"])
        b2 = {**b1, "operation_id": extraction["id"], "request_sha256": extraction["request_sha256"]}
        r1 = analysis.evaluate_response(first_bytes, b1, p1)
        r2 = analysis.evaluate_response(json.dumps(response()["structure"]).encode(), b2, p2)
        dependency = {"first_operation_id": b1["operation_id"], "first_response_sha256": r1["response_sha256"],
                      "second_operation_id": b2["operation_id"], "second_request_sha256": b2["request_sha256"],
                      "input_json_pointer": "/messages/1/content", "input_decoding": [{"format": "json", "pointer": "/answer_text"}]}
        _, composed = analysis.compose_two_stage_outcome(b1, r1, b2, r2, protocol(), first_response_bytes=first_bytes,
                         second_request_bytes=method_graph.canonical(extraction["request"]), dependency=dependency)
        self.assertEqual(value(composed, "format"), 1)
        self.assertIsNone(value(composed, "cost_usd"))
        self.assertEqual(extraction["input_text_sha256"], composed["dependency"]["first_response_sha256"])


class ExistingContract(unittest.TestCase):
    def test_public_contract_existing_schema_codec_and_recovery(self):
        presentation = json.loads((BASE/"handoff-example.json").read_text())
        projection = method_graph.load_projection(BASE/"handoff-projection.json")
        artifact = analysis.build_handoff_artifact(presentation, protocol(), projection, projected_at="2026-10-09T00:00:00Z")
        self.assertEqual(artifact["contract"]["schema"], "loom.method_graph/1")
        self.assertEqual(artifact["trace"]["schema"], "loom.method_run_trace/1")
        recovered = method_graph.recover_results(artifact)
        self.assertTrue(recovered)
        files = method_graph.recover_files(artifact)
        self.assertEqual(files["policy"], method_graph.canonical(protocol()))
        self.assertEqual(artifact["trace"]["measurements"]["adapter_provider_calls"], 0)
        self.assertFalse(artifact["producer_evidence"]["producer_execution_verified"])

    def test_presentation_machine_schema(self):
        from jsonschema import Draft202012Validator
        example = json.loads((BASE/"handoff-example.json").read_text())
        validator = Draft202012Validator(json.loads((BASE/"handoff-contract.json").read_text()))
        validator.validate(example)
        modified = deepcopy(example); modified["expert"]["quality"] = "winner"
        with self.assertRaises(Exception): validator.validate(modified)


from decimal import Decimal

if __name__ == "__main__":
    unittest.main()
