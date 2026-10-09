"""Private workflow projection of the recovered real panel, no model dispatch."""
from __future__ import annotations
import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import experiment_workflow_v1 as workflow
from loom.tools.structure import research_programme_manifest as manifests


def read(path):
    return workflow.strict_json(Path(path).read_bytes())


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with path.open('xb') as f:
        f.write(workflow.canonical(value) + b'\n')
    path.chmod(0o600)


def prepare(checkpoint, output, plan):
    checkpoint, output = Path(checkpoint), Path(output).resolve()
    workflow.require(not output.exists(), 'new_private_output_required')
    workflow.require(not any((p / '.git').exists() for p in (output, *output.parents)), 'private_output_inside_git')
    for base, filename in ((checkpoint, 'CHECKPOINT_MANIFEST.json'), (checkpoint/'prepared', 'FREEZE.json')):
        for name, expected in read(base/filename)['files'].items():
            path = (base/name).resolve()
            workflow.require(path.is_relative_to(base.resolve()), 'unsafe_frozen_path')
            workflow.require(hashlib.sha256(path.read_bytes()).hexdigest() == expected, 'frozen_file_mismatch')
    old = read(checkpoint/'prepared/prepared/manifest.json')
    inputs = read(checkpoint/'prepared/inputs.json')['cases']
    configs = read(checkpoint/'prepared/prepared/configurations.json')
    output.mkdir(mode=0o700)
    producers = {'workflow': Path(workflow.__file__), 'preparation': Path(__file__),
                 'manifest_validator': Path(manifests.__file__)}
    producer_hashes = {}
    for name, path in producers.items():
        raw = path.read_bytes(); target = output/'producer-sources'/(name+'.py')
        target.parent.mkdir(exist_ok=True, mode=0o700); target.write_bytes(raw); target.chmod(0o600)
        producer_hashes[name] = hashlib.sha256(raw).hexdigest()
    write(output/'producer-versions.json', producer_hashes)
    qfamilies = {q['id']: c['source_id'] for c in inputs for q in c['judgment_queries']}
    sources = {q['id']: workflow.digest(c['turns']) for c in inputs for q in c['judgment_queries']}
    bodies, original = {}, {}
    for op in old['operations']:
        qid, cid = op['metadata']['query_id'], op['metadata']['configuration_id']
        raw = (checkpoint/'prepared/prepared'/op['request_file']).read_bytes()
        workflow.require(hashlib.sha256(raw).hexdigest() == op['request_sha256'], 'request_hash_mismatch')
        bodies.setdefault(qid, {})[cid] = workflow.strict_json(raw)
        original[qid, cid] = op
    receipts = []
    for panel in plan['panels']:
        chosen = []
        for selector in panel['selectors']:
            matches = [c for c in configs if all(c.get(k) == v for k, v in selector.items())]
            workflow.require(len(matches) == 1, 'configuration_selection_ambiguous')
            chosen.append(matches[0]['configuration_id'])
        spec = deepcopy(plan['spec_common'])
        spec.update(id=panel['id'], campaign_id=old['programme_id'],
                    variants={'mode': 'list', 'values': [{'configuration_id': c} for c in chosen]},
                    scope=[{'id': q, 'family': qfamilies[q], 'source_sha256': sources[q],
                            'split': 'development', 'input': bodies[q]} for q in sorted(bodies)])
        if 'axes' in panel:
            spec['variants'] = {'mode': 'matrix', 'axes': panel['axes']}
            fields = [a['name'] for a in panel['axes']]
            spec['request_template'] = {'$match': {'table': 'case', 'fields': fields, 'value_field': 'request'}}
            for case in spec['scope']:
                case['input'] = [{**{k:c[k] for k in fields}, 'request': bodies[case['id']][c['configuration_id']]}
                                 for c in configs if c['configuration_id'] in chosen]
        directory = output/panel['id']; directory.mkdir(mode=0o700)
        write(directory/'spec.json', spec)
        queue = workflow.Queue(directory/'queue.sqlite')
        queue.add(workflow.ordered_jobs(spec))
        snapshot = queue.snapshot(); queue.db.close()
        # Preserve scientific identity and exact original request bytes in the
        # existing payer format; the queue adds no alternate billing mechanism.
        operations = []
        for slot in snapshot:
            row = slot['job']
            if 'configuration_id' in row['variant']:
                cid = row['variant']['configuration_id']
            else:
                matches = [c for c in configs if all(c[k] == v for k,v in row['variant'].items())]
                workflow.require(len(matches) == 1, 'configuration_selection_ambiguous')
                cid = matches[0]['configuration_id']
            source = original[row['case_id'], cid]
            raw = workflow.canonical(row['request'])
            workflow.require(hashlib.sha256(raw).hexdigest() == source['request_sha256'], 'render_changed_prompt')
            op = deepcopy(source)
            op['operation_id'] = row['operation_id']
            op['metadata'].update(source_operation_id=source['operation_id'],
                                  workflow_spec_sha256=workflow.digest(spec),
                                  repetition_index=row['repetition'], queue_ordinal=row['queue_ordinal'])
            target = directory/'payer'/op['request_file']; target.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
            if not target.exists():
                target.write_bytes(raw); target.chmod(0o600)
            operations.append(op)
        manifest = {'schema': old['schema'], 'programme_id': old['programme_id'],
                    'stage_id': panel['id'], 'operations': operations,
                    'metadata': {'source_manifest_sha256': hashlib.sha256((checkpoint/'prepared/prepared/manifest.json').read_bytes()).hexdigest(),
                                 'spec_sha256': workflow.digest(spec), 'dispatch_ready': False,
                                 'response_cache_disabled': True, 'no_paid_calls': True}}
        write(directory/'payer/manifest.json', manifest)
        manifests.validate_manifest(manifest, base_dir=directory/'payer')
        write(directory/'queue-snapshot.json', snapshot)
        write(directory/'summary.json', workflow.summary(spec, snapshot))
        freeze = {'files': {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in sorted(directory.rglob('*')) if p.is_file()},
                  'quality': None, 'reference_status': 'provisional_assistant_authored',
                  'before_evaluation': True, 'current_budget': None, 'new_paid_calls': 0}
        freeze['producer_sha256'] = producer_hashes
        write(directory/'FREEZE.json', freeze)
        receipts.append({'panel': panel['id'], 'conversations': len(set(qfamilies.values())),
                         'queries': len(bodies), 'configurations': len(chosen),
                         'operations': len(operations), 'spec_sha256': workflow.digest(spec),
                         'manifest_sha256': hashlib.sha256((directory/'payer/manifest.json').read_bytes()).hexdigest(),
                         'freeze_sha256': hashlib.sha256((directory/'FREEZE.json').read_bytes()).hexdigest(),
                         'request_bytes_unchanged': True, 'current_cost_upper_bound_usd': None,
                         'producer_sha256': producer_hashes,
                         'dispatch_ready': False, 'new_paid_calls': 0})
    write(output/'PREPARATION_RECEIPT.json', receipts)
    return receipts


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--plan',type=Path,default=Path(__file__).with_name('workflow-plan.json'))
    args=p.parse_args()
    print(json.dumps(prepare(args.checkpoint,args.output,read(args.plan)),indent=2))
