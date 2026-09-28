"""Offline integration checks; these are not model quality measurements."""
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


if __name__ == "__main__":
    unittest.main()
