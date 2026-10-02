"""Replay preserved first samples, without generating or repairing responses."""
from pathlib import Path
import argparse
import json
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from loom.tools.structure.graph_reply_v1.experiment import score_samples, compare_payloads, compare_composed_payloads


def read(name):
    return json.loads((HERE / name).read_text(encoding="utf-8"))


def replay():
    cases, expectations = read("cases.json"), read("expectations.json")
    samples = {"schema": "loom.graph_reply_samples/1", "samples": [],
               "sample_origin": "in-session-model-pilot",
               "model_identity": "inherited-session-model; exact endpoint not exposed"}
    requests = {"schema": "loom.graph_reply_prepared/1", "requests": []}
    for mode in ("graph", "flat"):
        samples["samples"].extend(read("samples_" + mode + ".json")["samples"])
        requests["requests"].extend(read("requests_" + mode + ".json")["requests"])
    score = score_samples(cases, expectations, samples, adjudications=read("adjudications.json"),
                          prepared_requests=requests)
    return {"verified_score_final.json": score,
            "verified_payloads_final.json": compare_payloads(score),
            "verified_composed_final.json": compare_composed_payloads(cases, samples)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, help="A new directory for replay files (no network calls).")
    args = parser.parse_args()
    products = replay()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
        for name, product in products.items():
            (args.output / name).write_text(json.dumps(product, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"external_api_calls": 0,
                      "summary": products["verified_score_final.json"]["summary_by_context_mode"],
                      "composition": products["verified_composed_final.json"]["totals_by_context_mode"]},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
