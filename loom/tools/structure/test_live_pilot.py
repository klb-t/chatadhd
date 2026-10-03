"""Offline integration checks; these are not model quality measurements."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import warnings
import zipfile

try:
    from . import live_pilot as pilot
    from .openrouter_runner import RunnerError
except ImportError:
    import live_pilot as pilot
    from openrouter_runner import RunnerError


def endpoint(model):
    return {"data": {"id": model, "endpoints": [{"tag": "test", "status": 0,
            "max_completion_tokens": 16384,
            "supported_parameters": ["max_tokens", "temperature", "response_format"],
            "pricing": {"prompt": "0.0000001", "completion": "0.0000003"}}]}}


def request():
    return {"schema": "loom.live_pilot_request/1", "enabled": False,
            "experiment_id": "offline-integration", "split": "dev", "methods": ["native_v1"],
            "models": [{"id": "test/model", "provider": "test"}],
            "budget_usd": 2, "max_tokens": 8192, "max_requests": 16}


class LivePilotTests(unittest.TestCase):
    def test_prepare_never_reads_gold_and_hashes_native_prompt(self):
        original = Path.read_bytes
        def guarded(path):
            if path.name.startswith("gold"):
                raise AssertionError("gold was read during prompt preparation")
            return original(path)
        with tempfile.TemporaryDirectory() as tmp, patch.object(Path, "read_bytes", guarded):
            out = Path(tmp) / "prepared"
            plan = pilot.prepare(request(), out, endpoint_loader=endpoint)
            manifest = pilot.read_json(out / "manifest.json")
            self.assertEqual(plan["request_count"], 16)
            self.assertEqual(manifest["metadata"]["live_calls_made_by_prepare"], 0)
            self.assertEqual(manifest["metadata"]["native_prompt_sha256"], hashlib.sha256(pilot.native_prompt().encode()).hexdigest())
            self.assertEqual(manifest["requests"][0]["body"]["messages"][0]["content"], pilot.native_prompt())
            self.assertLess(float(plan["total_reservation_usd"]), 2)

    def test_utf8_anchor_aid_is_only_exact_source_coordinates(self):
        packet = {"observations": [{"id": "ob", "text": "Żółć: A→B."}]}
        for span in pilot.token_spans(packet):
            raw = packet["observations"][0]["text"].encode()
            self.assertEqual(raw[span["byte_start"]:span["byte_start"] + span["byte_len"]].decode(), span["quote"])

    def test_model_and_provider_are_explicit_and_capacity_checked(self):
        bad = request(); bad["max_requests"] = 15
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                pilot.prepare(bad, Path(tmp)/"out", endpoint_loader=endpoint)
            bad = request(); bad["models"][0]["provider"] = "missing"
            with self.assertRaises(ValueError):
                pilot.prepare(bad, Path(tmp)/"out", endpoint_loader=endpoint)
            bad = request(); bad["budget_usd"] = 3
            with self.assertRaises(ValueError):
                pilot.prepare(bad, Path(tmp)/"out", endpoint_loader=endpoint)

    def test_missing_results_stay_in_denominator_report_immutable(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)/"prepared"
            plan = pilot.prepare(request(), out, endpoint_loader=endpoint)
            run = out/"run"; run.mkdir()
            pilot.write_new(run/"ledger.json", {"schema": "loom.openrouter_ledger/1", "manifest_hash": plan["manifest_hash"], "attempts": []})
            result = pilot.score(out/"manifest.json", run, out/"score.json")
            self.assertEqual(len(result["rows"]), 16)
            self.assertTrue(all(r["status"] == "not_attempted" for r in result["rows"]))
            self.assertEqual(sum(g["planned"] for g in result["summary"]), 16)
            self.assertTrue(all(g["semantic_exact_over_planned"] == 0 for g in result["summary"]))
            with self.assertRaises(FileExistsError):
                pilot.score(out/"manifest.json", run, out/"score.json")

    def test_ledger_and_response_tampering_fail_before_scoring(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)/"prepared"
            plan = pilot.prepare(request(), out, endpoint_loader=endpoint)
            run = out/"run"; run.mkdir()
            planned = plan["requests"][0]
            row = {k: planned[k] for k in ("id", "request_hash", "reservation_usd")}
            row.update(state="completed", response_file=planned["id"]+".response.bin", response_sha256="wrong")
            (run/row["response_file"]).write_bytes(b'{}')
            pilot.write_new(run/"ledger.json", {"schema": "loom.openrouter_ledger/1", "manifest_hash": plan["manifest_hash"], "attempts": [row]})
            with self.assertRaises(RunnerError):
                pilot.score(out/"manifest.json", run, out/"score.json")
            self.assertFalse((out/"score.json").exists())

    def test_duplicate_json_keys_are_not_silently_repaired(self):
        with self.assertRaises(ValueError):
            pilot.parse(b'{"bundles":[],"bundles":[{}]}')


class ContinuationTests(unittest.TestCase):
    """Replay captured run evidence offline; no responses are generated or retried."""
    artifact = "docs/research/inputs/openrouter-native-dev-2026-09-28.zip"
    artifact_hash = "da619121ff8d4ed2fd28ea9a1753cb1c9f4d1da414447647ad1ae3acb2afed48"

    def setUp(self):
        with zipfile.ZipFile(pilot.REPO / self.artifact) as archive:
            self.files = {name: archive.read(name) for name in archive.namelist()}
        self.source = pilot.parse(self.files["manifest.json"])
        self.ledger = pilot.parse(self.files["run/ledger.json"])
        self.req = deepcopy(self.source["metadata"]["pilot_request"])
        self.req["experiment_id"] = "offline-native-remainder"
        self.req["continuation"] = {"artifact": self.artifact, "sha256": self.artifact_hash}
        self.endpoints = {data["data"]["id"]: data for data in pilot.parse(self.files["endpoint_snapshots.json"])}

    def prepare(self, request_value=None, endpoint_loader=None):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)/"prepared"
            plan = pilot.prepare(request_value or self.req, output,
                                 endpoint_loader=endpoint_loader or self.endpoints.__getitem__)
            return plan, pilot.read_json(output/"manifest.json")

    def test_actual_run_preserves_all_32_bodies_and_excludes_all_six_attempts(self):
        original = Path.read_bytes
        def guarded(path):
            if path.name.startswith("gold"):
                raise AssertionError("gold read by continuation preparation")
            return original(path)
        with patch.object(Path, "read_bytes", guarded):
            plan, manifest = self.prepare()
        self.assertEqual(plan["request_count"], 26)
        self.assertEqual([r["id"] for r in manifest["requests"]], [r["id"] for r in self.source["requests"][6:]])
        self.assertEqual([r["body"] for r in manifest["requests"]], [r["body"] for r in self.source["requests"][6:]])
        continuation = manifest["metadata"]["continuation"]
        self.assertEqual([r["state"] for r in continuation["excluded_attempts"]], ["completed"]*5 + ["uncertain"])
        self.assertEqual(continuation["source_artifact_sha256"], self.artifact_hash)
        self.assertEqual(continuation["source_experiment_id"], "live-structure-dev-v2")
        self.assertEqual(continuation["original_request_count"], 32)
        self.assertFalse(continuation["automatic_retry_permitted"])

    def test_request_drift_and_capacity_refused(self):
        for mutation in (lambda r: r.update(max_tokens=4096),
                         lambda r: r.update(max_requests=26),
                         lambda r: r.update(methods=["native_anchors_v1"]),
                         lambda r: r.update(experiment_id=self.source["experiment_id"]),
                         lambda r: r["continuation"].update(sha256="0"*64)):
            value = deepcopy(self.req); mutation(value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.prepare(value)
        endpoints = deepcopy(self.endpoints)
        for data in endpoints.values():
            for endpoint_value in data["data"]["endpoints"]:
                endpoint_value["pricing"]["prompt"] = "0.0000007"
        with self.assertRaisesRegex(ValueError, "body changed"):
            self.prepare(endpoint_loader=endpoints.__getitem__)

    def test_artifact_path_must_be_exact_repo_input_path(self):
        for name in ("/tmp/source.zip", "docs/research/inputs/../source.zip",
                     "docs/research/inputs//source.zip", "docs/research/inputs/x\\y.zip"):
            value = deepcopy(self.req); value["continuation"]["artifact"] = name
            with self.subTest(name=name), self.assertRaises(ValueError):
                pilot.validate_request(value)

    def test_tampered_ledger_and_responses_refused_even_with_new_archive_hash(self):
        variants = {}
        bad = deepcopy(self.ledger); bad["attempts"][0], bad["attempts"][1] = bad["attempts"][1], bad["attempts"][0]
        variants["nonprefix"] = {"run/ledger.json": pilot.canonical(bad)}
        bad = deepcopy(self.ledger); bad["manifest_hash"] = "0"*64
        variants["manifest_hash"] = {"run/ledger.json": pilot.canonical(bad)}
        variants["response_hash"] = {"run/"+self.ledger["attempts"][0]["response_file"]: b"tampered"}
        variants["unaccounted_response"] = {"run/unaccounted.response.bin": b"{}"}
        variants["dangerous_path"] = {"../outside.json": b"{}"}
        variants["expanded_limit"] = {"oversized.bin": b"a"*(pilot.MAX_FILE+1)}
        with tempfile.TemporaryDirectory(dir=pilot.REPO/"docs/research/inputs") as tmp:
            archive_path = Path(tmp)/"changed.zip"
            for variant, changes in variants.items():
                with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                    for name, raw in (self.files | changes).items():
                        archive.writestr(name, raw)
                req = deepcopy(self.req)
                req["continuation"] = {"artifact": archive_path.relative_to(pilot.REPO).as_posix(),
                                       "sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest()}
                with self.subTest(variant=variant), self.assertRaises((ValueError, RunnerError)):
                    self.prepare(req)

    def test_duplicate_and_symlink_zip_entries_refused(self):
        with tempfile.TemporaryDirectory(dir=pilot.REPO/"docs/research/inputs") as tmp:
            archive_path = Path(tmp)/"changed.zip"
            for variant in ("duplicate", "symlink"):
                with warnings.catch_warnings(), zipfile.ZipFile(archive_path, "w") as archive:
                    warnings.simplefilter("ignore", UserWarning)
                    for name, raw in self.files.items():
                        archive.writestr(name, raw)
                    if variant == "duplicate":
                        archive.writestr("manifest.json", self.files["manifest.json"])
                    else:
                        info = zipfile.ZipInfo("link"); info.create_system = 3
                        info.external_attr = 0o120777 << 16
                        archive.writestr(info, "manifest.json")
                req = deepcopy(self.req)
                req["continuation"] = {"artifact": archive_path.relative_to(pilot.REPO).as_posix(),
                                       "sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest()}
                with self.subTest(variant=variant), self.assertRaisesRegex(ValueError, "unsafe"):
                    self.prepare(req)


if __name__ == "__main__":
    unittest.main()
