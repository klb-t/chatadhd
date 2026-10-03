#!/usr/bin/env python3
"""Source-located alternative envelopes crossing tentative segmentation cuts.

The unchanged parser sees original physical lines. Alternatives never replace
topic observations, share their combined focus, or enter main coverage/proofs.
"""
from __future__ import annotations

from collections import defaultdict
from copy import deepcopy
import hashlib
import json

try:
    from .extract import VERSION as EXTRACT_VERSION, extract_record, _candidate_graph
except ImportError:
    from extract import VERSION as EXTRACT_VERSION, extract_record, _candidate_graph

VERSION = "boundary-alternative-envelopes/1"


def _hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                    separators=(",", ":")).encode("utf-8")).hexdigest()


def _span(text, turn_id, start, end):
    return {"turn_id": turn_id, "char_start": start, "char_end": end,
            "byte_start": len(text[:start].encode("utf-8")), "byte_end": len(text[:end].encode("utf-8")),
            "quote": text[start:end], "sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
            "coordinate_space": "input.turns[].text"}


def _check_span(text, span, *, quote=None):
    if not isinstance(span, dict):
        raise ValueError("source span must be an object")
    a, b = span.get("char_start"), span.get("char_end")
    if type(a) is not int or type(b) is not int or not 0 <= a <= b <= len(text):
        raise ValueError("invalid source character offsets")
    if quote is not None and text[a:b] != quote:
        raise ValueError("source quote mismatch")
    if "quote" in span and span["quote"] != text[a:b]:
        raise ValueError("source span quote mismatch")
    if span.get("byte_start") != len(text[:a].encode("utf-8")) or span.get("byte_end") != len(text[:b].encode("utf-8")):
        raise ValueError("source UTF-8 offsets mismatch")
    if "sha256" in span and span["sha256"] != hashlib.sha256(text.encode("utf-8")).hexdigest():
        raise ValueError("source text hash mismatch")
    return a, b


def _primary_index(primary_extractions, turns, conversation_id):
    index = defaultdict(list)
    for extraction in primary_extractions:
        supplied = extraction.get("input", {})
        if supplied.get("conversation_id") != conversation_id:
            raise ValueError("primary extraction belongs to another or unspecified conversation")
        turn_id = supplied.get("turn_id")
        if turn_id not in turns:
            raise ValueError("primary extraction references an unknown source turn")
        text = turns[turn_id]["text"]
        parent = supplied.get("source_span")
        if parent is not None:
            if parent.get("turn_id", turn_id) != turn_id:
                raise ValueError("primary parent span turn mismatch")
            base, _ = _check_span(text, parent, quote=supplied.get("text"))
        else:
            base = None
        for candidate in extraction.get("candidates", []):
            local = candidate.get("span", {})
            local_text = supplied.get("text")
            if not isinstance(local_text, str):
                raise ValueError("primary extraction must retain its input text")
            _check_span(local_text, local)
            source = candidate.get("source_span")
            if source is not None:
                if source.get("turn_id") != turn_id:
                    raise ValueError("primary candidate turn mismatch")
                a, b = _check_span(text, source, quote=local["quote"])
                if base is not None and (a, b) != (base + local["char_start"], base + local["char_end"]):
                    raise ValueError("primary local and parent source coordinates disagree")
            elif base is not None:
                a, b = base + local["char_start"], base + local["char_end"]
                if text[a:b] != local["quote"]:
                    raise ValueError("primary derived source quote mismatch")
            else:
                raise ValueError("primary candidate lacks a verifiable turn location")
            key = (turn_id, a, b, candidate.get("operation"))
            index[key].append({"id": candidate.get("id"), "signature": _signature(candidate)})
    return index


def _signature(candidate):
    return {"operation": candidate.get("operation"), "statement_type": candidate.get("statement_type"),
            "slots": {key: slot.get("text") for key, slot in candidate.get("slots", {}).items()},
            "formula_candidate": candidate.get("formula_candidate")}


def recover_envelopes(record, segmentation, primary_extractions):
    """Return a separate, deduplicated alternative projection over original text.

    Input uses pipeline/topics conversation records and extract_record results.
    No segment is combined or selected for a recovered candidate. Every local
    observation's focus remains a separate set requiring scope/identity review.
    """
    if not isinstance(record, dict) or not isinstance(segmentation, dict) or not isinstance(primary_extractions, list):
        raise ValueError("record/segmentation must be objects and primary_extractions an array")
    raw_turns = record.get("turns", [])
    if not isinstance(raw_turns, list):
        raise ValueError("record.turns must be an array")
    turns = {}
    for turn in raw_turns:
        ident, text = turn.get("id"), turn.get("text")
        if not isinstance(ident, str) or not ident or ident in turns or not isinstance(text, str):
            raise ValueError("source turns require unique nonempty IDs and text")
        turns[ident] = turn
    if "conversation_id" in segmentation and segmentation["conversation_id"] != record.get("id", ""):
        raise ValueError("segmentation belongs to a different declared conversation")
    for source in segmentation.get("sources", []):
        turn = turns.get(source.get("turn_id"))
        if turn is None:
            raise ValueError("segmentation source references an unknown turn")
        text = turn["text"]
        if source.get("sha256") != hashlib.sha256(text.encode("utf-8")).hexdigest() or source.get("n_chars") != len(text) or source.get("n_bytes") != len(text.encode("utf-8")):
            raise ValueError("segmentation source hash/length differs from original turn")
    observed, observation_ids = defaultdict(list), set()
    observations = segmentation.get("observations", [])
    if not isinstance(observations, list):
        raise ValueError("segmentation.observations must be an array")
    for observation in observations:
        ident, source = observation.get("id"), observation.get("source", {})
        turn_id = source.get("turn_id")
        if not isinstance(ident, str) or not ident or ident in observation_ids or turn_id not in turns:
            raise ValueError("segmentation observation identity/location is invalid")
        observation_ids.add(ident)
        if not isinstance(observation.get("text"), str):
            raise ValueError("segmentation observation must retain its source text")
        _check_span(turns[turn_id]["text"], source, quote=observation["text"])
        if not isinstance(observation.get("segment_id"), str) or not observation["segment_id"]:
            raise ValueError("segmentation observation requires a segment ID")
        observed[turn_id].append(observation)
    for items in observed.values():
        items.sort(key=lambda item: (item["source"]["char_start"], item["source"]["char_end"], item["id"]))
    conversation_id = record.get("id") or "local_conversation_" + _hash(record)[:20]
    primary = _primary_index(primary_extractions, turns, conversation_id)
    alternatives, suppressed, turn_extractions, unknown = [], [], [], []
    for turn_id, turn in turns.items():
        text = turn["text"]
        extraction = extract_record({"id": "alternative_turn_" + _hash([conversation_id, turn_id])[:20],
                                     "source_id": record.get("id"), "conversation_id": conversation_id,
                                     "turn_id": turn_id, "text": text, "role": turn.get("role"),
                                     "source_span": _span(text, turn_id, 0, len(text)),
                                     "scope_status": "unassigned_alternative_source_boundary"})
        turn_extractions.append(extraction)
        for item in extraction["unknown"]:
            span = item["span"]
            unknown.append({**deepcopy(item), "turn_id": turn_id,
                            "source_span": _span(text, turn_id, span["char_start"], span["char_end"])})
        for candidate in extraction["candidates"]:
            local = candidate["span"]
            a, b = local["char_start"], local["char_end"]
            key = (turn_id, a, b, candidate["operation"])
            if key in primary:
                if any(item["signature"] != _signature(candidate) for item in primary[key]):
                    raise ValueError("same-span primary operation has a conflicting interpretation payload")
                suppressed.append({"turn_id": turn_id, "char_start": a, "char_end": b,
                                   "operation": candidate["operation"], "candidate_id": candidate["id"],
                                   "primary_candidate_ids": sorted(item["id"] for item in primary[key])})
                continue
            overlaps, cuts = [], set()
            for observation in observed[turn_id]:
                source = observation["source"]
                c, d = source["char_start"], source["char_end"]
                if c >= b or d <= a:
                    continue
                overlap_start, overlap_end = max(a, c), min(b, d)
                cuts.update(point for point in (c, d) if a < point < b)
                overlaps.append({"observation_id": observation["id"], "segment_id": observation["segment_id"],
                                 "source": _span(text, turn_id, c, d),
                                 "overlap": _span(text, turn_id, overlap_start, overlap_end),
                                 "focus_entity_ids": deepcopy(observation.get("focus_entity_ids", [])),
                                 "focus_basis": observation.get("focus_basis"),
                                 "focus_evidence_observation_ids": deepcopy(observation.get("focus_evidence_observation_ids", [])),
                                 "reference_status": observation.get("reference_status"),
                                 "boundary": observation.get("boundary"),
                                 "scope_status": observation.get("inference_status", "candidate")})
            segments = sorted({item["segment_id"] for item in overlaps})
            result = {**deepcopy(candidate), "source_span": _span(text, turn_id, a, b),
                      "kind": "alternative_source_envelope", "extraction_status": "unchecked",
                      "scope": {"conversation_id": conversation_id, "turn_id": turn_id,
                                "segment_id": None, "overlapping_segment_ids": segments},
                      "overlapping_observations": overlaps,
                      "segmentation_cut_char_offsets": sorted(cuts),
                      "spans_segmentation_cut": bool(cuts), "crosses_topic_boundary": len(segments) > 1,
                      "observation_mapping_status": "overlap_only_unverified" if overlaps else "no_observation_overlap",
                      "requires_scope_review": True, "identity_verified": False,
                      "scope_interpretation": "local_focus_sets_are_alternatives_not_one_combined_subject",
                      "projection_only": True, "counted_in_primary_coverage": False,
                      "confidence": None, "persistable_claim": False, "inference_eligible": False,
                      "automatic_mutation": False}
            result["id"] = "boundary_alternative_" + _hash([VERSION, result])[:20]
            alternatives.append(result)
    return {"version": VERSION, "extractor_version": EXTRACT_VERSION, "input": deepcopy(record),
            "segmentation_hash": _hash(segmentation), "primary_extractions_hash": _hash(primary_extractions),
            "alternatives": alternatives, "primary_matches_suppressed": suppressed,
            "turn_extractions": turn_extractions, "unknown_physical_lines": unknown,
            "alternative_projection": {"structure": _candidate_graph(alternatives),
                                       "interpretation": "separate_operation_envelopes; no_shared_scope_or_identity"},
            "diagnostic": {"source_turns": len(turns), "alternative_envelopes": len(alternatives),
                           "suppressed_primary_matches": len(suppressed),
                           "alternatives_spanning_segmentation_cuts": sum(x["spans_segmentation_cut"] for x in alternatives),
                           "alternatives_crossing_topic_boundaries": sum(x["crosses_topic_boundary"] for x in alternatives),
                           "unknown_physical_lines": len(unknown), "primary_coverage_unchanged": True},
            "status": "alternative_projection_only", "confidence": None, "persistable_claim": False,
            "inference_eligible": False, "automatic_mutation": False}
