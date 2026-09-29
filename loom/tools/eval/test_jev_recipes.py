"""Offline preparation guards. Fabricated response objects only; no model quality measured."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).parent))
import jev_recipes as m

class Recipes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases=m.load(m.FIXTURE/'cases.json')['cases']
        cls.gold=m.load(m.FIXTURE/'gold.json')['cases']
        cls.trials=m.build(cls.cases)
    def trial(self,cid,arm):
        return next(t for t in self.trials if t['trial_id']==cid+'.'+arm)
    def test_counts(self):
        self.assertEqual(len(self.cases),32)
        self.assertEqual(sum(c['language']=='pl' for c in self.cases),16)
        self.assertEqual(len(self.trials),188)
        self.assertEqual(sum(t['execute_if'] is not None for t in self.trials),16)
    def test_ids_paths(self):
        d=copy.deepcopy(self.cases);d[0]['id']='../wrong'
        with self.assertRaises(ValueError): m.build(d)
    def test_duplicates(self):
        with self.assertRaises(ValueError):m.build(self.cases+[self.cases[0]])
    def test_bad_recipe(self):
        with self.assertRaises(ValueError):m.instruction('q','undefined')
    def test_bad_question_shape(self):
        d=copy.deepcopy(self.trials[0]['body']);d['questions']['x']=1
        with self.assertRaises(ValueError):m.validate_body(d)
    def test_all_contracts(self):
        for t in self.trials:
            m.validate_body(t['body']);self.assertEqual(m.digest(t['body']),t['body_sha256'])
    def test_gold_has_same_cases(self):
        self.assertEqual(set(self.gold),{c['id'] for c in self.cases})
    def test_gold_not_opened_by_builder(self):
        with patch('builtins.open',side_effect=AssertionError('no read')):
            self.assertEqual(m.build(self.cases),self.trials)
    def test_no_gold_or_management_fields_in_body(self):
        forbidden={'gold','rationale','case_id','family','language','arm','body_sha256','execute_if'}
        def walk(x):
            if isinstance(x,dict):
                self.assertFalse(forbidden & set(x))
                for v in x.values():walk(v)
            elif isinstance(x,list):
                for v in x:walk(v)
        for t in self.trials:walk(t['body'])
    def test_key_variants_keep_leaves_and_order(self):
        for c in self.cases:
            if c['family']=='choice':continue
            base=self.trial(c['id'],'object_meaningful')['body']
            for arm in ('object_neutral','object_nonsense'):
                b=self.trial(c['id'],arm)['body'];self.assertEqual(base['state'],b['state'])
                for q in base['questions']:
                    self.assertEqual(list(base['questions'][q]['instructions'].values()),list(b['questions'][q]['instructions'].values()))
                    self.assertEqual(base['questions'][q]['criteria'],b['questions'][q]['criteria'])
    def test_string_same_instruction_fragments(self):
        a=self.trial('r01','string')['body'];b=self.trial('r01','object_meaningful')['body']
        self.assertEqual(a['questions']['expressed']['instructions'],'\n'.join(b['questions']['expressed']['instructions'].values()))
    def test_structured_criteria_same_text(self):
        for q in self.trial('s01','structured_criteria')['body']['questions'].values():
            c=q['criteria'];self.assertTrue(all(set(x)=={'rule'} for x in (c.values() if isinstance(c,dict) else c)))
    def test_qid_control_only_transport_names(self):
        a=self.trial('r01','object_meaningful')['body'];b=self.trial('r01','qid_control')['body']
        self.assertEqual(a['state'],b['state']);self.assertEqual(list(a['questions'].values()),list(b['questions'].values()))
        self.assertNotEqual(list(a['questions']),list(b['questions']))
    def test_two_distinct_relation_rubrics(self):
        q=self.trial('r01','string')['body']['questions']
        self.assertNotEqual(q['expressed']['instructions'],q['abstraction']['instructions'])
        self.assertTrue(any(v.get('expressed')==0 and v.get('abstraction')==1 for v in self.gold.values()))
    def test_multilabel_not_exclusive(self):
        qs=self.trial('s01','string')['body']['questions']
        self.assertEqual([q['type'] for k,q in qs.items() if k.startswith('subgraph_')],['noul']*3)
        self.assertGreater(sum(self.gold['s01']['subgraphs'].values()),1)
        self.assertEqual(sum(self.gold['s05']['subgraphs'].values()),0)
    def test_detail_levels_cumulative(self):
        state=self.trial('s01','string')['body']['state'];rs=state['available_representations']
        self.assertEqual(rs['0'],'')
        for i in range(1,4):self.assertTrue(rs[str(i+1)].startswith(rs[str(i)]))
        self.assertEqual(self.trial('s01','string')['body']['questions']['detail']['criteria'],m.DETAIL)
    def test_flat_nested_target_present_without_body_label(self):
        for c in self.cases:
            if c['family']!='choice':continue
            ss=[set(self.trial(c['id'],'flat_'+w)['body']['questions']['route']['criteria']) for w in ('3','5','9')]
            self.assertTrue(ss[0]<ss[1]<ss[2]);self.assertIn(self.gold[c['id']]['route'],ss[0])
    def test_reversal_preserves_options_changes_hash(self):
        a=self.trial('c01','flat_9');b=self.trial('c01','flat_9_reversed')
        ac=a['body']['questions']['route']['criteria'];bc=b['body']['questions']['route']['criteria']
        self.assertEqual(ac,bc);self.assertEqual(list(ac),list(reversed(bc)));self.assertNotEqual(a['body_sha256'],b['body_sha256'])
    def test_child_uses_actual_root_even_when_wrong(self):
        answer={'type':'choice','choice':'lab','probabilities':{'repo':.1,'lab':.8,'other':.1}}
        out=m.resolve_child(self.trials,'c01.hier_root',answer)
        self.assertEqual(len(out),1);self.assertEqual(out[0]['arm'],'hier_lab')
        self.assertNotIn(self.gold['c01']['route'],out[0]['body']['questions']['route']['criteria'])
    def test_other_no_child(self):
        a={'type':'choice','choice':'other','probabilities':{'repo':.1,'lab':.1,'other':.8}}
        self.assertEqual(m.resolve_child(self.trials,'c01.hier_root',a),[])
    def test_bad_root_distributions(self):
        for p,w in [({'repo':.5,'lab':.5,'other':0},'repo'),({'repo':.8,'lab':.4,'other':0},'repo'),({'repo':.8,'lab':.1,'other':.1},'lab'),({'repo':float('nan'),'lab':.1,'other':.1},'repo')]:
            with self.assertRaises(ValueError):m.resolve_child(self.trials,'c01.hier_root',{'type':'choice','choice':w,'probabilities':p})
    def test_no_network(self):
        with patch.object(socket,'socket',side_effect=AssertionError('network forbidden')):
            self.assertEqual(m.verify(),self.trials)
    def test_freeze(self):self.assertEqual(m.verify(),self.trials)
    def test_manifest_tamper(self):
        with patch.object(m,'build',return_value=[]):
            with self.assertRaises(ValueError):m.verify()
    def test_no_mutation(self):
        a=copy.deepcopy(self.cases);m.build(a);self.assertEqual(a,self.cases)
    def test_export_exact_new_directory_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'requests';args=[sys.executable,str(Path(m.__file__)),'--export',str(target)]
            r=subprocess.run(args,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            index=m.load(target/'INDEX.json');self.assertEqual(index['activation'],'disabled');self.assertIsNone(index['max_cost_usd'])
            self.assertEqual(len(index['trials']),188)
            for t in index['trials']:self.assertEqual(hashlib.sha256((target/t['file']).read_bytes()).hexdigest(),t['body_sha256'])
            self.assertNotEqual(index['suggested_order'],sorted(index['suggested_order']))
            r=subprocess.run(args,capture_output=True,text=True);self.assertEqual(r.returncode,2)
    def test_duplicate_json_keys_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'d.json';p.write_text('{"a":1,"a":2}')
            with self.assertRaises(ValueError):m.load(p)

if __name__=='__main__':unittest.main()
