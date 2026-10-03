#!/usr/bin/env python3
from copy import deepcopy
from datetime import datetime,timezone
import hashlib,importlib.util,json
from pathlib import Path
import sys,zipfile

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];sys.path.insert(0,str(ROOT));sys.path.insert(0,str(HERE))
import test_packet_watch_03 as fixtures
from loom.tools.structure.agentic_graph_v1 import packet as codec
from loom.tools.structure.agentic_graph_cache_v1 import cache as producer


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    source=HERE/'cache_audited_source_24.py'
    with source.open('xb') as f:f.write(Path(producer.__file__).read_bytes())
    spec=importlib.util.spec_from_file_location('loom.tools.structure.agentic_graph_cache_v1.philosophy_cache_native_snapshot_24',source)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    policy={'schema':'loom.graph_packet_cache_policy/1','mode':'verified_history','max_entries':None,
            'max_retained_bytes':None,'max_entry_bytes':None,'on_capacity':'evict_lru'}
    apply_policy={'schema':'loom.graph_packet_apply_policy/1','acceptance':'auto','allow_source_tombstones':True}
    base=fixtures.packet();original_source=deepcopy(base['sources'][0]);original_claim=deepcopy(base['claims'][0]);codec.validate_packet(base)
    ids=['coupled_tombstone_parent','restore_parent','raw_origin_time_retention','stale_inverse','rehashed_raw_before','valid_last_event_alternative']
    files=[source,Path(codec.__file__),Path(codec.safe.__file__),Path(fixtures.__file__),HERE/'packet_audited_source_03.py',Path(__file__),HERE/'PROTOCOL_24_NATIVE_CACHE_SPOT.md']
    freeze={'schema':'loom.philosophy_watch_native_cache_freeze/1','known_at':datetime.now(timezone.utc).isoformat(),
            'files_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},'case_ids':ids,'input_packet_id':base['packet_id'],'paid_requests':0}
    with (HERE/'NATIVE_CACHE_FREEZE_24.json').open('x') as f:json.dump(freeze,f,indent=2);f.write('\n')
    hide=codec.empty_diff(base,proposal_id='coupled-tombstone',origin=fixtures.MODEL,known_at=fixtures.T2)
    hide['claims']['remove']=[{'id':original_claim['id'],'before_sha256':codec.digest(original_claim),'reason':'hide projection, preserve raw history'}]
    hide['sources']['remove']=[{'id':original_source['observation']['id'],'before_sha256':codec.digest(original_source),'reason':'hide projection, preserve raw history'}]
    hidden,hide_receipt=codec.apply_diff(base,hide,apply_policy)
    restore=codec.empty_diff(hidden,proposal_id='explicit-restore',origin=fixtures.MODEL,known_at=fixtures.T2)
    restore['sources']['add']=[deepcopy(original_source)];restore['claims']['add']=[deepcopy(original_claim)]
    restored,restore_receipt=codec.apply_diff(hidden,restore,apply_policy)
    cache=module.VerifiedPacketCache(policy);cache.validate_packet(base)
    rows=[]
    def capture(ident,fn):
        try:rows.append({'id':ident,'state':'pass','trace':fn()})
        except Exception as exc:rows.append({'id':ident,'state':'failed','exception_type':type(exc).__name__,'exception':str(exc)})
    def reject(fn):
        try:fn()
        except Exception as exc:return {'exception_type':type(exc).__name__,'exception':str(exc)}
        raise AssertionError('expected_rejection_missing')
    def tombstone():
        value,receipt=cache.validate_with_receipt(hidden);assert receipt['path']=='verified_immediate_parent_hit'
        assert value==hidden and not value['claims'] and not value['sources']
        after,applied=codec.apply_diff(cache.validate_packet(base),hide,apply_policy)
        assert codec.safe.canonical(after)==codec.safe.canonical(hidden) and codec.safe.canonical(applied)==codec.safe.canonical(hide_receipt)
        return {'path':receipt['path'],'exact_head_and_application_receipt':True}
    capture(ids[0],tombstone)
    def restoration():
        value,receipt=cache.validate_with_receipt(restored);assert receipt['path']=='verified_immediate_parent_hit'
        assert value['sources'][0]==original_source and value['claims'][0]==original_claim
        assert codec.invert_application(restore_receipt,value)==hidden
        return {'path':receipt['path'],'exact_source_and_claim_restored':True}
    capture(ids[1],restoration)
    def provenance():
        changes={c['collection']:c for c in hidden['history'][-1]['changes']}
        assert changes['sources']['before']==original_source and changes['claims']['before']==original_claim
        assert changes['sources']['before_provenance']==base['provenance']['sources']['ob-1']
        assert restored['sources'][0]['known_at']==fixtures.T1
        assert restored['claims'][0]['assessment']['origin']=='archive' and restored['provenance']['claims']['cl-1']['origin']==fixtures.MODEL
        assert restore_receipt['acceptance_establishes_content_truth'] is False
        return {'raw_utf8_sha256':original_source['text_sha256'],'source_known_at':fixtures.T1,'instrument_origin_separate':True,'acceptance_is_not_content_truth':True}
    capture(ids[2],provenance)
    capture(ids[3],lambda:reject(lambda:codec.invert_application(hide_receipt,cache.validate_packet(restored))))
    bad=deepcopy(hidden);change=next(c for c in bad['history'][-1]['changes'] if c['collection']=='sources')
    change['before']['observation']['text']+=' changed retained raw';change['before']['text_sha256']=hashlib.sha256(change['before']['observation']['text'].encode()).hexdigest()
    change['before_provenance']['record_sha256']=codec.digest(change['before'])
    event=bad['history'][-1];event['application_id']=codec.digest({k:v for k,v in event.items() if k!='application_id'});bad['packet_id']=codec.digest(codec._packet_payload(bad))
    def corrupt():
        strict=reject(lambda:codec.validate_packet(bad));fast=reject(lambda:cache.validate_packet(bad));assert strict==fast;return {'equal_rejection':strict}
    capture(ids[4],corrupt)
    alternative=deepcopy(hide);alternative['proposal_id']='competing-tombstone-proposal';other,other_receipt=codec.apply_diff(base,alternative,apply_policy)
    def fork():
        value,receipt=cache.validate_with_receipt(other);assert value==other and other['packet_id']!=hidden['packet_id'] and receipt['path']=='verified_immediate_parent_hit'
        assert value['history'][-1]['changes']==hidden['history'][-1]['changes'];return {'path':receipt['path'],'alternative_identity_retained':True}
    capture(ids[5],fork)
    with zipfile.ZipFile(HERE/'native_cache_first_evidence_24.zip','x',compression=zipfile.ZIP_DEFLATED) as z:
        for name,value in [('base',base),('hide_diff',hide),('hidden',hidden),('hide_receipt',hide_receipt),('restore_diff',restore),('restored',restored),('restore_receipt',restore_receipt),('bad',bad),('alternative',other),('alternative_receipt',other_receipt)]:z.writestr(name+'.json',codec.safe.canonical(value))
    report={'schema':'loom.philosophy_watch_native_cache_results/1','known_at':datetime.now(timezone.utc).isoformat(),
            'checks':rows,'passed':sum(r['state']=='pass' for r in rows),'failed':sum(r['state']=='failed' for r in rows),'paid_requests':0,
            'scope':'nonempty native structural/origin equivalence, not source semantics or timing'}
    with (HERE/'NATIVE_CACHE_FIRST_RESULTS_24.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report))


if __name__=='__main__':main()
