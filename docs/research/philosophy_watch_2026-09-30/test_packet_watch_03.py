"""Independent new mechanism fixtures against a pinned producer source snapshot."""
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
SNAPSHOT = HERE / 'packet_audited_source_03.py'
spec = importlib.util.spec_from_file_location('loom.tools.structure.agentic_graph_v1.philosophy_packet_snapshot_03', SNAPSHOT)
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
T1 = '2026-09-01T10:00:00Z'
T2 = '2026-09-02T10:00:00Z'
ORIGIN = {'kind': 'recorded', 'actor': 'authored-mechanism', 'model': None,
          'recipe_sha256': None, 'response_sha256': None}
MODEL = {'kind': 'model', 'actor': 'scripted-frontier', 'model': 'authored/any-model',
         'recipe_sha256': 'a'*64, 'response_sha256': 'b'*64}


def locator():
    return {'source': 'authored-source-sha', 'member': 'conversation.json', 'json_pointer': '/turns/0/text',
            'byte_start': 0, 'byte_len': len('Jeśli A, to B. Żółć 🧪.'.encode()), 'time_start': None, 'time_end': None, 'line': 1}


def source():
    text = 'Jeśli A, to B. Żółć 🧪.'
    return {'observation': {'id': 'ob-1', 'unit': 'unit-1', 'kind': 'utterance', 'text': text,
        'locator': locator(), 'lang': 'pl', 'date': T1, 'ordinal': 0, 'artifact_type': 'conversation',
        'speaker': 'Ada', 'attrs': {'branch': 'main'}}, 'known_at': T1,
        'text_sha256': hashlib.sha256(text.encode()).hexdigest()}


def entity(ident):
    return {'id': ident, 'kind': 'proposition', 'canonical_key': ident, 'label': ident,
        'labels': {'pl': ident}, 'aliases': [], 'parent': '', 'first_seen': T1, 'last_seen': T1,
        'evidence_class': 'observed', 'origin': 'archive', 'confidence': 0.3, 'status': 'active', 'attrs': {}}


def claim(ident='cl-1'):
    return {'id': ident, 'subject': 'e-a', 'predicate': 'implies', 'object': 'e-b', 'value': None,
        'qualifiers': {'valid_from': '', 'valid_to': '', 'version': '', 'branch': 'main', 'scope': 'Ada', 'lang': 'pl', 'extra': {}},
        'assessment': {'basis': {'support': [{'observation': 'ob-1', 'locator': locator(),
            'quote': source()['observation']['text'], 'extractor': 'authored-mechanism@1', 'quality': 1.0}], 'derivation': None},
            'evidence_class': 'observed', 'origin': 'archive', 'confidence': 0.3,
            'premises': {'claims': [], 'principles': [], 'assumptions': []},
            'counter': {'observations': [], 'claims': []}, 'status': 'active',
            'consequences': {'claims': [], 'predictions': [], 'checks': []},
            'open': {'slots': [], 'questions': [], 'fill_query': None}, 'expected_property': None,
            'check_state': 'n/a', 'alternatives': []}}


def packet(*, with_claim=True):
    return p.make_packet(entities=[entity('e-a'), entity('e-b')], claims=[claim()] if with_claim else [],
                         sources=[source()], task={'operation': 'whole-archive-analysis'}, origin=ORIGIN, known_at=T1)


def policy(acceptance='auto', tombstones=False):
    return {'schema': 'loom.graph_packet_apply_policy/1', 'acceptance': acceptance, 'allow_source_tombstones': tombstones}


def diff(base):
    return p.empty_diff(base, proposal_id='scripted-proposal-1', origin=MODEL, known_at=T2)


def stamp(value):
    value['packet_id'] = p.digest({k:v for k,v in value.items() if k != 'packet_id'})
    return value


class PacketWatchTests(unittest.TestCase):
    def test_native_roundtrip_exact_unicode_and_fields(self):
        base=packet(); decoded=p.decode_packet(p.encode_packet(base))
        self.assertEqual(decoded,base)
        self.assertEqual(decoded['claims'][0],claim())
        self.assertEqual(decoded['sources'][0],source())

    def test_make_packet_does_not_mutate_input_records(self):
        raw=claim(); before=deepcopy(raw)
        p.make_packet(entities=[entity('e-a'),entity('e-b')],claims=[raw],sources=[source()],origin=ORIGIN)
        self.assertEqual(raw,before)

    def test_no_op_preview_does_not_change_supplied_base(self):
        base=packet(); before=deepcopy(base); preview=p.preview_diff(base,diff(base))
        self.assertEqual(base,before)
        self.assertFalse(preview['canonical_store_written'])
        self.assertFalse(preview['acceptance_establishes_content_truth'])

    def test_automatic_acceptance_needs_no_extra_review(self):
        base=packet(); proposal=diff(base)
        proposal['task']={'before_sha256':p.digest(base['task']),'after':{'scope':'whole_archive','reasoning':{'effort':'high'}}}
        applied,receipt=p.apply_diff(base,proposal,policy())
        self.assertTrue(receipt['accepted']); self.assertFalse(receipt['explicitly_accepted'])
        self.assertEqual(applied['task']['scope'],'whole_archive')

    def test_review_preset_can_be_explicitly_accepted(self):
        base=packet(); proposal=diff(base)
        applied,receipt=p.apply_diff(base,proposal,policy('preview'),explicitly_accepted=True)
        self.assertTrue(receipt['accepted']); self.assertNotEqual(applied['packet_id'],base['packet_id'])

    def test_preview_preset_keeps_exact_projection(self):
        base=packet(); applied,receipt=p.apply_diff(base,diff(base),policy('preview'))
        self.assertEqual(applied,base); self.assertFalse(receipt['accepted'])

    def test_acceptance_does_not_rewrite_assessment_or_source_origin(self):
        base=packet(); proposal=diff(base); after=deepcopy(base['claims'][0]); after['assessment']['status']='contested'
        proposal['claims']['update']=[{'id':'cl-1','before_sha256':p.digest(base['claims'][0]),'after':after}]
        applied,receipt=p.apply_diff(base,proposal,policy())
        self.assertEqual(applied['claims'][0]['assessment']['origin'],'archive')
        self.assertEqual(applied['provenance']['claims']['cl-1']['origin'],MODEL)
        self.assertEqual(applied['provenance']['claims']['cl-1']['known_at'],T2)
        self.assertFalse(receipt['acceptance_establishes_content_truth'])
        self.assertEqual(applied['sources'],base['sources'])

    def test_inverse_restores_exact_head_and_retains_receipt_bytes(self):
        base=packet(); applied,receipt=p.apply_diff(base,diff(base),policy()); saved=deepcopy(receipt)
        self.assertEqual(p.invert_application(receipt,applied),base)
        self.assertEqual(receipt,saved)

    def test_inverse_rejects_noncurrent_head(self):
        base=packet(); applied,receipt=p.apply_diff(base,diff(base),policy())
        later,_=p.apply_diff(applied,diff(applied),policy())
        with self.assertRaises(ValueError): p.invert_application(receipt,later)

    def test_stale_base_rejected(self):
        base=packet(); proposal=diff(base); proposal['base_packet_sha256']='f'*64
        with self.assertRaises(ValueError): p.preview_diff(base,proposal)

    def test_compare_and_swap_cannot_overwrite_other_record(self):
        base=packet(); proposal=diff(base)
        proposal['entities']['update']=[{'id':'e-a','before_sha256':'0'*64,'after':entity('e-a')}]
        with self.assertRaises(ValueError): p.preview_diff(base,proposal)

    def test_raw_source_same_id_cannot_change_bytes(self):
        base=packet(); proposal=diff(base); after=deepcopy(source()); after['observation']['text']='Different source'
        after['text_sha256']=hashlib.sha256(after['observation']['text'].encode()).hexdigest()
        proposal['sources']['update']=[{'id':'ob-1','before_sha256':p.digest(source()),'after':after}]
        with self.assertRaises(ValueError): p.preview_diff(base,proposal)

    def test_raw_source_new_id_is_allowed_and_old_retained(self):
        base=packet(); proposal=diff(base); after=deepcopy(source()); after['observation']['id']='ob-2';after['known_at']=T2
        proposal['sources']['add']=[after]
        applied,_=p.apply_diff(base,proposal,policy())
        self.assertEqual(applied['sources'][0],source());self.assertEqual(len(applied['sources']),2)

    def test_source_tombstone_is_policy_choice_and_retains_history(self):
        base=packet(with_claim=False); proposal=diff(base)
        proposal['sources']['remove']=[{'id':'ob-1','before_sha256':p.digest(source()),'reason':'hide projection, retain bytes'}]
        with self.assertRaises(ValueError): p.apply_diff(base,proposal,policy(tombstones=False))
        applied,receipt=p.apply_diff(base,proposal,policy(tombstones=True))
        self.assertEqual(applied['sources'],[])
        self.assertEqual(applied['history'][-1]['changes'][0]['before'],source())
        self.assertEqual(p.invert_application(receipt,applied),base)

    def test_source_remove_cannot_leave_dangling_claim_support(self):
        base=packet(); proposal=diff(base)
        proposal['sources']['remove']=[{'id':'ob-1','before_sha256':p.digest(source()),'reason':'hide'}]
        with self.assertRaises(ValueError):p.apply_diff(base,proposal,policy(tombstones=True))

    def test_claim_content_change_needs_new_id(self):
        base=packet(); proposal=diff(base); after=deepcopy(claim());after['subject'],after['object']='e-b','e-a'
        proposal['claims']['update']=[{'id':'cl-1','before_sha256':p.digest(claim()),'after':after}]
        with self.assertRaises(ValueError):p.preview_diff(base,proposal)

    def test_entity_identity_change_needs_new_id(self):
        base=packet(); proposal=diff(base); after=entity('e-a');after['canonical_key']='different'
        proposal['entities']['update']=[{'id':'e-a','before_sha256':p.digest(entity('e-a')),'after':after}]
        with self.assertRaises(ValueError):p.preview_diff(base,proposal)

    def test_observed_support_required_but_worldtruth_not_certified(self):
        c=claim();c['assessment']['basis']['support']=[]
        with self.assertRaises(ValueError):p.validate_claim(c)
        base=packet(); proposal=diff(base); c=claim('cl-reverse');c['subject'],c['object']='e-b','e-a'
        proposal['claims']['add']=[c]
        _,receipt=p.apply_diff(base,proposal,policy())
        self.assertFalse(receipt['acceptance_establishes_content_truth'])

    def test_dangling_premise_rejected(self):
        c=claim();c['assessment']['premises']['claims']=['not-in-packet']
        with self.assertRaises(ValueError):p.make_packet(entities=[entity('e-a'),entity('e-b')],claims=[c],sources=[source()],origin=ORIGIN)

    def test_provenance_hash_drift_rejected_even_with_recomputed_packet_hash(self):
        base=packet();base['provenance']['claims']['cl-1']['record_sha256']='0'*64;stamp(base)
        with self.assertRaises(ValueError):p.validate_packet(base)

    def test_unrestricted_graph_resources_do_not_inherit_16mib_transport_cap(self):
        base=packet();base['task']['archive_excerpt']='x'*(17*1024*1024);stamp(base)
        p.validate_packet(base)
        with self.assertRaises(ValueError):p.validate_packet(base,resource_limits={'max_string_bytes':1024})

    def test_user_optional_depth_and_count_limits_are_editable(self):
        node='leaf'
        for _ in range(70):node=[node]
        p.validate_json_resources(node)
        with self.assertRaises(ValueError):p.validate_json_resources(node,{'max_depth':64})
        p.validate_json_resources(node,{'max_depth':128})
        p.validate_json_resources(['x']*210000)
        with self.assertRaises(ValueError):p.validate_json_resources(['x']*210000,{'max_nodes':200000})

    def test_large_budget_model_and_reasoning_are_open_task_data(self):
        base=packet();base['task'].update(budget_usd=1000000000,model='new/provider-model',scope='whole_archive',reasoning={'new_parameter':1000000});stamp(base)
        p.validate_packet(base)

    def test_new_definition_kind_and_operation_are_open_data(self):
        base=packet();proposal=diff(base)
        proposal['definitions']['add']=[{'id':'def-new','kind':'new_nary_reasoning_structure','description':'hypothesis only','examples':[],'origin':MODEL,'attrs':{}}]
        proposal['annotations']['operations']=[{'kind':'invent_unseen_structure','inputs':['e-a'],'outputs':['def-new'],'reason':'scripted proposal'}]
        applied,_=p.apply_diff(base,proposal,policy())
        self.assertEqual(applied['definitions'][0]['kind'],'new_nary_reasoning_structure')

    def test_model_instrument_needs_model_identity(self):
        bad=deepcopy(MODEL);bad['model']=None
        with self.assertRaises(ValueError):p.validate_origin(bad)

    def test_native_nan_and_boolean_confidence_rejected(self):
        for value in (float('nan'),float('inf'),True,-0.1,1.1):
            c=claim();c['assessment']['confidence']=value
            with self.assertRaises(ValueError):p.validate_claim(c)

    def test_unverified_imported_history_shape_is_not_certified_by_hash(self):
        base=packet()
        event={'application_id':'', 'base_packet_id':'arbitrary', 'diff':{'schema':'garbage'},
               'changes':[{}], 'previous_task':{}, 'result_origin':MODEL}
        event['application_id']=p.digest({k:v for k,v in event.items() if k!='application_id'})
        base['history'].append(event);stamp(base)
        with self.assertRaises(ValueError):p.validate_packet(base)


if __name__=='__main__':
    unittest.main()
