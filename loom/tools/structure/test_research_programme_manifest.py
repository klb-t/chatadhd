"""Offline tests for exact request artifacts and legacy manifest projection."""
from copy import deepcopy
import hashlib
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure import research_programme_manifest as m
from loom.tools.structure import openrouter_runner as wire

ROOT = Path(__file__).resolve().parents[3]
RESEARCH = ROOT / "docs/research"


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def fixture(self):
        # Whitespace and Unicode are part of the exact bytes sent to transport.
        raw = ('{\n "provider": {"only": ["fixture/provider"], "allow_fallbacks": false},\n'
               ' "model": "fixture/model", "messages": [{"role":"user","content":"żółw"}],\n'
               ' "max_tokens": 16\n}\n').encode()
        (self.directory / "request.json").write_bytes(raw)
        operation = {"operation_id": "case-1", "route_id": "chat", "request_file": "request.json",
                     "request_sha256": hashlib.sha256(raw).hexdigest(),
                     "model_id": "fixture/model", "provider_id": "fixture/provider",
                     "units_upper_bounds": {"prompt": 1024, "completion": 16, "request": 1}}
        return {"schema": m.SCHEMA, "programme_id": "programme", "stage_id": "stage-1",
                "operations": [operation]}, raw

    def test_load_retains_exact_request_bytes(self):
        manifest, raw = self.fixture()
        path = self.directory / "manifest.json"
        path.write_bytes(wire.canonical(manifest))
        self.assertEqual(m.load_manifest(path), manifest)
        operation = m.load_operations(path)[0]
        self.assertEqual(operation["request_bytes"], raw)
        self.assertNotEqual(raw, wire.canonical(operation["request_body"]))
        self.assertEqual(m.read_request(manifest["operations"][0], self.directory), raw)

    def test_manifest_snapshot_parse_is_strict_and_detached_from_path(self):
        manifest, _ = self.fixture()
        raw = wire.canonical(manifest)
        snapshot = m.read_manifest_bytes(raw)
        path = self.directory / "manifest.json"
        path.write_bytes(b'{"replaced":true}')
        self.assertEqual(m.load_manifest(snapshot, base_dir=self.directory), manifest)
        for invalid in (b'{"stage_id":"a","stage_id":"b"}', b'{"x":NaN}', b'[]', "{}"):
            with self.subTest(raw=invalid), self.assertRaises(m.ManifestError):
                m.read_manifest_bytes(invalid)

    def test_request_tampering_and_missing_artifact_rejected(self):
        manifest, raw = self.fixture()
        (self.directory / "request.json").write_bytes(raw + b" ")
        with self.assertRaisesRegex(m.ManifestError, "request_artifact_hash_mismatch"):
            m.load_manifest(manifest, base_dir=self.directory)
        (self.directory / "request.json").unlink()
        with self.assertRaisesRegex(m.ManifestError, "request_artifact_missing"):
            m.load_manifest(manifest, base_dir=self.directory)

    def test_duplicate_operation_rejected(self):
        manifest, _ = self.fixture()
        manifest["operations"].append(deepcopy(manifest["operations"][0]))
        with self.assertRaisesRegex(m.ManifestError, "duplicate_operation_id"):
            m.load_manifest(manifest, base_dir=self.directory)

    def test_stage_programme_and_base_dir_required(self):
        manifest, _ = self.fixture()
        for field in ("stage_id", "programme_id"):
            changed = deepcopy(manifest)
            del changed[field]
            with self.subTest(field=field), self.assertRaises(m.ManifestError):
                m.load_manifest(changed, base_dir=self.directory)
        with self.assertRaisesRegex(m.ManifestError, "manifest_base_dir_required"):
            m.load_manifest(manifest)

    def test_identity_mismatch_and_fallback_rejected(self):
        manifest, raw = self.fixture()
        for field in ("model_id", "provider_id"):
            changed = deepcopy(manifest)
            changed["operations"][0][field] = "different"
            with self.subTest(field=field), self.assertRaisesRegex(m.ManifestError, "request_model_provider_mismatch"):
                m.load_manifest(changed, base_dir=self.directory)
        body = wire.parse_json(raw)
        body["provider"]["allow_fallbacks"] = True
        changed_raw = wire.canonical(body)
        (self.directory / "request.json").write_bytes(changed_raw)
        manifest["operations"][0]["request_sha256"] = hashlib.sha256(changed_raw).hexdigest()
        with self.assertRaisesRegex(m.ManifestError, "one_pinned_request_provider_required"):
            m.load_manifest(manifest, base_dir=self.directory)

    def test_request_path_cannot_escape_manifest_directory(self):
        manifest, _ = self.fixture()
        for name in ("../request.json", "/tmp/request.json"):
            manifest["operations"][0]["request_file"] = name
            with self.subTest(name=name), self.assertRaises(m.ManifestError):
                m.load_manifest(manifest, base_dir=self.directory)

    def test_route_mismatch_rejected(self):
        manifest, _ = self.fixture()
        manifest["operations"][0]["route_id"] = "jev"
        with self.assertRaisesRegex(m.ManifestError, "request_route_mismatch"):
            m.load_manifest(manifest, base_dir=self.directory)

    def test_nonfinite_negative_boolean_or_empty_units_rejected(self):
        manifest, _ = self.fixture()
        for value in ("NaN", "Infinity", -1, True, {}, None):
            changed = deepcopy(manifest)
            changed["operations"][0]["units_upper_bounds"] = {} if value == {} else {"prompt": value}
            with self.subTest(value=value), self.assertRaises(m.ManifestError):
                m.load_manifest(changed, base_dir=self.directory)

    def test_legacy_hash_tampering_rejected_before_artifacts_written(self):
        source = RESEARCH / "analysis_optimization_2026-10-02/prepared/j_active/manifest.json"
        document = wire.parse_json(source.read_bytes())
        document["requests"][0]["body"]["state"]["text"] += " "
        path = self.directory / "source.json"
        path.write_bytes(wire.canonical(document))
        output = self.directory / "output"
        with self.assertRaisesRegex(m.ManifestError, "prepared_request_hash_mismatch"):
            m.adapt_prepared_manifest(path, output, programme_id="p", stage_id="s")
        self.assertFalse(output.exists())

    def test_duplicate_legacy_sources_rejected_before_artifacts_written(self):
        source = RESEARCH / "analysis_optimization_2026-10-02/prepared/g_rules_t0/manifest.json"
        output = self.directory / "output"
        with self.assertRaisesRegex(m.ManifestError, "duplicate_operation_id"):
            m.adapt_prepared_manifests([source, source], output, programme_id="p", stage_id="s")
        self.assertFalse(output.exists())

    def test_analysis_432_bodies_and_hashes_match_existing_transports(self):
        prepared = RESEARCH / "analysis_optimization_2026-10-02/prepared"
        output = self.directory / "output"
        projected = m.adapt_analysis_optimization(prepared, output, programme_id="analysis", stage_id="paired")
        self.assertEqual(len(projected["operations"]), 432)
        self.assertEqual(sum(row["route_id"] == "chat" for row in projected["operations"]), 192)
        self.assertEqual(sum(row["route_id"] == "jev" for row in projected["operations"]), 240)
        originals = {}
        for path in prepared.glob("*/manifest.json"):
            document = wire.parse_json(path.read_bytes())
            for request in document["requests"]:
                originals[(path.parent.name, request["id"])] = request
        for row in m.load_operations(output / "manifest.json"):
            original = originals[(row["metadata"]["arm_id"], row["metadata"]["prepared_request_id"])]
            self.assertEqual(row["request_bytes"], wire.canonical(original["body"]))
            self.assertEqual(row["request_sha256"], wire.digest(original["body"]))
            self.assertEqual(row["minimum_reservation_usd"], original["reservation_usd"])
        self.assertEqual(m.load_manifest(output / "manifest.json"), projected)

    def test_explicit_optional_components_and_arm_operation_overrides(self):
        source = RESEARCH / "analysis_optimization_2026-10-02/prepared/g_rules_t0/manifest.json"
        original = wire.parse_json(source.read_bytes())
        first_id = original["experiment_id"] + "." + original["requests"][0]["id"]
        projected = m.adapt_prepared_manifest(source, self.directory / "output", programme_id="p", stage_id="s",
            units_upper_bounds={"input_cache_read": 0},
            units_by_arm={"g_rules_t0": {"web_search": 0, "completion": 200}},
            units_by_operation={first_id: {"completion": 300}})
        units = projected["operations"][0]["units_upper_bounds"]
        self.assertEqual(units["input_cache_read"], 0)
        self.assertEqual(units["web_search"], 0)
        self.assertEqual(units["completion"], 300)
        self.assertEqual(projected["operations"][1]["units_upper_bounds"]["completion"], 200)

    def test_allowance_policy_is_caller_data_and_bound_to_manifest(self):
        source = RESEARCH / "analysis_optimization_2026-10-02/prepared/g_rules_t0/manifest.json"
        policy = {"chat": {"prompt_fixed_allowance": 17, "prompt_per_message_allowance": 3, "request": 2},
                  "jev": {"prompt_fixed_allowance": 19, "completion": 5, "request": 2}}
        projected = m.adapt_prepared_manifest(source, self.directory / "output", programme_id="p", stage_id="s",
                                              units_policy=policy)
        raw = m.read_request(projected["operations"][0], self.directory / "output")
        body = wire.parse_json(raw)
        self.assertEqual(projected["operations"][0]["units_upper_bounds"]["prompt"], len(raw) + 17 + 3 * len(body["messages"]))
        self.assertEqual(projected["operations"][0]["units_upper_bounds"]["request"], 2)
        self.assertEqual(projected["metadata"]["units_policy_sha256"], wire.digest(policy))
        policy["chat"]["request"] = 9
        self.assertEqual(projected["metadata"]["units_policy"]["chat"]["request"], 2)
        changed = deepcopy(projected)
        changed["metadata"]["units_policy"]["chat"]["request"] = 8
        with self.assertRaisesRegex(m.ManifestError, "units_policy_hash_mismatch"):
            m.load_manifest(changed, base_dir=self.directory / "output")

    def test_invalid_allowance_policy_rejected_before_writes(self):
        source = RESEARCH / "analysis_optimization_2026-10-02/prepared/g_rules_t0/manifest.json"
        original = wire.parse_json(m.UNITS_POLICY_PATH.read_bytes())
        for value in (-1, True, "NaN", None):
            policy = deepcopy(original)
            policy["chat"]["prompt_fixed_allowance"] = value
            output = self.directory / str(value)
            with self.subTest(value=value), self.assertRaises(m.ManifestError):
                m.adapt_prepared_manifest(source, output, programme_id="p", stage_id="s", units_policy=policy)
            self.assertFalse(output.exists())

    def test_unknown_override_selector_rejected_before_writes(self):
        source = RESEARCH / "analysis_optimization_2026-10-02/prepared/g_rules_t0/manifest.json"
        for key in ("units_by_arm", "units_by_operation"):
            output = self.directory / key
            with self.subTest(key=key), self.assertRaisesRegex(m.ManifestError, "units_override_unknown"):
                m.adapt_prepared_manifest(source, output, programme_id="p", stage_id="s",
                                          **{key: {"typo": {"prompt": 10000}}})
            self.assertFalse(output.exists())

    def test_other_stage_preparations_preserve_wire_bytes(self):
        stages = [
            ("model_research_2026-10-04/stage2/prepared/prepared.json", 60),
            ("model_research_2026-10-04/extraction/prepared/requests_preview.json", 72),
            ("model_research_2026-10-04/frontier/historical-final/manifest.json", 36),
            ("model_research_2026-10-04/followup-frontier-reply/final-verified-lineage/stage3/manifest.json", 12),
            ("model_research_2026-10-04/followup-frontier-reply/final-verified-lineage/stage4/manifest.json", 12),
        ]
        for index, (name, count) in enumerate(stages):
            with self.subTest(source=name):
                source = RESEARCH / name
                output = self.directory / str(index)
                projected = m.adapt_prepared_manifest(source, output, programme_id="p", stage_id="s")
                self.assertEqual(len(projected["operations"]), count)
                document = wire.parse_json(source.read_bytes())
                rows = document if isinstance(document, list) else document["requests"]
                expected = [wire.canonical(row.get("body", row.get("request", row.get("historical_openrouter_plan", {})).get("body"))) for row in rows]
                self.assertEqual([row["request_bytes"] for row in m.load_operations(output / "manifest.json")], expected)

    def test_abstract_frontier_preparation_requires_exact_body(self):
        source = RESEARCH / "model_research_2026-10-04/frontier/final/manifest.json"
        with self.assertRaisesRegex(m.ManifestError, "exact_prepared_body_required"):
            m.adapt_prepared_manifest(source, self.directory / "output", programme_id="p", stage_id="s")

    def test_existing_adapted_manifest_is_immutable_and_identical_projection_reusable(self):
        source = RESEARCH / "analysis_optimization_2026-10-02/prepared/g_rules_t0/manifest.json"
        output = self.directory / "output"
        original = m.adapt_prepared_manifest(source, output, programme_id="p", stage_id="s")
        self.assertEqual(m.adapt_prepared_manifest(source, output, programme_id="p", stage_id="s"), original)
        with self.assertRaisesRegex(m.ManifestError, "adapted_artifact_already_exists"):
            m.adapt_prepared_manifest(source, output, programme_id="p", stage_id="different")
        self.assertEqual(m.load_manifest(output / "manifest.json"), original)


if __name__ == "__main__":
    unittest.main()
