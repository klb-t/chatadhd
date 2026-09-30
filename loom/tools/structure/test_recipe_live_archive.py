"""Exact first-byte backup survives path/code relocation; all responses mocked."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from zipfile import ZipFile
try:
    from . import recipe_live_archive as archive
    from . import recipe_live_score as score
    from .test_recipe_live_pilot import HTTP
except ImportError:
    import recipe_live_archive as archive
    import recipe_live_score as score
    from test_recipe_live_pilot import HTTP
p = archive.runner


class RecipeArchiveTests(unittest.TestCase):
    def test_exact_archive_and_relocated_offline_scorer_replay(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); prepared = root / 'prepared'; run_dir = root / 'run'
            m = p.prepare(p.frozen_plan(), prepared, transport_fn=HTTP(), run_dir=run_dir)
            result = p.run(m, transport_fn=HTTP(), key_loader=lambda: 'unit-secret')
            report = score.score(m, run_dir)
            score_path = root / 'first_score.json'; p.write_new(score_path, report)
            output = root / 'evidence.zip'
            inventory = archive.archive(prepared, output, score_path=score_path)
            self.assertEqual(inventory['completed_calls'], 32)
            extracted = root / 'extracted'
            with ZipFile(output) as z:
                for name, metadata in inventory['files'].items():
                    raw = z.read(name)
                    self.assertEqual(p.hashlib.sha256(raw).hexdigest(), metadata['sha256'])
                    self.assertEqual(len(raw), metadata['bytes'])
                    self.assertNotIn(b'unit-secret', raw)
                # Own generated bounded archive, not an untrusted uploaded zip.
                z.extractall(extracted)
            for attempt in result['attempts']:
                name = attempt['response_file']
                self.assertEqual((extracted / 'run' / name).read_bytes(), (run_dir / name).read_bytes())
            execution = subprocess.run([sys.executable, 'replay/loom/tools/structure/recipe_live_score.py',
                '--manifest', 'prepared/manifest.json', '--run-dir', 'run', '--output', 'replayed_score.json'],
                cwd=extracted, capture_output=True, text=True, timeout=20)
            self.assertEqual(execution.returncode, 0, execution.stderr + execution.stdout)
            self.assertEqual(p.pilot.read_json(extracted / 'replayed_score.json'), report)
            with self.assertRaises(FileExistsError):
                archive.archive(prepared, output)

    def test_archive_rejects_open_or_tampered_first_series(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); prepared = root / 'prepared'; run_dir = root / 'run'
            m = p.prepare(p.frozen_plan(), prepared, transport_fn=HTTP(), run_dir=run_dir)
            with self.assertRaises(KeyboardInterrupt):
                p.run(m, transport_fn=HTTP(failure=KeyboardInterrupt()), key_loader=lambda: 'unit-secret')
            with self.assertRaisesRegex(p.safe.RunnerError, 'closed_first_series'):
                archive.archive(prepared, root / 'no.zip')
            def forbidden(*args):
                self.fail('Interrupted first attempt cannot call network')
            p.run(m, transport_fn=forbidden, key_loader=forbidden)
            inventory = archive.archive(prepared, root / 'closed.zip')
            self.assertEqual(inventory['attempted_calls'], 1)
            self.assertEqual(inventory['completed_calls'], 0)
            (run_dir / '00.result.json').write_bytes(b'{}')
            with self.assertRaisesRegex(p.safe.RunnerError, 'terminal_receipt'):
                archive.archive(prepared, root / 'bad.zip')


if __name__ == '__main__':
    unittest.main()
