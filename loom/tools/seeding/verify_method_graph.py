#!/usr/bin/env python3
"""Reproduce the inspected synthetic V4 capture proof without paid calls.

Use --library for the separate native consumer proof. No automatic build and
no silent skip. --replay-frozen-producer executes only the explicitly supplied
trusted synthetic run's archived program; ordinary export never executes it.
Large derived packets/receipts are reproducible from existing frozen bytes and
are not duplicated into the repository evidence.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import gzip
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

# Also works when reviewing this proposal outside the repository.
spec = importlib.util.spec_from_file_location("_seeding_bridge_verify", Path(__file__).with_name("method_graph.py"))
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class ExportConfirmationRequired(ValueError):
    """A recorded preflight decision, before export or native store mutation."""

    def __init__(self, preflight):
        self.preflight = preflight
        super().__init__("expected usage increase requires confirmation; review estimate and choose export settings")


def projection_clock(fixture_clock=None):
    """Actual UTC by default; an explicit fixture clock never claims observation."""
    if fixture_clock is None:
        projected_at = datetime.now(timezone.utc).isoformat()
        return {"mode": "observed_utc", "simulation": False, "projected_at": projected_at,
                "semantics": "current UTC time observed by verifier after reading source bytes"}
    timestamp = datetime.fromisoformat(fixture_clock)
    if timestamp.tzinfo is None or timestamp.utcoffset() is None:
        raise ValueError("fixture_clock_requires_timezone")
    return {"mode": "fixture_simulation", "simulation": True,
            "projected_at": timestamp.astimezone(timezone.utc).isoformat(),
            "semantics": "explicit simulated projection time; not actual source observation time"}


def preflight_export(captured, projection, *, confirm_large_export=False, fixture_clock=None):
    """Use the exporter's real-byte estimate and configured preset before work.

    The shared estimator rejects excessive support serialization before building
    a packet; ordinary presets perform its exact dry run. No native consumer or
    frozen producer is invoked by this helper.
    """
    clock = projection_clock(fixture_clock)
    estimate = bridge.estimate(captured, projection, projected_at=clock["projected_at"])
    required = estimate["requires_confirmation"]
    decision = {"requires_confirmation": required, "caller_confirmed": bool(confirm_large_export),
                "confirmation_ratio": projection["export"]["confirmation_ratio"],
                "decision": ("awaiting_confirmation" if required and not confirm_large_export else
                             "confirmed_large_export" if required else "permitted_without_confirmation"),
                "estimate_kind": estimate["estimate_kind"],
                "confirmation_source": "explicit_cli_flag" if confirm_large_export else None}
    preflight = {"estimate": estimate, "usage_decision": decision,
                 "verification_clock": clock, "projected_at": clock["projected_at"]}
    if required and not confirm_large_export:
        raise ExportConfirmationRequired(preflight)
    return preflight


def build_verified_artifact(captured, projection, preflight):
    """Build only after a permitted decision; preserve simulation disclosure."""
    if preflight["usage_decision"]["decision"] == "awaiting_confirmation":
        raise ExportConfirmationRequired(preflight)
    artifact = bridge.build_artifact(captured, projection, projected_at=preflight["projected_at"])
    clock = preflight["verification_clock"]
    artifact["producer_evidence"]["verification_clock"] = clock
    if clock["simulation"]:
        packet = artifact["packet"]
        packet["task"]["verification_clock"] = clock
        for row in packet["sources"]:
            attrs = row["observation"]["attrs"]
            attrs["verification_clock"] = clock
            attrs["known_at_semantics"] = "simulated projection time; no actual source observation time claimed"
        # A native consumer persists Packet, so the disclosure must be inside it.
        # Reconstruct through the shared codec: source record hashes, provenance
        # and packet identity must reflect the added attributes.
        origin = next(iter(packet["provenance"]["entities"].values()))["origin"]
        artifact["packet"] = bridge.codec.make_packet(
            definitions=packet["definitions"], entities=packet["entities"], claims=packet["claims"],
            sources=packet["sources"], task=packet["task"], origin=origin, known_at=preflight["projected_at"])
    return artifact


def write_summary(directory, summary):
    (directory / "verification.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(summary, ensure_ascii=False))


def matrix_hash(records):
    import hashlib
    digest = hashlib.sha256()
    for row in records:
        digest.update(bridge.canonical([row["input"], row["json_pointer"], row["value"]]) + b"\n")
    return digest.hexdigest()


def frozen_producer_replay(run, fixture, output_parent):
    """Exact first saved V4 output bytes, including fixed timestamp/gzip headers."""
    run = Path(run)
    saved = bridge.strict_json(gzip.decompress((run / "predictions.json.gz").read_bytes()))
    module_name = "_seeding_v4_frozen_producer_replay"
    spec = importlib.util.spec_from_file_location(module_name, run / "prototype_frozen.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    module.FIXTURE = Path(fixture)
    timestamp = saved["manifest"]["predicted_at"]
    class FixedClock:
        @classmethod
        def now(cls, tz=None):
            return datetime.fromisoformat(timestamp)
    module.datetime = FixedClock
    started_cpu, started = time.process_time(), time.perf_counter()
    destination = Path(output_parent) / "frozen-producer-replay"
    module.experiment(destination, run / "policy_frozen.json", run / "protocol_frozen.md")
    cpu, wall = time.process_time() - started_cpu, time.perf_counter() - started
    hashes = {}
    for name in ("predictions.json.gz", "results.json.gz", "prototype_frozen.py", "policy_frozen.json", "protocol_frozen.md"):
        original, replayed = (run / name).read_bytes(), (destination / name).read_bytes()
        if original != replayed:
            raise ValueError("frozen_replay_bytes_changed:" + name)
        hashes[name] = bridge.sha256(original)
    return {"passed": True, "cpu_seconds": cpu, "wall_seconds": wall,
            "scope": "fixed-time actual archived V4 experiment, complete output bytes", "exact_files_sha256": hashes}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument("--run", type=Path)
    source_group.add_argument("--synthetic-small", action="store_true", help="save the independent tiny fixture and its native bindings")
    parser.add_argument("--projection", type=Path, default=bridge.DEFAULT_PROJECTION)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--library", type=Path, help="existing native library; no automatic build")
    parser.add_argument("--replay-frozen-producer", action="store_true")
    parser.add_argument("--confirm-large-export", action="store_true", help="explicit caller confirmation after reviewing the shared export estimate")
    parser.add_argument("--fixture-clock", help="timezone-aware ISO timestamp for a disclosed development simulation; default is current UTC")
    parser.add_argument("--evidence-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    args.evidence_dir.mkdir(parents=True, exist_ok=True)
    if any(args.evidence_dir.iterdir()):
        parser.error("evidence directory must be new/empty; preserve earlier proof")
    if args.synthetic_small:
        spec = importlib.util.spec_from_file_location("_seeding_bridge_tiny_fixture", Path(__file__).with_name("test_method_graph.py"))
        fixture_builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture_builder)
        args.run = args.evidence_dir / "synthetic-run"
        fixture_builder.frozen_run(args.run)
    cfg = bridge.load_projection(args.projection)
    captured = bridge.capture_run(args.run, cfg, input_paths={"fixture": args.fixture} if args.fixture else {})
    try:
        preflight = preflight_export(captured, cfg, confirm_large_export=args.confirm_large_export,
                                    fixture_clock=args.fixture_clock)
    except ExportConfirmationRequired as error:
        write_summary(args.evidence_dir, {"schema": "loom.verification.seeding_method_bridge/1", "passed": False,
            "evaluation_class": "inspected_synthetic_development_regression", "provider_calls": 0,
            "native_verification": "not_requested", "artifact_built": False, **error.preflight})
        parser.error(str(error))
    except ValueError as error:
        parser.error(str(error))
    started_cpu, started = time.process_time(), time.perf_counter()
    artifact = build_verified_artifact(captured, cfg, preflight)
    raw = bridge.canonical(artifact)
    encoded = gzip.compress(raw, mtime=0)
    cpu, wall = time.process_time() - started_cpu, time.perf_counter() - started
    recovered = bridge.recover_results(artifact)
    restored_files = bridge.recover_files(artifact)
    for role, row in captured["files"].items():
        if restored_files[role] != row["raw"]:
            raise ValueError("frozen_bytes_changed:" + role)
    source_roles = {row["observation"]["id"]: row["observation"]["attrs"].get("role") for row in artifact["packet"]["sources"]}
    actual = {(source_roles[row["attrs"]["source_ref"]], row["attrs"]["json_pointer"]): recovered[row["id"]]
              for row in artifact["packet"]["entities"] if row["id"] in recovered}
    records = list(bridge.selected_records(captured, cfg))
    if actual != {(row["input"], row["json_pointer"]): row["value"] for row in records}:
        raise ValueError("record_values_changed")
    after = [dict(row, value=actual[(row["input"], row["json_pointer"])]) for row in records]
    baseline = sum(len(captured["files"][role]["raw"]) for role in cfg["source_roles"]["outputs"])
    summary = {"schema": "loom.verification.seeding_method_bridge/1", "passed": True,
               "evaluation_class": "inspected_synthetic_development_regression", "provider_calls": 0,
               "records_exact": len(records), "record_counts": artifact["trace"]["measurements"],
               "before_matrix_sha256": matrix_hash(records), "after_matrix_sha256": matrix_hash(after),
               "source_files_exact": {role: {"bytes": len(row["raw"]), "sha256": row["sha256"]} for role, row in captured["files"].items()},
               "packet_id": artifact["packet"]["packet_id"], "artifact_sha256": bridge.sha256(raw),
               "artifact_json_bytes": len(raw), "artifact_gzip_bytes": len(encoded), "baseline_gzip_bytes": baseline,
               "total_output_ratio": 1 + len(encoded) / baseline if baseline else None,
               "adapter_cpu_seconds": cpu, "adapter_wall_seconds": wall,
               "producer_cpu_seconds": None, "native_verification": "not_requested", "quality_gain_claimed": False,
               "artifact_built": True, **preflight}
    with tempfile.TemporaryDirectory(prefix="seeding-method-bridge-proof-") as directory:
        if args.replay_frozen_producer:
            if args.fixture is None:
                parser.error("--replay-frozen-producer requires the explicitly supplied trusted synthetic fixture")
            summary["frozen_producer_replay"] = frozen_producer_replay(args.run, args.fixture, directory)
            summary["adapter_to_current_frozen_replay_cpu_ratio"] = cpu / summary["frozen_producer_replay"]["cpu_seconds"]
        if args.library is not None:
            repo = Path(bridge.codec.__file__).resolve().parents[4]
            spec = importlib.util.spec_from_file_location("_method_artifact_consumer", repo / "loom/src/packet/tests/verify_method_graph_artifact.py")
            verifier = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(verifier)
            result, receipt = verifier.verify_artifact(artifact, args.library, Path(directory) / "store")
            summary["native_verification"] = result
            summary["native_verification"]["library_sha256"] = bridge.sha256(args.library.read_bytes())
            summary["native_verification"]["receipt_json_sha256"] = bridge.sha256(bridge.canonical(receipt))
            summary["native_verification"]["receipt_json_bytes"] = len(bridge.canonical(receipt))
            if args.synthetic_small:
                predicates = {cfg["vocabulary"]["predicates"][key] for key in
                              ("version_of", "requests_method_version", "produced_in_run", "produced_by_method_version", "uses_projection", "evaluation_record")}
                native_bindings = {"contract": artifact["contract"], "trace": artifact["trace"],
                    "verification_clock": preflight["verification_clock"], "usage_decision": preflight["usage_decision"],
                    "provenance_edges": [row for row in receipt["packet"]["claims"] if row["predicate"] in predicates],
                    "native_sources": [{"id": row["observation"]["id"], "text_sha256": row["text_sha256"], "attrs": row["observation"]["attrs"]}
                                       for row in receipt["packet"]["sources"]],
                    "scope": "Actual native receipt excerpts and source hashes; full packet/receipt reproducible, producer execution unverified"}
                (args.evidence_dir / "native-bindings.json").write_text(json.dumps(native_bindings, ensure_ascii=False, indent=2) + "\n")
    write_summary(args.evidence_dir, summary)


if __name__ == "__main__":
    main()
