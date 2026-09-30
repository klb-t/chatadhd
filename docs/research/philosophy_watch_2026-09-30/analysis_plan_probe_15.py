#!/usr/bin/env python3
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import zipfile

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.contracts import analysis_plan_ref as producer
from loom.tools.contracts import validate as validation


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    source=HERE/'analysis_plan_audited_source_15.py'
    with source.open('xb') as f:f.write(Path(producer.__file__).read_bytes())
    spec=importlib.util.spec_from_file_location('loom.tools.contracts.philosophy_plan_snapshot_15',source)
    ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
    def limits():return {d:{'limit':'100','accounting':'cumulative'} for d in ref.DIMENSIONS}
    method={'id':'method','method':'authored:source_analysis','runtime':{'id':'local:scripted','config':{}},
            'depends_on':[],'source_refs':['source'],'variant_axes':[],'required_capabilities':['local'],
            'scope':{'project_access_grants':[]},'roles':[],'tools':[], 'reasoning':{'recipe':'authored mechanism'},
            'acceptance_policy':{'mode':'automatic'},'evaluation_policy':{'retain_all':True},
            'reservation':{d:'1' for d in ref.DIMENSIONS},'config':{}}
    plan={'schema':'loom.analysis_plan/1','id':'philosophy-provenance','resource_budget_ref':'authored-shared',
          'semantic_contract':{'id':'other.domain/graph-review','version':'1','config':{'loss_policy':'explicit'}},
          'sources':[{'id':'source','ref':'authored:mutable-source','provenance':{'known_at':None}}],
          'variant_axes':{},'selection':{'mode':'all'},'methods':[method],
          'resource_limits':limits(),'presets':{'model':'authored:any','acceptance':'automatic'},'extensions':{}}
    def response(output,measure=None,provenance=None):
        return {'output':output,'measurements':{} if measure is None else {'money_usd':measure},
                'measurement_provenance':{} if provenance is None else {'money_usd':provenance}}
    def execute(plan,ledger,callback,loader=None):
        return ref.execute_variant(plan,0,ledger,{('authored:source_analysis','local:scripted'):callback},capabilities=['local'],packet_loader=loader)
    cases=['context_preserved','unpinned_source_resolution_drift','null_zero_provenance','empty_zero_provenance',
           'unknown_measurement_held','tool_configuration_no_dispatch','explicit_revision_changes_identity']
    files=[source,Path(validation.__file__),producer.SCHEMA_PATH,Path(__file__),HERE/'PROTOCOL_15_ANALYSIS_PLAN_PROVENANCE.md']
    freeze={'schema':'loom.philosophy_watch_plan_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},'inputs_sha256':ref.digest(plan),
            'case_ids':cases,'paid_requests':0,'canonical_graph_writes':0}
    with (HERE/'ANALYSIS_PLAN_FREEZE_15.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    reports=[];inventory=[]
    with zipfile.ZipFile(HERE/'analysis_plan_first_scripted_evidence_15.zip','x',compression=zipfile.ZIP_DEFLATED) as archive:
        for ident in cases:
            p=deepcopy(plan);observed=[];loads=[];packet={'schema':'other.domain/1','raw':'packetA'}
            if ident=='tool_configuration_no_dispatch':p['methods'][0]['tools']=[{'name':'erase','capability':True,'execute':False}]
            if ident=='explicit_revision_changes_identity':p['sources'][0]['provenance']['revision_sha256']='a'*64
            with TemporaryDirectory(prefix='plan-watch-') as temporary:
                ledger=ref.ResourceLedger(temporary,p['resource_limits'],budget_id=p['resource_budget_ref'])
                def loader(context):loads.append(deepcopy(packet));return packet
                def callback(context,loaded):
                    observed.append(deepcopy(context))
                    if ident=='null_zero_provenance':return {'output':{'authored':True},'measurements':{'money_usd':'0'},'measurement_provenance':{'money_usd':None}}
                    if ident=='empty_zero_provenance':return response({'authored':True},'0',{})
                    return response({'loaded_packet':deepcopy(loaded),'candidate_origin':'authored mechanism'})
                first=execute(p,ledger,callback,loader)
                record={'id':ident,'first_state':first['results']['method']['state'],
                        'money_usage':first['resource_usage']['money_usd'],'loads':deepcopy(loads),
                        'semantic_contract_preserved':bool(observed and observed[0]['semantic_contract']==p['semantic_contract']),
                        'source_provenance_preserved':bool(observed and observed[0]['sources']==p['sources']),
                        'original_packet_unchanged':packet=={'schema':'other.domain/1','raw':'packetA'},
                        'canonical_graph_writes':first['canonical_graph_writes'],'acceptance_policy_executed':first['acceptance_policy_executed']}
                if ident=='unpinned_source_resolution_drift':
                    packet['raw']='packetB';second=execute(p,ledger,callback,loader)
                    record.update(callback_invocations=len(observed),loader_invocations=len(loads),
                        current_unpinned_packet=deepcopy(packet),cached_result_packet=second['results']['method']['result']['output']['loaded_packet'],
                        same_attempt=first['results']['method']['attempt_id']==second['results']['method']['attempt_id'])
                elif ident=='explicit_revision_changes_identity':
                    p['sources'][0]['provenance']['revision_sha256']='b'*64
                    second=execute(p,ledger,callback,loader)
                    record.update(callback_invocations=len(observed),distinct_attempt=first['results']['method']['attempt_id']!=second['results']['method']['attempt_id'])
                elif ident=='tool_configuration_no_dispatch':
                    record.update(tools_arrived_intact=observed[0]['method']['tools']==p['methods'][0]['tools'],
                                  resource_scope_arrived_intact=observed[0]['method']['scope']==p['methods'][0]['scope'],
                                  engine_tool_dispatch_implemented=False)
                for file in sorted(Path(temporary).rglob('*.json')):
                    blob=file.read_bytes();name=ident+'/'+str(file.relative_to(temporary));archive.writestr(name,blob)
                    inventory.append({'path':name,'bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest()})
                reports.append(record)
        archive.writestr('inventory.json',json.dumps(inventory,sort_keys=True).encode())
    report={'schema':'loom.philosophy_watch_plan_results/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
            'cases':reports,'artifact_inventory':inventory,'actual_paid_requests':0,'actual_paid_cost_usd':'0',
            'scope':'authored caller/adapter provenance diagnostics, not actual billing or action permissions'}
    with (HERE/'ANALYSIS_PLAN_FIRST_RESULTS_15.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps({'cases':reports,'actual_paid_requests':0}))


if __name__=='__main__':main()
