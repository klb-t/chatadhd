"""Analyze missing outcomes of an exact frozen real preparation, without dispatch."""
import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from loom.tools.seeding.method_graph import canonical, strict_json
from loom.tools.structure import experiment_analysis_v1 as analysis


def save(path, value):
    with Path(path).open('xb') as stream:
        stream.write(canonical(value)+b'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--context-dir', type=Path, required=True)
    parser.add_argument('--expected-manifest-sha256', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--public-receipt', type=Path, required=True)
    args = parser.parse_args()
    base = Path(__file__).parent
    root = args.context_dir.resolve()
    raw = (root/'MANIFEST.json').read_bytes()
    analysis.require(hashlib.sha256(raw).hexdigest() == args.expected_manifest_sha256, 'context_manifest_mismatch')
    manifest = strict_json(raw)
    for name in ('matrix.jsonl', 'jobs-index.jsonl'):
        analysis.require(hashlib.sha256((root/name).read_bytes()).hexdigest() == manifest['files'][name], 'context_index_hash_mismatch')
    rows = [strict_json(line) for line in (root/'matrix.jsonl').read_bytes().splitlines()]
    jobs = [strict_json(line) for line in (root/'jobs-index.jsonl').read_bytes().splitlines()]
    out = args.output_dir.resolve()
    analysis.require(not out.exists() and not any((p/'.git').exists() for p in (out,*out.parents)), 'new_private_analysis_directory_required')
    out.mkdir(parents=True, mode=0o700)
    protocol = strict_json((base/'evaluation-protocol.json').read_bytes())
    planned = []
    for row in rows:
        first = next((call for call in row['calls'] if call['kind'] == 'answer'), None)
        request_hash = None if first is None else first['request_sha256']
        planned.append({'operation_id':row['id'], 'variant_id':analysis.digest(row['variant']),
            'family':row['family_id'], 'question_id':row['task_id'], 'repetition':0,
            'panel_id':'expanded-real-v2', 'split':row['split'],
            'source_sha256':row['identity']['source_sha256'],
            'view_sha256':row['view']['output_sha256'], 'request_sha256':request_hash,
            'preparation_status':'pending' if request_hash is None else 'prepared',
            'measurement_kind':'new_real_export_inference',
            'request_hash_scope':'prepared_primary_request_only_not_complete_multicall_outcome'})
    # No fabricated evaluation rows: absence is represented by an empty capture list.
    summary = analysis.measurement_summary(planned, [])
    selected = {r['variant']['response_form']:analysis.digest(r['variant']) for r in rows
                if r['variant']['context_representation']=='exact_text' and
                   r['variant']['context_resolution']=='full' and r['variant']['preference_mode']=='none'}
    comparisons = []
    for right in ('text_plus_structure','text_then_structure'):
        comparisons.append(analysis.paired_comparison(planned, [], selected['graph_direct'],selected[right],
            'source_fidelity','assistant',protocol,measurement_kinds=('new_real_export_inference','composed_real_export_outcome')))
    save(out/'planned-variant-intentions.json', planned)
    save(out/'records.json', [])
    save(out/'paired-missing.json', comparisons)
    save(out/'summary.json', summary)
    for path in out.iterdir(): path.chmod(0o600)
    receipt = {'schema':'loom.thread7_unmeasured_analysis_receipt/1',
        'context_manifest_sha256':args.expected_manifest_sha256,
        'context_matrix_sha256':manifest['files']['matrix.jsonl'],
        'context_jobs_index_sha256':manifest['files']['jobs-index.jsonl'],
        'protocol_sha256':analysis.digest(protocol),
        'source_population':'separately_frozen_12_family_expansion_not_historical_panel',
        'planned_variant_intentions':len(planned),'dependent_requests_or_pending_intentions':len(jobs),
        'preparation_statuses':dict(Counter(r['status'] for r in rows)),
        'family_counts_by_split':{split:len({r['family'] for r in planned if r['split']==split}) for split in ('development','validation')},
        'validation_independence':False, 'reference_status':'provisional_assistant_authored',
        'summary':summary, 'paired_missing_comparisons':comparisons,
        'quality':None,'actual_new_model_calls':0,'actual_new_model_cost_usd':'0',
        'status':'missing_results_reported_not_quality_estimated',
        'private_files':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in out.iterdir()},
        'producer_file_sha256':hashlib.sha256(Path(analysis.__file__).read_bytes()).hexdigest()}
    save(out/'MANIFEST.json', receipt); (out/'MANIFEST.json').chmod(0o600)
    save(args.public_receipt, receipt)
    print(json.dumps({'planned_variant_intentions':len(planned),'captured_results':0,'quality':None,'cost_usd':'0'}))


if __name__ == '__main__':
    main()
