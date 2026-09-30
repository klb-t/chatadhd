"""Same-author exact-byte recount, no native rerun, gold or API."""
from collections import Counter
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_panel_v2 as panel
from evaluate_native_v3 import grounding, resolve_pointer


def run():
    freeze = json.loads((panel.HERE / 'freeze_before_evaluation2.json').read_text())
    for path, expected in freeze['files_sha256'].items():
        assert panel.sha(panel.ROOT / path) == expected, path
    ledger = json.loads((panel.HERE / 'first_run_ledger2.json').read_text())
    originals = {c['id']: c for c in json.loads((panel.HERE / 'source_only_inputs.json').read_text())}
    arms = {}
    for row in ledger['rows']:
        count = arms.setdefault(row['arm'], Counter())
        count['planned_cases'] += 1
        if row['state'] != 'completed':
            count['unavailable_cases'] += 1
            continue
        directory = panel.ROOT / row['output_directory']
        snapshot = json.loads((directory / 'native_snapshot.json').read_text())
        document = json.loads((panel.ROOT / row['input_path']).read_text())
        original = originals[row['case_id']]
        observations = snapshot['bodies']['loom_kb_observations']
        by_id = {o['id']: o for o in observations}
        covered, utterances = set(), set()
        for observation in observations:
            bound = grounding(observation, document, original)
            assert bound['valid'] and bound['same_original_date'], observation['id']
            count['exact_observations'] += 1
            expected_source = 'sha256:' + row['input_sha256']
            assert observation['locator']['source'] == expected_source
            count['observation_raw_source_hash_matches'] += 1
            covered.add(bound['turn_id'])
            if observation['kind'] == 'utterance':
                utterances.add(bound['turn_id'])
        count['unique_turns_with_observation'] += len(covered)
        count['unique_turns_with_full_utterance'] += len(utterances)
        count['original_turns'] += len(original['turns'])
        for claim in snapshot['bodies']['loom_kb_claims']:
            support = claim['assessment']['basis']['support']
            assert claim['assessment']['evidence_class'] == 'observed'
            assert support, claim['id']
            for evidence in support:
                observation = by_id[evidence['observation']]
                locator = evidence['locator']
                text = resolve_pointer(document, locator['json_pointer'])
                quote = text.encode()[locator['byte_start']:locator['byte_start'] + locator['byte_len']].decode()
                assert quote == evidence['quote']
                assert locator['source'] == 'sha256:' + row['input_sha256']
                assert evidence['locator'] == observation['locator']
                count['claim_support_bindings_exact'] += 1
            count['claims_with_exact_nonempty_source_support'] += 1
        count['completed_cases'] += 1
    result = {'schema': 'loom.research.native_source_binding_recount/1',
        'same_author_audit': True, 'independent_review': False, 'native_reruns': 0,
        'gold_read': False, 'validation_read': False, 'paid_calls': 0,
        'first_frozen_files_verified': len(freeze['files_sha256']),
        'arms': {k: dict(v) for k, v in arms.items()}, 'semantic_truth_quality_measured': False}
    panel.write_new(panel.HERE / 'SOURCE_BINDING_RECOUNT.json', result)
    print(json.dumps(result))


if __name__ == '__main__':
    run()
