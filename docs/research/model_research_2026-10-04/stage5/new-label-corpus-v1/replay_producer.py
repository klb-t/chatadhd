"""Deterministically replay this authored input DATA through frozen recipes.

Reads this directory's inputs.json and frozen code/recipe only. No gold, controls,
old fixture loaders, provider execution, key, budget, score or network access.
The observed size 96 is corpus data, not a new runtime limit.
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path
import hashlib
import json
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]


def guard(event, arguments):
    if event.startswith("socket."):
        raise RuntimeError("offline_corpus_replay_network_forbidden")
    if event in {"subprocess.Popen", "os.system"}:
        raise RuntimeError("offline_corpus_replay_process_forbidden")
    if event == "open" and isinstance(arguments[0], (str, bytes)):
        value = arguments[0].decode("utf-8") if isinstance(arguments[0], bytes) else arguments[0]
        p = Path(value).resolve()
        s = str(p)
        if ("/fixtures/research/" in s or "/eval/real-holdout-key" in s
                or "/docs/research/source_view_experiment_2026-09-30/" in s
                or "/docs/research/recipe_experiments_2026-09-30/" in s
                or ("ledger" in p.name.lower() and p.suffix not in {".py", ".pyc"})
                or "tmp-key" in s or p.name == "gold.json"
                or ("response" in p.name.lower() and p.suffix not in {".py", ".pyc"})):
            raise RuntimeError("offline_corpus_replay_forbidden_input: " + s)


def canonical(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def write_exact(path, value):
    content = canonical(value)
    if path.exists() and path.read_bytes() != content:
        raise ValueError("replay_data_drift: " + str(path))
    if not path.exists():
        path.write_bytes(content)
    return hashlib.sha256(content).hexdigest()


def main():
    sys.dont_write_bytecode = True
    sys.addaudithook(guard)
    sys.path.insert(0, str(ROOT))
    from loom.tools.structure.w3_directed_commitment_v1 import experiment as w3
    from loom.tools.structure import source_view_roles_v1 as roles

    source = HERE / "inputs.json"
    document = json.loads(source.read_text(encoding="utf-8"))
    design = json.loads((HERE / "corpus-design.json").read_text(encoding="utf-8"))
    cases = document["cases"]
    qids = [q["id"] for case in cases for q in case["judgment_queries"]]

    output = HERE / "prepared"
    output.mkdir(exist_ok=True)
    hashes = {}
    hashes["inputs.json"] = hashlib.sha256(source.read_bytes()).hexdigest()
    recipe_hashes = {}
    for variant in design["recipe_variants"]:
        arm, method = variant["arm"], variant["method_id"]
        rows = w3.specs(cases, arm)
        if len(rows) != len(qids) or [r["case_id"] for r in rows] != qids:
            raise ValueError("recipe_query_inventory_drift")
        for case in cases:
            for query in case["judgment_queries"]:
                payload = w3.panel.query_payload(case, query)
                card = roles.role_representation(payload)
                if (card["source_record"] != payload
                        or card["requested_relation_roles"]["requested_attributed_speaker"]
                        != query["attributed_to"]
                        or any(w3.panel._time(t["known_at"]) > w3.panel._time(query["as_of"])
                               for t in payload["turns"])):
                    raise ValueError("role_or_temporal_prefix_drift")
        filename = method + "_inputs.json"
        hashes["prepared/" + filename] = write_exact(output / filename, rows)
        recipe_hashes[method] = hashlib.sha256(canonical(rows[0]["questions"])).hexdigest()

    comparison = design["recipe_comparison"]
    active = json.loads((output / (comparison["base_method"] + "_inputs.json")).read_text())
    directed = json.loads((output / (comparison["candidate_method"] + "_inputs.json")).read_text())
    recipe = w3.recipe()
    path = recipe["changed_field"].split(".")
    for a, b in zip(active, directed):
        expected = json.loads(json.dumps(a))
        parent = expected
        for component in path[:-1]:
            parent = parent[component]
        parent[path[-1]] += recipe["append"]
        if expected != b:
            raise ValueError("frozen_recipe_delta_drift")

    receipt = {
        "schema": "loom.new_label_corpus.offline_replay/1",
        "corpus_id": document["corpus_id"],
        "families": len(cases), "queries_per_recipe": len(qids),
        "language_families": dict(Counter(c["language"] for c in cases)),
        "role_contract_payloads_checked": len(qids) * len(design["recipe_variants"]),
        "physical_temporal_prefixes_checked": len(qids) * len(design["recipe_variants"]),
        "exact_recipe_delta_queries_checked": len(active),
        "frozen_recipe_question_sha256": recipe_hashes,
        "gold_read": False, "fixture_loaders_called": False,
        "network_calls": 0, "paid_calls": 0, "model_responses_read": False,
        "replay_file_sha256": hashes,
    }
    write_exact(output / "offline-replay.json", receipt)
    print(json.dumps(receipt, ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
