#!/usr/bin/env python3
"""Replay local W3 registry / W4 native compatibility; no provider transport."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

PIN = '03c670caa6ee3d8ac2c478186548114f8e83927f'
ROOT = Path(__file__).resolve().parents[4]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'loom/build/dev')
    parser.add_argument('--evidence-dir', type=Path, required=True)
    parser.add_argument('--harness', type=Path, default=Path(__file__).with_name('method_registry_variants.verify.cc'))
    args = parser.parse_args()
    evidence = args.evidence_dir.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    library = build / 'libloom.so.0.1.0'
    entries = json.loads((build / 'compile_commands.json').read_text())
    entry = next(row for row in entries if row['file'].endswith('/src/context/context_engine.cpp'))
    commands, hashes = [], {}

    def run(command, name, cwd=ROOT):
        commands.append({'name': name, 'cwd': str(cwd), 'argv': command})
        (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (evidence / (name + '.txt')).open('w') as log:
            result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        (evidence / (name + '-result.json')).write_text(json.dumps({'exit_code': result.returncode}) + '\n')
        if result.returncode:
            raise RuntimeError(f'{name}: exit {result.returncode}; see preserved log')

    with tempfile.TemporaryDirectory(prefix='w4-pinned-registry-') as directory:
        overlay = Path(directory)
        (overlay / 'context').mkdir()
        for filename in ('method_registry.h', 'method_registry.cpp'):
            path = overlay / 'context' / filename
            path.write_bytes(subprocess.check_output(
                ['git', 'show', PIN + ':loom/src/context/' + filename], cwd=ROOT))
            hashes['w3:' + filename] = sha(path)
        compiler = entry.get('arguments') or shlex.split(entry['command'])
        for filename, source in (('registry', overlay / 'context/method_registry.cpp'),
                                 ('harness', args.harness.resolve())):
            command = compiler.copy()
            command[1:1] = ['-x', 'c++']
            command.insert(1, '-I' + str(overlay))
            command[command.index('-o') + 1] = str(overlay / (filename + '.o'))
            command[-1] = str(source)
            hashes[filename + '_source'] = sha(source)
            run(command, 'compile-' + filename, Path(entry['directory']))
        # Matches this proof's WERROR Debug/vendored-SQLite/OpenSSL build.
        archives = [build / name for name in ('libloom_core.a', 'libloom_sqlite3_amalgamation.a', 'libloom_miniz.a')]
        for archive in archives:
            hashes[archive.name] = sha(archive)
        executable = overlay / 'variants'
        run([compiler[0], '-Wl,--no-keep-memory', str(overlay / 'registry.o'),
             str(overlay / 'harness.o'), '-o', str(executable),
             *(str(path) for path in archives), '-ldl', '-lm', '-lssl', '-lcrypto', '-pthread'], 'link')
        hashes['executable'] = sha(executable)
        hashes['library'] = sha(library)
        run([str(executable), str(Path(__file__).with_name('method-graph-fixture.json')),
             str(evidence / 'producer')], 'producer')
        for name in ('observed-model-alias', 'nested-combination', 'nested-combination-alias'):
            run([sys.executable, str(Path(__file__).with_name('verify_method_graph_artifact.py')),
                 '--library', str(library), '--artifact', str(evidence / 'producer' / (name + '.json')),
                 '--evidence-dir', str(evidence / 'consumer' / name)], 'consumer-' + name)
    (evidence / 'manifest.json').write_text(json.dumps({
        'schema': 'loom.method_registry_variant_reproduction/1', 'passed': True,
        'registry_commit': PIN, 'provider_calls': 0, 'transport_execution_verified': False,
        'hashes': hashes}, indent=2) + '\n')
    print('3/3 real registry variants and independent native consumers passed; zero provider calls')


if __name__ == '__main__':
    main()
