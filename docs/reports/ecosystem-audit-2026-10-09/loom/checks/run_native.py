#!/usr/bin/env python3
"""Build unchanged loom sources and audit probes with an explicit SQLite source.

Requires cc/c++, SQLite amalgamation and the pinned repo; performs no download.
All compiled files and databases are temporary. This script does not use CI.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--repo', required=True, type=Path)
p.add_argument('--sqlite-dir', required=True, type=Path)
a = p.parse_args()
repo, sqlite_dir = a.repo.resolve(), a.sqlite_dir.resolve()
checks = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='loom-audit-native-') as tmp:
    tmp = Path(tmp)
    sqlite_obj = tmp / 'sqlite3.o'
    subprocess.run(['cc', '-O0', '-c', str(sqlite_dir / 'sqlite3.c'), '-o', str(sqlite_obj)], check=True)
    sources = [str(repo / 'src' / name) for name in ['db.cpp', 'db_graph.cpp', 'db_messages.cpp', 'event_bus.cpp']]
    tests = {
        'db_compat_test': repo / 'tests/db_compat_test.cpp',
        'event_bus_test': repo / 'tests/event_bus_test.cpp',
        'reproduce_native': checks / 'reproduce_native.cpp',
    }
    results = {}
    for name, test in tests.items():
        target = tmp / name
        cmd = ['c++', '-std=c++20', '-I', str(repo / 'include'), '-I', str(sqlite_dir),
               *sources, str(test), str(sqlite_obj), '-ldl', '-pthread', '-o', str(target)]
        subprocess.run(cmd, check=True)
        run = [str(target)]
        if name == 'reproduce_native':
            run.append(str(tmp / 'future-version.db'))
        results[name] = subprocess.check_output(run, text=True).strip()
    print(json.dumps({
        'results': results,
        'sqlite_sha256': {f: hashlib.sha256((sqlite_dir / f).read_bytes()).hexdigest()
                          for f in ['sqlite3.c', 'sqlite3.h']},
        'scope': 'Direct compiler build, not CMake/CTest or sanitizer execution',
    }, indent=2))
