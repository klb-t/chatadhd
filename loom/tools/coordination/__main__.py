"""Local task ownership, inspection and consistent backup commands.

No network/provider clients or credential loading are available in this CLI.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import sys

from .leases import CoordinationError, LeaseStore
from .saved_workflow import SavedWorkflow, decode_json


def read_json(path):
    """Treat ambiguous object keys and non-finite numbers as invalid inputs."""
    return decode_json(Path(path).read_bytes())


def parser():
    root = argparse.ArgumentParser(description=__doc__)
    root.add_argument("--database", type=Path, required=True,
                      help="Shared local SQLite file; all workers use the same file")
    commands = root.add_subparsers(dest="command", required=True)
    register = commands.add_parser("register", help="Register an immutable task specification")
    register.add_argument("--task", required=True)
    register.add_argument("--source-commit", required=True, help="Full Git commit hash")
    register.add_argument("--spec", type=Path, required=True, help="Task specification JSON")
    status = commands.add_parser("status", help="Inspect task and optionally complete receipt history")
    status.add_argument("--task", required=True)
    status.add_argument("--receipts", action="store_true")
    backup = commands.add_parser("backup", help="Create a new consistent, non-overwriting database snapshot")
    backup.add_argument("--output", type=Path, required=True)
    replay = commands.add_parser("graph-replay", help="Replay an exact saved graph transcript; no network calls")
    replay.add_argument("--task", required=True)
    replay.add_argument("--source-commit", required=True)
    replay.add_argument("--owner", required=True)
    replay.add_argument("--packet", type=Path, required=True)
    replay.add_argument("--method", type=Path, required=True)
    replay.add_argument("--recorded-run", type=Path, required=True)
    replay.add_argument("--output-root", type=Path, required=True)
    replay.add_argument("--lease-seconds", type=float, default=300)
    replay.add_argument("--explicit-acceptance", type=Path,
                        help="Optional original acceptance selection JSON; must reproduce saved requests")
    analysis = commands.add_parser("analysis-graph", help="Execute one AnalysisPlan variant with local GraphPacket diffs")
    analysis.add_argument("--task", required=True)
    analysis.add_argument("--source-commit", required=True)
    analysis.add_argument("--owner", required=True)
    analysis.add_argument("--plan", type=Path, required=True)
    analysis.add_argument("--packet", type=Path, required=True)
    analysis.add_argument("--variant-index", type=int, default=0)
    analysis.add_argument("--ledger-directory", type=Path, required=True)
    analysis.add_argument("--output-root", type=Path, required=True)
    analysis.add_argument("--lease-seconds", type=float, default=300)
    return root


def run(args):
    if args.command == "analysis-graph":
        from .analysis_graph import run_coordinated_analysis_graph
        plan_raw, packet_raw = args.plan.read_bytes(), args.packet.read_bytes()
        return run_coordinated_analysis_graph(LeaseStore(args.database),
            task_id=args.task, source_commit=args.source_commit, owner=args.owner,
            plan=decode_json(plan_raw), packet=decode_json(packet_raw), variant_index=args.variant_index,
            ledger_directory=args.ledger_directory, output_root=args.output_root,
            lease_seconds=args.lease_seconds, input_binding={
                "plan_file_sha256": hashlib.sha256(plan_raw).hexdigest(),
                "packet_file_sha256": hashlib.sha256(packet_raw).hexdigest()})
    if args.command in ("status", "backup") and not args.database.is_file():
        raise CoordinationError("database_not_found")
    if args.command == "register":
        spec = read_json(args.spec)
        return {"task": LeaseStore(args.database).register(args.task,
                    source_commit=args.source_commit, spec=spec)}
    if args.command == "graph-replay":
        from .graph_workflow import run_coordinated_workflow
        # Read each input once: hash and decoded value describe the same bytes.
        packet_raw, method_raw = args.packet.read_bytes(), args.method.read_bytes()
        packet, method = decode_json(packet_raw), decode_json(method_raw)
        acceptance_raw = args.explicit_acceptance.read_bytes() if args.explicit_acceptance else None
        acceptance = decode_json(acceptance_raw) if acceptance_raw is not None else None
        transport = SavedWorkflow(args.recorded_run, packet, method)
        preflight = transport.preflight(packet, method, explicit_acceptance=acceptance)
        binding = {"mode": "exact_saved_transcript_replay", "transcript": transport.manifest,
                   "packet_file_sha256": hashlib.sha256(packet_raw).hexdigest(),
                   "method_file_sha256": hashlib.sha256(method_raw).hexdigest(),
                   "acceptance_file_sha256": hashlib.sha256(acceptance_raw).hexdigest() if acceptance_raw is not None else None,
                   "preflight": preflight, "network_calls": 0}
        store = LeaseStore(args.database)
        result = run_coordinated_workflow(store, task_id=args.task, source_commit=args.source_commit,
            owner=args.owner, packet=packet, method=method, transport=transport,
            output_root=args.output_root, lease_seconds=args.lease_seconds,
            explicit_acceptance=acceptance, transport_binding=binding)
        return {"mode": "exact_saved_transcript_replay", "network_calls": 0,
                "preflight": preflight, "execution": result}
    store = LeaseStore(args.database)
    if args.command == "status":
        result = {"task": store.inspect(args.task)}
        if args.receipts:
            result["receipts"] = store.receipts(args.task)
        return result
    if args.command == "backup":
        return store.backup(args.output)
    raise CoordinationError("unknown_command")


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        result = run(args)
    except (CoordinationError, OSError, ValueError, KeyError, TypeError, sqlite3.Error) as exc:
        # Stable error class plus explicit validation reason; no source document
        # dump or exception traceback is emitted into task receipts.
        error = {"error_type": type(exc).__name__, "reason": str(exc)}
        if hasattr(exc, "evidence"):
            error["evidence"] = exc.evidence
        print(json.dumps(error,
                         ensure_ascii=True, sort_keys=True), file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
