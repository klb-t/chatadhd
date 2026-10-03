#!/usr/bin/env python3
"""Bounded offline text-to-structure research adapter, not a semantic parser.

Only full, enumerated EN/PL envelopes are recognized. Their slots remain opaque
source text. Restricted copular clauses also produce *unchecked* logical
candidates. These are never silently promoted to Loom claims or premises.
Unsupported content remains in the result, with exact spans and a reason.

Offsets are half-open Unicode-character and UTF-8-byte offsets in ``input.text``.
An upstream locator/span is retained verbatim, not reinterpreted as a raw-source
byte coordinate. Give already topic-segmented records separately: this adapter
never merges records, turns, topic scopes, or physical lines.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
from typing import Any

try:
    from .structure_methods import compare, formula_graph, validate_formula, validate_graph
except ImportError:
    from structure_methods import compare, formula_graph, validate_formula, validate_graph

VERSION = "bounded-text-structure/2"
FLAGS = re.IGNORECASE | re.UNICODE
WORD = r"[^\W\d_][\w-]*"
OPERATION_FAMILIES = {"conditional": "conditional_branch", "branch": "conditional_branch",
                      "exception": "exception", "universal_inclusion": "category_inclusion",
                      "type_inclusion": "category_inclusion", "comparison": "comparison",
                      "goal_constraint": "goal_constraint", "assertion": "predicate"}
# This list gates *logical interpretation*, not detection of an opaque envelope.
# In particular neither negation nor modal/quantifier scope is guessed.
LOGICAL_UNSAFE = re.compile(
    r"\b(?:not|no|never|neither|nor|only|unless|except|maybe|possibly|perhaps|"
    r"may|might|must|should|could|would|can|will|shall|some|any|all|every|each|"
    r"none|nobody|someone|anyone|everyone|everything|nothing|something|"
    r"and|or|if|then|because|it|this|that|they|we|i|he|she|"
    r"nie|nigdy|żaden|żadna|żadne|każdy|każda|każde|wszystkie|wszyscy|"
    r"niektóre|niektórzy|może|musi|muszą|powinien|powinna|powinni|"
    r"tylko|chyba|albo|lub|oraz|i|jeśli|jeżeli|ponieważ|on|ona|ono|to)\b",
    FLAGS,
)


def _canonical(value: Any) -> str:
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


def _id(prefix: str, value: Any) -> str:
    return prefix + hashlib.sha256(_canonical(value).encode("utf-8")).hexdigest()[:20]


def _span(text: str, start: int, end: int) -> dict:
    return {"char_start": start, "char_end": end,
            "byte_start": len(text[:start].encode("utf-8")),
            "byte_end": len(text[:end].encode("utf-8")),
            "coordinate_space": "input.text", "quote": text[start:end]}


def _trim_span(text: str, start: int, end: int) -> tuple[int, int]:
    while start < end and text[start].isspace():
        start += 1
    while end > start and text[end - 1].isspace():
        end -= 1
    return start, end


def _lines(text: str):
    """Conservative segmentation: no guessed sentence/topic boundaries."""
    offset = 0
    for line in text.splitlines(keepends=True):
        start, end = _trim_span(text, offset, offset + len(line))
        if start < end:
            yield start, end
        offset += len(line)


def _symbol(value: str) -> str:
    # No stemming, lemmatization, coreference, alias resolution, or translation.
    return " ".join(value.casefold().split())


def _atomic(text: str, lang: str) -> dict | None:
    if LOGICAL_UNSAFE.search(text):
        return None
    copula = "is" if lang == "en" else "jest"
    match = re.fullmatch(rf"(?P<subject>{WORD})\s+{copula}\s+(?P<property>{WORD})", text, FLAGS)
    if not match:
        return None
    return {"op": "atom", "predicate": "property:" + _symbol(match["property"]),
            "args": [_symbol(match["subject"])]}


# Grammar is intentionally small and visible. A successful envelope match is
# not a claim that the opaque clauses' entire semantics has been interpreted.
PATTERNS = [
    ("en.if_then_else", "en", "branch", "conditional", r"If (?P<condition>.+), then (?P<consequence>.+), else (?P<alternative>.+)"),
    ("pl.if_then_else", "pl", "branch", "conditional", r"(?:Jeśli|Jeżeli) (?P<condition>.+), to (?P<consequence>.+), w przeciwnym razie (?P<alternative>.+)"),
    ("en.if_then", "en", "conditional", "conditional", r"If (?P<condition>.+), then (?P<consequence>.+)"),
    ("pl.if_then", "pl", "conditional", "conditional", r"(?:Jeśli|Jeżeli) (?P<condition>.+), to (?P<consequence>.+)"),
    ("en.unless", "en", "exception", "default_with_exception", r"(?P<default>.+), unless (?P<exception>.+)"),
    ("pl.unless", "pl", "exception", "default_with_exception", r"(?P<default>.+), chyba że (?P<exception>.+)"),
    ("en.all_are", "en", "universal_inclusion", "universal_assertion", r"All (?P<subclass>.+) are (?P<superclass>.+)"),
    ("pl.all_are", "pl", "universal_inclusion", "universal_assertion", r"Wszystkie (?P<subclass>.+) są (?P<superclass>.+)"),
    ("en.kind_of", "en", "type_inclusion", "type_inclusion", r"(?P<subclass>.+) is a kind of (?P<superclass>.+)"),
    ("pl.kind_of", "pl", "type_inclusion", "type_inclusion", r"(?P<subclass>.+) jest rodzajem (?P<superclass>.+)"),
    ("en.more_than", "en", "comparison", "comparison", r"(?P<left>.+) is more (?P<dimension>[^\W\d_][\w-]*) than (?P<right>.+)"),
    ("pl.more_than", "pl", "comparison", "comparison", r"(?P<left>.+) jest bardziej (?P<dimension>[^\W\d_][\w-]*) niż (?P<right>.+)"),
    ("en.goal_constraint", "en", "goal_constraint", "goal_with_constraint", r"Goal: (?P<goal>.+); constraint: (?P<constraint>.+)"),
    ("pl.goal_constraint", "pl", "goal_constraint", "goal_with_constraint", r"Cel: (?P<goal>.+); ograniczenie: (?P<constraint>.+)"),
    ("en.want_subject_to", "en", "goal_constraint", "goal_with_constraint", r"We want to (?P<goal>.+), subject to (?P<constraint>.+)"),
    ("pl.goal_limited", "pl", "goal_constraint", "goal_with_constraint", r"Celem jest (?P<goal>.+); ograniczeniem jest (?P<constraint>.+)"),
    ("en.atomic", "en", "assertion", "assertion", rf"(?P<subject>{WORD}) is (?P<property>{WORD})"),
    ("pl.atomic", "pl", "assertion", "assertion", rf"(?P<subject>{WORD}) jest (?P<property>{WORD})"),
]
COMPILED = [(name, lang, operation, statement, re.compile(pattern, FLAGS))
            for name, lang, operation, statement, pattern in PATTERNS]


def _formula(operation: str, slots: dict, lang: str) -> tuple[dict | None, str]:
    if operation == "conditional":
        left, right = (_atomic(slots[key], lang) for key in ("condition", "consequence"))
        if left and right:
            return {"op": "implies", "left": left, "right": right}, "restricted_copular_clauses"
    elif operation == "assertion":
        phrase = slots["subject"] + (" is " if lang == "en" else " jest ") + slots["property"]
        atom = _atomic(phrase, lang)
        if atom:
            return atom, "restricted_copular_clause"
    elif operation == "universal_inclusion":
        # No plural/singular coercion. Class labels are opaque lexical symbols.
        if all(re.fullmatch(rf"{WORD}(?: {WORD}){{0,2}}", value, FLAGS)
               and not LOGICAL_UNSAFE.search(value) for value in slots.values()):
            return {"op": "forall", "var": "_member", "body": {
                "op": "implies",
                "left": {"op": "atom", "predicate": "class:" + _symbol(slots["subclass"]), "args": ["_member"]},
                "right": {"op": "atom", "predicate": "class:" + _symbol(slots["superclass"]), "args": ["_member"]}}}, "restricted_universal_classes"
    return None, "logical_semantics_outside_grammar"


def _recognize(text: str, start: int, end: int) -> tuple[dict | None, str]:
    body_end = end - 1 if text[end - 1] == "." else end
    body = text[start:body_end]
    if any(char in body for char in ".!?\n\r"):
        return None, "sentence_boundary_or_punctuation_outside_grammar"
    for name, lang, operation, statement, pattern in COMPILED:
        match = pattern.fullmatch(body)
        if not match:
            continue
        slots = {}
        for role in match.groupdict():
            local_start, local_end = match.span(role)
            a, b = _trim_span(text, start + local_start, start + local_end)
            slots[role] = {"text": text[a:b], "span": _span(text, a, b)}
        # A nested delimiter defeats this bounded parser. Preserve it as unknown
        # instead of matching a misleading fragment or choosing an attachment.
        if any(re.search(r"[,;]|\b(?:then|else|w przeciwnym razie)\b", slot["text"], FLAGS)
               for slot in slots.values()):
            return None, "nested_or_ambiguous_clause_attachment"
        formula, formula_reason = _formula(operation, {k: v["text"] for k, v in slots.items()}, lang)
        if operation == "assertion" and formula is None:
            return None, "negation_modality_quantification_or_reference_outside_grammar"
        if formula:
            validate_formula(formula)
        return {"operation": operation, "operation_family": OPERATION_FAMILIES[operation],
                "operation_status": "representation_only_no_performed_operation_inferred",
                "statement_type": statement, "language": lang,
                "slots": slots, "span": _span(text, start, end),
                "method": name, "confidence": None, "calibration_status": "unavailable",
                "formula_candidate": formula, "formula_status": "unchecked" if formula else "abstained",
                "formula_reason": formula_reason, "interpretation": "recognized_envelope_with_opaque_slots"}, "recognized"
    return None, "no_supported_envelope"


def _candidate_graph(candidates: list[dict]) -> dict:
    """A labeled operation/slot graph; source text is outside graph labels.

    Slot vertices are separate even for equal words. Cross-record coreference
    would be an unsupported inference; comparison may map vertices consistently.
    """
    graph = {"nodes": [], "edges": [], "projection": "bounded_operation_slots"}
    role_map = {"goal": "intent", "constraint": "constraint", "condition": "constraint",
                "exception": "constraint", "consequence": "output", "alternative": "output",
                "default": "output", "dimension": "check"}
    for candidate in candidates:
        ident = candidate["id"]
        graph["nodes"].append({"id": ident, "kind": "thought_operation", "role": "transformation",
                               "qualifiers": {"operation": candidate["operation"],
                                              "statement_type": candidate["statement_type"]},
                               "provenance": {"candidate_id": ident, "span": candidate["span"]}})
        for role, slot in candidate["slots"].items():
            slot_id = ident + ":" + role
            graph["nodes"].append({"id": slot_id, "kind": "opaque_source_slot", "role": role_map.get(role, ""),
                                   "qualifiers": {"argument_role": role}, "text": slot["text"],
                                   "provenance": {"candidate_id": ident, "span": slot["span"]}})
            graph["edges"].append({"source": ident, "target": slot_id, "predicate": role, "qualifiers": {}})
    validate_graph(graph)
    return graph


def extract_record(record: dict, *, policy: str = "bounded") -> dict:
    """Extract independent research candidates from one located text record.

    ``text`` must be explicitly supplied; a claim's arbitrary literal ``value``
    is never assumed to be a source quote. ``id``/``record_id``, ``source_id``,
    ``turn_id``, ``topic_id``, ``unit``, ``locator``, ``source_span``, assessment
    and any unknown fields survive unchanged under ``input``. Offsets always
    refer to the preserved input text. Missing source identity is explicit.
    The default ``bounded`` policy retains the original physical-line grammar.
    ``explicit_relations`` opts into a separate research envelope projection
    with reversible Markdown/line-wrap mapping and no logical formulas.
    """
    if policy == "explicit_relations":
        try:
            from .relation_envelopes import extract_relations
        except ImportError:
            from relation_envelopes import extract_relations
        return extract_relations(record)
    if policy != "bounded":
        raise ValueError("policy must be bounded or explicit_relations")
    if not isinstance(record, dict) or not isinstance(record.get("text"), str):
        raise ValueError("record requires an explicit text string; use extract_claim with located observations")
    text = record["text"]
    locator = record.get("locator") if isinstance(record.get("locator"), dict) else {}
    source_id = record.get("source_id") or record.get("source") or locator.get("source")
    record_id = record.get("record_id") or record.get("id") or _id("text_", record)
    scope = {key: deepcopy(record.get(key)) for key in ("turn_id", "topic_id", "segment_id", "unit")}
    candidates, unknown = [], []
    eligible_chars = 0
    for start, end in _lines(text):
        eligible_chars += end - start
        candidate, reason = _recognize(text, start, end)
        if candidate:
            candidate["id"] = _id("candidate_", [VERSION, record_id, source_id, scope, candidate])
            candidate["record_id"] = record_id
            candidate["source_id"] = source_id
            candidate["scope"] = deepcopy(scope)
            candidates.append(candidate)
        else:
            unknown.append({"span": _span(text, start, end), "status": "abstained", "reason": reason,
                            "statement_type": "unknown", "confidence": None})
    recognized_chars = sum(c["span"]["char_end"] - c["span"]["char_start"] for c in candidates)
    status = "parsed" if candidates and not unknown else "partial" if candidates else "abstained"
    return {"version": VERSION, "id": _id("extraction_", [VERSION, record]),
            "record_id": record_id, "source_id": source_id,
            "source_identity_status": "supplied_unverified" if source_id else "missing",
            "text": text, "text_sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
            "input": deepcopy(record), "scope": scope, "status": status,
            "candidates": candidates, "unknown": unknown, "structure": _candidate_graph(candidates),
            "coverage": {"eligible_characters": eligible_chars, "recognized_envelope_characters": recognized_chars,
                         "envelope_character_coverage": recognized_chars / eligible_chars if eligible_chars else None,
                         "physical_units": len(candidates) + len(unknown), "recognized_envelopes": len(candidates),
                         "logical_candidates": sum(c["formula_candidate"] is not None for c in candidates),
                         "semantic_accuracy": None, "interpretation": "grammar_coverage_not_gold_structure_accuracy"},
            "confidence": None, "calibration_status": "unavailable", "persistable_claim": False}


def extract_claim(claim: dict, observations: dict[str, dict]) -> dict:
    """Follow actual observation references, retaining claim and support exactly.

    Each referenced observation is extracted independently. No source is invented
    for a missing observation and no claim text/value substitutes for its quote.
    """
    supports = claim.get("assessment", {}).get("basis", {}).get("support", [])
    if not isinstance(supports, list):
        raise ValueError("claim assessment.basis.support must be a list")
    extracted, unknown = [], []
    for index, support in enumerate(supports):
        observation_id = support.get("observation", support.get("observation_id")) if isinstance(support, dict) else None
        observation = observations.get(observation_id)
        if observation is None:
            unknown.append({"support_index": index, "observation_id": observation_id, "reason": "observation_unavailable"})
            continue
        if not isinstance(observation.get("text"), str):
            unknown.append({"support_index": index, "observation_id": observation_id, "reason": "observation_text_unavailable"})
            continue
        quote = support.get("quote")
        if not isinstance(quote, str) or not quote:
            unknown.append({"support_index": index, "observation_id": observation_id, "reason": "support_quote_unavailable"})
            continue
        start = observation["text"].find(quote)
        if start < 0:
            unknown.append({"support_index": index, "observation_id": observation_id, "reason": "support_quote_not_in_observation"})
            continue
        if observation["text"].find(quote, start + 1) >= 0:
            unknown.append({"support_index": index, "observation_id": observation_id, "reason": "support_quote_location_ambiguous"})
            continue
        extracted.append(extract_record({**deepcopy(observation), "text": quote, "claim_id": claim.get("id"),
                                         "observation_id": observation_id, "claim_support": deepcopy(support),
                                         "observation_text": observation["text"],
                                         "support_span_in_observation": _span(observation["text"], start, start + len(quote)),
                                         "claim_assessment": deepcopy(claim.get("assessment"))}))
    return {"version": VERSION, "claim": deepcopy(claim), "extractions": extracted, "unknown": unknown,
            "status": "located" if extracted else "abstained", "persistable_claim": False}


def candidate_logic(extraction: dict, *, accept_unchecked: bool = False) -> list[dict]:
    """Explicit opt-in for a conditional research experiment, never source truth.

    There is deliberately no Assessment: an unchecked projection does not yet
    meet the Claim contract. The formula cannot become a premise for ``infer``.
    The original upstream assessment remains in provenance for human review.
    """
    if not accept_unchecked:
        raise ValueError("logical candidates are unchecked; explicitly accept_unchecked for projection experiments")
    return [{"claim_id": candidate["id"], "formula": deepcopy(candidate["formula_candidate"]),
             "qualifiers": {"scope": candidate["id"]}, "extraction_status": "unchecked",
             "confidence": None, "calibration_status": "unavailable", "conditional_on_extraction": True,
             "provenance": {"source_id": candidate["source_id"], "record_id": candidate["record_id"],
                            "scope": deepcopy(candidate["scope"]), "span": deepcopy(candidate["span"]),
                            "method": candidate["method"], "version": VERSION}}
            for candidate in extraction["candidates"] if candidate["formula_candidate"] is not None]


def _independent_formula_graph(extraction: dict) -> dict:
    """Project each formula separately, retaining only within-formula identity."""
    result = {"nodes": [], "edges": []}
    for entry in candidate_logic(extraction, accept_unchecked=True):
        # Source-specific scope controls eligibility, not the comparable shape.
        graph = formula_graph([{**entry, "qualifiers": {}}])
        prefix = entry["claim_id"] + ":"
        for node in graph["nodes"]:
            node["id"] = prefix + node["id"]
            result["nodes"].append(node)
        for edge in graph["edges"]:
            edge["source"] = prefix + edge["source"]
            edge["target"] = prefix + edge["target"]
            result["edges"].append(edge)
    return result


def compare_extractions(left: dict, right: dict, *, projection: str = "operations") -> dict:
    if projection == "operations":
        result = compare(left, right)
    elif projection == "logical_candidates":
        result = compare({"text": left["text"], "structure": _independent_formula_graph(left)},
                         {"text": right["text"], "structure": _independent_formula_graph(right)})
    else:
        raise ValueError("projection must be operations or logical_candidates")
    result["coverage"] = {"left": deepcopy(left["coverage"]), "right": deepcopy(right["coverage"]),
                          "grounding": "exact_input_spans_checked; source_identity_not_verified"}
    result["projection"] = projection
    result["validity"] = "not_assessed; similarity_conditional_on_bounded_extraction"
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="JSON array of independent {id,source_id,text,...} records")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compare", nargs=2, metavar=("LEFT_ID", "RIGHT_ID"))
    parser.add_argument("--policy", choices=("bounded", "explicit_relations"), default="bounded")
    args = parser.parse_args()
    records = json.loads(args.input.read_text(encoding="utf-8"))
    if not isinstance(records, list):
        parser.error("input must be a JSON array")
    results = [extract_record(record, policy=args.policy) for record in records]
    output_version = (results[0]["version"] if results else
                      extract_record({"text": ""}, policy=args.policy)["version"])
    output = {"version": output_version, "extractions": results}
    if args.policy != "bounded":
        output["policy"] = args.policy
    if args.compare:
        by_id = {result["record_id"]: result for result in results}
        try:
            left, right = (by_id[key] for key in args.compare)
        except KeyError as exc:
            parser.error(f"comparison ID missing: {exc}")
        output["comparison"] = {projection: compare_extractions(left, right, projection=projection)
                                for projection in ("operations", "logical_candidates")}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
