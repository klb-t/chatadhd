#!/usr/bin/env python3
"""Independent, goal-conditioned diagnostics; never reads synthetic_dev/holdout.

The frozen data file is authoritative. Generated exports contain source prose,
not labels/formulas. Formal annotations measure representation/proof behavior
conditional on an annotation and do not establish natural-language extraction.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from typing import Any


DEFAULT_FIXTURE = Path(__file__).resolve().parents[2] / "tests/fixtures/eval/independent_structure_v1/cases.json"


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()


def digest(value: Any) -> str:
    return hashlib.sha256(canonical(value)).hexdigest()


def load_fixture(path: Path = DEFAULT_FIXTURE) -> dict:
    fixture = json.loads(path.read_text())
    validate(fixture)
    return fixture


def validate(fixture: dict) -> dict:
    """Reject leakage, ungrounded source spans, and malformed group splits."""
    assert fixture["schema"] == "loom.independent_eval/1"
    cases = {c["id"]: c for c in fixture["cases"]}
    assert len(cases) == len(fixture["cases"]), "duplicate case ID"
    groups: dict[str, set[str]] = defaultdict(set)
    claims = set()
    for case in cases.values():
        groups[case["group"]].add(case["split"])
        assert case["split"] in {"development", "validation"}
        assert case["source_sha256"] == hashlib.sha256(case["text"].encode()).hexdigest()
        assert set(case["goals"].values()) <= {"include", "exclude", "abstain"}
        local_claims = {c["claim_id"] for c in case.get("logic", [])}
        assert not claims.intersection(local_claims), "claim IDs must be globally unique"
        claims.update(local_claims)
        for claim in case.get("logic", []):
            assessment = claim["assessment"]
            assert assessment["evidence_class"] == "observed"
            for support in assessment["basis"]["support"]:
                start, end = support["locator"]["byte_range"]
                assert case["text"].encode()[start:end].decode() == support["quote"]
                assert support["locator"]["source_id"] == case["id"]
        for candidate in case.get("conclusions", []):
            assert set(candidate["premise_claim_ids"]) <= local_claims
            assert candidate["status"] in {"entailed", "unsupported", "defeated", "underdetermined"}
        graph = case.get("structure", {})
        nodes = {n["id"] for n in graph.get("nodes", [])}
        assert len(nodes) == len(graph.get("nodes", []))
        for edge in graph.get("edges", []):
            assert {edge["source"], edge["target"]} <= nodes
    assert all(len(splits) == 1 for splits in groups.values()), "group crosses split"
    for pair in fixture["pairs"]:
        assert pair["a"] in cases and pair["b"] in cases
        assert cases[pair["a"]]["split"] == cases[pair["b"]]["split"] == pair["split"]
        assert pair["expected"] in {"equivalent", "non_equivalent", "underdetermined"}
    expected = fixture["frozen_labels_sha256"]
    assert expected == digest(label_payload(fixture)), "frozen labels changed"
    return {"cases": len(cases), "pairs": len(fixture["pairs"]),
            "splits": dict(Counter(c["split"] for c in cases.values())),
            "categories": dict(Counter(c["category"] for c in cases.values())),
            "labels_sha256": expected}


def label_payload(fixture: dict) -> dict:
    # Freeze source prose AND annotations. A changed formula must not silently
    # keep the same nominal label hash and appear to be the same benchmark.
    return {"cases": fixture["cases"], "pairs": fixture["pairs"]}


def materialize(fixture: dict, output: Path, split: str = "all") -> dict:
    """Source-only ChatGPT export. Never scan cases.json as conversation data."""
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    selected = [c for c in fixture["cases"] if split == "all" or c["split"] == split]
    for index, case in enumerate(selected):
        # Dates are controlled within category. Only explicit same-time cases
        # share a timestamp; proximity never changes their gold label.
        timestamp = case.get("timestamp", 1750000000 + index * 86400)
        rows.append({"id": case["id"], "title": case.get("title", "Discussion"),
                     "create_time": timestamp, "current_node": "message",
                     "mapping": {"message": {"id": "message", "parent": None, "children": [],
                        "message": {"id": "message", "author": {"role": "user"},
                            "content": {"content_type": "text", "parts": [case["text"]]},
                            "create_time": timestamp, "metadata": {}}}}})
    path = output / "conversations.json"
    path.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n")
    return {"path": str(path), "cases": len(rows), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def confusion(rows: list[tuple[dict, dict]], goal: str) -> dict:
    count = Counter()
    failures = []
    for case, predicted in rows:
        gold = case["goals"][goal]
        if gold == "abstain":
            count["gold_abstain"] += 1
            continue
        count["labeled_positive" if gold == "include" else "labeled_negative"] += 1
        decision = predicted.get("selected")
        if not isinstance(decision, bool):
            count["prediction_missing_or_abstain"] += 1
            continue
        key = ("t" if decision == (gold == "include") else "f") + ("p" if decision else "n")
        count[key] += 1
        if key in {"fp", "fn"}:
            failures.append({"id": case["id"], "gold": gold, "selected": decision,
                             "score": predicted.get("score"), "features": predicted.get("features", {}),
                             "reasons": predicted.get("reasons", []), "rationale": case["labels"]["rationale"]})
    tp, fp, fn, tn = (count[x] for x in ("tp", "fp", "fn", "tn"))
    return {**{x: count[x] for x in ("tp", "fp", "fn", "tn", "gold_abstain", "prediction_missing_or_abstain")},
            "precision": tp / (tp + fp) if tp + fp else None,
            "recall": tp / count["labeled_positive"] if count["labeled_positive"] else None,
            "conditional_recall": tp / (tp + fn) if tp + fn else None,
            "labeled_positive": count["labeled_positive"], "labeled_negative": count["labeled_negative"],
            "decision_coverage": (tp + fp + fn + tn) / (count["labeled_positive"] + count["labeled_negative"])
                if count["labeled_positive"] + count["labeled_negative"] else None,
            "evaluated": tp + fp + fn + tn, "failures": failures}


def evaluate(fixture: dict, predictions: dict) -> dict:
    """Evaluate decisions only for the declared goal; never collapse axes.

    predictions: {goal, cases:[{id,selected,score,features,reasons}],
                  pairs:[{id,scores:{method:number}}], conclusions:[...]}.
    No numeric similarity threshold is fitted to the validation set.
    """
    goal = predictions.get("goal", "self_project")
    by_id = {p["id"]: p for p in predictions.get("cases", [])}
    known = {c["id"] for c in fixture["cases"]}
    if set(by_id) - known:
        raise ValueError("predictions contain unknown case IDs")
    report: dict[str, Any] = {"schema": "loom.independent_report/1", "goal": goal,
        "fixture_labels_sha256": fixture["frozen_labels_sha256"],
        "method": predictions.get("method", "unspecified"), "split_reports": {},
        "limitations": ["Independent synthetic diagnostics, not owner-export accuracy.",
                         "Formal proof results are conditional on gold annotation; extraction is separate.",
                         "Missing decisions are counted and reduce overall recall; conditional recall is reported separately. Ambiguous gold is excluded and counted."]}
    for split in ("development", "validation"):
        rows = [(c, by_id.get(c["id"], {})) for c in fixture["cases"] if c["split"] == split]
        categories = sorted({c["category"] for c, _ in rows})
        report["split_reports"][split] = {"overall": confusion(rows, goal),
            "categories": {cat: confusion([(c, p) for c, p in rows if c["category"] == cat], goal) for cat in categories}}
    # Paired contrast ordering avoids selecting a cutoff after seeing validation.
    predicted_pairs = {p["id"]: p for p in predictions.get("pairs", [])}
    comparisons = defaultdict(dict)
    for pair in fixture["pairs"]:
        if pair["expected"] == "underdetermined":
            continue
        p = predicted_pairs.get(pair["id"], {})
        comparisons[(pair["split"], pair["group"])][pair["expected"]] = (pair, p)
    ordering = []
    for (split, group), group_pairs in sorted(comparisons.items()):
        if set(group_pairs) != {"equivalent", "non_equivalent"}:
            continue
        positive, negative = group_pairs["equivalent"][1], group_pairs["non_equivalent"][1]
        for method in sorted(set(positive.get("scores", {})) & set(negative.get("scores", {}))):
            yes, no = positive["scores"][method], negative["scores"][method]
            available = isinstance(yes, (int, float)) and isinstance(no, (int, float))
            ordering.append({"split": split, "group": group, "method": method,
                "equivalent_score": yes, "contrast_score": no, "margin": yes - no if available else None,
                "correct": yes > no if available else None, "tie": yes == no if available else False,
                "available": available})
    report["structural_pair_ordering"] = ordering
    report["prediction_counts"] = {"cases": len(by_id), "pairs": len(predicted_pairs), "conclusions": len(predictions.get("conclusions", []))}
    return report


def run_structure(fixture: dict, implementation: Path) -> dict:
    """Execute a frozen method on gold inputs, without exposing labels to it."""
    spec = importlib.util.spec_from_file_location("independent_structure_method", implementation)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    by_id = {c["id"]: c for c in fixture["cases"]}
    predictions: dict[str, Any] = {"goal": "conceptual_structure", "cases": [], "pairs": [], "conclusions": [],
        "method": {"path": str(implementation), "sha256": hashlib.sha256(implementation.read_bytes()).hexdigest()}}
    for pair in fixture["pairs"]:
        if pair["expected"] == "underdetermined":
            continue
        # Labels and candidate conclusions are deliberately not method inputs.
        inputs = [{k: by_id[ident][k] for k in ("text", "logic", "structure") if k in by_id[ident]}
                  for ident in (pair["a"], pair["b"])]
        result = module.compare(*inputs)
        scores = {key: result[key] for key in ("lexical_cosine", "role_relation_cosine", "wl_cosine")}
        for key in ("alignment", "semantic_alignment"):
            if isinstance(result.get(key), dict) and isinstance(result[key].get("score"), (int, float)):
                scores[key] = result[key]["score"]
        predictions["pairs"].append({"id": pair["id"], "scores": scores, "details": result})
    proof_cases = []
    for case in fixture["cases"]:
        if not case.get("logic"):
            continue
        result = module.infer(case["logic"])
        formulas = {canonical(item["formula"]): item for item in result["candidates"]}
        expected_formulas = {canonical(item["formula"]) for item in case["conclusions"]}
        for gold in case["conclusions"]:
            proposed = formulas.get(canonical(gold["formula"]))
            predictions["conclusions"].append({"id": gold["id"], "case_id": case["id"],
                "split": case["split"], "category": case["category"], "gold": gold["status"],
                "proposed": proposed is not None, "proposal": proposed,
                "correct": (proposed is not None) == (gold["status"] == "entailed")})
        proof_cases.append({"id": case["id"], "split": case["split"], "coverage": result["coverage"],
            "blocked": result["blocked"], "unscored_extra_candidates": [x for x in result["candidates"]
                if canonical(x["formula"]) not in expected_formulas]})
    report = evaluate(fixture, predictions)
    report.pop("split_reports")  # This run does not execute a retrieval policy.
    report["selection_evaluation"] = "not_run"
    report["source_extraction"] = {"status": "not_implemented_in_evaluated_method", "evaluated_cases": 0,
        "gold_formula_cases": len(proof_cases), "interpretation": "This is not end-to-end natural-language reasoning accuracy."}
    report["proof_cases"] = proof_cases
    report["conclusion_results"] = predictions["conclusions"]
    report["method_predictions"] = predictions["pairs"]
    report["structural_summary"] = {}
    for split in ("development", "validation"):
        rows = [r for r in report["structural_pair_ordering"] if r["split"] == split]
        summary = {}
        for method in sorted({r["method"] for r in rows}):
            subset = [r for r in rows if r["method"] == method]
            summary[method] = {"correct": sum(r["correct"] for r in subset), "ties": sum(r["tie"] for r in subset),
                               "groups": len(subset)}
        report["structural_summary"][split] = summary
    report["proof_summary"] = {}
    for split in ("development", "validation"):
        rows = [r for r in predictions["conclusions"] if r["split"] == split]
        positive = [r for r in rows if r["gold"] == "entailed"]
        negative = [r for r in rows if r["gold"] != "entailed"]
        report["proof_summary"][split] = {"entailed_proposed": sum(r["proposed"] for r in positive),
            "entailed_total": len(positive), "unsupported_proposed": sum(r["proposed"] for r in negative),
            "unsupported_total": len(negative), "failures": [r for r in rows if not r["correct"]]}
    return report


def load_method(implementation: Path):
    sys.path.insert(0, str(implementation.resolve().parent))
    spec = importlib.util.spec_from_file_location("independent_" + implementation.stem, implementation)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def formula_signature(formula: dict) -> bytes:
    """Gold has no class/property prefix; normalize naming only, not meaning."""
    def walk(f: dict, bound: tuple = ()):
        op = f["op"]
        if op == "atom":
            predicate = f["predicate"].casefold()
            if predicate.startswith(("property:", "class:")):
                predicate = predicate.split(":", 1)[1]
            return [op, predicate, [bound.index(a) if a in bound else a.casefold() for a in f["args"]]]
        if op in {"forall", "exists"}:
            return [op, walk(f["body"], bound + (f["var"],))]
        if op in {"not", "modal"}:
            return [op, f.get("mode"), walk(f["arg"], bound)]
        if op in {"and", "or"}:
            return [op, sorted([walk(a, bound) for a in f["args"]], key=canonical)]
        return [op, walk(f["left"], bound), walk(f["right"], bound)]
    return canonical(walk(formula))


def run_extraction(fixture: dict, implementation: Path) -> dict:
    module = load_method(implementation)
    extractions = {}
    case_results = []
    for case in fixture["cases"]:
        result = module.extract_record({"id": case["id"], "source_id": case["id"], "text": case["text"]})
        extractions[case["id"]] = result
        gold = {formula_signature(c["formula"]): c for c in case.get("logic", [])}
        proposals = [p for p in result["candidates"] if p.get("formula_candidate") is not None]
        matching = [p for p in proposals if formula_signature(p["formula_candidate"]) in gold]
        spans = [item["span"] for item in result["candidates"] + result["unknown"]]
        exact_spans = sum(case["text"].encode()[s["byte_start"]:s["byte_end"]].decode() == s["quote"] for s in spans)
        case_results.append({"id": case["id"], "split": case["split"], "category": case["category"],
            "status": result["status"], "coverage": result["coverage"], "gold_formulas": len(gold),
            "exact_formula_matches": len({formula_signature(p["formula_candidate"]) for p in matching}),
            "formula_proposals": len(proposals), "unmatched_formula_proposals": [p for p in proposals if p not in matching],
            "source_spans": len(spans), "verified_source_spans": exact_spans, "unknown": result["unknown"]})
    pair_predictions = []
    for pair in fixture["pairs"]:
        if pair["expected"] == "underdetermined":
            continue
        scores, details = {}, {}
        for projection in ("operations", "logical_candidates"):
            result = module.compare_extractions(extractions[pair["a"]], extractions[pair["b"]], projection=projection)
            details[projection] = result
            for method in ("role_relation_cosine", "wl_cosine"):
                scores[projection + ":" + method] = result[method]
            scores[projection + ":alignment"] = result["alignment"]["score"]
        pair_predictions.append({"id": pair["id"], "scores": scores, "details": details})
    report = evaluate(fixture, {"goal": "conceptual_structure", "pairs": pair_predictions})
    report.pop("split_reports")
    report["selection_evaluation"] = "not_run"
    report["method"] = {"path": str(implementation), "sha256": hashlib.sha256(implementation.read_bytes()).hexdigest()}
    report["case_results"] = case_results
    report["case_extractions"] = list(extractions.values())
    report["pair_predictions"] = pair_predictions
    report["summaries"] = {}
    for split in ("development", "validation"):
        groups = {"all": [r for r in case_results if r["split"] == split]}
        groups.update({cat: [r for r in case_results if r["split"] == split and r["category"] == cat]
                       for cat in sorted({r["category"] for r in case_results})})
        report["summaries"][split] = {cat: {"cases": len(rows),
            "statuses": dict(Counter(r["status"] for r in rows)),
            "recognized_envelopes": sum(r["coverage"]["recognized_envelopes"] for r in rows),
            "physical_units": sum(r["coverage"]["physical_units"] for r in rows),
            "gold_formulas": sum(r["gold_formulas"] for r in rows),
            "exact_formula_matches": sum(r["exact_formula_matches"] for r in rows),
            "formula_proposals": sum(r["formula_proposals"] for r in rows),
            "source_spans": sum(r["source_spans"] for r in rows),
            "verified_source_spans": sum(r["verified_source_spans"] for r in rows)} for cat, rows in groups.items()}
    report["interpretation"] = ["Actual source text was parsed; supplied gold formulas/labels were not parser inputs.",
        "Formula equality normalizes case, bound variables, documented property:/class: prefixes only; class/property typing is not scored.",
        "Unmatched proposals outside the formal-gold subset are unscored, not automatically false positives.",
        "Grammar-envelope recognition is not semantic correctness or calibrated confidence.",
        "No extracted candidate was promoted to an eligible proof premise; end-to-end proof validity is not measured."]
    return report


def run_topics(fixture: dict, implementation: Path, expectations: Path) -> dict:
    module = load_method(implementation)
    gold = json.loads(expectations.read_text())
    assert gold["fixture_hash"] == fixture["frozen_labels_sha256"]
    payload = {k: v for k, v in gold.items() if k != "sha256"}
    assert digest(payload) == gold["sha256"]
    by_id = {c["id"]: c for c in fixture["cases"]}
    rows, outputs = [], []
    for expected in gold["cases"]:
        case = by_id[expected["case_id"]]
        input_record = {k: case[k] for k in ("id", "turns", "entities")}
        result = module.analyze(input_record)
        outputs.append({"case_id": case["id"], "result": result})
        turns = {t["id"]: t["text"] for t in case["turns"]}
        verified = sum(turns[o["source"]["turn_id"]].encode()[o["source"]["byte_start"]:o["source"]["byte_end"]].decode() == o["text"] for o in result["observations"])
        for span in expected["expected_spans"]:
            observations = [o for o in result["observations"] if o["source"]["turn_id"] == span["turn_id"]
                and o["source"]["char_start"] < span["char_end"] and o["source"]["char_end"] > span["char_start"]]
            focus = sorted({p for o in observations for p in o["focus_entity_ids"]})
            extra = sorted(set(focus) - set(span["focus_entity_ids"]))
            missing = sorted(set(span["focus_entity_ids"]) - set(focus))
            boundaries = {v for o in observations for v in (o["source"]["char_start"], o["source"]["char_end"])}
            rows.append({"id": span["id"], "case_id": case["id"], "split": case["split"],
                "expected_focus": span["focus_entity_ids"], "predicted_focus": focus, "extra_focus": extra,
                "missing_focus": missing, "focus_correct": not extra and not missing,
                "gold_boundaries_present": span["char_start"] in boundaries and span["char_end"] in boundaries,
                "observation_ids": [o["id"] for o in observations]})
        outputs[-1]["verified_source_spans"] = verified
        outputs[-1]["source_spans"] = len(result["observations"])
    report = {"schema": "loom.independent_topics_report/1", "fixture_hash": fixture["frozen_labels_sha256"],
        "expectations_hash": gold["sha256"], "method": {"path": str(implementation),
            "sha256": hashlib.sha256(implementation.read_bytes()).hexdigest()}, "span_results": rows, "case_results": outputs,
        "interpretation": ["Actual source turns and entity aliases were inputs; labels and source expectations were withheld.",
            "Local focus and segmentation are measured independently of whole-conversation project membership.",
            "Overlapping observations with extra focus are counted as errors even if another span correctly names the entity.",
            "This is an eight-conversation synthetic diagnostic, not general reference-resolution accuracy."]}
    report["summaries"] = {split: {"spans": len(subset), "focus_correct": sum(r["focus_correct"] for r in subset),
        "extra_focus_spans": sum(bool(r["extra_focus"]) for r in subset), "missing_focus_spans": sum(bool(r["missing_focus"]) for r in subset),
        "gold_boundaries_present": sum(r["gold_boundaries_present"] for r in subset)}
        for split in ("development", "validation") for subset in [[r for r in rows if r["split"] == split]]}
    return report


def run_catalog(fixture: dict, library: Path) -> dict:
    """Production baseline through public catalog C ABI; no import/model calls."""
    lib = ctypes.CDLL(str(library.resolve()))
    pointer, string = ctypes.c_void_p, ctypes.c_char_p
    lib.loom_init_ex.argtypes = [string, ctypes.POINTER(pointer)]
    lib.loom_init_ex.restype = pointer
    lib.loom_shutdown.argtypes = [pointer]
    lib.loom_free_string.argtypes = [pointer]
    lib.loom_set_log_stderr.argtypes = [ctypes.c_int]
    lib.loom_set_log_stderr(0)
    for name in ("loom_catalog_scan", "loom_catalog_score"):
        getattr(lib, name).argtypes = [pointer, string, pointer, pointer]
        getattr(lib, name).restype = pointer
    for name in ("loom_catalog_select", "loom_catalog_query", "loom_catalog_preview"):
        getattr(lib, name).argtypes = [pointer, string]
        getattr(lib, name).restype = pointer

    def decode(ptr):
        if not ptr:
            raise RuntimeError("C ABI returned null")
        try:
            value = json.loads(ctypes.string_at(ptr).decode())
        finally:
            lib.loom_free_string(ptr)
        if isinstance(value, dict) and set(value) == {"error"}:
            raise RuntimeError(value["error"])
        return value

    def call(name, ctx, argument, progress=False):
        data = argument.encode() if isinstance(argument, str) else canonical(argument)
        args = [ctx, data] + ([None, None] if progress else [])
        return decode(getattr(lib, name)(*args))

    results, runs = [], []
    for split in ("development", "validation"):
        with tempfile.TemporaryDirectory(prefix="loom-independent-") as temporary:
            root = Path(temporary)
            exported = materialize(fixture, root / "source", split)
            error = pointer()
            ctx = lib.loom_init_ex(canonical({"data_dir": str(root / "runtime"), "start_workers": False}), ctypes.byref(error))
            if not ctx:
                raise RuntimeError(decode(error.value) if error.value else "runtime initialization failed")
            try:
                scanned = call("loom_catalog_scan", ctx, {"sources": [exported["path"]], "threads": 1}, True)
                scored = call("loom_catalog_score", ctx, {"llm": "off"}, True)
                decisions = call("loom_catalog_select", ctx, scored["run_id"])["decisions"]
                by_unit = {d["unit_id"]: d for d in decisions}
                units = call("loom_catalog_query", ctx, {"limit": len(fixture["cases"]) + 10})
                for unit in units:
                    uid = unit["unit"]["id"]
                    preview = call("loom_catalog_preview", ctx, uid)
                    decision = by_unit[uid]
                    score = preview["score"]
                    results.append({"id": unit["ext_id"], "selected": decision["selected"],
                        "score": decision["score"], "features": score.get("features", {}),
                        "reasons": {"selection": decision.get("reasons", []), "score": score.get("reasons", [])},
                        "decided_by": decision["decided_by"], "label": decision["label"],
                        "unit_id": uid, "split": split})
                runs.append({"split": split, "source_sha256": exported["sha256"], "scan": scanned,
                             "score": scored, "units": len(units), "decisions": len(decisions)})
            finally:
                lib.loom_shutdown(ctx)
    goals = {g: evaluate(fixture, {"goal": g, "cases": results, "method": "production_default_catalog"})
             for g in ("self_project", "self_discovery", "owner_philosophy", "conceptual_structure")}
    data = DEFAULT_FIXTURE.parents[4] / "data"
    pack_files = {str(p.relative_to(data)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(data.rglob("*")) if p.is_file()} if data.is_dir() else {}
    return {"schema": "loom.independent_catalog_report/1", "fixture_hash": fixture["frozen_labels_sha256"],
        "method": {"library": str(library), "sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
            "native_baseline": "33fb083", "policy": "unchanged production default", "llm": "off"},
        "data_file_manifest_sha256": digest(pack_files), "data_file_manifest": pack_files,
        "runs": runs, "decisions": results, "goal_reports": goals,
        "limitations": ["This corpus is balanced synthetic diagnostics, not original synthetic_dev or owner exports.",
            "The production policy is not conditioned on these four goals; comparisons expose scope mismatch, not four separate tuned selectors.",
            "Whole-source scan/score/select only; no data import, network, model inference or runtime mutation beyond disposable catalog state."]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("validate")
    generate = sub.add_parser("materialize")
    generate.add_argument("--output", type=Path, required=True)
    generate.add_argument("--split", choices=("all", "development", "validation"), default="all")
    score = sub.add_parser("evaluate")
    score.add_argument("--predictions", type=Path, required=True)
    score.add_argument("--output", type=Path, required=True)
    structure = sub.add_parser("run-structure")
    structure.add_argument("--implementation", type=Path, required=True)
    structure.add_argument("--output", type=Path, required=True)
    extraction = sub.add_parser("run-extraction")
    extraction.add_argument("--implementation", type=Path, required=True)
    extraction.add_argument("--output", type=Path, required=True)
    topics = sub.add_parser("run-topics")
    topics.add_argument("--implementation", type=Path, required=True)
    topics.add_argument("--expectations", type=Path, required=True)
    topics.add_argument("--output", type=Path, required=True)
    catalog = sub.add_parser("run-catalog")
    catalog.add_argument("--library", type=Path, required=True)
    catalog.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    fixture = load_fixture(args.fixture)
    if args.command == "validate":
        result = validate(fixture)
    elif args.command == "materialize":
        result = materialize(fixture, args.output, args.split)
    else:
        if args.command == "run-structure":
            result = run_structure(fixture, args.implementation)
        elif args.command == "run-extraction":
            result = run_extraction(fixture, args.implementation)
        elif args.command == "run-topics":
            result = run_topics(fixture, args.implementation, args.expectations)
        elif args.command == "run-catalog":
            result = run_catalog(fixture, args.library)
        else:
            result = evaluate(fixture, json.loads(args.predictions.read_text()))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
        result = {"path": str(args.output), "fixture_labels_sha256": fixture["frozen_labels_sha256"]}
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
