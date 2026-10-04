"""Reproduce the audit benchmark using generated data outside the repository."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import resource
import sqlite3
import sys
import time


def find_repo() -> Path:
    for parent in Path(__file__).resolve().parents:
        if (parent / 'loom/tools/eval/archive_cost.py').is_file():
            return parent
    raise ValueError('Cannot locate repository; pass --repo PATH')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module', choices=('baseline', 'current'))
    parser.add_argument('--repo', type=Path, help='repository root; otherwise found from script location')
    parser.add_argument('--db', type=Path, required=True, help='generated database path outside the repository')
    parser.add_argument('--create', action='store_true', help='create synthetic data; refuses to overwrite any file')
    parser.add_argument('--rows', type=int, default=600000)
    args = parser.parse_args()
    repo = args.repo.resolve() if args.repo else find_repo()
    db = args.db.resolve()
    if db.is_relative_to(repo):
        parser.error('--db must be outside the repository to keep generated databases unpublished')
    if args.rows < 0:
        parser.error('--rows must be nonnegative')
    if args.create:
        if db.exists():
            parser.error('database already exists; choose a fresh --db path')
        db.parent.mkdir(parents=True, exist_ok=True)
        with sqlite3.connect(db) as con:
            con.execute('CREATE TABLE messages(id TEXT, conv_id TEXT, role TEXT, text TEXT, status TEXT)')
            statuses = ('active', 'version', 'excluded', 'deleted', None)
            for start in range(0, args.rows, 5000):
                con.executemany('INSERT INTO messages VALUES (?,?,?,?,?)', (
                    (str(i), str(i // 2), 'tool' if i % 7 == 0 else ('user' if i % 2 else 'assistant'),
                     'synthetic ' + ('x' * 118), statuses[i % 5])
                    for i in range(start, min(start + 5000, args.rows))))
        print(json.dumps({'rows': args.rows, 'database_bytes': db.stat().st_size}))
        return
    module_path = (Path(__file__).with_name('archive_cost_before.py') if args.module == 'baseline'
                   else repo / 'loom/tools/eval/archive_cost.py')
    spec = importlib.util.spec_from_file_location('archive_cost', module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    before = resource.getrusage(resource.RUSAGE_SELF)
    started = time.perf_counter()
    stats = module.archive_stats(db)
    elapsed = time.perf_counter() - started
    after = resource.getrusage(resource.RUSAGE_SELF)
    # Linux reports KiB, macOS reports bytes. Benchmark receipts were measured
    # on Linux; convert macOS output so the portable script uses the same unit.
    peak_rss_kib = after.ru_maxrss / 1024 if sys.platform == 'darwin' else after.ru_maxrss
    print(json.dumps({
        'seconds': elapsed,
        'cpu_seconds': after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime,
        'peak_rss_kib': peak_rss_kib,
        'legacy_messages': stats['messages'],
        'legacy_chars_active': stats['chars_active'],
        'raw': stats.get('raw'),
        'projected': stats.get('projected'),
        'scope_groups': len(stats.get('conversation_scope_groups', [])),
    }, sort_keys=True))


if __name__ == '__main__':
    main()
