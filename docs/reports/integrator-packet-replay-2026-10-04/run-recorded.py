#!/usr/bin/env python3
"""Exact-source offline static overlay reproduction; scratch output only."""
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
GIT_REPO = Path('/workspace/scratch/2fbae43b23fa/chatadhd')
W4_REPO = Path('/workspace/scratch/b189e486f506/chatadhd')
LOOM = W4_REPO / 'loom'
W2 = '34cc920dd3cdb0c0fca0a514569b19111429583f'
W4 = '3caa6b4dcfb6412294bf816c1105bcf99afacdbc'
manifest = {'w2_commit': W2, 'w4_commit': W4,
            'isolation': 'W2 policy + W4 packet C API overlay, W4 static core; explicit Config preset',
            'paid_calls': 0, 'network_calls': 0, 'commands': [], 'sources': {}, 'artifacts': {}}


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


with (ROOT / 'build-and-run.log').open('w') as log:
    def run(command, **kwargs):
        manifest['commands'].append(command)
        log.write(json.dumps(command) + '\n')
        log.flush()
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, **kwargs)

    sources = [
        (W2, 'loom/include/loom/usage_policy.h', 'include/loom/usage_policy.h'),
        (W2, 'loom/src/policy/usage_policy.cpp', 'src/usage_policy.cpp'),
        (W2, 'loom/src/core/config_usage_policy.cpp', 'src/config_usage_policy.cpp'),
        (W4, 'loom/src/capi/capi_packet.cpp', 'src/capi_packet.cpp'),
        (W4, 'loom/src/packet/packet.h', 'src/packet/packet.h'),
    ]
    for commit, source, destination in sources:
        destination = ROOT / destination
        destination.parent.mkdir(parents=True, exist_ok=True)
        command = ['git', 'show', f'{commit}:{source}']
        data = subprocess.check_output(command, cwd=GIT_REPO)
        destination.write_bytes(data)
        manifest['commands'].append(command)
        manifest['sources'][str(destination.relative_to(ROOT))] = {
            'git_source': f'{commit}:{source}', 'sha256': digest(destination)}
    manifest['sources']['harness.cc'] = {'sha256': digest(ROOT / 'harness.cc')}
    manifest['sources']['run.py'] = {'sha256': digest(ROOT / 'run.py')}
    includes = ['-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src'),
                '-I' + str(LOOM / 'include'), '-I' + str(LOOM / 'src'),
                '-I' + str(LOOM / 'src/capi'),
                '-isystem', str(LOOM / 'third_party/nlohmann'),
                '-isystem', str(LOOM / 'third_party/sqlite')]
    objects = []
    for name in ['usage_policy', 'config_usage_policy', 'capi_packet']:
        source = ROOT / 'src' / (name + '.cpp')
        target = ROOT / (name + '.o')
        objects.append(target)
        run(['g++', '-std=c++20', '-fPIC', '-fvisibility=hidden', *includes,
             '-c', str(source), '-o', str(target)])
    libraries = [LOOM / 'build/dev' / name for name in
                 ['libloom_core.a', 'libloom_sqlite3_amalgamation.a', 'libloom_miniz.a']]
    for artifact in libraries:
        manifest['artifacts'][str(artifact)] = {'sha256': digest(artifact), 'bytes': artifact.stat().st_size}
    executable = ROOT / 'reproduce'
    run(['g++', '-std=c++20', *includes, str(ROOT / 'harness.cc'),
         *map(str, objects), *map(str, libraries), '-lssl', '-lcrypto',
         '-lpthread', '-ldl', '-o', str(executable)])
    manifest['artifacts']['reproduce'] = {'sha256': digest(executable)}
    with tempfile.TemporaryDirectory(prefix='synthetic-data-', dir=ROOT) as data:
        command = [str(executable), data]
        manifest['commands'].append(command)
        result = subprocess.run(command, text=True, capture_output=True)
        (ROOT / 'result.json').write_text(result.stdout)
        log.write(result.stdout + result.stderr)
        result.check_returncode()
    manifest['result'] = 'reproduced: two executed operations, one calls sample of 1'
    manifest['result_sha256'] = digest(ROOT / 'result.json')
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(manifest['result'])
