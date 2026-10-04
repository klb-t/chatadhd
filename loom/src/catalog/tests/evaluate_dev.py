#!/usr/bin/env python3
"""Measure the public synthetic_dev catalog fixture through the native C ABI.

This runner reads no independent, blind, real or holdout corpus. Public gold
provides the fictional owner's declared project aliases and evaluation labels;
labels never enter native scoring or construct semantic vectors. Optional
semantic input is an externally prepared replay envelope, passed unchanged.
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


LOOM = Path(__file__).resolve().parents[3]
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


def validate_replay(value: Any) -> None:
    """Catch accidental evaluation-label payloads; do not transform evidence."""
    if not isinstance(value, dict):
        raise ValueError("semantic replay envelope must be a JSON object")
    forbidden = {"gold", "ground_truth", "gold_labels", "evaluation_labels",
                 "noise_traps", "noise_generic", "expected_selected"}
    pending = [value]
    while pending:
        node = pending.pop()
        if isinstance(node, dict):
            leaked = forbidden.intersection(node)
            if leaked:
                raise ValueError("evaluation-only keys in semantic input: " + ", ".join(sorted(leaked)))
            pending.extend(node.values())
        elif isinstance(node, list):
            pending.extend(node)


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
        self.library.loom_set_config_json.argtypes = [pointer, string]
        self.library.loom_set_config_json.restype = ctypes.c_int
        for name in ("loom_catalog_scan", "loom_catalog_score", "loom_knowledge_run"):
            getattr(self.library, name).argtypes = [pointer, string, pointer, pointer]
            getattr(self.library, name).restype = pointer
        for name in ("loom_catalog_select", "loom_catalog_query", "loom_catalog_preview", "loom_kb_policy"):
            getattr(self.library, name).argtypes = [pointer, string]
            getattr(self.library, name).restype = pointer
        for name in ("loom_get_config", "loom_kb_pack", "loom_info"):
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


def auc(rows: list[dict], field: str) -> float:
    positive = [r for r in rows if r["gold"] == "relevant"]
    negative = [r for r in rows if r["gold"] != "relevant"]
    wins = sum(1.0 if p[field] > n[field] else 0.5 if p[field] == n[field] else 0.0
               for p in positive for n in negative)
    return wins / (len(positive) * len(negative))


def summarize(rows: list[dict], auxiliary: list[dict], threshold: float) -> dict:
    counts = {"tp": 0, "fp": 0, "fn": 0, "tn": 0}
    for row in rows:
        positive = row["gold"] == "relevant"
        key = ("t" if row["selected"] == positive else "f") + ("p" if row["selected"] else "n")
        counts[key] += 1
    tp, fp, fn, tn = (counts[key] for key in ("tp", "fp", "fn", "tn"))
    lexical = {r["id"] for r in rows + auxiliary if r["lexical_score"] >= threshold}
    semantic = {r["id"] for r in rows + auxiliary if r["semantic_score"] >= threshold}
    by_id = {r["id"]: r for r in rows + auxiliary}
    def diagnostic(ids: set[str]) -> list[dict]:
        return [{"id": identifier, "gold": by_id[identifier]["gold"],
                 "selected": by_id[identifier]["selected"]} for identifier in sorted(ids)]
    ranking = {}
    for field in ("score", "lexical_score", "semantic_score"):
        ordered = sorted(rows, key=lambda row: (-row[field], row["id"]))
        hits = sum(row["gold"] == "relevant" for row in ordered[:45])
        ranking[field] = {"auc": auc(rows, field), "hits_at_45": hits,
                          "recall_at_45": hits / 45, "precision_at_45": hits / 45}
    return {"labeled_conversations": len(rows), "relevant_total": 45, "noise_traps_total": 5,
            "noise_generic_total": 15, **counts,
            "precision": tp / (tp + fp) if tp + fp else None, "recall": tp / 45,
            "noise_traps_selected": sum(r["selected"] for r in rows if r["gold"] == "noise_traps"),
            "noise_generic_selected": sum(r["selected"] for r in rows if r["gold"] == "noise_generic"),
            "miss_ids": sorted(r["id"] for r in rows if r["gold"] == "relevant" and not r["selected"]),
            "false_positive_ids": sorted(r["id"] for r in rows if r["gold"] != "relevant" and r["selected"]),
            "auxiliary": {"total": len(auxiliary), "selected": sum(r["selected"] for r in auxiliary)},
            "ranking": ranking,
            "shadow": {"tau_relevant": threshold, "lexical_count": len(lexical),
                       "semantic_count": len(semantic), "both": len(lexical & semantic),
                       "lexical_only": diagnostic(lexical - semantic),
                       "semantic_only": diagnostic(semantic - lexical)}}


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
    patch = {}
    semantic_receipt = None
    if arguments.semantic_input:
        semantic_path = arguments.semantic_input.resolve()
        envelope = json.loads(semantic_path.read_text(encoding="utf-8"))
        validate_replay(envelope)
        patch["catalog_semantic_candidates"] = envelope
        semantic_receipt = {"path": str(semantic_path), "sha256": sha256(semantic_path),
                            "injected_unchanged": True, "origin": "externally_supplied_unverified"}
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
                "knowledge_config": config, "score_config": score, "runtime_config_patch": patch,
                "semantic_input": semantic_receipt,
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
            if patch:
                status = native.library.loom_set_config_json(context, canonical(patch))
                if status != 0:
                    raise RuntimeError("loom_set_config_json failed: " + str(status))
            snapshot["runtime_config_effective"] = native.call("loom_get_config", context)
            snapshot["native_pack"] = native.call("loom_kb_pack", context)
            thresholds = native.call("loom_kb_policy", context, "thresholds")
            snapshot["native_thresholds"] = thresholds
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
                       "lexical_score": features.get("lexical_score", 0.0),
                       "semantic_score": features.get("semantic_score", 0.0),
                       "reasons": {"selection": decision["reasons"], "score": preview["score"]["reasons"]}}
                (rows if identifier in gold else auxiliary).append(row)
            if {r["id"] for r in rows} != set(gold):
                raise RuntimeError("native decisions do not cover all 65 labeled DEV conversations")
            summary = summarize(rows, auxiliary, thresholds["catalog"]["tau_relevant"])
            report = {"schema": "loom.catalog_dev_report/1", "inputs": snapshot,
                      "inputs_receipt": str(inputs_path), "summary": summary,
                      "native_score_summary": scored, "native_preparation": stage,
                      "native_profiles": native_profiles,
                      "conversations": sorted(rows, key=lambda r: r["id"]),
                      "auxiliary_documents": sorted(auxiliary, key=lambda r: r["id"]),
                      "limitations": ["Public synthetic development corpus; not independent or real-archive accuracy.",
                                      "Configured fictional project aliases enter native profile construction; evaluation labels enter metrics only.",
                                      "Replay input, when present, is supplied unchanged; this runner neither infers nor constructs model vectors.",
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
    parser.add_argument("--semantic-input", type=Path)
    arguments = parser.parse_args()
    report = run(arguments)
    print(json.dumps(report["summary"], ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
