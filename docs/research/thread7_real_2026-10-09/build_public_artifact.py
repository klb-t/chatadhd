"""Project allowlisted preparation receipts using the existing method contract."""
from copy import deepcopy
from datetime import datetime, timezone
import gzip
import json
from pathlib import Path
import tempfile
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from jsonschema import Draft202012Validator
from loom.tools.seeding import method_graph as graph
from loom.tools.structure import experiment_analysis_v1 as analysis


@analysis.public_error_boundary
def build(base):
    base=Path(base)
    profile=graph.load_projection(base/'graph-projection.json')
    analysis.approve_public_input('workflow/graph-projection.json', profile)
    analysis.approve_public_input('workflow/projection-bytes', profile.source_bytes)
    receipts=graph.strict_json((base/'workflow-preparation-receipt.json').read_bytes())
    analysis.approve_public_input('workflow/workflow-preparation-receipt.json', receipts)
    # This input is an explicit public receipt projection, never raw requests.
    allowed={'panel','conversations','queries','configurations','operations','spec_sha256',
             'manifest_sha256','freeze_sha256','request_bytes_unchanged','current_cost_upper_bound_usd',
             'dispatch_ready','new_paid_calls','producer_sha256'}
    if any(set(row)-allowed for row in receipts):raise ValueError('nonpublic_receipt_field')
    with tempfile.TemporaryDirectory() as temp:
        d=Path(temp)
        originals={'policy_frozen.json':base/'workflow-plan.json',
                   'prototype_frozen.py':ROOT/'loom/tools/structure/experiment_workflow_v1.py',
                   'protocol_frozen.md':base/'HANDOFF_B.md'}
        hashes={}
        for n,p in originals.items():
            raw=p.read_bytes()
            role='workflow/program' if n=='prototype_frozen.py' else 'workflow/'+p.name
            analysis.approve_public_input(role,raw)
            (d/n).write_bytes(raw)
        for role,name in [('policy','policy_frozen.json'),('code','prototype_frozen.py'),('protocol','protocol_frozen.md')]:
            hashes[role]=graph.sha256((d/name).read_bytes())
        when=datetime.now(timezone.utc).isoformat()
        manifest={'predicted_at':when,'hashes':hashes,'execution_status':'offline_preparation_no_model_execution'}
        (d/'predictions.json').write_bytes(graph.canonical({'manifest':manifest,'panels':receipts})+b'\n')
        (d/'results.json').write_bytes(graph.canonical({'manifest':manifest,'panels':[
            {'panel':r['panel'],'planned':r['operations'],'quality':None,'new_calls':0,'settings_changed':False} for r in receipts]})+b'\n')
        artifact=graph.build_artifact(graph.capture_run(d,profile),profile,projected_at=when)
    validator=Draft202012Validator(json.loads((ROOT/'loom/src/packet/method-graph.schema.json').read_text()))
    validator.validate(artifact['contract']);validator.validate(artifact['trace'])
    graph.codec.validate_packet(artifact['packet'])
    assert graph.recover_files(artifact)
    assert graph.recover_results(artifact)
    raw=graph.canonical(artifact)+b'\n';target=base/'public-workflow-artifact.json.gz'
    target.write_bytes(gzip.compress(raw,mtime=0))
    receipt={'schema':'loom.thread7_contract_verification/1','manifest_contract':'loom.method_graph/1',
      'run_contract':'loom.method_run_trace/1','artifact_sha256':graph.sha256(raw),
      'compressed_sha256':graph.sha256(target.read_bytes()),'entities':len(artifact['packet']['entities']),
      'claims':len(artifact['packet']['claims']),'sources':len(artifact['packet']['sources']),
      'result_entities':len(artifact['result_entity_ids']),'shape_schema_validation':True,
      'existing_python_packet_codec_validation':True,'source_byte_recovery':True,'result_recovery':True,
      'native_cpp_execution':False,'canonical_store_written':False,'provider_calls':0,'quality':None}
    (base/'contract-verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


if __name__=='__main__':
    print(json.dumps(build(Path(__file__).parent),indent=2))
