"""Offline public-C-ABI regression probe; preserves the first output."""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output already exists; retain the first result')
    lib = ctypes.CDLL(str(args.library.resolve()))
    lib.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.loom_init_ex.restype = ctypes.c_void_p
    lib.loom_shutdown.argtypes = [ctypes.c_void_p]
    lib.loom_free_string.argtypes = [ctypes.c_void_p]
    lib.loom_kb_query.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.loom_kb_query.restype = ctypes.c_void_p
    lib.loom_knowledge_run.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p]
    lib.loom_knowledge_run.restype = ctypes.c_void_p

    def take(pointer):
        if not pointer:
            raise RuntimeError('null native result')
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            lib.loom_free_string(pointer)

    with tempfile.TemporaryDirectory(prefix='loom-latest-run-probe-') as directory:
        error = ctypes.c_void_p()
        opts = json.dumps({'data_dir': directory, 'start_workers': False}).encode()
        ctx = lib.loom_init_ex(opts, ctypes.byref(error))
        if not ctx:
            raise RuntimeError(take(error.value))
        try:
            setup = take(lib.loom_knowledge_run(ctx, b'{"stages":["catalog"]}', None, None))
            if setup.get('status') != 'done':
                raise RuntimeError(setup)
            initial = take(lib.loom_kb_query(ctx, b'{"what":"stats"}'))
            expected = initial['run']
            with sqlite3.connect(Path(directory) / 'chatadhd.db') as db:
                db.execute('UPDATE loom_kb_runs SET created=? WHERE run_id=?',
                           ('2000-01-01T00:00:00Z', expected))
                for index in range(51):
                    db.execute('''INSERT INTO loom_kb_runs
                        (run_id,archive_run_id,pack_hash,status,inputs,summary,created,replayed_seq)
                        SELECT ?,archive_run_id,pack_hash,'running','{}','{}',?,0
                        FROM loom_kb_runs WHERE run_id=?''',
                               (f'kr_probe_{index:03d}', '2026-01-01T00:00:00Z', expected))
            automatic = take(lib.loom_kb_query(ctx, b'{"what":"stats"}'))
            explicit = take(lib.loom_kb_query(ctx, json.dumps({'what': 'stats', 'run': expected}).encode()))
            result = {
                'schema': 'loom.latest_native_run_probe/1',
                'fixture': 'one completed empty catalog run, then 51 newer synthetic running rows',
                'library_sha256': hashlib.sha256(args.library.read_bytes()).hexdigest(),
                'instrument_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'expected_run': expected, 'automatic': automatic, 'explicit': explicit,
                'automatic_selects_completed': automatic.get('run') == expected,
                'explicit_selects_requested': explicit.get('run') == expected,
                'remote_model_calls': 0,
            }
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open('x', encoding='utf-8') as output:
                json.dump(result, output, ensure_ascii=False, indent=2)
                output.write('\n')
            print(json.dumps({key: result[key] for key in
                              ('automatic_selects_completed', 'explicit_selects_requested')}))
            return 0 if result['automatic_selects_completed'] and result['explicit_selects_requested'] else 1
        finally:
            lib.loom_shutdown(ctx)


if __name__ == '__main__':
    raise SystemExit(main())
