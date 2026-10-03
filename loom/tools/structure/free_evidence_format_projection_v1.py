"""Post-DEV offline syntax projection of existing first free-source responses.

The strict primary compiler and original model objects stay untouched. The one
new data policy maps exact unambiguous turn-ID strings to {turn_id} objects.
"""
from __future__ import annotations
import argparse
from collections import Counter
from copy import deepcopy
from decimal import Decimal
from pathlib import Path

try:
    from . import graph_free_extraction as free
except ImportError:
    import graph_free_extraction as free

panel, replay, safe = free.panel, free.integrity, free.safe
ROOT = panel.ROOT
POLICY = Path(__file__).with_name('free_evidence_format_projection_v1.json')
DESTINATION = ROOT / 'docs/research/recipe_experiments_2026-09-30/evidence_format_projection_v1'
BASE = ROOT / 'docs/research/graph_free_extraction_v1/prepared'
ORIGINAL_FREE_SHA256 = '5ccb81d038d80343a0407630fc4ce806b549e9f3c1a2c5798971092f5a70d91d'


def policy():
    value = replay.read(POLICY)
    expected = {'schema': 'loom.free_evidence_format_projection.policy/1',
        'split': 'diagnostic_dev', 'parent_recipe': 'free_source_only_v1',
        'convert': 'exact_unambiguous_turn_id_string_to_turn_id_object',
        'arrays': ['source_assertions', 'status_events'],
        'unresolvable_string': 'leave_unchanged_for_primary_rejection',
        'objects': 'preserve_unchanged', 'other_fields': 'preserve_unchanged',
        'duplicate_json_keys': 'unavailable_no_salvage',
        'truncated_response': 'unavailable_no_salvage',
        'thresholds_and_reference_alignment': 'unchanged',
        'paid_calls': 0, 'canonical_graph_writes': 0}
    if value != expected or panel.digest_file(free.__file__) != ORIGINAL_FREE_SHA256:
        raise ValueError('evidence_projection_policy_or_primary_compiler_drift')
    return value


def normalize_evidence(value, source):
    """Never use gold, aliases, guessed IDs, endpoint or chronology repair."""
    rules = policy()
    if isinstance(value, (bytes, str)):
        value = safe.parse_json(value)  # Original duplicate-key guard remains.
    if not isinstance(value, dict):
        raise ValueError('model_object_required')
    counts = Counter(turn['id'] for turn in source['turns'])
    derived = deepcopy(value); changes = []
    for field in rules['arrays']:
        records = derived.get(field)
        if not isinstance(records, list):
            continue
        for index, record in enumerate(records):
            if not isinstance(record, dict) or not isinstance(record.get('evidence'), list):
                continue
            for offset, evidence in enumerate(record['evidence']):
                if isinstance(evidence, str) and evidence and counts[evidence] == 1:
                    record['evidence'][offset] = {'turn_id': evidence}
                    changes.append({'pointer': f'/{field}/{index}/evidence/{offset}',
                        'original': evidence, 'derived': {'turn_id': evidence},
                        'raw_turn_sha256': safe.digest(next(t for t in source['turns'] if t['id'] == evidence))})
    return derived, changes


def project(base_result, source):
    if base_result['state'] != 'completed':
        return deepcopy(base_result), {'case_id': base_result['case_id'],
            'base_unavailability_preserved': True, 'evidence_items_converted': 0,
            'changes': [], 'no_raw_parse_salvage': True}
    original = base_result['raw_model_object']
    if free.compile_free(original, source) != base_result:
        raise ValueError('base_compiled_or_source_drift')
    derived, changes = normalize_evidence(original, source)
    compiled = free.compile_free(derived, source)
    if (compiled['discovered_nodes'] != base_result['discovered_nodes']
            or compiled['source_payload_sha256'] != base_result['source_payload_sha256']):
        raise ValueError('projection_node_or_raw_source_drift')
    receipt = {'case_id': base_result['case_id'], 'base_unavailability_preserved': False,
        'original_model_object': deepcopy(original),
        'original_model_object_sha256': safe.digest(original),
        'derived_model_object_sha256': safe.digest(derived),
        'source_payload_sha256': safe.digest(source), 'policy_sha256': safe.digest(policy()),
        'changes': changes, 'evidence_items_converted': len(changes),
        'baseline_accepted_assertions': len(base_result['source_assertions']),
        'projected_accepted_assertions': len(compiled['source_assertions']),
        'baseline_invalid_assertions': base_result['invalid_assertions'],
        'projected_invalid_assertions': compiled['invalid_assertions'],
        'baseline_accepted_events': len(base_result['status_events']),
        'projected_accepted_events': len(compiled['status_events'])}
    # Every previously accepted typed record must be retained byte-for-byte;
    # conversion affects only records previously rejected for evidence format.
    for field in ('source_assertions', 'status_events'):
        if any(record not in compiled[field] for record in base_result[field]):
            raise ValueError('previously_accepted_record_changed')
    return compiled, receipt


def input_artifacts():
    files = [POLICY, Path(__file__), Path(__file__).with_name('test_free_evidence_format_projection_v1.py'),
        Path(free.__file__), Path(panel.__file__), Path(replay.__file__), Path(safe.__file__),
        DESTINATION / 'PROTOCOL.md', DESTINATION / 'FIRST_MECHANISM_RESULTS.json',
        ROOT / 'docs/research/graph_free_extraction_v1/FREEZE.json',
        panel.FIXTURE / 'manifest.json', panel.FIXTURE / 'inputs_dev.json']
    for batch in ('batch01', 'batch02'):
        files.extend([BASE / batch / 'prepared/manifest.json', BASE / batch / 'run/ledger.json',
            BASE / batch / 'first_score/compiled_first.json'])
        files.extend(sorted((BASE / batch / 'run').glob('*.response.bin')))
    return files


def freeze():
    policy()
    if (DESTINATION / 'compiled_first.json').exists():
        raise ValueError('cannot_freeze_after_projection_predictions')
    value = {'schema': 'loom.free_evidence_format_projection.freeze/1',
        'frozen_at_utc': safe._utc(), 'before_projection_predictions': True,
        'baseline_dev_outcomes_already_known': True, 'split': 'diagnostic_dev',
        'no_validation_read': True, 'new_paid_calls': 0,
        'single_variable': 'exact unambiguous turn-ID string evidence decoding',
        'primary_compiler_unchanged': True,
        'files_sha256': {str(p.relative_to(ROOT)): panel.digest_file(p) for p in input_artifacts()}}
    panel.write_new(DESTINATION / 'FREEZE.json', value)
    return value


def validate_freeze():
    record = replay.read(DESTINATION / 'FREEZE.json')
    if record.get('before_projection_predictions') is not True:
        raise ValueError('missing_pre_prediction_projection_freeze')
    for name, expected in record['files_sha256'].items():
        if panel.digest_file(ROOT / name) != expected:
            raise ValueError('frozen_projection_artifact_drift')
    return record


def evaluate():
    validate_freeze(); cases = panel.load_dev_inputs()
    base_outputs, summaries = [], []
    for batch in ('batch01', 'batch02'):
        outputs, summary = free.load_run(BASE / batch / 'prepared/manifest.json', BASE / batch / 'run', cases)
        if outputs != replay.read(BASE / batch / 'first_score/compiled_first.json'):
            raise ValueError('baseline_first_compilation_drift')
        base_outputs.extend(outputs); summaries.append(summary)
    if len(base_outputs) != 24 or {c['id'] for c in cases} != {r['case_id'] for r in base_outputs}:
        raise ValueError('planned_24_case_inventory_drift')
    by_case = {c['id']: c for c in cases}
    projected, receipts = [], []
    for result in base_outputs:
        output, receipt = project(result, free.source_payload(by_case[result['case_id']]))
        projected.append(output); receipts.append(receipt)
    panel.write_new(DESTINATION / 'compiled_first.json', projected)
    panel.write_new(DESTINATION / 'projection_receipts_first.json', receipts)
    # Only after first projections persist; reference inventory never drives them.
    golds = panel.load_dev_gold()
    baseline_score = free.score_free(cases, golds, base_outputs)
    projection_score = free.score_free(cases, golds, projected)
    original = {r['case_id']: r for r in base_outputs}
    summary = {'schema': 'loom.free_evidence_format_projection.results/1',
        'execution_kind': 'offline_projection_of_existing_actual_first_responses',
        'new_paid_calls': 0, 'new_reported_cost_usd': '0', 'planned_cases': 24,
        'baseline_unavailable': sum(r['state'] != 'completed' for r in base_outputs),
        'projected_unavailable': sum(r['state'] != 'completed' for r in projected),
        'old_known_reported_cost_usd': str(sum((safe._money(s['reported_known_cost_usd']) for s in summaries), Decimal(0))),
        'evidence_items_converted': sum(r['evidence_items_converted'] for r in receipts),
        'mean_converted_evidence_items_per_planned_case': sum(r['evidence_items_converted'] for r in receipts) / 24,
        'accepted_assertion_delta': sum(len(r.get('source_assertions', [])) - len(original[r['case_id']].get('source_assertions', [])) for r in projected),
        'accepted_event_delta': sum(len(r.get('status_events', [])) - len(original[r['case_id']].get('status_events', [])) for r in projected),
        'previously_accepted_records_preserved': True,
        'baseline': {k: baseline_score[k] for k in ('strict_edges', 'strict_status_events', 'strict_reference_atom_alignment')},
        'projection': {k: projection_score[k] for k in ('strict_edges', 'strict_status_events', 'strict_reference_atom_alignment')},
        'diagnostic_dev_only': True, 'not_a_new_model_run': True,
        'alignment_dependent_lower_bound_not_semantic_hallucination_rate': True,
        'source_clause_adequacy_not_proven_by_citation': True, 'primary_gates_unchanged': True,
        'no_graph_promotion': True, 'no_validation_read': True}
    panel.write_new(DESTINATION / 'score_first.json', projection_score)
    panel.write_new(DESTINATION / 'RESULTS.json', summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('freeze', 'evaluate'))
    args = parser.parse_args(); result = freeze() if args.command == 'freeze' else evaluate()
    print(safe.canonical(result).decode())


if __name__ == '__main__':
    main()
