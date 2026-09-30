"""Offline size and cost estimate for reading an imported archive with a model.

Reads a Loom/ChatADHD data directory's SQLite database (after `loom import`),
counts the text a model would have to read per conversation, and estimates the
cost of one reading pass per model from a pricing table. No network, no model
calls, no writes: the owner can run it on a private export and nothing leaves
the machine.

Token counts are ESTIMATES from character counts (a range: ~3 chars/token for
Polish-heavy text, ~4 for English). Exact counts need a tokenizer or the
provider's count_tokens endpoint; the cost figures are planning numbers only.

    python3 loom/tools/eval/archive_cost.py --db ~/.chatadhd/chatadhd.db
    python3 loom/tools/eval/archive_cost.py --db DB --fraction 0.1 --json
"""
from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

PRICING = Path(__file__).with_name("pricing_2026-09-25.json")
CHARS_PER_TOKEN = {"low": 4.0, "high": 3.0}  # low/high token estimate


def archive_stats(db_path: str | Path) -> dict:
    """Character counts per conversation for active messages and kept versions (branches).

    Deleted messages are excluded; excluded ones are counted separately (the
    owner hid them, a reader may still be asked to skip them).
    """
    con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    try:
        rows = con.execute(
            "SELECT conv_id, role, status, length(COALESCE(text, '')) FROM messages WHERE status != 'deleted'"
        ).fetchall()
    finally:
        con.close()
    convs: dict[str, dict] = {}
    totals = {"messages": 0, "chars_active": 0, "chars_versions": 0, "chars_excluded": 0}
    by_role: dict[str, int] = {}
    for conv_id, role, status, n in rows:
        c = convs.setdefault(conv_id, {"messages": 0, "chars": 0})
        totals["messages"] += 1
        c["messages"] += 1
        if status == "version":
            totals["chars_versions"] += n
        elif status == "excluded":
            totals["chars_excluded"] += n
        else:
            totals["chars_active"] += n
            by_role[role] = by_role.get(role, 0) + n
        if status != "excluded":
            c["chars"] += n
    sizes = sorted(c["chars"] for c in convs.values())
    return {
        "conversations": len(convs),
        **totals,
        "chars_by_role": dict(sorted(by_role.items())),
        "conversation_chars": {
            "median": sizes[len(sizes) // 2] if sizes else 0,
            "max": sizes[-1] if sizes else 0,
        },
    }


def estimate(stats: dict, pricing: dict, *, fraction: float = 1.0, output_ratio: float = 0.4,
             prefix_tokens: int = 8000, include_versions: bool = True) -> dict:
    """One reading pass: conversation text as input, output_ratio x input as output
    (thinking is billed as output), plus a cached definitions prefix per conversation."""
    if not 0 < fraction <= 1:
        raise ValueError("fraction must be in (0, 1]")
    chars = stats["chars_active"] + (stats["chars_versions"] if include_versions else 0)
    n_conv = stats["conversations"] * fraction
    out = {"assumptions": {"fraction": fraction, "output_ratio": output_ratio, "prefix_tokens": prefix_tokens,
                           "include_versions": include_versions, "chars_per_token": CHARS_PER_TOKEN,
                           "pricing_as_of": pricing.get("as_of")},
           "tokens": {}, "models": {}}
    for band, cpt in CHARS_PER_TOKEN.items():
        out["tokens"][band] = round(chars * fraction / cpt)
    for model, p in pricing["models"].items():
        row = {}
        for band, tok in out["tokens"].items():
            usd = (tok * p["input"] + tok * output_ratio * p["output"] + n_conv * prefix_tokens * p["cache_read"]) / 1e6
            row[band] = round(usd, 2)
            row[band + "_batch"] = round(usd * pricing.get("batch_discount", 1.0), 2)
        out["models"][model] = row
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--db", required=True, help="path to chatadhd.db (opened read-only)")
    ap.add_argument("--pricing", default=str(PRICING))
    ap.add_argument("--fraction", type=float, default=1.0, help="share of conversations read by the model")
    ap.add_argument("--output-ratio", type=float, default=0.4)
    ap.add_argument("--prefix-tokens", type=int, default=8000)
    ap.add_argument("--no-versions", action="store_true", help="skip kept older branches")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    stats = archive_stats(a.db)
    pricing = json.loads(Path(a.pricing).read_text(encoding="utf-8"))
    est = estimate(stats, pricing, fraction=a.fraction, output_ratio=a.output_ratio,
                   prefix_tokens=a.prefix_tokens, include_versions=not a.no_versions)
    if a.json:
        json.dump({"stats": stats, "estimate": est}, sys.stdout, ensure_ascii=False, indent=2)
        print()
        return 0
    print(f"{stats['conversations']} conversations, {stats['messages']} messages; chars active "
          f"{stats['chars_active']:,}, older versions {stats['chars_versions']:,}, excluded {stats['chars_excluded']:,}")
    print(f"estimated tokens read (fraction {a.fraction}): {est['tokens']['low']:,} - {est['tokens']['high']:,}")
    print(f"USD per reading pass (low - high; batch in brackets), prices as of {pricing.get('as_of')}:")
    for model, r in est["models"].items():
        print(f"  {model:20s} {r['low']:>10,.2f} - {r['high']:>10,.2f}   [{r['low_batch']:,.2f} - {r['high_batch']:,.2f}]")
    print("Estimates only: token counts from characters; verify prices before spending.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
