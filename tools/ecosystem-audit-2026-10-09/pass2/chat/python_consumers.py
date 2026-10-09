#!/usr/bin/env python3
"""Independent actual Python consumers. No live transport; exits 1 on acceptance failure."""
import argparse, hashlib, json, logging, pathlib, socket, subprocess, sys, tempfile
from unittest.mock import patch

def main():
    p=argparse.ArgumentParser(); p.add_argument('--repo',required=True); p.add_argument('--sha',required=True)
    p.add_argument('--phase',choices=['both','reproduction','acceptance'],default='both'); p.add_argument('--output',required=True)
    a=p.parse_args(); repo=pathlib.Path(a.repo).resolve()
    sha=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    if sha!=a.sha: raise SystemExit('checkout SHA mismatch')
    subprocess.run(['git','-C',str(repo),'diff','--exit-code',sha,'--','engine','core'],check=True,stdout=subprocess.DEVNULL)
    sys.path.insert(0,str(repo)); logging.disable(logging.CRITICAL)
    from engine.db import Database
    from engine.graph_engine import GraphEngine
    from engine.semantic_worker import SemanticWorker
    from engine.memory_engine import MemoryEngine
    from engine.providers import ProviderManager, ProviderError, GroqASR
    from engine.github_sync import GitHubSync, SyncConfig, GitHubFile
    import importlib
    crypto=importlib.import_module("core.crypto")
    import requests
    rows=[]
    def check(id,phase,ok,observed):
        if a.phase in ('both',phase): rows.append(dict(id=id,phase=phase,status='PASS' if ok else 'FAIL',observed=observed))
    def blocked(*args,**kwargs): raise AssertionError('real network prohibited by audit')
    class Response:
        def __init__(self,data,status=200): self.data=data; self.status_code=status
        def json(self): return self.data
        def raise_for_status(self):
            if self.status_code>=400: raise requests.HTTPError('synthetic failure')
    with tempfile.TemporaryDirectory(prefix='audit-chat-') as tmp, patch.object(socket.socket,'connect',blocked), patch.object(socket,'create_connection',blocked):
        root=pathlib.Path(tmp); dbpath=root/'chat.sqlite'; db=Database(dbpath)
        conv=db.create_conv('synthetic worker')['id']; mid=db.create_msg(conv,'Synthetic message long enough for pending worker.', 'user')
        class TimeoutLLM:
            enabled=True
            def analyse(self,text): raise TimeoutError('synthetic timeout at LLM seam')
        worker=SemanticWorker(db,TimeoutLLM(),GraphEngine(db),{'semantic_analysis':True},{})
        worker._drain_batch(); status=db.get_msg(mid)['semantic_status']; pending=db.count_pending_semantic(); db.close()
        db=Database(dbpath); reopened=db.get_msg(mid)['semantic_status']; db.close()
        seen={'status':status,'pending':pending,'reopened':reopened,'errors':worker._errors,'processed':worker._processed}
        check('CH-011.timeout','reproduction',status=='done' and reopened=='done' and pending==0,seen)
        check('CH-011.timeout','acceptance',status!='done' and reopened!='done',seen)

        mem=root/'memory.json'; mem.write_text(json.dumps([{'id':'original','content':'synthetic','future_extension':{'revision':7}}]))
        m=MemoryEngine(mem); m.update_node('original',content='edited synthetic'); after=json.loads(mem.read_text())
        kept=after[0].get('future_extension')=={'revision':7}
        check('A2-CH-001.unknown-field','reproduction',not kept,{'unknown_field_preserved':kept})
        check('A2-CH-001.unknown-field','acceptance',kept,{'unknown_field_preserved':kept})
        malformed='[{"id":"original","content":"synthetic"},{"id":"invalid"},{"id":"last","content":"synthetic tail"}]'
        mem.write_text(malformed); m=MemoryEngine(mem); rejected=False
        try: m.add_node('synthetic addition')
        except Exception: rejected=True
        raw=mem.read_text(); rows_after=json.loads(raw) if not rejected else []
        loss=not rejected and not any(x.get('id')=='last' for x in rows_after) and raw!=malformed
        check('A2-CH-001.partial-load','reproduction',loss,{'write_rejected':rejected,'later_original_row_lost':loss})
        check('A2-CH-001.partial-load','acceptance',rejected and raw==malformed,{'write_rejected':rejected,'original_untouched':raw==malformed})

        pm=ProviderManager({'groq_api_key':'synthetic-not-secret','google_speech_api_key':'synthetic-not-secret'})
        calls=[]
        def post(url,**kw):
            calls.append('groq' if 'groq.com' in url else 'google')
            return Response({},503) if 'groq.com' in url else Response({'results':[{'alternatives':[{'transcript':'synthetic','confidence':0.5}]}]})
        with patch('requests.post',post):
            pm.transcribe_bytes(b'synthetic audio')
            check('A2-CH-002.implicit-fallback','reproduction',calls==['groq','google'],{'providers':list(calls)})
            if a.phase in ('both','acceptance'): rows.append({'id':'A2-CH-002.data-defined-order','phase':'acceptance','status':'BLOCKED_MISSING_CONTRACT','observed':{'public_strategy_input':False,'required_test':'two profiles select different fallback order; explicit provider already supports no fallback'}})
            calls.clear(); refused=False
            try: pm.transcribe_bytes(b'synthetic audio',provider='groq')
            except ProviderError: refused=True
            check('CH-M2.explicit-provider','acceptance',refused and calls==['groq'],{'providers':list(calls),'failure_explicit':refused})
            calls.clear(); refused=False
            try: pm.transcribe_bytes(b'synthetic audio',provider='unsupported')
            except ProviderError: refused=True
            check('CH-M2.unknown-provider','acceptance',refused and not calls,{'refused':refused,'calls':len(calls)})
        with patch('requests.post',lambda *args,**kwargs:Response({'text':'synthetic'})):
            asr_result=GroqASR('synthetic').transcribe_bytes(b'synthetic'); confidence=asr_result.confidence
            check('A2-CH-003.unknown-confidence','reproduction',confidence==0.9,{'provider_confidence_absent':True,'reported':confidence})
            check('A2-CH-003.unknown-confidence','acceptance',confidence is None or getattr(asr_result,'confidence_source',None) in ('estimated','declared'),{'provider_confidence_absent':True,'reported':confidence})

        syncdir=root/'sync'; syncdir.mkdir(); (syncdir/'x.txt').write_text('BBBB')
        remote=[{'type':'file','path':'x.txt','sha':hashlib.sha1(b'blob 4\0AAAA').hexdigest(),'size':4,'download_url':'https://invalid.example/synthetic'}]
        sync=GitHubSync(SyncConfig(repo='synthetic/offline',local_path=str(syncdir)))
        with patch('requests.get',lambda *args,**kw:Response(remote)):
            state=sync.get_sync_status()[0].status
        check('A2-CH-004.same-size','reproduction',state=='synced',{'distinct_git_blob_hashes':True,'status':state})
        check('A2-CH-004.same-size','acceptance',state!='synced',{'distinct_git_blob_hashes':True,'status':state})
        f=GitHubFile('x.txt','',4,local_path=str(syncdir/'x.txt'))
        sync.config.sync_direction='pull_only'
        with patch('requests.put',side_effect=blocked) as spy: guarded=not sync.push_file(f) and spy.call_count==0
        check('CH-M3.sync-direction','acceptance',guarded,{'push_refused_before_transport':guarded})
        with patch.object(crypto,'_HAS_CRYPTO',False):
            refused=False
            try: crypto.CryptoEngine().encrypt('synthetic','synthetic-password')
            except RuntimeError: refused=True
            check('CH-M1.missing-crypto','acceptance',refused,{'explicit_error':refused,'plaintext_returned':not refused})
    paths=['engine/db.py','engine/semantic_worker.py','engine/graph_engine.py','engine/memory_engine.py','engine/providers.py','engine/github_sync.py','core/crypto.py']
    out={'schema':'klbt.audit.receipt/2','repo':'klb-t/chatadhd','sha':sha,'suite':'chat-python-consumers','scope':'actual Python/SQLite/filesystem consumers; provider requests intercepted; LLM timeout injected at dependency seam; no UI/device', 'external_network':'blocked','source_hashes':{x:hashlib.sha256((repo/x).read_bytes()).hexdigest() for x in paths},'cases':rows}
    out['counts']={s:sum(x['status']==s for x in rows) for s in ['PASS','FAIL','BLOCKED_MISSING_CONTRACT']}
    pathlib.Path(a.output).write_text(json.dumps(out,indent=2)+'\n'); print(json.dumps(out['counts']))
    return int(out['counts']['FAIL']>0 or out['counts']['BLOCKED_MISSING_CONTRACT']>0)
if __name__=='__main__': sys.exit(main())
