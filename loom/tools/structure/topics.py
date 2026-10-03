#!/usr/bin/env python3
"""Offline topic-local observation/context experiment; never mutates Loom data.

Bounded lexical heuristics propose segment boundaries and context links. They do
not extract arbitrary claims, establish entity identity, or prove topic membership.
Source spans and rejected interpretations remain available. See TOPICS.md.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re

VERSION = "topic-context-experiment/1"
POLICY_PATH = Path(__file__).with_name("topics_policy.json")


def _hash(value) -> str:
    encoded = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _literal(text: str):
    # Unicode word boundaries protect short identifiers and underscore adjacency.
    return re.compile(r"(?<!\w)" + re.escape(text) + r"(?!\w)", re.IGNORECASE)


def _load_policy(policy: dict | None) -> dict:
    result = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
    if policy is not None:
        unknown = set(policy) - set(result)
        if unknown:
            raise ValueError(f"unknown topic policy fields: {sorted(unknown)}")
        result.update(deepcopy(policy))
    for name in ("context_window_tokens", "minimum_shared_terms", "novelty_minimum_terms",
                 "focus_max_unanchored_spans", "context_node_budget"):
        if type(result[name]) is not int or not 1 <= result[name] <= 10000:
            raise ValueError(f"{name} must be an integer in [1,10000]")
    for name in ("reset_cues", "return_cues", "continuation_cues", "stopwords"):
        if not isinstance(result[name], list) or any(not isinstance(x, str) or not x for x in result[name]):
            raise ValueError(f"{name} must be a nonempty-string array")
    return result


def _entities(entities: list[dict]) -> list[dict]:
    if not isinstance(entities, list):
        raise ValueError("entities must be an array")
    result, seen = [], set()
    for entity in entities:
        ident = entity.get("id")
        if not isinstance(ident, str) or not ident or ident in seen:
            raise ValueError("entity ids must be nonempty and unique")
        seen.add(ident)
        aliases = entity.get("aliases", [])
        if not isinstance(aliases, list):
            raise ValueError("entity aliases must be an array")
        for raw in aliases:
            alias = {"text": raw} if isinstance(raw, str) else deepcopy(raw)
            if not isinstance(alias, dict) or not isinstance(alias.get("text"), str) or not alias["text"]:
                raise ValueError("alias requires nonempty text")
            for field in ("requires_any", "excludes"):
                values = alias.get(field, [])
                if not isinstance(values, list) or any(not isinstance(x, str) or not x for x in values):
                    raise ValueError(f"alias {field} must be a string array")
            minimum = alias.get("min_context", 1 if alias.get("requires_any") or alias.get("ambiguous") else 0)
            if type(minimum) is not int or minimum < 0:
                raise ValueError("alias min_context must be a nonnegative integer")
            result.append({**alias, "entity_id": ident, "min_context": minimum})
    return sorted(result, key=lambda x: (x["entity_id"], x["text"]))


def _cue(text: str, values: list[str]) -> str | None:
    # Cues are interpreted only at the start of a local span.
    stripped = text.lstrip(" \t\r\n,;:—–-")
    for value in sorted(values, key=lambda x: (-len(x), x)):
        match = _literal(value).match(stripped)
        if match:
            return value
    return None


def _span_ranges(text: str, policy: dict) -> list[tuple[int, int]]:
    boundaries = {0, len(text)}
    for match in re.finditer(r"(?<=[.!?;])\s+|\n+", text):
        boundaries.add(match.end())
    for value in policy["reset_cues"] + policy["return_cues"]:
        for match in _literal(value).finditer(text):
            # A marker inside a clause can make a new topic late in a turn.
            # Require punctuation before it to avoid e.g. 'take it back to Ada'.
            prefix = text[:match.start()].rstrip()
            if not prefix or prefix[-1] in ",;:.!?—–\n":
                boundaries.add(match.start())
    points = sorted(boundaries)
    return [(a, b) for a, b in zip(points, points[1:]) if text[a:b].strip()]


def _terms(text: str, stopwords: set[str]) -> set[str]:
    return {t.casefold() for t in re.findall(r"\w+(?:\+\+|#)?", text)
            if len(t) > 1 and t.casefold() not in stopwords and not t.isdigit()}


def _mentions(text: str, aliases: list[dict], window: int) -> list[dict]:
    tokens = list(re.finditer(r"\w+", text))
    results = []
    for alias in aliases:
        for match in _literal(alias["text"]).finditer(text):
            before = [i for i, token in enumerate(tokens) if token.end() <= match.start()]
            after = [i for i, token in enumerate(tokens) if token.start() >= match.end()]
            left = tokens[max(0, (before[-1] + 1 if before else 0) - window)].start() if tokens else 0
            right = tokens[min(len(tokens) - 1, (after[0] if after else len(tokens)) + window - 1)].end() if tokens else len(text)
            left, right = min(left, match.start()), max(right, match.end())
            context = text[left:right]
            positive = sorted(cue for cue in alias.get("requires_any", []) if _literal(cue).search(context))
            negative = sorted(cue for cue in alias.get("excludes", []) if _literal(cue).search(context))
            accepted = not negative and len(positive) >= alias["min_context"]
            results.append({"entity_id": alias["entity_id"], "alias": alias["text"],
                            "char_start": match.start(), "char_end": match.end(),
                            "surface": text[match.start():match.end()], "accepted": accepted,
                            "reason": "negative_local_context" if negative else
                                      "local_alias_match" if accepted else "insufficient_local_context",
                            "positive_cues": positive, "negative_cues": negative,
                            "context_char_start": left, "context_char_end": right,
                            "evidence_channel": "lexical_alias", "semantic_identity": "not_verified"})
    return sorted(results, key=lambda m: (m["char_start"], m["char_end"], m["entity_id"], m["alias"]))


def segment_conversation(turns: list[dict], entities: list[dict], policy: dict | None = None) -> dict:
    """Propose causal, topic-local segments, preserving source observations.

    Earlier observation decisions do not depend on later spans or turns. Segment
    summaries can grow, so callers attaching claims must use observation focus,
    never the union of a completed segment's anchors.
    """
    policy = _load_policy(policy)
    aliases, stops = _entities(entities), set(policy["stopwords"])
    if not isinstance(turns, list):
        raise ValueError("turns must be an array")
    seen, observations, segments = set(), [], []
    active, history, active_terms, anchor_span_ids = [], {}, set(), []
    segment, unanchored = None, 0
    source_records = []
    for turn in turns:
        ident, text = turn.get("id"), turn.get("text")
        if not isinstance(ident, str) or not ident or ident in seen or not isinstance(text, str):
            raise ValueError("turn requires a unique nonempty id and string text")
        seen.add(ident)
        source_hash = hashlib.sha256(text.encode("utf-8")).hexdigest()
        source_records.append({"turn_id": ident, "sha256": source_hash, "n_chars": len(text),
                               "n_bytes": len(text.encode("utf-8")), "role": turn.get("role", "unknown")})
        for start, end in _span_ranges(text, policy):
            local = text[start:end]
            span_id = "obs_" + _hash([ident, start, end, local])[:20]
            mentions = _mentions(local, aliases, policy["context_window_tokens"])
            explicit = sorted({m["entity_id"] for m in mentions if m["accepted"]})
            rejected = {m["entity_id"] for m in mentions if not m["accepted"]}
            terms = _terms(local, stops)
            reset, returned = _cue(local, policy["reset_cues"]), _cue(local, policy["return_cues"])
            continuation = _cue(local, policy["continuation_cues"])
            shared = sorted(terms & active_terms)
            boundary = None
            if segment is None:
                boundary = "start"
            elif reset:
                boundary = "explicit_reset"
            elif returned:
                boundary = "explicit_return"
            elif explicit and set(explicit) != set(active):
                boundary = "anchor_change"
            elif active and rejected.intersection(active):
                boundary = "rejected_active_alias"
            elif not explicit and not continuation and len(terms) >= policy["novelty_minimum_terms"] and len(shared) < policy["minimum_shared_terms"]:
                boundary = "lexical_novelty_candidate"
            elif active and not explicit and unanchored >= policy["focus_max_unanchored_spans"]:
                boundary = "focus_expired"
            if boundary:
                prior = segment["id"] if segment else None
                return_targets = sorted({history[e] for e in explicit if e in history}) if returned else []
                segment = {"id": "seg_" + _hash([span_id, boundary])[:20], "boundary": boundary,
                           "boundary_observation_id": span_id, "previous_segment_id": prior,
                           "return_to_segment_ids": return_targets, "observation_ids": [],
                           "anchor_entity_ids": [], "status": "candidate"}
                segments.append(segment)
                active, active_terms, anchor_span_ids, unanchored = [], set(), [], 0
                shared = []
            prior_anchors = list(anchor_span_ids)
            own_support = []
            if explicit:
                focus, basis = explicit, "local_alias"
                active = explicit
                anchor_span_ids = [span_id]
                own_support = [span_id]
                unanchored = 0
                for entity in explicit:
                    history[entity] = segment["id"]
            else:
                unanchored += 1
                if active and len(shared) >= policy["minimum_shared_terms"]:
                    focus, basis = list(active), "corroborated_continuity_candidate"
                    own_support = prior_anchors + [span_id]
                elif active and continuation:
                    # A discourse cue alone is insufficient evidence of identity.
                    focus, basis = [], "unresolved_continuation"
                else:
                    focus, basis = [], "unanchored"
            obs = {"id": span_id, "segment_id": segment["id"], "text": local,
                   "source": {"turn_id": ident, "char_start": start, "char_end": end,
                              "byte_start": len(text[:start].encode("utf-8")),
                              "byte_end": len(text[:end].encode("utf-8")), "sha256": source_hash},
                   "role": turn.get("role", "unknown"), "mentions": mentions,
                   "focus_entity_ids": focus, "focus_basis": basis,
                   "reference_status": {"local_alias": "explicit_local_alias",
                                        "corroborated_continuity_candidate": "candidate_continuation",
                                        "unresolved_continuation": "unresolved",
                                        "unanchored": "no_reference"}[basis],
                   "focus_evidence_observation_ids": own_support,
                   "possible_continuation_entity_ids": list(active) if basis == "unresolved_continuation" else [],
                   "shared_terms": shared, "control_cue": reset or returned or continuation,
                   "boundary": boundary, "inference_status": "candidate", "confidence": None}
            observations.append(obs)
            segment["observation_ids"].append(span_id)
            segment["anchor_entity_ids"] = sorted(set(segment["anchor_entity_ids"]) | set(explicit))
            # Only the immediately preceding span can corroborate the next span;
            # arbitrary vocabulary accumulated in a long topic cannot license it.
            if focus:
                active_terms = terms
                if basis == "corroborated_continuity_candidate":
                    anchor_span_ids = own_support
    return {"version": VERSION, "policy": policy, "policy_hash": _hash(policy),
            "sources": source_records, "observations": observations, "segments": segments,
            "limitations": ["lexical_and_discourse_heuristics_only", "no_calibrated_confidence",
                            "no_general_coreference_or_semantic_identity", "no_graph_mutation"]}


def project_context(segmentation: dict, graph: dict, claims: list[dict] | None = None) -> dict:
    """Read-only one-hop graph lookup and proposed updates for grounded spans.

    Neighbors are contextual candidates, never inherited identities or premises.
    Claims use {id,text?,source:{turn_id,char_start,char_end}}. Invalid or partially
    out-of-range source spans are blocked; crossing topic spans stays ambiguous.
    """
    graph = deepcopy(graph)
    nodes, edges = graph.get("nodes", []), graph.get("edges", [])
    if not isinstance(nodes, list) or not isinstance(edges, list):
        raise ValueError("graph requires nodes/edges arrays")
    by_id = {}
    for node in nodes:
        ident = node.get("id")
        if not isinstance(ident, str) or not ident or ident in by_id:
            raise ValueError("graph node ids must be unique and nonempty")
        by_id[ident] = node
    for edge in edges:
        if edge.get("source") not in by_id or edge.get("target") not in by_id or not isinstance(edge.get("predicate"), str):
            raise ValueError("graph edge requires existing endpoints and predicate")
    budget = segmentation["policy"]["context_node_budget"]
    contexts, proposals, blocked = [], [], []
    observations = segmentation["observations"]
    for obs in observations:
        anchors = [ident for ident in obs["focus_entity_ids"] if ident in by_id]
        matches = {ident: {"node_id": ident, "reason": obs["focus_basis"], "via": [],
                           "existing_claim_ids": sorted(by_id[ident].get("claim_ids", []))} for ident in anchors}
        for edge in sorted(edges, key=lambda e: (e["source"], e["target"], e["predicate"])):
            for direct, other in ((edge["source"], edge["target"]), (edge["target"], edge["source"])):
                if direct not in anchors or other in anchors:
                    continue
                item = matches.setdefault(other, {"node_id": other, "reason": "graph_neighbor_only",
                                                   "via": [], "existing_claim_ids": sorted(by_id[other].get("claim_ids", []))})
                item["via"].append({"source": edge["source"], "target": edge["target"],
                                    "predicate": edge["predicate"], "claim_ids": deepcopy(edge.get("claim_ids", [])),
                                    "qualifiers": deepcopy(edge.get("qualifiers", {}))})
        ordered = sorted(matches.values(), key=lambda x: (x["node_id"] not in anchors, x["node_id"]))
        contexts.append({"observation_id": obs["id"], "segment_id": obs["segment_id"],
                         "candidates": ordered[:budget], "omitted_by_budget": max(0, len(ordered) - budget),
                         "evidence_observation_ids": obs["focus_evidence_observation_ids"],
                         "eligibility_as_premise": "not_assessed"})
        if obs["focus_entity_ids"]:
            proposals.append({"kind": "context_link", "observation_ids": [obs["id"]],
                              "subject_candidates": obs["focus_entity_ids"],
                              "evidence_observation_ids": obs["focus_evidence_observation_ids"],
                              "basis": obs["focus_basis"]})
        elif obs["boundary"] in {"explicit_reset", "lexical_novelty_candidate"}:
            proposals.append({"kind": "new_topic_review", "observation_ids": [obs["id"]],
                              "subject_candidates": [], "basis": obs["boundary"]})
    claim_ids = set()
    for claim in claims or []:
        ident, source = claim.get("id"), claim.get("source", {})
        if not isinstance(ident, str) or not ident or ident in claim_ids:
            raise ValueError("claim ids must be unique and nonempty")
        claim_ids.add(ident)
        start, end = source.get("char_start"), source.get("char_end")
        relevant = [o for o in observations if o["source"]["turn_id"] == source.get("turn_id")]
        source_record = next((s for s in segmentation["sources"] if s["turn_id"] == source.get("turn_id")), None)
        if type(start) is not int or type(end) is not int or start < 0 or start >= end or not source_record or end > source_record["n_chars"]:
            blocked.append({"claim_id": ident, "reason": "invalid_source_span"})
            continue
        covering = [o for o in relevant if o["source"]["char_start"] < end and o["source"]["char_end"] > start]
        if not covering:
            blocked.append({"claim_id": ident, "reason": "source_span_has_no_observation"})
            continue
        subjects = sorted({e for o in covering for e in o["focus_entity_ids"]})
        segments = sorted({o["segment_id"] for o in covering})
        proposals.append({"kind": "claim_context_review", "claim_id": ident,
                          "observation_ids": [o["id"] for o in covering], "segment_ids": segments,
                          "subject_candidates": subjects, "basis": "source_span_projection",
                          "crosses_topic_boundary": len(segments) > 1,
                          "requires_scope_review": len(segments) > 1 or not subjects,
                          "source": deepcopy(source)})
    duplicates = defaultdict(list)
    for obs in observations:
        # Exact wording is a view-compression proposal, never semantic claim merge.
        duplicates[obs["text"]].append(obs["id"])
    for text, identifiers in sorted(duplicates.items()):
        if len(identifiers) > 1:
            proposals.append({"kind": "compact_repeated_observation_view", "observation_ids": identifiers,
                              "basis": "exact_text_equality", "text_sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
                              "preserve_all_observations": True, "merge_claims": False})
    for proposal in proposals:
        proposal.update(status="candidate", automatic_mutation=False, persistable_claim=False,
                        confidence=None)
        proposal["id"] = "proposal_" + _hash(proposal)[:20]
    return {"version": VERSION, "graph_hash": _hash(graph), "contexts": contexts,
            "update_proposals": proposals, "blocked_claims": blocked, "mutations_applied": 0}


def analyze(record: dict, policy: dict | None = None) -> dict:
    result = segment_conversation(record.get("turns", []), record.get("entities", []), policy)
    result["conversation_id"] = record.get("id", "")
    result["context_projection"] = project_context(result, record.get("graph", {"nodes": [], "edges": []}),
                                                     record.get("claims", []))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="conversation/graph JSON; source is not modified")
    parser.add_argument("--policy", type=Path, help="optional policy override JSON")
    args = parser.parse_args()
    record = json.loads(args.input.read_text(encoding="utf-8"))
    policy = json.loads(args.policy.read_text(encoding="utf-8")) if args.policy else None
    print(json.dumps(analyze(record, policy), ensure_ascii=False, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
