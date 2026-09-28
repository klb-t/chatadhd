"""synthetic_dev scorecard: the whole knowledge pipeline, end to end.

The corpus (tests/fixtures/eval/synthetic_dev) is about a FICTIONAL persona;
its ground_truth.json is the answer key, expressed in LOOM_CONCEPTUAL_MODEL
terms, with every fact located by (conv_id, node_id). The harness runs the
real pipeline through `loom knowledge run` (catalog -> extract -> resolve ->
assess -> generalize -> materialize) and joins the ground truth to what the
pipeline stored through those locators:

    ground truth (conv_id, node_id)
      -> catalog unit (loom_cat_units.ext_id == conv_id)
      -> observations of that unit with attrs.node == node_id
      -> claims / decisions / principles / operators supported by them

Runs:
  A   full corpus, no cut                 -> every area's metrics
  A'  the same inputs in another data dir -> determinism (stage output hashes, product bytes)
  B   full corpus, prior_cut = T          -> temporal holdout: predictions made from <= T,
                                              evaluated by the pipeline against > T, scored here
The only owner input is the persona's own project names (the equivalent of
the real owner's profiles/self.json): stage_params.catalog.profile.extra_terms.
"""
from __future__ import annotations

import filecmp
import json
import shutil
from collections import defaultdict
from pathlib import Path
from typing import Any

from .common import KB, claim_obs, claim_quotes, evidence_class, fold, overlap, ratio, run_knowledge

GT_REL = Path("tests/fixtures/eval/synthetic_dev")


def gt_dir(loom_root: Path) -> Path:
    return loom_root / GT_REL


def base_config(loom_root: Path, gt: dict[str, Any]) -> dict[str, Any]:
    d = gt_dir(loom_root)
    aliases: list[str] = []
    for p in gt["projects"]:
        aliases.extend(p["aliases"])
    return {
        "sources": [str(d / "chatgpt_export.zip"), str(d / "claude_export.zip")],
        "project": "NoteFlow",
        "stage_params": {"catalog": {"profile": {"extra_terms": aliases}}},
    }


class Join:
    """ground-truth locators -> pipeline ids."""

    def __init__(self, kb: KB) -> None:
        self.kb = kb
        ext = kb.unit_ext_ids()
        self.obs_by_loc: dict[tuple[str, str], set[str]] = defaultdict(set)
        self.obs: dict[str, dict[str, Any]] = {}
        for o in kb.observations():
            self.obs[o["id"]] = o
            node = (o.get("attrs") or {}).get("node")
            conv = ext.get(o.get("unit", ""), "")
            if node and conv:
                self.obs_by_loc[(conv, node)].add(o["id"])
        self.claims = kb.claims()
        self.claims_by_obs: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for c in self.claims:
            for o in claim_obs(c):
                self.claims_by_obs[o].append(c)

    def obs_of(self, units: list[dict[str, Any]] | dict[str, Any]) -> set[str]:
        if isinstance(units, dict):
            units = [units]
        out: set[str] = set()
        for u in units:
            out |= self.obs_by_loc.get((u.get("conv_id", ""), u.get("node_id", "")), set())
        return out


# ── area metrics ─────────────────────────────────────────────────────
def score_catalog(kb: KB, gt: dict[str, Any]) -> dict[str, Any]:
    sel = kb.selection()
    units = gt["units"]

    def n_sel(arr: list[dict[str, Any]]) -> int:
        return sum(1 for u in arr if sel.get(u["conv_id"]))

    rel = n_sel(units["relevant"])
    selected = sum(1 for v in sel.values() if v)
    return {
        "recall": ratio(rel, len(units["relevant"])),
        "precision": ratio(rel, selected),
        "trap_fpr": ratio(n_sel(units["noise_traps"]), len(units["noise_traps"])),
        "generic_selected": n_sel(units["noise_generic"]),
        "selected": selected,
    }


def score_projects(kb: KB, gt: dict[str, Any]) -> dict[str, Any]:
    ents = [e for e in kb.entities() if e["kind"] == "project" and e.get("status") == "active"]
    surf = {e["id"]: {fold(a["surface"]) for a in e.get("aliases", [])} | {fold(e["label"])} for e in ents}
    found, aliases_found, aliases_total, fragments = 0, 0, 0, 0
    matched_ids: set[str] = set()
    per: dict[str, Any] = {}
    for p in gt["projects"]:
        gal = {fold(a) for a in p["aliases"]}
        hits = [eid for eid, s in surf.items() if s & gal]
        matched_ids.update(hits)
        best = max(hits, key=lambda eid: len(surf[eid] & gal), default=None)
        n_al = len(surf[best] & gal) if best else 0
        found += 1 if hits else 0
        fragments += max(0, len(hits) - 1)
        aliases_found += n_al
        aliases_total += len(gal)
        per[p["id"]] = {"entities": len(hits), "aliases_on_best": n_al, "aliases": len(gal)}
    return {
        "project_recall": ratio(found, len(gt["projects"])),
        "alias_recall_on_canonical": ratio(aliases_found, aliases_total),
        "fragmented_extra_entities": fragments,
        "spurious_projects": len([e for e in ents if e["id"] not in matched_ids]),
        "per_project": per,
    }


def version_strings(kb: KB, j: Join) -> set[str]:
    labels = {e["id"]: e["label"] for e in kb.entities()}
    out: set[str] = set()
    for c in j.claims:
        if c["predicate"] in ("has_version", "version_of"):
            for v in (c.get("value"), labels.get(c.get("object", ""), "")):
                if isinstance(v, str) and v:
                    out.add(v.lstrip("vV"))
        q = (c.get("qualifiers") or {}).get("version")
        if q:
            out.add(q.lstrip("vV"))
    for e in kb.entities():
        if e["kind"] == "version":
            out.add(e["label"].lstrip("vV"))
    return out


def score_versions(kb: KB, j: Join, gt: dict[str, Any]) -> dict[str, Any]:
    have = version_strings(kb, j)
    n = hit = 0
    for p in gt["projects"]:
        for v in p["versions"]:
            if "chat" not in v.get("source", "chat"):
                continue  # repo-only versions: no repo in this corpus run
            n += 1
            hit += 1 if v["version"] in have else 0
    return {"version_recall_chat": ratio(hit, n), "versions_total_chat": n}


def all_decisions(gt: dict[str, Any]) -> list[dict[str, Any]]:
    return [d for p in gt["projects"] for d in p["decisions"]]


def score_decisions(kb: KB, j: Join, gt: dict[str, Any]) -> dict[str, Any]:
    decs = kb.table("decisions")
    dclaims = {c["id"]: c for c in j.claims if c["predicate"] == "decides"}
    by_obs: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for d in decs:
        c = dclaims.get(d["id"])
        for o in claim_obs(c) if c else []:
            by_obs[o].append(d)
    kept_open = {c["id"]: c for c in dclaims.values() if isinstance(c.get("value"), dict) and "kept_open" in c["value"]}
    match: dict[str, dict[str, Any] | None] = {}
    rec = chosen_ok = 0
    for g in all_decisions(gt):
        obs = j.obs_of(g["unit"])
        cands = [d for o in obs for d in by_obs.get(o, [])]
        m = cands[0] if cands else None
        if not m:
            # a decision kept open is a `decides` claim without a Decision row
            for o in obs:
                for c in j.claims_by_obs.get(o, []):
                    if c["id"] in kept_open:
                        m = {"id": c["id"], "alternatives": [], "status": "active", "kept_open": True}
        match[g["id"]] = m
        if not m:
            continue
        rec += 1
        chosen = [a.get("label", "") for a in m.get("alternatives", []) if a.get("chosen")]
        if g.get("chosen") is None or any(fold(g["chosen"]) in fold(x) or overlap(g["chosen"], x) >= 0.3 for x in chosen):
            chosen_ok += 1
    sup_n = sup_ok = 0
    for g in all_decisions(gt):
        if not g.get("supersedes"):
            continue
        sup_n += 1
        old = match.get(g["supersedes"])
        if old and old.get("status") == "superseded":
            sup_ok += 1
    return {
        "decision_recall": ratio(rec, len(match)),
        "chosen_accuracy": ratio(chosen_ok, rec),
        "supersession_recall": ratio(sup_ok, sup_n),
        "decisions_total": len(match),
        "decisions_found": len(decs),
    }, match


def score_forks(kb: KB, gt: dict[str, Any]) -> dict[str, Any]:
    forks = kb.table("forks")
    n = hit = 0
    for p in gt["projects"]:
        for f in p["forks"]:
            n += 1
            names = {fold(b["name"]) for b in f["branches"]}
            ok = False
            for x in forks:
                labels = {fold(s.get("label", "")) for s in x.get("sides", [])}
                if labels & names or (x.get("date", "")[:10] == f.get("base_date") and len(x.get("sides", [])) >= 2):
                    ok = True
            hit += ok
    return {"fork_recall": ratio(hit, n), "forks_found": len(forks)}


def score_statuses(kb: KB, gt: dict[str, Any]) -> dict[str, Any]:
    recs = kb.table("status_records")
    have = {(r["status"], r.get("date", "")[:10]) for r in recs}
    n = hit = 0
    osc_n = osc_hit = 0
    by_entity: dict[str, set[str]] = defaultdict(set)
    for r in recs:
        by_entity[r["entity"]].add(r["status"])
    osc_entities = sum(1 for s in by_entity.values() if {"lost", "restored"} <= s)
    for p in gt["projects"]:
        for f in p["features_status"]:
            sts = [e["status"] for e in f["events"]]
            if "lost" in sts and "restored" in sts:
                osc_n += 1
            for e in f["events"]:
                n += 1
                hit += (e["status"], e["date"]) in have
    return {"status_event_recall": ratio(hit, n), "status_events_total": n, "oscillating_features_gt": osc_n,
            "oscillating_entities_found": osc_entities, "status_records": len(recs)}


def score_open_questions(j: Join, gt: dict[str, Any]) -> dict[str, Any]:
    n = hit = 0
    for q in gt["open_questions"]:
        n += 1
        obs = j.obs_of(q["units"])
        hit += any(c["predicate"] == "has_question" for o in obs for c in j.claims_by_obs.get(o, []))
    return {"open_question_recall": ratio(hit, n)}


def score_contradictions(j: Join, gt: dict[str, Any]) -> dict[str, Any]:
    n = hit = 0
    for x in gt["contradictions"]:
        n += 1
        ev = x.get("evidence", {})
        obs = j.obs_of([v for v in ev.values() if isinstance(v, dict)])
        hit += any(((c.get("assessment") or {}).get("status") == "contested") for o in obs for c in j.claims_by_obs.get(o, []))
    contested = sum(1 for c in j.claims if (c.get("assessment") or {}).get("status") == "contested")
    return {"contradiction_recall": ratio(hit, n), "contested_claims": contested}


def score_areas(kb: KB, gt: dict[str, Any]) -> dict[str, Any]:
    areas = kb.table("areas")
    n = hit = 0
    for a in gt["areas"]:
        n += 1
        hit += any(overlap(a["generalization_pl"], x.get("statement", "")) >= 0.4 for x in areas)
    return {"area_recall": ratio(hit, n), "areas_found": len(areas),
            "inferred_members": sum(len(x.get("inferred_members", [])) for x in areas)}


def principle_obs(p: dict[str, Any]) -> set[str]:
    out = set(p.get("evidence_for", []))
    out |= {s.get("observation", "") for s in p.get("sources", []) if s.get("observation")}
    return out


def score_principles(kb: KB, j: Join, gt: dict[str, Any]) -> dict[str, Any]:
    ours = kb.table("principles")
    matched_ours: set[str] = set()
    rec = lvl = frm = 0
    for g in gt["principles"]:
        obs = j.obs_of(g["phrasings"])
        cands = [p for p in ours if principle_obs(p) & obs]
        if not cands:
            continue
        rec += 1
        best = max(cands, key=lambda p: (len(principle_obs(p) & obs), p.get("confidence", 0), p["id"]))
        matched_ours.update(p["id"] for p in cands)
        lvl += best.get("level") == g["level"]
        frm += best.get("form") == g["form"]
    discovered = [p for p in ours if principle_obs(p)]
    return {
        "principle_recall": ratio(rec, len(gt["principles"])),
        "level_accuracy": ratio(lvl, rec),
        "form_accuracy": ratio(frm, rec),
        "principles_with_evidence": len(discovered),
        "principle_precision": ratio(len([p for p in discovered if p["id"] in matched_ours]), len(discovered)),
        "principles_total": len(ours),
    }


def score_operators(kb: KB, j: Join, gt: dict[str, Any], dmatch: dict[str, Any]) -> dict[str, Any]:
    ops = kb.table("operators")
    dec_obs = {g["id"]: j.obs_of(g["unit"]) for g in all_decisions(gt)}
    rec = 0
    for g in gt["operators"]:
        want: set[str] = set()
        want_claims: set[str] = set()
        for ex in g["examples"]:
            want |= dec_obs.get(ex["decision_id"], set())
            m = dmatch.get(ex["decision_id"])
            if m:
                want_claims.add(m["id"])
        ok = False
        for o in ops:
            got_obs = {e.get("observation", "") for e in o.get("examples", [])}
            got_claims = {e.get("claim", "") for e in o.get("examples", [])} | set((o.get("basis") or {}).get("decisions", []))
            # an operator recovers g when it generalises >= 2 of g's example decisions
            if len((got_obs & want)) + len(got_claims & want_claims) >= 2:
                ok = True
        rec += ok
    return {"operator_recall": ratio(rec, len(gt["operators"])), "operators_found": len(ops)}


def score_false_certainty(kb: KB, j: Join) -> dict[str, Any]:
    n = bad_support = bad_quote = bad_ep = 0
    by_class: dict[str, int] = defaultdict(int)
    ep_state: dict[str, int] = defaultdict(int)
    ids = {c["id"]: c for c in j.claims}
    extrap_premise = 0
    for c in j.claims:
        a = c.get("assessment") or {}
        ec = a.get("evidence_class", "")
        by_class[ec] += 1
        n += 1
        if ec == "observed":
            sup = (a.get("basis") or {}).get("support") or []
            if not sup:
                bad_support += 1
            for s in sup:
                o = j.obs.get(s.get("observation", ""))
                if o is None or fold(s.get("quote", "")) not in fold(o.get("text", "")):
                    bad_quote += 1
                    break
        if ec == "inferred":
            if not a.get("expected_property"):
                bad_ep += 1
            ep_state[a.get("check_state", "")] += 1
        for pid in (a.get("premises") or {}).get("claims", []):
            p = ids.get(pid)
            if p and evidence_class(p) in ("extrapolated", "absent"):
                extrap_premise += 1
    fc = bad_support + bad_ep + extrap_premise
    return {
        "false_certainty_rate": ratio(fc, n) if n else 0.0,
        "observed_without_support": bad_support,
        "observed_quote_not_in_observation": bad_quote,
        "inferred_without_expected_property": bad_ep,
        "extrapolated_or_absent_premises": extrap_premise,
        "claims_by_evidence_class": dict(sorted(by_class.items())),
        "expected_property_states": dict(sorted(ep_state.items())),
    }


def score_predictions(kb: KB, j: Join, gt: dict[str, Any], dmatch: dict[str, Any]) -> dict[str, Any]:
    preds = kb.table("predictions")
    dec_obs = {g["id"]: j.obs_of(g["unit"]) for g in all_decisions(gt)}
    ops = {o["id"]: o for o in kb.table("operators")}
    hits = 0
    detail = []
    for g in gt["predictions"]:
        before = dec_obs.get(g["before_T_decision"], set())
        before_claim = (dmatch.get(g["before_T_decision"]) or {}).get("id", "")
        actual = dec_obs.get(g["actual_decision"], set())
        actual_claim = (dmatch.get(g["actual_decision"]) or {}).get("id", "")
        ok = False
        made = False
        for p in preds:
            op = ops.get(p.get("operator", ""), {})
            ex_obs = {e.get("observation", "") for e in op.get("examples", [])}
            ex_claims = {e.get("claim", "") for e in op.get("examples", [])} | set((op.get("basis") or {}).get("decisions", []))
            if not (ex_obs & before or before_claim in ex_claims):
                continue
            made = True
            against = set(p.get("evaluated_against", []))
            if p.get("outcome") == "holds" and (against & actual or actual_claim in against):
                ok = True
        hits += ok
        detail.append({"id": g["id"], "predicted_from_before_T_decision": made, "held_on_actual_decision": ok})
    neg_n = neg_fp = 0
    for u in gt.get("unpredictable_post_T_decisions", []):
        neg_n += 1
        c = (dmatch.get(u["decision_id"]) or {}).get("id", "")
        obs = dec_obs.get(u["decision_id"], set())
        neg_fp += any(p.get("outcome") == "holds" and p.get("confidence", 0) >= 0.7 and (set(p.get("evaluated_against", [])) & (obs | {c}))
                      for p in preds)
    outcomes: dict[str, int] = defaultdict(int)
    for p in preds:
        outcomes[p.get("outcome", "")] += 1
    return {
        "prediction_accuracy_solution_class": ratio(hits, len(gt["predictions"])),
        "predictions_made": len(preds),
        "outcomes": dict(sorted(outcomes.items())),
        "negative_control_false_positives": neg_fp,
        "negative_controls": neg_n,
        "detail": detail,
    }


def determinism(res_a: dict[str, Any], res_b: dict[str, Any], out_a: Path, out_b: Path) -> dict[str, Any]:
    ha = [(s["stage"], s["output_hash"]) for s in res_a["stages"]]
    hb = [(s["stage"], s["output_hash"]) for s in res_b["stages"]]
    files_a = sorted(p.name for p in out_a.glob("*"))
    files_b = sorted(p.name for p in out_b.glob("*"))
    same_files = files_a == files_b and all(filecmp.cmp(out_a / f, out_b / f, shallow=False) for f in files_a)
    diff = [s for (s, x), (_, y) in zip(ha, hb) if x != y]
    return {"stage_hashes_identical": ha == hb, "differing_stages": diff, "products_byte_identical": same_files,
            "run_ids_identical": res_a["run"] == res_b["run"], "products": len(files_a)}


def evaluate(loom: str, loom_root: Path, work: Path) -> dict[str, Any]:
    gt = json.loads((gt_dir(loom_root) / "ground_truth.json").read_text(encoding="utf-8"))
    cut = gt["temporal_cut"]["date"]
    if work.exists():
        shutil.rmtree(work)
    cfg = base_config(loom_root, gt)
    res_a = run_knowledge(loom, work / "a", {**cfg, "out_dir": str(work / "a_out")})
    res_a2 = run_knowledge(loom, work / "a2", {**cfg, "out_dir": str(work / "a2_out")})
    res_b = run_knowledge(loom, work / "b", {**cfg, "prior_cut": cut, "out_dir": str(work / "b_out")})

    kb = KB(work / "a" / "chatadhd.db", res_a["run"])
    j = Join(kb)
    dec, dmatch = score_decisions(kb, j, gt)
    kb_b = KB(work / "b" / "chatadhd.db", res_b["run"])
    j_b = Join(kb_b)
    _, dmatch_b = score_decisions(kb_b, j_b, gt)
    card = {
        "corpus": gt["corpus_id"],
        "temporal_cut": cut,
        "catalog": score_catalog(kb, gt),
        "resolve": score_projects(kb, gt),
        "extract": {
            **score_versions(kb, j, gt),
            **dec,
            **score_forks(kb, gt),
            **score_statuses(kb, gt),
            **score_open_questions(j, gt),
            **score_areas(kb, gt),
        },
        "assess": score_contradictions(j, gt),
        "generalize": {**score_principles(kb, j, gt), **score_operators(kb, j, gt, dmatch)},
        "holdout": score_predictions(kb_b, j_b, gt, dmatch_b),
        "epistemic": score_false_certainty(kb, j),
        "calibration_ece": None,  # I9: no labelled correctness per claim yet -> not measured, never fabricated
        "determinism": determinism(res_a, res_a2, work / "a_out", work / "a2_out"),
        "stages": {s["stage"]: s["stats"] for s in res_a["stages"]},
    }
    return card


def check_floors(card: dict[str, Any], floors: dict[str, Any]) -> list[str]:
    """floors: {"area.metric": min} (or {"area.metric": {"max": x}}); returns the failures."""
    fails = []
    for key, want in floors.items():
        if key.startswith("_"):
            continue
        cur: Any = card
        for part in key.split("."):
            cur = cur.get(part) if isinstance(cur, dict) else None
        if isinstance(want, dict) and "max" in want:
            if cur is None or cur > want["max"]:
                fails.append(f"{key} = {cur} > max {want['max']}")
        elif isinstance(want, bool):
            if cur is not want:
                fails.append(f"{key} = {cur}, expected {want}")
        elif cur is None or cur < want:
            fails.append(f"{key} = {cur} < floor {want}")
    return fails


def render_markdown(card: dict[str, Any]) -> str:
    lines = [f"# Knowledge-layer scorecard — {card['corpus']} (cut {card['temporal_cut']})", ""]
    for area in ("catalog", "resolve", "extract", "assess", "generalize", "holdout", "epistemic", "determinism"):
        lines.append(f"## {area}")
        for k, v in card[area].items():
            if isinstance(v, (dict, list)):
                v = json.dumps(v, ensure_ascii=False)
            lines.append(f"- {k}: {v}")
        lines.append("")
    lines.append(f"- calibration_ece: {card['calibration_ece']} (not measured: no per-claim correctness labels yet)")
    return "\n".join(lines) + "\n"
