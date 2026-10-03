#!/usr/bin/env python3
"""Independent, source-gold candidate contract evaluation; no model calls."""
from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics
import sys
import time

FIXTURES = Path(__file__).resolve().parents[2] / 'tests/fixtures/eval/independent_candidate_graph_v1'


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(',', ':'), allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def load_fixture(path):
    data = json.loads(Path(path).read_text())
    if data['frozen_sha256'] != digest({k: v for k, v in data.items() if k != 'frozen_sha256'}):
        raise ValueError('frozen fixture changed')
    if data['schema'] != 'loom.independent_candidate_cases/1':
        raise ValueError('unknown independent fixture schema')
    return data


def spans_in(value):
    if isinstance(value, dict):
        if {'observation', 'quote', 'byte_start', 'byte_len'} <= value.keys():
            yield value
        for child in value.values():
            yield from spans_in(child)
    elif isinstance(value, list):
        for child in value:
            yield from spans_in(child)


def source_integrity(case):
    """Check against independent raw bytes, never a method's copied source."""
    source = case['source']
    raw = source['raw_utf8'].encode()
    sha = hashlib.sha256(raw).hexdigest()
    checks = [source['sha256'] == sha, source['id'] == 'sha256:' + sha]
    observations = {o['id']: o for o in case['source_packet']['observations']}
    for observation in observations.values():
        loc = observation['locator']
        checks.append(loc['source'] == source['id'])
        checks.append(type(loc['byte_start']) is int and type(loc['byte_len']) is int and
                      loc['byte_start'] >= 0 and loc['byte_len'] > 0 and loc['byte_start'] + loc['byte_len'] <= len(raw))
        checks.append(raw[loc['byte_start']:loc['byte_start'] + loc['byte_len']] == observation['text'].encode())
    spans = list(spans_in(case['direct_bundle'])) + list(spans_in(case['stage1']))
    for span in spans:
        observation = observations.get(span['observation'])
        if observation is None or type(span['byte_start']) is not int or type(span['byte_len']) is not int:
            checks.append(False)
            continue
        start, length = span['byte_start'], span['byte_len']
        text = observation['text'].encode()
        checks.append(start >= 0 and length > 0 and start + length <= len(text))
        checks.append(text[start:start + length] == span['quote'].encode())
    expected_anchor_hash = digest({'packet_hash': digest(case['source_packet']), 'anchoring': case['stage1']})
    checks.append(case['stage2']['anchors_hash'] == expected_anchor_hash)
    return {'verified': all(checks), 'checks': len(checks), 'span_references': len(spans),
            'unique_spans': len({canonical(s) for s in spans}), 'anchors_hash_verified': checks[-1]}


def normalized_support(support):
    """Ignore only compiler-added provenance fields; keep exact span identity."""
    return sorted(({k: s[k] for k in ('observation', 'quote', 'byte_start', 'byte_len')} for s in support), key=canonical)


def normalized_claim(claim, reverse_entities=None):
    reverse_entities = reverse_entities or {}
    out = {k: deepcopy(claim[k]) for k in ('subject', 'predicate', 'object', 'value', 'qualifiers', 'assessment')}
    out['subject'] = reverse_entities.get(out['subject'], out['subject'])
    out['object'] = reverse_entities.get(out['object'], out['object'])
    out['qualifiers']['scope'] = reverse_entities.get(out['qualifiers']['scope'], out['qualifiers']['scope'])
    out['assessment']['basis']['support'] = normalized_support(out['assessment']['basis']['support'])
    return out


def frame_gold_claims(case):
    """Independent expansion of the frozen public frame syntax, before methods.

    This audits the supplied A/B gold for consistency; it is not a parser or a
    compiler result. Handle assignment and derived graph generation stay outside.
    """
    anchors = {a['handle']: a for a in case['stage1']['anchors']}
    alternatives = []
    for alternative in case['stage2']['alternatives']:
        claims = []

        def emit(record, subject, predicate, obj='', value=None, port=None, ordinal=None):
            support = {canonical(s): s for h in record['support'] for s in anchors[h]['support']}
            extra = {k: record[k] for k in ('polarity', 'assertion_context')}
            if port is not None:
                extra.update(port=port, ordinal=ordinal)
            claims.append({'subject': subject, 'predicate': predicate, 'object': obj, 'value': value,
                'qualifiers': {'scope': record['scope'], 'extra': extra},
                'assessment': {'basis': {'support': [deepcopy(support[k]) for k in sorted(support)]},
                               'premises': {'claims': deepcopy(record['premises'])}}})

        for frame in alternative['frames']:
            emit(frame, frame['anchor'], 'operation_type', value=frame['operation'])
            if frame['operation'] == 'quantifier':
                emit(frame, frame['anchor'], 'quantifier_kind', value=frame['quantifier_kind'])
                emit(frame, frame['anchor'], 'introduces_scope', obj=frame['introduced_scope'])
            for operand in frame['operands']:
                emit(frame, frame['anchor'], 'operand', obj=operand['target'], port=operand['port'], ordinal=operand['ordinal'])
        for link in alternative['links']:
            emit(link, link['subject'], link['predicate'], obj=link['object'])
        alternatives.append(Counter({canonical(normalized_claim(c)): 1 for c in claims}))
    return alternatives


def draft_audit(case, compiled):
    """Compare actual compiled drafts against immutable manually authored gold.

    Agreement between A and B alone is not a truth oracle: both use a compiler.
    This reconstruction independently checks every source-supported draft field.
    """
    bundle = case['direct_bundle']
    expected_entities = {e['handle']: e for e in bundle['entity_drafts']}
    drafts = compiled.get('drafts', {})
    entities, claims = drafts.get('entities', []), drafts.get('claims', [])
    refmap = compiled.get('refmap', {})
    mapping = refmap.get('entities', {})
    reverse = {value: key for key, value in mapping.items()}
    local_ids = {mapping[h] for h in expected_entities if h in mapping}
    canonical_ids = {e['id'] for e in case['source_packet']['entities']}
    scope_ids = {mapping[h] for h, e in expected_entities.items() if e['kind'] == 'scope' and h in mapping}
    checks = []
    observed_handles = []
    for entity in entities:
        handle = entity.get('source_handle')
        observed_handles.append(handle)
        expected = expected_entities.get(handle)
        if expected is None:
            checks.append(False)
            continue
        checks.extend(entity.get(k) == expected[k] for k in ('kind', 'label', 'attrs'))
        checks.append(normalized_support(entity.get('support', [])) == normalized_support(expected['support']))
        checks.append(mapping.get(handle) == entity.get('id'))
    checks.append(Counter(observed_handles) == Counter(expected_entities.keys()))
    entity_ids = [e.get('id') for e in entities]
    claim_ids = [c.get('id') for c in claims]
    unique_ids = (len(set(entity_ids)) == len(entity_ids) and len(set(claim_ids)) == len(claim_ids)
                  and all(isinstance(x, str) and x for x in entity_ids + claim_ids))
    map_closed = {h for h in mapping if h.startswith('@')} == set(expected_entities)
    expected_claims = Counter(canonical(normalized_claim(c)) for c in bundle['claim_drafts'])
    actual_claims = Counter(canonical(normalized_claim(c, reverse)) for c in claims)
    references_closed = unique_ids and map_closed and len(local_ids) == len(expected_entities) and all(
        c['subject'] in local_ids | canonical_ids and
        (c['object'] == '' or c['object'] in local_ids | canonical_ids) and
        c['qualifiers']['scope'] in scope_ids for c in claims)
    preserved = sum((expected_claims & actual_claims).values())
    source_only = all(set(c.get('assessment', {})) == {'basis', 'premises'} for c in claims)
    observations = {o['id']: o for o in case['source_packet']['observations']}
    added_provenance = True
    for span in [s for e in entities for s in e.get('support', [])] + [s for c in claims for s in c['assessment']['basis']['support']]:
        observation = observations.get(span['observation'])
        added_provenance = added_provenance and observation is not None and span.get('locator') == observation['locator'] and span.get('observation_text_hash') == hashlib.sha256(observation['text'].encode()).hexdigest()
    return {'entities_preserved': all(checks), 'claims_preserved': expected_claims == actual_claims and references_closed,
            'compiled_references_closed': references_closed,
            'unique_draft_ids': unique_ids, 'added_provenance_correct': added_provenance,
            'expected_entity_count': len(expected_entities), 'actual_entity_count': len(entities),
            'expected_claim_count': sum(expected_claims.values()), 'preserved_claim_count': preserved,
            'actual_claim_count': len(claims), 'no_fabricated_assessment_fields': source_only,
            'normalized_claims': sorted(actual_claims.elements())}


def load_module(path, name):
    path = Path(path)
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def guard_state(result):
    return result.get('no_inference') is True and result.get('no_persistence') is True


def coverage_audit(case, compiled, contract='direct'):
    gold = case['direct_bundle']
    expected_rows, expected_unknowns = deepcopy(gold['coverage']), deepcopy(gold['unknowns'])
    if contract == 'frames':
        expected_rows, expected_unknowns = deepcopy(case['stage1']['coverage']), deepcopy(case['stage1']['unknowns'])
        anchors = {a['handle']: a for a in case['stage1']['anchors']}
        for field, destination in [('coverage', expected_rows), ('unknowns', expected_unknowns)]:
            for row in case['stage2']['alternatives'][0][field]:
                expanded = deepcopy(row)
                supports = {canonical(s): s for h in row['support'] for s in anchors[h]['support']}
                expanded['support'] = [deepcopy(supports[k]) for k in sorted(supports)]
                destination.append(expanded)
    coverage = compiled.get('coverage', {})
    source_bytes = sum(len(o['text'].encode()) for o in case['source_packet']['observations'])
    expected_status = case['expected']['coverage_status']
    rows_ok = coverage.get('source_status_rows') == expected_rows
    unknowns_ok = coverage.get('located_unknowns') == expected_unknowns
    byte_checks = (coverage.get('source_bytes') == source_bytes and
        coverage.get('by_status_bytes', {}).get(expected_status) == source_bytes and
        coverage.get('uncovered_bytes') == 0 and coverage.get('uncovered_spans') == [] and
        coverage.get('unknown_bytes') == (source_bytes if expected_unknowns else 0))
    expected_rep = 'complete_declared' if gold['roots'] else 'unrepresented'
    graph = compiled.get('graph', {})
    accepted_representation = bool(compiled.get('valid') and graph.get('nodes') and
                                   coverage.get('representation_status') != 'unrepresented')
    expected_acceptance = bool(not case['expected']['reject'] and gold['roots'])
    return {'coverage_retained': rows_ok and unknowns_ok and byte_checks,
            'coverage_rows_retained': rows_ok, 'unknowns_retained': unknowns_ok,
            'byte_counts_correct': byte_checks,
            'declared_representation_status_correct': coverage.get('representation_status') == expected_rep,
            'accepted_representation': accepted_representation,
            'accepted_representation_correct': accepted_representation == expected_acceptance,
            'semantic_accuracy_unclaimed': coverage.get('semantic_accuracy', 'missing') is None,
            'source_packet_retained': compiled.get('retained_input', {}).get('source_packet') == case['source_packet']}


def graph_audit(case, compiled):
    """Reconstruct graph incidence against source gold, not copied sidecars."""
    graph = compiled.get('graph', {})
    node_list, edges = graph.get('nodes', []), graph.get('edges', [])
    nodes = {n['id']: n for n in node_list}
    mapping = compiled.get('refmap', {}).get('entities', {})
    reverse = {v: k for k, v in mapping.items()}
    checks = [len(nodes) == len(node_list)]
    gold = case['direct_bundle']
    for entity in gold['entity_drafts']:
        node = nodes.get(mapping.get(entity['handle']), {})
        expected_qualifiers = deepcopy(entity['attrs'])
        if entity['kind'] == 'expression_occurrence':
            expected_qualifiers['declared_root'] = entity['handle'] in gold['roots']
        checks.append(node.get('kind') == entity['kind'] and node.get('label') == entity['label'] and
                      node.get('qualifiers') == expected_qualifiers)
    expected_roots = {mapping.get(h) for h in gold['roots']}
    roots = {n['id'] for n in node_list if n.get('qualifiers', {}).get('declared_root') is True}
    checks.append(roots == expected_roots)
    outgoing = {}
    for edge in edges:
        checks.append(edge['source'] in nodes and edge['target'] in nodes and edge.get('qualifiers', {}) == {})
        outgoing.setdefault(edge['source'], []).append(edge)
    expected_claims = Counter(canonical(normalized_claim(c)) for c in gold['claim_drafts'])
    seen_claims = Counter()
    checked_claim_ids = set()
    for claim in compiled.get('drafts', {}).get('claims', []):
        key = canonical(normalized_claim(claim, reverse))
        seen_claims[key] += 1
        # The expected fields are directly selected from manually authored gold.
        matches = [c for c in gold['claim_drafts'] if canonical(normalized_claim(c)) == key]
        if not matches:
            checks.append(False)
            continue
        expected = matches[0]
        cid = claim['id']
        checked_claim_ids.add(cid)
        node = nodes.get(cid, {})
        checks.append(node.get('kind') == 'candidate_claim' and node.get('qualifiers') ==
                      {'predicate': expected['predicate'], **expected['qualifiers']['extra']})
        expected_edges = [('subject', mapping.get(expected['subject'], expected['subject'])),
                          ('scope', mapping[expected['qualifiers']['scope']])]
        if expected['object']:
            expected_edges.append(('object', mapping.get(expected['object'], expected['object'])))
        else:
            literal_edges = [e for e in outgoing.get(cid, []) if e['predicate'] == 'value']
            if len(literal_edges) != 1:
                checks.append(False)
            else:
                target = literal_edges[0]['target']
                literal = nodes[target]
                checks.append(literal.get('kind') == 'literal' and literal.get('qualifiers') ==
                              {'type': 'string', 'value': expected['value']})
                expected_edges.append(('value', target))
        # This pilot has no prior-Claim premises; do not imply coverage for them.
        checks.append(not expected['assessment']['premises']['claims'])
        actual_edges = [(e['predicate'], e['target']) for e in outgoing.get(cid, [])]
        checks.append(Counter(actual_edges) == Counter(expected_edges))
    checks.append(seen_claims == expected_claims)
    checks.append({n['id'] for n in node_list if n.get('kind') == 'candidate_claim'} == checked_claim_ids)
    checks.append(all(e['source'] in checked_claim_ids for e in edges))
    return {'graph_restrictions_retained': all(checks), 'checks': len(checks),
            'roots_retained': roots == expected_roots, 'gold_claim_count': sum(expected_claims.values()),
            'checked_claim_count': len(checked_claim_ids)}


def run(fixture_paths, compiler_path, frames_path):
    fixtures = [load_fixture(p) for p in fixture_paths]
    compiler = load_module(compiler_path, 'candidate_graph')
    frames = load_module(frames_path, 'grounded_frames')
    rows = []
    for fixture in fixtures:
        for case in fixture['cases']:
            if not source_integrity(case)['verified']:
                raise ValueError('independent source integrity failed for ' + case['id'])
            packet, bundle = deepcopy(case['source_packet']), deepcopy(case['direct_bundle'])
            before = canonical({'packet': packet, 'bundle': bundle})
            start = time.perf_counter_ns()
            direct = compiler.compile_bundle(bundle, packet)
            direct_ms = (time.perf_counter_ns() - start) / 1e6
            direct_unchanged = before == canonical({'packet': packet, 'bundle': bundle})
            packet, stage1, stage2 = deepcopy(case['source_packet']), deepcopy(case['stage1']), deepcopy(case['stage2'])
            before = canonical({'packet': packet, 'stage1': stage1, 'stage2': stage2})
            start = time.perf_counter_ns()
            composed = frames.compile_frames(packet, stage1, stage2)
            frames_ms = (time.perf_counter_ns() - start) / 1e6
            frames_unchanged = before == canonical({'packet': packet, 'stage1': stage1, 'stage2': stage2})
            direct_audit = draft_audit(case, direct) if direct['valid'] else None
            alternatives = composed.get('alternatives', [])
            frame_audits = [draft_audit(case, a['compiled']) for a in alternatives if a.get('compiled', {}).get('valid')]
            expected_valid = not case['expected']['reject']
            equality = direct_audit is not None and len(frame_audits) == 1 and direct_audit['normalized_claims'] == frame_audits[0]['normalized_claims']
            alternatives_correct = ([a['id'] for a in alternatives] == [a['id'] for a in case['stage2']['alternatives']]) if composed['valid'] else not expected_valid
            rows.append({'id': case['id'], 'split': case['split'], 'language': case['language'], 'family': case['family'],
                'expected_valid': expected_valid, 'expected_coverage': case['expected']['coverage_status'],
                'source_integrity': source_integrity(case), 'direct_valid': direct['valid'], 'frames_valid': composed['valid'],
                'direct_validity_correct': direct['valid'] == expected_valid, 'frames_validity_correct': composed['valid'] == expected_valid,
                'direct_audit': direct_audit, 'frame_audits': frame_audits, 'a_b_claim_equivalence': equality,
                'serialized_payload_bytes': {'direct_bundle': len(canonical(case['direct_bundle']).encode()),
                    'anchors': len(canonical(case['stage1']).encode()), 'composition': len(canonical(case['stage2']).encode()),
                    'frames_total': len(canonical(case['stage1']).encode()) + len(canonical(case['stage2']).encode())},
                'direct_coverage_audit': coverage_audit(case, direct),
                'frames_coverage_audits': [coverage_audit(case, a['compiled'], 'frames') for a in alternatives],
                'direct_graph_audit': graph_audit(case, direct) if direct['valid'] else None,
                'frames_graph_audits': [graph_audit(case, a['compiled']) for a in alternatives if a['compiled']['valid']],
                'alternative_set_correct': alternatives_correct,
                'inputs_unchanged': direct_unchanged and frames_unchanged,
                'all_gates_closed': guard_state(direct) and guard_state(composed) and all(guard_state(a['compiled']) for a in alternatives),
                'latency_ms': {'direct': direct_ms, 'frames': frames_ms}, 'direct_result': direct, 'frames_result': composed})
    summaries = {}
    for split in ('development', 'validation'):
        selected = [r for r in rows if r['split'] == split]
        summaries[split] = {'cases': len(selected), 'expected_valid': sum(r['expected_valid'] for r in selected),
            'direct_validity_correct': sum(r['direct_validity_correct'] for r in selected),
            'frames_validity_correct': sum(r['frames_validity_correct'] for r in selected),
            'direct_gold_preservation': sum(bool(r['direct_audit']) and all(r['direct_audit'][k] for k in ('entities_preserved','claims_preserved','no_fabricated_assessment_fields')) for r in selected),
            'frames_gold_preservation': sum(len(r['frame_audits']) == 1 and all(r['frame_audits'][0][k] for k in ('entities_preserved','claims_preserved','no_fabricated_assessment_fields')) for r in selected),
            'a_b_equivalent': sum(r['a_b_claim_equivalence'] for r in selected),
            'accepted_representations': sum(r['direct_coverage_audit']['accepted_representation'] for r in selected),
            'coverage_retained': sum(r['direct_coverage_audit']['coverage_retained'] for r in selected),
            'frames_coverage_retained': sum(len(r['frames_coverage_audits']) == 1 and r['frames_coverage_audits'][0]['coverage_retained'] for r in selected),
            'direct_graph_retained': sum(bool(r['direct_graph_audit']) and r['direct_graph_audit']['graph_restrictions_retained'] for r in selected),
            'frames_graph_retained': sum(len(r['frames_graph_audits']) == 1 and r['frames_graph_audits'][0]['graph_restrictions_retained'] for r in selected),
            'closed_gate_cases': sum(r['all_gates_closed'] for r in selected), 'unchanged_inputs': sum(r['inputs_unchanged'] for r in selected),
            'direct_median_ms': statistics.median(r['latency_ms']['direct'] for r in selected),
            'frames_median_ms': statistics.median(r['latency_ms']['frames'] for r in selected)}
    represented = [r for r in rows if r['direct_coverage_audit']['accepted_representation']]
    by_id = {r['id']: r for r in rows}
    contrast_retention = []
    for fixture in fixtures:
        for contrast in fixture['contrasts']:
            members = [by_id[contrast[k]] for k in ('left', 'right')]
            contrast_retention.append({**contrast, 'split': fixture['split'],
                'both_direct_graphs_preserve_gold': all(r['direct_graph_audit'] and r['direct_graph_audit']['graph_restrictions_retained'] and r['direct_audit']['claims_preserved'] for r in members),
                'both_frame_graphs_preserve_gold': all(len(r['frames_graph_audits']) == 1 and r['frames_graph_audits'][0]['graph_restrictions_retained'] and r['frame_audits'][0]['claims_preserved'] for r in members),
                'interpretation': 'Preservation of each side of the independently labeled contrast, not a matcher accuracy or natural-language equivalence score.'})
    payload_sizes = {key: {'minimum': min(r['serialized_payload_bytes'][key] for r in represented),
        'median': statistics.median(r['serialized_payload_bytes'][key] for r in represented),
        'maximum': max(r['serialized_payload_bytes'][key] for r in represented)}
        for key in ('direct_bundle', 'anchors', 'composition', 'frames_total')}
    return {'schema': 'loom.independent_candidate_report/1',
        'fixtures': {str(p): f['frozen_sha256'] for p, f in zip(fixture_paths, fixtures)},
        'method_sha256': {str(p): hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in (compiler_path, frames_path)},
        'evaluator_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'summaries': summaries, 'contrast_retention': contrast_retention,
        'represented_payload_sizes_bytes': {'cases': len(represented), 'sizes': payload_sizes,
            'interpretation': 'Canonical serialized UTF-8 supplied gold payloads only; not tokenizer counts, actual model outputs or provider cost.'}, 'rows': rows,
        'limitations': ['Manually supplied source-grounded candidate graphs and frames, not model extraction quality.',
            'A/B agreement is measured separately from per-field agreement with independent source gold.',
            'Draft preservation is not proof validity, world truth or canonical persistence eligibility.',
            'Local one-call timings describe small curated cases and are not production latency guarantees.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--frames', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run([FIXTURES / 'development_cases.json', FIXTURES / 'validation_cases.json'], args.compiler, args.frames)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'path': str(args.output), 'summaries': result['summaries']}))


if __name__ == '__main__':
    main()
