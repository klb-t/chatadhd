#!/usr/bin/env python3
"""Evaluation harness of Loom's knowledge layer (LOOM_CONCEPTUAL_MODEL §7).

  synthetic   end-to-end scorecard on tests/fixtures/eval/synthetic_dev
              (fictional persona, exact ground truth), optional floors gate
  holdout     stage a verifiable historical Git snapshot and emit its manifest.
              Strict prediction is currently unavailable because the runner
              loads a present-day embedded policy pack. Exits 2, never scores
              or reads a real answer key (see tools/eval/README.md).
  selfhost    discovery over sanitized tracked HEAD sources -> products directory

Examples:
  python3 tools/eval/knowledge_eval.py synthetic --loom build/dev/cli/loom --out /tmp/card.json \
      --floors tools/eval/synthetic_floors.json
  python3 tools/eval/knowledge_eval.py holdout --loom build/dev/cli/loom --repo .. --cut 2026-03-06 \
      --out ../docs/selfhost/v2/predictions/cut_2026-03-06.json
"""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from kbeval import bench, realrun, synthetic  # noqa: E402

LOOM_ROOT = HERE.parent.parent


def parse_cut(value: str) -> str:
    try:
        realrun.cutoff_timestamp(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    return value


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")


def bench_main(a) -> int:
    if a.bcmd == "stage":
        print(json.dumps(bench.stage(Path(a.repo).resolve(), Path(a.dest).resolve())))
        return 0
    if a.bcmd == "compare":
        ra = json.loads(Path(a.a).read_text(encoding="utf-8"))
        rb = json.loads(Path(a.b).read_text(encoding="utf-8"))
        print(bench.compare(ra, rb))
        return 0 if all(v is True for k, v in bench.identical(ra, rb).items() if isinstance(v, bool)) else 1
    if a.synthetic:
        gt = json.loads((synthetic.gt_dir(LOOM_ROOT) / "ground_truth.json").read_text(encoding="utf-8"))
        cfg = synthetic.base_config(LOOM_ROOT, gt)
        if a.import_mode:
            cfg["stage_params"]["catalog"]["import"] = {"mode": a.import_mode}
    else:
        cfg = {"sources": [str(Path(a.input).resolve())]}
    rep = bench.run(a.loom, cfg, Path(a.work), a.label)
    write_json(Path(a.out), rep)
    print(json.dumps({k: rep[k] for k in ("label", "wall_s", "timers_ms", "counters", "stage_hashes")}, indent=1))
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("synthetic", help="synthetic_dev scorecard")
    s.add_argument("--loom", required=True)
    s.add_argument("--work", help="work directory (default: a temp dir)")
    s.add_argument("--out", help="scorecard JSON path")
    s.add_argument("--markdown", help="scorecard markdown path")
    s.add_argument("--floors", help="floors JSON: fail (exit 1) when a metric is below its floor")

    h = sub.add_parser("holdout", help="audit historical sources; report unavailable strict prediction")
    h.add_argument("--loom", required=True)
    h.add_argument("--repo", required=True, help="the chatadhd repository root")
    h.add_argument("--cut", required=True, action="append", type=parse_cut, help="YYYY-MM-DD, inclusive UTC (repeatable)")
    h.add_argument("--work", help="work directory (default: a temp dir)")
    h.add_argument("--out", required=True, help="predictions JSON (one cut) or a directory (several cuts)")
    h.add_argument("--products", help="also keep the materialized products of each cut here")

    r = sub.add_parser("selfhost", help="full real self-discovery run")
    r.add_argument("--loom", required=True)
    r.add_argument("--repo", required=True)
    r.add_argument("--work", help="work directory (default: a temp dir)")
    r.add_argument("--out", required=True, help="products directory")

    b = sub.add_parser("bench", help="timing + output-identity harness (see kbeval/bench.py)")
    bsub = b.add_subparsers(dest="bcmd", required=True)
    bs = bsub.add_parser("stage", help="freeze tracked-HEAD text sources of a repository into DEST")
    bs.add_argument("--repo", required=True)
    bs.add_argument("--dest", required=True)
    br = bsub.add_parser("run", help="run the pipeline over a frozen input and write a report")
    br.add_argument("--loom", required=True)
    br.add_argument("--input", help="frozen repository input directory (from `bench stage`)")
    br.add_argument("--synthetic", action="store_true", help="use synthetic_dev exports instead")
    br.add_argument("--import-mode", help="catalog import mode, e.g. full")
    br.add_argument("--work", required=True)
    br.add_argument("--label", default="run")
    br.add_argument("--out", required=True, help="report JSON")
    bc = bsub.add_parser("compare", help="compare two reports")
    bc.add_argument("a")
    bc.add_argument("b")

    s.add_argument("--import-mode", help="catalog import mode (e.g. full: extract every unit)")

    a = ap.parse_args(argv)
    if a.cmd == "bench":
        return bench_main(a)
    work = Path(a.work) if a.work else Path(tempfile.mkdtemp(prefix="loom-kbeval-"))

    if a.cmd == "synthetic":
        card = synthetic.evaluate(a.loom, LOOM_ROOT, work, import_mode=a.import_mode)
        if a.out:
            write_json(Path(a.out), card)
        if a.markdown:
            Path(a.markdown).parent.mkdir(parents=True, exist_ok=True)
            Path(a.markdown).write_text(synthetic.render_markdown(card), encoding="utf-8")
        print(synthetic.render_markdown(card))
        if a.floors:
            fails = synthetic.check_floors(card, json.loads(Path(a.floors).read_text(encoding="utf-8")))
            for f in fails:
                print("FLOOR FAILED:", f, file=sys.stderr)
            return 1 if fails else 0
        return 0

    if a.cmd == "holdout":
        out = Path(a.out)
        many = len(a.cut) > 1 or out.suffix != ".json"
        unavailable = False
        for cut in a.cut:
            dest = out / f"cut_{cut}.json" if many else out
            dest.parent.mkdir(parents=True, exist_ok=True)
            prod = Path(a.products) / f"cut_{cut}" if a.products else None
            try:
                preds = realrun.holdout(a.loom, Path(a.repo).resolve(), cut, work / f"cut_{cut}", prod)
            except realrun.EvaluationUnavailable as exc:
                preds = {"status": "unavailable", "protocol": "strict_temporal_holdout", "cut": cut,
                         "benchmark_valid": False, "predictions": [], "predictive_accuracy": None,
                         "reason": str(exc), "pipeline_executed": False}
            write_json(dest, preds)
            unavailable |= preds.get("status") != "done"
            print(f"cut {cut}: {preds.get('status')} -> {dest}")
            if preds.get("reason"):
                print(preds["reason"], file=sys.stderr)
        return 2 if unavailable else 0

    if a.cmd == "selfhost":
        res = realrun.selfhost(a.loom, Path(a.repo).resolve(), work, Path(a.out))
        print(json.dumps({k: res[k] for k in ("run", "status")}, indent=1))
        for s in res["stages"]:
            print(s["stage"], json.dumps(s["stats"], ensure_ascii=False)[:300])
        return 0
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError) as exc:
        print(f"evaluation failed: {exc}", file=sys.stderr)
        sys.exit(2)

