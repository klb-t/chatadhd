#!/usr/bin/env python3
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path('/workspace/scratch/943af489e102/chatadhd')
OUT = pathlib.Path('/dev/shm/chatadhd-thread5/extension-migration-race')
BUILD = pathlib.Path('/dev/shm/chatadhd-thread5/baseline-build')

def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

receipt = {'kind': 'focused derivative: current patch objects before historical core archive; not clean-source integration',
           'root_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
           'files': {}, 'commands': []}
for rel in ['loom/src/db/annotations.cpp', 'loom/tests/test_db_annotations.cpp', 'loom/tests/main.cpp',
            'loom/include/loom/db.h', 'loom/include/loom/sqlite.h']:
    receipt['files'][rel] = sha(ROOT / rel)
receipt['core_before'] = sha(BUILD / 'libloom_core.a')
old = OUT / 'annotations-before.cpp'
old.write_bytes(subprocess.check_output(['git', 'show', 'HEAD:loom/src/db/annotations.cpp'], cwd=ROOT))
receipt['before_source_sha256'] = sha(old)
flags = ['-std=c++20', '-O0', '-DNDEBUG', '-DJSON_USE_IMPLICIT_CONVERSIONS=1', '-DLOOM_VENDORED_SQLITE=1',
         '-Wall', '-Wextra', '-Wpedantic', '-Wshadow', '-Wnon-virtual-dtor', '-Wold-style-cast',
         '-Wcast-align', '-Woverloaded-virtual', '-Wnull-dereference', '-Wimplicit-fallthrough',
         '-Wno-unused-parameter', '-Werror', '-I' + str(ROOT / 'loom/include'), '-I' + str(ROOT / 'loom/src'),
         '-I' + str(ROOT / 'loom/tests')]
for rel in ['loom/third_party/doctest', 'loom/third_party/nlohmann', 'loom/third_party/sqlite']:
    flags += ['-isystem', str(ROOT / rel)]

def command(args, label, expected=0):
    result = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (label + '.log')).write_bytes(result.stdout)
    output = result.stdout.decode('utf-8', 'backslashreplace')
    receipt['commands'].append({'label': label, 'argv': args, 'exit_code': result.returncode})
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(label + ': exit ' + str(result.returncode), flush=True)
    if result.returncode != expected:
        print(output[-6000:], flush=True)
        raise SystemExit('unexpected result for ' + label)
    return output

for src, name in [(ROOT / 'loom/tests/main.cpp', 'main'),
                  (ROOT / 'loom/tests/test_db_annotations.cpp', 'test'),
                  (old, 'annotations-before'),
                  (ROOT / 'loom/src/db/annotations.cpp', 'annotations-after')]:
    command(['/usr/bin/c++', *flags, '-c', str(src), '-o', str(OUT / (name + '.o'))], 'compile-' + name)

symbols = subprocess.check_output(['nm', str(OUT / 'annotations-after.o')], text=True)
symbol = next(line.split()[-1] for line in symbols.splitlines() if 'migrate_message_extensions' in line and ' T ' in line)
receipt['trace_symbol'] = symbol
for mode, annotation in [('before', 'annotations-before'), ('after', 'annotations-after')]:
    binary = OUT / ('annotations-' + mode)
    link = command(['/usr/bin/c++', '-O0', '-DNDEBUG', str(OUT / 'main.o'), str(OUT / 'test.o'),
                    str(OUT / (annotation + '.o')), str(BUILD / 'libloom_core.a'),
                    str(BUILD / 'libloom_sqlite3_amalgamation.a'), '-ldl', '-lm', str(BUILD / 'libloom_miniz.a'),
                    '/usr/lib/x86_64-linux-gnu/libssl.so', '/usr/lib/x86_64-linux-gnu/libcrypto.so',
                    '-Wl,--trace-symbol=' + symbol, '-o', str(binary)], 'link-' + mode)
    assert 'libloom_core.a(annotations.cpp.o)' not in link
    receipt[mode + '_archive_annotations_member_not_pulled'] = True
    filters = ['--test-case=extension migration rechecks concurrent upgrades under its write lock'] if mode == 'before' else ['--test-suite=db.annotations']
    output = command([str(binary), *filters, '--no-intro=true', '--success=true'], 'test-' + mode, 1 if mode == 'before' else 0)
    receipt[mode + '_summary'] = '\n'.join(line for line in output.splitlines() if '[doctest]' in line)
receipt['core_after'] = sha(BUILD / 'libloom_core.a')
assert receipt['core_before'] == receipt['core_after']
for rel, expected in receipt['files'].items():
    assert sha(ROOT / rel) == expected, rel
receipt['all_source_and_archive_hashes_stable'] = True
(OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(receipt['before_summary'], flush=True)
print(receipt['after_summary'], flush=True)
