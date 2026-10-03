#!/usr/bin/env python3
"""N2 cost instrument v2: streaming library SHA and current RSS snapshots.

No model/network calls. Writes only a new evidence directory and owned temporary
fixture databases. Baseline and candidate use the same C ABI and seed bytes.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import resource
import sqlite3
import subprocess
import sys
import tempfile
import time


def encoded(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


def digest(value):
    return hashlib.sha256(value).hexdigest()


def digest_file(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def current_rss_kib():
    # Linux snapshot, not peak/cache ownership; the returned C buffer is live.
    for line in Path("/proc/self/status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    raise RuntimeError("Linux VmRSS unavailable")


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class Native:
    def __init__(self, library, directory):
        self.lib = ctypes.CDLL(str(library))
        self.lib.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self.lib.loom_init_ex.restype = ctypes.c_void_p
        self.lib.loom_shutdown.argtypes = [ctypes.c_void_p]
        self.lib.loom_free_string.argtypes = [ctypes.c_void_p]
        for name in ("loom_kb_pack",):
            fn = getattr(self.lib, name)
            fn.argtypes, fn.restype = [ctypes.c_void_p], ctypes.c_void_p
        for name in ("loom_kb_judge", "loom_context_build"):
            fn = getattr(self.lib, name)
            fn.argtypes, fn.restype = [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p
        error = ctypes.c_void_p()
        self.ctx = self.lib.loom_init_ex(encoded({"data_dir": str(directory), "start_workers": False}), ctypes.byref(error))
        if not self.ctx:
            message = self.take(error.value) if error.value else b"unknown initialization error"
            raise RuntimeError(message.decode())

    def take(self, pointer):
        if not pointer:
            raise RuntimeError("unexpected null C ABI string")
        try:
            return ctypes.string_at(pointer)
        finally:
            self.lib.loom_free_string(pointer)

    def call(self, name, argument=None):
        args = [self.ctx] if argument is None else [self.ctx, encoded(argument)]
        raw = self.take(getattr(self.lib, name)(*args))
        value = json.loads(raw)
        if isinstance(value, dict) and set(value) == {"error"}:
            raise RuntimeError(value)
        return value

    def close(self):
        self.lib.loom_shutdown(self.ctx)


def fixture(size, pack_hash):
    entities, claims = [], []
    for i in range(size):
        eid, cid = f"e.n2.{i:05}", f"cl.n2.{i:05}"
        entities.append({"id": eid, "kind": "component", "canonical_key": eid, "label": f"component {i}"})
        claims.append({"id": cid, "subject": eid, "predicate": "records", "value": f"topic{i % 32:02}",
            "qualifiers": {"valid_from": "2026-09-30"}, "assessment": {
                "evidence_class": "observed", "origin": "archive", "confidence": 0.9,
                "basis": {"support": [{"observation": f"ob.n2.{i:05}", "extractor": "n2.synthetic@1",
                    "quote": f"topic{i % 32:02} preserves the original record and its exact source provenance. "
                             "A durable checkpoint permits inspection after interruption. "
                             f"The component marker is number {i}; retain all evidence bytes."}]}}})
    return {"run": "kr.n2.synthetic", "pack_hash": pack_hash, "entities": entities, "claims": claims}


def seed(native, directory, data):
    # Public API creates the current schema, including every ancillary table.
    # This unused judgement is synthetic setup, never replayed into a run.
    native.call("loom_kb_judge", {"target_kind": "claim", "target": "cl.n2.setup", "verdict": "confirm",
                                  "reason": "Synthetic benchmark schema setup; no evidence judgement is measured."})
    with sqlite3.connect(directory / "chatadhd.db") as db:
        db.execute("INSERT INTO loom_kb_runs(run_id,pack_hash,status,inputs,created) VALUES(?,?,?,?,?)",
                   (data["run"], data["pack_hash"], "done", '{"fixture":"n2.synthetic"}', "2026-09-30T00:00:00Z"))
        db.executemany("INSERT INTO loom_kb_entities(run_id,id,kind,canonical_key,body) VALUES(?,?,?,?,?)", [
            (data["run"], e["id"], e["kind"], e["canonical_key"], encoded(e).decode()) for e in data["entities"]])
        db.executemany("INSERT INTO loom_kb_claims(run_id,id,subject,predicate,evidence,origin,confidence,body) VALUES(?,?,?,?,?,?,?,?)", [
            (data["run"], c["id"], c["subject"], c["predicate"], "observed", "archive", 0.9, encoded(c).decode()) for c in data["claims"]])


def request(size, theses, channel):
    return {"run": "kr.n2.synthetic", "text": "Inspect explicit plan", "lang": "en", "goal_type": "answer_question",
        "relation_hops": 0, "detail_resolution": "raw", "budget_tokens": 100000,
        "candidate_channels": [{"id": channel, "limit": 10, "min_score": 0}], "candidate_scan_limit": size + 1,
        "plan": {"id": "n2.synthetic.plan", "source_ref": {"fixture": "n2.synthetic.cost.v1"}, "theses": [
            {"id": f"thesis{i:02}", "text": f"topic{i % 32:02}", "require_counter_evidence": False} for i in range(theses)]}}


def child(args):
    out = args.out
    out.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="loom-n2-cost-") as temporary:
        directory = Path(temporary)
        native = Native(args.library, directory)
        try:
            data = fixture(args.size, native.call("loom_kb_pack")["hash"])
            seed(native, directory, data)
            req = request(args.size, args.theses, args.channel)
            save(out / "seed.json", data)
            save(out / "request.json", req)
            argument = encoded(req)
            result = {"size": args.size, "theses": args.theses, "channel": args.channel,
                      "seed_sha256": digest(encoded(data)), "request_sha256": digest(argument), "calls": []}
            first = None
            for number in range(3):
                rss_before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                current_before = current_rss_kib()
                wall, cpu = time.perf_counter_ns(), time.process_time_ns()
                pointer = native.lib.loom_context_build(native.ctx, argument)
                elapsed_cpu, elapsed_wall = time.process_time_ns() - cpu, time.perf_counter_ns() - wall
                rss_native = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                current_native = current_rss_kib()
                raw = native.take(pointer)
                output = json.loads(raw)
                record = {"kind": "cold" if number == 0 else "warm", "index": number,
                          "native_wall_ns": elapsed_wall, "process_cpu_ns": elapsed_cpu,
                          "current_rss_before_kib": current_before, "current_rss_after_native_kib": current_native,
                          "current_rss_delta_kib": current_native - current_before,
                          "peak_rss_before_kib": rss_before, "peak_rss_after_native_kib": rss_native,
                          "peak_rss_after_decode_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                          "output_bytes": len(raw), "output_sha256": digest(raw), "same_as_cold": first is None or first == raw}
                if isinstance(output, dict) and set(output) == {"error"}:
                    record["error"] = output["error"]
                result["calls"].append(record)
                save(out / "measurements.json", result)
                if number == 0:
                    first = raw
                    (out / "output-cold.json").write_bytes(raw)
                if record.get("error") or not record["same_as_cold"]:
                    raise RuntimeError(record)
        finally:
            native.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=lambda s: Path(s).resolve())
    parser.add_argument("--out", required=True, type=lambda s: Path(s).resolve())
    parser.add_argument("--source", default="unspecified")
    parser.add_argument("--compare", type=lambda s: Path(s).resolve())
    parser.add_argument("--child", action="store_true")
    parser.add_argument("--size", type=int)
    parser.add_argument("--theses", type=int)
    parser.add_argument("--channel")
    args = parser.parse_args()
    if args.child:
        child(args)
        return
    args.out.mkdir(parents=True, exist_ok=False)
    report = {"schema": "loom.n2.context_cost/2", "source": args.source,
              "library": str(args.library), "library_sha256": digest_file(args.library),
              "instrument_sha256": digest(Path(__file__).read_bytes()), "python": sys.version,
              "platform": os.uname()._asdict() if hasattr(os.uname(), "_asdict") else list(os.uname()),
              "comparison": str(args.compare) if args.compare else None, "cases": []}
    for size in (64, 256, 1024):
        for theses in (1, 4, 16):
            for channel in ("tfidf", "uninstalled-control"):
                name = f"claims{size}_theses{theses}_{channel}"
                command = [sys.executable, str(Path(__file__).resolve()), "--child", "--library", str(args.library),
                           "--out", str(args.out / name), "--size", str(size), "--theses", str(theses), "--channel", channel]
                row = {"name": name, "command": command}
                try:
                    run = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
                    row["exit_code"] = run.returncode
                    (args.out / f"{name}.stdout.txt").write_text(run.stdout)
                    (args.out / f"{name}.stderr.txt").write_text(run.stderr)
                except subprocess.TimeoutExpired as error:
                    row.update(status="timeout", timeout_seconds=120)
                    (args.out / f"{name}.timeout.txt").write_text(str(error))
                measurement = args.out / name / "measurements.json"
                if measurement.exists():
                    row["measurement"] = json.loads(measurement.read_text())
                if args.compare and (args.out / name / "output-cold.json").exists():
                    old = args.compare / name / "output-cold.json"
                    row["exact_baseline_output"] = old.exists() and old.read_bytes() == (args.out / name / "output-cold.json").read_bytes()
                    for filename, key in (("seed.json", "exact_baseline_seed"), ("request.json", "exact_baseline_request")):
                        old_file = args.compare / name / filename
                        row[key] = old_file.exists() and old_file.read_bytes() == (args.out / name / filename).read_bytes()
                report["cases"].append(row)
                save(args.out / "report.json", report)
                print(json.dumps({"case": name, "exit_code": row.get("exit_code"), "status": row.get("status"),
                                  "exact_baseline_output": row.get("exact_baseline_output"),
                                  "wall_ms": [round(c["native_wall_ns"] / 1e6, 3) for c in row.get("measurement", {}).get("calls", [])]}), flush=True)
                if row.get("exit_code") != 0 or row.get("exact_baseline_output") is False:
                    raise SystemExit(1)


if __name__ == "__main__":
    main()
