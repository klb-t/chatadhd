#!/usr/bin/env python3
"""Frozen, local learned-embedding arm; no gold, provider calls or fitting.

prepare downloads only public pinned model artifacts. run is offline and requires
an independently supplied exact input SHA256. Runtime dependencies may be loaded
from <output>/runtime_packages; no credentials or ordinary user configs are read.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
import platform
import sys
import time
import urllib.request
from pathlib import Path

MODEL_ID = "sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2"
REVISION = "e8f8c211226b894fcb81acc59f3b34ba3efd5f42"
ARTIFACTS = {
    "onnx/model.onnx": (470301610, "10f7a088420252b26caf819236ca2c9d2987afd0fc06fec7553b542a5655a05a"),
    "tokenizer.json": (9081518, "2c3387be76557bd40970cec13153b3bbf80407865484b209e655e5e4729076b8"),
    "config.json": (645, None),
    "tokenizer_config.json": (526, None),
    "special_tokens_map.json": (239, None),
    "sentence_bert_config.json": (53, None),
    "1_Pooling/config.json": (190, None),
    "modules.json": (229, None),
    "README.md": (3888, None),
}
MAX_MODEL_BYTES = 600_000_000
MAX_TOKENS = 128
METHOD = "learned_multilingual_minilm_l12_v2_onnx_cosine_v1"


def digest_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


def versions() -> dict:
    found = {"python": platform.python_version()}
    for name in ("numpy", "onnxruntime", "tokenizers", "protobuf", "sympy", "packaging", "huggingface-hub"):
        try:
            found[name] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            found[name] = None
    return found


def recipe() -> dict:
    return {
        "schema": "loom.local_embedding_panel.protocol/1", "method": METHOD,
        "model_id": MODEL_ID, "revision": REVISION,
        "model_card": "https://huggingface.co/" + MODEL_ID,
        "representation": "learned_sentence_embedding", "dimensions": 384,
        "runtime": "CPU ONNXRuntime; no torch or remote inference",
        "input_text": "exact left/right strings; no text normalization or prompt prefixes",
        "tokenization": "pinned tokenizer.json, special tokens, longest-first truncation to128 total tokens",
        "truncation_policy": "retain prefix at128 tokens; report per-text untruncated token counts",
        "pooling": "attention-mask mean of token embeddings, including non-padding special tokens",
        "normalization": "L2 normalization after pooling, float32 vectors",
        "score": "cosine = float64 accumulation of dot product of normalized float32 vectors",
        "prediction": None, "threshold_policy": "no decision threshold; no fit or calibration on48pairs",
        "primary_measurement": "frozen per-pair cosine; all quality scoring delegated after vectors freeze",
        "max_model_bytes": MAX_MODEL_BYTES, "expected_artifacts": ARTIFACTS,
        "not_claimed": ["logical equivalence", "structural graph extraction", "calibrated similarity", "model-free method"],
        "separation": "this program never reads gold labels, outcome reports, holdout keys or paid APIs",
    }


def protocol(output: Path) -> dict:
    path = output / "protocol.json"
    expected = json.loads(json.dumps(recipe()))
    if path.exists():
        current = json.loads(path.read_text(encoding="utf-8"))
        if current != expected:
            raise ValueError("existing_protocol_does_not_match_frozen_recipe")
    else:
        write_json(path, expected)
    return expected


def prepare(output: Path) -> dict:
    protocol(output)  # Write BEFORE any model/data measurement.
    if sum(v[0] for v in ARTIFACTS.values()) > MAX_MODEL_BYTES:
        raise ValueError("model_size_exceeds_authorized_download_budget")
    local = output / "model"
    manifest = []
    for name, (size, sha) in ARTIFACTS.items():
        target = local / name
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            request = urllib.request.Request(
                f"https://huggingface.co/{MODEL_ID}/resolve/{REVISION}/{name}?download=true",
                headers={"User-Agent": "Loom-Local-Embedding-Research/1"},
            )
            tmp = target.with_suffix(target.suffix + ".part")
            try:
                total = 0
                with urllib.request.urlopen(request, timeout=60) as response, tmp.open("wb") as dest:
                    for block in iter(lambda: response.read(1024 * 1024), b""):
                        total += len(block)
                        if total > size:
                            raise ValueError("download_exceeded_declared_artifact_size")
                        dest.write(block)
                if total != size:
                    raise ValueError("download_size_mismatch")
                tmp.rename(target)
            except Exception:
                tmp.unlink(missing_ok=True)
                raise
        actual = digest_file(target)
        if target.stat().st_size != size or (sha and actual != sha):
            raise ValueError("pinned_artifact_integrity_mismatch:" + name)
        manifest.append({"file": name, "bytes": size, "sha256": actual})
        print(json.dumps({"prepared": name, "bytes": size, "sha256": actual}), flush=True)
    result = {"schema": "loom.local_embedding_panel.model_manifest/1", "model_id": MODEL_ID,
              "revision": REVISION, "artifacts": manifest, "versions": versions(),
              "protocol_sha256": digest_file(output / "protocol.json")}
    write_json(output / "model_manifest.json", result)
    return result


def load_instrument(output: Path, threads: int):
    import numpy as np
    import onnxruntime as ort
    from tokenizers import Tokenizer
    manifest = json.loads((output / "model_manifest.json").read_text(encoding="utf-8"))
    for item in manifest["artifacts"]:
        path = output / "model" / item["file"]
        if digest_file(path) != item["sha256"]:
            raise ValueError("offline_artifact_changed:" + item["file"])
    pooling = json.loads((output / "model/1_Pooling/config.json").read_text(encoding="utf-8"))
    sentence = json.loads((output / "model/sentence_bert_config.json").read_text(encoding="utf-8"))
    if sentence.get("max_seq_length") != MAX_TOKENS or not pooling.get("pooling_mode_mean_tokens"):
        raise ValueError("pinned_recipe_config_mismatch")
    environment = {"schema": "loom.local_embedding_panel.environment/1", "versions": versions(),
                   "platform": platform.platform(), "machine": platform.machine(),
                   "provider": "CPUExecutionProvider", "threads": threads,
                   "protocol_sha256": digest_file(output / "protocol.json"),
                   "code_sha256": digest_file(Path(__file__)),
                   "model_manifest_sha256": digest_file(output / "model_manifest.json")}
    frozen_environment = output / "instrument_environment.json"
    if frozen_environment.exists():
        if json.loads(frozen_environment.read_text(encoding="utf-8")) != environment:
            raise ValueError("instrument_environment_changed_since_first_measurement")
    else:
        write_json(frozen_environment, environment)  # Before first session/inference.
    tokenizer = Tokenizer.from_file(str(output / "model/tokenizer.json"))
    settings = ort.SessionOptions()
    settings.intra_op_num_threads = threads
    settings.inter_op_num_threads = 1
    settings.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    session = ort.InferenceSession(str(output / "model/onnx/model.onnx"), sess_options=settings,
                                   providers=["CPUExecutionProvider"])

    def embed(text: str):
        tokenizer.no_truncation()
        tokenizer.no_padding()
        original = tokenizer.encode(text, add_special_tokens=True)
        count = len(original.ids)
        tokenizer.enable_truncation(max_length=MAX_TOKENS, strategy="longest_first", direction="right")
        encoded = tokenizer.encode(text, add_special_tokens=True)
        values = {"input_ids": np.asarray([encoded.ids], dtype=np.int64),
                  "attention_mask": np.asarray([encoded.attention_mask], dtype=np.int64),
                  "token_type_ids": np.asarray([encoded.type_ids], dtype=np.int64)}
        inputs = {entry.name: values[entry.name] for entry in session.get_inputs()}
        returned = session.run(None, inputs)
        states = returned[0]
        if states.ndim != 3 or states.shape[:2] != inputs["input_ids"].shape or states.shape[2] != 384:
            raise ValueError("unexpected_onnx_token_embedding_shape")
        mask = inputs["attention_mask"].astype(np.float32)[..., None]
        pooled = np.sum(states.astype(np.float32) * mask, axis=1) / np.maximum(mask.sum(axis=1), np.float32(1e-9))
        vector = pooled[0].astype(np.float32)
        norm = float(np.linalg.norm(vector.astype(np.float64)))
        if not np.isfinite(vector).all() or norm == 0:
            raise ValueError("nonfinite_or_zero_embedding")
        vector = (vector / norm).astype(np.float32)
        return vector, {"untruncated_tokens": count, "encoded_tokens": len(encoded.ids), "truncated": count > MAX_TOKENS}

    return np, embed, {"inputs": [x.name for x in session.get_inputs()],
                       "outputs": [x.name for x in session.get_outputs()],
                       "providers": session.get_providers(), "threads": threads, "versions": versions(),
                       "environment_sha256": digest_file(frozen_environment)}


def self_test(output: Path, threads: int) -> dict:
    protocol(output)
    np, embed, runtime = load_instrument(output, threads)
    a, meta = embed("To jest wyłącznie syntetyczny test lokalny.")
    b, _ = embed("To jest wyłącznie syntetyczny test lokalny.")
    long, longmeta = embed("Test " * 400)
    cosine = float(np.dot(a.astype(np.float64), b.astype(np.float64)))
    if a.shape != (384,) or not math.isclose(cosine, 1.0, abs_tol=1e-6) or not longmeta["truncated"]:
        raise AssertionError("embedding_mechanism_self_test_failed")
    result = {"schema": "loom.local_embedding_panel.self_test/1", "runtime": runtime,
              "identity_cosine": cosine, "dimensions": len(a), "short_tokenization": meta,
              "long_tokenization": longmeta, "quality_model_measurement": False}
    write_json(output / "self_test.json", result)
    return result


def run(inputs: Path, expected_sha: str, output: Path, threads: int, expected_pairs: int) -> dict:
    protocol(output)
    if digest_file(inputs) != expected_sha:
        raise ValueError("independently_verified_inputs_hash_mismatch")
    destination = output / "scores.json"
    if destination.exists():
        raise ValueError("first_scores_already_saved_refuse_overwrite")
    rows = json.loads(inputs.read_text(encoding="utf-8"))
    if not isinstance(rows, list) or len(rows) != expected_pairs:
        raise ValueError("unexpected_pair_denominator")
    np, embed, runtime = load_instrument(output, threads)
    started = time.perf_counter()
    vectors, rows_out, text_index, seen_ids = [], [], {}, set()
    for pair in rows:
        case_id = pair["case_id"]
        if case_id in seen_ids:
            raise ValueError("duplicate_case_id")
        seen_ids.add(case_id)
        state = json.loads(pair["state"]["text"])
        if set(state) != {"left", "right"} or not all(isinstance(state[k], str) and state[k] for k in ("left", "right")):
            raise ValueError("pair_text_schema_mismatch")
        refs = []
        for side in ("left", "right"):
            text = state[side]
            key = hashlib.sha256(text.encode("utf-8")).hexdigest()
            if key not in text_index:
                vector, meta = embed(text)
                index = len(vectors)
                vectors.append(vector)
                text_index[key] = {"index": index, "text_sha256": key, "text_utf8_bytes": len(text.encode("utf-8")),
                                   "vector_sha256": hashlib.sha256(vector.astype("<f4").tobytes()).hexdigest(), **meta}
            refs.append(text_index[key])
        score = float(np.dot(vectors[refs[0]["index"]].astype(np.float64), vectors[refs[1]["index"]].astype(np.float64)))
        rows_out.append({"case_id": case_id, "language": pair.get("language"), "method": METHOD,
                         "score": score, "prediction": None, "available": True,
                         "representation": "raw_text_learned_sentence_embedding",
                         "provenance": {"model_id": MODEL_ID, "revision": REVISION,
                                        "protocol_sha256": digest_file(output / "protocol.json"),
                                        "left": refs[0], "right": refs[1]}})
    array = np.stack(vectors).astype("<f4")
    np.save(output / "vectors.npy", array, allow_pickle=False)
    result = {"schema": "loom.local_embedding_panel.scores/1", "method": METHOD,
              "inputs_sha256": expected_sha, "protocol_sha256": digest_file(output / "protocol.json"),
              "code_sha256": digest_file(Path(__file__)), "model_manifest_sha256": digest_file(output / "model_manifest.json"),
              "vectors_file": "vectors.npy", "vectors_sha256": digest_file(output / "vectors.npy"),
              "vector_count": len(vectors), "dimensions": array.shape[1], "runtime": runtime,
              "coverage": {"pair_total": len(rows), "available_pairs": len(rows_out), "unavailable_pairs": 0,
                           "unique_texts": len(vectors), "truncated_unique_texts": sum(x["truncated"] for x in text_index.values())},
              "elapsed_seconds": time.perf_counter() - started, "rows": rows_out,
              "threshold": None, "quality_evaluation": "not_performed; no_gold_read"}
    write_json(destination, result)
    write_json(output / "run_manifest.json", {"schema": "loom.local_embedding_panel.run_manifest/1",
               "scores_sha256": digest_file(destination), "vectors_sha256": result["vectors_sha256"],
               "inputs_sha256": expected_sha, "protocol_sha256": result["protocol_sha256"],
               "code_sha256": result["code_sha256"], "availability": result["coverage"]})
    return {k: v for k, v in result.items() if k != "rows"}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "self-test", "run"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--inputs", type=Path)
    parser.add_argument("--inputs-sha256")
    parser.add_argument("--pairs", type=int, default=48)
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args(argv)
    packages = args.output / "runtime_packages"
    if packages.exists():
        sys.path.insert(0, str(packages.resolve()))
    args.output.mkdir(parents=True, exist_ok=True)
    if args.threads < 1 or args.pairs < 1:
        parser.error("threads/pairs must be positive")
    if args.command == "prepare":
        result = prepare(args.output)
    elif args.command == "self-test":
        result = self_test(args.output, args.threads)
    else:
        if args.inputs is None or args.inputs_sha256 is None:
            parser.error("run requires inputs and independently verified inputs-sha256")
        result = run(args.inputs, args.inputs_sha256, args.output, args.threads, args.pairs)
    print(json.dumps(result, ensure_ascii=False, allow_nan=False), flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        # urllib/ONNX exceptions can include URLs or inputs; report type only.
        print(json.dumps({"status": "failed", "error_type": type(error).__name__}), file=sys.stderr)
        raise SystemExit(1)
