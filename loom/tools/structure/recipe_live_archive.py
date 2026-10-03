#!/usr/bin/env python3
"""Exclusive, exact evidence backup and offline replay; never reads credentials."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
try:
    from . import recipe_live_pilot as runner
except ImportError:
    import recipe_live_pilot as runner


def archive(prepared_dir, output, *, score_path=None):
    prepared = Path(prepared_dir)
    manifest = runner.pilot.read_json(prepared / 'manifest.json')
    runner.validate_manifest(manifest)
    directory = Path(manifest['run_dir'])
    # Share the execution lock: never snapshot a POST in progress.
    with runner.safe._lock(directory):
        ledger = runner.pilot.read_json(directory / 'ledger.json')
        attempts = runner.validate_ledger(ledger, manifest, directory)
        if (any(a['state'] == 'started' for a in attempts) or
                not ledger.get('stopped_reason') and len(attempts) != len(manifest['requests'])):
            raise runner.safe.RunnerError('recipe_archive_requires_closed_first_series')
        files = {}
        for name in ('plan.json', 'manifest.json', 'endpoint_snapshot.json'):
            files['prepared/' + name] = (prepared / name).read_bytes()
        files['run/ledger.json'] = (directory / 'ledger.json').read_bytes()
        for i, row in enumerate(attempts):
            for name in (f'{i:02d}.started.json', f'{i:02d}.result.json'):
                files['run/' + name] = (directory / name).read_bytes()
            if 'response_file' in row:
                name = row['response_file']
                files['run/' + name] = (directory / name).read_bytes()
        for path, expected in manifest['code_sha256'].items():
            raw = (runner.REPO / path).read_bytes()
            if hashlib.sha256(raw).hexdigest() != expected:
                raise runner.safe.RunnerError('recipe_executed_code_changed_before_archive')
            files['replay/' + path] = raw
        files['replay/loom/tools/structure/recipe_live_score.py'] = (runner.HERE / 'recipe_live_score.py').read_bytes()
        for name in ('requests.zip', 'gold.json', 'corpus.json', 'recipes.json', 'manifest.json', 'source_freeze.json'):
            files['replay/loom/tests/fixtures/eval/jev_recipes_v1/' + name] = (runner.FIXTURE / name).read_bytes()
        if score_path is not None:
            raw = Path(score_path).read_bytes()
            score = runner.safe.parse_json(raw)
            if score.get('manifest_hash') != runner.safe.digest(manifest):
                raise runner.safe.RunnerError('recipe_archive_score_manifest_mismatch')
            files['analysis/first_score.json'] = raw
        files['REPLAY.md'] = (
            '# Offline first-response replay\n\n'
            'Extract this archive into a new directory, then run there:\n\n'
            '```sh\npython replay/loom/tools/structure/recipe_live_score.py '
            '--manifest prepared/manifest.json --run-dir run --output replayed_score.json\n```\n\n'
            'This scores exact archived first bytes without keys or network. The manifest retains '
            'its original absolute run path for identity; the scorer reads the explicitly supplied '
            'evidence copy. INVENTORY.json hashes all archive payloads. Code and frozen T3 inputs '
            'are included so future code edits do not invalidate this replay.\n\n'
            'Never rerun paid calls to reconstruct lost responses. Before any execution resume '
            'after a workspace restore, restore the original run/ receipts and ledger checkpoint '
            'into the canonical run directory recorded in prepared/manifest.json. Do not change '
            'the prepared manifest or key cap. The existing dedicated USD 2 nonresetting key '
            'ceiling remains shared with all jobs. Credentials and lock files are not archived.\n'
        ).encode('utf-8')
        inventory = {'schema': 'loom.jev_recipe_evidence_archive/1', 'experiment_id': runner.EXPERIMENT,
                     'manifest_hash': runner.safe.digest(manifest), 'archived_at': runner.safe._utc(),
                     'attempted_calls': len(attempts), 'completed_calls': sum(a['state'] == 'completed' for a in attempts),
                     'stopped_reason': ledger.get('stopped_reason'),
                     'files': {name: {'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}
                               for name, raw in sorted(files.items())}}
        files['INVENTORY.json'] = runner.safe.canonical(inventory) + b'\n'
        if sum(len(raw) for raw in files.values()) > 128 * 1024 * 1024:
            raise runner.safe.RunnerError('recipe_archive_size_limit')
        buffer = io.BytesIO()
        with ZipFile(buffer, 'w', compression=ZIP_DEFLATED, compresslevel=9) as z:
            for name, raw in sorted(files.items()):
                info = ZipInfo(name, date_time=(2026, 9, 30, 0, 0, 0))
                info.compress_type = ZIP_DEFLATED
                info.external_attr = 0o100600 << 16
                z.writestr(info, raw, compress_type=ZIP_DEFLATED, compresslevel=9)
        runner.write_bytes_new(output, buffer.getvalue())
        return inventory


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--score', type=Path)
    args = parser.parse_args(argv)
    try:
        result = archive(args.prepared_dir, args.output, score_path=args.score)
        print(json.dumps({'archived_calls': result['attempted_calls'], 'completed_calls': result['completed_calls'],
                          'files': len(result['files']), 'stopped_reason': result['stopped_reason']}))
        return 0
    except (runner.safe.RunnerError, OSError, ValueError, KeyError, TypeError):
        print(json.dumps({'ok': False, 'error': 'recipe_archive_rejected'}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
