#!/usr/bin/env python3
"""Local stdlib audit probes; no network/model calls, synthetic input only."""
import argparse
import ast
import contextlib
import importlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
repo = args.repo.resolve()
sys.path.insert(0, str(repo / 'server'))
tmp = tempfile.TemporaryDirectory(prefix='ageds-philosophy-audit-')
for key, value in {'EW_DATA_DIR': tmp.name, 'EW_DB_PATH': str(Path(tmp.name)/'audit.db'), 'EW_STORE_DIR': str(Path(tmp.name)/'store'), 'EW_WHISPER_MODEL': 'audit-stub-only'}.items():
    os.environ[key] = value

class SemanticProbes(unittest.TestCase):
    def test_asr_model_environment_consumed_but_vad_forced(self):
        # Execute unchanged adapter class/function AST without importing unrelated
        # HTTP/media dependencies; actual ASR boundary is a stub, no model loaded.
        from app.config import settings
        import dataclasses
        import importlib.metadata
        import platform
        from numbers import Integral, Real
        from typing import BinaryIO
        source = ast.parse((repo/'server/app/worker.py').read_text())
        nodes = [n for n in source.body if getattr(n, 'name', None) in
                 {'_version', '_json_number', 'TranscriptionResult', 'FasterWhisperAdapter'}]
        namespace = dict(settings=settings, dataclasses=dataclasses, platform=platform,
                         importlib=importlib, Integral=Integral, Real=Real, BinaryIO=BinaryIO,
                         dataclass=dataclasses.dataclass, field=dataclasses.field)
        exec(compile(ast.Module(body=nodes, type_ignores=[]), 'worker.py adapter AST', 'exec'), namespace)
        FasterWhisperAdapter = namespace['FasterWhisperAdapter']
        seen = {}
        class FakeModel:
            def __init__(self, model, **kwargs):
                seen.update(model=model, constructor=kwargs)
            def transcribe(self, audio, **kwargs):
                seen['transcribe'] = kwargs
                return [], types.SimpleNamespace(language=None)
        with patch.dict(sys.modules, {'faster_whisper': types.SimpleNamespace(WhisperModel=FakeModel)}):
            result = FasterWhisperAdapter().transcribe(io.BytesIO(b'synthetic-no-audio'))
        self.assertEqual(seen['model'], 'audit-stub-only')
        self.assertEqual(seen['transcribe'], {'word_timestamps': True, 'vad_filter': True})
        self.assertEqual(result.text, '')

    def test_domain_header_dictionary_controls_recognition(self):
        from app.scanner import _kind_for_header, _phone
        self.assertEqual(_kind_for_header('numer telefonu'), 'phone')
        self.assertIsNone(_kind_for_header('telefono'))
        self.assertIsNone(_phone('12345'))
        self.assertEqual(_phone('123456'), '123456')

    def test_scan_limits_are_consumed_and_reported(self):
        from app.scanner import ScanLimits, scan_sources
        source = Path(tmp.name)/'source'
        source.mkdir(exist_ok=True)
        (source/'a.txt').write_text('synthetic A')
        (source/'b.txt').write_text('synthetic B')
        limited = scan_sources(source, limits=ScanLimits(max_files=1))
        full = scan_sources(source, limits=ScanLimits(max_files=2))
        self.assertEqual(len(limited['files']), 1)
        self.assertEqual(len(full['files']), 2)
        self.assertEqual(limited['policy']['limits']['max_files'], 1)
        self.assertFalse(limited['coverage']['complete'])

    def test_queue_deduplicates_by_artifact_not_recipe(self):
        from app.db import init_db, session
        from app.transcription import queue_transcription
        init_db()
        with session() as db:
            aid = db.execute("INSERT INTO artifacts(original_name) VALUES ('synthetic')").lastrowid
        first = queue_transcription(aid, 20)
        second = queue_transcription(aid, 50)
        self.assertEqual(first, second)
        with session() as db:
            row = dict(db.execute('SELECT * FROM jobs WHERE id=?', (first,)).fetchone())
        self.assertEqual(row['priority'], 50)
        self.assertEqual(json.loads(row['payload_json']), {})
        self.assertNotIn('model', row)

    def test_metadata_export_has_no_live_configuration_entities(self):
        from app.db import init_db
        from app.packages import export_metadata_package
        init_db()
        payload = export_metadata_package()
        tables = set(payload['tables'])
        self.assertTrue({'links','jobs','processing_runs','derived_text'} <= tables)
        self.assertFalse({'settings','user_profiles','methods','models','workflows'} & tables)
        self.assertNotIn('audit-stub-only', json.dumps(payload))

    def test_search_hard_ceiling_rejects_request(self):
        from app.search import validate_query, SearchQueryError
        with self.assertRaises(SearchQueryError):
            validate_query('anything', limit=201)

buffer = io.StringIO()
result = unittest.TextTestRunner(stream=buffer, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(SemanticProbes))
receipt = {
    'repo': 'klb-t/AGEDS',
    'sha': subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
    'tests_run': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
    'passed': result.wasSuccessful(),
    'runtime': 'Python stdlib; synthetic temporary SQLite/files; fake faster-whisper boundary',
    'model_calls': 0, 'network_calls': 0,
    'limitations': ['No Android/JVM/device or browser execution', 'No real ASR or quality measurement', 'ASR probe executes unchanged adapter AST isolated from unavailable anyio/HTTP modules', 'Export omission probe proves tested snapshot, complemented by static consumer trace'],
    'log': buffer.getvalue(),
}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n')
print(buffer.getvalue())
tmp.cleanup()
sys.exit(0 if result.wasSuccessful() else 1)
