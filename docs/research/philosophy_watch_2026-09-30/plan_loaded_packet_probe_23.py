#!/usr/bin/env python3
from copy import deepcopy
from datetime import datetime,timezone
import hashlib,importlib.util,json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import zipfile

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.contracts import analysis_plan_ref as producer


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    snapshot=HERE/'analysis_plan_audited_source_23.py'
    with snapshot.open('xb') as f:f.write(Path(producer.__file__).read_bytes())
    spec=importlib.util.spec_from_file_location('loom.tools.contracts.philosophy_plan_snapshot_23',snapshot)
    ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
    limits={d:{'limit':'100','accounting':'cumulative','measurement_policy':{
        s:'use_declared_amount' if s in ref.KNOWN_STATUSES else 'max_reservation_amount' for s in ref.MEASUREMENT_STATUSES}} for d in ref.DIMENSIONS}
    method={'id':'one','method':'authored:analysis','runtime':{'id':'local:scripted','config':{}},
        'depends_on':[],'source_refs':['source'],'variant_axes':[],'required_capabilities':['local'],
        'scope':{},'roles':[],'tools':[],'reasoning':{},'acceptance_policy':{'mode':'automatic'},
        'evaluation_policy':{},'reservation':{d:'1' for d in ref.DIMENSIONS},'config':{}}
    plan={'schema':'loom.analysis_plan/1','id':'loaded-packet-audit','resource_budget_ref':'authored-shared',
        'semantic_contract':{'id':'distinct.domain/graph','version':'1','config':{}},
        'sources':[{'id':'source','ref':'authored:raw-source','provenance':{'known_at':None},
                   'binding':{'mode':'pinned_snapshot','snapshot_id':'authored-first-source'}}],
        'variant_axes':{},'selection':{'mode':'all'},'methods':[method],
        'resource_limits':limits,'presets':{},'extensions':{}}
    original={'schema':'distinct.domain/1','raw':'Żółć 🧪 first retained packet'}
    freeze={'schema':'loom.philosophy_watch_loaded_packet_freeze/1','known_at':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in (snapshot,producer.SCHEMA_PATH,Path(__file__),HERE/'PROTOCOL_23_PLAN_LOADED_PACKET_RETEST.md')},
            'plan_sha256':ref.digest(plan),'packet_sha256':ref.digest(original),'paid_requests':0}
    with (HERE/'PLAN_LOADED_PACKET_FREEZE_23.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    with TemporaryDirectory(prefix='plan-loaded-watch-') as temporary:
        calls=[];loads=[];observed=[];ledger=ref.ResourceLedger(temporary,limits,budget_id=plan['resource_budget_ref'])
        def loader(context):loads.append(1);return original
        def callback(context,packet):
            calls.append(1);observed.append(context)
            files=list(Path(temporary).glob('attempt-*/loaded_packet_first.json'));assert len(files)==1
            assert json.loads(files[0].read_text())=={'loader_supplied':True,'packet':original}
            assert context['source_binding_verification']=='caller_declared_not_verified'
            packet['raw']='mutation in callback'
            return {'output':{'proposal':True},'measurements':{},'measurement_provenance':{}}
        def execute():return ref.execute_variant(plan,0,ledger,{('authored:analysis','local:scripted'):callback},capabilities=['local'],packet_loader=loader)
        first=execute();second=execute();out=first['results']['one'];replayed=second['results']['one']
        assert out['source_binding_verification']==replayed['source_binding_verification']=='caller_declared_not_verified'
        assert out['loaded_packet_sha256']==replayed['loaded_packet_sha256']==ref.digest({'loader_supplied':True,'packet':original})
        assert len(calls)==len(loads)==1 and replayed['dispatch']=='replayed_bound_snapshot'
        path=next(Path(temporary).glob('attempt-*/loaded_packet_first.json'))
        assert json.loads(path.read_text())=={'loader_supplied':True,'packet':original}
        report={'schema':'loom.philosophy_watch_loaded_packet_results/1','known_at':datetime.now(timezone.utc).isoformat(),
            'snapshot_retained_before_callback':True,'private_callback_mutation_preserved_original':True,
            'caller_declared_not_verified_on_context_dispatch_replay':True,'resolved_packet_envelope_sha256':out['loaded_packet_sha256'],'raw_packet_sha256':ref.digest(original),
            'loader_calls':len(loads),'callback_calls':len(calls),'replay_dispatch':replayed['dispatch'],
            'paid_requests':0,'canonical_graph_writes':first['canonical_graph_writes']}
        with zipfile.ZipFile(HERE/'plan_loaded_packet_evidence_23.zip','x',compression=zipfile.ZIP_DEFLATED) as archive:
            for file in sorted(Path(temporary).rglob('*.json')):archive.writestr(str(file.relative_to(temporary)),file.read_bytes())
            path.write_text('{"schema":"distinct.domain/1","raw":"tampered retained packet"}')
            try:execute()
            except ref.PlanError as exc:report['retained_packet_drift_rejected']=exc.code
            else:raise AssertionError('retained_packet_drift_was_not_rejected')
            archive.writestr('tampered_loaded_packet.json',path.read_bytes())
    with (HERE/'PLAN_LOADED_PACKET_FIRST_RESULTS_23.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report))


if __name__=='__main__':main()
