"""Tests of historical profile derivation/guards, not new model evaluations."""
from __future__ import annotations
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parent))
import model_profiles as m
D=m.load(m.EXTRACT);H=m.digest(m.EXTRACT.read_bytes());P=m.load(m.OUTPUT)

class Profiles(unittest.TestCase):
    def test_schema(self):self.assertEqual(m.validate(P),[])
    def test_reproducible(self):self.assertEqual(m.derive(D,H),P)
    def test_no_input_mutation(self):
        d=copy.deepcopy(D);m.derive(d,H);self.assertEqual(d,D)
    def test_per_question_not_global_reliability(self):
        self.assertEqual(len(P['profiles']),14)
        self.assertEqual([p['key']['question_id'] for p in P['profiles'][:12]],[f'q{i:02}' for i in range(1,13)])
        self.assertNotIn('global_reliability',json.dumps(P))
    def test_aggregate_counts_reconcile(self):
        qs=P['profiles'][:12]
        self.assertEqual(sum(p['population']['planned'] for p in qs),768)
        self.assertEqual(sum(p['metrics']['accuracy']['denominator']-p['metrics']['accuracy']['numerator'] for p in qs),26)
        self.assertEqual(sum(p['metrics']['selective_error']['numerator'] for p in qs),5)
        self.assertEqual(sum(p['metrics']['selective_error']['denominator'] for p in qs),673)
    def test_q01_preserves_weakness(self):
        q=P['profiles'][0]
        self.assertEqual(q['metrics']['positive_precision']['value'],.3)
        self.assertEqual(q['metrics']['positive_precision']['denominator'],20)
        self.assertEqual(q['metrics']['selective_error']['numerator'],5)
    def test_q11_not_rounded_to_perfect(self):
        q=P['profiles'][10]
        self.assertAlmostEqual(q['metrics']['positive_precision']['value'],6/11)
        self.assertIsNone(q['metrics']['positive_precision']['denominator'])
    def test_unknown_positive_counts_not_invented(self):
        for q in P['profiles'][1:12]:
            self.assertIsNone(q['metrics']['positive_precision']['numerator'])
            self.assertIsNone(q['metrics']['positive_precision']['denominator'])
    def test_no_per_question_billing(self):
        for q in P['profiles'][:12]:
            self.assertIsNone(q['cost']['reported_total_usd'])
            self.assertEqual(q['cost']['allocation'],'shared_batch_only')
    def test_no_latency_division(self):
        self.assertTrue(all(p['latency']['per_question_seconds'] is None for p in P['profiles']))
    def test_native_not_58_requests(self):
        native=P['batches']['native_v1_combined']
        self.assertEqual(native['original_plan'],32)
        self.assertEqual(native['responses']+native['uncertain']+native['untouched'],32)
        self.assertEqual(sum(p['population']['planned'] for p in P['profiles'][12:]),32)
    def test_native_semantic_accuracy_unavailable(self):
        for q in P['profiles'][12:]:
            stat=q['metrics']['semantic_accuracy_conditional_on_valid_contract']
            self.assertIsNone(stat['value']);self.assertEqual(stat['denominator'],0)
    def test_unknown_costs_not_free(self):
        self.assertEqual(P['batches']['native_v1_combined']['uncertain'],2)
        self.assertEqual(P['batches']['native_v1_combined']['uncertain_reservation_usd'],'0.00602193')
        self.assertEqual(P['batches']['native_v1_combined']['cost_usd'],'0.02617219')
    def test_pooled_failures_not_assigned_to_model(self):
        for p in P['profiles'][12:]:
            self.assertNotIn('quote_mismatch',{f['kind'] for f in p['failure_modes']})
    def test_model_version_not_inherited(self):
        self.assertTrue(all(not p['validity']['inherit_to_other_version'] for p in P['profiles']))
        for p in P['profiles'][12:]:self.assertIsNone(p['key']['version'])
    def test_owner_assessment_is_not_score(self):
        o=P['owner_observations'][0]
        self.assertIsNone(o['benchmark_score']);self.assertEqual(o['model_ids'],[])
    def test_compensation_not_experiment_result(self):
        self.assertIsNone(P['compensation']['ablation']['current_results'])
        self.assertEqual(P['compensation']['status'],'proposed_unrun')
    def test_mechanism_tests_not_model_statistics(self):
        self.assertEqual(P['mechanism_evidence']['ctest_initial_passed'],70)
        self.assertEqual(P['mechanism_evidence']['focused_rerun_passed'],1)
        self.assertEqual(P['mechanism_evidence']['ctest_reconciled_passed'],71)
        self.assertTrue(all('ctest' not in p['metrics'] for p in P['profiles']))
    def test_ratio_mismatch_rejected(self):
        p=copy.deepcopy(P);p['profiles'][0]['metrics']['accuracy']['value']=1
        self.assertIn('ratio mismatch',m.validate(p))
    def test_unavailable_cannot_be_perfect(self):
        p=copy.deepcopy(P);p['profiles'][12]['metrics']['semantic_accuracy_conditional_on_valid_contract']['value']=1
        self.assertIn('unavailable metric has value',m.validate(p))
    def test_population_mismatch_rejected(self):
        p=copy.deepcopy(P);p['profiles'][0]['population']['missing']=1
        self.assertIn('population mismatch',m.validate(p))
    def test_unknown_evidence_rejected(self):
        p=copy.deepcopy(P);p['profiles'][0]['metrics']['brier']['evidence']['source_id']='invented'
        self.assertIn('unknown metric evidence',m.validate(p))
    def test_duplicate_profile_rejected(self):
        p=copy.deepcopy(P);p['profiles'].append(copy.deepcopy(p['profiles'][0]))
        self.assertIn('duplicate profile id',m.validate(p))
    def test_no_automatic_promotion(self):
        p=copy.deepcopy(P);p['profiles'][0]['promotion']['automatic']=True
        self.assertTrue(m.validate(p))
    def test_no_calibration_guarantee(self):
        p=copy.deepcopy(P);p['profiles'][0]['calibration']['empirically_certified']=True
        self.assertTrue(m.validate(p))
    def test_per_question_cost_rejected(self):
        p=copy.deepcopy(P);p['profiles'][0]['cost']['reported_total_usd']='0.0001'
        self.assertIn('per-question cost invented',m.validate(p))
    def test_nonfinite_cost_rejected(self):
        p=copy.deepcopy(P);p['profiles'][12]['cost']['reported_total_usd']='NaN'
        self.assertIn('cost range',m.validate(p))
    def test_noninteger_reconstruction_rejected(self):
        with self.assertRaises(ValueError):m.integer_product(64,.3333)
    def test_changed_q_inventory_rejected(self):
        d=copy.deepcopy(D);d['jev']['questions'].pop()
        with self.assertRaises(ValueError):m.derive(d,H)
    def test_changed_counts_rejected(self):
        d=copy.deepcopy(D);d['jev']['questions'][0]['errors']=13
        with self.assertRaises(ValueError):m.derive(d,H)
    def test_changed_native_count_rejected(self):
        d=copy.deepcopy(D);d['native']['models'][0]['responses']=12
        with self.assertRaises(ValueError):m.derive(d,H)
    def test_actual_source_absence_reported(self):
        with tempfile.TemporaryDirectory() as d:
            issues=m.verify_repo_sources(Path(d),D)
            self.assertEqual(len(issues),len(D['sources']))
    def test_wrong_source_hash_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);data=copy.deepcopy(D);data['sources']=[D['sources'][2]]
            p=root/data['sources'][0]['path'];p.parent.mkdir(parents=True);p.write_text('not the report')
            self.assertIn('source version mismatch: jev_report',m.verify_repo_sources(root,data))
    def test_no_network(self):
        with patch('socket.socket',side_effect=AssertionError('network prohibited')):
            self.assertEqual(m.derive(D,H),P);self.assertEqual(m.validate(P),[])
    def test_cli(self):
        run=subprocess.run([sys.executable,m.__file__],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(json.loads(run.stdout)['profiles'],14)
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'out.json';cmd=[sys.executable,m.__file__,'--emit',str(p)]
            self.assertEqual(subprocess.run(cmd,capture_output=True).returncode,0)
            self.assertEqual(subprocess.run(cmd,capture_output=True).returncode,2)
            self.assertEqual(m.load(p),P)

if __name__=='__main__':unittest.main()
