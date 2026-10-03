"""Independent preparation checks using fresh tiny inputs; no gold is opened."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import live_pilot as pilot
    from .openrouter_runner import RunnerError
except ImportError:
    import live_pilot as pilot
    from openrouter_runner import RunnerError


def request():
    return {"schema": "loom.live_pilot_request/1", "enabled": False,
            "experiment_id": "independent-prepare", "split": "dev",
            "methods": ["native_v1"],
            "models": [{"id": "independent/model", "provider": "independent"}],
            "budget_usd": 2, "max_tokens": 1024, "max_requests": 8}


def cases():
    return [{"case_id": "source-only-case", "language": "pl",
             "private_expected_answer": "DO_NOT_INCLUDE_THIS_IN_MODEL_INPUT",
             "source_packet": {"schema": "loom.source_packet/1", "snapshot_id": "sample",
                               "observations": [{"id": "ob-one", "text": "Żółw odpoczywa."}],
                               "entities": [], "claims": []}}]


def endpoint(model):
    return {"data": {"id": model, "endpoints": [{"tag": "independent", "status": 0,
            "max_completion_tokens": 8192,
            "supported_parameters": ["response_format", "max_tokens", "temperature"],
            "pricing": {"prompt": "0.0000001", "completion": "0.0000002"}}]}}


class IndependentPreparationTests(unittest.TestCase):
    def setUp(self):
        self.inputs = patch.object(pilot, "load_inputs", return_value=(cases(), {"fixture": "independent"}))
        self.prompt = patch.object(pilot, "native_prompt", return_value="Extract a source-grounded graph.")
        self.inputs.start()
        self.prompt.start()
        self.addCleanup(self.inputs.stop)
        self.addCleanup(self.prompt.stop)

    def test_case_annotation_is_excluded_from_both_method_prompts(self):
        for method in ("native_v1", "native_anchors_v1"):
            messages = pilot.messages_for(cases()[0], method)
            self.assertNotIn("DO_NOT_INCLUDE_THIS_IN_MODEL_INPUT", str(messages))
            self.assertIn("Żółw", messages[1]["content"])
            self.assertEqual(messages[0]["role"], "system")

    def test_capacity_rejected_before_endpoint_requests(self):
        value = request()
        value["methods"] = ["native_v1", "native_anchors_v1"]
        value["max_requests"] = 1
        with tempfile.TemporaryDirectory() as td, patch.object(pilot, "fetch_endpoints") as network:
            with self.assertRaises(ValueError):
                pilot.prepare(value, Path(td) / "out", endpoint_loader=network)
            network.assert_not_called()
            self.assertFalse((Path(td) / "out").exists())

    def test_provider_ambiguity_and_endpoint_identity_fail_closed(self):
        for mutate in (lambda data: data["data"].update(id="another/model"),
                       lambda data: data["data"]["endpoints"].append(deepcopy(data["data"]["endpoints"][0]))):
            data = endpoint("independent/model")
            mutate(data)
            with tempfile.TemporaryDirectory() as td, self.assertRaises(ValueError):
                pilot.prepare(request(), Path(td) / "out", endpoint_loader=lambda _: data)

    def test_non_token_endpoint_charge_prevents_a_plan(self):
        data = endpoint("independent/model")
        data["data"]["endpoints"][0]["pricing"]["request"] = "0.25"
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaises(RunnerError):
                pilot.prepare(request(), Path(td) / "out", endpoint_loader=lambda _: data)
            self.assertFalse((Path(td) / "out").exists())

    def test_explicit_provider_and_no_fallback_survive_preparation(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "out"
            pilot.prepare(request(), output, endpoint_loader=endpoint)
            manifest = pilot.read_json(output / "manifest.json")
            self.assertEqual(len(manifest["requests"]), 1)
            body = manifest["requests"][0]["body"]
            self.assertEqual(body["provider"]["only"], ["independent"])
            self.assertIs(body["provider"]["allow_fallbacks"], False)
            self.assertIs(body["provider"]["require_parameters"], True)
            self.assertEqual(body["provider"]["max_price"], {"prompt": 0.1, "completion": 0.2})

    def test_existing_preparation_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "out"
            pilot.prepare(request(), output, endpoint_loader=endpoint)
            before = (output / "manifest.json").read_bytes()
            changed = request()
            changed["max_tokens"] = 2048
            with self.assertRaises(FileExistsError):
                pilot.prepare(changed, output, endpoint_loader=endpoint)
            self.assertEqual((output / "manifest.json").read_bytes(), before)

    def test_response_identity_records_only_explicit_endpoint_aliases(self):
        data = endpoint("independent/model")
        data["data"]["endpoints"][0].update(
            model_id="independent/canonical", name="Independent | independent/reported",
            provider_name="Independent Provider")
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "out"
            pilot.prepare(request(), output, endpoint_loader=lambda _: data)
            identity = pilot.read_json(output / "manifest.json")["requests"][0]["metadata"]["response_identity"]
            self.assertEqual(set(identity["model_ids"]), {
                "independent/model", "independent/canonical", "independent/reported"})
            self.assertEqual(set(identity["providers"]), {"independent", "Independent Provider"})

    def test_foreign_or_missing_response_identity_receives_no_semantic_credit(self):
        # This temporary label stub is authored here, not repository gold.
        gold = json.dumps([{"case_id": "source-only-case"}]).encode()
        fixture_manifest = {"files_sha256": {"gold.dev.json": hashlib.sha256(gold).hexdigest()}}
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "gold.dev.json").write_bytes(gold)
            with patch.object(pilot, "FIXTURES", root), patch.object(
                    pilot, "load_inputs", return_value=(cases(), fixture_manifest)):
                output = root / "out"
                plan = pilot.prepare(request(), output, endpoint_loader=endpoint)
                manifest = pilot.read_json(output / "manifest.json")
                planned = plan["requests"][0]
                packet_hash = manifest["requests"][0]["metadata"]["packet_hash"]
                for index, (model, provider, status) in enumerate([
                    ("wrong/model", "independent", "identity_mismatch"),
                    ("independent/model", "wrong-provider", "identity_mismatch"),
                    (None, "independent", "identity_mismatch"),
                    ("independent/model", "independent", "unlocated_abstention"),
                ]):
                    run = output / ("run-" + str(index))
                    run.mkdir()
                    response = {"model": model, "provider": provider,
                                "choices": [{"finish_reason": "stop", "message": {
                                    "content": json.dumps({"schema_version": 2,
                                                           "packet_hash": packet_hash,
                                                           "bundles": []})}}]}
                    raw = json.dumps(response).encode()
                    name = planned["id"] + ".response.bin"
                    (run / name).write_bytes(raw)
                    row = {k: planned[k] for k in ("id", "request_hash", "reservation_usd")}
                    row.update(state="completed", response_file=name,
                               response_sha256=hashlib.sha256(raw).hexdigest())
                    pilot.write_new(run / "ledger.json", {"schema": "loom.openrouter_ledger/1",
                                    "manifest_hash": plan["manifest_hash"], "attempts": [row]})
                    report = pilot.score(output / "manifest.json", run, run / "score.json")
                    self.assertEqual(report["rows"][0]["status"], status)
                    self.assertEqual(report["rows"][0]["observed_model"], model)
                    self.assertFalse(report["rows"][0]["semantic_exact"])
                    self.assertEqual(report["summary"][0]["semantic_exact_over_planned"], 0)


if __name__ == "__main__":
    unittest.main()
