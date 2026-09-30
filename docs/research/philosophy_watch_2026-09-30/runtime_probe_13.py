#!/usr/bin/env python3
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import types
import zipfile

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.structure import openrouter_runner as safe
from loom.tools.structure.agentic_graph_v1 import packet as codec


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def snapshot():
    folder=HERE/'runtime_audited_source_13';folder.mkdir(exist_ok=False)
    producer=ROOT/'loom/tools/structure/frontier_panel_v1'
    prefix='loom.tools.structure.runtime_watch_snapshot_13'
    package=types.ModuleType(prefix);package.__path__=[str(folder)];sys.modules[prefix]=package
    modules={}
    for name in ('adapter','bounded_runner','runtime'):
        file=folder/(name+'.py')
        with file.open('xb') as f:f.write((producer/(name+'.py')).read_bytes())
        spec=importlib.util.spec_from_file_location(prefix+'.'+name,file)
        module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
        setattr(package,name,module);modules[name]=module
    return folder,modules['adapter'],modules['runtime']


def main():
    folder,a,rt=snapshot();_,_,identities=a.load_presets();identity=identities['gpt61_sol']
    presets=a.read(a.HERE/'runtime_presets.json')['presets']
    origin={'kind':'user','actor':'owner','model':None,'recipe_sha256':None,'response_sha256':None}
    packet=codec.make_packet(task={'scope':'supplied_packet','goal':'offline invariant audit'},origin=origin)
    def request(kind='client_loop',**changes):
        config=deepcopy(presets[kind]);config.update(changes);return rt.prepare_runtime(packet,config)
    def tool(name):return {'type':'function','function':{'name':name,'parameters':{'type':'object','properties':{}}}}
    def call(ident,name,args='{}'):return {'id':ident,'type':'function','function':{'name':name,'arguments':args}}
    def response(calls=None,cost='.01',bad_identity=False):
        message={'role':'assistant','content':None if calls else '{}'}
        if calls:message['tool_calls']=calls
        usage={'is_byok':False,'prompt_tokens':2,'completion_tokens':2}
        if cost is not None:usage['cost']=cost
        return safe.canonical({'model':'fabricated-unobserved' if bad_identity else identity['model_aliases'][-1],
            'provider':identity['provider_aliases'][-1],'usage':usage,
            'choices':[{'finish_reason':'tool_calls' if calls else 'stop','message':message}]})
    cases=['malformed_tool_cost','unknown_cost','identity_rejection_cost','denied_tool',
           'partial_tools_failure','initial_request_content_drift','symbolic_million_agents']
    files=[folder/'adapter.py',folder/'bounded_runner.py',folder/'runtime.py',Path(codec.__file__),
           Path(safe.__file__),a.HERE/'runtime_presets.json',a.PRESETS,a.RECIPES,
           HERE/'PROTOCOL_13_RUNTIME_RETEST.md',Path(__file__)]
    freeze={'schema':'loom.philosophy_watch_runtime_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},'cases':cases,
            'actual_paid_requests':0,'actual_credentials_read':False}
    with (HERE/'RUNTIME_FREEZE_13.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    reports=[];inventory=[]
    with zipfile.ZipFile(HERE/'runtime_first_scripted_evidence_13.zip','x',compression=zipfile.ZIP_DEFLATED) as archive:
        for ident in cases:
            handled=[];req=request();raw=response()
            handlers={}
            if ident=='malformed_tool_cost':raw=response([call('a','lookup','[invalid')])
            elif ident=='unknown_cost':raw=response(cost=None)
            elif ident=='identity_rejection_cost':raw=response(bad_identity=True)
            elif ident=='denied_tool':
                req=request(tools=[tool('lookup')]);raw=response([call('a','lookup')])
                handlers={'lookup':lambda args,context:handled.append('unexpected') or {}}
            elif ident=='partial_tools_failure':
                req=request(tools=[tool('first'),tool('second')],tool_permissions={
                    'first':{'execute':True,'scope':'scripted'},'second':{'execute':True,'scope':'scripted'}})
                raw=response([call('a','first'),call('b','second')])
                def first(args,context):handled.append('first_completed');return {'retained_action_result':'first successful value'}
                def second(args,context):handled.append('second_entered');raise RuntimeError('scripted second handler failure')
                handlers={'first':first,'second':second}
            elif ident=='symbolic_million_agents':
                req=request('managed_swarm',max_concurrent_subagents=1000000)
                reports.append({'id':ident,'configured_concurrency':req['body']['multi_agent']['max_concurrent_subagents'],
                    'worker_allocation_performed':False,'account_availability_verified':req['account_availability_verified']})
                archive.writestr(ident+'/request.json',safe.canonical(req));continue
            with TemporaryDirectory(prefix='runtime-watch-') as d:
                run=Path(d)/'run';ledger=rt.execute_scripted(req,run,lambda current:raw,tool_handlers=handlers)
                record={'id':ident,'handled':handled,'state':ledger['attempts'][0]['state'],
                    'stopped_reason':ledger['stopped_reason'],'retained_scripted_charge_usd':ledger['retained_budget_charge_usd'],
                    'reported_scripted_cost_usd':ledger['attempts'][0].get('reported_cost_usd'),
                    'actual_paid_cost_usd':'0','tool_result_files':sorted(p.name for p in run.glob('*.tool_results.json'))}
                if ident=='initial_request_content_drift':
                    initial=a.read(run/'initial_request.json');initial['semantic_contract']='fabricated.different/1'
                    initial['config']['semantic_contract']='fabricated.different/1'
                    (run/'initial_request.json').write_bytes(safe.canonical(initial))
                    record['initial_contents_match_stored_hash']=safe.digest({k:v for k,v in initial.items() if k!='request_sha256'})==initial['request_sha256']
                try:record['replay_returned']=True;record['replay_outputs']=rt.replay_scripted(run)['outputs']
                except Exception as exc:record.update(replay_returned=False,replay_exception_type=type(exc).__name__,replay_exception=str(exc))
                for file in sorted(run.iterdir()):
                    if not file.is_file():continue
                    blob=file.read_bytes();name=ident+'/'+file.name;archive.writestr(name,blob)
                    inventory.append({'path':name,'bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest()})
                reports.append(record)
        archive.writestr('inventory.json',safe.canonical(inventory))
    report={'schema':'loom.philosophy_watch_runtime_results/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
            'actual_paid_requests':0,'actual_paid_cost_usd':'0','cases':reports,'artifact_inventory':inventory,
            'scope':'offline scripted invariant probes, not hosted API access or model quality'}
    with (HERE/'RUNTIME_FIRST_RESULTS_13.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps({'cases':reports,'actual_paid_requests':0}))


if __name__=='__main__':main()
