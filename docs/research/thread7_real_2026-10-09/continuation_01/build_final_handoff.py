"""Version the completed public handoff from allowlisted aggregate receipts."""
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT))
from jsonschema import Draft202012Validator
from loom.tools.seeding import method_graph as graph
from loom.tools.structure import experiment_analysis_v1 as analysis


@analysis.public_error_boundary
def main():
    base=Path(__file__).parent
    def read(name):
        value=graph.strict_json((base/name).read_bytes())
        return analysis.approve_public_input('continuation01/'+name,value)
    presentation=read('handoff-example.json')
    contract=read('handoff-contract.json')
    context=read('context-expanded-v2-receipt.json')
    queues=read('connection-receipt.json')
    presentation['schema']='loom.thread7_result_presentation/2'
    presentation['version']=2
    presentation['basic']={
        'summary_pl':'Przygotowano 12 rodzin OpenAI i Anthropic, 2079 wiadomości (23.01–09.09.2026). Gotowych jest 675 requestów; małe alternatywy mają 6 lub 10 wywołań. Koszt nowego etapu: 0 USD. Jakość modeli nie została jeszcze zmierzona.',
        'details_ref':'docs/research/thread7_real_2026-10-09/continuation_01/HANDOFF_FINAL_B.md'}
    presentation['expert']['preparation_count']=context['unique_prepared_request_bodies']
    presentation['expert']['preparation_details']={
        'variant_intentions':context['slots'], 'fully_prepared':context['slot_status_counts']['prepared'],
        'primary_ready_extraction_pending':context['slot_status_counts']['primary_prepared_dependency_pending'],
        'source_preference_annotations_pending':context['slot_status_counts']['pending'],
        'small_scope_operations':[r['queued_operations'] for r in queues['scopes']],
        'small_scopes_are_alternatives':True, 'live_dispatch_allowed':False}
    presentation['expert']['limitations'] += ['unresolved_native_parent_references_6',
        'selected_attachment_references_unresolved_75', 'source_preferences_verified_in_one_family_only',
        'prepared_body_is_not_provider_admission', 'reserved_incomplete_journal_needs_external_resolution']
    for name, role in [('context-expanded-v2-receipt.json','context_preparation'),
                       ('connection-receipt.json','queue_payer_preparation'),
                       ('FINAL_TESTS.json','mechanical_verification')]:
        presentation['evidence'].append({'role':role,'sha256':hashlib.sha256((base/name).read_bytes()).hexdigest(),
                                         'visibility':'public_aggregate_receipt'})
    statuses={'sources':'separate_frozen_historical_and_expanded','views':'prepared_with_explicit_loss',
              'spec':'small_scopes_frozen','variants':'prepared_or_dependency_pending','queue':'prepared_no_dispatch',
              'payer':'verified_boundary_live_preflight_pending','results':'no_new_responses',
              'evaluations':'source_bound_protocol_missing_metrics','candidate_presets':'unvalidated_no_adoption'}
    for row in presentation['pipeline']:row['status']=statuses[row['stage']]
    contract['properties']['schema']['const']=presentation['schema']
    contract['properties']['version']['const']=2
    contract['properties']['basic']['properties']['summary_pl']['const']=presentation['basic']['summary_pl']
    contract['properties']['expert']['properties']['preparation_details']={
        'const':presentation['expert']['preparation_details']}
    contract['properties']['expert']['required'].append('preparation_details')
    contract['properties']['evidence']['items']['properties']['visibility']={'enum':['private_bytes_public_digest','public_aggregate_receipt']}
    Draft202012Validator(contract).validate(presentation)
    artifact=analysis.build_handoff_artifact(presentation,read('evaluation-protocol.json'),
        graph.load_projection(base/'handoff-projection.json'),projected_at=datetime.now(timezone.utc).isoformat())
    for name,value in [('handoff-final-example-v2.json',presentation),('handoff-final-contract-v2.json',contract)]:
        with (base/name).open('xb') as stream:stream.write(graph.canonical(value)+b'\n')
    raw=graph.canonical(artifact)+b'\n';compressed=gzip.compress(raw,mtime=0)
    with (base/'handoff-final-artifact-v3.json.gz').open('xb') as stream:stream.write(compressed)
    receipt={'schema':'loom.thread7_final_contract_receipt/1','contracts':['loom.method_graph/1','loom.method_run_trace/1'],
             'schema_and_python_codec_verified':True,'source_and_result_bytes_recoverable':True,
             'artifact_sha256':hashlib.sha256(raw).hexdigest(),'compressed_sha256':hashlib.sha256(compressed).hexdigest(),
             'presentation_sha256':hashlib.sha256(graph.canonical(presentation)+b'\n').hexdigest(),
             'native_execution':False,'new_calls':0,'new_cost_usd':'0'}
    with (base/'handoff-final-verification.json').open('x') as stream:json.dump(receipt,stream,indent=2);stream.write('\n')
    print(json.dumps(receipt))


if __name__=='__main__':main()
