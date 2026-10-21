"""Work blocks: stretches of the time axis where the user switched between several conversations back and forth.

Input: the *runs* of one stretch of the timeline (a run = consecutive messages of one conversation, in time order).
A switch is a change of conversation between two neighbouring runs.  A work block is a maximal chain of runs in which
the switches are more than ``min_switches`` (preset ``timeline.work_block``; the default 3 means "more than three").
A run belongs to the chain when its conversation comes back in the chain (>= 2 runs) -- a one-off run sandwiched between
two such runs stays inside (``sandwich``), so a short detour does not cut real parallel work in two.  ``max_gap_minutes``
(0 = off) cuts the chain where the pause between two runs is longer.  Every number is a setting, none is a rule.
"""
from datetime import datetime
from typing import Any, Optional

DEFAULTS = {"enabled": True, "min_switches": 3, "sandwich": True, "max_gap_minutes": 0}


def _t(s: Optional[str]) -> Optional[datetime]:
    try:
        return datetime.fromisoformat(str(s).replace("Z", "+00:00")) if s else None
    except ValueError:
        return None


def find_work_blocks(runs: list[dict[str, Any]], cfg: Optional[dict[str, Any]] = None) -> list[dict[str, Any]]:
    """runs: dicts with ``conversation``, ``first``, ``last`` (ISO).  Returns [{"start", "end", "switches", "conversations"}]
    as index ranges (start..end inclusive) into ``runs``."""
    c = dict(DEFAULTS, **(cfg or {}))
    n = len(runs)
    if not c["enabled"] or n < 2:
        return []
    # connected pieces: cut at long pauses
    cuts = [0]
    gap = float(c["max_gap_minutes"] or 0)
    for i in range(1, n):
        a, b = _t(runs[i - 1].get("last")), _t(runs[i].get("first"))
        if gap > 0 and a is not None and b is not None and (b - a).total_seconds() / 60.0 > gap:
            cuts.append(i)
    cuts.append(n)
    out: list[dict[str, Any]] = []
    for lo, hi in zip(cuts, cuts[1:]):
        seg = runs[lo:hi]
        count: dict[str, int] = {}
        for r in seg:
            count[r["conversation"]] = count.get(r["conversation"], 0) + 1
        member = [count[r["conversation"]] >= 2 for r in seg]
        if c["sandwich"]:
            for i in range(1, len(seg) - 1):
                if not member[i] and member[i - 1] and member[i + 1]:
                    member[i] = True
        i = 0
        while i < len(seg):
            if not member[i]:
                i += 1
                continue
            j = i
            while j + 1 < len(seg) and member[j + 1]:
                j += 1
            switches = sum(1 for k in range(i + 1, j + 1) if seg[k]["conversation"] != seg[k - 1]["conversation"])
            if switches > int(c["min_switches"]):
                out.append({"start": lo + i, "end": lo + j, "switches": switches,
                            "conversations": len({seg[k]["conversation"] for k in range(i, j + 1)})})
            i = j + 1
    return out
