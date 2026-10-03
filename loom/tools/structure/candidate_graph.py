#!/usr/bin/env python3
"""Validate grounded Entity/Claim drafts; derive a graph without asserting truth.

No model calls, native writes, AST authority, inference or implicit evidence
classification. See CANDIDATE_GRAPH.md for the frozen research contract.
"""
from __future__ import annotations

from collections import defaultdict
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re

VERSION = "candidate-graph/1"
VOCABULARY = json.loads(Path(__file__).with_name("candidate_graph_vocabulary.json").read_text())
DEFAULT_LIMITS = VOCABULARY["limits"]
HANDLE = re.compile(r"^@[A-Za-z0-9_.:-]{1,127}$")


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _hash(value):
    return hashlib.sha256(_canonical(value).encode("utf-8")).hexdigest()


class _Rejected(Exception):
    def __init__(self, code, path, message):
        self.error = {"code": code, "path": path, "message": message}


def _need(condition, code, path, message):
    if not condition:
        raise _Rejected(code, path, message)


def _exact(value, fields, path):
    _need(isinstance(value, dict) and set(value) == set(fields), "shape", path,
          "required fields: " + ", ".join(sorted(fields)))


def _text(value, path, *, empty=False):
    _need(isinstance(value, str) and (empty or bool(value.strip())) and len(value) <= 4096,
          "text", path, "expected bounded string" + ("" if empty else " (nonempty)"))


def _union_size(intervals):
    end, total = 0, 0
    for start, stop in sorted(intervals):
        if stop > end:
            total += stop - max(start, end)
            end = stop
    return total


class _Validator:
    def __init__(self, bundle, packet, limits):
        self.bundle, self.packet = bundle, packet
        self.limits = dict(DEFAULT_LIMITS)
        _need(limits is None or isinstance(limits, dict), "limits", "limits", "limits must be an object")
        for key, value in (limits or {}).items():
            _need(key in self.limits and type(value) is int and 1 <= value <= self.limits[key],
                  "limits", "limits." + key, "limit must lower a named positive maximum")
            self.limits[key] = value
        self.observations, self.existing_entities, self.existing_claims = {}, {}, {}
        self.entities, self.claims = {}, {}
        self.membership, self.parents, self.operations = {}, {}, {}
        self.edges = defaultdict(list)
        self.scope_owner, self.binder_owner = {}, {}
        self.coverage = {"representation_status": "unknown", "semantic_accuracy": None}

    def span(self, span, path):
        _exact(span, {"observation", "byte_start", "byte_len", "quote"}, path)
        o = self.observations.get(span["observation"] if isinstance(span["observation"], str) else None)
        _need(o is not None, "unknown_observation", path, "support is outside the source packet")
        start, length, quote = span["byte_start"], span["byte_len"], span["quote"]
        _need(type(start) is int and type(length) is int and start >= 0 and length > 0 and isinstance(quote, str),
              "span_bounds", path, "support needs nonempty exact UTF-8 byte span")
        source = o["text"].encode("utf-8")
        _need(start + length <= len(source), "span_bounds", path, "span exceeds observation")
        try:
            exact = source[start:start + length].decode("utf-8")
        except UnicodeDecodeError:
            raise _Rejected("utf8_boundary", path, "span splits a UTF-8 code point")
        _need(exact == quote and len(quote.encode("utf-8")) == length,
              "quote_mismatch", path, "quote differs from exact observation bytes")
        return span

    def support(self, value, path):
        _need(isinstance(value, list) and 1 <= len(value) <= self.limits["max_support"],
              "support", path, "support must be a nonempty bounded list")
        for index, span in enumerate(value):
            self.span(span, f"{path}[{index}]")

    def packet_check(self):
        p = self.packet
        _need(isinstance(p, dict) and p.get("schema") == "loom.source_packet/1", "packet_schema", "source_packet", "wrong packet schema")
        _text(p.get("snapshot_id"), "source_packet.snapshot_id")
        for key in ("observations", "entities", "claims"):
            _need(isinstance(p.get(key), list), "packet_shape", "source_packet." + key, "expected list")
        _need(len(p["observations"]) <= self.limits["max_observations"], "budget", "source_packet.observations", "too many observations")
        for item in p["observations"]:
            _need(isinstance(item, dict), "packet_shape", "observation", "expected native observation object")
            for key in ("id", "unit", "text"):
                _need(isinstance(item.get(key), str) and (key == "text" or bool(item[key])), "packet_shape", "observation." + key, "missing observation field")
            _need(isinstance(item.get("locator"), dict) and isinstance(item["locator"].get("source"), str) and item["locator"]["source"],
                  "packet_locator", "observation.locator", "located source required")
            _need(item["id"] not in self.observations, "duplicate_id", "observation.id", "observation IDs must be unique")
            self.observations[item["id"]] = item
        _need(sum(len(o["text"].encode("utf-8")) for o in self.observations.values()) <= self.limits["max_source_bytes"],
              "budget", "source_packet.observations", "source byte budget exceeded")
        for collection, target in (("entities", self.existing_entities), ("claims", self.existing_claims)):
            for item in p[collection]:
                _need(isinstance(item, dict) and isinstance(item.get("id"), str) and item["id"] and not item["id"].startswith("@"),
                      "packet_id", collection, "existing record needs non-local ID")
                _need(item["id"] not in target, "duplicate_id", collection, "duplicate existing ID")
                target[item["id"]] = item
        _need(not (set(self.existing_entities) & set(self.existing_claims)), "typed_namespace", "source_packet", "Entity and Claim IDs must not collide")
        for ident, entity in self.existing_entities.items():
            _text(entity.get("kind"), "entities." + ident + ".kind")
        for ident, claim in self.existing_claims.items():
            a = claim.get("assessment")
            _need(isinstance(a, dict) and {"basis", "premises", "evidence_class", "origin", "confidence", "status"} <= set(a),
                  "missing_assessment", "claims." + ident, "existing Claim Assessment must be retained, not invented")
            _need(isinstance(a["basis"], dict) and isinstance(a["premises"], dict), "assessment_shape", "claims." + ident, "basis/premises must be objects")
            _need(a["evidence_class"] in {"observed", "derived", "inferred", "extrapolated", "absent", "user"} and
                  a["status"] in {"active", "contested", "superseded", "rejected"}, "assessment_shape", "claims." + ident, "invalid native evidence/status")
            _need(type(a["confidence"]) in (int, float) and 0 <= a["confidence"] <= 1,
                  "assessment_shape", "claims." + ident, "native confidence required")

    def draft_check(self):
        b = self.bundle
        _exact(b, {"schema", "packet_id", "entity_drafts", "claim_drafts", "roots", "coverage", "unknowns"}, "bundle")
        _need(b["schema"] == "loom.candidate_graph/1" and b["packet_id"] == self.packet["snapshot_id"],
              "bundle_schema", "bundle", "schema or packet identity mismatch")
        for key in ("entity_drafts", "claim_drafts", "roots", "coverage", "unknowns"):
            _need(isinstance(b[key], list), "shape", "bundle." + key, "expected list")
        _need(len(b["entity_drafts"]) <= self.limits["max_entities"] and len(b["claim_drafts"]) <= self.limits["max_claims"],
              "budget", "bundle", "draft count limit exceeded")
        handles = set()
        for collection, target in (("entity_drafts", self.entities), ("claim_drafts", self.claims)):
            for index, item in enumerate(b[collection]):
                path = f"bundle.{collection}[{index}]"
                _need(isinstance(item, dict) and isinstance(item.get("handle"), str) and HANDLE.fullmatch(item["handle"]), "handle", path, "invalid local handle")
                _need(item["handle"] not in handles, "duplicate_handle", path, "handles are unique across namespaces")
                handles.add(item["handle"])
                target[item["handle"]] = item
        for h, e in self.entities.items():
            _exact(e, {"handle", "kind", "label", "attrs", "support"}, h)
            _need(e["kind"] in VOCABULARY["entity_kinds"], "entity_kind", h, "unsupported occurrence kind")
            _text(e["label"], h + ".label", empty=True)
            self.support(e["support"], h + ".support")
            a = e["attrs"]
            expected = {"scope": {"scope_type", "assertion_context"}, "term_occurrence": {"term_type", "symbol"},
                        "binder": {"symbol"}, "expression_occurrence": set()}[e["kind"]]
            _exact(a, expected, h + ".attrs")
            if e["kind"] == "scope":
                _need(a["scope_type"] in VOCABULARY["scope_types"] and a["assertion_context"] in VOCABULARY["assertion_contexts"], "scope_type", h, "unsupported scope/context")
            if "symbol" in a:
                _text(a["symbol"], h + ".attrs.symbol")
            if "term_type" in a:
                _need(a["term_type"] in VOCABULARY["term_types"], "term_type", h, "unsupported term type")
        for h, c in self.claims.items():
            _exact(c, {"handle", "subject", "predicate", "object", "value", "qualifiers", "assessment"}, h)
            for name in ("subject", "object"):
                _need(isinstance(c[name], str), "endpoint", h + "." + name, "endpoint must be an entity reference")
            _need(c["subject"] in self.entities, "subject", h, "structural claim subject must be a local occurrence")
            _need(c["predicate"] in VOCABULARY["predicates"], "predicate", h, "unsupported structural predicate")
            is_literal = c["predicate"] in {"operation_type", "quantifier_kind"}
            if is_literal:
                _need(c["object"] == "" and isinstance(c["value"], str), "literal", h, "type claims require literal strings")
            else:
                _need(c["object"] in self.entities or c["object"] in self.existing_entities, "endpoint", h, "object is not a local or allowlisted Entity")
                _need(c["value"] is None, "object_value_xor", h, "entity edge must not also have a literal")
            _exact(c["qualifiers"], {"scope", "extra"}, h + ".qualifiers")
            scope = c["qualifiers"]["scope"]
            _need(isinstance(scope, str) and scope in self.entities and self.entities[scope]["kind"] == "scope", "scope", h, "qualifier needs local scope entity")
            extra = c["qualifiers"]["extra"]
            fields = {"polarity", "assertion_context"} | ({"port", "ordinal"} if c["predicate"] == "operand" else set())
            _exact(extra, fields, h + ".qualifiers.extra")
            _need(extra["polarity"] in VOCABULARY["polarities"] and extra["assertion_context"] in VOCABULARY["assertion_contexts"], "polarity_context", h, "explicit polarity and assertion context required")
            _need(extra["assertion_context"] == self.entities[scope]["attrs"]["assertion_context"], "context_mismatch", h, "claim context differs from qualifying scope")
            if c["predicate"] == "operand":
                _text(extra["port"], h + ".port")
                _need(type(extra["ordinal"]) is int and extra["ordinal"] >= 0, "ordinal", h, "ordinal must be nonnegative integer")
            _exact(c["assessment"], {"basis", "premises"}, h + ".assessment")
            _exact(c["assessment"]["basis"], {"support"}, h + ".assessment.basis")
            self.support(c["assessment"]["basis"]["support"], h + ".support")
            _exact(c["assessment"]["premises"], {"claims"}, h + ".premises")
            refs = c["assessment"]["premises"]["claims"]
            _need(isinstance(refs, list) and all(isinstance(x, str) for x in refs) and len(set(refs)) == len(refs), "premises", h, "premise list must contain distinct Claim IDs")
            for ref in refs:
                _need(ref in self.existing_claims, "premise_reference", h, "premise Claim is outside allowlist")
                a = self.existing_claims[ref]["assessment"]
                _need(a["evidence_class"] not in {"absent", "extrapolated"} and a["status"] not in {"rejected", "superseded"}, "premise_ineligible", h, "native premise evidence/status is ineligible")
            self.edges[c["predicate"]].append(c)

    def _kind(self, ref):
        return self.entities[ref]["kind"] if ref in self.entities else "existing"

    def _single_map(self, predicate):
        result = {}
        for c in self.edges[predicate]:
            _need(c["subject"] not in result, "cardinality", c["handle"], "duplicate " + predicate)
            result[c["subject"]] = c["object"]
        return result

    def _ancestors(self, scope):
        chain = []
        while scope:
            _need(scope not in chain, "scope_cycle", scope, "scope parent cycle")
            chain.append(scope)
            _need(len(chain) <= self.limits["max_depth"], "depth", scope, "scope depth limit")
            scope = self.parents.get(scope)
        return chain

    def structure_check(self):
        scopes = {h for h, e in self.entities.items() if e["kind"] == "scope"}
        expressions = {h for h, e in self.entities.items() if e["kind"] == "expression_occurrence"}
        self.membership = self._single_map("in_scope")
        self.parents = self._single_map("scope_parent")
        _need(set(self.membership) == set(self.entities) - scopes and all(s in scopes for s in self.membership.values()), "scope_membership", "in_scope", "every non-scope occurrence needs exactly one local scope")
        _need(set(self.parents) <= scopes and set(self.parents.values()) <= scopes, "scope_parent", "scope_parent", "scope parents must be scopes")
        for s in scopes:
            self._ancestors(s)
        for c in self.claims.values():
            s, p, o = c["subject"], c["predicate"], c["object"]
            expected = o if p == "in_scope" else s if p == "scope_parent" else self.membership.get(s)
            _need(c["qualifiers"]["scope"] == expected, "qualifier_scope", c["handle"], "claim is qualified by the wrong occurrence scope")
            if p == "operation_type":
                _need(s in expressions and s not in self.operations and c["value"] in VOCABULARY["operations"], "operation_type", c["handle"], "each expression needs one supported operation type")
                self.operations[s] = c["value"]
        _need(set(self.operations) == expressions, "operation_type", "expressions", "every expression needs operation_type")
        quantifiers = {h for h, op in self.operations.items() if op == "quantifier"}
        qkinds = {}
        for c in self.edges["quantifier_kind"]:
            _need(c["subject"] in quantifiers and c["subject"] not in qkinds and c["value"] in VOCABULARY["quantifier_kinds"], "quantifier_kind", c["handle"], "quantifier_kind must be forall or exists exactly once")
            qkinds[c["subject"]] = c["value"]
        _need(set(qkinds) == quantifiers, "quantifier_kind", "quantifiers", "quantifier kind missing")
        introduced = self._single_map("introduces_scope")
        _need(set(introduced) == quantifiers and len(set(introduced.values())) == len(introduced), "quantifier_scope", "introduces_scope", "each quantifier owns one distinct scope")
        for q, s in introduced.items():
            _need(s in scopes and self.entities[s]["attrs"]["scope_type"] == "quantifier" and self.parents.get(s) == self.membership[q], "quantifier_scope", q, "introduced scope must be quantifier child of occurrence scope")
            self.scope_owner[s] = q
        _need({s for s in scopes if self.entities[s]["attrs"]["scope_type"] == "quantifier"} == set(self.scope_owner), "quantifier_scope", "scopes", "unowned quantifier scope")
        operands = defaultdict(lambda: defaultdict(list))
        syntax = defaultdict(list)
        used = set()
        for c in self.edges["operand"]:
            s, o, x = c["subject"], c["object"], c["qualifiers"]["extra"]
            _need(s in expressions, "operand_subject", c["handle"], "operand subject must be expression")
            ports = VOCABULARY["operations"][self.operations[s]]["ports"]
            _need(x["port"] in ports, "operand_port", c["handle"], "unsupported operand port for operation")
            target = ports[x["port"]]["target"]
            kind = self._kind(o)
            ok = kind == target
            if target == "predicate":
                ok = kind == "existing" or kind == "term_occurrence" and self.entities[o]["attrs"]["term_type"] == "predicate"
            if target == "term":
                ok = kind == "existing" or kind == "term_occurrence" and self.entities[o]["attrs"]["term_type"] in {"constant", "variable"}
            _need(ok, "operand_type", c["handle"], "operand target has wrong kind")
            operands[s][x["port"]].append((x["ordinal"], o))
            used.add(o)
            if o in expressions:
                syntax[s].append(o)
            if o in self.entities:
                source_scope, target_scope = self.membership[s], self.membership[o]
                if self.operations[s] == "quantifier":
                    _need(target_scope == introduced[s], "operand_scope", c["handle"], "quantifier operands must be in its child scope")
                else:
                    allowed_child = self.parents.get(target_scope) == source_scope and self.entities[target_scope]["attrs"]["scope_type"] in {"quotation", "hypothesis"}
                    _need(target_scope == source_scope or allowed_child, "operand_scope", c["handle"], "operand escapes or crosses scope")
        for h, op in self.operations.items():
            for port, rule in VOCABULARY["operations"][op]["ports"].items():
                entries = sorted(operands[h][port])
                _need(rule["min"] <= len(entries) <= rule["max"] and [i for i, _ in entries] == list(range(len(entries))), "operand_cardinality", h + "." + port, "missing/duplicate operand or noncontiguous ordinal")
            if op == "quantifier":
                binder = operands[h]["binder"][0][1]
                _need(binder not in self.binder_owner, "binder_owner", binder, "binder cannot belong to two quantifiers")
                self.binder_owner[binder] = h
        binders = {h for h, e in self.entities.items() if e["kind"] == "binder"}
        _need(binders == set(self.binder_owner), "binder_owner", "binders", "every binder must be owned")
        variables = {h for h, e in self.entities.items() if e["kind"] == "term_occurrence" and e["attrs"]["term_type"] == "variable"}
        bound = self._single_map("bound_to")
        _need(set(bound) == variables and set(bound.values()) <= binders, "binding", "bound_to", "every variable occurrence needs one binder")
        by_scope_symbol = {}
        for binder in binders:
            key = (self.membership[binder], self.entities[binder]["attrs"]["symbol"])
            _need(key not in by_scope_symbol, "binding_ambiguity", binder, "same-name binders in one scope")
            by_scope_symbol[key] = binder
        for variable, binder in bound.items():
            symbol = self.entities[variable]["attrs"]["symbol"]
            visible = [by_scope_symbol[(s, symbol)] for s in self._ancestors(self.membership[variable]) if (s, symbol) in by_scope_symbol]
            _need(visible and visible[0] == binder, "binding_capture", variable, "reference escapes, captures or skips nearest same-name binder")
        denoted = self._single_map("denotes")
        for term, entity in denoted.items():
            _need(self._kind(term) == "term_occurrence" and term not in variables and entity in self.existing_entities,
                  "denotes", term, "denotes requires nonvariable local term and allowlisted Entity")
        roots = self.bundle["roots"]
        _need(all(isinstance(h, str) and h in expressions for h in roots) and len(set(roots)) == len(roots), "roots", "roots", "roots must be distinct expression handles")
        visited, heights = set(), {}

        def walk(node, active):
            _need(node not in active, "syntax_cycle", node, "expression operand cycle")
            _need(len(active) < self.limits["max_depth"], "depth", node, "expression depth limit")
            if node in heights:
                _need(len(active) + heights[node] <= self.limits["max_depth"], "depth", node, "expression depth limit")
                return heights[node]
            visited.add(node)
            height = 1 + max((walk(child, active | {node}) for child in syntax[node]), default=0)
            _need(height <= self.limits["max_depth"], "depth", node, "expression depth limit")
            heights[node] = height
            return height

        for root in roots:
            walk(root, set())
        _need(visited == expressions, "unreachable_expression", "roots", "every expression must be reachable from a root")
        _need((set(self.entities) - expressions - scopes) <= used, "unused_occurrence", "entities", "orphan term/binder occurrence")
        used_scopes = {s for h in self.membership for s in self._ancestors(self.membership[h])}
        _need(used_scopes == scopes, "unused_scope", "scopes", "orphan scope")

    def coverage_check(self):
        handles = set(self.entities) | set(self.claims)
        by_status = {s: defaultdict(list) for s in VOCABULARY["coverage_statuses"]}
        covered, unknown = defaultdict(list), defaultdict(list)
        for index, row in enumerate(self.bundle["coverage"]):
            path = f"coverage[{index}]"
            _exact(row, {"support", "status", "reason", "drafts"}, path)
            self.support(row["support"], path + ".support")
            _text(row["reason"], path + ".reason")
            _need(row["status"] in by_status, "coverage_status", path, "unsupported coverage status")
            _need(isinstance(row["drafts"], list) and all(isinstance(h, str) and h in handles for h in row["drafts"]), "coverage_reference", path, "coverage points outside local drafts")
            _need(row["status"] != "represented" or row["drafts"], "coverage_reference", path, "represented coverage needs draft references")
            for span in row["support"]:
                interval = (span["byte_start"], span["byte_start"] + span["byte_len"])
                by_status[row["status"]][span["observation"]].append(interval)
                covered[span["observation"]].append(interval)
        for index, row in enumerate(self.bundle["unknowns"]):
            path = f"unknowns[{index}]"
            _exact(row, {"support", "reason"}, path)
            self.support(row["support"], path + ".support")
            _text(row["reason"], path + ".reason")
            for span in row["support"]:
                unknown[span["observation"]].append((span["byte_start"], span["byte_start"] + span["byte_len"]))
        total = sum(len(o["text"].encode("utf-8")) for o in self.observations.values())
        counts = {status: sum(_union_size(spans) for spans in refs.values()) for status, refs in by_status.items()}
        uncovered = total - sum(_union_size(spans) for spans in covered.values())
        uncovered_spans = []
        for ident, observation in sorted(self.observations.items()):
            raw, end = observation["text"].encode("utf-8"), 0
            for start, stop in sorted(covered[ident]):
                if start > end:
                    uncovered_spans.append({"observation": ident, "byte_start": end, "byte_len": start - end,
                                            "quote": raw[end:start].decode("utf-8")})
                end = max(end, stop)
            if end < len(raw):
                uncovered_spans.append({"observation": ident, "byte_start": end, "byte_len": len(raw) - end,
                                        "quote": raw[end:].decode("utf-8")})
        unknown_bytes = sum(_union_size(s) for s in unknown.values())
        wholly_declared = uncovered == 0 and counts["represented"] == total and unknown_bytes == 0 and not any(
            count for status, count in counts.items() if status != "represented")
        self.coverage = {"source_observations": len(self.observations), "source_bytes": total,
                         "by_status_bytes": counts, "represented_bytes": counts["represented"],
                         "uncovered_bytes": uncovered, "uncovered_spans": uncovered_spans, "unknown_bytes": unknown_bytes,
                         "representation_status": "unrepresented" if not self.entities else "complete_declared" if wholly_declared else "partial",
                         "semantic_accuracy": None, "source_status_rows": deepcopy(self.bundle["coverage"]),
                         "located_unknowns": deepcopy(self.bundle["unknowns"])}

    def run(self):
        for value, name, cap in ((self.packet, "source_packet", "max_packet_bytes"), (self.bundle, "bundle", "max_bundle_bytes")):
            _need(len(_canonical(value).encode("utf-8")) <= self.limits[cap], "budget", name, cap + " exceeded")
        self.packet_check()
        self.draft_check()
        self.structure_check()
        self.coverage_check()


def _validate(bundle, source_packet, limits):
    report = {"version": VERSION, "valid": False, "status": "rejected", "errors": [],
              "retained_input": {"bundle": deepcopy(bundle), "source_packet": deepcopy(source_packet)},
              "coverage": {"representation_status": "unknown", "semantic_accuracy": None}, "packet_hash": None}
    validator = None
    try:
        report["packet_hash"] = _hash(source_packet)
        validator = _Validator(bundle, source_packet, limits)
        validator.run()
        report.update(valid=True, status="valid", coverage=deepcopy(validator.coverage))
    except _Rejected as exc:
        report["errors"].append(exc.error)
    except (TypeError, ValueError, KeyError, UnicodeError, OverflowError, RecursionError) as exc:
        # Malformed JSON-shaped values are a rejection, never partial graph data.
        report["errors"].append({"code": "invalid_json_shape", "path": "input", "message": str(exc)})
    return report, validator


def validate_bundle(bundle, source_packet, limits=None):
    """Return a reversible report; source correctness is not self-graded."""
    return _validate(bundle, source_packet, limits)[0]


def compile_bundle(bundle, source_packet, limits=None):
    """Resolve local draft handles and derive a typed incidence graph only."""
    report, validator = _validate(bundle, source_packet, limits)
    report.update(drafts={"entities": [], "claims": []}, graph={"id": "", "nodes": [], "edges": []},
                  refmap={"entities": {}, "claims": {}}, losses=[], no_inference=True, no_persistence=True)
    if not report["valid"]:
        report["losses"] = [{"kind": "rejected_projection", "detail": "No graph emitted; complete input retained"}]
        return report
    v = validator
    packet_hash = report["packet_hash"]
    entity_refs, claim_refs = report["refmap"]["entities"], report["refmap"]["claims"]
    nodes, edges = {}, []
    # Existing IDs are opaque strings; reserve a projection namespace that
    # cannot collide with any allowlisted native record ID.
    projection_prefix = "projection:"
    native_ids = set(v.existing_entities) | set(v.existing_claims)
    while any(ident.startswith(projection_prefix) for ident in native_ids):
        projection_prefix = "projection:" + projection_prefix

    def support(spans):
        out = []
        for span in spans:
            o = v.observations[span["observation"]]
            out.append({**deepcopy(span), "locator": deepcopy(o["locator"]),
                        "observation_text_hash": hashlib.sha256(o["text"].encode("utf-8")).hexdigest()})
        return out

    for handle, entity in sorted(v.entities.items()):
        ident = "draft_e_" + _hash([packet_hash, entity])
        entity_refs[handle] = ident
        draft = {"id": ident, "source_handle": handle, "kind": entity["kind"], "label": entity["label"],
                 "attrs": deepcopy(entity["attrs"]), "support": support(entity["support"])}
        report["drafts"]["entities"].append(draft)
        attributes = deepcopy(entity["attrs"])
        if entity["kind"] == "expression_occurrence":
            attributes["declared_root"] = handle in bundle["roots"]
        nodes[ident] = {"id": ident, "kind": entity["kind"], "label": entity["label"],
                        "qualifiers": attributes, "provenance": {"draft_handle": handle, "packet_hash": packet_hash}}
    used_existing = {c["object"] for c in v.claims.values() if c["object"] in v.existing_entities}
    for ident in sorted(used_existing):
        entity_refs[ident] = ident
        e = v.existing_entities[ident]
        nodes[ident] = {"id": ident, "kind": "existing_entity:" + e["kind"], "label": e.get("label", ""),
                        "lexical_identity": {"namespace": "existing_entity", "value": ident}, "qualifiers": {},
                        "provenance": {"entity_id": ident, "packet_hash": packet_hash}}
    used_claims = {ref for c in v.claims.values() for ref in c["assessment"]["premises"]["claims"]}
    for ident in sorted(used_claims):
        assessment = v.existing_claims[ident]["assessment"]
        nodes[projection_prefix + "prior:" + ident] = {"id": projection_prefix + "prior:" + ident, "kind": "existing_claim_reference",
                                   "lexical_identity": {"namespace": "existing_claim", "value": ident},
                                   "qualifiers": {k: assessment[k] for k in ("evidence_class", "origin", "status")},
                                   "provenance": {"claim_id": ident, "packet_hash": packet_hash}}

    def edge(src, dst, predicate, claim_id):
        edges.append({"id": "edge_" + _hash([src, dst, predicate, claim_id]), "source": src, "target": dst,
                      "predicate": predicate, "qualifiers": {}, "claim_ids": [claim_id]})

    for handle, claim in sorted(v.claims.items()):
        draft = deepcopy(claim)
        draft.pop("handle")
        draft["source_handle"] = handle
        draft["subject"] = entity_refs[draft["subject"]]
        if draft["object"]:
            draft["object"] = entity_refs[draft["object"]]
        draft["qualifiers"]["scope"] = entity_refs[draft["qualifiers"]["scope"]]
        draft["assessment"]["basis"]["support"] = support(draft["assessment"]["basis"]["support"])
        ident = "draft_c_" + _hash([packet_hash, draft])
        claim_refs[handle] = ident
        draft["id"] = ident
        report["drafts"]["claims"].append(draft)
        nodes[ident] = {"id": ident, "kind": "candidate_claim",
                        "qualifiers": {"predicate": draft["predicate"], **deepcopy(draft["qualifiers"]["extra"])},
                        "claim_ids": [ident], "provenance": {"draft_handle": handle, "packet_hash": packet_hash}}
        edge(ident, draft["subject"], "subject", ident)
        if draft["object"]:
            edge(ident, draft["object"], "object", ident)
        else:
            literal = projection_prefix + "literal:" + ident
            nodes[literal] = {"id": literal, "kind": "literal", "qualifiers": {"type": "string", "value": draft["value"]}, "claim_ids": [ident]}
            edge(ident, literal, "value", ident)
        edge(ident, draft["qualifiers"]["scope"], "scope", ident)
        for premise in draft["assessment"]["premises"]["claims"]:
            edge(ident, projection_prefix + "prior:" + premise, "premise", ident)
    graph = {"nodes": [nodes[k] for k in sorted(nodes)], "edges": sorted(edges, key=lambda e: e["id"])}
    graph["id"] = "candidate_graph_" + _hash(graph)
    report["graph"] = graph
    report["losses"] = [
        {"kind": "comparison_sidecar", "fields": ["source bytes", "support locators", "full old-context Assessment", "unknown packet metadata"],
         "retained_at": "retained_input and drafts; graph provenance/refmap identifies records", "deleted": False},
        {"kind": "unrepresented_source", "uncovered_bytes": report["coverage"]["uncovered_bytes"],
         "unknown_bytes": report["coverage"]["unknown_bytes"], "wildcard_nodes_created": False},
        {"kind": "unsupported_semantics", "items": ["truth", "inference validity", "source interpretation correctness", "future-context eligibility", "canonical promotion"]}]
    return report


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("source_packet", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    fn = validate_bundle if args.validate_only else compile_bundle
    print(json.dumps(fn(json.loads(args.bundle.read_text()), json.loads(args.source_packet.read_text())), ensure_ascii=False, indent=2))
