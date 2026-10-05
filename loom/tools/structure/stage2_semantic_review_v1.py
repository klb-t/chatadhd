"""Validate source-only reference/review records; aggregate manual judgements.

This module does not infer semantics, match labels, call models, or certify truth.
The prepared request inventory, not the number of received reviews, is denominator.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_bytes())


def check_span(span, observations):
    observation = observations.get(span.get('observation'))
    if observation is None:
        raise ValueError('unknown_observation')
    start, length = span.get('byte_start'), span.get('byte_len')
    if (type(start) is not int or type(length) is not int or
            start < 0 or length <= 0):
        raise ValueError('invalid_utf8_span')
    raw = observation['text'].encode('utf-8')
    quote = span.get('quote')
    if not isinstance(quote, str) or not quote or start + length > len(raw):
        raise ValueError('invalid_utf8_span')
    if raw[start:start + length] != quote.encode('utf-8'):
        raise ValueError('exact_span_mismatch')


def validate_reference(reference, inputs):
    if reference.get('schema') != 'loom.native_semantic_review.reference/1':
        raise ValueError('reference_schema')
    cases = {c['id']: c for c in inputs['cases']}
    refs = reference['cases']
    if len({c['case_id'] for c in refs}) != len(refs) or set(cases) != {c['case_id'] for c in refs}:
        raise ValueError('reference_case_inventory')
    ids = set()
    for ref in refs:
        case = cases[ref['case_id']]
        if ref['packet_hash'] != case['packet_hash']:
            raise ValueError('reference_packet_hash')
        obs = {o['id']: o for o in case['source_packet']['observations']}
        locators = ref['source_locators']
        if {x['observation'] for x in locators} != set(obs):
            raise ValueError('reference_locator_inventory')
        for loc in locators:
            original = obs[loc['observation']]
            if any(loc[k] != original[k] for k in ('ordinal', 'speaker', 'locator')) or loc['node'] != original['attrs']['node']:
                raise ValueError('reference_locator_drift')
        if not ref['criteria']:
            raise ValueError('missing_case_criteria')
        for criterion in ref['criteria']:
            if criterion['id'] in ids:
                raise ValueError('duplicate_criterion')
            ids.add(criterion['id'])
            if not criterion['support'] or not criterion['expected_interpretation'] or not criterion['forbidden_interpretations']:
                raise ValueError('empty_semantic_criterion')
            for span in criterion['support']:
                check_span(span, obs)
    return len(ids)


def score(reference, inputs, prepared, reviews):
    """Score manually judged alternatives; missing records stay unresolved.

    Review schema: {records:[{request_id, request_hash, response_sha256,
    native_contract_valid, bundle_count, reviewer, criteria:[{criterion_id,
    bundle_judgements:[{bundle_index, judgement, rationale, candidate_pointers,
    source_support}]}]}]}. Every alternative must satisfy a criterion for that
    request criterion to satisfy it. Any violated alternative is a violation.
    Native validity and response hashes are reviewer declarations, not verified
    by this aggregation tool; retain their independently checked evidence.
    """
    validate_reference(reference, inputs)
    refs = {c['case_id']: c for c in reference['cases']}
    packets = {c['id']: c for c in inputs['cases']}
    planned = prepared['requests']
    plan_map = {p['request']['id']: p for p in planned}
    if len(plan_map) != len(planned) or len(planned) != prepared['planned_requests']:
        raise ValueError('planned_request_inventory')
    records = reviews.get('records', [])
    records_map = {r['request_id']: r for r in records}
    if len(records_map) != len(records) or not set(records_map) <= set(plan_map):
        raise ValueError('review_request_inventory')
    outcomes = []
    for rid, request in plan_map.items():
        ref = refs[request['case_id']]
        record = records_map.get(rid)
        judgement_map = {}
        eligible = False
        bundle_count = 0
        if record:
            if record['request_hash'] != request['request_hash']:
                raise ValueError('review_request_hash')
            digest = record.get('response_sha256', '')
            if len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
                raise ValueError('review_response_hash')
            if not record.get('reviewer'):
                raise ValueError('reviewer_required')
            bundle_count = record['bundle_count']
            if type(bundle_count) is not int or bundle_count < 0:
                raise ValueError('review_bundle_count')
            eligible = record.get('native_contract_valid') is True and bundle_count > 0
            judgements = record.get('criteria', [])
            judgement_map = {j['criterion_id']: j for j in judgements}
            if len(judgement_map) != len(judgements) or not set(judgement_map) <= {c['id'] for c in ref['criteria']}:
                raise ValueError('review_criterion_inventory')
        obs = {o['id']: o for o in packets[request['case_id']]['source_packet']['observations']}
        for criterion in ref['criteria']:
            result = 'unresolved'
            judgement = judgement_map.get(criterion['id'])
            if judgement:
                entries = judgement.get('bundle_judgements', [])
                indices = [j['bundle_index'] for j in entries]
                if len(set(indices)) != len(indices) or any(type(i) is not int or i < 0 or i >= bundle_count for i in indices):
                    raise ValueError('review_bundle_inventory')
                statuses = []
                for entry in entries:
                    status = entry['judgement']
                    if status not in ('satisfied', 'violated', 'unresolved') or not entry.get('rationale'):
                        raise ValueError('review_judgement_required')
                    if not entry.get('source_support'):
                        raise ValueError('review_source_support_required')
                    for span in entry['source_support']:
                        check_span(span, obs)
                    pointers = entry.get('candidate_pointers', [])
                    if status != 'unresolved' and (not pointers or any(not p.startswith(f"/bundles/{entry['bundle_index']}/") for p in pointers)):
                        raise ValueError('review_candidate_pointer_required')
                    statuses.append(status)
                if eligible and 'violated' in statuses:
                    result = 'violated'
                elif eligible and len(entries) == bundle_count and statuses and all(s == 'satisfied' for s in statuses):
                    result = 'satisfied'
            outcomes.append({'request_id': rid, 'case_id': request['case_id'], 'method_id': request['method_id'], 'criterion_id': criterion['id'], 'judgement': result})
    counts = {s: sum(o['judgement'] == s for o in outcomes) for s in ('satisfied', 'violated', 'unresolved')}
    return {'schema': 'loom.native_semantic_review.score/1', 'evidence_status': 'manual_source_interpretation_agreement_not_automated_semantic_truth', 'planned_requests': len(planned), 'received_review_records': len(records), 'planned_criterion_slots': len(outcomes), 'counts': counts, 'satisfied_fraction': counts['satisfied'] / len(outcomes) if outcomes else None, 'outcomes': outcomes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True)
    parser.add_argument('--inputs', required=True)
    parser.add_argument('--prepared', required=True)
    parser.add_argument('--reviews')
    args = parser.parse_args()
    path = Path(args.reference)
    reference = read(path)
    for relative, digest in reference['source_sha256'].items():
        if hashlib.sha256((path.parent / relative).read_bytes()).hexdigest() != digest:
            raise ValueError('reference_source_drift')
    inputs, prepared = read(args.inputs), read(args.prepared)
    result = score(reference, inputs, prepared, read(args.reviews) if args.reviews else {'records': []})
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))


if __name__ == '__main__':
    main()
