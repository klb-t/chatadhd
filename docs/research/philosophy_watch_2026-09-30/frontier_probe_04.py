#!/usr/bin/env python3
"""Execute first configured-bound scripted probes against an immutable source copy."""
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

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT))
from loom.tools.structure import graph_panel_live as panel, openrouter_runner as safe


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def snapshot_modules():
    folder=HERE/'frontier_audited_source_04';folder.mkdir(exist_ok=False)
    producer=ROOT/'loom/tools/structure/frontier_panel_v1'
    for name in ('adapter.py','bounded_runner.py'):
        with (folder/name).open('xb') as f:f.write((producer/name).read_bytes())
    prefix='loom.tools.structure.frontier_watch_snapshot_04'
    package=types.ModuleType(prefix);package.__path__=[str(folder)];sys.modules[prefix]=package
    modules={}
    for name in ('adapter','bounded_runner'):
        spec=importlib.util.spec_from_file_location(prefix+'.'+name,folder/(name+'.py'))
        module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
        setattr(package,name,module);modules[name]=module
    return folder,modules['adapter'],modules['bounded_runner']


def main():
    folder,a,client=snapshot_modules()
    presets,configs,identities=a.load_presets();recipes=a.load_recipes();case=panel.load_dev_inputs()[0]
    cfg=deepcopy(configs['gpt6_luna']);identity=identities['gpt6_luna']
    source_rows=a.dev_rows([case],'supplied_edge_judgment','baseline',cfg,identity,recipes)[:1]
    freeze={'schema':'loom.philosophy_watch_frontier_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in
                           (folder/'adapter.py',folder/'bounded_runner.py',Path(__file__),HERE/'PROTOCOL_04_FRONTIER.md',Path(safe.__file__))},
            'snapshot_dependencies':a.dependencies(),'paid_requests':0,'actual_credentials_read':False,
            'cases':['configured_8mib_response','configured_large_cost']}
    with (HERE/'FRONTIER_FREEZE_04.json').open('x',encoding='utf-8') as f:json.dump(freeze,f,indent=2);f.write('\n')
    observations=[];archive_path=HERE/'frontier_first_scripted_evidence_04.zip'
    inventory=[]
    with zipfile.ZipFile(archive_path,'x',compression=zipfile.ZIP_DEFLATED) as archive:
        for ident in freeze['cases']:
            execution=deepcopy(presets['default_execution'])
            rows=deepcopy(source_rows)
            if ident=='configured_large_cost':
                execution['budget_usd']='1000000000';execution['batch_reservation_cap_usd']='3000000'
                rows[0]['reservation_usd']='3000000';reported_cost='2000000'
                content={'query_id':rows[0]['id'],'label':'unknown'}
                key_limit='1000000000'
            else:
                execution['limits']['max_response_bytes']=8*1024*1024
                reported_cost='0.00001';key_limit='2'
                content={'query_id':rows[0]['id'],'label':'unknown','padding':'x'*(2*1024*1024+100)}
            manifest=a.manifest_for(rows,cfg,'gpt6_luna','supplied_edge_judgment','baseline','all24',execution,1)
            raw=safe.canonical({'model':identity['model_aliases'][-1],'provider':identity['provider_aliases'][-1],
                 'choices':[{'finish_reason':'stop','message':{'content':safe.canonical(content).decode()}}],
                 'usage':{'cost':reported_cost,'is_byok':False,'prompt_tokens':10,'completion_tokens':10}})
            calls=[]
            def scripted(method,path,body,key):
                assert key=='offline-philosophy-probe-credential'
                calls.append({'method':method,'path':path,'body_sha256':hashlib.sha256(body).hexdigest() if body else None})
                if method=='GET':
                    return 200,safe.canonical({'data':{'limit':key_limit,'limit_remaining':key_limit,'limit_reset':None,
                           'is_management_key':False,'include_byok_in_limit':False,'byok_usage':'0'}})
                return 200,raw
            with TemporaryDirectory(prefix='frontier-watch-') as temp:
                run_dir=Path(temp)/'run';manifest_path=Path(temp)/'manifest.json'
                manifest_path.write_bytes(safe.canonical(manifest))
                ledger=client.run_manifest(manifest,run_dir,transport_fn=scripted,
                                           key_loader=lambda:'offline-philosophy-probe-credential')
                result={'id':ident,'scripted_post_count':sum(c['method']=='POST' for c in calls),'calls':calls,
                        'raw_bytes':len(raw),'raw_sha256':hashlib.sha256(raw).hexdigest(),
                        'configured_response_bytes':execution['limits']['max_response_bytes'],
                        'fake_reported_cost_usd':reported_cost,'actual_paid_cost_usd':'0',
                        'run_attempt_state':ledger['attempts'][0]['state'],
                        'run_retained_reported_cost':ledger['attempts'][0].get('reported_cost_usd')}
                try:
                    compiled,summary,_=a.replay(manifest_path,run_dir)
                    result.update(replay_returned=True,compiled_state=compiled[0]['state'],summary=summary)
                except Exception as exc:
                    result.update(replay_returned=False,replay_exception_type=type(exc).__name__,replay_exception=str(exc))
                observations.append(result)
                files=[manifest_path,run_dir/'ledger.json',run_dir/ledger['attempts'][0]['response_file']]
                for file in files:
                    name=ident+'/'+file.name;blob=file.read_bytes();archive.writestr(name,blob)
                    inventory.append({'path':name,'bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest()})
        archive.writestr('inventory.json',safe.canonical(inventory))
    report={'schema':'loom.philosophy_watch_frontier_results/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
            'planned_scripted_cases':2,'actual_paid_requests':0,'actual_paid_cost_usd':'0','observations':observations,
            'evidence_archive_sha256':sha(archive_path),'evidence_inventory':inventory,
            'scope':'configured-bound mechanics; fabricated transport receipts, not real model quality/billing'}
    with (HERE/'FRONTIER_FIRST_RESULTS_04.json').open('x',encoding='utf-8') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps({'cases':len(observations),'replay_exceptions':[r.get('replay_exception') for r in observations],
                      'actual_paid_requests':0}))


if __name__=='__main__':main()
