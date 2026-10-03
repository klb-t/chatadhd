"""Exact-byte checkpoint of native source-only inputs and first outputs; no DB/key."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile
HERE = Path(__file__).resolve().parent
SINGLE_FILES = ('source_only_inputs.json', 'prepared_manifest.json', 'first_run_ledger.json',
                'first_run_ledger2.json', 'first_results3.json', 'SOURCE_BINDING_RECOUNT.json')
RUN_FILES = ('executed_config.json', 'process_metrics.json', 'stdout.bin', 'stderr.bin', 'native_snapshot.json')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def safe_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or str(path) != name:
        raise ValueError('unsafe_archive_path')
    if name in SINGLE_FILES:
        return True
    if len(path.parts) == 3 and path.parts[0] == 'prepared' and path.parts[1] in ('transport_user', 'literal_speaker_role'):
        return path.name.startswith('gpv1_dev_') and path.name.endswith('.json')
    return len(path.parts) == 3 and path.parts[0] in ('first_runs', 'first_runs2') and path.name in RUN_FILES


def pack():
    paths = [HERE / name for name in SINGLE_FILES]
    for prefix in ('prepared', 'first_runs', 'first_runs2'):
        paths += sorted(p for p in (HERE / prefix).rglob('*') if p.is_file())
    entries = []
    destination = HERE / 'first_evidence.zip'
    with destination.open('xb') as output, zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in paths:
            name = str(path.relative_to(HERE))
            if not safe_name(name):
                raise ValueError('unregistered_measurement_path:' + name)
            raw = path.read_bytes()
            info = zipfile.ZipInfo(name, (2026, 9, 30, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, raw)
            entries.append({'path': name, 'bytes': len(raw), 'sha256': digest(raw)})
        archive.writestr('INVENTORY.json', json.dumps({'entries': entries}, sort_keys=True, indent=2) + '\n')
    result = {'schema': 'loom.research.native_source_free_first_archive/1', 'entries': entries,
        'archive_sha256': digest(destination.read_bytes()), 'archive_bytes': destination.stat().st_size,
        'raw_bytes': sum(e['bytes'] for e in entries), 'credentials_included': False,
        'databases_included': False, 'validation_included': False, 'weights_included': False}
    with (HERE / 'FIRST_ARCHIVE.json').open('x') as output:
        output.write(json.dumps(result, sort_keys=True, indent=2) + '\n')


def verify(restore=False):
    manifest = json.loads((HERE / 'FIRST_ARCHIVE.json').read_text())
    if digest((HERE / 'first_evidence.zip').read_bytes()) != manifest['archive_sha256']:
        raise ValueError('archive_hash_drift')
    values = []
    with zipfile.ZipFile(HERE / 'first_evidence.zip') as archive:
        entries = json.loads(archive.read('INVENTORY.json'))['entries']
        if entries != manifest['entries']:
            raise ValueError('archive_inventory_drift')
        names = [e['path'] for e in entries]
        if len(set(names)) != len(names) or set(archive.namelist()) != set(names) | {'INVENTORY.json'} or len(archive.namelist()) != len(names) + 1:
            raise ValueError('archive_member_drift')
        for entry in entries:
            if not safe_name(entry['path']):
                raise ValueError('unregistered_measurement_path')
            raw = archive.read(entry['path'])
            if len(raw) != entry['bytes'] or digest(raw) != entry['sha256']:
                raise ValueError('first_bytes_drift:' + entry['path'])
            values.append((entry['path'], raw))
    if restore:
        for name, raw in values:
            destination = HERE / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists():
                if destination.read_bytes() != raw:
                    raise ValueError('refuse_overwriting_different_first_measurement')
            else:
                with destination.open('xb') as output:
                    output.write(raw)
    return {'verified_payloads': len(values), 'raw_bytes': sum(len(raw) for _, raw in values), 'restored': restore}


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('pack', 'verify', 'restore'))
    args = parser.parse_args()
    if args.stage == 'pack':
        pack()
    else:
        print(json.dumps(verify(args.stage == 'restore')))
