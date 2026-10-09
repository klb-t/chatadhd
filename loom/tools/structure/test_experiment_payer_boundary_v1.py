"""Mechanical fault tests only. All credentials, responses, costs are fixtures.

No fixture ledger is part of the owner's 720-attempt campaign.
"""
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import hashlib
import io
import itertools
import json
from pathlib import Path
import sqlite3
import tempfile
import threading
import unittest
from unittest.mock import patch

from loom.tools.structure import experiment_workflow_v1 as w
from loom.tools.structure import experiment_payer_boundary_v1 as boundary
from loom.tools.structure.test_experiment_workflow_v1 import spec
import test_research_programme_runner as fixtures

payer = boundary.payer


class Crash(BaseException):
    pass


class BoundaryTests(unittest.TestCase):
    def setUp(self):
        self.f=fixtures.ProgrammeRunnerTests('test_exact_bytes_durable_reserve_receipts_and_usage')
        self.f.setUp();self.addCleanup(self.f.doCleanups)
        self.path=self.f.root/'queue.sqlite'
        self.q=w.Queue(self.path);self.addCleanup(self.q.db.close)
        job=next(w.ordered_jobs(spec()))
        job.update(operation_id='one',request_sha256=hashlib.sha256(self.f.body).hexdigest(),
                   request=json.loads(self.f.body),requested_model='fake/model',requested_provider={'only':['fake']})
        self.q.add([job]);self.q.claim('worker','one')
        self.adapter=boundary.PayerBoundary(self.q,self.f.private,self.f.repo,self.f.policy['programme_id'],
            payer.transport.key_fingerprint('fixture-credential-not-a-real-key'),payer.sha(self.f.manifest.read_bytes()))
        self.base=self.f.send
        self.f.send=self.adapter.transport(self.base,controlled_transport=True)

    def run_stage(self):
        f=self.f
        return payer.run_stage(f.policy,f.manifest,f.evidence,f.private,f.keyfile,f.repo,transport_fn=f.send)

    def original(self):
        with sqlite3.connect(self.f.private/'ledger.sqlite3') as db:
            return json.loads(db.execute('SELECT payload FROM attempts').fetchone()[0])

    def test_verified_reservation_and_actual_dispatch_order(self):
        self.assertEqual(self.run_stage()['status'],'completed')
        self.assertEqual(self.q.snapshot()[0]['state'],'pending')
        self.adapter.sync_verified()
        self.assertEqual(self.q.snapshot()[0]['state'],'captured')
        self.assertTrue(self.q.snapshot()[0]['result']['billing_verified'])
        self.assertEqual(self.q.actual_order()[0]['operation_id'],'one')
        self.assertEqual(len(self.q.evidence('one')),3)
        self.base.calls.clear();self.run_stage();self.adapter.sync_verified()
        self.assertEqual(self.base.calls,[]);self.assertEqual(len(self.q.actual_order()),1)

    def test_fake_reference_and_missing_reservation_rejected(self):
        self.run_stage()
        with self.assertRaisesRegex(ValueError,'binding_mismatch'):
            self.adapter.verify('one',{'reservation':'caller-says-paid'})
        ref={'programme_id':self.adapter.programme_id,'operation_id':'absent',
             'manifest_sha256':self.adapter.manifest,'key_fingerprint_sha256':self.adapter.fingerprint}
        with self.assertRaisesRegex(ValueError,'reservation_missing'):
            self.adapter.verify('absent',ref)

    def test_transport_requires_verified_header_implementation(self):
        with self.assertRaisesRegex(ValueError,'independent_response_transport_required'):
            self.adapter.transport(self.base)

    def test_timeout_after_send_preserves_unknown_and_no_retry(self):
        def timeout(method,route,body=None,params=None):
            result=self.base(method,route,body,params)
            if method=='POST':raise TimeoutError('fixture')
            return result
        self.f.send=self.adapter.transport(timeout,controlled_transport=True)
        result=self.run_stage()
        self.assertEqual(result['status'],'stopped')
        self.assertIsNone(self.original()['actual_cost_usd'])
        self.assertEqual(result['unresolved_reservations_usd'],'0.0220')
        with self.assertRaises(payer.ProgrammeError):self.run_stage()
        self.assertEqual(len(self.base.posts()),1)
        self.assertEqual(self.q.snapshot()[0]['state'],'pending')

    def test_partial_bad_and_cached_raw_are_preserved_without_success(self):
        for index,response in enumerate(({'raw':b'{"id":','transport_error':'response_read_failed'},
                                       {'raw':b'not-json'}, {'response_cache_status':'HIT'})):
            with self.subTest(index=index):
                if index:
                    self.f.private=self.f.root/('private'+str(index));self.base.usage=0
                    self.q.db.close();self.q=w.Queue(self.f.root/('q'+str(index)))
                    self.addCleanup(self.q.db.close)
                    job=next(w.jobs(spec()));job.update(operation_id='one',request_sha256=payer.sha(self.f.body))
                    self.q.add([job]);self.q.claim('worker')
                    self.adapter=boundary.PayerBoundary(self.q,self.f.private,self.f.repo,self.f.policy['programme_id'],
                        payer.transport.key_fingerprint('fixture-credential-not-a-real-key'),payer.sha(self.f.manifest.read_bytes()))
                    self.f.send=self.adapter.transport(self.base,controlled_transport=True)
                self.base.response_override=response
                self.assertEqual(self.run_stage()['status'],'stopped')
                raw=next((self.f.private/'records').glob('*.response.bin')).read_bytes()
                if 'raw' in response:self.assertEqual(raw,response['raw'])
                self.assertIsNone(self.original()['actual_cost_usd'])
                with self.assertRaises(payer.ProgrammeError):self.adapter.sync_verified()
                self.assertNotEqual(self.q.snapshot()[0]['state'],'captured')

    def test_manifest_drift_and_request_tampering_stop(self):
        self.adapter.manifest='a'*64
        result=self.run_stage()
        self.assertEqual(result['status'],'stopped');self.assertEqual(len(self.base.posts()),0)
        self.assertIsNone(self.original()['actual_cost_usd'])

    def test_request_witness_tampering_blocks_before_post(self):
        reserve=payer.PrivateLedger.reserve
        def tamper(ledger,*a,**kw):
            row=reserve(ledger,*a,**kw)
            path=ledger.records/(payer.sha(row['operation_id'].encode())+'.request.bin')
            path.write_bytes(b'changed fixture request')
            return row
        with patch.object(payer.PrivateLedger,'reserve',tamper):
            self.assertEqual(self.run_stage()['status'],'stopped')
        self.assertEqual(len(self.base.posts()),0)
        self.assertEqual(self.q.snapshot()[0]['state'],'claimed')

    def test_campaign_or_key_binding_mismatch_blocks(self):
        self.adapter.fingerprint='a'*64
        self.assertEqual(self.run_stage()['status'],'stopped')
        self.assertEqual(len(self.base.posts()),0)

    def test_cancellation_does_not_release_reserved_money(self):
        original=payer.PrivateLedger.reserve
        def cancel_after_reserve(ledger,*a,**kw):
            row=original(ledger,*a,**kw);self.q.cancel('one');return row
        with patch.object(payer.PrivateLedger,'reserve',cancel_after_reserve):
            result=self.run_stage()
        self.assertEqual(result['status'],'stopped');self.assertEqual(len(self.base.posts()),0)
        self.assertEqual(result['unresolved_reservations_usd'],'0.0220')
        self.assertEqual(self.q.snapshot()[0]['state'],'cancellation_requested')

    def test_six_crash_boundaries_and_restart(self):
        # Use fresh fixture per boundary to preserve rather than reset each ledger.
        for point in ('before_reservation','after_reservation','after_send','before_response_write',
                      'after_response_write','before_settlement'):
            with self.subTest(point=point):
                t=BoundaryTests('test_verified_reservation_and_actual_dispatch_order');t.setUp()
                try:
                    reserve=payer.PrivateLedger.reserve;write=payer.write_private
                    def reserve_fault(ledger,*a,**kw):
                        if point=='before_reservation':raise Crash()
                        row=reserve(ledger,*a,**kw)
                        if point=='after_reservation':raise Crash()
                        return row
                    def write_fault(path,raw):
                        if str(path).endswith('.response.bin') and point=='before_response_write':raise Crash()
                        write(path,raw)
                        if str(path).endswith('.response.bin') and point=='after_response_write':raise Crash()
                    def transport_fault(method,route,body=None,params=None):
                        result=t.base(method,route,body,params)
                        if (method=='POST' and point=='after_send') or (route=='generation' and point=='before_settlement'):raise Crash()
                        return result
                    t.f.send=t.adapter.transport(transport_fault,controlled_transport=True)
                    with patch.object(payer.PrivateLedger,'reserve',reserve_fault),patch.object(payer,'write_private',write_fault),self.assertRaises(Crash):
                        t.run_stage()
                    saved={p.name:p.read_bytes() for p in (t.f.private/'records').iterdir()}
                    count=len(t.base.posts());t.f.send=t.adapter.transport(t.base,controlled_transport=True)
                    t.q.db.close();t.q=w.Queue(t.path);t.addCleanup(t.q.db.close);t.adapter.queue=t.q
                    if point=='before_reservation':
                        t.adapter.release_unreserved_claim('one','worker')
                        self.assertIsNotNone(t.q.claim('worker','one'))
                        self.assertEqual(t.run_stage()['status'],'completed')
                        self.assertEqual(len(t.base.posts()),count+1)
                    else:
                        with self.assertRaises(payer.ProgrammeError):t.run_stage()
                        with self.assertRaises(payer.ProgrammeError):t.adapter.release_unreserved_claim('one','worker')
                        self.assertEqual(len(t.base.posts()),count)
                        self.assertIsNone(t.original()['actual_cost_usd'])
                    self.assertTrue(all((t.f.private/'records'/p).read_bytes()==raw for p,raw in saved.items()))
                finally:t.doCleanups()

    def test_delayed_settlement_appends_proof_no_paid_retry(self):
        self.base.generation_pending=100
        self.assertEqual(self.run_stage()['status'],'stopped')
        first={p:p.read_bytes() for p in (self.f.private/'records').iterdir()}
        original=self.original()
        with self.assertRaises(payer.ProgrammeError):self.adapter.sync_verified()
        self.base.generation_pending=0;self.base.calls.clear()
        result=payer.reconcile_stop(self.f.policy,self.f.manifest,self.f.evidence,self.f.private,
            self.f.keyfile,self.f.repo,transport_fn=self.f.send,captured_pending=True)
        self.assertEqual(result['status'],'stop_resolved_read_only');self.assertEqual(self.base.posts(),[])
        self.assertEqual(self.original(),original)
        self.assertTrue(all(p.read_bytes()==raw for p,raw in first.items()))
        self.adapter.sync_verified()
        self.assertTrue(self.q.snapshot()[0]['result']['billing_verified'])
        with sqlite3.connect(self.f.private/'ledger.sqlite3') as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM attempt_resolutions').fetchone()[0],1)


class QueueResilienceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'queue.sqlite';self.q=w.Queue(self.path);self.addCleanup(self.q.db.close)
        self.s=spec();self.rows=list(w.ordered_jobs(self.s));self.q.add(self.rows)

    def test_parallel_claim_has_single_owner(self):
        barrier=threading.Barrier(8)
        def claim(index):
            queue=w.Queue(self.path)
            try:barrier.wait();return queue.claim(str(index),self.rows[0]['operation_id'])
            finally:queue.db.close()
        with ThreadPoolExecutor(max_workers=8) as pool:
            results=list(pool.map(claim,range(8)))
        self.assertEqual(sum(r is not None for r in results),1)

    def test_duplicate_checkpoint_with_different_event_id(self):
        self.s['id']='new-trigger-spec';self.s['triggers']['opt_in']=True
        event={'id':'first','kind':'checkpoint','checkpoint_identity':'source-hash:node-id'}
        self.assertTrue(self.q.trigger(self.s,event,1))
        self.assertFalse(self.q.trigger(self.s,{**event,'id':'same-checkpoint-new-event'},100))
        reopened=w.Queue(self.path)
        try:self.assertFalse(reopened.trigger(self.s,{**event,'id':'third'},200))
        finally:reopened.db.close()

    def test_same_source_event_can_trigger_separate_opted_in_specs(self):
        first=spec();first['id']='trigger-a';first['triggers']['opt_in']=True
        second=deepcopy(first);second['id']='trigger-b'
        event={'id':'source-event','kind':'checkpoint','checkpoint_identity':'source:checkpoint'}
        self.assertTrue(self.q.trigger(first,event,1))
        self.assertTrue(self.q.trigger(second,event,1))
        self.assertFalse(self.q.trigger(first,event,100))
        self.assertFalse(self.q.trigger(second,event,100))

    def test_legacy_global_event_journal_migrates_without_losing_evidence(self):
        path=Path(self.temp.name)/'legacy.sqlite'
        with sqlite3.connect(path) as db:
            db.execute('CREATE TABLE events(id TEXT PRIMARY KEY,spec TEXT NOT NULL,at REAL NOT NULL)')
            db.execute('INSERT INTO events VALUES(?,?,?)',('source-event','old-spec',1.0))
        old=spec();old['id']='old-spec';old['triggers']['opt_in']=True
        other=deepcopy(old);other['id']='other-spec'
        event={'id':'source-event','kind':'checkpoint'}
        queue=w.Queue(path)
        try:
            self.assertFalse(queue.trigger(old,event,100))
            self.assertTrue(queue.trigger(other,event,100))
            self.assertEqual(queue.db.execute('SELECT * FROM events_legacy_v1').fetchall(),
                             [('source-event','old-spec',1.0)])
        finally:queue.db.close()
        queue=w.Queue(path)
        try:
            self.assertFalse(queue.trigger(other,event,200))
            self.assertEqual(queue.db.execute('SELECT COUNT(*) FROM events').fetchone()[0],2)
        finally:queue.db.close()

    def test_spec_drift_and_batch_rollback(self):
        self.s['repetitions']=3
        with self.assertRaisesRegex(ValueError,'spec_identity_reused'):self.q.add(w.ordered_jobs(self.s))
        self.assertEqual(len(self.q.snapshot()),6)

    def test_cancel_prepared_and_append_evidence(self):
        operation=self.rows[0]['operation_id']
        self.assertEqual(self.q.cancel(operation),'cancelled');self.assertIsNone(self.q.claim('w',operation))
        self.q.append_evidence(operation,'observation',{'value':1});self.q.append_evidence(operation,'observation',{'value':2})
        self.assertEqual([r['payload']['value'] for r in self.q.evidence(operation)],[1,2])

    def test_unknown_and_pending_not_success(self):
        operation=self.rows[0]['operation_id'];self.q.claim('w',operation)
        with self.assertRaisesRegex(ValueError,'unverified'):
            self.q.mark_verified_dispatched(operation,{},lambda *args:{'verified':False})
        self.assertEqual(self.q.snapshot()[0]['result'],None)


class OrderingTests(unittest.TestCase):
    def test_random_permutation_is_bijection(self):
        for size in (1,2,3,5,16,65,1000):
            values=[w._permuted_index(i,size,17) for i in range(size)]
            self.assertEqual(set(values),set(range(size)))
            self.assertEqual(values,[w._permuted_index(i,size,17) for i in range(size)])

    def test_strategies_preserve_request_and_comparison_identity(self):
        s=spec();baseline=list(w.jobs(s))
        for mode in ('blocked','controlled_random','cache_aware'):
            with self.subTest(mode=mode):
                s['ordering']={'mode':mode,'seed':17,'window_size':4}
                rows=list(w.ordered_jobs(s))
                self.assertEqual({(r['comparison_identity'],r['request_sha256']) for r in rows},
                                 {(r['comparison_identity'],r['request_sha256']) for r in baseline})
                self.assertEqual([r['queue_ordinal'] for r in rows],list(range(6)))
                self.assertTrue(all(r['headers']['X-OpenRouter-Cache']=='false' for r in rows))

    def test_large_order_is_lazy(self):
        s=spec();s['variants']={'mode':'matrix','axes':[
            {'name':'model','values':['m/a','m/b']},*[{'name':str(i),'values':list(range(10))} for i in range(20)]]}
        for mode in ('blocked','controlled_random','cache_aware'):
            s['ordering']={'mode':mode,'seed':19,'window_size':16}
            self.assertEqual(len(list(itertools.islice(w.ordered_jobs(s),5))),5)

    def test_response_cache_header_observed_no_live_network(self):
        f=fixtures.ProgrammeRunnerTests();f.setUp();self.addCleanup(f.doCleanups)
        class Response(io.BytesIO):
            headers={'X-OpenRouter-Cache-Status':'HIT'}
            def getcode(self):return 200
        class Opener:
            def open(self,request,timeout):
                self.request=request
                return Response(b'{}')
        opener=Opener()
        with patch.object(payer.transport.urllib.request,'build_opener',return_value=opener):
            transport=boundary.CacheObservedTransport(f.policy['transport'],'fixture-only')
        result=transport.request('POST','chat',b'{}')
        self.assertEqual(result['response_cache_status'],'HIT')
        headers={k.lower():v for k,v in opener.request.header_items()}
        self.assertEqual(headers['x-openrouter-cache'],'false')
        # A request-header echo must not masquerade as an observed response hit.
        Response.headers={'X-OpenRouter-Cache':'HIT'}
        self.assertIsNone(transport.request('POST','chat',b'{}')['response_cache_status'])
        transport.close()


if __name__=='__main__':unittest.main()
