"""Invented offline fixtures for the source-bound repetition helper.

No actual programme outputs, credentials, account metadata or provider calls.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

try:
    from . import programme_repetition_v1 as r
except ImportError:
    import programme_repetition_v1 as r


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class RepetitionFixture:
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="invented-repetition-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source_dir = self.directory / "source"
        self.source_dir.mkdir()
        self.source_path = self.source_dir / "manifest.json"
        self.output = self.directory / "projected"
        self.queries = ["invented-query-Z", "invented-query-A"]
        self.arms = ["invented-arm-blue", "invented-arm-green"]
        self.membership = [
            {"selection_id": "instrument-first", "arm_id": self.arms[0]},
            {"selection_id": "instrument-second", "arm_id": self.arms[1]},
        ]
        self.selection = {"selected": [row["selection_id"] for row in self.membership]}
        self.selection_raw = canonical(self.selection)
        self.originals = {}
        # Source order deliberately differs from the explicit target order.
        operations = []
        for arm in reversed(self.arms):
            for query in reversed(self.queries):
                state = canonical({"query": {"id": query}, "source": "invented żółw"}).decode()
                body = {"model": "invented/model-v1",
                        "provider": {"only": ["invented-provider"], "allow_fallbacks": False},
                        "state": {"text": state},
                        "questions": {
                            "q01": {"type": "noul", "instructions": arm + " support", "criteria": {"true": "yes", "false": "no"}},
                            "q02": {"type": "noul", "instructions": arm + " refute", "criteria": {"true": "yes", "false": "no"}}}}
                # Retain noncanonical whitespace and Unicode as exact source bytes.
                raw = (json.dumps(body, ensure_ascii=False, indent=1) + "\n").encode()
                filename = arm + "." + query + ".json"
                (self.source_dir / filename).write_bytes(raw)
                operation = {
                    "operation_id": arm + "." + query,
                    "route_id": "jev", "request_file": filename, "request_sha256": digest(raw),
                    "model_id": body["model"], "provider_id": "invented-provider",
                    "units_upper_bounds": {"prompt": 5001, "completion": 0, "request": 1, "invented_charge": "0.25"},
                    "minimum_reservation_usd": "0.0031",
                    "metadata": {"arm_id": arm, "prepared_request_id": query,
                                 "untouched": {"values": ["one", "two"]}}}
                operations.append(operation)
                self.originals[(arm, query)] = (operation, raw)
        self.source = {"schema": "loom.research_programme_manifest/1", "programme_id": "invented-programme",
                       "stage_id": "invented-source", "operations": operations,
                       "metadata": {"invented_provenance": "fixture-only"}}
        self.source_path.write_bytes(canonical(self.source))
        self.gold_raw = canonical({"invented": "gold placeholder until score interface is supplied"})
        self.policy = {
            "schema": "loom.programme_repetition_policy/1",
            "programme_id": "invented-programme", "stage_id": "invented-repeat-stage",
            "repetitions": 2, "order_dimensions": ["repetition", "query", "arm"], "seed": None,
            "source_manifest_sha256": digest(self.source_path.read_bytes()),
            "selection_sha256": digest(self.selection_raw),
            "selection_ids_pointer": "/selected", "arm_membership": self.membership,
            "arm_metadata_pointer": "/arm_id", "query_metadata_pointer": "/prepared_request_id",
            "planned_query_ids": self.queries,
            "query_identity": {"json_string_pointer": "/state/text", "id_pointer": "/query/id"},
            "prompt_pointers": ["/questions"], "scoring": {"gold_sha256": digest(self.gold_raw), "bootstrap": None, "label_response_states": ["completed", "pending_billing"]}}
        self.policy_raw = canonical(self.policy)

    def prepare(self, policy=None, selection=None):
        return r.prepare(self.source_path,
                         self.selection_raw if selection is None else canonical(selection),
                         self.policy_raw if policy is None else canonical(policy), self.output)

    def verify(self, policy=None, selection=None):
        return r.verify(self.source_path,
                        self.selection_raw if selection is None else canonical(selection),
                        self.policy_raw if policy is None else canonical(policy), self.output / "manifest.json")

    def replace_source(self, source):
        self.source = source
        self.source_path.write_bytes(canonical(source))
        self.policy["source_manifest_sha256"] = digest(self.source_path.read_bytes())
        self.policy_raw = canonical(self.policy)


class RepetitionTests(RepetitionFixture, unittest.TestCase):
    def test_explicit_grid_order_preserves_exact_bytes_units_pair_and_floor(self):
        manifest = self.prepare()
        operations = manifest["operations"]
        self.assertEqual(len(operations), 8)
        self.assertEqual(len({op["operation_id"] for op in operations}), 8)
        expected = [self.originals[(arm, query)] for _ in range(2)
                    for query in self.queries for arm in self.arms]
        for repeated, (source, raw) in zip(operations, expected):
            self.assertEqual((self.output / repeated["request_file"]).read_bytes(), raw)
            for field in ("request_sha256", "route_id", "model_id", "provider_id",
                          "units_upper_bounds", "minimum_reservation_usd"):
                self.assertEqual(repeated[field], source[field])
            self.assertNotEqual(raw, canonical(json.loads(raw)))
        self.assertEqual(self.verify(), manifest)

    def test_count_and_order_are_policy_data(self):
        policy = deepcopy(self.policy)
        policy["repetitions"] = 3
        policy["order_dimensions"] = ["arm", "query", "repetition"]
        manifest = self.prepare(policy=policy)
        expected = [self.originals[(arm, query)][0]["request_sha256"]
                    for arm in self.arms for query in self.queries for _ in range(3)]
        self.assertEqual([op["request_sha256"] for op in manifest["operations"]], expected)

    def test_clones_do_not_share_mutable_units(self):
        manifest = self.prepare()
        operations = manifest["operations"]
        matching = [op for op in operations if op["request_sha256"] == operations[0]["request_sha256"]]
        self.assertEqual(len(matching), 2)
        matching[0]["units_upper_bounds"]["prompt"] = 99
        self.assertEqual(matching[1]["units_upper_bounds"]["prompt"], 5001)
        self.assertEqual(json.loads(self.source_path.read_bytes()), self.source)

    def test_changed_source_manifest_rejected_before_writes(self):
        source = deepcopy(self.source)
        source["metadata"]["invented_provenance"] = "changed"
        self.source_path.write_bytes(canonical(source))
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_changed_source_body_and_recomputed_operation_hash_still_fails_source_binding(self):
        source = deepcopy(self.source)
        operation = source["operations"][0]
        path = self.source_dir / operation["request_file"]
        body = json.loads(path.read_bytes())
        body["questions"]["q01"]["instructions"] = "changed prompt"
        raw = canonical(body)
        path.write_bytes(raw)
        operation["request_sha256"] = digest(raw)
        self.source_path.write_bytes(canonical(source))
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_changed_child_body_and_recomputed_hash_is_not_a_repetition(self):
        manifest = self.prepare()
        operation = manifest["operations"][0]
        path = self.output / operation["request_file"]
        body = json.loads(path.read_bytes())
        body["questions"]["q01"]["instructions"] = "replacement prompt"
        raw = canonical(body)
        path.write_bytes(raw)
        operation["request_sha256"] = digest(raw)
        (self.output / "manifest.json").write_bytes(canonical(manifest))
        with self.assertRaises(ValueError):
            self.verify()

    def test_changed_child_units_or_floor_rejected(self):
        self.prepare()
        path = self.output / "manifest.json"
        original = path.read_bytes()
        for field, value in (("units_upper_bounds", {"prompt": 1}), ("minimum_reservation_usd", "0")):
            manifest = json.loads(original)
            manifest["operations"][0][field] = value
            path.write_bytes(canonical(manifest))
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.verify()

    def test_duplicate_or_unknown_selection_ids_rejected(self):
        for selected in (["instrument-first", "instrument-first"], ["instrument-first", "unknown"]):
            selection = {"selected": selected}
            policy = deepcopy(self.policy)
            policy["selection_sha256"] = digest(canonical(selection))
            with self.subTest(selected=selected), self.assertRaises(ValueError):
                r.prepare(self.source_path, canonical(selection), canonical(policy), self.output)
            self.assertFalse(self.output.exists())

    def test_missing_selected_arm_or_query_rejected(self):
        for excluded in (self.arms[0], self.queries[0]):
            source = deepcopy(self.source)
            source["operations"] = [op for op in source["operations"]
                                    if excluded not in (op["metadata"]["arm_id"], op["metadata"]["prepared_request_id"])]
            self.replace_source(source)
            with self.subTest(excluded=excluded), self.assertRaises(ValueError):
                self.prepare()
            self.assertFalse(self.output.exists())
            # Restore the full source for the second subcase.
            source = deepcopy(self.source)
            source["operations"] = [deepcopy(value[0]) for value in self.originals.values()]
            self.replace_source(source)

    def test_duplicate_source_arm_query_and_planned_query_ids_rejected(self):
        source = deepcopy(self.source)
        clone = deepcopy(source["operations"][0])
        clone["operation_id"] += ".different-id-same-arm-query"
        source["operations"].append(clone)
        self.replace_source(source)
        with self.assertRaises(ValueError):
            self.prepare()
        source["operations"].pop()
        self.replace_source(source)
        policy = deepcopy(self.policy)
        policy["planned_query_ids"] = [self.queries[0], self.queries[0]]
        with self.assertRaises(ValueError):
            self.prepare(policy=policy)
        self.assertFalse(self.output.exists())

    def test_query_id_in_body_must_match_source_metadata(self):
        source = deepcopy(self.source)
        operation = source["operations"][0]
        path = self.source_dir / operation["request_file"]
        body = json.loads(path.read_bytes())
        state = json.loads(body["state"]["text"])
        state["query"]["id"] = "different-query"
        body["state"]["text"] = canonical(state).decode()
        raw = canonical(body)
        path.write_bytes(raw)
        operation["request_sha256"] = digest(raw)
        self.replace_source(source)
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_invalid_repetition_counts_rejected(self):
        for count in (True, 0, -1, 1.5, "2", None):
            policy = deepcopy(self.policy)
            policy["repetitions"] = count
            with self.subTest(count=count), self.assertRaises(ValueError):
                self.prepare(policy=policy)
            self.assertFalse(self.output.exists())

    def test_child_duplicate_or_missing_operation_rejected(self):
        self.prepare()
        path = self.output / "manifest.json"
        original = path.read_bytes()
        for action in ("duplicate", "missing"):
            manifest = json.loads(original)
            if action == "duplicate":
                manifest["operations"].append(deepcopy(manifest["operations"][0]))
            else:
                manifest["operations"].pop()
            path.write_bytes(canonical(manifest))
            with self.subTest(action=action), self.assertRaises(ValueError):
                self.verify()


class RepetitionScoreTests(RepetitionFixture, unittest.TestCase):
    def setUp(self):
        RepetitionFixture.setUp(self)
        self.gold_raw = canonical([{
            "case_id": "invented-source-case", "family": "invented-family", "language": "xx",
            "judgments": [{"query_id": query, "label": "supported"} for query in self.queries]}])
        self.policy["scoring"]["gold_sha256"] = digest(self.gold_raw)
        self.policy_raw = canonical(self.policy)

    def bundle(self):
        manifest = self.prepare()
        manifest_hash = digest((self.output / "manifest.json").read_bytes())
        requests, rows, responses = [], [], []
        for index, op in enumerate(manifest["operations"]):
            generation_id = "invented-generation-" + str(index)
            body = json.loads((self.output / op["request_file"]).read_bytes())
            projection = {"id": generation_id, "model": op["model_id"],
                          "usage": {"cost": "0.0001"},
                          "answers": {"q01": {"type": "noul", "noul": .9},
                                      "q02": {"type": "noul", "noul": .1}}}
            generation = {"id": generation_id, "model": op["model_id"],
                          "provider_name": op["provider_id"], "is_byok": False,
                          "total_cost": "0.0001"}
            response_hash, generation_hash = digest(canonical(projection)), digest(canonical(generation))
            requests.append({"operation_id": op["operation_id"], "request_sha256": op["request_sha256"], "body": body})
            rows.append({"operation_id": op["operation_id"], "request_sha256": op["request_sha256"],
                         "manifest_sha256": manifest_hash, "state": "completed", "http_status": 200,
                         "response_available": True, "response_ledger_bound": True,
                         "exact_sent_request_capture_verified": True,
                         "requested_model": op["model_id"], "requested_provider": op["provider_id"],
                         "response_model": op["model_id"], "observed_model": op["model_id"],
                         "observed_provider": op["provider_id"], "response_sha256": response_hash,
                         "generation_sha256": generation_hash, "generation_id": generation_id,
                         "reported_cost_usd": "0.0001", "billing_verified": True,
                         "billing_replay_verified": True, "is_byok": False, "actual_cost_usd": "0.0001"})
            responses.append({"operation_id": op["operation_id"], "response_sha256": response_hash,
                              "generation_sha256": generation_hash, "projection": projection,
                              "generation_projection": generation})
        return {"schema": "loom.programme_results/1", "programme_id": manifest["programme_id"],
                "stage_id": manifest["stage_id"], "manifest_sha256": manifest_hash,
                "planned_operation_ids": [op["operation_id"] for op in manifest["operations"]],
                "planned_operations": len(manifest["operations"]), "saved_row_count": len(rows),
                "requests": requests, "rows": rows, "responses": responses}

    def score(self, bundle, gold_raw=None):
        return r.score(self.source_path, self.selection_raw, self.policy_raw,
                       self.output / "manifest.json", canonical(bundle),
                       self.gold_raw if gold_raw is None else gold_raw)

    def test_all_first_answers_correct_with_planned_repeat_denominators(self):
        result = self.score(self.bundle())
        self.assertEqual(result["planned_operations"], 8)
        self.assertEqual(len(result["records"]), 8)
        self.assertEqual([arm["arm_id"] for arm in result["arms"]], self.arms)
        for arm in result["arms"]:
            self.assertEqual(arm["pooled_source_label_report"]["query_count"], 4)
            self.assertEqual(arm["pooled_source_label_report"]["available"], 4)
            self.assertEqual(arm["pooled_source_label_report"]["accuracy_all_queries"], 1)
            for repetition in arm["per_repetition"]:
                self.assertEqual(repetition["source_label_report"]["query_count"], 2)
                self.assertEqual(repetition["source_label_report"]["available"], 2)
            self.assertEqual(arm["cost"], {"known_verified_usd": "0.0004", "unknown_planned_attempts": 0,
                                          "complete": True, "total_usd": "0.0004"})

    def test_missing_or_first_failed_answer_stays_in_denominator(self):
        bundle = self.bundle()
        first = bundle["rows"][0]["operation_id"]
        missing = deepcopy(bundle)
        missing["rows"] = [row for row in missing["rows"] if row["operation_id"] != first]
        missing["responses"] = [row for row in missing["responses"] if row["operation_id"] != first]
        missing["saved_row_count"] = len(missing["rows"])
        failed = deepcopy(bundle)
        failed["rows"][0]["state"] = "uncertain"
        for changed in (missing, failed):
            result = self.score(changed)
            report = result["arms"][0]["pooled_source_label_report"]
            self.assertEqual((report["query_count"], report["available"], report["unavailable"]), (4, 3, 1))
            self.assertEqual(report["accuracy_all_queries"], .75)
            self.assertEqual(result["records"][0]["prediction"]["state"], "unavailable")
            self.assertEqual(result["arms"][0]["cost"]["unknown_planned_attempts"], 1)

    def test_first_http_error_without_generation_identity_stays_unavailable(self):
        bundle = self.bundle()
        bundle["rows"][0].update(state="uncertain", http_status=400, generation_id=None,
                                  billing_verified=False, billing_replay_verified=False,
                                  actual_cost_usd=None)
        bundle["responses"][0]["projection"] = {"error": {"message": "invented first failure"}}
        result = self.score(bundle)
        self.assertEqual(result["records"][0]["prediction"]["state"], "unavailable")
        self.assertEqual(result["arms"][0]["pooled_source_label_report"]["query_count"], 4)
        self.assertEqual(result["arms"][0]["pooled_source_label_report"]["unavailable"], 1)

    def test_valid_label_remains_available_when_billing_replay_is_unknown(self):
        bundle = self.bundle()
        bundle["rows"][0]["billing_replay_verified"] = False
        result = self.score(bundle)
        self.assertEqual(result["arms"][0]["pooled_source_label_report"]["available"], 4)
        self.assertEqual(result["records"][0]["prediction"]["label"], "supported")
        self.assertIsNone(result["records"][0]["actual_cost_usd"])
        cost = result["arms"][0]["cost"]
        self.assertEqual(cost["known_verified_usd"], "0.0003")
        self.assertEqual(cost["unknown_planned_attempts"], 1)
        self.assertFalse(cost["complete"])
        self.assertIsNone(cost["total_usd"])

    def test_threshold_unknown_conflict_and_malformed_answers_are_distinct(self):
        original = self.bundle()
        cases = [
            ({"q01": {"type": "noul", "noul": .5}, "q02": {"type": "noul", "noul": .5}}, "unknown", 4),
            ({"q01": {"type": "noul", "noul": .9}, "q02": {"type": "noul", "noul": .9}}, "conflicting", 3),
            ({"q01": {"type": "noul", "noul": .9}}, None, 3),
            ({"q01": {"type": "noul", "noul": True}, "q02": {"type": "noul", "noul": .1}}, None, 3),
        ]
        for answers, label, available in cases:
            bundle = deepcopy(original)
            bundle["responses"][0]["projection"]["answers"] = answers
            with self.subTest(label=label, answers=answers):
                result = self.score(bundle)
                self.assertEqual(result["records"][0]["prediction"].get("label"), label)
                self.assertEqual(result["arms"][0]["pooled_source_label_report"]["available"], available)
                self.assertTrue(result["arms"][0]["cost"]["complete"])

    def test_duplicate_first_row_or_capture_cannot_replace_failed_answer(self):
        original = self.bundle()
        for inventory in ("rows", "responses"):
            bundle = deepcopy(original)
            bundle["rows"][0]["state"] = "uncertain"
            replacement = deepcopy(bundle[inventory][0])
            if inventory == "rows":
                replacement["state"] = "completed"
            else:
                replacement["projection"]["answers"]["q01"]["noul"] = .99
            bundle[inventory].append(replacement)
            bundle["saved_row_count"] = len(bundle["rows"])
            with self.subTest(inventory=inventory), self.assertRaises(ValueError):
                self.score(bundle)

    def test_unknown_or_request_binding_changed_first_row_rejected(self):
        original = self.bundle()
        for field, value in (("operation_id", "unplanned-operation"), ("request_sha256", "a" * 64),
                             ("manifest_sha256", "b" * 64)):
            bundle = deepcopy(original)
            bundle["rows"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.score(bundle)

    def test_planned_inventory_changed_or_public_request_missing_rejected(self):
        original = self.bundle()
        variants = []
        changed = deepcopy(original)
        changed["planned_operation_ids"].reverse()
        variants.append(changed)
        changed = deepcopy(original)
        changed["requests"].pop()
        variants.append(changed)
        changed = deepcopy(original)
        changed["requests"].append(deepcopy(changed["requests"][0]))
        variants.append(changed)
        for bundle in variants:
            with self.assertRaises(ValueError):
                self.score(bundle)

    def test_orphan_capture_without_first_row_rejected(self):
        bundle = self.bundle()
        bundle["rows"].pop(0)
        bundle["saved_row_count"] = len(bundle["rows"])
        with self.assertRaises(ValueError):
            self.score(bundle)

    def test_public_request_boolean_number_substitution_rejected(self):
        bundle = self.bundle()
        bundle["requests"][0]["body"]["provider"]["allow_fallbacks"] = 0
        with self.assertRaises(ValueError):
            self.score(bundle)

    def test_explicit_billing_flags_cannot_contradict_known_credit_claim(self):
        original = self.bundle()
        for mode in ("byok", "unknown"):
            bundle = deepcopy(original)
            bundle["rows"][0]["billing_mode"] = mode
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, "repetition_credit_attestation_contradiction"):
                self.score(bundle)
        bundle = deepcopy(original)
        bundle["responses"][0]["projection"]["usage"]["is_byok"] = True
        with self.assertRaisesRegex(ValueError, "repetition_credit_attestation_contradiction"):
            self.score(bundle)

    def test_pending_credit_not_inferred_from_unknown_or_byok_first_usage(self):
        original = self.bundle()
        for mode, is_byok in (("unknown", None), ("byok", True)):
            bundle = deepcopy(original)
            bundle["rows"][0].update(state="pending_billing", billing_mode=mode,
                billing_verified=False, billing_replay_verified=False, actual_cost_usd=None)
            bundle["responses"][0]["projection"]["usage"]["is_byok"] = is_byok
            result = self.score(bundle)
            self.assertEqual(result["records"][0]["prediction"]["label"], "supported")
            self.assertIsNone(result["records"][0]["actual_cost_usd"])
            self.assertFalse(result["records"][0]["billing_complete"])

    def test_verified_cost_contradiction_rejected(self):
        original = self.bundle()
        for field in ("actual_cost_usd", "reported_cost_usd"):
            bundle = deepcopy(original)
            bundle["rows"][0][field] = "0.01"
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.score(bundle)
        bundle = deepcopy(original)
        bundle["responses"][0]["generation_projection"]["total_cost"] = "0.2"
        with self.assertRaises(ValueError):
            self.score(bundle)

    def test_equal_missing_or_malformed_capture_hashes_do_not_bind_an_answer(self):
        original = self.bundle()
        for value in (None, "", "not-a-sha256", "A" * 64):
            bundle = deepcopy(original)
            bundle["rows"][0].update(response_sha256=value, generation_sha256=value,
                                     billing_verified=False, billing_replay_verified=False)
            bundle["responses"][0].update(response_sha256=value, generation_sha256=value)
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "repetition_first_capture_hash_invalid"):
                self.score(bundle)

    def test_pending_billing_bound_first_answer_scores_without_credit(self):
        bundle = self.bundle()
        bundle["rows"][0].update(state="pending_billing", generation_sha256=None, billing_verified=False,
                                 billing_replay_verified=False, actual_cost_usd=None)
        bundle["responses"][0].update(generation_sha256=None, generation_projection={})
        result = self.score(bundle)
        self.assertEqual(result["records"][0]["prediction"]["label"], "supported")
        self.assertEqual(result["arms"][0]["pooled_source_label_report"]["available"], 4)
        self.assertEqual(result["records"][0]["source_first_row_state"], "pending_billing")
        self.assertIsNone(result["records"][0]["actual_cost_usd"])
        self.assertIsNone(result["arms"][0]["cost"]["total_usd"])

    def test_pending_billing_without_bound_generation_id_is_not_a_valid_first_receipt(self):
        bundle = self.bundle()
        bundle["rows"][0].update(state="pending_billing", generation_id=None, generation_sha256=None,
                                 billing_verified=False, billing_replay_verified=False, actual_cost_usd=None)
        bundle["responses"][0]["projection"].pop("id")
        bundle["responses"][0].update(generation_sha256=None, generation_projection={})
        with self.assertRaisesRegex(ValueError, "repetition_generation_identity_invalid"):
            self.score(bundle)

    def test_pending_billing_label_eligibility_is_caller_data(self):
        self.policy["scoring"]["label_response_states"] = ["completed"]
        self.policy_raw = canonical(self.policy)
        bundle = self.bundle()
        bundle["rows"][0].update(state="pending_billing", billing_verified=False, billing_replay_verified=False, actual_cost_usd=None)
        result = self.score(bundle)
        self.assertEqual(result["records"][0]["prediction"]["state"], "unavailable")
        self.assertEqual(result["arms"][0]["pooled_source_label_report"]["available"], 3)

    def test_same_successful_capture_hash_cannot_prove_distinct_repeats(self):
        original = self.bundle()
        for component, reason in (("response_sha256", "repetition_duplicate_successful_first_capture"),
                                  ("generation_sha256", "repetition_duplicate_verified_generation_capture")):
            bundle = deepcopy(original)
            bundle["rows"][1][component] = bundle["rows"][0][component]
            bundle["responses"][1][component] = bundle["responses"][0][component]
            with self.subTest(component=component), self.assertRaisesRegex(ValueError, reason):
                self.score(bundle)

    def test_shared_failed_error_capture_is_retained_without_completed_claim(self):
        bundle = self.bundle()
        for index in (0, 1):
            bundle["rows"][index].update(state="uncertain", http_status=400, generation_id=None,
                billing_verified=False, billing_replay_verified=False, actual_cost_usd=None,
                response_sha256="a" * 64, generation_sha256=None)
            bundle["responses"][index].update(projection={"error": "same invented provider failure"},
                response_sha256="a" * 64, generation_sha256=None, generation_projection={})
        result = self.score(bundle)
        self.assertEqual([r["prediction"]["state"] for r in result["records"][:2]], ["unavailable", "unavailable"])
        self.assertTrue(all(not r["billing_complete"] for r in result["records"][:2]))

    def test_bad_generation_hash_cannot_be_equal_bound_credit_evidence(self):
        original = self.bundle()
        for value in ("", "not-a-sha256", "A" * 64):
            bundle = deepcopy(original)
            bundle["rows"][0]["generation_sha256"] = value
            bundle["responses"][0]["generation_sha256"] = value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "repetition_generation_capture_hash_invalid"):
                self.score(bundle)
        bundle = deepcopy(original)
        bundle["rows"][0]["generation_sha256"] = None
        bundle["responses"][0]["generation_sha256"] = None
        with self.assertRaisesRegex(ValueError, "repetition_credit_receipt_projection_mismatch"):
            self.score(bundle)

    def test_missing_generation_proof_keeps_bound_first_label_and_unknown_cost(self):
        bundle = self.bundle()
        bundle["rows"][0].update(generation_sha256=None, billing_verified=False,
                                 billing_replay_verified=False, actual_cost_usd=None)
        bundle["responses"][0].update(generation_sha256=None, generation_projection={})
        result = self.score(bundle)
        self.assertEqual(result["records"][0]["prediction"]["label"], "supported")
        self.assertIsNone(result["records"][0]["actual_cost_usd"])
        self.assertFalse(result["records"][0]["billing_complete"])
        self.assertIsNone(result["arms"][0]["cost"]["total_usd"])

    def test_duplicate_generation_identity_rejected(self):
        bundle = self.bundle()
        duplicate = bundle["rows"][0]["generation_id"]
        bundle["rows"][1]["generation_id"] = duplicate
        bundle["responses"][1]["projection"]["id"] = duplicate
        bundle["responses"][1]["generation_projection"]["id"] = duplicate
        with self.assertRaises(ValueError):
            self.score(bundle)

    def test_reordered_supplied_rows_and_projections_keep_planned_score_order(self):
        bundle = self.bundle()
        expected = self.score(bundle)
        reordered = deepcopy(bundle)
        for inventory in ("requests", "rows", "responses"):
            reordered[inventory].reverse()
        result = self.score(reordered)
        self.assertEqual(result["records"], expected["records"])
        self.assertEqual(result["arms"], expected["arms"])
        self.assertNotEqual(result["input_sha256"]["normalized_bundle"], expected["input_sha256"]["normalized_bundle"])

    def test_changed_gold_bytes_and_gold_duplicate_inventory_rejected(self):
        bundle = self.bundle()
        with self.assertRaises(ValueError):
            self.score(bundle, gold_raw=self.gold_raw + b" ")
        gold = json.loads(self.gold_raw)
        gold[0]["judgments"].append(deepcopy(gold[0]["judgments"][0]))
        self.gold_raw = canonical(gold)
        self.policy["scoring"]["gold_sha256"] = digest(self.gold_raw)
        self.policy_raw = canonical(self.policy)
        self.output = self.directory / "duplicate-gold-preparation"
        bundle = self.bundle()
        with self.assertRaises(ValueError):
            self.score(bundle)


if __name__ == "__main__":
    unittest.main()


class RepetitionBoundaryTests(RepetitionFixture, unittest.TestCase):
    def test_ambiguous_first_row_cannot_be_opted_into_completed_labels(self):
        for states in (["uncertain"], ["failed"], [], ["completed", "completed"]):
            policy = deepcopy(self.policy)
            policy["scoring"]["label_response_states"] = states
            with self.subTest(states=states), self.assertRaises(ValueError):
                self.prepare(policy=policy)
        self.assertFalse(self.output.exists())

    def test_existing_manifest_collision_is_checked_before_new_artifact_writes(self):
        self.output.mkdir()
        (self.output / "manifest.json").write_bytes(b"different saved invocation")
        with self.assertRaisesRegex(ValueError, "repetition_output_already_exists"):
            self.prepare()
        self.assertEqual(list(self.output.iterdir()), [self.output / "manifest.json"])

    def test_existing_request_collision_is_checked_before_other_writes(self):
        original = next(iter(self.originals.values()))[0]
        path = self.output / "requests" / (original["request_sha256"] + ".json")
        path.parent.mkdir(parents=True)
        path.write_bytes(b"different immutable request")
        with self.assertRaisesRegex(ValueError, "repetition_output_already_exists"):
            self.prepare()
        self.assertEqual(list(self.output.rglob("*.json")), [path])

    def test_scientific_origin_and_prompt_hashes_are_explicit_and_unchanged(self):
        manifest = self.prepare()
        for op in manifest["operations"]:
            origin = op["metadata"]["scientific_repetition"]
            original, raw = self.originals[(origin["arm_id"], origin["query_id"])]
            self.assertEqual(origin["source_operation_id"], original["operation_id"])
            self.assertEqual(origin["source_request_sha256"], digest(raw))
            self.assertEqual(origin["source_prompt_sha256"]["/questions"], digest(canonical(json.loads(raw)["questions"])))
            self.assertFalse(origin["retry_or_replacement"])
        plan = manifest["metadata"]["scientific_repetition"]
        self.assertEqual(plan["minimum_reservation_sum_usd"], "0.0248")
        self.assertEqual(plan["planned_operations_per_arm_per_repetition"], 2)
        self.assertTrue(plan["current_pair_prices_fx_and_budget_gate_required"])
        self.assertFalse(plan["new_corpus_created"])


class RepetitionBootstrapTests(RepetitionFixture, unittest.TestCase):
    setUp = RepetitionScoreTests.setUp
    bundle = RepetitionScoreTests.bundle
    score = RepetitionScoreTests.score
    def test_correlated_family_bootstrap_is_data_and_reproducible(self):
        self.policy["scoring"]["bootstrap"] = {"samples": 17, "seed": 31, "family_pointer": "/family", "confidence": .9}
        self.policy_raw = canonical(self.policy)
        bundle = self.bundle()
        result = self.score(bundle)
        bootstrap = result["arms"][0]["correlated_family_bootstrap"]
        self.assertEqual(bootstrap["family_count"], 1)
        self.assertEqual(bootstrap["samples"], 17)
        self.assertEqual(bootstrap["correctness_all_planned_interval"], [1, 1])
        self.assertEqual(self.score(bundle), result)

    def test_missing_bootstrap_family_is_explicit_error(self):
        self.policy["scoring"]["bootstrap"] = {"samples": 7, "seed": 0, "family_pointer": "/absent_family", "confidence": .8}
        self.policy_raw = canonical(self.policy)
        with self.assertRaises(KeyError):
            self.score(self.bundle())
