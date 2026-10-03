"""Explicit relation-envelope research channel with reversible source mapping.

This channel enumerates source wording, not relation truth or complete logical
semantics. It does not veto the legacy bounded channel. Formula projection is
deliberately unavailable, and every operand remains an opaque exact source
substring. A detection projection may remove paired Markdown emphasis and fold
physical line wrapping; its codepoint-to-source map is retained in each unit.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re

try:
    from .extract import _candidate_graph, _id, _span, _trim_span
except ImportError:
    from extract import _candidate_graph, _id, _span, _trim_span

VERSION = "explicit-relation-envelopes/1"
POLICY_PATH = Path(__file__).with_name("relation_envelopes_policy.json")
FLAGS = re.IGNORECASE | re.UNICODE


def load_policy(path: Path = POLICY_PATH) -> dict:
    """Load only the selected policy, never any fixture or evaluation labels."""
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != "loom.research.relation_envelope_policy/1":
        raise ValueError("unsupported relation-envelope policy schema")
    if not isinstance(value.get("patterns"), list):
        raise ValueError("relation-envelope policy requires patterns")
    names = set()
    for entry in value["patterns"]:
        name = entry.get("id")
        if not isinstance(name, str) or name in names:
            raise ValueError("policy pattern IDs must be unique strings")
        names.add(name)
        regex = re.compile(entry["pattern"], FLAGS)
        if "cue" not in regex.groupindex or not set(entry["roles"]) <= regex.groupindex.keys():
            raise ValueError("policy pattern requires named cue and operand groups")
    return value


def _blocks(text: str):
    """Paragraph/list boundaries are retained; never merge fenced/quoted text."""
    offset = 0
    pending = None
    fenced = None
    for line in text.splitlines(keepends=True):
        start, end = _trim_span(text, offset, offset + len(line))
        stripped = text[start:end]
        fence = re.match(r"(`{3,}|~{3,})", stripped)
        # A fence is never interpreted as an asserted relation, including its
        # opening/closing line. Closing markers must use the opening delimiter.
        if fence or fenced:
            if pending:
                yield (*pending, None)
                pending = None
            if start < end:
                yield (start, end, "fenced_code_context")
            if fence:
                marker = fence[1][0]
                if fenced and marker == fenced[0] and len(fence[1]) >= fenced[1]:
                    fenced = None
                elif not fenced:
                    fenced = (marker, len(fence[1]))
            offset += len(line)
            continue
        boundary = not stripped or re.match(r"(?:#{1,6}\s|>\s?|(?:[-+*]|\d+[.)])\s)", stripped)
        if boundary and pending:
            yield (*pending, None)
            pending = None
        if not stripped:
            offset += len(line)
            continue
        if stripped.startswith(">"):
            yield (start, end, "quoted_block_context")
        elif re.match(r"#{1,6}\s", stripped):
            yield (start, end, "heading_context")
        else:
            bullet = re.match(r"(?:[-+*]|\d+[.)])\s+", stripped)
            if bullet:
                start += bullet.end()
            if pending:
                pending = (pending[0], end)
            else:
                pending = (start, end)
        offset += len(line)
    if pending:
        yield (*pending, None)


def _projection(text: str, start: int, end: int) -> tuple[str, list[list[int]]]:
    """Each projected codepoint maps to one nonempty original character range.

    All removed characters remain in the preserved source and bounding spans.
    No lowercasing, stemming, translation, or punctuation rewriting occurs.
    """
    removed = set()
    raw = text[start:end]
    # Only paired emphasis delimiters are presentation. Unmatched delimiters
    # remain visible; underscores inside identifiers are never removed.
    for match in re.finditer(r"(\*\*|__|\*)(?=\S)(.+?\S|\S)\1", raw, re.DOTALL):
        a, b = match.span()
        n = len(match[1])
        removed.update(range(start + a, start + a + n))
        removed.update(range(start + b - n, start + b))
    chars, mapping = [], []
    i = start
    while i < end:
        if i in removed:
            i += 1
            continue
        if text[i].isspace():
            a = i
            while i < end and text[i].isspace():
                i += 1
            chars.append(" ")
            mapping.append([a, i])
        else:
            chars.append(text[i])
            mapping.append([i, i + 1])
            i += 1
    return "".join(chars), mapping


def _sentences(projected: str, mapping: list[list[int]]):
    """Conservative punctuation segmentation; reject unexplained inner dots.

    A period splits only before an uppercase/codepoint letter sentence start or
    end of block. Decimal points and identifiers therefore stay inside a unit,
    where the policy may abstain. This is a research segmentation proposal.
    """
    start = 0
    for match in re.finditer(r"[.!?](?=\s+[A-ZĄĆĘŁŃÓŚŹŻ]|$)", projected):
        end = match.end()
        a, b = start, end
        while a < b and projected[a].isspace():
            a += 1
        while b > a and projected[b - 1].isspace():
            b -= 1
        if a < b:
            yield projected[a:b], mapping[a:b]
        start = end
    a, b = start, len(projected)
    while a < b and projected[a].isspace():
        a += 1
    while b > a and projected[b - 1].isspace():
        b -= 1
    if a < b:
        yield projected[a:b], mapping[a:b]


def _located(text: str, projected: str, mapping: list[list[int]], a: int, b: int,
             *, trim: bool = True) -> dict:
    if trim:
        while a < b and projected[a].isspace():
            a += 1
        while b > a and projected[b - 1].isspace():
            b -= 1
    if a >= b:
        raise ValueError("empty source slot")
    start, end = mapping[a][0], mapping[b - 1][1]
    fragments = []
    for x, y in mapping[a:b]:
        if fragments and x == fragments[-1][1]:
            fragments[-1][1] = y
        else:
            fragments.append([x, y])
    return {"text": text[start:end], "span": _span(text, start, end),
            "normalized_text": projected[a:b],
            "source_fragments": [_span(text, x, y) for x, y in fragments]}


def _recognize(text: str, projected: str, mapping: list[list[int]], policy: dict):
    body = projected[:-1] if projected.endswith(".") else projected
    for guard in policy.get("unit_guards", []):
        if re.search(guard["pattern"], body, FLAGS):
            return None, guard["reason"]
    matches = []
    rejected = None
    for entry in policy["patterns"]:
        match = re.fullmatch(entry["pattern"], body, FLAGS)
        if not match:
            continue
        reason = None
        for guard in entry.get("guards", []):
            target = match[guard["role"]]
            found = bool(re.search(guard["pattern"], target, FLAGS))
            if found == guard.get("reject_on_match", True):
                reason = guard["reason"]
                break
        if reason:
            rejected = reason
            continue
        slots = {role: _located(text, projected, mapping, *match.span(role))
                 for role in entry["roles"]}
        candidate = {"operation": entry["operation"], "operation_family": entry["family"],
                     "operation_status": "representation_only_no_performed_operation_inferred",
                     "statement_type": "explicit_relation_source_envelope", "language": entry["language"],
                     "slots": slots, "cue": _located(text, projected, mapping, *match.span("cue"), trim=False),
                     "span": _span(text, mapping[0][0], mapping[-1][1]),
                     "method": entry["id"], "qualifiers": deepcopy(entry.get("qualifiers", {})),
                     "confidence": None, "calibration_status": "unavailable",
                     "formula_candidate": None, "formula_status": "abstained",
                     "formula_reason": "opaque_operands_not_logical_semantics",
                     "interpretation": "recognized_envelope_with_opaque_slots"}
        matches.append(candidate)
    # Competing attachments are unknown, never selected by declaration order.
    if len(matches) > 1:
        return None, "multiple_supported_envelopes_or_ambiguous_attachment"
    return (matches[0], "recognized") if matches else (None, rejected or "no_supported_envelope")


def extract_relations(record: dict, *, policy: dict | None = None) -> dict:
    if not isinstance(record, dict) or not isinstance(record.get("text"), str):
        raise ValueError("record requires an explicit text string")
    policy = deepcopy(policy) if policy is not None else load_policy()
    policy_sha256 = hashlib.sha256(json.dumps(policy, sort_keys=True, ensure_ascii=False,
                                            separators=(",", ":")).encode()).hexdigest()
    text = record["text"]
    locator = record.get("locator") if isinstance(record.get("locator"), dict) else {}
    source_id = record.get("source_id") or record.get("source") or locator.get("source")
    record_id = record.get("record_id") or record.get("id") or _id("text_", record)
    scope = {key: deepcopy(record.get(key)) for key in ("turn_id", "topic_id", "segment_id", "unit")}
    candidates, unknown, units = [], [], []
    for start, end, blocked in _blocks(text):
        projected, mapping = _projection(text, start, end)
        for unit_text, unit_map in _sentences(projected, mapping):
            span = _span(text, unit_map[0][0], unit_map[-1][1])
            units.append({"span": span, "projection": unit_text, "source_map": unit_map,
                          "segmentation_status": "proposed_not_topic_resolution"})
            candidate, reason = (None, blocked) if blocked else _recognize(text, unit_text, unit_map, policy)
            if candidate:
                candidate.update({"record_id": record_id, "source_id": source_id, "scope": deepcopy(scope),
                                  "known_at": deepcopy(record.get("known_at"))})
                candidate["id"] = _id("candidate_", [VERSION, policy_sha256, record_id, source_id, scope, candidate])
                candidates.append(candidate)
            else:
                unknown.append({"span": span, "status": "abstained", "reason": reason,
                                "statement_type": "unknown", "confidence": None})
    eligible = sum(unit["span"]["char_end"] - unit["span"]["char_start"] for unit in units)
    recognized = sum(c["span"]["char_end"] - c["span"]["char_start"] for c in candidates)
    status = "parsed" if candidates and not unknown else "partial" if candidates else "abstained"
    return {"version": VERSION, "id": _id("extraction_", [VERSION, policy_sha256, record]),
            "policy_id": policy["id"], "policy_sha256": policy_sha256,
            "record_id": record_id, "source_id": source_id,
            "source_identity_status": "supplied_unverified" if source_id else "missing",
            "text": text, "text_sha256": hashlib.sha256(text.encode()).hexdigest(),
            "input": deepcopy(record), "scope": scope, "status": status,
            "candidates": candidates, "unknown": unknown, "units": units,
            "structure": _candidate_graph(candidates),
            "coverage": {"eligible_characters": eligible, "recognized_envelope_characters": recognized,
                         "envelope_character_coverage": recognized / eligible if eligible else None,
                         "physical_units": len(units), "recognized_envelopes": len(candidates),
                         "logical_candidates": 0, "semantic_accuracy": None,
                         "interpretation": "grammar_coverage_not_gold_structure_accuracy"},
            "confidence": None, "calibration_status": "unavailable", "persistable_claim": False}
