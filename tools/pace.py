#!/usr/bin/env python3
"""Tempo zużycia tygodnia: gdzie jesteśmy względem planu właściciela i czy wolno wystartować kolejnego agenta.

  python3 tools/pace.py --used 30 [--inflight 1] [--model haiku] [--now 2026-10-21T17:33:00Z]

--used = seven_day.utilization z rate_limit_event w procentach. Plan i koszty: tools/pace_plan.json.
Dwie reguły startu (obie muszą przejść): (1) punkt kontrolny: used + (w_toku + 1) * koszt <= cel następnego punktu - margines
(drogę hamowania agentów, którzy już chodzą, liczymy z góry); (2) tempo: (w_toku + 1) * koszt <= potrzebne_tempo * lookahead_h,
gdzie potrzebne_tempo = (cel następnego punktu - used) / godziny do punktu - bez tego dałoby się zużyć całą pulę w jednym zrywie.
"""
import argparse, json, pathlib, sys
from datetime import datetime, timezone

PLAN = pathlib.Path(__file__).with_name("pace_plan.json")
STATE = pathlib.Path(__file__).with_name("pace_state.json")


def ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


def target_at(plan, now):
    pts = [(ts(plan["week_start"]), 0.0)] + [(ts(w["at"]), float(w["target"])) for w in plan["waypoints"]]
    if now <= pts[0][0]:
        return 0.0
    for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
        if now <= t1:
            return v0 + (v1 - v0) * (now - t0) / (t1 - t0)
    return pts[-1][1]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--used", type=float, required=True)
    ap.add_argument("--inflight", type=int, default=0)
    ap.add_argument("--model", default="default")
    ap.add_argument("--now")
    ap.add_argument("--start", action="store_true", help="zapisz teraz jako start pakietu w pace_state.json (po werdykcie MOŻNA)")
    a = ap.parse_args(argv)
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    now = ts(a.now) if a.now else datetime.now(timezone.utc)
    costs = plan["cost_per_package"]
    cost = costs.get(a.model) or costs["default"]
    nxt = next((w for w in plan["waypoints"] if ts(w["at"]) > now), None)
    tgt = target_at(plan, now)
    print(f"teraz {now:%Y-%m-%d %H:%M} UTC; zużyte {a.used:g} %; cel na teraz {tgt:.1f} % (odchylenie {a.used - tgt:+.1f} pkt)")
    if nxt is None:
        print("po ostatnim punkcie planu (reset minął albo tuż-tuż): nic do planowania")
        return 0
    hours = (ts(nxt["at"]) - now).total_seconds() / 3600
    need = (float(nxt["target"]) - a.used) / hours
    print(f"następny punkt: {nxt['at']} -> {nxt['target']} % ({nxt['note']}); za {hours:.1f} h; potrzebne tempo {need:+.2f} pkt/h = {need * 24:+.1f} pkt/dobę")
    committed = a.used + (a.inflight + 1) * cost
    r1 = committed <= float(nxt["target"]) - plan["margin"]
    r2 = (a.inflight + 1) * cost <= need * plan["lookahead_h"]
    print(f"koszt pakietu {cost:g} pkt ({a.model}), w toku {a.inflight}; po starcie zobowiązane {committed:.1f} % "
          f"(punkt kontrolny: {'ok' if r1 else 'PRZEKROCZONY'}); w {plan['lookahead_h']:g} h wolno {need * plan['lookahead_h']:.2f} pkt, trzeba {(a.inflight + 1) * cost:.2f} (tempo: {'ok' if r2 else 'ZA SZYBKO'})")
    # reguła 3 - odstęp: średnie tempo, nie tylko zryw; jeden pakiet na koszt/potrzebne_tempo godzin od startu poprzedniego
    starts = sorted(ts(x) for x in json.loads(STATE.read_text(encoding="utf-8")).get("starts", [])) if STATE.exists() else []
    r3, spacing, earliest = True, 0.0, None
    if starts and need > 0:
        spacing = (a.inflight + 1) * cost / need
        earliest = starts[-1].timestamp() + spacing * 3600
        r3 = now.timestamp() >= earliest
        print(f"odstęp od ostatniego startu ({starts[-1]:%H:%M}): potrzebne {spacing:.1f} h, najwcześniej {datetime.fromtimestamp(earliest, timezone.utc):%H:%M} UTC ({'ok' if r3 else 'ZA WCZEŚNIE'})")
    if need <= 0:
        print("WERDYKT: HAMUJ - zużycie jest już powyżej celu następnego punktu planu; nic nowego, aż punkt minie")
    elif r1 and r2 and r3:
        print("WERDYKT: MOŻNA wystartować jeden pakiet")
        if a.start:
            st = json.loads(STATE.read_text(encoding="utf-8")) if STATE.exists() else {"starts": []}
            st.setdefault("starts", []).append(now.strftime("%Y-%m-%dT%H:%M:%SZ"))
            STATE.write_text(json.dumps(st, indent=2) + "\n", encoding="utf-8")
            print("zapisano start w pace_state.json")
    elif r1 and r2 and not r3:
        print(f"WERDYKT: CZEKAJ do {datetime.fromtimestamp(earliest, timezone.utc):%H:%M} UTC - średnie tempo byłoby za szybkie (zryw już mieści się, odstęp nie)")
    elif not r1:
        print(f"WERDYKT: CZEKAJ - pakiet (razem z tymi w toku) przekroczyłby cel punktu kontrolnego; zostaje {float(nxt['target']) - a.used:.1f} pkt do celu, margines {plan['margin']:g}")
    else:
        print("WERDYKT: CZEKAJ - za szybko względem tempa; wystartuj, gdy któryś z agentów w toku się skończy")
    return 0


if __name__ == "__main__":
    sys.exit(main())
