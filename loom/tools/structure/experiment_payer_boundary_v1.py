"""Research queue adapter for the existing programme payer; no second ledger.

This module has no transport implementation and no CLI dispatch. The caller
supplies the existing run_stage transport after its ordinary preflight. Tests
use a controlled transport. Reserved/ambiguous attempts never become retryable
because a worker restarts, a claim ages, or a cancellation is requested.
"""
from __future__ import annotations

from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path
import sqlite3

from . import research_programme_runner as payer
from .experiment_workflow_v1 import canonical, digest, require, strict_json, result_record


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()



class CacheObservedTransport(payer.transport.OpenRouterTransport):
    """Existing transport with forced independent-response policy and observation.

    No retry, credential, timeout, redirect or origin rules are replaced. Only
    allowlisted cache headers are retained; missing header is unknown. Review
    official provider rules again before actual dispatch and freeze that receipt.
    """
    def __init__(self, config, key):
        config = deepcopy(config)
        headers = config.setdefault('headers_by_method',{}).setdefault('POST',{})
        for name in list(headers):
            if name.lower() == 'x-openrouter-cache':
                del headers[name]
        headers['X-OpenRouter-Cache'] = 'false'
        super().__init__(config,key)
        self._observed_cache = None
        class Observe:
            def __init__(proxy,base,owner):
                proxy.base,proxy.owner=base,owner
            def __getattr__(proxy,name):
                return getattr(proxy.base,name)
            def seen(proxy,response):
                headers = getattr(response,'headers',{}) or {}
                values={str(k).lower():str(v) for k,v in headers.items()}
                status=values.get('x-openrouter-cache-status')
                proxy.owner._observed_cache=status.upper() if status is not None else None
                return response
            def open(proxy,*args,**kwargs):
                return proxy.seen(proxy.base.open(*args,**kwargs))
            def request(proxy,*args,**kwargs):
                return proxy.seen(proxy.base.request(*args,**kwargs))
        if self._backend == 'requests_session':
            self._session = Observe(self._session,self)
        else:
            self._opener = Observe(self._opener,self)

    def request(self,method,route_id,body=None,params=None):
        self._observed_cache = None
        result = super().request(method,route_id,body,params)
        result['response_cache_status'] = self._observed_cache
        result['response_cache_disabled'] = method == 'POST'
        return result


class PayerBoundary:
    """Verify PrivateLedger reservation immediately before passing through POST.

    Call inside run_stage's existing exclusive ledger lock. The read-only SQLite
    connection observes committed reservations; it never creates/settles money.
    A reference is a bound lookup key, not a caller assertion of available funds.
    """
    def __init__(self, queue, private_dir, repo_root, programme_id,
                 key_fingerprint_sha256, manifest_sha256):
        self.queue = queue
        self.directory = Path(private_dir).absolute()
        self.repo_root = Path(repo_root).resolve()
        self.programme_id = programme_id
        self.fingerprint = key_fingerprint_sha256
        self.manifest = manifest_sha256
        require(all(isinstance(x,str) and len(x)==64 for x in (self.fingerprint,self.manifest)),
                'payer_binding_hash_required')

    def _rows(self):
        payer.private_path(self.directory, self.repo_root, directory=True)
        path = payer.private_path(self.directory / 'ledger.sqlite3',self.repo_root)
        require(path.is_file(),'existing_payer_ledger_required')
        with sqlite3.connect(path.as_uri() + '?mode=ro', uri=True) as db:
            binding = db.execute('SELECT programme_id,fingerprint FROM binding').fetchall()
            require(binding == [(self.programme_id,self.fingerprint)],'payer_campaign_or_key_mismatch')
            proofs = {operation_id: strict_json(payload) for operation_id, payload in db.execute(
                'SELECT operation_id,payload FROM attempt_resolutions')}
            rows = [strict_json(p) for p, in db.execute('SELECT payload FROM attempts ORDER BY rowid')]
            return [{**row, **proofs[row['operation_id']]['projection']} if row['operation_id'] in proofs else row
                    for row in rows]

    def verify(self, operation_id, reference):
        require(reference == {'programme_id':self.programme_id,'operation_id':operation_id,
                'manifest_sha256':self.manifest,'key_fingerprint_sha256':self.fingerprint},
                'reservation_reference_binding_mismatch')
        rows = [r for r in self._rows() if r['operation_id'] == operation_id]
        require(len(rows)==1,'payer_reservation_missing')
        row = rows[0]
        require(row['state']=='reserved' and row.get('actual_cost_usd') is None,'payer_reservation_not_dispatchable')
        require(row['manifest_sha256']==self.manifest and row['programme_id']==self.programme_id
                and row['key_fingerprint_sha256']==self.fingerprint,'reservation_manifest_or_campaign_mismatch')
        amount = Decimal(row['reservation_usd'])
        require(amount.is_finite() and amount > 0,'payer_reservation_invalid')
        token = _sha(operation_id.encode())
        started_path = payer.private_path(self.directory/'records'/(token+'.started.json'),self.repo_root)
        request_path = payer.private_path(self.directory/'records'/(token+'.request.bin'),self.repo_root)
        started = strict_json(started_path.read_bytes())
        require(started == row,'durable_reservation_witness_changed')
        require(_sha(request_path.read_bytes())==row['request_sha256'],'frozen_payer_request_changed')
        return {'verified':True,'operation_id':operation_id,'request_sha256':row['request_sha256'],
                'manifest_sha256':self.manifest,'started_sha256':_sha(started_path.read_bytes()),
                'authority':'research_programme_runner.PrivateLedger','reservation_state':'reserved'}

    def transport(self, downstream, *, controlled_transport=False):
        """GETs pass through; every POST is bound, journaled, and never retried here."""
        require(controlled_transport or isinstance(getattr(downstream,'__self__',None),CacheObservedTransport),
                'independent_response_transport_required')
        def send(method, route, body=None, params=None):
            if method != 'POST':
                return downstream(method,route,body,params)
            require(isinstance(body,bytes),'exact_request_bytes_required')
            candidates = [r for r in self._rows() if r['state']=='reserved'
                          and r['request_sha256']==_sha(body)]
            require(len(candidates)==1,'ambiguous_or_missing_payer_reservation')
            row = candidates[0]
            require(row['receipt_operation']['route_id']==route,'reservation_route_mismatch')
            reference = {'programme_id':self.programme_id,'operation_id':row['operation_id'],
                         'manifest_sha256':self.manifest,'key_fingerprint_sha256':self.fingerprint}
            self.queue.mark_verified_dispatched(row['operation_id'],reference,self.verify)
            # Journal is committed before send. An exception after this point has
            # unknown provider effects, even if downstream claims no response.
            try:
                response = downstream(method,route,body,params)
                status=response.get('response_cache_status')
                self.queue.append_evidence(row['operation_id'],'transport_response_metadata',
                    {'response_cache_status':status,'raw_sha256':_sha(response['raw']) if isinstance(response.get('raw'),bytes) else None})
                # Leave first bytes intact so the payer captures even an invalid
                # envelope before conservatively marking the attempt uncertain.
                malformed=False
                try:
                    envelope=strict_json(response['raw'])
                    malformed=not isinstance(envelope,dict)
                except Exception:
                    malformed=True
                if malformed or status == 'HIT':
                    response={**response,'transport_error':'response_cache_hit_not_independent' if status=='HIT' else 'malformed_response_no_retry'}
                return response
            except BaseException:
                self.queue.append_evidence(row['operation_id'],'transport_interrupted',
                    {'dispatch_possible':True,'retry_authorized':False,'cost_usd':None})
                raise
        return send

    def sync_verified(self):
        """Project only fully verified terminal payer receipts; no paid transport.

        Pending/uncertain ledger validation raises, preserving the queue state.
        The existing reconcile_stop / resolve_captured_attempt procedures may
        append GET-only proofs. Call this again only after they validate.
        """
        ledger = payer.PrivateLedger(self.directory,self.repo_root,self.programme_id,self.fingerprint)
        with ledger.locked():
            for row in ledger.rows():
                queued = self.queue.db.execute('SELECT payload FROM jobs WHERE id=?',(row['operation_id'],)).fetchone()
                if not queued:
                    continue
                require(row['manifest_sha256']==self.manifest,'sync_manifest_mismatch')
                job = strict_json(queued[0])
                raw = ledger.response_bytes(row)
                # Receipt state/cost is reconstructed by PrivateLedger.validate.
                observations=[x['payload'] for x in self.queue.evidence(row['operation_id'])
                              if x['kind']=='transport_response_metadata']
                cache_status=observations[0].get('response_cache_status') if observations else row.get('response_cache_status')
                require(cache_status!='HIT' and row.get('response_cache_status')!='HIT','cached_response_not_independent')
                require(not (observations and row.get('response_cache_status') is not None and cache_status is not None
                             and row.get('response_cache_status') != cache_status), 'recovered_cache_observation_conflict')
                record = result_record(job,raw,{'observed_model':row.get('model'),
                    'response_cache_status':cache_status,
                    'observed_provider':row.get('provider'),'actual_cost_usd':row['actual_cost_usd'],
                    'generation_id':row.get('generation_id'),'http_status':row.get('http_status'),
                    'latency_seconds':row.get('latency_seconds'),'billing_verified':True,
                    'payer_response_reference':_sha(row['operation_id'].encode())+'.response.bin'})
                self.queue.capture_first(row['operation_id'],record)
                self.queue.append_evidence(row['operation_id'],'payer_verified_settlement',
                    {'attempt_sha256':digest(row),'state':row['state'],
                     'resolution_id':row.get('attempt_resolution_id')})

    def release_unreserved_claim(self, operation_id, worker):
        """Explicit recovery before reservation, under the sole payer's lock.

        No lease timeout releases work. Any reserved/uncertain payer row causes
        validation to fail before this method can alter the scheduling state.
        """
        ledger = payer.PrivateLedger(self.directory,self.repo_root,self.programme_id,self.fingerprint)
        with ledger.locked():
            require(not any(r['operation_id']==operation_id for r in ledger.rows()),'payer_attempt_blocks_requeue')
            self.queue.db.execute('BEGIN IMMEDIATE')
            try:
                claim=self.queue.db.execute('SELECT worker FROM claims WHERE job=?',(operation_id,)).fetchone()
                require(claim==(worker,),'claim_owner_mismatch')
                changed=self.queue.db.execute("UPDATE jobs SET state='prepared' WHERE id=? AND state='claimed'",(operation_id,)).rowcount
                require(changed==1,'claim_not_releasable')
                self.queue.db.execute('DELETE FROM claims WHERE job=?',(operation_id,))
                self.queue.append_evidence(operation_id,'explicit_unreserved_recovery',{'worker':worker,'existing_payer_checked':True})
                self.queue.db.execute('COMMIT')
            except BaseException:
                self.queue.db.execute('ROLLBACK');raise
