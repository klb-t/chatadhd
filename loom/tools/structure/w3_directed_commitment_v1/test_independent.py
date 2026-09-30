"""Independent adversarial DEV mechanism tests; never make inference requests."""
from __future__ import annotations

from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from . import experiment as e


class IndependentIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='w3-independent-review-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_actual_v2_request_cannot_be_relabelled_v3(self):
        manifest = e.HISTORY / 'active_refute_v2/prepared/manifest.json'
        cases = e.base.load_fixture('inputs_dev.json')
        with self.assertRaisesRegex(ValueError, 'recipe_or_endpoint_identity_drift'):
            e.validate_requests(manifest, cases, 'directed_refute_v3')

    def test_empty_ledger_preserves_entire_planned_denominator(self):
        arm = 'active_refute_v2'
        historical = e.HISTORY / arm
        ledger = e.integrity.read(historical / 'run/ledger.json')
        ledger['attempts'] = []
        run = self.root / 'run'
        run.mkdir()
        (run / 'ledger.json').write_text(json.dumps(ledger))
        rows, summary = e.replay(historical / 'prepared/manifest.json', run,
                                 e.base.load_fixture('inputs_dev.json'), arm)
        self.assertEqual(len(rows), 48)
        self.assertEqual(summary['outcomes'], {'unavailable': 48})
        self.assertEqual(summary['attempted_requests'], 0)
        self.assertEqual(len({r['query_id'] for r in rows}), 48)
        score = e.panel.score_judgments(e.base.load_fixture('gold_dev.json'), rows)
        self.assertEqual((score['query_count'], score['available'], score['unavailable']), (48, 0, 48))
        self.assertEqual(score['accuracy_all_queries'], 0)

    def test_gold_is_byte_frozen_without_loading_labels(self):
        plan = self.root / 'plan'
        with patch.object(e, 'load_controls', wraps=e.load_controls) as loader:
            e.prepare(plan)
        self.assertEqual([c.args for c in loader.call_args_list], [('inputs_dev.json',)])
        freeze = e.integrity.read(plan / 'freeze.json')
        for directory in (e.CONTROLS, e.base.FIXTURE):
            path = directory / 'gold_dev.json'
            self.assertEqual(freeze['source_files_sha256'][str(path.relative_to(e.ROOT))],
                             hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertTrue(freeze['gold_bytes_hashed'])
        self.assertFalse(freeze['gold_content_loaded'])

    def test_incomplete_freeze_inventory_is_rejected(self):
        plan = self.root / 'plan'
        e.prepare(plan)
        freeze = e.integrity.read(plan / 'freeze.json')
        freeze['source_files_sha256'] = {}
        freeze['plan_files_sha256'] = {}
        (plan / 'freeze.json').write_text(json.dumps(freeze))
        with self.assertRaisesRegex(ValueError, 'freeze_inventory'):
            e.verify_plan(plan)

    @contextmanager
    def fake_archive(self, *, corrupt_member=False, corrupt_container=False):
        history = self.root / 'history'
        history.mkdir()
        member = 'history/active_refute_v2/run/example.response.bin'
        original = b'first immutable response'
        files = {member: {'bytes': len(original), 'sha256': hashlib.sha256(original).hexdigest()}}
        archive = history / 'first_evidence.zip'
        with ZipFile(archive, 'w') as zipped:
            zipped.writestr('inventory.json', json.dumps({'files': files}))
            zipped.writestr(member, b'wrong immutable response' if corrupt_member else original)
        raw = archive.read_bytes()
        receipt = {'archive_bytes': len(raw), 'archive_sha256': hashlib.sha256(raw).hexdigest(), 'files': files}
        (history / 'FIRST_EVIDENCE_ARCHIVE.json').write_text(json.dumps(receipt))
        if corrupt_container:
            archive.write_bytes(raw + b'tampered')
        with patch.object(e, 'ROOT', self.root), patch.object(e, 'HISTORY', history):
            yield original

    def test_archive_container_tamper_rejected_before_writes(self):
        destination = self.root / 'restored'
        with self.fake_archive(corrupt_container=True):
            with self.assertRaisesRegex(ValueError, 'archive_container_drift'):
                e.restore_history(destination)
        self.assertFalse(destination.exists())

    def test_archive_member_tamper_rejected_despite_matching_container_hash(self):
        destination = self.root / 'restored'
        with self.fake_archive(corrupt_member=True):
            with self.assertRaisesRegex(ValueError, 'archive_member_drift'):
                e.restore_history(destination)
        self.assertFalse(destination.exists())

    def test_archive_restore_preserves_exact_payload_bytes(self):
        destination = self.root / 'restored'
        with self.fake_archive() as original:
            binding = e.restore_history(destination)
        target = destination / 'active_refute_v2/example.response.bin'
        self.assertEqual(target.read_bytes(), original)
        self.assertEqual(binding['members']['active_refute_v2/example.response.bin'],
                         hashlib.sha256(original).hexdigest())

    def test_actual_archive_replay_preserves_both_original_first_outputs(self):
        restored = self.root / 'restored'
        e.restore_history(restored)
        cases = e.base.load_fixture('inputs_dev.json')
        for arm in e.base.ARMS:
            with self.subTest(arm=arm):
                rows, summary = e.replay(e.HISTORY / arm / 'prepared/manifest.json', restored / arm, cases, arm)
                expected = e.integrity.read(e.HISTORY / arm / 'first_score/compiled_first.json')
                self.assertEqual(rows, expected)
                self.assertEqual(len(rows), 48)
                self.assertEqual(summary['planned_requests'], 48)
                self.assertEqual(sum(summary['outcomes'].values()), 48)

    def test_actual_profile_artifact_has_no_measured_v3_profile(self):
        from ...eval import model_profiles
        path = e.ROOT / 'docs/research/w3_directed_commitment_v1/replay_first/profiles.json'
        profiles = e.integrity.read(path)
        self.assertEqual(model_profiles.validate(profiles), [])
        self.assertEqual(profiles['new_model_calls'], 0)
        self.assertEqual({p['key']['recipe_id'] for p in profiles['profiles']}, set(e.base.ARMS))
        self.assertEqual(profiles['compensation']['status'], 'proposed_unrun')
        self.assertIsNone(profiles['compensation']['ablation']['current_results'])
        accuracies = {p['key']['recipe_id']: (p['metrics']['accuracy']['numerator'],
                                             p['metrics']['accuracy']['denominator'])
                      for p in profiles['profiles']}
        self.assertEqual(accuracies, {'historical_refute_v1': (42, 48), 'active_refute_v2': (45, 48)})


if __name__ == '__main__':
    unittest.main()
