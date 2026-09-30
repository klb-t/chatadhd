"""Synthetic mechanisms; no DEV or sealed validation fixture/gold is read."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import local_baselines as local
POLICY={'query_representation':'attribution: {attributed_to}\nrelation: {relation}\nsource: {source_text}\ntarget: {target_text}',
        'candidate_representation':'speaker: {speaker}\ntext: {text}'}


def case():
    return {'id':'synthetic','source_id':'fake_source','language':'en',
        'turns':[{'id':'past','known_at':'2026-01-01T00:00:00Z','speaker':'Mira','text':'Alpha implies beta.'},
                 {'id':'future','known_at':'2026-01-02T00:00:00Z','speaker':'Mira','text':'I withdraw the old conditional.'}],
        'node_inventory':[{'id':'A','text':'alpha','aliases':[]},{'id':'B','text':'beta','aliases':[]}],
        'judgment_queries':[]}


def query():
    return {'id':'q','relation':'implies','source':'A','target':'B','attributed_to':'Mira',
        'as_of':'2026-01-01T00:00:00Z','scope':'explicit_source'}


class LocalBaselineMechanisms(unittest.TestCase):
    def test_time_cutoff_physically_excludes_future_candidate(self):
        row=local.prepare_query(case(),query(),POLICY)
        self.assertEqual([c['turn_id']for c in row['candidates']],['past'])
        self.assertEqual([t['id']for t in row['prefix_payload']['turns']],['past'])
        self.assertEqual(row['future_excluded_turn_count'],1)
        self.assertEqual(row['prefix_sha256'],local.adapter.safe.digest(row['prefix_payload']))
        self.assertIsNone(row['prediction'])
        self.assertFalse(row['source_judgment_available'])

    def test_representation_uses_only_requested_inventory_endpoints_and_attribution(self):
        c=case();c['node_inventory'].append({'id':'Z','text':'forbidden_other_inventory_text','aliases':['unused']})
        row=local.prepare_query(c,query(),POLICY)
        self.assertEqual(row['query_text'],'attribution: Mira\nrelation: implies\nsource: alpha\ntarget: beta')
        self.assertNotIn('forbidden_other_inventory_text',row['query_text'])
        self.assertEqual(row['candidates'][0]['text'],'Alpha implies beta.')

    def test_unicode_spans_source_identity_and_original_order_survive(self):
        c=case();c['turns'][0]['text']='Żółw śpi.'
        row=local.prepare_query(c,query(),POLICY);cand=row['candidates'][0]
        self.assertEqual(cand['source_id'],'fake_source')
        self.assertEqual(cand['span']['char_end'],9)
        self.assertEqual(cand['span']['byte_end'],len('Żółw śpi.'.encode()))
        self.assertEqual(cand['eligible_order'],0)

    def test_token_cosine_cannot_prove_implication_direction(self):
        forward='attribution: Mira\nrelation: implies\nsource: alpha\ntarget: beta'
        reverse='attribution: Mira\nrelation: implies\nsource: beta\ntarget: alpha'
        self.assertEqual(local.tokens(forward),local.tokens(reverse))
        self.assertAlmostEqual(local.cosine(local.tokens(forward),local.tokens(reverse)),1)

    def test_missing_vector_is_not_a_negative_relation(self):
        self.assertIsNone(local.cosine(local.tokens('alpha'),local.tokens('')))
        self.assertIsNone(local.naive_label(None,.5))
        self.assertEqual(local.naive_label(0.,.5),'unknown')
        self.assertEqual(local.naive_label(.5,.5),'supported')
        self.assertNotEqual(local.naive_label(.49,.5),'refuted')

    def test_union_has_no_negative_or_unknown_veto(self):
        self.assertEqual(local.union([None,.1,.8]),.8)
        self.assertIsNone(local.union([None,None]))
        self.assertEqual(local.union([.8,.1]),local.union([.1,.8]))

    def test_ties_use_original_eligible_order_not_truth_or_recency(self):
        c=[{'turn_id':'later','eligible_order':1,'scores':{'m':.5}},
           {'turn_id':'earlier','eligible_order':0,'scores':{'m':.5}},
           {'turn_id':'unknown','eligible_order':2,'scores':{'m':None}}]
        self.assertEqual([r['turn_id']for r in local.rank_candidates(c,'m')],['earlier','later','unknown'])

    def test_character_frequency_is_not_corpus_fitted(self):
        self.assertEqual(local.chars('ŻÓŁW  śpi'),local.chars('żółw śpi'))
        self.assertAlmostEqual(local.cosine(local.chars('some text'),local.chars('some text')),1)


if __name__=='__main__':unittest.main()
