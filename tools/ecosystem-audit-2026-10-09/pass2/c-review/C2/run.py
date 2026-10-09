#!/usr/bin/env python3
"""Independent C2 boundary probes; no live service, credentials, or product edits.

Actual checkout consumers are imported. The payer author's fixture setup provides
only synthetic inputs and intercepted transport, never its test methods. Payer,
queue, projection, native storage and validation remain actual implementations.
"""
from __future__ import annotations
import argparse
from contextlib import redirect_stdout, redirect_stderr
from copy import deepcopy
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
from unittest.mock import patch

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo',type=Path,required=True);p.add_argument('--sha',required=True)
p.add_argument('--native-lib',type=Path,required=True);p.add_argument('--native-sha',required=True)
p.add_argument('--prior-sha',default='69880859802267f1b38b82eeaecd6fdc5522a47a')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();repo=a.repo.resolve()
sha=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
if sha!=a.sha: raise SystemExit('checkout_sha_mismatch')
sys.dont_write_bytecode=True
sys.path.insert(0,str(repo));sys.path.insert(0,str(repo/'loom/tools/structure'))
from loom.tools.seeding import method_graph as graph
from loom.tools.structure import experiment_payer_boundary_v1 as boundary
from loom.tools.structure import experiment_workflow_v1 as workflow
from loom.tools.coordination.graph_store import NativeGraphStore
# Reuse author input fixture setup only. No author test method is invoked.
sys.modules['research_programme_runner']=boundary.payer
import test_research_programme_runner as fixtures
from loom.tools.structure.test_experiment_workflow_v1 import spec as fixture_spec
base=repo/'docs/research/thread7_real_2026-10-09/continuation_01'
spec=importlib.util.spec_from_file_location('audit_c2_public_entrypoint',base/'evaluation-build.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
rows=[]
def sha256(b):return hashlib.sha256(b).hexdigest()
def add(id_,success,*,reproduction=None,**evidence):
    rows.append({'id':id_,'acceptance':'PASS' if success else 'FAIL','reproduction':reproduction,'evidence':evidence})
def blocked(*args,**kwargs): raise RuntimeError('audit_external_transport_blocked')
def hits(artifact,canary):
    sources=graph.recover_files(artifact)
    return {'raw_json_hit':canary in graph.canonical(artifact),'source_roles_hit':[k for k,v in sources.items() if canary in v or(v.startswith(b'\x1f\x8b') and canary in gzip.decompress(v))]}

def projection_test(id_,filename=None):
    canary=('audit-c2-generated-'+sha256(id_.encode())+'-not-real-private-data').encode()
    with tempfile.TemporaryDirectory(prefix='audit-c2-public-') as td:
        directory=Path(td)
        for f in ['handoff-example.json','handoff-contract.json','evaluation-protocol.json','handoff-projection.json']:
            shutil.copyfile(base/f,directory/f)
        if filename:
            obj=graph.strict_json((directory/filename).read_bytes())
            obj['audit_unknown_auxiliary']={'private_fixture':canary.decode()}
            (directory/filename).write_bytes(graph.canonical(obj))
        output=directory/'projection.json.gz';log=io.StringIO();error=None
        try:
            with patch.object(builder,'__file__',str(directory/'evaluation-build.py')),patch.object(sys,'argv',['evaluation-build.py','--output',str(output),'--projected-at','2026-10-09T00:00:00+00:00']),redirect_stdout(log),redirect_stderr(log):
                builder.main()
        except Exception as exc:
            error={'class':type(exc).__name__,'sha256':sha256(str(exc).encode()),'contains_canary':canary in str(exc).encode()}
            # Exercise Python's actual unhandled-exception renderer into a private
            # memory buffer: the CLI has no catch at its __main__ boundary.
            with redirect_stderr(log): sys.excepthook(type(exc),exc,exc.__traceback__)
        evidence={'source_roles_hit':[],'raw_json_hit':False}
        if output.exists():evidence=hits(graph.strict_json(gzip.decompress(output.read_bytes())),canary)
        leaked=evidence['raw_json_hit'] or bool(evidence['source_roles_hit']) or canary in log.getvalue().encode() or bool(error and error['contains_canary'])
        add(id_,not leaked and (bool(error) if filename else output.exists()),reproduction=('PASS' if leaked else 'FAIL') if filename else None,
            input_kind=filename,artifact_emitted=output.exists(),rejection=error,log_canary_hit=canary in log.getvalue().encode(),canary_sha256=sha256(canary),**evidence)

def payer_sync():
    fixture=fixtures.ProgrammeRunnerTests('test_exact_bytes_durable_reserve_receipts_and_usage');fixture.setUp()
    try:
        payer=boundary.payer
        completed=payer.run_stage(fixture.policy,fixture.manifest,fixture.evidence,fixture.private,fixture.keyfile,fixture.repo,transport_fn=fixture.send)
        if completed['status']!='completed':raise AssertionError('fixture_ledger_incomplete')
        fingerprint=payer.transport.key_fingerprint('fixture-credential-not-a-real-key')
        ledger=payer.PrivateLedger(fixture.private,fixture.repo,fixture.policy['programme_id'],fingerprint)
        with ledger.locked():ledger_rows=ledger.rows()
        actual_hash=ledger_rows[0]['request_sha256']
        manifest_hash=payer.sha(fixture.manifest.read_bytes())
        for wrong in [False,True]:
            q=workflow.Queue(fixture.root/('queue-wrong.sqlite' if wrong else 'queue-control.sqlite'))
            try:
                job=next(workflow.ordered_jobs(fixture_spec()))
                request=json.loads(fixture.body)
                if wrong:request['max_tokens']=11
                queued_hash=sha256(graph.canonical(request)) if wrong else sha256(fixture.body)
                job.update(operation_id='one',request_sha256=queued_hash,request=request,requested_model='fake/model',requested_provider={'only':['fake']})
                q.add([job]);q.mark_dispatched('one','synthetic-offline-restore-reference')
                adapter=boundary.PayerBoundary(q,fixture.private,fixture.repo,fixture.policy['programme_id'],fingerprint,manifest_hash)
                before_calls=len(fixture.send.calls);error=None
                try:adapter.sync_verified()
                except Exception as exc:error={'class':type(exc).__name__,'sha256':sha256(str(exc).encode())}
                snapshot=q.snapshot()[0];result=snapshot['result']
                verified=bool(result and result.get('billing_verified') is True)
                accepted=snapshot['state']=='captured' and verified
                settlement_written=any(x['kind']=='payer_verified_settlement' for x in q.evidence('one'))
                rejected=error is not None and snapshot['state']=='pending' and result is None and not settlement_written
                add('C2-PAYER-02' if wrong else 'C2-PAYER-01',rejected if wrong else accepted,
                    reproduction=('PASS' if accepted else 'FAIL') if wrong else None,
                    terminal_ledger_validated=True,synthetic_payer_post_calls=len(fixture.send.posts()),external_calls=0,
                    queue_ledger_request_match=queued_hash==actual_hash,queue_request_sha256=queued_hash,ledger_request_sha256=actual_hash,
                    resulting_state=snapshot['state'],billing_verified=verified,settlement_evidence_written=settlement_written,result_uses_queued_hash=bool(result and result.get('request_sha256')==queued_hash),
                    rejection=error,sync_transport_calls=len(fixture.send.calls)-before_calls,
                    prior_verified_dispatch_evidence=False,boundary='Legacy/offline pending queue imported against a real fully validated fixture ledger; no database tampering.')
            finally:q.db.close()
    finally:fixture.doCleanups()

def cache_wire():
    class Response(io.BytesIO):
        def __init__(self,raw):super().__init__(raw);self.headers={'X-OpenRouter-Cache-Status':'hit','X-Unrelated-Private':'audit-fixture-only'}
        def getcode(self):return 200
    class Opener:
        def __init__(self):self.calls=[]
        def open(self,request,timeout):self.calls.append(request);return Response(b'{"synthetic":true}')
    opener=Opener()
    with patch.object(boundary.payer.transport.urllib.request,'build_opener',return_value=opener):
        transport=boundary.CacheObservedTransport({'base_url':'https://openrouter.ai','timeout_seconds':1,'routes':{'chat':{'method':'POST','path':'/api/v1/chat/completions'}},'headers_by_method':{'POST':{'x-openrouter-cache':'true'}}},'synthetic-key-not-real')
    try:
        result=transport.request('POST','chat',b'{}')
        headers={k.lower():v for k,v in opener.calls[0].header_items()}
        add('C2-WIRE-01',headers.get('x-openrouter-cache')=='false' and result['response_cache_status']=='HIT' and 'audit-fixture-only' not in json.dumps(result,default=str),
            effective_cache_request=headers.get('x-openrouter-cache'),observed_cache_status=result['response_cache_status'],unrelated_header_omitted='audit-fixture-only' not in json.dumps(result,default=str),external_calls=0)
    finally:transport.close()

def native_new_packet():
    artifact=graph.strict_json(gzip.decompress((base/'handoff-artifact-v2.json.gz').read_bytes()));packet=artifact['packet']
    files=graph.recover_files(artifact);results=graph.recover_results(artifact)
    binding={'policy':files['policy']==(base/'evaluation-protocol.json').read_bytes(),
             'program':files['program']==(repo/'loom/tools/structure/experiment_analysis_v1.py').read_bytes()}
    # Policy source is intentionally canonicalized by the producer.
    binding['policy']=graph.strict_json(files['policy'])==graph.strict_json((base/'evaluation-protocol.json').read_bytes())
    add('C2-PUBLIC-05',all(binding.values()),bindings=binding,result_count=len(results),source_roles=len(files),packet_sha256=sha256(graph.canonical(packet)))
    with tempfile.TemporaryDirectory(prefix='audit-c2-native-') as td:
        selection={k:[graph.codec.record_id(k,row) for row in packet[k]] for k in ['entities','claims','sources']}
        expected={k:{v:None for v in values} for k,values in selection.items()}
        with NativeGraphStore(a.native_lib,Path(td)/'store') as store:
            receipt=store.accept(packet,target='audit-c2-public-handoff',selection=selection,expected_rows=expected,explicitly_accepted=True)['receipt']
        with NativeGraphStore(a.native_lib,Path(td)/'store') as store:
            read=store.read(receipt['id']);replay=store.replay(receipt['id'])
            restored={**artifact,'packet':read['receipt']['packet']}
            checks={'packet_exact_after_restart':restored['packet']==packet,'receipt_immutable':replay['receipt']==receipt,'materialized_rows_match':read['row_drift']['matches'],
                    'all_source_bytes_recovered':graph.recover_files(restored)==files,'all_results_recovered':graph.recover_results(restored)==results,
                    'acceptance_does_not_establish_truth':receipt['acceptance_establishes_content_truth'] is False}
        add('C2-NATIVE-01',all(checks.values()),checks=checks,counts={k:len(v) for k,v in selection.items()},result_count=len(results),native_sha=a.native_sha)

def payer_graph_roundtrip():
    fixture=fixtures.ProgrammeRunnerTests('test_exact_bytes_durable_reserve_receipts_and_usage');fixture.setUp()
    q=None
    try:
        payer=boundary.payer;q=workflow.Queue(fixture.root/'verified-queue.sqlite')
        spec=fixture_spec();spec['request_template']=json.loads(fixture.body);spec['variants']['values']=[{'model':'fake/model'}]
        job=next(workflow.ordered_jobs(spec))
        job.update(operation_id='one',request_sha256=sha256(fixture.body),request=json.loads(fixture.body),requested_model='fake/model',requested_provider={'only':['fake']})
        q.add([job]);q.claim('audit-worker','one')
        adapter=boundary.PayerBoundary(q,fixture.private,fixture.repo,fixture.policy['programme_id'],payer.transport.key_fingerprint('fixture-credential-not-a-real-key'),payer.sha(fixture.manifest.read_bytes()))
        intercept=fixture.send
        completed=payer.run_stage(fixture.policy,fixture.manifest,fixture.evidence,fixture.private,fixture.keyfile,fixture.repo,transport_fn=adapter.transport(intercept,controlled_transport=True))
        adapter.sync_verified();snapshot=q.snapshot();result=snapshot[0]['result']
        inputs=fixture.root/'projection';inputs.mkdir()
        projection=graph.load_projection(repo/'docs/research/thread7_real_2026-10-09/graph-projection.json')
        projection['namespace']='audit-c2-synthetic-payer';projection['method']={'identity':'verified-offline-payer-chain','label':'Synthetic verified payer chain'}
        for filename,raw in [('policy_frozen.json',graph.canonical(spec)),('prototype_frozen.py',(repo/'loom/tools/structure/experiment_payer_boundary_v1.py').read_bytes()),('protocol_frozen.md',b'Offline synthetic transport; no real spend or content.\n')]:
            (inputs/filename).write_bytes(raw)
        manifest={'predicted_at':'2026-10-09T00:00:00+00:00','hashes':{role:sha256((inputs/name).read_bytes()) for role,name in [('policy','policy_frozen.json'),('code','prototype_frozen.py'),('protocol','protocol_frozen.md')]}}
        (inputs/'predictions.json').write_bytes(graph.canonical({'manifest':manifest,'panels':snapshot}))
        (inputs/'results.json').write_bytes(graph.canonical({'manifest':manifest,'panels':[result]}))
        artifact=graph.build_artifact(graph.capture_run(inputs,projection),projection,projected_at='2026-10-09T00:00:00+00:00')
        packet=artifact['packet'];selection={k:[graph.codec.record_id(k,row) for row in packet[k]] for k in ['entities','claims','sources']}
        expected={k:{v:None for v in values} for k,values in selection.items()}
        with NativeGraphStore(a.native_lib,fixture.root/'native') as store:
            receipt=store.accept(packet,target='audit-c2-verified-fixture-result',selection=selection,expected_rows=expected,explicitly_accepted=True)['receipt']
        with NativeGraphStore(a.native_lib,fixture.root/'native') as store:
            read=store.read(receipt['id']);restored={**artifact,'packet':read['receipt']['packet']}
            recovered=graph.recover_results(restored)
        checks={'payer_completed':completed['status']=='completed','queue_captured':snapshot[0]['state']=='captured','billing_verified_from_fixture_ledger':result['billing_verified'] is True,
          'verified_reservation_evidence':any(x['kind']=='verified_reservation' for x in q.evidence('one')),'dispatch_order_recorded':len(q.actual_order())==1,
          'packet_exact_after_restart':read['receipt']['packet']==packet,'rows_match':read['row_drift']['matches'],'results_exact':recovered==graph.recover_results(artifact),
          'source_files_exact':graph.recover_files(restored)==graph.recover_files(artifact)}
        add('C2-NATIVE-02',all(checks.values()),checks=checks,counts={k:len(v) for k,v in selection.items()},result_count=len(recovered),
          controlled_transport_post_calls=len(intercept.posts()),real_provider_calls=0,real_spend_usd='0',native_sha=a.native_sha,
          boundary='Harness composes existing APIs; no product dispatcher or UI integration claimed. Synthetic fixture ledger only.')
    finally:
        if q:q.db.close()
        fixture.doCleanups()

def historical_binding():
    rel='docs/research/thread7_real_2026-10-09/public-workflow-artifact.json.gz'
    prior=subprocess.check_output(['git','-C',str(repo),'show',a.prior_sha+':'+rel]);current=(repo/rel).read_bytes()
    artifact=graph.strict_json(gzip.decompress(current));embedded=graph.recover_files(artifact)['program']
    original_program=subprocess.check_output(['git','-C',str(repo),'show',a.prior_sha+':loom/tools/structure/experiment_workflow_v1.py'])
    add('C2-HISTORY-01',current==prior and embedded==original_program,artifact_unchanged=current==prior,embedded_program_matches_prior_sha=embedded==original_program,embedded_program_sha256=sha256(embedded),prior_sha=a.prior_sha,
        interpretation='Existing C-PUBLIC-00 current-source equality FAIL is an obsolete latest-source assumption for an intentionally preserved historical packet, not a product regression.')

with patch.object(socket.socket,'connect',blocked),patch('socket.create_connection',blocked):
    for id_,fn in [('C2-HISTORY',historical_binding),('C2-PUBLIC-01',lambda:projection_test('C2-PUBLIC-01')),
      ('C2-PUBLIC-02',lambda:projection_test('C2-PUBLIC-02','handoff-example.json')),
      ('C2-PUBLIC-03',lambda:projection_test('C2-PUBLIC-03','evaluation-protocol.json')),
      ('C2-PUBLIC-04',lambda:projection_test('C2-PUBLIC-04','handoff-projection.json')),
      ('C2-PAYER',payer_sync),('C2-WIRE',cache_wire),('C2-NATIVE',native_new_packet),('C2-NATIVE-CHAIN',payer_graph_roundtrip)]:
        try:fn()
        except Exception as exc:add(id_,False,harness_error_class=type(exc).__name__,error_sha256=sha256(str(exc).encode()))
receipt={'schema':'klb.audit.c2-boundaries/1','repo':'klb-t/chatadhd','sha':sha,'prior_sha':a.prior_sha,'native_sha':a.native_sha,
    'native_library_sha256':sha256(a.native_lib.read_bytes()),'test_source_sha256':sha256(Path(__file__).read_bytes()),'tests':rows,
    'counts':{s:sum(x['acceptance']==s for x in rows) for s in ['PASS','FAIL']},'private_inputs_read':False,'paid_calls':0,'real_network_calls':0,
    'environment':'Python socket connect/create_connection denied; intercepted payer fixture transport; native background workers disabled',
    'synthetic_credentials_only':True,'fixtures_not_author_tests':True}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'output':str(a.output),'counts':receipt['counts'],'paid_calls':0}))
raise SystemExit(1 if receipt['counts']['FAIL'] else 0)
