#!/usr/bin/env python3
from copy import deepcopy
from datetime import datetime,timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT))
from loom.tools.structure.agentic_graph_v1 import packet as codec
from loom.tools.structure.agentic_graph_cache_v1 import cache as producer


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    source=HERE/'cache_audited_source_18.py'
    with source.open('xb') as f:f.write(Path(producer.__file__).read_bytes())
    spec=importlib.util.spec_from_file_location('loom.tools.structure.agentic_graph_cache_v1.philosophy_cache_snapshot_18',source)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    origin={'kind':'user','actor':'authored-watch','model':None,'recipe_sha256':None,'response_sha256':None}
    def policy(mode='verified_history',**changes):return {'schema':'loom.graph_packet_cache_policy/1','mode':mode,
        'max_entries':None,'max_retained_bytes':None,'max_entry_bytes':None,'on_capacity':'evict_lru',**changes}
    def heads(count,prefix='one'):
        values=[codec.make_packet(origin=origin,task={'source':'authored native fixture'})]
        for i in range(count):
            diff=codec.empty_diff(values[-1],proposal_id=prefix+str(i),origin=origin,known_at='2026-09-01T10:00:00Z')
            current,_=codec.apply_diff(values[-1],diff,{'schema':'loom.graph_packet_apply_policy/1','acceptance':'auto','allow_source_tombstones':False})
            values.append(current)
        return values
    rows=[]
    def capture(ident,fn):
        try:rows.append({'id':ident,'state':'pass','trace':fn()})
        except Exception as exc:rows.append({'id':ident,'state':'failed','exception_type':type(exc).__name__,'exception':str(exc)})
    def rejection(fn):
        try:fn()
        except Exception as exc:return {'exception_type':type(exc).__name__,'exception':str(exc)}
        raise AssertionError('expected_rejection_missing')
    cases=['whole_hit_exact_application','verified_parent_extension','changed_old_prefix','invalid_old_history',
           'mutable_callers','foreign_entry','public_receipt','resources_on_hit','policy_and_limits_change',
           'zero_storage','large_unlimited_input','selected_function_binding','transitive_history_binding']
    files=[source,Path(codec.__file__),Path(codec.safe.__file__),Path(__file__),HERE/'PROTOCOL_18_PACKET_CACHE_RETEST.md']
    freeze={'schema':'loom.philosophy_watch_cache_freeze/1','frozen_at_utc':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},'case_ids':cases,'paid_requests':0}
    with (HERE/'PACKET_CACHE_FREEZE_18.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    chain=heads(4)
    def whole():
        original=chain[-1];diff=codec.empty_diff(original,proposal_id='later',origin=origin,known_at='2026-09-02T10:00:00Z')
        p={'schema':'loom.graph_packet_apply_policy/1','acceptance':'auto','allow_source_tombstones':False}
        after,receipt=codec.apply_diff(original,diff,p)
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(original)
        copied,cache_receipt=cache.validate_with_receipt(original);a,r=codec.apply_diff(copied,diff,p)
        assert codec.safe.canonical(a)==codec.safe.canonical(after) and codec.safe.canonical(r)==codec.safe.canonical(receipt)
        assert cache_receipt['path']=='whole_packet_hit';return {'exact_graph_and_application_receipt_bytes':True}
    capture(cases[0],whole)
    def parent():
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(chain[-2]);out,r=cache.validate_with_receipt(chain[-1])
        assert out==chain[-1] and r['path']=='verified_immediate_parent_hit';return {'path':r['path']}
    capture(cases[1],parent)
    def fork():
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(chain[-1]);other=heads(4,'different')[-1]
        out,r=cache.validate_with_receipt(other);assert out==other and r['path']=='strict_fallback';return {'path':r['path']}
    capture(cases[2],fork)
    def bad_history():
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(chain[-1]);bad=deepcopy(chain[-1]);bad['history'][0]['diff']['schema']='invented'
        bad['history'][0]['application_id']=codec.digest({k:v for k,v in bad['history'][0].items() if k!='application_id'})
        bad['packet_id']=codec.digest(codec._packet_payload(bad));a=rejection(lambda:codec.validate_packet(bad));b=rejection(lambda:cache.validate_packet(bad))
        assert a==b;return {'strict_and_cache_rejection':a}
    capture(cases[3],bad_history)
    def mutability():
        original=chain[0];caller=deepcopy(original);cache=module.VerifiedPacketCache(policy());result=cache.validate_packet(caller)
        result['task']['source']='changed returned';caller['task']['source']='changed caller'
        assert cache.validate_packet(original)==original;rejection(lambda:cache.validate_packet(caller));return {'private_bytes_retained':True}
    capture(cases[4],mutability)
    def foreign():
        one=module.VerifiedPacketCache(policy());two=module.VerifiedPacketCache(policy());one.validate_packet(chain[0]);key,entry=next(iter(one._entries.items()));two._entries[key]=entry
        result=rejection(lambda:two.validate_packet(chain[0]));assert result['exception_type']=='CacheIntegrityError';return result
    capture(cases[5],foreign)
    def public():
        cache=module.VerifiedPacketCache(policy());_,receipt=cache.validate_with_receipt(chain[0]);return rejection(lambda:cache.validate_packet(receipt))
    capture(cases[6],public)
    def resources():
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(chain[0]);a=rejection(lambda:codec.validate_packet(chain[0],resource_limits={'max_nodes':2}));b=rejection(lambda:cache.validate_packet(chain[0],resource_limits={'max_nodes':2}));assert a==b;return b
    capture(cases[7],resources)
    def reconfigured():
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(chain[0]);_,r=cache.validate_with_receipt(chain[0],resource_limits={});assert r['path']=='strict_fallback'
        cache.reconfigure(policy('whole_packet'));assert cache.stats()['entries']==0;_,r=cache.validate_with_receipt(chain[0]);assert r['path']=='strict_fallback';return {'both_changes_miss':True}
    capture(cases[8],reconfigured)
    def zero():
        cache=module.VerifiedPacketCache(policy(max_entries=0));assert cache.validate_packet(chain[-1])==chain[-1];assert cache.stats()['entries']==0;return {'valid_without_storage':True}
    capture(cases[9],zero)
    def large():
        value=deepcopy(chain[0]);value['task']['archive']='x'*(17*1024*1024);value['packet_id']=codec.digest(codec._packet_payload(value));codec.validate_packet(value)
        cache=module.VerifiedPacketCache(policy());assert cache.validate_packet(value)==value;return {'bytes':len(codec.safe.canonical(value)),'entries':cache.stats()['entries']}
    capture(cases[10],large)
    def selected_binding():
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(chain[0]);original=codec._validate_packet_content
        try:
            codec._validate_packet_content=lambda packet:None
            result=rejection(lambda:cache.validate_packet(chain[0]));assert result['exception_type']=='CacheIntegrityError';return result
        finally:codec._validate_packet_content=original
    capture(cases[11],selected_binding)
    def transitive():
        cache=module.VerifiedPacketCache(policy());cache.validate_packet(chain[-1]);original=codec._validate_history
        try:
            def changed(packet):raise ValueError('authored_changed_history_validator')
            codec._validate_history=changed
            strict=rejection(lambda:codec.validate_packet(chain[-1]))
            try:_,receipt=cache.validate_with_receipt(chain[-1])
            except module.CacheIntegrityError:return {'binding_drift_rejected':True,'strict':strict}
            raise AssertionError('transitive_validator_rebound_but_cache_returned_'+receipt['path'])
        finally:codec._validate_history=original
    capture(cases[12],transitive)
    report={'schema':'loom.philosophy_watch_cache_results/1','finished_at_utc':datetime.now(timezone.utc).isoformat(),
            'planned':len(rows),'passed':sum(r['state']=='pass' for r in rows),'failed':sum(r['state']=='failed' for r in rows),
            'checks':rows,'paid_requests':0,'scope':'authored strict/cache equivalence, not CPU or semantic-quality benchmark'}
    with (HERE/'PACKET_CACHE_FIRST_RESULTS_18.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report))


if __name__=='__main__':main()
