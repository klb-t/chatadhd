#!/usr/bin/env python3
"""Run the exact thread-5 admission tests with a pinned external W2 overlay.

Requires the parent's explicit AFTER-core-ready signal before invocation.
No W2 source is copied into the thread-5 repository; no provider is contacted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

HARNESS = Path(__file__).resolve().parent
W2_COMMIT = '34cc920dd3cdb0c0fca0a514569b19111429583f'


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core-build-dir', type=Path, required=True)
    parser.add_argument('--acknowledge-after-core-ready', action='store_true', required=True)
    args = parser.parse_args()
    build = args.core_build_dir.resolve()
    core = build / 'libloom_core.a'
    if not core.is_file():
        parser.error('final libloom_core.a is not ready')
    prepared = json.loads((HARNESS / 'source-receipt.json').read_text())
    root = Path(prepared['repository']).resolve()
    if prepared['w2_commit'] != W2_COMMIT:
        parser.error('unexpected W2 source pin')
    for entry in prepared['files']:
        if sha256(Path(entry['local_path'])) != entry['sha256']:
            parser.error('prepared W2 source hash changed')
    if sha256(root / 'loom/include/loom/config.h') != prepared['config_header_sha256']:
        parser.error('Config header changed since external overlay preparation')
    sources = [root / 'loom/tests/main.cpp', root / 'loom/tests/test_import_usage.cpp',
               root / 'loom/src/import/import_usage.cpp', HARNESS / 'overlay/usage_policy.cpp',
               HARNESS / 'overlay/config_usage_policy.cpp', HARNESS / 'overlay/config.cpp',
               HARNESS / 'test_receipt_evidence.cpp']
    record = {'w2_commit': W2_COMMIT, 'root_commit_at_run': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'core_build_dir': str(build), 'core_sha256': sha256(core),
        'provider_calls': 0, 'sources': [{'path': str(p), 'sha256': sha256(p)} for p in sources],
        'headers': [{'path': str(root / p), 'sha256': sha256(root / p)} for p in [
            'loom/include/loom/config.h', 'loom/include/loom/importer.h',
            'loom/include/loom/runtime.h', 'loom/src/import/import_usage.h', 'loom/tests/test_helpers.h']],
        'commands': []}
    output = HARNESS / 'output'
    output.mkdir(exist_ok=True)
    log = output / 'run.log'
    receipt = output / 'run-receipt.json'

    def run(command: list[str], environment: dict[str, str] | None = None) -> int:
        started = time.monotonic()
        with log.open('a') as handle:
            handle.write('$ ' + shlex.join(command) + '\n')
            handle.flush()
            result = subprocess.run(command, cwd=root, env=environment, stdout=handle, stderr=subprocess.STDOUT)
        record['commands'].append({'argv': command, 'exit_code': result.returncode,
                                   'elapsed_seconds': time.monotonic() - started})
        receipt.write_text(json.dumps(record, indent=2) + '\n')
        return result.returncode

    log.write_text('External pinned W2 + exact thread-5 import admission integration\n')
    compiler_cache = next(line.split('=', 1)[1] for line in (build / 'CMakeCache.txt').read_text().splitlines()
                          if line.startswith('CMAKE_CXX_COMPILER:FILEPATH='))
    compiler = os.environ.get('CXX', compiler_cache)
    common = [compiler, '-std=c++20', '-O0', '-g0', '-Wall', '-Wextra', '-Wpedantic',
              '-Wshadow', '-Werror', '-Wno-unused-parameter', '-pthread',
              '-DNDEBUG',
              '-DLOOM_VENDORED_SQLITE=1', '-DLOOM_HAVE_OPENSSL=1',
              '-DCPPHTTPLIB_OPENSSL_SUPPORT=1', '-DJSON_USE_IMPLICIT_CONVERSIONS=1',
              '-DLOOM_VERSION_STRING="0.1.0"', '-I' + str(HARNESS / 'overlay/include'),
              '-I' + str(root / 'loom/include'), '-I' + str(root / 'loom/src'),
              '-I' + str(root / 'loom/tests'), '-isystem', str(root / 'loom/third_party/nlohmann'),
              '-isystem', str(root / 'loom/third_party/doctest')]
    objects = []
    for index, source in enumerate(sources):
        obj = output / (str(index) + '-' + source.stem + '.o')
        result = run(common + ['-c', str(source), '-o', str(obj)])
        if result:
            return result
        objects.append(str(obj))
    for entry in record['sources']:
        if sha256(Path(entry['path'])) != entry['sha256']:
            raise RuntimeError('source changed during compilation; refuse mixed evidence')
    # Use the actual project's Ninja library order, without --whole-archive.
    # Overlay objects therefore satisfy Config/ImportUsageSession before libcore.
    ninja_lines = (build / 'build.ninja').read_text().splitlines()
    start = next(i for i, line in enumerate(ninja_lines) if line.startswith('build loom_tests:'))
    raw = next(line.split('=', 1)[1].strip() for line in ninja_lines[start + 1:]
               if line.startswith('  LINK_LIBRARIES ='))
    libraries = [token if token.startswith('-') or Path(token).is_absolute() else str(build / token)
                 for token in shlex.split(raw)]
    link_map = output / 'link.map'
    binary = output / 'w2-import-usage-tests'
    result = run([compiler, '-pthread', *objects, '-Wl,-Map,' + str(link_map), *libraries, '-o', str(binary)])
    if result:
        return result
    map_text = link_map.read_text()
    for member in ['config.cpp.o', 'import_usage.cpp.o']:
        if 'libloom_core.a(' + member + ')' in map_text:
            raise RuntimeError('base archive unexpectedly supplied overlaid member ' + member)
    record['excluded_base_archive_members'] = ['config.cpp.o', 'import_usage.cpp.o']
    record['binary_sha256'] = sha256(binary)
    record['link_map_sha256'] = sha256(link_map)
    env = dict(os.environ)
    env['W2_INTEGRATION_EVIDENCE'] = str(output / 'confirmation-evidence.json')
    result = run([str(binary), '--test-suite=import.usage*', '--no-intro=true', '--success=true'], env)
    if sha256(core) != record['core_sha256']:
        raise RuntimeError('core archive changed during integration run; refuse mixed evidence')
    record['result'] = 'passed' if result == 0 else 'failed'
    evidence_path = output / 'confirmation-evidence.json'
    if evidence_path.is_file():
        record['confirmation_evidence_sha256'] = sha256(evidence_path)
    receipt.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'result': record['result'], 'log': str(log), 'receipt': str(receipt),
                      'confirmation_evidence': str(evidence_path)}))
    return result


if __name__ == '__main__':
    raise SystemExit(main())
