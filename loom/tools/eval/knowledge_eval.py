#!/usr/bin/env python3
"""Evaluation harness of Loom's knowledge layer (LOOM_CONCEPTUAL_MODEL §7).

  synthetic   end-to-end scorecard on tests/fixtures/eval/synthetic_dev
              (fictional persona, exact ground truth), optional floors gate
  holdout     temporal holdout on the REAL sources of this repository: stage
              everything dated <= T, run the pipeline with prior_cut = T and
              write predictions JSON (format: tools/eval/README.md). It never
              reads an answer key; the lead scores the file separately.
  selfhost    the full real self-discovery run (no cut) -> products directory

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

from kbeval import realrun, synthetic  # noqa: E402

LOOM_ROOT = HERE.parent.parent


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("synthetic", help="synthetic_dev scorecard")
    s.add_argument("--loom", required=True)
    s.add_argument("--work", help="work directory (default: a temp dir)")
    s.add_argument("--out", help="scorecard JSON path")
    s.add_argument("--markdown", help="scorecard markdown path")
    s.add_argument("--floors", help="floors JSON: fail (exit 1) when a metric is below its floor")

    h = sub.add_parser("holdout", help="temporal holdout predictions on the real repository")
    h.add_argument("--loom", required=True)
    h.add_argument("--repo", required=True, help="the chatadhd repository root")
    h.add_argument("--cut", required=True, action="append", help="YYYY-MM-DD (repeatable)")
    h.add_argument("--work", help="work directory (default: a temp dir)")
    h.add_argument("--out", required=True, help="predictions JSON (one cut) or a directory (several cuts)")
    h.add_argument("--products", help="also keep the materialized products of each cut here")

    r = sub.add_parser("selfhost", help="full real self-discovery run")
    r.add_argument("--loom", required=True)
    r.add_argument("--repo", required=True)
    r.add_argument("--work", help="work directory (default: a temp dir)")
    r.add_argument("--out", required=True, help="products directory")

    a = ap.parse_args(argv)
    work = Path(a.work) if a.work else Path(tempfile.mkdtemp(prefix="loom-kbeval-"))

    if a.cmd == "synthetic":
        card = synthetic.evaluate(a.loom, LOOM_ROOT, work)
        text = json.dumps(card, ensure_ascii=False, indent=1)
        if a.out:
            Path(a.out).write_text(text + "\n", encoding="utf-8")
        if a.markdown:
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
        for cut in a.cut:
            dest = out / f"cut_{cut}.json" if many else out
            dest.parent.mkdir(parents=True, exist_ok=True)
            prod = Path(a.products) / f"cut_{cut}" if a.products else None
            preds = realrun.holdout(a.loom, Path(a.repo).resolve(), cut, work / f"cut_{cut}", prod)
            dest.write_text(json.dumps(preds, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
            print(f"cut {cut}: {len(preds['predictions'])} predictions -> {dest}")
        return 0

    if a.cmd == "selfhost":
        res = realrun.selfhost(a.loom, Path(a.repo).resolve(), work, Path(a.out))
        print(json.dumps({k: res[k] for k in ("run", "status")}, indent=1))
        for s in res["stages"]:
            print(s["stage"], json.dumps(s["stats"], ensure_ascii=False)[:300])
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
