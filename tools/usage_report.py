#!/usr/bin/env python3
"""Na co poszły tokeny sesji: z zapisów (transcriptów) Claude Code, bez czytania treści - tylko pola `usage`.

  python3 tools/usage_report.py <katalog projektu z .jsonl> [--since 2026-10-21T00:00:00Z] [--until ...] [--top 12]
  np. python3 tools/usage_report.py /root/.claude/projects/-home-user-chatadhd --since 2026-10-21T00:00:00Z

Liczy sesję główną (<id>.jsonl) i agentów (<id>/subagents/**/agent-*.jsonl; workflow rozpoznany po wf_*.json). Jedna wiadomość
modelu bywa zapisana w kilku liniach (po jednej na blok treści) - liczona raz, po `message.id`.
WAGI to założenie (stosunki z cennika API, odczyt z cache = 1): wejście 10, zapis cache 5 min 12,5, zapis cache 1 h 20, wyjście 50.
Limit abonamentu nie jest publicznie równy cennikowi API, więc to RÓWNOWAŻNIKI do porównań (kto zużył ile względem innych),
nie punkty limitu. Nie widzi innych sesji konta (np. S2) ani czatów właściciela - uruchom osobno na ich katalogach.
"""
import argparse, collections, json, pathlib, sys
from datetime import datetime

W_IN, W_W5, W_W1, W_OUT, W_READ = 10.0, 12.5, 20.0, 50.0, 1.0


def ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


def read_file(path, since, until):
    """-> {message_id: (timestamp, model, usage)} dla wiadomości asystenta w oknie czasu."""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if '"usage"' not in line:
                continue
            try:
                d = json.loads(line)
            except ValueError:
                continue
            m = d.get("message")
            if d.get("type") != "assistant" or not isinstance(m, dict) or not isinstance(m.get("usage"), dict):
                continue
            t = d.get("timestamp")
            if not t:
                continue
            tt = ts(t)
            if (since and tt < since) or (until and tt >= until):
                continue
            mid = m.get("id") or d.get("uuid")
            prev = out.get(mid)
            if prev is None or (m["usage"].get("output_tokens") or 0) >= (prev[2].get("output_tokens") or 0):
                out[mid] = (tt, m.get("model") or "?", m["usage"])
    return out


def totals(msgs):
    a = collections.Counter()
    for _, model, u in msgs.values():
        cc = u.get("cache_creation") or {}
        w5 = cc.get("ephemeral_5m_input_tokens")
        w1 = cc.get("ephemeral_1h_input_tokens")
        if w5 is None and w1 is None:
            w5, w1 = u.get("cache_creation_input_tokens") or 0, 0
        a["turns"] += 1
        a["input"] += u.get("input_tokens") or 0
        a["read"] += u.get("cache_read_input_tokens") or 0
        a["w5"] += w5 or 0
        a["w1"] += w1 or 0
        a["output"] += u.get("output_tokens") or 0
    a["eq"] = a["input"] * W_IN + a["read"] * W_READ + a["w5"] * W_W5 + a["w1"] * W_W1 + a["output"] * W_OUT
    return a


def fmt(a):
    ctx = (a["read"] / a["turns"]) if a["turns"] else 0
    return (f"{a['eq'] / 1e6:8.1f} mln rów.  | ruchów {a['turns']:5d} | śr. kontekst z cache {ctx / 1e3:6.0f} tys. | odczyt {a['read'] / 1e6:7.1f} mln, "
            f"zapis {(a['w5'] + a['w1']) / 1e6:5.2f} mln, wyjście {a['output'] / 1e6:5.2f} mln")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("--since")
    ap.add_argument("--until")
    ap.add_argument("--top", type=int, default=12)
    a = ap.parse_args(argv)
    root = pathlib.Path(a.root)
    since, until = (ts(a.since) if a.since else None), (ts(a.until) if a.until else None)
    groups = collections.OrderedDict()   # etykieta -> {message_id: ...}
    models = collections.defaultdict(collections.Counter)
    names = {}
    for wf in root.glob("*/workflows/wf_*.json"):
        try:
            j = json.loads(wf.read_text(encoding="utf-8"))
            names[wf.stem] = (j.get("workflowName") or pathlib.Path(j.get("scriptPath", "")).stem or wf.stem)
        except (ValueError, OSError):
            pass
    for f in sorted(root.glob("*.jsonl")):
        groups["SESJA GŁÓWNA"] = read_file(f, since, until)
    for f in sorted(root.glob("*/subagents/**/agent-*.jsonl")):
        parts = f.parts
        wf = next((p for p in parts if p.startswith("wf_")), None)
        meta = f.with_suffix(".meta.json")
        desc = ""
        try:
            desc = json.loads(meta.read_text(encoding="utf-8")).get("description", "")
        except (ValueError, OSError):
            pass
        label = f"agent {f.stem[6:14]} [{names.get(wf, wf) if wf else 'poza workflow'}] {desc}".strip()
        msgs = read_file(f, since, until)
        if msgs:
            groups[label] = msgs
    rows = {k: totals(v) for k, v in groups.items() if v}
    main_t = rows.get("SESJA GŁÓWNA", collections.Counter())
    agents = {k: v for k, v in rows.items() if k != "SESJA GŁÓWNA"}
    ag_t = collections.Counter()
    for v in agents.values():
        ag_t.update(v)
    total_eq = (main_t["eq"] + ag_t["eq"]) or 1
    print(f"okno: {a.since or 'od początku'} .. {a.until or 'teraz'}  (równoważniki z założonych wag; patrz opis)")
    print(f"SESJA GŁÓWNA : {fmt(main_t)}  ({100 * main_t['eq'] / total_eq:.0f} %)")
    print(f"AGENCI ({len(agents):3d}): {fmt(ag_t)}  ({100 * ag_t['eq'] / total_eq:.0f} %)")
    by_wf = collections.defaultdict(collections.Counter)
    for k, v in agents.items():
        by_wf[k.split("[", 1)[1].split("]", 1)[0] if "[" in k else k].update(v)
    print("\nWorkflow (suma agentów):")
    for k, v in sorted(by_wf.items(), key=lambda kv: -kv[1]["eq"]):
        print(f"  {100 * v['eq'] / total_eq:5.1f} %  {k:38.38s} agentów≥1, ruchów {v['turns']:5d}, {v['eq'] / 1e6:7.1f} mln rów.")
    print(f"\nNajwiększych {a.top} agentów:")
    for k, v in sorted(agents.items(), key=lambda kv: -kv[1]["eq"])[: a.top]:
        print(f"  {100 * v['eq'] / total_eq:5.1f} %  ruchów {v['turns']:4d}  śr.kontekst {v['read'] / max(v['turns'], 1) / 1e3:4.0f} tys.  {k[:90]}")
    mm = collections.defaultdict(collections.Counter)
    for lab, msgs in groups.items():
        for _, model, u in msgs.values():
            one = totals({0: (None, model, u)})
            mm[model].update(one)
    print("\nWedług modelu:")
    for k, v in sorted(mm.items(), key=lambda kv: -kv[1]["eq"]):
        print(f"  {k:24s} {100 * v['eq'] / total_eq:5.1f} %  {v['eq'] / 1e6:8.1f} mln rów., ruchów {v['turns']}")
    byh = collections.defaultdict(collections.Counter)
    for lab, msgs in groups.items():
        for t, model, u in msgs.values():
            byh[t.strftime("%m-%d %H")][ "main" if lab == "SESJA GŁÓWNA" else "agents"] += totals({0: (None, model, u)})["eq"]
    print("\nGodzinami (UTC), mln równoważników: sesja główna / agenci")
    for h in sorted(byh):
        print(f"  {h}:00  {byh[h]['main'] / 1e6:6.1f} / {byh[h]['agents'] / 1e6:6.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
