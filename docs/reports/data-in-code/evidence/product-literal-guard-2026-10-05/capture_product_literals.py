#!/usr/bin/env python3
"""Reproduce the offline strict audit and retain exact negative evidence."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def encode(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def save(path, value):
    path.write_bytes(encode(value))


def snapshot(root, policy, additional):
    files, pending = set(additional), [root / scope for scope in policy['scopes']]
    while pending:
        path = pending.pop()
        try:
            metadata = path.lstat()
        except FileNotFoundError:
            if path.relative_to(root).as_posix() in policy['optional_scopes']:
                continue
            raise
        if stat.S_ISDIR(metadata.st_mode):
            with os.scandir(path) as entries:
                pending.extend(Path(entry.path) for entry in entries)
        else:
            files.add(path)
    records = []
    for path in sorted(files):
        metadata = path.lstat()
        record = {'path': path.relative_to(root).as_posix(), 'bytes': metadata.st_size}
        resolved = path.resolve()
        if path.is_symlink():
            record['link_target'] = os.readlink(path)
        if resolved.is_relative_to(root) and stat.S_ISREG(path.stat().st_mode):
            raw = path.read_bytes()
            record.update(bytes=len(raw), sha256=digest(raw))
        else:
            record['sha256'] = None
        records.append(record)
    return {'schema': 'loom.product_literal_source_snapshot/1', 'files': records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--expected-source-head')
    parser.add_argument('--expected-exit', type=int, choices=(0, 1, 2), default=1)
    args = parser.parse_args()
    root, out = args.repo_root.resolve(), args.output.resolve()
    head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, check=True,
                          capture_output=True, text=True).stdout.strip()
    if args.expected_source_head and head != args.expected_source_head:
        raise SystemExit('Source HEAD differs from the explicitly requested capture.')
    pack = root / 'loom/data/validation/product_literals.pack'
    schema = root / 'docs/contracts/product_literals.schema.json'
    guard = root / 'loom/src/util/product_literal_guard.py'
    policy = json.loads(pack.read_bytes())
    if any(out == (root / scope).resolve() or out.is_relative_to((root / scope).resolve()) for scope in policy['scopes']):
        raise SystemExit('Evidence destination is inside a product scope.')
    out.mkdir(parents=True, exist_ok=True)
    additional = {pack, schema, guard, Path(__file__).resolve(),
                  root / 'loom/tests/compat/test_product_literal_guard.py'}
    for generation in policy['generated_outputs']:
        additional.add(root / generation['generator']['path'])
        additional.add(root / generation['path'])
        additional.update(root / item['path'] for item in generation['inputs'])
    before = snapshot(root, policy, additional)
    save(out / 'source-snapshot-before.json', before)
    report_path, manifest_path = out / 'strict-report.json', out / 'strict-manifest.json'
    command = [sys.executable, '-B', str(guard), '--root', str(root), '--pack', str(pack),
               '--schema', str(schema), '--check', '--report', str(report_path),
               '--manifest', str(manifest_path)]
    result = subprocess.run(command, cwd=root, capture_output=True, check=False,
                            env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'})
    (out / 'strict.stdout').write_bytes(result.stdout)
    (out / 'strict.stderr').write_bytes(result.stderr)
    after = snapshot(root, policy, additional)
    save(out / 'source-snapshot-after.json', after)
    changed = before != after
    raw = report_path.read_bytes()
    report = json.loads(raw)
    manifest = json.loads(manifest_path.read_bytes())
    compressed = out / 'strict-report.json.gz'
    with compressed.open('wb') as target:
        with gzip.GzipFile(filename='', mode='wb', fileobj=target, mtime=0) as stream:
            stream.write(raw)
    if gzip.decompress(compressed.read_bytes()) != raw:
        raise SystemExit('Compressed evidence does not preserve the exact original report.')
    report_path.unlink()
    file_rows = {row['path']: dict(row, unclassified=0, allowed=0, issue_codes=[])
                 for row in manifest['source_files']}
    for finding in report['findings']:
        file_rows[finding['path']][finding['status'].lower()] += 1
    for issue in report['issues']:
        if issue.get('path') in file_rows:
            file_rows[issue['path']]['issue_codes'].append(issue['code'])
    owners = {'schema': 'loom.product_literal_owner_debt/1', 'source_head': head,
              'routing': 'Longest data-declared path prefix; fallback owner 9 requires assignment. Shared files need owner review.',
              'counts': report['counts'], 'by_owner': report['by_owner'],
              'file_rows': [file_rows[name] for name in sorted(file_rows)],
              'discovery_errors': manifest.get('discovery_errors', []),
              'issues_by_code': dict(Counter(item['code'] for item in report['issues'])),
              'full_findings': {'path': compressed.name, 'uncompressed_sha256': digest(raw)}}
    save(out / 'owner-debt.json', owners)
    receipt = {'schema': 'loom.product_literal_actual_capture/1', 'source_head': head,
               'command': command, 'cwd': str(root), 'exit_code': result.returncode,
               'expected_exit': args.expected_exit, 'valid': report['valid'],
               'counts': report['counts'], 'by_owner': report['by_owner'],
               'source_snapshot_files': len(before['files']),
               'source_snapshots_identical': not changed,
               'source_snapshot_sha256': digest(encode(before)),
               'source_writes_measured': 0 if not changed else None,
               'uncompressed_report_sha256': digest(raw), 'uncompressed_report_bytes': len(raw),
               'compressed_report_sha256': digest(compressed.read_bytes()),
               'compressed_report_bytes': compressed.stat().st_size,
               'manifest_sha256': digest(manifest_path.read_bytes()),
               'guard_sha256': digest(guard.read_bytes()), 'pack_sha256': digest(pack.read_bytes()),
               'schema_sha256': digest(schema.read_bytes()), 'capture_sha256': digest(Path(__file__).read_bytes()),
               'test_source_sha256': digest((root / 'loom/tests/compat/test_product_literal_guard.py').read_bytes()),
               'network_calls': 0, 'generator_output_writes': 0,
               'acceptance': 'Strict negative retained; no repository R42 acceptance claim.' if not report['valid'] else 'Review the declared scope and manual reasons separately.'}
    save(out / 'actual-capture-receipt.json', receipt)
    print(json.dumps({key: receipt[key] for key in ('source_head', 'exit_code', 'valid', 'counts', 'source_snapshots_identical', 'network_calls')}, sort_keys=True))
    return 0 if result.returncode == args.expected_exit and not changed else 1


if __name__ == '__main__':
    raise SystemExit(main())
