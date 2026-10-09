"""Connect frozen real context preparations to the existing queue/payer format.

No dispatch, response invention, current price claim or reservation occurs here.
Only ready single-call preparations enter a small chosen scope. The full matrix
and dependencies remain in the original immutable preparation archive.
"""
from copy import deepcopy
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import experiment_workflow_v1 as workflow
from loom.tools.structure import research_programme_manifest as payer_manifest

PRODUCER_VERSION = 'loom.thread7_prepared_connector/2'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def read(path):
    return workflow.strict_json(Path(path).read_bytes())


def write(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with path.open('xb') as stream:
        stream.write(raw)
    path.chmod(0o600)


def dump(path, value):
    write(path, workflow.canonical(value) + b'\n')


def source_binding(source, row, prepared_sources, corpus=None):
    """Bind distinct digest domains without treating a family ID as a version.

    prepare_release indexes the canonical context-source object, whereas the
    corpus panel identifies canonical native-conversation JSON, not ZIP/member
    bytes. Older direct-hash inputs have no context-source table and must match
    the declared source hash exactly; they do not prove source content.
    """
    if prepared_sources is None:
        workflow.require(row['source_sha256'] == source['raw_sha256'],
                         'preparation_source_version_mismatch')
        return {'source_sha256': source['raw_sha256'],
                'preparation_source_sha256': row['source_sha256'],
                'preparation_source_hash_domain': 'legacy_declared_source_hash',
                'source_hash_domain': 'legacy_declared_source_hash',
                'source_content_verified': False}
    matches = [item for item in prepared_sources
               if item['family_id'] == source['family_id'] and workflow.digest(item) == row['source_sha256']]
    workflow.require(len(matches) == 1, 'preparation_source_version_mismatch')
    workflow.require(matches[0]['source_sha256'] == source['raw_sha256'],
                     'preparation_original_source_version_mismatch')
    workflow.require('native_conversation' in matches[0] and
                     workflow.digest(matches[0]['native_conversation']) == source['raw_sha256'],
                     'preparation_native_source_content_mismatch')
    workflow.require(corpus is not None and 'path' in source and 'normalized_sha256' in source,
                     'corpus_source_content_binding_required')
    source_path = (Path(corpus) / source['path']).resolve()
    workflow.require(source_path.is_relative_to(Path(corpus).resolve()), 'corpus_source_path_escape')
    normalized_raw = source_path.read_bytes()
    workflow.require(sha(normalized_raw) == source['normalized_sha256'], 'corpus_source_version_mismatch')
    normalized = workflow.strict_json(normalized_raw)
    workflow.require(normalized['family_id'] == source['family_id'] and
                     normalized['raw_sha256'] == source['raw_sha256'] and
                     workflow.digest(normalized['native_conversation']) == source['raw_sha256'] and
                     normalized['native_conversation'] == matches[0]['native_conversation'],
                     'corpus_native_source_content_mismatch')
    return {'source_sha256': source['raw_sha256'],
            'preparation_source_sha256': row['source_sha256'],
            'preparation_source_hash_domain': 'canonical_context_source',
            'source_hash_domain': 'canonical_native_conversation',
            'normalized_source_sha256': source['normalized_sha256'],
            'source_content_verified': True}


def connect(prepared, corpus, output, plan):
    prepared, corpus, output = map(lambda p: Path(p).resolve(), (prepared, corpus, output))
    workflow.require(not output.exists(), 'new_private_output_required')
    workflow.require(not any((p / '.git').exists() for p in (output, *output.parents)), 'private_output_inside_git')
    raw_manifest = (prepared / 'MANIFEST.json').read_bytes()
    workflow.require(sha(raw_manifest) == plan['preparation_manifest_sha256'], 'preparation_version_mismatch')
    frozen = read(prepared / 'MANIFEST.json')
    for name, record in frozen['files'].items():
        path = (prepared / name).resolve()
        workflow.require(path.is_relative_to(prepared), 'frozen_path_escape')
        expected = record['sha256'] if isinstance(record, dict) else record
        workflow.require(sha(path.read_bytes()) == expected, 'frozen_payload_changed')
    workflow.require('jobs-index.jsonl' in frozen['files'], 'preparation_index_not_frozen')
    workflow.require(not (prepared / 'sources.json').exists() or 'sources.json' in frozen['files'],
                     'preparation_sources_not_frozen')
    prepared_sources = read(prepared / 'sources.json') if 'sources.json' in frozen['files'] else None
    corpus_raw = (corpus / 'panel.json').read_bytes()
    workflow.require(sha(corpus_raw) == plan['corpus_panel_sha256'], 'corpus_version_mismatch')
    sources = read(corpus / 'panel.json')['sources']
    chosen = []
    for provider in plan['provider_order']:
        candidates = [s for s in sources if s['provider'] == provider and s['split'] == 'tuning']
        workflow.require(bool(candidates), 'tuning_provider_unavailable')
        chosen.append(min(candidates, key=lambda s: (s['features']['text_chars'], s['raw_sha256'])))
    rows = [workflow.strict_json(line) for line in (prepared / 'jobs-index.jsonl').read_bytes().splitlines()]
    output.mkdir(mode=0o700)
    receipts = []
    for scope in plan['scopes']:
        cases, originals, provenance, source_versions = [], {}, {}, {}
        for source in chosen:
            matches = [r for r in rows if r['family_id'] == source['family_id'] and r['phase'] == 'answer']
            case_inputs = []
            for variant in scope['variants']:
                workflow.require(variant['response_form'] in plan['single_call_response_forms'],
                                 'multi_call_variant_requires_dependency_executor')
                matching = [r for r in matches if r['variant'] == variant]
                workflow.require(len(matching) == 1, 'preparation_selection_ambiguous')
                row = matching[0]
                binding = source_binding(source, row, prepared_sources, corpus)
                workflow.require(row['body_ready'] is True and row['status'] == 'prepared', 'pending_preparation_not_dispatchable')
                workflow.require(row['body_path'] in frozen['files'], 'prepared_body_not_frozen')
                raw = (prepared / row['body_path']).read_bytes()
                workflow.require(sha(raw) == row['body_sha256'], 'prepared_body_changed')
                body = workflow.strict_json(raw)
                workflow.require(workflow.canonical(body) == raw, 'queue_renderer_changes_exact_bytes')
                case_inputs.append({**variant, 'request': body})
                originals[source['family_id'], workflow.digest(variant)] = raw
                provenance[source['family_id'], workflow.digest(variant)] = row
                source_versions[source['family_id'], workflow.digest(variant)] = binding
            cases.append({'id': source['family_id'], 'family': source['family_id'],
                          'source_sha256': source['raw_sha256'], 'split': 'development', 'input': case_inputs})
        spec = deepcopy(plan['spec_common'])
        spec.update(id=scope['id'], campaign_id=plan['campaign_id'], scope=cases,
                    preparation_connector=PRODUCER_VERSION,
                    variants={'mode': 'list', 'values': scope['variants']},
                    request_template={'$match': {'table': 'case', 'fields': sorted(scope['variants'][0]), 'value_field': 'request'}})
        target = output / scope['id']
        dump(target / 'spec.json', spec)
        queue = workflow.Queue(target / 'queue.sqlite')
        count = queue.add(workflow.ordered_jobs(spec))
        snapshot = queue.snapshot()
        queue.db.close()
        operations, bindings = [], []
        # Queue.snapshot is an identity-sorted inspection view. The existing
        # payer runs manifest order, so preserve the frozen scheduler ordinal.
        for item in sorted(snapshot, key=lambda row: row['job']['queue_ordinal']):
            job = item['job']
            raw = workflow.canonical(job['request'])
            origin = provenance[job['family'], workflow.digest(job['variant'])]
            source_version = source_versions[job['family'], workflow.digest(job['variant'])]
            workflow.require(raw == originals[job['family'], workflow.digest(job['variant'])], 'queue_prompt_changed')
            requested = job['request']['provider']
            workflow.require(requested.get('only') == [plan['requested_provider_id']] and requested.get('allow_fallbacks') is False,
                             'payer_provider_must_be_explicit_in_frozen_request')
            filename = 'requests/' + sha(raw) + '.json'
            request_path = target / 'payer' / filename
            if request_path.exists():
                workflow.require(request_path.read_bytes() == raw, 'request_digest_collision')
            else:
                write(request_path, raw)
            operations.append({'operation_id': job['operation_id'], 'route_id': spec['route_id'],
                               'model_id': job['request']['model'], 'provider_id': plan['requested_provider_id'],
                               'request_file': filename, 'request_sha256': sha(raw),
                               'units_upper_bounds': {'prompt': len(raw) + plan['prompt_units_fixed_allowance'],
                                                      'completion': job['request']['max_tokens'], 'request': 1},
                               'metadata': {'source_preparation_id': origin['preparation_id'],
                                            'source_request_sha256': origin['body_sha256'],
                                            'workflow_spec_sha256': workflow.digest(spec),
                                            'queue_ordinal': job['queue_ordinal'], 'phase': 'answer'}})
            bindings.append({'operation_id': job['operation_id'], 'preparation_id': origin['preparation_id'],
                             'family': job['family'], 'question_id': origin['task_id'],
                             'variant_id': workflow.digest(job['variant']), 'variant': job['variant'],
                             **source_version,
                             'view_sha256': workflow.digest(workflow.strict_json(job['request']['messages'][1]['content'])['context']),
                             'request_sha256': sha(raw), 'split': 'development', 'repetition': job['repetition'],
                             'panel_id': scope['id'], 'response_status': 'not_executed', 'quality': None})
        manifest = {'schema': payer_manifest.SCHEMA, 'programme_id': plan['campaign_id'],
                    'stage_id': scope['id'], 'operations': operations,
                    'metadata': {'preparation_manifest_sha256': sha(raw_manifest), 'spec_sha256': workflow.digest(spec),
                                 'preparation_connector': PRODUCER_VERSION,
                                 'dispatch_ready': False, 'live_price_upper_usd': None,
                                 'requires_fresh_existing_payer_preflight': True, 'reservation_booked': False}}
        payer_manifest.validate_manifest(manifest, base_dir=target / 'payer')
        dump(target / 'payer/manifest.json', manifest)
        dump(target / 'queue-snapshot.json', snapshot)
        dump(target / 'analysis-bindings.json', bindings)
        dump(target / 'FREEZE.json', {'files': {str(p.relative_to(target)): sha(p.read_bytes()) for p in sorted(target.rglob('*')) if p.is_file()},
                                     'producer_version': PRODUCER_VERSION,
                                     'producer_sha256': sha(Path(__file__).read_bytes()), 'workflow_sha256': sha(Path(workflow.__file__).read_bytes()),
                                     'new_calls': 0, 'quality': None})
        receipts.append({'scope': scope['id'], 'families': len(cases), 'variants': len(scope['variants']), 'queued_operations': count,
                         'producer_version': PRODUCER_VERSION,
                         'payer_manifest_sha256': sha((target/'payer/manifest.json').read_bytes()),
                         'spec_sha256': workflow.digest(spec), 'freeze_sha256': sha((target/'FREEZE.json').read_bytes()),
                         'body_bytes_preserved': True, 'new_calls': 0, 'new_cost_usd': '0',
                         'current_phase_upper_usd': None, 'dispatch_ready': False, 'quality': None})
    dump(output / 'PLAN.json', plan)
    dump(output / 'RECEIPT.json', receipts)
    return receipts


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', required=True)
    parser.add_argument('--corpus', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--plan', required=True)
    args = parser.parse_args()
    print(json.dumps(connect(args.prepared, args.corpus, args.output, read(args.plan)), sort_keys=True))
