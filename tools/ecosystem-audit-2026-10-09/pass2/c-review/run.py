#!/usr/bin/env python3
"""Independent C public projection / actual transport / native store probes.

Imports the checkout's actual consumers. All records and credentials created by
this runner are synthetic. No private archive/key is read; no network is sent.
Acceptance failure is exit 1; reproduction is reported separately and cannot
make a product acceptance test green. No captured canary bytes are serialized.
"""
from __future__ import annotations
import argparse
import base64
from copy import deepcopy
from contextlib import redirect_stdout, redirect_stderr
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
from unittest.mock import patch

P = argparse.ArgumentParser(description=__doc__)
P.add_argument('--repo', type=Path, required=True)
P.add_argument('--sha', required=True)
P.add_argument('--native-lib', type=Path)
P.add_argument('--native-sha')
P.add_argument('--output', type=Path, required=True)
P.add_argument('--phase', choices=['projection', 'native', 'all'], default='all')
A = P.parse_args()
REPO = A.repo.resolve()
ACTUAL_SHA = subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip()
if ACTUAL_SHA != A.sha:
    raise SystemExit('checkout_sha_mismatch')
sys.path.insert(0,str(REPO))
sys.dont_write_bytecode = True
from loom.tools.seeding import method_graph as graph
from loom.tools.structure import experiment_workflow_v1 as workflow
from loom.tools.structure import research_programme_transport as transport
from loom.tools.coordination.graph_store import NativeGraphStore

BASE = REPO/'docs/research/thread7_real_2026-10-09'
SPECP = importlib.util.spec_from_file_location('audited_public_builder',BASE/'build_public_artifact.py')
builder = importlib.util.module_from_spec(SPECP)
SPECP.loader.exec_module(builder)
OBSERVATIONS=[]

def hash_bytes(value): return hashlib.sha256(value).hexdigest()
def add(id_, category, acceptance, *, reproduction=None, **evidence):
    OBSERVATIONS.append({'id':id_, 'category':category,
                         'reproduction':reproduction,
                         'acceptance':acceptance, 'evidence':evidence})
def block_connect(*args, **kwargs): raise RuntimeError('audit_external_transport_blocked')
def artifact_canary_hits(artifact, canary):
    # Both visible JSON and base64/source members; base64 is not redaction.
    raw=graph.canonical(artifact)
    direct=canary in raw
    recovered=graph.recover_files(artifact)
    hit_roles=[role for role,value in recovered.items() if canary in value or
               (value.startswith(b'\x1f\x8b') and canary in gzip.decompress(value))]
    return direct,hit_roles

def projected_canary(name, mutate, expect_failure=True):
    canary=('audit-generated-'+hash_bytes(name.encode())+'-not-a-secret').encode()
    with tempfile.TemporaryDirectory(prefix='audit-c-public-') as tmp:
        base=Path(tmp)
        for filename in ['graph-projection.json','workflow-preparation-receipt.json','workflow-plan.json','HANDOFF_B.md']:
            (base/filename).write_bytes((BASE/filename).read_bytes())
        mutate(base,canary.decode())
        log=io.StringIO(); error=None
        try:
            with redirect_stdout(log),redirect_stderr(log): builder.build(base)
        except Exception as exc:
            # Never expose input or raw exception, even synthetic canaries.
            error={'class':type(exc).__name__, 'sha256':hash_bytes(str(exc).encode()),
                   'contains_canary':canary in str(exc).encode()}
        produced=(base/'public-workflow-artifact.json.gz').exists()
        direct,roles=False,[]
        if produced:
            artifact=graph.strict_json(gzip.decompress((base/'public-workflow-artifact.json.gz').read_bytes()))
            direct,roles=artifact_canary_hits(artifact,canary)
        leaked=direct or bool(roles) or canary in log.getvalue().encode() or bool(error and error['contains_canary'])
        safe=not leaked and (error is not None or not expect_failure)
        add(name,'public-projection-boundary','PASS' if safe else 'FAIL',
            reproduction=('PASS' if leaked else 'FAIL') if expect_failure else None,
            generated_canary_sha256=hash_bytes(canary), artifact_emitted=produced,
            raw_json_hit=direct, decoded_source_roles_hit=roles,
            log_canary_hit=canary in log.getvalue().encode(), rejection=error)

def mutate_json(base,filename,callback):
    obj=graph.strict_json((base/filename).read_bytes()); callback(obj)
    (base/filename).write_bytes(graph.canonical(obj))

def fixture_spec():
    # Fixture inputs only; the checkout renders and hashes actual bodies.
    return {'schema':'loom.experiment_spec/1','id':'audit-synthetic-c-v1','campaign_id':'audit-offline',
      'variants':{'mode':'list','values':[{'model':'fixture/model-a','temperature':0.0},{'model':'fixture/model-b','temperature':0.7}]},
      'scope':[{'id':'case-1','family':'family-1','source_sha256':'a'*64,'split':'development','input':'synthetic audit source'}],
      'repetitions':1,'request_template':{'model':{'$variant':'model'},'temperature':{'$variant':'temperature'},
         'messages':[{'role':'user','content':{'$variant':'case'}}]},
      'response_cache':False,'prompt_cache_policy':'observe','cache_prefix_messages':1,'route_id':'chat',
      'endpoint_identity':'fixture/endpoint','ordering':{'mode':'balanced_blocks','seed':13},
      'adoption_policy':'manual','metrics':['native_execution'],'evaluators':['audit-fixture'],
      'budget':{'campaign_reset':False},'triggers':{'opt_in':False,'events':['checkpoint'],'frequency_seconds':0}}

class CapturedResponse(io.BytesIO):
    def getcode(self): return 200
class CapturedOpener:
    def __init__(self): self.calls=[]
    def open(self,request,timeout):
        self.calls.append({'headers':{k.lower():v for k,v in request.header_items()},
                           'body':request.data,'timeout':timeout})
        return CapturedResponse(b'{"synthetic":true}')

def wire_probes():
    rows=list(workflow.ordered_jobs(fixture_spec()))
    config={'base_url':'https://openrouter.ai','timeout_seconds':1,
            'routes':{'chat':{'method':'POST','path':'/api/v1/chat/completions'}}}
    receipts=[]
    for explicit in [False,True]:
        cfg=deepcopy(config)
        if explicit: cfg['headers_by_method']={'POST':rows[0]['headers']}
        t=transport.OpenRouterTransport(cfg,'audit-synthetic-credential')
        intercepted=CapturedOpener();t._opener=intercepted
        for row in rows: t.request('POST','chat',workflow.canonical(row['request']))
        sent=[graph.strict_json(x['body']) for x in intercepted.calls]
        receipts.append({'explicit_policy':explicit,
                         'requested_cache_header':rows[0]['headers']['X-OpenRouter-Cache'],
                         'actual_cache_header':intercepted.calls[0]['headers'].get('x-openrouter-cache'),
                         'models':sorted(r['model'] for r in sent),
                         'temperatures':sorted(r['temperature'] for r in sent)})
        t.close()
    add('C-WIRE-01','real-transport-with-intercept','PASS' if receipts[1]['models']==['fixture/model-a','fixture/model-b'] and receipts[1]['temperatures']==[0.0,0.7] and receipts[1]['actual_cache_header']=='false' else 'FAIL',
        profiles_change_actual_body=receipts[1]['models']==['fixture/model-a','fixture/model-b'] and receipts[1]['temperatures']==[0.0,0.7],
        observations=receipts, external_calls=0,
        boundary='Explicit transport headers_by_method works; the workflow row alone is not a payer dispatcher.')
    # No copying of payer logic: real transport error firewall gets a failing
    # intercepted opener carrying only a generated synthetic canary.
    canary='audit-error-'+hash_bytes(b'error-canary')
    class BrokenOpener:
        def open(self,*args,**kwargs): raise RuntimeError(canary)
    t=transport.OpenRouterTransport(config,'audit-synthetic-credential');t._opener=BrokenOpener()
    try: t.request('POST','chat',b'{}'); safe=False
    except Exception as exc: safe=canary not in str(exc) and str(exc)=='transport_failed'
    t.close()
    add('C-WIRE-02','real-transport-error-firewall','PASS' if safe else 'FAIL',
        raw_exception_hidden=safe,canary_sha256=hash_bytes(canary.encode()),external_calls=0)

def result_binding_probe():
    spec=fixture_spec();spec['repetitions']=2
    spec['variants']['values']=spec['variants']['values'][:1]
    rows=list(workflow.ordered_jobs(spec))
    assert rows[0]['request_sha256']==rows[1]['request_sha256']
    assert rows[0]['operation_id']!=rows[1]['operation_id']
    with tempfile.TemporaryDirectory(prefix='audit-c-binding-') as tmp:
        q=workflow.Queue(Path(tmp)/'queue.sqlite');q.add(rows)
        for i,row in enumerate(rows): q.mark_dispatched(row['operation_id'],'synthetic-reservation:'+str(i))
        result=workflow.result_record(rows[1],b'synthetic response for occurrence two',
                  {'http_status':200,'billing_verified':False,'payer_response_reference':'synthetic-response:1'})
        error=None
        try: q.capture_first(rows[0]['operation_id'],result)
        except ValueError as exc: error=hash_bytes(str(exc).encode())
        slot=next(x for x in q.snapshot() if x['job']['operation_id']==rows[0]['operation_id'])
        q.db.close()
        mismatch=bool(slot['result'] and slot['result']['operation_id']!=slot['job']['operation_id'])
        add('C-BIND-01','occurrence-payer-result-binding','FAIL' if mismatch else 'PASS',
            reproduction='PASS' if mismatch else 'FAIL',
            equal_request_sha256=True,distinct_operation_ids=True,
            wrong_occurrence_captured=mismatch,actual_state=slot['state'],
            rejection_sha256=error,synthetic_only=True,
            boundary='Same body is expected for repeated trials; request hash alone cannot bind an occurrence.')

def synthetic_artifact(directory, *, edited=False):
    spec=fixture_spec()
    if edited:
        spec['id']='audit-synthetic-c-v2'
        spec['variants']['values'][0]['temperature']=0.2
    rows=list(workflow.ordered_jobs(spec))
    q=workflow.Queue(directory/'private-queue.sqlite');q.add(rows)
    captures=[]
    for index,row in enumerate(rows):
        # An external payer would supply this reference and raw. Here only an
        # explicit fixture reference exists; no budget authentication is claimed.
        q.mark_dispatched(row['operation_id'],'synthetic-payer-reservation:'+str(index))
        raw=graph.canonical({'synthetic':True,'ordinal':index})
        result=workflow.result_record(row,raw,{'http_status':200,'billing_verified':False,
                    'payer_response_reference':'synthetic-payer-response:'+str(index)})
        q.capture_first(row['operation_id'],result)
        captures.append(result)
    snapshots=q.snapshot();q.db.close()
    # Generic graph consumer inputs, not a product implementation or payer copy.
    projection=graph.load_projection(BASE/'graph-projection.json')
    projection['namespace']='audit-synthetic-c'
    projection['method']={'identity':'synthetic-workflow-capture','label':'Synthetic workflow capture'}
    documents={'policy_frozen.json':spec,'predictions.json':{'panels':snapshots},
               'results.json':{'panels':captures}}
    (directory/'prototype_frozen.py').write_bytes((REPO/'loom/tools/structure/experiment_workflow_v1.py').read_bytes())
    (directory/'protocol_frozen.md').write_bytes(b'Offline synthetic payer boundary audit; not a model experiment.\n')
    (directory/'policy_frozen.json').write_bytes(graph.canonical(spec))
    manifest={'predicted_at':'2026-10-09T00:00:00+00:00','hashes':{
          role:hash_bytes((directory/filename).read_bytes()) for role,filename in
          [('policy','policy_frozen.json'),('code','prototype_frozen.py'),('protocol','protocol_frozen.md')]}}
    for filename in ['predictions.json','results.json']:
        documents[filename]['manifest']=manifest
        (directory/filename).write_bytes(graph.canonical(documents[filename]))
    artifact=graph.build_artifact(graph.capture_run(directory,projection),projection,
                                  projected_at='2026-10-09T00:00:00+00:00')
    return artifact,rows,captures,snapshots

def native_roundtrip(id_,artifact,directory):
    packet=artifact['packet']
    selection={key:[graph.codec.record_id(key,row) for row in packet[key]] for key in ['entities','claims','sources']}
    expected={key:{id_:None for id_ in values} for key,values in selection.items()}
    original_files=graph.recover_files(artifact)
    original_results=graph.recover_results(artifact)
    with NativeGraphStore(A.native_lib,directory) as store:
        result=store.accept(packet,target='audit-c:'+id_,selection=selection,expected_rows=expected,explicitly_accepted=True)
        receipt=result['receipt'];receipt_id=receipt['id']
        assert receipt['packet']==packet
        assert receipt['acceptance_establishes_content_truth'] is False
    # A brand-new native context reads the persistent store, not a Python codec
    # roundtrip or retained in-process mapping.
    with NativeGraphStore(A.native_lib,directory) as store:
        read=store.read(receipt_id); replay=store.replay(receipt_id)
        restored={**artifact,'packet':read['receipt']['packet']}
        checks={'exact_packet_after_native_restart':read['receipt']['packet']==packet,
                'materialized_rows_match':read['row_drift']['matches'],
                'receipt_immutable':replay['receipt']==receipt,
                'source_bytes_recovered':graph.recover_files(restored)==original_files,
                'results_recovered':graph.recover_results(restored)==original_results}
    add(id_,'native-write-restart-read-replay','PASS' if all(checks.values()) else 'FAIL',
        checks=checks, selected_counts={k:len(v) for k,v in selection.items()},
        recovered_source_roles=len(original_files),recovered_results=len(original_results),
        packet_sha256=hash_bytes(graph.canonical(packet)),receipt_id_sha256=hash_bytes(receipt_id.encode()),
        model_execution=False,external_calls=0)

def mutate_packet(packet,mutation):
    # Fixture forging only: use actual codec digest to keep integrity hashes
    # coherent, so native rejection must reach the semantic/DTO boundary.
    result=deepcopy(packet);mutation(result)
    for key in graph.codec.COLLECTIONS:
        for row in result[key]:
            id_=graph.codec.record_id(key,row)
            if id_ in result['provenance'][key]:
                result['provenance'][key][id_]['record_sha256']=graph.codec.digest(row)
    result['packet_id']=graph.codec.digest(graph.codec._packet_payload(result))
    return result

def native_boundaries(artifact,directory):
    packet=artifact['packet']
    selection={key:[graph.codec.record_id(key,row) for row in packet[key]] for key in ['entities','claims','sources']}
    expected={key:{id_:None for id_ in values} for key,values in selection.items()}
    mutations=[('C-GRAPH-01','unknown_native_entity_member',lambda p:p['entities'][0].update(audit_future_extension={'opaque':'synthetic'})),
               ('C-GRAPH-02','unsupported_packet_version',lambda p:p.update(schema='loom.graph_packet/999')),
               ('C-GRAPH-03','duplicate_entity_identity',lambda p:p['entities'].append(deepcopy(p['entities'][0]))),
               ('C-GRAPH-04','unresolved_entity_parent',lambda p:p['entities'][0].update(parent='synthetic-unresolved-reference'))]
    with NativeGraphStore(A.native_lib,directory) as store:
        for ident,name,mutation in mutations:
            malformed=mutate_packet(packet,mutation)
            request={'operation':'accept','target':'audit-c-boundary:'+name,'packet':malformed,
                     'selection':selection,'expected_rows':expected,'explicitly_accepted':True}
            error=None
            try: store.execute(request)
            except Exception as exc: error={'class':type(exc).__name__,'code':getattr(exc,'code',None),'message_sha256':hash_bytes(str(exc).encode())}
            add(ident,'direct-native-boundary','PASS' if error else 'FAIL',mutation=name,explicit_rejection=error,
                python_codec_bypassed=True,integrity_hashes_recomputed=True)
        original=store.accept(packet,target='audit-c-cas',selection=selection,expected_rows=expected,explicitly_accepted=True)['receipt']
        first=mutate_packet(packet,lambda p:p['entities'][0].update(label='audit branch A'))
        accepted=store.accept(first,target='audit-c-cas',selection=selection,expected_rows=original['stored_row_sha256'],explicitly_accepted=True)
        second=mutate_packet(packet,lambda p:p['entities'][0].update(label='audit branch B'))
        stale_rejected=False
        try: store.accept(second,target='audit-c-cas',selection=selection,expected_rows=original['stored_row_sha256'],explicitly_accepted=True)
        except Exception: stale_rejected=True
        unchanged=store.read(accepted['receipt']['id'])['row_drift']['matches']
        add('C-GRAPH-05','native-branch-cas','PASS' if stale_rejected and unchanged else 'FAIL',stale_branch_rejected=stale_rejected,first_branch_remains_current=unchanged)

def native_recipe_history(directory):
    before=directory/'before';before.mkdir()
    after=directory/'after';after.mkdir()
    old,_,_,_=synthetic_artifact(before)
    new,_,_,_=synthetic_artifact(after,edited=True)
    receipts=[]
    with NativeGraphStore(A.native_lib,directory/'store') as store:
        for artifact in [old,new]:
            packet=artifact['packet']
            selection={key:[graph.codec.record_id(key,row) for row in packet[key]] for key in ['entities','claims','sources']}
            expected={key:{id_:receipts[0]['stored_row_sha256'][key].get(id_) if receipts else None for id_ in values} for key,values in selection.items()}
            receipts.append(store.accept(packet,target='audit-c-versioned-results',selection=selection,expected_rows=expected,explicitly_accepted=True)['receipt'])
    with NativeGraphStore(A.native_lib,directory/'store') as store:
        restored=store.replay(receipts[0]['id'])['receipt']['packet']
        old_trace=old['trace']
        old_results=graph.recover_results({**old,'packet':restored})
        checks={'recipe_version_changed':old_trace['recipe_sha256']!=new['trace']['recipe_sha256'],
                'old_receipt_unchanged':restored==old['packet'],
                'old_results_recover_exactly':old_results==graph.recover_results(old),
                'old_binding_preserved':all(x['method_version_id']==old_trace['method_version_id'] for x in old_trace['result_bindings']),
                'new_method_version_distinct':old_trace['method_version_id']!=new['trace']['method_version_id']}
    add('C-GRAPH-06','native-old-recipe-after-new-profile','PASS' if all(checks.values()) else 'FAIL',checks=checks,
        old_recipe_sha256=old['trace']['recipe_sha256'],new_recipe_sha256=new['trace']['recipe_sha256'],
        scope='Offline workflow projection only; no application Basic/Advanced/Expert UI claim.')

with patch.object(socket.socket,'connect',block_connect),patch('socket.create_connection',block_connect):
    published=graph.strict_json(gzip.decompress((BASE/'public-workflow-artifact.json.gz').read_bytes()))
    if A.phase in ['projection','all']:
        graph.codec.validate_packet(published['packet'])
        files=graph.recover_files(published); results=graph.recover_results(published)
        expected_files={'policy':BASE/'workflow-plan.json','program':REPO/'loom/tools/structure/experiment_workflow_v1.py','protocol':BASE/'HANDOFF_B.md'}
        exact={role:files[role]==filename.read_bytes() for role,filename in expected_files.items()}
        add('C-PUBLIC-00','committed-public-artifact','PASS' if all(exact.values()) else 'FAIL',
            exact_committed_source_bindings=exact,source_roles=len(files),result_records=len(results),
            packet_sha256=hash_bytes(graph.canonical(published['packet'])),
            boundary='Confirms recovered public bytes, not privacy equivalence to unread private inputs.')
        projected_canary('C-PUBLIC-01',lambda d,c: mutate_json(d,'workflow-preparation-receipt.json',lambda x:x[0].update(audit_unknown=c)),expect_failure=False)
        projected_canary('C-PUBLIC-02',lambda d,c: mutate_json(d,'workflow-preparation-receipt.json',lambda x:x[0]['producer_sha256'].update(audit_unknown={'secret':c})))
        projected_canary('C-PUBLIC-03',lambda d,c: mutate_json(d,'workflow-plan.json',lambda x:x.update(audit_auxiliary={'private_note':c})))
        projected_canary('C-PUBLIC-04',lambda d,c: mutate_json(d,'graph-projection.json',lambda x:x.update(audit_auxiliary={'private_note':c})))
        projected_canary('C-PUBLIC-05',lambda d,c: mutate_json(d,'workflow-preparation-receipt.json',lambda x:x[0].update(spec_sha256=c)))
        wire_probes()
        result_binding_probe()
    if A.phase in ['native','all']:
        if not A.native_lib or not A.native_sha:
            add('C-NATIVE','native-write-restart-read-replay','BLOCKED',reason='--native-lib and --native-sha required')
        else:
            for ident,source in [('C-NATIVE-01','published'),('C-NATIVE-02','synthetic')]:
                try:
                    with tempfile.TemporaryDirectory(prefix='audit-c-native-') as td:
                        directory=Path(td)
                        artifact=published
                        if source=='synthetic':
                            inputs=directory/'inputs';inputs.mkdir()
                            artifact,rows,captures,snapshots=synthetic_artifact(inputs)
                        native_roundtrip(ident,artifact,directory/'store')
                        if source=='synthetic':
                            add('C-PAYER-BOUNDARY','binding-not-authentication','PASS',
                                payer_reference_present=all(s['payer_reference'] for s in snapshots),
                                unverified_billing_preserved=all(r['billing_verified'] is False for r in captures),
                                observed_model_unknown=all(r['observed_model'] is None for r in captures),
                                raw_response_bound_by_hash=True,
                                live_payer_authenticated=False,
                                synthetic_response_reference=True,
                                boundary='No application dispatcher invokes this complete path; harness composes public existing APIs explicitly.')
                except Exception as exc:
                    add(ident,'native-write-restart-read-replay','FAIL',error_class=type(exc).__name__,
                        error_sha256=hash_bytes(str(exc).encode()),
                        known_code=getattr(exc,'code',None))

            for ident,function in [('C-GRAPH-BOUNDARIES',lambda d:native_boundaries(published,d)),('C-GRAPH-RECIPE',native_recipe_history)]:
                try:
                    with tempfile.TemporaryDirectory(prefix='audit-c-graph-') as td: function(Path(td))
                except Exception as exc:
                    add(ident,'native-integration','FAIL',error_class=type(exc).__name__,
                        error_sha256=hash_bytes(str(exc).encode()),known_code=getattr(exc,'code',None))

receipt={'schema':'klb.audit.c-review/1','repo':'klb-t/chatadhd','sha':ACTUAL_SHA,
         'native_sha':A.native_sha,'native_library_sha256':hash_bytes(A.native_lib.read_bytes()) if A.native_lib else None,
         'phase':A.phase,'test_source_sha256':hash_bytes(Path(__file__).read_bytes()),
         'network_policy':'Python socket connect/create_connection denied; intercepted payer transport; native workers disabled',
         'private_inputs_read':False,'paid_calls':0,'tests':OBSERVATIONS,
         'counts':{status:sum(x['acceptance']==status for x in OBSERVATIONS) for status in ['PASS','FAIL','BLOCKED']}}
A.output.parent.mkdir(parents=True,exist_ok=True)
A.output.write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n')
print(json.dumps({'output':str(A.output),'counts':receipt['counts'],'paid_calls':0}))
raise SystemExit(1 if receipt['counts']['FAIL'] else 2 if receipt['counts']['BLOCKED'] else 0)
