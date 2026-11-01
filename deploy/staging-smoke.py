#!/usr/bin/env python3
"""Test an already built own runtime package after relocation, without checkout.

No build, install, dependency download, host change or deployment. Requires
Linux setpriv/Landlock and a precompiled test-only URL network fixture.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--package', required=True)
    parser.add_argument('--work-dir', required=True)
    parser.add_argument('--network-fixture', required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    package, stage, fixture = (Path(p).resolve() for p in
                               (args.package, args.work_dir, args.network_fixture))
    if any(p == root or not p.is_relative_to(root) for p in (package, stage, fixture)):
        parser.error('all paths must be inside the current own checkout')
    if stage.is_relative_to(package):
        parser.error('test work directory must be outside the immutable package')
    if stage.exists():
        parser.error('use a fresh work directory; previous evidence must be preserved')
    for path in ('bin/loom-server', 'lib/libloom.so', 'share/loom/docx_basic/runtime.json', 'web/index.html'):
        if not (package / path).is_file():
            parser.error('missing package artifact: ' + path)
    allowed = {'bin', 'lib', 'share', 'web', 'VERSION', 'BUILD_INFO', 'MANIFEST.json'}
    if set(p.name for p in package.iterdir()) - allowed:
        parser.error('unexpected package entries; data/config/test support must stay outside')
    for path in package.rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(package):
            parser.error('package symlink escapes immutable root: ' + str(path))
    manifest = json.loads((package / 'MANIFEST.json').read_text())
    expected = {name: entry['sha256'] for name, entry in manifest['files'].items()}
    actual = hashes(package)
    del actual['MANIFEST.json']
    if actual != expected:
        parser.error('package contents do not match MANIFEST.json')
    print('package source_head=' + manifest['source_head'] + ' build_type=' + manifest['build_type'], flush=True)
    stage.mkdir(parents=True)
    copied = stage / 'package-original-location'
    shutil.copytree(package, copied, symlinks=True)
    relocated = stage / 'package-relocated'
    copied.rename(relocated)
    support = ['server_flow_harness.py', 'test_server_guest_credentials.py', 'test_server_guests.py',
               'test_server_security.py', 'test_server_pdf_flow.py', 'test_server_docx_flow.py',
               'test_docx_package.py', 'test_server_url_share_positive.py']
    files = ['loom/server/tests/' + name for name in support] + [
        'loom/tools/docx_basic/tests/fixtures.py',
        'loom/tests/fixtures/documents/unicode-compressed.pdf',
        'loom/tests/fixtures/documents/mixed-text-blank.pdf',
        'loom/tests/fixtures/local_ocr/synthetic-pl.png', 'loom/tests/fixtures/local_ocr/expected.txt',
        'loom/tests/fixtures/url_share/conversation.json']
    for name in files:
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
    shutil.copyfile(root / 'deploy/tests/staging_smoke.py', stage / 'loom/server/tests/staging_smoke.py')
    shutil.copyfile(fixture, stage / 'network_fixture.so')
    work = stage / 'work'
    (work / 'tmp').mkdir(parents=True)
    env = {k: os.environ[k] for k in ('PATH', 'LANG', 'LC_ALL') if k in os.environ}
    env.update(TMPDIR=str(work / 'tmp'), PYTHONDONTWRITEBYTECODE='1', OMP_THREAD_LIMIT='1',
        LOOM_PACKAGE_ROOT=str(relocated), LOOM_SERVER_BIN=str(relocated / 'bin/loom-server'),
        LOOM_URL_NETWORK_FIXTURE=str(stage / 'network_fixture.so'),
        LOOM_FLOW_EVIDENCE_DIR=str(work / 'evidence'),
        LOOM_DENIED_SOURCE_PATHS=json.dumps([str(root / 'loom/tools/docx_basic/reader.py'),
                                           str(package / 'bin/loom-server'), str(root / 'loom/src/import/url_share/worker.py')]))
    command = ['setpriv', '--no-new-privs', '--landlock-access', 'fs',
        '--landlock-rule', 'path-beneath:execute,read-file,read-dir:/usr',
        '--landlock-rule', 'path-beneath:read-file,read-dir:/proc',
        '--landlock-rule', 'path-beneath:read-file,write-file:/dev/null',
        '--landlock-rule', 'path-beneath:execute,read-file,read-dir:' + str(stage),
        '--landlock-rule', 'path-beneath:execute,write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,make-sym,refer,truncate:' + str(work),
        '/usr/bin/python3', '-B', str(stage / 'loom/server/tests/staging_smoke.py')]
    before = hashes(relocated)
    (stage / 'test-command.json').write_text(json.dumps({'command': command, 'cwd': str(work),
        'source_support': {p: hashlib.sha256((root / p).read_bytes()).hexdigest()
                           for p in files + ['deploy/tests/staging_smoke.py', 'deploy/staging-smoke.py']},
        'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'package_before': before}, indent=2) + '\n')
    result = subprocess.run(command, cwd=work, env=env)
    summary_path = work / 'smoke-result.json'
    summary = json.loads(summary_path.read_text()) if summary_path.is_file() else {}
    completed = (summary.get('run') == 8 and summary.get('success') is True
                 and all(summary.get(key) == 0 for key in ('failures', 'errors', 'skipped')))
    unchanged = before == hashes(relocated) == hashes(package)
    (stage / 'test-result.json').write_text(json.dumps({'exit_code': result.returncode,
        'immutable_package_unchanged': unchanged, 'all_eight_cases_completed': completed}, indent=2) + '\n')
    return result.returncode or (0 if unchanged and completed else 1)


if __name__ == '__main__':
    raise SystemExit(main())
