"""Lossless deterministic archive and no-overwrite restore for frozen cache data."""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import platform
import tempfile
import zipfile
import zlib

DOC_REL = Path('docs/research/agentic_graph_cache_v1')
SOURCE_FREEZE_SHA256 = '43bbd8c930c688f527ef5a798fe1b6bb5144b0cde0496d7e49609b2f3c641bf1'
RAW_MEMBERS = tuple(sorted(str(DOC_REL / name) for name in (
    'chain_first/fixtures.json', 'first_series/fixtures.json', 'chain_first/timed_first.jsonl',
    'first_series/timed_first.json', 'chain_first/retention_fork_fixture.json',
    'chain_first/memory_first.jsonl', 'first_series/memory_first.json')))
ARCHIVE_NAME = 'raw_measurements_v1.zip'
INVENTORY_NAME = 'RAW_ARCHIVE_INVENTORY.json'
RECEIPT_NAME = 'RAW_ARCHIVE_RESTORE_VERIFICATION.json'
STAGE_NAME = 'STAGING_MANIFEST.json'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode() + b'\n'


def write_new(path, value):
    with path.open('xb') as output:
        output.write(canonical(value))


def safe_member(path):
    if (not isinstance(path, str) or not path or '\\' in path or
            PurePosixPath(path).is_absolute() or '..' in PurePosixPath(path).parts or
            str(PurePosixPath(path)) != path or not path.startswith(str(DOC_REL) + '/')):
        raise ValueError('raw_archive_member_path_invalid')
    return path


def frozen_source(repo):
    path = repo / DOC_REL / 'SOURCE_PACKAGE_FREEZE.json'; raw = path.read_bytes()
    if sha(raw) != SOURCE_FREEZE_SHA256:
        raise ValueError('raw_archive_source_freeze_changed')
    frozen = json.loads(raw)
    for record in frozen['records']:
        actual = (repo / record['path']).read_bytes()
        if len(actual) != record['bytes'] or sha(actual) != record['sha256']:
            raise ValueError('raw_archive_original_frozen_member_changed:' + record['path'])
    return frozen


def zip_bytes(repo, records):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9, allowZip64=True) as archive:
        archive.comment = b''
        for record in sorted(records, key=lambda item: item['path'].encode('utf-8')):
            raw = (repo / safe_member(record['path'])).read_bytes()
            if len(raw) != record['bytes'] or sha(raw) != record['sha256']:
                raise ValueError('raw_archive_member_changed_before_build')
            info = zipfile.ZipInfo(record['path'], date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED; info.create_system = 3
            info.external_attr = 0o100644 << 16; info.internal_attr = 0
            info.extra = b''; info.comment = b''
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    return output.getvalue()


def archive_payloads(archive_path, inventory):
    if inventory.get('schema') != 'loom.graph_packet_cache_raw_archive/1':
        raise ValueError('raw_archive_inventory_schema_invalid')
    if inventory.get('source_package_freeze_sha256') != SOURCE_FREEZE_SHA256:
        raise ValueError('raw_archive_inventory_source_freeze_invalid')
    raw = archive_path.read_bytes()
    if sha(raw) != inventory['archive_sha256'] or len(raw) != inventory['archive_bytes']:
        raise ValueError('raw_archive_bytes_hash_or_size_mismatch')
    records = inventory['members']; names = [safe_member(record['path']) for record in records]
    if len(names) != len(set(names)) or names != sorted(names, key=lambda name: name.encode('utf-8')):
        raise ValueError('raw_archive_inventory_order_or_duplicate_invalid')
    payloads = []
    with zipfile.ZipFile(io.BytesIO(raw), 'r') as archive:
        if archive.namelist() != names:
            raise ValueError('raw_archive_member_inventory_mismatch')
        for record, info in zip(records, archive.infolist()):
            if (info.is_dir() or info.external_attr >> 16 != 0o100644 or
                    info.date_time != (1980, 1, 1, 0, 0, 0) or info.extra or info.comment):
                raise ValueError('raw_archive_member_metadata_invalid')
            with archive.open(info) as member:
                payload = member.read(record['bytes'] + 1)
            if len(payload) != record['bytes'] or sha(payload) != record['sha256']:
                raise ValueError('raw_archive_member_hash_or_size_mismatch')
            payloads.append((record, payload))
    return payloads


def restore(archive_path, inventory, destination, *, allow_existing_root=False):
    destination = Path(destination)
    if destination.is_symlink() or (destination.exists() and (not allow_existing_root or not destination.is_dir())):
        raise ValueError('raw_archive_restore_destination_exists')
    payloads = archive_payloads(Path(archive_path), inventory)
    # Inspect all paths before any write. Existing directories may be used only
    # with the explicit flag; existing files, including symlinks, never are.
    for record, _ in payloads:
        target = destination / record['path']
        if target.exists() or target.is_symlink():
            raise ValueError('raw_archive_restore_target_exists')
        current = target.parent
        while current != destination:
            if current.is_symlink() or (current.exists() and not current.is_dir()):
                raise ValueError('raw_archive_restore_parent_invalid')
            current = current.parent
    destination.mkdir(parents=True, exist_ok=allow_existing_root)
    for record, payload in payloads:
        target = destination / record['path']; target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as output:
            output.write(payload)
    return [{'path': record['path'], 'bytes': (destination / record['path']).stat().st_size,
             'sha256': sha((destination / record['path']).read_bytes())} for record, _ in payloads]


def pack(repo):
    repo = repo.resolve(); doc = repo / DOC_REL
    output_paths = [doc / name for name in (ARCHIVE_NAME, INVENTORY_NAME, RECEIPT_NAME, STAGE_NAME)]
    if any(path.exists() or path.is_symlink() for path in output_paths):
        raise ValueError('raw_archive_first_packaging_already_exists')
    frozen = frozen_source(repo)
    records_by_name = {record['path']: record for record in frozen['records']}
    records = [records_by_name[name] for name in RAW_MEMBERS]
    raw = zip_bytes(repo, records)
    if raw != zip_bytes(repo, records):
        raise ValueError('raw_archive_canonical_repeat_differs')
    archive_path = doc / ARCHIVE_NAME
    with archive_path.open('xb') as output: output.write(raw)
    inventory = {'schema': 'loom.graph_packet_cache_raw_archive/1',
                 'source_package_freeze_sha256': SOURCE_FREEZE_SHA256,
                 'archive_path': str(DOC_REL / ARCHIVE_NAME), 'archive_sha256': sha(raw), 'archive_bytes': len(raw),
                 'original_member_bytes': sum(record['bytes'] for record in records), 'member_count': len(records),
                 'members': records,
                 'canonical_zip': {'compression': 'deflate', 'level': 9, 'date_time': [1980, 1, 1, 0, 0, 0],
                                   'regular_file_mode': '100644', 'member_order': 'UTF8 lexical', 'extra_comment': 'empty',
                                   'python_version': platform.python_version(), 'zlib_version': zlib.ZLIB_VERSION},
                 'lossless_exact_original_bytes': True, 'originals_deleted_or_modified': False}
    write_new(doc / INVENTORY_NAME, inventory)
    checks = []
    with tempfile.TemporaryDirectory(prefix='chatadhd-cache-archive-') as root_name:
        temp = Path(root_name); destination = temp / 'fresh'
        restored = restore(archive_path, inventory, destination)
        if restored != records: raise ValueError('raw_archive_fresh_restore_differs')
        checks.append({'case': 'fresh_exact_member_restore', 'passed': True, 'members': len(restored)})
        before = [(target, target.read_bytes()) for target in destination.rglob('*') if target.is_file()]
        for allow in (False, True):
            try: restore(archive_path, inventory, destination, allow_existing_root=allow)
            except ValueError: pass
            else: raise ValueError('raw_archive_existing_destination_overwritten')
            if any(target.read_bytes() != value for target, value in before):
                raise ValueError('raw_archive_existing_destination_changed')
            checks.append({'case': 'existing_files_reject_before_write', 'allow_existing_root': allow, 'passed': True})
        partial = temp / 'partial'; existing = partial / records[-1]['path']
        existing.parent.mkdir(parents=True); existing.write_bytes(b'pre-existing sentinel')
        try: restore(archive_path, inventory, partial, allow_existing_root=True)
        except ValueError: pass
        else: raise ValueError('raw_archive_partial_existing_target_overwritten')
        if existing.read_bytes() != b'pre-existing sentinel' or sum(path.is_file() for path in partial.rglob('*')) != 1:
            raise ValueError('raw_archive_partial_destination_changed')
        checks.append({'case': 'all_targets_preflight_before_any_write', 'passed': True})
        existing_root = temp / 'empty_existing_root'; existing_root.mkdir()
        if restore(archive_path, inventory, existing_root, allow_existing_root=True) != records:
            raise ValueError('raw_archive_existing_directory_restore_differs')
        checks.append({'case': 'explicit_existing_root_with_absent_targets_exact', 'passed': True})
        for variant in ('archive_bytes', 'member_hash', 'traversal_name'):
            bad_inventory = deepcopy(inventory); bad_archive = archive_path
            if variant == 'archive_bytes':
                bad_archive = temp / 'corrupted.zip'; bad_archive.write_bytes(raw[:-1] + bytes([raw[-1] ^ 1]))
            elif variant == 'member_hash': bad_inventory['members'][0]['sha256'] = '0' * 64
            else: bad_inventory['members'][0]['path'] = '../outside'
            unused = temp / ('reject-' + variant)
            try: restore(bad_archive, bad_inventory, unused)
            except ValueError: pass
            else: raise ValueError('raw_archive_corrupt_variant_accepted:' + variant)
            if unused.exists(): raise ValueError('raw_archive_rejected_variant_wrote_files')
            checks.append({'case': variant + '_rejects_before_destination_creation', 'passed': True})
    frozen_source(repo)
    receipt = {'schema': 'loom.graph_packet_cache_raw_archive_restore_verification/1',
               'source_package_freeze_sha256': SOURCE_FREEZE_SHA256, 'original_frozen_files_unchanged': frozen['record_count'],
               'archive_sha256': sha(raw), 'archive_bytes': len(raw), 'member_count': len(records),
               'inventory_sha256': sha((doc / INVENTORY_NAME).read_bytes()),
               'canonical_second_build_exact_bytes': True, 'verified_restored_members': restored,
               'checks': checks, 'checks_passed': len(checks), 'original_files_deleted': 0,
               'network_calls': 0, 'benchmark_rerun': False, 'semantic_quality_measured': False}
    write_new(doc / RECEIPT_NAME, receipt)
    retained = [record['path'] for record in frozen['records'] if record['path'] not in RAW_MEMBERS]
    additions = [str(DOC_REL / name) for name in ('SOURCE_PACKAGE_FREEZE.json', ARCHIVE_NAME, INVENTORY_NAME,
                  RECEIPT_NAME, STAGE_NAME, 'PACKAGING.md')]
    additions.append(str(Path(__file__).resolve().relative_to(repo)))
    stage_paths = sorted(set(retained + additions)); manifest_path = str(DOC_REL / STAGE_NAME)
    stage_records = [{'path': name, 'bytes': (repo / name).stat().st_size, 'sha256': sha((repo / name).read_bytes())}
                     for name in stage_paths if name != manifest_path]
    write_new(doc / STAGE_NAME, {'schema': 'loom.graph_packet_cache_staging_manifest/1',
              'source_package_freeze_sha256': SOURCE_FREEZE_SHA256, 'archive_sha256': sha(raw),
              'stage_paths': stage_paths, 'stage_path_count': len(stage_paths), 'stage_file_records': stage_records,
              'self_record_excluded_to_avoid_recursive_digest': manifest_path,
              'excluded_duplicate_raw_paths': list(RAW_MEMBERS), 'originals_retained_workingtree': True,
              'restoration': 'package_archive restore --archive ZIP --inventory JSON --destination FRESH_ROOT; explicit --allow-existing-root permits directories but never overwrites files'})
    return {'archive_bytes': len(raw), 'original_member_bytes': inventory['original_member_bytes'],
            'archive_sha256': sha(raw), 'stage_paths': stage_paths, 'stage_path_count': len(stage_paths),
            'staging_manifest_sha256': sha((doc / STAGE_NAME).read_bytes()), 'checks_passed': len(checks)}


def main(argv=None):
    parser = argparse.ArgumentParser(); sub = parser.add_subparsers(dest='action', required=True)
    create = sub.add_parser('pack'); create.add_argument('--repo-root', type=Path, default=Path.cwd())
    extract = sub.add_parser('restore'); extract.add_argument('--archive', type=Path, required=True)
    extract.add_argument('--inventory', type=Path, required=True); extract.add_argument('--destination', type=Path, required=True)
    extract.add_argument('--allow-existing-root', action='store_true'); args = parser.parse_args(argv)
    if args.action == 'pack': print(json.dumps(pack(args.repo_root), indent=2))
    else:
        print(json.dumps({'restored_members': restore(args.archive, json.loads(args.inventory.read_bytes()), args.destination,
                                                      allow_existing_root=args.allow_existing_root)}, indent=2))


if __name__ == '__main__': main()
