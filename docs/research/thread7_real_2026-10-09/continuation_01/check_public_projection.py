"""Scan the new public delta using private source canaries without logging them.

This is a bounded disclosure check, not proof that arbitrary personal data is
absent. Public aggregates and producer code still require reviewer inspection.
The source files are read only; no credential locations are searched.
"""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for item in value.values():
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)


def parts(name, raw, depth=0):
    if depth > 8:
        raise ValueError('archive_depth_exceeded')
    yield name, raw
    if raw.startswith(b'\x1f\x8b'):
        yield from parts(name + '/gzip', gzip.decompress(raw), depth + 1)
    elif raw.startswith(b'PK\x03\x04'):
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            for member in archive.infolist():
                if not member.is_dir():
                    yield from parts(name + '/' + member.filename, archive.read(member), depth + 1)
    else:
        try:
            value = json.loads(raw)
        except (ValueError, UnicodeError):
            return
        if isinstance(value, dict) and value.get('schema') in ('loom.method_graph/1', 'loom.method_graph_fixture/1'):
            from loom.tools.seeding.method_graph import recover_files
            for role, data in recover_files(value).items():
                yield from parts(name + '/recovered/' + role, data, depth + 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--historical', type=Path)
    parser.add_argument('--base', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.repo))
    canaries = set()
    source_files = sorted(args.sources.glob('*.json'))
    historical_files = sorted(args.historical.glob('*/original-conversation.json')) if args.historical else []
    if args.historical and len(historical_files) != 3:
        raise ValueError('historical_source_count_mismatch')
    native_values = [json.loads(path.read_bytes()) for path in historical_files]
    for path in source_files:
        value = json.loads(path.read_bytes())
        native = value.get('native_conversation', {})
        if not native:
            raise ValueError('native_source_required')
        native_values.append(native)
    for native in native_values:
        for text in strings(native):
            if len(text) >= 160 and len(text.strip()) >= 100:
                for piece in (text[:160], text[-160:]):
                    canaries.add(piece.encode())
                    canaries.add(json.dumps(piece, ensure_ascii=True)[1:-1].encode())
        for key in ('id', 'uuid', 'conversation_id', 'title', 'name'):
            text = native.get(key)
            if isinstance(text, str) and len(text) >= 20:
                canaries.add(text.encode())
                canaries.add(json.dumps(text, ensure_ascii=True)[1:-1].encode())
    tracked = subprocess.check_output(['git', 'diff', '--name-only', args.base, '--'], cwd=args.repo).decode().splitlines()
    untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'], cwd=args.repo).decode().splitlines()
    files = sorted(set(tracked + untracked))
    hits, hashes, count = [], {}, 0
    for name in files:
        path = args.repo / name
        if not path.is_file() or path.resolve() == args.output.resolve():
            continue
        raw = path.read_bytes()
        hashes[name] = digest(raw)
        for part, payload in parts(name, raw):
            count += 1
            for canary in canaries:
                if canary in payload:
                    hits.append({'path': part, 'kind': 'private_source_canary', 'canary_sha256': digest(canary)})
            if re.search(rb'sk-or-v1-[A-Za-z0-9]{20,}', payload):
                hits.append({'path': part, 'kind': 'openrouter_credential_pattern'})
    result = {'schema': 'loom.thread7_public_projection_check/1',
              'source_families': len(source_files), 'historical_families': len(historical_files), 'private_canaries': len(canaries),
              'base_commit': args.base, 'files': hashes, 'decoded_parts_checked': count,
              'hits': hits, 'passed': not hits,
              'includes_metadata_errors_and_archive_payloads': True,
              'limitation': 'Finite exact-source canaries and key pattern; aggregate/code review also required. This is not a general PII classifier.',
              'new_calls': 0, 'new_cost_usd': '0'}
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps({key: result[key] for key in ('source_families', 'private_canaries', 'decoded_parts_checked', 'passed')}))
    if hits:
        print(json.dumps({'hit_count': len(hits), 'paths': sorted({row['path'] for row in hits})}))
        raise SystemExit(1)


if __name__ == '__main__':
    main()
