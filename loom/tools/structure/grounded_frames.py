"""Transient exact-span anchors -> native-shaped candidate graph drafts.

No text parser, provider calls, authoritative AST, inference or store writes.
Both extraction strategies ultimately use candidate_graph.compile_bundle.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json

VERSION = "grounded-frames/1"
ANCHOR_SCHEMA = "loom.grounded_anchors/1"
FRAME_SCHEMA = "loom.grounded_frames/1"
OPERATIONS = {"predicate_application", "conditional", "negation", "conjunction", "quantifier"}
LINKS = {"in_scope", "scope_parent", "bound_to", "denotes"}
KINDS = {"expression_occurrence", "term_occurrence", "binder", "scope"}
CONTEXTS = {"asserted", "hypothetical", "quoted", "unknown"}
POLARITIES = {"positive", "negative", "unknown"}
STATUSES = {"represented", "partial", "unsupported", "ambiguous", "omitted"}
DEFAULT_LIMITS = {"max_anchors": 256, "max_frames": 256, "max_links": 2048,
                  "max_alternatives": 8, "max_bytes": 4194304}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _hash(value):
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


class _Invalid(ValueError):
    def __init__(self, code, path, message):
        self.error = {"code": code, "path": path, "message": message}


def _need(test, code, path, message):
    if not test:
        raise _Invalid(code, path, message)


def _shape(value, fields, path):
    _need(isinstance(value, dict) and set(value) == set(fields), "shape", path,
          "required fields only: " + ", ".join(sorted(fields)))


def _text(value):
    return isinstance(value, str) and bool(value.strip())


def _handle(value):
    return _text(value) and value.startswith("@") and len(value) <= 200 and not any(c.isspace() for c in value)


def _array(value, path, maximum=4096):
    _need(isinstance(value, list) and len(value) <= maximum, "array_limit", path, "expected a bounded array")
    return value


def _limits(supplied):
    limits = dict(DEFAULT_LIMITS)
    if supplied is not None:
        _need(isinstance(supplied, dict) and set(supplied) <= set(limits), "limits", "limits", "unknown limit")
        for key, value in supplied.items():
            _need(type(value) is int and 1 <= value <= DEFAULT_LIMITS[key], "limits", key,
                  "limits may only reduce the positive default bound")
            limits[key] = value
    return limits


def _bounded(value, limit):
    _need(len(canonical(value).encode("utf-8")) <= limit, "byte_limit", "$", "input exceeds byte budget")


def _packet(packet):
    _need(isinstance(packet, dict) and packet.get("schema") == "loom.source_packet/1" and
          _text(packet.get("snapshot_id")), "packet", "source_packet", "explicit source packet required")
    observations = {}
    for index, item in enumerate(_array(packet.get("observations"), "observations")):
        path = f"observations[{index}]"
        _need(isinstance(item, dict) and _text(item.get("id")) and isinstance(item.get("text"), str),
              "observation", path, "observation requires identity and exact text")
        _need(item["id"] not in observations, "duplicate_observation", path, "observation ID must be unique")
        _need(isinstance(item.get("locator"), dict) and _text(item["locator"].get("source")),
              "locator", path, "observation must retain its source locator")
        observations[item["id"]] = item
    for collection in ("entities", "claims"):
        _array(packet.get(collection), collection)
    return observations


def _support(spans, observations, path):
    _array(spans, path, 64)
    _need(bool(spans), "support", path, "nonempty exact source support required")
    for index, span in enumerate(spans):
        here = f"{path}[{index}]"
        _shape(span, {"observation", "byte_start", "byte_len", "quote"}, here)
        _need(span["observation"] in observations, "observation_reference", here, "unknown observation")
        a, n = span["byte_start"], span["byte_len"]
        data = observations[span["observation"]]["text"].encode("utf-8")
        _need(type(a) is int and type(n) is int and 0 <= a < a + n <= len(data),
              "span_range", here, "invalid observation-local byte range")
        _need(isinstance(span["quote"], str) and data[a:a+n] == span["quote"].encode("utf-8"),
              "quote_mismatch", here, "quote must equal the exact UTF-8 source bytes")


def _coverage(records, observations, anchors, path, expand=None):
    out = []
    for index, record in enumerate(_array(records, path)):
        here = f"{path}[{index}]"
        _shape(record, {"support", "status", "reason", "drafts"}, here)
        _need(record["status"] in STATUSES and _text(record["reason"]), "coverage", here, "explicit status and reason required")
        drafts = _array(record["drafts"], here + ".drafts")
        _need(all(isinstance(h, str) and h in anchors for h in drafts), "anchor_reference", here, "coverage refers only to earlier anchors")
        _need(record["status"] != "represented" or bool(drafts), "coverage", here, "represented coverage needs a draft")
        support = expand(record["support"], here) if expand else record["support"]
        _support(support, observations, here + ".support")
        out.append({**deepcopy(record), "support": deepcopy(support)})
    return out


def _unknowns(records, observations, path, expand=None):
    out = []
    for index, record in enumerate(_array(records, path)):
        here = f"{path}[{index}]"
        _shape(record, {"support", "reason"}, here)
        _need(_text(record["reason"]), "unknown_reason", here, "located unknown needs a reason")
        support = expand(record["support"], here) if expand else record["support"]
        _support(support, observations, here + ".support")
        out.append({"support": deepcopy(support), "reason": record["reason"]})
    return out


def _anchor_data(packet, response, limits):
    _bounded({"source_packet": packet, "anchoring": response}, limits["max_bytes"])
    observations = _packet(packet)
    _shape(response, {"schema", "packet_id", "anchors", "coverage", "unknowns"}, "anchoring")
    _need(response["schema"] == ANCHOR_SCHEMA and response["packet_id"] == packet["snapshot_id"],
          "packet_identity", "anchoring", "anchoring must refer to this exact packet")
    anchors = {}
    for index, anchor in enumerate(_array(response["anchors"], "anchors", limits["max_anchors"])):
        path = f"anchors[{index}]"
        _shape(anchor, {"handle", "kind", "label", "attrs", "support"}, path)
        _need(_handle(anchor["handle"]) and anchor["handle"] not in anchors, "anchor_handle", path, "unique local handle required")
        _need(anchor["kind"] in KINDS and isinstance(anchor["label"], str) and isinstance(anchor["attrs"], dict),
              "anchor_shape", path, "anchor kind, label and attrs required")
        _support(anchor["support"], observations, path + ".support")
        anchors[anchor["handle"]] = anchor
    _coverage(response["coverage"], observations, anchors, "anchoring.coverage")
    _unknowns(response["unknowns"], observations, "anchoring.unknowns")
    packet_hash = _hash(packet)
    prepared = {"schema": "loom.prepared_anchors/1", "packet_hash": packet_hash,
                "anchors_hash": _hash({"packet_hash": packet_hash, "anchoring": response}),
                "anchoring": deepcopy(response)}
    return prepared, observations, anchors


def _report(stage, valid, **extra):
    return {"version": VERSION, "stage": stage, "valid": valid, "status": "valid" if valid else "rejected",
            "errors": [], "no_inference": True, "no_persistence": True, **extra}


def _rejected(stage, exc, retained):
    error = exc.error if isinstance(exc, _Invalid) else {"code": "invalid_json_or_type", "path": "$", "message": str(exc)}
    return _report(stage, False, errors=[error], retained_input=deepcopy(retained))


def prepare_anchors(source_packet, anchor_response, *, limits=None):
    """Validate exact source grounding, returning a hash-bound frozen input copy.

    This does not validate the meaning of kinds/labels or select any referent.
    Attribute, graph-context and composition contracts are checked by the common
    compiler later. Hash binding detects changed inputs; it is not authentication.
    """
    try:
        prepared, _, _ = _anchor_data(source_packet, anchor_response, _limits(limits))
        return _report("anchors", True, prepared=prepared)
    except (ValueError, TypeError, KeyError, OverflowError, RecursionError) as exc:
        return _rejected("anchors", exc, {"source_packet": source_packet, "anchoring": anchor_response})


def _compose(packet, prepared, response, limits):
    _shape(prepared, {"schema", "packet_hash", "anchors_hash", "anchoring"}, "prepared")
    checked, observations, anchors = _anchor_data(packet, prepared["anchoring"], limits)
    _need(prepared == checked, "changed_anchors", "prepared", "prepared packet/anchor bytes changed")
    _bounded(response, limits["max_bytes"])
    _shape(response, {"schema", "packet_id", "anchors_hash", "alternatives"}, "composition")
    _need(response["schema"] == FRAME_SCHEMA and response["packet_id"] == packet["snapshot_id"] and
          response["anchors_hash"] == prepared["anchors_hash"], "changed_anchors", "composition", "composition must bind the frozen anchors")
    existing_entities = {e["id"] for e in packet["entities"] if isinstance(e, dict) and _text(e.get("id"))}

    def local(value, path):
        _need(isinstance(value, str) and value in anchors, "anchor_reference", path, "new or unknown anchor is forbidden")

    def support(handles, path):
        _array(handles, path, 64)
        _need(bool(handles), "support", path, "support must reference earlier anchors")
        spans = {}
        for handle in handles:
            local(handle, path)
            for span in anchors[handle]["support"]:
                spans[canonical(span)] = deepcopy(span)
        return [spans[key] for key in sorted(spans)]

    alternatives, ids = [], set()
    for index, reading in enumerate(_array(response["alternatives"], "alternatives", limits["max_alternatives"])):
        path = f"alternatives[{index}]"
        _shape(reading, {"id", "frames", "links", "roots", "coverage", "unknowns"}, path)
        _need(_text(reading["id"]) and reading["id"] not in ids, "alternative_id", path, "unique alternative identity required")
        ids.add(reading["id"])
        claims = {}

        def claim(subject, predicate, object_, value, data, here, port=None, ordinal=None):
            local(subject, here + ".subject")
            local(data["scope"], here + ".scope")
            _need(data["polarity"] in POLARITIES and data["assertion_context"] in CONTEXTS,
                  "qualifiers", here, "explicit polarity and assertion context required")
            premises = _array(data["premises"], here + ".premises", 64)
            _need(all(_text(p) and not p.startswith("@") for p in premises), "premise_reference", here,
                  "Assessment premises must be existing Claim IDs, never syntax handles")
            extra = {"polarity": data["polarity"], "assertion_context": data["assertion_context"]}
            if port is not None:
                extra.update(port=port, ordinal=ordinal)
            draft = {"subject": subject, "predicate": predicate, "object": object_, "value": value,
                     "qualifiers": {"scope": data["scope"], "extra": extra},
                     "assessment": {"basis": {"support": support(data["support"], here + ".support")},
                                    "premises": {"claims": deepcopy(premises)}}}
            handle = "@gf_" + _hash(draft)
            claims[handle] = {"handle": handle, **draft}

        for number, frame in enumerate(_array(reading["frames"], path + ".frames", limits["max_frames"])):
            here = f"{path}.frames[{number}]"
            fields = {"anchor", "operation", "scope", "operands", "polarity", "assertion_context", "support", "premises"}
            if isinstance(frame, dict) and frame.get("operation") == "quantifier":
                fields |= {"quantifier_kind", "introduced_scope"}
            _shape(frame, fields, here)
            _need(frame["operation"] in OPERATIONS, "unsupported_operation", here,
                  "unsupported operations must remain located unknowns, not wildcard frames")
            local(frame["anchor"], here + ".anchor")
            claim(frame["anchor"], "operation_type", "", frame["operation"], frame, here)
            for operand in _array(frame["operands"], here + ".operands"):
                _shape(operand, {"port", "ordinal", "target"}, here + ".operands")
                _need(isinstance(operand["target"], str) and
                      (operand["target"] in anchors or operand["target"] in existing_entities),
                      "anchor_reference", here + ".target", "operand must reference an earlier anchor or allowlisted entity")
                _need(_text(operand["port"]) and type(operand["ordinal"]) is int and operand["ordinal"] >= 0,
                      "operand", here, "explicit port and nonnegative ordinal required")
                claim(frame["anchor"], "operand", operand["target"], None, frame, here,
                      operand["port"], operand["ordinal"])
            if frame["operation"] == "quantifier":
                local(frame["introduced_scope"], here + ".introduced_scope")
                claim(frame["anchor"], "quantifier_kind", "", frame["quantifier_kind"], frame, here)
                claim(frame["anchor"], "introduces_scope", frame["introduced_scope"], None, frame, here)
        for number, link in enumerate(_array(reading["links"], path + ".links", limits["max_links"])):
            here = f"{path}.links[{number}]"
            _shape(link, {"subject", "predicate", "object", "scope", "polarity", "assertion_context", "support", "premises"}, here)
            _need(link["predicate"] in LINKS, "unsupported_link", here, "only scope, binding and reference links are supported")
            if link["predicate"] != "denotes":
                local(link["object"], here + ".object")
            claim(link["subject"], link["predicate"], link["object"], None, link, here)
        roots = _array(reading["roots"], path + ".roots")
        for root in roots:
            local(root, path + ".roots")
        coverage = deepcopy(prepared["anchoring"]["coverage"])
        coverage.extend(_coverage(reading["coverage"], observations, anchors, path + ".coverage", support))
        unknowns = deepcopy(prepared["anchoring"]["unknowns"])
        unknowns.extend(_unknowns(reading["unknowns"], observations, path + ".unknowns", support))
        bundle = {"schema": "loom.candidate_graph/1", "packet_id": packet["snapshot_id"],
                  "entity_drafts": [deepcopy(anchors[h]) for h in sorted(anchors)],
                  "claim_drafts": [claims[h] for h in sorted(claims)], "roots": deepcopy(roots),
                  "coverage": coverage, "unknowns": unknowns}
        alternatives.append({"id": reading["id"], "bundle": bundle})
    _need(bool(alternatives), "alternatives", "alternatives", "one or more explicit interpretations required")
    return alternatives


def compose_frames(source_packet, prepared, composition, *, limits=None):
    """Expand separate readings to common candidate bundles; no graph semantics are guessed."""
    try:
        alternatives = _compose(source_packet, prepared, composition, _limits(limits))
        return _report("composition", True, alternatives=alternatives,
                       packet_hash=prepared["packet_hash"], anchors_hash=prepared["anchors_hash"])
    except (ValueError, TypeError, KeyError, OverflowError, RecursionError) as exc:
        return _rejected("composition", exc, {"source_packet": source_packet, "prepared": prepared, "composition": composition})


def compile_frames(source_packet, anchor_response, composition, *, limits=None, compiler_limits=None):
    """Ground, compose, then delegate each reading to the same compiler as direct graphs.

    Normal invalid input is a report, not an exception. Alternative reports are
    never joined into one graph. No API in this module contacts a model or store.
    """
    anchored = prepare_anchors(source_packet, anchor_response, limits=limits)
    if not anchored["valid"]:
        return {**anchored, "stages": {"anchors": anchored["status"]}, "alternatives": []}
    composed = compose_frames(source_packet, anchored["prepared"], composition, limits=limits)
    if not composed["valid"]:
        return {**composed, "stages": {"anchors": "valid", "composition": composed["status"]}, "alternatives": []}
    try:
        from .candidate_graph import compile_bundle
    except ImportError:
        from candidate_graph import compile_bundle
    alternatives = []
    for alternative in composed["alternatives"]:
        options = {} if compiler_limits is None else {"limits": compiler_limits}
        compiled = compile_bundle(alternative["bundle"], source_packet, **options)
        alternatives.append({**alternative, "compiled": compiled})
    valid = all(item["compiled"]["valid"] for item in alternatives)
    return _report("compiler", valid, alternatives=alternatives, packet_hash=composed["packet_hash"],
                   anchors_hash=composed["anchors_hash"],
                   stages={"anchors": "valid", "composition": "valid", "compiler": "valid" if valid else "rejected"},
                   errors=[{"alternative": item["id"], **error} for item in alternatives for error in item["compiled"].get("errors", [])])
