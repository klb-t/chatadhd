"""Native inventory and exact source grounding; no invented semantic accuracy."""
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_panel_v2 as panel


def resolve_pointer(document, pointer):
    value = document
    if not pointer.startswith('/'):
        raise ValueError('nonempty_rfc6901_pointer_required')
    for token in pointer[1:].split('/'):
        key = token.replace('~1', '/').replace('~0', '~')
        value = value[int(key)] if isinstance(value, list) else value[key]
    return value


def grounding(observation, document, original):
    locator = observation['locator']
    try:
        pointed = resolve_pointer(document, locator['json_pointer'])
        if not isinstance(pointed, str):
            return {'valid': False, 'reason': 'pointer_not_text'}
        start, length = locator['byte_start'], locator['byte_len']
        if not isinstance(start, int) or not isinstance(length, int) or start < 0 or length < 0:
            return {'valid': False, 'reason': 'relative_utf8_bounds_missing_or_invalid'}
        quote = pointed.encode()[start:start + length].decode()
        if quote != observation['text']:
            return {'valid': False, 'reason': 'exact_native_quote_mismatch'}
        turn_id = locator['json_pointer'].split('/')[2].replace('~1', '/').replace('~0', '~')
        turn = {t['id']: t for t in original['turns']}[turn_id]
        if pointed != turn['text']:
            return {'valid': False, 'reason': 'original_turn_text_drift'}
        same_date = datetime.fromisoformat(observation['date'].replace('Z', '+00:00')) == datetime.fromisoformat(turn['known_at'].replace('Z', '+00:00'))
        return {'valid': True, 'turn_id': turn_id, 'same_original_date': same_date,
            'same_original_speaker': observation['speaker'] == turn['speaker'],
            'native_speaker': observation['speaker'], 'original_speaker': turn['speaker'],
            'byte_start': start, 'byte_len': length}
    except (KeyError, IndexError, ValueError, UnicodeDecodeError, TypeError) as error:
        return {'valid': False, 'reason': 'unbound_pointer_or_utf8_span', 'exception_type': type(error).__name__}


def run():
    before = json.loads((panel.HERE / 'freeze_before_evaluation2.json').read_text())
    for name, h in before['files_sha256'].items():
        if panel.sha(panel.ROOT / name) != h:
            raise ValueError('first_native_artifact_drift:' + name)
    ledger = json.loads((panel.HERE / 'first_run_ledger2.json').read_text())
    sources = {c['id']: c for c in json.loads((panel.HERE / 'source_only_inputs.json').read_text())}
    policy = json.loads((panel.HERE / 'policy.json').read_text())
    arms = {name: {'counts': Counter(), 'cases': [], 'predicates': Counter(), 'evidence_classes': Counter(),
        'observation_kinds': Counter(), 'native_speakers': Counter(), 'semantic_graph_quality': None} for name in policy['arms']}
    for row in ledger['rows']:
        arm = arms[row['arm']]
        arm['counts']['planned_cases'] += 1
        arm['counts']['planned_turns'] += row['source_turns']
        case = {'case_id': row['case_id'], 'state': row['state']}
        if row['state'] != 'completed':
            arm['counts']['unavailable_cases'] += 1
            arm['cases'].append(case)
            continue
        arm['counts']['completed_cases'] += 1
        directory = panel.ROOT / row['output_directory']
        snapshot = json.loads((directory / 'native_snapshot.json').read_text())
        document = json.loads((panel.ROOT / row['input_path']).read_text())
        original = sources[row['case_id']]
        bodies = snapshot['bodies']
        case['counts'] = {k: len(v) for k, v in bodies.items()}
        for table, values in bodies.items():
            arm['counts'][table] += len(values)
        evidence = []
        for observation in bodies['loom_kb_observations']:
            bound = grounding(observation, document, original)
            evidence.append({'observation_id': observation['id']} | bound)
            arm['counts']['observations_grounded_exactly'] += bound['valid']
            arm['counts']['observations_invalid_grounding'] += not bound['valid']
            arm['counts']['observations_original_date_preserved'] += bool(bound.get('same_original_date'))
            arm['counts']['observations_original_speaker_identity_preserved'] += bool(bound.get('same_original_speaker'))
            arm['observation_kinds'][observation['kind']] += 1
            arm['native_speakers'][observation['speaker']] += 1
        case['observation_bindings'] = evidence
        obs_ids = {o['id'] for o in bodies['loom_kb_observations']}
        for claim in bodies['loom_kb_claims']:
            arm['predicates'][claim['predicate']] += 1
            assessment = claim['assessment']
            arm['evidence_classes'][assessment['evidence_class']] += 1
            support = assessment.get('basis', {}).get('support', [])
            if assessment['evidence_class'] == 'observed':
                arm['counts']['observed_claims'] += 1
                arm['counts']['observed_claims_with_nonempty_support'] += bool(support)
                arm['counts']['observed_claims_all_refs_in_case'] += bool(support) and all(s['observation'] in obs_ids for s in support)
            arm['counts']['implies_or_causes_predicate_claims'] += claim['predicate'] in ('implies', 'causes')
        raw_container_copies = [b for b in snapshot['blob_verifications'] if b.get('available') and
            b.get('actual_sha256') == row['input_sha256'] and b['blob_hash'] == row['input_sha256']]
        case['raw_container_copy_verified'] = bool(raw_container_copies)
        arm['counts']['raw_container_copies_verified'] += bool(raw_container_copies)
        messages = snapshot['imported_core_messages']
        arm['counts']['core_imported_messages'] += len(messages)
        expected_texts = Counter(t['text'] for t in original['turns'])
        case['core_message_text_multiset_matches_original'] = Counter(m['text'] for m in messages) == expected_texts
        arm['counts']['core_message_texts_all_preserved_cases'] += case['core_message_text_multiset_matches_original']
        case['process_metrics'] = json.loads((directory / 'process_metrics.json').read_text())
        case['wall_seconds_driver'] = row['wall_seconds']
        arm['cases'].append(case)
    for arm in arms.values():
        for name in ('counts', 'predicates', 'evidence_classes', 'observation_kinds', 'native_speakers'):
            arm[name] = dict(arm[name])
        if arm['counts'].get('implies_or_causes_predicate_claims', 0):
            arm['semantic_quality_unavailable_reason'] = 'typed_predicate_candidates_require_separate_frozen_actor_polarity_node_adapter_not_automatically_equivalent'
        else:
            arm['semantic_quality_unavailable_reason'] = 'no_native_implies_or_causes_typed_edge_output_contract'
        completed = [c for c in arm['cases'] if c['state'] == 'completed']
        arm['runtime'] = {'sum_driver_wall_seconds': sum(c['wall_seconds_driver'] for c in completed),
            'sum_native_user_cpu_seconds': sum(c['process_metrics']['user_cpu_seconds'] for c in completed),
            'sum_native_system_cpu_seconds': sum(c['process_metrics']['system_cpu_seconds'] for c in completed),
            'max_native_process_rss_kib': max((c['process_metrics']['max_rss_kib'] for c in completed), default=None)}
    result = {'schema': 'loom.research.native_source_free_inventory_results/1', 'created_at': datetime.now(timezone.utc).isoformat(),
        'split': 'dev', 'arms': arms, 'planned_source_cases_per_arm': 24, 'all_source_only_extractions_frozen_before_evaluation': True,
        'this_arm_gold_read': False, 'prior_dev_gold_exposed_elsewhere': True, 'semantic_accuracy': None,
        'semantic_precision': None, 'semantic_recall': None, 'content_truth_accuracy': None,
        'native_binary_sha256': ledger['native_binary_sha256'], 'run_ledger_sha256': panel.sha(panel.HERE / 'first_run_ledger2.json'),
        'scorer_sha256': panel.sha(__file__), 'validation_accessed': False, 'paid_calls': 0}
    panel.write_new(panel.HERE / 'first_results3.json', result)
    print(json.dumps({'arms': {k: v['counts'] for k, v in arms.items()}, 'semantic_accuracy': None, 'paid_calls': 0}))


if __name__ == '__main__':
    run()
