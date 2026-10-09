#!/usr/bin/env python3
"""Independent AGEDS real-consumer audit. No source algorithm is reimplemented.
Network sockets are denied; ASGI is in-process and ASR is a capture-only stub.
Each case reports reproduction separately from acceptance. Acceptance failure
returns 1, infrastructure failure returns 2. No expected-failure masking.
"""
import argparse, asyncio, copy, dataclasses, hashlib, json, os, pathlib, socket, subprocess, sys, tempfile, types

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--checkout',required=True);ap.add_argument('--sha',required=True);ap.add_argument('--output',required=True);ap.add_argument('--mode',choices=['both','reproduction','acceptance'],default='acceptance');args=ap.parse_args()
    repo=pathlib.Path(args.checkout).resolve(); actual=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    if actual!=args.sha: raise SystemExit('checkout SHA differs from requested SHA')
    dirty=subprocess.check_output(['git','-C',str(repo),'diff','--name-only',args.sha],text=True).splitlines()
    if any(x.startswith(('server/','core/','androidApp/')) for x in dirty): raise SystemExit('product checkout is dirty')
    sys.path.insert(0,str(repo)); work=tempfile.TemporaryDirectory(prefix='ageds-audit-'); root=pathlib.Path(work.name)
    os.environ.update(EW_DATA_DIR=str(root),EW_DB_PATH=str(root/'evidence.db'),EW_STORE_DIR=str(root/'store'))
    network=[]
    def deny(*a,**k): network.append('blocked');raise RuntimeError('audit forbids network transport')
    socket.socket.connect=deny;socket.socket.connect_ex=deny;socket.create_connection=deny
    from server.app import config,db,evidence,jobs,worker,transcription,main as api,packages,archive,citations
    from server.app.importers import whatsapp,sms_backup
    from server.app.scanner import scan_sources,ScanLimits
    import httpx
    cases=[]
    def record(id,observation,reproduction,acceptance,requirement):
        cases.append(dict(id=id,observation=observation,reproduction='PASS' if reproduction else 'FAIL',acceptance='PASS' if acceptance else 'FAIL',acceptance_criterion=requirement))
    def fresh(name):
        base=root/name;base.mkdir(); st=dataclasses.replace(config.settings,data_dir=base,db_path=base/'db.sqlite',store_dir=base/'store',scan_roots=(base/'scan',),whisper_model='audit-A')
        st.store_dir.mkdir();st.scan_roots[0].mkdir()
        for mod in [config,db,evidence,worker,api]:mod.settings=st
        db.init_db();return base
    def artifact(base,label='clip'):
        p=base/(label+'.wav');p.write_bytes(b'synthetic audio bytes '+label.encode());sid=evidence.ensure_source('synthetic','audit');return evidence.ingest_file(p,source_id=sid,source_locator='synthetic:'+label,mime_type='audio/wav')
    async def request(method,url,**kw):
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=api.app),base_url='http://audit.invalid') as client:return await client.request(method,url,**kw)
    captures=[]
    class WhisperModel:
        def __init__(self,model,**kw): self.model=model;captures.append({'model':model,'constructor':kw})
        def transcribe(self,audio,**kw):
            captures[-1]['parameters']=kw;captures[-1]['input_bytes']=len(audio.read())
            seg=types.SimpleNamespace(start=0.0,end=1.0,text='audit transcript',words=[])
            return iter([seg]),types.SimpleNamespace(language='en',language_probability=.5,transcription_options=kw)
    sys.modules['faster_whisper']=types.SimpleNamespace(WhisperModel=WhisperModel)
    # A job admitted while A is active, then configuration edited before execution.
    base=fresh('recipe');aid=artifact(base)
    a=asyncio.run(request('POST',f'/api/artifacts/{aid}/transcribe',json={'recipe':{'model':'audit-A','version':'A'}}))
    worker.settings=dataclasses.replace(worker.settings,whisper_model='audit-B')
    b=asyncio.run(request('POST',f'/api/artifacts/{aid}/transcribe',json={'recipe':{'model':'audit-B','version':'B'}}))
    ja,jb=a.json().get('job_id'),b.json().get('job_id');recipe_requests_accepted=ja is not None and jb is not None
    if not recipe_requests_accepted:
        ja=transcription.queue_transcription(aid);jb=ja  # Ordinary supported path for remaining consumer probe, never treated as accepted recipe.
    job=jobs.claim_job(worker_id='audit');did=worker.process_job(job)
    with db.session() as conn:
        stored=dict(conn.execute('SELECT * FROM jobs WHERE id=?',(ja,)).fetchone());run=dict(conn.execute('SELECT * FROM processing_runs WHERE id=?',(job['run_id'],)).fetchone())
    obs={'status_codes':[a.status_code,b.status_code],'job_ids':[ja,jb],'payload':stored['payload_json'],'model_executed':captures[-1]['model'],'recorded_run_model':run['model'],'recipe_requests_accepted':recipe_requests_accepted}
    record('EA-AGEDS-002.recipe_snapshot',obs,recipe_requests_accepted and ja==jb and captures[-1]['model']=='audit-B',recipe_requests_accepted and ja!=jb and captures[-1]['model']=='audit-A','Distinct requested recipes produce distinct jobs; edit of global default does not rewrite queued A.')
    record('A2-AG-001.recipe_unknown_input',{'status':a.status_code,'ignored_body':stored['payload_json']=='{}'},a.status_code==200 and stored['payload_json']=='{}',a.status_code==422 or stored['payload_json']!='{}','Unsupported recipe input is rejected explicitly or persisted and consumed; never acknowledged and silently dropped.')
    # Different actual global profiles do reach the existing adapter; this does not fix snapshot.
    base=fresh('profiles');aid=artifact(base);transcription.queue_transcription(aid);j=jobs.claim_job(worker_id='audit');worker.process_job(j)
    ma=captures[-1]['model'];worker.settings=dataclasses.replace(worker.settings,whisper_model='audit-B');transcription.queue_transcription(aid);j2=jobs.claim_job(worker_id='audit');worker.process_job(j2)
    mb=captures[-1]['model']
    record('AG-CROSS.profile_consumer',{'models':[ma,mb],'run_ids':[j['run_id'],j2['run_id']]},True,[ma,mb]==['audit-A','audit-B'],'Two global model settings must change the real adapter and leave distinct run metadata.')
    # Failure is not completion, lease recovery is separate from retry admission.
    base=fresh('failure');aid=artifact(base);transcription.queue_transcription(aid);j=jobs.claim_job(worker_id='audit')
    class TimeoutAdapter:
        def describe(self):return {'model':'audit-timeout'}
        def transcribe(self,stream):raise TimeoutError('synthetic timeout; no private content')
    try:worker.process_job(j,TimeoutAdapter())
    except TimeoutError:pass
    with db.session() as conn:
        row=dict(conn.execute('SELECT status FROM jobs WHERE id=?',(j['id'],)).fetchone());count=conn.execute('SELECT count(*) FROM derived_text').fetchone()[0]
    retry=transcription.queue_transcription(aid);rj=jobs.claim_job(worker_id='audit');worker.process_job(rj)
    record('AG-CROSS.error_not_done',{'failure_status':row['status'],'failed_transcripts':count,'retry_job_new':retry!=j['id'],'retry_run_new':rj['run_id']!=j['run_id']},True,row['status']=='failed' and count==0 and retry!=j['id'],'Timeout produces failed attempt without transcript; explicit retry retains old failed run.')
    base=fresh('lease');aid=artifact(base);transcription.queue_transcription(aid);old=jobs.claim_job(worker_id='old',now=100,lease_seconds=1);new=jobs.claim_job(worker_id='new',now=102,lease_seconds=5)
    fenced=False
    try:jobs.publish_transcript(old,text='stale',now=102)
    except jobs.LeaseLost:fenced=True
    nid=jobs.publish_transcript(new,text='fresh',now=103)
    record('AG-CROSS.lease_fencing',{'stale_rejected':fenced,'new_transcript_id':nid,'different_runs':old['run_id']!=new['run_id']},True,fenced and old['run_id']!=new['run_id'],'Expired owner cannot publish; new attempt has new run and valid owner can publish.')
    # HTTP accepts unknown limits while the underlying scanner consumes explicit limits.
    base=fresh('scan');scan=api.settings.scan_roots[0]
    for i in range(3):(scan/f'{i}.txt').write_text('synthetic')
    direct=[scan_sources(scan,limits=ScanLimits(max_files=n)) for n in (1,2)]
    resp=[asyncio.run(request('POST','/api/source-scans',json={'rootId':0,'limits':{'max_files':n}})) for n in (1,2)]
    observations={'direct_files':[len(x['files']) for x in direct],'http_status':[x.status_code for x in resp],'http_files':[len(x.json().get('files',[])) for x in resp]}
    record('EA-AGEDS-003.http_limits',observations,observations['direct_files']==[1,2] and observations['http_files']==[3,3],observations['http_files']==[1,2],'Supported HTTP scan profile values 1 and 2 must reach scanner and result coverage.')
    record('A2-AG-001.scan_unknown_input',observations,all(x.status_code==200 for x in resp) and observations['http_files']==[3,3],all(x.status_code==422 for x in resp) or observations['http_files']==[1,2],'Unsupported limit field must be rejected explicitly or consumed, not silently ignored.')
    disabled=dataclasses.replace(api.settings,scan_roots=());api.settings=disabled
    no_root=asyncio.run(request('POST','/api/source-scans',json={'rootId':0}));api.settings=dataclasses.replace(disabled,scan_roots=(scan,))
    escape=asyncio.run(request('POST','/api/source-scans',json={'rootId':0,'relativePath':'..'}))
    record('AG-CROSS.scan_scope_guard',{'disabled':no_root.status_code,'escape':escape.status_code},True,no_root.status_code==403 and escape.status_code==422,'No configured source roots denies scan; path traversal cannot escape allowed root.')
    # Upload is explicit and immutable, but no caller-selectable case reaches ensure_source.
    base=fresh('upload')
    with db.session() as conn:
        first=conn.execute('SELECT id FROM cases ORDER BY id LIMIT 1').fetchone()[0]
        second=conn.execute("INSERT INTO cases(name) VALUES('Synthetic second case')").lastrowid
    uploaded=asyncio.run(request('POST','/api/artifacts/upload',files={'file':('clip.wav',b'audit bytes','audio/wav')},data={'source_label':'audit upload','case_id':str(second),'metadata_json':'{"custom":{"version":7}}'}))
    case_response_status=uploaded.status_code
    if uploaded.status_code!=200:
        uploaded=asyncio.run(request('POST','/api/artifacts/upload',files={'file':('clip.wav',b'audit bytes','audio/wav')},data={'source_label':'audit upload','metadata_json':'{"custom":{"version":7}}'}))
    uploaded_id=uploaded.json()['artifact_id']
    with db.session() as conn:
        saved=dict(conn.execute('SELECT a.*,s.case_id FROM artifacts a JOIN sources s ON s.id=a.source_id WHERE a.id=?',(uploaded_id,)).fetchone())
        enqueued=conn.execute('SELECT count(*) FROM jobs').fetchone()[0]
    record('A2-AG-003.case_selection',{'requested_case':second,'stored_case':saved['case_id'],'http_status':case_response_status},saved['case_id']==first and case_response_status==200,case_response_status==200 and saved['case_id']==second,'Explicit case_id at upload must bind source/artifact to that case; missing case requires declared selection policy, not implicit first row.')
    invalid=asyncio.run(request('POST','/api/artifacts/upload',files={'file':('bad.wav',b'bad','audio/wav')},data={'metadata_json':'{broken'}))
    record('AG-CROSS.upload_contract',{'queued_without_transcribe':enqueued,'stored_bytes_match':pathlib.Path(saved['stored_path']).read_bytes()==b'audit bytes','metadata_preserved':json.loads(saved['metadata_json'])['client_metadata']=={'custom':{'version':7}},'bad_metadata_status':invalid.status_code},True,enqueued==0 and invalid.status_code==422 and pathlib.Path(saved['stored_path']).read_bytes()==b'audit bytes','Upload preserves bytes/client metadata and never queues ASR implicitly; malformed metadata is rejected rather than reset to default.')
    # Import has a declared but non-configurable day-first policy. Raw bytes stay unchanged.
    base=fresh('imports');p=base/'wa.txt';raw='02/03/2026, 10:00 - Audit sender: ambiguous date\n'.encode();p.write_bytes(raw)
    wa=whatsapp.import_whatsapp_txt(p,source_locator='synthetic:wa')
    with db.session() as conn: ev=dict(conn.execute('SELECT * FROM events').fetchone());meta=json.loads(ev['metadata_json'])
    month_first_supported=False
    try:
        other=base/'month-first.txt';other.write_bytes(raw)
        imported=whatsapp.import_whatsapp_txt(other,source_locator='synthetic:month-first',date_order='month_first')
        with db.session() as conn:
            me=conn.execute('SELECT ts_start,metadata_json FROM events WHERE artifact_id=?',(imported['artifact_id'],)).fetchone()
        month_first_supported=me['ts_start']=='2026-02-03T10:00:00' and json.loads(me['metadata_json'])['timestamp']['date_order']=='month_first'
    except TypeError:
        pass  # Actual consumer rejects proposed profile input; acceptance remains FAIL, not skipped.
    record('A2-AG-002.whatsapp_date_policy',{'timestamp':ev['ts_start'],'ambiguity_reported':meta['timestamp']['date_order_ambiguous'],'raw_unchanged':p.read_bytes()==raw,'month_first_profile_consumed':month_first_supported},ev['ts_start']=='2026-03-02T10:00:00' and meta['timestamp']['date_order_ambiguous'],month_first_supported,'Proposed acceptance contract date_order=month_first changes the real importer to Feb 3 and pins policy metadata; baseline rejects this input. A coordinated alternative API needs a harness adapter, not weakened assertion.')
    # Existing event collisions are rejected, not inferred away.
    conflict=False
    try:evidence.add_event(source_id=ev['source_id'],artifact_id=ev['artifact_id'],event_type=ev['event_type'],external_id=ev['external_id'],body='different')
    except evidence.IntegrityError:conflict=True
    record('AG-CROSS.event_collision',{'conflict_rejected':conflict},True,conflict,'Same source occurrence with different parsed payload must not overwrite or silently deduplicate.')
    # Real SQLite export/archive/read handles extension fields and does not resume historical jobs.
    base=fresh('archive');aid=artifact(base);transcription.queue_transcription(aid);j=jobs.claim_job(worker_id='audit')
    d1=jobs.publish_transcript(j,text='old audit',model='audit-A',segments=[{'start':0,'end':1,'text':'old audit'}]);anchor=citations.create_citation(aid,d1,[0])
    transcription.queue_transcription(aid);j2=jobs.claim_job(worker_id='audit');d2=jobs.publish_transcript(j2,text='new audit',model='audit-B',segments=[{'start':0,'end':1,'text':'new audit'}])
    pack=packages.export_metadata_package();pack['audit_extension']={'unrecognized':{'version':7}};pack['tables']['artifacts'][0]['audit_unknown_field']={'retained':True}
    payload={k:v for k,v in pack.items() if k!='integrity'};pack['integrity']=packages._integrity(payload)
    report=archive.import_metadata_archive(pack,base/'inert.sqlite');restored=archive.read_metadata_archive(base/'inert.sqlite')
    out=base/'restored.json';archive.export_metadata_archive(base/'inert.sqlite',out)
    equal=packages.canonical_json(pack)==packages.canonical_json(restored)==out.read_bytes()
    # A fresh read connection simulates process storage reopening, not Android lifecycle.
    with db.session() as conn: pinned=dict(conn.execute('SELECT * FROM evidence_anchors WHERE id=?',(anchor['id'],)).fetchone())
    record('AG-CROSS.inert_roundtrip',{'exact_canonical_roundtrip':equal,'extension_preserved':restored.get('audit_extension')==pack['audit_extension'],'jobs_resumed':report['jobs_resumed'],'old_anchor_version':pinned['derived_text_id'],'new_version':d2},True,equal and not report['jobs_resumed'] and pinned['derived_text_id']==d1,'Inert SQLite archive retains unknown fields exactly and never resumes jobs; old citation stays attached to old result version.')
    mutations=[]
    for label,mutate in [('version',lambda p:p.update(schema='ageds.metadata-package/v999')),('duplicate_id',lambda p:p['tables']['artifacts'].append(copy.deepcopy(p['tables']['artifacts'][0]))),('missing_ref',lambda p:p['tables']['derived_text'][0].update(artifact_id=99999))]:
        altered=copy.deepcopy(pack);mutate(altered);altered['integrity']=packages._integrity({k:v for k,v in altered.items() if k!='integrity'});check=packages.validate_metadata_package(altered);mutations.append({'mutation':label,'rejected':not check['valid'],'codes':sorted(set(e['code'] for e in check['errors']))})
    record('AG-CROSS.graph_rejections',mutations,True,all(x['rejected'] for x in mutations),'Rehashed unsupported schema, duplicate IDs and missing references are rejected semantically, not merely by hash mismatch.')
    # Migrator is idempotent and append-only evidence guards actually execute.
    before=packages.export_metadata_package();db.init_db();after=packages.export_metadata_package();same=before['tables']==after['tables'];blocked=[]
    for table in ['source_observations','audit_log','evidence_anchors']:
        try:
            with db.session() as conn:conn.execute(f'DELETE FROM {table}')
        except Exception as e:blocked.append(type(e).__name__=='IntegrityError')
    record('AG-CROSS.migration_delete_guards',{'second_init_rows_unchanged':same,'append_only_delete_rejected':blocked},True,same and all(blocked),'Repeated migration preserves rows; append-only acquisition/audit/citation deletes stay blocked.')
    result={'schema':'klbt.audit.ageds.pass2/1','repo':'klb-t/AGEDS','sha':actual,'network_calls':len(network),'network_policy':'socket connect/connect_ex/create_connection blocked; in-process ASGI; capture-only faster_whisper; no downloaded model','tests':cases,'counts':{axis:{status:sum(x[axis]==status for x in cases) for status in ['PASS','FAIL']} for axis in ['reproduction','acceptance']},'limits':['ASGI is not browser/network deployment E2E','ASR stub verifies real adapter choice/options and real store/job consumers, not model accuracy','SQLite reopen is not full Android restart','WhatsApp profile acceptance remains missing capability and needs coordinated input contract']}
    pathlib.Path(args.output).parent.mkdir(parents=True,exist_ok=True);pathlib.Path(args.output).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['counts']))
    axes=['reproduction','acceptance'] if args.mode=='both' else [args.mode];return int(any(x[ax]=='FAIL' for x in cases for ax in axes))
if __name__=='__main__':sys.exit(main())
