#!/usr/bin/env python3
"""Validate authored fixtures or export one causal, gold-free model request.

This is evaluation I/O over context_delta.prepare_context, not a graph store,
extractor, model caller or semantic scorer. `export` never opens gold/split files.
"""
import argparse
from copy import deepcopy
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
sys.path.insert(0, str(ROOT / 'loom/tools/structure'))
from context_delta import prepare_context


def read_lines(name):
    return [json.loads(line) for line in (HERE / name).read_text(encoding='utf-8').splitlines() if line]


def instant(value):
    if not isinstance(value, str):
        return None
    result = datetime.fromisoformat(value.replace('Z', '+00:00'))
    return result if result.tzinfo else None


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def project(case, ordinal):
    """All previous messages + complete target, never any later message body.

    Unknown-time old Claims remain visible, explicitly unknown. Claims with any
    future endpoint/support dependency are withheld until that dependency arrives.
    Availability filtering is not a relevance prediction: every eligible Claim is
    presented, including irrelevant candidates. Full snapshot undo sidecars stay local.
    """
    if type(ordinal) is not int or not 0 <= ordinal < len(case['messages']):
        raise ValueError('ordinal out of range')
    target = case['messages'][ordinal]
    cut = instant(target['observed_at'])
    snapshot = case['source_packet']
    observations = {o['id']: o for o in snapshot['observations']}
    entities = {e['id']: e for e in snapshot['entities']}
    def available(record):
        at = instant(record.get('observed_at'))
        return at is None or at <= cut
    spans = []
    for msg in case['messages'][:ordinal + 1]:
        text = observations[msg['observation']]['text']
        spans.append({'id': msg['span_id'], 'observation': msg['observation'],
                      'byte_start': 0, 'byte_len': len(text.encode()), 'quote': text})
    claim_refs = []
    for claim in snapshot['claims']:
        dependencies = [claim]
        dependencies += [observations[s['observation']] for s in claim['assessment']['basis']['support']]
        dependencies += [entities[e] for e in (claim.get('subject'), claim.get('object')) if e in entities]
        if all(available(record) for record in dependencies):
            claim_refs.append({'claim_id': claim['id'], 'selection_reason': 'All temporally eligible memory candidates; relevance not preselected.'})
    prepared = prepare_context(snapshot, spans, claim_refs, time_cut=target['observed_at'])
    if prepared['status'] != 'ready':
        raise ValueError(prepared.get('errors'))
    packet = prepared['packet']
    # Full-snapshot hashes would vary when future data changes even though no
    # future bytes are shown. Keep the native packet hash for delta compatibility;
    # it carries no semantic label and is not a substitute for a causal source view.
    return {'schema': 'loom.eval.conversation_context_request/1', 'case_id': case['case_id'],
            'language': case['language'], 'topics': deepcopy(case['topics']),
            'target_message_id': target['id'], 'messages': deepcopy(case['messages'][:ordinal + 1]),
            'source_packet': packet,
            'instruction': ('Evaluate only the target message, using the supplied prefix and memory candidates. '
                'A message may belong to zero, one or multiple listed topics. Cite exact UTF-8 source spans. '
                'Use onset for first membership in this conversation and return after at least one intervening '
                'message without that topic. Distinguish correction, contradiction and analogy to old Claims. '
                'Select the old Claims needed to interpret, compare or answer this message; do not select every '
                'topically related Claim. Report unresolved or ambiguous project references without guessing. '
                'For messages asking about historical availability, distinguish known_prior from unknown. '
                'Never use later messages or convert hypothetical/differently scoped statements into factual conflicts. '
                'Return one JSON object with case_id, message_id, memberships, boundaries, relations, '
                'selected_claim_ids, reference_decisions, history_decisions. Each membership uses topic_id and span; '
                'each boundary uses topic_id,type,span; each relation uses type,target_claim_id,span. '
                'A span has observation,byte_start,byte_len,quote. Reference decisions use span,status,'
                'alternative_topic_ids,selected_topic_id. History decisions use claim_id,status. '
                'Keep empty arrays explicitly. Do not change stored Claims or resolve conflicts automatically.')}


def exact_span(value, observations):
    assert set(value) == {'observation', 'byte_start', 'byte_len', 'quote'}
    raw = observations[value['observation']]['text'].encode()
    a, n = value['byte_start'], value['byte_len']
    assert type(a) is int and type(n) is int and a >= 0 and n > 0
    assert raw[a:a+n] == value['quote'].encode()


def validate():
    cases = read_lines('inputs.jsonl')
    gold = {row['case_id']: row for row in read_lines('gold.jsonl')}
    splits = json.loads((HERE / 'split.json').read_text())['cases']
    assert len(cases) == len(gold) == len(splits) == 16
    assert len({c['case_id'] for c in cases}) == 16
    family_splits = {}
    count = 0
    for item in splits:
        family_splits.setdefault(item['family'], set()).add(item['split'])
    assert all(len(values) == 1 for values in family_splits.values())
    assert len(family_splits) == 8
    for family in family_splits:
        pair = [x for x in splits if x['family'] == family]
        assert {x['language'] for x in pair} == {'pl', 'en'}
    for case in cases:
        source = case['source_packet']
        assert source['schema'] == 'loom.source_packet/1'
        observations = {o['id']: o for o in source['observations']}
        claims = {c['id']: c for c in source['claims']}
        topics = {t['id'] for t in case['topics']}
        labels = gold[case['case_id']]['labels']
        assert len(case['messages']) == len(labels) == 6
        assert [m['ordinal'] for m in case['messages']] == list(range(6))
        seen_topics = set()
        previous_topics = set()
        for ordinal, (message, label) in enumerate(zip(case['messages'], labels)):
            assert message['id'] == label['message_id']
            assert label['observation'] == message['observation']
            packet = project(case, ordinal)
            visible = {o['id']: o for o in packet['source_packet']['observations']}
            later = {m['observation'] for m in case['messages'][ordinal+1:]}
            assert not set(visible) & later
            assert set(label['selected_claim_ids']) <= {c['id'] for c in packet['source_packet']['claims']}
            current_topics = {x['topic_id'] for x in label['memberships']}
            assert current_topics <= topics
            expected_boundaries = {(t, 'onset' if t not in seen_topics else 'return') for t in current_topics - previous_topics}
            assert {(x['topic_id'], x['type']) for x in label['boundaries']} == expected_boundaries
            seen_topics |= current_topics
            previous_topics = current_topics
            for field in ('memberships', 'boundaries', 'relations', 'reference_decisions'):
                for item in label[field]:
                    exact_span(item['span'], observations)
                    assert item['span']['observation'] == message['observation']
            for rel in label['relations']:
                assert rel['target_claim_id'] in label['selected_claim_ids']
                assert rel['type'] in ('correction', 'contradiction', 'analogy')
            for ref in label['reference_decisions']:
                assert ref['status'] in ('unresolved', 'ambiguous')
                assert ref['selected_topic_id'] is None
                assert set(ref['alternative_topic_ids']) <= topics
            for decision in label['history_decisions']:
                assert decision['claim_id'] in claims
                assert decision['status'] in ('unknown', 'known_prior')
                claim = claims[decision['claim_id']]
                dependencies = [claim] + [observations[s['observation']] for s in claim['assessment']['basis']['support']]
                times = [instant(item.get('observed_at')) for item in dependencies]
                if decision['status'] == 'known_prior':
                    assert all(t is not None and t < instant(message['observed_at']) for t in times)
                else:
                    assert any(t is None for t in times)
            # An explicit sentinel mutation verifies that hidden future text does
            # not occur in the prompt, beyond merely checking Observation IDs.
            if ordinal + 1 < len(case['messages']):
                mutated = deepcopy(case)
                later_id = case['messages'][ordinal + 1]['observation']
                marker = 'FORBIDDEN_FUTURE_SENTINEL_7c7fb34b'
                next(o for o in mutated['source_packet']['observations'] if o['id'] == later_id)['text'] = marker
                assert marker not in canonical(project(mutated, ordinal))
            count += 1
    protocol = json.loads((HERE / 'protocol.json').read_text())
    assert protocol['schema'] == 'loom.eval.conversation_context_protocol/1'
    result = {'status': 'valid', 'cases': len(cases), 'families': len(family_splits),
              'languages': ['pl', 'en'], 'causal_prefix_packets': count,
              'live_model_calls': 0, 'semantic_accuracy_measured': False,
              'files_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(HERE.iterdir())
                               if p.is_file() and p.suffix in ('.json', '.jsonl', '.py') and p.name != 'validation.json'}}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('validate')
    export = sub.add_parser('export')
    export.add_argument('case_id')
    export.add_argument('ordinal', type=int)
    args = parser.parse_args()
    if args.command == 'validate':
        result = validate()
    else:
        case = next(c for c in read_lines('inputs.jsonl') if c['case_id'] == args.case_id)
        result = project(case, args.ordinal)
    print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))

if __name__ == '__main__':
    main()
