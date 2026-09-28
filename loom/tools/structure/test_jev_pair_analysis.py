"""Offline scoring checks using fabricated responses, never live model requests."""
from pathlib import Path
import tempfile
import unittest

try:
    from . import jev_pair_analysis as analysis
    from .test_jev_live_pilot import HTTP, config
except ImportError:
    import jev_pair_analysis as analysis
    from test_jev_live_pilot import HTTP, config

pilot = analysis.pilot
FIXTURE = Path(__file__).resolve().parents[2]/'tests/fixtures/eval/jev_structure_pairs_v1'


class PairAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)/'run'
        inputs = pilot.read_json(FIXTURE/'inputs.json')
        self.gold = pilot.read_json(FIXTURE/'gold.json')
        self.manifest = pilot.prepare(config(),inputs,self.directory,transport_fn=HTTP())

    def complete(self, probabilities=None):
        if probabilities is None:
            probabilities = [.9 if g['labels']['q01'] else .1 for g in self.gold]
        pilot.run(self.manifest,self.directory,transport_fn=HTTP(probabilities),key_loader=lambda:'fabricated-unit-key')

    def test_groups_follow_contrast_not_self_base_ids(self):
        self.complete()
        result = analysis.analyze(self.directory,FIXTURE)
        self.assertEqual(result['overall']['confusion'],{'tp':32,'tn':16,'fp':0,'fn':0})
        self.assertEqual(result['all_six_correct_families'],8)
        self.assertEqual(result['bilingual']['both_correct'],24)
        self.assertEqual({k:v['planned'] for k,v in result['groups']['contrast_kind'].items()},
                         {'paraphrase':16,'domain_transfer':16,'foil':16})
        self.assertTrue(result['interpretation']['no_legacy_invariance_comparison'])

    def test_missing_response_is_not_correct_negative(self):
        pilot.run(self.manifest,self.directory,transport_fn=HTTP(failure=TimeoutError()),key_loader=lambda:'fabricated-unit-key')
        result = analysis.analyze(self.directory,FIXTURE)
        self.assertEqual(result['overall']['available'],0)
        self.assertEqual(result['overall']['missing'],48)
        self.assertEqual(result['overall']['confusion'],{'tp':0,'tn':0,'fp':0,'fn':0})
        self.assertEqual(result['audit']['attempts_with_unknown_cost'],1)

    def test_in_progress_rejected(self):
        ledger = {'schema':'loom.jev_ledger/1','manifest_hash':pilot.safe.digest(self.manifest),
                  'attempts':[]}
        pilot.write_new(self.directory/'ledger.json',ledger)
        with self.assertRaisesRegex(ValueError,'run_not_terminal'):
            analysis.analyze(self.directory,FIXTURE)

    def test_response_tamper_rejected(self):
        self.complete()
        path = next(self.directory.glob('*.response.bin'))
        path.write_bytes(path.read_bytes()+b' ')
        with self.assertRaisesRegex(pilot.safe.RunnerError,'response_hash_mismatch'):
            analysis.analyze(self.directory,FIXTURE)

    def test_frozen_gold_tamper_rejected(self):
        self.complete()
        fixture = Path(self.tmp.name)/'fixture'
        fixture.mkdir()
        for name in ('README.md','manifest.json','inputs.json','gold.json'):
            (fixture/name).write_bytes((FIXTURE/name).read_bytes())
        (fixture/'gold.json').write_bytes((fixture/'gold.json').read_bytes()+b' ')
        with self.assertRaisesRegex(ValueError,'fixture_hash_mismatch'):
            analysis.analyze(self.directory,fixture)

    def test_selective_precision_boundaries_and_missing(self):
        rows = [{'probability':p,'label':y} for p,y in ((.8,1),(.8,0),(.2,0),(.21,1),(None,0))]
        result = analysis.selective(rows,.2,.8)
        self.assertEqual(result['selected'],3)
        self.assertEqual(result['precision_available'],.5)
        self.assertEqual(result['coverage_original_planned'],.6)
        self.assertEqual(result['confusion'],{'tp':1,'tn':1,'fp':1,'fn':0})
        self.assertEqual(result['errors'],1)
        self.assertNotIn('selective_error',result)
        self.assertEqual(analysis.selective([{'probability':.24,'label':1}],.3,.7)['error_fraction'],1)


if __name__ == '__main__':
    unittest.main()
