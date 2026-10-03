"""Boundary tests for the paired experiment, not model-quality measurements."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.structure import analysis_optimization_v1 as e


class ExperimentIntegrity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.arms = e.jev_arms()

    def test_same_inventory_and_no_labels_in_requests(self):
        ids = [r['case_id'] for r in self.arms['j_active']]
        self.assertEqual(len(ids),48)
        self.assertEqual(len(set(ids)),48)
        for rows in self.arms.values():
            self.assertEqual([r['case_id'] for r in rows],ids)
            for row in rows:
                payload=json.loads(row['state']['text'])
                source=payload.get('source_record',payload)
                self.assertEqual(set(source),{'case_id','source_id','node_inventory','query','turns'})
                self.assertNotIn('gold',source)

    def test_only_refutation_criterion_changes(self):
        for a,b in zip(self.arms['j_active'],self.arms['j_directed']):
            copied=deepcopy(a)
            copied['questions']['q02']['criteria']['false']+=e.w3.recipe()['append']
            self.assertEqual(copied,b)

    def test_role_card_preserves_source_and_query(self):
        for a,b in zip(self.arms['j_directed'],self.arms['j_roles']):
            source=json.loads(a['state']['text'])
            represented=json.loads(b['state']['text'])
            self.assertEqual(represented['source_record'],source)
            self.assertEqual(a['questions'],b['questions'])
            nodes={n['id']:n for n in source['node_inventory']}
            self.assertEqual(represented['requested_relation_roles']['directed_source_proposition'],nodes[source['query']['source']])
            self.assertEqual(represented['requested_relation_roles']['directed_target_proposition'],nodes[source['query']['target']])

    def test_split_preserves_both_question_bytes(self):
        for a,b,c in zip(self.arms['j_directed'],self.arms['j_split_q01'],self.arms['j_split_q02']):
            self.assertEqual(a['state'],b['state']);self.assertEqual(a['state'],c['state'])
            self.assertEqual(b['questions']|c['questions'],a['questions'])

    def test_split_missing_answer_is_not_unknown(self):
        left=[{'query_id':'q','state':'completed','probabilities':{'q01':.2}}]
        self.assertEqual(e.combine_split(left,[]),[{'query_id':'q','state':'unavailable'}])
        right=[{'query_id':'q','state':'completed','probabilities':{'q02':.2}}]
        self.assertEqual(e.combine_split(left,right)[0]['label'],'unknown')

    def test_half_threshold_and_conflicting_are_explicit(self):
        def row(key,p):return [{'query_id':'q','state':'completed','probabilities':{key:p}}]
        self.assertEqual(e.combine_split(row('q01',.5),row('q02',.5))[0]['label'],'unknown')
        self.assertEqual(e.combine_split(row('q01',.51),row('q02',.51))[0]['label'],'conflicting')

    def test_temperature_is_isolated(self):
        row=self.arms['j_directed'][0]
        a=e.llm_body(row,False,0);b=e.llm_body(row,False,.3)
        a['temperature']=.3
        self.assertEqual(a,b)

    def test_prompt_change_keeps_data_schema_and_parameters(self):
        row=self.arms['j_directed'][0]
        a=e.llm_body(row,False,0);b=e.llm_body(row,True,0)
        a['messages'][0]['content']+=e.RULES
        self.assertEqual(a,b)
        self.assertFalse(b['provider']['allow_fallbacks'])
        self.assertTrue(b['provider']['require_parameters'])

    def test_all_requests_pass_existing_clients(self):
        for rows in self.arms.values():
            for row in rows:
                e.jev.validate_body({'model':e.jev.MODEL,'provider':e.w3.provider(),
                                    'state':row['state'],'questions':row['questions']})
        for detailed in (False,True):
            for temperature in (0,.3):
                for row in self.arms['j_directed']:
                    body=e.llm_body(row,detailed,temperature)
                    self.assertGreater(float(e.safe.estimate_reservation(body)['minimum_reservation_usd']),0)

    def test_absent_ledger_has_no_invented_predictions(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);e.write(root/'a/manifest.json',{'requests':[]})
            self.assertEqual(e.predictions(root,root/'empty',{'arm':'a','kind':'jev'}),[])


if __name__=='__main__':
    unittest.main()
