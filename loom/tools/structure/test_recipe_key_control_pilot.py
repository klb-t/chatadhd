"""Optional8 uses frozen v1 controls without mutating its first32 contract."""
from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile
try:
    from . import recipe_key_control_pilot as control
    from .test_recipe_live_pilot import HTTP
except ImportError:
    import recipe_key_control_pilot as control
    from test_recipe_live_pilot import HTTP
p = control.v1


class KeyControlTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name); self.run_dir = self.root / 'run'
        self.manifest = control.prepare(control.frozen_plan(), self.root / 'prepared',
                                        transport_fn=HTTP(), run_dir=self.run_dir)
    def run_pilot(self, http):
        return control.run(self.manifest, transport_fn=http, key_loader=lambda: 'unit-secret')
    def offline(self):
        def forbidden(*args):
            self.fail('terminal resume must be offline')
        return control.run(self.manifest, transport_fn=forbidden, key_loader=forbidden)

    def test_exact_unexecuted_ids_and_only_instruction_keys_change(self):
        plan = control.frozen_plan(); bodies, index = control.recipes.unpack(control.FIXTURE / 'requests.zip')
        records = {r['request_id']: r for r in index['records']}
        self.assertEqual(len(plan['requests']), 8)
        self.assertEqual(plan['total_reservation_usd'], '0.008')
        self.assertFalse({r['id'] for r in plan['requests']} & {r['id'] for r in p.frozen_plan()['requests']})
        for row in plan['requests']:
            archived = p.safe.parse_json(bodies[row['source_file']])
            actual = p.safe.parse_json(control.wire_bodies()[row['id']]); actual['provider'].pop('max_price')
            self.assertEqual(actual, archived)
            meaningful = p.safe.parse_json(bodies[records[row['case_id'] + '.object_meaningful']['file']])
            self.assertEqual(actual['state'], meaningful['state'])
            for task in ('expressed', 'inferred'):
                a, b = actual['questions'][task], meaningful['questions'][task]
                self.assertEqual(list(a['instructions'].values()), list(b['instructions'].values()))
                self.assertNotEqual(list(a['instructions']), list(b['instructions']))
                self.assertEqual(a['criteria'], b['criteria'])
            self.assertNotIn('split', actual['state'])

    def test_isolated_binding_retains_original_v1_globals_and_plan(self):
        original = (p.EXPERIMENT, p.ARMS, p.validate_manifest, p.wire_bodies, p.safe.digest(p.frozen_plan()))
        result = self.run_pilot(HTTP())
        self.assertEqual(len(result['attempts']), 8)
        self.assertEqual(result['experiment_id'], control.EXPERIMENT)
        self.assertEqual(original, (p.EXPERIMENT, p.ARMS, p.validate_manifest, p.wire_bodies, p.safe.digest(p.frozen_plan())))
        self.assertEqual(p.code_hashes(), control.FROZEN_V1_CODE)
        self.assertEqual(self.offline(), result)
        self.assertEqual(len(p.frozen_plan()['requests']), 32)

    def test_caps_source_aliases_and_first_client_hash_reject_before_post(self):
        for mutate in (lambda m: m.update(batch_cap_usd='.007'), lambda m: m.update(batch_cap_usd='.11'),
                       lambda m: m.update(global_key_cap_usd='2.01'),
                       lambda m: m['requests'].append(deepcopy(m['requests'][0])),
                       lambda m: m['model_aliases'].append('typesafe/other'),
                       lambda m: m['requests'][0]['body']['state'].update(candidate_relation='changed')):
            m = deepcopy(self.manifest); mutate(m); http = HTTP()
            with self.assertRaises(p.safe.RunnerError):
                control.run(m, transport_fn=http, key_loader=lambda: 'unit-secret')
            self.assertFalse(http.calls)
        with patch.object(p, 'code_hashes', return_value={}):
            with self.assertRaisesRegex(p.safe.RunnerError, 'frozen_v1_client'):
                control.code_hashes()

    def test_success_first8_exact_wire_and_receipts(self):
        http = HTTP(audit_status=404); result = self.run_pilot(http)
        self.assertEqual(len(http.posts()), 8)
        self.assertEqual(result['reported_cost_usd'], '0.0000336')
        self.assertEqual(result['attempts_with_unknown_cost'], 0)
        self.assertEqual(len(list(self.run_dir.glob('*.started.json'))), 8)
        self.assertEqual(len(list(self.run_dir.glob('*.result.json'))), 8)
        wires = control.wire_bodies()
        for row, call in zip(self.manifest['requests'], http.posts()):
            self.assertEqual(call[2], wires[row['id']])
        p.validate_ledger(result, self.manifest, self.run_dir)

    def test_unknown_or_bad_first_stops_and_cost_remains_independent(self):
        http = HTTP(response=lambda v: (503, p.safe.canonical(v)))
        result = self.run_pilot(http)
        self.assertEqual(len(http.posts()), 1)
        self.assertEqual(result['reported_cost_usd'], '0.0000042')
        self.assertEqual(self.offline(), result)
        checkpoint = deepcopy(result); checkpoint.pop('stopped_reason')
        p.safe._atomic(self.run_dir / 'ledger.json', p.safe.canonical(checkpoint))
        self.assertEqual(self.offline(), result)

    def test_interrupted_first_not_retried(self):
        http = HTTP(failure=KeyboardInterrupt())
        with self.assertRaises(KeyboardInterrupt):
            self.run_pilot(http)
        result = self.offline()
        self.assertEqual(len(http.posts()), 1)
        self.assertEqual(result['attempts_with_unknown_cost'], 1)
        self.assertEqual(result['attempts'][0]['state'], 'uncertain')

    def test_inherited_nonresetting_global_remaining_gate_before_post(self):
        for mutate in (lambda d,n: d.pop('limit_reset'), lambda d,n: d.update(limit_remaining='.007'),
                       lambda d,n: d.update(limit=2.01), lambda d,n: d.update(byok_usage='.1')):
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as folder:
                m = deepcopy(self.manifest); m['run_dir'] = str(Path(folder).resolve())
                http = HTTP(key_change=mutate)
                with self.assertRaises(p.safe.RunnerError):
                    control.run(m, transport_fn=http, key_loader=lambda: 'unit-secret')
                self.assertFalse(http.posts())

    def test_scorer_retains16new_and16frozen_comparator_denominators(self):
        self.run_pilot(HTTP()); result = control.score(self.manifest, self.run_dir)
        self.assertEqual(result['new_decisions']['planned'], 16)
        self.assertEqual(len(result['new_rows']), 16)
        self.assertEqual(len(result['first32_comparator_rows']), 16)
        self.assertEqual(len(result['contrasts']), 32)
        for arm in ('object_meaningful', 'string', *control.ARMS):
            m = result['profiles_same_four_cases']['expressed'][arm]
            self.assertEqual(m['planned'], 4)
            self.assertEqual(m['planned_positive'], 0)
            self.assertIsNone(m['recall_observed'])
            self.assertEqual(m['baselines_planned']['all_negative_accuracy'], 1)
        # Mock new all-positive loses to simpler source-expression baseline.
        self.assertEqual(result['profiles_same_four_cases']['expressed']['object_neutral']['accuracy_observed'], 0)
        self.assertEqual(result['profiles_same_four_cases']['expressed']['object_meaningful']['accuracy_observed'], 1)
        self.assertIsNone(result['profiles_same_four_cases']['expressed']['object_meaningful']['precision_observed'])

    def test_get_only_preflight_no_attempt_files(self):
        http = HTTP()
        result = control.preflight(self.manifest, transport_fn=http, key_loader=lambda: 'unit-secret')
        self.assertTrue(result['nonresetting'])
        self.assertFalse(self.run_dir.exists())
        self.assertEqual([c[0] for c in http.calls], ['GET', 'GET'])

    def test_exact_archive_replays_all_four_arm_comparison_offline(self):
        self.run_pilot(HTTP()); report = control.score(self.manifest, self.run_dir)
        score_path = self.root / 'score.json'; p.write_new(score_path, report)
        archive_path = self.root / 'evidence.zip'
        inventory = control.archive(self.root / 'prepared', archive_path, score_path=score_path)
        extracted = self.root / 'extracted'
        with ZipFile(archive_path) as z:
            for name, metadata in inventory['files'].items():
                raw = z.read(name)
                self.assertEqual(p.hashlib.sha256(raw).hexdigest(), metadata['sha256'])
                self.assertEqual(len(raw), metadata['bytes'])
                self.assertNotIn(b'unit-secret', raw)
            z.extractall(extracted)  # Only our generated bounded archive.
        result = subprocess.run([sys.executable, 'replay/loom/tools/structure/recipe_key_control_pilot.py',
            'score', '--manifest', 'prepared/manifest.json', '--run-dir', 'run', '--output', 'replayed_score.json'],
            cwd=extracted, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(p.pilot.read_json(extracted / 'replayed_score.json'), report)
        with self.assertRaises(FileExistsError):
            control.archive(self.root / 'prepared', archive_path)

    def test_new_cli_reports_stop_with_nonzero_exit_without_altering_v1(self):
        m = deepcopy(self.manifest); m['run_dir'] = str(control.RUN_DIR.resolve())
        with patch.object(control.pilot, 'read_json', return_value=m), patch.object(control, 'run', return_value={
                'attempts': [{}], 'stopped_reason': 'fixture-stop'}):
            self.assertEqual(control.main(['run', '--manifest', 'unused.json']), 2)


if __name__ == '__main__':
    unittest.main()
