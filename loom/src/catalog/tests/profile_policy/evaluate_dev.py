#!/usr/bin/env python3
"""Measure the public synthetic_dev catalog fixture through the native C ABI.

This runner reads no independent, blind, real or holdout corpus. Public gold
provides the fictional owner's declared project aliases and evaluation labels;
labels enter evaluation only. The runner applies no policy or vector override
and performs no provider calls; the complete native DEV result is recorded.
"""
from __future__ import annotations

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import tempfile
from typing import Any


LOOM = Path(__file__).resolve().parents[4]
REPOSITORY = LOOM.parent
FIXTURE = LOOM / "tests/fixtures/eval/synthetic_dev"


def canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2,
                                    sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def source_manifest() -> dict[str, str]:
    chosen = [p for directory in (LOOM / "src/catalog", LOOM / "data")
              for p in sorted(directory.rglob("*"))
              if p.is_file() and "__pycache__" not in p.parts and "results" not in p.parts]
    chosen += [LOOM / "include/loom/catalog.h", LOOM / "include/loom/loom.h",
               LOOM / "src/capi/capi_catalog.cpp"]
    return {str(p.relative_to(REPOSITORY)): sha256(p) for p in sorted(set(chosen))}


def commit_id() -> str:
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPOSITORY,
                            capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else "unavailable"


class Native:
    def __init__(self, path: Path) -> None:
        self.library = ctypes.CDLL(str(path.resolve()))
        pointer, string = ctypes.c_void_p, ctypes.c_char_p
        self.library.loom_init_ex.argtypes = [string, ctypes.POINTER(pointer)]
        self.library.loom_init_ex.restype = pointer
        self.library.loom_shutdown.argtypes = [pointer]
        self.library.loom_shutdown.restype = None
        self.library.loom_free_string.argtypes = [pointer]
        self.library.loom_free_string.restype = None
        self.library.loom_set_log_stderr.argtypes = [ctypes.c_int]
        self.library.loom_set_log_stderr.restype = None
        self.library.loom_set_log_stderr(0)
        for name in ("loom_catalog_score", "loom_knowledge_run"):
            getattr(self.library, name).argtypes = [pointer, string, pointer, pointer]
            getattr(self.library, name).restype = pointer
        for name in ("loom_catalog_select", "loom_catalog_query", "loom_catalog_preview", "loom_kb_policy"):
            getattr(self.library, name).argtypes = [pointer, string]
            getattr(self.library, name).restype = pointer
        for name in ("loom_get_config", "loom_kb_pack"):
            getattr(self.library, name).argtypes = [pointer]
            getattr(self.library, name).restype = pointer

    def decode(self, pointer: int | None) -> Any:
        if not pointer:
            raise RuntimeError("native API returned null")
        try:
            value = json.loads(ctypes.string_at(pointer).decode("utf-8"))
        finally:
            self.library.loom_free_string(pointer)
        if isinstance(value, dict) and set(value) == {"error"}:
            raise RuntimeError(value["error"])
        return value

    def call(self, name: str, context: int, argument: Any = None, *, progress: bool = False) -> Any:
        arguments = [context]
        if argument is not None:
            arguments.append(argument.encode("utf-8") if isinstance(argument, str) else canonical(argument))
        if progress:
            arguments += [None, None]
        return self.decode(getattr(self.library, name)(*arguments))

    def open(self, options: dict) -> int:
        error = ctypes.c_void_p()
        context = self.library.loom_init_ex(canonical(options), ctypes.byref(error))
        if not context:
            detail = self.decode(error.value) if error.value else "native runtime initialization failed"
            raise RuntimeError(detail)
        return context


def summarize(rows: list[dict], auxiliary: list[dict]) -> dict:
    counts = {"tp": 0, "fp": 0, "fn": 0, "tn": 0}
    for row in rows:
        positive = row["gold"] == "relevant"
        key = ("t" if row["selected"] == positive else "f") + ("p" if row["selected"] else "n")
        counts[key] += 1
    tp, fp, fn, tn = (counts[key] for key in ("tp", "fp", "fn", "tn"))
    return {"records_total": len(rows) + len(auxiliary), "labeled_conversations": len(rows),
            "relevant_total": tp + fn,
            "noise_traps_total": sum(row["gold"] == "noise_traps" for row in rows),
            "noise_generic_total": sum(row["gold"] == "noise_generic" for row in rows), **counts,
            "precision": tp / (tp + fp) if tp + fp else None,
            "recall": tp / (tp + fn) if tp + fn else None,
            "noise_traps_selected": sum(row["selected"] for row in rows if row["gold"] == "noise_traps"),
            "noise_generic_selected": sum(row["selected"] for row in rows if row["gold"] == "noise_generic"),
            "miss_ids": sorted(row["id"] for row in rows if row["gold"] == "relevant" and not row["selected"]),
            "false_positive_ids": sorted(row["id"] for row in rows if row["gold"] != "relevant" and row["selected"]),
            "auxiliary": {"total": len(auxiliary), "selected": sum(row["selected"] for row in auxiliary)}}


def run(arguments: argparse.Namespace) -> dict:
    library = arguments.library.resolve()
    truth_path = FIXTURE / "ground_truth.json"
    truth = json.loads(truth_path.read_text(encoding="utf-8"))
    categories = truth["units"]
    if {key: len(value) for key, value in categories.items()} != {
            "relevant": 45, "noise_traps": 5, "noise_generic": 15}:
        raise ValueError("synthetic_dev denominators changed; review the runner protocol")
    aliases = [alias for project in truth["projects"] for alias in project["aliases"]]
    gold = {row["conv_id"]: category for category, values in categories.items() for row in values}
    if len(gold) != 65:
        raise ValueError("duplicate DEV conversation IDs")
    sources = [FIXTURE / "chatgpt_export.zip", FIXTURE / "claude_export.zip"]
    scan = {"sources": [str(p) for p in sources], "threads": 1}
    profile = {"extra_terms": aliases}
    score = {"llm": "off"}
    config = {"sources": scan["sources"], "stages": ["catalog"], "llm": "off",
              "stage_params": {"catalog": {"scan": scan, "profile": profile, "score": score,
                                             "import": {"dry_run": True, "import_messages": False}}}}
    manifest = source_manifest()
    snapshot = {"schema": "loom.catalog_dev_inputs/1", "recorded_at": datetime.now(timezone.utc).isoformat(),
                "native_commit": commit_id(), "library": {"path": str(library), "sha256": sha256(library)},
                "source_manifest": manifest, "source_manifest_sha256": hashlib.sha256(canonical(manifest)).hexdigest(),
                "fixture_sha256": {p.name: sha256(p) for p in sources + [truth_path]},
                "profile_input": {"source": "public fictional owner's declared aliases", "aliases": aliases},
                "knowledge_config": config, "score_config": score,
                "boundaries": {"corpus": "synthetic_dev_only", "holdout_read": False,
                               "blind_read": False, "provider_calls": 0, "core_import": False}}
    inputs_path = arguments.output.with_name(arguments.output.stem + ".inputs.json")
    write_json(inputs_path, snapshot)
    native = Native(library)
    with tempfile.TemporaryDirectory(prefix="loom-catalog-dev-") as temporary:
        options = {"data_dir": str(Path(temporary) / "runtime"), "start_workers": False}
        context = native.open(options)
        try:
            snapshot["runtime_options"] = options
            snapshot["runtime_config_before"] = native.call("loom_get_config", context)
            snapshot["runtime_config_effective"] = native.call("loom_get_config", context)
            snapshot["native_pack"] = native.call("loom_kb_pack", context)
            thresholds = native.call("loom_kb_policy", context, "thresholds")
            snapshot["native_thresholds"] = thresholds
            snapshot["native_relevance"] = native.call("loom_kb_policy", context, "relevance")
            write_json(inputs_path, snapshot)  # Freeze exact inputs before scanning/scoring.
            stage = native.call("loom_knowledge_run", context, config, progress=True)
            if stage.get("status") != "done":
                raise RuntimeError("native catalog preparation failed: " + json.dumps(stage))
            scored = native.call("loom_catalog_score", context, score, progress=True)
            run_id = scored["run_id"]
            decisions = native.call("loom_catalog_select", context, run_id)["decisions"]
            by_unit = {d["unit_id"]: d for d in decisions}
            units = native.call("loom_catalog_query", context,
                                {"run_id": run_id, "sort": "id", "limit": max(1, len(decisions))})
            if len(units) != len(decisions):
                raise RuntimeError("native query did not cover every scored decision")
            database = Path(temporary) / "runtime/chatadhd.db"
            with sqlite3.connect(database.as_uri() + "?mode=ro", uri=True) as connection:
                sketches = {uid: json.loads(body) for uid, body in connection.execute(
                    "SELECT id, sketch FROM loom_cat_units")}
                native_profiles = [json.loads(body) for (body,) in connection.execute(
                    "SELECT body FROM loom_cat_profiles ORDER BY id")]
            rows, auxiliary = [], []
            for unit in units:
                identifier = unit["ext_id"] or unit["unit"]["id"]
                uid = unit["unit"]["id"]
                decision = by_unit[uid]
                preview = native.call("loom_catalog_preview", context, uid)
                features = preview["score"]["features"]
                row = {"id": identifier, "gold": gold.get(identifier, "auxiliary"),
                       "unit_id": uid, "content_hash": unit["content_hash"],
                       "title": unit["unit"]["title"], "n_chars": unit["n_chars"],
                       "head": unit["head"], "selected": decision["selected"],
                       "score": decision["score"], "label": decision["label"],
                       "decided_by": decision["decided_by"], "features": features,
                       "sketch": sketches[uid],
                       "native_unit": unit, "native_decision": decision, "native_preview": preview,
                       "reasons": {"selection": decision["reasons"], "score": preview["score"]["reasons"]}}
                (rows if identifier in gold else auxiliary).append(row)
            if {r["id"] for r in rows} != set(gold):
                raise RuntimeError("native decisions do not cover all 65 labeled DEV conversations")
            if len(units) != 68 or len(auxiliary) != 3:
                raise RuntimeError("native result does not cover exactly 68 DEV records, including 3 auxiliary documents")
            summary = summarize(rows, auxiliary)
            report = {"schema": "loom.catalog_dev_report/1", "inputs": snapshot,
                      "inputs_receipt": str(inputs_path), "summary": summary,
                      "native_score_summary": scored, "native_preparation": stage,
                      "native_profiles": native_profiles,
                      "conversations": sorted(rows, key=lambda r: r["id"]),
                      "auxiliary_documents": sorted(auxiliary, key=lambda r: r["id"]),
                      "limitations": ["Public synthetic development corpus; not independent or real-archive accuracy.",
                                      "Configured fictional project aliases enter native profile construction; evaluation labels enter metrics only.",
                                      "No relevance-policy override, semantic replay input, vector injection or provider execution is performed.",
                                      "Character and word TF-IDF channels share lexical source features; their agreement is not independent semantic evidence."]}
        finally:
            native.library.loom_shutdown(context)
    if sha256(library) != snapshot["library"]["sha256"]:
        raise RuntimeError("library changed during measurement; report cannot certify the recorded binary")
    write_json(arguments.output, report)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    report = run(arguments)
    print(json.dumps(report["summary"], ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
