"""Offline controls for an identity-only binding; no transport or key fixture."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.structure import jev_dispatch_binding_v1 as binding
from loom.tools.structure import openrouter_runner as wire
from loom.tools.structure import research_programme_manifest as manifests


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.scoring = self.directory / "scoring"
        self.prepared = self.directory / "prepared"
        self.output = self.directory / "dispatch"
        self.scoring.mkdir(); self.prepared.mkdir()
        self.request_raw = ('{\n "model":"test/model", "provider":{"only":["test/provider"],'
                            '"allow_fallbacks":false}, "questions":{"q01":"Żółw?"},'
                            '"state":{"text":"not a credential"}\n}\n').encode()
        (self.prepared / "request.json").write_bytes(self.request_raw)
        operation = {"operation_id": "original.case", "route_id": "jev", "request_file": "request.json",
                     "request_sha256": binding.sha(self.request_raw), "model_id": "test/model",
                     "provider_id": "test/provider", "units_upper_bounds": {"request": 1}}
        self.manifest = {"schema": manifests.SCHEMA, "programme_id": "original-programme", "stage_id": "frozen-stage",
                         "operations": [operation], "metadata": {"unchanged": True}}
        self.manifest_path = self.prepared / "manifest.json"
        self.manifest_raw = wire.canonical(self.manifest) + b"\n"
        self.manifest_path.write_bytes(self.manifest_raw)
        self.gold_raw = b'{"gold":"unchanged"}\n'
        (self.directory / "gold.json").write_bytes(self.gold_raw)
        (self.directory / "source.json").write_bytes(b"{}\n")
        (self.directory / "helper.py").write_bytes(b"def helper(): return 0\n")
        helper_raw = b"# replay helper preserved exactly\n"
        (self.scoring / "score_first_only.py").write_bytes(helper_raw)
        gold_spec = {"path": "../gold.json", "raw_sha256": binding.sha(self.gold_raw)}
        source_spec = {"path": "../source.json", "raw_sha256": binding.sha(b"{}\n")}
        helper_spec = {"path": "../helper.py", "raw_sha256": binding.sha(b"def helper(): return 0\n")}
        self.config = {"binding_status": "bound", "replay_adapter_raw_sha256": binding.sha(helper_raw),
                       "paid_manifest": {"path": "../prepared/manifest.json", "raw_sha256": binding.sha(self.manifest_raw),
                                         "programme_id": self.manifest["programme_id"], "stage_id": "frozen-stage"},
                       "gold": gold_spec, "source_inputs": source_spec,
                       "arms": [{"prepared_specs": source_spec}],
                       "instrument": {"panel": helper_spec, "key_validator": helper_spec,
                                      "threshold": 0.5, "strict": True},
                       "authored_inventory": {"operations": 1}, "operation_mapping": [{"operation_id": "original.case"}],
                       "response_policy": {"missing_remains_in_denominator": True}}
        self.config_path = self.scoring / "configuration.json"
        self.config_raw = json.dumps(self.config, ensure_ascii=False, indent=2).encode() + b"\n"
        self.config_path.write_bytes(self.config_raw)
        self.policy_path = self.directory / "operator-policy.json"
        self.policy_path.write_bytes(b'{"programme_id":"existing-programme","private_path":"do not publish"}\n')
        self.addCleanup(patch.stopall)
        patch.object(binding, "ORIGINAL_MANIFEST_SHA256", binding.sha(self.manifest_raw)).start()
        patch.object(binding, "ORIGINAL_CONFIG_SHA256", binding.sha(self.config_raw)).start()
        patch.object(binding, "ORIGINAL_GOLD_SHA256", binding.sha(self.gold_raw)).start()

    def run_binding(self):
        return binding.bind(self.manifest_path, self.config_path, self.policy_path, self.output)

    def test_only_identity_and_paid_manifest_binding_change(self):
        receipt = self.run_binding()
        changed = wire.parse_json((self.output / "manifest.json").read_bytes())
        config = wire.parse_json((self.output / "configuration.json").read_bytes())
        binding._validate_delta(self.manifest, changed, self.config, config)
        self.assertEqual(changed["programme_id"], "existing-programme")
        self.assertEqual(changed["operations"], self.manifest["operations"])
        self.assertEqual((self.output / "request.json").read_bytes(), self.request_raw)
        self.assertNotEqual(self.request_raw, wire.canonical(wire.parse_json(self.request_raw)))
        self.assertEqual(self.manifest_path.read_bytes(), self.manifest_raw)
        self.assertEqual(self.config_path.read_bytes(), self.config_raw)
        self.assertEqual((self.output / "score_first_only.py").read_bytes(), (self.scoring / "score_first_only.py").read_bytes())
        self.assertTrue(receipt["all_operations_request_bytes_and_ids_equal"])
        self.assertFalse(receipt["dispatch"])
        self.assertFalse(receipt["private_key_or_ledger_read"])
        self.assertNotIn(b"do not publish", (self.output / "BINDING_RECEIPT.json").read_bytes())

    def test_bound_config_relative_paths_resolve_to_original_dependencies(self):
        self.run_binding()
        config = wire.parse_json((self.output / "configuration.json").read_bytes())
        for spec in binding._dependencies(config):
            self.assertEqual((self.output / spec["path"]).resolve(), (self.scoring / spec["path"]).resolve())
            self.assertEqual(binding.sha((self.output / spec["path"]).read_bytes()), spec["raw_sha256"])
        self.assertEqual((self.output / config["paid_manifest"]["path"]).resolve(), self.output / "manifest.json")

    def test_frozen_manifest_tampering_is_rejected_before_output(self):
        self.manifest_path.write_bytes(self.manifest_raw + b" ")
        with self.assertRaisesRegex(binding.BindingError, "original_manifest_hash_changed"):
            self.run_binding()
        self.assertFalse(self.output.exists())

    def test_frozen_gold_tampering_is_rejected_before_output(self):
        (self.directory / "gold.json").write_bytes(b'{"gold":"tampered"}\n')
        with self.assertRaisesRegex(binding.BindingError, "frozen_dependency_hash_changed"):
            self.run_binding()
        self.assertFalse(self.output.exists())

    def test_threshold_or_mapping_tampering_is_rejected_by_source_pin(self):
        for field in ("instrument", "operation_mapping"):
            changed = deepcopy(self.config)
            changed[field] = {"tampered": True}
            self.config_path.write_bytes(wire.canonical(changed))
            with self.subTest(field=field), self.assertRaisesRegex(binding.BindingError, "original_config_hash_changed"):
                self.run_binding()
        self.assertFalse(self.output.exists())

    def test_forbidden_body_id_parameter_and_config_deltas_rejected(self):
        for field in ("operation_id", "request_sha256", "units_upper_bounds"):
            changed = deepcopy(self.manifest)
            changed["operations"][0][field] = "tampered"
            with self.subTest(field=field), self.assertRaisesRegex(binding.BindingError, "forbidden_manifest_drift"):
                binding._validate_delta(self.manifest, changed, self.config, self.config)
        for field in ("gold", "instrument", "operation_mapping", "response_policy"):
            changed = deepcopy(self.config)
            changed[field] = {"tampered": True}
            with self.subTest(field=field), self.assertRaisesRegex(binding.BindingError, "forbidden_config_drift"):
                binding._validate_delta(self.manifest, self.manifest, self.config, changed)

    def test_request_bytes_tampering_rejected(self):
        (self.prepared / "request.json").write_bytes(self.request_raw + b" ")
        with self.assertRaisesRegex(manifests.ManifestError, "request_artifact_hash_mismatch"):
            self.run_binding()
        self.assertFalse(self.output.exists())

    def test_nonempty_output_and_wrong_relative_base_rejected(self):
        self.output.mkdir(); (self.output / "existing").write_bytes(b"keep")
        with self.assertRaisesRegex(binding.BindingError, "output_directory_not_empty"):
            self.run_binding()
        self.assertEqual((self.output / "existing").read_bytes(), b"keep")
        with self.assertRaisesRegex(binding.BindingError, "output_must_be_sibling"):
            binding.bind(self.manifest_path, self.config_path, self.policy_path, self.directory / "nested" / "wrong")

    def test_missing_duplicate_or_invalid_explicit_operator_policy_rejected(self):
        for raw in (b'{}', b'{"programme_id":"a","programme_id":"b"}', b'{"programme_id":"invalid identity"}'):
            self.policy_path.write_bytes(raw)
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                self.run_binding()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
