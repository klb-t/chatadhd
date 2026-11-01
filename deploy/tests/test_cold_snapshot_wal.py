"""One nonempty recovery-WAL gate on a NEW copy of an already closed synthetic DB.
No account generator, credentials, source checkpoint, immutable SQLite or servers.
Use an explicit expected hash and a fresh --work; never reuse an interrupted run.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys


def sha_noatime(path, destination=None):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NOATIME)
    try:
        before = os.fstat(fd)
        h = hashlib.sha256()
        target = open(destination, 'xb') if destination else None
        try:
            while True:
                chunk = os.read(fd, 1024 * 1024)
                if not chunk:
                    break
                h.update(chunk)
                if target:
                    target.write(chunk)
            if target:
                target.flush()
                os.fsync(target.fileno())
        finally:
            if target:
                target.close()
        after = os.fstat(fd)
        fields = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns', 'st_atime_ns')
        assert all(getattr(before, k) == getattr(after, k) for k in fields), 'input changed during copy/hash'
        return h.hexdigest()
    finally:
        os.close(fd)


def writer(db):
    conn = sqlite3.connect(db)
    assert conn.execute('PRAGMA journal_mode=WAL').fetchone()[0] == 'wal'
    conn.execute('PRAGMA wal_autocheckpoint=0')
    conn.execute('PRAGMA synchronous=FULL')
    assert conn.execute('PRAGMA wal_autocheckpoint').fetchone()[0] == 0
    assert conn.execute('PRAGMA synchronous').fetchone()[0] == 2
    assert conn.execute("SELECT count(*) FROM sqlite_master WHERE name='cold_restore_wal_canary'").fetchone()[0] == 0
    before = sha_noatime(db)
    conn.execute('BEGIN IMMEDIATE')
    conn.execute('CREATE TABLE cold_restore_wal_canary (id INTEGER PRIMARY KEY, value TEXT NOT NULL)')
    conn.execute('INSERT INTO cold_restore_wal_canary VALUES (?, ?)', (1, 'synthetic-committed-wal'))
    conn.commit()
    assert not conn.in_transaction
    assert conn.execute('SELECT count(*) FROM cold_restore_wal_canary').fetchone()[0] == 1
    after = sha_noatime(db)
    assert before == after, 'change reached main DB instead of remaining solely in WAL'
    wal = Path(str(db) + '-wal')
    assert wal.stat().st_size > 32
    data = {'commit_complete': True, 'main_before_sha256': before, 'main_after_sha256': after,
            'wal_bytes': wal.stat().st_size, 'wal_sha256': sha_noatime(wal),
            'sqlite_version': sqlite3.sqlite_version, 'autocheckpoint': 0, 'synchronous': 'FULL',
            'termination': 'os._exit(0) after COMMIT; no SQLite close/checkpoint'}
    payload = (json.dumps(data) + '\n').encode()
    while payload:
        payload = payload[os.write(1, payload):]
    # Deliberately skip SQLite close: simulate a terminated process after durable
    # COMMIT, leaving genuine SQLite recovery WAL. Only this own child exits.
    os._exit(0)


def readonly_check(db, expected):
    conn = sqlite3.connect(db.as_uri() + '?mode=ro', uri=True)
    try:
        conn.execute('PRAGMA query_only=ON')
        assert conn.execute('PRAGMA integrity_check').fetchall() == [('ok',)]
        assert conn.execute('PRAGMA foreign_key_check').fetchall() == []
        count = conn.execute("SELECT count(*) FROM sqlite_master WHERE name='cold_restore_wal_canary'").fetchone()[0]
        assert count == int(expected), 'WAL contribution does not match expectation'
        if expected:
            assert conn.execute('SELECT id,value FROM cold_restore_wal_canary').fetchall() == [(1, 'synthetic-committed-wal')]
        return {'integrity': 'ok', 'foreign_key_errors': 0, 'canary_table_count': count,
                'committed_canary_rows': 1 if expected else 0, 'mode': 'ro', 'uses_wal': expected}
    finally:
        conn.close()


def gate(args):
    os.umask(0o077)
    work = Path(args.work).absolute()
    work.mkdir(mode=0o700)
    closed = Path(args.closed_sqlite).absolute()
    tool = Path(args.tool).absolute()
    result = {'success': False, 'tool_sha256': sha_noatime(tool), 'closed_input_sha256': args.expected_sha256}
    commands = []
    def invoke(*parts):
        cmd = [sys.executable, str(tool), *map(str, parts)]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        commands.append({'argv': cmd, 'exit_code': r.returncode, 'stdout': r.stdout, 'stderr': r.stderr})
        (work/'tool-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        assert r.returncode == 0 and not r.stderr, 'cold tool refusal; inspect preserved tool-commands.json'
        data = json.loads(r.stdout)
        assert data['ok']
        return data
    try:
        source = work/'wal-source'
        source.mkdir(mode=0o700)
        db = source/'runtime.sqlite'
        assert sha_noatime(closed, db) == args.expected_sha256
        child = subprocess.run([sys.executable, __file__, '--writer', str(db)], capture_output=True, text=True, timeout=30)
        (work/'writer.log').write_text(child.stdout + child.stderr)
        assert child.returncode == 0 and not child.stderr, 'fixture writer failed'
        result['writer'] = json.loads(child.stdout)
        result['writer']['exit_code'] = child.returncode
        # run() waited for the sole SQLite child; no source DB is opened again.
        assert sha_noatime(db) == result['writer']['main_before_sha256']
        assert sha_noatime(Path(str(db) + '-wal')) == result['writer']['wal_sha256']
        control = work/'main-only-control'
        control.mkdir(mode=0o700)
        sha_noatime(db, control/'runtime.sqlite')
        result['main_only_control'] = readonly_check(control/'runtime.sqlite', False)
        pre = invoke('preflight', '--source', source, '--allowed-root', work,
                     '--receipt', work/'preflight.json', '--writers-stopped')
        backup = invoke('backup', '--source', source, '--allowed-root', work,
                        '--destination', work/'backup', '--destination-root', work,
                        '--preflight', work/'preflight.json', '--preflight-sha256', pre['receipt_sha256'], '--writers-stopped')
        restored = invoke('restore', '--snapshot', work/'backup', '--allowed-root', work,
                          '--destination', work/'restored', '--destination-root', work,
                          '--manifest-sha256', backup['manifest_sha256'], '--writers-stopped')
        assert restored['manifest_sha256'] == backup['manifest_sha256']
        restored_db = work/'restored/data/runtime.sqlite'
        assert sha_noatime(restored_db) == result['writer']['main_before_sha256']
        restored_wal = Path(str(restored_db) + '-wal')
        assert restored_wal.stat().st_size == result['writer']['wal_bytes']
        assert sha_noatime(restored_wal) == result['writer']['wal_sha256']
        result['restored_with_wal'] = readonly_check(restored_db, True)
        spec = importlib.util.spec_from_file_location('cold_snapshot_under_test', tool)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        expected = json.loads((work/'preflight.json').read_text())['tree']
        tree = module.ColdTree(source)
        try:
            assert tree.scan() == expected
            assert tree.scan() == expected
        finally:
            tree.close()
        assert sha_noatime(closed) == args.expected_sha256
        result.update(success=True, source_unchanged=True, accepted_input_unchanged=True,
                      source_entries=len(expected), sqlite_locked=pre['sqlite_locked'],
                      snapshot_manifest_sha256=backup['manifest_sha256'],
                      preflight_sha256=pre['receipt_sha256'])
    finally:
        (work/'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--writer')
    p.add_argument('--tool')
    p.add_argument('--closed-sqlite')
    p.add_argument('--expected-sha256')
    p.add_argument('--work')
    a = p.parse_args()
    if a.writer:
        writer(Path(a.writer).absolute())
    else:
        if not all([a.tool, a.closed_sqlite, a.expected_sha256, a.work]):
            p.error('gate requires tool/closed-sqlite/expected-sha256/work')
        gate(a)
