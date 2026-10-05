"""Pure study aggregation keeps planned decisions and split-call semantics."""
from copy import deepcopy
import unittest

from loom.tools.structure import analysis_optimization_v1 as study
from loom.tools.structure.programme_study_results_v1 import score_predictions


ARMS = (
    'j_active', 'j_directed', 'j_roles', 'j_split_q01', 'j_split_q02',
    'g_brief_t0', 'g_brief_t03', 'g_rules_t0', 'g_rules_t03',
)


class ProgrammeStudyScore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gold = study.w3.load_controls('gold_dev.json')
        cls.query_ids = [q['query_id'] for case in cls.gold for q in case['judgments']]

    def test_empty_results_match_retained_negative_control_and_keep_all_slots(self):
        raw = {arm: [] for arm in ARMS}
        retained = study.read(
            study.ROOT / 'docs/research/model_research_2026-10-04/study/empty_run_score.json')
        result = score_predictions(raw, self.gold)
        for field in ('reports', 'by_language_and_family', 'paired_changes'):
            self.assertEqual(result[field], retained[field])
        self.assertEqual(len(result['reports']), 8)
        self.assertEqual(sum(r['query_count'] for r in result['reports'].values()), 384)
        for report in result['reports'].values():
            self.assertEqual((report['available'], report['unavailable']), (0, 48))
            self.assertEqual(report['accuracy_all_queries'], 0)
        self.assertEqual(raw, {arm: [] for arm in ARMS})

    def test_split_missing_conflicting_and_half_threshold_remain_distinct(self):
        raw = {arm: [] for arm in ARMS}
        support, conflict, missing, threshold = self.query_ids[:4]
        raw['j_split_q01'] = [
            {'query_id': support, 'state': 'completed', 'probabilities': {'q01': .51}},
            {'query_id': conflict, 'state': 'completed', 'probabilities': {'q01': .51}},
            {'query_id': missing, 'state': 'completed', 'probabilities': {'q01': .2}},
            {'query_id': threshold, 'state': 'completed', 'probabilities': {'q01': .5}},
        ]
        raw['j_split_q02'] = [
            {'query_id': support, 'state': 'completed', 'probabilities': {'q02': .2}},
            {'query_id': conflict, 'state': 'completed', 'probabilities': {'q02': .51}},
            {'query_id': threshold, 'state': 'completed', 'probabilities': {'q02': .5}},
        ]
        before = deepcopy(raw)
        joined = study.combine_split(raw['j_split_q01'], raw['j_split_q02'])
        self.assertEqual([row.get('label') for row in joined],
                         ['supported', 'conflicting', None, 'unknown'])
        result = score_predictions(raw, self.gold)
        report = result['reports']['j_split']
        self.assertEqual((report['query_count'], report['available'], report['unavailable']),
                         (48, 2, 46))
        self.assertEqual(raw, before)

    def test_partial_valid_results_do_not_shrink_accuracy_denominator(self):
        raw = {arm: [] for arm in ARMS}
        target = self.gold[0]['judgments'][0]
        raw['g_brief_t0'] = [study.panel.compile_gpt_judgment(
            {'query_id': target['query_id'], 'label': target['label']}, target['query_id'])]
        result = score_predictions(raw, self.gold)
        report = result['reports']['g_brief_t0']
        self.assertEqual((report['query_count'], report['available'], report['unavailable']),
                         (48, 1, 47))
        self.assertEqual(report['accuracy_all_queries'], 1 / 48)
        self.assertEqual(report['coverage'], 1 / 48)


if __name__ == '__main__':
    unittest.main()
