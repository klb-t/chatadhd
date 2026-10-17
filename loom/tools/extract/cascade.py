"""Stage ``select``: the cascade (``cascades/*.json``) over grains.

Levels run flows: L0 masks (exclude or pass), L1 lexical, L2 vectors — local
and free; model levels (cheap/strong) are present in the data but disabled
unless the user enables them, and this slice records what they WOULD get
(count of ambiguous grains) without ever calling anything.  Every grain gets
``{score, decision, confidence, reasons}`` per level, the levels are combined
(``weighted_max`` preset), bands split the score into include / ambiguous /
exclude, manual decisions win, and everything lands in ``explain``.
Grain results propagate up to the conversation with the max score and the
coverage (share of grains above the include band) — both visible.
"""
import json
from dataclasses import dataclass, field
from typing import Any, Optional

from .criteria import Criteria, mask_check
from .datafiles import DataStore, store as default_store
from .lexical import Lexicon, lexicon as default_lexicon, score_text
from .model import Unit, grain_id
from .vectors import VectorIndex

DECISIONS = ("include", "exclude", "ambiguous")


@dataclass
class Grain:
    id: str
    unit: Unit
    kind: str                       # conversation | branch | window | message | block
    text: str
    message_ids: list[str]
    branch: str = ""
    window: int = 0


@dataclass
class LevelOutcome:
    level: str
    flow: str
    score: Optional[float]
    decision: Optional[str]
    reasons: list[dict[str, Any]] = field(default_factory=list)
    skipped: Optional[str] = None

    def as_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"level": self.level, "flow": self.flow}
        if self.skipped:
            d["skipped"] = self.skipped
            return d
        d["score"] = None if self.score is None else round(self.score, 4)
        d["decision"] = self.decision
        d["reasons"] = self.reasons
        return d


@dataclass
class GrainDecision:
    grain: Grain
    score: float
    decision: str
    confidence: float
    source: str
    levels: list[LevelOutcome]

    def as_dict(self) -> dict[str, Any]:
        u = self.grain.unit
        return {"grain": self.grain.id, "grain_kind": self.grain.kind, "unit": u.uid, "title": u.title,
                "service": u.account.get("service"), "messages": self.grain.message_ids if self.grain.kind != "conversation" else None,
                "score": round(self.score, 4), "decision": self.decision, "confidence": round(self.confidence, 4),
                "source": self.source, "levels": [lv.as_dict() for lv in self.levels]}


@dataclass
class UnitDecision:
    unit: Unit
    score: float
    decision: str
    confidence: float
    source: str
    coverage: float
    grains: int
    grains_included: int
    best_grain: Optional[str]
    reasons: list[dict[str, Any]]
    manual: Optional[dict[str, Any]] = None
    selected_messages: Optional[set[str]] = None      # None = whole unit

    def as_dict(self) -> dict[str, Any]:
        return {"unit": self.unit.uid, "title": self.unit.title, "service": self.unit.account.get("service"),
                "score": round(self.score, 4), "decision": self.decision, "confidence": round(self.confidence, 4),
                "source": self.source, "coverage": round(self.coverage, 4), "grains": self.grains,
                "grains_included": self.grains_included, "best_grain": self.best_grain, "reasons": self.reasons[:20],
                "manual": self.manual,
                "messages": sorted(self.selected_messages) if self.selected_messages is not None else None}


@dataclass
class CascadeResult:
    cascade_id: str
    grain: str
    units: dict[str, UnitDecision]
    grains: list[GrainDecision]
    levels: list[dict[str, Any]]
    shadow: dict[str, list[str]]
    bands: dict[str, float]
    vectors: Optional[dict[str, Any]]
    model_pending: dict[str, Any]

    def counts(self) -> dict[str, int]:
        out = {d: 0 for d in DECISIONS}
        for ud in self.units.values():
            out[ud.decision] = out.get(ud.decision, 0) + 1
        return out

    def included(self) -> list[UnitDecision]:
        return [d for d in self.units.values() if d.decision == "include"]

    def explain_rows(self) -> list[dict[str, Any]]:
        return [g.as_dict() for g in self.grains]

    def as_dict(self) -> dict[str, Any]:
        return {"cascade": self.cascade_id, "grain": self.grain, "counts": self.counts(), "levels": self.levels,
                "shadow": {k: len(v) for k, v in self.shadow.items()}, "bands": self.bands, "vectors": self.vectors,
                "model_pending": self.model_pending}


# ───────────────────────────── grains ─────────────────────────────
def grain_text(unit: Unit, message_ids: Optional[list[str]], include_blocks: list[str], *, with_title: bool = True) -> str:
    parts = [unit.title] if with_title and unit.title else []
    ids = set(message_ids) if message_ids is not None else None
    for m in unit.messages:
        if ids is not None and m.id not in ids:
            continue
        for b in m.blocks:
            if b.kind in include_blocks and b.text:
                parts.append(b.text)
    return "\n".join(parts)


def _block_text(unit: Unit, b: Any) -> str:
    parts = [b.name or "", b.text or ""]
    if isinstance(b.input, (dict, list)):
        parts.append(json.dumps(b.input, ensure_ascii=False, sort_keys=True))
    if b.kind == "attachment" or b.ref:
        a = next((a for a in unit.attachments if a.ref == b.ref), None)
        parts += [b.ref or "", (a.name or "") if a is not None else "", (a.mime or b.mime or "") if a is not None else (b.mime or "")]
    return "\n".join(p for p in parts if p)


def make_grains(unit: Unit, grain: str, include_blocks: list[str], window: int = 6, step: int = 3,
                block_kinds: Optional[list[str]] = None) -> list[Grain]:
    if grain == "block":
        # one grain per tool call, tool result, reasoning or attachment block (preset grain_blocks); a decision on a
        # block selects its message, and the unit's decision is the best of its blocks (with level, source, confidence)
        kinds = set(block_kinds or ["tool_use", "tool_result", "thinking", "attachment"])
        out_b: list[Grain] = []
        for m in unit.messages:
            for i, b in enumerate(m.blocks):
                if b.kind in kinds:
                    out_b.append(Grain(grain_id(unit, "block", message=m.id, block=i), unit, "block", _block_text(unit, b), [m.id]))
            for a in unit.attachments:  # attachments a message carries only in its metadata
                if a.message == m.id and not any(b.ref == a.ref for b in m.blocks) and "attachment" in kinds:
                    out_b.append(Grain(grain_id(unit, "block", message=m.id, block=len(m.blocks)), unit, "block",
                                       "\n".join(x for x in (a.ref, a.name or "", a.mime or "") if x), [m.id]))
        return out_b  # a conversation without such blocks has no block grain: it is not decided at this grain
    if grain == "conversation" or not unit.messages:
        return [Grain(grain_id(unit, "conversation"), unit, "conversation", grain_text(unit, None, include_blocks),
                      [m.id for m in unit.messages])]
    out: list[Grain] = []
    if grain == "message":
        for m in unit.messages:
            out.append(Grain(grain_id(unit, "message", message=m.id), unit, "message",
                             grain_text(unit, [m.id], include_blocks, with_title=False), [m.id]))
        return out
    branches = unit.branches()
    if grain == "branch":
        for i, path in enumerate(branches):
            label = path[-1].branch if path else str(i)
            ids = [m.id for m in path]
            out.append(Grain(grain_id(unit, "branch", branch=label or str(i)), unit, "branch",
                             grain_text(unit, ids, include_blocks), ids, branch=label))
        return out
    if grain == "window":
        n = window if window and window > 0 else 0
        seen: set[tuple[str, ...]] = set()
        k = 0
        for path in branches:
            ids = [m.id for m in path]
            if n == 0 or len(ids) <= n:
                spans = [ids]
            else:
                spans = [ids[i:i + n] for i in range(0, max(1, len(ids) - n + 1), max(1, step))]
                if spans[-1][-1] != ids[-1]:
                    spans.append(ids[-n:])
            for span in spans:
                key = tuple(span)
                if key in seen:
                    continue
                seen.add(key)
                out.append(Grain(grain_id(unit, "window", window=k), unit, "window", grain_text(unit, span, include_blocks), list(span), window=k))
                k += 1
        return out
    raise ValueError(f"unknown grain: {grain!r}")


# ───────────────────────────── cascade ─────────────────────────────
class Cascade:
    def __init__(self, units: list[Unit], criteria: Criteria, ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None,
                 *, cascade_id: Optional[str] = None, grain: Optional[str] = None, manual: Optional[dict[str, dict[str, Any]]] = None,
                 vectors: Optional[VectorIndex] = None, model_enabled: bool = False, bands: Optional[dict[str, float]] = None,
                 model_runner: Any = None, topic: str = "") -> None:
        self.ds = ds or default_store()
        self.model_runner = model_runner
        self.topic = topic
        self.lex = lex or default_lexicon()
        self.units = [u for u in units if u.kind in ("conversation", "session")]
        self.criteria = criteria
        self.cascade = self.ds.entry("cascades", cascade_id or criteria.cascade)
        self.grain = grain or criteria.grain or self.ds.preset("grain", "conversation")
        self.manual = manual or {}
        self.vectors = vectors
        self.model_enabled = model_enabled
        self.bands = dict(self.cascade.get("bands") or {"include": 0.75, "exclude": 0.25})
        if bands:
            self.bands.update(bands)
        self.flows = self.ds.entries("flows")
        self.window = int(self.ds.preset("window.size", 6))
        self.step = int(self.ds.preset("window.step", 3))
        self.saturation = float(self.ds.preset("lexical.saturation", 6.0))
        self.top_k = int(self.ds.preset("lexical.explain_top_k", 10))
        self.tolerance = float(self.ds.preset("masks.date_tolerance_hours", 14))

    # -- levels -------------------------------------------------------------------
    def _band(self, score: float) -> str:
        if score >= self.bands["include"]:
            return "include"
        if score <= self.bands["exclude"]:
            return "exclude"
        return "ambiguous"

    def _level_masks(self, unit: Unit) -> LevelOutcome:
        ok, reasons = mask_check(unit, self.criteria.masks, tolerance_hours=self.tolerance)
        return LevelOutcome("L0", "masks", None if ok else 0.0, None if ok else "exclude",
                            [{"mask": r} for r in reasons])

    def _level_lexical(self, grain: Grain, level: dict[str, Any]) -> LevelOutcome:
        flow = self.flows.get(level["flow"], {})
        sat = float((flow.get("parameters") or {}).get("saturation", self.saturation))
        r = score_text(grain.text, self.criteria.keywords, self.criteria.regexes, self.criteria.exclude_keywords,
                       self.criteria.exclude_regexes, self.lex, saturation=sat)
        decision = "exclude" if r.excluded_by else self._band(r.score)
        return LevelOutcome(level["id"], level["flow"], r.score, decision, r.reasons(self.top_k))

    def _vector_scores(self, grains: list[Grain]) -> dict[str, tuple[float, str]]:
        """grain id → (best similarity, query) over every vector query of the criteria."""
        out: dict[str, tuple[float, str]] = {}
        if not self.criteria.vectors or not grains:
            return out
        if self.vectors is None:
            self.vectors = VectorIndex(tier=self.ds.preset("vectors.tier"), allow_download=bool(self.ds.preset("vectors.allow_download", False)))
        self.vectors.index([(g.id, g.text) for g in grains])
        thr_default = float(self.ds.preset("vectors.threshold", 0.35))
        cand_default = int(self.ds.preset("vectors.candidates", 0))
        for q in self.criteria.vectors:
            text = q.get("query") if isinstance(q, dict) else str(q)
            if not text:
                continue
            cands = int(q.get("candidates", cand_default)) if isinstance(q, dict) else cand_default
            for gid, s in self.vectors.query(text, candidates=cands, threshold=0.0):
                if gid not in out or s > out[gid][0]:
                    out[gid] = (s, text)
        self._vector_threshold = min([float(q.get("threshold", thr_default)) for q in self.criteria.vectors if isinstance(q, dict)] or [thr_default])
        return out

    def _level_vectors(self, grain: Grain, level: dict[str, Any], scores: dict[str, tuple[float, str]]) -> LevelOutcome:
        if not self.criteria.vectors:
            return LevelOutcome(level["id"], level["flow"], None, None, skipped="no vector queries in criteria")
        if self.vectors is None:
            return LevelOutcome(level["id"], level["flow"], None, None, skipped="no vector index (run with vectors enabled)")
        s, q = scores.get(grain.id, (0.0, ""))
        thr = getattr(self, "_vector_threshold", 0.35)
        # similarity → score: below threshold scales into the exclude band, above into include
        if s >= thr:
            score = min(1.0, self.bands["include"] + (s - thr) / max(1e-9, 1.0 - thr) * (1.0 - self.bands["include"]))
        else:
            score = (s / thr) * self.bands["include"] if thr > 0 else 0.0
        reasons = [{"query": q, "similarity": round(s, 4), "threshold": thr, "tier": self.vectors.tier_name if self.vectors else None}] if q else []
        return LevelOutcome(level["id"], level["flow"], score, self._band(score), reasons)

    def _level_model(self, grain: "Grain", level: dict[str, Any], flow_id: str) -> LevelOutcome:
        from .flows import verdict_outcome
        crit = self.criteria
        summary = ", ".join(crit.terms[:12]) if getattr(crit, "terms", None) else crit.id
        parsed = self.model_runner.call(flow_id, {"topic": self.topic or crit.id, "title": grain.unit.title or "",
                                                  "window": grain.text, "criteria_summary": summary})
        decision, conf, reason = verdict_outcome(parsed)
        if decision is None:
            return LevelOutcome(level["id"], flow_id, None, None, skipped=reason)
        score = {"include": 0.5 + conf / 2, "exclude": 0.5 - conf / 2, "ambiguous": 0.5}[decision]
        return LevelOutcome(level["id"], flow_id, score, decision, [{"model": True, "confidence": conf, "reason": reason}])

    # -- combine ----------------------------------------------------------------------
    def _combine(self, outcomes: list[LevelOutcome]) -> tuple[float, float, str]:
        """(score, confidence, source) from decisive local levels."""
        mode = self.cascade.get("combine", "weighted_max")
        weights = self.criteria.weights
        scored = [(lv, weights.get(lv.flow, weights.get(lv.level, 1.0))) for lv in outcomes if lv.score is not None and not lv.skipped]
        if not scored:
            return 0.0, 0.0, "none"
        if any(lv.level == "L0" and lv.decision == "exclude" for lv, _ in scored):
            return 0.0, 1.0, "L0"
        if mode == "weighted_mean":
            tot = sum(w for _, w in scored) or 1.0
            score = sum(lv.score * w for lv, w in scored) / tot
            src = max(scored, key=lambda x: x[0].score * x[1])[0].level
            spread = max(lv.score for lv, _ in scored) - min(lv.score for lv, _ in scored)
            return min(1.0, score), max(0.0, 1.0 - spread), src
        if mode == "vote":
            votes: dict[str, float] = {}
            for lv, w in scored:
                votes[lv.decision or "ambiguous"] = votes.get(lv.decision or "ambiguous", 0.0) + w
            best = max(votes.items(), key=lambda kv: (kv[1], kv[0]))[0]
            score = {"include": 1.0, "ambiguous": 0.5, "exclude": 0.0}[best]
            src = next(lv.level for lv, _ in scored if lv.decision == best)
            return score, votes[best] / (sum(votes.values()) or 1.0), src
        if mode == "first_decisive":
            for lv, _ in scored:
                if lv.decision in ("include", "exclude"):
                    return lv.score, 1.0, lv.level
            lv, _ = scored[-1]
            return lv.score, 0.5, lv.level
        # weighted_max (preset): the strongest weighted channel decides
        best_lv, best_w = max(scored, key=lambda x: (x[0].score * x[1], -int(x[0].level[1:] or 0)))
        score = min(1.0, best_lv.score * best_w)
        others = [lv.score * w for lv, w in scored if lv is not best_lv]
        conf = 1.0 - (max(others) if others else 0.0) * 0.5 if best_lv.decision != "include" else min(1.0, 0.5 + score / 2)
        return score, max(0.0, min(1.0, conf)), best_lv.level

    # -- run ------------------------------------------------------------------------------
    def run(self) -> CascadeResult:
        levels = [lv for lv in self.cascade["levels"]]
        lex_flow = self.flows.get("lexical", {})
        include_blocks = list(((lex_flow.get("input") or {}).get("include_blocks")) or ["text"])
        grains: list[Grain] = []
        unit_grains: dict[str, list[Grain]] = {}
        unit_mask: dict[str, LevelOutcome] = {}
        has_masks = any(lv["flow"] == "masks" for lv in levels)
        for u in self.units:
            if has_masks:
                unit_mask[u.uid] = self._level_masks(u)
            gs = make_grains(u, self.grain, include_blocks, self.window, self.step, self.ds.preset("grain_blocks"))
            unit_grains[u.uid] = gs
            grains.extend(gs)
        vec_level = next((lv for lv in levels if lv["flow"] == "vectors" and lv.get("enabled", True)), None)
        passed_grains = [g for g in grains if not (unit_mask.get(g.unit.uid) and unit_mask[g.unit.uid].decision == "exclude")]
        vscores = self._vector_scores(passed_grains) if vec_level else {}

        stats = {lv["id"]: {"id": lv["id"], "flow": lv["flow"], "enabled": lv.get("enabled", True), "evaluated": 0,
                            "include": 0, "exclude": 0, "ambiguous": 0, "skipped": 0} for lv in levels}
        grain_decisions: list[GrainDecision] = []
        shadow = {"lexical_only": [], "vectors_only": [], "both": []}
        model_pending: dict[str, Any] = {}
        for g in grains:
            outcomes: list[LevelOutcome] = []
            excluded = False
            state = "all"
            model_decided: Optional[tuple[str, Optional[float], str]] = None
            for lv in levels:
                flow = lv["flow"]
                on = lv.get("on", "all")
                if not lv.get("enabled", True) and not self.model_enabled:
                    stats[lv["id"]]["skipped"] += 1
                    outcomes.append(LevelOutcome(lv["id"], flow, None, None, skipped="disabled (enable with --model)"))
                    continue
                if on == "passed" and excluded:
                    outcomes.append(LevelOutcome(lv["id"], flow, None, None, skipped="excluded earlier"))
                    stats[lv["id"]]["skipped"] += 1
                    continue
                if on == "ambiguous" and state != "ambiguous":
                    outcomes.append(LevelOutcome(lv["id"], flow, None, None, skipped=f"on: ambiguous (state {state})"))
                    stats[lv["id"]]["skipped"] += 1
                    continue
                if on == "included" and state != "include":
                    outcomes.append(LevelOutcome(lv["id"], flow, None, None, skipped=f"on: included (state {state})"))
                    stats[lv["id"]]["skipped"] += 1
                    continue
                if flow == "masks":
                    out = unit_mask.get(g.unit.uid) or LevelOutcome(lv["id"], flow, None, None)
                elif flow == "lexical":
                    out = self._level_lexical(g, lv)
                elif flow == "vectors":
                    out = self._level_vectors(g, lv, vscores)
                else:
                    fl = self.flows.get(flow, {})
                    if fl.get("kind") == "model":
                        model_pending.setdefault(flow, {"grains": 0, "chars": 0})
                        model_pending[flow]["grains"] += 1
                        model_pending[flow]["chars"] += len(g.text)
                        if self.model_runner is not None:
                            out = self._level_model(g, lv, flow)
                            if out.decision in ("include", "exclude"):
                                model_decided = (out.decision, out.score, lv["id"])
                        else:
                            out = LevelOutcome(lv["id"], flow, None, None,
                                               skipped="model flow counted, not called (enable with --model and accept the estimate)")
                    else:
                        out = LevelOutcome(lv["id"], flow, None, None, skipped=f"unknown flow kind {fl.get('kind')}")
                outcomes.append(out)
                if out.skipped:
                    stats[lv["id"]]["skipped"] += 1
                    continue
                stats[lv["id"]]["evaluated"] += 1
                if out.decision:
                    stats[lv["id"]][out.decision] += 1
                decides = lv.get("decides") or list(DECISIONS)
                if out.decision == "exclude" and "exclude" in decides:
                    excluded = True
                    state = "exclude"
                elif out.decision in ("include", "ambiguous") and out.decision in decides:
                    state = out.decision if state != "include" else "include"
            score, conf, src = self._combine([o for o in outcomes if not o.flow or self.flows.get(o.flow, {}).get("kind") != "model"])
            decision = "exclude" if excluded else self._band(score)
            if model_decided is not None and not excluded:
                # a model level runs on what the local levels left open; its verdict decides that grain
                decision, src = model_decided[0], model_decided[2]
                conf = next((float(o.reasons[0].get("confidence", conf)) for o in outcomes if o.level == src and o.reasons), conf)
                score = model_decided[1] if model_decided[1] is not None else score
            if decision == "ambiguous" and self.cascade.get("unresolved") in ("include", "exclude"):
                decision = self.cascade["unresolved"]
                src = f"{src}+unresolved:{decision}"
            lexs = next((o.score for o in outcomes if o.flow == "lexical" and o.score is not None), None)
            vecs = next((o.score for o in outcomes if o.flow == "vectors" and o.score is not None), None)
            if lexs is not None and vecs is not None:
                li, vi = lexs >= self.bands["include"], vecs >= self.bands["include"]
                if li and not vi:
                    shadow["lexical_only"].append(g.id)
                elif vi and not li:
                    shadow["vectors_only"].append(g.id)
                elif li and vi:
                    shadow["both"].append(g.id)
            grain_decisions.append(GrainDecision(g, score, decision, conf, src, outcomes))

        # propagate to units
        by_unit: dict[str, list[GrainDecision]] = {}
        for gd in grain_decisions:
            by_unit.setdefault(gd.grain.unit.uid, []).append(gd)
        units_out: dict[str, UnitDecision] = {}
        for u in self.units:
            gds = by_unit.get(u.uid, [])
            if not gds:
                continue
            best = max(gds, key=lambda d: (d.score, d.grain.id))
            inc = [d for d in gds if d.decision == "include"]
            coverage = len(inc) / len(gds)
            reasons: list[dict[str, Any]] = []
            for lv in best.levels:
                for r in lv.reasons:
                    reasons.append({"level": lv.level, **r})
            selected = None if self.grain == "conversation" else {mid for d in inc for mid in d.grain.message_ids}
            ud = UnitDecision(u, best.score, best.decision, best.confidence, best.source, coverage, len(gds), len(inc),
                              best.grain.id, reasons, selected_messages=selected)
            man = self.manual.get(u.uid)
            if man and self.cascade.get("manual", "wins") == "wins" and man.get("decision") in DECISIONS:
                ud.manual = {"was": {"decision": ud.decision, "score": round(ud.score, 4), "source": ud.source}, **man}
                ud.decision = man["decision"]
                ud.source = "manual"
                ud.confidence = 1.0
                if man["decision"] == "include":
                    ud.selected_messages = None
            units_out[u.uid] = ud
        # the shadow list is about units, not grains, when grain = conversation
        return CascadeResult(self.cascade["id"], self.grain, units_out, grain_decisions, list(stats.values()), shadow, self.bands,
                             self.vectors.describe() if self.vectors else None, model_pending)
