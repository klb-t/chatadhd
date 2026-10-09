"""Seal a private continuation without rewriting any preceding archive.

This helper performs no provider requests and never reads credentials. Inputs
are explicitly supplied private directories/files. Do not point it at a home,
repository root, or credential directory. Source archives are copied verbatim.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def seal(output, roots, bindings):
    output = Path(output)
    if output.exists():
        raise ValueError('new_checkpoint_filename_required')
    payloads = {}
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for alias, source in roots.items():
            if not alias.isidentifier():
                raise ValueError('invalid_root_alias')
            source = Path(source).resolve(strict=True)
            paths = [source] if source.is_file() else sorted(source.rglob('*'))
            for path in paths:
                if path.is_symlink():
                    raise ValueError('symlink_not_allowed')
                if not path.is_file():
                    continue
                if path.suffix in ('.pyc',) or '__pycache__' in path.parts:
                    continue
                name = alias + '/' + (path.name if source.is_file() else path.relative_to(source).as_posix())
                if name in payloads:
                    raise ValueError('duplicate_archive_path')
                raw = path.read_bytes()
                payloads[name] = {'sha256': sha(raw), 'bytes': len(raw)}
                archive.writestr(name, raw)
        manifest = {'schema': 'loom.thread7_continuation_checkpoint/1',
                    'bindings': bindings, 'files': payloads,
                    'campaign_credential_added': False,
                    'raw_source_secret_scan': 'not_performed_immutable_sources_retained',
                    'new_paid_calls': 0,
                    'new_paid_cost_usd': '0', 'provider_usage_usd': None,
                    'provider_reservations_usd': None}
        archive.writestr('RESUME_MANIFEST.json', json.dumps(manifest, sort_keys=True, indent=2) + '\n')
    output.chmod(0o600)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('checkpoint_crc_failed')
        for name, record in payloads.items():
            raw = archive.read(name)
            if sha(raw) != record['sha256'] or len(raw) != record['bytes']:
                raise ValueError('checkpoint_payload_failed')
    return {'schema': 'loom.thread7_checkpoint_receipt/1', 'sha256': sha(output.read_bytes()),
            'bytes': output.stat().st_size, 'payloads_verified': len(payloads),
            'manifest_sha256': sha(json.dumps(manifest, sort_keys=True, indent=2).encode() + b'\n'),
            'new_paid_calls': 0, 'new_paid_cost_usd': '0'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text())
    print(json.dumps(seal(plan['output'], plan['roots'], plan['bindings']), sort_keys=True))
