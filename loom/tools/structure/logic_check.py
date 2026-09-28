"""Bounded propositional checking of supplied candidate graphs; no source truth claim."""
from collections import defaultdict
from copy import deepcopy
import hashlib
import json
import math
import sys

try:
    from .candidate_graph import validate_bundle
except ImportError:
    from candidate_graph import validate_bundle

VERSION = "candidate-propositional-check/1"
DEFAULT_LIMITS = {"max_atoms": 12, "max_assignments": 4096}
_MAX_INPUT_BYTES = 4 * 1024 * 1024


def _preflight(value):
    """Bound ordinary JSON shapes before recursive copies/validation."""
    stack, nodes, text_bytes = [(value, 0)], 0, 0
    while stack:
        item, depth = stack.pop()
        nodes += 1
        if nodes > 100000 or depth > 128:
            return "JSON nesting/node bound exceeded"
        if type(item) is int and item.bit_length() > 4096:
            return "JSON integer bound exceeded"
        if item is None or type(item) in (bool, int):
            continue
        if type(item) is float:
            if not math.isfinite(item):
                return "Non-finite number is not JSON"
        elif type(item) is str:
            try:
                text_bytes += len(item.encode("utf-8"))
            except UnicodeError:
                return "Invalid UTF-8 string"
            if text_bytes > _MAX_INPUT_BYTES:
                return "Input text bound exceeded"
        elif type(item) is list:
            if len(item) > 100000:
                return "JSON collection bound exceeded"
            stack.extend((v, depth + 1) for v in item)
        elif type(item) is dict:
            if len(item) > 100000 or any(type(k) is not str for k in item):
                return "Invalid JSON object keys/size"
            stack.extend((v, depth + 1) for pair in item.items() for v in pair)
        else:
            return "Input contains a non-JSON value"
    return None


def check_bundle(bundle, source_packet, premise_roots, conclusion_root, limits=None):
    """Check one explicit query; labels/symbols never identify atomic propositions.

    Material implication is an assumption of this projection, not a claim about
    the source's conditional. Different predicate-application handles remain
    independent atoms, even if their predicate/argument labels are identical.
    """
    result = {
        "version": VERSION, "status": "rejected", "errors": [],
        "packet_hash": None, "validation": None, "retained_input": None,
        "fragment": "classical_propositional_shared_occurrences",
        "assumptions": {
            "conditional": "material_implication",
            "atom_identity": "predicate_application_occurrence_handle_only",
            "distinct_occurrences": "independent_even_when_labels_symbols_or_arguments_match",
            "premises": "caller_selected_declared_roots_only",
        },
        "no_persistence": True, "no_promotion": True,
        "source_interpretation_validated": False, "source_truth_validated": False,
        "atoms": [], "expression_sources": {}, "ignored_roots": [],
        "assignments_checked": 0, "required_assignments": None,
        "premise_models": 0, "premise_consistent": None,
        "witnesses": {"premises": None, "conclusion_true": None, "conclusion_false": None},
    }

    def stop(status, code, path, message):
        result["status"] = status
        result["errors"].append({"code": code, "path": path, "message": message})
        return result

    inputs = {"bundle": bundle, "source_packet": source_packet,
              "premise_roots": premise_roots, "conclusion_root": conclusion_root,
              "limits": limits}
    problem = _preflight(inputs)
    if problem:
        result["retention"] = {"status": "not_copied", "input_ownership": "caller"}
        return stop("rejected", "input_preflight", "input", problem)
    result["retained_input"] = deepcopy(inputs)
    result["retention"] = {"status": "retained"}
    effective = dict(DEFAULT_LIMITS)
    if limits is not None:
        if type(limits) is not dict:
            return stop("rejected", "limits", "limits", "Expected a limit object")
        for key, value in limits.items():
            if key not in effective or type(value) is not int or not 1 <= value <= effective[key]:
                return stop("rejected", "limits", "limits." + key, "Limits may only lower named positive maxima")
            effective[key] = value
    result["limits"] = effective
    try:
        validation = validate_bundle(bundle, source_packet)
    except (TypeError, ValueError, KeyError, UnicodeError, OverflowError, RecursionError) as exc:
        return stop("rejected", "validation_input", "input", str(exc))
    # Complete inputs are retained once by this report, not duplicated here.
    result["validation"] = {k: v for k, v in validation.items() if k != "retained_input"}
    result["packet_hash"] = validation.get("packet_hash")
    if not validation["valid"]:
        return stop("rejected", "bundle_rejected", "bundle", "Candidate graph validation failed")
    roots = bundle["roots"]
    if (type(premise_roots) is not list
            or any(type(h) is not str or h not in roots for h in premise_roots)
            or len(set(premise_roots)) != len(premise_roots)):
        return stop("rejected", "premise_roots", "premise_roots", "Expected distinct declared expression roots")
    if type(conclusion_root) is not str or conclusion_root not in roots:
        return stop("rejected", "conclusion_root", "conclusion_root", "Conclusion must be a declared expression root")
    result["ignored_roots"] = sorted(set(roots) - set(premise_roots) - {conclusion_root})
    result["conclusion_is_premise"] = conclusion_root in premise_roots
    entities = {e["handle"]: e for e in bundle["entity_drafts"]}
    scopes = [e for e in entities.values() if e["kind"] == "scope"]
    if len(scopes) != 1 or scopes[0]["attrs"] != {"scope_type": "assertion", "assertion_context": "asserted"}:
        return stop("unsupported", "scope", "bundle", "Only one asserted assertion scope is supported")
    allowed_operations = {"predicate_application", "conditional", "negation", "conjunction"}
    operations, ports, outgoing = {}, defaultdict(lambda: defaultdict(list)), defaultdict(list)
    for claim in bundle["claim_drafts"]:
        handle = claim["handle"]
        extra = claim["qualifiers"]["extra"]
        if extra["polarity"] != "positive" or extra["assertion_context"] != "asserted":
            return stop("unsupported", "qualifier", handle, "Negative/unknown structural qualifiers are not logical negation")
        if claim["assessment"]["premises"]["claims"]:
            return stop("unsupported", "external_premises", handle, "Prior Claim references are not compiled as formulas")
        predicate = claim["predicate"]
        if predicate not in {"operation_type", "in_scope", "operand"}:
            return stop("unsupported", "relation", handle, "Relation has no semantics in this conservative fragment")
        subject = claim["subject"]
        outgoing[subject].append(claim)
        if predicate == "operation_type":
            if claim["value"] not in allowed_operations:
                return stop("unsupported", "operation", handle, "Unsupported logical operation")
            operations[subject] = claim["value"]
        elif predicate == "operand":
            ports[subject][extra["port"]].append((extra["ordinal"], claim["object"]))
    for per_node in ports.values():
        for port, targets in per_node.items():
            per_node[port] = [target for _, target in sorted(targets)]
    selected = list(premise_roots) + [conclusion_root]
    used, pending = set(), list(selected)
    while pending:
        handle = pending.pop()
        if handle in used:
            continue
        used.add(handle)
        if operations[handle] != "predicate_application":
            pending.extend(child for targets in ports[handle].values() for child in targets)
    observations = {o["id"]: o for o in source_packet["observations"]}

    def support_for(handle):
        spans = list(entities[handle]["support"])
        spans.extend(s for c in outgoing[handle] for s in c["assessment"]["basis"]["support"])
        unique, seen = [], set()
        for span in spans:
            key = (span["observation"], span["byte_start"], span["byte_len"], span["quote"])
            if key in seen:
                continue
            seen.add(key)
            obs = observations[span["observation"]]
            unique.append({**deepcopy(span), "locator": deepcopy(obs["locator"]),
                           "observation_text_hash": hashlib.sha256(obs["text"].encode("utf-8")).hexdigest()})
        return unique

    atom_handles = sorted(h for h in used if operations[h] == "predicate_application")
    result["expression_sources"] = {h: support_for(h) for h in sorted(used)}
    result["atoms"] = [{"handle": h, "support": result["expression_sources"][h]} for h in atom_handles]
    required = 1 << len(atom_handles)
    result["required_assignments"] = required
    if len(atom_handles) > effective["max_atoms"] or required > effective["max_assignments"]:
        return stop("limit", "search_limit", "limits", "Exact search exceeds the configured atom/assignment bound")

    def evaluate(handle, assignment, memo):
        if handle in memo:
            return memo[handle]
        op, p = operations[handle], ports[handle]
        if op == "predicate_application":
            value = assignment[handle]
        elif op == "negation":
            value = not evaluate(p["body"][0], assignment, memo)
        elif op == "conjunction":
            value = all(evaluate(h, assignment, memo) for h in p["member"])
        else:  # Supported conditional only; its material meaning is explicit.
            value = not evaluate(p["antecedent"][0], assignment, memo) or evaluate(p["consequent"][0], assignment, memo)
        memo[handle] = value
        return value

    for number in range(required):
        assignment = {handle: bool(number & (1 << i)) for i, handle in enumerate(atom_handles)}
        memo = {}
        result["assignments_checked"] += 1
        if not all(evaluate(h, assignment, memo) for h in premise_roots):
            continue
        result["premise_models"] += 1
        if result["witnesses"]["premises"] is None:
            result["witnesses"]["premises"] = dict(assignment)
        outcome = evaluate(conclusion_root, assignment, memo)
        key = "conclusion_true" if outcome else "conclusion_false"
        if result["witnesses"][key] is None:
            result["witnesses"][key] = dict(assignment)
    result["premise_consistent"] = result["premise_models"] > 0
    if not result["premise_consistent"]:
        result["status"] = "inconsistent_premises"
    elif result["witnesses"]["conclusion_false"] is None:
        result["status"] = "entailed"
    elif result["witnesses"]["conclusion_true"] is None:
        result["status"] = "contradicted"
    else:
        result["status"] = "undetermined"
    return result


def main():
    """Read one bounded JSON query from stdin; write its machine-readable report."""
    raw = sys.stdin.buffer.read(_MAX_INPUT_BYTES + 1)
    try:
        if len(raw) > _MAX_INPUT_BYTES:
            raise ValueError("Input exceeds 4 MiB")
        request = json.loads(raw)
        if type(request) is not dict or set(request) - {"bundle", "source_packet", "premise_roots", "conclusion_root", "limits"}:
            raise ValueError("Invalid request object")
        if not {"bundle", "source_packet", "premise_roots", "conclusion_root"} <= request.keys():
            raise ValueError("Missing required request fields")
        report = check_bundle(**request)
    except (ValueError, TypeError, UnicodeError, RecursionError) as exc:
        report = {"version": VERSION, "status": "rejected", "errors": [{"code": "input", "path": "stdin", "message": str(exc)}],
                  "retained_input": None, "no_persistence": True, "no_promotion": True,
                  "source_interpretation_validated": False, "source_truth_validated": False}
    json.dump(report, sys.stdout, ensure_ascii=False, allow_nan=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
