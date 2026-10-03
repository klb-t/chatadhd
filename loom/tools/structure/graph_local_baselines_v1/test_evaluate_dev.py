"""Independent evidence/scoring mechanisms; no real DEV or validation file read."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import evaluate_dev as evaluation
import secondary_set_union as secondary


def sample():
    case={'id':'c','source_id':'synthetic:c','turns':[
        {'id':'t1','speaker':'Mira','known_at':'2026-01-01T00:00:00Z','text':'Alpha implies beta.'},
        {'id':'t2','speaker':'Mira','known_at':'2026-01-02T00:00:00Z','text':'I withdraw that conditional.'}]}
    def span(turn):
        return {'source_id':case['source_id'],'turn_id':turn['id'],'coordinate_space':'turn.text','quote':turn['text'],
            'char_start':0,'char_end':len(turn['text']),'byte_start':0,'byte_end':len(turn['text'].encode())}
    old={'id':'e1','relation':'implies','source':'A','target':'B','attributed_to':'Mira','polarity':'positive',
         'known_at':case['turns'][0]['known_at'],'evidence':[span(case['turns'][0])]}
    gold={'source_assertions':[old],'status_events':[{'assertion_id':'e1','status':'superseded','superseded_by':'replacement',
        'known_at':case['turns'][1]['known_at'],'evidence':[span(case['turns'][1])]}]}
    query={'relation':'implies','source':'A','target':'B','attributed_to':'Mira','as_of':case['turns'][1]['known_at']}
    return case,query,gold


class EvidenceMechanisms(unittest.TestCase):
    def test_withdrawal_refutation_targets_event_not_old_support(self):
        case,query,gold=sample()
        evidence,counts=evaluation.relevant_evidence(case,query,gold,{'label':'refuted','support_assertion_ids':['e1']})
        self.assertEqual([e['turn_id']for e in evidence],['t2'])
        self.assertEqual(counts['status_events_used'],1)

    def test_future_withdrawal_cannot_enter_earlier_support_view(self):
        case,query,gold=sample();query['as_of']=case['turns'][0]['known_at']
        evidence,counts=evaluation.relevant_evidence(case,query,gold,{'label':'supported','support_assertion_ids':['e1']})
        self.assertEqual([e['turn_id']for e in evidence],['t1'])
        self.assertEqual(counts['status_events_used'],0)

    def test_missing_evidence_is_unknown_not_explicit_denial(self):
        case,query,gold=sample()
        evidence,counts=evaluation.relevant_evidence(case,query,gold,{'label':'unknown','support_assertion_ids':[]})
        self.assertEqual(evidence,[])
        self.assertEqual(counts['status_events_used'],0)

    def test_narrow_duplicate_annotation_does_not_inflate_unique_span_denominator(self):
        case,query,gold=sample();gold['source_assertions'][0]['evidence']*=2
        evidence,_=evaluation.relevant_evidence(case,query,gold,{'label':'supported','support_assertion_ids':['e1']})
        self.assertEqual(len(evidence),1)

    def test_matched_cardinality_reports_actual_cost_and_relevant_denominators(self):
        rows=[{'query_id':'q','relevant_turn_ids':['t2'],'relevant_evidence':[{'turn_id':'t2'}]}]
        summary=secondary.selection_summary({'q':{'t1','t2'}},rows)
        self.assertEqual(summary['selected_unique_turns'],2)
        self.assertEqual(summary['selected_relevant_turns'],1)
        self.assertEqual(summary['turn_precision'],.5)
        self.assertEqual(summary['evidence_recall'],1)
        self.assertEqual(summary['cost_per_query']['mean'],2)


if __name__=='__main__':unittest.main()
