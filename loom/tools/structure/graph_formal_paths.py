"""Pure formal path reasoning over supplied source assertions, never world truth.

No network, credential reads, validation loading or authoritative graph writes.
The input graph's extraction/annotation quality is a separate measured stage.
"""
from __future__ import annotations

from collections import defaultdict
from copy import deepcopy
from datetime import datetime
import json
from pathlib import Path

POLICY_PATH = Path(__file__).with_name('graph_formal_paths_policy.json')
VERSION = 'loom.graph_formal_paths/1'
PREMISE_MODES = {'authored_mechanism', 'oracle_annotation', 'model_predicted_source_assertion'}


def _time(value):
    if not isinstance(value, str) or not value:
        raise ValueError('known_at_not_string')
    try:
        result = datetime.fromisoformat(value.replace('Z', '+00:00'))
    except ValueError as exc:
        raise ValueError('invalid_known_at') from exc
    if result.tzinfo is None:
        raise ValueError('known_at_without_timezone')
    return result


def _text(value, field):
    if not isinstance(value, str) or not value:
        raise ValueError('invalid_' + field)
    return value


def _evidence(value):
    if not isinstance(value, list) or not value:
        raise ValueError('missing_evidence')
    for item in value:
        if not isinstance(item, dict):
            raise ValueError('invalid_evidence')
        for key in ('source_id', 'turn_id', 'quote'):
            _text(item.get(key), 'evidence_' + key)
        for start, end in (('char_start', 'char_end'), ('byte_start', 'byte_end')):
            if start in item or end in item:
                if (type(item.get(start)) is not int or type(item.get(end)) is not int or
                        item[start] < 0 or item[end] <= item[start]):
                    raise ValueError('invalid_evidence_coordinates')


def load_policy():
    return json.loads(POLICY_PATH.read_text(encoding='utf-8'))


def _policy(value):
    value = deepcopy(load_policy() if value is None else value)
    reference = load_policy()
    if not isinstance(value, dict) or set(value) != set(reference):
        raise ValueError('invalid_policy_fields')
    for key in ('schema', 'inference_rule', 'temporal', 'attribution', 'conflict_policy', 'min_edges'):
        if value[key] != reference[key] or (key == 'min_edges' and type(value[key]) is not int):
            raise ValueError('unsupported_policy_' + key)
    for key in ('max_path_length', 'max_paths', 'max_search_states'):
        if type(value[key]) is not int or value[key] < 1:
            raise ValueError('invalid_policy_' + key)
    return value


def _assertions(values):
    if not isinstance(values, list):
        raise ValueError('assertions_not_list')
    output = {}
    for row in values:
        if not isinstance(row, dict):
            raise ValueError('invalid_assertion')
        for key in ('id', 'relation', 'source', 'target', 'attributed_to'):
            _text(row.get(key), key)
        if row['id'] in output:
            raise ValueError('duplicate_assertion_id')
        if row.get('polarity') not in ('positive', 'negative'):
            raise ValueError('invalid_polarity')
        if row.get('content_truth') != 'unverified' or row.get('basis_class') != 'observed_source_assertion':
            raise ValueError('premise_is_not_unverified_source_assertion')
        _time(row.get('known_at')); _evidence(row.get('evidence'))
        output[row['id']] = deepcopy(row)
    return output


def _events(values, assertions):
    if not isinstance(values, list):
        raise ValueError('events_not_list')
    output = []
    for event in values:
        if not isinstance(event, dict) or event.get('status') != 'superseded':
            raise ValueError('invalid_status_event')
        old_id = _text(event.get('assertion_id'), 'event_assertion_id')
        new_id = _text(event.get('superseded_by'), 'event_superseded_by')
        if old_id not in assertions or new_id not in assertions:
            raise ValueError('event_assertion_missing')
        old, new = assertions[old_id], assertions[new_id]
        when = _time(event.get('known_at')); _evidence(event.get('evidence'))
        if _time(old['known_at']) >= when or _time(new['known_at']) != when:
            raise ValueError('invalid_supersession_time')
        if old['attributed_to'] != new['attributed_to']:
            raise ValueError('cross_speaker_supersession')
        output.append(deepcopy(event))
    return sorted(output, key=lambda e: (_time(e['known_at']), e['assertion_id'], e['superseded_by'],
                                        json.dumps(e, sort_keys=True, ensure_ascii=False)))


def _path(edges, source):
    latest = max(edges, key=lambda e: (_time(e['known_at']), e['id']))
    return {
        'premise_assertion_ids': [e['id'] for e in edges],
        'node_ids': [source] + [e['target'] for e in edges],
        'known_at': latest['known_at'],
        'provenance': [{'assertion_id': e['id'], 'attributed_to': e['attributed_to'],
                        'known_at': e['known_at'], 'evidence': deepcopy(e['evidence'])} for e in edges],
        'basis_class': 'inferred', 'content_truth': 'unverified'
    }


def solve_paths(source_assertions, status_events, query, policy=None, *, premise_mode='authored_mechanism'):
    """Return ALL simple directed positive-implies paths, or explicit unavailable.

    Every simple directed path is inclusion-minimal by premise-edge IDs: adding
    a removable directed cycle only adds unnecessary premises. Parallel premise
    assertions and longer incomparable paths remain separate alternatives.
    Supplied negated propositions are opaque IDs, never inferred complements.
    """
    chosen = _policy(policy)
    if not isinstance(premise_mode, str) or premise_mode not in PREMISE_MODES:
        raise ValueError('invalid_premise_mode')
    if not isinstance(query, dict):
        raise ValueError('invalid_query')
    for key in ('id', 'source', 'target', 'attributed_to'):
        _text(query.get(key), 'query_' + key)
    if query.get('relation') != 'implies' or query.get('scope') != 'formal_implication':
        raise ValueError('formal_implies_query_required')
    cutoff = _time(query.get('as_of'))
    assertions = _assertions(source_assertions)
    events = _events(status_events, assertions)
    applicable_events = [e for e in events if _time(e['known_at']) <= cutoff]
    inactive = {e['assertion_id'] for e in applicable_events}
    eligible = [e for e in assertions.values()
                if _time(e['known_at']) <= cutoff and e['id'] not in inactive and
                e['attributed_to'] == query['attributed_to'] and e['relation'] == 'implies']
    positives = [e for e in eligible if e['polarity'] == 'positive']
    negatives = [e for e in eligible if e['polarity'] == 'negative']
    adjacency = defaultdict(list)
    for edge in sorted(positives, key=lambda e: (e['source'], e['target'], e['id'])):
        adjacency[edge['source']].append(edge)
    negative_pairs = {(e['source'], e['target']) for e in negatives}
    conflict_ids = sorted(e['id'] for e in positives if (e['source'], e['target']) in negative_pairs)
    result = {
        'schema': VERSION, 'query_id': query['id'], 'as_of': query['as_of'],
        'relation': query['relation'], 'source': query['source'], 'target': query['target'],
        'attributed_to': query['attributed_to'], 'scope': query['scope'],
        'premise_mode': premise_mode, 'policy': chosen, 'state': 'completed',
        'complete': True, 'label': 'unknown', 'basis_class': 'none',
        'content_truth': 'unverified', 'known_at': None, 'paths': [], 'search_states': 0,
        'eligible_positive_assertion_ids': sorted(e['id'] for e in positives),
        'eligible_negative_assertion_ids': sorted(e['id'] for e in negatives),
        'inactive_assertion_ids': sorted(inactive),
        'status_event_provenance': deepcopy(applicable_events),
        'positive_negative_conflict_assertion_ids': conflict_ids,
        'world_truth_accuracy': None, 'no_graph_promotion': True,
        'limitation': 'formal_support_conditional_on_supplied_premises; not_direct_observation_or_semantic_truth'
    }
    # No identity axiom or cycle-based reflexivity is declared by this policy.
    if query['source'] == query['target']:
        return result
    paths = []

    class LimitReached(Exception):
        pass

    try:
        # Iterator frames keep memory proportional to path depth. There is no
        # Python recursion limit and no queue of every unexplored branch.
        frames = [(query['source'], iter(adjacency[query['source']]))]
        seen = {query['source']}; edges = []; result['search_states'] = 1
        while frames:
            try:
                edge = next(frames[-1][1])
            except StopIteration:
                node, _ = frames.pop(); seen.remove(node)
                if edges:
                    edges.pop()
                continue
            nxt = edge['target']
            if nxt in seen:
                continue
            if len(edges) >= chosen['max_path_length']:
                raise LimitReached('max_path_length')
            if nxt == query['target']:
                if len(paths) >= chosen['max_paths']:
                    raise LimitReached('max_paths')
                paths.append(_path(edges + [edge], query['source']))
            else:
                if result['search_states'] >= chosen['max_search_states']:
                    raise LimitReached('max_search_states')
                result['search_states'] += 1
                edges.append(edge); seen.add(nxt)
                frames.append((nxt, iter(adjacency[nxt])))
    except LimitReached as exc:
        result.update(state='unavailable', complete=False, label=None, reason=str(exc),
                      explored_partial_path_count=len(paths))
        return result
    paths.sort(key=lambda p: (len(p['premise_assertion_ids']), tuple(p['premise_assertion_ids'])))
    if paths:
        earliest = min(paths, key=lambda p: (_time(p['known_at']), tuple(p['premise_assertion_ids'])))
        result.update(label='supported', basis_class='inferred', known_at=earliest['known_at'], paths=paths)
    return result
