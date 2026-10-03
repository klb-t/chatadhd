#!/usr/bin/env python3
"""Independent, bounded source-anchored diagnostic scoring; no model judge.

The trees below are disposable evaluation projections of existing candidate
Entity/Claim graphs. They are neither an authoritative AST nor persisted state.
"""
from __future__ import annotations

from collections import defaultdict
import hashlib
import json
import math

try:
    from .candidate_graph import validate_bundle
except ImportError:
    from candidate_graph import validate_bundle

VERSION = 'loom.live_pilot_score/1'


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False)


def _sha(value):
    return hashlib.sha256(_canonical(value).encode('utf-8')).hexdigest()


def _preflight(value):
    """Bound input before the shared validator's retaining deepcopy."""
    stack = [(value, 0)]
    count = string_bytes = 0
    while stack:
        item, depth = stack.pop()
        count += 1
        if depth > 64 or count > 100000:
            return 'nesting or item bound exceeded'
        if isinstance(item, str):
            try:
                string_bytes += len(item.encode('utf-8'))
            except UnicodeError:
                return 'invalid Unicode'
            if string_bytes > 1048576:
                return 'string byte bound exceeded'
        elif item is None or type(item) is bool:
            pass
        elif type(item) is int:
            if item.bit_length() > 4096:
                return 'integer bound exceeded'
        elif type(item) is float:
            if not math.isfinite(item):
                return 'non-finite number'
        elif isinstance(item, dict):
            if len(item) > 100000 or not all(isinstance(k, str) for k in item):
                return 'object bound or key type'
            stack.extend((k, depth + 1) for k in item)
            stack.extend((v, depth + 1) for v in item.values())
        elif isinstance(item, list):
            if len(item) > 100000:
                return 'array item bound exceeded'
            stack.extend((v, depth + 1) for v in item)
        else:
            return 'not a JSON value'
    return None


def _supports_valid(bundle, packet):
    """Check grounding independently so a graph error is not a source success."""
    try:
        observations = {o['id']: o['text'].encode('utf-8') for o in packet['observations']}
        supports = [e['support'] for e in bundle['entity_drafts']]
        supports += [c['assessment']['basis']['support'] for c in bundle['claim_drafts']]
        supports += [r['support'] for k in ('coverage', 'unknowns') for r in bundle[k]]
        if not supports:
            return False
        for support in supports:
            if not isinstance(support, list) or not support:
                return False
            for span in support:
                if set(span) != {'observation', 'byte_start', 'byte_len', 'quote'}:
                    return False
                start, length = span['byte_start'], span['byte_len']
                if type(start) is not int or type(length) is not int or start < 0 or length <= 0:
                    return False
                raw = observations[span['observation']]
                if start + length > len(raw) or raw[start:start + length].decode('utf-8') != span['quote']:
                    return False
        return True
    except (KeyError, TypeError, UnicodeDecodeError, AttributeError):
        return False


def _shape(value):
    """Erase lexical anchors only, preserving operation, role and binding shape."""
    if isinstance(value, list):
        return [_shape(v) for v in value]
    if not isinstance(value, dict):
        return value
    result = {}
    for k, v in value.items():
        if k == 'predicate':
            result[k] = '*'
        elif k == 'arguments':
            result[k] = [a if isinstance(a, dict) and 'variable' in a else '*' for a in v]
        else:
            result[k] = _shape(v)
    return result


def _project(bundle, anchors):
    entities = {e['handle']: e for e in bundle['entity_drafts']}
    rows = defaultdict(list)
    for claim in bundle['claim_drafts']:
        rows[(claim['subject'], claim['predicate'])].append(claim)
    unresolved = []

    def claim_value(handle, predicate):
        return rows[(handle, predicate)][0]

    def context(handle):
        scope = claim_value(handle, 'in_scope')['object']
        return entities[scope]['attrs']['assertion_context']

    def anchor(handle):
        entity = entities.get(handle)
        if entity is None:
            unresolved.append({'handle': handle, 'reason': 'existing_entity_not_anchored_in_this_pilot'})
            return {'unresolved': handle}
        matches = set()
        for support in entity['support']:
            begin = support['byte_start']
            end = begin + support['byte_len']
            for name, locations in anchors.items():
                if any(loc['observation'] == support['observation'] and
                       begin <= loc['byte_start'] and
                       loc['byte_start'] + loc['byte_len'] <= end for loc in locations):
                    matches.add(name)
        if len(matches) != 1:
            unresolved.append({'handle': handle, 'reason': 'ambiguous_or_missing_source_anchor',
                               'candidate_anchors': sorted(matches)})
            return {'unresolved': handle}
        return next(iter(matches))

    def term(handle, bindings):
        entity = entities.get(handle)
        if entity and entity['kind'] == 'term_occurrence' and entity['attrs']['term_type'] == 'variable':
            binder = claim_value(handle, 'bound_to')['object']
            if binder not in bindings:
                return {'unresolved_binding': handle}
            return {'variable': bindings.index(binder)}
        return anchor(handle)

    def walk(handle, bindings):
        operation = claim_value(handle, 'operation_type')['value']
        ports = defaultdict(list)
        for claim in rows[(handle, 'operand')]:
            extra = claim['qualifiers']['extra']
            ports[extra['port']].append((extra['ordinal'], claim['object']))
        ports = {p: [h for _, h in sorted(v)] for p, v in ports.items()}
        out = {'op': operation, 'context': context(handle)}
        if operation == 'predicate_application':
            out.update(predicate=anchor(ports['predicate'][0]),
                       arguments=[term(h, bindings) for h in ports.get('argument', [])])
        elif operation == 'conditional':
            out.update(antecedent=walk(ports['antecedent'][0], bindings),
                       consequent=walk(ports['consequent'][0], bindings))
        elif operation == 'negation':
            out['body'] = walk(ports['body'][0], bindings)
        elif operation == 'conjunction':
            out['members'] = [walk(h, bindings) for h in ports['member']]
        elif operation == 'quantifier':
            out['op'] = claim_value(handle, 'quantifier_kind')['value']
            inner = bindings + [ports['binder'][0]]
            out['body'] = walk(ports['body'][0], inner)
            if ports.get('restriction'):
                out['restriction'] = walk(ports['restriction'][0], inner)
        return out

    roots = [walk(handle, []) for handle in bundle['roots']]
    return sorted(roots, key=_canonical), unresolved


def score_case(case_input, gold, bundle):
    """Score one case against frozen human-authored diagnostic expectations.

    `bundle` is parsed JSON in loom.candidate_graph/1 format. Inputs and gold
    identify the same exact packet; mismatch is a caller error (ValueError).
    Unknown/refused/network responses are not valid bundles and score false.
    Semantically equivalent rewrites outside the frozen grammar may score false;
    this is strict representation fidelity, not logical equivalence evaluation.
    """
    packet = case_input['source_packet']
    if case_input['case_id'] != gold['case_id'] or _sha(packet) != gold['source_packet_sha256']:
        raise ValueError('case/gold source identity mismatch')
    preflight_error = _preflight(bundle)
    checked = ({'valid': False, 'errors': [{'code': 'score_input_bound', 'path': 'bundle',
                                           'message': preflight_error}],
                'coverage': {'representation_status': 'unknown', 'semantic_accuracy': None}}
               if preflight_error else validate_bundle(bundle, packet))
    envelope = (isinstance(bundle, dict) and bundle.get('schema') == 'loom.candidate_graph/1' and
                set(bundle) == {'schema', 'packet_id', 'entity_drafts', 'claim_drafts', 'roots', 'coverage', 'unknowns'})
    source_valid = _supports_valid(bundle, packet) if envelope and not preflight_error else False
    result = {'schema': VERSION, 'case_id': case_input['case_id'], 'language': case_input['language'],
              'source_packet_sha256': _sha(packet), 'bundle_sha256': None if preflight_error else _sha(bundle),
              'expected': gold['expected'], 'envelope_valid': envelope,
              'contract_valid': checked['valid'], 'source_support_valid': source_valid,
              'errors': checked['errors'], 'coverage': checked['coverage'],
              'represented': False, 'abstained': False, 'structural_exact': False,
              'anchored_semantic_exact': False, 'semantic_exact': False,
              'projection': [], 'unresolved_anchors': [], 'nonpositive_claim_count': 0,
              'no_model_judge': True, 'no_inference': True, 'no_persistence': True}
    if not checked['valid']:
        return result
    result['represented'] = bool(bundle['roots'])
    result['abstained'] = not bundle['roots'] and bool(bundle['unknowns'])
    result['nonpositive_claim_count'] = sum(
        c['qualifiers']['extra']['polarity'] != 'positive' for c in bundle['claim_drafts'])
    if gold['expected'] == 'abstain':
        # Frozen scope is a whole short passage: no guessed partial graph may
        # masquerade as correct disambiguation or as full located abstention.
        total = sum(len(o['text'].encode('utf-8')) for o in packet['observations'])
        correct = result['abstained'] and checked['coverage']['unknown_bytes'] == total
        result['structural_exact'] = correct
        result['anchored_semantic_exact'] = correct
        result['semantic_exact'] = correct
        return result
    projected, unresolved = _project(bundle, gold['anchors'])
    expected = sorted(gold['roots'], key=_canonical)
    result['projection'], result['unresolved_anchors'] = projected, unresolved
    result['structural_exact'] = sorted((_shape(x) for x in projected), key=_canonical) == sorted((_shape(x) for x in expected), key=_canonical)
    result['anchored_semantic_exact'] = projected == expected
    result['semantic_exact'] = (result['anchored_semantic_exact'] and
                                result['nonpositive_claim_count'] == 0 and
                                not bundle['unknowns'])
    return result
