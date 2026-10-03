#!/usr/bin/env python3
"""Explicitly scoped thought-composition experiment over unchecked extraction.

Shared literal symbols are an assumption, never established entity identity.
No parser changes, source mutation, inference eligibility or Claim persistence.
"""
from __future__ import annotations

from collections import defaultdict
from copy import deepcopy
import hashlib
import json

try:
    from .structure_methods import compare, formula_graph, validate_formula, validate_graph
    from .extract import _candidate_graph
except ImportError:
    from structure_methods import compare, formula_graph, validate_formula, validate_graph
    from extract import _candidate_graph

VERSION = "scoped-thought-projection/1"
BINDINGS = {"separate", "literal_within_scope"}


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _identifier(label, value):
    return label + hashlib.sha256(_canonical(value).encode("utf-8")).hexdigest()[:20]


def _required_string(value, name):
    if not isinstance(value, str) or not value.strip():
        raise ValueError(name + " must be an explicit nonempty string")
    return value


def _scope(extraction):
    supplied = extraction.get("input", {})
    if not isinstance(supplied, dict):
        raise ValueError("extraction.input must preserve the source record")
    conversation = _required_string(supplied.get("conversation_id"), "input.conversation_id")
    segment = _required_string(supplied.get("segment_id"), "input.segment_id")
    # Check all redundant fields. Source IDs cannot substitute for conversation
    # IDs: one raw archive can contain arbitrarily many conversations.
    for label, fields in [("extraction", extraction), ("extraction.scope", extraction.get("scope", {}))]:
        if not isinstance(fields, dict):
            raise ValueError(label + " must be an object")
        for key, expected in (("conversation_id", conversation), ("segment_id", segment)):
            if key in fields and fields[key] != expected:
                raise ValueError(label + "." + key + " conflicts with explicit input scope")
    return conversation, segment


def _verify_span(text, span):
    if not isinstance(span, dict) or span.get("coordinate_space") != "input.text":
        raise ValueError("candidate/unknown span must use input.text coordinates")
    a, b = span.get("char_start"), span.get("char_end")
    if type(a) is not int or type(b) is not int or not 0 <= a < b <= len(text):
        raise ValueError("invalid local source character span")
    if span.get("quote") != text[a:b]:
        raise ValueError("source quote does not match exact input span")
    if span.get("byte_start") != len(text[:a].encode("utf-8")) or span.get("byte_end") != len(text[:b].encode("utf-8")):
        raise ValueError("UTF-8 source offsets do not match character offsets")


def _union_graphs(graphs):
    result = {"nodes": [], "edges": []}
    for index, graph in enumerate(graphs):
        validate_graph(graph)
        prefix = f"component_{index}:"
        for node in graph["nodes"]:
            result["nodes"].append({**deepcopy(node), "id": prefix + node["id"]})
        for edge in graph["edges"]:
            result["edges"].append({**deepcopy(edge), "source": prefix + edge["source"], "target": prefix + edge["target"]})
    validate_graph(result)
    return result


def _symbol_occurrences(formula, candidate_id, path=(), bound=()):
    op = formula["op"]
    if op == "atom":
        yield {"kind": "predicate_symbol", "symbol": formula["predicate"],
               "candidate_id": candidate_id, "path": list(path + ("predicate",))}
        for pos, term in enumerate(formula["args"]):
            if term not in bound:
                yield {"kind": "constant", "symbol": term, "candidate_id": candidate_id,
                       "path": list(path + ("args", pos))}
    elif op in {"forall", "exists"}:
        yield from _symbol_occurrences(formula["body"], candidate_id, path + ("body",), bound + (formula["var"],))
    elif op in {"not", "modal"}:
        yield from _symbol_occurrences(formula["arg"], candidate_id, path + ("arg",), bound)
    elif op in {"and", "or"}:
        for pos, child in enumerate(formula["args"]):
            yield from _symbol_occurrences(child, candidate_id, path + ("args", pos), bound)
    else:
        for field in ("left", "right"):
            yield from _symbol_occurrences(formula[field], candidate_id, path + (field,), bound)


def project_scope(extractions, binding="separate", *, max_formulas=64, max_graph_nodes=256):
    """Project one explicitly declared conversation segment, preserving evidence.

    Every extraction must retain input.conversation_id and input.segment_id.
    Different turns are permitted within that segment. Segment identity can be
    tentative: its uncertainty is retained, never silently promoted to verified.
    Identical duplicate inputs are rejected rather than silently counted twice.

    ``separate`` retains identity only inside each individual formula. In
    ``literal_within_scope``, exactly equal parser symbols share graph vertices
    across formulas, explicitly conditional on lexical coreference assumptions.
    Quantifier variables remain local binders in both modes.
    """
    if binding not in BINDINGS:
        raise ValueError("binding must be separate or literal_within_scope")
    if type(max_formulas) is not int or not 1 <= max_formulas <= 4096:
        raise ValueError("max_formulas must be an integer in [1,4096]")
    if type(max_graph_nodes) is not int or not 1 <= max_graph_nodes <= 512:
        raise ValueError("max_graph_nodes must be an integer in [1,512]")
    if not isinstance(extractions, list) or not extractions:
        raise ValueError("at least one extraction with explicit scope is required")
    scope = None
    extraction_ids, record_ids, candidate_ids = set(), set(), set()
    entries, provenance, unknown, logical_omissions, uncertainties = [], [], [], [], []
    symbols = defaultdict(list)
    eligible_characters = recognized_characters = physical_units = 0
    atomic_graphs = []
    for extraction in extractions:
        if not isinstance(extraction, dict):
            raise ValueError("each extraction must be an object")
        local_scope = _scope(extraction)
        if scope is not None and local_scope != scope:
            raise ValueError("cannot merge different conversation or segment scopes")
        scope = local_scope
        extraction_id = _required_string(extraction.get("id"), "extraction.id")
        record_id = _required_string(extraction.get("record_id"), "extraction.record_id")
        if extraction_id in extraction_ids or record_id in record_ids:
            raise ValueError("duplicate extraction/observation IDs would inflate source evidence")
        extraction_ids.add(extraction_id)
        record_ids.add(record_id)
        supplied = extraction["input"]
        locator = supplied.get("locator") if isinstance(supplied.get("locator"), dict) else {}
        expected_source = supplied.get("source_id") or supplied.get("source") or locator.get("source")
        expected_record = supplied.get("record_id") or supplied.get("id") or _identifier("text_", supplied)
        if extraction.get("source_id") != expected_source or record_id != expected_record:
            raise ValueError("extraction attribution conflicts with its preserved input record")
        for fields in (extraction.get("scope", {}),):
            for key in ("turn_id", "topic_id", "unit"):
                if key in fields and fields[key] != supplied.get(key):
                    raise ValueError("extraction scope conflicts with its preserved input record")
        outer_span = supplied.get("source_span")
        if isinstance(outer_span, dict) and "turn_id" in outer_span and outer_span["turn_id"] != supplied.get("turn_id"):
            raise ValueError("outer source span conflicts with the declared turn")
        text = supplied.get("text")
        if not isinstance(text, str) or extraction.get("text") != text:
            raise ValueError("extraction text must equal the preserved input text")
        if extraction.get("text_sha256") != hashlib.sha256(text.encode("utf-8")).hexdigest():
            raise ValueError("extraction text hash mismatch")
        candidates, unknowns = extraction.get("candidates"), extraction.get("unknown")
        if not isinstance(candidates, list) or not isinstance(unknowns, list):
            raise ValueError("extraction must retain candidate and unknown arrays")
        uncertainty = {"extraction_id": extraction_id, "record_id": record_id,
                       "scope_status": supplied.get("scope_status", "declared_unverified"),
                       "scope_evidence": deepcopy(supplied.get("scope_evidence")),
                       "focus_basis": supplied.get("focus_basis"),
                       "source_identity_status": extraction.get("source_identity_status", "unknown"),
                       "unknown_statement_count": len(unknowns)}
        uncertainties.append(uncertainty)
        local_spans = []
        for item in unknowns:
            _verify_span(text, item.get("span"))
            local_spans.append(item["span"])
            unknown.append({**deepcopy(item), "extraction_id": extraction_id, "record_id": record_id,
                            "source_id": extraction.get("source_id"), "turn_id": supplied.get("turn_id"),
                            "source_span": deepcopy(supplied.get("source_span"))})
        for candidate in candidates:
            ident = _required_string(candidate.get("id"), "candidate.id")
            if ident in candidate_ids:
                raise ValueError("duplicate candidate IDs cannot be merged")
            candidate_ids.add(ident)
            _verify_span(text, candidate.get("span"))
            local_spans.append(candidate["span"])
            for slot in candidate.get("slots", {}).values():
                _verify_span(text, slot.get("span"))
                if slot.get("text") != slot["span"]["quote"]:
                    raise ValueError("slot text conflicts with its exact source quote")
                if not candidate["span"]["char_start"] <= slot["span"]["char_start"] < slot["span"]["char_end"] <= candidate["span"]["char_end"]:
                    raise ValueError("slot source span is outside its candidate envelope")
            candidate_scope = candidate.get("scope", {})
            if not isinstance(candidate_scope, dict):
                raise ValueError("candidate.scope must be an object")
            for key, expected in (("conversation_id", scope[0]), ("segment_id", scope[1])):
                if key in candidate_scope and candidate_scope[key] != expected:
                    raise ValueError("candidate scope conflicts with its source observation")
            for key in ("turn_id", "topic_id", "unit"):
                if key in candidate_scope and candidate_scope[key] != supplied.get(key):
                    raise ValueError("candidate scope conflicts with its preserved input record")
            candidate_source_span = candidate.get("source_span")
            if isinstance(candidate_source_span, dict) and "turn_id" in candidate_source_span and candidate_source_span["turn_id"] != supplied.get("turn_id"):
                raise ValueError("candidate outer span conflicts with its declared turn")
            if candidate.get("record_id") != record_id or candidate.get("source_id") != extraction.get("source_id"):
                raise ValueError("candidate source identity conflicts with its source observation")
            provenance.append({"candidate_id": ident, "extraction_id": extraction_id,
                               "record_id": record_id, "source_id": extraction.get("source_id"),
                               "turn_id": supplied.get("turn_id"), "span": deepcopy(candidate["span"]),
                               "source_span": deepcopy(candidate.get("source_span", supplied.get("source_span"))),
                               "method": candidate.get("method"), "formula_status": candidate.get("formula_status")})
            recognized_characters += candidate["span"]["char_end"] - candidate["span"]["char_start"]
            formula = candidate.get("formula_candidate")
            if formula is None:
                logical_omissions.append({"candidate_id": ident, "reason": candidate.get("formula_reason", "no_logical_candidate")})
                continue
            validate_formula(formula)
            # No assessment is propagated from source, candidate or caller.
            entry = {"claim_id": ident, "formula": deepcopy(formula), "qualifiers": {},
                     "extraction_status": "unchecked", "conditional_on_extraction": True,
                     "confidence": None, "persistable_claim": False}
            entries.append(entry)
            for occurrence in _symbol_occurrences(formula, ident):
                symbols[(occurrence["kind"], occurrence["symbol"])].append(occurrence)
        local_spans.sort(key=lambda span: (span["char_start"], span["char_end"]))
        if any(left["char_end"] > right["char_start"] for left, right in zip(local_spans, local_spans[1:])):
            raise ValueError("overlapping candidate/unknown spans would inflate coverage")
        cursor = 0
        for span in local_spans:
            if text[cursor:span["char_start"]].strip():
                raise ValueError("source content missing from both candidates and unknown spans")
            cursor = span["char_end"]
        if text[cursor:].strip():
            raise ValueError("source content missing from both candidates and unknown spans")
        eligible_characters += sum(span["char_end"] - span["char_start"] for span in local_spans)
        physical_units += len(local_spans)
        # Rebuild from the preserved candidates, never trust a stale independently
        # supplied graph to add or silently remove recognized source envelopes.
        atomic_graphs.append(_candidate_graph(candidates))
    if len(entries) > max_formulas:
        raise ValueError("scope exceeds max_formulas; partition explicitly rather than silently dropping sources")
    shared = []
    for (kind, symbol), occurrences in sorted(symbols.items()):
        referring = sorted({o["candidate_id"] for o in occurrences})
        if len(referring) > 1:
            shared.append({"kind": kind, "symbol": symbol, "candidate_ids": referring,
                           "occurrences": occurrences,
                           "status": "assumed_same_referent" if binding == "literal_within_scope" else "not_bound_across_statements"})
    structures = {}
    for abstraction in ("structural", "semantic"):
        structures[abstraction] = (formula_graph(entries, abstraction) if binding == "literal_within_scope"
                                   else _union_graphs([formula_graph([entry], abstraction) for entry in entries]))
    atomic_structure = _union_graphs(atomic_graphs)
    if any(len(graph["nodes"]) > max_graph_nodes for graph in [*structures.values(), atomic_structure]):
        raise ValueError("scope exceeds max_graph_nodes; bounded experiment refuses rather than truncates evidence")
    output = {"version": VERSION, "scope": {"conversation_id": scope[0], "segment_id": scope[1],
                                            "status": "declared_not_verified", "uncertainties": uncertainties},
              "binding": {"mode": binding, "identity_verified": False,
                          "cross_statement_symbols": shared,
                          "assumptions": (["Equal parser predicate/constant symbols refer to the same property/entity within this declared segment.",
                                           "Declared segment membership is not independently verified."] if binding == "literal_within_scope"
                                          else ["Identity inside each formula follows its unchecked parser candidate; no cross-statement identity is assumed."])},
              "structure": structures["structural"], "semantic_structure": structures["semantic"],
              "atomic_structure": atomic_structure,
              "text": "\n".join(item["text"] for item in extractions),
              "extractions": deepcopy(extractions), "candidate_provenance": provenance,
              "unknown_statements": unknown, "logical_omissions": logical_omissions,
              "projection_inputs": entries,
              "coverage": {"source_records": len(extractions), "physical_units": physical_units,
                           "recognized_envelopes": len(candidate_ids), "logical_formulas": len(entries),
                           "unknown_physical_units": len(unknown), "eligible_characters": eligible_characters,
                           "recognized_envelope_characters": recognized_characters,
                           "envelope_character_coverage": recognized_characters / eligible_characters if eligible_characters else None,
                           "semantic_accuracy": None, "interpretation": "source_coverage_not_structure_accuracy"},
              "status": "projection_candidate" if entries else "logically_unrepresented",
              "confidence": None, "calibration_status": "unavailable", "persistable_claim": False,
              "inference_eligible": False, "automatic_mutation": False,
              "limits": {"max_formulas": max_formulas, "max_graph_nodes": max_graph_nodes,
                         "input_truncated": False, "statement_order_represented": False}}
    output["id"] = _identifier("scope_projection_", output)
    return output


def compare_scopes(left, right, *, rounds=2, budget=10000):
    """Compare separate source scopes under the same declared binding policy.

    A structural match is conditional on extraction and binding assumptions.
    Semantic projection retains literal symbols, not verified semantic identity.
    Each alignment has its own visible search budget; exhaustion means unknown.
    """
    for record in (left, right):
        if record.get("version") != VERSION or record.get("binding", {}).get("mode") not in BINDINGS:
            raise ValueError("comparison requires scoped_projection results")
    if left["binding"]["mode"] != right["binding"]["mode"]:
        raise ValueError("binding policies must match for an interpretable scope comparison")
    comparisons = {}
    for label, graph_key in (("logical_structural", "structure"),
                             ("logical_literal_semantic", "semantic_structure"),
                             ("atomic_operations", "atomic_structure")):
        result = compare({"text": left["text"], "structure": left[graph_key]},
                         {"text": right["text"], "structure": right[graph_key]}, rounds=rounds, budget=budget)
        result["validity"] = "not_assessed; conditional_on_extraction_scope_and_binding_assumptions"
        result["coverage"] = {"left": deepcopy(left["coverage"]), "right": deepcopy(right["coverage"]),
                              "grounding": "verified_local_quotes; raw_source_identity_and_interpretation_unverified"}
        comparisons[label] = result
    return {"version": VERSION, "left_scope": deepcopy(left["scope"]), "right_scope": deepcopy(right["scope"]),
            "binding": left["binding"]["mode"], "comparisons": comparisons,
            "budget_per_projection": budget, "projection_count": 3,
            "confidence": None, "identity_verified": False, "validity": "not_assessed",
            "persistable_claim": False, "inference_eligible": False, "automatic_mutation": False}
