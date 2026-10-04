"""Offline native GraphPacket differential tests; synthetic data only.

Reply expectations are pinned Python compiler output in src/packet/tests.
No provider call and no private archives are used.
"""
import copy
import ctypes
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

REPO=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(REPO/'loom/tools/structure'))
from agentic_graph_v1 import packet as graph

ORIGIN={'kind':'user','actor':'synthetic-test','model':None,'recipe_sha256':None,'response_sha256':None}
POLICY={'schema':'loom.graph_packet_apply_policy/1','acceptance':'auto','allow_source_tombstones':False}

class PacketTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path=os.environ.get('LOOM_LIBRARY')
        if not path: raise RuntimeError('LOOM_LIBRARY required: native tests must execute')
        cls.lib=ctypes.CDLL(path)
        cls.lib.loom_init_ex.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p)]
        cls.lib.loom_init_ex.restype=ctypes.c_void_p
        cls.lib.loom_shutdown.argtypes=[ctypes.c_void_p]
        cls.lib.loom_packet.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
        cls.lib.loom_packet.restype=ctypes.c_void_p
        cls.lib.loom_free_string.argtypes=[ctypes.c_void_p]
        cls.tmp=tempfile.TemporaryDirectory()
        err=ctypes.c_void_p()
        cls.ctx=cls.lib.loom_init_ex(json.dumps({'data_dir':cls.tmp.name,'start_workers':False}).encode(),ctypes.byref(err))
        if not cls.ctx: raise AssertionError(ctypes.string_at(err.value))
        cls.fixtures=json.loads((REPO/'loom/src/packet/tests/reply-fixtures.json').read_text())['cases']

    @classmethod
    def tearDownClass(cls):
        cls.lib.loom_shutdown(cls.ctx);cls.tmp.cleanup()

    def invoke(self,r):
        raw=r if isinstance(r,bytes) else json.dumps(r,ensure_ascii=False).encode()
        ptr=self.lib.loom_packet(self.ctx,raw)
        self.assertTrue(ptr)
        try: return json.loads(ctypes.string_at(ptr))
        finally: self.lib.loom_free_string(ptr)

    def ok(self,r):
        value=self.invoke(r);self.assertNotIn('error',value,value);return value

    def packet(self): return graph.make_packet(origin=ORIGIN)

    def test_empty_and_validation(self):
        p=self.packet()
        self.assertEqual(self.ok({'operation':'validate','packet':p}),p)
        d=graph.empty_diff(p,proposal_id='d',origin=ORIGIN)
        self.assertEqual(self.ok({'operation':'empty_diff','packet':p,'proposal_id':'d','origin':ORIGIN}),d)

    def test_codec_make_and_capabilities(self):
        p=self.packet()
        self.assertEqual(self.ok({'operation':'make','origin':ORIGIN}),p)
        encoded=self.ok({'operation':'encode','packet':p})
        self.assertEqual(encoded.encode(),graph.encode_packet(p))
        self.assertEqual(self.ok({'operation':'decode','raw':encoded}),p)
        self.assertIn('compile_reply',self.ok({'operation':'capabilities'})['operations'])

    def test_invalid_utf8_is_captured_through_base64(self):
        import base64
        base={k:v for k,v in self.fixtures[0]['request'].items() if k!='raw'}
        raw=b'\xff\x00first response'
        result=self.invoke({**base,'raw_base64':base64.b64encode(raw).decode()})
        self.assertIn('error',result)
        self.assertEqual(base64.b64decode(result['error']['raw_capture']['raw_base64']),raw)
        self.assertEqual(result['error']['raw_capture']['byte_len'],len(raw))
        self.assertIn('error',self.invoke({**base,'raw':'x','raw_base64':'eA=='}))

    def test_apply_preview_history_and_inverse(self):
        p=self.packet()
        for i in range(5):
            d=graph.empty_diff(p,proposal_id=f'd{i}',origin=ORIGIN)
            d['task']={'before_sha256':graph.digest(p['task']),'after':{'step':i,'unicode':'ą🙂','value':1.0}}
            expected,receipt=graph.apply_diff(p,d,POLICY)
            result=self.ok({'operation':'apply','packet':p,'diff':d,'policy':POLICY})
            self.assertEqual(result,{'packet':expected,'receipt':receipt})
            self.assertEqual(self.ok({'operation':'preview','packet':p,'diff':d}),graph.preview_diff(p,d))
            self.assertEqual(self.ok({'operation':'invert','packet':expected,'receipt':receipt}),p)
            p=expected
        self.assertEqual(self.ok({'operation':'validate','packet':p}),p)

    def test_reply_reference_compilation(self):
        for fixture in self.fixtures:
            with self.subTest(schema=json.loads(fixture['request']['raw'])['schema']):
                self.assertEqual(self.ok(fixture['request']),fixture['expected'])
                c=fixture['expected'];p=fixture['request']['packet']
                self.assertEqual(self.ok({'operation':'validate_compilation','packet':p,'compilation':c}),c)
                expected,receipt=graph.apply_diff(p,c['diff'],POLICY)
                self.assertEqual(self.ok({'operation':'apply_compiled_reply','packet':p,'compilation':c,'policy':POLICY}),{'packet':expected,'receipt':receipt})

    def test_record_edits_order_and_tombstones(self):
        p=self.fixtures[0]['request']['packet'];d=self.fixtures[0]['expected']['diff']
        p=graph.apply_diff(p,d,POLICY)[0]
        # Update derived labels without changing identity, preserve source bytes.
        d=graph.empty_diff(p,proposal_id='rename',origin=ORIGIN)
        e=copy.deepcopy(p['entities'][1]);e['label']='Updated label'
        d['entities']['update']=[{'id':e['id'],'before_sha256':graph.digest(p['entities'][1]),'after':e}]
        expected,receipt=graph.apply_diff(p,d,POLICY)
        result=self.ok({'operation':'apply','packet':p,'diff':d,'policy':POLICY})
        self.assertEqual(result,{'packet':expected,'receipt':receipt})
        self.assertEqual(self.ok({'operation':'invert','packet':expected,'receipt':receipt}),p)
        d=graph.empty_diff(expected,proposal_id='remove-all',origin=ORIGIN)
        for name in graph.COLLECTIONS:
            d[name]['remove']=[{'id':graph.record_id(name,r),'before_sha256':graph.digest(r),'reason':'synthetic projection cleanup'} for r in expected[name]]
        policy={**POLICY,'allow_source_tombstones':True}
        after,receipt=graph.apply_diff(expected,d,policy)
        self.assertEqual(self.ok({'operation':'apply','packet':expected,'diff':d,'policy':policy}),{'packet':after,'receipt':receipt})
        self.assertEqual(self.ok({'operation':'invert','packet':after,'receipt':receipt}),expected)
        self.assertIn('error',self.invoke({'operation':'apply','packet':expected,'diff':d,'policy':POLICY}))

    def test_rehashed_history_forgery_is_rejected(self):
        p=self.packet();d=graph.empty_diff(p,proposal_id='d',origin=ORIGIN)
        d['task']={'before_sha256':graph.digest(p['task']),'after':{'a':1}}
        p=graph.apply_diff(p,d,POLICY)[0]
        for field in ['previous_task','previous_order','changes','diff']:
            bad=copy.deepcopy(p)
            if field=='previous_task':bad['history'][0][field]={'forged':True}
            elif field=='previous_order':bad['history'][0][field]['entities']=['missing']
            elif field=='changes':bad['history'][0][field]=[{'collection':'entities'}]
            else:bad['history'][0]['diff']['task']['after']={'a':2}
            event=bad['history'][0];event['application_id']=graph.digest({k:v for k,v in event.items() if k!='application_id'})
            bad['packet_id']=graph.digest({k:v for k,v in bad.items() if k!='packet_id'})
            self.assertIn('error',self.invoke({'operation':'validate','packet':bad}))

    def test_invalid_wire_captures_first_bytes(self):
        base=self.fixtures[0]['request']
        reply=json.loads(base['raw'])
        variants=['not json','{"duplicate":0,"duplicate":1}','\ufeff'+base['raw']]
        for mutation in ['partition','unknown','duplicate','stale','disconnected','root_parent','null_leaf']:
            r=copy.deepcopy(reply)
            if mutation=='partition':r['nodes'][0]['text']='wrong'
            elif mutation=='unknown':r['nodes'][0]['children']=['missing']
            elif mutation=='duplicate':r['nodes'].append(r['nodes'][0])
            elif mutation=='stale':r['base_packet_sha256']='0'*64
            elif mutation=='disconnected':r['nodes'][0]['children']=[]
            elif mutation=='root_parent':r['nodes'][1]['children']=['root']
            else:r['schema']='loom.graph_reply/2';r['nodes'][1]['text']=None
            variants.append(json.dumps(r))
        for raw in variants:
            value=self.invoke({**base,'raw':raw})
            self.assertIn('error',value,raw)
            self.assertEqual(value['error']['raw_capture']['sha256'],graph.hashlib.sha256(raw.encode()).hexdigest())
            import base64
            self.assertEqual(base64.b64decode(value['error']['raw_capture']['raw_base64']),raw.encode())

    def test_compilation_replay_rejects_rehashed_changes(self):
        p=self.fixtures[0]['request']['packet'];c=copy.deepcopy(self.fixtures[0]['expected'])
        c['response_text']='forged';c['compilation_sha256']=graph.digest({k:v for k,v in c.items() if k!='compilation_sha256'})
        self.assertIn('error',self.invoke({'operation':'validate_compilation','packet':p,'compilation':c}))

    def test_duplicate_outer_keys_and_resource_policy(self):
        self.assertIn('error',self.invoke(b'{"operation":"capture","raw":"a","raw":"b"}'))
        p=self.packet()
        self.assertIn('error',self.invoke({'operation':'validate','packet':p,'resource_limits':{'max_nodes':2}}))
        self.assertEqual(self.ok({'operation':'validate','packet':p,'resource_limits':{'max_nodes':None,'max_depth':None}}),p)
        self.assertIn('error',self.invoke({'operation':'validate','packet':p,'resource_limits':{'max_depth':0}}))

    def test_preview_is_not_acceptance(self):
        p=self.packet();d=graph.empty_diff(p,proposal_id='d',origin=ORIGIN)
        d['task']={'before_sha256':graph.digest(p['task']),'after':{'x':True}}
        policy={**POLICY,'acceptance':'preview'}
        expected,receipt=graph.apply_diff(p,d,policy)
        self.assertEqual(self.ok({'operation':'apply','packet':p,'diff':d,'policy':policy}),{'packet':expected,'receipt':receipt})
        self.assertFalse(receipt['accepted']);self.assertEqual(expected,p)
        self.assertEqual(self.ok({'operation':'apply','packet':p,'diff':d,'policy':policy,'explicitly_accepted':True})['packet'],graph.apply_diff(p,d,policy,explicitly_accepted=True)[0])

if __name__=='__main__': unittest.main()
