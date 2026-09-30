"""Pack/restore exact first measurement bytes; no model download or API."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile
HERE = Path(__file__).resolve().parent
FILES = ('first_scores.json', 'first_results.json', 'byte_budget_results.json',
         'first_embedding/vectors.npy', 'first_embedding/index.json', 'first_embedding/scores.json')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def pack():
    entries = []
    zip_path = HERE / 'first_evidence.zip'
    with zip_path.open('xb') as out:
        with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name in FILES:
                raw = (HERE / name).read_bytes()
                info = zipfile.ZipInfo(name, date_time=(2026, 9, 30, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, raw)
                entries.append({'path': name, 'bytes': len(raw), 'sha256': digest(raw)})
            archive.writestr('INVENTORY.json', json.dumps({'entries': entries}, ensure_ascii=False, sort_keys=True, indent=2) + '\n')
    sidecar = {'schema': 'loom.research.retrieval_exploration_first_archive/1', 'archive_sha256': digest(zip_path.read_bytes()),
               'archive_bytes': zip_path.stat().st_size, 'raw_bytes': sum(e['bytes'] for e in entries), 'entries': entries,
               'credentials_included': False, 'model_weights_included': False, 'validation_included': False}
    with (HERE / 'FIRST_ARCHIVE.json').open('x') as out:
        out.write(json.dumps(sidecar, sort_keys=True, indent=2) + '\n')


def verify_and_restore(restore=False):
    sidecar = json.loads((HERE / 'FIRST_ARCHIVE.json').read_text())
    raw_zip = (HERE / 'first_evidence.zip').read_bytes()
    if digest(raw_zip) != sidecar['archive_sha256']:
        raise ValueError('archive_byte_hash_drift')
    with zipfile.ZipFile(HERE / 'first_evidence.zip') as archive:
        entries = json.loads(archive.read('INVENTORY.json'))['entries']
        if entries != sidecar['entries'] or {e['path'] for e in entries} != set(FILES):
            raise ValueError('archive_inventory_drift')
        if len(archive.namelist()) != len(FILES) + 1 or set(archive.namelist()) != set(FILES) | {'INVENTORY.json'}:
            raise ValueError('archive_member_inventory_drift')
        values = []
        for entry in entries:
            name = entry['path']
            p = PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts:
                raise ValueError('unsafe_archive_path')
            raw = archive.read(name)
            if len(raw) != entry['bytes'] or digest(raw) != entry['sha256']:
                raise ValueError('first_measurement_byte_drift:' + name)
            values.append((name, raw))
        if restore:
            for name, raw in values:
                target = HERE / name
                if target.exists():
                    if target.read_bytes() != raw:
                        raise ValueError('refuse_overwriting_different_measurement:' + name)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as out:
                    out.write(raw)
    return {'verified_payloads': len(entries), 'raw_bytes': sum(e['bytes'] for e in entries), 'restored': restore}


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', choices=('pack', 'verify', 'restore'))
    args = p.parse_args()
    if args.stage == 'pack':
        pack()
    else:
        print(json.dumps(verify_and_restore(args.stage == 'restore')))
