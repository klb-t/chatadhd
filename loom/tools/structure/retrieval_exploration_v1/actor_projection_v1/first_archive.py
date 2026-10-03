"""Exact-byte archive for first and corrected actor-projection measurements; no API."""
import hashlib
import json
from pathlib import Path
import zipfile
HERE = Path(__file__).resolve().parent
FILES = ('first_projection.json', 'second_projection.json', 'first_controls.json', 'second_controls.json', 'prepared_control_manifest.json', 'PARITY_RECOUNT.json')

def names():
    manifest = HERE / 'prepared_control_manifest.json'
    if manifest.exists():
        expected = json.loads(manifest.read_text())
        return FILES + tuple('prepared_control_sources/' + row['case_id'] + '.json' for row in expected['rows'])
    sidecar = json.loads((HERE / 'FIRST_ARCHIVE.json').read_text())
    return tuple(entry['path'] for entry in sidecar['entries'])


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def pack():
    entries = []
    destination = HERE / 'first_evidence.zip'
    with destination.open('xb') as out, zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in names():
            raw = (HERE / name).read_bytes()
            info = zipfile.ZipInfo(name, (2026, 9, 30, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, raw)
            entries.append({'path': name, 'bytes': len(raw), 'sha256': digest(raw)})
        z.writestr('INVENTORY.json', json.dumps({'entries': entries}, sort_keys=True, indent=2) + '\n')
    result = {'schema': 'loom.research.actor_projection_first_and_corrected_archive/1', 'entries': entries,
        'archive_sha256': digest(destination.read_bytes()), 'archive_bytes': destination.stat().st_size,
        'raw_bytes': sum(v['bytes'] for v in entries), 'credentials_included': False,
        'validation_included': False, 'weights_included': False}
    with (HERE / 'FIRST_ARCHIVE.json').open('x') as out:
        out.write(json.dumps(result, sort_keys=True, indent=2) + '\n')


def verify(restore=False):
    manifest = json.loads((HERE / 'FIRST_ARCHIVE.json').read_text())
    if digest((HERE / 'first_evidence.zip').read_bytes()) != manifest['archive_sha256']:
        raise ValueError('archive_hash_drift')
    values = []
    with zipfile.ZipFile(HERE / 'first_evidence.zip') as z:
        entries = json.loads(z.read('INVENTORY.json'))['entries']
        if entries != manifest['entries'] or {v['path'] for v in entries} != set(names()):
            raise ValueError('archive_inventory_drift')
        if len(z.namelist()) != len(names()) + 1 or set(z.namelist()) != set(names()) | {'INVENTORY.json'}:
            raise ValueError('archive_member_drift')
        for entry in entries:
            name = entry['path']
            if name not in FILES and (not name.startswith('prepared_control_sources/') or '/' in name.removeprefix('prepared_control_sources/') or '..' in name or not name.endswith('.json')):
                raise ValueError('unsafe_control_archive_path')
            raw = z.read(entry['path'])
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
                with destination.open('xb') as out:
                    out.write(raw)
    return {'verified_payloads': len(values), 'raw_bytes': sum(len(raw) for name, raw in values), 'restored': restore}


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', choices=('pack', 'verify', 'restore'))
    args = p.parse_args()
    if args.stage == 'pack':
        pack()
    else:
        print(json.dumps(verify(args.stage == 'restore')))
