"""Selective receipt/privacy, first-failure population and exact graph regressions."""
from copy import deepcopy
import gzip
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.structure import programme_results_v1 as tool


def fixture(root, *, malformed=False, billing=True):
    prepared, private = root / "prepared", root / "private"
    (prepared / "requests").mkdir(parents=True)
    (private / "records").mkdir(parents=True)
    body = {"model": "typesafe/jev-1.13", "provider": {"only": ["typesafe"], "allow_fallbacks": False},
            "state": {"text": "authored synthetic query"},
            "questions": {"q01": {"type": "noul", "instructions": "synthetic support"},
                          "q02": {"type": "noul", "instructions": "synthetic refutation"}}}
    raw_body = tool.manifests.wire.canonical(body)
    request_hash = tool.sha(raw_body)
    (prepared / "requests/request.json").write_bytes(raw_body)
    operations = [{"operation_id": ident, "route_id": "jev", "request_file": "requests/request.json",
                   "request_sha256": request_hash, "model_id": body["model"], "provider_id": "typesafe",
                   "units_upper_bounds": {"prompt": 100, "completion": 0, "request": 1},
                   "metadata": {"arm_id": "authored_arm", "prepared_request_id": ident,
                                "source_manifest_sha256": "a" * 64}} for ident in ("first", "second", "unattempted")]
    manifest = {"schema": tool.manifests.SCHEMA, "programme_id": "synthetic-programme", "stage_id": "synthetic-stage", "operations": operations}
    raw_manifest = tool.manifests.wire.canonical(manifest)
    (prepared / "manifest.json").write_bytes(raw_manifest)
    response = {"id": "synthetic-generation", "model": "typesafe/jev-1.13-20261001", "provider": "TypeSafe",
                "usage": {"cost": "0.000032928", "input_tokens": 42, "output_tokens": 0, "is_byok": False},
                "answers": {"q01": {"type": "noul", "noul": .8}, "q02": {"type": "noul", "noul": .1}},
                "personal_metadata": "DROP-PROVIDER-METADATA"}
    raw_response = b"invalid original response bytes" if malformed else json.dumps(response).encode()
    raw_generation = json.dumps({"data": {"id": "synthetic-generation", "model": response["model"],
        "provider_name": "TypeSafe", "api_type": "decisions", "total_cost": "0.000032928", "is_byok": False,
        "personal_metadata": "DROP-GENERATION-METADATA"}}).encode()
    token = tool.sha(b"first")
    (private / "records" / (token + ".request.bin")).write_bytes(raw_body)
    (private / "records" / (token + ".response.bin")).write_bytes(raw_response)
    (private / "records" / (token + ".generation.bin")).write_bytes(raw_generation)
    row = {"operation_id": "first", "programme_id": manifest["programme_id"], "stage_id": manifest["stage_id"],
           "manifest_sha256": tool.sha(raw_manifest), "request_sha256": request_hash, "state": "completed" if not malformed else "uncertain",
           "started_at": "2026-10-05T00:00:00Z", "finished_at": "2026-10-05T00:00:01Z",
           "actual_cost_usd": "0.000032928" if billing else None, "reservation_usd": "0.001", "billing_verified": billing,
           "is_byok": False, "response_sha256": tool.sha(raw_response), "generation_sha256": tool.sha(raw_generation),
           "http_status": 200, "latency_seconds": 1.2, "input_tokens": 42, "output_tokens": 0,
           "key_fingerprint_sha256": "DO-NOT-READ-PRIVATE-FINGERPRINT",
           "receipt_operation": {"model_id": body["model"], "provider_id": "typesafe", "model_aliases": [response["model"]],
                                 "provider_aliases": ["TypeSafe"], "route_id": "jev", "api_type": "decisions"}}
    second = {**row, "operation_id": "second", "state": "uncertain", "response_sha256": None,
              "generation_sha256": None, "actual_cost_usd": None, "billing_verified": False, "http_status": None}
    with sqlite3.connect(private / "ledger.sqlite3") as db:
        db.execute("CREATE TABLE attempts(operation_id TEXT PRIMARY KEY,payload TEXT)")
        for attempt in (row, second):
            db.execute("INSERT INTO attempts VALUES(?,?)", (attempt["operation_id"], json.dumps(attempt)))
        db.execute("CREATE TABLE binding(fingerprint TEXT)")
        db.execute("INSERT INTO binding VALUES('DO-NOT-READ-BINDING')")
    return prepared / "manifest.json", private


class ProgrammeResults(unittest.TestCase):
    def test_selective_projection_keeps_actual_identity_and_excludes_private_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, private = fixture(Path(directory))
            bundle = tool.normalize(manifest, private)
            self.assertEqual((bundle["planned_operations"], len(bundle["rows"])), (3, 2))
            row = bundle["rows"][0]
            self.assertTrue(row["billing_replay_verified"])
            self.assertTrue(row["exact_sent_request_capture_verified"])
            self.assertEqual(row["actual_cost_usd"], "0.000032928")
            self.assertNotEqual(row["observed_model"], row["requested_model"])
            serialized = json.dumps(bundle)
            for forbidden in ("FINGERPRINT", "BINDING", "DROP-PROVIDER", "DROP-GENERATION"):
                self.assertNotIn(forbidden, serialized)
            self.assertFalse(bundle["private_binding_fields_read"])

    def test_failed_and_unattempted_denominators_resources_and_producer_are_real_edges(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, private = fixture(root)
            bundle = tool.normalize(manifest, private)
            with patch.object(tool.graph, "ROOT", root):
                receipt = tool.export_results(bundle, root / "public", observed_on="2026-10-05")
            packet = json.loads(gzip.decompress((root / "public/packet.json.gz").read_bytes()))
            tool.graph.codec.validate_packet(packet)
            self.assertEqual(receipt["attempted_operations"], 2)
            self.assertEqual(sum(e["kind"] == "research_event" for e in packet["entities"]), 2)
            self.assertFalse(any(e["kind"] == "model_profile" for e in packet["entities"]))
            coverages = [c["value"] for c in packet["claims"] if c["predicate"] == "method_evaluation"
                         and c["qualifiers"]["extra"]["metric_id"] == "completion_coverage"]
            self.assertEqual(sorted((m["numerator"], m["denominator"]) for m in coverages), [(0, 3), (1, 3)])
            configured = {e["id"]: e for e in packet["entities"] if e["kind"] == "configured_research_method"}
            versions = {e["id"] for e in packet["entities"] if e["kind"] == "research_method_version"}
            event_ids = {e["id"] for e in packet["entities"] if e["kind"] == "research_event"}
            actual = [c for c in packet["claims"] if c["predicate"] == "produced_by" and c["subject"] in event_ids]
            self.assertEqual(len(actual), 2)
            self.assertTrue(all(configured[c["object"]]["parent"] in versions for c in actual))

    def test_invalid_first_bytes_preserved_and_unknown_billing_does_not_erase_answer(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, private = fixture(Path(directory), malformed=True)
            bundle = tool.normalize(manifest, private)
            self.assertIsNone(bundle["responses"][0]["projection"])
            self.assertTrue(bundle["rows"][0]["response_ledger_bound"])
            self.assertEqual(bundle["responses"][0]["response_sha256"], bundle["rows"][0]["response_sha256"])
        with tempfile.TemporaryDirectory() as directory:
            manifest, private = fixture(Path(directory), billing=False)
            bundle = tool.normalize(manifest, private)
            self.assertIsNone(bundle["rows"][0]["actual_cost_usd"])
            self.assertIsNotNone(bundle["responses"][0]["projection"]["answers"])

    def test_changed_ledger_request_or_capture_is_rejected_without_repair(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, private = fixture(Path(directory))
            token = tool.sha(b"first")
            path = private / "records" / (token + ".response.bin")
            original = path.read_bytes()
            path.write_bytes(original + b" ")
            with self.assertRaisesRegex(ValueError, "capture_hash_drift"):
                tool.normalize(manifest, private)
            self.assertEqual(path.read_bytes(), original + b" ")
        with tempfile.TemporaryDirectory() as directory:
            manifest, private = fixture(Path(directory))
            path = private / "records" / (tool.sha(b"first") + ".request.bin")
            path.write_bytes(path.read_bytes() + b" ")
            with self.assertRaisesRegex(ValueError, "exact_sent_request_capture_drift"):
                tool.normalize(manifest, private)

    def test_late_generation_projection_retains_original_404_and_excludes_full_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, private = fixture(Path(directory))
            token = tool.sha(b"first")
            original_path = private / "records" / (token + ".generation.bin")
            final_generation = original_path.read_bytes()
            first_404 = b'{"error":{"message":"pending"}}'
            original_path.write_bytes(first_404)
            late_name = "b" * 32 + ".captured-generation-read-0.bin"
            (private / late_name).write_bytes(final_generation)
            with sqlite3.connect(private / "ledger.sqlite3") as db:
                row = json.loads(db.execute("SELECT payload FROM attempts WHERE operation_id='first'").fetchone()[0])
                projection = {name: row.get(name) for name in tool.RESOLUTION_FIELDS}
                projection["generation_sha256"] = tool.sha(final_generation)
                row.update(state="uncertain", generation_sha256=tool.sha(first_404), actual_cost_usd=None, billing_verified=False)
                original_payload = json.dumps(row)
                db.execute("UPDATE attempts SET payload=? WHERE operation_id='first'", (original_payload,))
                db.execute("CREATE TABLE attempt_resolutions(operation_id TEXT PRIMARY KEY,payload TEXT)")
                proof = {"projection": projection, "late_reads": [{"file": late_name, "raw_sha256": tool.sha(final_generation), "http_status": 200}],
                         "binding": {"key_fingerprint_sha256": "DO-NOT-READ-LATE-PRIVATE-BINDING"}}
                db.execute("INSERT INTO attempt_resolutions VALUES('first',?)", (json.dumps(proof),))
            bundle = tool.normalize(manifest, private)
            row = bundle["rows"][0]
            self.assertEqual((row["original_state"], row["state"]), ("uncertain", "completed"))
            self.assertTrue(row["billing_late_resolved"])
            self.assertTrue(row["billing_replay_verified"])
            self.assertEqual(row["original_generation_sha256"], tool.sha(first_404))
            self.assertEqual(original_path.read_bytes(), first_404)
            self.assertNotIn("DO-NOT-READ-LATE", json.dumps(bundle))
            with sqlite3.connect(private / "ledger.sqlite3") as db:
                self.assertEqual(db.execute("SELECT payload FROM attempts WHERE operation_id='first'").fetchone()[0], original_payload)
                proof["late_reads"][0]["file"] = "key-before.bin"
                db.execute("UPDATE attempt_resolutions SET payload=? WHERE operation_id='first'", (json.dumps(proof),))
            with self.assertRaisesRegex(ValueError, "late_generation_binding_drift"):
                tool.normalize(manifest, private)

    def test_duplicate_generation_identity_cannot_double_bill(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, private = fixture(Path(directory))
            with sqlite3.connect(private / "ledger.sqlite3") as db:
                first = json.loads(db.execute("SELECT payload FROM attempts WHERE operation_id='first'").fetchone()[0])
                second = {**first, "operation_id": "second"}
                db.execute("UPDATE attempts SET payload=? WHERE operation_id='second'", (json.dumps(second),))
            for suffix in (".response.bin", ".generation.bin"):
                source = private / "records" / (tool.sha(b"first") + suffix)
                (private / "records" / (tool.sha(b"second") + suffix)).write_bytes(source.read_bytes())
            with self.assertRaisesRegex(ValueError, "duplicate_generation_identity"):
                tool.normalize(manifest, private)


if __name__ == "__main__":
    unittest.main()
