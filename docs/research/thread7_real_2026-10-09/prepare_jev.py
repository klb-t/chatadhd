"""Reuse historical Jev question recipes with exact corrected real source views."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import jev_live_pilot as jev
from loom.tools.structure import research_programme_manifest as manifests
from loom.tools.structure import experiment_workflow_v1 as workflow
from prepare_workflow import read, write


def prepare(checkpoint, output, plan):
    checkpoint, output = Path(checkpoint), Path(output).resolve()
    workflow.require(not output.exists(), 'new_output_required')
    workflow.require(not any((p/'.git').exists() for p in (output,*output.parents)), 'private_output_inside_git')
    source_path=checkpoint/'prepared/prepared/manifest.json'
    source=manifests.load_manifest(source_path)
    views={}
    for op in source['operations']:
        qid=op['metadata']['query_id']
        body=read(source_path.parent/op['request_file'])
        text=body['messages'][1]['content']
        workflow.require(qid not in views or views[qid]==text, 'source_view_inconsistent')
        views[qid]=text
    output.mkdir(mode=0o700)
    legacy_paths=[];recipes=[];rejected=[]
    for arm in plan['arms']:
        path=ROOT/arm['legacy_manifest']; raw=path.read_bytes(); legacy=read(path)
        workflow.require(hashlib.sha256(raw).hexdigest()==arm['legacy_manifest_sha256'], 'recipe_hash_mismatch')
        body=legacy['requests'][0]['body']
        questions=body['questions']
        workflow.require(all(r['body']['questions']==questions for r in legacy['requests']), 'recipe_questions_vary')
        requests=[]
        for qid,text in sorted(views.items()):
            request={**{k:deepcopy(v) for k,v in body.items() if k!='state'}, 'state':{'text':text}}
            write(output/'desired-requests'/arm['id']/(hashlib.sha256(qid.encode()).hexdigest()+'.json'),request)
            try:
                jev.validate_body(request)
            except jev.safe.RunnerError as e:
                rejected.append({'arm':arm['id'],'case_id':qid,'code':str(e)});continue
            requests.append({'id':qid,'body':request,'reservation_usd':legacy['requests'][0]['reservation_usd']})
        prepared={'schema':legacy['schema'],'experiment_id':arm['stage_identity'],
                  'budget_usd':plan['nonrenewing_cap_usd'],'max_requests':len(requests),'requests':requests,
                  'metadata':{'arm_id':arm['id'],'source_manifest_sha256':hashlib.sha256(source_path.read_bytes()).hexdigest(),
                              'scope':'selected_visible_real_development_sources','reference_status':'provisional',
                              'dispatch_ready':False,'current_pricing':None}}
        target=output/arm['id']/'manifest.json';write(target,prepared);legacy_paths.append(target)
        recipes.append({'arm':arm['id'],'question_recipe_sha256':workflow.digest(questions),
                        'legacy_manifest_sha256':arm['legacy_manifest_sha256'],'planned':len(views),'prepared':len(requests)})
    paid=manifests.adapt_prepared_manifests(legacy_paths,output/'payer',programme_id=source['programme_id'],
                    stage_id=plan['stage_id'])
    write(output/'PRIVATE_REJECTIONS.json',rejected)
    receipt={'schema':'loom.thread7_real_jev_preparation/1','recipes':recipes,'queries':len(views),
             'operations':len(paid['operations']),'desired_operations':len(views)*len(plan['arms']),
             'mechanically_blocked_operations':len(rejected),
             'missing_slot_policy':'Retain missing desired slots; admissible subset is not the full comparison.',
             'new_paid_calls':0,'dispatch_ready':False,'current_cost_upper_usd':None,
             'source_view_semantics':'Exact corrected upstream real-panel state; no scope renaming or flattening.',
             'source_manifest_sha256':hashlib.sha256(source_path.read_bytes()).hexdigest(),
             'payer_manifest_sha256':hashlib.sha256((output/'payer/manifest.json').read_bytes()).hexdigest(),
             'reference_status':'assistant_authored_provisional','quality':None}
    write(output/'PREPARATION_RECEIPT.json',receipt)
    write(output/'FREEZE.json',{'files':{str(p.relative_to(output)):hashlib.sha256(p.read_bytes()).hexdigest()
                                      for p in sorted(output.rglob('*')) if p.is_file()},'before_evaluation':True})
    return receipt


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--checkpoint',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path);p.add_argument('--plan',type=Path,default=Path(__file__).with_name('jev-plan.json'))
    a=p.parse_args();print(json.dumps(prepare(a.checkpoint,a.output,read(a.plan)),indent=2))
