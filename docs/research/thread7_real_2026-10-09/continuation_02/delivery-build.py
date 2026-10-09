"""Assemble the measured continuation's public projection using the existing exporter.

No model calls, profile engine, provider access, or native execution occurs here.
Inputs are previously reviewed public aggregates; this is not a privacy scrubber.
"""
import argparse
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from jsonschema import Draft202012Validator
from loom.tools.seeding import method_graph as graph
from loom.tools.structure import experiment_analysis_v1 as analysis

BASE = Path(__file__).parent
PREVIOUS = BASE.parent / 'continuation_01'


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def read(path):
    return graph.strict_json(path.read_bytes())


@analysis.public_error_boundary
def build(policy):
    analysis.approve_public_input('continuation02/delivery-policy.json',policy)
    approved_inputs = {}
    for item in policy['inputs']:
        path=(BASE / item['path']).resolve()
        raw=path.read_bytes()
        analysis.approve_public_input('continuation02/'+item['path'],raw)
        if digest(raw) != item['sha256']:
            raise ValueError('public_input_digest_changed')
        approved_inputs[path]=raw
    # Consume the bytes actually approved; reopening would introduce a race
    # for auxiliary contract fields absent from the presentation hash.
    def read(path):
        return graph.strict_json(approved_inputs[path.resolve()])
    presentation = read(PREVIOUS / 'handoff-final-example-v2.json')
    contract = read(PREVIOUS / 'handoff-final-contract-v2.json')
    preferences = read(BASE / 'preferences-receipt.json')
    retrieval = read(BASE / 'retrieval-results-native-reciprocal-v2.json')
    correction = read(BASE / 'retrieval-results-native-reciprocal-receipt.json')
    representations = read(BASE / 'representation-results.json')
    recovery = read(BASE / 'recovery-tests.json')
    presentation.update(schema='loom.thread7_result_presentation/3', version=3,
                        validation_status='offline_measured_model_quality_unvalidated')
    presentation['basic'] = policy['basic']
    expert = presentation['expert']
    expert['preparation_count'] = preferences['preparation']['primary_body_count']
    expert['preparation_details'] = {
        'variant_intentions': preferences['preparation']['total_rows'],
        'fully_prepared': preferences['preparation']['status_counts']['prepared'],
        'primary_ready_extraction_pending': preferences['preparation']['pending_second_phase'],
        'source_preference_annotations_pending': preferences['preparation']['ready_missing_annotation_rows'],
        'live_dispatch_allowed': False,
        'old_small_scope_queues_reference_previous_preparation_version': True,
        'new_preparation_requires_new_frozen_queue_and_payer_manifest': True,
    }
    expert['limitations'] = policy['limitations']
    expert['expanded_panel']['feature_semantics'] = 'source_reviewed_preferences_and_provisional_retrieval_tasks_not_independent_gold'
    expert['offline'] = {
        'unit_of_dependence': retrieval['unit_of_dependence'],
        'source_review': {k: preferences[k] for k in ['families', 'source_user_messages', 'candidate_records', 'decisions', 'relations', 'independent_adjudication']},
        'source_preference_semantics': preferences['preparation']['semantics'],
        'retrieval': {
            'tasks': retrieval['tasks'], 'families': retrieval['families'],
            'answerable_tasks': 46, 'bounded_absence_tasks': 2,
            'methods': retrieval['methods'], 'budgets': retrieval['budgets'],
            'first_rankings': correction['first_rankings'],
            'corrected_rankings': correction['new_corrected_rankings'],
            'executed_rankings_total': correction['total_new_method_rankings_this_package'],
            'current_context_evaluation_rows': retrieval['rows'],
            'current_table_replays_unchanged_rankings': retrieval['replayed_method_rankings'],
            'pool': 'same_frozen_family_scope_for_each_task_across_all_methods',
            'independent_holdout': False, 'post_measurement_adapter_correction': True,
            'metrics_are_mechanical_against_provisional_researcher_annotations': True,
            'model_tokens': None, 'tokenizer': None,
            'summaries': [{**{k: r[k] for k in ['split','method','budget_id','family_count','task_count']},
                'metrics': {k: r['metrics'][k] for k in policy['retrieval_metrics']}}
                for r in retrieval['summaries']],
            'timing_note': retrieval['timing_note'],
        },
        'representations': {
            'comparisons': representations['comparisons'], 'families': representations['families'],
            'summary': [{k: r[k] for k in policy['representation_fields']} for r in representations['results']],
            'limits': representations['limits'],
        },
        'recovery': {
            'tests': recovery['tests_run'], 'new_tests': recovery['new_recovery_tests'],
            'test_data': recovery['test_data'], 'real_provider_calls': recovery['real_provider_calls'],
            'owner_attempts_recovered': 0, 'limits': recovery['limits'],
        },
        'candidate_collections': [{k: i[k] for k in ['path','sha256']} for i in policy['inputs'] if i['role'] == 'candidate_presets'],
        'runtime_boundary': {
            'json_schema_and_python_codec': 'checked_for_this_public_projection',
            'native_method_registry': 'not_verified_known_integration_gap',
            'finding': 'A3-DISC-001', 'native_execution': False,
            'description': 'GraphPacketStore acceptance is not MethodRegistry acceptance or execution; equivalent DTO key/order/numeric handling remains an A/B integration gap.',
        },
    }
    presentation['evidence'] = [r for r in presentation['evidence'] if r['visibility'] == 'private_bytes_public_digest']
    for row in presentation['evidence']:
        row['role'] = policy['inherited_evidence_role_overrides'].get(row['role'], row['role'])
    stage_start = read(BASE / 'START.json')
    presentation['evidence'].append({'role': 'immediate_previous_checkpoint',
        'sha256': stage_start['previous_checkpoint']['sha256'], 'visibility': 'private_bytes_public_digest'})
    presentation['evidence'] += [{'role': i['role'], 'sha256': i['sha256'], 'visibility': 'public_aggregate_receipt'} for i in policy['inputs']]
    for row in presentation['pipeline']:
        row['status'] = policy['pipeline'][row['stage']]
    contract['description'] = 'Versioned public result snapshot extending continuation_01 v2; offline measurements and unmeasured LLM quality are distinct. Not a shared runtime or profile-engine schema.'
    for key in ['schema','version','validation_status']:
        contract['properties'][key] = {'const': presentation[key]}
    contract['properties']['basic'] = {'const': presentation['basic']}
    ep = contract['properties']['expert']
    for key in ['preparation_details','offline']:
        ep['properties'][key] = {'const': expert[key]}
        if key not in ep['required']:
            ep['required'].append(key)
    ep['properties']['expanded_panel']['properties']['feature_semantics'] = {'const': expert['expanded_panel']['feature_semantics']}
    Draft202012Validator.check_schema(contract)
    Draft202012Validator(contract).validate(presentation)
    projection = read(PREVIOUS / 'handoff-projection.json')
    projection['namespace'] = 'thread7-continuation02-public-offline-results'
    projection['method']['label'] = 'Offline source retrieval and preservation result projection'
    projection['projections']['records'][1]['id'] = 'offline_measured_and_model_unmeasured_results'
    # Existing evaluation protocol continues to define the null model metrics;
    # the offline protocols are separately pinned in the evidence and policy.
    artifact = analysis.build_handoff_artifact(presentation, read(PREVIOUS / 'evaluation-protocol.json'),
                                               projection, projected_at=policy['projected_at'])
    captured = graph.strict_json(graph.recover_files(artifact)['results'])
    if captured['records'] != [presentation]:
        raise ValueError('result_roundtrip_mismatch')
    return presentation, contract, artifact


@analysis.public_error_boundary
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=BASE)
    args = parser.parse_args()
    policy = read(BASE / 'delivery-policy.json')
    presentation, contract, artifact = build(policy)
    outputs = {'delivery-example-v3.json': graph.canonical(presentation)+b'\n',
               'delivery-contract-v3.json': graph.canonical(contract)+b'\n',
               'delivery-artifact-v3.json.gz': gzip.compress(graph.canonical(artifact)+b'\n', mtime=0)}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, raw in outputs.items():
        with (args.output/name).open('xb') as stream:
            stream.write(raw)
    receipt = {'schema':'loom.thread7_final_contract_receipt/1',
               'contracts':['loom.method_graph/1','loom.method_run_trace/1'],
               'schema_and_python_codec_verified':True, 'source_and_result_bytes_recoverable':True,
               'native_execution':False, 'native_method_registry_compatible':None,
               'known_native_integration_gap':'A3-DISC-001', 'new_calls':0, 'new_cost_usd':'0',
               'policy_sha256':digest((BASE/'delivery-policy.json').read_bytes()),
               'builder_sha256':digest(Path(__file__).read_bytes()),
               'outputs':{name:digest(raw) for name,raw in outputs.items()}}
    with (args.output/'delivery-receipt.json').open('xb') as stream:
        stream.write(graph.canonical(receipt)+b'\n')
    print(json.dumps(receipt, sort_keys=True))


if __name__ == '__main__':
    main()
